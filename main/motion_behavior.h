#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MOTION_BEHAVIOR_ACTION_SPIN_360 = 0,
    MOTION_BEHAVIOR_ACTION_GREETING,
    MOTION_BEHAVIOR_ACTION_WIGGLE,
    MOTION_BEHAVIOR_ACTION_NOD,
} motion_behavior_action_t;

esp_err_t motion_behavior_init(void);
/* 仅控制小智的语音、MCP、表情动作，不影响手动、Web 或人脸跟随。 */
void motion_behavior_set_xiaozhi_enabled(bool enabled);
void motion_behavior_submit_manual(float linear, float angular);
void motion_behavior_submit_manual_control(float linear,
                                           float angular,
                                           int servo_pulse_us,
                                           bool servo_enabled);
void motion_behavior_submit_manual_servo(int servo_pulse_us);
void motion_behavior_release_manual(void);
void motion_behavior_submit_web(float linear, float angular);
void motion_behavior_submit_web_control(float linear,
                                        float angular,
                                        int servo_pulse_us,
                                        bool servo_enabled);
void motion_behavior_release_web(void);
esp_err_t motion_behavior_submit_mcp(float linear,
                                     float angular,
                                     uint32_t duration_ms);
esp_err_t motion_behavior_submit_mcp_servo(int servo_pulse_us,
                                            uint32_t hold_ms);
void motion_behavior_on_user_text(const char *text);
void motion_behavior_on_emotion(const char *emotion);
esp_err_t motion_behavior_start_spin_360(bool clockwise);
esp_err_t motion_behavior_start_named(motion_behavior_action_t action);
/* 页面发起的动作归 TOUCH 所有，不依赖小智 MCP 是否启用。 */
esp_err_t motion_behavior_start_manual_named(motion_behavior_action_t action);

#ifdef __cplusplus
}
#endif
