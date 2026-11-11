# Figures

<!-- Written by kokeb-docs figures from docs/diagrams/figures/*.py. Do not edit by hand. -->

Every figure on this site is drawn by a Python module from facts extracted from the code, so it is rebuilt,
not redrawn, when the code changes. CI runs `kokeb-docs figures --check` and fails when a
committed figure no longer matches the code. Each figure below lists the files it is drawn from.

## One channel write and its outcomes

<!-- figure: channel-write -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="channel-write.dark.svg">
  <img src="channel-write.svg"
    alt="A row of five bytes, the control register LEDn_ON_L then ON_L, ON_H, OFF_L, OFF_H, applied at the
    STOP; below, three outcome cards: acknowledged, NACK part-way, address NACK; at the bottom the one-
    byte switch-off.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/channel-write.py`](../../docs/diagrams/figures/channel-write.py)

Drawn from:

- `applyOne, switchOn, switchOff, afterSwitchOff, writeChannels` in [`src/pca9685.ipp`](../../src/pca9685.ipp)
- `kLed0OnL, kBytesPerChannel, kLedFull` in [`inc/pca9685_registers.hpp`](../../inc/pca9685_registers.hpp)

## Inside the PCA9685

<!-- figure: chip-blocks -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-blocks.dark.svg">
  <img src="chip-blocks.svg"
    alt="Block diagram of the PCA9685: its function blocks, how they connect and which package pins reach
    each block.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/pca9685_i2c_interface.hpp`](../../inc/pca9685_i2c_interface.hpp)

## PCA9685: Typical application

<!-- figure: chip-circuit-typical -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-typical.dark.svg">
  <img src="chip-circuit-typical.svg"
    alt="Schematic of the PCA9685 typical application: the chip with U2 MCU, R1 resistor 4.7 kΩ, R2 resistor
    4.7 kΩ, R3 resistor 10 kΩ, C1 capacitor 100 nF, R4 resistor 150 Ω, D1 led, J1 5 V supply; the MCU
    wires carry the port hooks GpioSet(CtrlPin::OutputEnable), Write() / WriteRead().">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/pca9685_i2c_interface.hpp`](../../inc/pca9685_i2c_interface.hpp)

## PCA9685 pinout, TSSOP-28

<!-- figure: chip-pinout -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-pinout.dark.svg">
  <img src="chip-pinout.svg"
    alt="Pinout of the PCA9685 in TSSOP-28, top view: 28 pins, each with its name, its electrical type as a
    colour and the driver hook that uses it.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/pca9685_i2c_interface.hpp`](../../inc/pca9685_i2c_interface.hpp)

## The layers, one of them yours

<!-- figure: driver-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="driver-layers.dark.svg">
  <img src="driver-layers.svg"
    alt="Layer diagram: your application calls the PCA9685 core, which sends register reads and writes
    through the I2C port contract to the port class you write, which drives the chip; the core and the
    contract are the same on every MCU.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/driver-layers.py`](../../docs/diagrams/figures/driver-layers.py)

Drawn from:

- `public calls` in [`inc/pca9685.hpp`](../../inc/pca9685.hpp)
- `I2cInterface` in [`inc/pca9685_i2c_interface.hpp`](../../inc/pca9685_i2c_interface.hpp)

## From the registers to the pin

<!-- figure: output-path -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="output-path.dark.svg">
  <img src="output-path.svg"
    alt="Five stages left to right: full OFF, full ON and the counter decide the channel logic; sleep freezes
    it; INVRT and OUTDRV set the pin level; OE high replaces it with the OUTNE level; a table gives the
    off level per configuration.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/output-path.py`](../../docs/diagrams/figures/output-path.py)

Drawn from:

- `OffLevelOutne, Mode2Value` in [`inc/pca9685.hpp`](../../inc/pca9685.hpp)
- `kOutneLow, kOutneHigh, kOutneHiZ, kLedFull` in [`inc/pca9685_registers.hpp`](../../inc/pca9685_registers.hpp)
- `applyOne` in [`src/pca9685.ipp`](../../src/pca9685.ipp)

## Sleep, wake and restart

<!-- figure: sleep-restart -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="sleep-restart.dark.svg">
  <img src="sleep-restart.svg"
    alt="Two columns: Sleep() with the outputs off or held, and the wake that clears SLEEP, waits for the
    oscillator, checks MODE1, MODE2 and PRE_SCALE, then writes RESTART and checks again; a band below
    says what a failed check leaves.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/sleep-restart.py`](../../docs/diagrams/figures/sleep-restart.py)

Drawn from:

- `Sleep, Wake, wakeSequence, completeWakeCheck, verifyRunning, SetPwmFrequency, changePrescale` in [`src/pca9685.ipp`](../../src/pca9685.ipp)
- `kMode1Sleep, kMode1Restart` in [`inc/pca9685_registers.hpp`](../../inc/pca9685_registers.hpp)
- `kOscillatorStartUs` in [`inc/pca9685_types.hpp`](../../inc/pca9685_types.hpp)

## What EnsureInitialized() does

<!-- figure: start-up -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="start-up.dark.svg">
  <img src="start-up.svg"
    alt="Flow chart of EnsureInitialized(): check the configuration and the address, bring the bus up, switch
    every output off, sleep and write PRE_SCALE, read everything back as the identity check, wake the
    oscillator and check the wake, enable OE; each failing step returns its error.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/start-up.py`](../../docs/diagrams/figures/start-up.py)

Drawn from:

- `EnsureInitialized, initialize, enterSafeState, configureClock, checkIdentity, wakeSequence, addressValid` in [`src/pca9685.ipp`](../../src/pca9685.ipp)
- `kMinI2cAddress, kMaxI2cAddress, kAllCallAddress, kOscillatorStartUs` in [`inc/pca9685_types.hpp`](../../inc/pca9685_types.hpp)
- `kAllLedOffH, kLedFull` in [`inc/pca9685_registers.hpp`](../../inc/pca9685_registers.hpp)
