---
hide:
  - navigation
  - toc
---

<div class="kb-hero" markdown>
<span class="kb-kicker">Driver</span>

# PCA9685 Driver

<p>A C++20 driver for the NXP PCA9685 16-channel, 12-bit PWM controller. It starts the chip from any state
with every output off, sets duty, phase and servo pulse widths, changes the frequency by the data sheet's sleep and
restart sequence, finds chip resets and never refuses a switch-off, on any MCU.</p>

[Quickstart](quickstart.md){ .md-button .md-button--primary }
[API guide](api-guide.md){ .md-button }
[Source on GitHub](https://github.com/kokeb-labs-drivers/driver-pca9685){ .md-button }
</div>

The driver has no platform code. You give it a small class that brings the bus up and moves bytes; it does the
rest: the start-up from any state, the register framing with auto-increment, the frequency arithmetic, the
sleep and restart sequence, reset detection and the bookkeeping of writes that may or may not have arrived.

<div class="kb-chip" markdown>
<div class="kb-chip__head">
<span class="kb-chip__kicker">PWM controller</span>
<span class="kb-chip__name">PCA9685</span>
<span class="kb-chip__vendor">NXP</span>
</div>

Function
:   16 outputs, each with its own 12-bit PWM (4096 steps) and turn-on delay, all at one frequency

Frequency
:   About 24 Hz to 1526 Hz from the 25 MHz internal oscillator (PRE_SCALE 3 to 255), or from EXTCLK up to 50 MHz

Interface
:   I2C up to 1 MHz (Fm+); 7-bit address `0x40` to `0x7F` from A0 to A5; register auto-increment

Outputs
:   Totem pole (25 mA sink, 10 mA source at 5 V) or open drain; invertible; OE pin sets all to a chosen level

Supply
:   2.3 V to 5.5 V; 5.5 V tolerant inputs and outputs
</div>


## What you get

- **One object per chip.** `PCA9685<YourPort>` takes a reference to your port and does no I/O until
  `EnsureInitialized()`. See [API guide](api-guide.md).
- **Switch-offs you can rely on.** A switch-off is always sent, whatever the driver doubts, and `Ok` means the
  output is off; `DisableOutputs()` uses the OE pin, no bus needed. See [Contract](contract.md).
- **Resets found.** A power-on or software reset is found by its MODE1 sentinel, and the driver never writes its
  cached state over a reset chip. See [Contract](contract.md#resets).
- **Errors you can act on.** Every call returns `Result<T>` with an `Error` such as `Bus`, `DeviceReset` or
  `StateUnknown`. See [API guide](api-guide.md#handle-errors).
- **Datasheet defaults.** `Config{}` holds the chip's reset values, with units in the field names, plus two
  presets for LEDs and servos. See [Configuration](configuration.md).
- **Zero-overhead portability.** The port is a CRTP class checked by a concept: the bus hooks and a delay, plus
  optional OE, failure and logging hooks found at compile time. See [Porting](porting.md).
- **No heap, no exceptions, no RTTI.** The host tests build with `-fno-exceptions -fno-rtti`.
- **Tested without hardware.** Host tests and a fuzzer run the driver against a register model of the chip that
  resets and sleeps on its own. See [Testing and CI](testing.md).

## Datasheet

<!-- generated: datasheet-card -->
| Document | Revision | Get it |
| --- | --- | --- |
| Datasheet: PCA9685 16-channel, 12-bit PWM Fm+ I2C-bus LED controller (NXP PCA9685) | Rev. 4, 2015-04-16 | [PDF](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf) · [vendor page](https://www.nxp.com/products/PCA9685) |

Written against these revisions; the SHA-256 of each file is in [`docs/datasheets.yml`](datasheets.yml). Where each part
of the driver is in the datasheet: [Datasheet map](datasheet.md).
<!-- /generated -->

## Where to go next

<div class="grid cards" markdown>

-   :material-rocket-launch-outline: **[Quickstart](quickstart.md)**

    ---

    Wire the chip, write the port, dim an LED in about 15 lines.

-   :material-chip: **[Hardware](hardware.md)**

    ---

    Pins, address, OE, EXTCLK, pull-ups, LEDs and servos, power.

-   :material-file-document-check-outline: **[Contract](contract.md)**

    ---

    What the driver promises, what it needs, threading and errors.

-   :material-code-braces: **[API guide](api-guide.md)**

    ---

    Start, set channels, change the frequency, sleep, handle resets and errors.

-   :material-swap-horizontal: **[Porting](porting.md)**

    ---

    Implement the port for your MCU.

-   :material-book-open-variant: **[API reference](api-reference.md)**

    ---

    Every class and member, generated from the headers.

</div>
