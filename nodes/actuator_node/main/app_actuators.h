/*
 * ESP-AgriNet Zigbee - Actuator node driver header
 * Copyright (c) 2025 ESP-AgriNet Project
 *
 * GPIO driver stack for the actuators controlled by the agrinet
 * actuator node:
 *   - Water pump      : relay + PWM (LEDC)
 *   - Ventilation fan : relay + PWM
 *   - LED grow light  : PWM dimmable LED driver
 *   - Heater          : relay
 *   - Roof window     : servo motor (LEDC PWM)
 */
#pragma once

#include "esp_err.h"
#include "agrinet_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------- Pin map --------------------------------- */
#define AGRINET_PUMP_GPIO        4   /* relay + PWM */
#define AGRINET_FAN_GPIO         5   /* relay + PWM */
#define AGRINET_LIGHT_GPIO       6   /* PWM LED     */
#define AGRINET_HEATER_GPIO      7   /* relay only  */
#define AGRINET_WINDOW_GPIO      10  /* servo PWM   */

/* LEDC channels */
#define AGRINET_PUMP_LEDC_CHAN   LEDC_CHANNEL_0
#define AGRINET_FAN_LEDC_CHAN    LEDC_CHANNEL_1
#define AGRINET_LIGHT_LEDC_CHAN  LEDC_CHANNEL_2
#define AGRINET_WINDOW_LEDC_CHAN LEDC_CHANNEL_3
#define AGRINET_LEDC_TIMER       LEDC_TIMER_0
#define AGRINET_LEDC_FREQ_HZ     1000      /* pump/fan/light PWM */
#define AGRINET_SERVO_FREQ_HZ    50        /* window servo */
#define AGRINET_LEDC_RES         LEDC_TIMER_12_BIT

/* Servo pulse widths in microseconds */
#define AGRINET_SERVO_PULSE_CLOSED_US  1000  /* 0 deg (window closed) */
#define AGRINET_SERVO_PULSE_OPEN_US    2000  /* 90 deg (window open)  */

/* ----------------------------- Public API ------------------------------ */
/**
 * @brief Initialise GPIOs and LEDC channels for all actuators.
 *        All actuators start in the OFF state.
 */
esp_err_t app_actuators_init(void);

/**
 * @brief Apply the given actuator state to the physical outputs.
 *        Only the fields indicated by changed_mask are modified.
 */
esp_err_t app_actuators_apply(const agrinet_actuator_state_t *state,
                              uint32_t changed_mask);

/**
 * @brief Read the current physical state of the actuators back into a
 *        struct (used at boot to recover state).
 */
esp_err_t app_actuators_read(agrinet_actuator_state_t *out);

#ifdef __cplusplus
}
#endif
