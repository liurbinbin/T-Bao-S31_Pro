#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "app_petlink.h"
#ifdef __cplusplus
extern "C" {
#endif
#define COMPANION_STAGE_WIDTH 256
#define COMPANION_STAGE_HEIGHT 112
void game_pet_companion_build(lv_obj_t *root, void (*back)(void));
void game_pet_companion_exit(void);
void game_pet_companion_tick(uint32_t ms);
void pet_companion_avatar(uint16_t *pixels, const app_petlink_state_t *state, uint32_t ms);
void pet_companion_reset(void);
void pet_companion_cancel(void);
void pet_companion_preview(uint8_t kind,uint32_t ms);
const char *pet_companion_dialogue(bool rover,bool english);
#ifdef __cplusplus
}
#endif
