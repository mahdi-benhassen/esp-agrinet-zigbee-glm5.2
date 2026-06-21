/*
 * ESP-AgriNet Zigbee - MQTT Topic & Payload Schema
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * Defines the MQTT topic namespace and JSON payload schema shared between
 * the gateway (publisher/subscriber) and any cloud / home-automation
 * controller that connects to the same broker.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "agrinet_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------- MQTT topic namespace -------------------------- */
/* All topics are rooted at "agrinet/<site_id>/...". site_id is a short    */
/* ASCII identifier configured per gateway via NVS (default: "gh1").      */
#define AGRINET_MQTT_TOPIC_PREFIX        "agrinet"

/* Discovery / Home-Assistant-style topics                                 */
#define AGRINET_MQTT_TOPIC_DISCOVERY     "agrinet/%s/discovery/%s"   /* site, node */
#define AGRINET_MQTT_TOPIC_NODE_STATE    "agrinet/%s/nodes/%s/state"
#define AGRINET_MQTT_TOPIC_NODE_SENSOR   "agrinet/%s/nodes/%s/sensors"
#define AGRINET_MQTT_TOPIC_NODE_ACTUATOR "agrinet/%s/nodes/%s/actuators"
#define AGRINET_MQTT_TOPIC_NODE_ALERT    "agrinet/%s/nodes/%s/alerts"
#define AGRINET_MQTT_TOPIC_GATEWAY_STATE "agrinet/%s/gateway/state"
#define AGRINET_MQTT_TOPIC_GATEWAY_LOG   "agrinet/%s/gateway/log"

/* Command topics - controller -> gateway -> node                         */
#define AGRINET_MQTT_TOPIC_CMD_ACTUATOR  "agrinet/%s/nodes/%s/actuators/%s/set"
/* where the last %s is one of: pump, fan, light, heater, window          */

/* ----------------------- Default MQTT broker --------------------------- */
#define AGRINET_MQTT_DEFAULT_BROKER_URI  "mqtt://broker.emqx.io:1883"
#define AGRINET_MQTT_DEFAULT_CLIENT_ID   "agrinet-gw-"

/* --------------------- JSON payload builders --------------------------- */
/**
 * @brief Build the sensor state JSON payload from a snapshot.
 * @return bytes written (excluding NUL) or -1 on error.
 */
int agrinet_mqtt_build_sensor_json(char *buf, size_t buf_len,
                                   const char *node_id,
                                   const agrinet_sensor_snapshot_t *snap);

/**
 * @brief Build the actuator state JSON payload.
 */
int agrinet_mqtt_build_actuator_json(char *buf, size_t buf_len,
                                     const char *node_id,
                                     const agrinet_actuator_state_t *state);

/**
 * @brief Build the alert JSON payload for the given alert mask.
 */
int agrinet_mqtt_build_alert_json(char *buf, size_t buf_len,
                                  const char *node_id,
                                  uint32_t alert_mask);

/**
 * @brief Build the gateway state JSON payload (network info + uptime).
 */
int agrinet_mqtt_build_gateway_state_json(char *buf, size_t buf_len,
                                          const char *site_id,
                                          const agrinet_network_info_t *net,
                                          uint32_t uptime_sec,
                                          uint16_t nodes_count);

/**
 * @brief Parse an actuator command JSON received on the command topic.
 *        Returns 0 on success and fills the appropriate actuator field
 *        plus *changed mask (one of AGRINET_ACT_* flag bits).
 */
#define AGRINET_ACT_CHANGE_PUMP    0x01
#define AGRINET_ACT_CHANGE_FAN     0x02
#define AGRINET_ACT_CHANGE_LIGHT   0x04
#define AGRINET_ACT_CHANGE_HEATER  0x08
#define AGRINET_ACT_CHANGE_WINDOW  0x10

int agrinet_mqtt_parse_actuator_cmd(const char *json, size_t len,
                                    agrinet_actuator_state_t *out_state,
                                    uint32_t *out_changed);

#ifdef __cplusplus
}
#endif
