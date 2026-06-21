/*
 * ESP-AgriNet Zigbee - Gateway WiFi implementation
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * STA-mode join with captive portal fallback.
 */
#include "app_wifi.h"
#include "agrinet_log.h"
#include "agrinet_types.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "lwip/ip_addr.h"

#include <string.h>
#include <stdio.h>

#define WIFI_CONNECTED_BIT   BIT0
#define WIFI_FAIL_BIT        BIT1
#define WIFI_MAX_RETRY       5

static const char *TAG = AGRINET_LOG_TAG_WIFI;
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_count = 0;
static char s_ip_str[16] = {0};
static bool s_wifi_connected = false;
static char s_portal_ssid[33] = {0};

extern char g_wifi_ssid[32];   /* declared in app_gateway.c */
extern char g_wifi_pass[64];

/* --------------------------------------------------------------------- */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            s_wifi_connected = false;
            if (s_retry_count < WIFI_MAX_RETRY) {
                esp_wifi_connect();
                s_retry_count++;
                AG_LOGW(TAG, "retry STA connect (%d/%d)", s_retry_count, WIFI_MAX_RETRY);
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                AG_LOGE(TAG, "STA connect failed - starting captive portal");
                /* Start SoftAP for provisioning */
                esp_wifi_set_mode(WIFI_MODE_AP);
                wifi_config_t ap_cfg = {0};
                strncpy((char *)ap_cfg.ap.ssid, s_portal_ssid, sizeof(ap_cfg.ap.ssid));
                ap_cfg.ap.ssid_len = strlen(s_portal_ssid);
                ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
                ap_cfg.ap.max_connection = 2;
                esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
                esp_wifi_start();
            }
            break;
        }
        case WIFI_EVENT_AP_STACONNECTED:
            AG_LOGI(TAG, "provisioning client connected to portal");
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        AG_LOGI(TAG, "got ip: %s", s_ip_str);
        s_retry_count = 0;
        s_wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* --------------------------------------------------------------------- */
esp_err_t app_wifi_init(void)
{
    /* NVS must already be initialised by app_gateway_init() */
    esp_netif_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();

    /* Build portal SSID using last byte of MAC */
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(s_portal_ssid, sizeof(s_portal_ssid), "AgriNet-GW-%02X%02X",
             mac[4], mac[5]);

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));
    AG_LOGI(TAG, "wifi initialised, portal SSID will be \"%s\" on failure",
            s_portal_ssid);
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
esp_err_t app_wifi_start(bool captive_on_failure)
{
    if (strlen(g_wifi_ssid) == 0) {
        AG_LOGW(TAG, "no WiFi SSID configured");
        if (!captive_on_failure) {
            return ESP_FAIL;
        }
        /* Start AP-only mode */
        wifi_config_t ap_cfg = {0};
        strncpy((char *)ap_cfg.ap.ssid, s_portal_ssid, sizeof(ap_cfg.ap.ssid));
        ap_cfg.ap.ssid_len = strlen(s_portal_ssid);
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
        ap_cfg.ap.max_connection = 2;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
        ESP_ERROR_CHECK(esp_wifi_start());
        AG_LOGI(TAG, "captive portal started: %s (open)", s_portal_ssid);
        return ESP_OK;
    }

    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid, g_wifi_ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, g_wifi_pass, sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    AG_LOGI(TAG, "wifi STA connecting to \"%s\"...", g_wifi_ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    if (bits & WIFI_CONNECTED_BIT) {
        AG_LOGI(TAG, "wifi connected");
        return ESP_OK;
    }
    AG_LOGE(TAG, "wifi initial connect failed - portal fallback will continue");
    return ESP_FAIL;
}

bool app_wifi_is_connected(void) { return s_wifi_connected; }

const char *app_wifi_get_ip_str(void)
{
    return s_wifi_connected ? s_ip_str : NULL;
}

esp_err_t app_wifi_wait_connected(uint32_t timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}
