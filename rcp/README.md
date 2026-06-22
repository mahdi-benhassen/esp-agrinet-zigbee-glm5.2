# RCP firmware for the gateway's ESP32-H2 radio coprocessor

This directory holds the project-specific sdkconfig for the ESP32-H2
Radio Coprocessor (RCP) firmware that ships on the gateway board.

The RCP firmware is built from the **ESP-IDF** `examples/openthread/ot_rcp`
example with the `OPENTHREAD_NCP_VENDOR_HOOK` option enabled. This hook
adds Zigbee-specific Spinel extensions so the ESP32-S3 gateway host can
manage the Zigbee network through the H2 radio.

## Build manually

```bash
# 1. Activate ESP-IDF
. $IDF_PATH/export.sh

# 2. Copy this sdkconfig.defaults into the ot_rcp example
cp sdkconfig.defaults $IDF_PATH/examples/openthread/ot_rcp/sdkconfig.defaults

# 3. Build the RCP
cd $IDF_PATH/examples/openthread/ot_rcp
idf.py set-target esp32h2
idf.py build

# 4. Flash to the ESP32-H2 of the gateway
idf.py -p /dev/ttyUSB0 flash
```

## CI

The CI workflow in `.github/workflows/build.yml` automates this: it copies
the `sdkconfig.defaults` into the `ot_rcp` example inside the ESP-IDF
Docker container and builds it there.

## Hardware connection

The ESP32-S3 host talks to the ESP32-H2 RCP over UART:

| ESP32-S3 pin | ESP32-H2 pin |
|--------------|--------------|
| GND          | GND          |
| GPIO4 (RX)   | TX           |
| GPIO5 (TX)   | RX           |

(Default UART pins — adjust in the gateway's sdkconfig.defaults if needed.)
