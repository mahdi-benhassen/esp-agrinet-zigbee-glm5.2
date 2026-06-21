# Flashing

This document describes how to flash each firmware to its target device.

---

## Prerequisites

- A working ESP-IDF environment (see [BUILD.md](BUILD.md))
- USB cables to connect each device to your computer
- One USB port per device (you can flash them one at a time if needed)

---

## Identify the serial port

On Linux:

```bash
ls /dev/ttyUSB*
# typical: /dev/ttyUSB0, /dev/ttyUSB1, ...
```

On macOS:

```bash
ls /dev/cu.usbserial-*
```

On Windows: check Device Manager → Ports (COM & LPT).

For the rest of this document, replace `/dev/ttyUSB0` with the port you identified.

---

## 1. Flash the gateway host (ESP32-S3)

```bash
. $IDF_PATH/export.sh
cd gateway
idf.py -p /dev/ttyUSB0 flash monitor
```

Press `Ctrl-]` to exit the monitor.

### Provision the gateway

After the first boot, the gateway has no WiFi credentials. It will start a captive portal named `AgriNet-GW-XXXX` (where XXXX is the last 2 bytes of the MAC address). Connect to it from a phone or laptop, then:

1. Open `http://192.168.4.1` in a browser (the gateway serves a minimal config page).
2. Enter WiFi SSID, WiFi password, MQTT broker URI and site ID.
3. Save. The gateway reboots and joins your WiFi.

> **Note**: the captive-portal HTTP server itself is a stub in this reference code. For production, integrate [esp_wifi_ctrl](https://components.espressif.com/components/espressif/esp_wifi_ctrl) or [wifi_provisioning](https://docs.espressif.com/projects/esp-idf/en/v5.2.3/esp32s3/api-reference/provisioning/wifi_provisioning.html). For now, you can also set the credentials directly via NVS at the factory stage, e.g.:
>
> ```bash
> # Write WiFi creds to NVS over serial
> idf.py -p /dev/ttyUSB0 monitor
> # At the gateway prompt (not implemented here), or use:
> esptool.py --port /dev/ttyUSB0 write_flash 0x9000 nvs_data.bin
> ```

---

## 2. Flash the gateway RCP (ESP32-H2)

The RCP firmware is built from `esp-zigbee-sdk`. Flash it before flashing the host, so the host can talk to a working RCP at boot.

```bash
. $IDF_PATH/export.sh
cd $IDF_PATH/../esp-zigbee-sdk/examples/esp_zigbee_gw/rcp
idf.py -p /dev/ttyUSB0 flash
```

> Flash the RCP **and** the host on the same physical ESP32-H2 + ESP32-S3 combo. The RCP firmware goes to the ESP32-H2; the host firmware goes to the ESP32-S3.

---

## 3. Flash the sensor node (ESP32-H2)

```bash
. $IDF_PATH/export.sh
cd nodes/sensor_node
idf.py -p /dev/ttyUSB1 flash monitor
```

On first boot, the node will try to join the network. Make sure the gateway has already formed the network (look for `network formed` in the gateway log).

---

## 4. Flash the actuator node (ESP32-H2)

```bash
. $IDF_PATH/export.sh
cd nodes/actuator_node
idf.py -p /dev/ttyUSB2 flash monitor
```

---

## 5. Erase flash before re-flashing

If a device has a different firmware (e.g. you're re-purposing an ESP32-H2 from sensor to actuator), erase it first:

```bash
idf.py -p /dev/ttyUSB0 erase-flash
idf.py -p /dev/ttyUSB0 flash
```

---

## 6. Using the helper script

The repo includes `scripts/flash.py` as a thin wrapper around `idf.py flash monitor`:

```bash
python3 scripts/flash.py gateway   /dev/ttyUSB0
python3 scripts/flash.py sensor    /dev/ttyUSB1
python3 scripts/flash.py actuator  /dev/ttyUSB2
python3 scripts/flash.py rcp       /dev/ttyUSB0
```

---

## 7. Verify operation

After flashing all four devices:

1. Open the gateway monitor: `idf.py -p /dev/ttyUSB0 monitor` — you should see:
   - `wifi connected` and `got ip: 192.168.x.y`
   - `mqtt client initialised` and `connected to broker`
   - `Zigbee stack initialised, forming network...`
   - `network formed: PAN=0x1A2B ch=15`
   - `network open for joining`

2. Open the sensor node monitor — you should see:
   - `Zigbee stack initialised, joining network...`
   - `joined network successfully`
   - `starting measurement cycle`
   - `report published: T=... H=... P=... soil=... lux=... co2=... bat=...`

3. Open the actuator node monitor — you should see:
   - `joined network successfully`
   - (idle until a command is received)

4. Subscribe to the MQTT broker from a laptop:

```bash
mosquitto_sub -h broker.emqx.io -p 1883 -t 'agrinet/#' -v
```

You should see:
- `agrinet/gh1/gateway/state {...}` every 60 seconds
- `agrinet/gh1/nodes/node-XXXXXX/sensors {...}` every 60 seconds per sensor node
- `agrinet/gh1/nodes/node-XXXXXX/alerts {...}` when an alert fires

5. Send a command to the actuator node:

```bash
# Replace XXXXXX with the actual node ID (printed in the gateway log)
mosquitto_pub -h broker.emqx.io -p 1883 \
  -t 'agrinet/gh1/nodes/node-XXXXXX/actuators/pump/set' \
  -m '{"pump": true, "pump_level": 80}'
```

The actuator node should turn the pump on at 80% PWM and the gateway should publish the updated actuator state on `agrinet/gh1/nodes/node-XXXXXX/actuators`.

---

## 8. Updating firmware over-the-air (OTA)

OTA is not implemented in this reference. For production, integrate [ESP-IDF OTA](https://docs.espressif.com/projects/esp-idf/en/v5.2.3/esp32s3/api-reference/system/ota.html) on each device, and consider adding a Zigbee OTA cluster (cluster ID 0x0019) so nodes can be upgraded via the gateway.
