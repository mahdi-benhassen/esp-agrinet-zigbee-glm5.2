/*
 * ESP-AgriNet Zigbee - Gateway WiFi module
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * Connects the gateway to the configured WiFi network, falls back to a
 * captive portal for provisioning when credentials are missing or
 * connection fails.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the WiFi subsystem.
 */
esp_err_t app_wifi_init(void);

/**
 * @brief Try to join the configured WiFi network. If creds are missing or
 *        the join fails, optionally start a captive portal (SoftAP) on the
 *        SSID "AgriNet-GW-XXXX" where the last 4 chars are the last byte
 *        of the gateway's MAC address.
 *
 * @param captive_on_failure whether to start a portal if connection fails
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t app_wifi_start(bool captive_on_failure);

/**
 * @brief Returns true if the gateway is currently associated to an AP.
 */
bool app_wifi_is_connected(void);

/**
 * @brief Returns the gateway's IP address as a string, or NULL when not
 *        connected. The returned pointer is valid until the next call.
 */
const char *app_wifi_get_ip_str(void);

/**
 * @brief Blocking wait (up to timeout_ms) for the WiFi to become associated.
 */
esp_err_t app_wifi_wait_connected(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
