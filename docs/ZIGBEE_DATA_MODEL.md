# Zigbee data model

This document describes the Zigbee clusters, attributes and endpoints exposed by each device in the ESP-AgriNet Zigbee system.

---

## Endpoint layout

| Endpoint | Device | Cluster set |
|----------|--------|-------------|
| 10 | Sensor node | Temp, humidity, pressure, illuminance, soil (custom), CO2 (custom), agrinet-cfg (custom), basic, identify, power-config |
| 11 | Actuator node — pump | on/off, level-control, basic, identify |
| 12 | Actuator node — fan | on/off, level-control, basic, identify |
| 13 | Actuator node — grow light | on/off, level-control, basic, identify |
| 14 | Actuator node — heater | on/off, basic, identify |
| 15 | Actuator node — window | on/off, basic, identify |
| 20 | Gateway telemetry | basic, identify |

All endpoints use the **Home Automation profile** (0x0104).

---

## Standard clusters used

### Basic (0x0000) — server

| Attribute | ID | Type | Notes |
|-----------|----|------|-------|
| ZCL version | 0x0000 | u8 | Default 0x03 |
| Manufacturer name | 0x0004 | string | "ESP-AgriNet" |
| Model identifier | 0x0005 | string | "Sensor-Node-H2" / "Actuator-Node-H2" / "Gateway-S3" |
| Power source | 0x0007 | u8 | Battery (sensor) / Mains (actuator, gateway) |

### Identify (0x0003) — server

Standard attributes (identify time, etc.). Used by Zigbee controllers to physically identify a device.

### Power configuration (0x0001) — server (sensor node only)

| Attribute | ID | Type | Notes |
|-----------|----|------|-------|
| Battery percentage remaining | 0x0021 | u8 | 0..200 (0.5% steps; we send 0..100 directly) |

### Temperature measurement (0x0402) — server (sensor node)

| Attribute | ID | Type | Unit | Range |
|-----------|----|------|------|-------|
| Measured value | 0x0000 | s16 | 0.01 °C | -4096..12000 |
| Min measured value | 0x0001 | s16 | 0.01 °C | -4000 |
| Max measured value | 0x0002 | s16 | 0.01 °C | 12000 |
| Tolerance | 0x0003 | u16 | 0.01 °C | 100 |

Reporting: every 30s..300s, change ≥ 0.5 °C.

### Relative humidity measurement (0x0405) — server (sensor node)

| Attribute | ID | Type | Unit | Range |
|-----------|----|------|------|-------|
| Measured value | 0x0000 | u16 | 0.01 % | 0..10000 |
| Min measured value | 0x0001 | u16 | 0.01 % | 0 |
| Max measured value | 0x0002 | u16 | 0.01 % | 10000 |

Reporting: every 30s..300s, change ≥ 2 %.

### Pressure measurement (0x0403) — server (sensor node)

| Attribute | ID | Type | Unit | Range |
|-----------|----|------|------|-------|
| Measured value | 0x0000 | s16 | hPa | 300..1100 |
| Min measured value | 0x0001 | s16 | hPa | 300 |
| Max measured value | 0x0002 | s16 | hPa | 1100 |

> Note: pressure is stored in hPa on the standard cluster but the gateway publishes it in Pascals on MQTT (×100).

Reporting: every 30s..300s, change ≥ 5 hPa.

### Illuminance measurement (0x0400) — server (sensor node)

| Attribute | ID | Type | Unit | Range |
|-----------|----|------|------|-------|
| Measured value | 0x0000 | u16 | lux (Zigbee: log10) | 0..0xFFFE |
| Min measured value | 0x0001 | u16 | | 0 |
| Max measured value | 0x0002 | u16 | | 0xFFFE |

> Note: the standard Zigbee illuminance attribute is `10000 * log10(lux + 1)`. The current implementation publishes raw lux; for full compliance, apply the conversion at the gateway.

### On/off (0x0006) — server (actuator node)

| Attribute | ID | Type | Notes |
|-----------|----|------|-------|
| On/off | 0x0000 | bool | 0=off, 1=on |

Commands accepted: `On`, `Off`, `Toggle`.

### Level control (0x0008) — server (actuator pump/fan/light)

| Attribute | ID | Type | Notes |
|-----------|----|------|-------|
| Current level | 0x0000 | u8 | 0..254 |

Commands accepted: `Move to level (with on/off)`.

---

## Manufacturer-specific clusters

Manufacturer code: **0x0000** (test/demo). Replace with your own assigned manufacturer code for production.

### Soil moisture (0xFC00) — server (sensor node)

| Attribute | ID | Type | Unit | Range |
|-----------|----|------|------|-------|
| Soil moisture measured value | 0x0000 | s16 | 0.01 % | 0..10000 |
| Soil moisture min value | 0x0001 | s16 | 0.01 % | 0 |
| Soil moisture max value | 0x0002 | s16 | 0.01 % | 10000 |
| Soil moisture tolerance | 0x0003 | s16 | 0.01 % | 100 |
| Soil temperature measured value | 0x0010 | s16 | 0.01 °C | -4000..12000 |
| Soil temperature min value | 0x0011 | s16 | 0.01 °C | -4000 |
| Soil temperature max value | 0x0012 | s16 | 0.01 °C | 12000 |
| Soil temperature tolerance | 0x0013 | s16 | 0.01 °C | 100 |

Reporting: every 30s..300s, change ≥ 2 %.

### CO2 measurement (0xFC01) — server (sensor node)

| Attribute | ID | Type | Unit | Range |
|-----------|----|------|------|-------|
| CO2 measured value | 0x0000 | u16 | ppm | 0..10000 |
| CO2 min measured | 0x0001 | u16 | ppm | 0 |
| CO2 max measured | 0x0002 | u16 | ppm | 10000 |
| CO2 tolerance | 0x0003 | u16 | ppm | 50 |

Reporting: every 30s..300s, change ≥ 50 ppm.

### AgriNet configuration (0xFC02) — server (every node)

| Attribute | ID | Type | Unit | Default |
|-----------|----|------|------|---------|
| Temperature high threshold | 0x0000 | s16 | 0.01 °C | 3500 (35.0 °C) |
| Temperature low threshold | 0x0001 | s16 | 0.01 °C | 500 (5.0 °C) |
| Humidity high threshold | 0x0002 | s16 | 0.01 % | 9000 (90 %) |
| Humidity low threshold | 0x0003 | s16 | 0.01 % | 3000 (30 %) |
| Soil dry threshold | 0x0004 | s16 | 0.01 % | 2500 (25 %) |
| Soil wet threshold | 0x0005 | s16 | 0.01 % | 8500 (85 %) |
| CO2 high threshold | 0x0006 | u16 | ppm | 1200 |
| Battery low threshold | 0x0007 | u8 | % | 20 |
| Report interval | 0x0008 | u16 | seconds | 60 |
| Active alert mask | 0x0009 | u8 | bitmask | 0 |

A controller can read these attributes to discover the current configuration, or write them to change thresholds on the fly.

---

## Default reporting configuration

The sensor node configures the following reporting on all measurement clusters at boot (in `agrinet_configure_default_reporting()`):

| Cluster | Attribute | Min interval | Max interval | Reportable change |
|---------|-----------|--------------|--------------|-------------------|
| Temperature | value | 30 s | 300 s | 0.5 °C |
| Humidity | value | 30 s | 300 s | 2 % |
| Pressure | value | 30 s | 300 s | 5 hPa |
| Illuminance | value | 30 s | 300 s | 50 (raw) |
| Soil moisture (custom) | value | 30 s | 300 s | 2 % |
| CO2 (custom) | value | 30 s | 300 s | 50 ppm |

For the custom clusters, reporting is also triggered manually by `agrinet_update_soil_moisture()` and `agrinet_update_co2()` after each measurement (broadcast to the bound group 0xFFFF).

---

## Device IDs

| Endpoint | HA device ID | Hex |
|----------|--------------|-----|
| 10 | ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID | 0x000C |
| 11 | ESP_ZB_HA_COLOR_DIMMABLE_LIGHT_DEVICE_ID | 0x0102 (re-used as pump) |
| 12 | ESP_ZB_HA_FAN_DEVICE_ID | 0x0030 |
| 13 | ESP_ZB_HA_DIMMABLE_LIGHT_DEVICE_ID | 0x0101 |
| 14 | ESP_ZB_HA_HEATING_COOLING_UNIT_DEVICE_ID | 0x0029 |
| 15 | ESP_ZB_HA_WINDOW_COVERING_DEVICE_ID | 0x0002 |
| 20 | ESP_ZB_HA_REMOTE_CONTROL_DEVICE_ID | 0x0008 |

> Note: some device IDs are re-used as a pragmatic approximation (e.g. the pump endpoint uses the dimmable light device ID because it has on/off + level semantics). For full Home Automation profile compliance, use the correct device ID per actuator type.

---

## Joining

- The coordinator opens the network for joining for 180 seconds after formation.
- Nodes perform BDB network steering and join automatically.
- No install code is used in this reference; for production, enable install-code-based joining for security.
