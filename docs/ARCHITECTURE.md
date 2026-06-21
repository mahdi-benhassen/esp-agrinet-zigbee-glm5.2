# Architecture

This document describes the system architecture of ESP-AgriNet Zigbee at three levels:

1. **Hardware** — which chip does what, how they are wired (see [HARDWARE.md](HARDWARE.md) for full BOM)
2. **Network** — how the Zigbee network is formed and how data flows to the cloud
3. **Software** — the responsibilities of each firmware and the shared component

---

## 1. Hardware overview

| Board | SoC | Role | Power | Notes |
|-------|-----|------|-------|-------|
| Gateway host | ESP32-S3-DevKitC-1 | Zigbee coordinator (host) + WiFi + MQTT | 5V USB | 8MB flash, PSRAM |
| Gateway RCP | ESP32-H2-DevKitM-1 | Radio coprocessor | Powered by host | Connected via UART |
| Sensor node | ESP32-H2-DevKitM-1 | Zigbee end device | Li-ion 18650 + USB | Battery monitor |
| Actuator node | ESP32-H2-DevKitM-1 | Zigbee router | 5V mains | Always-on |

The gateway is a **two-chip design**: the ESP32-S3 runs the application (WiFi, MQTT, Zigbee coordinator host stack) and the ESP32-H2 acts as a pure Zigbee radio coprocessor (RCP). They communicate over UART1 at 115200 baud. This split lets the gateway use the more powerful S3 for networking while leveraging the H2's IEEE 802.15.4 radio.

The sensor node and actuator node are **single-chip ESP32-H2 designs**. The ESP32-H2 includes a built-in 802.15.4 radio, so no external Zigbee module is required.

---

## 2. Network architecture

```
                    Cloud / Home Controller
                            ▲
                            │ MQTT (TCP/IP, TLS optional)
                            ▼
              ┌─────────────────────────────┐
              │  MQTT broker                │
              │  (e.g. broker.emqx.io:1883) │
              └─────────────┬───────────────┘
                            │
              ┌─────────────▼───────────────┐
              │  ESP-AgriNet Gateway        │
              │  ESP32-S3 (WiFi + host) +   │
              │  ESP32-H2 (RCP)             │
              │  Zigbee coordinator         │
              └─────────────┬───────────────┘
                            │ Zigbee 802.15.4 (PAN 0x1A2B, ch 15)
        ┌───────────────────┼───────────────────┐
        │                   │                   │
  ┌─────▼─────┐       ┌─────▼─────┐        ┌────▼─────┐
  │  Sensor   │       │ Actuator  │        │  Sensor  │
  │  node #1  │       │   node    │        │  node #2 │
  │ (ZED)     │       │ (ZR)      │        │ (ZED)    │
  └───────────┘       └───────────┘        └──────────┘
```

### Zigbee network roles

- **Coordinator** (gateway): forms the network, selects PAN ID and channel, manages joins, routes ZCL commands to/from MQTT.
- **Router** (actuator node): mains-powered, always-on, can relay messages for other nodes.
- **End device** (sensor node): battery-powered, sleeps between reports, talks to its parent (coordinator or router).

### Channel and PAN

Default: **PAN ID 0x1A2B**, **Channel 15** (2.425 GHz). Both are configurable via NVS on the gateway and via `sdkconfig.defaults` on the nodes.

### Network formation flow

1. Gateway boots → WiFi connects → MQTT connects → Zigbee coordinator starts
2. Coordinator performs BDB network formation → network open for joining (180s)
3. Each node (sensor / actuator) performs BDB network steering → finds coordinator → joins
4. Node registers its endpoints via ZCL; gateway caches the node table
5. Coordinator closes joining window (or keeps it open if `esp_zb_bdb_open_network()` is called again)

---

## 3. Software architecture

### 3.1 Component graph

```
                     ┌──────────────────────┐
                     │ agrinet_common       │
                     │  - types             │
                     │  - clusters          │
                     │  - mqtt_schema       │
                     │  - log helpers       │
                     └──────────┬───────────┘
                                │
       ┌────────────────────────┼────────────────────────┐
       │                        │                        │
┌──────▼──────┐          ┌──────▼──────┐          ┌──────▼──────┐
│  gateway    │          │ sensor_node │          │ actuator    │
│  (ESP32-S3) │          │ (ESP32-H2)  │          │ node (H2)   │
└─────────────┘          └─────────────┘          └─────────────┘
```

`agrinet_common` is a regular ESP-IDF component under `components/agrinet_common/` that is included via `EXTRA_COMPONENT_DIRS` in each firmware's top-level `CMakeLists.txt`. It contains:

- `agrinet_types.h` — common data types (`agrinet_sensor_snapshot_t`, `agrinet_actuator_state_t`, `agrinet_thresholds_t`, alert bitmasks)
- `agrinet_clusters.h/.c` — manufacturer-specific Zigbee clusters (soil moisture 0xFC00, CO2 0xFC01, agrinet config 0xFC02) and the default reporting configuration helper
- `agrinet_mqtt_schema.h/.c` — MQTT topic constants and JSON payload builders / parsers
- `agrinet_log.h` — shared log tags

### 3.2 Gateway firmware responsibilities

The gateway runs three concurrent subsystems:

1. **WiFi STA** (`app_wifi.c`) — joins the configured network, falls back to a captive portal (SoftAP) on failure for provisioning.
2. **MQTT client** (`app_mqtt.c`) — connects to the broker, publishes sensor / actuator / alert / gateway-state JSON payloads, subscribes to the wildcard actuator command topic.
3. **Zigbee coordinator** (`app_gateway.c`) — talks to the ESP32-H2 RCP over UART1, forms the network, accepts node joins, receives ZCL attribute reports, forwards them to MQTT, and translates MQTT actuator commands back to ZCL on/off / level-control commands.

The gateway also maintains a **node table** (up to 32 entries) with each node's short address, IEEE address, endpoint, last-seen timestamp and online/offline state. A periodic heartbeat (1 Hz) marks nodes offline if they have not been heard from in 5 minutes, and republishes the gateway state every 60 seconds.

### 3.3 Sensor node firmware responsibilities

1. Initialises the I2C bus and all sensors (BME280, BH1750, SCD41, soil moisture ADC, battery ADC).
2. Joins the Zigbee network as an end device via BDB network steering.
3. Every 60 seconds (configurable), performs a measurement cycle:
   - BME280: temperature, humidity, pressure (Bosch compensation algorithm)
   - BH1750: one-shot high-res illuminance
   - SCD41: single-shot CO2 (blocks 5s)
   - Soil moisture: analog read → linear map 0..100%
   - Battery: voltage divider → 0..100%
4. Updates the corresponding Zigbee attributes (standard clusters + custom soil/CO2 clusters). Reporting is configured on the standard clusters, so the gateway receives the values automatically.
5. Computes an alert mask based on configured thresholds (in the agrinet config cluster) and persists it.
6. Sleeps until the next cycle (tickless idle).

### 3.4 Actuator node firmware responsibilities

1. Joins the Zigbee network as a router.
2. Exposes 5 endpoints (pump, fan, light, heater, window), each with an on/off cluster and (for the first three) a level-control cluster.
3. Receives ZCL on/off and level-control commands from the gateway.
4. Maps the command to the right LEDC PWM channel or GPIO relay and applies it physically via `app_actuators_apply()`.
5. Updates the on/off / level attribute to confirm state to the coordinator.

### 3.5 Data model

See [ZIGBEE_DATA_MODEL.md](ZIGBEE_DATA_MODEL.md) for the full list of clusters, attributes and endpoints exposed by each device.

### 3.6 MQTT bridge schema

See [MQTT_API.md](MQTT_API.md) for the topic namespace and JSON payloads.

---

## 4. Boot sequence

```
Gateway boot:
  1. NVS init
  2. Load config (site_id, mqtt_uri, wifi_ssid, wifi_pass, pan_id, channel)
  3. WiFi init + connect (or captive portal fallback)
  4. MQTT init + connect + subscribe to cmd topics
  5. Zigbee platform configure (UART1 to RCP)
  6. esp_zb_init(coordinator)
  7. Register gateway telemetry endpoint (EP 20)
  8. esp_zb_start(false) → esp_zb_main_loop_iteration() (blocking)
  9. BDB network formation → network open 180s
  10. Running state: receive reports, forward to MQTT; receive cmds, forward to Zigbee

Sensor node boot:
  1. NVS init
  2. I2C + ADC + sensor init (BME280, BH1750, SCD41, soil, battery)
  3. Zigbee platform configure
  4. esp_zb_init(end device)
  5. Register sensor endpoint (EP 10) with all clusters
  6. Configure default reporting
  7. esp_zb_start(false) → esp_zb_main_loop_iteration() (blocking)
  8. BDB network steering → join coordinator
  9. Sensor task: every 60s, read sensors → update attributes → sleep

Actuator node boot:
  1. NVS init
  2. GPIO + LEDC init (pump, fan, light, heater, window)
  3. Zigbee platform configure
  4. esp_zb_init(router)
  5. Register actuator endpoints (EP 11..15)
  6. esp_zb_start(false) → esp_zb_main_loop_iteration() (blocking)
  7. BDB network steering → join coordinator
  8. Receive ZCL cmds → apply to outputs
```

---

## 5. Security considerations

This is a reference implementation. For production deployments:

- **Enable Zigbee network encryption** with a real install code on each device (the SDK supports install-code-based joining).
- **Change the default PAN ID** and use a randomly generated 16-byte network key.
- **Use TLS for MQTT** (set `mqtt://` → `mqtts://` in `g_mqtt_uri` and provision device certificates).
- **Provision WiFi credentials via the captive portal** rather than hardcoding them.
- **Disable the open SoftAP** once WiFi is configured.
- **Enable secure boot and flash encryption** on all ESP32 targets.
