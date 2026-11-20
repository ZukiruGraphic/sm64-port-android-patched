#include <dirent.h>
#include <sys/stat.h>
#include <math.h>
#ifdef RAPI_GL

#include <stdint.h>
#include <stdbool.h>

#ifndef _LANGUAGE_C
# define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#ifdef __MINGW32__
# define FOR_WINDOWS 1
#else
# define FOR_WINDOWS 0
#endif

#if FOR_WINDOWS || defined(OSX_BUILD)
# define GLEW_STATIC
# include <GL/glew.h>
#endif

#define GL_GLEXT_PROTOTYPES 1

#ifdef WAPI_SDL2
# include <SDL2/SDL.h>
# ifdef USE_GLES
#  include <SDL2/SDL_opengles2.h>
# else
#  include <SDL2/SDL_opengl.h>
# endif
#elif defined(WAPI_SDL1)
# include <SDL/SDL.h>
# ifndef GLEW_STATIC
#  include <SDL/SDL_opengl.h>
# endif
#endif

#include "../platform.h"
#include "../configfile.h"
#include "gfx_cc.h"
#include "gfx_rendering_api.h"

#define TEX_CACHE_STEP 512

struct ShaderProgram {
    uint32_t shader_id;
    GLuint opengl_program_id;
    uint8_t num_inputs;
    bool used_textures[2];
    uint8_t num_floats;
    GLint attrib_locations[7];
    GLint uniform_locations[5];
    uint8_t attrib_sizes[7];
    uint8_t num_attribs;
    bool used_noise;
};

struct GLTexture {
    GLuint gltex;
    GLfloat size[2];
    bool filter;
};

static struct ShaderProgram shader_program_pool[64];
static uint8_t shader_program_pool_size;
static GLuint opengl_vbo;

static int tex_cache_size = 0;
static int num_textures = 0;
static struct GLTexture *tex_cache = NULL;

static struct ShaderProgram *opengl_prg = NULL;
static struct GLTexture *opengl_tex[2];
static int opengl_curtex = 0;

static uint32_t frame_count;

static bool gfx_opengl_z_is_from_0_to_1(void) {
    return false;
}

static void gfx_opengl_vertex_array_set_attribs(struct ShaderProgram *prg) {
    size_t num_floats = prg->num_floats;
    size_t pos = 0;

    for (int i = 0; i < prg->num_attribs; i++) {
        glEnableVertexAttribArray(prg->attrib_locations[i]);
        glVertexAttribPointer(prg->attrib_locations[i], prg->attrib_sizes[i], GL_FLOAT, GL_FALSE, num_floats * sizeof(float), (void *) (pos * sizeof(float)));
        pos += prg->attrib_sizes[i];
    }
}

static inline void gfx_opengl_set_shader_uniforms(struct ShaderProgram *prg) {
    if (prg->used_noise)
        glUniform1f(prg->uniform_locations[4], (float)frame_count);
}

static inline void gfx_opengl_set_texture_uniforms(struct ShaderProgram *prg, const int tile) {
    if (prg->used_textures[tile] && opengl_tex[tile]) {
        glUniform2f(prg->uniform_locations[tile*2 + 0], opengl_tex[tile]->size[0], opengl_tex[tile]->size[1]);
        glUniform1i(prg->uniform_locations[tile*2 + 1], opengl_tex[tile]->filter);
    }
}

static void gfx_opengl_unload_shader(struct ShaderProgram *old_prg) {
    if (old_prg != NULL) {
        for (int i = 0; i < old_prg->num_attribs; i++)
            glDisableVertexAttribArray(old_prg->attrib_locations[i]);
        if (old_prg == opengl_prg)
            opengl_prg = NULL;
    } else {
        opengl_prg = NULL;
    }
}

static void gfx_opengl_load_shader(struct ShaderProgram *new_prg) {
    opengl_prg = new_prg;
    glUseProgram(new_prg->opengl_program_id);
    gfx_opengl_vertex_array_set_attribs(new_prg);
    gfx_opengl_set_shader_uniforms(new_prg);
    gfx_opengl_set_texture_uniforms(new_prg, 0);
    gfx_opengl_set_texture_uniforms(new_prg, 1);
}

static void append_str(char *buf, size_t *len, const char *str) {
    while (*str != '\0') buf[(*len)++] = *str++;
}

static void append_line(char *buf, size_t *len, const char *str) {
    while (*str != '\0') buf[(*len)++] = *str++;
    buf[(*len)++] = '\n';
}

static const char *shader_item_to_str(uint32_t item, bool with_alpha, bool only_alpha, bool inputs_have_alpha, bool hint_single_element) {
    if (!only_alpha) {
        switch (item) {
            case SHADER_0:
                return with_alpha ? "vec4(0.0, 0.0, 0.0, 0.0)" : "vec3(0.0, 0.0, 0.0)";
            case SHADER_INPUT_1:
                return with_alpha || !inputs_have_alpha ? "vInput1" : "vInput1.rgb";
            case SHADER_INPUT_2:
                return with_alpha || !inputs_have_alpha ? "vInput2" : "vInput2.rgb";
            case SHADER_INPUT_3:
                return with_alpha || !inputs_have_alpha ? "vInput3" : "vInput3.rgb";
            case SHADER_INPUT_4:
                return with_alpha || !inputs_have_alpha ? "vInput4" : "vInput4.rgb";
            case SHADER_TEXEL0:
                return with_alpha ? "texVal0" : "texVal0.rgb";
            case SHADER_TEXEL0A:
                return hint_single_element ? "texVal0.a" :
                    (with_alpha ? "vec4(texelVal0.a, texelVal0.a, texelVal0.a, texelVal0.a)" : "vec3(texelVal0.a, texelVal0.a, texelVal0.a)");
            case SHADER_TEXEL1:
                return with_alpha ? "texVal1" : "texVal1.rgb";
        }
    } else {
        switch (item) {
            case SHADER_0:
                return "0.0";
            case SHADER_INPUT_1:
                return "vInput1.a";
            case SHADER_INPUT_2:
                return "vInput2.a";
            case SHADER_INPUT_3:
                return "vInput3.a";
            case SHADER_INPUT_4:
                return "vInput4.a";
            case SHADER_TEXEL0:
                return "texVal0.a";
            case SHADER_TEXEL0A:
                return "texVal0.a";
            case SHADER_TEXEL1:
                return "texVal1.a";
        }
    }
}

static void append_formula(char *buf, size_t *len, uint8_t c[2][4], bool do_single, bool do_multiply, bool do_mix, bool with_alpha, bool only_alpha, bool opt_alpha) {
    if (do_single) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    } else if (do_multiply) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
    } else if (do_mix) {
        append_str(buf, len, "mix(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, ")");
    } else {
        append_str(buf, len, "(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " - ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ") * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, " + ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    }
}

static struct ShaderProgram *gfx_opengl_create_and_load_new_shader(uint32_t shader_id) {
    uint8_t c[2][4];
    for (int i = 0; i < 4; i++) {
        c[0][i] = (shader_id >> (i * 3)) & 7;
        c[1][i] = (shader_id >> (12 + i * 3)) & 7;
    }
    bool opt_alpha = (shader_id & SHADER_OPT_ALPHA) != 0;
    bool opt_fog = (shader_id & SHADER_OPT_FOG) != 0;
    bool opt_texture_edge = (shader_id & SHADER_OPT_TEXTURE_EDGE) != 0;
    bool opt_noise = (shader_id & SHADER_OPT_NOISE) != 0;

    bool used_textures[2] = { 0, 0 };
    int num_inputs = 0;
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 4; j++) {
            if (c[i][j] >= SHADER_INPUT_1 && c[i][j] <= SHADER_INPUT_4) {
                if (c[i][j] > num_inputs) {
                    num_inputs = c[i][j];
                }
            }
            if (c[i][j] == SHADER_TEXEL0 || c[i][j] == SHADER_TEXEL0A) {
                used_textures[0] = true;
            }
            if (c[i][j] == SHADER_TEXEL1) {
                used_textures[1] = true;
            }
        }
    }
    bool do_single[2] = { c[0][2] == 0, c[1][2] == 0 };
    bool do_multiply[2] = { c[0][1] == 0 && c[0][3] == 0, c[1][1] == 0 && c[1][3] == 0 };
    bool do_mix[2] = { c[0][1] == c[0][3], c[1][1] == c[1][3] };
    bool color_alpha_same = (shader_id & 0xfff) == ((shader_id >> 12) & 0xfff);

    char vs_buf[1024];
    char fs_buf[2048];
    size_t vs_len = 0;
    size_t fs_len = 0;
    size_t num_floats = 4;

    // Vertex shader
#ifdef USE_GLES
    append_line(vs_buf, &vs_len, "#version 100");
#else
    append_line(vs_buf, &vs_len, "#version 120");
#endif
    append_line(vs_buf, &vs_len, "attribute vec4 aVtxPos;");
    if (used_textures[0] || used_textures[1]) {
        append_line(vs_buf, &vs_len, "attribute vec2 aTexCoord;");
        append_line(vs_buf, &vs_len, "varying vec2 vTexCoord;");
        num_floats += 2;
    }
    if (opt_fog) {
        append_line(vs_buf, &vs_len, "attribute vec4 aFog;");
        append_line(vs_buf, &vs_len, "varying vec4 vFog;");
        num_floats += 4;
    }
    for (int i = 0; i < num_inputs; i++) {
        vs_len += sprintf(vs_buf + vs_len, "attribute vec%d aInput%d;\n", opt_alpha ? 4 : 3, i + 1);
        vs_len += sprintf(vs_buf + vs_len, "varying vec%d vInput%d;\n", opt_alpha ? 4 : 3, i + 1);
        num_floats += opt_alpha ? 4 : 3;
    }
    append_line(vs_buf, &vs_len, "void main() {");
    if (used_textures[0] || used_textures[1]) {
        append_line(vs_buf, &vs_len, "vTexCoord = aTexCoord;");
    }
    if (opt_fog) {
        append_line(vs_buf, &vs_len, "vFog = aFog;");
    }
    for (int i = 0; i < num_inputs; i++) {
        vs_len += sprintf(vs_buf + vs_len, "vInput%d = aInput%d;\n", i + 1, i + 1);
    }
    append_line(vs_buf, &vs_len, "gl_Position = aVtxPos;");
    append_line(vs_buf, &vs_len, "}");

    // Fragment shader
#ifdef USE_GLES
    append_line(fs_buf, &fs_len, "#version 100");
    append_line(fs_buf, &fs_len, "precision mediump float;");
#else
    append_line(fs_buf, &fs_len, "#version 120");
#endif

    if (used_textures[0] || used_textures[1]) {
        append_line(fs_buf, &fs_len, "varying vec2 vTexCoord;");
    }
    if (opt_fog) {
        append_line(fs_buf, &fs_len, "varying vec4 vFog;");
    }
    for (int i = 0; i < num_inputs; i++) {
        fs_len += sprintf(fs_buf + fs_len, "varying vec%d vInput%d;\n", opt_alpha ? 4 : 3, i + 1);
    }
    if (used_textures[0]) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uTex0;");
        append_line(fs_buf, &fs_len, "uniform vec2 uTex0Size;");
        append_line(fs_buf, &fs_len, "uniform bool uTex0Filter;");
    }
    if (used_textures[1]) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uTex1;");
        append_line(fs_buf, &fs_len, "uniform vec2 uTex1Size;");
        append_line(fs_buf, &fs_len, "uniform bool uTex1Filter;");
    }

    // 3 point texture filtering
    // Original author: ArthurCarvalho
    // Slightly modified GLSL implementation by twinaphex, mupen64plus-libretro project.

    if (used_textures[0] || used_textures[1]) {
        if (configFiltering == 2) {
            append_line(fs_buf, &fs_len, "#define TEX_OFFSET(off) texture2D(tex, texCoord - (off)/texSize)");
            append_line(fs_buf, &fs_len, "vec4 filter3point(in sampler2D tex, in vec2 texCoord, in vec2 texSize) {");
            append_line(fs_buf, &fs_len, "  vec2 offset = fract(texCoord*texSize - vec2(0.5));");
            append_line(fs_buf, &fs_len, "  offset -= step(1.0, offset.x + offset.y);");
            append_line(fs_buf, &fs_len, "  vec4 c0 = TEX_OFFSET(offset);");
            append_line(fs_buf, &fs_len, "  vec4 c1 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y));");
            append_line(fs_buf, &fs_len, "  vec4 c2 = TEX_OFFSET(vec2(offset.x, offset.y - sign(offset.y)));");
            append_line(fs_buf, &fs_len, "  return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);");
            append_line(fs_buf, &fs_len, "}");
            append_line(fs_buf, &fs_len, "vec4 sampleTex(in sampler2D tex, in vec2 uv, in vec2 texSize, in bool dofilter) {");
            append_line(fs_buf, &fs_len, "if (dofilter)");
            append_line(fs_buf, &fs_len, "return filter3point(tex, uv, texSize);");
            append_line(fs_buf, &fs_len, "else");
            append_line(fs_buf, &fs_len, "return texture2D(tex, uv);");
            append_line(fs_buf, &fs_len, "}");
        } else {
            append_line(fs_buf, &fs_len, "vec4 sampleTex(in sampler2D tex, in vec2 uv, in vec2 texSize, in bool dofilter) {");
            append_line(fs_buf, &fs_len, "return texture2D(tex, uv);");
            append_line(fs_buf, &fs_len, "}");
        }
    }

    if (opt_alpha && opt_noise) {
        append_line(fs_buf, &fs_len, "uniform float frame_count;");

        append_line(fs_buf, &fs_len, "float random(in vec3 value) {");
        append_line(fs_buf, &fs_len, "    float random = dot(sin(value), vec3(12.9898, 78.233, 37.719));");
#ifdef USE_GLES
        append_line(fs_buf, &fs_len, "    return fract(sin(random) * 143.7585453);");
#else
        append_line(fs_buf, &fs_len, "    return fract(sin(random) * 143758.5453);");
#endif
        append_line(fs_buf, &fs_len, "}");
    }

    append_line(fs_buf, &fs_len, "void main() {");

    if (used_textures[0]) {
        append_line(fs_buf, &fs_len, "vec4 texVal0 = sampleTex(uTex0, vTexCoord, uTex0Size, uTex0Filter);");
    }
    if (used_textures[1]) {
        append_line(fs_buf, &fs_len, "vec4 texVal1 = sampleTex(uTex1, vTexCoord, uTex1Size, uTex1Filter);");
    }

    append_str(fs_buf, &fs_len, opt_alpha ? "vec4 texel = " : "vec3 texel = ");
    if (!color_alpha_same && opt_alpha) {
        append_str(fs_buf, &fs_len, "vec4(");
        append_formula(fs_buf, &fs_len, c, do_single[0], do_multiply[0], do_mix[0], false, false, true);
        append_str(fs_buf, &fs_len, ", ");
        append_formula(fs_buf, &fs_len, c, do_single[1], do_multiply[1], do_mix[1], true, true, true);
        append_str(fs_buf, &fs_len, ")");
    } else {
        append_formula(fs_buf, &fs_len, c, do_single[0], do_multiply[0], do_mix[0], opt_alpha, false, opt_alpha);
    }
    append_line(fs_buf, &fs_len, ";");

    if (opt_texture_edge && opt_alpha) {
        append_line(fs_buf, &fs_len, "if (texel.a > 0.3) texel.a = 1.0; else discard;");
    }
    // TODO discard if alpha is 0?
    if (opt_fog) {
        if (opt_alpha) {
            append_line(fs_buf, &fs_len, "texel = vec4(mix(texel.rgb, vFog.rgb, vFog.a), texel.a);");
        } else {
            append_line(fs_buf, &fs_len, "texel = mix(texel, vFog.rgb, vFog.a);");
        }
    }

    if (opt_alpha && opt_noise) 
        append_line(fs_buf, &fs_len, "texel.a *= floor(random(floor(vec3(gl_FragCoord.xy, frame_count))) + 0.5);");

    if (opt_alpha) {
        append_line(fs_buf, &fs_len, "gl_FragColor = texel;");
    } else {
        append_line(fs_buf, &fs_len, "gl_FragColor = vec4(texel, 1.0);");
    }
    append_line(fs_buf, &fs_len, "}");

    vs_buf[vs_len] = '\0';
    fs_buf[fs_len] = '\0';

    /*puts("Vertex shader:");
    puts(vs_buf);
    puts("Fragment shader:");
    puts(fs_buf);
    puts("End");*/

    const GLchar *sources[2] = { vs_buf, fs_buf };
    const GLint lengths[2] = { vs_len, fs_len };
    GLint success;

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &sources[0], &lengths[0]);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 0;
        glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        fprintf(stderr, "Vertex shader compilation failed\n");
        glGetShaderInfoLog(vertex_shader, max_length, &max_length, &error_log[0]);
        fprintf(stderr, "%s\n", &error_log[0]);
        sys_fatal("vertex shader compilation failed (see terminal)");
    }

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &sources[1], &lengths[1]);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 0;
        glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        fprintf(stderr, "Fragment shader compilation failed\n");
        glGetShaderInfoLog(fragment_shader, max_length, &max_length, &error_log[0]);
        fprintf(stderr, "%s\n", &error_log[0]);
        sys_fatal("fragment shader compilation failed (see terminal)");
    }

    GLuint shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);

    size_t cnt = 0;

    struct ShaderProgram *prg = &shader_program_pool[shader_program_pool_size++];
    prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aVtxPos");
    prg->attrib_sizes[cnt] = 4;
    ++cnt;

    if (used_textures[0] || used_textures[1]) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aTexCoord");
        prg->attrib_sizes[cnt] = 2;
        ++cnt;
    }

    if (opt_fog) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aFog");
        prg->attrib_sizes[cnt] = 4;
        ++cnt;
    }

    for (int i = 0; i < num_inputs; i++) {
        char name[16];
        sprintf(name, "aInput%d", i + 1);
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
        prg->attrib_sizes[cnt] = opt_alpha ? 4 : 3;
        ++cnt;
    }

    prg->shader_id = shader_id;
    prg->opengl_program_id = shader_program;
    prg->num_inputs = num_inputs;
    prg->used_textures[0] = used_textures[0];
    prg->used_textures[1] = used_textures[1];
    prg->num_floats = num_floats;
    prg->num_attribs = cnt;

    gfx_opengl_load_shader(prg);

    if (used_textures[0]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex0");
        prg->uniform_locations[0] = glGetUniformLocation(shader_program, "uTex0Size");
        prg->uniform_locations[1] = glGetUniformLocation(shader_program, "uTex0Filter");
        glUniform1i(sampler_location, 0);
    }
    if (used_textures[1]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex1");
        prg->uniform_locations[2] = glGetUniformLocation(shader_program, "uTex1Size");
        prg->uniform_locations[3] = glGetUniformLocation(shader_program, "uTex1Filter");
        glUniform1i(sampler_location, 1);
    }

    if (opt_alpha && opt_noise) {
        prg->uniform_locations[4] = glGetUniformLocation(shader_program, "frame_count");
        prg->used_noise = true;
    } else {
        prg->used_noise = false;
    }

    return prg;
}

static struct ShaderProgram *gfx_opengl_lookup_shader(uint32_t shader_id) {
    for (size_t i = 0; i < shader_program_pool_size; i++) {
        if (shader_program_pool[i].shader_id == shader_id) {
            return &shader_program_pool[i];
        }
    }
    return NULL;
}

static void gfx_opengl_shader_get_info(struct ShaderProgram *prg, uint8_t *num_inputs, bool used_textures[2]) {
    *num_inputs = prg->num_inputs;
    used_textures[0] = prg->used_textures[0];
    used_textures[1] = prg->used_textures[1];
}

static GLuint gfx_opengl_new_texture(void) {
    if (num_textures >= tex_cache_size) {
        tex_cache_size += TEX_CACHE_STEP;
        tex_cache = realloc(tex_cache, sizeof(struct GLTexture) * tex_cache_size);
        if (!tex_cache) sys_fatal("out of memory allocating texture cache");
        // invalidate these because they might be pointing to garbage now
        opengl_tex[0] = NULL;
        opengl_tex[1] = NULL;
    }
    glGenTextures(1, &tex_cache[num_textures].gltex);
    return num_textures++;
}

static void gfx_opengl_select_texture(int tile, GLuint texture_id) {
     opengl_tex[tile] = tex_cache + texture_id;
     opengl_curtex = tile;
     glActiveTexture(GL_TEXTURE0 + tile);
     glBindTexture(GL_TEXTURE_2D, opengl_tex[tile]->gltex);
     gfx_opengl_set_texture_uniforms(opengl_prg, tile);
}

static void gfx_opengl_upload_texture(const uint8_t *rgba32_buf, int width, int height) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba32_buf);
    opengl_tex[opengl_curtex]->size[0] = width;
    opengl_tex[opengl_curtex]->size[1] = height;
}

static uint32_t gfx_cm_to_opengl(uint32_t val) {
    if (val & G_TX_CLAMP) {
        return GL_CLAMP_TO_EDGE;
    }
    return (val & G_TX_MIRROR) ? GL_MIRRORED_REPEAT : GL_REPEAT;
}

static void gfx_opengl_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    const GLenum filter = linear_filter ? GL_LINEAR : GL_LINEAR;
    glActiveTexture(GL_TEXTURE0 + tile);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gfx_cm_to_opengl(cms));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gfx_cm_to_opengl(cmt));
    opengl_curtex = tile;
    if (opengl_tex[tile]) {
        opengl_tex[tile]->filter = linear_filter;
        gfx_opengl_set_texture_uniforms(opengl_prg, tile);
    }
}

static void gfx_opengl_set_depth_test(bool depth_test) {
    if (depth_test) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

static void gfx_opengl_set_depth_mask(bool z_upd) {
    glDepthMask(z_upd ? GL_TRUE : GL_FALSE);
}

static void gfx_opengl_set_zmode_decal(bool zmode_decal) {
    if (zmode_decal) {
        glPolygonOffset(-2, -2);
        glEnable(GL_POLYGON_OFFSET_FILL);
    } else {
        glPolygonOffset(0, 0);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
}

static void gfx_opengl_set_viewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
}


static void gfx_opengl_set_scissor(int x, int y, int width, int height) {
    glScissor(x, y, width, height);
}


static void gfx_opengl_set_use_alpha(bool use_alpha) {
    if (use_alpha) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
}

static void gfx_opengl_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    //printf("flushing %d tris\n", buf_vbo_num_tris);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * buf_vbo_len, buf_vbo, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 3 * buf_vbo_num_tris);
}

static inline bool gl_get_version(int *major, int *minor, bool *is_es) {
    const char *vstr = (const char *)glGetString(GL_VERSION);
    if (!vstr || !vstr[0]) return false;

    if (!strncmp(vstr, "OpenGL ES ", 10)) {
        vstr += 10;
        *is_es = true;
    } else if (!strncmp(vstr, "OpenGL ES-CM ", 13)) {
        vstr += 13;
        *is_es = true;
    }

    return (sscanf(vstr, "%d.%d", major, minor) == 2);
}

static void post_shader_load_once(void);
static void gfx_opengl_init(void) {
#if FOR_WINDOWS || defined(OSX_BUILD)
    GLenum err;
    if ((err = glewInit()) != GLEW_OK)
        sys_fatal("could not init GLEW:\n%s", glewGetErrorString(err));
#endif

    tex_cache_size = TEX_CACHE_STEP;
    tex_cache = calloc(tex_cache_size, sizeof(struct GLTexture));
    if (!tex_cache) sys_fatal("out of memory allocating texture cache");

    // check GL version
    int vmajor, vminor;
    bool is_es = false;
    gl_get_version(&vmajor, &vminor, &is_es);
    if (vmajor < 2 && vminor < 1 && !is_es)
        sys_fatal("OpenGL 2.1+ is required.\nReported version: %s%d.%d", is_es ? "ES" : "", vmajor, vminor);

    glGenBuffers(1, &opengl_vbo);
    
    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);
    
    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    post_shader_load_once();
}

static void gfx_opengl_on_resize(void) {
}

int g_inResFBO = 0;
static GLuint s_resFBO = 0, s_resTex = 0, s_resDepth = 0;
static int s_resW = 0, s_resH = 0, s_resScaleLast = 100;
static unsigned int s_lowFBO = 0, s_lowTex = 0, s_lowProg = 0;
static unsigned int s_prevFBO = 0, s_prevTex = 0, s_prevProg = 0;
static unsigned int s_prevLowFBO = 0, s_prevLowTex = 0;
static unsigned int s_prevLowFBO1 = 0, s_prevLowTex1 = 0; static int s_prevW = 0, s_prevH = 0; static int s_lowW = 0, s_lowH = 0; static int s_lowWinW = 0, s_lowWinH = 0;
static int s_scalingFilterLast = 0;
static int s_winW = 1280, s_winH = 720;
static GLuint s_blitProg = 0, s_blitVBO = 0;
static char g_postFs[16384] = {0};
static int g_postFsReady = 0;
char g_shaderFs[32][16384] = {{0}};
static unsigned int g_shaderProg[32] = {0};
static unsigned int s_tmpFBO = 0, s_tmpTex = 0;
static unsigned int s_tmpFBO2 = 0, s_tmpTex2 = 0;
char g_shaderNames[32][64] = {{0}};
int g_shaderCount = 0;
int g_shaderEnabled[32] = {0};
int g_shaderActive = -1; // index shader yang aktif, -1 = off
void post_shader_scan(void);
void post_shader_reload(void);
extern const char *sys_exe_path(void);
extern const char *fs_get_write_path(const char *vpath);
void post_shader_scan(void) {
    char want_list[32][64] = {{0}}; int want_n = 0;
    for (int i = 0; i < g_shaderCount && i < 32; i++) if (g_shaderEnabled[i]) { snprintf(want_list[want_n], 64, "%s", g_shaderNames[i]); want_n++; }
    // juga simpen active lama buat kompat, tapi multi yang dipake
    char want_save[64] = {0}; if (want_n > 0) snprintf(want_save, sizeof(want_save), "%s", want_list[0]);
    // PPSSPP-style: JANGAN rebuild dari nol, urutan Up/Down ilang kalo rebuild.
    // Pertahanin urutan lama, cuma hapus yg udah gak ada + append file baru di bawah.
    {
        char oldNames[32][64] = {{0}}; int oldN = g_shaderCount;
        for (int i = 0; i < oldN; i++) snprintf(oldNames[i], 64, "%s", g_shaderNames[i]);
        // nanti di bawah, g_shaderCount dibikin ulang tapi mulai dari oldNames yg masih ada
        // tandain: kita kosongin dulu tapi inget oldNames
        g_shaderCount = 0;
        for (int i = 0; i < oldN; i++) {
            // cek file masih ada di salah satu folder? cek nanti di scan, simpan dulu ke temp
            // cara simpel: masukin lagi oldNames, nanti file yg gak ada kehapus di pass kedua
            snprintf(g_shaderNames[g_shaderCount], 64, "%s", oldNames[i]);
            // enabled sementara 0, nanti dipulihin dari want_list di bawah
            g_shaderEnabled[g_shaderCount] = 0;
            g_shaderCount++;
        }
        // simpan oldNames ke static biar scan di bawah tau mana yg baru
        // (scan di bawah cuma nambah yg belum ada, jadi urutan lama aman)
    }
    // scan 2 folder: files/shaders + files/user/shaders, pake opendir
    const char *dirs[2]; char d1[1024]; snprintf(d1, sizeof(d1), "%s/shaders", sys_exe_path()); char d2[1024]; snprintf(d2, sizeof(d2), "%s", fs_get_write_path("shaders")); dirs[0] = d1; dirs[1] = d2;
    for (int di = 0; di < 2; di++) {
        DIR *dir = opendir(dirs[di]); if (!dir) continue;
        struct dirent *e; while ((e = readdir(dir)) != NULL) {
            const char *n = e->d_name; size_t len = strlen(n); if (len < 6) continue; if (strcmp(n + len - 5, ".glsl") != 0) continue;
            int exists = 0; for (int i = 0; i < g_shaderCount; i++) if (strcmp(g_shaderNames[i], n) == 0) exists = 1;
            if (exists) continue; if (g_shaderCount >= 32) break;
            snprintf(g_shaderNames[g_shaderCount], 64, "%s", n); g_shaderEnabled[g_shaderCount] = 0; g_shaderCount++;
        }
        closedir(dir);
    }
    // hapus nama yg file-nya udah gak ada di kedua folder
    {
        for (int i = 0; i < g_shaderCount; ) {
            char pp1[1024]; snprintf(pp1, sizeof(pp1), "%s/shaders/%s", sys_exe_path(), g_shaderNames[i]);
            char pp2base[1024]; snprintf(pp2base, sizeof(pp2base), "%s", fs_get_write_path("shaders"));
            char pp2[1024]; snprintf(pp2, sizeof(pp2), "%s/%s", pp2base, g_shaderNames[i]);
            FILE *f1 = fopen(pp1, "r"); FILE *f2 = f1 ? NULL : fopen(pp2, "r");
            int ada = (f1 != NULL || f2 != NULL);
            if (f1) fclose(f1); if (f2) fclose(f2);
            if (!ada) {
                for (int k = i; k + 1 < g_shaderCount; k++) {
                    snprintf(g_shaderNames[k], 64, "%s", g_shaderNames[k+1]);
                    g_shaderEnabled[k] = g_shaderEnabled[k+1];
                }
                g_shaderCount--;
            } else i++;
        }
    }
    // pertahanin MULTI toggle by nama (want_list), JANGAN baca active.txt di scan
    for (int k = 0; k < want_n; k++) { for (int j = 0; j < g_shaderCount; j++) if (strcmp(g_shaderNames[j], want_list[k]) == 0) g_shaderEnabled[j] = 1; }
    if (want_n > 0) { for (int j = 0; j < g_shaderCount; j++) if (strcmp(g_shaderNames[j], want_list[0]) == 0) g_shaderActive = j; }
    if (g_shaderActive >= g_shaderCount) g_shaderActive = g_shaderCount - 1;
}
void post_shader_reload(void) {
    g_postFsReady = 0; g_postFs[0] = 0;
    for (int i = 0; i < 32; i++) { g_shaderFs[i][0] = 0; if (g_shaderProg[i]) { glDeleteProgram(g_shaderProg[i]); g_shaderProg[i] = 0; } }
    post_shader_scan();
    { char ap[1024]; snprintf(ap, sizeof(ap), "%s/shaders/active.txt", sys_exe_path()); FILE *af = fopen(ap, "w"); if (af) { for (int i = 0; i < g_shaderCount; i++) if (g_shaderEnabled[i]) fprintf(af, "%s\n", g_shaderNames[i]); fclose(af); } }
    { char op[1024]; snprintf(op, sizeof(op), "%s/shaders/order.txt", sys_exe_path()); FILE *of = fopen(op, "w"); if (of) { for (int i = 0; i < g_shaderCount; i++) fprintf(of, "%s\n", g_shaderNames[i]); fclose(of); } }
    // load SEMUA shader yang enabled, urutan = urutan list (atas=belakang, bawah=depan)
    for (int si = 0; si < g_shaderCount; si++) if (g_shaderEnabled[si]) {
        char path1[1024]; snprintf(path1, sizeof(path1), "%s/shaders/%s", sys_exe_path(), g_shaderNames[si]);
        char path2[1024]; snprintf(path2, sizeof(path2), "%s", fs_get_write_path("shaders")); char full2[1024]; snprintf(full2, sizeof(full2), "%s/%s", path2, g_shaderNames[si]);
        const char *tryP2[2]; tryP2[0] = path1; tryP2[1] = full2;
        for (int ii = 0; ii < 2; ii++) { FILE *ff2 = fopen(tryP2[ii], "r"); if (!ff2) continue; size_t nn = fread(g_shaderFs[si], 1, sizeof(g_shaderFs[si])-1, ff2); fclose(ff2); g_shaderFs[si][nn] = 0; if (strstr(g_shaderFs[si], "void main") && strstr(g_shaderFs[si], "precision") && strstr(g_shaderFs[si], "vUV")) { printf("post shader reloaded multi: %s\n", tryP2[ii]); break; } else { g_shaderFs[si][0] = 0; } }
    }
    // kompat: g_postFs = shader pertama yang enabled (biar kode lama gak jebol), tapi res_blit pake g_shaderFs
    for (int si = 0; si < g_shaderCount; si++) if (g_shaderEnabled[si] && g_shaderFs[si][0]) { snprintf(g_postFs, sizeof(g_postFs), "%s", g_shaderFs[si]); break; }
    g_postFsReady = 1; for (int i = 0; i < 32; i++) if (g_shaderProg[i]) { glDeleteProgram(g_shaderProg[i]); g_shaderProg[i] = 0; } s_blitProg = 0; return;
}
static void post_shader_load_once(void) {
    if (g_postFsReady) return;
    g_postFsReady = 1;
    g_postFs[0] = 0;
    // cuma bikin folder, JANGAN bikin file shader default lagi - file baru cuma dari tombol Add di ImGui
    { char d1[1024]; snprintf(d1, sizeof(d1), "%s/shaders", sys_exe_path()); mkdir(d1, 0777);
      char d2[1024]; snprintf(d2, sizeof(d2), "%s", fs_get_write_path("shaders")); mkdir(d2, 0777); }
    post_shader_scan();
    // kalo folder kosong, bikin 1 default shader ke data internal, tapi TETEP OFF
    if (g_shaderCount == 0) {
        char dp[1024]; snprintf(dp, sizeof(dp), "%s/shaders/default.glsl", sys_exe_path());
        FILE *w = fopen(dp, "w");
        if (w) { const char *def = "precision mediump float;\nvarying vec2 vUV;\nuniform sampler2D uTex;\nuniform vec2 uResolution;\nuniform vec2 uTexelSize;\nvoid main() {\n  vec4 c = texture2D(uTex, vUV);\n  gl_FragColor = c;\n}\n"; fputs(def, w); fclose(w); printf("default shader generated (OFF): %s\n", dp); }
        post_shader_scan();
        for (int k = 0; k < g_shaderCount; k++) g_shaderEnabled[k] = 0; g_shaderActive = -1;
    }
    // restore URUTAN dari order.txt dulu (ON+OFF), baru restore toggle dari active.txt
    { char op[1024]; snprintf(op, sizeof(op), "%s/shaders/order.txt", sys_exe_path()); FILE *of = fopen(op, "r"); if (of) { char olines[32][64] = {{0}}; int on = 0; char oline[64]; while (on < 32 && fgets(oline, sizeof(oline), of)) { oline[strcspn(oline, "\r\n")] = 0; if (!oline[0]) continue; snprintf(olines[on], 64, "%s", oline); on++; } fclose(of); if (on > 0) { char nn[32][64] = {{0}}; int ne[32] = {0}; char nf[32][16384] = {{0}}; int nc = 0; int used[32] = {0}; for (int a = 0; a < on; a++) for (int k = 0; k < g_shaderCount; k++) if (!used[k] && strcmp(g_shaderNames[k], olines[a]) == 0) { snprintf(nn[nc], 64, "%s", g_shaderNames[k]); ne[nc] = g_shaderEnabled[k]; snprintf(nf[nc], sizeof(nf[nc]), "%s", g_shaderFs[k]); nc++; used[k] = 1; break; } for (int k = 0; k < g_shaderCount; k++) if (!used[k]) { snprintf(nn[nc], 64, "%s", g_shaderNames[k]); ne[nc] = g_shaderEnabled[k]; snprintf(nf[nc], sizeof(nf[nc]), "%s", g_shaderFs[k]); nc++; } for (int k = 0; k < nc; k++) { snprintf(g_shaderNames[k], 64, "%s", nn[k]); g_shaderEnabled[k] = ne[k]; snprintf(g_shaderFs[k], sizeof(g_shaderFs[k]), "%s", nf[k]); } g_shaderCount = nc; } } }
    // restore MULTI dari active.txt CUMA pas awal start (1 baris per shader, urutan = list)
    { char ap[1024]; snprintf(ap, sizeof(ap), "%s/shaders/active.txt", sys_exe_path()); FILE *af = fopen(ap, "r"); if (af) { char line[64]; for (int k = 0; k < g_shaderCount; k++) g_shaderEnabled[k] = 0; g_shaderActive = -1; while (fgets(line, sizeof(line), af)) { line[strcspn(line, "\r\n")] = 0; if (!line[0]) continue; for (int k = 0; k < g_shaderCount; k++) if (strcmp(g_shaderNames[k], line) == 0) { g_shaderEnabled[k] = 1; if (g_shaderActive < 0) g_shaderActive = k; } } fclose(af); } }
    // kalo gak ada yang aktif, biarin g_postFs kosong = passthrough texture-only, gak ada efek
    if (g_shaderActive < 0 || g_shaderActive >= g_shaderCount || !g_shaderEnabled[g_shaderActive]) return;
    // STARTUP: load SEMUA yang enabled ke g_shaderFs, bukan cuma pertama ke g_postFs
    // kalo cuma pertama, res_blit ngitung enN dari g_shaderFs -> shader 2+ ke-skip walau toggle nyala
    for (int si = 0; si < g_shaderCount; si++) if (g_shaderEnabled[si]) {
        char q1[1024]; snprintf(q1, sizeof(q1), "%s/shaders/%s", sys_exe_path(), g_shaderNames[si]);
        char q2b[1024]; snprintf(q2b, sizeof(q2b), "%s", fs_get_write_path("shaders")); char q2[1024]; snprintf(q2, sizeof(q2), "%s/%s", q2b, g_shaderNames[si]);
        const char *tp[2]; tp[0] = q1; tp[1] = q2;
        for (int i = 0; i < 2; i++) {
            FILE *ff = fopen(tp[i], "r");
            if (!ff) continue;
            size_t n = fread(g_shaderFs[si], 1, sizeof(g_shaderFs[si])-1, ff);
            fclose(ff);
            g_shaderFs[si][n] = 0;
            if (strstr(g_shaderFs[si], "void main") && strstr(g_shaderFs[si], "precision") && strstr(g_shaderFs[si], "vUV")) { printf("post shader loaded: %s\n", tp[i]); break; }
            printf("post shader invalid, skip: %s\n", tp[i]);
            g_shaderFs[si][0] = 0;
        }
    }
    for (int si = 0; si < g_shaderCount; si++) if (g_shaderEnabled[si] && g_shaderFs[si][0]) { snprintf(g_postFs, sizeof(g_postFs), "%s", g_shaderFs[si]); break; }
}
static void res_create_fbo(int w, int h) {
    if (s_resFBO) { glDeleteFramebuffers(1, &s_resFBO); glDeleteTextures(1, &s_resTex); glDeleteRenderbuffers(1, &s_resDepth); s_resFBO = 0; }
    s_resW = w; s_resH = h;
    glGenFramebuffers(1, &s_resFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, s_resFBO); g_inResFBO = 1;
    glGenTextures(1, &s_resTex);
    glBindTexture(GL_TEXTURE_2D, s_resTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (configScalingFiltering == 1 ? GL_NEAREST : GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (configScalingFiltering == 1 ? GL_NEAREST : GL_LINEAR)); // NEAREST biar kotak-kotak, kerasa burik-nya, bukan blur halus
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_resTex, 0);
    glGenRenderbuffers(1, &s_resDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, s_resDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s_resDepth);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); g_inResFBO = 0;
}
static void res_blit(void) {
    // SAVE state game - biar gak ngerusak frame berikutnya
    GLint oldProg = 0, oldVBO = 0, oldTex = 0, oldFBO = 0;
    GLint oldViewport[4] = {0,0,0,0};
    GLboolean oldDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean oldBlend = glIsEnabled(GL_BLEND);
    GLboolean oldScissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProg);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldVBO);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTex);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    if (!s_blitProg) {
        const char *vs = "attribute vec2 aPos; attribute vec2 aUV; varying vec2 vUV; void main(){ vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }";
const char *fs_default = "precision mediump float; varying vec2 vUV; uniform sampler2D uTex; void main(){ gl_FragColor = texture2D(uTex, vUV); }"; const char *fs = (g_postFs[0] && strstr(g_postFs, "void main") != NULL) ? g_postFs : fs_default;
        GLuint v = glCreateShader(GL_VERTEX_SHADER); glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
        GLuint f = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
        { GLint ok = 0; glGetShaderiv(f, GL_COMPILE_STATUS, &ok); if (!ok) { char log[1024]; glGetShaderInfoLog(f, sizeof(log), NULL, log); printf("post shader compile FAIL: %s\n", log); glDeleteShader(f); f = glCreateShader(GL_FRAGMENT_SHADER); const char *fs2 = "precision mediump float; varying vec2 vUV; uniform sampler2D uTex; void main(){ gl_FragColor = texture2D(uTex, vUV); }"; glShaderSource(f, 1, &fs2, NULL); glCompileShader(f); } }
        s_blitProg = glCreateProgram(); glAttachShader(s_blitProg, v); glAttachShader(s_blitProg, f); glLinkProgram(s_blitProg);
        { GLint ok = 0; glGetProgramiv(s_blitProg, GL_LINK_STATUS, &ok); if (!ok) { char log[1024]; glGetProgramInfoLog(s_blitProg, sizeof(log), NULL, log); printf("post shader LINK FAIL: %s\n", log); glDeleteProgram(s_blitProg); glDeleteShader(v); glDeleteShader(f); GLuint v2 = glCreateShader(GL_VERTEX_SHADER); const char *vs2 = "attribute vec2 aPos; attribute vec2 aUV; varying vec2 vUV; void main(){ vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }"; glShaderSource(v2, 1, &vs2, NULL); glCompileShader(v2); GLuint f2 = glCreateShader(GL_FRAGMENT_SHADER); const char *fs3 = "precision mediump float; varying vec2 vUV; uniform sampler2D uTex; void main(){ gl_FragColor = texture2D(uTex, vUV); }"; glShaderSource(f2, 1, &fs3, NULL); glCompileShader(f2); s_blitProg = glCreateProgram(); glAttachShader(s_blitProg, v2); glAttachShader(s_blitProg, f2); glLinkProgram(s_blitProg); } }
        float quad[] = { -1,-1, 0,0,  1,-1, 1,0,  -1,1, 0,1,  1,1, 1,1 };
        glGenBuffers(1, &s_blitVBO); glBindBuffer(GL_ARRAY_BUFFER, s_blitVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, oldVBO);
    }
    // LOWRES: 6x3 di 1280x720 = slider 10 -> eff 0.5% (pow curve line 943), buat bloom enteng
    {
        int lw = (int)(s_winW * 0.005f); if (lw < 1) lw = 1;
        int lh = (int)(s_winH * 0.005f); if (lh < 1) lh = 1;
        if (!s_lowFBO || s_lowWinW != s_winW || s_lowWinH != s_winH || s_lowW != lw || s_lowH != lh) {
            if (s_lowFBO) { glDeleteFramebuffers(1, &s_lowFBO); glDeleteTextures(1, &s_lowTex); s_lowFBO = 0; s_lowTex = 0; }
            s_lowW = lw; s_lowH = lh; s_lowWinW = s_winW; s_lowWinH = s_winH;
            glGenFramebuffers(1, &s_lowFBO); glGenTextures(1, &s_lowTex);
            glBindTexture(GL_TEXTURE_2D, s_lowTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s_lowW, s_lowH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glBindFramebuffer(GL_FRAMEBUFFER, s_lowFBO); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_lowTex, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
        }
        // PREVLOW: copy low lama (frame sebelumnya) -> prevLow, SEBELUM low ditimpa frame baru
        if (!s_prevLowFBO || s_lowW != 0) {
            // bikin / recreate kalo ukuran low berubah
            static int plW = 0, plH = 0;
            if (!s_prevLowFBO || plW != s_lowW || plH != s_lowH) {
                if (s_prevLowFBO) { glDeleteFramebuffers(1, &s_prevLowFBO); glDeleteTextures(1, &s_prevLowTex); s_prevLowFBO = 0; s_prevLowTex = 0; }
                plW = s_lowW; plH = s_lowH;
                glGenFramebuffers(1, &s_prevLowFBO); glGenTextures(1, &s_prevLowTex);
                glBindTexture(GL_TEXTURE_2D, s_prevLowTex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, plW, plH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
                glBindFramebuffer(GL_FRAMEBUFFER, s_prevLowFBO); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_prevLowTex, 0);
                glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
            }
            // bikin prevLow1 (2 frame lalu) kalo belum ada, seukuran low
            { static int pl1W = 0, pl1H = 0; if (!s_prevLowFBO1 || pl1W != s_lowW || pl1H != s_lowH) { if (s_prevLowFBO1) { glDeleteFramebuffers(1, &s_prevLowFBO1); glDeleteTextures(1, &s_prevLowTex1); s_prevLowFBO1 = 0; s_prevLowTex1 = 0; } pl1W = s_lowW; pl1H = s_lowH; glGenFramebuffers(1, &s_prevLowFBO1); glGenTextures(1, &s_prevLowTex1); glBindTexture(GL_TEXTURE_2D, s_prevLowTex1); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pl1W, pl1H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL); glBindFramebuffer(GL_FRAMEBUFFER, s_prevLowFBO1); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_prevLowTex1, 0); glBindFramebuffer(GL_FRAMEBUFFER, oldFBO); }
              // geser: prev0 (1 frame lalu) -> prev1 (2 frame lalu), SEBELUM prev0 ditimpa low baru
              if (s_prevLowTex && s_lowProg) { GLint cf1 = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &cf1); glBindFramebuffer(GL_FRAMEBUFFER, s_prevLowFBO1); glViewport(0, 0, s_lowW, s_lowH); glUseProgram(s_lowProg); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, s_prevLowTex); glUniform1i(glGetUniformLocation(s_lowProg, "uTex"), 0); if (s_blitVBO) { glBindBuffer(GL_ARRAY_BUFFER, s_blitVBO); GLint pp1 = glGetAttribLocation(s_lowProg, "aPos"); GLint pu1 = glGetAttribLocation(s_lowProg, "aUV"); glEnableVertexAttribArray(pp1); glEnableVertexAttribArray(pu1); glVertexAttribPointer(pp1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0); glVertexAttribPointer(pu1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float))); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); glDisableVertexAttribArray(pp1); glDisableVertexAttribArray(pu1); } glBindFramebuffer(GL_FRAMEBUFFER, cf1); } }
            // copy low lama -> prevLow pake s_lowProg (passthrough), kalo low udah pernah keisi
            if (s_lowTex && s_lowProg) { GLint cf = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &cf); glBindFramebuffer(GL_FRAMEBUFFER, s_prevLowFBO); glViewport(0, 0, s_lowW, s_lowH); glUseProgram(s_lowProg); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, s_lowTex); glUniform1i(glGetUniformLocation(s_lowProg, "uTex"), 0); if (s_blitVBO) { glBindBuffer(GL_ARRAY_BUFFER, s_blitVBO); GLint pp2 = glGetAttribLocation(s_lowProg, "aPos"); GLint pu2 = glGetAttribLocation(s_lowProg, "aUV"); glEnableVertexAttribArray(pp2); glEnableVertexAttribArray(pu2); glVertexAttribPointer(pp2, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0); glVertexAttribPointer(pu2, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float))); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); glDisableVertexAttribArray(pp2); glDisableVertexAttribArray(pu2); } glBindFramebuffer(GL_FRAMEBUFFER, cf); }
        }
        // GLES2: downscale pake quad passthrough, bukan glBlitFramebuffer (cuma GLES3)
        if (!s_lowProg) {
            const char *lvs = "attribute vec2 aPos; attribute vec2 aUV; varying vec2 vUV; void main(){ vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }";
            const char *lfs = "precision mediump float; varying vec2 vUV; uniform sampler2D uTex; void main(){ gl_FragColor = texture2D(uTex, vUV); }";
            GLuint lv = glCreateShader(GL_VERTEX_SHADER); glShaderSource(lv, 1, &lvs, NULL); glCompileShader(lv);
            GLuint lf = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(lf, 1, &lfs, NULL); glCompileShader(lf);
            s_lowProg = glCreateProgram(); glAttachShader(s_lowProg, lv); glAttachShader(s_lowProg, lf); glLinkProgram(s_lowProg);
        }
        { GLint curFBO = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &curFBO);
          glBindFramebuffer(GL_FRAMEBUFFER, s_lowFBO); glViewport(0, 0, s_lowW, s_lowH);
          glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND);
          glUseProgram(s_lowProg); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, s_resTex);
          glUniform1i(glGetUniformLocation(s_lowProg, "uTex"), 0);
          if (s_blitVBO) { glBindBuffer(GL_ARRAY_BUFFER, s_blitVBO); GLint lp = glGetAttribLocation(s_lowProg, "aPos"); GLint lu = glGetAttribLocation(s_lowProg, "aUV"); glEnableVertexAttribArray(lp); glEnableVertexAttribArray(lu); glVertexAttribPointer(lp, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0); glVertexAttribPointer(lu, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float))); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); glDisableVertexAttribArray(lp); glDisableVertexAttribArray(lu); }
          glBindFramebuffer(GL_FRAMEBUFFER, curFBO); }
    }
    // PREV: frame sebelumnya buat motion blur / LCD persistence, seukuran s_resW/H
    if (!s_prevFBO || s_prevW != s_resW || s_prevH != s_resH) {
        if (s_prevFBO) { glDeleteFramebuffers(1, &s_prevFBO); glDeleteTextures(1, &s_prevTex); s_prevFBO = 0; s_prevTex = 0; }
        s_prevW = s_resW; s_prevH = s_resH;
        glGenFramebuffers(1, &s_prevFBO); glGenTextures(1, &s_prevTex);
        glBindTexture(GL_TEXTURE_2D, s_prevTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s_prevW, s_prevH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, s_prevFBO); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_prevTex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
        if (!s_prevProg) { const char *pvs = "attribute vec2 aPos; attribute vec2 aUV; varying vec2 vUV; void main(){ vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }"; const char *pfs = "precision mediump float; varying vec2 vUV; uniform sampler2D uTex; void main(){ gl_FragColor = texture2D(uTex, vUV); }"; GLuint pv = glCreateShader(GL_VERTEX_SHADER); glShaderSource(pv, 1, &pvs, NULL); glCompileShader(pv); GLuint pf = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(pf, 1, &pfs, NULL); glCompileShader(pf); s_prevProg = glCreateProgram(); glAttachShader(s_prevProg, pv); glAttachShader(s_prevProg, pf); glLinkProgram(s_prevProg); }
    }
    // MULTI-PASS: kumpulin index enabled sesuai urutan list (atas=belakang, bawah=depan)
    int enIdx[32]; int enN = 0; for (int i = 0; i < g_shaderCount; i++) if (g_shaderEnabled[i] && g_shaderFs[i][0]) enIdx[enN++] = i;
    if (enN == 0) {
        // gak ada shader aktif -> passthrough biasa (kode lama di bawah tetep jalan, pake fs_default)
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, s_winW, s_winH);
        g_inResFBO = 0;
    } else if (enN == 1) {
        // 1 shader -> path lama (single)
        snprintf(g_postFs, sizeof(g_postFs), "%s", g_shaderFs[enIdx[0]]);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, s_winW, s_winH);
        g_inResFBO = 0;
    } else {
        // >1 shader -> ping-pong: pass 0..N-2 ke temp FBO, pass terakhir ke layar
        // PPSSPP: JANGAN bikin temp di sini. Bikin A/B cuma di dalam loop (yg udah glBindFramebuffer dulu).
        // Kalo bikin di sini tanpa bind, glFramebufferTexture2D nimpa s_resFBO -> s_resTex ke-freeze.
        // Recreate temp kalo ukuran render berubah:
        {
            static int tmpW = 0, tmpH = 0;
            if ((s_tmpFBO || s_tmpFBO2) && (tmpW != s_resW || tmpH != s_resH)) {
                if (s_tmpFBO) { glDeleteFramebuffers(1, &s_tmpFBO); glDeleteTextures(1, &s_tmpTex); s_tmpFBO = 0; s_tmpTex = 0; }
                if (s_tmpFBO2) { glDeleteFramebuffers(1, &s_tmpFBO2); glDeleteTextures(1, &s_tmpTex2); s_tmpFBO2 = 0; s_tmpTex2 = 0; }
            }
            tmpW = s_resW; tmpH = s_resH;
        }
        // helper: compile program per-shader on demand ada di bawah, di sini kita loop manual
        // untuk hemat, kita pake s_blitProg yang di-compile per pass via g_postFs swap
        unsigned int curTex = s_resTex; // LAYER: semua pass sample texture asli, bukan hasil pass sebelumnya
        for (int pi = 0; pi < enN; pi++) {
            int isLast = (pi == enN - 1);
            snprintf(g_postFs, sizeof(g_postFs), "%s", g_shaderFs[enIdx[pi]]);
            // force recompile untuk shader ini: hapus program lama, bikin baru di bawah (kita set s_blitProg=0 tiap pass, biar ke-compile ulang dari g_postFs)
            if (s_blitProg) { glDeleteProgram(s_blitProg); s_blitProg = 0; }
            // compile + draw untuk pass ini - duplikat logika compile di bawah, tapi kita panggil via goto-like: kita set flag dan break ke kode draw biasa dengan FBO tujuan yang beda
            // Simpel: kita jalanin pass ini langsung di sini (compile + draw)
            {
                const char *vs = "attribute vec2 aPos; attribute vec2 aUV; varying vec2 vUV; void main(){ vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }";
                const char *fs = g_shaderFs[enIdx[pi]];
                GLuint prog = g_shaderProg[enIdx[pi]];
                if (!prog) {
                    GLuint v = glCreateShader(GL_VERTEX_SHADER); glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
                    GLuint f = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
                    { GLint okm = 0; glGetShaderiv(f, GL_COMPILE_STATUS, &okm); if (!okm) { char logm[1024]; glGetShaderInfoLog(f, sizeof(logm), NULL, logm); printf("post shader %d COMPILE FAIL: %s\n", enIdx[pi], logm); glDeleteShader(f); f = glCreateShader(GL_FRAGMENT_SHADER); const char *fs_p = "precision mediump float; varying vec2 vUV; uniform sampler2D uTex; void main(){ gl_FragColor = texture2D(uTex, vUV); }"; glShaderSource(f, 1, &fs_p, NULL); glCompileShader(f); } }
                    prog = glCreateProgram(); glAttachShader(prog, v); glAttachShader(prog, f); glLinkProgram(prog);
                    g_shaderProg[enIdx[pi]] = prog;
                }
                // PPSSPP multibuffer: dest gantian A -> B -> A ... , pass terakhir ke layar
                int isLast2 = (pi == enN - 1);
                if (isLast2) { glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, s_winW, s_winH); }
                else if (pi % 2 == 0) {
                    // pass genap -> tulis ke A, bikin A kalo belum ada
                    if (!s_tmpFBO) { glGenFramebuffers(1, &s_tmpFBO); glGenTextures(1, &s_tmpTex); glBindTexture(GL_TEXTURE_2D, s_tmpTex); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s_resW, s_resH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL); glBindFramebuffer(GL_FRAMEBUFFER, s_tmpFBO); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_tmpTex, 0); }
                    glBindFramebuffer(GL_FRAMEBUFFER, s_tmpFBO); glViewport(0, 0, s_resW, s_resH);
                } else {
                    // pass ganjil -> tulis ke B
                    if (!s_tmpFBO2) { glGenFramebuffers(1, &s_tmpFBO2); glGenTextures(1, &s_tmpTex2); glBindTexture(GL_TEXTURE_2D, s_tmpTex2); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s_resW, s_resH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL); glBindFramebuffer(GL_FRAMEBUFFER, s_tmpFBO2); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_tmpTex2, 0); }
                    glBindFramebuffer(GL_FRAMEBUFFER, s_tmpFBO2); glViewport(0, 0, s_resW, s_resH);
                }
                glDisable(GL_BLEND); // chain, bukan blend - depan nerusin hasil belakang
                g_inResFBO = 0;
                glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND);
                glUseProgram(prog);
                glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, curTex);
                glUniform1i(glGetUniformLocation(prog, "uTex"), 0); { GLint pl0 = glGetUniformLocation(prog, "uPrevLowTex0"); if (pl0 >= 0) { glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, s_prevLowTex); glUniform1i(pl0, 3); glActiveTexture(GL_TEXTURE0); } GLint pl1 = glGetUniformLocation(prog, "uPrevLowTex1"); if (pl1 >= 0) { glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, s_prevLowTex1); glUniform1i(pl1, 4); glActiveTexture(GL_TEXTURE0); } GLint pl = glGetUniformLocation(prog, "uPrevLowTex"); if (pl >= 0) { glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, s_prevLowTex); glUniform1i(pl, 3); glActiveTexture(GL_TEXTURE0); } } { GLint pv = glGetUniformLocation(prog, "uPrevTex"); if (pv >= 0) { glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, s_prevTex); glUniform1i(pv, 2); glActiveTexture(GL_TEXTURE0); } } { GLint ll = glGetUniformLocation(prog, "uLowTex"); if (ll >= 0) { glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, s_lowTex); glUniform1i(ll, 1); glActiveTexture(GL_TEXTURE0); } GLint lrl = glGetUniformLocation(prog, "uLowResolution"); if (lrl >= 0) glUniform2f(lrl, (float)s_lowW, (float)s_lowH); GLint ltl = glGetUniformLocation(prog, "uLowTexelSize"); if (ltl >= 0) glUniform2f(ltl, s_lowW ? 1.0f / (float)s_lowW : 0.0f, s_lowH ? 1.0f / (float)s_lowH : 0.0f); } { GLint rl = glGetUniformLocation(prog, "uResolution"); if (rl >= 0) glUniform2f(rl, (float)s_resW, (float)s_resH); GLint tl = glGetUniformLocation(prog, "uTexelSize"); if (tl >= 0) glUniform2f(tl, s_resW ? 1.0f / (float)s_resW : 0.0f, s_resH ? 1.0f / (float)s_resH : 0.0f); }
                if (!s_blitVBO) { float quad2[] = { -1,-1, 0,0,  1,-1, 1,0,  -1,1, 0,1,  1,1, 1,1 }; glGenBuffers(1, &s_blitVBO); glBindBuffer(GL_ARRAY_BUFFER, s_blitVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(quad2), quad2, GL_STATIC_DRAW); }
                glBindBuffer(GL_ARRAY_BUFFER, s_blitVBO);
                GLint aP = glGetAttribLocation(prog, "aPos"); GLint aU = glGetAttribLocation(prog, "aUV");
                glEnableVertexAttribArray(aP); glEnableVertexAttribArray(aU);
                glVertexAttribPointer(aP, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
                glVertexAttribPointer(aU, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                glDisableVertexAttribArray(aP); glDisableVertexAttribArray(aU);
                // JANGAN delete prog/vbo tiap frame - di-cache biar gak freeze
                // chain: pass berikutnya sample hasil pass ini
                if (pi % 2 == 0) curTex = s_tmpTex; else curTex = s_tmpTex2;
            }
        }
        // simpen s_resTex sekarang ke prev buat frame berikutnya
        { glBindFramebuffer(GL_FRAMEBUFFER, s_prevFBO); glViewport(0, 0, s_prevW, s_prevH); glUseProgram(s_prevProg); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, s_resTex); glUniform1i(glGetUniformLocation(s_prevProg, "uTex"), 0); if (s_blitVBO) { glBindBuffer(GL_ARRAY_BUFFER, s_blitVBO); GLint pp = glGetAttribLocation(s_prevProg, "aPos"); GLint pu = glGetAttribLocation(s_prevProg, "aUV"); glEnableVertexAttribArray(pp); glEnableVertexAttribArray(pu); glVertexAttribPointer(pp, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0); glVertexAttribPointer(pu, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float))); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); glDisableVertexAttribArray(pp); glDisableVertexAttribArray(pu); } }
        glDisable(GL_BLEND);
        glUseProgram(oldProg);
        glBindBuffer(GL_ARRAY_BUFFER, oldVBO);
        glBindTexture(GL_TEXTURE_2D, oldTex);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, s_winW, s_winH);
        if (oldDepth) glEnable(GL_DEPTH_TEST); if (oldBlend) glEnable(GL_BLEND); if (oldScissor) glEnable(GL_SCISSOR_TEST);
        g_inResFBO = 0;
        return;
    }
    glViewport(0, 0, s_winW, s_winH);
    glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND);
    glUseProgram(s_blitProg);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, s_resTex);
    glUniform1i(glGetUniformLocation(s_blitProg, "uTex"), 0); { GLint pl0 = glGetUniformLocation(s_blitProg, "uPrevLowTex0"); if (pl0 >= 0) { glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, s_prevLowTex); glUniform1i(pl0, 3); glActiveTexture(GL_TEXTURE0); } GLint pl1 = glGetUniformLocation(s_blitProg, "uPrevLowTex1"); if (pl1 >= 0) { glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, s_prevLowTex1); glUniform1i(pl1, 4); glActiveTexture(GL_TEXTURE0); } GLint pl = glGetUniformLocation(s_blitProg, "uPrevLowTex"); if (pl >= 0) { glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, s_prevLowTex); glUniform1i(pl, 3); glActiveTexture(GL_TEXTURE0); } } { GLint pv = glGetUniformLocation(s_blitProg, "uPrevTex"); if (pv >= 0) { glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, s_prevTex); glUniform1i(pv, 2); glActiveTexture(GL_TEXTURE0); } } { GLint ll = glGetUniformLocation(s_blitProg, "uLowTex"); if (ll >= 0) { glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, s_lowTex); glUniform1i(ll, 1); glActiveTexture(GL_TEXTURE0); } GLint lrl = glGetUniformLocation(s_blitProg, "uLowResolution"); if (lrl >= 0) glUniform2f(lrl, (float)s_lowW, (float)s_lowH); GLint ltl = glGetUniformLocation(s_blitProg, "uLowTexelSize"); if (ltl >= 0) glUniform2f(ltl, s_lowW ? 1.0f / (float)s_lowW : 0.0f, s_lowH ? 1.0f / (float)s_lowH : 0.0f); } { GLint rl = glGetUniformLocation(s_blitProg, "uResolution"); if (rl >= 0) glUniform2f(rl, (float)s_resW, (float)s_resH); GLint tl = glGetUniformLocation(s_blitProg, "uTexelSize"); if (tl >= 0) glUniform2f(tl, s_resW ? 1.0f / (float)s_resW : 0.0f, s_resH ? 1.0f / (float)s_resH : 0.0f); }
    glBindBuffer(GL_ARRAY_BUFFER, s_blitVBO);
    GLint aPos = glGetAttribLocation(s_blitProg, "aPos"); GLint aUV = glGetAttribLocation(s_blitProg, "aUV");
    glEnableVertexAttribArray(aPos); glEnableVertexAttribArray(aUV);
    glVertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glVertexAttribPointer(aUV, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(aPos); glDisableVertexAttribArray(aUV);
    // simpen s_resTex sekarang ke prev buat frame berikutnya
    { glBindFramebuffer(GL_FRAMEBUFFER, s_prevFBO); glViewport(0, 0, s_prevW, s_prevH); glUseProgram(s_prevProg); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, s_resTex); glUniform1i(glGetUniformLocation(s_prevProg, "uTex"), 0); if (s_blitVBO) { glBindBuffer(GL_ARRAY_BUFFER, s_blitVBO); GLint pp = glGetAttribLocation(s_prevProg, "aPos"); GLint pu = glGetAttribLocation(s_prevProg, "aUV"); glEnableVertexAttribArray(pp); glEnableVertexAttribArray(pu); glVertexAttribPointer(pp, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0); glVertexAttribPointer(pu, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float))); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); glDisableVertexAttribArray(pp); glDisableVertexAttribArray(pu); } }
    // RESTORE state game - balikin semua ke state sebelum blit
    glUseProgram(oldProg);
    glBindBuffer(GL_ARRAY_BUFFER, oldVBO);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, oldTex);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); g_inResFBO = 0; // JANGAN balik ke FBO, ImGui render ke layar!
    glViewport(0, 0, s_winW, s_winH); // Viewport balik ke layar penuh, bukan ke FBO kecil
    if (oldDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (oldBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (oldScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
}
static void gfx_opengl_start_frame(void) {
    frame_count++;
    { extern unsigned int g_resolutionScale; extern int g_sdlWinW, g_sdlWinH; if (g_sdlWinW > 0 && g_sdlWinH > 0) { s_winW = g_sdlWinW; s_winH = g_sdlWinH; } int sc = (int)g_resolutionScale; if (sc < 10) sc = 10; if (sc > 100) sc = 100; if (sc < 100) { int capped = sc; float eff_f; if (sc >= 100) { eff_f = 100; } else { eff_f = 100.0f * powf(0.005f, (100.0f - (float)capped) / 90.0f); if (eff_f < 0.5f) eff_f = 0.5f; } int eff = (int)eff_f; int rw = (int)(s_winW * eff_f / 100.0f); int rh = (int)(s_winH * eff_f / 100.0f); if (rw < 1) rw = 1; if (rh < 1) rh = 1; if (!s_resFBO || abs(s_resW - rw) > 0 || abs(s_resH - rh) > 0 || s_resScaleLast != eff || s_scalingFilterLast != (int)configScalingFiltering) { res_create_fbo(rw, rh); s_resScaleLast = eff; s_scalingFilterLast = (int)configScalingFiltering; } glBindFramebuffer(GL_FRAMEBUFFER, s_resFBO); g_inResFBO = 1; glViewport(0, 0, rw, rh); } else { int rw = s_winW; int rh = s_winH; if (rw < 1) rw = 1; if (rh < 1) rh = 1; if (!s_resFBO || abs(s_resW - rw) > 0 || abs(s_resH - rh) > 0 || s_resScaleLast != 100 || s_scalingFilterLast != (int)configScalingFiltering) { res_create_fbo(rw, rh); s_resScaleLast = 100; s_scalingFilterLast = (int)configScalingFiltering; } glBindFramebuffer(GL_FRAMEBUFFER, s_resFBO); g_inResFBO = 1; glViewport(0, 0, rw, rh); } }

    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE); // Must be set to clear Z-buffer
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
}

static void gfx_opengl_end_frame(void) { { extern unsigned int g_resolutionScale; if (s_resFBO) { res_blit(); } } }

static void gfx_opengl_finish_render(void) {
}

static void gfx_opengl_shutdown(void) {
}

struct GfxRenderingAPI gfx_opengl_api = {
    gfx_opengl_z_is_from_0_to_1,
    gfx_opengl_unload_shader,
    gfx_opengl_load_shader,
    gfx_opengl_create_and_load_new_shader,
    gfx_opengl_lookup_shader,
    gfx_opengl_shader_get_info,
    gfx_opengl_new_texture,
    gfx_opengl_select_texture,
    gfx_opengl_upload_texture,
    gfx_opengl_set_sampler_parameters,
    gfx_opengl_set_depth_test,
    gfx_opengl_set_depth_mask,
    gfx_opengl_set_zmode_decal,
    gfx_opengl_set_viewport,
    gfx_opengl_set_scissor,
    gfx_opengl_set_use_alpha,
    gfx_opengl_draw_triangles,
    gfx_opengl_init,
    gfx_opengl_on_resize,
    gfx_opengl_start_frame,
    gfx_opengl_end_frame,
    gfx_opengl_finish_render,
    gfx_opengl_shutdown
};

#endif // RAPI_GL
