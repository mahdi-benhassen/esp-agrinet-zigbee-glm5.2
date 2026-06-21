/*
 * ESP-AgriNet Zigbee - MQTT JSON payload builders
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * Self-contained JSON builders (no external JSON lib required) for the
 * agrinet MQTT topic schema. Output is always NUL-terminated.
 */
#include "agrinet_mqtt_schema.h"
#include "agrinet_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = AGRINET_LOG_TAG_MQTT;

int agrinet_mqtt_build_sensor_json(char *buf, size_t buf_len,
                                   const char *node_id,
                                   const agrinet_sensor_snapshot_t *s)
{
    if (!buf || !node_id || !s || buf_len == 0) return -1;

    int n = snprintf(buf, buf_len,
        "{\"node\":\"%s\",\"ts\":%lu,"
        "\"temp_c\":%.2f,\"humidity_pct\":%.2f,\"pressure_pa\":%ld,"
        "\"soil_moisture_pct\":%.2f,\"soil_temp_c\":%.2f,"
        "\"illuminance_lux\":%u,\"co2_ppm\":%u,\"battery_pct\":%d}",
        node_id,
        (unsigned long)s->timestamp_ms,
        s->temperature_centideg / 100.0f,
        s->humidity_centi_pct / 100.0f,
        (long)s->pressure_pa,
        s->soil_moisture_centi_pct / 100.0f,
        s->soil_temp_centideg / 100.0f,
        (unsigned)s->illuminance_lux,
        (unsigned)s->co2_ppm,
        (int)s->battery_pct);

    if (n < 0 || (size_t)n >= buf_len) {
        AG_LOGE(TAG, "sensor json truncated (need %d, have %u)", n, (unsigned)buf_len);
        return -1;
    }
    return n;
}

int agrinet_mqtt_build_actuator_json(char *buf, size_t buf_len,
                                     const char *node_id,
                                     const agrinet_actuator_state_t *a)
{
    if (!buf || !node_id || !a || buf_len == 0) return -1;

    int n = snprintf(buf, buf_len,
        "{\"node\":\"%s\",\"ts\":%lu,"
        "\"pump\":%s,\"pump_level\":%u,"
        "\"fan\":%s,\"fan_speed\":%u,"
        "\"light\":%s,\"light_level\":%u,"
        "\"heater\":%s,\"window\":%s}",
        node_id,
        (unsigned long)a->timestamp_ms,
        a->pump        == AGRINET_ACT_ON ? "on" : "off", a->pump_level,
        a->fan         == AGRINET_ACT_ON ? "on" : "off", a->fan_speed,
        a->grow_light  == AGRINET_ACT_ON ? "on" : "off", a->grow_light_level,
        a->heater      == AGRINET_ACT_ON ? "on" : "off",
        a->window      == AGRINET_ACT_ON ? "on" : "off");

    if (n < 0 || (size_t)n >= buf_len) {
        AG_LOGE(TAG, "actuator json truncated");
        return -1;
    }
    return n;
}

int agrinet_mqtt_build_alert_json(char *buf, size_t buf_len,
                                  const char *node_id,
                                  uint32_t alert_mask)
{
    if (!buf || !node_id || buf_len == 0) return -1;

    char alerts[160] = {0};
    size_t pos = 0;
    const char *sep = "";

#define APPEND_ALERT(_mask, _str)                                          \
    do {                                                                   \
        if ((alert_mask & (_mask)) && pos + sizeof(_str) + 4 < sizeof(alerts)) { \
            pos += snprintf(alerts + pos, sizeof(alerts) - pos, "%s\"%s\"", sep, (_str)); \
            sep = ",";                                                     \
        }                                                                  \
    } while (0)

    APPEND_ALERT(AGRINET_ALERT_TEMP_HIGH,     "temp_high");
    APPEND_ALERT(AGRINET_ALERT_TEMP_LOW,      "temp_low");
    APPEND_ALERT(AGRINET_ALERT_HUMIDITY_HIGH, "humidity_high");
    APPEND_ALERT(AGRINET_ALERT_HUMIDITY_LOW,  "humidity_low");
    APPEND_ALERT(AGRINET_ALERT_SOIL_DRY,      "soil_dry");
    APPEND_ALERT(AGRINET_ALERT_SOIL_WET,      "soil_wet");
    APPEND_ALERT(AGRINET_ALERT_CO2_HIGH,      "co2_high");
    APPEND_ALERT(AGRINET_ALERT_BATTERY_LOW,   "battery_low");
#undef APPEND_ALERT

    int n = snprintf(buf, buf_len,
        "{\"node\":\"%s\",\"alerts\":[%s],\"mask\":%lu}",
        node_id, alerts, (unsigned long)alert_mask);
    if (n < 0 || (size_t)n >= buf_len) {
        AG_LOGE(TAG, "alert json truncated");
        return -1;
    }
    return n;
}

int agrinet_mqtt_build_gateway_state_json(char *buf, size_t buf_len,
                                          const char *site_id,
                                          const agrinet_network_info_t *net,
                                          uint32_t uptime_sec,
                                          uint16_t nodes_count)
{
    if (!buf || !site_id || !net || buf_len == 0) return -1;

    int n = snprintf(buf, buf_len,
        "{\"site\":\"%s\",\"uptime_sec\":%lu,"
        "\"pan_id\":\"0x%04X\",\"channel\":%u,\"nodes\":%u,"
        "\"ext_pan_id\":\"%02X%02X%02X%02X%02X%02X%02X%02X\"}",
        site_id,
        (unsigned long)uptime_sec,
        (unsigned)net->pan_id, (unsigned)net->channel,
        (unsigned)nodes_count,
        net->ext_pan_id[0], net->ext_pan_id[1], net->ext_pan_id[2], net->ext_pan_id[3],
        net->ext_pan_id[4], net->ext_pan_id[5], net->ext_pan_id[6], net->ext_pan_id[7]);

    if (n < 0 || (size_t)n >= buf_len) {
        AG_LOGE(TAG, "gw state json truncated");
        return -1;
    }
    return n;
}

/* --------------------------------------------------------------------- */
/* Minimal JSON parser for the actuator command topic.                    */
/* Recognises keys: pump, fan, light, heater, window (on/off or 0/1)      */
/*                : pump_level, fan_speed, light_level (0..100 integer)    */
/* --------------------------------------------------------------------- */
static const char *find_key(const char *json, size_t len, const char *key)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = json;
    const char *end = json + len;
    size_t plen = strlen(pattern);
    while (p + plen <= end) {
        if (memcmp(p, pattern, plen) == 0) {
            return p + plen;
        }
        p++;
    }
    return NULL;
}

static bool parse_bool_value(const char *p, const char *end, bool *out)
{
    while (p < end && (*p == ' ' || *p == ':' || *p == '\t')) p++;
    if (p + 4 <= end && memcmp(p, "true", 4) == 0) { *out = true;  return true; }
    if (p + 5 <= end && memcmp(p, "false", 5) == 0) { *out = false; return true; }
    if (p < end && (*p == '1')) { *out = true;  return true; }
    if (p < end && (*p == '0')) { *out = false; return true; }
    if (p + 2 <= end && (p[0] == 'o' && p[1] == 'n'))  { *out = true;  return true; }
    if (p + 3 <= end && (p[0] == 'o' && p[1] == 'f' && p[2] == 'f')) { *out = false; return true; }
    return false;
}

static bool parse_u8_value(const char *p, const char *end, uint8_t *out)
{
    while (p < end && (*p == ' ' || *p == ':' || *p == '\t')) p++;
    if (p >= end || *p < '0' || *p > '9') return false;
    long v = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        if (v > 255) return false;
        p++;
    }
    *out = (uint8_t)v;
    return true;
}

int agrinet_mqtt_parse_actuator_cmd(const char *json, size_t len,
                                    agrinet_actuator_state_t *out_state,
                                    uint32_t *out_changed)
{
    if (!json || !out_state || !out_changed) return -1;
    const char *end = json + len;
    *out_changed = 0;
    bool b;
    uint8_t u8;
    const char *p;

    if ((p = find_key(json, len, "pump")) && parse_bool_value(p, end, &b)) {
        out_state->pump = b ? AGRINET_ACT_ON : AGRINET_ACT_OFF;
        *out_changed |= AGRINET_ACT_CHANGE_PUMP;
    }
    if ((p = find_key(json, len, "fan")) && parse_bool_value(p, end, &b)) {
        out_state->fan = b ? AGRINET_ACT_ON : AGRINET_ACT_OFF;
        *out_changed |= AGRINET_ACT_CHANGE_FAN;
    }
    if ((p = find_key(json, len, "light")) && parse_bool_value(p, end, &b)) {
        out_state->grow_light = b ? AGRINET_ACT_ON : AGRINET_ACT_OFF;
        *out_changed |= AGRINET_ACT_CHANGE_LIGHT;
    }
    if ((p = find_key(json, len, "heater")) && parse_bool_value(p, end, &b)) {
        out_state->heater = b ? AGRINET_ACT_ON : AGRINET_ACT_OFF;
        *out_changed |= AGRINET_ACT_CHANGE_HEATER;
    }
    if ((p = find_key(json, len, "window")) && parse_bool_value(p, end, &b)) {
        out_state->window = b ? AGRINET_ACT_ON : AGRINET_ACT_OFF;
        *out_changed |= AGRINET_ACT_CHANGE_WINDOW;
    }
    if ((p = find_key(json, len, "pump_level"))  && parse_u8_value(p, end, &u8)) {
        out_state->pump_level = u8; *out_changed |= AGRINET_ACT_CHANGE_PUMP;
    }
    if ((p = find_key(json, len, "fan_speed"))   && parse_u8_value(p, end, &u8)) {
        out_state->fan_speed = u8; *out_changed |= AGRINET_ACT_CHANGE_FAN;
    }
    if ((p = find_key(json, len, "light_level")) && parse_u8_value(p, end, &u8)) {
        out_state->grow_light_level = u8; *out_changed |= AGRINET_ACT_CHANGE_LIGHT;
    }
    return 0;
}
