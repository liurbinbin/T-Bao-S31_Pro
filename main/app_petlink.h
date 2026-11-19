#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t phase;
    bool bound, connected, active, confirmed, locked, fault;
    uint32_t code;
    int16_t linear, angular, battery;
    uint8_t stage, form, nature, event;
    esp_err_t error;
    bool mismatch;
    uint8_t reason, unpair, event_origin, event_result;
    uint32_t generation, event_revision, event_id, event_seed, event_started;
    uint32_t tx_failed, rx_dropped, reconnects, ack_age;
    bool input_paused;
    uint8_t handshake;
    uint32_t business_age, auth_failed, tx_nomem, poll_max_gap, ack_seq;
    esp_err_t last_tx_error;
} app_petlink_state_t;
/* 所有耗时初始化、协商和 NVS 操作均由专用 worker 执行。 */
esp_err_t app_petlink_open(void);
void app_petlink_close(void);
void app_petlink_pair(void);
void app_petlink_confirm(void);
void app_petlink_stop(void);
void app_petlink_forget(void);
void app_petlink_event(uint8_t event);
void app_petlink_get_state(app_petlink_state_t *state);
#ifdef __cplusplus
}
#endif
