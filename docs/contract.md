# Contract

What the PCA9685 driver promises, what it needs, and the rules a caller follows.

The driver inherits the [Kokeb Labs driver contract](https://github.com/kokeb-labs/.github/blob/main/standards/driver.md);
this page states only what is specific to it. A change to anything here is a breaking change, listed under
**Breaking** in the [changelog](changelog.md).

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From NXP PCA9685 Rev. 4:

    - LED All Call and Sub Call addresses: 7.1.2 LED All Call I2C-bus address; 7.1.3 LED Sub Call I2C-bus addresses, [p. 8](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=8)
    - Register summary and auto-increment: Table 4 Register summary, [p. 10](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=10)
    - MODE1, sleep, EXTCLK and the oscillator start-up: 7.3.1 Mode register 1, MODE1; Table 5, [p. 14](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=14)
    - Restart mode: 7.3.1.1 Restart mode, [p. 15](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=15)
    - MODE2, output drive, inversion, change on STOP, OUTNE: 7.3.2 Mode register 2, MODE2; Table 6, [p. 16](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=16)
    - ALL_LED registers and the orderly shutdown: 7.3.4 ALL_LED_ON and ALL_LED_OFF control; Table 8, [p. 25](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=25)
    - SUBADR1, the reset sentinel: 7.3.6 SUBADR1 to SUBADR3; Table 9, [p. 26](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=26)
    - Output enable, OE: 7.4 Active LOW output enable input; Table 11, [p. 27](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=27)
    - Power-on reset: 7.5 Power-on reset, [p. 27](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=27)
    - Software reset by the general call: 7.6 Software reset, [p. 28](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=28)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Promises

- The constructor is `noexcept`, stores the configuration and the address, does no I/O and cannot fail.
- `EnsureInitialized()` is the only way to start the driver. Its first call checks the configuration and the
  address (`InvalidArgument`, nothing sent), brings the bus up, **switches every output off first** (OE high,
  ALL_LED_OFF_H, MODE2 with OCH = 0, ALL_LED_OFF_H again, each sent even if one before it failed), then writes
  MODE1, PRE_SCALE and the SUBADR1 sentinel, reads MODE1, MODE2, SUBADR1, PRE_SCALE and all 16 channels back (the
  identity check), wakes the oscillator, waits 500 µs, checks the wake and enables OE. On any failure the driver
  stays uninitialised and the next call starts again from the beginning. Once started, a later call is not a
  shortcut: it runs the reset check (one 3-byte read), returns `Ok` when the chip kept its state, and starts again
  from the beginning when it reset.
- Every call that touches the chip returns `Error::NotInitialized` before the first `EnsureInitialized()` succeeds,
  except `Deinitialize()` (below) and `DisableOutputs()` (a pin write). No call initialises the chip as a side
  effect. `ApplyConfig()` before the start only stores the configuration.
- Every port failure propagates, as `Bus`, or as `NoDevice` when the port's `GetLastFailure()` reports an address
  NACK; no getter returns a stale or zero value after a failed read.
- A reply a floating, stuck or shared line can produce is never data: a reserved bit set (MODE2 bits 7 to 5,
  SUBADR1 bit 0, bits 7 to 5 of an `_H` channel byte) is `InvalidReply`; MODE1 with AI clear is reset evidence.
  Equal ON and OFF counts with no full bit are a value (the output is never asserted, figure 9): a switch-on NACKed
  part-way can leave them, so one such channel does not fail a read of all 16. A channel read of all zeros, which
  SDA stuck low also gives, counts only once a second frame agrees: one more MODE1/MODE2/SUBADR1 read must match
  the running chip, or the read is `InvalidReply` and leaves every channel in doubt as it was.
- The only wait is the oscillator start-up, `kOscillatorStartUs` (500 µs, the data sheet maximum) after every wake
  from the internal oscillator. No call polls.
- `Config{}` equals the reset values of MODE1, MODE2 and PRE_SCALE (except that the driver clears ALLCALL and sets
  AI, and SUBADR1 holds the sentinel).
- A missing `DelayUs()` is a compile error; a missing `GpioSet()` gives `Unsupported` from `DisableOutputs()` and
  `EnableOutputs()`; a missing `GetLastFailure()` reports every failure as `Bus`; a missing `Log()` logs nothing. A
  misdeclared hook is a compile error.
- `Deinitialize()` always does its work, whatever the driver believes (never started, started, or a reset found),
  on an address `EnsureInitialized()` accepts; any other is `InvalidArgument` with no I/O (0x70 is LED All Call at
  power-up, where the writes would reach every PCA9685). It drives OE high first, which needs no bus, then brings
  the bus up and sends ALL_LED_OFF_H, then, only when that switch-off was sent (asleep, an output still on
  would be held on), a wake first when an output may be held and MODE1.SLEEP. It returns `Ok` only when all of that
  was sent; the driver is uninitialised afterwards either way.

## Switch-offs

- **One data byte.** Every switch-off is sent as a single data byte: `0x10` to LEDn_OFF_H (register `0x06 + 4n +
  3`), or `0x10` to ALL_LED_OFF_H (figure 24). One byte is stored or it is not, and full OFF takes precedence over
  everything else the channel holds (table 7 remark). A 4-byte burst NACKed part-way would leave new ON bytes with
  the old OFF bytes, applied at the STOP (section 7.3.2, note 2): a phase-shifted channel could end up nearly 100 %
  on. So no switch-off ever makes a channel more active, even when it fails.
- **Never refused.** A channel call with an off value (`full_off` set, `SetDuty(n, 0)`, `SetPulseWidthUs(n, 0)`,
  `SetChannelOff()`), `SetChannelsOff()`, `SetAllOff()`, `DisableOutputs()` and `Sleep(SleepMode::OutputsOff)`'s
  first write are sent whatever the driver doubts: an unknown channel, an unknown prescaler, a pending wake check.
  Once the driver has started, they are sent also while it is uninitialised after a reset was found or
  `Deinitialize()`. An off request is never `InvalidArgument` (only a channel number above 15 is).
- **Every one, first.** A bank call sends each off value as its single byte before any burst; it still sends them
  when it refuses its switch-ons (an invalid value, a failing wake check). `SetChannelsOff()` keeps going after a
  failed write.
- **Ok means off.** `Ok` from a switch-off means the channel's output is off on the chip. While the chip may be
  asleep holding its outputs (after `Sleep(SleepMode::HoldOutputs)`, or a wake that was not confirmed), the write is
  sent and the call returns `Asleep`: asleep, no output can be switched on or off (table 5, note 4), so
  `DisableOutputs()` (OE high, at once) or `Wake()` completes it.
- **No shortcut.** No call skips a write because the driver's copy says it is not needed.

## Writes that may or may not have arrived

- A switch-off byte that failed left the channel as it was, or off: the copy keeps the riskier "as it was"; nothing
  becomes unknown.
- A switch-on or a bank burst whose transfer failed with `Bus` may have reached the chip, in whole or in part: with
  OCH = 0 the bytes acknowledged before a NACK reach the outputs at the STOP (section 7.3.2). The channels it covered
  become unknown (`GetUncertainChannels()`); `GetChannel()` returns `StateUnknown` for them and `EnableOutputs()`
  refuses while any is unknown. A successful write, a switch-off, `ReadChannel()` or `ReadChannels()` makes them
  known again.
- A failed PRE_SCALE write makes the prescaler unknown; `SetPulseWidthUs()` refuses (`StateUnknown`, nothing sent)
  until a successful `SetPwmFrequency()`, `ReadPrescale()` or wake check.
- PRE_SCALE ignores writes while awake (table 4, note 1), which the chip still acknowledges: `SetPwmFrequency()`
  always reads it back and returns `WriteMismatch` when it did not take.
- `NoDevice` (an address NACK) proves nothing arrived: nothing becomes unknown.
- The driver keeps MODE2.OCH = 0. With OCH = 1 a channel changes only when "all 4" of its registers are loaded
  (section 7.3.2, note 3), and which bytes count after a NACK is not specified.

## Resets

- A power-on reset (VDD below V_POR) or a software reset (the general call `0x06`) loads every reset value:
  MODE1 = 0x11, asleep with AI clear; every channel full OFF; MODE2 = 0x04; SUBADR1 = 0xE2; PRE_SCALE = 0x1E;
  EXTCLK clear (sections 7.5, 7.6).
- **The sentinels.** The driver sets MODE1.AI and never clears it, and writes MODE2, EXTCLK and SUBADR1 only at
  start-up (SUBADR1 = `0x5A`, inert while MODE1.SUB1 = 0, table 9). Every reset check reads MODE1, MODE2 and SUBADR1
  in one 3-byte burst; AI = 0, or MODE2, EXTCLK or SUBADR1 different from the copy, is reset evidence. SUBADR1 is the
  one no MODE1 write of the driver can mask, so a reset is found even when MODE2 and PRE_SCALE equal their reset
  values (`Config{}`).
- **Two frames decide.** Reset evidence is confirmed by a second read before the driver acts (handbook rule 12d);
  a single corrupted read is an error (`InvalidReply`) or a glitch the second read clears, never a reset.
- **Acting on it.** The call drives OE high, writes the single-byte ALL_LED_OFF_H, marks the driver uninitialised
  and returns `DeviceReset`. The switch-off calls and `Deinitialize()` keep working from there.
- **Read before a cached write.** `Sleep()`, `Wake()` and `SetPwmFrequency()` read MODE1, MODE2 and SUBADR1 before
  they write MODE1 from the copy; `CheckForReset()`, `EnsureInitialized()` on a started driver and `EnableOutputs()`
  read them too; `Verify::ReadBack`, `ReadChannel()`, `ReadChannels()` and `ReadPrescale()` read them first (a burst
  read needs AI).
- **A reset the driver has not seen** leaves the chip asleep, and asleep no output can switch on. Channel calls
  keep returning `Ok` then (the chip acknowledges their writes; with AI clear they land in one register), so call
  `CheckForReset()` where a supply dip is possible.
- **Never run on reset values.** The driver wakes the chip only by a MODE1 write with SLEEP = 0 (the wake, and the
  RESTART write, which clears SLEEP too), and every such write is followed by the wake check: MODE1, MODE2, SUBADR1
  and PRE_SCALE read and compared with the copy. A check that fails stays pending: the next call runs it first, and
  a switch-on is refused until it passes.
- **Two frames for a state change.** Only an acknowledged wake write followed by a check that reads SLEEP = 0 clears
  the doubt that an output may be held; a read alone never does.
- **Active-low loads.** After a reset every output is low and not inverted. A load wired active high is off; one
  wired active low is **on** until `EnsureInitialized()` runs again ([Hardware](hardware.md#reference-application-circuit)).

## Depends on

| Dependency | Version | Why |
| --- | --- | --- |
| A C++20 compiler | GCC 10, Clang 13 or later | Concepts constrain the port |
| CMake | 3.16 or later | Generates `pca9685_version.h` |
| Your port | | The bus, within the bus requirements; the delay; optionally OE, at its inactive level (high) before the first transfer, the failure reason and logging ([Porting](porting.md)) |

Nothing else: no RTOS, no SDK, no other repository of the org.

## Threading

The driver is not thread-safe and takes no lock. Use each driver object from one task at a time, or guard every
call on it with one mutex; the layer that owns the driver owns that lock, and the bus lock when several chips
share a bus. Nothing may run in an ISR: every call can block for a bus transfer or a delay. Separate objects
share no state. Another master on the bus must not write to the chip: the driver's copy would not know.

## Errors

`pca9685::Error` inside `pca9685::Result<T>`. Values 0 to 9 are the shared base set; this driver returns
`NotInitialized`, `InvalidArgument`, `Bus`, `DeviceIdMismatch` and `Unsupported` from it, plus seven chip-specific
values.

| `Error` | Value | Returned when |
| --- | --- | --- |
| `Ok` | 0 | The call succeeded |
| `NotInitialized` | 1 | `EnsureInitialized()` has not succeeded, or `Deinitialize()` ran or a reset was found since (switch-offs still go out once started) |
| `InvalidArgument` | 2 | A `Config` field, the address or an argument is out of range; nothing was sent (a bank call still sends its switch-offs), also by `Deinitialize()` |
| `Bus` | 3 | A port hook returned `false`: a data NACK, a bus error, an OE pin write |
| `Timeout`, `Crc`, `Busy`, `DeviceFault` | 4, 6, 7, 8 | Never returned |
| `DeviceIdMismatch` | 5 | At start-up, a register did not read back what was written: no PCA9685, or another part |
| `Unsupported` | 9 | `DisableOutputs()` or `EnableOutputs()` without `GpioSet()` |
| `WriteMismatch` | 16 | A read-back differs: `Verify::ReadBack`, PRE_SCALE in `SetPwmFrequency()`, SLEEP still set after `Wake()` |
| `NoDevice` | 17 | An address NACK, with `GetLastFailure()` |
| `DeviceReset` | 18 | The chip reset since the start, confirmed by two reads; OE high, every channel off, the driver uninitialised |
| `InvalidReply` | 19 | A read returned what no PCA9685 can send (a reserved bit set, or an inconsistent MODE1/MODE2/SUBADR1 frame), or a channel read of all zeros the MODE1/MODE2/SUBADR1 read did not confirm |
| `Asleep` | 20 | Written, but the chip is or may be asleep: the outputs do not follow until `Wake()` |
| `StateUnknown` | 21 | The call depends on a value a failed write left unknown; nothing was sent |
| `ExternalClockLatched` | 22 | MODE1.EXTCLK is set but the internal clock is asked for |

The [API guide](api-guide.md#handle-errors) says what to do about each.

## Memory

No heap, no exceptions, no RTTI, no `std::function`, no `std::string`, no `printf` and no static mutable state. A
driver object holds a reference to the port, the address, a `Config`, the copy of the 16 channels and a few flags:
144 bytes on a 64-bit host. The largest stack buffers are one burst of all 16 channels: 64 bytes, and 65 in the
register helper's frame.

## Platform

Platform-free: the same code runs on any target with a C++20 compiler, including the host tests. The ESP-IDF port
in [`examples/esp32`](examples.md) is an example, not part of the driver.

## Deviations from the standard

None.
