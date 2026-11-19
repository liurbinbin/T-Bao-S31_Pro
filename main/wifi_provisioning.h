/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read WiFi credentials from NVS ("wifi_creds") and connect as station.
 *
 * @param timeout_ms  Max time to wait for an IP address after starting STA.
 * @return ESP_OK on successful association + IP; ESP_ERR_NOT_FOUND if no creds
 *         stored; other esp_err_t on connect failure / timeout.
 */
esp_err_t wifi_sta_connect_from_nvs(int timeout_ms);

/** @brief Return true when the station currently has an AP connection. */
bool wifi_sta_is_connected(void);

/* 复用唯一 Wi-Fi 生命周期；离线时仅启动 STA 无线电供 ESP-NOW 使用。 */
esp_err_t wifi_prepare_petlink(void);
esp_err_t wifi_petlink_enter(void);
void wifi_petlink_leave(void);

/**
 * @brief Consume an explicit provisioning request left by the WiFi reset UI.
 *
 * The request is cleared before returning when possible, so a power cycle
 * during setup does not force provisioning forever.
 */
bool wifi_provisioning_take_request(void);

/**
 * @brief Enter SoftAP provisioning mode. NEVER returns.
 *
 * Starts an open SoftAP "Xiaozhi-XXXX" (XXXX from the STA MAC), a tiny HTTP
 * server with a captive-portal form at http://192.168.4.1, and a DNS redirect
 * so phones auto-open the config page. The on-board screen shows the AP name
 * and the URL. When the user submits valid credentials they are persisted to
 * NVS and the chip reboots into normal (station) mode.
 */
void provisioning_enter(void);

/**
 * @brief Clear the full-screen provisioning overlay drawn on lv_layer_top().
 *
 * Safe to call even if no overlay was ever drawn.
 */
void provisioning_hide_screen(void);

/**
 * @brief Erase the stored WiFi credentials and schedule provisioning.
 *
 * Used by the UI "重置 WiFi" button: a background task erases the credentials,
 * records an explicit provisioning request, and reboots after a short delay.
 * The next boot enters SoftAP provisioning outside the LVGL task.
 */
void wifi_creds_erase_and_reboot(void);

#ifdef __cplusplus
}
#endif
