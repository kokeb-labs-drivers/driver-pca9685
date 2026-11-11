# Datasheet map

The driver follows the PCA9685 datasheet. This page says which revision, where to get it, and where each part of
the driver is described in it, so you can check any behaviour against the source. Section names and page numbers
are the datasheet's own (the printed page numbers).


<!-- generated: datasheet-map -->
- **Datasheet: PCA9685 16-channel, 12-bit PWM Fm+ I2C-bus LED controller**, NXP PCA9685, revision Rev. 4 (2015-04-16).
  Get it from the [vendor page](https://www.nxp.com/products/PCA9685) or as a
  [PDF](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf).
  SHA-256 of the file used: `237d47f339cac4c3a0d56a5f0b4d3c93df71e3eb43f36ac57ea4ff38e6b2e585`.

| Topic | Datasheet section | Page | In the driver | Explained on |
| --- | --- | --- | --- | --- |
| Pins and packages (TSSOP28, HVQFN28) | 6 Pinning information; Table 3 Pin description | [p. 6](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=6) | `CtrlPin::OutputEnable` | [Hardware](hardware.md), [Quickstart](quickstart.md) |
| Slave address, A0 to A5 | 7.1.1 Regular I2C-bus slave address; Figure 4 | [p. 7](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=7) | `kI2cAddress`, `kMinI2cAddress`, `kMaxI2cAddress`, `Error::InvalidArgument` | [Hardware](hardware.md), [Porting](porting.md), [Troubleshooting](troubleshooting.md) |
| LED All Call and Sub Call addresses | 7.1.2 LED All Call I2C-bus address; 7.1.3 LED Sub Call I2C-bus addresses | [p. 8](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=8) | `kAllCallAddress`, MODE1.ALLCALL and SUB1 to SUB3 cleared by `EnsureInitialized()` | [Hardware](hardware.md), [Contract](contract.md) |
| Software Reset address | 7.1.4 Software Reset I2C-bus address | [p. 9](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=9) | (not sent by the driver; its effect is found by `CheckForReset()`) | [Hardware](hardware.md) |
| Control register | 7.2 Control register | [p. 9](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=9) | `pca9685::reg` (`inc/pca9685_registers.hpp`) | [Architecture](architecture.md) |
| Register summary and auto-increment | Table 4 Register summary | [p. 10](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=10) | `pca9685::reg`, MODE1.AI set by `EnsureInitialized()`, bursts of up to 64 bytes | [Architecture](architecture.md), [Contract](contract.md) |
| MODE1, sleep, EXTCLK and the oscillator start-up | 7.3.1 Mode register 1, MODE1; Table 5 | [p. 14](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=14) | `EnsureInitialized()`, `Sleep()`, `Wake()`, `kOscillatorStartUs`, `Config::clock`, `Error::ExternalClockLatched` | [API guide](api-guide.md), [Configuration](configuration.md), [Contract](contract.md) |
| Restart mode | 7.3.1.1 Restart mode | [p. 15](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=15) | `Sleep(SleepMode::HoldOutputs)`, `Wake()`, `SetPwmFrequency()`, `Error::Asleep` | [API guide](api-guide.md), [Contract](contract.md) |
| MODE2, output drive, inversion, change on STOP, OUTNE | 7.3.2 Mode register 2, MODE2; Table 6 | [p. 16](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=16) | `Config::output_drive`, `Config::invert_outputs`, `detail::Mode2Value()` | [Configuration](configuration.md), [Hardware](hardware.md), [Contract](contract.md) |
| LED output and PWM control, full ON and full OFF | 7.3.3 LED output and PWM control; Table 7; Figure 9 | [p. 16](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=16) | `Pwm`, `PwmForDuty()`, `SetPwm()`, `SetDuty()`, `SetFullOn()`, `SetChannelOff()`, `SetChannels()` | [API guide](api-guide.md), [Architecture](architecture.md) |
| ALL_LED registers and the orderly shutdown | 7.3.4 ALL_LED_ON and ALL_LED_OFF control; Table 8 | [p. 25](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=25) | `SetAllOff()`, `Sleep(SleepMode::OutputsOff)`, `EnsureInitialized()`, `Deinitialize()` | [API guide](api-guide.md), [Contract](contract.md) |
| PWM frequency, PRE_SCALE | 7.3.5 PWM frequency PRE_SCALE; Equation 1 | [p. 25](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=25) | `PrescaleFor()`, `FrequencyFor()`, `CountsForPulseUs()`, `SetPwmFrequency()`, `SetPulseWidthUs()`, `Validate()` | [Configuration](configuration.md), [API guide](api-guide.md) |
| SUBADR1, the reset sentinel | 7.3.6 SUBADR1 to SUBADR3; Table 9 | [p. 26](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=26) | `detail::kSubAdr1Sentinel`, written by `EnsureInitialized()`, read by every reset check | [Contract](contract.md) |
| Output enable, OE | 7.4 Active LOW output enable input; Table 11 | [p. 27](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=27) | `DisableOutputs()`, `EnableOutputs()`, `detail::OffLevelOutne()` | [Hardware](hardware.md), [Porting](porting.md), [Contract](contract.md) |
| Power-on reset | 7.5 Power-on reset | [p. 27](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=27) | `CheckForReset()`, `Error::DeviceReset`, the reset sentinel MODE1.AI | [Contract](contract.md), [Troubleshooting](troubleshooting.md) |
| Software reset by the general call | 7.6 Software reset | [p. 28](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=28) | `CheckForReset()`, `Error::DeviceReset` | [Contract](contract.md), [Troubleshooting](troubleshooting.md) |
| External drivers, INVRT and OUTDRV | 7.7 Using the PCA9685 with and without external drivers; Table 12; Figures 13 to 15 | [p. 29](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=29) | `DirectLedPreset()`, `ServoPreset()`, `Config::invert_outputs`, `Config::output_drive` | [Hardware](hardware.md), [Configuration](configuration.md) |
| I2C bus transactions | 9 Bus transactions; Figures 20 to 24 | [p. 32](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=32) | `I2cInterface`, `I2cPort`, `kRepeatedStart`, every register access | [Porting](porting.md), [Architecture](architecture.md) |
| Typical application | 10 Application design-in information; Figure 26 | [p. 35](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=35) | - | [Hardware](hardware.md) |
| Supply, power-on reset voltage and output currents | 11 Limiting values; 12 Static characteristics | [p. 38](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=38) | - | [Hardware](hardware.md) |
| I2C timing, EXTCLK and OE propagation | 13 Dynamic characteristics; Table 15 | [p. 40](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=40) | `kMaxClockHz`, `kMaxOscillatorHz`, `DisableOutputs()` | [Hardware](hardware.md), [Porting](porting.md) |
<!-- /generated -->

## When the datasheet and the driver differ

The driver is written against the revision above. If you have a newer revision, compare its revision history with
the topics above; a change to a pin, a register, a timing or a value the driver uses is a bug report for this
repository. Where the driver deliberately goes beyond the datasheet, the [Contract](contract.md) says so.

The repository never contains the PDF itself: vendors own their documents. Download it from the vendor, and compare
its SHA-256 with the one above to confirm you have the same file.
