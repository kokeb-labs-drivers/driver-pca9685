# Configuration

`pca9685::Config` holds everything `EnsureInitialized()` writes to the chip. Its defaults are the chip's reset
values, so `Config{}` describes a chip that was just reset.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From NXP PCA9685 Rev. 4:

    - MODE1, sleep, EXTCLK and the oscillator start-up: 7.3.1 Mode register 1, MODE1; Table 5, [p. 14](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=14)
    - MODE2, output drive, inversion, change on STOP, OUTNE: 7.3.2 Mode register 2, MODE2; Table 6, [p. 16](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=16)
    - PWM frequency, PRE_SCALE: 7.3.5 PWM frequency PRE_SCALE; Equation 1, [p. 25](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=25)
    - External drivers, INVRT and OUTDRV: 7.7 Using the PCA9685 with and without external drivers; Table 12; Figures 13 to 15, [p. 29](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=29)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Fields

| Field | Default (reset value) | Register | Range |
| --- | --- | --- | --- |
| `pwm_frequency_hz` | 200 | PRE_SCALE = round(oscillator / (4096 x f)) - 1, 0x1E | PRE_SCALE 3 to 255: about 24 Hz to 1526 Hz at 25 MHz (section 7.3.5) |
| `oscillator_hz` | 25 000 000 | (none: the arithmetic) | 1 MHz to 50 MHz; the internal oscillator is 25 MHz typical |
| `clock` | `ClockSource::Internal` | MODE1.EXTCLK | `External` is sticky until a power cycle or software reset (table 5) |
| `output_drive` | `OutputDrive::TotemPole` | MODE2.OUTDRV | `OpenDrain` for LEDs with Zener diodes (section 10, question 5) |
| `invert_outputs` | `false` | MODE2.INVRT | `true` for loads wired active low (table 12) |

What the driver sets itself:

| Register bits | Value | Why |
| --- | --- | --- |
| MODE1.AI | 1 | Bursts of up to 64 bytes; the reset sentinel ([Contract](contract.md#resets)) |
| MODE1.ALLCALL, SUB1 to SUB3 | 0 | No group address can change the chip behind the driver |
| MODE2.OCH | 0 | Every byte of one transfer reaches the outputs together at the STOP ([Contract](contract.md#writes-that-may-or-may-not-have-arrived)) |
| MODE2.OUTNE | the off level | OE high always switches the outputs off ([Hardware](hardware.md#output-enable-oe)) |

The frequency follows equation 1 with rounding: the frequency the chip runs at is
`oscillator / (4096 x (PRE_SCALE + 1))`, which `GetPwmFrequencyHz()` returns and `FrequencyFor()` computes
offline. 200 Hz programs 197 Hz, 50 Hz programs 50 Hz, 1000 Hz programs 1017 Hz.

## Presets

| Preset | For | Differs from `Config{}` |
| --- | --- | --- |
| `ServoPreset()` | Hobby servos, external N-type drivers | 50 Hz |
| `DirectLedPreset()` | LEDs wired from the supply into the outputs (figure 15) | 1000 Hz, open drain, inverted |

`DirectLedPreset()` wires a load active low: after a chip reset it is lit until `EnsureInitialized()` runs again
([Hardware](hardware.md#reference-application-circuit)).

```cpp
--8<-- "tests/docs/configuration.cpp:custom"
```

## Validation

`Validate(config)` checks a configuration without the chip: every enumeration holds an enumerator, the oscillator
is in range, the frequency is 1 Hz to `kMaxPwmFrequencyHz` and its prescaler 3 to 255. `EnsureInitialized()` and
`ApplyConfig()` run it before any I/O and return `InvalidArgument`. A value is never clamped.

A configuration known at build time can be proven there:

```cpp
--8<-- "tests/docs/configuration.cpp:checked"
```

## Changing it later

`SetPwmFrequency()` changes the frequency while running ([API guide](api-guide.md#change-the-frequency)).
`ApplyConfig()` on a started driver runs the whole start-up again with the new configuration, which switches every
channel off first; before the start it only stores it.

## Kconfig

Under ESP-IDF, `idf.py menuconfig` has a **PCA9685 PWM controller** menu ([`_config/Kconfig`](../_config/Kconfig)):
the frequency, the oscillator, the external clock, open drain and inversion. `pca9685::KconfigDefaults()` in
`pca9685_kconfig.hpp` returns `Config{}` with those values; without Kconfig it equals `Config{}`.

```cpp
--8<-- "tests/docs/configuration.cpp:kconfig"
```
