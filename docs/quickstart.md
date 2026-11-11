# Quickstart

Run the driver on your PC first, with no hardware; then dim an LED from your MCU: wire the chip, write a small
port class, call three driver functions.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From NXP PCA9685 Rev. 4:

    - Pins and packages (TSSOP28, HVQFN28): 6 Pinning information; Table 3 Pin description, [p. 6](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=6)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Try it on your PC

[`examples/host/`](../examples/host/README.md) runs the real driver against a simulated PCA9685, through a host
port. From the repository root, with CMake and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/pca9685_host_example
```

```text
PCA9685 driver 0.1.0 on the host
pca9685: initialised
PWM 50 Hz (PRE_SCALE 121)
LED0 duty 0/4096
LED0 duty 1024/4096
LED0 duty 2048/4096
LED0 duty 3072/4096
LED0 duty 4096/4096
servo on LED1: 1498160 ns pulse
```

The calls in [`main.cpp`](../examples/host/main.cpp) are the ones below; only the port differs on an MCU. CI builds
and runs this example on every pull request.

## 1. Wire it

| MCU | PCA9685 (TSSOP28 pin) | Notes |
| --- | --- | --- |
| I2C SCL | SCL (26) | Pull-up to VDD; 4.7 kΩ suits 100 kHz and 400 kHz |
| I2C SDA | SDA (27) | Pull-up to VDD |
| GND | A0 to A5 (1 to 5, 24) | Address `0x40` (`kI2cAddress`); the pins have no pull-ups, never leave one open |
| GND | EXTCLK (25) | Must be grounded when the internal oscillator is used |
| GND | OE (23) | Outputs always enabled for this example |
| 3.3 V or 5 V | VDD (28) | 2.3 V to 5.5 V; 100 nF to VSS close to the pin |
| GND | VSS (14) | |
| LED0 (6) | LED anode, cathode through 330 Ω to GND | Driven high by the totem-pole output |

The [Hardware](hardware.md) page has every pin of both packages, OE wired to a GPIO, servos, open-drain LEDs and
the power.

## 2. Add the driver

Add the repository as a submodule and link the `pca9685::pca9685` CMake target. [Installation](installation.md)
covers CMake on any MCU, the installed package and ESP-IDF.

```bash
git submodule add https://github.com/kokeb-labs-drivers/driver-pca9685.git external/driver-pca9685
```

## 3. Write the port

The port is the only platform code: it brings the bus up, moves bytes and waits. Replace the `platform_*` calls
with your SDK's.

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

[Porting](porting.md) explains each method, the optional hooks and the transfer rules.

## 4. Dim the LED

```cpp title="main.cpp"
--8<-- "tests/docs/quickstart.cpp:quickstart"
```

What each step does:

1. `EnsureInitialized()` calls your port's `EnsureInitialized()`, switches every output off whatever state the
   chip was left in, writes `Config{}` (the reset values: 200 Hz, totem pole, not inverted), reads it back to
   prove a PCA9685 answers, and wakes the oscillator with its 500 µs start-up wait.
2. `SetDuty(0, 1024)` writes LED0's four registers in one transfer: on at count 0, off at count 1024 of 4096.
3. `SetChannelOff(0)` sets LED0's full OFF bit. A switch-off is never refused.

This example is built by the host CI, so it compiles against the current headers.

## If it does not work

| Symptom | First thing to check |
| --- | --- |
| `EnsureInitialized()` returns `Bus` | Nothing acknowledged 0x40: SDA, SCL, the pull-ups, VDD, A0 to A5 ([Troubleshooting](troubleshooting.md#ensureinitialized-returns-bus-or-nodevice)) |
| `EnsureInitialized()` returns `DeviceIdMismatch` | Something answered that is not a PCA9685, or SDA is stuck |
| `EnsureInitialized()` returns `ExternalClockLatched` | EXTCLK is set on the chip from an earlier run: power-cycle it |
| The calls return `Ok` but the LED stays dark | OE is high, or the LED's polarity needs `invert_outputs` ([Troubleshooting](troubleshooting.md#the-calls-return-ok-but-an-output-does-not-change)) |

## Next

- [API guide](api-guide.md): servos, banks of channels, the frequency, sleep and resets.
- [Configuration](configuration.md): presets, every `Config` field, Kconfig.
- [Examples](examples.md): the host example and the MCU examples.
- [Platforms](platforms.md): which MCUs have a port and an example.
