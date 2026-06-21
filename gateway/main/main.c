/*
 * ESP-AgriNet Zigbee - Gateway firmware entry point
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * This firmware runs on the ESP32-S3 host of the gateway. It talks to the
 * ESP32-H2 RCP firmware over UART to manage the Zigbee network, and to
 * the cloud / home controller over WiFi/MQTT.
 *
 * Hardware: ESP32-S3-DevKitC + ESP32-H2-DevKitM (connected via UART)
 *           UART0 = console, UART1 = host<->RCP link
 */
#include "app_gateway.h"
#include "app_wifi.h"
#include "app_mqtt.h"
#include "agrinet_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = AGRINET_LOG_TAG_GATEWAY;

/* Heartbeat task: 1Hz tick that drives gateway heartbeat + watchdog */
static void heartbeat_task(void *arg)
{
    (void)arg;
    while (1) {
        app_gateway_heartbeat();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    AG_LOGI(TAG, "");
    AG_LOGI(TAG, "============================================");
    AG_LOGI(TAG, "  ESP-AgriNet Zigbee Gateway v%s", AGRINET_VERSION_STRING);
    AG_LOGI(TAG, "  ESP32-S3 (host) + ESP32-H2 (RCP)");
    AG_LOGI(TAG, "============================================");
    AG_LOGI(TAG, "");

    ESP_ERROR_CHECK(app_gateway_init());

    /* Start the gateway - this brings up WiFi, MQTT and the Zigbee
     * coordinator. Note: app_gateway_start() blocks in the Zigbee main
     * loop after WiFi/MQTT init, so spawn a separate heartbeat task. */
    if (app_gateway_start() != ESP_OK) {
        AG_LOGE(TAG, "gateway start failed - entering error state");
        return;
    }

    /* Spawn the heartbeat task before the blocking zb loop would normally
     * prevent us - in this design we start the zigbee coordinator on a
     * dedicated task instead. The simpler approach used here is to spawn
     * the coordinator task inside app_gateway_start(). The heartbeat task
     * below runs only if start() returned (error case). */
    xTaskCreate(heartbeat_task, "hb", 4096, NULL, 5, NULL);

    AG_LOGI(TAG, "gateway main returning (Zigbee loop is on a separate task)");
}
