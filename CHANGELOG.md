# Changelog

All notable changes to this repository are listed here. Versions follow [Semantic Versioning](https://semver.org/).
Entries under **Breaking** need source changes in code that used the affected API; any of them calls for a major
version bump (a minor bump before 1.0).

## [Unreleased]

### Changed

- The repository moved to kokeb-labs-drivers; URLs follow.

## [0.1.0] - 2026-11-10

### Added

- **Driver.** `pca9685::PCA9685<Port>` for the NXP PCA9685 16-channel, 12-bit PWM controller (data sheet Rev. 4):
  header-only C++20, no heap, exceptions or RTTI, every call returning `Result<T>`.
- **Start-up from any state.** `EnsureInitialized()` switches every output off first (OE high, ALL_LED_OFF_H before
  and after MODE2 with OCH = 0), sets SLEEP and AI, EXTCLK by the two-write sequence, PRE_SCALE and the SUBADR1
  reset sentinel, reads back MODE1, MODE2, SUBADR1, PRE_SCALE and every channel as the identity check, wakes the
  oscillator with its 500 µs wait, checks the wake and enables OE. It refuses the LED All Call address 0x70 and
  clears ALLCALL and the Sub Call enables. Once started, a later call runs the reset check instead of returning at
  once.
- **Channels.** `SetPwm()`, `SetDuty()` with a phase delay, `SetPulseWidthUs()` from the programmed prescaler,
  `SetFullOn()`, `SetChannelOff()`, `SetChannels()` (up to 16 in one transfer), `SetChannelsOff()`, `SetAllOff()`;
  a switch-on writes its four registers in one transfer, applied at the STOP; a switch-off is one data byte,
  LEDn_OFF_H or ALL_LED_OFF_H = 0x10, so even a failed switch-off never makes a channel more active; bank calls send
  their switch-offs first; `Verify::ReadBack` on the single-channel calls.
- **Frequency, sleep and restart.** `SetPwmFrequency()` by sleep, PRE_SCALE, read-back, wake and RESTART;
  `Sleep(SleepMode::OutputsOff)` and `Sleep(SleepMode::HoldOutputs)`; `Wake()` with the RESTART sequence.
- **Output enable.** `DisableOutputs()` and `EnableOutputs()` through the optional `GpioSet()` hook; the driver
  chooses MODE2.OUTNE so that OE high always gives the level of a channel that is off.
- **Resets.** A power-on or software reset is found by MODE1.AI = 0, or MODE2, EXTCLK, SUBADR1 (a sentinel no MODE1
  write can mask) or PRE_SCALE back at its reset value, confirmed by a second read (`DeviceReset`,
  `CheckForReset()`); acting on it drives OE high and writes ALL_LED_OFF_H. MODE1 is never written from the copy
  over a reset chip, every write that clears SLEEP is followed by the wake check, and `EnableOutputs()` checks first.
  Once started, the switch-offs keep working after a reset was found, and `Deinitialize()` always does its work:
  OE high first, which needs no bus, and nothing at all on an address the start-up refuses (0x70, LED All Call).
- **Stuck lines.** A reserved bit set is `InvalidReply`; a channel read of all zeros, which SDA stuck low also gives,
  is data only when one more MODE1/MODE2/SUBADR1 read matches the running chip.
- **Failed writes.** A switch-on or burst that failed may have arrived: channels and the prescaler become unknown
  (`GetUncertainChannels()`, `StateUnknown`); switch-ons that depend on them are refused, switch-offs never are,
  and a bank call that refuses its switch-ons still sends its switch-offs.
- **Configuration.** `Config` with the reset values, `Validate()`, `CheckedConfig`, `ServoPreset()`,
  `DirectLedPreset()`, `PrescaleFor()`, `FrequencyFor()`, `CountsForPulseUs()`, `PulseNsForCounts()`,
  `PwmForDuty()`; Kconfig options for ESP-IDF.
- **Port.** `I2cInterface` and the `I2cPort` concept, bus requirements (`kI2cAddress`, `kMaxClockHz`,
  `kRepeatedStart`), optional `GpioSet()`, `GetLastFailure()` and `Log()` hooks; the host port and two ESP-IDF
  ports, OE on a GPIO and OE tied to VSS.
- **Tests.** A register model written from the data sheet that resets, sleeps and restarts on its own and offers
  both readings of each open question; host tests for every call and failure path, including a NACK at every byte
  of every switch-off and a reset before every transfer of a session; a fuzz target whose invariants are the safety
  rules (a switch-off never makes a channel more active), with its corpus.
- **Docs.** Every page for this chip, the hardware page with the pins of both packages and the application
  circuits, and figures of the start-up, the sleep and restart sequence, a channel write and the output path;
  `docs/hardware.yml` with the pinout, block diagram and typical-application figures and the generated pin and
  parts tables drawn from it.
