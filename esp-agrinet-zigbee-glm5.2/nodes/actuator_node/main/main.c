/*
 * ESP-AgriNet Zigbee - Actuator node firmware entry point
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * ESP32-H2 router firmware that exposes 5 actuator endpoints:
 *   EP11 = pump      (on/off + level)
 *   EP12 = fan       (on/off + level)
 *   EP13 = grow light (on/off + level)
 *   EP14 = heater    (on/off only)
 *   EP15 = window    (on/off only - servo)
 *
 * Receives ZCL on/off and level-control commands from the gateway and
 * applies them to the physical outputs via app_actuators_apply().
 *
 * Hardware: ESP32-H2-DevKitM-1 + 4 relay modules + 1 servo + LED driver
 *           Mains-powered (always-on router)
 */
#include "app_actuators.h"
#include "agrinet_log.h"
#include "agrinet_types.h"
#include "agrinet_clusters.h"

#include "esp_zigbee.h"
#include "esp_zigbee_nwk.h"
#include "esp_zigbee_zcl_command.h"
#include "esp_zigbee_attribute.h"
#include "esp_zigbee_endpoint.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = AGRINET_LOG_TAG_ACT;
static agrinet_actuator_state_t s_act_state = {0};

/* --------------------------------------------------------------------- */
/* Helper to build an actuator endpoint (on/off + optional level)        */
/* --------------------------------------------------------------------- */
static void add_actuator_endpoint(esp_zb_ep_list_t *ep_list, uint8_t ep_id,
                                  uint8_t device_id, bool has_level)
{
    esp_zb_cluster_list_t *cl = esp_zb_cluster_list_create();

    /* Basic */
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_MAINS_SINGLE_PHASE_0,
    };
    esp_zb_attribute_list_t *basic = esp_zb_basic_cluster_create(&basic_cfg);
    esp_zb_cluster_list_add_basic_cluster(cl, basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Identify */
    esp_zb_identify_cluster_cfg_t id_cfg = {0};
    esp_zb_attribute_list_t *identify = esp_zb_identify_cluster_create(&id_cfg);
    esp_zb_cluster_list_add_identify_cluster(cl, identify, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* On/off */
    esp_zb_on_off_cluster_cfg_t onoff_cfg = { .on_off = ESP_ZB_ZCL_ON_OFF_OFF };
    esp_zb_attribute_list_t *onoff = esp_zb_on_off_cluster_create(&onoff_cfg);
    esp_zb_cluster_list_add_on_off_cluster(cl, onoff, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Level control (optional) */
    if (has_level) {
        esp_zb_level_control_cluster_cfg_t lvl_cfg = { .current_level = 0 };
        esp_zb_attribute_list_t *level = esp_zb_level_control_cluster_create(&lvl_cfg);
        esp_zb_cluster_list_add_level_control_cluster(cl, level, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    }

    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint = ep_id,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = device_id,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, cl, ep_cfg);
}

/* --------------------------------------------------------------------- */
/* ZCL command handler (on/off and level)                                */
/* --------------------------------------------------------------------- */
static void actuator_zcl_cb(esp_zb_core_action_callback_id_t cb_id,
                            const void *message)
{
    if (cb_id != ESP_ZB_CORE_CMD_ON_OFF_CB_ID &&
        cb_id != ESP_ZB_CORE_CMD_LEVEL_CONTROL_MOVE_TO_LEVEL_CB_ID) {
        return;
    }
    /* The esp-zigbee-sdk exposes the source endpoint and value via the
     * callback params. We use the endpoint to map to our actuators. */

    /* Map the endpoint back to the right actuator */
    uint8_t ep = 0;
    uint32_t changed = 0;
    agrinet_actuator_state_t st = s_act_state;

    if (cb_id == ESP_ZB_CORE_CMD_ON_OFF_CB_ID) {
        const esp_zb_zcl_on_off_message_t *msg =
            (const esp_zb_zcl_on_off_message_t *)message;
        ep = msg->zcl_basic_cmd.dst_endpoint;
        bool on = (msg->command == ESP_ZB_ZCL_CMD_ON_OFF_ON_ID);
        AG_LOGI(TAG, "on/off cmd ep=%d on=%d", ep, on);
        switch (ep) {
            case AGRINET_EP_ACTUATOR_PUMP:   st.pump = on ? AGRINET_ACT_ON : AGRINET_ACT_OFF;
                if (st.pump_level == 0) st.pump_level = 100;
                changed = AGRINET_ACT_CHANGE_PUMP; break;
            case AGRINET_EP_ACTUATOR_FAN:    st.fan = on ? AGRINET_ACT_ON : AGRINET_ACT_OFF;
                if (st.fan_speed == 0) st.fan_speed = 100;
                changed = AGRINET_ACT_CHANGE_FAN; break;
            case AGRINET_EP_ACTUATOR_LIGHT:  st.grow_light = on ? AGRINET_ACT_ON : AGRINET_ACT_OFF;
                if (st.grow_light_level == 0) st.grow_light_level = 100;
                changed = AGRINET_ACT_CHANGE_LIGHT; break;
            case AGRINET_EP_ACTUATOR_HEATER: st.heater = on ? AGRINET_ACT_ON : AGRINET_ACT_OFF;
                changed = AGRINET_ACT_CHANGE_HEATER; break;
            case AGRINET_EP_ACTUATOR_WINDOW: st.window = on ? AGRINET_ACT_ON : AGRINET_ACT_OFF;
                changed = AGRINET_ACT_CHANGE_WINDOW; break;
            default: return;
        }
        app_actuators_apply(&st, changed);
        s_act_state = st;

        /* Update the on/off attribute to confirm state */
        uint8_t onoff_val = on ? ESP_ZB_ZCL_ON_OFF_ON : ESP_ZB_ZCL_ON_OFF_OFF;
        esp_zb_zcl_set_attribute_val(ep, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
            ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &onoff_val, false);
    } else if (cb_id == ESP_ZB_CORE_CMD_LEVEL_CONTROL_MOVE_TO_LEVEL_CB_ID) {
        const esp_zb_zcl_move_to_level_message_t *msg =
            (const esp_zb_zcl_move_to_level_message_t *)message;
        ep = msg->zcl_basic_cmd.dst_endpoint;
        uint8_t level = msg->level;
        AG_LOGI(TAG, "level cmd ep=%d level=%u", ep, level);
        switch (ep) {
            case AGRINET_EP_ACTUATOR_PUMP:   st.pump_level = level;
                if (level > 0) st.pump = AGRINET_ACT_ON;
                changed = AGRINET_ACT_CHANGE_PUMP; break;
            case AGRINET_EP_ACTUATOR_FAN:    st.fan_speed = level;
                if (level > 0) st.fan = AGRINET_ACT_ON;
                changed = AGRINET_ACT_CHANGE_FAN; break;
            case AGRINET_EP_ACTUATOR_LIGHT:  st.grow_light_level = level;
                if (level > 0) st.grow_light = AGRINET_ACT_ON;
                changed = AGRINET_ACT_CHANGE_LIGHT; break;
            default: return;
        }
        app_actuators_apply(&st, changed);
        s_act_state = st;

        /* Update the level attribute */
        esp_zb_zcl_set_attribute_val(ep, ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
            ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID, &level, false);
    }
}

/* --------------------------------------------------------------------- */
/* Zigbee stack status callback                                          */
/* --------------------------------------------------------------------- */
static void actuator_zb_stack_status(esp_zb_zdo_signal_type_t sig,
                                     esp_zb_zdo_signal_cb_params_t *params)
{
    (void)params;
    switch (sig) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        AG_LOGI(TAG, "Zigbee stack initialised, joining network...");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        AG_LOGI(TAG, "network steering complete");
        break;
    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        AG_LOGW(TAG, "left network");
        break;
    default:
        AG_LOGD(TAG, "zb signal 0x%02x", sig);
        break;
    }
}

/* --------------------------------------------------------------------- */
/* Endpoint registration                                                 */
/* --------------------------------------------------------------------- */
static void register_actuator_endpoints(void)
{
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    add_actuator_endpoint(ep_list, AGRINET_EP_ACTUATOR_PUMP,
        ESP_ZB_HA_COLOR_DIMMABLE_LIGHT_DEVICE_ID, true);     /* level */
    add_actuator_endpoint(ep_list, AGRINET_EP_ACTUATOR_FAN,
        ESP_ZB_HA_FAN_DEVICE_ID, true);                      /* level */
    add_actuator_endpoint(ep_list, AGRINET_EP_ACTUATOR_LIGHT,
        ESP_ZB_HA_DIMMABLE_LIGHT_DEVICE_ID, true);           /* level */
    add_actuator_endpoint(ep_list, AGRINET_EP_ACTUATOR_HEATER,
        ESP_ZB_HA_HEATING_COOLING_UNIT_DEVICE_ID, false);
    add_actuator_endpoint(ep_list, AGRINET_EP_ACTUATOR_WINDOW,
        ESP_ZB_HA_WINDOW_COVERING_DEVICE_ID, false);
    esp_zb_device_register(ep_list);

    /* Register ZCL command callbacks */
    esp_zb_core_action_handler_register(actuator_zcl_cb);
    esp_zb_device_add_custom_cb(ESP_ZB_CORE_CMD_ON_OFF_CB_ID, NULL);
    esp_zb_device_add_custom_cb(ESP_ZB_CORE_CMD_LEVEL_CONTROL_MOVE_TO_LEVEL_CB_ID, NULL);

    AG_LOGI(TAG, "actuator endpoints registered (ep %d-%d)",
            AGRINET_EP_ACTUATOR_PUMP, AGRINET_EP_ACTUATOR_WINDOW);
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
    ESP_ERROR_CHECK(esp_zb_platform_configure(&config));

    /* Router config (mains-powered) */
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZR_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    register_actuator_endpoints();
    esp_zb_register_stack_status_handler(actuator_zb_stack_status);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_main_loop_iteration();
}
