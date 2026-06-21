/*
 * ESP-AgriNet Zigbee - Common Types
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * Shared data types used across gateway and nodes for the
 * smart agriculture / greenhouse monitoring system.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------- Versioning ------------------------------ */
#define AGRINET_VERSION_MAJOR   1
#define AGRINET_VERSION_MINOR   0
#define AGRINET_VERSION_PATCH   0

#define AGRINET_VERSION_STRING  "1.0.0"

/* --------------------------- Device classes ---------------------------- */
typedef enum {
    AGRINET_DEVICE_GATEWAY     = 0x00,  /* ESP32-S3 + ESP32-H2 RCP            */
    AGRINET_DEVICE_SENSOR_NODE = 0x01,  /* ESP32-H2 sensor end device         */
    AGRINET_DEVICE_ACT_NODE    = 0x02,  /* ESP32-H2 actuator router           */
    AGRINET_DEVICE_RCP         = 0x03,  /* ESP32-H2 radio coprocessor         */
} agrinet_device_class_t;

/* ------------------------- Sensor measurement -------------------------- */
/* All sensor values are reported as int16/int32 scaled to the SI unit.    */
/* Temperature  : int16  degrees Celsius * 100     (e.g. 2345 = 23.45 C)   */
/* Humidity     : int16  percent * 100             (e.g. 5600 = 56.00 %)   */
/* Pressure     : int32  Pascals                   (e.g. 101325 = 1013.25 hPa) */
/* Soil moisture: int16  percent * 100             (0..10000)              */
/* Illuminance  : int16  lux                                                */
/* CO2          : int16  ppm                                           */
/* Battery      : int8   percent 0..100                                */
typedef struct {
    int16_t  temperature_centideg;     /* C * 100                           */
    int16_t  humidity_centi_pct;       /* % * 100                           */
    int32_t  pressure_pa;              /* Pa                                */
    int16_t  soil_moisture_centi_pct;  /* % * 100                           */
    int16_t  soil_temp_centideg;       /* C * 100                           */
    uint16_t illuminance_lux;          /* lux                               */
    uint16_t co2_ppm;                  /* ppm                               */
    int8_t   battery_pct;              /* 0..100  (-1 = mains powered)      */
    uint32_t timestamp_ms;             /* uptime in ms at last sample       */
} agrinet_sensor_snapshot_t;

/* ---------------------------- Actuator state --------------------------- */
typedef enum {
    AGRINET_ACT_OFF = 0,
    AGRINET_ACT_ON  = 1,
} agrinet_act_state_t;

typedef struct {
    agrinet_act_state_t pump;          /* irrigation pump                   */
    uint8_t             pump_level;    /* 0..100 percent (PWM duty)         */
    agrinet_act_state_t fan;           /* ventilation fan                   */
    uint8_t             fan_speed;     /* 0..100 percent                    */
    agrinet_act_state_t grow_light;    /* LED grow light on/off             */
    uint8_t             grow_light_level; /* 0..100 percent dimming        */
    agrinet_act_state_t heater;        /* greenhouse heater                 */
    agrinet_act_state_t window;        /* roof window (open=1, closed=0)    */
    uint32_t            timestamp_ms;
} agrinet_actuator_state_t;

/* ------------------------------- Alerts -------------------------------- */
typedef enum {
    AGRINET_ALERT_NONE              = 0x00,
    AGRINET_ALERT_TEMP_HIGH         = 0x01,
    AGRINET_ALERT_TEMP_LOW          = 0x02,
    AGRINET_ALERT_HUMIDITY_HIGH     = 0x04,
    AGRINET_ALERT_HUMIDITY_LOW      = 0x08,
    AGRINET_ALERT_SOIL_DRY          = 0x10,
    AGRINET_ALERT_SOIL_WET          = 0x20,
    AGRINET_ALERT_CO2_HIGH          = 0x40,
    AGRINET_ALERT_BATTERY_LOW       = 0x80,
} agrinet_alert_mask_t;

/* ----------------------------- Thresholds ------------------------------ */
typedef struct {
    int16_t temp_high_centideg;        /* alert above this temperature       */
    int16_t temp_low_centideg;         /* alert below this temperature       */
    int16_t humidity_high_centi_pct;
    int16_t humidity_low_centi_pct;
    int16_t soil_dry_centi_pct;
    int16_t soil_wet_centi_pct;
    uint16_t co2_high_ppm;
    uint8_t  battery_low_pct;
} agrinet_thresholds_t;

#define AGRINET_DEFAULT_THRESHOLDS { \
    .temp_high_centideg   = 3500,  /* 35.0 C                              */ \
    .temp_low_centideg    =  500,  /*  5.0 C                              */ \
    .humidity_high_centi_pct = 9000, /* 90 %                              */ \
    .humidity_low_centi_pct  = 3000, /* 30 %                              */ \
    .soil_dry_centi_pct   = 2500,  /* 25 %                               */ \
    .soil_wet_centi_pct   = 8500,  /* 85 %                               */ \
    .co2_high_ppm         = 1200,                                          \
    .battery_low_pct      = 20,                                            \
}

/* ----------------------------- Network info ---------------------------- */
typedef struct {
    uint16_t pan_id;
    uint8_t  channel;
    uint8_t  ext_pan_id[8];
    uint8_t  gateway_short_addr[2];   /* network byte order                 */
} agrinet_network_info_t;

#define AGRINET_DEFAULT_PAN_ID    0x1A2B
#define AGRINET_DEFAULT_CHANNEL   15

#ifdef __cplusplus
}
#endif
