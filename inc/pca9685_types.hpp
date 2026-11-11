/**
 * @file pca9685_types.hpp
 * @brief Public types of the PCA9685 driver: errors, `Result<T>`, channel values, configuration,
 * presets and the frequency and pulse-width arithmetic.
 * @copyright Copyright (c) 2026 The driver-pca9685 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Everything here is plain data and `constexpr` arithmetic with no platform dependency, so
 * handlers, tests and tools can include it without the driver template. The configuration defaults
 * are the chip's reset values (section 7.3), so `Config{}` describes a chip that was just reset.
 *
 * Section, table and figure numbers refer to the NXP PCA9685 product data sheet, Rev. 4
 * (16 April 2015).
 */
#ifndef PCA9685_TYPES_HPP
#define PCA9685_TYPES_HPP

#include <cstdint>

#include "pca9685_detail.hpp"
#include "pca9685_registers.hpp"
#include "pca9685_version.h"

/** @brief The PCA9685 driver: everything it declares lives here; internals in `detail`. */
namespace pca9685 {

/**
 * @defgroup pca9685_types Driver types
 * @brief Errors, results, channel values, configuration and presets.
 * @{
 */

/**
 * @brief Why a call failed. Every call of the driver returns one of these in its `Result`.
 *
 * Values 0 to 9 are the shared base set; chip-specific values start at 16. `[[nodiscard]]` on
 * the type: a function that returns a bare `Error` cannot have it ignored either (KB-CPP20-037).
 */
enum class [[nodiscard]] Error : uint8_t {
  // Base set: the same names and values in every driver of the org (standards/driver.md).
  Ok = 0,               ///< The call succeeded.
  NotInitialized = 1,   ///< `EnsureInitialized()` has not succeeded yet, or `Deinitialize()` ran
                        ///< or a reset was found since.
  InvalidArgument = 2,  ///< An argument, the address or a `Config` field is out of range; nothing
                        ///< was sent (a bank call still sends its switch-offs).
  Bus = 3,              ///< A port hook returned `false`: a NACK of a data byte, a bus error, or a
                        ///< failed output-enable pin write.
  Timeout = 4,          ///< Not returned by this driver (no wait polls the chip).
  DeviceIdMismatch = 5, ///< At start-up, a register did not read back what was just written: no
                        ///< PCA9685 answers, or another part does (the chip has no ID register).
  Crc = 6,              ///< Not returned by this driver (I2C transfers carry no CRC).
  Busy = 7,             ///< Not returned by this driver (the chip has no busy state).
  DeviceFault = 8,      ///< Not returned by this driver (the chip reports no faults).
  Unsupported = 9,      ///< The port has no output-enable `GpioSet()` hook, which this call needs.
  // Chip-specific values start at 16; each one is returned by some call (tests/test_errors.cpp).
  WriteMismatch = 16, ///< A register read back differently from what was just written: a write the
  ///< chip acknowledged but did not keep. Channel calls with `Verify::ReadBack`;
  ///< `SetPwmFrequency()` always (PRE_SCALE ignores a write while awake).
  NoDevice = 17, ///< Nothing acknowledged the address byte: no chip at the address, or it is
                 ///< unpowered. Needs a port with the optional `GetLastFailure()` hook; without
                 ///< it this is `Bus`.
  DeviceReset =
      18, ///< The chip went through a power-on or software reset since the start, confirmed
          ///< by two reads: MODE1.AI = 0, or MODE2, EXTCLK, SUBADR1 or PRE_SCALE back at
          ///< its reset value while the driver's copy differs (sections 7.5, 7.6). The
          ///< driver drove OE high, wrote ALL_LED_OFF_H and is now uninitialised.
  InvalidReply = 19, ///< A read returned bits the chip cannot send: a read-only bit set, or an
                     ///< inconsistent MODE1/MODE2/SUBADR1 frame. A floating, stuck or shared line,
                     ///< never data.
  Asleep = 20,       ///< The registers were written, but the chip is (or may be) asleep, so the
                     ///< outputs do not follow until `Wake()`; for a switch-off, an output may hold
                     ///< its last level (section 7.3.1, note 4). `DisableOutputs()` acts at once.
  StateUnknown = 21, ///< The call depends on a value a failed write left unknown (a channel, the
                     ///< prescaler); nothing was sent. Read it back, or write it again.
  ExternalClockLatched = 22, ///< MODE1.EXTCLK is set on the chip but `Config` asks for the
                             ///< internal oscillator: the bit is sticky until a power cycle or a
                             ///< software reset (table 5).
};

/**
 * @brief The name of an error, for logs.
 * @param error The error.
 * @return A static string such as `"Bus"`; `"Unknown"` for a value that is not an enumerator.
 */
constexpr const char* ToString(Error error) noexcept {
  switch (error) {
  case Error::Ok:
    return "Ok";
  case Error::NotInitialized:
    return "NotInitialized";
  case Error::InvalidArgument:
    return "InvalidArgument";
  case Error::Bus:
    return "Bus";
  case Error::Timeout:
    return "Timeout";
  case Error::DeviceIdMismatch:
    return "DeviceIdMismatch";
  case Error::Crc:
    return "Crc";
  case Error::Busy:
    return "Busy";
  case Error::DeviceFault:
    return "DeviceFault";
  case Error::Unsupported:
    return "Unsupported";
  case Error::WriteMismatch:
    return "WriteMismatch";
  case Error::NoDevice:
    return "NoDevice";
  case Error::DeviceReset:
    return "DeviceReset";
  case Error::InvalidReply:
    return "InvalidReply";
  case Error::Asleep:
    return "Asleep";
  case Error::StateUnknown:
    return "StateUnknown";
  case Error::ExternalClockLatched:
    return "ExternalClockLatched";
  default:
    break; // a value made by static_cast from an integer that is not an enumerator
  }
  return "Unknown";
}

/**
 * @brief The value of a call, or why there is none.
 *
 * Check `ok()` (or the `bool` conversion) before reading `value`; on failure `value` is `T{}`.
 *
 * @tparam T The value type. `Result<void>` carries only the error.
 */
template <typename T>
struct [[nodiscard]] Result {
  T value{};              ///< The value; `T{}` when the call failed.
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /**
   * @brief A successful result.
   * @param v The value.
   * @return `{v, Error::Ok}`.
   */
  static constexpr Result Success(T v) noexcept {
    return {v, Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{T{}, e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {T{}, e};
  }
};

/** @brief The result of a call that returns no value. */
template <>
struct [[nodiscard]] Result<void> {
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /** @return A successful result. */
  static constexpr Result Success() noexcept {
    return {Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {e};
  }
};

/** @brief Severity passed to the optional `Log()` hook. */
enum class LogLevel : uint8_t {
  Error = 0, ///< A call failed.
  Warning,   ///< Something unexpected that the driver recovered from.
  Info,      ///< Lifecycle events: initialised, reset found.
  Debug,     ///< Detail for bring-up.
};

/** @brief Whether a call reads back what it wrote (handbook KB-ARCH-041). */
enum class Verify : uint8_t {
  None = 0,     ///< Write only. Default.
  ReadBack = 1, ///< Write, then read MODE1, MODE2, SUBADR1 (the reset check) and the channel
                ///< back; a difference is `Error::WriteMismatch`.
};

/** @brief The output stage of all 16 outputs (MODE2.OUTDRV, table 6). */
enum class OutputDrive : uint8_t {
  OpenDrain = 0, ///< Sink only. Needed for LEDs with integrated Zener diodes (section 10, Q5).
  TotemPole = 1, ///< Sink 25 mA, source 10 mA. Reset value.
};

/** @brief The clock the PWM counter runs from (MODE1.EXTCLK, table 5). */
enum class ClockSource : uint8_t {
  Internal = 0, ///< The internal oscillator, 25 MHz typical. Reset value.
  External = 1, ///< The EXTCLK pin, DC to 50 MHz; sticky until a power cycle or software reset.
};

/** @brief What `Sleep()` does with the outputs (section 7.3.1.1). */
enum class SleepMode : uint8_t {
  OutputsOff = 0,  ///< Every channel off first (ALL_LED_OFF_H), then sleep: the safe low-power
                   ///< state; `Wake()` leaves every channel off.
  HoldOutputs = 1, ///< Sleep with the channels as they are; `Wake()` restarts them (RESTART).
                   ///< While asleep an output may hold its last level.
};

namespace detail {
/// Last enumerator of `Verify` (detail::IsEnumerator).
template <>
struct EnumLast<Verify> {
  static constexpr Verify kValue = Verify::ReadBack; ///< The last enumerator.
};
/// Last enumerator of `OutputDrive`.
template <>
struct EnumLast<OutputDrive> {
  static constexpr OutputDrive kValue = OutputDrive::TotemPole; ///< The last enumerator.
};
/// Last enumerator of `ClockSource`.
template <>
struct EnumLast<ClockSource> {
  static constexpr ClockSource kValue = ClockSource::External; ///< The last enumerator.
};
/// Last enumerator of `SleepMode`.
template <>
struct EnumLast<SleepMode> {
  static constexpr SleepMode kValue = SleepMode::HoldOutputs; ///< The last enumerator.
};
} // namespace detail

/// Number of PWM channels, LED0 to LED15.
inline constexpr uint8_t kChannelCount = reg::kChannels;
/// Counter steps per PWM period: 4096 (12 bits, section 7.3.3).
inline constexpr uint32_t kCountsPerPeriod = 4096U;
/// Highest ON or OFF count, 4095.
inline constexpr uint16_t kMaxCount = reg::kMaxCount;
/// `SetDuty()` value for a channel that is on for the whole period (full ON).
inline constexpr uint16_t kFullDuty = 4096U;
/// Typical frequency of the internal oscillator, in hertz (section 2, table 5).
inline constexpr uint32_t kInternalOscillatorHz = 25'000'000U;
/// Highest EXTCLK frequency, in hertz (table 15, f_EXTCLK).
inline constexpr uint32_t kMaxOscillatorHz = 50'000'000U;
/// Lowest `Config::oscillator_hz` the driver accepts, in hertz (EXTCLK runs from DC; below this
/// no PWM frequency of at least 1 Hz exists).
inline constexpr uint32_t kMinOscillatorHz = 1'000'000U;
/// Highest PWM frequency the arithmetic accepts, in hertz; `Validate()` then checks that the
/// prescaler is in range, which for the internal oscillator caps it at about 1526 Hz.
inline constexpr uint32_t kMaxPwmFrequencyHz = 10'000U;
/// Oscillator start-up after SLEEP is cleared, in microseconds (table 5, note 2: 500 us maximum).
inline constexpr uint32_t kOscillatorStartUs = 500U;
/// Lowest I2C address of a PCA9685: 1 A5 A4 A3 A2 A1 A0 with every pin low (figure 4).
inline constexpr uint8_t kMinI2cAddress = 0x40;
/// Highest I2C address of a PCA9685, every address pin high.
inline constexpr uint8_t kMaxI2cAddress = 0x7F;
/// The LED All Call address at power-up (ALLCALLADR = E0h): every PCA9685 acknowledges it until
/// MODE1.ALLCALL is cleared, so no chip may use it as its own (section 7.1.2).
inline constexpr uint8_t kAllCallAddress = 0x70;

/**
 * @brief The value of one channel: its four registers, decoded (table 7, figure 9).
 *
 * The output is asserted when the 12-bit counter reaches `on_count` and negated when it reaches
 * `off_count`. `full_on` (LEDn_ON_H bit 4) keeps it asserted, `full_off` (LEDn_OFF_H bit 4) keeps
 * it negated and takes precedence over `full_on`. `Pwm{}` is the reset value: off.
 */
struct Pwm {
  uint16_t on_count{0};  ///< LEDn_ON[11:0], 0 to 4095.
  uint16_t off_count{0}; ///< LEDn_OFF[11:0], 0 to 4095; not equal to `on_count` unless a full bit
                         ///< is set (section 7.3.4).
  bool full_on{false};   ///< LEDn_ON_H[4].
  bool full_off{true};   ///< LEDn_OFF_H[4]. Reset value: set.

  /** @brief Exact comparison of every field (integral members only, KB-CPP20-033). */
  friend constexpr bool operator==(const Pwm& lhs, const Pwm& rhs) noexcept = default;
};

/** @return A channel value that is off: LEDn_OFF_H[4] set, the reset value. */
constexpr Pwm PwmOff() noexcept {
  return Pwm{};
}

/** @return A channel value that is on for the whole period: LEDn_ON_H[4] set. */
constexpr Pwm PwmFullOn() noexcept {
  return Pwm{.on_count = 0U, .off_count = 0U, .full_on = true, .full_off = false};
}

/**
 * @brief Whether a channel value switches the channel off: LEDn_OFF_H[4] set, which takes
 * precedence over everything else (section 7.3.3).
 * @param pwm The value.
 * @return true when the channel is off.
 */
constexpr bool IsOff(const Pwm& pwm) noexcept {
  return pwm.full_off;
}

/**
 * @brief Whether a channel value can be written: both counts at most 4095 and, unless a full bit
 * is set, different (the registers "should never be programmed with the same values", section
 * 7.3.4).
 * @param pwm The value.
 * @return true when valid.
 */
constexpr bool IsValid(const Pwm& pwm) noexcept {
  if ((pwm.on_count > kMaxCount) || (pwm.off_count > kMaxCount)) {
    return false;
  }
  return pwm.full_on || pwm.full_off || (pwm.on_count != pwm.off_count);
}

/**
 * @brief The channel value for a duty cycle and a phase delay.
 *
 * The output is asserted from count `phase` for `duty` counts, wrapping past 4095 into the next
 * period (section 7.3.3, example 2). Staggering the phase of several channels spreads their
 * switching edges and the supply current surges.
 *
 * @param duty 0 (off) to `kFullDuty` (on for the whole period).
 * @param phase 0 to 4095, the delay before the output is asserted.
 * @return The value, or `InvalidArgument` when either argument is out of range.
 */
constexpr Result<Pwm> PwmForDuty(uint16_t duty, uint16_t phase = 0U) noexcept {
  if ((duty > kFullDuty) || (phase > kMaxCount)) {
    return Result<Pwm>::Failure(Error::InvalidArgument);
  }
  if (duty == 0U) {
    return Result<Pwm>::Success(PwmOff());
  }
  if (duty == kFullDuty) {
    return Result<Pwm>::Success(PwmFullOn());
  }
  const uint32_t off = (static_cast<uint32_t>(phase) + static_cast<uint32_t>(duty)) & 0x0FFFU;
  return Result<Pwm>::Success(Pwm{.on_count = phase,
                                  .off_count = static_cast<uint16_t>(off),
                                  .full_on = false,
                                  .full_off = false});
}

/**
 * @brief Everything `EnsureInitialized()` writes to the chip.
 *
 * The defaults are the reset values of MODE1, MODE2 and PRE_SCALE (tables 5, 6 and 8): 200 Hz from
 * the internal oscillator, totem-pole outputs, not inverted. Fields with a unit carry it as a
 * suffix.
 *
 * The driver chooses MODE2.OUTNE itself, so that a high OE pin always sets every output to the
 * level of a channel that is off: low when not inverted, high (totem pole) or high impedance (open
 * drain) when inverted (table 11). The driver keeps MODE1.ALLCALL and SUB1 to SUB3 cleared, so no
 * group address can change the chip behind its back. It keeps MODE2.OCH = 0 (the reset value):
 * written channel registers reach the outputs at the I2C STOP, so every byte of one transfer
 * changes together, and the bytes before a NACK at its STOP. OCH = 1 (change on ACK) is not
 * offered: it changes a channel once "all 4 registers" are loaded (section 7.3.2, note 3), and the
 * data sheet leaves open which bytes count after a NACK, so a switch-off could reach the output
 * part-written.
 */
struct Config {
  uint32_t pwm_frequency_hz{200}; ///< PWM frequency; PRE_SCALE = round(oscillator / (4096 x f)) - 1
                                  ///< must be 3 to 255 (equation 1). Reset value: 200 Hz (0x1E).
  uint32_t oscillator_hz{kInternalOscillatorHz}; ///< The counter clock, `kMinOscillatorHz` to
                                                 ///< `kMaxOscillatorHz`: 25 MHz typical internal,
                                                 ///< a measured value to calibrate, or EXTCLK.
  ClockSource clock{ClockSource::Internal};      ///< MODE1.EXTCLK. Reset value: internal.
  OutputDrive output_drive{OutputDrive::TotemPole}; ///< MODE2.OUTDRV. Reset value: totem pole.
  bool invert_outputs{false};                       ///< MODE2.INVRT. Reset value: not inverted.
};

/**
 * @brief Check a configuration without touching the chip.
 *
 * Fails when an enum holds a value that is not an enumerator, the oscillator is outside
 * `kMinOscillatorHz` to `kMaxOscillatorHz`, or the frequency is 0, above `kMaxPwmFrequencyHz`, or
 * needs a prescaler outside 3 to 255 (section 7.3.5).
 *
 * @param config The configuration.
 * @return `Error::Ok` or `Error::InvalidArgument`.
 */
constexpr Error Validate(const Config& config) noexcept;

/**
 * @brief The PRE_SCALE value for a frequency, by equation 1 of section 7.3.5, rounded to nearest.
 * @param frequency_hz The PWM frequency, 1 to `kMaxPwmFrequencyHz`.
 * @param oscillator_hz The counter clock, `kMinOscillatorHz` to `kMaxOscillatorHz`.
 * @return The prescaler, or `InvalidArgument` when an argument or the result (3 to 255) is out of
 *         range: never clamped (handbook KB-CPP-126).
 */
constexpr Result<uint8_t> PrescaleFor(uint32_t frequency_hz, uint32_t oscillator_hz) noexcept {
  if ((frequency_hz == 0U) || (frequency_hz > kMaxPwmFrequencyHz) ||
      (oscillator_hz < kMinOscillatorHz) || (oscillator_hz > kMaxOscillatorHz)) {
    return Result<uint8_t>::Failure(Error::InvalidArgument);
  }
  // round(osc / (4096 f)) as (osc + 2048 f) / (4096 f): at most 70.5e6, inside uint32_t.
  const uint32_t divisor = kCountsPerPeriod * frequency_hz;
  const uint32_t rounded = (oscillator_hz + (divisor / 2U)) / divisor;
  if ((rounded < (uint32_t{reg::kPreScaleMin} + 1U)) || (rounded > 256U)) {
    return Result<uint8_t>::Failure(Error::InvalidArgument);
  }
  return Result<uint8_t>::Success(static_cast<uint8_t>(rounded - 1U));
}

/**
 * @brief The PWM frequency a prescaler gives, rounded to the nearest hertz (equation 1 inverted).
 * @param prescale PRE_SCALE.
 * @param oscillator_hz The counter clock.
 * @return oscillator / (4096 x (prescale + 1)), rounded.
 */
constexpr uint32_t FrequencyFor(uint8_t prescale, uint32_t oscillator_hz) noexcept {
  const uint32_t divisor = kCountsPerPeriod * (static_cast<uint32_t>(prescale) + 1U);
  return (oscillator_hz + (divisor / 2U)) / divisor;
}

/**
 * @brief The counts of a pulse width at a prescaler: one count is (prescale + 1) / oscillator
 * seconds. Uses the chip's programmed prescaler (handbook KB-CPP-128).
 * @param width_us The pulse width in microseconds; 0 is off.
 * @param prescale PRE_SCALE.
 * @param oscillator_hz The counter clock.
 * @return The counts, rounded to nearest, or `InvalidArgument` when the width rounds to 0 counts
 *         without being 0, or to more than 4095 (it does not fit in one period).
 */
constexpr Result<uint16_t> CountsForPulseUs(uint32_t width_us, uint8_t prescale,
                                            uint32_t oscillator_hz) noexcept {
  using detail::operator""_u16;
  if (width_us == 0U) {
    return Result<uint16_t>::Success(0_u16);
  }
  const uint64_t per_count = 1'000'000ULL * (static_cast<uint64_t>(prescale) + 1ULL);
  const uint64_t counts =
      ((static_cast<uint64_t>(width_us) * oscillator_hz) + (per_count / 2ULL)) / per_count;
  if ((counts == 0ULL) || (counts > kMaxCount)) {
    return Result<uint16_t>::Failure(Error::InvalidArgument);
  }
  return Result<uint16_t>::Success(static_cast<uint16_t>(counts));
}

/**
 * @brief The pulse width of a number of counts, in nanoseconds, rounded to nearest: what a
 * quantised `SetPulseWidthUs()` programmed.
 * @param counts 0 to 4096.
 * @param prescale PRE_SCALE.
 * @param oscillator_hz The counter clock, not 0.
 * @return counts x (prescale + 1) / oscillator, in nanoseconds.
 */
constexpr uint32_t PulseNsForCounts(uint16_t counts, uint8_t prescale,
                                    uint32_t oscillator_hz) noexcept {
  const uint64_t ticks = static_cast<uint64_t>(counts) * (static_cast<uint64_t>(prescale) + 1ULL);
  const uint64_t osc = (oscillator_hz == 0U) ? 1ULL : static_cast<uint64_t>(oscillator_hz);
  return static_cast<uint32_t>(((ticks * 1'000'000'000ULL) + (osc / 2ULL)) / osc);
}

constexpr Error Validate(const Config& config) noexcept {
  if (!detail::IsEnumerator(config.clock) || !detail::IsEnumerator(config.output_drive)) {
    return Error::InvalidArgument;
  }
  return PrescaleFor(config.pwm_frequency_hz, config.oscillator_hz).error;
}

/**
 * @brief LEDs wired from the supply through a resistor to the outputs (figure 15): open drain,
 * inverted, so that a channel that is off leaves the output high impedance and the LED dark.
 * Mind the reset level: a chip that resets is not inverted and drives every output low, which
 * lights such an LED until `EnsureInitialized()` runs again (docs/hardware.md).
 * @return The preset.
 */
constexpr Config DirectLedPreset() noexcept {
  Config c{};
  c.output_drive = OutputDrive::OpenDrain;
  c.invert_outputs = true;
  c.pwm_frequency_hz = 1000U;
  return c;
}

/**
 * @brief Hobby servos and external N-type drivers (figure 13): totem pole, not inverted, so the
 * reset level, low, is off. 50 Hz, a 20 ms period.
 * @return The preset.
 */
constexpr Config ServoPreset() noexcept {
  Config c{};
  c.pwm_frequency_hz = 50U;
  return c;
}

// The arithmetic and the presets, proved at build time (KB-CPP20-012).
static_assert(Validate(Config{}) == Error::Ok);
static_assert(Validate(DirectLedPreset()) == Error::Ok);
static_assert(Validate(ServoPreset()) == Error::Ok);
// Equation 2: 200 Hz from 25 MHz is 0x1E, the reset value of PRE_SCALE.
static_assert(PrescaleFor(200U, kInternalOscillatorHz).value == reg::kPreScaleReset);
// 1526 Hz is PRE_SCALE 3 and PRE_SCALE 255 gives 24 Hz (section 7.3.5); 1800 Hz and 23 Hz do not
// fit.
static_assert(PrescaleFor(1526U, kInternalOscillatorHz).value == 3U);
static_assert(FrequencyFor(0xFFU, kInternalOscillatorHz) == 24U);
static_assert(PrescaleFor(24U, kInternalOscillatorHz).value == 253U);
static_assert(!PrescaleFor(1800U, kInternalOscillatorHz).ok());
static_assert(!PrescaleFor(23U, kInternalOscillatorHz).ok());
static_assert(!PrescaleFor(0U, kInternalOscillatorHz).ok());
static_assert(FrequencyFor(reg::kPreScaleReset, kInternalOscillatorHz) == 197U);
// 50 Hz: PRE_SCALE 121; 1.5 ms is 307 counts of 4.88 us.
static_assert(PrescaleFor(50U, kInternalOscillatorHz).value == 121U);
static_assert(CountsForPulseUs(1500U, 121U, kInternalOscillatorHz).value == 307U);
static_assert(PulseNsForCounts(307U, 121U, kInternalOscillatorHz) == 1'498'160U);
static_assert(!CountsForPulseUs(30'000U, 121U, kInternalOscillatorHz).ok());
// Example 1 of section 7.3.3: a 10 % delay (409) and a 20 % duty (819) end at count 1228.
static_assert(PwmForDuty(819U, 409U).value.off_count == 1228U);
// Example 2: a 90 % delay (3685) and a 90 % duty (3686) wrap to 3275.
static_assert(PwmForDuty(3686U, 3685U).value.off_count == 3275U);
static_assert(IsOff(PwmForDuty(0U).value) && PwmForDuty(kFullDuty).value.full_on);
static_assert(!PwmForDuty(kFullDuty + 1U).ok() && !PwmForDuty(1U, 4096U).ok());
static_assert(IsValid(Pwm{}) && IsValid(PwmFullOn()) && !IsValid(Pwm{.full_off = false}) &&
              !IsValid(Pwm{.on_count = 4096U}));

/**
 * @brief Reached only when a `CheckedConfig` is built from an invalid configuration: it is never
 * defined, so the build fails and names it (KB-CPP20-013).
 */
void ConfigIsInvalid() noexcept;

/**
 * @brief A configuration proven valid at build time.
 *
 * `constexpr CheckedConfig kBoard{Config{.pwm_frequency_hz = 50}};` fails to compile, naming
 * `ConfigIsInvalid`, when `Validate()` rejects the configuration. Pass `kBoard.Get()` to the
 * driver; the driver still validates at start-up, for configurations built at run time.
 */
class CheckedConfig {
public:
  /**
   * @brief Check `config` at build time.
   * @param config The configuration; it must pass `Validate()`.
   */
  consteval explicit CheckedConfig(const Config& config) noexcept : config_(config) {
    if (Validate(config) != Error::Ok) {
      ConfigIsInvalid();
    }
  }
  /** @return The checked configuration. */
  [[nodiscard]] constexpr const Config& Get() const& noexcept {
    return config_;
  }
  /** @brief Not on a temporary: the reference would dangle (KB-CPP20-044). */
  void Get() const&& = delete;

private:
  Config config_;
};

/** @return The driver version, e.g. `"0.1.0"`, from the generated `pca9685_version.h`. No I/O. */
constexpr const char* GetDriverVersion() noexcept {
  return PCA9685_VERSION_STRING;
}
/** @return The major version. No I/O. */
constexpr uint32_t GetDriverVersionMajor() noexcept {
  return PCA9685_VERSION_MAJOR;
}
/** @return The minor version. No I/O. */
constexpr uint32_t GetDriverVersionMinor() noexcept {
  return PCA9685_VERSION_MINOR;
}
/** @return The patch version. No I/O. */
constexpr uint32_t GetDriverVersionPatch() noexcept {
  return PCA9685_VERSION_PATCH;
}

/** @} */

} // namespace pca9685

#endif // PCA9685_TYPES_HPP
