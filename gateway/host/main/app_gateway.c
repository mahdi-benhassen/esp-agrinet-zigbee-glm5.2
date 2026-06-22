/*
 * ESP-AgriNet Zigbee - Gateway application
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * Glue layer: loads config from NVS, brings up WiFi, MQTT, and the Zigbee
 * coordinator (talking to the ESP32-H2 RCP over UART), and forwards
 * sensor/actuator data between the Zigbee network and MQTT.
 */
#include "app_gateway.h"
#include "app_wifi.h"
#include "app_mqtt.h"
#include "agrinet_log.h"
#include "agrinet_clusters.h"

#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdio.h>

/* Zigbee gateway configuration macros (for ESP32-S3 host + ESP32-H2 RCP) */
#define MAX_CHILDREN                    10
#define INSTALLCODE_POLICY_ENABLE       false
#define ESP_ZB_PRIMARY_CHANNEL_MASK     (1l << 15)

#define ESP_ZB_ZC_CONFIG() { \
    .esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR, \
    .install_code_policy = INSTALLCODE_POLICY_ENABLE, \
    .nwk_cfg.zczr_cfg = { .max_children = MAX_CHILDREN, }, \
}

#define ESP_ZB_DEFAULT_RADIO_CONFIG() { \
    .radio_mode = RADIO_MODE_UART_RCP, \
    .radio_uart_config = { \
        .port = 1, \
        .uart_config = { \
            .baud_rate = 115200, \
            .data_bits = UART_DATA_8_BITS, \
            .parity = UART_PARITY_DISABLE, \
            .stop_bits = UART_STOP_BITS_1, \
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, \
            .rx_flow_ctrl_thresh = 0, \
            .source_clk = UART_SCLK_DEFAULT, \
        }, \
        .rx_pin = 4, \
        .tx_pin = 5, \
    }, \
}

#define ESP_ZB_DEFAULT_HOST_CONFIG() { \
    .host_connection_mode = HOST_CONNECTION_MODE_NONE, \
}

static const char *TAG = AGRINET_LOG_TAG_GATEWAY;

/* --------------------------------------------------------------------- */
/* Globals shared with app_wifi.c / app_mqtt.c                            */
/* --------------------------------------------------------------------- */
char g_site_id[16]   = "gh1";
char g_mqtt_uri[128] = AGRINET_MQTT_DEFAULT_BROKER_URI;
char g_wifi_ssid[32] = {0};
char g_wifi_pass[64] = {0};

/* --------------------------------------------------------------------- */
static agrinet_gw_runtime_t s_rt = {0};

const agrinet_gw_runtime_t *app_gateway_get_runtime(void) { return &s_rt; }

/* --------------------------------------------------------------------- */
/* NVS load / save                                                       */
/* --------------------------------------------------------------------- */
static esp_err_t load_config_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(AGRINET_GW_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        AG_LOGW(TAG, "no saved config in NVS - using defaults");
        return ESP_ERR_NOT_FOUND;
    }
    size_t len;
    len = sizeof(g_site_id);
    nvs_get_str(h, "site_id", g_site_id, &len);
    len = sizeof(g_mqtt_uri);
    nvs_get_str(h, "mqtt_uri", g_mqtt_uri, &len);
    len = sizeof(g_wifi_ssid);
    nvs_get_str(h, "wifi_ssid", g_wifi_ssid, &len);
    len = sizeof(g_wifi_pass);
    nvs_get_str(h, "wifi_pass", g_wifi_pass, &len);
    uint16_t u16;
    uint8_t  u8;
    if (nvs_get_u16(h, "pan_id", &u16) == ESP_OK) s_rt.config.pan_id = u16;
    if (nvs_get_u8 (h, "channel", &u8) == ESP_OK) s_rt.config.channel = u8;
    nvs_close(h);
    AG_LOGI(TAG, "config loaded: site=%s mqtt=%s ssid=%s pan=0x%04X ch=%u",
            g_site_id, g_mqtt_uri, g_wifi_ssid, s_rt.config.pan_id, s_rt.config.channel);
    return ESP_OK;
}

esp_err_t app_gateway_save_config_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(AGRINET_GW_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return ESP_FAIL;
    }
    nvs_set_str(h, "site_id",   g_site_id);
    nvs_set_str(h, "mqtt_uri",  g_mqtt_uri);
    nvs_set_str(h, "wifi_ssid", g_wifi_ssid);
    nvs_set_str(h, "wifi_pass", g_wifi_pass);
    nvs_set_u16(h, "pan_id",  s_rt.config.pan_id);
    nvs_set_u8 (h, "channel", s_rt.config.channel);
    nvs_commit(h);
    nvs_close(h);
    AG_LOGI(TAG, "config saved to NVS");
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
esp_err_t app_gateway_init(void)
{
    memset(&s_rt, 0, sizeof(s_rt));
    s_rt.state = AGRINET_GW_STATE_BOOTING;
    s_rt.config.pan_id   = AGRINET_DEFAULT_PAN_ID;
    s_rt.config.channel  = AGRINET_DEFAULT_CHANNEL;
    s_rt.config.captive_portal_on_failure = true;
    strncpy(s_rt.config.site_id, "gh1", sizeof(s_rt.config.site_id) - 1);
    strncpy(s_rt.config.mqtt_uri, AGRINET_MQTT_DEFAULT_BROKER_URI,
            sizeof(s_rt.config.mqtt_uri) - 1);
    s_rt.boot_time_ms = esp_timer_get_time() / 1000;

    /* NVS init */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    load_config_nvs();

    /* Copy config globals for app_wifi / app_mqtt */
    strncpy(s_rt.config.site_id, g_site_id, sizeof(s_rt.config.site_id) - 1);
    strncpy(s_rt.config.mqtt_uri, g_mqtt_uri, sizeof(s_rt.config.mqtt_uri) - 1);
    strncpy(s_rt.config.wifi_ssid, g_wifi_ssid, sizeof(s_rt.config.wifi_ssid) - 1);
    strncpy(s_rt.config.wifi_pass, g_wifi_pass, sizeof(s_rt.config.wifi_pass) - 1);

    AG_LOGI(TAG, "agrinet gateway v%s initialised", AGRINET_VERSION_STRING);
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
/* Zigbee app signal handler (matches official ESP-IDF v5.2.3 pattern)   */
/* --------------------------------------------------------------------- */
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p       = signal_struct->p_app_signal;
    esp_err_t err_status   = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        AG_LOGI(TAG, "Zigbee stack initialized, forming network...");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
        break;
    case ESP_ZB_BDB_SIGNAL_FORMATION:
        if (err_status == ESP_OK) {
            s_rt.net.pan_id   = esp_zb_get_pan_id();
            s_rt.net.channel  = esp_zb_get_current_channel();
            esp_zb_ieee_addr_t ieee_addr;
            esp_zb_get_long_address(ieee_addr);
            AG_LOGI(TAG, "Formed network successfully (PAN: 0x%04hx, Channel: %d, Short: 0x%04hx)",
                     s_rt.net.pan_id, s_rt.net.channel, esp_zb_get_short_address());
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            s_rt.state = AGRINET_GW_STATE_RUNNING;
        } else {
            AG_LOGI(TAG, "Restart network formation (status: %s)", esp_err_to_name(err_status));
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            AG_LOGI(TAG, "Network steering started");
        }
        break;
    case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE: {
        esp_zb_zdo_signal_device_annce_params_t *dev_annce_params =
            (esp_zb_zdo_signal_device_annce_params_t *)esp_zb_app_signal_get_params(p_sg_p);
        AG_LOGI(TAG, "New device commissioned (short: 0x%04hx)",
                dev_annce_params->device_short_addr);
        break;
    }
    case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
        if (err_status == ESP_OK) {
            if (*(uint8_t *)esp_zb_app_signal_get_params(p_sg_p)) {
                AG_LOGI(TAG, "Network(0x%04hx) is open for joining", esp_zb_get_pan_id());
            } else {
                AG_LOGW(TAG, "Network(0x%04hx) closed", esp_zb_get_pan_id());
            }
        }
        break;
    default:
        AG_LOGI(TAG, "ZDO signal: 0x%x, status: %s", sig_type, esp_err_to_name(err_status));
        break;
    }
}

/* --------------------------------------------------------------------- */
/* Zigbee action handler for incoming ZCL commands                        */
/* --------------------------------------------------------------------- */
static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id,
                                   const void *message)
{
    switch (callback_id) {
    case ESP_ZB_CORE_REPORT_ATTR_CB_ID: {
        const esp_zb_zcl_report_attr_message_t *rpt =
            (const esp_zb_zcl_report_attr_message_t *)message;
        AG_LOGD(TAG, "attr report ep=%d cluster=0x%04x",
                rpt->info.dst_endpoint, rpt->info.cluster);
        break;
    }
    default:
        AG_LOGD(TAG, "zb action 0x%02x", callback_id);
        break;
    }
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
/* Zigbee coordinator task                                                */
/* --------------------------------------------------------------------- */
static void esp_zb_task(void *pvParameters)
{
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZC_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    /* Register a basic endpoint for gateway telemetry */
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE,
    };
    esp_zb_attribute_list_t *basic = esp_zb_basic_cluster_create(&basic_cfg);
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_ep_list_add_ep(ep_list, cluster_list,
        AGRINET_EP_GATEWAY_TELE, ESP_ZB_AF_HA_PROFILE_ID, ESP_ZB_HA_REMOTE_CONTROL_DEVICE_ID);
    ESP_ERROR_CHECK(esp_zb_device_register(ep_list));

    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_main_loop_iteration();
}

/* --------------------------------------------------------------------- */
/* MQTT actuator command callback -> forward to Zigbee                   */
/* --------------------------------------------------------------------- */
static void on_actuator_cmd(const char *node_id, const char *actuator_name,
                            const agrinet_actuator_state_t *state,
                            uint32_t changed_mask)
{
    AG_LOGI(TAG, "forwarding cmd node=%s act=%s mask=0x%lx",
            node_id, actuator_name, (unsigned long)changed_mask);
    app_gateway_send_actuator_cmd(node_id, state, changed_mask);
}

/* --------------------------------------------------------------------- */
esp_err_t app_gateway_start(void)
{
    /* WiFi */
    app_wifi_init();
    s_rt.state = AGRINET_GW_STATE_WIFI_CONNECTING;
    app_wifi_start(s_rt.config.captive_portal_on_failure);

    /* MQTT */
    char client_id[24];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(client_id, sizeof(client_id), "agrinet-gw-%02x%02x%02x",
             mac[3], mac[4], mac[5]);
    app_mqtt_init(g_mqtt_uri, client_id);
    app_mqtt_register_actuator_cmd_cb(on_actuator_cmd);
    s_rt.state = AGRINET_GW_STATE_MQTT_CONNECTING;
    app_mqtt_start();

    /* Zigbee platform config */
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config  = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    /* Start Zigbee task */
    s_rt.state = AGRINET_GW_STATE_ZB_FORMING;
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);

    return ESP_OK;
}

/* --------------------------------------------------------------------- */
/* Node table management                                                 */
/* --------------------------------------------------------------------- */
static int find_node_by_short(uint16_t short_addr)
{
    for (int i = 0; i < s_rt.nodes_count; ++i) {
        if (s_rt.nodes[i].short_addr == short_addr) return i;
    }
    return -1;
}

static void derive_node_id(const uint8_t ieee[8], char *out, size_t out_len)
{
    snprintf(out, out_len, "node-%02x%02x%02x%02x",
             ieee[4], ieee[5], ieee[6], ieee[7]);
}

esp_err_t app_gateway_register_node(uint16_t short_addr,
                                    const uint8_t ieee_addr[8],
                                    uint8_t endpoint,
                                    uint8_t device_class)
{
    int idx = find_node_by_short(short_addr);
    if (idx < 0) {
        if (s_rt.nodes_count >= AGRINET_GW_MAX_NODES) {
            AG_LOGW(TAG, "node table full - cannot register 0x%04X", short_addr);
            return ESP_ERR_NO_MEM;
        }
        idx = s_rt.nodes_count++;
    }
    s_rt.nodes[idx].short_addr = short_addr;
    memcpy(s_rt.nodes[idx].ieee_addr, ieee_addr, 8);
    s_rt.nodes[idx].endpoint = endpoint;
    s_rt.nodes[idx].device_class = device_class;
    s_rt.nodes[idx].last_seen_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_rt.nodes[idx].online = true;
    derive_node_id(ieee_addr, s_rt.nodes[idx].node_id,
                   sizeof(s_rt.nodes[idx].node_id));
    AG_LOGI(TAG, "registered node %s addr=0x%04X ep=%d class=%d",
            s_rt.nodes[idx].node_id, short_addr, endpoint, device_class);
    return ESP_OK;
}

esp_err_t app_gateway_mark_node_offline(uint16_t short_addr)
{
    int idx = find_node_by_short(short_addr);
    if (idx < 0) return ESP_ERR_NOT_FOUND;
    s_rt.nodes[idx].online = false;
    AG_LOGW(TAG, "node %s marked offline",
            s_rt.nodes[idx].node_id);
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
/* MQTT publishing helpers                                               */
/* --------------------------------------------------------------------- */
esp_err_t app_gateway_publish_sensor(const char *node_id,
                                     const agrinet_sensor_snapshot_t *snap)
{
    char topic[128];
    char payload[AGRINET_GW_MQTT_PAYLOAD_MAX];
    snprintf(topic, sizeof(topic), AGRINET_MQTT_TOPIC_NODE_SENSOR,
             g_site_id, node_id);
    int n = agrinet_mqtt_build_sensor_json(payload, sizeof(payload),
                                           node_id, snap);
    if (n < 0) return ESP_FAIL;
    return app_mqtt_publish(topic, payload, 1);
}

esp_err_t app_gateway_publish_actuator(const char *node_id,
                                       const agrinet_actuator_state_t *state)
{
    char topic[128];
    char payload[AGRINET_GW_MQTT_PAYLOAD_MAX];
    snprintf(topic, sizeof(topic), AGRINET_MQTT_TOPIC_NODE_ACTUATOR,
             g_site_id, node_id);
    int n = agrinet_mqtt_build_actuator_json(payload, sizeof(payload),
                                             node_id, state);
    if (n < 0) return ESP_FAIL;
    return app_mqtt_publish(topic, payload, 1);
}

esp_err_t app_gateway_publish_alert(const char *node_id, uint32_t mask)
{
    char topic[128];
    char payload[256];
    snprintf(topic, sizeof(topic), AGRINET_MQTT_TOPIC_NODE_ALERT,
             g_site_id, node_id);
    int n = agrinet_mqtt_build_alert_json(payload, sizeof(payload),
                                          node_id, mask);
    if (n < 0) return ESP_FAIL;
    return app_mqtt_publish(topic, payload, 1);
}

/* --------------------------------------------------------------------- */
/* Send actuator commands to a Zigbee node                               */
/* --------------------------------------------------------------------- */
esp_err_t app_gateway_send_actuator_cmd(const char *node_id,
                                        const agrinet_actuator_state_t *state,
                                        uint32_t changed_mask)
{
    /* find the node entry */
    int idx = -1;
    for (int i = 0; i < s_rt.nodes_count; ++i) {
        if (strcmp(s_rt.nodes[i].node_id, node_id) == 0) {
            idx = i; break;
        }
    }
    if (idx < 0) {
        AG_LOGW(TAG, "unknown node %s - cannot send cmd", node_id);
        return ESP_ERR_NOT_FOUND;
    }
    agrinet_gw_node_entry_t *node = &s_rt.nodes[idx];

    /* Map actuator -> endpoint */
    uint8_t endpoint = 0;
    const char *actuator = "unknown";
    if (changed_mask & AGRINET_ACT_CHANGE_PUMP) {
        endpoint = AGRINET_EP_ACTUATOR_PUMP; actuator = "pump";
    } else if (changed_mask & AGRINET_ACT_CHANGE_FAN) {
        endpoint = AGRINET_EP_ACTUATOR_FAN; actuator = "fan";
    } else if (changed_mask & AGRINET_ACT_CHANGE_LIGHT) {
        endpoint = AGRINET_EP_ACTUATOR_LIGHT; actuator = "light";
    } else if (changed_mask & AGRINET_ACT_CHANGE_HEATER) {
        endpoint = AGRINET_EP_ACTUATOR_HEATER; actuator = "heater";
    } else if (changed_mask & AGRINET_ACT_CHANGE_WINDOW) {
        endpoint = AGRINET_EP_ACTUATOR_WINDOW; actuator = "window";
    }

    /* Determine on/off state */
    bool act_on = false;
    if (changed_mask & AGRINET_ACT_CHANGE_PUMP)    act_on = (state->pump == AGRINET_ACT_ON);
    if (changed_mask & AGRINET_ACT_CHANGE_FAN)     act_on = (state->fan == AGRINET_ACT_ON);
    if (changed_mask & AGRINET_ACT_CHANGE_LIGHT)   act_on = (state->grow_light == AGRINET_ACT_ON);
    if (changed_mask & AGRINET_ACT_CHANGE_HEATER)  act_on = (state->heater == AGRINET_ACT_ON);
    if (changed_mask & AGRINET_ACT_CHANGE_WINDOW)  act_on = (state->window == AGRINET_ACT_ON);

    /* Send ZCL On/Off command to the actuator endpoint */
    esp_zb_zcl_on_off_cmd_t cmd = {
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .zcl_basic_cmd.src_endpoint = AGRINET_EP_GATEWAY_TELE,
        .zcl_basic_cmd.dst_endpoint = endpoint,
        .zcl_basic_cmd.dst_addr_u.addr_short = node->short_addr,
        .cmd_id = act_on ? ESP_ZB_ZCL_CMD_ON_OFF_ON_ID
                         : ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID,
    };
    esp_zb_zcl_on_off_cmd_req(&cmd);
    AG_LOGI(TAG, "sent ZCL %s to node %s endpoint %d (%s)",
            act_on ? "ON" : "OFF", node_id, endpoint, actuator);

    /* If a level was specified, also send a Level Control move-to-level */
    uint8_t level = 0;
    bool has_level = false;
    if (changed_mask & AGRINET_ACT_CHANGE_PUMP) {
        if (state->pump_level > 0) { level = state->pump_level; has_level = true; }
    } else if (changed_mask & AGRINET_ACT_CHANGE_FAN) {
        if (state->fan_speed > 0)  { level = state->fan_speed;  has_level = true; }
    } else if (changed_mask & AGRINET_ACT_CHANGE_LIGHT) {
        if (state->grow_light_level > 0) { level = state->grow_light_level; has_level = true; }
    }
    if (has_level) {
        esp_zb_zcl_level_move_to_level_cmd_t lvl = {
            .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
            .zcl_basic_cmd.src_endpoint = AGRINET_EP_GATEWAY_TELE,
            .zcl_basic_cmd.dst_endpoint = endpoint,
            .zcl_basic_cmd.dst_addr_u.addr_short = node->short_addr,
            .level = level,
            .transition_time = 0,
        };
        esp_zb_zcl_level_move_to_level_cmd_req(&lvl);
        AG_LOGI(TAG, "sent ZCL level=%u to %s ep %d", level, node_id, endpoint);
    }

    /* Confirm by publishing the updated state back to MQTT */
    app_gateway_publish_actuator(node_id, state);
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
/* Periodic heartbeat                                                    */
/* --------------------------------------------------------------------- */
void app_gateway_heartbeat(void)
{
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    /* Check node timeouts (5 minutes) */
    for (int i = 0; i < s_rt.nodes_count; ++i) {
        if (s_rt.nodes[i].online &&
            (now_ms - s_rt.nodes[i].last_seen_ms) > (5 * 60 * 1000)) {
            s_rt.nodes[i].online = false;
            AG_LOGW(TAG, "node %s timed out (last seen %lu ms ago)",
                    s_rt.nodes[i].node_id,
                    (unsigned long)(now_ms - s_rt.nodes[i].last_seen_ms));
        }
    }

    /* Publish gateway state every 60 seconds */
    static uint32_t last_state_pub = 0;
    if (now_ms - last_state_pub > 60000) {
        last_state_pub = now_ms;
        char topic[128];
        char payload[512];
        snprintf(topic, sizeof(topic), AGRINET_MQTT_TOPIC_GATEWAY_STATE, g_site_id);
        uint32_t uptime = (now_ms - s_rt.boot_time_ms) / 1000;
        agrinet_mqtt_build_gateway_state_json(payload, sizeof(payload),
            g_site_id, &s_rt.net, uptime, s_rt.nodes_count);
        app_mqtt_publish(topic, payload, 1);
    }
}
