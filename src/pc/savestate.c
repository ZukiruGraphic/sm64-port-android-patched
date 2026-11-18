#include "savestate.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "game/save_file.h"
#include "game/area.h"
#include "game/level_update.h"
#include "game/mario.h"
#include "fs/fs.h"
#include <GL/gl.h>
extern struct SaveBuffer gSaveBuffer;
extern void initiate_warp(s16 destLevel, s16 destArea, s16 destWarpNode, s32 arg3); // LOAD INSTANT - declare manual, gak ada di .h!
SavestateSlot gSavestateSlots[SAVESTATE_SLOTS];
static int sPendingRestore = -1;
static float sRestoreX=0, sRestoreY=0, sRestoreZ=0;
static struct SaveBuffer sSlotSave[SAVESTATE_SLOTS];
static void slot_path(int slot, char *out, int isBmp) {
    // Folder user: Android/data/com.vdavid003.sm64port/files/savestate/slot1.bin
    // Pake fs_get_write_path biar ikut sys_user_path(), bukan /sdcard/Download!
    char tmp[64];
    if (isBmp) snprintf(tmp, 64, "savestate/slot%d.bmp", slot+1);
    else snprintf(tmp, 64, "savestate/slot%d.bin", slot+1);
    const char *full = fs_get_write_path(tmp);
    if (!full) { out[0] = 0; return; }
    snprintf(out, 128, "%s", full);
}
static void write_bmp(const char *path, int w, int h) {
    (void)path; (void)w; (void)h; return; // DIMATIIN - glReadPixels di ImGui bisa FC, .bin aja yang penting!
    // glReadPixels -> BMP 24-bit, header 54 byte, tanpa lib!
    unsigned char *px = malloc((size_t)w*h*3);
    if (!px) return;
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
    FILE *f = fopen(path, "wb");
    if (!f) { free(px); return; }
    int rowSize = (w*3 + 3) & ~3;
    int imgSize = rowSize * h;
    unsigned char hdr[54] = {0};
    hdr[0]='B'; hdr[1]='M';
    *(int*)(hdr+2) = 54 + imgSize; *(int*)(hdr+10) = 54; *(int*)(hdr+14) = 40;
    *(int*)(hdr+18) = w; *(int*)(hdr+22) = h; *(short*)(hdr+26) = 1; *(short*)(hdr+28) = 24;
    *(int*)(hdr+34) = imgSize;
    fwrite(hdr, 1, 54, f);
    unsigned char *row = calloc(1, rowSize);
    for (int y=0; y<h; y++) {
        // BMP bawah-ke-atas + BGR
        unsigned char *src = px + ((h-1-y)*w*3);
        for (int x=0; x<w; x++) { row[x*3+0]=src[x*3+2]; row[x*3+1]=src[x*3+1]; row[x*3+2]=src[x*3+0]; }
        fwrite(row, 1, rowSize, f);
    }
    free(row); free(px); fclose(f);
}
void savestate_init(void) {
    for (int i=0;i<SAVESTATE_SLOTS;i++) {
        char path[128]; slot_path(i, path, 0);
        if (path[0]==0) { gSavestateSlots[i].used=false; continue; }
        if (path[0]==0) { gSavestateSlots[i].used=false; continue; }
        FILE *f = fopen(path, "rb");
        if (!f) { gSavestateSlots[i].used = false; continue; }
        // bin: SaveBuffer + level/course/area + mario pos + star + timeStr
        if (fread(&sSlotSave[i], 1, sizeof(struct SaveBuffer), f) != sizeof(struct SaveBuffer)) { fclose(f); gSavestateSlots[i].used=false; continue; }
        fread(&gSavestateSlots[i].levelNum, 1, sizeof(int), f);
        fread(&gSavestateSlots[i].courseNum, 1, sizeof(int), f);
        fread(&gSavestateSlots[i].areaIndex, 1, sizeof(int), f);
        fread(&gSavestateSlots[i].marioX, 1, sizeof(float), f);
        fread(&gSavestateSlots[i].marioY, 1, sizeof(float), f);
        fread(&gSavestateSlots[i].marioZ, 1, sizeof(float), f);
        fread(&gSavestateSlots[i].starCount, 1, sizeof(int), f);
        fread(gSavestateSlots[i].timeStr, 1, 32, f);
        fclose(f); gSavestateSlots[i].used = true; gSavestateSlots[i].texId = 0;
    }
}
void savestate_save_slot(int slot) {
    if (slot<0||slot>=SAVESTATE_SLOTS) return;
    if (!gMarioState) return; // SAVE di menu / loading = CRASH, tolak!
    if (gCurrLevelNum == 0) return; // Level 0 = menu, tolak!
    save_file_do_save(0);
    memcpy(&sSlotSave[slot], &gSaveBuffer, sizeof(struct SaveBuffer));
    gSavestateSlots[slot].used = true;
    gSavestateSlots[slot].levelNum = gCurrLevelNum;
    gSavestateSlots[slot].courseNum = gCurrCourseNum;
    gSavestateSlots[slot].areaIndex = gCurrAreaIndex;
    if (gMarioState) { gSavestateSlots[slot].marioX=gMarioState->pos[0]; gSavestateSlots[slot].marioY=gMarioState->pos[1]; gSavestateSlots[slot].marioZ=gMarioState->pos[2]; }
    gSavestateSlots[slot].starCount = save_file_get_total_star_count(0, 0, 25);
    time_t t = time(NULL); struct tm *tmv = localtime(&t);
    strftime(gSavestateSlots[slot].timeStr, 32, "%Y-%m-%d %H:%M", tmv);
    char bin[128], bmp[128]; slot_path(slot, bin, 0); slot_path(slot, bmp, 1);
    if (bin[0]==0) return;
    FILE *f = fopen(bin, "wb"); if (f) {
        fwrite(&sSlotSave[slot], 1, sizeof(struct SaveBuffer), f);
        fwrite(&gSavestateSlots[slot].levelNum, 1, sizeof(int), f);
        fwrite(&gSavestateSlots[slot].courseNum, 1, sizeof(int), f);
        fwrite(&gSavestateSlots[slot].areaIndex, 1, sizeof(int), f);
        fwrite(&gSavestateSlots[slot].marioX, 1, sizeof(float), f);
        fwrite(&gSavestateSlots[slot].marioY, 1, sizeof(float), f);
        fwrite(&gSavestateSlots[slot].marioZ, 1, sizeof(float), f);
        fwrite(&gSavestateSlots[slot].starCount, 1, sizeof(int), f);
        fwrite(gSavestateSlots[slot].timeStr, 1, 32, f);
        fclose(f);
    }
    // Screenshot otomatis 320x240 biar file kecil + preview cepet
    write_bmp(bmp, 320, 240);
    gSavestateSlots[slot].texId = 0;
}
void savestate_load_slot(int slot) {
    if (slot<0||slot>=SAVESTATE_SLOTS||!gSavestateSlots[slot].used) return;
    if (!gMarioState) return; // LOAD pas Mario gak ada = CRASH, tolak!
    memcpy(&gSaveBuffer, &sSlotSave[slot], sizeof(struct SaveBuffer));
    gSaveFileModified = 1;
    save_file_do_save(0);
    // Warp ke level slot (balik ke awal level slot, skip intro + bintang ke-save)
    // Posisi Mario persis gak di-restore (rawan crash), tapi level + save-nya balik!
    gWarpCheckpoint.courseNum = (u8)gSavestateSlots[slot].courseNum;
    gWarpCheckpoint.levelID = (u8)gSavestateSlots[slot].levelNum;
    gWarpCheckpoint.areaNum = (u8)gSavestateSlots[slot].areaIndex;
    gWarpCheckpoint.actNum = 1; gWarpCheckpoint.warpNode = 0x0A;
    // LOAD INSTANT - tanpa animasi warp / fade!
    if (gCurrLevelNum == gSavestateSlots[slot].levelNum && gCurrAreaIndex == gSavestateSlots[slot].areaIndex) {
        // Level sama: teleport langsung, 0 frame!
        gMarioState->pos[0] = gSavestateSlots[slot].marioX;
        gMarioState->pos[1] = gSavestateSlots[slot].marioY;
        gMarioState->pos[2] = gSavestateSlots[slot].marioZ;
        gMarioState->vel[0] = 0; gMarioState->vel[1] = 0; gMarioState->vel[2] = 0;
        gMarioState->forwardVel = 0;
        sPendingRestore = -1;
    } else {
        // Level beda: initiate_warp langsung, tanpa level_trigger_warp (tanpa fade 48 frame)!
        sPendingRestore = -1;
        initiate_warp(gSavestateSlots[slot].levelNum, gSavestateSlots[slot].areaIndex, 0x0A, 0);
    }
}
void savestate_tick(void) {
    if (sPendingRestore < 0) return;
    if (gCurrLevelNum != gSavestateSlots[sPendingRestore].levelNum) return;
    if (gCurrAreaIndex != gSavestateSlots[sPendingRestore].areaIndex) return;
    gMarioState->pos[0] = sRestoreX;
    gMarioState->pos[1] = sRestoreY;
    gMarioState->pos[2] = sRestoreZ;
    gMarioState->vel[0] = 0; gMarioState->vel[1] = 0; gMarioState->vel[2] = 0;
    gMarioState->forwardVel = 0;
    sPendingRestore = -1;
}
void savestate_load_preview(int slot) {
    (void)slot; return; // DIMATIIN - glGenTextures di ImGui bisa FC, preview dimatiin!
    if (slot<0||slot>=SAVESTATE_SLOTS||!gSavestateSlots[slot].used||gSavestateSlots[slot].texId) return;
    char bmp[128]; slot_path(slot, bmp, 1);
    FILE *f = fopen(bmp, "rb"); if (!f) return;
    fseek(f, 54, SEEK_SET);
    int w=320, h=240; unsigned char *px = malloc((size_t)w*h*3); if (!px) { fclose(f); return; }
    // BMP BGR bawah-ke-atas -> RGB atas-ke-bawah buat OpenGL
    unsigned char *row = malloc((w*3+3)&~3);
    for (int y=0; y<h; y++) { fread(row, 1, (w*3+3)&~3, f); unsigned char *dst = px + ((h-1-y)*w*3); for (int x=0;x<w;x++){ dst[x*3+0]=row[x*3+2]; dst[x*3+1]=row[x*3+1]; dst[x*3+2]=row[x*3+0]; } }
    free(row); fclose(f);
    GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
    free(px); gSavestateSlots[slot].texId = tex;
}
