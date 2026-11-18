#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SAVESTATE_SLOTS 5
typedef struct {
    bool used;
    char timeStr[32]; // 2026-11-17 04:30
    int levelNum, courseNum, areaIndex;
    float marioX, marioY, marioZ;
    int starCount;
    unsigned int texId; // preview BMP -> OpenGL texture, 0 = belum load
} SavestateSlot;
extern SavestateSlot gSavestateSlots[SAVESTATE_SLOTS];
void savestate_init(void);
void savestate_save_slot(int slot);
void savestate_load_slot(int slot);
void savestate_load_preview(int slot);
void savestate_tick(void);
#ifdef __cplusplus
}
#endif
