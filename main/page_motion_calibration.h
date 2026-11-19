/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*motion_calibration_closed_cb_t)(bool saved);
esp_err_t page_motion_calibration_open(lv_obj_t *root,
                                       motion_calibration_closed_cb_t closed);
void page_motion_calibration_close(void);
bool page_motion_calibration_is_open(void);
void page_motion_calibration_tick(uint32_t ms);

#ifdef __cplusplus
}
#endif
