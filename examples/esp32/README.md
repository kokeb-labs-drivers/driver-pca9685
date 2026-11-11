# PCA9685 ESP32 examples

ESP-IDF applications that run the PCA9685 driver on an ESP32-C6, built by CI on every pull request.

## Wiring (ESP32-C6 DevKitC-1)

| ESP32-C6 | PCA9685 (TSSOP28 pin) | Notes |
| --- | --- | --- |
| GPIO6 | SDA (27) | I2C0, 400 kHz; 4.7 kΩ pull-up to 3V3 on the board |
| GPIO7 | SCL (26) | 4.7 kΩ pull-up to 3V3 |
| GPIO11 | OE (23) | 10 kΩ pull-up to 3V3, so the outputs stay off until the ESP32 drives it (`Esp32Pca9685Port`). A board with OE tied to GND uses `Esp32Pca9685PortOeTied` instead |
| GND | A0 to A5 (1 to 5, 24) | Address `0x40`; tie each pin high or low, never open |
| GND | EXTCLK (25) | Grounded when the internal oscillator is used |
| 3V3 | VDD (28) | 100 nF to GND at the pin |
| GND | VSS (14) | |
| LED0 (6) | LED with resistor to GND | An LED driven high by the totem-pole output (`Config{}`) |
| LED1 (7) | Servo signal | Servo powered from its own 5 V supply, grounds joined |

## Build and flash

```bash
git submodule update --init scripts      # the ESP-IDF build scripts
. $IDF_PATH/export.sh                     # ESP-IDF v5.5
./scripts/build_app.sh list
./scripts/build_app.sh pca9685_basic_example Debug
./scripts/flash_app.sh flash_monitor pca9685_basic_example Debug
```

`idf.py menuconfig` shows "PCA9685 PWM controller" under Component config: the PWM frequency, the
oscillator frequency, the clock source, open drain and inversion that `pca9685::KconfigDefaults()`
returns. Set 50 Hz for the servo.

## Expected output

```text
I (312) pca9685_example: PCA9685 driver 0.1.0
pca9685: initialised
I (420) pca9685_example: duty 0/4096, servo 999948 ns
I (520) pca9685_example: duty 204/4096, servo 1050940 ns
```

An `EnsureInitialized failed: Bus` line means nothing acknowledged address 0x40: check SDA, SCL, the
pull-ups, VDD and the A0 to A5 straps. `DeviceIdMismatch` means something answered that is not a
PCA9685.

## Files

| File | What it is |
| --- | --- |
| [`main/pca9685_basic_example.cpp`](main/pca9685_basic_example.cpp) | The example app |
| [`main/esp32_pca9685_port.hpp`](main/esp32_pca9685_port.hpp) | The ESP-IDF ports: bus, delay, log, and OE on a GPIO or tied to GND |
| [`CMakeLists.txt`](CMakeLists.txt) | The ESP-IDF project; `EXTRA_COMPONENT_DIRS` points at the repository root, which is the component |
| [`app_config.yml`](app_config.yml) | The app matrix for the scripts, CI and the docs |
