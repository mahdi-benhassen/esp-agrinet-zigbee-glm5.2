#!/usr/bin/env bash
#
# ESP-AgriNet Zigbee - Build-all helper
#
# Builds every firmware in this project. Requires a working ESP-IDF
# environment (IDF_PATH set, idf.py on PATH).
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
    echo "==> Building gateway host (ESP32-S3)"
    cd "$ROOT_DIR/gateway/host"
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
    RCP_EXAMPLE="$IDF_PATH/examples/openthread/ot_rcp"
    if [ ! -d "$RCP_EXAMPLE" ]; then
        echo "    ot_rcp example not found at $RCP_EXAMPLE - skipping RCP build"
        echo "    (make sure ESP-IDF is installed and IDF_PATH is set)"
        return 0
    fi
    # Append our vendor-hook config to the ot_rcp example's defaults
    cat "$ROOT_DIR/gateway/rcp/sdkconfig.defaults" >> "$RCP_EXAMPLE/sdkconfig.defaults"
    cd "$RCP_EXAMPLE"
    idf.py set-target esp32h2
    idf.py build
}

case "$TARGET" in
    all)
        build_rcp
        build_gateway
        build_sensor_node
        build_actuator_node
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
