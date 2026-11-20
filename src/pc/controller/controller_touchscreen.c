//Feel free to use it in your port too, but please keep authorship!
//Touch Controls made by: VDavid003
#ifdef TOUCH_CONTROLS
#include <ultra64.h>
#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "config.h"
#include "sm64.h"
#include "game/game_init.h"
#include "game/memory.h"
#include "game/segment2.h"
#include "gfx_dimensions.h"
#include "pc/gfx/gfx_pc.h"

#include "controller_api.h"
#include "controller_touchscreen.h"
#include <stdlib.h>
extern float configAnalogSens;

#define SCREEN_WIDTH_API 1280
#define SCREEN_HEIGHT_API 960

#define LEFT_EDGE ((int)floorf(SCREEN_WIDTH_API / 2 - SCREEN_HEIGHT_API / 2 * gfx_current_dimensions.aspect_ratio))
#define RIGHT_EDGE ((int)ceilf(SCREEN_WIDTH_API / 2 + SCREEN_HEIGHT_API / 2 * gfx_current_dimensions.aspect_ratio))

#define CORRECT_TOUCH_X(x) ((x * (RIGHT_EDGE - LEFT_EDGE)) + LEFT_EDGE)
#define CORRECT_TOUCH_Y(y) (y * SCREEN_HEIGHT_API)

#define JOYSTICK_SIZE 280

enum ControlElementType {
    Joystick,
    Button
};

struct Position {
    s32 x,y;
};

struct ControlElement {
    enum ControlElementType type;
    struct Position (*GetPos)();
    u8 touchID; //0 = not being touched, 1-255 = Finger being used
    //Joystick
    int joyX, joyY;
    //Button
    int buttonID;
    char character;
    int slideTouch;
    //Zoom baru Z+ / Z- : 0 = button game biasa, 1 = Z+ (zoom masuk), -1 = Z- (zoom keluar), buttonID = 0 biar gak ke game
    int zoomDir;
};

#include "controller_touchscreen_layouts.inc"

static struct ControlElement *ControlElements = ControlElementsDefault;
int g_editButtonMode = 0;
int g_snapGrid = 1;
float g_gridSize = 50.0f;
int g_showCenter = 1;
struct Position customPos[32];
int hasCustom[32] = {0};
static int snap_to_grid(int pos, int center, int grid) { if (!g_snapGrid || grid < 10) return pos; int off = pos - center; int snapped = center + (int)((off + grid/2) / grid) * grid; if (off < 0) snapped = center + (int)((off - grid/2) / grid) * grid; if (abs(pos - center) < grid/2) snapped = center; return snapped; }
static int ControlElementsLength = sizeof(ControlElementsDefault)/sizeof(struct ControlElement);
struct Position Touchscreen_GetPos(int i) { if (i < 0 || i >= ControlElementsLength) { struct Position z = {0, 0}; return z; } if (hasCustom[i]) return customPos[i]; return ControlElements[i].GetPos(); }

#define TRIGGER_DETECT(size) (((pos.x + size / 2 > CORRECT_TOUCH_X(event->x)) && (pos.x - size / 2 < CORRECT_TOUCH_X(event->x))) &&\
                              ((pos.y + size / 2 > CORRECT_TOUCH_Y(event->y)) && (pos.y - size / 2 < CORRECT_TOUCH_Y(event->y))))

void touch_down(struct TouchEvent* event) {
    struct Position pos;
    for(int i = 0; i < ControlElementsLength; i++) {
        if (ControlElements[i].touchID == 0) {
            pos = hasCustom[i] ? customPos[i] : ControlElements[i].GetPos();
            switch (ControlElements[i].type) {
                case Joystick:
                    if (TRIGGER_DETECT(JOYSTICK_SIZE)) {
                        ControlElements[i].touchID = event->touchID;
                        if (!g_editButtonMode) { ControlElements[i].joyX = CORRECT_TOUCH_X(event->x) - pos.x; ControlElements[i].joyY = CORRECT_TOUCH_Y(event->y) - pos.y; } // BLOCK: pas edit, jangan gerakin stick/kamera
                    }
                    break;
                case Button:
                    if (TRIGGER_DETECT(120)) {
                        ControlElements[i].touchID = event->touchID;
                    }
                    break;
            }
        }
    }
}

void touch_motion(struct TouchEvent* event) {
    struct Position pos;
    for(int i = 0; i < ControlElementsLength; i++) {
        pos = hasCustom[i] ? customPos[i] : ControlElements[i].GetPos();
        if (ControlElements[i].touchID == event->touchID) {
                if (g_editButtonMode) {
                    int nx = snap_to_grid(CORRECT_TOUCH_X(event->x), SCREEN_WIDTH_API / 2, (int)g_gridSize);
                    int ny = snap_to_grid(CORRECT_TOUCH_Y(event->y), SCREEN_HEIGHT_API / 2, (int)g_gridSize);
                    int overlap = 0;
                    for (int j = 0; j < ControlElementsLength; j++) {
                        if (j == i) continue;
                        struct Position op = hasCustom[j] ? customPos[j] : ControlElements[j].GetPos();
                        int dx = nx - op.x;
                        int dy = ny - op.y;
                        if (dx*dx + dy*dy < 60*60) { overlap = 1; break; }
                    }
                    if (overlap) continue;
                    /* debug_drag disabled */
                    customPos[i].x = nx;
                    { FILE *bf = fopen("/sdcard/Android/data/com.vdavid003.sm64port/files/user/btn_pos.txt", "w"); if (bf) { for (int k = 0; k < 13; k++) if (hasCustom[k]) fprintf(bf, "btn_pos_%d %d %d\n", k, customPos[k].x, customPos[k].y); fclose(bf); } }
                    customPos[i].y = ny;
                    hasCustom[i] = 1;
                    continue;
                }
            pos = hasCustom[i] ? customPos[i] : ControlElements[i].GetPos();
                switch (ControlElements[i].type) {
                    case Joystick:
                        ; //workaround
                        s32 x,y;
                        x = CORRECT_TOUCH_X(event->x) - pos.x;
                        y = CORRECT_TOUCH_Y(event->y) - pos.y;
                        if (pos.x + JOYSTICK_SIZE/2 < CORRECT_TOUCH_X(event->x))
                            x = JOYSTICK_SIZE/2;
                        if (pos.x - JOYSTICK_SIZE/2 > CORRECT_TOUCH_X(event->x))
                            x = -JOYSTICK_SIZE/2;
                        if (pos.y + JOYSTICK_SIZE/2 < CORRECT_TOUCH_Y(event->y))
                            y = JOYSTICK_SIZE/2;
                        if (pos.y - JOYSTICK_SIZE/2 > CORRECT_TOUCH_Y(event->y))
                            y = -JOYSTICK_SIZE/2;

                        ControlElements[i].joyX = x;
                        ControlElements[i].joyY = y;
                        break;
                    case Button:
                        if (ControlElements[i].slideTouch && !TRIGGER_DETECT(120)) {
                            ControlElements[i].slideTouch = 0;
                            ControlElements[i].touchID = 0;
                        }
                        break;
                }
        }
        else {
            switch (ControlElements[i].type) {
                case Joystick:
                    break;
                case Button:
                    if (TRIGGER_DETECT(120)) {
                        ControlElements[i].slideTouch = 1;
                        ControlElements[i].touchID = event->touchID;
                    }
                    break;
            }
        }
    }
}

static void handle_touch_up(int i) {//seperated for when the layout changes
    ControlElements[i].touchID = 0;
    switch (ControlElements[i].type) {
        case Joystick:
            ControlElements[i].joyX = 0;
            ControlElements[i].joyY = 0;
            break;
        case Button:
            break;
    }
}

void touch_up(struct TouchEvent* event) { /* debug_drag disabled */ if (g_editButtonMode) { extern void configfile_save(const char *); extern const char *configfile_name(void); configfile_save(configfile_name()); }
    struct Position pos;
    for(int i = 0; i < ControlElementsLength; i++) {
        if (ControlElements[i].touchID == event->touchID) {
            handle_touch_up(i);
        }
    }
}

ALIGNED8 static const u8 texture_button[] = {
#include "textures/touchcontrols/touch_button.rgba16.inc.c"
};

ALIGNED8 static const u8 texture_button_dark[] = {
#include "textures/touchcontrols/touch_button_dark.rgba16.inc.c"
};

//Sprite drawing code stolen from src/game/print.c

static void select_button_texture(int dark) {
    const u8 *const *glyphs = segmented_to_virtual(main_hud_lut);

    gDPPipeSync(gDisplayListHead++);
    if (!dark) {
        gDPSetTextureImage(gDisplayListHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, texture_button);
    }
    else {
        gDPSetTextureImage(gDisplayListHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, texture_button_dark);
    }
    gSPDisplayList(gDisplayListHead++, dl_hud_img_load_tex_block);
}

static void select_char_texture(char num) {
    const u8 *const *glyphs = segmented_to_virtual(main_hud_lut);

    gDPPipeSync(gDisplayListHead++);
    gDPSetTextureImage(gDisplayListHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, glyphs[num - 87]);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_load_tex_block);
}

static void DrawSprite(s32 x, s32 y, int scaling) {
    gSPTextureRectangle(gDisplayListHead++, x - (15 << scaling), y - (15 << scaling), x + (15 << scaling),
                        y + (15 << scaling), G_TX_RENDERTILE, 0, 0, 4 << (11 - scaling), 1 << (11 - scaling));
}


// SKIN DOANG - Shaolin Monks Rangga Casper, mode Xbox - V4 FINAL
// V1 gagal: pake 'A'/'B' besar, padahal layout lu 'a'/'b' kecil + gak ada MODULATE
// V4: mapping pake buttonID + zoomDir, combine dipaksa MODULATE biar warna nempel
// Urutan: base -> overlay solid -> outline lagi -> huruf. Outline gak ketimpa.
static void shaolin_skin_set_prim(int buttonID, int zoomDir, int pressed) {
    if (!pressed) {
        gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 255, 255, 255);
        return;
    }
    gDPSetCombineMode(gDisplayListHead++, G_CC_MODULATERGB, G_CC_MODULATERGB);
    if (zoomDir == 1) { gDPSetPrimColor(gDisplayListHead++, 0, 0, 82, 176, 66, 255); return; }
    if (zoomDir == -1) { gDPSetPrimColor(gDisplayListHead++, 0, 0, 232, 17, 35, 255); return; }
    if (buttonID == A_BUTTON) { gDPSetPrimColor(gDisplayListHead++, 0, 0, 82, 176, 66, 255); return; }
    if (buttonID == B_BUTTON) { gDPSetPrimColor(gDisplayListHead++, 0, 0, 232, 17, 35, 255); return; }
    if (buttonID == START_BUTTON) { gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 255, 255, 255); return; }
    if (buttonID == Z_TRIG) { gDPSetPrimColor(gDisplayListHead++, 0, 0, 180, 180, 180, 255); return; }
    if (buttonID == R_TRIG) { gDPSetPrimColor(gDisplayListHead++, 0, 0, 80, 80, 90, 255); return; }
    if (buttonID == U_CBUTTONS) { gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 185, 0, 255); return; }
    if (buttonID == D_CBUTTONS) { gDPSetPrimColor(gDisplayListHead++, 0, 0, 0, 120, 215, 255); return; }
    if (buttonID == L_CBUTTONS) { gDPSetPrimColor(gDisplayListHead++, 0, 0, 0, 120, 215, 255); return; }
    if (buttonID == R_CBUTTONS) { gDPSetPrimColor(gDisplayListHead++, 0, 0, 0, 120, 215, 255); return; }
    gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 255, 255, 255);
}
static void shaolin_skin_reset_combine(void) {
    gDPSetCombineMode(gDisplayListHead++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 255, 255, 255);
}

void render_touch_controls(void) {
    Mtx *mtx;

    mtx = alloc_display_list(sizeof(*mtx));

    if (mtx == NULL) {
        return;
    }

    guOrtho(mtx, 0.0f, SCREEN_WIDTH, 0.0f, SCREEN_HEIGHT, -10.0f, 10.0f, 1.0f);
    gSPPerspNormalize((Gfx *) (gDisplayListHead++), 0xFFFF);
    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);

    struct Position pos;
    for (int i = 0; i < ControlElementsLength; i++) {
        select_button_texture(0);
        switch (ControlElements[i].type) {
            case Joystick:
                pos = hasCustom[i] ? customPos[i] : ControlElements[i].GetPos();
                DrawSprite(pos.x, pos.y, 3);
                DrawSprite(pos.x + 4 + ControlElements[i].joyX, pos.y + 4 + ControlElements[i].joyY, 2);
                break;
            case Button: {
                int pressed = ControlElements[i].touchID ? 1 : 0;
                pos = hasCustom[i] ? customPos[i] : ControlElements[i].GetPos();
                select_button_texture(0);
                gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 255, 255, 255);
                DrawSprite(pos.x - 8, pos.y, 2);
                if (pressed) {
                    select_button_texture(1);
                    shaolin_skin_set_prim(ControlElements[i].buttonID, ControlElements[i].zoomDir, 1);
                    DrawSprite(pos.x - 8, pos.y, 2);
                }
                select_button_texture(0);
                gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 255, 255, 255);
                select_char_texture(ControlElements[i].character);
                DrawSprite(pos.x, pos.y, 1);
                shaolin_skin_reset_combine();
                break;
            }
        }
    }

    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
}

static void touchscreen_init(void) {
    { FILE *bf = fopen("/sdcard/Android/data/com.vdavid003.sm64port/files/user/btn_pos.txt", "r"); if (bf) { char line[128]; while (fgets(line, sizeof(line), bf)) { int idx, x, y; if (sscanf(line, "btn_pos_%d %d %d", &idx, &x, &y) == 3 && idx >= 0 && idx < 32) { customPos[idx].x = x; customPos[idx].y = y; hasCustom[idx] = 1; } } fclose(bf); } }
}

static void touchscreen_read(OSContPad *pad) {
    extern int g_ZoomHold;
    if (g_editButtonMode) { pad->button = 0; pad->stick_x = 0; pad->stick_y = 0; g_ZoomHold = 0; return; } // BLOCK ALL: tombol + stick + zoom + kamera, pas edit // Z+ / Z- dari virtual control, bukan DPad game, bukan Z_TRIG game
    int zoomHeld = 0; // 0 = gak ada Z+ / Z- virtual yang dipencet, 1 = Z+ dipencet, -1 = Z- dipencet
    for(int i = 0; i < ControlElementsLength; i++) {
        switch (ControlElements[i].type) {
            case Joystick:
                if (ControlElements[i].joyX || ControlElements[i].joyY) {
                    pad->stick_x = (int)((ControlElements[i].joyX + JOYSTICK_SIZE/2) * 255 / JOYSTICK_SIZE - 128) * configAnalogSens; if (pad->stick_x > 127) pad->stick_x = 127; if (pad->stick_x < -128) pad->stick_x = -128;
                    pad->stick_y = (int)((-ControlElements[i].joyY + JOYSTICK_SIZE/2) * 255 / JOYSTICK_SIZE - 128) * configAnalogSens; if (pad->stick_y > 127) pad->stick_y = 127; if (pad->stick_y < -128) pad->stick_y = -128; //inverted for some reason
                }
                break;
            case Button:
                if (ControlElements[i].touchID) {
                    pad->button |= ControlElements[i].buttonID; // Z+ / Z- buttonID = 0, jadi game gak ke-trigger, aman!
                    if (ControlElements[i].zoomDir != 0) {
                        zoomHeld = ControlElements[i].zoomDir; // Z+ = 1, Z- = -1, cuma set g_ZoomHold
                    }
                }
                break;
        }
    }
    // Touchscreen jalan duluan, jadi selalu set g_ZoomHold = zoomHeld (0 = berhenti, biar gak nyangkut)
    // ImGui jalan sesudah touchscreen, jadi kalo ImGui Z+ / Z- yang dipencet, ImGui bakal set lagi ke 1 / -1, aman!
    g_ZoomHold = zoomHeld;
}

static u32 touchscreen_rawkey(void) { //dunno what this does but I'll skip it for now
    return VK_INVALID;
}

struct ControllerAPI controller_touchscreen = {
    0,
    touchscreen_init,
    touchscreen_read,
    touchscreen_rawkey,
    NULL,
    NULL,
    NULL,
    NULL
};
#endif
