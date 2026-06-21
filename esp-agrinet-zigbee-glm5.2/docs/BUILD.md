# Build instructions

This document explains how to set up the build environment and compile every firmware in this project.

---

## 1. Install ESP-IDF

ESP-AgriNet Zigbee targets **ESP-IDF v5.2.x** (tested with v5.2.3). Follow the official installation guide:

- [Linux / macOS](https://docs.espressif.com/projects/esp-idf/en/v5.2.3/esp32s3/get-started/linux-macos-setup.html)
- [Windows](https://docs.espressif.com/projects/esp-idf/en/v5.2.3/esp32s3/get-started/windows-setup.html)

After installation, verify:

```bash
. $IDF_PATH/export.sh
idf.py --version
# Should print: ESP-IDF v5.2.3
```

---

## 2. Clone esp-zigbee-sdk

The Zigbee SDK is required for all four firmware targets. Clone it as a sibling of ESP-IDF:

```bash
cd $IDF_PATH/..
git clone --branch v0.5.0 https://github.com/espressif/esp-zigbee-sdk.git
```

The `idf_component.yml` files in this project will pull the Zigbee libraries (`esp-zigbee-lib` and `esp-zboss-lib`) automatically via the IDF Component Manager. The local clone is only needed to build the **RCP firmware** (which lives in `esp-zigbee-sdk/examples/esp_zigbee_gw/rcp/`).

---

## 3. Build all firmwares

The simplest way is to use the included helper script:

```bash
. $IDF_PATH/export.sh
./scripts/build_all.sh
```

This builds, in order:

1. Gateway host firmware (ESP32-S3)
2. Sensor node firmware (ESP32-H2)
3. Actuator node firmware (ESP32-H2)
4. Gateway RCP firmware (ESP32-H2)

Each firmware is built in its own directory and the resulting `.bin` files appear under `build/`.

---

## 4. Build a single firmware

### 4.1 Gateway (ESP32-S3)

```bash
cd gateway
idf.py set-target esp32s3
idf.py build
```

Output: `gateway/build/esp_agrinet_gateway.bin`

### 4.2 Sensor node (ESP32-H2)

```bash
cd nodes/sensor_node
idf.py set-target esp32h2
idf.py build
```

Output: `nodes/sensor_node/build/esp_agrinet_sensor_node.bin`

### 4.3 Actuator node (ESP32-H2)

```bash
cd nodes/actuator_node
idf.py set-target esp32h2
idf.py build
```

Output: `nodes/actuator_node/build/esp_agrinet_actuator_node.bin`

### 4.4 Gateway RCP (ESP32-H2)

```bash
RCP_DIR=$IDF_PATH/../esp-zigbee-sdk/examples/esp_zigbee_gw/rcp
cp rcp/sdkconfig.defaults "$RCP_DIR/"
cd "$RCP_DIR"
idf.py set-target esp32h2
idf.py build
```

Output: `$RCP_DIR/build/esp_zigbee_rcp.bin` (or similar)

---

## 5. Configuration

Each firmware has a `sdkconfig.defaults` file that sets the project-specific defaults. To override them at build time, either:

1. Edit `sdkconfig.defaults` and re-run `idf.py build`, or
2. Run `idf.py menuconfig` and tweak the live `sdkconfig`, then save.

Common things to configure:

- **PAN ID**: `CONFIG_ZB_PAN_ID` (default `0x1A2B`)
- **Channel**: `CONFIG_ZB_CHANNEL` (default `15`)
- **TX power**: `CONFIG_ZB_TX_POWER` (default `20` dBm)
- **WiFi SSID/password**: configured at runtime via NVS, not sdkconfig
- **MQTT broker URI**: configured at runtime via NVS, default `mqtt://broker.emqx.io:1883`
- **Site ID**: configured at runtime via NVS, default `gh1`

To set WiFi/MQTT/site at provisioning time, see [FLASHING.md](FLASHING.md).

---

## 6. CI build

The GitHub Actions workflow in `.github/workflows/build.yml` reproduces the full build on Ubuntu 22.04 with ESP-IDF v5.2.3. Each firmware is built as a separate job and the resulting `.bin` files are uploaded as build artifacts. Use this as a reference if your local build environment differs.

---

## 7. Troubleshooting

### `idf.py: command not found`

You forgot to source `export.sh`:

```bash
. $IDF_PATH/export.sh
```

### `esp_zigbee.h: No such file or directory`

The Zigbee libraries were not fetched. Make sure `idf_component.yml` is in your `main/` directory and that you have network access to the IDF Component Registry. You can also run:

```bash
idf.py reconfigure
```

### `set-target` fails with target mismatch

Each firmware has its target pinned in `sdkconfig.defaults`. If you see a mismatch, delete `sdkconfig` and the `build/` directory, then re-run `set-target`:

```bash
rm -rf build sdkconfig
idf.py set-target esp32s3   # or esp32h2
```

### RCP build fails

The RCP firmware lives in the external `esp-zigbee-sdk` repo. Make sure you've cloned the matching version (v0.5.0):

```bash
cd $IDF_PATH/../esp-zigbee-sdk
git checkout v0.5.0
```

### Build runs out of memory on CI

The CI workflow uses GitHub-hosted runners with 7 GB RAM, which is sufficient. If you're building on a smaller VM, increase swap or build firmwares one at a time.
