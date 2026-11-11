# API guide

The driver's calls, task by task. Every example on this page is compiled by the host build from
[`tests/docs/api_tasks.cpp`](../tests/docs/api_tasks.cpp).

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From NXP PCA9685 Rev. 4:

    - MODE1, sleep, EXTCLK and the oscillator start-up: 7.3.1 Mode register 1, MODE1; Table 5, [p. 14](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=14)
    - Restart mode: 7.3.1.1 Restart mode, [p. 15](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=15)
    - LED output and PWM control, full ON and full OFF: 7.3.3 LED output and PWM control; Table 7; Figure 9, [p. 16](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=16)
    - ALL_LED registers and the orderly shutdown: 7.3.4 ALL_LED_ON and ALL_LED_OFF control; Table 8, [p. 25](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=25)
    - PWM frequency, PRE_SCALE: 7.3.5 PWM frequency PRE_SCALE; Equation 1, [p. 25](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=25)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## The shape of the API

- One object per chip: `pca9685::PCA9685<Port> pwm(port, address, config)`. The constructor does no I/O.
- Every call returns `Result<T>`: check `ok()` (or the `bool` conversion) before reading `value`; on failure
  `error` says why and `value` is `T{}`.
- Channels are 0 to 15 (LED0 to LED15). A channel's value is a `Pwm`: the ON and OFF counts of the 12-bit counter
  (0 to 4095) and the full ON and full OFF bits. `PwmOff()` is the reset value; `PwmForDuty(duty, phase)` builds
  one from a duty in 4096ths.
- Calls with a trailing `Verify verify = Verify::None` can read back what they wrote: `Verify::ReadBack` costs two
  more reads (MODE1 and MODE2, then the channel) and returns `WriteMismatch` when the chip holds something else.
- Calls without I/O are `Get...()` and `MayBeAsleep()`; everything else may touch the bus.

## Initialise

```cpp
--8<-- "tests/docs/api_tasks.cpp:lifecycle"
```

The first call switches every output off, writes and reads back the configuration and wakes the oscillator:
thirteen transfers and a 500 µs wait. Once started, a call runs the reset check (one read) and starts again only
when the chip reset; call it again after `DeviceReset`.

## Set a channel

| Call | Writes | Notes |
| --- | --- | --- |
| `SetDuty(ch, duty, phase)` | on at `phase`, off `duty` counts later | 0 is off, `kFullDuty` (4096) full ON; wraps into the next period (section 7.3.3, example 2) |
| `SetPulseWidthUs(ch, us)` | on at 0, off after `us` | Uses the programmed PRE_SCALE; returns the programmed width in ns |
| `SetFullOn(ch)` | LEDn_ON_H bit 4 | |
| `SetChannelOff(ch)` | LEDn_OFF_H = 0x10, one data byte | Never refused; works also after a reset was found |
| `SetPwm(ch, value)` | the four registers of `value`; an off value as one byte | Equal ON and OFF counts without a full bit are refused (section 7.3.4) |

A switch-on is one 4-byte transfer from LEDn_ON_L, with auto-increment; the four bytes reach the output together
at the STOP. A switch-off is the one byte LEDn_OFF_H = 0x10: stored or not, never part-way, and full OFF takes
precedence over the rest, so a failed switch-off never makes a channel more active. Phases spread the switching edges of
many channels and the supply current with them.

### Servos

```cpp
--8<-- "tests/docs/api_tasks.cpp:servo"
```

With `ServoPreset()` (50 Hz, PRE_SCALE 121) one count is 4.88 µs, so 1.5 ms is 307 counts; the call returns
1 498 160 ns, what was programmed. A width that rounds to 0 counts or to more than 4095 is `InvalidArgument`, never
clamped. The internal oscillator is 25 MHz typical: calibrate `Config::oscillator_hz` for accurate pulses
([Hardware](hardware.md#extclk)).

## Set many channels at once

```cpp
--8<-- "tests/docs/api_tasks.cpp:bank"
```

`SetChannels(first, values)` sends its off values first, one byte each, then writes up to 16 consecutive channels
in one transfer, so they change together at its STOP. `SetChannelsOff(mask)` switches off every channel of a 16-bit
mask, one transfer each, and keeps going after
a failure. `SetAllOff()` writes ALL_LED_OFF_H = 0x10: every channel off with one data byte (figure 24).

## Change the frequency

`SetPwmFrequency(hz)` runs the data sheet's sequence: the reset check, SLEEP (the outputs hold for that moment),
PRE_SCALE, a read-back of PRE_SCALE, the wake with its 500 µs wait, the wake check and RESTART when channels were
running (sections 7.3.1.1, 7.3.5). The counts of each channel stay as they are, so pulse widths scale with the
period: write them again. `GetPwmFrequencyHz()` returns the frequency of the programmed PRE_SCALE, which equation 1
rounds: 1000 Hz programs 1017 Hz (PRE_SCALE 5).

## Sleep and wake

| Call | What happens |
| --- | --- |
| `Sleep()` or `Sleep(SleepMode::OutputsOff)` | Every channel off (ALL_LED), then MODE1.SLEEP: the safe low-power state. `Wake()` leaves every channel off. |
| `Sleep(SleepMode::HoldOutputs)` | MODE1.SLEEP with the channels as they are; RESTART sets. While asleep an output may hold its level, so a switch-off then returns `Asleep`. |
| `Wake()` | The reset check, SLEEP = 0, 500 µs, the wake check, then RESTART = 1 when it reads 1: the channels that ran before run again. |

Asleep, channel writes reach the registers, not the outputs (table 5, note 4); the calls return `Asleep`. Use
`DisableOutputs()` for an immediate switch-off while asleep.

## Switch everything off now

```cpp
--8<-- "tests/docs/api_tasks.cpp:emergency"
```

`DisableOutputs()` drives OE high: every output goes to the off level within 60 ns, with no bus traffic, in any
state. `SetAllOff()` then switches the channels off, so `EnableOutputs()` later does not turn them back on.
`EnableOutputs()` is refused (`StateUnknown`) while any channel is unknown after a failed write.

## Find a reset

```cpp
--8<-- "tests/docs/api_tasks.cpp:reset"
```

`CheckForReset()` is one 3-byte read of MODE1, MODE2 and SUBADR1 (about 90 µs at 400 kHz); reset evidence is
confirmed by a second read before the driver acts on it. A chip that reset is asleep with
every channel off, and channel writes cannot tell; call it once per control period where the supply can dip.
[Contract](contract.md#resets) has the rule.

## Handle errors

```cpp
--8<-- "tests/docs/api_tasks.cpp:errors"
```

| `Error` | What happened | What to do |
| --- | --- | --- |
| `NotInitialized` | Not started, or a reset was found | `EnsureInitialized()` |
| `InvalidArgument` | An argument or a `Config` field is out of range; nothing sent | Fix the value |
| `Bus` | A NACK or a bus error; the write may have arrived | Retry the call; the channels it covered are unknown until then |
| `NoDevice` | Nothing acknowledged the address | Check the wiring and the address |
| `DeviceIdMismatch` | Something answered that is not a PCA9685 | Check the address and the part |
| `DeviceReset` | The chip reset (supply dip, software reset) | `EnsureInitialized()`, then write the channels again |
| `InvalidReply` | A read the chip cannot send: a stuck or floating line | Check SDA, the pull-ups, other parts on the bus |
| `Asleep` | Written; the outputs follow after `Wake()` | `Wake()`, or `DisableOutputs()` for an immediate off |
| `StateUnknown` | A failed write left a value unknown; nothing sent | `ReadChannels()` or `ReadPrescale()`, or write it again |
| `WriteMismatch` | The chip kept something else than was written | Retry; check for noise |
| `ExternalClockLatched` | EXTCLK is set on the chip | Power-cycle the chip, or configure the external clock |
| `Unsupported` | The port has no `GpioSet()` for OE | Add the hook, or do without OE |

## Diagnostics

| Call | I/O | Returns |
| --- | --- | --- |
| `GetChannel(ch)` | none | The copy of the channel; `StateUnknown` after a failed write |
| `GetUncertainChannels()` | none | The channels whose last write failed |
| `ReadChannel(ch)`, `ReadChannels()` | reset check, then one read | What the chip holds, into the copy |
| `GetPrescale()`, `GetPwmFrequencyHz()` | none | The programmed prescaler and frequency |
| `ReadPrescale()` | reset check, then one read | PRE_SCALE as the chip holds it |
| `ReadRegister(address)` | one read, no check | Any register 0x00 to 0x45 or PRE_SCALE |
| `GetLastMode1()`, `GetLastMode2()` | none | MODE1 and MODE2 as last read |
| `MayBeAsleep()` | none | Whether the chip is or may be asleep |

## Shut down

```cpp
--8<-- "tests/docs/api_tasks.cpp:deinit"
```

`Deinitialize()` drives OE high, switches every channel off and puts the chip to sleep (2.2 µA typical), then
marks the driver uninitialised, even when a write failed.
