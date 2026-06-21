/*
 * ESP-AgriNet Zigbee - Logging helpers
 * Copyright (c) 2025 ESP-AgriNet Project
 */
#pragma once

#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGRINET_LOG_TAG_GENERIC   "agrinet"
#define AGRINET_LOG_TAG_GATEWAY   "agrinet.gw"
#define AGRINET_LOG_TAG_ZIGBEE    "agrinet.zb"
#define AGRINET_LOG_TAG_WIFI      "agrinet.wifi"
#define AGRINET_LOG_TAG_MQTT      "agrinet.mqtt"
#define AGRINET_LOG_TAG_SENSOR    "agrinet.sensor"
#define AGRINET_LOG_TAG_ACT       "agrinet.act"
#define AGRINET_LOG_TAG_RCP       "agrinet.rcp"
#define AGRINET_LOG_TAG_NVS       "agrinet.nvs"

#define AG_LOGI(tag, fmt, ...)   ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#define AG_LOGW(tag, fmt, ...)   ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#define AG_LOGE(tag, fmt, ...)   ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#define AG_LOGD(tag, fmt, ...)   ESP_LOGD(tag, fmt, ##__VA_ARGS__)
#define AG_LOGV(tag, fmt, ...)   ESP_LOGV(tag, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
