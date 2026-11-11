/**
 * @file fuzz_pca9685.cpp
 * @brief Fuzz target: the driver over a bus and a chip whose behaviour the fuzzer controls, with
 * the safety rules as invariants.
 *
 * The fuzzer's bytes choose a configuration, then a sequence of driver calls; on every transfer
 * they also choose what the bus does: pass it to the chip model, NACK the address, NACK a data
 * byte part-way (the bytes before it are stored), lose the ACK of a completed write, drop or
 * corrupt an acknowledged write, flip or replace the bytes of a read, or reset the chip just
 * before it. Between calls they can reset the chip (power-on or the general-call software reset),
 * flip any reading of the data sheet's open questions, let another master write through the LED
 * All Call address, or take the bus down.
 *
 * After every call the target checks the driver's promises (docs/contract.md), and aborts, naming
 * the line, when one breaks:
 *
 * - a call returns an `Error` the enum defines; a failed value is `T{}`;
 * - before `EnsureInitialized()` succeeds, no call but `DisableOutputs()` makes a transfer;
 * - `InvalidArgument` means nothing was sent, except a bank call's switch-offs;
 * - `DeviceReset` leaves the driver uninitialised;
 * - a switch-off on a driver that has started once is always sent (one write per channel asked
 *   for), also after a reset was found;
 * - a switch-off, failed or not, never leaves a channel more active than before (live duty on the
 *   chip), unless a write of the run was acknowledged and then dropped or corrupted;
 * - an `Ok` `Deinitialize()` leaves every channel inactive and OE high;
 * - an `Ok` switch-off leaves the channel inactive on the chip, unless a write of the run was
 *   acknowledged and then dropped or corrupted (outside what I2C promises);
 * - an `Ok` `DisableOutputs()` leaves OE high;
 * - after an `Ok` call that ran the wake check, a running chip holds the driver's MODE2 and
 *   prescaler, unless a read of the run was tampered with;
 * - every log line is a non-empty plain-text message.
 *
 * Built two ways (tests/fuzz/CMakeLists.txt): with libFuzzer (Clang, `-DPCA9685_BUILD_FUZZERS=ON`),
 * and as a plain executable that replays the corpus in tests/fuzz/corpus/ as a CTest on any
 * compiler.
 */
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "mock_pca9685.hpp" // the chip model, then the bus mock in mock_pca9685_bus.hpp
#include "pca9685.hpp"

namespace {

using pca9685::Error;
using pca9685::Pwm;

/// The fuzzer's bytes, read front to back; zeros once they run out.
class Input {
public:
  Input(const uint8_t* data, std::size_t size) noexcept : data_(data), size_(size) {}

  [[nodiscard]] bool Empty() const noexcept {
    return pos_ >= size_;
  }
  uint8_t Byte() noexcept {
    return pos_ < size_ ? data_[pos_++] : uint8_t{0};
  }
  uint16_t Word() noexcept {
    const auto high = Byte();
    return static_cast<uint16_t>((static_cast<uint32_t>(high) << 8U) | Byte());
  }

private:
  const uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
};

/// A broken promise: name the line and abort, so libFuzzer saves the input as a crash.
void Check(bool promise, int line = __builtin_LINE()) noexcept {
  if (!promise) {
    (void)std::fprintf(stderr, "fuzz_pca9685: promise broken at line %d\n", line);
    std::abort();
  }
}

/// What the bus does with one transfer, chosen by the fuzzer; any other value passes it.
constexpr uint8_t kFail = 0xFF;     ///< Address NACK: nothing reaches the chip.
constexpr uint8_t kNack = 0xFE;     ///< A data byte NACKed part-way: the bytes before it stored.
constexpr uint8_t kLostAck = 0xFD;  ///< A write completes on the chip but is reported failed.
constexpr uint8_t kDrop = 0xFC;     ///< A write acknowledged and dropped; a read's bits flipped.
constexpr uint8_t kReplace = 0xFB;  ///< A write stored corrupted; a read's bytes replaced.
constexpr uint8_t kResetNow = 0xFA; ///< A power-on reset just before the transfer.

/// The port: the chip model behind a bus the fuzzer controls.
class FuzzPort : public pca9685::I2cInterface<FuzzPort> {
public:
  explicit FuzzPort(Input& in) noexcept : in_(in) {}

  [[nodiscard]] bool EnsureInitialized() const noexcept {
    return bus_up_ok_;
  }

  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    Check(data != nullptr && len >= 2 && len <= 1 + pca9685::detail::kMaxBurst);
    if ((data == nullptr) || (len < 2U) || (len > (1U + pca9685::detail::kMaxBurst))) {
      return false; // unreachable: Check() aborted
    }
    const uint8_t action = nextAction();
    if (action == kFail) {
      last_ = pca9685::I2cFailure::AddressNack;
      return false;
    }
    if (action == kDrop) {
      untrusted_write_ = true;
      return true; // acknowledged, never stored
    }
    std::array<uint8_t, 1 + pca9685::detail::kMaxBurst> payload{};
    for (std::size_t i = 0; i < len; ++i) {
      payload[i] = data[i];
    }
    if (action == kReplace) {
      untrusted_write_ = true;
      payload[len - 1U] = static_cast<uint8_t>(payload[len - 1U] ^ (in_.Byte() | 1U));
    }
    const std::size_t limit = (action == kNack) ? (in_.Byte() % len) : len;
    const int acked = chip_.Write(addr, payload.data(), len, limit);
    if (acked < 0) {
      last_ = pca9685::I2cFailure::AddressNack;
      return false;
    }
    last_ = pca9685::I2cFailure::DataNack;
    return std::cmp_equal(acked, len) && (action != kLostAck);
  }

  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    Check(tx != nullptr && rx != nullptr && tx_len == 1 && rx_len >= 1 &&
          rx_len <= pca9685::detail::kMaxBurst);
    if ((tx == nullptr) || (rx == nullptr) || (rx_len > pca9685::detail::kMaxBurst)) {
      return false; // unreachable: Check() aborted
    }
    const uint8_t action = nextAction();
    if ((action == kFail) || !chip_.Read(addr, tx[0], rx, rx_len)) {
      last_ = pca9685::I2cFailure::AddressNack;
      return false;
    }
    for (std::size_t i = 0; i < rx_len; ++i) {
      if (action == kDrop) {
        rx[i] = static_cast<uint8_t>(rx[i] ^ in_.Byte());
        tampered_ = true;
      } else if (action == kReplace) {
        rx[i] = in_.Byte();
        tampered_ = true;
      } else {
        // The chip's byte, unchanged.
      }
    }
    return true;
  }

  void DelayUs(uint32_t us) noexcept {
    chip_.Elapse(us);
  }

  /// OE; the fuzzer may fail the pin write.
  bool GpioSet(pca9685::CtrlPin pin, pca9685::GpioSignal signal) noexcept {
    Check(pin == pca9685::CtrlPin::OutputEnable);
    ++transfers_;
    if (in_.Byte() == kFail) {
      return false;
    }
    chip_.oe_high = signal == pca9685::GpioSignal::INACTIVE;
    return true;
  }

  bool GetLastFailure(pca9685::I2cFailure& failure) const noexcept {
    failure = last_;
    return true;
  }

  /// The driver logs plain text: one non-empty line, never a format string.
  void Log(pca9685::LogLevel level, const char* message) noexcept {
    (void)level;
    Check(message != nullptr && message[0] != '\0');
  }

  pca9685_test::Pca9685Model& Chip() noexcept {
    return chip_;
  }
  bool started_once = false; ///< The harness saw the driver started.

  [[nodiscard]] int Transfers() const noexcept {
    return transfers_;
  }
  void SetBusUp(bool ok) noexcept {
    bus_up_ok_ = ok;
  }
  [[nodiscard]] bool UntrustedWrite() const noexcept {
    return untrusted_write_;
  }
  [[nodiscard]] bool Tampered() const noexcept {
    return tampered_;
  }

private:
  uint8_t nextAction() noexcept {
    ++transfers_;
    const uint8_t action = in_.Byte();
    if (action == kResetNow) {
      chip_.PowerOnReset();
    }
    return action;
  }

  Input& in_;
  pca9685_test::Pca9685Model chip_;
  pca9685::I2cFailure last_ = pca9685::I2cFailure::Other;
  int transfers_ = 0;
  bool bus_up_ok_ = true;
  bool untrusted_write_ = false;
  bool tampered_ = false;
};

using Driver = pca9685::PCA9685<FuzzPort>;

bool KnownError(Error e) noexcept {
  switch (e) {
  case Error::Ok:
  case Error::NotInitialized:
  case Error::InvalidArgument:
  case Error::Bus:
  case Error::DeviceIdMismatch:
  case Error::Unsupported:
  case Error::WriteMismatch:
  case Error::NoDevice:
  case Error::DeviceReset:
  case Error::InvalidReply:
  case Error::Asleep:
  case Error::StateUnknown:
  case Error::ExternalClockLatched:
    return true;
  case Error::Timeout:
  case Error::Crc:
  case Error::Busy:
  case Error::DeviceFault:
  default:
    break; // never returned by this driver
  }
  return false;
}

/// A configuration from the fuzzer's bytes; sometimes invalid on purpose.
pca9685::Config FuzzConfig(Input& in) noexcept {
  pca9685::Config c{};
  const uint8_t flags = in.Byte();
  c.invert_outputs = (flags & 0x01U) != 0U;
  c.output_drive = static_cast<pca9685::OutputDrive>((flags >> 1U) % 3U); // 2: not an enumerator
  c.clock = static_cast<pca9685::ClockSource>((flags >> 5U) % 3U);
  if ((flags & 0x80U) != 0U) {
    c.pwm_frequency_hz = in.Word() % 2000U; // 0 and above 1526 Hz are invalid at 25 MHz
    c.oscillator_hz = 1'000'000U * (in.Byte() % 52U);
  }
  return c;
}

/// A channel value; often invalid (counts above 4095, equal counts).
Pwm FuzzPwm(Input& in) noexcept {
  const uint8_t flags = in.Byte();
  return Pwm{.on_count = static_cast<uint16_t>(in.Word() & 0x1FFFU),
             .off_count = static_cast<uint16_t>(in.Word() & 0x1FFFU),
             .full_on = (flags & 1U) != 0U,
             .full_off = (flags & 2U) != 0U};
}

pca9685::Verify FuzzVerify(Input& in) noexcept {
  return static_cast<pca9685::Verify>(in.Byte() % 3U); // 2: not an enumerator
}

/// Change the chip between two calls.
void FuzzChip(pca9685_test::Pca9685Model& chip, Input& in) noexcept {
  switch (in.Byte() % 6U) {
  case 0:
    chip.PowerOnReset(); // a supply dip below V_POR
    break;
  case 1: {
    const std::array<uint8_t, 1> swrst{0x06}; // another master's general-call software reset
    (void)chip.Write(0x00, swrst.data(), swrst.size(), swrst.size());
    break;
  }
  case 2: {
    // Another master through the LED All Call address: only a chip still answering it changes.
    const std::array<uint8_t, 2> poke{static_cast<uint8_t>(0x06U + (in.Byte() % 64U)), in.Byte()};
    (void)chip.Write(0x70, poke.data(), poke.size(), poke.size());
    break;
  }
  case 3:
    chip.readings.sleep_holds_outputs = !chip.readings.sleep_holds_outputs;
    break;
  case 4:
    chip.readings.ack_needs_all_four = !chip.readings.ack_needs_all_four;
    break;
  default:
    chip.readings.wake_runs_without_restart = !chip.readings.wake_runs_without_restart;
    break;
  }
}

/// What one call was, for the checks after it.
struct Call {
  bool was_up = false;
  int before = 0;
  uint16_t off_mask = 0;                  ///< Channels the call switches off.
  bool off_only = false;                  ///< The call is a switch-off (never refused).
  bool wake_checked = false;              ///< The call runs the wake check before an Ok.
  bool io_before_start = false;           ///< The call may make transfers before the start.
  bool bank_with_offs = false;            ///< A bank call that sends its switch-offs on a refusal.
  bool started_once = false;              ///< The driver had started at least once before the call.
  bool deinit = false;                    ///< The call was Deinitialize().
  std::array<uint32_t, 16> duty_before{}; ///< Live duty of every channel before the call.
};

/// The live duty of a channel on the chip, 0 to 4096 counts (what reaches the output logic).
uint32_t LiveDuty(const pca9685_test::Pca9685Model& chip, uint32_t ch) noexcept {
  const auto b = chip.Live(ch);
  if ((b[3] & 0x10U) != 0U) {
    return 0U;
  }
  if ((b[1] & 0x10U) != 0U) {
    return 4096U;
  }
  const uint32_t on = ((b[1] & 0x0FU) << 8U) | b[0];
  const uint32_t off = ((b[3] & 0x0FU) << 8U) | b[2];
  return (on == off) ? 0U : ((off - on) & 0x0FFFU);
}

void CheckAfter(const Driver& d, FuzzPort& port, const Call& c, Error e) noexcept {
  const int sent = port.Transfers() - c.before;
  Check(KnownError(e));
  // Once started, switch-offs go out also while uninitialised (a reset was found).
  const bool offs_allowed = c.started_once && ((c.off_mask != 0U) || c.bank_with_offs);
  if (!c.was_up && !c.io_before_start && !offs_allowed) {
    Check(sent == 0);
  }
  if ((e == Error::InvalidArgument) && !c.bank_with_offs) {
    Check(sent == 0);
  }
  if (e == Error::DeviceReset) {
    Check(!d.IsInitialized());
  }
  if ((c.was_up || c.started_once) && c.off_only) {
    // Never refused: one write per channel at least (SetAllOff: one for all).
    const int asked = (c.off_mask == 0xFFFFU) ? 1 : std::popcount(c.off_mask);
    Check(sent >= asked);
  }
  if ((c.off_mask != 0U) && !port.UntrustedWrite()) {
    for (uint32_t ch = 0; ch < 16U; ++ch) {
      if ((c.off_mask & (1U << ch)) != 0U) {
        Check(LiveDuty(port.Chip(), ch) <= c.duty_before[ch]); // B1: never more on
        Check((e != Error::Ok) || !port.Chip().ChannelActive(ch));
      }
    }
  }
  if (c.deinit && (e == Error::Ok) && !port.UntrustedWrite()) {
    for (uint32_t ch = 0; ch < 16U; ++ch) {
      Check(!port.Chip().ChannelActive(ch));
    }
    Check(port.Chip().oe_high);
  }
  if ((e == Error::Ok) && c.wake_checked && d.IsInitialized() && !port.Chip().Asleep() &&
      !port.Tampered()) {
    const pca9685::Config& cfg = d.GetConfig();
    Check(port.Chip().Reg(0x01) == pca9685::detail::Mode2Value(cfg));
    const auto prescale = d.GetPrescale();
    if (prescale) {
      Check(port.Chip().Reg(0xFE) == prescale.value);
    }
  }
}

template <typename T>
void CheckValue(const pca9685::Result<T>& r) noexcept {
  if (!r) {
    Check(r.value == T{});
  }
}

Error RunChannelCall(Driver& d, Input& in, uint8_t op, Call& c) noexcept {
  const auto ch = static_cast<uint8_t>(in.Byte() % 17U); // 16 is out of range
  const uint16_t bit = (ch < 16U) ? static_cast<uint16_t>(1U << ch) : uint16_t{0};
  switch (op) {
  case 0: {
    const Pwm value = FuzzPwm(in);
    const auto verify = FuzzVerify(in);
    c.off_only = value.full_off && (bit != 0U);
    c.off_mask = value.full_off ? bit : uint16_t{0};
    c.wake_checked = true;
    return d.SetPwm(ch, value, verify).error;
  }
  case 1: {
    const uint16_t duty = in.Word() % 4100U;
    const uint16_t phase = in.Word() % 4100U;
    c.off_mask = ((duty == 0U) && (phase <= 4095U)) ? bit : uint16_t{0};
    c.off_only = c.off_mask != 0U;
    c.wake_checked = true;
    return d.SetDuty(ch, duty, phase).error;
  }
  case 2: {
    const uint32_t width = in.Word();
    c.off_mask = (width == 0U) ? bit : uint16_t{0};
    c.off_only = c.off_mask != 0U;
    c.wake_checked = true;
    const auto r = d.SetPulseWidthUs(ch, width);
    CheckValue(r);
    return r.error;
  }
  case 3:
    c.wake_checked = true;
    return d.SetFullOn(ch, FuzzVerify(in)).error;
  default: {
    const auto verify = FuzzVerify(in);
    c.off_mask = bit;
    c.off_only = bit != 0U;
    c.wake_checked = true;
    return d.SetChannelOff(ch, verify).error;
  }
  }
}

Error RunBankCall(Driver& d, Input& in, uint8_t op, Call& c) noexcept {
  switch (op) {
  case 0: {
    const auto first = static_cast<uint8_t>(in.Byte() % 17U);
    const std::size_t count = in.Byte() % 18U;
    std::array<Pwm, 17> values{};
    for (std::size_t i = 0; i < count; ++i) {
      values[i] = FuzzPwm(in);
      if (values[i].full_off && ((first + i) < 16U)) {
        c.off_mask = static_cast<uint16_t>(c.off_mask | (1U << (first + i)));
      }
    }
    c.bank_with_offs = c.off_mask != 0U;
    c.wake_checked = true;
    return d.SetChannels(first, std::span<const Pwm>{values}.first(count)).error;
  }
  case 1:
    c.off_mask = in.Word();
    c.off_only = c.off_mask != 0U;
    c.wake_checked = c.off_mask != 0U; // a mask of 0 sends nothing and checks nothing
    return d.SetChannelsOff(c.off_mask).error;
  default:
    c.off_mask = 0xFFFFU;
    c.off_only = true;
    c.wake_checked = true;
    return d.SetAllOff().error;
  }
}

Error RunOther(Driver& d, FuzzPort& port, Input& in, uint8_t op, Call& c) noexcept {
  switch (op) {
  case 0: {
    c.io_before_start = true;
    const auto r = d.EnsureInitialized();
    if (r) {
      Check(d.IsInitialized());
    }
    if (!c.was_up) {
      Check(r.ok() == d.IsInitialized());
    }
    return r.error;
  }
  case 1: {
    c.io_before_start = true;
    c.deinit = true;
    const auto r = d.Deinitialize();
    Check(!d.IsInitialized());
    return r.error;
  }
  case 2:
    c.io_before_start = c.was_up;
    return d.ApplyConfig(FuzzConfig(in)).error;
  case 3: {
    const auto r = d.ReadChannel(static_cast<uint8_t>(in.Byte() % 17U));
    CheckValue(r);
    return r.error;
  }
  case 4: {
    const auto r = d.ReadChannels();
    return r.error;
  }
  case 5: {
    c.io_before_start = true;
    const auto r = d.DisableOutputs();
    if (r) {
      Check(port.Chip().oe_high);
    }
    return r.error;
  }
  case 6:
    c.wake_checked = true;
    return d.EnableOutputs().error;
  case 7:
    c.wake_checked = true;
    return d.SetPwmFrequency(in.Word() % 2000U).error;
  case 8: {
    const auto r = d.ReadPrescale();
    CheckValue(r);
    return r.error;
  }
  case 9:
    return d.Sleep(static_cast<pca9685::SleepMode>(in.Byte() % 3U)).error;
  case 10:
    c.wake_checked = true;
    return d.Wake().error;
  case 11:
    c.wake_checked = true;
    return d.CheckForReset().error;
  case 12: {
    const auto r = d.ReadRegister(in.Byte());
    CheckValue(r);
    return r.error;
  }
  case 13: {
    const auto r = d.GetChannel(static_cast<uint8_t>(in.Byte() % 17U));
    if (r) {
      Check((r.value.on_count <= pca9685::kMaxCount) && (r.value.off_count <= pca9685::kMaxCount));
    }
    CheckValue(r);
    return r.error;
  }
  case 14:
    FuzzChip(port.Chip(), in);
    return Error::Ok;
  default:
    port.SetBusUp((in.Byte() & 0x07U) != 0U);
    return Error::Ok;
  }
}

void RunOne(Driver& d, FuzzPort& port, Input& in) noexcept {
  const uint8_t op = in.Byte() % 24U;
  Call c{};
  c.was_up = d.IsInitialized();
  c.started_once = port.started_once;
  c.before = port.Transfers();
  for (uint32_t ch = 0; ch < 16U; ++ch) {
    c.duty_before[ch] = LiveDuty(port.Chip(), ch);
  }
  Error e = Error::Ok;
  if (op < 5U) {
    e = RunChannelCall(d, in, op, c);
  } else if (op < 8U) {
    e = RunBankCall(d, in, static_cast<uint8_t>(op - 5U), c);
  } else {
    e = RunOther(d, port, in, static_cast<uint8_t>(op - 8U), c);
    if (op >= 22U) {
      c.before = port.Transfers(); // a chip or bus event is not a driver call
    }
  }
  CheckAfter(d, port, c, e);
  port.started_once = port.started_once || d.IsInitialized();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
  Input in(data, size);
  FuzzPort port(in);
  const uint8_t address = ((in.Byte() & 0x0FU) == 0U) ? uint8_t{0x70} : pca9685::kI2cAddress;
  Driver driver(port, address, FuzzConfig(in));
  for (int calls = 0; calls < 64 && !in.Empty(); ++calls) {
    RunOne(driver, port, in);
  }
  return 0;
}
