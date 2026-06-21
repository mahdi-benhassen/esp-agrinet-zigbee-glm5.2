# Hardware

This document describes the hardware reference design for ESP-AgriNet Zigbee.

---

## Bill of materials

### Common parts

| Part | Qty | Notes |
|------|-----|-------|
| ESP32-H2-DevKitM-1 | 1 per node + 1 for gateway RCP | Espressif dev board |
| USB-C cable (data) | 1 per device | For flashing and console |
| 3.3V power supply or USB charger | 1 per device | |

### Gateway (ESP32-S3 + ESP32-H2)

| Part | Qty | Notes |
|------|-----|-------|
| ESP32-S3-DevKitC-1 (N16R8) | 1 | 16 MB flash, 8 MB PSRAM |
| ESP32-H2-DevKitM-1 | 1 | Used as RCP |
| Jumper wires (F-F) | 4 | UART host ↔ RCP |
| MicroSD card (optional) | 1 | Local logging if needed |

Wiring between ESP32-S3 and ESP32-H2 (RCP):

| ESP32-S3 pin | ESP32-H2 pin | Function |
|--------------|--------------|----------|
| GPIO17 (UART1 TX) | GPIO0 (UART0 RX) | Host → RCP |
| GPIO18 (UART1 RX) | GPIO1 (UART0 TX) | RCP → Host |
| GND | GND | Common ground |
| 5V (or 3V3) | 5V (or 3V3) | Power RCP from host |

### Sensor node (ESP32-H2)

| Part | Qty | Notes |
|------|-----|-------|
| ESP32-H2-DevKitM-1 | 1 | |
| BME280 breakout | 1 | Temperature / humidity / pressure |
| BH1750 breakout | 1 | Illuminance |
| SCD41 breakout | 1 | CO2 (true CO2, NDIR) |
| Capacitive soil moisture sensor v1.2 | 1 | Analog output |
| Li-ion 18650 + holder | 1 | Battery |
| TP4056 charge module | 1 | USB charging for the 18650 |
| 100nF capacitor | 1 | ADC input filter for soil sensor |
| Resistors: 100kΩ, 100kΩ | 2 | Battery voltage divider |

Sensor node wiring (ESP32-H2):

| Sensor | Sensor pin | ESP32-H2 pin | Notes |
|--------|-----------|--------------|-------|
| BME280 | SDA / SCL / VDD / GND | GPIO8 / GPIO9 / 3V3 / GND | I2C addr 0x76 |
| BH1750 | SDA / SCL / VDD / GND | GPIO8 / GPIO9 / 3V3 / GND | I2C addr 0x23 |
| SCD41  | SDA / SCL / VDD / GND | GPIO8 / GPIO9 / 3V3 / GND | I2C addr 0x62 |
| Soil moisture | AOUT | GPIO1 (ADC1_CH0) | Analog |
| Battery divider | midpoint | GPIO2 (ADC1_CH1) | 1:2 ratio |

### Actuator node (ESP32-H2)

| Part | Qty | Notes |
|------|-----|-------|
| ESP32-H2-DevKitM-1 | 1 | |
| 5V relay module (active high) | 3 | Pump, fan, heater |
| MOSFET module (IRLZ44N) | 1 | PWM dimming for LED grow light |
| LED grow light strip (12V, 10W) | 1 | |
| 12V water pump (submersible) | 1 | For irrigation |
| 12V DC fan (120mm) | 1 | For ventilation |
| 12V heating element (PTC) | 1 | For greenhouse heating |
| SG90 servo motor | 1 | For roof window |
| 12V / 5V power supply | 1 | Mains-powered |

Actuator node wiring (ESP32-H2):

| Actuator | Control pin | ESP32-H2 GPIO | Driver |
|----------|-------------|---------------|--------|
| Water pump | IN | GPIO4 | Relay + PWM (LEDC ch0) |
| Ventilation fan | IN | GPIO5 | Relay + PWM (LEDC ch1) |
| Grow light | IN | GPIO6 | MOSFET PWM (LEDC ch2) |
| Heater | IN | GPIO7 | Relay (GPIO only) |
| Roof window | signal | GPIO10 | Servo PWM (LEDC ch3, 50Hz) |

---

## Power considerations

### Sensor node (battery)

The sensor node is designed for battery operation:

- **BME280, BH1750**: low-power, sub-mA in measurement mode, <1µA in sleep
- **SCD41**: this is the biggest power draw. In single-shot mode it consumes ~30mA for 5s per measurement. Between measurements it can be put to idle (~2mA) or sleep (<1µA)
- **ESP32-H2 in light sleep**: ~130µA
- **Capacitive soil moisture sensor**: ~5mA while powered; should be powered from a GPIO to switch off between reads

With a 3000mAh 18650 and 60-second reporting interval, expected battery life is **~3-5 days** with SCD41 enabled, or **~2-3 weeks** with SCD41 disabled.

To extend battery life further:
- Power the soil moisture sensor from a GPIO (high during read, low otherwise)
- Increase the report interval to 5-10 minutes
- Disconnect SCD41 if CO2 is not critical
- Use deep sleep with RTC wakeup

### Actuator node (mains)

Always-on. Total power budget depends on the actuators connected; the ESP32-H2 itself draws ~30mA active.

### Gateway (USB-powered)

The ESP32-S3 + ESP32-H2 combo draws ~80mA idle, ~200mA during WiFi TX. USB power is sufficient.

---

## Enclosure recommendations

- **Sensor node**: IP65-rated weatherproof enclosure with cable glands for the soil probe. Add a small desiccant pack to prevent condensation.
- **Actuator node**: ventilated enclosure (the relays and MOSFET generate heat). Keep high-voltage mains wiring separated from low-voltage control wiring.
- **Gateway**: indoor enclosure only. Keep antennas (WiFi + Zigbee) clear of metal.

---

## Antenna notes

The ESP32-H2-DevKitM-1 has an on-board PCB antenna for 802.15.4. Range in open air is ~30m line-of-sight. For larger greenhouses, consider:

- Adding a router (actuator node) every 20-30m to extend the mesh
- Using an external antenna version of the ESP32-H2 module (ESP32-H2-MINI-1U)
- Keeping the gateway centrally located and elevated

---

## Schematic diagrams

Hand-drawn schematics are not included in this repo, but the wiring tables above are sufficient to assemble the system on breadboard or perfboard. For production, design a custom PCB integrating:

- ESP32-H2-MINI-1 module
- Sensor breakouts
- Power management (TP4056 + boost converter for battery operation)
- Relay drivers with flyback diodes
- Terminal blocks for sensor and actuator connections
