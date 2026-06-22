/*
 * ESP-AgriNet Zigbee - Sensor node firmware entry point
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * ESP32-H2 end-device firmware for greenhouse/agriculture sensor
 * monitoring. Joins the agrinet Zigbee network and periodically
 * reports temperature, humidity, pressure, illuminance, soil moisture,
 * soil temperature and CO2.
 */
#include "app_sensors.h"
#include "agrinet_log.h"
#include "agrinet_types.h"
#include "agrinet_clusters.h"

#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = AGRINET_LOG_TAG_SENSOR;

#define AGRINET_SENSOR_REPORT_INTERVAL_SEC    60

/* Zigbee end-device configuration macros (native radio on ESP32-H2) */
#define INSTALLCODE_POLICY_ENABLE       false
#define ED_AGING_TIMEOUT                ESP_ZB_ED_AGING_TIMEOUT_64MIN
#define ED_KEEP_ALIVE                   3000
#define ESP_ZB_PRIMARY_CHANNEL_MASK     ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK

#define ESP_ZB_ZED_CONFIG() { \
    .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED, \
    .install_code_policy = INSTALLCODE_POLICY_ENABLE, \
    .nwk_cfg.zed_cfg = { \
        .ed_timeout = ED_AGING_TIMEOUT, \
        .keep_alive = ED_KEEP_ALIVE, \
    }, \
}

#define ESP_ZB_DEFAULT_RADIO_CONFIG() { .radio_mode = RADIO_MODE_NATIVE, }
#define ESP_ZB_DEFAULT_HOST_CONFIG() { .host_connection_mode = HOST_CONNECTION_MODE_NONE, }

/* --------------------------------------------------------------------- */
/* Zigbee app signal handler (matches official ESP-IDF v5.2.3 pattern)   */
/* --------------------------------------------------------------------- */
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p       = signal_struct->p_app_signal;
    esp_err_t err_status   = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        AG_LOGI(TAG, "Zigbee stack initialized, joining network...");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            AG_LOGI(TAG, "Device started up in %s factory-reset mode",
                    esp_zb_bdb_is_factory_new() ? "" : "non");
            if (esp_zb_bdb_is_factory_new()) {
                AG_LOGI(TAG, "Start network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                AG_LOGI(TAG, "Device rebooted");
            }
        } else {
            AG_LOGW(TAG, "Failed to initialize Zigbee stack (status: %s)",
                    esp_err_to_name(err_status));
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            AG_LOGI(TAG, "Joined network successfully");
        } else {
            AG_LOGI(TAG, "Network steering was not successful (status: %s)",
                    esp_err_to_name(err_status));
        }
        break;
    default:
        AG_LOGI(TAG, "ZDO signal: 0x%x, status: %s", sig_type, esp_err_to_name(err_status));
        break;
    }
}

/* --------------------------------------------------------------------- */
/* Zigbee action handler for incoming ZCL commands                        */
/* --------------------------------------------------------------------- */
static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id,
                                   const void *message)
{
    AG_LOGD(TAG, "zb action 0x%02x", callback_id);
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
/* Zigbee task                                                            */
/* --------------------------------------------------------------------- */
static void esp_zb_task(void *pvParameters)
{
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    /* Create a simple sensor endpoint with basic + identify clusters */
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    /* Basic cluster */
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE,
    };
    esp_zb_attribute_list_t *basic = esp_zb_basic_cluster_create(&basic_cfg);
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Identify cluster */
    esp_zb_identify_cluster_cfg_t id_cfg = {0};
    esp_zb_attribute_list_t *identify = esp_zb_identify_cluster_create(&id_cfg);
    esp_zb_cluster_list_add_identify_cluster(cluster_list, identify, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Temperature measurement cluster */
    esp_zb_temperature_meas_cluster_cfg_t temp_cfg = {
        .measured_value = 0,
        .min_measured_value = -4000,
        .max_measured_value = 12000,
    };
    esp_zb_attribute_list_t *temp_meas = esp_zb_temperature_meas_cluster_create(&temp_cfg);
    esp_zb_cluster_list_add_temperature_meas_cluster(cluster_list, temp_meas, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Humidity measurement cluster */
    esp_zb_humidity_meas_cluster_cfg_t hum_cfg = {
        .measured_value = 0,
        .min_measured_value = 0,
        .max_measured_value = 10000,
    };
    esp_zb_attribute_list_t *hum_meas = esp_zb_humidity_meas_cluster_create(&hum_cfg);
    esp_zb_cluster_list_add_humidity_meas_cluster(cluster_list, hum_meas, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Add endpoint */
    esp_zb_ep_list_add_ep(ep_list, cluster_list, AGRINET_EP_SENSOR,
        ESP_ZB_AF_HA_PROFILE_ID, ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID);
    ESP_ERROR_CHECK(esp_zb_device_register(ep_list));

    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_main_loop_iteration();
}

/* --------------------------------------------------------------------- */
/* Sensor reporting task                                                 */
/* --------------------------------------------------------------------- */
static void sensor_task(void *arg)
{
    (void)arg;
    agrinet_sensor_readings_t readings;
    agrinet_sensor_snapshot_t snap;
    uint16_t report_interval = AGRINET_SENSOR_REPORT_INTERVAL_SEC;

    vTaskDelay(pdMS_TO_TICKS(10000));

    while (1) {
        AG_LOGI(TAG, "starting measurement cycle");
        if (app_sensors_read(&readings) == ESP_OK) {
            app_sensors_to_snapshot(&readings, &snap);

            /* Update standard Zigbee attributes */
            esp_zb_zcl_set_attribute_val(AGRINET_EP_SENSOR,
                ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
                &snap.temperature_centideg, false);

            esp_zb_zcl_set_attribute_val(AGRINET_EP_SENSOR,
                ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID,
                &snap.humidity_centi_pct, false);

            AG_LOGI(TAG, "report: T=%.2fC H=%.1f%% P=%ldPa soil=%.1f%% lux=%u co2=%u bat=%d",
                    snap.temperature_centideg / 100.0f,
                    snap.humidity_centi_pct / 100.0f,
                    (long)snap.pressure_pa,
                    snap.soil_moisture_centi_pct / 100.0f,
                    (unsigned)snap.illuminance_lux,
                    (unsigned)snap.co2_ppm,
                    (int)snap.battery_pct);
        } else {
            AG_LOGW(TAG, "sensor read failed");
        }

        uint32_t sleep_ms = (report_interval * 1000) - 5000;
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }
}

/* --------------------------------------------------------------------- */
/* App main                                                              */
/* --------------------------------------------------------------------- */
void app_main(void)
{
    AG_LOGI(TAG, "");
    AG_LOGI(TAG, "============================================");
    AG_LOGI(TAG, "  ESP-AgriNet Sensor Node v%s", AGRINET_VERSION_STRING);
    AG_LOGI(TAG, "  ESP32-H2 + BME280 + BH1750 + SCD41 + soil");
    AG_LOGI(TAG, "============================================");
    AG_LOGI(TAG, "");

    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* Sensors */
    ESP_ERROR_CHECK(app_sensors_init());

    /* Zigbee platform */
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config  = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    /* Start the sensor measurement task */
    xTaskCreate(sensor_task, "sensor", 8192, NULL, 5, NULL);

    /* Start Zigbee task */
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
