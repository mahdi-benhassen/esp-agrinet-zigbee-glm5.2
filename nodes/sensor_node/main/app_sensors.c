/*
 * ESP-AgriNet Zigbee - Sensor node I2C + ADC driver implementation
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * BME280, BH1750, SCD41 and capacitive soil moisture driver stack.
 * The drivers cover only the measurement path; trimming/calibration
 * of BME280 follows the Bosch reference algorithm (simplified).
 */
#include "app_sensors.h"
#include "agrinet_log.h"
#include "agrinet_types.h"

#include "driver/i2c.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <math.h>

static const char *TAG = AGRINET_LOG_TAG_SENSOR;

/* --------------------------------------------------------------------- */
/* I2C helpers                                                            */
/* --------------------------------------------------------------------- */
static esp_err_t i2c_write(uint8_t addr, const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, (uint8_t *)data, len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(AGRINET_I2C_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) i2c_master_read(cmd, buf, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, buf + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(AGRINET_I2C_NUM, cmd, pdMS_TO_TICKS(200));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_write(addr, buf, sizeof(buf));
}

/* --------------------------------------------------------------------- */
/* BME280 (simplified driver)                                            */
/* --------------------------------------------------------------------- */
typedef struct {
    uint16_t dig_T1; int16_t dig_T2, dig_T3;
    uint16_t dig_P1; int16_t dig_P2, dig_P3, dig_P4, dig_P5;
    int16_t dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t  dig_H1, dig_H3; int16_t dig_H2; int16_t dig_H4, dig_H5; int8_t dig_H6;
    int32_t  t_fine;
} bme280_calib_t;

static bme280_calib_t s_bme_cal;

static esp_err_t bme280_read_calib(void)
{
    uint8_t buf[24];
    /* T1..P9 (0x88..0x9F) */
    if (i2c_read_reg(BME280_I2C_ADDR, 0x88, buf, 6) != ESP_OK)  return ESP_FAIL;
    s_bme_cal.dig_T1 = (uint16_t)(buf[1] << 8 | buf[0]);
    s_bme_cal.dig_T2 = (int16_t)(buf[3] << 8 | buf[2]);
    s_bme_cal.dig_T3 = (int16_t)(buf[5] << 8 | buf[4]);

    if (i2c_read_reg(BME280_I2C_ADDR, 0x8E, buf, 18) != ESP_OK) return ESP_FAIL;
    s_bme_cal.dig_P1 = (uint16_t)(buf[1] << 8 | buf[0]);
    s_bme_cal.dig_P2 = (int16_t)(buf[3] << 8 | buf[2]);
    s_bme_cal.dig_P3 = (int16_t)(buf[5] << 8 | buf[4]);
    s_bme_cal.dig_P4 = (int16_t)(buf[7] << 8 | buf[6]);
    s_bme_cal.dig_P5 = (int16_t)(buf[9] << 8 | buf[8]);
    s_bme_cal.dig_P6 = (int16_t)(buf[11] << 8 | buf[10]);
    s_bme_cal.dig_P7 = (int16_t)(buf[13] << 8 | buf[12]);
    s_bme_cal.dig_P8 = (int16_t)(buf[15] << 8 | buf[14]);
    s_bme_cal.dig_P9 = (int16_t)(buf[17] << 8 | buf[16]);

    uint8_t h[7];
    if (i2c_read_reg(BME280_I2C_ADDR, 0xA1, h, 1) != ESP_OK)   return ESP_FAIL;
    s_bme_cal.dig_H1 = h[0];
    if (i2c_read_reg(BME280_I2C_ADDR, 0xE1, h, 7) != ESP_OK)   return ESP_FAIL;
    s_bme_cal.dig_H2 = (int16_t)(h[1] << 8 | h[0]);
    s_bme_cal.dig_H3 = h[2];
    s_bme_cal.dig_H4 = (int16_t)((h[3] << 4) | (h[4] & 0x0F));
    s_bme_cal.dig_H5 = (int16_t)((h[5] << 4) | (h[4] >> 4));
    s_bme_cal.dig_H6 = (int8_t)h[6];
    return ESP_OK;
}

static esp_err_t bme280_init(void)
{
    /* Soft reset */
    i2c_write_reg(BME280_I2C_ADDR, 0xE0, 0xB6);
    vTaskDelay(pdMS_TO_TICKS(10));
    /* ctrl_hum = x1 */
    i2c_write_reg(BME280_I2C_ADDR, 0xF2, 0x01);
    /* ctrl = temp/press x1, no IIR */
    i2c_write_reg(BME280_I2C_ADDR, 0xF4, 0x27);
    /* config: 1000ms standby, no IIR */
    i2c_write_reg(BME280_I2C_ADDR, 0xF5, 0xA0);
    vTaskDelay(pdMS_TO_TICKS(50));
    if (bme280_read_calib() != ESP_OK) {
        AG_LOGE(TAG, "BME280 calibration read failed");
        return ESP_FAIL;
    }
    AG_LOGI(TAG, "BME280 initialised");
    return ESP_OK;
}

static esp_err_t bme280_read(int16_t *temp_cdeg, int16_t *hum_cdeg, int32_t *press_pa)
{
    uint8_t buf[8];
    /* 0xF7..0xFE : press(3), temp(3), hum(2) */
    if (i2c_read_reg(BME280_I2C_ADDR, 0xF7, buf, 8) != ESP_OK) return ESP_FAIL;
    int32_t adc_p = (buf[0] << 12) | (buf[1] << 4) | (buf[2] >> 4);
    int32_t adc_t = (buf[3] << 12) | (buf[4] << 4) | (buf[5] >> 4);
    int32_t adc_h = (buf[6] << 8) | buf[7];

    /* Temperature compensation (Bosch datasheet) */
    int32_t var1 = ((((adc_t >> 3) - ((int32_t)s_bme_cal.dig_T1 << 1))) *
                    ((int32_t)s_bme_cal.dig_T2)) >> 11;
    int32_t var2 = (((((adc_t >> 4) - ((int32_t)s_bme_cal.dig_T1)) *
                      ((adc_t >> 4) - ((int32_t)s_bme_cal.dig_T1))) >> 12) *
                    ((int32_t)s_bme_cal.dig_T3)) >> 14;
    s_bme_cal.t_fine = var1 + var2;
    int32_t t_cdeg = (s_bme_cal.t_fine * 5 + 128) >> 8;  /* 0.01 deg C */

    /* Pressure compensation */
    int64_t var3 = (int64_t)s_bme_cal.t_fine - 128000;
    int64_t var4 = var3 * var3 * (int64_t)s_bme_cal.dig_P6;
    var4 = var4 + ((var3 * (int64_t)s_bme_cal.dig_P5) << 17);
    var4 = var4 + (((int64_t)s_bme_cal.dig_P4) << 35);
    var3 = ((var4 >> 47) + ((var3 * var3 * (int64_t)s_bme_cal.dig_P3) << 1)
            + ((var3 * (int64_t)s_bme_cal.dig_P2) << 15)) >> 18;
    int64_t p4 = ((var3 >> 15) + ((int64_t)1) + (((int64_t)s_bme_cal.dig_P1) * 2)) >> 1;
    int64_t p = 1048576 - adc_p;
    p = (p - (var4 >> 12)) * 6250;
    p = (p + ((var4 >> 12) * (int64_t)s_bme_cal.dig_P8) / (p4 * 2)) >> 8;
    p = p + ((var4 >> 12) * (int64_t)s_bme_cal.dig_P7) / 4096;
    int32_t pa = (int32_t)(p + (p4 * (int64_t)s_bme_cal.dig_P8) / 32768);
    pa = pa + ((int64_t)pa >> 8); /* hi-resolution */

    /* Humidity compensation */
    int32_t v_x1 = s_bme_cal.t_fine - 76800;
    v_x1 = (((((adc_h << 14) - (((int32_t)s_bme_cal.dig_H4) << 20) -
               (((int32_t)s_bme_cal.dig_H5) * v_x1)) + 16384) >> 15) *
            (((((((v_x1 * (int32_t)s_bme_cal.dig_H6) >> 10) *
                 (((v_x1 * (int32_t)s_bme_cal.dig_H3) >> 11) + 32768)) >> 10) +
               2097152) * (int32_t)s_bme_cal.dig_H2 + 8192) >> 14));
    v_x1 = v_x1 - (((((v_x1 >> 15) * (v_x1 >> 15)) >> 7) *
                    (int32_t)s_bme_cal.dig_H1) >> 4);
    if (v_x1 < 0) v_x1 = 0;
    if (v_x1 > 419430400) v_x1 = 419430400;
    int32_t h_cdeg = (v_x1 >> 12) * 100;  /* % * 100 */

    *temp_cdeg = (int16_t)t_cdeg;
    *press_pa  = pa;
    *hum_cdeg  = (int16_t)h_cdeg;
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
/* BH1750 driver                                                         */
/* --------------------------------------------------------------------- */
static esp_err_t bh1750_init(void)
{
    /* Power on, then reset */
    uint8_t pwr = 0x01; i2c_write(BH1750_I2C_ADDR, &pwr, 1);
    uint8_t rst = 0x07; i2c_write(BH1750_I2C_ADDR, &rst, 1);
    AG_LOGI(TAG, "BH1750 initialised");
    return ESP_OK;
}

static esp_err_t bh1750_read(uint16_t *lux_out)
{
    /* One-time high-res mode 2 = 0x21 (1 lux res) */
    uint8_t mode = 0x21;
    if (i2c_write(BH1750_I2C_ADDR, &mode, 1) != ESP_OK) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(180));  /* max 180ms */
    uint8_t buf[2];
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BH1750_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &buf[0], I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &buf[1], I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(AGRINET_I2C_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) return ret;
    uint16_t raw = (buf[0] << 8) | buf[1];
    *lux_out = (uint16_t)(raw / 1.2f);
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
/* SCD41 driver                                                          */
/* --------------------------------------------------------------------- */
static esp_err_t scd41_init(void)
{
    /* Stop periodic measurement if running */
    uint8_t stop[2] = { 0x01, 0x04 };
    i2c_write(SCD41_I2C_ADDR, stop, 2);
    vTaskDelay(pdMS_TO_TICKS(500));
    /* Re-init - send measure_single_shot (0x219D) */
    uint8_t single[2] = { 0x21, 0x9D };
    if (i2c_write(SCD41_I2C_ADDR, single, 2) != ESP_OK) {
        AG_LOGW(TAG, "SCD41 not responding - CO2 readings will be 0");
        return ESP_FAIL;
    }
    AG_LOGI(TAG, "SCD41 initialised");
    return ESP_OK;
}

static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
        }
    }
    return crc;
}

static esp_err_t scd41_read(uint16_t *co2_ppm, int16_t *temp_cdeg, uint16_t *hum_centi)
{
    uint8_t read_cmd[2] = { 0xEC, 0x05 };
    if (i2c_write(SCD41_I2C_ADDR, read_cmd, 2) != ESP_OK) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(5));
    uint8_t buf[9];
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SCD41_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    for (int i = 0; i < 8; i++) i2c_master_read_byte(cmd, &buf[i], I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &buf[8], I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(AGRINET_I2C_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) return ret;
    if (crc8(&buf[0], 2) != buf[2] ||
        crc8(&buf[3], 2) != buf[5] ||
        crc8(&buf[6], 2) != buf[8]) {
        AG_LOGW(TAG, "SCD41 CRC mismatch");
        return ESP_FAIL;
    }
    *co2_ppm  = (buf[0] << 8) | buf[1];
    *temp_cdeg = (int16_t)((((buf[3] << 8) | buf[4]) * 17500) / 65535 - 4500);
    *hum_centi = (uint16_t)(((buf[6] << 8) | buf[7]) * 100 / 65535);
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
/* Soil moisture (analog) + Battery                                     */
/* --------------------------------------------------------------------- */
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_adc_cali_handle = NULL;
static bool s_adc_calibrated = false;

static bool adc_calibration_init(adc_unit_t unit, adc_channel_t chan, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = chan,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
        *out_handle = handle;
        return true;
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
        *out_handle = handle;
        return true;
    }
#endif

    return false;
}

static esp_err_t adc_setup(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = AGRINET_SOIL_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &s_adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, AGRINET_SOIL_ADC_CHAN, &chan_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, AGRINET_BATT_ADC_CHAN, &chan_cfg));

    s_adc_calibrated = adc_calibration_init(AGRINET_SOIL_ADC_UNIT, AGRINET_SOIL_ADC_CHAN, ADC_ATTEN_DB_12, &s_adc_cali_handle);
    AG_LOGI(TAG, "ADC setup: calibrated=%d", s_adc_calibrated);
    return ESP_OK;
}

/* Capacitive soil moisture sensor calibration constants (per project) */
#define SOIL_MOISTURE_DRY_RAW   3200  /* ADC reading when sensor is in air */
#define SOIL_MOISTURE_WET_RAW   1400  /* ADC reading when sensor is in water */

static int16_t read_soil_moisture_centi(void)
{
    int raw = 0;
    if (adc_oneshot_read(s_adc_handle, AGRINET_SOIL_ADC_CHAN, &raw) != ESP_OK) return 0;
    /* Linear map: dry -> 0%, wet -> 100% */
    int pct_x100;
    if (raw >= SOIL_MOISTURE_DRY_RAW) pct_x100 = 0;
    else if (raw <= SOIL_MOISTURE_WET_RAW) pct_x100 = 10000;
    else pct_x100 = (int32_t)(SOIL_MOISTURE_DRY_RAW - raw) * 10000 /
                    (SOIL_MOISTURE_DRY_RAW - SOIL_MOISTURE_WET_RAW);
    return (int16_t)pct_x100;
}

static int8_t read_battery_pct(void)
{
    int raw = 0;
    if (adc_oneshot_read(s_adc_handle, AGRINET_BATT_ADC_CHAN, &raw) != ESP_OK) return -1;
    int mv = 0;
    if (s_adc_calibrated) {
        adc_cali_raw_to_voltage(s_adc_cali_handle, raw, &mv);
    } else {
        mv = raw * 1100 / 4095;
    }
    /* 1:2 voltage divider - 2x to get actual battery voltage */
    mv *= 2;
    /* Li-ion 18650: 4200mV full, 3000mV empty */
    if (mv >= 4200) return 100;
    if (mv <= 3000) return 0;
    return (int8_t)((mv - 3000) / 12);  /* approx 0..100 */
}

/* --------------------------------------------------------------------- */
/* Top-level API                                                         */
/* --------------------------------------------------------------------- */
esp_err_t app_sensors_init(void)
{
    /* I2C master init */
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = AGRINET_I2C_SDA_PIN,
        .scl_io_num = AGRINET_I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = AGRINET_I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(AGRINET_I2C_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(AGRINET_I2C_NUM, conf.mode, 0, 0, 0));

    ESP_ERROR_CHECK(adc_setup());

    bme280_init();
    bh1750_init();
    scd41_init();
    AG_LOGI(TAG, "all sensors initialised");
    return ESP_OK;
}

esp_err_t app_sensors_sleep(void)
{
    /* BME280 sleep */
    i2c_write_reg(BME280_I2C_ADDR, 0xF4, 0x00);
    /* SCD41 stop periodic */
    uint8_t stop[2] = { 0x01, 0x04 };
    i2c_write(SCD41_I2C_ADDR, stop, 2);
    return ESP_OK;
}

esp_err_t app_sensors_wake(void)
{
    /* BME280 force mode */
    i2c_write_reg(BME280_I2C_ADDR, 0xF4, 0x27);
    return ESP_OK;
}

esp_err_t app_sensors_read(agrinet_sensor_readings_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

    /* BME280 */
    int16_t bme_t, bme_h;
    int32_t bme_p;
    if (bme280_read(&bme_t, &bme_h, &bme_p) == ESP_OK) {
        out->temperature_centideg = bme_t;
        out->humidity_centi_pct   = bme_h;
        out->pressure_pa          = bme_p;
    } else {
        AG_LOGW(TAG, "BME280 read failed");
    }

    /* BH1750 */
    uint16_t lux;
    if (bh1750_read(&lux) == ESP_OK) {
        out->illuminance_lux = lux;
    } else {
        AG_LOGW(TAG, "BH1750 read failed");
    }

    /* SCD41 - single shot then wait 5s */
    uint8_t single[2] = { 0x21, 0x9D };
    i2c_write(SCD41_I2C_ADDR, single, 2);
    vTaskDelay(pdMS_TO_TICKS(5000));
    uint16_t co2; int16_t scd_t; uint16_t scd_h;
    if (scd41_read(&co2, &scd_t, &scd_h) == ESP_OK) {
        out->co2_ppm = co2;
        /* Use SCD41 temperature as the soil temp proxy if available */
        out->soil_temp_centideg = scd_t;
    } else {
        /* Fall back to BME280 temperature */
        out->soil_temp_centideg = bme_t;
    }

    /* Soil moisture (analog) */
    out->soil_moisture_centi_pct = read_soil_moisture_centi();

    /* Battery */
    out->battery_pct = read_battery_pct();

    AG_LOGD(TAG, "read: T=%.2fC H=%.1f%% P=%ldPa soil=%.1f%% lux=%u co2=%u bat=%d",
            out->temperature_centideg / 100.0f,
            out->humidity_centi_pct / 100.0f,
            (long)out->pressure_pa,
            out->soil_moisture_centi_pct / 100.0f,
            (unsigned)out->illuminance_lux,
            (unsigned)out->co2_ppm,
            (int)out->battery_pct);
    return ESP_OK;
}

void app_sensors_to_snapshot(const agrinet_sensor_readings_t *r,
                             agrinet_sensor_snapshot_t *out)
{
    out->temperature_centideg    = r->temperature_centideg;
    out->humidity_centi_pct      = r->humidity_centi_pct;
    out->pressure_pa             = r->pressure_pa;
    out->soil_moisture_centi_pct = r->soil_moisture_centi_pct;
    out->soil_temp_centideg      = r->soil_temp_centideg;
    out->illuminance_lux         = r->illuminance_lux;
    out->co2_ppm                 = r->co2_ppm;
    out->battery_pct             = r->battery_pct;
    out->timestamp_ms            = r->timestamp_ms;
}
