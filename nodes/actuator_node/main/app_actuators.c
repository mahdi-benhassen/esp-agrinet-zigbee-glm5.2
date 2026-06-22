/*
 * ESP-AgriNet Zigbee - Actuator node driver implementation
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * GPIO + LEDC PWM drivers for pump, fan, light, heater, window servo.
 */
#include "app_actuators.h"
#include "agrinet_log.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = AGRINET_LOG_TAG_ACT;
static agrinet_actuator_state_t s_state = {0};

/* --------------------------------------------------------------------- */
static esp_err_t ledc_setup_channel(ledc_channel_t chan, int gpio, int freq_hz)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = AGRINET_LEDC_TIMER,
        .duty_resolution = AGRINET_LEDC_RES,
        .freq_hz         = freq_hz,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    /* Use separate timer for the servo (50 Hz) */
    if (freq_hz == AGRINET_SERVO_FREQ_HZ) {
        timer_cfg.timer_num = LEDC_TIMER_1;
    }
    esp_err_t r = ledc_timer_config(&timer_cfg);
    if (r != ESP_OK) return r;

    ledc_channel_config_t ch = {
        .gpio_num   = gpio,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = chan,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = (freq_hz == AGRINET_SERVO_FREQ_HZ) ? LEDC_TIMER_1 : AGRINET_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
        .flags.output_invert = 0,
    };
    return ledc_channel_config(&ch);
}

static void set_pwm_pct(ledc_channel_t chan, uint8_t pct, bool on)
{
    uint32_t max = (1 << AGRINET_LEDC_RES) - 1;  /* 4095 for 12-bit */
    uint32_t duty = on ? (max * pct) / 100 : 0;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, chan, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, chan);
}

static void set_servo_pct(ledc_channel_t chan, uint8_t pct_open)
{
    /* pct_open: 0 (closed) .. 100 (fully open) */
    uint32_t pulse_us = AGRINET_SERVO_PULSE_CLOSED_US +
        (uint32_t)(AGRINET_SERVO_PULSE_OPEN_US - AGRINET_SERVO_PULSE_CLOSED_US) * pct_open / 100;
    /* duty = pulse_us * max / (1e6 / freq_hz) */
    uint32_t max = (1 << AGRINET_LEDC_RES) - 1;
    uint32_t duty = (uint32_t)((uint64_t)pulse_us * AGRINET_SERVO_FREQ_HZ * max) / 1000000ULL;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, chan, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, chan);
}

/* --------------------------------------------------------------------- */
esp_err_t app_actuators_init(void)
{
    memset(&s_state, 0, sizeof(s_state));

    /* Configure plain GPIO for the heater relay */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << AGRINET_HEATER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(AGRINET_HEATER_GPIO, 0);

    /* LEDC channels */
    ESP_ERROR_CHECK(ledc_setup_channel(AGRINET_PUMP_LEDC_CHAN,   AGRINET_PUMP_GPIO,   AGRINET_LEDC_FREQ_HZ));
    ESP_ERROR_CHECK(ledc_setup_channel(AGRINET_FAN_LEDC_CHAN,    AGRINET_FAN_GPIO,    AGRINET_LEDC_FREQ_HZ));
    ESP_ERROR_CHECK(ledc_setup_channel(AGRINET_LIGHT_LEDC_CHAN,  AGRINET_LIGHT_GPIO,  AGRINET_LEDC_FREQ_HZ));
    ESP_ERROR_CHECK(ledc_setup_channel(AGRINET_WINDOW_LEDC_CHAN, AGRINET_WINDOW_GPIO, AGRINET_SERVO_FREQ_HZ));

    /* Start with everything off / closed */
    set_pwm_pct(AGRINET_PUMP_LEDC_CHAN, 0, false);
    set_pwm_pct(AGRINET_FAN_LEDC_CHAN, 0, false);
    set_pwm_pct(AGRINET_LIGHT_LEDC_CHAN, 0, false);
    set_servo_pct(AGRINET_WINDOW_LEDC_CHAN, 0);

    s_state.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    AG_LOGI(TAG, "actuators initialised (pump,fan,light,heater,window)");
    return ESP_OK;
}

esp_err_t app_actuators_apply(const agrinet_actuator_state_t *state,
                              uint32_t changed_mask)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    s_state.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

    if (changed_mask & AGRINET_ACT_CHANGE_PUMP) {
        uint8_t pct = state->pump_level > 0 ? state->pump_level : 100;
        set_pwm_pct(AGRINET_PUMP_LEDC_CHAN, pct, state->pump == AGRINET_ACT_ON);
        s_state.pump = state->pump;
        s_state.pump_level = state->pump_level;
        AG_LOGI(TAG, "pump %s level=%u", state->pump == AGRINET_ACT_ON ? "ON" : "OFF", pct);
    }
    if (changed_mask & AGRINET_ACT_CHANGE_FAN) {
        uint8_t pct = state->fan_speed > 0 ? state->fan_speed : 100;
        set_pwm_pct(AGRINET_FAN_LEDC_CHAN, pct, state->fan == AGRINET_ACT_ON);
        s_state.fan = state->fan;
        s_state.fan_speed = state->fan_speed;
        AG_LOGI(TAG, "fan %s speed=%u", state->fan == AGRINET_ACT_ON ? "ON" : "OFF", pct);
    }
    if (changed_mask & AGRINET_ACT_CHANGE_LIGHT) {
        uint8_t pct = state->grow_light_level > 0 ? state->grow_light_level : 100;
        set_pwm_pct(AGRINET_LIGHT_LEDC_CHAN, pct, state->grow_light == AGRINET_ACT_ON);
        s_state.grow_light = state->grow_light;
        s_state.grow_light_level = state->grow_light_level;
        AG_LOGI(TAG, "light %s level=%u", state->grow_light == AGRINET_ACT_ON ? "ON" : "OFF", pct);
    }
    if (changed_mask & AGRINET_ACT_CHANGE_HEATER) {
        gpio_set_level(AGRINET_HEATER_GPIO, state->heater == AGRINET_ACT_ON ? 1 : 0);
        s_state.heater = state->heater;
        AG_LOGI(TAG, "heater %s", state->heater == AGRINET_ACT_ON ? "ON" : "OFF");
    }
    if (changed_mask & AGRINET_ACT_CHANGE_WINDOW) {
        set_servo_pct(AGRINET_WINDOW_LEDC_CHAN, state->window == AGRINET_ACT_ON ? 100 : 0);
        s_state.window = state->window;
        AG_LOGI(TAG, "window %s", state->window == AGRINET_ACT_ON ? "OPEN" : "CLOSED");
    }
    return ESP_OK;
}

esp_err_t app_actuators_read(agrinet_actuator_state_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = s_state;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    return ESP_OK;
}
