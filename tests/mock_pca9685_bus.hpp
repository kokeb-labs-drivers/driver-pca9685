/**
 * @file mock_pca9685_bus.hpp
 * @brief I2C mock ports over the register model, with fault injection. The transaction decoding
 * here is the only bus-specific part of the host tests.
 *
 * | Port | Hooks |
 * | --- | --- |
 * | `MinimalPort` | the required ones plus `DelayUs()` (the oscillator wait); OE wired low |
 * | `FullPort` | also `GpioSet()` (OE), `GetLastFailure()` and `Log()` |
 */
#ifndef PCA9685_MOCK_PCA9685_BUS_HPP
#define PCA9685_MOCK_PCA9685_BUS_HPP

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "mock_pca9685.hpp" // the model; it includes this file at its end, the guards make either order work
#include "pca9685_i2c_interface.hpp"
#include "pca9685_types.hpp"

namespace pca9685_test {

/// One transfer as the master sent it.
struct Frame {
  bool write = true;          ///< Write(), or WriteRead().
  uint8_t addr = 0;           ///< 7-bit address.
  std::vector<uint8_t> bytes; ///< The payload (a write) or the control register (a read).
  std::size_t rx_len = 0;     ///< Bytes read.
};

/// What a read returns when a fault replaces the chip's answer.
enum class ReadFault : uint8_t {
  None,     ///< The chip answers.
  StuckLow, ///< SDA held low: every bit 0, every byte acknowledged.
  Floating, ///< SDA high through the data bytes: every byte 0xFF (an ACK glitch or another part).
};

/// Shared state of the mock ports: the chip, fault injection and the transfer log.
struct MockBusState {
  Pca9685Model chip;
  bool bus_up_ok = true;      ///< EnsureInitialized() result.
  int fail_transfer = -1;     ///< Address NACK on this transfer index (0-based); -1 never.
  int fail_from = -1;         ///< Address NACK on every transfer from this index; -1 never.
  int nack_transfer = -1;     ///< Data NACK on this transfer...
  std::size_t nack_after = 0; ///< ...after this many payload bytes (the control register is 1).
  int lost_ack_transfer = -1; ///< The transfer completes on the chip but is reported failed.
  int reset_before = -1;      ///< Power-on reset just before this transfer.
  int corrupt_transfer = -1;  ///< A write stored with its last byte XORed with `corrupt_mask`.
  uint8_t corrupt_mask = 0x01;
  int drop_transfer = -1;            ///< A write acknowledged but never stored (noise on the bus).
  int corrupt_read = -1;             ///< A read whose byte `corrupt_read_byte`...
  std::size_t corrupt_read_byte = 0; ///< ...is XORed with `corrupt_read_mask` (noise).
  uint8_t corrupt_read_mask = 0x01;
  ReadFault read_fault = ReadFault::None; ///< What reads return...
  int read_fault_from = 0;                ///< ...from this transfer index on.
  pca9685::I2cFailure last_failure = pca9685::I2cFailure::Other;
  int transfers = 0;
  std::vector<Frame> log;

  [[nodiscard]] bool Fails(int index) const noexcept {
    return (index == fail_transfer) || ((fail_from >= 0) && (index >= fail_from));
  }

  bool DoWrite(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    const int index = transfers++;
    log.push_back(Frame{true, addr, std::vector<uint8_t>(data, data + len), 0});
    if (index == reset_before) {
      chip.PowerOnReset();
    }
    if (Fails(index)) {
      last_failure = pca9685::I2cFailure::AddressNack;
      chip.Elapse(Pca9685Model::kFrameUs);
      return false;
    }
    if (index == drop_transfer) {
      chip.Elapse(Pca9685Model::kFrameUs);
      return true;
    }
    std::vector<uint8_t> payload(data, data + len);
    if ((index == corrupt_transfer) && (len > 1U)) {
      payload[len - 1U] = static_cast<uint8_t>(payload[len - 1U] ^ corrupt_mask);
    }
    const std::size_t limit = (index == nack_transfer) ? nack_after : len;
    const int acked = chip.Write(addr, payload.data(), len, limit);
    if (acked < 0) {
      last_failure = pca9685::I2cFailure::AddressNack;
      return false;
    }
    if (std::cmp_less(acked, len)) {
      last_failure = pca9685::I2cFailure::DataNack;
      return false;
    }
    if (index == lost_ack_transfer) {
      last_failure = pca9685::I2cFailure::Other;
      return false;
    }
    return true;
  }

  bool DoWriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                   std::size_t rx_len) noexcept {
    const int index = transfers++;
    log.push_back(Frame{false, addr, std::vector<uint8_t>(tx, tx + tx_len), rx_len});
    if (index == reset_before) {
      chip.PowerOnReset();
    }
    if (Fails(index) || (tx_len != 1U)) {
      last_failure = pca9685::I2cFailure::AddressNack;
      return false;
    }
    if ((read_fault != ReadFault::None) && (index >= read_fault_from)) {
      for (std::size_t i = 0; i < rx_len; ++i) {
        rx[i] = (read_fault == ReadFault::Floating) ? uint8_t{0xFF} : uint8_t{0x00};
      }
      return true; // the line, not the chip, made the bytes
    }
    if (!chip.Read(addr, tx[0], rx, rx_len)) {
      last_failure = pca9685::I2cFailure::AddressNack;
      return false;
    }
    if ((index == corrupt_read) && (corrupt_read_byte < rx_len)) {
      rx[corrupt_read_byte] = static_cast<uint8_t>(rx[corrupt_read_byte] ^ corrupt_read_mask);
    }
    return true;
  }

  /// The write frames to register `reg`, in order.
  [[nodiscard]] int WritesTo(uint8_t reg) const noexcept {
    int n = 0;
    for (const Frame& f : log) {
      n += (f.write && !f.bytes.empty() && (f.bytes[0] == reg)) ? 1 : 0;
    }
    return n;
  }
};

/// The required I2C hooks over a MockBusState. Derive `class P : public MockPortBase<P>`.
template <typename Self>
class MockPortBase : public pca9685::I2cInterface<Self> {
public:
  MockBusState state;
  int bus_up_calls = 0;

  bool EnsureInitialized() noexcept {
    bus_up_calls++;
    return state.bus_up_ok;
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    return state.DoWrite(addr, data, len);
  }
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    return state.DoWriteRead(addr, tx, tx_len, rx, rx_len);
  }
};

/// Required hooks plus the delay; time passes in the model as the driver waits. OE is wired low.
class MinimalPort : public MockPortBase<MinimalPort> {
public:
  uint64_t delayed_us = 0; ///< Sum of DelayUs() arguments.

  void DelayUs(uint32_t us) noexcept {
    delayed_us += us;
    state.chip.Elapse(us);
  }
};

/// Every optional hook, with counters. OE starts high (pulled up, the MCU not driving yet).
class FullPort : public MockPortBase<FullPort> {
public:
  uint64_t delayed_us = 0; ///< Sum of DelayUs() arguments.
  int log_lines = 0;       ///< Log() calls.
  int error_lines = 0;     ///< Log() calls at LogLevel::Error.
  bool gpio_ok = true;     ///< GpioSet() result.
  int gpio_writes = 0;     ///< GpioSet() calls.

  FullPort() noexcept {
    state.chip.oe_high = true;
  }

  void DelayUs(uint32_t us) noexcept {
    delayed_us += us;
    state.chip.Elapse(us);
  }
  bool GpioSet(pca9685::CtrlPin pin, pca9685::GpioSignal signal) noexcept {
    gpio_writes++;
    if (!gpio_ok || (pin != pca9685::CtrlPin::OutputEnable)) {
      return false;
    }
    state.chip.oe_high = signal == pca9685::GpioSignal::INACTIVE;
    return true;
  }
  bool GetLastFailure(pca9685::I2cFailure& failure) noexcept {
    failure = state.last_failure;
    return true;
  }
  void Log(pca9685::LogLevel level, const char* message) noexcept {
    (void)message;
    log_lines++;
    if (level == pca9685::LogLevel::Error) {
      error_lines++;
    }
  }
};

} // namespace pca9685_test

#endif // PCA9685_MOCK_PCA9685_BUS_HPP
