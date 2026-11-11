# Testing and CI

How the driver is checked: host tests against a model of the chip, a fuzzer that corrupts what the chip sends
back, compiled documentation, an install test, ESP32 and Cortex-M builds, static analysis and measured quality
gates, on every pull request.

## Host tests

The tests build the real driver against a register model of the PCA9685 written from the data sheet, which
changes state on its own as the chip does: a power-on reset or a general-call software reset at any moment,
auto-increment only with MODE1.AI, PRE_SCALE that ignores writes while awake, a sticky EXTCLK, RESTART that sets
when the chip sleeps with a live channel and restarts the channels only 500 µs after the wake, written bytes that
reach the outputs at the STOP (or per ACK with OCH = 1), the outputs through full OFF, full ON, INVRT, OUTDRV and OE
with OUTNE, and the LED All Call and Sub Call addresses. The mock bus injects every fault a real bus has: an address
NACK, a data NACK part-way (the bytes before it stored), a lost ACK, an acknowledged write that is dropped or
corrupted, SDA stuck low or reading all ones, a reset just before any transfer. They build with the firmware's
flags, `-fno-exceptions -fno-rtti`, and warnings as errors, on GCC and Clang.

```bash
cmake -S . -B build -DPCA9685_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

| File | Covers |
| --- | --- |
| [`tests/test_pca9685.cpp`](../tests/test_pca9685.cpp) | `Deinitialize()` with OE high before the bus and no I/O on a refused address; the start-up's exact frames and waits, from a fresh, running, held or OCH = 1 chip; every channel call, banks and masks; the frequency sequence; sleep, wake and restart under each reading; OE; diagnostics; `NotInitialized` before the start |
| [`tests/test_reset.cpp`](../tests/test_reset.cpp) | Power-on and software resets found by every path, with `Config{}` too (the SUBADR1 sentinel); a reset before every transfer of a session; one corrupted read never taken for a reset; switch-offs and `Deinitialize()` after a found reset; nothing written over a reset chip; a reset before a wake or a RESTART write caught by the wake check; pending checks; the group addresses |
| [`tests/test_failed_writes.cpp`](../tests/test_failed_writes.cpp) | A NACK at every byte of every switch-off never makes a phase-shifted channel more active; NACK part-way, lost ACKs and address NACKs; unknown channels and prescaler; every switch-off sent under every doubt; bank calls that refuse switch-ons and still switch off; two frames for a state change |
| [`tests/test_bus_faults.cpp`](../tests/test_bus_faults.cpp) | Stuck and floating lines at start-up and after; SDA stuck low from a channel read on never clears a channel's doubt; `InvalidReply`; `NoDevice` against `Bus`; no empty transfer |
| [`tests/test_errors.cpp`](../tests/test_errors.cpp) | The base `Error` values are 0 to 9; every chip-specific value (16 and up) is returned by some call; `Validate()` and the arithmetic |
| [`tests/test_model.cpp`](../tests/test_model.cpp) | The model against the data sheet, and its register map against the driver's with `static_assert` |
| [`tests/fuzz/`](../tests/fuzz/fuzz_pca9685.cpp) | The fuzz target; CTest replays its corpus (`pca9685_fuzz_pca9685_corpus`), see below |
| [`tests/test_version.cpp`](../tests/test_version.cpp) | The version getters follow the generated header (also against a 9.8.7 probe) |
| [`tests/test_kconfig.cpp`](../tests/test_kconfig.cpp) | `KconfigDefaults()` with and without `CONFIG_` values |
| [`tests/compile_fail/`](../tests/compile_fail/port_without_delay.cpp) | Code that must not compile, each for its `EXPECT:` diagnostic: a port without `DelayUs()`, a `void GpioSet()`, a `void GetLastFailure()`, a `Log()` without `noexcept`, a `DelayUs(uint16_t)`, a hook with a narrowed parameter or without `noexcept`, an invalid `CheckedConfig`, a class that is not a port |
| [`tests/mock_pca9685.hpp`](../tests/mock_pca9685.hpp) | The register model, with its own register map; the bus and its faults are in `mock_pca9685_bus.hpp` |
| [`tests/check_version_copy.cmake`](../tests/check_version_copy.cmake) | `_config/Doxyfile` carries the CMake version |
| [`examples/host/`](../examples/host/README.md) | The host example, built with the test flags and run (`pca9685_host_example`) |
| [`tests/check_install.cmake`](../tests/check_install.cmake) | Installs into a scratch prefix and builds [`tests/install_consumer/`](../tests/install_consumer/CMakeLists.txt) with `find_package(pca9685)` |
| [`tests/check_doxygen.cmake`](../tests/check_doxygen.cmake) | Doxygen with `_config/Doxyfile` and warnings as errors, when `doxygen` is installed |

### The model stands alone

[`tests/mock_pca9685.hpp`](../tests/mock_pca9685.hpp) is written from the data sheet, not from the driver: it does
not include `pca9685_registers.hpp` or `pca9685_types.hpp`. Its register map, bits, reset values and timing are its
own (namespace `pca9685_test::model_reg`, each with the data sheet's table), so a wrong constant in the driver
cannot hide behind the same wrong constant in the model (handbook KB-TST-030).
[`tests/test_model.cpp`](../tests/test_model.cpp) compares the two at compile time:

```cpp
--8<-- "tests/test_model.cpp:cross-check"
```

When the build stops on one of these, one of the two maps is wrong: check it against the section the model names,
and fix that one. A new register goes into both, with its check. The model has a virtual clock (`Elapse()`, driven
by the mock port's `DelayUs()` and 10 µs per transfer), so the 500 µs oscillator start-up is real.

### Both readings of every open question

Where the data sheet leaves a question open, the model's `Readings` offers both answers and the safety tests run
under each:

| Question | Reading A (default) | Reading B |
| --- | --- | --- |
| Asleep, what does an output do? (table 5, note 4) | Keeps the level it had | Goes inactive |
| With OCH = 1, when does a channel change? (section 7.3.2, note 3) | Once all four of its registers were written | On every byte |
| After a wake without a RESTART write, do the channels run? (section 7.3.1.1) | Yes | They stay stopped until a RESTART write or a channel write |

## Fuzz tests

Everything the driver reads comes from the chip over the bus, so the data read is what a noisy cable, a wrong part
or a broken chip controls. [`tests/fuzz/fuzz_pca9685.cpp`](../tests/fuzz/fuzz_pca9685.cpp) lets libFuzzer choose a
configuration and an address, then up to 64 driver calls, and on every bus transfer what the bus does: pass it to
the model, NACK the address, NACK a data byte part-way, lose the ACK of a completed write, drop or corrupt an
acknowledged write, flip or replace the bytes of a read, or reset the chip just before it. Between calls it can
reset the chip (power-on or software reset), flip any of the three readings above, let another master write
through the LED All Call address, or take the bus down. After every call it checks the driver's promises and
aborts, naming the line, when one breaks:

- a call returns an `Error` the enum defines; a failed value is `T{}`;
- before `EnsureInitialized()` succeeds no call but `DisableOutputs()` makes a transfer, and `InvalidArgument`
  means nothing was sent (except a bank call's switch-offs);
- `DeviceReset` leaves the driver uninitialised;
- **a switch-off on a driver that has started once is always sent**, one write per channel asked for, also after
  a reset was found;
- **a switch-off, failed or not, never leaves a channel more active than before** (its live duty on the chip);
- an `Ok` `Deinitialize()` leaves every channel inactive and OE high;
- **an `Ok` switch-off leaves the channel inactive on the chip** (unless a write of the run was acknowledged and
  then dropped or corrupted, which I2C does not promise against);
- an `Ok` `DisableOutputs()` leaves OE high;
- **after an `Ok` call that ran the wake check, a running chip holds the driver's MODE2 and prescaler** (unless a
  read of the run was tampered with);
- every log line is a non-empty plain-text message.

Three corpus entries pin the design rules these checks found, each also a model test: a RESTART write clears SLEEP
too, so the wake check runs again after it (`reset-before-restart`); with OCH = 1 a switch-off could reach the output
part-written when the chip counts registers across transfers, so the driver keeps OCH = 0 (`och-ack-stale-bytes`);
and a single read never clears the doubt that an output is held (`held-after-lost-wake`).

```bash
# libFuzzer needs Clang (on macOS, Homebrew's LLVM: link its libc++, and add -Wl,-ld_classic if ld asks)
cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DPCA9685_BUILD_TESTS=ON -DPCA9685_BUILD_FUZZERS=ON
cmake --build build-fuzz --target fuzz_pca9685
./build-fuzz/tests/fuzz/fuzz_pca9685 -max_total_time=300 -max_len=512 tests/fuzz/corpus/fuzz_pca9685
```

The same file also builds without libFuzzer as `fuzz_replay_fuzz_pca9685`, and CTest replays every input in
[`tests/fuzz/corpus/fuzz_pca9685/`](../tests/fuzz/corpus/fuzz_pca9685) on GCC and Clang
(`pca9685_fuzz_pca9685_corpus`): named seeds, the inputs that broke a promise, and a set that covers every edge the
fuzzer reached, so a fixed bug stays fixed. CI fuzzes for 120 s on every pull request, from the seeds plus a corpus
it keeps between runs (job `Host quality / fuzz`).

## Quality gates

The `Host quality` job measures the library code, `inc/` and `src/` only (tests and examples are left out), and
fails when a value is over the limits of this repository's assurance level, `high` (set in
[`.kokeb/handbook.yml`](../.kokeb/handbook.yml), defined by the handbook's ADR-0020):

| Gate | Tool and how it is measured | Limit at `high` |
| --- | --- | --- |
| Complexity | lizard 1.24.1, modified cyclomatic complexity (`-m`: a `switch` counts once) per function | CCN at most 15 |
| Function length | lizard, lines of code per function | at most 60 |
| Parameters | lizard, per function | at most 6 |
| Line coverage | GCC 14 `--coverage -O0`, every CTest, gcovr 8.7 `--merge-lines` (a template's lines count once across its instances), `--exclude-unreachable-branches --exclude-throw-branches` | at least 85 % |
| Branch coverage | same run | at least 75 % |
| MC/DC | GCC `-fcondition-coverage`, same run | measured, not gated at `high` (100 % at `safety`) |
| Sanitizers | Clang, `-fsanitize=address,undefined -fno-sanitize-recover=all`, every CTest | no report |
| Fuzzing | libFuzzer with ASan and UBSan, 120 s per target | no broken promise |

The reports are in the run's `host-quality-reports` artifact and its step summary. To run the gates locally:

```bash
# Coverage (GCC 14 or later), then the complexity limits
cmake -S . -B build-cov -DCMAKE_CXX_COMPILER=g++-14 -DPCA9685_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="--coverage -O0 -fcondition-coverage"
cmake --build build-cov && ctest --test-dir build-cov
gcovr --root . --filter inc/ --filter src/ --merge-lines --exclude-unreachable-branches --txt build-cov
lizard -m -C 15 -L 60 -a 6 inc src

# Sanitizers (Clang)
cmake -S . -B build-san -DCMAKE_CXX_COMPILER=clang++ -DPCA9685_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -O1 -g"
cmake --build build-san && ctest --test-dir build-san --output-on-failure
```

The fuzz gate runs locally as shown in [Fuzz tests](#fuzz-tests).

clang-tidy 20 ([`_config/.clang-tidy`](../_config/.clang-tidy): bugprone, CERT, clang-analyzer,
cppcoreguidelines, modernize, performance, portability and readability, each disabled check with its reason) and
cppcheck (strict) run in the `C++ lint` and `Repository checks` jobs and fail on any finding.

The `MISRA` job ([`ci-misra.yml`](../.github/workflows/ci-misra.yml)) runs the shared `ru-misra` workflow of
ci-tools at level `high`: it traces a host build and the Cortex-M build (`pca9685_optimised_build`) of every
translation unit, checks them against MISRA C++:2023 and the handbook's C++20 profile (the CodeQL coding-standards
and profile packs, plus the validated clang-tidy and cppcheck checks), and reports each finding against the
handbook's compliance matrix and deviation records. A finding is fixed in the code; a deliberate exception names
its deviation on the line before it (`// codeql::misra_deviation_next_line(MD-CPP-009)` above the `.ipp` include
of `inc/pca9685.hpp`). Until the repository is public the job runs with `open_source_prerelease: true`, the
declaration that the code is released under the licence in `LICENSE` and is to be published.

## Documentation snippets

Every C++ block on this site is included from [`tests/docs/`](../tests/docs/my_port.hpp), which the host build
compiles (target `pca9685_doc_snippets`) with the driver instantiated over the documented port. A change to
the API that breaks a page breaks the build first. The README's example is a generated copy of the same file;
`kokeb-docs check` fails when it drifts.

## CI workflows

Everything runs from [`ci.yml`](../.github/workflows/ci.yml), which calls one workflow file per check and ends in
the `CI gate` job, the single required check (`CI gate / gate`).

| Job | What it checks |
| --- | --- |
| Host tests | GCC, then Clang (one job), build and run every test above, then build and run `examples/host` on its own |
| Host quality | The gates above: complexity, coverage, sanitizers, fuzzing |
| Cross-compile | `arm-none-eabi-g++` for Cortex-M0+, M4F and M7F with the strict flags, warnings as errors (nothing linked) |
| C++ lint | clang-format and clang-tidy over the sources |
| MISRA | MISRA C++:2023 and the C++20 profile on traced host and Cortex-M builds, at level `high` (see [Quality gates](#quality-gates)) |
| Docs | Strict site build (`_config/mkdocs.yml`), figure and generated-region drift, Doxygen (`_config/Doxyfile`), offline link check, Lighthouse |
| Repository checks | One job: DCO on pull requests, the org standard, Markdown and YAML lint, the [Kokeb Labs handbook](https://github.com/kokeb-labs/handbook) rules for `.kokeb/handbook.yml` (by rule ID), template drift (warns only) and cppcheck over the driver sources; a second job runs the handbook's source checks (include guards, bus requirements, raw access) |
| CI gate | Passes only if every job above passed: the one required check |

The **ESP32 examples** job builds every app in `examples/esp32/app_config.yml`, for each listed ESP-IDF version and
build type, with the repository root as the driver's component, and lints the example sources. The gate needs it
too.

The workflows call the shared, versioned workflows of
[kokeb-labs/ci-tools](https://github.com/kokeb-labs/ci-tools) at `@v1`.

## On hardware

Everything above runs against the model. These bench steps confirm what only the chip can, each **not verified on
hardware** until someone runs it and records the board and the result in the pull request's bench-status section:

1. **Start-up from any state.** Run the ESP32 example, then reset only the MCU while LED0 is lit: the next
   `EnsureInitialized()` switches it off before anything else. Repeat with the chip left asleep by
   `Sleep(SleepMode::HoldOutputs)`.
2. **Reset detection.** Pull VDD below 0.2 V for a moment while the outputs run: every output goes low (off for an
   active-high load), and the next `CheckForReset()` returns `DeviceReset`. Send the general call `0x00 0x06` from a
   second master: the same.
3. **Sleep and restart.** With LED0 at 50 %, `Sleep(SleepMode::HoldOutputs)`: record the level LED0 holds (the
   data sheet leaves it open; the driver assumes it may stay on). `Wake()`: LED0 runs again; capture SDA, SCL and
   LED0 and check the RESTART write comes at least 500 µs after the SLEEP = 0 write.
4. **Frequency and pulse width.** Measure the period on LED0 at `Config{}` (197 Hz expected from 25 MHz) and at
   `ServoPreset()` (50 Hz); compute the oscillator from it and check `SetPulseWidthUs(1, 1500)` against a scope
   after setting `Config::oscillator_hz` to the measured value.
5. **A NACK part-way.** With a logic analyser on SDA and SCL, interrupt SDA during a 4-byte channel write: the
   output after the STOP matches the bytes acknowledged, and the driver reports the channel unknown.
6. **OE.** With OE on a GPIO, `DisableOutputs()` takes every output to the off level for each configuration of the
   OUTNE table on the Hardware page; measure the delay against 60 ns.
