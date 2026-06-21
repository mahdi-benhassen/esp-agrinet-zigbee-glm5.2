## ESP-AgriNet common component

Shared code used by the gateway and node firmwares:

- `agrinet_types.h`       - common data types (sensor snapshots, actuator state, thresholds)
- `agrinet_clusters.h/c`  - manufacturer-specific Zigbee clusters (soil moisture, CO2, agrinet config)
- `agrinet_mqtt_schema.h/c` - MQTT topic namespace and JSON payload builders/parsers
- `agrinet_log.h`         - shared log tags

### Dependencies

- `esp_zigbee` (from `esp-zigbee-sdk`)
- `esp_timer`, `esp_log` (from `esp-idf`)
