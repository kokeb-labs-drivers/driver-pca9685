// clang-format off: the hook table below must keep one row per line.
/**
 * @file pca9685_i2c_interface.hpp
 * @brief The I2C port of the PCA9685 driver: the bus requirements, the CRTP base, the `I2cPort`
 * concept, the optional port hooks and the register framing.
 * @copyright Copyright (c) 2026 The driver-pca9685 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * The driver never calls an SDK. It calls hooks on a class you derive from
 * `pca9685::I2cInterface<Self>`:
 *
 * | Hook | Contract |
 * | --- | --- |
 * | `bool EnsureInitialized() noexcept` | Bring the bus up on the first call; `true` at once afterwards |
 * | `bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept` | START, `addr` + W, `len` bytes, STOP; `false` on a NACK or bus error. After a NACK the port still sends the STOP |
 * | `bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx, std::size_t rx_len) noexcept` | START, `addr` + W, `tx`, repeated START, `addr` + R, `rx_len` bytes (NACK on the last), STOP |
 * | `void DelayUs(uint32_t us) noexcept` | Wait at least `us` microseconds: the 500 us oscillator start-up |
 * | `bool GpioSet(CtrlPin pin, GpioSignal signal) noexcept` (optional) | Drive the OE pin: `ACTIVE` enables the outputs (OE low), `INACTIVE` disables them (OE high) |
 * | `bool GetLastFailure(I2cFailure& failure) noexcept` (optional) | After `Write()` or `WriteRead()` returned `false`: why. `true` and `failure` set when the port knows |
 * | `void Log(LogLevel level, const char* message) noexcept` (optional) | One plain-text line per lifecycle event or failure |
 *
 * `addr` is the 7-bit address. The port moves raw bytes: the register address travels in the
 * payload, and the port never retries or configures the chip. What the chip needs from the bus is
 * below as `constexpr` values (`kI2cAddress`, `kMaxClockHz`, `kRepeatedStart`); a port checks its
 * settings against them with `static_assert`.
 *
 * Section, table and figure numbers refer to the NXP PCA9685 product data sheet, Rev. 4
 * (16 April 2015).
 */
// clang-format on
#ifndef PCA9685_I2C_INTERFACE_HPP
#define PCA9685_I2C_INTERFACE_HPP

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "pca9685_detail.hpp"
#include "pca9685_types.hpp"

namespace pca9685 {

/**
 * @name Bus requirements
 * What the PCA9685 needs from the I2C bus. A port and a board configuration check their settings
 * against these with `static_assert` (handbook KB-CPP-123).
 * @{
 */
/// 7-bit I2C address with A5 to A0 low, 1000000 (section 7.1.1, figure 4); the pins add 0 to 63.
inline constexpr uint8_t kI2cAddress = kMinI2cAddress;
/// Highest SCL frequency: Fast-mode Plus, 1 MHz (table 15, f_SCL).
inline constexpr uint32_t kMaxClockHz = 1'000'000U;
/// `true`: a register read is a combined write-read with a repeated START (figure 22): the
/// control register is written, then the address is sent again with R/W = 1.
inline constexpr bool kRepeatedStart = true;
/** @} */

/** @brief Control pins the port may drive through the optional `GpioSet()` hook. */
enum class CtrlPin : uint8_t {
  OutputEnable = 0, ///< OE, active low on the package pin (section 7.4). INACTIVE (pin high)
                    ///< sets every output to the MODE2.OUTNE level, which the driver makes the
                    ///< level of a channel that is off; ACTIVE (pin low) lets the channels drive.
};

/** @brief Logical level of a control pin; the port maps it to the physical polarity. */
enum class GpioSignal : uint8_t {
  INACTIVE = 0, ///< The pin function is deasserted: for OE, outputs disabled (pin high).
  ACTIVE = 1,   ///< The pin function is asserted: for OE, outputs enabled (pin low).
};

/**
 * @brief Why a `Write()` or `WriteRead()` hook returned `false`, as the optional `GetLastFailure()`
 * hook reports it.
 *
 * Only `AddressNack` changes what the driver returns: nothing acknowledged the address byte, so
 * nothing reached the chip, and the driver returns `Error::NoDevice` and keeps its copy. Every
 * other failure, and every failure of a port without the hook, is `Error::Bus`, and the bytes the
 * chip acknowledged before it may have been stored.
 */
enum class I2cFailure : uint8_t {
  Other = 0,       ///< Arbitration lost, a stuck line, a timeout, or a NACK the port cannot place.
  AddressNack = 1, ///< No device acknowledged the address byte.
  DataNack = 2,    ///< The device acknowledged its address, then not a data byte.
};

/**
 * @brief CRTP base of the platform I2C port.
 *
 * Derive `class MyPort : public pca9685::I2cInterface<MyPort>` and implement the hooks in this
 * file's description. The base declares no hook itself (a forwarding hook would turn a missing
 * override into infinite recursion) and no data; the driver checks your class with the `I2cPort`
 * concept, so a missing or misdeclared hook is a compile error that names it.
 *
 * @tparam Derived Your port class.
 */
template <typename Derived>
class I2cInterface {
public:
  /** @brief Ports are not copied: the driver holds a reference. */
  I2cInterface(const I2cInterface&) = delete;
  /** @brief Ports are not copied: the driver holds a reference. */
  I2cInterface& operator=(const I2cInterface&) = delete;

protected:
  /** @brief Only the derived port is constructed. */
  I2cInterface() noexcept = default;
  /** @brief Destroyed through the derived port only. */
  ~I2cInterface() noexcept = default;
};

/**
 * @brief Satisfied by a class that derives from `I2cInterface<Port>` and implements the required
 * bus hooks with exactly the types above, `noexcept` and returning `bool`.
 *
 * Each hook's address is matched with `detail::ExactHook`, which takes a member function of exactly
 * the signature and converts no value: a hook declared with a narrower length or without
 * `noexcept` does not satisfy the concept (handbook KB-CPP20-006). `DelayUs()` is checked by the
 * driver at the first call that waits.
 *
 * @tparam Port The port class.
 */
template <typename Port>
concept I2cPort = std::derived_from<Port, I2cInterface<Port>> && requires {
  detail::ExactHook<Port, bool() noexcept>::Take(&Port::EnsureInitialized);
  detail::ExactHook<Port, bool(uint8_t, const uint8_t*, std::size_t) noexcept>::Take(&Port::Write);
  detail::ExactHook<Port, bool(uint8_t, const uint8_t*, std::size_t, uint8_t*,
                               std::size_t) noexcept>::Take(&Port::WriteRead);
};

namespace detail {

// Optional hooks, all in this one place (KB-CPP-036): a `Declares*` concept only tests the name,
// the `Has*` concept the exact signature, `noexcept` and the result type. The driver rejects a hook
// that is declared but does not match (KB-CPP20-007).

/// The port declares a member called `DelayUs`, whatever its signature.
template <typename Port>
concept DeclaresDelayUs =
    requires { &Port::DelayUs; } || requires(Port& port) { port.DelayUs(1_u32); };

/// The port waits: `void DelayUs(uint32_t us) noexcept`. A narrower parameter would truncate.
template <typename Port>
concept HasDelayUs = requires { ExactHook<Port, void(uint32_t) noexcept>::Take(&Port::DelayUs); };

/// The port declares a member called `GpioSet`, whatever its signature.
template <typename Port>
concept DeclaresGpioSet = requires { &Port::GpioSet; } || requires(Port& port) {
  port.GpioSet(CtrlPin::OutputEnable, GpioSignal::INACTIVE);
};

/// The port drives OE: `bool GpioSet(CtrlPin, GpioSignal) noexcept`.
template <typename Port>
concept HasGpioSet =
    requires { ExactHook<Port, bool(CtrlPin, GpioSignal) noexcept>::Take(&Port::GpioSet); };

/// The port declares a member called `GetLastFailure`, whatever its signature.
template <typename Port>
concept DeclaresGetLastFailure = requires {
  &Port::GetLastFailure;
} || requires(Port& port, I2cFailure& failure) { port.GetLastFailure(failure); };

/// The port explains a failed transfer: `bool GetLastFailure(I2cFailure& failure) noexcept`. The
/// parameter must be a non-const lvalue reference, so a hook that could not set it is rejected.
template <typename Port>
concept HasGetLastFailure =
    requires { ExactHook<Port, bool(I2cFailure&) noexcept>::Take(&Port::GetLastFailure); };

/// The port declares a member called `Log`, whatever its signature.
template <typename Port>
concept DeclaresLog =
    requires { &Port::Log; } || requires(Port& port) { port.Log(LogLevel::Info, ""); };

/// The port takes log lines: `void Log(LogLevel, const char* message) noexcept`.
template <typename Port>
concept HasLog =
    requires { ExactHook<Port, void(LogLevel, const char*) noexcept>::Take(&Port::Log); };

/// Wait through the port. A port without `DelayUs()` fails to compile at the first call that needs
/// a wait: the oscillator start-up is a requirement, never skipped.
template <typename Port>
void DelayUs(Port& port, uint32_t us) noexcept {
  if constexpr (HasDelayUs<Port>) {
    port.DelayUs(us);
  } else {
    static_assert(kAlwaysFalse<Port>,
                  "pca9685: the port must implement `void DelayUs(uint32_t us) noexcept`");
  }
}

/// Log through the port, or nothing when the port has no `Log()`. The driver never formats text.
template <typename Port>
void Log(Port& port, [[maybe_unused]] LogLevel level,
         [[maybe_unused]] const char* message) noexcept {
  if constexpr (HasLog<Port>) {
    port.Log(level, message);
  } else {
    (void)port;
  }
}

/// The error for a transfer the port reported as failed: `NoDevice` when the port says nothing
/// acknowledged the address, `Bus` otherwise (also when the port has no `GetLastFailure()`).
template <typename Port>
[[nodiscard]] Error TransferError(Port& port) noexcept {
  if constexpr (HasGetLastFailure<Port>) {
    I2cFailure failure = I2cFailure::Other;
    if (port.GetLastFailure(failure) && (failure == I2cFailure::AddressNack)) {
      return Error::NoDevice;
    }
  } else {
    (void)port;
  }
  return Error::Bus;
}

/// Longest register burst the driver issues, in data bytes: all 16 channels, 64 bytes.
inline constexpr std::size_t kMaxBurst =
    static_cast<std::size_t>(reg::kChannels) * reg::kBytesPerChannel;

/// A burst size the register helpers accept: 1 to `kMaxBurst` bytes, checked at compile time.
template <std::size_t N>
concept BurstSize = (N >= 1U) && (N <= kMaxBurst);

/// Register access over I2C: the control register (the register address) is the first payload
/// byte; reads use a repeated START (`kRepeatedStart`). A burst of more than one byte relies on
/// MODE1.AI, which the driver sets at start-up. No transfer is ever empty: a zero-length burst is
/// refused before any I/O (handbook KB-CPP-130).
template <I2cPort Port>
class RegisterBus {
public:
  RegisterBus(Port& port, uint8_t address) noexcept : port_(port), address_(address) {}

  Port& GetPort() & noexcept {
    return port_;
  }

  [[nodiscard]] uint8_t GetAddress() const noexcept {
    return address_;
  }

  /// Read `out.size()` registers from `reg`; the size is checked at compile time.
  template <std::size_t N>
    requires BurstSize<N>
  Error Read(uint8_t reg, std::span<uint8_t, N> out) noexcept {
    return Read(reg, std::span<uint8_t>{out});
  }

  /// Read `out.size()` registers from `reg`. `InvalidArgument` (nothing sent) for 0 or more than
  /// `kMaxBurst` bytes, `NoDevice` or `Bus` when the transfer fails.
  Error Read(uint8_t reg, std::span<uint8_t> out) noexcept {
    if (out.empty() || (out.size() > kMaxBurst)) {
      return Error::InvalidArgument;
    }
    const std::array<uint8_t, 1> pointer{reg};
    return port_.WriteRead(address_, pointer.data(), pointer.size(), out.data(), out.size())
               ? Error::Ok
               : TransferError(port_);
  }

  /// Write `data.size()` registers from `reg`; the size is checked at compile time.
  template <std::size_t N>
    requires BurstSize<N>
  Error Write(uint8_t reg, std::span<const uint8_t, N> data) noexcept {
    return Write(reg, std::span<const uint8_t>{data});
  }

  /// Write `data.size()` registers from `reg`. `InvalidArgument` (nothing sent) for 0 or more than
  /// `kMaxBurst` bytes, `NoDevice` or `Bus` when the transfer fails.
  Error Write(uint8_t reg, std::span<const uint8_t> data) noexcept {
    if (data.empty() || (data.size() > kMaxBurst)) {
      return Error::InvalidArgument;
    }
    std::array<uint8_t, 1U + kMaxBurst> frame{};
    frame[0] = reg;
    const std::size_t count = data.size();
    for (std::size_t i = 0U; i < count; ++i) {
      frame[1U + i] = data[i];
    }
    return port_.Write(address_, frame.data(), count + 1U) ? Error::Ok : TransferError(port_);
  }

private:
  Port& port_;
  uint8_t address_;
};

} // namespace detail

} // namespace pca9685

#endif // PCA9685_I2C_INTERFACE_HPP
