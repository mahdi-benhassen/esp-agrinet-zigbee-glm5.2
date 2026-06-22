/*
 * ESP-AgriNet Zigbee - Actuator node firmware entry point
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * ESP32-H2 router firmware that exposes 5 actuator endpoints:
 *   EP11 = pump      (on/off)
 *   EP12 = fan       (on/off)
 *   EP13 = grow light (on/off)
 *   EP14 = heater    (on/off)
 *   EP15 = window    (on/off - servo)
 *
 * Receives ZCL on/off commands from the gateway and applies them
 * to the physical outputs via app_actuators_apply().
 */
#include "app_actuators.h"
#include "agrinet_log.h"
#include "agrinet_types.h"
#include "agrinet_mqtt_schema.h"
#include "agrinet_clusters.h"

#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = AGRINET_LOG_TAG_ACT;
static agrinet_actuator_state_t s_act_state = {0};

/* Zigbee router configuration macros (native radio on ESP32-H2) */
#define MAX_CHILDREN                    10
#define INSTALLCODE_POLICY_ENABLE       false
#define ESP_ZB_PRIMARY_CHANNEL_MASK     ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK

#define ESP_ZB_ZR_CONFIG() { \
    .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER, \
    .install_code_policy = INSTALLCODE_POLICY_ENABLE, \
    .nwk_cfg.zczr_cfg = { .max_children = MAX_CHILDREN, }, \
}

#define ESP_ZB_DEFAULT_RADIO_CONFIG() { .radio_mode = RADIO_MODE_NATIVE, }
#define ESP_ZB_DEFAULT_HOST_CONFIG() { .host_connection_mode = HOST_CONNECTION_MODE_NONE, }

/* --------------------------------------------------------------------- */
/* Helper to build an actuator endpoint (on/off only)                     */
/* --------------------------------------------------------------------- */
static void add_actuator_endpoint(esp_zb_ep_list_t *ep_list, uint8_t ep_id, uint16_t device_id)
{
    esp_zb_cluster_list_t *cl = esp_zb_zcl_cluster_list_create();

    /* Basic */
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE,
    };
    esp_zb_attribute_list_t *basic = esp_zb_basic_cluster_create(&basic_cfg);
    esp_zb_cluster_list_add_basic_cluster(cl, basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Identify */
    esp_zb_identify_cluster_cfg_t id_cfg = {0};
    esp_zb_attribute_list_t *identify = esp_zb_identify_cluster_create(&id_cfg);
    esp_zb_cluster_list_add_identify_cluster(cl, identify, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* On/off */
    esp_zb_on_off_cluster_cfg_t onoff_cfg = { .on_off = 0 };
    esp_zb_attribute_list_t *onoff = esp_zb_on_off_cluster_create(&onoff_cfg);
    esp_zb_cluster_list_add_on_off_cluster(cl, onoff, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_ep_list_add_ep(ep_list, cl, ep_id, ESP_ZB_AF_HA_PROFILE_ID, device_id);
}

/* --------------------------------------------------------------------- */
/* ZCL command handler (on/off)                                          */
/* --------------------------------------------------------------------- */
static esp_err_t actuator_zcl_cb(esp_zb_core_action_callback_id_t cb_id,
                                 const void *message)
{
    if (cb_id == ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) {
        const esp_zb_zcl_set_attr_value_message_t *msg =
            (const esp_zb_zcl_set_attr_value_message_t *)message;
        uint8_t ep = msg->info.dst_endpoint;
        bool on = false;

        if (msg->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF &&
            msg->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID &&
            msg->attribute.data.value) {
            on = *(bool *)msg->attribute.data.value;
        }

        AG_LOGI(TAG, "on/off cmd ep=%d on=%d", ep, on);

        agrinet_actuator_state_t st = s_act_state;
        uint32_t changed = 0;
        switch (ep) {
            case AGRINET_EP_ACTUATOR_PUMP:   st.pump = on ? AGRINET_ACT_ON : AGRINET_ACT_OFF;
                changed = AGRINET_ACT_CHANGE_PUMP; break;
            case AGRINET_EP_ACTUATOR_FAN:    st.fan = on ? AGRINET_ACT_ON : AGRINET_ACT_OFF;
                changed = AGRINET_ACT_CHANGE_FAN; break;
            case AGRINET_EP_ACTUATOR_LIGHT:  st.grow_light = on ? AGRINET_ACT_ON : AGRINET_ACT_OFF;
                changed = AGRINET_ACT_CHANGE_LIGHT; break;
            case AGRINET_EP_ACTUATOR_HEATER: st.heater = on ? AGRINET_ACT_ON : AGRINET_ACT_OFF;
                changed = AGRINET_ACT_CHANGE_HEATER; break;
            case AGRINET_EP_ACTUATOR_WINDOW: st.window = on ? AGRINET_ACT_ON : AGRINET_ACT_OFF;
                changed = AGRINET_ACT_CHANGE_WINDOW; break;
            default: return ESP_OK;
        }
        app_actuators_apply(&st, changed);
        s_act_state = st;
    }
    return ESP_OK;
}

/* --------------------------------------------------------------------- */
/* Zigbee app signal handler                                             */
/* --------------------------------------------------------------------- */
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p       = signal_struct->p_app_signal;
    esp_err_t err_status   = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        AG_LOGI(TAG, "Zigbee stack initialized, joining network...");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            AG_LOGI(TAG, "Device started up in %s factory-reset mode",
                    esp_zb_bdb_is_factory_new() ? "" : "non");
            if (esp_zb_bdb_is_factory_new()) {
                AG_LOGI(TAG, "Start network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                AG_LOGI(TAG, "Device rebooted");
            }
        } else {
            AG_LOGW(TAG, "Failed to initialize Zigbee stack (status: %s)",
                    esp_err_to_name(err_status));
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            AG_LOGI(TAG, "Joined network successfully");
        } else {
            AG_LOGI(TAG, "Network steering was not successful (status: %s)",
                    esp_err_to_name(err_status));
        }
        break;
    default:
        AG_LOGI(TAG, "ZDO signal: 0x%x, status: %s", sig_type, esp_err_to_name(err_status));
        break;
    }
}

/* --------------------------------------------------------------------- */
/* Zigbee task                                                            */
/* --------------------------------------------------------------------- */
static void esp_zb_task(void *pvParameters)
{
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZR_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    /* Register actuator endpoints */
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    add_actuator_endpoint(ep_list, AGRINET_EP_ACTUATOR_PUMP,   ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID);
    add_actuator_endpoint(ep_list, AGRINET_EP_ACTUATOR_FAN,    ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID);
    add_actuator_endpoint(ep_list, AGRINET_EP_ACTUATOR_LIGHT,  ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID);
    add_actuator_endpoint(ep_list, AGRINET_EP_ACTUATOR_HEATER, ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID);
    add_actuator_endpoint(ep_list, AGRINET_EP_ACTUATOR_WINDOW, ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID);
    ESP_ERROR_CHECK(esp_zb_device_register(ep_list));

    esp_zb_core_action_handler_register(actuator_zcl_cb);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_main_loop_iteration();
}

/* --------------------------------------------------------------------- */
/* App main                                                              */
/* --------------------------------------------------------------------- */
void app_main(void)
{
    AG_LOGI(TAG, "");
    AG_LOGI(TAG, "============================================");
    AG_LOGI(TAG, "  ESP-AgriNet Actuator Node v%s", AGRINET_VERSION_STRING);
    AG_LOGI(TAG, "  ESP32-H2 + 4 relays + LED driver + servo");
    AG_LOGI(TAG, "============================================");
    AG_LOGI(TAG, "");

    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* Actuators */
    ESP_ERROR_CHECK(app_actuators_init());

    /* Zigbee platform */
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config  = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    /* Start Zigbee task */
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
