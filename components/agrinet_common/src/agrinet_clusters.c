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
#include "esp_zigbee.h"
#include "esp_zigbee_cluster.h"
#include "esp_zigbee_attribute.h"
#include "esp_zigbee_zcl_command.h"
#include <string.h>

/* Attribute default values used on cluster creation. */
#define SOIL_MOISTURE_MIN_DEFAULT    0       /* 0 %   * 100   */
#define SOIL_MOISTURE_MAX_DEFAULT    10000   /* 100 % * 100   */
#define SOIL_TEMP_MIN_DEFAULT        -4000   /* -40.0 C * 100 */
#define SOIL_TEMP_MAX_DEFAULT        12000   /* 120.0 C * 100 */
#define SOIL_TOLERANCE_DEFAULT       100     /* 1 % * 100    */

#define CO2_MIN_DEFAULT              0
#define CO2_MAX_DEFAULT              10000
#define CO2_TOLERANCE_DEFAULT        50

static const char *TAG = AGRINET_LOG_TAG_ZIGBEE;

/* --------------------------------------------------------------------- */
/* Internal helper macros for compact attribute definitions.             */
/* --------------------------------------------------------------------- */
#define DEFINE_ATTR_U16(_id, _val) \
    { .id = (_id), .data_type = ESP_ZB_ZCL_ATTR_TYPE_U16, .access = ESP_ZB_ZCL_ATTR_ACCESS_READING, .value.u16 = (_val) }

#define DEFINE_ATTR_I16(_id, _val) \
    { .id = (_id), .data_type = ESP_ZB_ZCL_ATTR_TYPE_S16, .access = ESP_ZB_ZCL_ATTR_ACCESS_READING, .value.s16 = (_val) }

#define DEFINE_ATTR_U8(_id, _val)  \
    { .id = (_id), .data_type = ESP_ZB_ZCL_ATTR_TYPE_U8,  .access = ESP_ZB_ZCL_ATTR_ACCESS_READING, .value.u8  = (_val) }

/* --------------------------------------------------------------------- */
/* Soil moisture cluster                                                 */
/* --------------------------------------------------------------------- */
esp_err_t agrinet_register_soil_moisture_cluster(uint8_t endpoint)
{
    esp_zb_attribute_list_t *cluster = esp_zb_cluster_list_get_cluster_by_id(
        esp_zb_ep_list_get_cluster_list(esp_zb_ep_list_get_endpoint(endpoint)),
        AGRINET_CLUSTER_SOIL_MOISTURE, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    /* The cluster is already attached via esp_zb_custom_cluster_add(); we
     * only need to ensure attributes exist. If the cluster wasn't created
     * yet, return an error so the caller can attach it first. */
    if (cluster == NULL) {
        AG_LOGE(TAG, "soil moisture cluster not attached on ep %d", endpoint);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;
    ret |= esp_zb_cluster_add_attr(cluster,
        AGRINET_ATTR_SOIL_MOISTURE_MEAS_VALUE, &(int16_t){0});
    ret |= esp_zb_cluster_add_attr(cluster,
        AGRINET_ATTR_SOIL_MOISTURE_MIN_VALUE, &(int16_t){SOIL_MOISTURE_MIN_DEFAULT});
    ret |= esp_zb_cluster_add_attr(cluster,
        AGRINET_ATTR_SOIL_MOISTURE_MAX_VALUE, &(int16_t){SOIL_MOISTURE_MAX_DEFAULT});
    ret |= esp_zb_cluster_add_attr(cluster,
        AGRINET_ATTR_SOIL_MOISTURE_TOLERANCE, &(int16_t){SOIL_TOLERANCE_DEFAULT});
    ret |= esp_zb_cluster_add_attr(cluster,
        AGRINET_ATTR_SOIL_TEMP_MEAS_VALUE, &(int16_t){0});
    ret |= esp_zb_cluster_add_attr(cluster,
        AGRINET_ATTR_SOIL_TEMP_MIN_VALUE, &(int16_t){SOIL_TEMP_MIN_DEFAULT});
    ret |= esp_zb_cluster_add_attr(cluster,
        AGRINET_ATTR_SOIL_TEMP_MAX_VALUE, &(int16_t){SOIL_TEMP_MAX_DEFAULT});
    ret |= esp_zb_cluster_add_attr(cluster,
        AGRINET_ATTR_SOIL_TEMP_TOLERANCE, &(int16_t){SOIL_TOLERANCE_DEFAULT});

    if (ret != ESP_OK) {
        AG_LOGE(TAG, "failed to add soil moisture attrs on ep %d", endpoint);
        return ret;
    }
    AG_LOGI(TAG, "soil moisture cluster registered on ep %d", endpoint);
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
/* CO2 cluster                                                            */
/* --------------------------------------------------------------------- */
esp_err_t agrinet_register_co2_cluster(uint8_t endpoint)
{
    esp_zb_attribute_list_t *cluster = esp_zb_cluster_list_get_cluster_by_id(
        esp_zb_ep_list_get_cluster_list(esp_zb_ep_list_get_endpoint(endpoint)),
        AGRINET_CLUSTER_CO2_MEAS, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    if (cluster == NULL) {
        AG_LOGE(TAG, "CO2 cluster not attached on ep %d", endpoint);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;
    ret |= esp_zb_cluster_add_attr(cluster,
        AGRINET_ATTR_CO2_MEAS_VALUE, &(uint16_t){0});
    ret |= esp_zb_cluster_add_attr(cluster,
        AGRINET_ATTR_CO2_MIN_MEASURED, &(uint16_t){CO2_MIN_DEFAULT});
    ret |= esp_zb_cluster_add_attr(cluster,
        AGRINET_ATTR_CO2_MAX_MEASURED, &(uint16_t){CO2_MAX_DEFAULT});
    ret |= esp_zb_cluster_add_attr(cluster,
        AGRINET_ATTR_CO2_TOLERANCE, &(uint16_t){CO2_TOLERANCE_DEFAULT});

    if (ret != ESP_OK) {
        AG_LOGE(TAG, "failed to add CO2 attrs on ep %d", endpoint);
        return ret;
    }
    AG_LOGI(TAG, "CO2 cluster registered on ep %d", endpoint);
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

    esp_zb_attribute_list_t *cluster = esp_zb_cluster_list_get_cluster_by_id(
        esp_zb_ep_list_get_cluster_list(esp_zb_ep_list_get_endpoint(endpoint)),
        AGRINET_CLUSTER_AGRINET_CFG, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    if (cluster == NULL) {
        AG_LOGE(TAG, "agrinet cfg cluster not attached on ep %d", endpoint);
        return ESP_ERR_INVALID_STATE;
    }

    int16_t  tmp_i16;
    uint16_t tmp_u16;
    uint8_t  tmp_u8;
    esp_err_t ret = ESP_OK;

    tmp_i16 = defaults->temp_high_centideg;
    ret |= esp_zb_cluster_add_attr(cluster, AGRINET_ATTR_CFG_TEMP_HIGH, &tmp_i16);
    tmp_i16 = defaults->temp_low_centideg;
    ret |= esp_zb_cluster_add_attr(cluster, AGRINET_ATTR_CFG_TEMP_LOW, &tmp_i16);
    tmp_i16 = defaults->humidity_high_centi_pct;
    ret |= esp_zb_cluster_add_attr(cluster, AGRINET_ATTR_CFG_HUM_HIGH, &tmp_i16);
    tmp_i16 = defaults->humidity_low_centi_pct;
    ret |= esp_zb_cluster_add_attr(cluster, AGRINET_ATTR_CFG_HUM_LOW, &tmp_i16);
    tmp_i16 = defaults->soil_dry_centi_pct;
    ret |= esp_zb_cluster_add_attr(cluster, AGRINET_ATTR_CFG_SOIL_DRY, &tmp_i16);
    tmp_i16 = defaults->soil_wet_centi_pct;
    ret |= esp_zb_cluster_add_attr(cluster, AGRINET_ATTR_CFG_SOIL_WET, &tmp_i16);
    tmp_u16 = defaults->co2_high_ppm;
    ret |= esp_zb_cluster_add_attr(cluster, AGRINET_ATTR_CFG_CO2_HIGH, &tmp_u16);
    tmp_u8  = defaults->battery_low_pct;
    ret |= esp_zb_cluster_add_attr(cluster, AGRINET_ATTR_CFG_BAT_LOW, &tmp_u8);
    tmp_u16 = 60;  /* default report interval in seconds */
    ret |= esp_zb_cluster_add_attr(cluster, AGRINET_ATTR_CFG_REPORT_INTERVAL_SEC, &tmp_u16);
    tmp_u8  = 0;
    ret |= esp_zb_cluster_add_attr(cluster, AGRINET_ATTR_CFG_ALERT_MASK, &tmp_u8);

    if (ret != ESP_OK) {
        AG_LOGE(TAG, "failed to add agrinet cfg attrs on ep %d", endpoint);
        return ret;
    }
    AG_LOGI(TAG, "agrinet cfg cluster registered on ep %d", endpoint);
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
/* Attribute updates                                                      */
/* --------------------------------------------------------------------- */
esp_err_t agrinet_update_soil_moisture(uint8_t endpoint,
                                       int16_t soil_moisture_centi_pct,
                                       int16_t soil_temp_centideg)
{
    esp_zb_zcl_status_t s;
    s = esp_zb_zcl_set_attribute_val(endpoint,
                                     AGRINET_CLUSTER_SOIL_MOISTURE,
                                     ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                     AGRINET_ATTR_SOIL_MOISTURE_MEAS_VALUE,
                                     &soil_moisture_centi_pct, false);
    if (s != ESP_ZB_ZCL_STATUS_SUCCESS) {
        AG_LOGE(TAG, "set soil moisture attr failed: 0x%02x", s);
        return ESP_FAIL;
    }
    s = esp_zb_zcl_set_attribute_val(endpoint,
                                     AGRINET_CLUSTER_SOIL_MOISTURE,
                                     ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                     AGRINET_ATTR_SOIL_TEMP_MEAS_VALUE,
                                     &soil_temp_centideg, false);
    if (s != ESP_ZB_ZCL_STATUS_SUCCESS) {
        AG_LOGE(TAG, "set soil temp attr failed: 0x%02x", s);
        return ESP_FAIL;
    }
    /* Send report if reporting is configured */
    esp_zb_zcl_report_attr_cmd_t cmd = {
        .address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_GROUP,
        .zcl_basic_cmd.src_endpoint = endpoint,
        .clusterID = AGRINET_CLUSTER_SOIL_MOISTURE,
        .attributeID = AGRINET_ATTR_SOIL_MOISTURE_MEAS_VALUE,
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,
        .dst_addr_u.group_addr = 0xFFFF,  /* broadcast to bound devices */
    };
    esp_zb_zcl_report_attr_cmd_req(&cmd);
    return ESP_OK;
}

esp_err_t agrinet_update_co2(uint8_t endpoint, uint16_t co2_ppm)
{
    esp_zb_zcl_status_t s = esp_zb_zcl_set_attribute_val(
        endpoint, AGRINET_CLUSTER_CO2_MEAS, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        AGRINET_ATTR_CO2_MEAS_VALUE, &co2_ppm, false);
    if (s != ESP_ZB_ZCL_STATUS_SUCCESS) {
        AG_LOGE(TAG, "set CO2 attr failed: 0x%02x", s);
        return ESP_FAIL;
    }
    esp_zb_zcl_report_attr_cmd_t cmd = {
        .address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_GROUP,
        .zcl_basic_cmd.src_endpoint = endpoint,
        .clusterID = AGRINET_CLUSTER_CO2_MEAS,
        .attributeID = AGRINET_ATTR_CO2_MEAS_VALUE,
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,
        .dst_addr_u.group_addr = 0xFFFF,
    };
    esp_zb_zcl_report_attr_cmd_req(&cmd);
    return ESP_OK;
}

esp_err_t agrinet_update_config(uint8_t endpoint, const agrinet_thresholds_t *th)
{
    if (th == NULL) return ESP_ERR_INVALID_ARG;
    esp_zb_zcl_status_t s;
    int16_t  i16;
    uint16_t u16;
    uint8_t  u8;
    esp_err_t ret = ESP_OK;

    i16 = th->temp_high_centideg;
    s = esp_zb_zcl_set_attribute_val(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_ATTR_CFG_TEMP_HIGH, &i16, false);
    if (s != ESP_ZB_ZCL_STATUS_SUCCESS) ret = ESP_FAIL;

    i16 = th->temp_low_centideg;
    s = esp_zb_zcl_set_attribute_val(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_ATTR_CFG_TEMP_LOW, &i16, false);
    if (s != ESP_ZB_ZCL_STATUS_SUCCESS) ret = ESP_FAIL;

    i16 = th->humidity_high_centi_pct;
    s = esp_zb_zcl_set_attribute_val(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_ATTR_CFG_HUM_HIGH, &i16, false);
    if (s != ESP_ZB_ZCL_STATUS_SUCCESS) ret = ESP_FAIL;

    i16 = th->humidity_low_centi_pct;
    s = esp_zb_zcl_set_attribute_val(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_ATTR_CFG_HUM_LOW, &i16, false);
    if (s != ESP_ZB_ZCL_STATUS_SUCCESS) ret = ESP_FAIL;

    i16 = th->soil_dry_centi_pct;
    s = esp_zb_zcl_set_attribute_val(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_ATTR_CFG_SOIL_DRY, &i16, false);
    if (s != ESP_ZB_ZCL_STATUS_SUCCESS) ret = ESP_FAIL;

    i16 = th->soil_wet_centi_pct;
    s = esp_zb_zcl_set_attribute_val(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_ATTR_CFG_SOIL_WET, &i16, false);
    if (s != ESP_ZB_ZCL_STATUS_SUCCESS) ret = ESP_FAIL;

    u16 = th->co2_high_ppm;
    s = esp_zb_zcl_set_attribute_val(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_ATTR_CFG_CO2_HIGH, &u16, false);
    if (s != ESP_ZB_ZCL_STATUS_SUCCESS) ret = ESP_FAIL;

    u8 = th->battery_low_pct;
    s = esp_zb_zcl_set_attribute_val(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, AGRINET_ATTR_CFG_BAT_LOW, &u8, false);
    if (s != ESP_ZB_ZCL_STATUS_SUCCESS) ret = ESP_FAIL;

    return ret;
}

esp_err_t agrinet_read_config(uint8_t endpoint, agrinet_thresholds_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    esp_zb_zcl_attribute_t *attr;
    attr = esp_zb_zcl_get_attribute(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
                                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                    AGRINET_ATTR_CFG_TEMP_HIGH);
    if (attr) out->temp_high_centideg = attr->value.s16;
    attr = esp_zb_zcl_get_attribute(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
                                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                    AGRINET_ATTR_CFG_TEMP_LOW);
    if (attr) out->temp_low_centideg = attr->value.s16;
    attr = esp_zb_zcl_get_attribute(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
                                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                    AGRINET_ATTR_CFG_HUM_HIGH);
    if (attr) out->humidity_high_centi_pct = attr->value.s16;
    attr = esp_zb_zcl_get_attribute(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
                                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                    AGRINET_ATTR_CFG_HUM_LOW);
    if (attr) out->humidity_low_centi_pct = attr->value.s16;
    attr = esp_zb_zcl_get_attribute(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
                                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                    AGRINET_ATTR_CFG_SOIL_DRY);
    if (attr) out->soil_dry_centi_pct = attr->value.s16;
    attr = esp_zb_zcl_get_attribute(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
                                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                    AGRINET_ATTR_CFG_SOIL_WET);
    if (attr) out->soil_wet_centi_pct = attr->value.s16;
    attr = esp_zb_zcl_get_attribute(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
                                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                    AGRINET_ATTR_CFG_CO2_HIGH);
    if (attr) out->co2_high_ppm = attr->value.u16;
    attr = esp_zb_zcl_get_attribute(endpoint, AGRINET_CLUSTER_AGRINET_CFG,
                                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                    AGRINET_ATTR_CFG_BAT_LOW);
    if (attr) out->battery_low_pct = attr->value.u8;
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
/* Reporting configuration helper                                         */
/* --------------------------------------------------------------------- */
esp_err_t agrinet_configure_default_reporting(uint8_t endpoint)
{
    /* Standard measurement clusters reporting. Report every 60s or if the
     * value changes by the minimum reportable change. */
    esp_zb_zcl_reporting_info_t reporting_info = {0};
    reporting_info.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
    reporting_info.endpoint = endpoint;
    reporting_info.cluster_id = AGRINET_CLUSTER_TEMP_MEAS;
    reporting_info.cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE;
    reporting_info.attr_id = ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID;
    reporting_info.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;
    reporting_info.proxy_tbl_index = 0;
    reporting_info.dst.short_addr = 0x0000;
    reporting_info.dst.endpoint = endpoint;
    reporting_info.u.send_info.min_interval = 30;
    reporting_info.u.send_info.max_interval = 300;
    reporting_info.u.send_info.def_min_interval = 30;
    reporting_info.u.send_info.def_max_interval = 300;
    reporting_info.u.send_info.delta.u16 = 50;  /* 0.5 C */

    /* Temperature */
    esp_zb_zcl_update_reporting_info(&reporting_info);

    /* Humidity */
    reporting_info.cluster_id = AGRINET_CLUSTER_HUMIDITY_MEAS;
    reporting_info.attr_id = ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID;
    reporting_info.u.send_info.delta.u16 = 200;  /* 2 % */
    esp_zb_zcl_update_reporting_info(&reporting_info);

    /* Pressure */
    reporting_info.cluster_id = AGRINET_CLUSTER_PRESSURE_MEAS;
    reporting_info.attr_id = ESP_ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_VALUE_ID;
    reporting_info.u.send_info.delta.u16 = 50;   /* 5 hPa */
    esp_zb_zcl_update_reporting_info(&reporting_info);

    /* Illuminance */
    reporting_info.cluster_id = AGRINET_CLUSTER_ILLUM_MEAS;
    reporting_info.attr_id = ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_VALUE_ID;
    reporting_info.u.send_info.delta.u16 = 50;
    esp_zb_zcl_update_reporting_info(&reporting_info);

    /* Soil moisture custom cluster */
    reporting_info.cluster_id = AGRINET_CLUSTER_SOIL_MOISTURE;
    reporting_info.attr_id = AGRINET_ATTR_SOIL_MOISTURE_MEAS_VALUE;
    reporting_info.u.send_info.delta.u16 = 200;  /* 2 % */
    esp_zb_zcl_update_reporting_info(&reporting_info);

    /* CO2 custom cluster */
    reporting_info.cluster_id = AGRINET_CLUSTER_CO2_MEAS;
    reporting_info.attr_id = AGRINET_ATTR_CO2_MEAS_VALUE;
    reporting_info.u.send_info.delta.u16 = 50;
    esp_zb_zcl_update_reporting_info(&reporting_info);

    AG_LOGI(TAG, "default reporting configured on ep %d", endpoint);
    return ESP_OK;
}
