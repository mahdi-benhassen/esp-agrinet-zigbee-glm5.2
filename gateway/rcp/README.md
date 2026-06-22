# RCP firmware for the gateway's ESP32-H2 radio coprocessor

This directory holds the project-specific sdkconfig additions for the
ESP32-H2 Radio Coprocessor (RCP) firmware that ships on the gateway board.

The RCP firmware is built from **ESP-IDF's** `examples/openthread/ot_rcp`
example. The only addition we make is `CONFIG_OPENTHREAD_NCP_VENDOR_HOOK=y`,
which extends the Spinel protocol with Zigbee-specific properties so the
ESP32-S3 gateway host can manage a Zigbee network through the H2 radio.

> **Do not overwrite** the ot_rcp example's `sdkconfig.defaults` with this
> file — the example needs its own settings to compile. **Append** instead.

## Build manually

```bash
# 1. Activate ESP-IDF
. $IDF_PATH/export.sh

# 2. Append our config to the ot_rcp example's defaults
cat gateway/rcp/sdkconfig.defaults >> $IDF_PATH/examples/openthread/ot_rcp/sdkconfig.defaults

# 3. Build the RCP
cd $IDF_PATH/examples/openthread/ot_rcp
idf.py set-target esp32h2
idf.py build

# 4. Flash to the ESP32-H2 of the gateway
idf.py -p /dev/ttyUSB0 flash
```

## CI

The CI workflow in `.github/workflows/build.yml` automates this: it
appends `gateway/rcp/sdkconfig.defaults` to the ot_rcp example's defaults
inside the ESP-IDF Docker container and builds it there.

## Hardware connection

The ESP32-S3 host talks to the ESP32-H2 RCP over UART:

| ESP32-S3 pin | ESP32-H2 pin |
|--------------|--------------|
| GND          | GND          |
| GPIO4 (RX)   | TX           |
| GPIO5 (TX)   | RX           |

(Default UART pins — adjust in `gateway/host/sdkconfig.defaults` if needed.)
