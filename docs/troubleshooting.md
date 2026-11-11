# Troubleshooting

Each section starts from what you see: an error a call returns, or an output that does not do what you set.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From NXP PCA9685 Rev. 4:

    - Slave address, A0 to A5: 7.1.1 Regular I2C-bus slave address; Figure 4, [p. 7](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=7)
    - Power-on reset: 7.5 Power-on reset, [p. 27](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=27)
    - Software reset by the general call: 7.6 Software reset, [p. 28](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=28)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## EnsureInitialized returns Bus or NoDevice

The port reported a failure: either its `EnsureInitialized()` returned `false`, or nothing acknowledged the address.
`NoDevice` needs a port with `GetLastFailure()`; without it the same fault is `Bus`.

1. Check that your port's `EnsureInitialized()` returns `true` on its own (log it).
2. Check the address: `0x40` plus A5 to A0 ([Hardware](hardware.md#address)). An open address pin floats: tie each
   one high or low.
3. Check SDA and SCL against the board, their pull-ups and VDD (2.3 V to 5.5 V).
4. With a logic analyser, check that the first frame, `0xFD 0x10` to the address, appears and is acknowledged.
5. After a bus error, clear the bus (nine SCL pulses with SDA released) before calling `EnsureInitialized()` again.

## EnsureInitialized returns DeviceIdMismatch

The chip has no ID register: the start-up writes MODE1, MODE2 and PRE_SCALE, then reads them back with all 16
channels, and something did not match.

| What you see on the bus | Likely cause | Fix |
| --- | --- | --- |
| Every read returns `0x00` | SDA held low: a stuck device, a short | Release the bus; check SDA continuity and other parts on the bus |
| Every read returns `0xFF` | Another part answers, or an ACK glitch | Scan the bus; check the address straps |
| MODE1 reads back without AI | Another part at the address | Check the part number on the package |
| A channel does not read full OFF | The off write did not stick | Check the supply and the bus integrity |

## EnsureInitialized returns ExternalClockLatched

MODE1.EXTCLK is set on the chip, from an earlier run that used EXTCLK, but `Config::clock` asks for the internal
oscillator. The bit is sticky (table 5): power-cycle the chip, or send the general-call software reset, or
configure `ClockSource::External` with the clock's frequency. Every output was switched off before the refusal.

## A call returns DeviceReset

The chip reset since the driver started it, confirmed by two reads: MODE1.AI read 0, or MODE2, EXTCLK, SUBADR1 or
PRE_SCALE was back at its reset value. The driver drove OE high and wrote ALL_LED_OFF_H; switch-offs keep working.
Usually a supply dip below V_POR (servo stall currents on a shared supply are the classic cause), or a general
call `0x06` from another master. The chip is asleep with every channel off; call `EnsureInitialized()` and write
the channels again. If it repeats, check the supply and its decoupling ([Hardware](hardware.md#power)).

## A call returns InvalidReply

A read returned what no PCA9685 can send: a read-only bit set, or a MODE1/MODE2/SUBADR1 frame that cannot be one
chip's. The line is floating, stuck or shared. Check SDA, the pull-ups and the other parts on the bus; the value was
not used, and one such read is never taken for a reset.

## A switch-off returns Asleep

The write was sent, but the chip is asleep (or may be, after a failed wake) and may hold its outputs: asleep, an
output can be neither switched on nor off (table 5, note 4). `DisableOutputs()` switches every output off at once
through OE, with no bus traffic; `Wake()` lets the outputs follow the registers again. A switch-on while asleep returns
`Asleep` too: the registers hold the value for the wake.

## A call returns StateUnknown

An earlier write failed and may have arrived in part: the call depends on what it left.

| Call | Unknown | Fix |
| --- | --- | --- |
| `GetChannel()`, `EnableOutputs()` | A channel (`GetUncertainChannels()`) | `ReadChannels()`, or write the channel again |
| `SetPulseWidthUs()`, `GetPrescale()` | The prescaler | `ReadPrescale()`, or `SetPwmFrequency()` again |

## SetPwmFrequency returns WriteMismatch

PRE_SCALE did not take the value. It ignores a write while MODE1.SLEEP is 0 and still acknowledges it (table 4,
note 1), so this means the SLEEP write was lost; `GetPrescale()` returns the value the chip holds. Retry, and check
the bus for noise if it repeats.

## The calls return Ok but an output does not change

| Check | Why |
| --- | --- |
| OE | High disables every output; with `GpioSet()`, `EnableOutputs()` |
| `MayBeAsleep()` | Asleep, the outputs do not follow; `Wake()` |
| `CheckForReset()` | A reset the driver has not seen leaves the chip asleep, and channel writes cannot tell |
| `invert_outputs` and the wiring | An LED from the supply into the output needs `DirectLedPreset()`; otherwise it lights when "off" ([Hardware](hardware.md#reference-application-circuit)) |
| `output_drive` | Open drain cannot drive high: an LED to ground needs totem pole |
| EXTCLK | With `ClockSource::External` and no clock on the pin, the counter does not run |

## Every output lit after a supply dip

A reset sets every output low and not inverted. A load wired active low (an LED from the supply, a P-type driver)
is then on, until `EnsureInitialized()` writes INVRT again; OE high does not help, OUTNE resets to 00 (low). Rewire
the load active high where it must stay off by itself ([Hardware](hardware.md#reference-application-circuit)).

## Servo positions are off by a few percent

The internal oscillator is 25 MHz typical, not exact, and the pulse widths scale with it. Measure the PWM period on
one output, set `Config::oscillator_hz` to `4096 x (PRE_SCALE + 1) / period`, and call `ApplyConfig()`.

## The PCA9685 runs hot

LEDs with integrated Zener diodes on totem-pole outputs make a low-resistance path to ground: use
`OutputDrive::OpenDrain` (section 10, question 5). Check the 25 mA per output and 400 mA total limits too.
