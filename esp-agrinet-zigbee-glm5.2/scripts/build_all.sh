#!/usr/bin/env bash
#
# ESP-AgriNet Zigbee - Build-all helper
#
# Builds every firmware in this project. Requires a working ESP-IDF
# environment (IDF_PATH set, idf.py on PATH). The script will skip a
# firmware if its target has already been set.
#
# Usage:
#   ./scripts/build_all.sh              # build everything
#   ./scripts/build_all.sh gateway      # build only the gateway host
#   ./scripts/build_all.sh sensor       # build only the sensor node
#   ./scripts/build_all.sh actuator     # build only the actuator node
#   ./scripts/build_all.sh rcp          # build only the gateway RCP
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TARGET="${1:-all}"

build_gateway() {
    echo "==> Building gateway (ESP32-S3 host)"
    cd "$ROOT_DIR/gateway"
    idf.py set-target esp32s3
    idf.py build
}

build_sensor_node() {
    echo "==> Building sensor node (ESP32-H2)"
    cd "$ROOT_DIR/nodes/sensor_node"
    idf.py set-target esp32h2
    idf.py build
}

build_actuator_node() {
    echo "==> Building actuator node (ESP32-H2)"
    cd "$ROOT_DIR/nodes/actuator_node"
    idf.py set-target esp32h2
    idf.py build
}

build_rcp() {
    echo "==> Building RCP (ESP32-H2 radio coprocessor)"
    IDF_ZB_SDK_DIR="${IDF_PATH}/../esp-zigbee-sdk"
    if [ ! -d "$IDF_ZB_SDK_DIR/examples/esp_zigbee_gw/rcp" ]; then
        echo "    esp-zigbee-sdk not found at $IDF_ZB_SDK_DIR - skipping RCP build"
        echo "    (clone https://github.com/espressif/esp-zigbee-sdk next to ESP-IDF)"
        return 0
    fi
    RCP_DIR="$IDF_ZB_SDK_DIR/examples/esp_zigbee_gw/rcp"
    cp "$ROOT_DIR/rcp/sdkconfig.defaults" "$RCP_DIR/"
    cd "$RCP_DIR"
    idf.py set-target esp32h2
    idf.py build
}

case "$TARGET" in
    all)
        build_gateway
        build_sensor_node
        build_actuator_node
        build_rcp
        ;;
    gateway)  build_gateway ;;
    sensor)   build_sensor_node ;;
    actuator) build_actuator_node ;;
    rcp)      build_rcp ;;
    *)
        echo "Unknown target: $TARGET"
        echo "Valid targets: all, gateway, sensor, actuator, rcp"
        exit 1
        ;;
esac

echo ""
echo "Build complete."
