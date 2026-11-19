/**
 * @file pca9685.hpp
 * @brief PCA9685 driver: the one header to include.
 * @copyright Copyright (c) 2026 The driver-pca9685 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * `pca9685::PCA9685<Port>` drives one NXP PCA9685 16-channel, 12-bit PWM controller through a
 * platform port you write (see `pca9685_i2c_interface.hpp`). The driver has no platform code,
 * allocates nothing, throws nothing, takes no lock and is not thread-safe: give each object one
 * owner, or guard it with one mutex.
 *
 * Section, table and figure numbers refer to the NXP PCA9685 product data sheet, Rev. 4
 * (16 April 2015).
 */
#ifndef PCA9685_HPP
#define PCA9685_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "pca9685_detail.hpp"
#include "pca9685_i2c_interface.hpp"
#include "pca9685_registers.hpp"
#include "pca9685_types.hpp"
#include "pca9685_version.h"

namespace pca9685 {

namespace detail {

// Register encoders and decoders: free constexpr functions, emitted once whatever the port type,
// and proved below with static_assert (KB-CPP20-009, -012).

/// The four bytes of a channel value: ON_L, ON_H, OFF_L, OFF_H (table 7).
[[nodiscard]] constexpr std::array<uint8_t, 4> EncodePwm(const Pwm& pwm) noexcept {
  const auto on = static_cast<uint32_t>(pwm.on_count) & 0x0FFFU;
  const auto off = static_cast<uint32_t>(pwm.off_count) & 0x0FFFU;
  const uint32_t on_full = pwm.full_on ? static_cast<uint32_t>(reg::kLedFull) : 0U;
  const uint32_t off_full = pwm.full_off ? static_cast<uint32_t>(reg::kLedFull) : 0U;
  return {static_cast<uint8_t>(on & 0xFFU), static_cast<uint8_t>((on >> 8U) | on_full),
          static_cast<uint8_t>(off & 0xFFU), static_cast<uint8_t>((off >> 8U) | off_full)};
}

/// A channel value read from the chip, or `InvalidReply` when a reserved bit (7..5 of an _H byte)
/// reads 1: no PCA9685 sends that, but a floating line does (table 7). Equal ON and OFF counts
/// without a full bit are a value: the driver never writes them, but a switch-on NACKed part-way
/// can leave them on the chip, where the output is never asserted (figure 9, example 4). A frame of
/// all zeros, which SDA stuck low also reads, is confirmed by the driver with a second frame.
[[nodiscard]] constexpr Result<Pwm> DecodePwm(std::span<const uint8_t, 4> b) noexcept {
  const auto on_h = static_cast<uint32_t>(b[1]);
  const auto off_h = static_cast<uint32_t>(b[3]);
  const auto reserved = static_cast<uint32_t>(reg::kLedHReserved);
  if (((on_h & reserved) != 0U) || ((off_h & reserved) != 0U)) {
    return Result<Pwm>::Failure(Error::InvalidReply);
  }
  const auto high = static_cast<uint32_t>(reg::kLedCountHigh);
  const auto full = static_cast<uint32_t>(reg::kLedFull);
  return Result<Pwm>::Success(Pwm{.on_count = static_cast<uint16_t>(((on_h & high) << 8U) | b[0]),
                                  .off_count = static_cast<uint16_t>(((off_h & high) << 8U) | b[2]),
                                  .full_on = (on_h & full) != 0U,
                                  .full_off = (off_h & full) != 0U});
}

/// MODE2.OUTNE for a configuration: the level of a channel that is off, so that OE high always
/// means off (table 11): low when not inverted; when inverted, high from a totem pole (OUTNE = 01)
/// and high impedance from an open drain (OUTNE = 10).
[[nodiscard]] constexpr uint32_t OffLevelOutne(const Config& c) noexcept {
  if (!c.invert_outputs) {
    return reg::kOutneLow;
  }
  return (c.output_drive == OutputDrive::TotemPole) ? reg::kOutneHigh : reg::kOutneHiZ;
}

/// MODE2 for a configuration (table 6). OCH stays 0: the outputs change at the STOP.
[[nodiscard]] constexpr uint8_t Mode2Value(const Config& c) noexcept {
  uint32_t v = 0U;
  if (c.invert_outputs) {
    v |= static_cast<uint32_t>(reg::kMode2Invrt);
  }
  if (c.output_drive == OutputDrive::TotemPole) {
    v |= static_cast<uint32_t>(reg::kMode2OutDrv);
  }
  return reg::Mode2Outne::Insert(static_cast<uint8_t>(v), OffLevelOutne(c));
}

/// MODE1 of a running chip: AI set, the clock source, SLEEP, RESTART and every group call clear
/// (table 5). The driver never writes another awake value.
[[nodiscard]] constexpr uint8_t Mode1Awake(const Config& c) noexcept {
  return (c.clock == ClockSource::External)
             ? static_cast<uint8_t>(static_cast<uint32_t>(reg::kMode1Ai) |
                                    static_cast<uint32_t>(reg::kMode1ExtClk))
             : reg::kMode1Ai;
}

/// SUBADR1 as the driver writes it at start-up and never again: away from its reset value 0xE2,
/// not 0x00 or 0xFF, bit 0 clear (read only). Inert while MODE1.SUB1 = 0 (table 9). A reset loads
/// 0xE2, so SUBADR1 is a reset sentinel that no MODE1 write of the driver can mask.
inline constexpr uint8_t kSubAdr1Sentinel = 0x5A;
static_assert((kSubAdr1Sentinel & 0x01U) == 0U);

/// `mode1` with `bits` set.
[[nodiscard]] constexpr uint8_t With(uint8_t mode1, uint8_t bits) noexcept {
  return static_cast<uint8_t>(static_cast<uint32_t>(mode1) | static_cast<uint32_t>(bits));
}

/// Whether `value` has any bit of `bits` set.
[[nodiscard]] constexpr bool Has(uint8_t value, uint8_t bits) noexcept {
  return (static_cast<uint32_t>(value) & static_cast<uint32_t>(bits)) != 0U;
}

/// The channel bit `channel` of a 16-bit channel mask (0 to 15; masked).
[[nodiscard]] constexpr uint16_t ChannelBit(uint32_t channel) noexcept {
  return static_cast<uint16_t>(uint32_t{1} << (channel & 0x0FU));
}

/// The bits of `count` channels from `first`, clipped to the 16 channels.
[[nodiscard]] constexpr uint16_t ChannelRange(uint32_t first, uint32_t count) noexcept {
  uint32_t mask = 0U;
  for (uint32_t i = 0U; i < reg::kChannels; ++i) {
    if ((i >= first) && (i < (first + count))) {
      mask |= uint32_t{1} << i;
    }
  }
  return static_cast<uint16_t>(mask);
}

// The reset configuration encodes to the reset value of MODE2; the encoders round-trip
// (KB-CPP20-012).
static_assert(Mode2Value(Config{}) == reg::kMode2Reset);
static_assert(Mode2Value(DirectLedPreset()) == 0x12U);
static_assert(EncodePwm(PwmOff())[3] == reg::kLedOffHReset);
static_assert(DecodePwm(EncodePwm(PwmForDuty(819U, 409U).value)).value ==
              PwmForDuty(819U, 409U).value);
static_assert(DecodePwm(EncodePwm(PwmFullOn())).value == PwmFullOn());
static_assert(DecodePwm(std::array<uint8_t, 4>{0xFFU, 0xFFU, 0xFFU, 0xFFU}).error ==
              Error::InvalidReply);
static_assert(DecodePwm(std::array<uint8_t, 4>{0U, 0U, 0U, 0U}).ok());
static_assert((ChannelRange(14U, 4U) == 0xC000U) && (ChannelBit(3U) == 0x0008U));

} // namespace detail

/**
 * @defgroup pca9685_driver PCA9685 driver
 * @brief The driver class.
 * @{
 */

// clang-format off: the hook table below must keep one row per line.
/**
 * @brief Driver for one PCA9685 on a platform port.
 *
 * Lifecycle: construct with the configuration (no I/O), `EnsureInitialized()` (every channel off,
 * the configuration written and read back, the oscillator started), use, optionally
 * `Deinitialize()` (every channel off, asleep). Every call that touches the chip returns
 * `Error::NotInitialized` until `EnsureInitialized()` has succeeded once; no call starts the chip
 * implicitly. `DisableOutputs()` (a pin write) and `Deinitialize()` work in every state, and once
 * started the switch-offs keep working while uninitialised (after a reset was found).
 *
 * Channels are numbered 0 to 15 (LED0 to LED15). The driver keeps a copy of every channel it
 * wrote (`GetChannel()`). A switch-on writes all four registers of the channel in one transfer,
 * and the driver keeps MODE2.OCH = 0, so the four reach the output together at the STOP (section
 * 7.3.2, note 2). Nothing is read back unless the call's last argument is `Verify::ReadBack`.
 *
 * **Switch-offs are one data byte and never refused.** A switch-off is `0x10` to LEDn_OFF_H, or to
 * ALL_LED_OFF_H for every channel (figure 24): one byte is stored or not, and full OFF takes
 * precedence (table 7), so even a failed switch-off never makes a channel more active (a 4-byte
 * burst NACKed part-way could: new ON bytes with the old OFF bytes, applied at the STOP). A channel
 * call whose value is off (`full_off`), a mask or bank call's off entries (sent first, before any
 * burst), `SetAllOff()` and `DisableOutputs()` always send their write, whatever the driver
 * doubts; a call that refuses its switch-ons still sends its switch-offs. Ok from a switch-off
 * means the output is off: while the chip may be asleep holding its outputs (after
 * `Sleep(SleepMode::HoldOutputs)`), the write is sent and the call returns `Asleep`;
 * `DisableOutputs()` then switches off at once.
 *
 * **Resets.** A power-on reset (supply below V_POR) or a software reset (general call, 06h) loads
 * every register with its reset value: MODE1 = 0x11 (asleep, AI clear), every channel full OFF
 * (sections 7.5, 7.6). The driver always sets MODE1.AI and never clears it, and writes MODE2,
 * EXTCLK and SUBADR1 (`detail::kSubAdr1Sentinel`, inert while SUB1 = 0) only at start-up; every
 * reset check reads MODE1, MODE2 and SUBADR1 in one 3-byte burst, and AI = 0 or any of the others
 * different from the copy is reset evidence. SUBADR1 is the sentinel no MODE1 write of the driver
 * can mask. The evidence is confirmed by a second read before the driver acts (rule 12d); acting
 * drives OE high, writes ALL_LED_OFF_H, marks the driver uninitialised and returns `DeviceReset`.
 * The driver reads them before it writes MODE1 from its copy (`Sleep()`, `Wake()`,
 * `SetPwmFrequency()`), in `CheckForReset()`, `EnsureInitialized()` once started and
 * `EnableOutputs()`, and with every `Verify::ReadBack`. A reset the
 * driver has not seen yet leaves the chip asleep, and asleep no output can switch on (table 5,
 * note 4); the driver wakes the chip only with a MODE1 write that it follows with a read of MODE1,
 * MODE2, SUBADR1 and PRE_SCALE, so a reset can never leave the chip running on a configuration
 * other than the driver's.
 *
 * **Failed writes.** A switch-on or burst whose transfer failed with `Bus` may have reached the
 * chip, in whole or in part (the bytes before a NACK are stored). The channels it covered become
 * unknown
 * (`GetUncertainChannels()`, `GetChannel()` returns `StateUnknown`) until a successful write or a
 * read; `EnableOutputs()` refuses while any is unknown. A failed PRE_SCALE write makes the
 * prescaler unknown, and `SetPulseWidthUs()` refuses until `ReadPrescale()` or a successful
 * `SetPwmFrequency()`. A failed MODE1 write that may have woken the chip leaves the wake check
 * pending: the next call runs it first, and a switch-on is refused until it passes. `NoDevice`, an
 * address NACK, proves nothing arrived: nothing becomes unknown.
 *
 * Optional port hooks, found at compile time:
 *
 * | Hook | Used for | Without it |
 * | --- | --- | --- |
 * | `bool GpioSet(CtrlPin, GpioSignal) noexcept` | OE: disabled first and enabled last by `EnsureInitialized()`; `DisableOutputs()`, `EnableOutputs()` | OE is wired low; the two calls return `Unsupported` |
 * | `bool GetLastFailure(I2cFailure& failure) noexcept` | Telling an address NACK (`NoDevice`) from other bus failures | Every failed transfer is `Bus` |
 * | `void Log(LogLevel, const char* message) noexcept` | One line per lifecycle event or failure | Nothing is logged |
 *
 * `void DelayUs(uint32_t us) noexcept` is required: the oscillator start-up wait. A hook declared
 * with the wrong signature (for example `void GpioSet(...)`) is a compile error.
 *
 * @tparam Port Your class derived from `I2cInterface<Port>`.
 */
// clang-format on
template <I2cPort Port>
class PCA9685 {
  static_assert(
      !detail::DeclaresDelayUs<Port> || detail::HasDelayUs<Port>,
      "pca9685: the DelayUs hook must be `void DelayUs(uint32_t us) noexcept`, so that no "
      "wait is truncated");
  static_assert(!detail::DeclaresGpioSet<Port> || detail::HasGpioSet<Port>,
                "pca9685: the optional GpioSet hook must be `bool GpioSet(CtrlPin, GpioSignal) "
                "noexcept`, so that a failed OE write is reported");
  static_assert(!detail::DeclaresGetLastFailure<Port> || detail::HasGetLastFailure<Port>,
                "pca9685: the optional GetLastFailure hook must be `bool "
                "GetLastFailure(I2cFailure& failure) noexcept`");
  static_assert(!detail::DeclaresLog<Port> || detail::HasLog<Port>,
                "pca9685: the optional Log hook must be `void Log(LogLevel, const char* message) "
                "noexcept`");

public:
  /**
   * @brief Bind the driver to a port, a 7-bit I2C address and a configuration. No I/O, no checks:
   * `EnsureInitialized()` validates both.
   * @param port The port; it must outlive the driver.
   * @param address 7-bit I2C address, `kMinI2cAddress` to `kMaxI2cAddress` (0x40 plus A5..A0)
   *        except `kAllCallAddress`.
   * @param config What `EnsureInitialized()` writes.
   */
  explicit PCA9685(Port& port, uint8_t address = kI2cAddress, const Config& config = {}) noexcept
      : bus_(port, address), config_(config) {}

  /** @brief One driver object per chip. */
  PCA9685(const PCA9685&) = delete;
  /** @brief One driver object per chip. */
  PCA9685& operator=(const PCA9685&) = delete;

  /// @name Lifecycle
  /// @{

  /**
   * @brief Start the chip on the first call. Once it has succeeded, a later call runs the reset
   * check (one 3-byte read) and returns `Ok`, or starts again from step 1 when the chip reset.
   *
   * Checks the configuration and the address (no I/O when either is bad), brings the bus up, then:
   *
   * 1. Switches everything off, whatever state an earlier run left: OE high (with `GpioSet()`),
   *    ALL_LED_OFF_H = 0x10 (one byte: it works whatever MODE1.AI holds, section 7.3.1.1), MODE2
   *    with OCH = 0 and the configured drive, inversion and OUTNE, and ALL_LED_OFF_H again, now
   *    sure to reach the outputs at the STOP (an earlier run may have left OCH = 1). Each of the
   *    four is sent even when one before it failed.
   * 2. Reads MODE1 and the byte after it (`DeviceIdMismatch` when they cannot come from a
   *    PCA9685; `ExternalClockLatched` when EXTCLK is set and the internal clock is asked for),
   *    writes SLEEP and AI (the oscillator stops, so PRE_SCALE can be written), sets EXTCLK by the
   *    two-write sequence of table 5 when asked, writes PRE_SCALE and the SUBADR1 sentinel.
   * 3. Identity, a check no floating or stuck line can pass (the chip has no ID register): MODE1,
   *    MODE2, SUBADR1 and PRE_SCALE must read back exactly what was written, and every channel
   *    full OFF.
   * 4. Wakes the oscillator (MODE1.SLEEP = 0), waits
   *    `kOscillatorStartUs` (internal clock only, table 5 note 2), reads MODE1, MODE2, SUBADR1
   *    and PRE_SCALE once more, and enables OE.
   *
   * 0.5 ms of waiting and thirteen transfers. After a failure the driver stays uninitialised
   * and the next call starts again at step 1.
   *
   * @return `Ok`, `InvalidArgument` (the configuration or the address; no I/O), `NoDevice`, `Bus`,
   *         `ExternalClockLatched`, `DeviceIdMismatch`, `DeviceReset` (the chip reset during the
   *         start-up) or `WriteMismatch` (the chip did not wake).
   */
  Result<void> EnsureInitialized() noexcept;

  /** @return true after a successful `EnsureInitialized()`, until `Deinitialize()` or a found
   * reset. No I/O. */
  [[nodiscard]] bool IsInitialized() const noexcept {
    return initialized_;
  }

  /**
   * @brief Switch every channel off and put the chip to sleep, its low-power safe state, and mark
   * the driver uninitialised.
   *
   * Always does its work, whatever the driver believes (never started, started, or a reset
   * found). An address that `EnsureInitialized()` refuses is `InvalidArgument` with no I/O (0x70
   * is LED All Call at power-up). Otherwise it drives OE high first (with `GpioSet()`; it needs no
   * bus), brings the bus up and sends ALL_LED_OFF_H = 0x10, then, only when the switch-off was sent
   * (asleep, an output still on would be held on), a wake first when an output may be held and
   * MODE1.SLEEP. `Ok` only when all of it was sent. The driver is uninitialised afterwards either
   * way.
   *
   * @return `Ok`, `InvalidArgument`, `NoDevice` or `Bus`.
   */
  Result<void> Deinitialize() noexcept;

  /**
   * @brief Replace the configuration. When initialised, run the whole start-up of
   * `EnsureInitialized()` again with it, which switches every channel off first; otherwise only
   * store it (no I/O).
   * @param config Checked with `Validate()` before any I/O.
   * @return `Ok`, `InvalidArgument` (nothing stored or sent), or the errors of
   *         `EnsureInitialized()` (the driver is then uninitialised).
   */
  Result<void> ApplyConfig(const Config& config) noexcept;

  /** @return The configuration stored or last applied; `pwm_frequency_hz` follows
   * `SetPwmFrequency()`. No I/O. */
  [[nodiscard]] const Config& GetConfig() const& noexcept {
    return config_;
  }
  /** @brief Not on a temporary driver: the reference would dangle (handbook KB-CPP20-044). */
  void GetConfig() const&& = delete;

  /** @return The 7-bit I2C address passed to the constructor. No I/O. */
  [[nodiscard]] uint8_t GetAddress() const noexcept {
    return bus_.GetAddress();
  }

  /**
   * @brief Find out whether the chip reset since the start: one 3-byte read of MODE1, MODE2 and
   * SUBADR1 (about 90 us at 400 kHz), a second one only to confirm reset evidence; also the
   * pending wake check, when there is one (one more read).
   *
   * Call it where a supply dip is possible, for example once per control period: a reset the
   * driver has not seen leaves the chip asleep with every channel off, and channel calls keep
   * returning `Ok` because a write cannot tell (the chip acknowledges it).
   *
   * @return `Ok`, `NotInitialized`, `NoDevice`, `Bus`, `InvalidReply` (a read-only bit reads 1,
   *         or an inconsistent frame) or `DeviceReset` (confirmed by two reads; OE high, every
   *         channel off, the driver uninitialised: call `EnsureInitialized()`).
   */
  Result<void> CheckForReset() noexcept;

  /// @}
  /// @name Channels
  /// @{

  /**
   * @brief Set one channel to a value: one 4-byte write (LEDn_ON_L to LEDn_OFF_H), or for an off
   * value the one byte LEDn_OFF_H = 0x10.
   *
   * An off value (`full_off`) is a switch-off, whatever else the value holds, and is never
   * refused; once the driver has started it is sent also while uninitialised. Any other value must
   * pass `IsValid()`, and is refused (nothing sent) while the wake check is pending and fails.
   *
   * @param channel 0 to 15.
   * @param value The channel value.
   * @param verify `Verify::ReadBack` to read MODE1, MODE2, SUBADR1 and the channel back (two more
   *        reads); an off value matches any value with full OFF.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (channel, value or verify; nothing sent),
   *         `NoDevice`, `Bus`, `Asleep`, `DeviceReset`, `InvalidReply` or `WriteMismatch`.
   */
  Result<void> SetPwm(uint8_t channel, const Pwm& value, Verify verify = Verify::None) noexcept;

  /**
   * @brief Set one channel to a duty cycle with a phase delay (`PwmForDuty()`), as `SetPwm()`.
   * @param channel 0 to 15.
   * @param duty 0 (off, a switch-off) to `kFullDuty` (full ON).
   * @param phase 0 to 4095: the count at which the output is asserted.
   * @param verify As `SetPwm()`.
   * @return As `SetPwm()`.
   */
  Result<void> SetDuty(uint8_t channel, uint16_t duty, uint16_t phase = 0U,
                       Verify verify = Verify::None) noexcept;

  /**
   * @brief Set one channel to a pulse of `width_us` at the start of each period, as `SetPwm()`.
   *
   * The counts come from the chip's prescaler as the driver last wrote and read it back (handbook
   * KB-CPP-128) and `Config::oscillator_hz`; the internal oscillator is 25 MHz typical, so
   * calibrate `oscillator_hz` for servo work (docs/hardware.md).
   *
   * @param channel 0 to 15.
   * @param width_us The pulse width; 0 is off (a switch-off).
   * @param verify As `SetPwm()`.
   * @return The programmed width in nanoseconds (quantised to one count), or the errors of
   *         `SetPwm()`; `InvalidArgument` also when the width rounds to 0 or more than 4095
   *         counts, `StateUnknown` when the prescaler is unknown after a failed write.
   */
  Result<uint32_t> SetPulseWidthUs(uint8_t channel, uint32_t width_us,
                                   Verify verify = Verify::None) noexcept;

  /**
   * @brief Switch one channel fully on (LEDn_ON_H bit 4), as `SetPwm(channel, PwmFullOn())`.
   * @param channel 0 to 15.
   * @param verify As `SetPwm()`.
   * @return As `SetPwm()`.
   */
  Result<void> SetFullOn(uint8_t channel, Verify verify = Verify::None) noexcept;

  /**
   * @brief Switch one channel off (LEDn_OFF_H bit 4), as `SetPwm(channel, PwmOff())`. Never
   * refused.
   * @param channel 0 to 15.
   * @param verify As `SetPwm()`; a value that is not an enumerator reads nothing back.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (the channel; nothing sent), `NoDevice`,
   *         `Bus`, `Asleep` (written, but an output may hold its level until `Wake()`),
   *         `DeviceReset`, `InvalidReply` or `WriteMismatch`.
   */
  Result<void> SetChannelOff(uint8_t channel, Verify verify = Verify::None) noexcept;

  /**
   * @brief Set `values.size()` consecutive channels from `first` in one transfer, so that with
   * OCH = 0 they change together at the STOP (section 7.3.2, note 2).
   *
   * When any value fails `IsValid()`, or the pending wake check fails, the call refuses the
   * switch-ons but still writes every off value, one channel per transfer, and returns the refusal.
   *
   * @param first The first channel, 0 to 15.
   * @param values 1 to 16 - `first` values.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (the range: nothing sent; a value: the
   *         switch-offs sent), `NoDevice`, `Bus`, `Asleep` or `DeviceReset`.
   */
  Result<void> SetChannels(uint8_t first, std::span<const Pwm> values) noexcept;

  /**
   * @brief Switch off every channel in `mask` (bit n = channel n), one data byte per channel
   * (LEDn_OFF_H = 0x10).
   * Never refused: a failed write does not stop the others. A mask of 0 sends nothing.
   * @param mask The channels.
   * @return `Ok`, `NotInitialized`, the first failure of the writes (`NoDevice`, `Bus`), `Asleep`
   *         or `DeviceReset`.
   */
  Result<void> SetChannelsOff(uint16_t mask) noexcept;

  /**
   * @brief Switch every channel off with one data byte, ALL_LED_OFF_H = 0x10 (figure 24, the
   * orderly shutdown of section 7.3.1.1). Never refused.
   * @return `Ok`, `NotInitialized`, `NoDevice`, `Bus`, `Asleep` or `DeviceReset`.
   */
  Result<void> SetAllOff() noexcept;

  /**
   * @brief The value the driver last wrote to or read from a channel. No I/O.
   * @param channel 0 to 15.
   * @return The value, or `NotInitialized`, `InvalidArgument`, or `StateUnknown` (its last write
   *         failed: call `ReadChannel()`).
   */
  [[nodiscard]] Result<Pwm> GetChannel(uint8_t channel) const noexcept;

  /** @return The channels whose last write failed, bit n = channel n; 0 when all are known. No
   * I/O. */
  [[nodiscard]] uint16_t GetUncertainChannels() const noexcept {
    return uncertain_;
  }

  /**
   * @brief Read one channel from the chip into the driver's copy: one 3-byte read of MODE1, MODE2
   * and SUBADR1 (the reset check: a 4-byte read needs AI) and one 4-byte read. A read of all
   * zeros, which SDA stuck low also gives, is confirmed by one more MODE1/MODE2/SUBADR1 read and
   * is `InvalidReply` when that read does not match the running chip.
   * @param channel 0 to 15.
   * @return The value, or `NotInitialized`, `InvalidArgument`, `NoDevice`, `Bus`, `DeviceReset` or
   *         `InvalidReply`.
   */
  Result<Pwm> ReadChannel(uint8_t channel) noexcept;

  /**
   * @brief Read all 16 channels into the driver's copy: the reset check, then one 64-byte read.
   * Makes every channel known again.
   * @return The values, or the errors of `ReadChannel()`.
   */
  Result<std::array<Pwm, kChannelCount>> ReadChannels() noexcept;

  /// @}
  /// @name Output enable (OE pin)
  /// @{

  /**
   * @brief Drive OE high: every output goes to the MODE2.OUTNE level, which the driver makes the
   * level of a channel that is off, within 60 ns (table 15, t_PHZ). The fast switch-off: no I2C,
   * and it works in every state, uninitialised, asleep or with a broken bus. Before the first
   * `EnsureInitialized()` and after a chip reset, OUTNE is the reset value: low.
   * @return `Ok`, `Unsupported` (the port has no `GpioSet()`) or `Bus` (the pin write failed).
   */
  Result<void> DisableOutputs() noexcept;

  /**
   * @brief Drive OE low: the outputs follow their channels again. A switch-on of every channel at
   * once, so it is refused while any channel is unknown or the wake check fails.
   * @return `Ok`, `NotInitialized`, `Unsupported`, `StateUnknown` (a channel is unknown: nothing
   *         sent), `Bus` or `DeviceReset`.
   */
  Result<void> EnableOutputs() noexcept;

  /// @}
  /// @name Frequency, sleep and restart
  /// @{

  /**
   * @brief Change the PWM frequency of all channels (PRE_SCALE, section 7.3.5).
   *
   * PRE_SCALE ignores writes while MODE1.SLEEP is 0 (table 4, note 1), so: the reset check (MODE1
   * read), MODE1.SLEEP if the chip is awake (the outputs hold for the moment), PRE_SCALE, a read
   * back of PRE_SCALE (unconditional: the chip acknowledges a write it ignores), then, if it was
   * awake, the wake of `Wake()` with its restart. Channel counts stay as they are, so pulse widths
   * scale with the period: write them again after it.
   *
   * @param frequency_hz 1 to `kMaxPwmFrequencyHz`; the prescaler must be 3 to 255.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (nothing sent), `NoDevice`, `Bus`,
   *         `InvalidReply`, `DeviceReset` or `WriteMismatch` (PRE_SCALE did not take the value).
   */
  Result<void> SetPwmFrequency(uint32_t frequency_hz) noexcept;

  /**
   * @brief The prescaler the driver last wrote and read back. No I/O.
   * @return PRE_SCALE, or `NotInitialized` or `StateUnknown` (its last write failed).
   */
  [[nodiscard]] Result<uint8_t> GetPrescale() const noexcept;

  /**
   * @brief The PWM frequency of the programmed prescaler, rounded to the nearest hertz. No I/O.
   * @return oscillator / (4096 x (PRE_SCALE + 1)), or the errors of `GetPrescale()`.
   */
  [[nodiscard]] Result<uint32_t> GetPwmFrequencyHz() const noexcept;

  /**
   * @brief Read PRE_SCALE into the driver's copy: the reset check, then a 1-byte read.
   * @return PRE_SCALE, or `NotInitialized`, `NoDevice`, `Bus`, `InvalidReply` or `DeviceReset`.
   */
  Result<uint8_t> ReadPrescale() noexcept;

  /**
   * @brief Stop the oscillator (MODE1.SLEEP = 1): the low-power mode (section 7.3.1).
   *
   * `SleepMode::OutputsOff` first switches every channel off (ALL_LED, never refused) and sleeps
   * only if that write succeeded. `SleepMode::HoldOutputs` keeps the channels for `Wake()` to
   * restart; while asleep an output may hold the level it had (the data sheet does not say), so a
   * switch-off then returns `Asleep`. Both read MODE1 first (the reset check) and write MODE1 from
   * the copy.
   *
   * @param mode What happens to the outputs.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (mode; nothing sent), `NoDevice`, `Bus`,
   *         `InvalidReply` or `DeviceReset`.
   */
  Result<void> Sleep(SleepMode mode = SleepMode::OutputsOff) noexcept;

  /**
   * @brief Start the oscillator and the channels again (section 7.3.1.1).
   *
   * Reads MODE1 (the reset check), writes MODE1 with SLEEP = 0, waits `kOscillatorStartUs`
   * (internal clock), reads MODE1, MODE2 and PRE_SCALE (the wake check), and when RESTART reads 1
   * writes it back as 1, which restarts the channels that ran before `Sleep()`: the data sheet's
   * order, SLEEP clear for 500 us before RESTART.
   *
   * @return `Ok`, `NotInitialized`, `NoDevice`, `Bus`, `InvalidReply`, `DeviceReset` or
   *         `WriteMismatch` (SLEEP still reads 1).
   */
  Result<void> Wake() noexcept;

  /** @return true when the chip is, or may be, asleep: after `Sleep()`, or a failed `Wake()`. No
   * I/O. */
  [[nodiscard]] bool MayBeAsleep() const noexcept {
    return asleep_maybe_;
  }

  /// @}
  /// @name Diagnostics: raw access and the last values read
  /// @{

  /**
   * @brief Read one register once: one transfer, no retry, no check (handbook KB-CPP-129).
   *
   * For bring-up and diagnostics. No register of the PCA9685 changes when read.
   *
   * @param address 0x00 to 0x45 (MODE1 to LED15_OFF_H) or 0xFE (PRE_SCALE); the write-only
   *        ALL_LED registers and the reserved TestMode register are refused.
   * @return The register value, or `NotInitialized`, `InvalidArgument` (nothing sent),
   *         `NoDevice` or `Bus`.
   */
  Result<uint8_t> ReadRegister(uint8_t address) noexcept;

  /** @return MODE1 as the driver last read it, by any call; 0 before the first. No I/O (handbook
   * KB-CPP-129). */
  [[nodiscard]] uint8_t GetLastMode1() const noexcept {
    return last_mode1_;
  }

  /** @return MODE2 as the driver last read it, by any call; 0 before the first. No I/O. */
  [[nodiscard]] uint8_t GetLastMode2() const noexcept {
    return last_mode2_;
  }

  /// @}

private:
  Result<void> initialize() noexcept;
  Error enterSafeState() noexcept;
  Result<void> configureClock() noexcept;
  Result<void> checkIdentity() noexcept;
  Result<void> setOutputEnable(GpioSignal signal) noexcept;
  Result<void> applyOne(uint8_t channel, const Pwm& value, Verify verify) noexcept;
  Result<void> switchOff(uint8_t channel, Verify verify) noexcept;
  Result<void> switchOn(uint8_t channel, const Pwm& value, Verify verify) noexcept;
  Result<void> writeOffs(uint8_t first, std::span<const Pwm> values) noexcept;
  Result<void> writeChannels(uint8_t first, std::span<const Pwm> values) noexcept;
  Result<void> writeAllOff() noexcept;
  Result<void> writeOff(uint8_t channel) noexcept;
  void markAllOff() noexcept;
  Result<void> afterSwitchOff(Result<void> written, uint8_t first, uint8_t count,
                              Verify verify) noexcept;
  Result<void> readBack(uint8_t first, uint8_t count) noexcept;
  Result<void> readChannelsInto(uint8_t first, std::span<Pwm> out) noexcept;
  Result<std::array<uint8_t, 3>> readModeBytes() noexcept;
  Result<void> checkZeroBurst(std::span<const uint8_t> window) noexcept;
  [[nodiscard]] bool addressValid() const noexcept;
  [[nodiscard]] bool looksReset(const std::array<uint8_t, 3>& modes) const noexcept;
  Result<uint8_t> readModes() noexcept;
  Result<uint8_t> verifyRunning() noexcept;
  Result<void> completeWakeCheck() noexcept;
  Result<void> wakeSequence() noexcept;
  void awakeConfirmed() noexcept;
  Result<void> changePrescale(uint8_t prescale) noexcept;
  Result<uint8_t> readByte(uint8_t reg) noexcept;
  Error writeByte(uint8_t reg, uint8_t value) noexcept;
  Result<void> markReset() noexcept;
  Result<void> fail(Error error, const char* message) noexcept;

  detail::RegisterBus<Port> bus_;
  Config config_{};
  std::array<Pwm, kChannelCount> channels_{}; ///< The copy: what each channel was set to.
  uint16_t uncertain_{0};                 ///< Channels whose last write failed (bit n = channel n).
  uint8_t mode1_{0};                      ///< MODE1 of the running chip (detail::Mode1Awake()).
  uint8_t mode2_{0};                      ///< MODE2 as last written.
  uint8_t prescale_{reg::kPreScaleReset}; ///< PRE_SCALE as last written and read back.
  uint8_t last_mode1_{0};                 ///< MODE1 as last read (GetLastMode1()).
  uint8_t last_mode2_{0};                 ///< MODE2 as last read (GetLastMode2()).
  bool prescale_known_{false};            ///< false after a failed PRE_SCALE write.
  bool asleep_maybe_{false};              ///< The chip is or may be asleep.
  bool held_maybe_{false};                ///< Asleep, an output may hold an on level.
  bool wake_check_pending_{false};        ///< A MODE1 write may have woken the chip, unverified.
  bool wake_acked_{false};                ///< The last wake write was acknowledged.
  bool initialized_{false};
  bool started_once_{false}; ///< EnsureInitialized() succeeded once: switch-offs keep working.
};

/** @} */

} // namespace pca9685

// codeql::misra_deviation_next_line(MD-CPP-009)
#include "../src/pca9685.ipp" // IWYU pragma: export

#endif // PCA9685_HPP
