/*
 * ESP-AgriNet Zigbee - Gateway Application Header
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * Public API for the gateway application: WiFi provisioning, MQTT bridge,
 * Zigbee coordinator lifecycle and agrinet command dispatch.
 */
#pragma once

#include "agrinet_types.h"
#include "agrinet_mqtt_schema.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------- Gateway configuration ------------------------- */
#define AGRINET_GW_SITE_ID_MAX_LEN     16
#define AGRINET_GW_NVS_NAMESPACE       "agrinet_gw"

#define AGRINET_GW_MQTT_URI_MAX_LEN    128
#define AGRINET_GW_WIFI_SSID_MAX_LEN   32
#define AGRINET_GW_WIFI_PASS_MAX_LEN   64

#define AGRINET_GW_MQTT_TOPIC_MAX_LEN  128
#define AGRINET_GW_MQTT_PAYLOAD_MAX    512

#define AGRINET_GW_MAX_NODES           32

/* --------------------- Gateway runtime state --------------------------- */
typedef enum {
    AGRINET_GW_STATE_BOOTING = 0,
    AGRINET_GW_STATE_WIFI_CONNECTING,
    AGRINET_GW_STATE_MQTT_CONNECTING,
    AGRINET_GW_STATE_ZB_FORMING,
    AGRINET_GW_STATE_RUNNING,
    AGRINET_GW_STATE_ERROR,
} agrinet_gw_state_t;

typedef struct {
    char site_id[AGINET_GW_SITE_ID_MAX_LEN];
    char mqtt_uri[AGINET_GW_MQTT_URI_MAX_LEN];
    char wifi_ssid[AGINET_GW_WIFI_SSID_MAX_LEN];
    char wifi_pass[AGINET_GW_WIFI_PASS_MAX_LEN];
    uint16_t pan_id;
    uint8_t  channel;
    /* if true, gateway will start a captive portal for provisioning when
     * WiFi creds are missing or invalid */
    bool     captive_portal_on_failure;
} agrinet_gw_config_t;

typedef struct {
    uint16_t short_addr;        /* network short address (0x0000 = unjoined) */
    uint8_t  ieee_addr[8];
    uint8_t  endpoint;
    uint8_t  device_class;      /* agrinet_device_class_t */
    char     node_id[16];       /* short ASCII identifier derived from ieee */
    uint32_t last_seen_ms;
    bool     online;
} agrinet_gw_node_entry_t;

typedef struct {
    agrinet_gw_state_t state;
    agrinet_gw_config_t config;
    agrinet_network_info_t net;
    agrinet_gw_node_entry_t nodes[AGINET_GW_MAX_NODES];
    uint16_t nodes_count;
    uint32_t boot_time_ms;
} agrinet_gw_runtime_t;

/* --------------------------- Public API -------------------------------- */
/**
 * @brief Initialise the gateway runtime, load config from NVS, set defaults.
 *        Must be called once before any other gateway function.
 */
esp_err_t app_gateway_init(void);

/**
 * @brief Start the gateway: WiFi -> MQTT -> Zigbee coordinator.
 *        Blocks until the coordinator is formed or an unrecoverable error.
 */
esp_err_t app_gateway_start(void);

/**
 * @brief Get pointer to the runtime state (read-only for callers).
 */
const agrinet_gw_runtime_t *app_gateway_get_runtime(void);

/**
 * @brief Register (or refresh) a node in the gateway's node table.
 *        Called from the Zigbee callback when a device joins or is
 *        discovered via ZDO.
 */
esp_err_t app_gateway_register_node(uint16_t short_addr,
                                    const uint8_t ieee_addr[8],
                                    uint8_t endpoint,
                                    uint8_t device_class);

/**
 * @brief Mark a node offline (called after a missed heartbeat timeout).
 */
esp_err_t app_gateway_mark_node_offline(uint16_t short_addr);

/**
 * @brief Forward a sensor snapshot received from a Zigbee node to the
 *        MQTT broker on the appropriate topic.
 */
esp_err_t app_gateway_publish_sensor(const char *node_id,
                                     const agrinet_sensor_snapshot_t *snap);

/**
 * @brief Forward an actuator state change received from a node (or
 *        initiated locally) to MQTT.
 */
esp_err_t app_gateway_publish_actuator(const char *node_id,
                                       const agrinet_actuator_state_t *state);

/**
 * @brief Publish an alert for a given node.
 */
esp_err_t app_gateway_publish_alert(const char *node_id, uint32_t mask);

/**
 * @brief Send an actuator command (received from MQTT) to a Zigbee node
 *        by issuing a ZCL on/off (and level) command on the right endpoint.
 */
esp_err_t app_gateway_send_actuator_cmd(const char *node_id,
                                        const agrinet_actuator_state_t *state,
                                        uint32_t changed_mask);

/**
 * @brief Periodic heartbeat - called every second from the gateway main
 *        task. Updates uptime, checks node timeouts, republishes state.
 */
void app_gateway_heartbeat(void);

#ifdef __cplusplus
}
#endif
