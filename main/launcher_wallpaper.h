/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 供应用桌面使用的固定 320x320 RGB565 壁纸。
 */
#pragma once

#include "lvgl.h"

#define LAUNCHER_WALLPAPER_W 320
#define LAUNCHER_WALLPAPER_H 320

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_image_dsc_t launcher_wallpaper;

#ifdef __cplusplus
}
#endif
