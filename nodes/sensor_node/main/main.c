/*
 * ESP-AgriNet Zigbee - Sensor node firmware entry point
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * ESP32-H2 end-device firmware for greenhouse/agriculture sensor
 * monitoring. Joins the agrinet Zigbee network and periodically
 * reports temperature, humidity, pressure, illuminance, soil moisture,
 * soil temperature and CO2.
 *
 * Hardware: ESP32-H2-DevKitM-1 + BME280 + BH1750 + SCD41 + capacitive
 *           soil moisture sensor (analog) + battery (Li-ion 18650)
 *
 * Wiring:
 *   I2C: SDA=GPIO8, SCL=GPIO9
 *   Soil moisture analog: GPIO1 (ADC1_CH0)
 *   Battery voltage divider: GPIO2 (ADC1_CH1)
 */
#include "app_sensors.h"
#include "agrinet_log.h"
#include "agrinet_types.h"
#include "agrinet_clusters.h"

#include "esp_zigbee.h"
#include "esp_zigbee_nwk.h"
#include "esp_zigbee_zcl_command.h"
#include "esp_zigbee_attribute.h"
#include "esp_zigbee_endpoint.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = AGRINET_LOG_TAG_SENSOR;

#define AGRINET_SENSOR_REPORT_INTERVAL_SEC    60

/* --------------------------------------------------------------------- */
/* Zigbee callbacks                                                      */
/* --------------------------------------------------------------------- */
static void sensor_zb_stack_status(esp_zb_zdo_signal_type_t sig,
                                   esp_zb_zdo_signal_cb_params_t *params)
{
    switch (sig) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        AG_LOGI(TAG, "Zigbee stack initialised, joining network...");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        AG_LOGI(TAG, "device first start / reboot");
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (params->bdb_steering.status == ESP_OK) {
            AG_LOGI(TAG, "joined network successfully");
        } else {
            AG_LOGW(TAG, "join failed - retrying in 5s");
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        }
        break;
    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        AG_LOGW(TAG, "left network");
        break;
    default:
        AG_LOGD(TAG, "zb signal 0x%02x", sig);
        break;
    }
}

/* --------------------------------------------------------------------- */
/* Build the sensor endpoint                                             */
/* --------------------------------------------------------------------- */
static void register_sensor_endpoints(void)
{
    /* Basic + identify + power config cluster on endpoint 10 */
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_cluster_list_t *cluster_list = esp_zb_cluster_list_create();

    /* Basic cluster */
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_BATTERY_2,
    };
    esp_zb_attribute_list_t *basic = esp_zb_basic_cluster_create(&basic_cfg);
    uint8_t mfr_name[] = "ESP-AgriNet";
    esp_zb_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, mfr_name);
    uint8_t model_id[] = "Sensor-Node-H2";
    esp_zb_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, model_id);
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Identify cluster */
    esp_zb_identify_cluster_cfg_t id_cfg = {0};
    esp_zb_attribute_list_t *identify = esp_zb_identify_cluster_create(&id_cfg);
    esp_zb_cluster_list_add_identify_cluster(cluster_list, identify, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Power configuration cluster (battery %) */
    esp_zb_power_config_cluster_cfg_t pcfg = {0};
    esp_zb_attribute_list_t *power = esp_zb_power_config_cluster_create(&pcfg);
    uint8_t bat_pct = 100;
    esp_zb_cluster_add_attr(power, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID, &bat_pct);
    esp_zb_cluster_list_add_power_config_cluster(cluster_list, power, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Temperature measurement */
    esp_zb_temp_measurement_cluster_cfg_t temp_cfg = {
        .measured_value = 0,
        .min_value = -4000,  /* -40 C */
        .max_value = 12000,  /* 120 C */
    };
    esp_zb_attribute_list_t *temp_meas = esp_zb_temp_meas_cluster_create(&temp_cfg);
    esp_zb_cluster_list_add_temp_measurement_cluster(cluster_list, temp_meas,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Relative humidity measurement */
    esp_zb_humidity_measurement_cluster_cfg_t hum_cfg = {
        .measured_value = 0,
        .min_value = 0,
        .max_value = 10000,
    };
    esp_zb_attribute_list_t *hum_meas = esp_zb_humidity_meas_cluster_create(&hum_cfg);
    esp_zb_cluster_list_add_humidity_measurement_cluster(cluster_list, hum_meas,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Pressure measurement */
    esp_zb_pressure_measurement_cluster_cfg_t p_cfg = {
        .measured_value = 0,
        .min_value = 30000,  /* 300 hPa */
        .max_value = 110000, /* 1100 hPa */
    };
    esp_zb_attribute_list_t *press_meas = esp_zb_pressure_meas_cluster_create(&p_cfg);
    esp_zb_cluster_list_add_pressure_measurement_cluster(cluster_list, press_meas,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Illuminance measurement */
    esp_zb_illuminance_cluster_cfg_t ill_cfg = {
        .measured_value = 0,
        .min_value = 0,
        .max_value = 0xFFFE,
    };
    esp_zb_attribute_list_t *ill_meas = esp_zb_illuminance_cluster_create(&ill_cfg);
    esp_zb_cluster_list_add_illuminance_measurement_cluster(cluster_list, ill_meas,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Manufacturer-specific soil moisture cluster */
    esp_zb_attribute_list_t *soil = esp_zb_zcl_cluster_create(AGRINET_CLUSTER_SOIL_MOISTURE);
    esp_zb_cluster_list_add_custom_cluster(cluster_list, soil,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_MANUFACTURER_CODE);

    /* Manufacturer-specific CO2 cluster */
    esp_zb_attribute_list_t *co2 = esp_zb_zcl_cluster_create(AGRINET_CLUSTER_CO2_MEAS);
    esp_zb_cluster_list_add_custom_cluster(cluster_list, co2,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_MANUFACTURER_CODE);

    /* AgriNet configuration cluster */
    esp_zb_attribute_list_t *cfg = esp_zb_zcl_cluster_create(AGRINET_CLUSTER_AGRINET_CFG);
    esp_zb_cluster_list_add_custom_cluster(cluster_list, cfg,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_MANUFACTURER_CODE);

    /* Endpoint descriptor - HA sensor device */
    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint = AGRINET_EP_SENSOR,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, cluster_list, ep_cfg);
    esp_zb_device_register(ep_list);

    /* Now register the custom cluster attributes */
    agrinet_register_soil_moisture_cluster(AGRINET_EP_SENSOR);
    agrinet_register_co2_cluster(AGRINET_EP_SENSOR);

    agrinet_thresholds_t th = AGRINET_DEFAULT_THRESHOLDS;
    agrinet_register_config_cluster(AGRINET_EP_SENSOR, &th);

    /* Configure default reporting on standard measurement clusters */
    agrinet_configure_default_reporting(AGRINET_EP_SENSOR);

    AG_LOGI(TAG, "sensor endpoint registered on ep %d", AGRINET_EP_SENSOR);
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

    /* Wait 10 seconds for the network to be joined */
    vTaskDelay(pdMS_TO_TICKS(10000));

    while (1) {
        AG_LOGI(TAG, "starting measurement cycle");
        if (app_sensors_read(&readings) == ESP_OK) {
            app_sensors_to_snapshot(&readings, &snap);

            /* Update standard Zigbee attributes (will be reported via the
             * reporting config set in agrinet_configure_default_reporting) */
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

            esp_zb_zcl_set_attribute_val(AGRINET_EP_SENSOR,
                ESP_ZB_ZCL_CLUSTER_ID_PRESSURE_MEASUREMENT,
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                ESP_ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_VALUE_ID,
                &snap.pressure_pa, false);

            esp_zb_zcl_set_attribute_val(AGRINET_EP_SENSOR,
                ESP_ZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT,
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_VALUE_ID,
                &snap.illuminance_lux, false);

            /* Update custom clusters (they fire their own reports) */
            agrinet_update_soil_moisture(AGRINET_EP_SENSOR,
                snap.soil_moisture_centi_pct, snap.soil_temp_centideg);
            agrinet_update_co2(AGRINET_EP_SENSOR, snap.co2_ppm);

            /* Update battery percentage */
            uint8_t bat = (uint8_t)(snap.battery_pct < 0 ? 0 : snap.battery_pct);
            esp_zb_zcl_set_attribute_val(AGRINET_EP_SENSOR,
                ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
                &bat, false);

            /* Compute alert mask */
            uint8_t alert_mask = 0;
            agrinet_thresholds_t th = AGRINET_DEFAULT_THRESHOLDS;
            agrinet_read_config(AGRINET_EP_SENSOR, &th);
            if (snap.temperature_centideg > th.temp_high_centideg) alert_mask |= AGRINET_ALERT_TEMP_HIGH;
            if (snap.temperature_centideg < th.temp_low_centideg)  alert_mask |= AGRINET_ALERT_TEMP_LOW;
            if (snap.humidity_centi_pct > th.humidity_high_centi_pct) alert_mask |= AGRINET_ALERT_HUMIDITY_HIGH;
            if (snap.humidity_centi_pct < th.humidity_low_centi_pct)  alert_mask |= AGRINET_ALERT_HUMIDITY_LOW;
            if (snap.soil_moisture_centi_pct < th.soil_dry_centi_pct) alert_mask |= AGRINET_ALERT_SOIL_DRY;
            if (snap.soil_moisture_centi_pct > th.soil_wet_centi_pct) alert_mask |= AGRINET_ALERT_SOIL_WET;
            if (snap.co2_ppm > th.co2_high_ppm) alert_mask |= AGRINET_ALERT_CO2_HIGH;
            if (snap.battery_pct >= 0 && snap.battery_pct < th.battery_low_pct)
                alert_mask |= AGRINET_ALERT_BATTERY_LOW;

            /* Persist alert mask in the config cluster so the gateway can
             * read it via a ZCL read */
            esp_zb_zcl_set_attribute_val(AGRINET_EP_SENSOR,
                AGRINET_CLUSTER_AGRINET_CFG, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                AGRINET_ATTR_CFG_ALERT_MASK, &alert_mask, false);

            AG_LOGI(TAG, "report published: T=%.2fC H=%.1f%% P=%ldPa soil=%.1f%% lux=%u co2=%u bat=%d alerts=0x%02x",
                    snap.temperature_centideg / 100.0f,
                    snap.humidity_centi_pct / 100.0f,
                    (long)snap.pressure_pa,
                    snap.soil_moisture_centi_pct / 100.0f,
                    (unsigned)snap.illuminance_lux,
                    (unsigned)snap.co2_ppm,
                    (int)snap.battery_pct,
                    (unsigned)alert_mask);
        } else {
            AG_LOGW(TAG, "sensor read failed");
        }

        /* Sleep until next cycle - 5s overhead from SCD41 measurement */
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
    ESP_ERROR_CHECK(esp_zb_platform_configure(&config));

    /* End-device config */
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    register_sensor_endpoints();

    /* Register stack status callback */
    esp_zb_register_stack_status_handler(sensor_zb_stack_status);

    /* Start the sensor measurement task */
    xTaskCreate(sensor_task, "sensor", 8192, NULL, 5, NULL);

    /* Start Zigbee (blocking) */
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_main_loop_iteration();
}
