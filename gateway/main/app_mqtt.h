/*
 * ESP-AgriNet Zigbee - Gateway MQTT module
 * Copyright (c) 2025 ESP-AgriNet Project
 */
#pragma once

#include "esp_err.h"
#include "agrinet_types.h"
#include "agrinet_mqtt_schema.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the MQTT client with the configured broker URI.
 *        Does not connect yet.
 */
esp_err_t app_mqtt_init(const char *broker_uri, const char *client_id);

/**
 * @brief Connect to the broker and subscribe to the command topics.
 */
esp_err_t app_mqtt_start(void);

/**
 * @brief Publish a payload to a topic with QoS 1.
 */
esp_err_t app_mqtt_publish(const char *topic, const char *payload, int qos);

/**
 * @brief Register a callback invoked when an actuator command is received.
 *        The callback gets the parsed node_id, actuator name and state.
 */
typedef void (*app_mqtt_actuator_cmd_cb_t)(const char *node_id,
                                           const char *actuator_name,
                                           const agrinet_actuator_state_t *state,
                                           uint32_t changed_mask);

esp_err_t app_mqtt_register_actuator_cmd_cb(app_mqtt_actuator_cmd_cb_t cb);

/**
 * @brief Returns true if the MQTT client is currently connected to the broker.
 */
bool app_mqtt_is_connected(void);

#ifdef __cplusplus
}
#endif
