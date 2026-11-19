/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "motion_math.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOTION_CALIBRATION_SERVO_MIN_SPAN_US 100

typedef struct {
    /* 当前板级接线固定为 reverse_a=true、swap_motors=true；字段保留用于兼容旧 NVS。 */
    bool reverse_a;
    bool reverse_b;
    bool swap_motors;
    int servo_low_us;
    int servo_high_us;
} motion_calibration_t;

void motion_calibration_defaults(motion_calibration_t *config);
bool motion_calibration_valid(const motion_calibration_t *config);
esp_err_t motion_calibration_load(motion_calibration_t *config);
esp_err_t motion_calibration_save(const motion_calibration_t *config);
void motion_calibration_apply_wheels(const motion_calibration_t *config,
                                     motion_wheel_mix_t *mix);
/* 控制器继续使用逻辑 1100..1900us，只在硬件输出边界转换至校准范围。 */
int motion_calibration_servo_to_physical(const motion_calibration_t *config,
                                        int logical_us);
int motion_calibration_servo_to_logical(const motion_calibration_t *config,
                                       int physical_us);

#ifdef __cplusplus
}
#endif
