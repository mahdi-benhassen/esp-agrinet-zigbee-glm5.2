/*
 * ESP-AgriNet Zigbee - Sensor node I2C driver stack
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * Lightweight I2C drivers for the sensors used by the agrinet sensor node:
 *   - BME280  : temperature / humidity / pressure
 *   - BH1750  : illuminance (lux)
 *   - SCD41   : CO2 (ppm)
 *   - Capacitive soil moisture sensor (analog ADC, no I2C)
 *
 * The drivers are intentionally minimal - only the readout path needed
 * by the sensor node. No configuration is exposed beyond power-on init.
 */
#pragma once

#include "esp_err.h"
#include "agrinet_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------- I2C bus --------------------------------- */
#define AGRINET_I2C_NUM         I2C_NUM_0
#define AGRINET_I2C_SDA_PIN     8
#define AGRINET_I2C_SCL_PIN     9
#define AGRINET_I2C_FREQ_HZ     100000

/* --------------------------- Sensor addresses -------------------------- */
#define BME280_I2C_ADDR         0x76
#define BH1750_I2C_ADDR         0x23    /* ADDR pin low */
#define SCD41_I2C_ADDR          0x62

/* ADC channel for capacitive soil moisture sensor */
#define AGRINET_SOIL_ADC_UNIT   ADC_UNIT_1
#define AGRINET_SOIL_ADC_CHAN   ADC_CHANNEL_0  /* GPIO1 on ESP32-H2 */
#define AGRINET_SOIL_GPIO       1

/* Battery voltage divider on ADC1_CH1 / GPIO2 */
#define AGRINET_BATT_ADC_CHAN   ADC_CHANNEL_1
#define AGRINET_BATT_GPIO       2

/* -------------------------- Sensor snapshot ---------------------------- */
typedef struct {
    /* BME280 */
    int16_t  temperature_centideg;     /* C * 100  */
    int16_t  humidity_centi_pct;       /* % * 100  */
    int32_t  pressure_pa;              /* Pa       */
    /* Soil (analog) */
    int16_t  soil_moisture_centi_pct;  /* % * 100  */
    int16_t  soil_temp_centideg;       /* C * 100 (est. from BME280) */
    /* BH1750 */
    uint16_t illuminance_lux;          /* lux      */
    /* SCD41 */
    uint16_t co2_ppm;                  /* ppm      */
    /* Battery */
    int8_t   battery_pct;              /* 0..100   */
    /* Timing */
    uint32_t timestamp_ms;
} agrinet_sensor_readings_t;

/* ----------------------------- Public API ------------------------------ */
/**
 * @brief Initialise the I2C bus and all sensors.
 */
esp_err_t app_sensors_init(void);

/**
 * @brief Power-down sensors to save energy (sleep mode).
 */
esp_err_t app_sensors_sleep(void);

/**
 * @brief Wake up sensors from sleep.
 */
esp_err_t app_sensors_wake(void);

/**
 * @brief Trigger a single measurement cycle on all sensors and return
 *        a populated agrinet_sensor_readings_t. For SCD41, this blocks
 *        for ~5 seconds (low-power periodic mode period).
 */
esp_err_t app_sensors_read(agrinet_sensor_readings_t *out);

/**
 * @brief Convert a sensor reading into the network snapshot type.
 */
void app_sensors_to_snapshot(const agrinet_sensor_readings_t *r,
                             agrinet_sensor_snapshot_t *out);

#ifdef __cplusplus
}
#endif
