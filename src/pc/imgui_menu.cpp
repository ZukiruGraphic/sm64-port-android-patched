#include <string>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
extern "C" {
#include "pc/configfile.h"
#include "pc/cheats.h"
static bool g_showShaderWindow = false;
extern char g_shaderNames[32][64];
extern int g_shaderCount;
extern int g_shaderEnabled[32];
extern int g_shaderActive;
extern char g_shaderFs[32][16384];
extern void post_shader_reload(void);
extern void post_shader_scan(void);
extern const char *sys_exe_path(void);
extern const char *fs_get_write_path(const char *path);
// HAPUS SAVESTATE: include dihapus
}

static bool g_imgui_visible = false;
static bool g_imgui_show_demo = false;
static int g_tab = 0;
static char sToast[64] = "";
static float sToastTimer = 0;

// Free cam drag - langsung ke game, bukan cheat
// Kamera tetep bisa digeser walau ImGui kebuka, buat debug rendering
extern "C" float g_camDragYaw = 0.0f;
extern "C" float g_camDragPitch = 0.0f;
extern float configSensH;
extern float configSensV;
extern float configAnalogSens;
extern int configInvertY;
extern "C" float g_LakituZoomDist;
extern float configZoom; // Zoom Lakitu dari Z+ / Z- baru, bukan DPad game
extern "C" int g_ZoomHold; // -1 = Z- tahan, 0 = diam, 1 = Z+ tahan
static float s_lastFingerX = -1;
static SDL_FingerID s_camFingerId = 0; // Jari kamera, biar gak ketimpa jari joystick
static float s_lastFingerY = -1;

extern "C" {
extern bool g_imgui_fullscreen;

void imgui_menu_init(SDL_Window *window, void *gl_context) {
    if (!window || !gl_context) return;
    SDL_GL_MakeCurrent(window, gl_context);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
    float ddpi = 400.0f;
    if (SDL_GetDisplayDPI(0, &ddpi, NULL, NULL) != 0) ddpi = 400.0f;
    if (ddpi <= 0) ddpi = 400.0f;
    float scale = ddpi / 160.0f;
    if (scale < 2.0f) scale = 2.0f;
    if (scale > 3.0f) scale = 3.0f;
    io.FontGlobalScale = scale;
    ImGui::GetStyle().ScaleAllSizes(scale);
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 100");
}

void imgui_menu_process_event(void *ev) {
    SDL_Event *event = (SDL_Event*)ev;
    ImGuiIO &io = ImGui::GetIO();
    if (event->type == SDL_FINGERDOWN || event->type == SDL_FINGERMOTION || event->type == SDL_FINGERUP) {
        int w = 0, h = 0;
        SDL_Window *win = SDL_GetWindowFromID(event->tfinger.windowID);
        if (win) SDL_GetWindowSize(win, &w, &h);
        if (w == 0) { w = (int)io.DisplaySize.x; h = (int)io.DisplaySize.y; }
        io.MousePos = ImVec2(event->tfinger.x * w, event->tfinger.y * h);
        if (event->type == SDL_FINGERDOWN) io.MouseDown[0] = true;
        if (event->type == SDL_FINGERUP) io.MouseDown[0] = false;

        // Free cam: drag jari di area game = muter kamera, gak perlu tombol virtual
        // Kamera tetep bisa digeser walau ImGui kebuka, buat debug rendering
        // Kalo jari di atas ImGui (WantCaptureMouse), jangan jadi drag kamera, biar bisa klik menu
        // Kalo jari di area game (walau ImGui kebuka), tetep jadi drag kamera
        extern int g_editButtonMode;
        if (g_editButtonMode) { s_camFingerId = 0; s_lastFingerX = -1; s_lastFingerY = -1; g_camDragYaw = 0; g_camDragPitch = 0; } // BLOCK: pas edit, drag area kosong jangan jadi jari kamera
        else if (event->type == SDL_FINGERDOWN) {
            // Block 40% kiri buat joystick + bedain jari pake fingerId
            // Kalo jari kamera udah ada, jangan timpa sama jari joystick / jari kedua
            if (io.WantCaptureMouse || event->tfinger.x < 0.4f || s_camFingerId != 0) {
                // Bukan jari kamera, biarin
            } else {
                s_camFingerId = event->tfinger.fingerId;
                s_lastFingerX = event->tfinger.x;
                s_lastFingerY = event->tfinger.y;
            }
        } else if (event->type == SDL_FINGERMOTION && !g_editButtonMode && s_lastFingerX >= 0 && event->tfinger.fingerId == s_camFingerId) { // BLOCK: pas edit, jangan muter kamera
            float dx = event->tfinger.x - s_lastFingerX;
            float dy = event->tfinger.y - s_lastFingerY;
            g_camDragYaw -= dx * 18000.0f * configSensH; // 12000, naik 1.5x dari 8000, biar lebih sensitif! // 8000, dari 80000 kegedean 10x = mental! // 10x (8000*10) // 3x, dikurangi dari 3.5x // 3.5x (8000*3.5), turun dari 30x, 30x kegedean // 30x (8000*30) // x10 (8000*10) // 5x (8000*5)
            g_camDragPitch += dy * 9000.0f * configSensV * (configInvertY ? -1.0f : 1.0f); // 6000, naik 1.5x dari 4000! // 4000, dari 40000! // 10x (4000*10) // 3x, dikurangi dari 3.5x // 3.5x (4000*3.5) // 30x (4000*30) // x10 (4000*10) // 5x (4000*5)
            s_lastFingerX = event->tfinger.x;
            s_lastFingerY = event->tfinger.y;
        } else if (event->type == SDL_FINGERUP) {
            // Cuma reset kalo yang diangkat itu jari kamera, bukan jari joystick
            if (event->tfinger.fingerId == s_camFingerId) {
                s_lastFingerX = -1;
                s_lastFingerY = -1;
                s_camFingerId = 0;
            }
        }
    }
    ImGui_ImplSDL2_ProcessEvent(event);
}

bool imgui_wants_capture() {
    ImGuiIO &io = ImGui::GetIO();
    if (!g_imgui_visible) {
        float W = io.DisplaySize.x;
        ImVec2 p = io.MousePos;
        if (p.x >= W - 180 && p.x <= W - 20 && p.y >= 20 && p.y <= 80) return true;
        return false;
    }
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
}

void imgui_menu_toggle() { g_imgui_visible = !g_imgui_visible; }
bool imgui_menu_is_visible() { return g_imgui_visible; }

void imgui_menu_frame() {
    // HAPUS SAVESTATE: savestate_tick dihapus
    if (sToastTimer > 0) sToastTimer -= 1.0f/60.0f;
    g_imgui_fullscreen = configWindow.fullscreen;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    {
        extern int g_editButtonMode;
        extern float g_gridSize;
        extern int g_showCenter;
        if (g_editButtonMode && g_showCenter) {
            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            ImVec2 sz = ImGui::GetIO().DisplaySize;
            float cx = sz.x * 0.5f;
            float cy = sz.y * 0.5f;
            float gs = g_gridSize;
            dl->AddLine(ImVec2(cx, 0), ImVec2(cx, sz.y), IM_COL32(255,255,255,255), 2.5f);
            dl->AddLine(ImVec2(0, cy), ImVec2(sz.x, cy), IM_COL32(255,255,255,255), 2.5f);
            for (float gx = cx + gs; gx < sz.x; gx += gs) dl->AddLine(ImVec2(gx, 0), ImVec2(gx, sz.y), IM_COL32(255,255,255,140), 1.5f);
            for (float gx = cx - gs; gx > 0; gx -= gs) dl->AddLine(ImVec2(gx, 0), ImVec2(gx, sz.y), IM_COL32(255,255,255,140), 1.5f);
            for (float gy = cy + gs; gy < sz.y; gy += gs) dl->AddLine(ImVec2(0, gy), ImVec2(sz.x, gy), IM_COL32(255,255,255,140), 1.5f);
            for (float gy = cy - gs; gy > 0; gy -= gs) dl->AddLine(ImVec2(0, gy), ImVec2(sz.x, gy), IM_COL32(255,255,255,140), 1.5f);
        }
    }
    if (sToastTimer > 0 && sToast[0]) { ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always); ImGui::Begin("##toast", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize); ImGui::Text("%s", sToast); ImGui::End(); }
    if (g_imgui_visible) {
        ImGui::SetNextWindowSize(ImVec2(900, 550), ImGuiCond_FirstUseEver);
        ImGui::Begin("SM64 Menu", &g_imgui_visible);
        ImGui::BeginChild("##sidebar", ImVec2(220, 0), true);
        const char *tabs[] = { "Graphic", "Audio", "Control", "Hacks", "System", "Debug" };
        for (int i = 0; i < 6; i++) {
            if (ImGui::Selectable(tabs[i], g_tab == i)) g_tab = i;
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##content", ImVec2(0, 0), false);
        if (g_tab == 0) {
            ImGui::Text("Graphic - sama kayak DISPLAY bawaan game");
            ImGui::Separator();
            if (ImGui::Checkbox("Fullscreen", &configWindow.fullscreen)) { configWindow.settings_changed = true; configfile_save(configfile_name()); } // Auto-save, gak pake Apply!
            ImGui::Separator();
            ImGui::Text("Zoom");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("Zoom Lakitu", &g_LakituZoomDist, 200.0f, 2000.0f, "%.0f")) { configZoom = g_LakituZoomDist; configfile_save(configfile_name()); } // Auto-save zoom!
            if (ImGui::Button("Z+ (Zoom In)")) { g_LakituZoomDist -= 50.0f; if (g_LakituZoomDist < 200.0f) g_LakituZoomDist = 200.0f; configZoom = g_LakituZoomDist; configfile_save(configfile_name()); } // Auto-save!
            if (ImGui::IsItemActive()) { g_ZoomHold = 1; }
            ImGui::SameLine();
            if (ImGui::Button("Z- (Zoom Out)")) { g_LakituZoomDist += 50.0f; if (g_LakituZoomDist > 2000.0f) g_LakituZoomDist = 2000.0f; configZoom = g_LakituZoomDist; configfile_save(configfile_name()); } // Auto-save!
            if (ImGui::IsItemActive()) { g_ZoomHold = -1; }
            ImGui::Separator();
            ImGui::Text("Resolution");
            ImGui::SetNextItemWidth(-1);
            static float s_resScale = -1.0f; if (s_resScale < 0) { extern unsigned int g_resolutionScale; s_resScale = (float)g_resolutionScale; } // SYNC: slider ngikutin yang ke-save, bukan selalu 100
            if (ImGui::SliderFloat("Resolution Scale", &s_resScale, 10.0f, 100.0f, "%.0f%%")) {
                // Step 5% - bulatin ke kelipatan 5
                s_resScale = (float)((int)(s_resScale / 5.0f + 0.5f) * 5);
                if (s_resScale < 10.0f) s_resScale = 10.0f;
                if (s_resScale > 100.0f) s_resScale = 100.0f;
                // 100% = resolusi layar penuh, turun 5% = render lebih kecil, di-upscale ke layar
                extern unsigned int g_resolutionScale; g_resolutionScale = (int)s_resScale;
                // Jangan ubah configWindow, biar gak flicker - resolusi diatur di viewport, bukan window
                configfile_save(configfile_name());
            }
            ImGui::Separator();
            if (ImGui::Checkbox("VSync", &configWindow.vsync)) { configWindow.settings_changed = true; configfile_save(configfile_name()); } // Auto-save!
            if (ImGui::Button("Shaders")) { g_showShaderWindow = true; }
            ImGui::Text("Scaling Filtering");
            { const char* scalingFilters[] = { "Linear", "Nearest" }; int tmpScaling = (int)configScalingFiltering; if (ImGui::Combo("##scaling_filter", &tmpScaling, scalingFilters, 2)) { configScalingFiltering = (unsigned int)tmpScaling; configfile_save(configfile_name()); } }
            ImGui::Text("Texture Filtering");
            const char *filters[] = { "Nearest", "Linear" };
            int tmpFilter = (int)configFiltering;
            if (ImGui::Combo("##filter", &tmpFilter, filters, 2)) { configFiltering = (unsigned int)tmpFilter; configfile_save(configfile_name()); } // Auto-save!
            if (ImGui::Checkbox("HUD", &configHUD)) { configfile_save(configfile_name()); } // Auto-save!
            ImGui::Spacing();
            ImGui::Text("Auto-save: Graphic langsung ke-save, gak pake Apply!");
        }
        // Shader Manager window baru
        if (g_showShaderWindow) {
            if (ImGui::Begin("Shaders", &g_showShaderWindow)) {
                ImGui::Text("Shaders folder: files/shaders/ + files/user/shaders/");
                if (ImGui::Button("Reload")) { post_shader_reload(); }
                ImGui::SameLine();
                if (ImGui::Button("Scan")) { post_shader_scan(); }
                ImGui::SameLine();
                if (ImGui::Button("Close")) { g_showShaderWindow = false; }
                ImGui::Separator();
                ImGui::Text("List shaders (nama ngikutin nama file):");
                for (int i = 0; i < g_shaderCount; i++) {
                    char label[128]; snprintf(label, sizeof(label), "%s##shader%d", g_shaderNames[i], i);
                    bool en = g_shaderEnabled[i] != 0;
                    if (ImGui::Checkbox(label, &en)) { g_shaderEnabled[i] = en ? 1 : 0; if (en) g_shaderActive = i; else if (g_shaderActive == i) { g_shaderActive = -1; for (int k = 0; k < g_shaderCount; k++) if (g_shaderEnabled[k]) { g_shaderActive = k; break; } } post_shader_reload(); }
                    ImGui::SameLine();
                    if (ImGui::Button(("Up##up"+std::to_string(i)).c_str())) { if (i > 0) { char tmp[64]; snprintf(tmp, sizeof(tmp), "%s", g_shaderNames[i]); snprintf(g_shaderNames[i], 64, "%s", g_shaderNames[i-1]); snprintf(g_shaderNames[i-1], 64, "%s", tmp); int te = g_shaderEnabled[i]; g_shaderEnabled[i] = g_shaderEnabled[i-1]; g_shaderEnabled[i-1] = te; { char tmpFs[16384]; snprintf(tmpFs, sizeof(tmpFs), "%s", g_shaderFs[i]); snprintf(g_shaderFs[i], sizeof(g_shaderFs[i]), "%s", g_shaderFs[i-1]); snprintf(g_shaderFs[i-1], sizeof(g_shaderFs[i-1]), "%s", tmpFs); } if (g_shaderActive == i) g_shaderActive = i-1; else if (g_shaderActive == i-1) g_shaderActive = i; } post_shader_reload(); }
                    ImGui::SameLine();
                    if (ImGui::Button(("Down##down"+std::to_string(i)).c_str())) { if (i < g_shaderCount - 1) { char tmp[64]; snprintf(tmp, sizeof(tmp), "%s", g_shaderNames[i]); snprintf(g_shaderNames[i], 64, "%s", g_shaderNames[i+1]); snprintf(g_shaderNames[i+1], 64, "%s", tmp); int te = g_shaderEnabled[i]; g_shaderEnabled[i] = g_shaderEnabled[i+1]; g_shaderEnabled[i+1] = te; { char tmpFs[16384]; snprintf(tmpFs, sizeof(tmpFs), "%s", g_shaderFs[i]); snprintf(g_shaderFs[i], sizeof(g_shaderFs[i]), "%s", g_shaderFs[i+1]); snprintf(g_shaderFs[i+1], sizeof(g_shaderFs[i+1]), "%s", tmpFs); } if (g_shaderActive == i) g_shaderActive = i+1; else if (g_shaderActive == i+1) g_shaderActive = i; } post_shader_reload(); }
                    ImGui::SameLine();
                    { char delLabel[64]; snprintf(delLabel, sizeof(delLabel), "\xF0\x9F\x97\x91##del%d", i); if (ImGui::Button(delLabel)) { char dp1[1024]; snprintf(dp1, sizeof(dp1), "%s/shaders/%s", sys_exe_path(), g_shaderNames[i]); remove(dp1); char dp2[1024]; snprintf(dp2, sizeof(dp2), "%s", fs_get_write_path("shaders")); char fulld[1024]; snprintf(fulld, sizeof(fulld), "%s/%s", dp2, g_shaderNames[i]); remove(fulld); if (g_shaderActive == i) g_shaderActive = -1; post_shader_scan(); post_shader_reload(); } }
                }
                ImGui::Separator();
                ImGui::Text("Add new shader:");
                static char newName[64] = {0};
                ImGui::InputText("##newshader", newName, sizeof(newName));
                ImGui::SameLine();
                if (ImGui::Button("Add")) { if (newName[0]) { char path[1024]; snprintf(path, sizeof(path), "%s/shaders/%s.glsl", sys_exe_path(), newName); FILE *w = fopen(path, "w"); if (w) { const char *def = "precision mediump float;\nvarying vec2 vUV;\nuniform sampler2D uTex;\nuniform vec2 uResolution;\nuniform vec2 uTexelSize;\nuniform sampler2D uLowTex;\nuniform sampler2D uPrevLowTex0;\nuniform sampler2D uPrevLowTex1;\nvoid main() {\n  vec4 c = texture2D(uTex, vUV);\n  // uResolution = resolusi layar, uTexelSize = 1.0/uResolution (buat AA/sharpen)\n  // uLowTex = frame low-res 6x3 (buat bloom murah)\n  // uPrevLowTex0 = low-res 1 frame lalu, uPrevLowTex1 = 2 frame lalu (buat ghost/motionblur)\n  // contoh bloom: vec3 bloom = texture2D(uLowTex, vUV).rgb;\n  gl_FragColor = c;\n}\n"; fputs(def, w); fclose(w); } newName[0] = 0; post_shader_scan(); } }
                ImGui::Text("Edit file-nya di ZArchiver, terus Reload, gak perlu relog.");
            }
            ImGui::End();
        }
        if (g_tab == 1) {
            ImGui::Text("Audio - sama kayak AUDIO bawaan game");
            ImGui::Separator();
            int tmpMaster = (int)configMasterVolume;
            int tmpMusic = (int)configMusicVolume;
            int tmpSfx = (int)configSfxVolume;
            int tmpEnv = (int)configEnvVolume;
            ImGui::Text("Master Volume");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##master", &tmpMaster, 0, 128)) { configMasterVolume = (unsigned int)tmpMaster; configfile_save(configfile_name()); }
            ImGui::Text("Music Volume");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##music", &tmpMusic, 0, 128)) { configMusicVolume = (unsigned int)tmpMusic; configfile_save(configfile_name()); }
            ImGui::Text("SFX Volume");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##sfx", &tmpSfx, 0, 128)) { configSfxVolume = (unsigned int)tmpSfx; configfile_save(configfile_name()); }
            ImGui::Text("Env Volume");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##env", &tmpEnv, 0, 128)) { configEnvVolume = (unsigned int)tmpEnv; configfile_save(configfile_name()); }
        } else if (g_tab == 2) {
            ImGui::Text("Control - Camera Sensitivity");
            ImGui::Separator();
            ImGui::Text("Horizontal Sensitivity");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##sensH", &configSensH, 0.1f, 5.0f, "%.1f")) { configfile_save(configfile_name()); } // Auto-save!
            ImGui::Text("Vertical Sensitivity");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##sensV", &configSensV, 0.1f, 5.0f, "%.1f")) { configfile_save(configfile_name()); } // Auto-save!
            ImGui::Text("Default 1.0 = 18000 / 9000, Max 5.0");
            ImGui::Separator();
            ImGui::Text("Analog Stick Sensitivity");
            ImGui::SetNextItemWidth(-1);
            float analogSens = configAnalogSens;
            if (ImGui::SliderFloat("##analogSens", &configAnalogSens, 0.5f, 3.0f, "%.1f")) { configfile_save(configfile_name()); }
            ImGui::Text("Invert Camera Y");
            bool invertY = configInvertY != 0;
            if (ImGui::Checkbox("Invert Y", &invertY)) { configInvertY = invertY ? 1 : 0; configfile_save(configfile_name()); }
            ImGui::Separator();
            ImGui::Text("Touchscreen Button");
            ImGui::SetNextItemWidth(-1);
            static float touchScale = 1.0f;
            ImGui::Text("Size");
            if (ImGui::SliderFloat("##touchScale", &touchScale, 0.5f, 1.5f, "%.1f")) { configfile_save(configfile_name()); }
            ImGui::SetNextItemWidth(-1);
            static float touchAlpha = 1.0f;
            ImGui::Text("Opacity");
            if (ImGui::SliderFloat("##touchAlpha", &touchAlpha, 0.3f, 1.0f, "%.1f")) { configfile_save(configfile_name()); }
            ImGui::Text("1.0 = normal");
            ImGui::Separator();
            ImGui::Text("Edit Button Position");
            extern int g_editButtonMode;
            extern int g_snapGrid;
            extern float g_gridSize;
            extern int g_showCenter;
            if (g_editButtonMode == 0) {
                if (ImGui::Button("Edit Button Position")) { g_editButtonMode = 1; }
            } else {
                ImGui::Text("Mode Edit: drag tombol di layar");
                if (ImGui::Button("Done")) { g_editButtonMode = 0; configfile_save(configfile_name()); }
            }
            ImGui::Text("Snap to Grid");
            {
                bool snapGrid = g_snapGrid != 0;
                if (ImGui::Checkbox("Snap to Grid", &snapGrid)) { g_snapGrid = snapGrid ? 1 : 0; configfile_save(configfile_name()); }
            }
            ImGui::Text("Grid Size");
            ImGui::SetNextItemWidth(-1);
            {
                float gridSize = g_gridSize;
                if (ImGui::SliderFloat("##gridSize", &gridSize, 10.0f, 100.0f, "%.0f")) { g_gridSize = gridSize; configfile_save(configfile_name()); }
            }
            ImGui::Text("Show Center Lines");
            {
                bool showCenter = g_showCenter != 0;
                if (ImGui::Checkbox("Show Center Lines", &showCenter)) { g_showCenter = showCenter ? 1 : 0; configfile_save(configfile_name()); }
            }
            if (ImGui::Button("Reset Position")) { remove("/sdcard/Android/data/com.vdavid003.sm64port/files/user/btn_pos.txt"); extern int hasCustom[]; for (int i = 0; i < 32; i++) hasCustom[i] = 0; g_snapGrid = 1; g_gridSize = 50.0f; g_showCenter = 1; configfile_save(configfile_name()); }
        } else if (g_tab == 3) {
            ImGui::Text("Hacks - sama kayak CHEATS bawaan game");
            ImGui::Separator();
            ImGui::Checkbox("Enable Cheats", &Cheats.EnableCheats);
            ImGui::Checkbox("Fly (hold A)", &Cheats.MoonJump);
            ImGui::Checkbox("God Mode", &Cheats.GodMode);
            ImGui::Checkbox("Infinite Lives", &Cheats.InfiniteLives);
            ImGui::Checkbox("Super Speed", &Cheats.SuperSpeed);
            ImGui::Checkbox("Responsive", &Cheats.Responsive);
            ImGui::Checkbox("Exit Anywhere", &Cheats.ExitAnywhere);
            ImGui::Checkbox("Huge Mario", &Cheats.HugeMario);
            ImGui::Checkbox("Tiny Mario", &Cheats.TinyMario);
        } else if (g_tab == 4) {
            ImGui::Text("System");
            ImGui::Separator();
            ImGui::Text("SM64 Port Android + ImGui");
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            if (ImGui::Button("Close Menu")) g_imgui_visible = false;
        } else if (g_tab == 5) {
            ImGui::Text("Debug");
            ImGui::Separator();
            ImGui::Text("Demo window taro sini");
            if (ImGui::Button("Open Demo Window")) g_imgui_show_demo = true;
            if (ImGui::Button("Close Demo Window")) g_imgui_show_demo = false;
            ImGui::Text("Camera drag: %.1f", g_camDragYaw);
        }
        ImGui::EndChild();
        ImGui::End();
        if (g_imgui_show_demo) {
            ImGui::ShowDemoWindow(&g_imgui_show_demo);
        }
    } else {
        ImDrawList *dl = ImGui::GetForegroundDrawList();
        ImGuiIO &io = ImGui::GetIO();
        float W = io.DisplaySize.x;
        const char *label = "SHOW";
        ImVec2 size = ImVec2(160, 60);
        ImVec2 pos = ImVec2(W - 180, 20);
        ImVec2 mp = io.MousePos;
        bool hover = (mp.x >= pos.x && mp.x <= pos.x + size.x && mp.y >= pos.y && mp.y <= pos.y + size.y);
        bool clicked = hover && ImGui::IsMouseClicked(0);
        ImU32 col = hover ? IM_COL32(80, 140, 220, 255) : IM_COL32(50, 110, 190, 255);
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), col, 8.0f);
        ImVec2 ts = ImGui::CalcTextSize(label);
        ImVec2 tp = ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + (size.y - ts.y) * 0.5f);
        dl->AddText(tp, IM_COL32(255, 255, 255, 255), label);
        if (clicked) g_imgui_visible = true;
    }
    // Overlay ZU / ZD DISABLE - FORCE CLOSE PAS OPEN, DisplaySize masih 0 pas baru open + NoMove + Always bikin ImGui crash
    // Balikin ke yang udah aman: virtual P / M + ImGui menu Z+ / Z- di Tab Debug, overlay dimatiin dulu!
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void imgui_menu_shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

} // extern "C"
