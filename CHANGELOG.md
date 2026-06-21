# Changelog

All notable changes to ESP-AgriNet Zigbee will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.0.0] - 2025-06-21

Initial release.

### Added

- **Gateway firmware** (ESP32-S3 host + ESP32-H2 RCP):
  - Zigbee coordinator with BDB network formation
  - WiFi STA with captive portal fallback
  - MQTT client with broker connection and command subscription
  - Node table management (up to 32 nodes)
  - Heartbeat task with 60-second gateway state publication
  - Actuator command forwarding from MQTT to Zigbee ZCL

- **Sensor node firmware** (ESP32-H2 end device):
  - BME280 driver (temperature, humidity, pressure with Bosch compensation)
  - BH1750 driver (one-shot high-res illuminance)
  - SCD41 driver (single-shot CO2 with CRC checks)
  - Capacitive soil moisture analog driver with linear calibration
  - Battery voltage monitor with ADC + voltage divider
  - Zigbee end-device with 60-second reporting interval
  - Custom clusters for soil moisture, CO2 and agrinet config
  - Default reporting configuration on all measurement clusters
  - Alert mask computation based on configurable thresholds

- **Actuator node firmware** (ESP32-H2 router):
  - 5 actuator endpoints (pump, fan, light, heater, window)
  - LEDC PWM drivers for pump, fan and light dimming
  - GPIO relay driver for heater
  - Servo PWM driver (50 Hz) for roof window
  - ZCL on/off and level-control command handling
  - Router role for mesh extension

- **Common component** (`agrinet_common`):
  - `agrinet_types.h` — shared data types and thresholds
  - `agrinet_clusters.h/.c` — manufacturer-specific Zigbee clusters
  - `agrinet_mqtt_schema.h/.c` — MQTT topic constants and JSON builders
  - `agrinet_log.h` — shared log tags

- **RCP config** for the ESP32-H2 radio coprocessor (uses esp-zigbee-sdk example)

- **Build system**:
  - Per-firmware `CMakeLists.txt` with `EXTRA_COMPONENT_DIRS` pointing to `components/`
  - Per-firmware `sdkconfig.defaults` and `partitions.csv`
  - `idf_component.yml` with esp-zigbee-lib and esp-zboss-lib dependencies
  - `scripts/build_all.sh` — one-shot build for all firmwares
  - `scripts/flash.py` — idf.py flash wrapper

- **CI/CD**:
  - GitHub Actions workflow (`.github/workflows/build.yml`) that builds all four firmware targets on ESP-IDF v5.2.3 and uploads build artifacts
  - `clang-format` lint step

- **Documentation**:
  - `README.md` — project overview and quick start
  - `docs/ARCHITECTURE.md` — system architecture, network topology, software layers
  - `docs/HARDWARE.md` — bill of materials and wiring
  - `docs/BUILD.md` — build environment setup and per-target build steps
  - `docs/FLASHING.md` — flashing and provisioning
  - `docs/MQTT_API.md` — MQTT topic namespace and JSON payload schema, with Home Assistant integration example
  - `docs/ZIGBEE_DATA_MODEL.md` — clusters, attributes, endpoints
  - `CONTRIBUTING.md` — contribution guidelines
  - `CHANGELOG.md` — this file
  - `LICENSE` — MIT

---

## Versioning

- **Major** — breaking protocol or API change (e.g. Zigbee cluster ID change, MQTT schema change)
- **Minor** — new feature, backward compatible (e.g. new sensor support, new actuator)
- **Patch** — bug fix or documentation update
