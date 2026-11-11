# Porting

Run the driver on a new MCU by writing one class: the port.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From NXP PCA9685 Rev. 4:

    - Slave address, A0 to A5: 7.1.1 Regular I2C-bus slave address; Figure 4, [p. 7](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=7)
    - Output enable, OE: 7.4 Active LOW output enable input; Table 11, [p. 27](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=27)
    - I2C bus transactions: 9 Bus transactions; Figures 20 to 24, [p. 32](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=32)
    - I2C timing, EXTCLK and OE propagation: 13 Dynamic characteristics; Table 15, [p. 40](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf#page=40)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

The driver never calls your SDK. It calls hooks on a class you derive from `pca9685::I2cInterface<Self>` (in
[`inc/pca9685_i2c_interface.hpp`](../inc/pca9685_i2c_interface.hpp)). The base uses CRTP and declares no hook
itself; the driver template is constrained by the `pca9685::I2cPort` concept, so calls are resolved at compile
time (no virtual functions, nothing to allocate) and a missing or misdeclared hook is a compile error at the point
of use.

## What you implement

| Method | Required | Contract |
| --- | --- | --- |
| `bool EnsureInitialized() noexcept` | yes | Bring the bus up on the first call; return `true` at once afterwards. `false` makes the driver's `EnsureInitialized()` return `Bus` |
| `bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept` | yes | START, `addr` + W, `len` bytes, STOP. `false` on a NACK or any bus error; send the STOP after a NACK too |
| `bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx, std::size_t rx_len) noexcept` | yes | START, `addr` + W, `tx`, repeated START, `addr` + R, `rx_len` bytes (NACK the last), STOP |
| `void DelayUs(uint32_t us) noexcept` | yes | Wait at least `us` microseconds: the 500 µs oscillator start-up |
| `bool GpioSet(CtrlPin, GpioSignal) noexcept` | no | Drive OE; `false` if the pin write failed |
| `bool GetLastFailure(I2cFailure& failure) noexcept` | no | After `Write()` or `WriteRead()` returned `false`: set `failure` and return `true` when the controller knows why |
| `void Log(LogLevel, const char* message) noexcept` | no | Print one line; `message` is plain text, never a format string |

`addr` is the 7-bit address the driver was constructed with; shift it yourself if your SDK wants the 8-bit form.

## The port

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

Rules the transfers must follow:

- **One call, one transaction.** The register address is the first byte of `Write()` and the `tx` of
  `WriteRead()`; never split a burst. The driver writes up to 65 bytes (all 16 channels) and reads up to 64 in one
  transaction, relying on auto-increment, which does not carry over a STOP.
- **A STOP after a NACK.** With MODE2.OCH = 0 the chip applies the bytes it acknowledged at the STOP; the driver
  counts on the STOP to know what may have reached the outputs.
- **Repeated START for `WriteRead()`**, no STOP between the write and the read (figure 22).
- **Raw bytes only.** Do not retry: a retried write can repeat a change the driver already counted as sent, and a
  retried read hides a fault. The driver owns the framing.
- **Bounded time-outs.** A stuck bus fails the call; it does not block forever. The chip resets its own bus
  interface after SDA or SCL is held low for 25 ms (table 15, note 1).
- **A bus clear before a retried start-up.** After a bus error, clock SCL nine times with SDA released (or use your
  SDK's bus recovery) before the application calls `EnsureInitialized()` again (handbook KB-HW-019).

## Bus requirements

The driver states what the PCA9685 needs from the bus as `constexpr` values in
[`inc/pca9685_i2c_interface.hpp`](../inc/pca9685_i2c_interface.hpp) (handbook KB-CPP-123). Check your port's
settings against them with `static_assert`, as both example ports do:

| Constant | Value | Meaning | Data sheet |
| --- | --- | --- | --- |
| `kI2cAddress` | `0x40` | 7-bit address with A5 to A0 low; the pins add 0 to 63 (0x40 to 0x7F, not 0x70) | 7.1.1, figure 4 |
| `kMaxClockHz` | `1'000'000` | Standard, Fast or Fast-mode Plus | 13, table 15 (f_SCL) |
| `kRepeatedStart` | `true` | A register read is one write-read with a repeated START | 9, figure 22 |

```cpp
inline constexpr uint32_t kMyClockHz = 400'000;
static_assert(kMyClockHz <= pca9685::kMaxClockHz, "SCL above the PCA9685's maximum");
static_assert(pca9685::kRepeatedStart, "WriteRead() must use a repeated START");
```

**NACKs.** The chip NACKs reserved register addresses (0x46 to 0xF9) and the general call with a byte other than
`0x06`; the driver sends neither. The NACK the master sends on the last byte it reads ends the read; it is not a
failure. A NACK the port reports is therefore always a fault: no chip at the address (`NoDevice` with
`GetLastFailure()`, `Bus` without it) or a data byte refused (`Bus`). The driver never probes with an empty or
zero-length transfer: every transfer carries the register address (handbook KB-CPP-130).

## Control pins

Each control pin has a defined idle level, and the port drives it there before it enables the pin's output and
before the driver's first transfer (handbook KB-HW-018). Configuring a pin as an output first and setting its level
afterwards gives the chip a pulse it did not ask for: here, a moment of enabled outputs.

| `CtrlPin` | Package pin | Polarity | Idle (`INACTIVE`) level | Before the first transfer |
| --- | --- | --- | --- | --- |
| `OutputEnable` | OE (TSSOP28 23, HVQFN28 20) | active low | high: every output at the off level | Set high, then enable the output |

`GpioSignal::ACTIVE` always means "function asserted", here "outputs enabled", which is a low level on the pin.
The driver drives OE `INACTIVE` as its first step of `EnsureInitialized()` and `ACTIVE` as its last, and again in
`DisableOutputs()` and `EnableOutputs()`. A board with OE tied to VSS needs no hook. The address pins and EXTCLK
are strapped on the board, not driven by the port.

## Optional hooks

The driver finds the optional hooks at compile time with C++20 `requires`, in one place
(`pca9685::detail`). A missing hook falls back in a documented, safe way or fails to compile; it never skips a
step silently.

| Hook | Used for | Without it |
| --- | --- | --- |
| `DelayUs()` | The 500 µs oscillator start-up after every wake | Compile error at the first call that waits |
| `GpioSet()` | OE: `EnsureInitialized()`, `DisableOutputs()`, `EnableOutputs()`, `Deinitialize()` | OE must be tied low; the two calls return `Error::Unsupported` |
| `GetLastFailure()` | Telling an address NACK (`NoDevice`, nothing arrived) from a data NACK (`Bus`, part of the write may have arrived) | Every failure is `Bus`, and the driver treats the write as possibly delivered |
| `Log()` | One line per lifecycle event or failure | Nothing is logged |

A port with every hook:

```cpp
--8<-- "tests/docs/ports.cpp:full-port"
```

```cpp
--8<-- "tests/docs/ports.cpp:output-enable"
```

A `GpioSet()` that returns `void` is a compile error: the driver must see a failed pin write. The
[compile-fail tests](testing.md) check this and the other hook rules.

## Reference implementations

Every port lives with its example, in `examples/<platform>/`; the driver has none. [Platforms](platforms.md) lists
them and how to add one.

- [`examples/host/host_pca9685_port.hpp`](../examples/host/host_pca9685_port.hpp): any PC, against a simulated
  chip. Start from it for a host build or a test bench; it is also the smallest complete port.
- [`examples/esp32/main/esp32_pca9685_port.hpp`](../examples/esp32/main/esp32_pca9685_port.hpp): ESP-IDF, with
  every hook. Start from it for any ESP-IDF target.

## Check your port

1. Build it: the ports on this page are compiled by the host CI from [`tests/docs/`](../tests/docs/my_port.hpp),
   with the driver instantiated over them.
2. Call `EnsureInitialized()`: `Ok` proves that writes, bursts and repeated-START reads work, since the start-up
   reads back every register it wrote.
3. Call `SetDuty(0, 2048)` and measure LED0: a square wave at 197 Hz typical (`Config{}`), 50 % duty.
4. With OE on a GPIO: `DisableOutputs()` takes LED0 to the off level at once; `EnableOutputs()` brings it back.

## Threads

The driver is not thread-safe. Give each driver object one owner task, or guard every call with one mutex. When
several chips share a bus, the port must also serialise the bus. See [Contract](contract.md#threading).

## Editor setup

The clang-format and clang-tidy configuration is in `_config/`, and the repository root has no copy of it. Tools
that take a path use it directly; CI does the same:

| Tool | Setting |
| --- | --- |
| Command line | `clang-format --style=file:_config/.clang-format`, `clang-tidy --config-file=_config/.clang-tidy -p build` |
| VS Code (C/C++ extension) | `"C_Cpp.clang_format_style": "file:${workspaceFolder}/_config/.clang-format"` and `"C_Cpp.codeAnalysis.clangTidy.args": ["--config-file=${workspaceFolder}/_config/.clang-tidy"]` |
| clangd (VS Code clangd, Neovim, Emacs), CLion | These find `.clang-format` and `.clang-tidy` only by name, from the file upwards: copy both to the root once (`cp _config/.clang-format _config/.clang-tidy .`). `.gitignore` lists the copies, so they never reach a commit; copy again after `_config/` changes |

Configure with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (clangd: `--compile-commands-dir=build`) so the editor sees the
same flags as the build.
