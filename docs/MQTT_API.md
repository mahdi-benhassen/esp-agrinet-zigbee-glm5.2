# MQTT API

This document describes the MQTT topic namespace and JSON payload schema used by the ESP-AgriNet Zigbee gateway.

---

## Topic namespace

All topics are rooted at `agrinet/<site_id>/...` where `site_id` is a short ASCII identifier configured per gateway (default: `gh1`).

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `agrinet/<site>/gateway/state` | Gateway → cloud | Periodic gateway state (every 60s) |
| `agrinet/<site>/gateway/log` | Gateway → cloud | Optional log forwarding (not enabled by default) |
| `agrinet/<site>/nodes/<node>/state` | Gateway → cloud | Node online/offline state changes |
| `agrinet/<site>/nodes/<node>/sensors` | Gateway → cloud | Sensor snapshot (every 60s per sensor node) |
| `agrinet/<site>/nodes/<node>/actuators` | Gateway → cloud | Actuator state (on change) |
| `agrinet/<site>/nodes/<node>/alerts` | Gateway → cloud | Alert events (on change) |
| `agrinet/<site>/nodes/<node>/actuators/<act>/set` | Cloud → gateway | Command to actuator |

`<node>` is the node identifier derived from the last 4 bytes of the IEEE address, e.g. `node-a1b2c3d4`. The gateway logs it when a node joins.

`<act>` is one of: `pump`, `fan`, `light`, `heater`, `window`.

---

## Payloads

### `agrinet/<site>/gateway/state`

Published every 60 seconds. QoS 1.

```json
{
  "site": "gh1",
  "uptime_sec": 3600,
  "pan_id": "0x1A2B",
  "channel": 15,
  "nodes": 2,
  "ext_pan_id": "A1B2C3D4E5F60718"
}
```

### `agrinet/<site>/nodes/<node>/sensors`

Published every 60 seconds per sensor node. QoS 1.

```json
{
  "node": "node-a1b2c3d4",
  "ts": 1234567,
  "temp_c": 23.45,
  "humidity_pct": 56.00,
  "pressure_pa": 101325,
  "soil_moisture_pct": 42.50,
  "soil_temp_c": 22.10,
  "illuminance_lux": 12500,
  "co2_ppm": 620,
  "battery_pct": 87
}
```

Field types and units:

| Field | Type | Unit | Range |
|-------|------|------|-------|
| `temp_c` | float | degrees Celsius | -40..120 |
| `humidity_pct` | float | percent | 0..100 |
| `pressure_pa` | int | Pascals | 30000..110000 |
| `soil_moisture_pct` | float | percent | 0..100 |
| `soil_temp_c` | float | degrees Celsius | -40..120 |
| `illuminance_lux` | int | lux | 0..65534 |
| `co2_ppm` | int | ppm | 0..10000 |
| `battery_pct` | int | percent | 0..100, or -1 if mains-powered |

### `agrinet/<site>/nodes/<node>/actuators`

Published on any actuator state change. QoS 1.

```json
{
  "node": "node-a1b2c3d4",
  "ts": 1234567,
  "pump": "on",
  "pump_level": 80,
  "fan": "off",
  "fan_speed": 0,
  "light": "on",
  "light_level": 100,
  "heater": "off",
  "window": "off"
}
```

`on`/`off` are string literals. Levels are 0..100 percent integers (PWM duty).

### `agrinet/<site>/nodes/<node>/alerts`

Published when the alert mask changes. QoS 1.

```json
{
  "node": "node-a1b2c3d4",
  "alerts": ["temp_high", "soil_dry"],
  "mask": 17
}
```

The `mask` is the decimal value of the alert bitmask; the `alerts` array lists the human-readable names of the currently-active alerts.

Alert names:

| Bit | Mask | Name | Meaning |
|-----|------|------|---------|
| 0 | 0x01 | `temp_high` | Temperature above threshold |
| 1 | 0x02 | `temp_low` | Temperature below threshold |
| 2 | 0x04 | `humidity_high` | Humidity above threshold |
| 3 | 0x08 | `humidity_low` | Humidity below threshold |
| 4 | 0x10 | `soil_dry` | Soil moisture below threshold |
| 5 | 0x20 | `soil_wet` | Soil moisture above threshold |
| 6 | 0x40 | `co2_high` | CO2 above threshold |
| 7 | 0x80 | `battery_low` | Battery below threshold |

---

## Command topics

To control an actuator, publish to:

```
agrinet/<site>/nodes/<node>/actuators/<act>/set
```

Payload (any subset of the fields is allowed; only the included fields are applied):

```json
{
  "pump": true,
  "pump_level": 80,
  "fan": "on",
  "fan_speed": 60,
  "light": false,
  "heater": 1,
  "window": "off"
}
```

Accepted value formats (per actuator):
- Boolean: `true` / `false`
- Integer: `1` / `0`
- String: `"on"` / `"off"`, `"true"` / `"false"`

Level fields (`pump_level`, `fan_speed`, `light_level`) accept integers 0..100.

### Examples

Turn the pump on at 80% duty:

```bash
mosquitto_pub -h broker.emqx.io -p 1883 \
  -t 'agrinet/gh1/nodes/node-a1b2c3d4/actuators/pump/set' \
  -m '{"pump": true, "pump_level": 80}'
```

Turn the heater off:

```bash
mosquitto_pub -h broker.emqx.io -p 1883 \
  -t 'agrinet/gh1/nodes/node-a1b2c3d4/actuators/heater/set' \
  -m '{"heater": false}'
```

Open the roof window:

```bash
mosquitto_pub -h broker.emqx.io -p 1883 \
  -t 'agrinet/gh1/nodes/node-a1b2c3d4/actuators/window/set' \
  -m '{"window": true}'
```

After receiving a command, the gateway:
1. Parses the JSON payload
2. Looks up the node by ID in its node table
3. Sends the appropriate ZCL on/off (and level-control if applicable) command to the actuator endpoint
4. Publishes the updated actuator state back on `agrinet/<site>/nodes/<node>/actuators`

---

## Quality of service

- All publishes use **QoS 1** (at-least-once delivery).
- Commands should also be published at QoS 1.
- Retained messages are not used by default; if you want the latest state on broker connect, configure your broker to retain `agrinet/<site>/nodes/+/actuators` and `agrinet/<site>/gateway/state`.

---

## Home Assistant integration

A minimal Home Assistant MQTT discovery configuration for a sensor node:

```yaml
mqtt:
  sensor:
    - name: "Greenhouse Temperature"
      state_topic: "agrinet/gh1/nodes/node-a1b2c3d4/sensors"
      value_template: "{{ value_json.temp_c }}"
      unit_of_measurement: "°C"
      device_class: temperature

    - name: "Greenhouse Humidity"
      state_topic: "agrinet/gh1/nodes/node-a1b2c3d4/sensors"
      value_template: "{{ value_json.humidity_pct }}"
      unit_of_measurement: "%"
      device_class: humidity

    - name: "Soil Moisture"
      state_topic: "agrinet/gh1/nodes/node-a1b2c3d4/sensors"
      value_template: "{{ value_json.soil_moisture_pct }}"
      unit_of_measurement: "%"
      device_class: moisture

    - name: "CO2"
      state_topic: "agrinet/gh1/nodes/node-a1b2c3d4/sensors"
      value_template: "{{ value_json.co2_ppm }}"
      unit_of_measurement: "ppm"

  switch:
    - name: "Irrigation Pump"
      command_topic: "agrinet/gh1/nodes/node-a1b2c3d4/actuators/pump/set"
      state_topic: "agrinet/gh1/nodes/node-a1b2c3d4/actuators"
      value_template: "{{ value_json.pump }}"
      payload_on: '{"pump": true}'
      payload_off: '{"pump": false}'
      state_on: "on"
      state_off: "off"
```

---

## Broker configuration

The default broker is `mqtt://broker.emqx.io:1883` (public EMQX broker, suitable for testing). For production, run your own broker:

- **Mosquitto**: `apt install mosquitto mosquitto-clients`
- **EMQX**: docker `docker run -d --name emqx -p 1883:1883 -p 18083:18083 emqx/emqx:latest`
- **HiveMQ Community Edition**: docker `docker run -d --name hivemq -p 1883:1883 hivemq/hivemq-ce:latest`

For secure deployments, set the broker URI to `mqtts://` (TLS) and provision device certificates.
