/* SPDX-License-Identifier: Apache-2.0 */
#include "motion_calibration.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include "actuator_control.h"
#include "nvs.h"

#define CALIBRATION_NAMESPACE "motion_cal"
#define CALIBRATION_KEY "config"
#define CALIBRATION_VERSION 1
#define CALIBRATION_FIXED_REVERSE_A true
#define CALIBRATION_FIXED_SWAP_MOTORS true

void motion_calibration_defaults(motion_calibration_t *config)
{
    *config = (motion_calibration_t) {
        .reverse_a = CALIBRATION_FIXED_REVERSE_A,
        .swap_motors = CALIBRATION_FIXED_SWAP_MOTORS,
        .servo_low_us = ACTUATOR_SERVO_PULSE_MIN_US,
        .servo_high_us = ACTUATOR_SERVO_PULSE_MAX_US,
    };
}

bool motion_calibration_valid(const motion_calibration_t *config)
{
    return config != NULL &&
           config->servo_low_us >= ACTUATOR_SERVO_PULSE_MIN_US &&
           config->servo_low_us <= ACTUATOR_SERVO_PULSE_MAX_US &&
           config->servo_high_us >= ACTUATOR_SERVO_PULSE_MIN_US &&
           config->servo_high_us <= ACTUATOR_SERVO_PULSE_MAX_US &&
           abs(config->servo_high_us - config->servo_low_us) >=
               MOTION_CALIBRATION_SERVO_MIN_SPAN_US;
}

esp_err_t motion_calibration_load(motion_calibration_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    motion_calibration_defaults(config);
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(CALIBRATION_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }
    uint8_t data[6] = {0};
    size_t size = sizeof(data);
    ret = nvs_get_blob(handle, CALIBRATION_KEY, data, &size);
    nvs_close(handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }
    if (size != sizeof(data) || data[0] != CALIBRATION_VERSION || (data[1] & ~7U)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    motion_calibration_t saved = {
        /* A 轮反向和电机交换由当前硬件固定，忽略旧 NVS 中的对应位。 */
        .reverse_a = CALIBRATION_FIXED_REVERSE_A,
        .reverse_b = (data[1] & 2U) != 0,
        .swap_motors = CALIBRATION_FIXED_SWAP_MOTORS,
        .servo_low_us = data[2] | ((int)data[3] << 8),
        .servo_high_us = data[4] | ((int)data[5] << 8),
    };
    if (!motion_calibration_valid(&saved)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *config = saved;
    return ESP_OK;
}

esp_err_t motion_calibration_save(const motion_calibration_t *config)
{
    if (!motion_calibration_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 显式编码，避免 bool 大小、结构体填充或编译器变化破坏 NVS 数据。 */
    const uint8_t data[6] = {
        CALIBRATION_VERSION,
        (CALIBRATION_FIXED_REVERSE_A ? 1U : 0U) |
            (config->reverse_b ? 2U : 0U) |
            (CALIBRATION_FIXED_SWAP_MOTORS ? 4U : 0U),
        config->servo_low_us & 0xff, (config->servo_low_us >> 8) & 0xff,
        config->servo_high_us & 0xff, (config->servo_high_us >> 8) & 0xff,
    };
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(CALIBRATION_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_set_blob(handle, CALIBRATION_KEY, data, sizeof(data));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

void motion_calibration_apply_wheels(const motion_calibration_t *config,
                                     motion_wheel_mix_t *mix)
{
    /* 当前板级接线固定为：交换 A/B，且 A 轮反向；不再由 UI 配置。 */
    if (CALIBRATION_FIXED_SWAP_MOTORS) {
        const float temporary = mix->motor_a;
        mix->motor_a = mix->motor_b;
        mix->motor_b = temporary;
    }
    if (CALIBRATION_FIXED_REVERSE_A) {
        mix->motor_a = -mix->motor_a;
    }
    if (config != NULL && config->reverse_b) {
        mix->motor_b = -mix->motor_b;
    }
}

int motion_calibration_servo_to_physical(const motion_calibration_t *config,
                                        int logical_us)
{
    float position = (float)(logical_us - ACTUATOR_SERVO_PULSE_MIN_US) /
                     (ACTUATOR_SERVO_PULSE_MAX_US - ACTUATOR_SERVO_PULSE_MIN_US);
    position = fmaxf(0.0f, fminf(1.0f, position));
    return config->servo_low_us + (int)lroundf(
        position * (config->servo_high_us - config->servo_low_us));
}

int motion_calibration_servo_to_logical(const motion_calibration_t *config,
                                       int physical_us)
{
    float position = (float)(physical_us - config->servo_low_us) /
                     (config->servo_high_us - config->servo_low_us);
    position = fmaxf(0.0f, fminf(1.0f, position));
    return ACTUATOR_SERVO_PULSE_MIN_US + (int)lroundf(
        position * (ACTUATOR_SERVO_PULSE_MAX_US - ACTUATOR_SERVO_PULSE_MIN_US));
}
