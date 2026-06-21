/*
 * ESP-AgriNet Zigbee - Custom Cluster Definitions
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * Defines standard Zigbee clusters used by the agriculture data model plus
 * a manufacturer-specific cluster for soil moisture (no Zigbee standard
 * exists yet) and a custom agrinet configuration cluster.
 */
#pragma once

#include "agrinet_types.h"
#include "esp_zigbee.h"
#include "esp_zigbee_cluster.h"
#include "esp_zigbee_attribute.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------- Standard Zigbee Cluster IDs ----------------------- */
/* (Already defined in esp_zigbee_zcl_cluster.h - re-listed here only as    */
/*  documentation for the data model used by this project.)                */
#define AGRINET_CLUSTER_BASIC          ESP_ZB_ZCL_CLUSTER_ID_BASIC
#define AGRINET_CLUSTER_IDENTIFY       ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY
#define AGRINET_CLUSTER_POWER_CFG      ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG
#define AGRINET_CLUSTER_ON_OFF         ESP_ZB_ZCL_CLUSTER_ID_ON_OFF
#define AGRINET_CLUSTER_LEVEL          ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL
#define AGRINET_CLUSTER_TEMP_MEAS      ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT
#define AGRINET_CLUSTER_HUMIDITY_MEAS  ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT
#define AGRINET_CLUSTER_PRESSURE_MEAS  ESP_ZB_ZCL_CLUSTER_ID_PRESSURE_MEASUREMENT
#define AGRINET_CLUSTER_ILLUM_MEAS     ESP_ZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT
#define AGRINET_CLUSTER_DIAGNOSTICS    ESP_ZB_ZCL_CLUSTER_ID_DIAGNOSTICS

/* ------------------ Manufacturer-specific cluster IDs ------------------ */
/* Range 0xFC00..0xFFFF is reserved for manufacturer-specific clusters.    */
#define AGRINET_CLUSTER_SOIL_MOISTURE  0xFC00  /* soil moisture + soil temp */
#define AGRINET_CLUSTER_CO2_MEAS       0xFC01  /* CO2 concentration         */
#define AGRINET_CLUSTER_AGRINET_CFG    0xFC02  /* thresholds + node config  */

/* Manufacturer code (test/demo only - replace with real one for production) */
#define AGRINET_MANUFACTURER_CODE      0x0000

/* --------------- Soil Moisture custom cluster attributes --------------- */
#define AGRINET_ATTR_SOIL_MOISTURE_MEAS_VALUE    0x0000  /* int16 % * 100    */
#define AGRINET_ATTR_SOIL_MOISTURE_MIN_VALUE     0x0001  /* int16            */
#define AGRINET_ATTR_SOIL_MOISTURE_MAX_VALUE     0x0002  /* int16            */
#define AGRINET_ATTR_SOIL_MOISTURE_TOLERANCE     0x0003  /* int16            */
#define AGRINET_ATTR_SOIL_TEMP_MEAS_VALUE        0x0010  /* int16 C * 100    */
#define AGRINET_ATTR_SOIL_TEMP_MIN_VALUE         0x0011
#define AGRINET_ATTR_SOIL_TEMP_MAX_VALUE         0x0012
#define AGRINET_ATTR_SOIL_TEMP_TOLERANCE         0x0013

/* --------------------- CO2 custom cluster attributes ------------------- */
#define AGRINET_ATTR_CO2_MEAS_VALUE              0x0000  /* uint16 ppm       */
#define AGRINET_ATTR_CO2_MIN_MEASURED            0x0001
#define AGRINET_ATTR_CO2_MAX_MEASURED            0x0002
#define AGRINET_ATTR_CO2_TOLERANCE               0x0003

/* ---------------- AgriNet config cluster attributes -------------------- */
#define AGRINET_ATTR_CFG_TEMP_HIGH               0x0000  /* int16 C*100      */
#define AGRINET_ATTR_CFG_TEMP_LOW                0x0001
#define AGRINET_ATTR_CFG_HUM_HIGH                0x0002
#define AGRINET_ATTR_CFG_HUM_LOW                 0x0003
#define AGRINET_ATTR_CFG_SOIL_DRY                0x0004
#define AGRINET_ATTR_CFG_SOIL_WET                0x0005
#define AGRINET_ATTR_CFG_CO2_HIGH                0x0006  /* uint16 ppm       */
#define AGRINET_ATTR_CFG_BAT_LOW                 0x0007  /* uint8 %          */
#define AGRINET_ATTR_CFG_REPORT_INTERVAL_SEC     0x0008  /* uint16 seconds   */
#define AGRINET_ATTR_CFG_ALERT_MASK              0x0009  /* uint8 bitmap     */

/* ------------------------- Endpoint layout ----------------------------- */
/* Each device exposes a fixed endpoint layout so the gateway can route    */
/* messages deterministically.                                            */
#define AGRINET_EP_SENSOR           10   /* temperature/humidity/soil/co2   */
#define AGRINET_EP_ACTUATOR_PUMP    11
#define AGRINET_EP_ACTUATOR_FAN     12
#define AGRINET_EP_ACTUATOR_LIGHT   13
#define AGRINET_EP_ACTUATOR_HEATER  14
#define AGRINET_EP_ACTUATOR_WINDOW  15
#define AGRINET_EP_GATEWAY_TELE     20   /* gateway telemetry endpoint      */

/* ------------------------ Cluster data models -------------------------- */
/* Helpers to attach the agrinet clusters to an endpoint descriptor.       */

/**
 * @brief Register the soil-moisture custom cluster on the given endpoint.
 *        Must be called inside the esp_zb attribute setup callback before
 *        esp_zb_device_register().
 */
esp_err_t agrinet_register_soil_moisture_cluster(uint8_t endpoint);

/**
 * @brief Register the CO2 custom cluster on the given endpoint.
 */
esp_err_t agrinet_register_co2_cluster(uint8_t endpoint);

/**
 * @brief Register the agrinet configuration cluster on the given endpoint.
 */
esp_err_t agrinet_register_config_cluster(uint8_t endpoint, const agrinet_thresholds_t *defaults);

/**
 * @brief Update the soil moisture / soil temperature attributes on the
 *        endpoint and (optionally) send a report if reporting is configured.
 */
esp_err_t agrinet_update_soil_moisture(uint8_t endpoint,
                                       int16_t soil_moisture_centi_pct,
                                       int16_t soil_temp_centideg);

/**
 * @brief Update the CO2 attribute and (optionally) send a report.
 */
esp_err_t agrinet_update_co2(uint8_t endpoint, uint16_t co2_ppm);

/**
 * @brief Update the agrinet config cluster from a thresholds struct.
 */
esp_err_t agrinet_update_config(uint8_t endpoint, const agrinet_thresholds_t *th);

/**
 * @brief Read the agrinet config cluster into a thresholds struct.
 */
esp_err_t agrinet_read_config(uint8_t endpoint, agrinet_thresholds_t *out);

/* ----------------------- Reporting configuration ----------------------- */
/* Helper to configure Zigbee reporting for the standard measurement      */
/* clusters used in this project. Call after esp_zb_device_register().    */
esp_err_t agrinet_configure_default_reporting(uint8_t endpoint);

#ifdef __cplusplus
}
#endif
