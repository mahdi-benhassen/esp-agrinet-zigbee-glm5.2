# RCP firmware for the gateway's ESP32-H2 radio coprocessor

This directory holds the project-specific sdkconfig for the ESP32-H2
Radio Coprocessor (RCP) firmware that ships on the gateway board.

The actual RCP application source is provided by the ESP-Zigbee-SDK
under `examples/esp_zigbee_gw/rcp`. To build it for this project:

```bash
# 1. Copy this sdkconfig.defaults into the esp-zigbee-sdk RCP example
cp sdkconfig.defaults $IDF_PATH/../esp-zigbee-sdk/examples/esp_zigbee_gw/rcp/

# 2. Build the RCP
cd $IDF_PATH/../esp-zigbee-sdk/examples/esp_zigbee_gw/rcp
idf.py set-target esp32h2
idf.py build

# 3. Flash to the ESP32-H2 of the gateway
idf.py -p /dev/ttyUSB0 flash
```

The CI workflow in `.github/workflows/build.yml` automates this.
