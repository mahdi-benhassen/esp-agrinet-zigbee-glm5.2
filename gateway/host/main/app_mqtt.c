/*
 * ESP-AgriNet Zigbee - Gateway MQTT implementation
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * Wraps esp-mqtt in a small publisher/subscriber facade with a single
 * callback for actuator commands.
 */
#include "app_mqtt.h"
#include "app_gateway.h"
#include "agrinet_log.h"

#include "mqtt_client.h"
#include "esp_event.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_CMDS 8
#define NODE_ID_LEN  16
#define ACT_NAME_LEN 12

static const char *TAG = AGRINET_LOG_TAG_MQTT;
static esp_mqtt_client_handle_t s_client = NULL;
static char s_broker_uri[128];
static char s_client_id[32];
static bool s_connected = false;
static app_mqtt_actuator_cmd_cb_t s_actuator_cb = NULL;

extern char g_site_id[16];

/* --------------------------------------------------------------------- */
static void mqtt_event_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    esp_mqtt_event_handle_t event = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        AG_LOGI(TAG, "connected to broker %s as %s", s_broker_uri, s_client_id);
        /* Subscribe to the wildcard actuator command topic */
        char topic[96];
        snprintf(topic, sizeof(topic), "agrinet/%s/nodes/+/actuators/+/set", g_site_id);
        esp_mqtt_client_subscribe(s_client, topic, 1);
        AG_LOGI(TAG, "subscribed to %s", topic);
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        AG_LOGW(TAG, "disconnected from broker");
        break;

    case MQTT_EVENT_DATA:
    {
        /* Topic: agrinet/<site>/nodes/<node>/actuators/<act>/set */
        AG_LOGD(TAG, "topic=%.*s payload=%.*s",
                event->topic_len, event->topic, event->data_len, event->data);

        char topic_buf[128];
        if (event->topic_len >= sizeof(topic_buf)) break;
        memcpy(topic_buf, event->topic, event->topic_len);
        topic_buf[event->topic_len] = '\0';

        /* Parse node_id and actuator name out of the topic */
        char node_id[NODE_ID_LEN] = {0};
        char act_name[ACT_NAME_LEN] = {0};
        /* expected: agrinet/<site>/nodes/<node>/actuators/<act>/set */
        char *p = topic_buf;
        char *tokens[8];
        int tok_count = 0;
        char *tok = strtok(p, "/");
        while (tok && tok_count < 8) {
            tokens[tok_count++] = tok;
            tok = strtok(NULL, "/");
        }
        if (tok_count >= 6 && strcmp(tokens[0], "agrinet") == 0 &&
            strcmp(tokens[2], "nodes") == 0 &&
            strcmp(tokens[4], "actuators") == 0 &&
            strcmp(tokens[6], "set") == 0) {
            strncpy(node_id, tokens[3], sizeof(node_id) - 1);
            strncpy(act_name, tokens[5], sizeof(act_name) - 1);
        } else {
            AG_LOGW(TAG, "unrecognised command topic: %s", topic_buf);
            break;
        }

        /* Parse the payload as an actuator command */
        agrinet_actuator_state_t st = {0};
        uint32_t changed = 0;
        if (agrinet_mqtt_parse_actuator_cmd(event->data, event->data_len,
                                            &st, &changed) == 0) {
            AG_LOGI(TAG, "cmd node=%s act=%s changed=0x%lx",
                    node_id, act_name, (unsigned long)changed);
            if (s_actuator_cb) {
                s_actuator_cb(node_id, act_name, &st, changed);
            }
        } else {
            AG_LOGW(TAG, "failed to parse actuator cmd payload");
        }
        break;
    }

    case MQTT_EVENT_ERROR:
        AG_LOGE(TAG, "MQTT_ERROR: type=%d", event->error_handle->error_type);
        break;

    default:
        break;
    }
}

/* --------------------------------------------------------------------- */
esp_err_t app_mqtt_init(const char *broker_uri, const char *client_id)
{
    if (!broker_uri || !client_id) return ESP_ERR_INVALID_ARG;
    strncpy(s_broker_uri, broker_uri, sizeof(s_broker_uri) - 1);
    strncpy(s_client_id, client_id, sizeof(s_client_id) - 1);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = s_broker_uri,
        .credentials.client_id = s_client_id,
        .network.timeout_ms = 5000,
        .buffer.size = 1024,
        .buffer.out_size = 1024,
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        AG_LOGE(TAG, "failed to init mqtt client");
        return ESP_FAIL;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    AG_LOGI(TAG, "mqtt client initialised (%s)", s_broker_uri);
    return ESP_OK;
}

esp_err_t app_mqtt_start(void)
{
    if (!s_client) return ESP_ERR_INVALID_STATE;
    return esp_mqtt_client_start(s_client);
}

esp_err_t app_mqtt_publish(const char *topic, const char *payload, int qos)
{
    if (!s_client || !s_connected) return ESP_ERR_INVALID_STATE;
    int id = esp_mqtt_client_publish(s_client, topic, payload, 0, qos, 0);
    if (id < 0) {
        AG_LOGW(TAG, "publish failed on %s", topic);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t app_mqtt_register_actuator_cmd_cb(app_mqtt_actuator_cmd_cb_t cb)
{
    s_actuator_cb = cb;
    return ESP_OK;
}

bool app_mqtt_is_connected(void) { return s_connected; }
