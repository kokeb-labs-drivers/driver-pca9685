/**
 * @file pca9685.ipp
 * @brief PCA9685 driver implementation, included by `pca9685.hpp`.
 * @copyright Copyright (c) 2026 The driver-pca9685 authors. SPDX-License-Identifier: MIT
 *
 * Section, table and figure numbers refer to the NXP PCA9685 product data sheet, Rev. 4
 * (16 April 2015).
 */
#ifndef PCA9685_IPP
#define PCA9685_IPP

namespace pca9685 {

namespace detail {

/// The first of two errors that is not `Ok`: a sequence that keeps going after a failure (a
/// switch-off) reports the first thing that went wrong.
[[nodiscard]] constexpr Error FirstError(Error first, Error next) noexcept {
  return (first != Error::Ok) ? first : next;
}

/// Every channel bit.
inline constexpr uint16_t kAllChannels = 0xFFFFU;

/// What a bank call holds: any switch-on, and whether every switch-on is valid.
struct BankScan {
  bool any_on;
  bool all_valid;
};

/// Scan the values of a bank call; an off value is always valid.
[[nodiscard]] constexpr BankScan ScanBank(std::span<const Pwm> values) noexcept {
  BankScan scan{.any_on = false, .all_valid = true};
  for (const Pwm& value : values) {
    if (!IsOff(value)) {
      scan.any_on = true;
      scan.all_valid = scan.all_valid && IsValid(value);
    }
  }
  return scan;
}

/// The values of a bank call as written: every off value as `PwmOff()` (at most 16 used).
[[nodiscard]] constexpr std::array<Pwm, kChannelCount> NormaliseBank(
    std::span<const Pwm> values) noexcept {
  std::array<Pwm, kChannelCount> out{};
  const std::size_t count = (values.size() < out.size()) ? values.size() : out.size();
  for (std::size_t i = 0U; i < count; ++i) {
    out[i] = IsOff(values[i]) ? PwmOff() : values[i];
  }
  return out;
}

static_assert(ScanBank(std::array<Pwm, 2>{PwmOff(), PwmFullOn()}).any_on);
static_assert(!ScanBank(std::array<Pwm, 1>{Pwm{.full_off = false}}).all_valid);
static_assert(NormaliseBank(std::array<Pwm, 1>{Pwm{.on_count = 9U}})[0] == PwmOff());

} // namespace detail

// -------------------------------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<void> PCA9685<Port>::EnsureInitialized() noexcept {
  if (initialized_) {
    // Not a shortcut over a chip that may have reset: the reset check runs first (one read).
    if (auto r = CheckForReset(); r || (r.error != Error::DeviceReset)) {
      return r;
    }
  }
  if (auto r = initialize(); !r) {
    return r;
  }
  initialized_ = true;
  started_once_ = true;
  detail::Log(bus_.GetPort(), LogLevel::Info, "pca9685: initialised");
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> PCA9685<Port>::initialize() noexcept {
  if (Validate(config_) != Error::Ok) {
    return fail(Error::InvalidArgument, "pca9685: invalid Config");
  }
  if (!addressValid()) {
    return fail(Error::InvalidArgument, "pca9685: invalid I2C address");
  }
  if (!bus_.GetPort().EnsureInitialized()) {
    return fail(Error::Bus, "pca9685: the port could not bring the bus up");
  }
  // Until the start-up has proved otherwise, the chip may be anything an earlier run left.
  asleep_maybe_ = true;
  held_maybe_ = true;
  wake_check_pending_ = false;
  prescale_known_ = false;
  if (const Error e = enterSafeState(); e != Error::Ok) {
    return fail(e, "pca9685: switching every output off failed");
  }
  if (auto r = configureClock(); !r) {
    return r;
  }
  if (auto r = checkIdentity(); !r) {
    return r;
  }
  if (auto r = wakeSequence(); !r) {
    return r;
  }
  if (asleep_maybe_) {
    return fail(Error::WriteMismatch, "pca9685: the chip did not wake");
  }
  if constexpr (detail::HasGpioSet<Port>) {
    if (auto r = setOutputEnable(GpioSignal::ACTIVE); !r) {
      return r;
    }
  }
  return Result<void>::Success();
}

template <I2cPort Port>
Error PCA9685<Port>::enterSafeState() noexcept {
  // Every write is sent even when one before it failed: a switch-off is never refused. The
  // ALL_LED_OFF_H writes are one byte each, so they land whatever MODE1.AI holds; the second one
  // follows MODE2 with OCH = 0, so it reaches the outputs at its STOP (section 7.3.2, note 2).
  Error first = Error::Ok;
  if constexpr (detail::HasGpioSet<Port>) {
    if (!bus_.GetPort().GpioSet(CtrlPin::OutputEnable, GpioSignal::INACTIVE)) {
      first = Error::Bus;
    }
  }
  const uint8_t mode2 = detail::Mode2Value(config_);
  first = detail::FirstError(first, writeByte(reg::kAllLedOffH, reg::kLedFull));
  first = detail::FirstError(first, writeByte(reg::kMode2, mode2));
  first = detail::FirstError(first, writeByte(reg::kAllLedOffH, reg::kLedFull));
  mode2_ = mode2;
  uncertain_ = detail::kAllChannels; // known again when the identity check reads them
  return first;
}

template <I2cPort Port>
Result<void> PCA9685<Port>::configureClock() noexcept {
  // MODE1.AI is still unknown: the second byte is MODE2 when AI reads 1, MODE1 again when it reads
  // 0. Either way a floating line (0xFF, 0xFF) is caught before EXTCLK is believed.
  std::array<uint8_t, 2> modes{};
  if (const Error e = bus_.Read(reg::kMode1, std::span{modes}); e != Error::Ok) {
    return fail(e, "pca9685: MODE1 read failed");
  }
  last_mode1_ = modes[0];
  const bool consistent = detail::Has(modes[0], reg::kMode1Ai)
                              ? !detail::Has(modes[1], reg::kMode2Reserved)
                              : (modes[1] == modes[0]);
  if (!consistent) {
    return fail(Error::DeviceIdMismatch, "pca9685: MODE1/MODE2 read is not a PCA9685");
  }
  const bool latched = detail::Has(modes[0], reg::kMode1ExtClk);
  if (latched && (config_.clock == ClockSource::Internal)) {
    return fail(Error::ExternalClockLatched, "pca9685: EXTCLK is set; power cycle the chip");
  }
  mode1_ = detail::Mode1Awake(config_);
  // SLEEP with AI: the oscillator stops, so PRE_SCALE takes a write (table 4, note 1). EXTCLK is
  // set only by a second write once SLEEP is set (table 5, bit 6).
  const uint8_t first = latched ? detail::With(reg::kMode1Ai, reg::kMode1ExtClk) : reg::kMode1Ai;
  if (const Error e = writeByte(reg::kMode1, detail::With(first, reg::kMode1Sleep));
      e != Error::Ok) {
    return fail(e, "pca9685: MODE1 write failed");
  }
  if ((config_.clock == ClockSource::External) && !latched) {
    if (const Error e = writeByte(reg::kMode1, detail::With(mode1_, reg::kMode1Sleep));
        e != Error::Ok) {
      return fail(e, "pca9685: MODE1 EXTCLK write failed");
    }
  }
  // Validate() passed, so the prescaler is in range.
  const auto prescale = PrescaleFor(config_.pwm_frequency_hz, config_.oscillator_hz);
  if (const Error e = writeByte(reg::kPreScale, prescale.value); e != Error::Ok) {
    return fail(e, "pca9685: PRE_SCALE write failed");
  }
  prescale_ = prescale.value;
  // The reset sentinel: SUBADR1 inert (SUB1 = 0) but away from its reset value, written here only.
  if (const Error e = writeByte(reg::kSubAdr1, detail::kSubAdr1Sentinel); e != Error::Ok) {
    return fail(e, "pca9685: SUBADR1 write failed");
  }
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> PCA9685<Port>::checkIdentity() noexcept {
  using detail::operator""_u8;
  // The chip has no ID register: MODE1, MODE2, SUBADR1 and PRE_SCALE must read back what was just
  // written, which no floating, stuck or looped line can echo (0x00 has AI clear, 0xFF the
  // reserved MODE2 bits set), and every channel must read full OFF.
  const auto modes = readModeBytes();
  if (!modes) {
    return fail(modes.error == Error::InvalidReply ? Error::DeviceIdMismatch : modes.error,
                "pca9685: MODE1/MODE2/SUBADR1 read failed");
  }
  const auto restart = static_cast<uint32_t>(reg::kMode1Restart);
  if (((static_cast<uint32_t>(modes.value[0]) & ~restart) !=
       static_cast<uint32_t>(detail::With(mode1_, reg::kMode1Sleep))) ||
      (modes.value[1] != mode2_) || (modes.value[2] != detail::kSubAdr1Sentinel)) {
    return fail(Error::DeviceIdMismatch, "pca9685: MODE1/MODE2/SUBADR1 did not read back");
  }
  const auto prescale = readByte(reg::kPreScale);
  if (!prescale) {
    return fail(prescale.error, "pca9685: PRE_SCALE read failed");
  }
  if (prescale.value != prescale_) {
    return fail(Error::DeviceIdMismatch, "pca9685: PRE_SCALE did not read back");
  }
  prescale_known_ = true;
  std::array<Pwm, kChannelCount> read{};
  if (auto r = readChannelsInto(0_u8, std::span{read}); !r) {
    const Error e = (r.error == Error::InvalidReply) ? Error::DeviceIdMismatch : r.error;
    return fail(e, "pca9685: channel read failed");
  }
  if (!std::all_of(read.cbegin(), read.cend(),
                   [](const Pwm& value) noexcept { return IsOff(value); })) {
    return fail(Error::DeviceIdMismatch, "pca9685: a channel did not read full OFF");
  }
  channels_ = read;
  uncertain_ = 0U;
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> PCA9685<Port>::Deinitialize() noexcept {
  // Always does its work, whatever the driver believes: started, never started, or a reset found.
  initialized_ = false;
  if (!addressValid()) {
    // No I/O on an address EnsureInitialized() refuses: 0x70 is LED All Call at power-up (7.1.2),
    // where the writes would switch off and put to sleep every PCA9685 on the bus.
    return fail(Error::InvalidArgument, "pca9685: invalid I2C address");
  }
  Error first = Error::Ok;
  if constexpr (detail::HasGpioSet<Port>) {
    // OE needs no bus: driven first, so a bus that does not come up still leaves the outputs off.
    if (!bus_.GetPort().GpioSet(CtrlPin::OutputEnable, GpioSignal::INACTIVE)) {
      first = Error::Bus;
    }
  }
  if (!bus_.GetPort().EnsureInitialized()) {
    return fail(Error::Bus, "pca9685: the port could not bring the bus up");
  }
  const Error off = writeAllOff().error;
  if (off != Error::Ok) {
    // Asleep, an output that is still on would be held on: no sleep without the switch-off.
    return Result<void>::Failure(off);
  }
  const uint8_t awake = started_once_ ? mode1_ : reg::kMode1Ai;
  if (held_maybe_) {
    // Asleep, an output may hold its level: wake so the outputs follow the off registers.
    first = detail::FirstError(first, writeByte(reg::kMode1, awake));
    detail::DelayUs(bus_.GetPort(), kOscillatorStartUs);
  }
  asleep_maybe_ = true; // a failed write may have taken effect
  wake_check_pending_ = false;
  first = detail::FirstError(first, writeByte(reg::kMode1, detail::With(awake, reg::kMode1Sleep)));
  if (first == Error::Ok) {
    held_maybe_ = false; // every channel off before the sleep
    return Result<void>::Success();
  }
  return Result<void>::Failure(first);
}

template <I2cPort Port>
Result<void> PCA9685<Port>::ApplyConfig(const Config& config) noexcept {
  if (Validate(config) != Error::Ok) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  config_ = config;
  if (!initialized_) {
    return Result<void>::Success(); // written by the first EnsureInitialized()
  }
  initialized_ = false;
  return EnsureInitialized();
}

template <I2cPort Port>
Result<void> PCA9685<Port>::CheckForReset() noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (wake_check_pending_) {
    return completeWakeCheck();
  }
  const auto modes = readModes();
  return modes ? Result<void>::Success() : Result<void>::Failure(modes.error);
}

// -------------------------------------------------------------------------------------------------
// Channels
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<void> PCA9685<Port>::SetPwm(uint8_t channel, const Pwm& value, Verify verify) noexcept {
  return applyOne(channel, value, verify);
}

template <I2cPort Port>
Result<void> PCA9685<Port>::SetDuty(uint8_t channel, uint16_t duty, uint16_t phase,
                                    Verify verify) noexcept {
  const auto value = PwmForDuty(duty, phase);
  if (!value) {
    return Result<void>::Failure(initialized_ ? value.error : Error::NotInitialized);
  }
  return applyOne(channel, value.value, verify);
}

template <I2cPort Port>
Result<uint32_t> PCA9685<Port>::SetPulseWidthUs(uint8_t channel, uint32_t width_us,
                                                Verify verify) noexcept {
  using detail::operator""_u32;
  if (width_us == 0U) {
    const auto off = applyOne(channel, PwmOff(), verify);
    return off ? Result<uint32_t>::Success(0_u32) : Result<uint32_t>::Failure(off.error);
  }
  if (!initialized_) {
    return Result<uint32_t>::Failure(Error::NotInitialized);
  }
  if ((channel >= kChannelCount) || !detail::IsEnumerator(verify)) {
    return Result<uint32_t>::Failure(Error::InvalidArgument);
  }
  if (!prescale_known_) {
    return Result<uint32_t>::Failure(Error::StateUnknown);
  }
  const auto counts = CountsForPulseUs(width_us, prescale_, config_.oscillator_hz);
  if (!counts) {
    return Result<uint32_t>::Failure(counts.error);
  }
  const Pwm value{.on_count = 0U, .off_count = counts.value, .full_on = false, .full_off = false};
  if (auto r = switchOn(channel, value, verify); !r) {
    return Result<uint32_t>::Failure(r.error);
  }
  return Result<uint32_t>::Success(
      PulseNsForCounts(counts.value, prescale_, config_.oscillator_hz));
}

template <I2cPort Port>
Result<void> PCA9685<Port>::SetFullOn(uint8_t channel, Verify verify) noexcept {
  return applyOne(channel, PwmFullOn(), verify);
}

template <I2cPort Port>
Result<void> PCA9685<Port>::SetChannelOff(uint8_t channel, Verify verify) noexcept {
  return applyOne(channel, PwmOff(), verify);
}

template <I2cPort Port>
Result<void> PCA9685<Port>::SetChannels(uint8_t first, std::span<const Pwm> values) noexcept {
  if (!initialized_ && !started_once_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  const std::size_t count = values.size();
  if ((first >= kChannelCount) || (count == 0U) ||
      (count > (static_cast<std::size_t>(kChannelCount) - first))) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  // Every switch-off first, one data byte each, before any burst (section 7.3.3, figure 24); also
  // when the driver is uninitialised after a reset was found.
  const auto offs = writeOffs(first, values);
  const detail::BankScan scan = detail::ScanBank(values);
  if (!scan.any_on) {
    return afterSwitchOff(offs, first, 0U, Verify::None);
  }
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  Error refusal = scan.all_valid ? Error::Ok : Error::InvalidArgument;
  if (refusal == Error::Ok) {
    refusal = completeWakeCheck().error;
  }
  if (refusal != Error::Ok) {
    return Result<void>::Failure(refusal); // the switch-ons are refused; the switch-offs went out
  }
  const auto normalised = detail::NormaliseBank(values);
  if (auto written = writeChannels(first, std::span{normalised}.first(count)); !written) {
    return written;
  }
  if (!offs) {
    return offs;
  }
  return asleep_maybe_ ? Result<void>::Failure(Error::Asleep) : Result<void>::Success();
}

template <I2cPort Port>
Result<void> PCA9685<Port>::SetChannelsOff(uint16_t mask) noexcept {
  if (!initialized_ && !started_once_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (mask == 0U) {
    return Result<void>::Success();
  }
  Result<void> written = Result<void>::Success();
  for (uint32_t channel = 0U; channel < kChannelCount; ++channel) {
    if ((static_cast<uint32_t>(mask) & static_cast<uint32_t>(detail::ChannelBit(channel))) != 0U) {
      // A failed write does not stop the others: a switch-off is never refused.
      const auto r = writeOff(static_cast<uint8_t>(channel));
      written = written ? r : written;
    }
  }
  return afterSwitchOff(written, 0U, 0U, Verify::None);
}

template <I2cPort Port>
Result<void> PCA9685<Port>::SetAllOff() noexcept {
  if (!initialized_ && !started_once_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  return afterSwitchOff(writeAllOff(), 0U, 0U, Verify::None);
}

template <I2cPort Port>
Result<Pwm> PCA9685<Port>::GetChannel(uint8_t channel) const noexcept {
  if (!initialized_) {
    return Result<Pwm>::Failure(Error::NotInitialized);
  }
  if (channel >= kChannelCount) {
    return Result<Pwm>::Failure(Error::InvalidArgument);
  }
  if ((static_cast<uint32_t>(uncertain_) & static_cast<uint32_t>(detail::ChannelBit(channel))) !=
      0U) {
    return Result<Pwm>::Failure(Error::StateUnknown);
  }
  return Result<Pwm>::Success(channels_[channel]);
}

template <I2cPort Port>
Result<Pwm> PCA9685<Port>::ReadChannel(uint8_t channel) noexcept {
  if (!initialized_) {
    return Result<Pwm>::Failure(Error::NotInitialized);
  }
  if (channel >= kChannelCount) {
    return Result<Pwm>::Failure(Error::InvalidArgument);
  }
  if (const auto modes = readModes(); !modes) {
    return Result<Pwm>::Failure(modes.error);
  }
  std::array<Pwm, 1> value{};
  if (auto r = readChannelsInto(channel, std::span{value}); !r) {
    return Result<Pwm>::Failure(r.error);
  }
  channels_[channel] = value[0];
  uncertain_ = static_cast<uint16_t>(static_cast<uint32_t>(uncertain_) &
                                     ~static_cast<uint32_t>(detail::ChannelBit(channel)));
  return Result<Pwm>::Success(value[0]);
}

template <I2cPort Port>
Result<std::array<Pwm, kChannelCount>> PCA9685<Port>::ReadChannels() noexcept {
  using detail::operator""_u8;
  using Values = std::array<Pwm, kChannelCount>;
  if (!initialized_) {
    return Result<Values>::Failure(Error::NotInitialized);
  }
  if (const auto modes = readModes(); !modes) {
    return Result<Values>::Failure(modes.error);
  }
  Values values{};
  if (auto r = readChannelsInto(0_u8, std::span{values}); !r) {
    return Result<Values>::Failure(r.error);
  }
  channels_ = values;
  uncertain_ = 0U;
  return Result<Values>::Success(values);
}

// -------------------------------------------------------------------------------------------------
// Output enable
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<void> PCA9685<Port>::DisableOutputs() noexcept {
  return setOutputEnable(GpioSignal::INACTIVE);
}

template <I2cPort Port>
Result<void> PCA9685<Port>::EnableOutputs() noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if constexpr (!detail::HasGpioSet<Port>) {
    return Result<void>::Failure(Error::Unsupported);
  } else {
    if (uncertain_ != 0U) {
      return Result<void>::Failure(Error::StateUnknown);
    }
    // A switch-on of every channel: the wake check and the reset check first.
    if (auto r = completeWakeCheck(); !r) {
      return r;
    }
    if (const auto modes = readModes(); !modes) {
      return Result<void>::Failure(modes.error);
    }
    return setOutputEnable(GpioSignal::ACTIVE);
  }
}

template <I2cPort Port>
Result<void> PCA9685<Port>::setOutputEnable(GpioSignal signal) noexcept {
  if constexpr (detail::HasGpioSet<Port>) {
    if (!bus_.GetPort().GpioSet(CtrlPin::OutputEnable, signal)) {
      return fail(Error::Bus, "pca9685: OE pin write failed");
    }
    return Result<void>::Success();
  } else {
    (void)signal;
    return Result<void>::Failure(Error::Unsupported);
  }
}

// -------------------------------------------------------------------------------------------------
// Frequency, sleep and restart
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<void> PCA9685<Port>::SetPwmFrequency(uint32_t frequency_hz) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  const auto prescale = PrescaleFor(frequency_hz, config_.oscillator_hz);
  if (!prescale) {
    return Result<void>::Failure(prescale.error);
  }
  if (auto r = completeWakeCheck(); !r) {
    return r;
  }
  const auto modes = readModes(); // the reset check before MODE1 is written from the copy
  if (!modes) {
    return Result<void>::Failure(modes.error);
  }
  const bool awake = !detail::Has(modes.value, reg::kMode1Sleep);
  Result<void> result = Result<void>::Success();
  if (awake) {
    // The outputs hold for the moment the oscillator is off (section 7.3.1.1).
    asleep_maybe_ = true;
    held_maybe_ = true;
    if (const Error e = writeByte(reg::kMode1, detail::With(mode1_, reg::kMode1Sleep));
        e != Error::Ok) {
      result = fail(e, "pca9685: MODE1 sleep write failed");
    }
  }
  if (result) {
    result = changePrescale(prescale.value);
  }
  if (awake) {
    // Woken even after a failure: a chip left asleep would hold its outputs.
    const auto woke = wakeSequence();
    result = result ? woke : result;
  }
  if (result) {
    config_.pwm_frequency_hz = frequency_hz;
  }
  return result;
}

template <I2cPort Port>
Result<void> PCA9685<Port>::changePrescale(uint8_t prescale) noexcept {
  prescale_known_ = false; // a failed write may have taken effect
  if (const Error e = writeByte(reg::kPreScale, prescale); e != Error::Ok) {
    return fail(e, "pca9685: PRE_SCALE write failed");
  }
  // Unconditional: the chip acknowledges a PRE_SCALE write it ignores (table 4, note 1).
  const auto back = readByte(reg::kPreScale);
  if (!back) {
    return fail(back.error, "pca9685: PRE_SCALE read failed");
  }
  prescale_ = back.value;
  prescale_known_ = true;
  if (back.value != prescale) {
    return fail(Error::WriteMismatch, "pca9685: PRE_SCALE did not take the value");
  }
  return Result<void>::Success();
}

template <I2cPort Port>
Result<uint8_t> PCA9685<Port>::GetPrescale() const noexcept {
  if (!initialized_) {
    return Result<uint8_t>::Failure(Error::NotInitialized);
  }
  return prescale_known_ ? Result<uint8_t>::Success(prescale_)
                         : Result<uint8_t>::Failure(Error::StateUnknown);
}

template <I2cPort Port>
Result<uint32_t> PCA9685<Port>::GetPwmFrequencyHz() const noexcept {
  const auto prescale = GetPrescale();
  if (!prescale) {
    return Result<uint32_t>::Failure(prescale.error);
  }
  return Result<uint32_t>::Success(FrequencyFor(prescale.value, config_.oscillator_hz));
}

template <I2cPort Port>
Result<uint8_t> PCA9685<Port>::ReadPrescale() noexcept {
  if (!initialized_) {
    return Result<uint8_t>::Failure(Error::NotInitialized);
  }
  if (const auto modes = readModes(); !modes) {
    return Result<uint8_t>::Failure(modes.error);
  }
  const auto value = readByte(reg::kPreScale);
  if (value) {
    prescale_ = value.value;
    prescale_known_ = true;
  }
  return value;
}

template <I2cPort Port>
Result<void> PCA9685<Port>::Sleep(SleepMode mode) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (!detail::IsEnumerator(mode)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (mode == SleepMode::OutputsOff) {
    // The orderly shutdown of section 7.3.1.1: off first, so nothing is held on while asleep.
    if (auto off = writeAllOff(); !off) {
      return off;
    }
  }
  const auto modes = readModes(); // the reset check before MODE1 is written from the copy
  if (!modes) {
    return Result<void>::Failure(modes.error);
  }
  asleep_maybe_ = true; // set first: a failed write may have taken effect
  if (mode == SleepMode::HoldOutputs) {
    held_maybe_ = true;
  }
  if (const Error e = writeByte(reg::kMode1, detail::With(mode1_, reg::kMode1Sleep));
      e != Error::Ok) {
    return fail(e, "pca9685: MODE1 sleep write failed");
  }
  wake_check_pending_ = false; // asleep: no wake to check
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> PCA9685<Port>::Wake() noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (const auto modes = readModes(); !modes) { // the reset check (section 7.3.1.1, step 1)
    return Result<void>::Failure(modes.error);
  }
  if (auto r = wakeSequence(); !r) {
    return r;
  }
  return asleep_maybe_ ? fail(Error::WriteMismatch, "pca9685: SLEEP still reads 1")
                       : Result<void>::Success();
}

// -------------------------------------------------------------------------------------------------
// Diagnostics
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<uint8_t> PCA9685<Port>::ReadRegister(uint8_t address) noexcept {
  if (!initialized_) {
    return Result<uint8_t>::Failure(Error::NotInitialized);
  }
  if ((address > reg::kLed15OffH) && (address != reg::kPreScale)) {
    return Result<uint8_t>::Failure(Error::InvalidArgument);
  }
  return readByte(address);
}

// -------------------------------------------------------------------------------------------------
// Private
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<void> PCA9685<Port>::applyOne(uint8_t channel, const Pwm& value, Verify verify) noexcept {
  // Started once, a switch-off still goes out when the driver is uninitialised (a reset found).
  if (!initialized_ && !(started_once_ && IsOff(value))) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (channel >= kChannelCount) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (IsOff(value)) {
    return switchOff(channel, verify); // never refused, whatever else the value holds
  }
  if (!IsValid(value) || !detail::IsEnumerator(verify)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  return switchOn(channel, value, verify);
}

template <I2cPort Port>
Result<void> PCA9685<Port>::switchOff(uint8_t channel, Verify verify) noexcept {
  return afterSwitchOff(writeOff(channel), channel, 1U, verify);
}

template <I2cPort Port>
Result<void> PCA9685<Port>::switchOn(uint8_t channel, const Pwm& value, Verify verify) noexcept {
  using detail::operator""_u8;
  // A wake that was never checked may have left the chip running on reset values: refused until
  // the check passes.
  if (auto r = completeWakeCheck(); !r) {
    return r;
  }
  const std::array<Pwm, 1> on{value};
  if (auto r = writeChannels(channel, std::span{on}); !r) {
    return r;
  }
  if (verify == Verify::ReadBack) {
    if (auto r = readBack(channel, 1_u8); !r) {
      return r;
    }
  }
  return asleep_maybe_ ? Result<void>::Failure(Error::Asleep) : Result<void>::Success();
}

template <I2cPort Port>
Result<void> PCA9685<Port>::afterSwitchOff(Result<void> written, uint8_t first, uint8_t count,
                                           Verify verify) noexcept {
  if (!written) {
    return written;
  }
  if (!initialized_) {
    // A reset found, or deinitialised: no wake check to run, nothing to read back.
    return held_maybe_ ? Result<void>::Failure(Error::Asleep) : Result<void>::Success();
  }
  if (auto r = completeWakeCheck(); !r) {
    return r;
  }
  if ((verify == Verify::ReadBack) && (count > 0U)) {
    if (auto r = readBack(first, count); !r) {
      return r;
    }
  }
  return held_maybe_ ? Result<void>::Failure(Error::Asleep) : Result<void>::Success();
}

template <I2cPort Port>
Result<void> PCA9685<Port>::writeOffs(uint8_t first, std::span<const Pwm> values) noexcept {
  Result<void> written = Result<void>::Success();
  const std::size_t count = values.size();
  for (std::size_t i = 0U; i < count; ++i) {
    if (IsOff(values[i])) {
      const auto r = writeOff(static_cast<uint8_t>(first + i));
      written = written ? r : written;
    }
  }
  return written;
}

template <I2cPort Port>
Result<void> PCA9685<Port>::writeChannels(uint8_t first, std::span<const Pwm> values) noexcept {
  // All four registers of every channel in one transfer: with OCH = 0 they change together at the
  // STOP, with OCH = 1 on the fourth ACK (section 7.3.2, notes 2 and 3).
  std::array<uint8_t, detail::kMaxBurst> bytes{};
  const std::size_t count = values.size();
  for (std::size_t i = 0U; i < count; ++i) {
    const auto encoded = detail::EncodePwm(values[i]);
    for (std::size_t b = 0U; b < encoded.size(); ++b) {
      bytes[(i * reg::kBytesPerChannel) + b] = encoded[b];
    }
  }
  const uint16_t bits = detail::ChannelRange(first, static_cast<uint32_t>(count));
  const Error e = bus_.Write(reg::ChannelBase(first),
                             std::span<const uint8_t>{bytes}.first(count * reg::kBytesPerChannel));
  if (e == Error::Ok) {
    for (std::size_t i = 0U; i < count; ++i) {
      channels_[first + i] = values[i];
    }
    uncertain_ =
        static_cast<uint16_t>(static_cast<uint32_t>(uncertain_) & ~static_cast<uint32_t>(bits));
    return Result<void>::Success();
  }
  if (e != Error::NoDevice) {
    // The bytes before a NACK are stored: the channels may hold old, new or mixed values.
    uncertain_ =
        static_cast<uint16_t>(static_cast<uint32_t>(uncertain_) | static_cast<uint32_t>(bits));
  }
  return fail(e, "pca9685: channel write failed");
}

template <I2cPort Port>
Result<void> PCA9685<Port>::writeAllOff() noexcept {
  // ALL_LED_OFF_H = 0x10, one data byte (figure 24): every channel's LEDn_OFF_H loaded at once,
  // whatever MODE1.AI holds; full OFF takes precedence over the other three bytes.
  const Error e = writeByte(reg::kAllLedOffH, reg::kLedFull);
  if (e != Error::Ok) {
    return fail(e, "pca9685: ALL_LED_OFF_H write failed"); // each channel as before, or off
  }
  markAllOff();
  uncertain_ = 0U;
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> PCA9685<Port>::readBack(uint8_t first, uint8_t count) noexcept {
  if (const auto modes = readModes(); !modes) {
    return Result<void>::Failure(modes.error);
  }
  std::array<Pwm, kChannelCount> read{};
  if (auto r = readChannelsInto(first, std::span{read}.first(count)); !r) {
    return r;
  }
  bool same = true;
  for (std::size_t i = 0U; i < count; ++i) {
    const Pwm& asked = channels_[first + i];
    // An off value matches any value with full OFF: the other bytes do not reach the output.
    same = same && (IsOff(asked) ? IsOff(read[i]) : (read[i] == asked));
    channels_[first + i] = read[i]; // the chip's value, whatever was asked
  }
  uncertain_ = static_cast<uint16_t>(static_cast<uint32_t>(uncertain_) &
                                     ~static_cast<uint32_t>(detail::ChannelRange(first, count)));
  return same ? Result<void>::Success()
              : fail(Error::WriteMismatch, "pca9685: a channel did not read back");
}

template <I2cPort Port>
Result<void> PCA9685<Port>::readChannelsInto(uint8_t first, std::span<Pwm> out) noexcept {
  // Callers pass at most kChannelCount - first values; one burst needs MODE1.AI.
  std::array<uint8_t, detail::kMaxBurst> bytes{};
  const std::size_t count = out.size();
  const auto window = std::span<uint8_t>{bytes}.first(count * reg::kBytesPerChannel);
  if (const Error e = bus_.Read(reg::ChannelBase(first), window); e != Error::Ok) {
    return fail(e, "pca9685: channel read failed");
  }
  if (auto r = checkZeroBurst(window); !r) {
    return r;
  }
  for (std::size_t i = 0U; i < count; ++i) {
    const auto value =
        detail::DecodePwm(window.subspan(i * reg::kBytesPerChannel).template first<4>());
    if (!value) {
      return fail(value.error, "pca9685: a channel read reserved bits or equal counts");
    }
    out[i] = value.value;
  }
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> PCA9685<Port>::checkZeroBurst(std::span<const uint8_t> window) noexcept {
  // Zeros are what SDA stuck low reads (handbook KB-CPP-116), and also a legal channel value
  // after a switch-on NACKed part-way. Told apart by a second frame that zeros cannot fake: the
  // MODE1/MODE2/SUBADR1 burst, where the sentinels read non-zero on a running chip (rule 12d).
  if (!std::all_of(window.begin(), window.end(),
                   [](uint8_t b) noexcept { return static_cast<uint32_t>(b) == 0U; })) {
    return Result<void>::Success();
  }
  const auto modes = readModeBytes();
  if (!modes) {
    return Result<void>::Failure(modes.error);
  }
  if (looksReset(modes.value)) {
    // A stuck line, or a reset: not data either way. The next reset check reads again and acts.
    return fail(Error::InvalidReply, "pca9685: a channel read all zeros and MODE1 did not agree");
  }
  return Result<void>::Success();
}

template <I2cPort Port>
Result<uint8_t> PCA9685<Port>::readModes() noexcept {
  const auto first = readModeBytes();
  if (!first) {
    return Result<uint8_t>::Failure(first.error);
  }
  if (!looksReset(first.value)) {
    return Result<uint8_t>::Success(first.value[0]);
  }
  // A frame with no integrity check never decides a state change alone (rule 12d): read again.
  const auto second = readModeBytes();
  if (!second) {
    return Result<uint8_t>::Failure(second.error);
  }
  if (!looksReset(second.value)) {
    return Result<uint8_t>::Success(second.value[0]); // the first read was a glitch
  }
  return Result<uint8_t>::Failure(markReset().error);
}

template <I2cPort Port>
Result<uint8_t> PCA9685<Port>::verifyRunning() noexcept {
  const auto modes = readModes();
  if (!modes) {
    return modes;
  }
  auto prescale = readByte(reg::kPreScale);
  if (prescale && prescale_known_ && (prescale.value != prescale_)) {
    prescale = readByte(reg::kPreScale); // confirmed by a second frame before acting (rule 12d)
    if (prescale && (prescale.value != prescale_)) {
      return Result<uint8_t>::Failure(markReset().error); // back at 0x1E, or someone wrote it
    }
  }
  if (!prescale) {
    return Result<uint8_t>::Failure(fail(prescale.error, "pca9685: PRE_SCALE read failed").error);
  }
  prescale_ = prescale.value;
  prescale_known_ = true;
  return modes;
}

template <I2cPort Port>
Result<void> PCA9685<Port>::completeWakeCheck() noexcept {
  if (!wake_check_pending_) {
    return Result<void>::Success();
  }
  const auto modes = verifyRunning();
  if (!modes) {
    return Result<void>::Failure(modes.error); // stays pending
  }
  if (detail::Has(modes.value, reg::kMode1Sleep)) {
    wake_check_pending_ = false;
    asleep_maybe_ = true; // the wake write did not take effect
    return Result<void>::Success();
  }
  if (!detail::Has(modes.value, reg::kMode1Restart)) {
    wake_check_pending_ = false;
    awakeConfirmed();
    return Result<void>::Success();
  }
  // RESTART reads 1 after a sleep with live channels: writing it as 1 restarts them, at least
  // 500 us after SLEEP was cleared (section 7.3.1.1, steps 2 and 3). That write clears SLEEP too,
  // so a chip that reset since the check would wake on it: the check runs once more after it.
  if (const Error e = writeByte(reg::kMode1, detail::With(mode1_, reg::kMode1Restart));
      e != Error::Ok) {
    return fail(e, "pca9685: RESTART write failed");
  }
  wake_acked_ = true; // the RESTART write clears SLEEP too
  const auto again = verifyRunning();
  if (!again) {
    return Result<void>::Failure(again.error); // stays pending
  }
  wake_check_pending_ = false;
  if (detail::Has(again.value, reg::kMode1Sleep)) {
    asleep_maybe_ = true;
  } else {
    awakeConfirmed();
  }
  return Result<void>::Success();
}

template <I2cPort Port>
void PCA9685<Port>::awakeConfirmed() noexcept {
  // Two frames agree: an acknowledged write with SLEEP = 0, then a read with SLEEP = 0. A read
  // alone never clears the doubt (handbook rule 12d): after a failed wake write the chip may still
  // be asleep holding its outputs, until Wake() writes again.
  if (wake_acked_) {
    asleep_maybe_ = false;
    held_maybe_ = false;
  }
  wake_acked_ = false;
}

template <I2cPort Port>
Result<void> PCA9685<Port>::wakeSequence() noexcept {
  // Set before the write: a failed write may have woken the chip, which is then unchecked.
  wake_check_pending_ = true;
  const Error e = writeByte(reg::kMode1, mode1_);
  wake_acked_ = e == Error::Ok;
  // The oscillator needs 500 us (table 5, note 2), also before a later RESTART write; EXTCLK needs
  // no start-up time.
  if (config_.clock == ClockSource::Internal) {
    detail::DelayUs(bus_.GetPort(), kOscillatorStartUs);
  }
  if (e != Error::Ok) {
    return fail(e, "pca9685: MODE1 wake write failed");
  }
  return completeWakeCheck();
}

template <I2cPort Port>
Result<uint8_t> PCA9685<Port>::readByte(uint8_t reg) noexcept {
  std::array<uint8_t, 1> value{};
  if (const Error e = bus_.Read(reg, std::span{value}); e != Error::Ok) {
    return Result<uint8_t>::Failure(e);
  }
  return Result<uint8_t>::Success(value[0]);
}

template <I2cPort Port>
Error PCA9685<Port>::writeByte(uint8_t reg, uint8_t value) noexcept {
  const std::array<uint8_t, 1> data{value};
  return bus_.Write(reg, std::span{data});
}

template <I2cPort Port>
Result<void> PCA9685<Port>::writeOff(uint8_t channel) noexcept {
  // One data byte, LEDn_OFF_H = 0x10: it is stored or it is not, and full OFF takes precedence over
  // everything else the channel holds (table 7 remark). A 4-byte burst NACKed part-way could leave
  // new ON bytes with the old OFF bytes: a channel more on than before (section 7.3.2, note 2).
  const auto reg = static_cast<uint8_t>(static_cast<uint32_t>(reg::ChannelBase(channel)) + 3U);
  const Error e = writeByte(reg, reg::kLedFull);
  if (e != Error::Ok) {
    return fail(e, "pca9685: switch-off write failed"); // the channel is as before, or off
  }
  channels_[channel].full_off = true;
  channels_[channel].off_count =
      static_cast<uint16_t>(static_cast<uint32_t>(channels_[channel].off_count) & 0x00FFU);
  // Off for sure now; its other bytes are what they were.
  uncertain_ = static_cast<uint16_t>(static_cast<uint32_t>(uncertain_) &
                                     ~static_cast<uint32_t>(detail::ChannelBit(channel)));
  return Result<void>::Success();
}

template <I2cPort Port>
Result<std::array<uint8_t, 3>> PCA9685<Port>::readModeBytes() noexcept {
  // MODE1, MODE2 and SUBADR1 in one read. The first byte is MODE1 whatever AI holds; with AI clear
  // the others are MODE1 again, which the AI check catches first.
  using Bytes = std::array<uint8_t, 3>;
  Bytes modes{};
  if (const Error e = bus_.Read(reg::kMode1, std::span{modes}); e != Error::Ok) {
    return Result<Bytes>::Failure(fail(e, "pca9685: MODE1/MODE2 read failed").error);
  }
  last_mode1_ = modes[0];
  last_mode2_ = modes[1];
  // With AI clear every byte is MODE1 (a reset sentinel itself); with AI set the read-only bits of
  // MODE2 and SUBADR1 read 0.
  const bool valid = detail::Has(modes[0], reg::kMode1Ai)
                         ? !(detail::Has(modes[1], reg::kMode2Reserved) ||
                             detail::Has(modes[2], reg::kSubAdrReadOnly))
                         : ((modes[1] == modes[0]) && (modes[2] == modes[0]));
  if (!valid) {
    return Result<Bytes>::Failure(
        fail(Error::InvalidReply, "pca9685: MODE1/MODE2/SUBADR1 read no PCA9685 sends").error);
  }
  return Result<Bytes>::Success(modes);
}

template <I2cPort Port>
bool PCA9685<Port>::addressValid() const noexcept {
  const uint8_t address = bus_.GetAddress();
  return (address >= kMinI2cAddress) && (address <= kMaxI2cAddress) && (address != kAllCallAddress);
}

template <I2cPort Port>
bool PCA9685<Port>::looksReset(const std::array<uint8_t, 3>& modes) const noexcept {
  // The sentinels: the driver sets AI and never clears it, and writes MODE2, EXTCLK and SUBADR1
  // only at start-up; a reset loads AI = 0, MODE2 = 0x04, EXTCLK = 0, SUBADR1 = 0xE2 (7.5, 7.6).
  const bool external = config_.clock == ClockSource::External;
  return !detail::Has(modes[0], reg::kMode1Ai) || (modes[1] != mode2_) ||
         (modes[2] != detail::kSubAdr1Sentinel) ||
         (detail::Has(modes[0], reg::kMode1ExtClk) != external);
}

template <I2cPort Port>
void PCA9685<Port>::markAllOff() noexcept {
  // What ALL_LED_OFF_H = 0x10 leaves in the copy: full OFF, OFF count bits 11..8 zero.
  for (Pwm& channel : channels_) {
    channel.full_off = true;
    channel.off_count = static_cast<uint16_t>(static_cast<uint32_t>(channel.off_count) & 0x00FFU);
  }
}

template <I2cPort Port>
Result<void> PCA9685<Port>::markReset() noexcept {
  // Two reads agree: act. Every output off, by OE and by the one-byte ALL_LED_OFF_H (it lands
  // whatever AI holds), then uninitialised. The switch-off calls keep working from here.
  initialized_ = false;
  if constexpr (detail::HasGpioSet<Port>) {
    if (!bus_.GetPort().GpioSet(CtrlPin::OutputEnable, GpioSignal::INACTIVE)) {
      detail::Log(bus_.GetPort(), LogLevel::Error, "pca9685: OE pin write failed");
    }
  }
  if (writeByte(reg::kAllLedOffH, reg::kLedFull) == Error::Ok) {
    markAllOff();
  }
  asleep_maybe_ = true;
  wake_check_pending_ = false;
  detail::Log(bus_.GetPort(), LogLevel::Warning, "pca9685: the chip reset; call EnsureInitialized");
  return Result<void>::Failure(Error::DeviceReset);
}

template <I2cPort Port>
Result<void> PCA9685<Port>::fail(Error error, const char* message) noexcept {
  detail::Log(bus_.GetPort(), LogLevel::Error, message);
  return Result<void>::Failure(error);
}

} // namespace pca9685

#endif // PCA9685_IPP
