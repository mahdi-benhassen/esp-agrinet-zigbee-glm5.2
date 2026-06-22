/*
 * ESP-AgriNet Zigbee - Custom Cluster Implementations
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * Implements the manufacturer-specific clusters (soil moisture, CO2,
 * agrinet config) and reporting helpers used by both the sensor node and
 * the gateway.
 */
#include "agrinet_clusters.h"
#include "agrinet_log.h"
#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"
#include <string.h>

static const char *TAG = AGRINET_LOG_TAG_ZIGBEE;

/* Attribute default values used on cluster creation. */
#define SOIL_MOISTURE_MIN_DEFAULT    0
#define SOIL_MOISTURE_MAX_DEFAULT    10000
#define SOIL_TEMP_MIN_DEFAULT        -4000
#define SOIL_TEMP_MAX_DEFAULT        12000
#define SOIL_TOLERANCE_DEFAULT       100

#define CO2_MIN_DEFAULT              0
#define CO2_MAX_DEFAULT              10000
#define CO2_TOLERANCE_DEFAULT        50

/* --------------------------------------------------------------------- */
/* Soil moisture cluster                                                 */
/* --------------------------------------------------------------------- */
esp_err_t agrinet_register_soil_moisture_cluster(uint8_t endpoint)
{
    AG_LOGI(TAG, "soil moisture cluster registered on ep %d (stub)", endpoint);
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
/* CO2 cluster                                                            */
/* --------------------------------------------------------------------- */
esp_err_t agrinet_register_co2_cluster(uint8_t endpoint)
{
    AG_LOGI(TAG, "CO2 cluster registered on ep %d (stub)", endpoint);
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
/* AgriNet config cluster                                                 */
/* --------------------------------------------------------------------- */
esp_err_t agrinet_register_config_cluster(uint8_t endpoint,
                                          const agrinet_thresholds_t *defaults)
{
    if (defaults == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    AG_LOGI(TAG, "agrinet cfg cluster registered on ep %d (stub)", endpoint);
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
/* Attribute updates                                                      */
/* --------------------------------------------------------------------- */
esp_err_t agrinet_update_soil_moisture(uint8_t endpoint,
                                       int16_t soil_moisture_centi_pct,
                                       int16_t soil_temp_centideg)
{
    esp_zb_zcl_set_attribute_val(endpoint,
                                 AGRINET_CLUSTER_SOIL_MOISTURE,
                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 AGRINET_ATTR_SOIL_MOISTURE_MEAS_VALUE,
                                 &soil_moisture_centi_pct, false);
    esp_zb_zcl_set_attribute_val(endpoint,
                                 AGRINET_CLUSTER_SOIL_MOISTURE,
                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 AGRINET_ATTR_SOIL_TEMP_MEAS_VALUE,
                                 &soil_temp_centideg, false);
    return ESP_OK;
}

esp_err_t agrinet_update_co2(uint8_t endpoint, uint16_t co2_ppm)
{
    esp_zb_zcl_set_attribute_val(endpoint,
                                 AGRINET_CLUSTER_CO2_MEAS,
                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 AGRINET_ATTR_CO2_MEAS_VALUE,
                                 &co2_ppm, false);
    return ESP_OK;
}

esp_err_t agrinet_update_config(uint8_t endpoint, const agrinet_thresholds_t *th)
{
    if (th == NULL) return ESP_ERR_INVALID_ARG;
    int16_t  i16;
    uint16_t u16;
    uint8_t  u8;

    i16 = th->temp_high_centideg;
    esp_zb_zcl_set_attribute_val(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_ATTR_CFG_TEMP_HIGH, &i16, false);
    i16 = th->temp_low_centideg;
    esp_zb_zcl_set_attribute_val(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_ATTR_CFG_TEMP_LOW, &i16, false);
    i16 = th->humidity_high_centi_pct;
    esp_zb_zcl_set_attribute_val(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_ATTR_CFG_HUM_HIGH, &i16, false);
    i16 = th->humidity_low_centi_pct;
    esp_zb_zcl_set_attribute_val(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_ATTR_CFG_HUM_LOW, &i16, false);
    i16 = th->soil_dry_centi_pct;
    esp_zb_zcl_set_attribute_val(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_ATTR_CFG_SOIL_DRY, &i16, false);
    i16 = th->soil_wet_centi_pct;
    esp_zb_zcl_set_attribute_val(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_ATTR_CFG_SOIL_WET, &i16, false);
    u16 = th->co2_high_ppm;
    esp_zb_zcl_set_attribute_val(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_ATTR_CFG_CO2_HIGH, &u16, false);
    u8 = th->battery_low_pct;
    esp_zb_zcl_set_attribute_val(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_ATTR_CFG_BAT_LOW, &u8, false);

    return ESP_OK;
}

esp_err_t agrinet_read_config(uint8_t endpoint, agrinet_thresholds_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    AG_LOGW(TAG, "agrinet_read_config: returning defaults (stub)");
    *out = (agrinet_thresholds_t)AGRINET_DEFAULT_THRESHOLDS;
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
/* Reporting configuration helper                                         */
/* --------------------------------------------------------------------- */
esp_err_t agrinet_configure_default_reporting(uint8_t endpoint)
{
    AG_LOGI(TAG, "default reporting configured on ep %d (stub)", endpoint);
    return ESP_OK;
}
