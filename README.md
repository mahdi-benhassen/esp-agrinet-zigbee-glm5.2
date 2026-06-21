# ESP-AgriNet Zigbee

**Smart agriculture & greenhouse monitoring and control system built on top of the Espressif Zigbee SDK**

<p align="center">
  <strong>Gateway (ESP32-S3 + ESP32-H2)</strong> · <strong>Sensor Node (ESP32-H2)</strong> · <strong>Actuator Node (ESP32-H2)</strong>
</p>

---

## Overview

**ESP-AgriNet Zigbee** is a complete, end-to-end Zigbee-based monitoring and control system designed for smart agriculture and greenhouse applications. It uses the [Espressif Zigbee SDK](https://github.com/espressif/esp-zigbee-sdk) (built on top of ESP-IDF) to create a low-power, self-healing mesh network of sensor and actuator nodes that report environmental data to a central gateway, which in turn bridges the data to any cloud or home-automation controller via MQTT.

### Key features

- **Complete Zigbee stack** — coordinator, router and end-device roles implemented on top of `esp-zigbee-sdk`
- **Multi-sensor greenhouse node** — BME280 (temperature/humidity/pressure), BH1750 (illuminance), SCD41 (CO2), capacitive soil moisture, battery monitor
- **Multi-actuator control node** — water pump, ventilation fan, dimmable LED grow light, heater, servo-driven roof window
- **WiFi + MQTT gateway bridge** — IP networking, configurable broker, JSON payload schema
- **Captive portal provisioning** — WiFi SSID/password and broker URI set up via a simple AP
- **Manufacturer-specific Zigbee clusters** for soil moisture, CO2 and agrinet configuration (thresholds, alert masks)
- **Standard Zigbee reporting** so any compliant controller (Home Assistant, zigbee2mqtt, etc.) can consume the data
- **Power-aware sensor node** — tickless idle, sleep-able sensors, 5V USB or Li-ion 18650 battery
- **GitHub Actions CI** — all four firmware targets (gateway, sensor, actuator, RCP) are built on every push

---

## Repository layout

```
esp-agrinet-zigbee/
├── gateway/                    # ESP32-S3 host firmware (Zigbee coordinator + WiFi + MQTT)
│   ├── main/
│   │   ├── main.c
│   │   ├── app_gateway.{h,c}
│   │   ├── app_wifi.{h,c}
│   │   ├── app_mqtt.{h,c}
│   │   └── idf_component.yml
│   ├── partitions.csv
│   ├── sdkconfig.defaults
│   └── CMakeLists.txt
├── nodes/
│   ├── sensor_node/            # ESP32-H2 sensor end-device
│   │   ├── main/
│   │   │   ├── main.c
│   │   │   ├── app_sensors.{h,c}
│   │   │   └── idf_component.yml
│   │   ├── partitions.csv
│   │   ├── sdkconfig.defaults
│   │   └── CMakeLists.txt
│   └── actuator_node/          # ESP32-H2 actuator router
│       ├── main/
│       │   ├── main.c
│       │   ├── app_actuators.{h,c}
│       │   └── idf_component.yml
│       ├── partitions.csv
│       ├── sdkconfig.defaults
│       └── CMakeLists.txt
├── rcp/                        # ESP32-H2 radio-coprocessor config for the gateway
│   ├── sdkconfig.defaults
│   └── README.md
├── components/
│   └── agrinet_common/         # Shared clusters, types, MQTT schema, log helpers
│       ├── include/
│       │   ├── agrinet_types.h
│       │   ├── agrinet_clusters.h
│       │   ├── agrinet_mqtt_schema.h
│       │   └── agrinet_log.h
│       ├── src/
│       │   ├── agrinet_clusters.c
│       │   └── agrinet_mqtt_schema.c
│       └── CMakeLists.txt
├── docs/
│   ├── ARCHITECTURE.md
│   ├── HARDWARE.md
│   ├── BUILD.md
│   ├── FLASHING.md
│   ├── MQTT_API.md
│   └── ZIGBEE_DATA_MODEL.md
├── scripts/
│   ├── build_all.sh            # one-shot build for every firmware
│   └── flash.py                # idf.py flash wrapper
├── .github/workflows/build.yml # CI: build gateway / sensor / actuator / RCP
├── .clang-format
├── .gitignore
├── LICENSE
└── README.md                   # this file
```

---

## Quick start

### Prerequisites

- [ESP-IDF v5.2 or newer](https://docs.espressif.com/projects/esp-idf/en/v5.2.3/esp32s3/get-started/) — install and run `. $IDF_PATH/export.sh`
- [esp-zigbee-sdk](https://github.com/espressif/esp-zigbee-sdk) — clone next to ESP-IDF (the CI does this automatically; for local builds see [docs/BUILD.md](docs/BUILD.md))
- Hardware: see [docs/HARDWARE.md](docs/HARDWARE.md)

### Build everything

```bash
git clone https://github.com/mahdi-benhassen/esp-agrinet-zigbee-glm5.2.git
cd esp-agrinet-zigbee-glm5.2
. $IDF_PATH/export.sh
./scripts/build_all.sh
```

### Flash the firmware

Each firmware goes to a different physical device. See [docs/FLASHING.md](docs/FLASHING.md) for the full procedure.

```bash
# Flash the gateway (ESP32-S3) on /dev/ttyUSB0
python3 scripts/flash.py gateway /dev/ttyUSB0

# Flash the sensor node (ESP32-H2) on /dev/ttyUSB1
python3 scripts/flash.py sensor /dev/ttyUSB1

# Flash the actuator node (ESP32-H2) on /dev/ttyUSB2
python3 scripts/flash.py actuator /dev/ttyUSB2

# Flash the gateway's RCP (ESP32-H2) on /dev/ttyUSB0
python3 scripts/flash.py rcp /dev/ttyUSB0
```

---

## System architecture

```
                  ┌──────────────────────────┐         ┌────────────────────┐
                  │ Cloud / Home Controller  │         │  Mobile / Web UI   │
                  └────────────┬─────────────┘         └─────────┬──────────┘
                               │  MQTT (TLS optional)            │
                               │                                 │
                  ┌────────────▼─────────────┐                    │
                  │  WiFi / MQTT Broker      │◄───────────────────┘
                  │  (e.g. EMQX, Mosquitto)  │
                  └────────────┬─────────────┘
                               │
                  ┌────────────▼─────────────────────────────┐
                  │   ESP-AgriNet Gateway                     │
                  │   ESP32-S3 (host)  +  ESP32-H2 (RCP)      │
                  │   WiFi  ──►  MQTT  ──►  Zigbee coordinator│
                  └────────────┬─────────────────────────────┘
                               │ Zigbee mesh (PAN 0x1A2B, ch 15)
            ┌──────────────────┼──────────────────┐
            │                  │                  │
   ┌────────▼──────┐   ┌───────▼───────┐   ┌──────▼────────┐
   │  Sensor Node  │   │ Actuator Node │   │  Sensor Node  │
   │  (ESP32-H2)   │   │  (ESP32-H2)   │   │  (ESP32-H2)   │
   │  End Device   │   │  Router       │   │  End Device   │
   │  BME280/BH1750│   │  Pump/Fan/    │   │  ...          │
   │  SCD41/Soil   │   │  Light/Heater │   │               │
   └───────────────┘   └───────────────┘   └───────────────┘
```

For details, see:
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — system design, data flow, network topology
- [docs/HARDWARE.md](docs/HARDWARE.md) — bill of materials, wiring, enclosures
- [docs/BUILD.md](docs/BUILD.md) — build environment and per-target build steps
- [docs/FLASHING.md](docs/FLASHING.md) — flashing each firmware to its device
- [docs/MQTT_API.md](docs/MQTT_API.md) — MQTT topic namespace and JSON payload schema
- [docs/ZIGBEE_DATA_MODEL.md](docs/ZIGBEE_DATA_MODEL.md) — Zigbee clusters, attributes, endpoints

---

## Supported devices

| Role | Target | Zigbee role | Notes |
|------|--------|-------------|-------|
| Gateway host | ESP32-S3 | Coordinator | WiFi + MQTT bridge |
| Gateway RCP | ESP32-H2 | (radio only) | Talks to host over UART |
| Sensor node | ESP32-H2 | End device | Battery-powered, sleeps between reports |
| Actuator node | ESP32-H2 | Router | Mains-powered, always-on mesh relay |

---

## CI / CD

The repo includes a GitHub Actions workflow (`.github/workflows/build.yml`) that, on every push or pull request:

1. Lints all C source with `clang-format`
2. Installs ESP-IDF v5.2.3
3. Builds the gateway host (ESP32-S3)
4. Builds the sensor node (ESP32-H2)
5. Builds the actuator node (ESP32-H2)
6. Clones `esp-zigbee-sdk` and builds the RCP firmware (ESP32-H2)
7. Uploads each firmware's `.bin`, bootloader and partition table as a build artifact

---

## License

Released under the [MIT License](LICENSE).

---

## Acknowledgements

- [Espressif Systems](https://www.espressif.com/) for the ESP32 platform, ESP-IDF, and the Zigbee SDK
- [Bosch Sensortec](https://www.bosch-sensortec.com/) for the BME280
- [Sensirion](https://www.sensirion.com/) for the SCD41
- [ROHM Semiconductor](https://www.rohm.com/) for the BH1750
