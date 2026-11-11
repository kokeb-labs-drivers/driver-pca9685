/**
 * @file fixture.hpp
 * @brief A port and a driver over the register model, and what "off" means at a pin.
 */
#ifndef PCA9685_TEST_FIXTURE_HPP
#define PCA9685_TEST_FIXTURE_HPP

#include <cstdint>

#include "mock_pca9685.hpp"
#include "pca9685.hpp"

namespace pca9685_test {

/// The level of an output whose channel is off, for the wiring a configuration describes: low
/// when not inverted; when inverted, high (totem pole) or high impedance (open drain).
inline Level OffLevel(const pca9685::Config& config) noexcept {
  if (!config.invert_outputs) {
    return Level::Low;
  }
  return (config.output_drive == pca9685::OutputDrive::TotemPole) ? Level::High : Level::HiZ;
}

/// Whether output `ch` is at the off level of `config`.
inline bool PinOff(const Pca9685Model& chip, uint32_t ch, const pca9685::Config& config) noexcept {
  return chip.PinLevel(ch) == OffLevel(config);
}

/// A value of `E` that is none of its enumerators, to test the checks against one.
template <typename E>
E NotAnEnumerator(uint8_t raw) noexcept {
  return static_cast<E>(raw); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange): on purpose
}

/// A port and a driver bound to it.
template <typename P>
struct Rig {
  P port;
  pca9685::PCA9685<P> pwm;

  explicit Rig(const pca9685::Config& config = {}, uint8_t address = pca9685::kI2cAddress) noexcept
      : port(), pwm(port, address, config) {}

  Pca9685Model& Chip() noexcept {
    return port.state.chip;
  }
  MockBusState& Bus() noexcept {
    return port.state;
  }
  /// Start the driver; true on success.
  bool Start() noexcept {
    return pwm.EnsureInitialized().ok();
  }
};

/// The value of a channel as four bytes, as the chip holds them.
inline pca9685::Pwm Decode(const std::array<uint8_t, 4>& b) noexcept {
  return pca9685::detail::DecodePwm(std::span<const uint8_t, 4>{b}).value;
}

/// Channel `ch` as stored in the chip's registers.
inline std::array<uint8_t, 4> Stored(const Pca9685Model& chip, uint32_t ch) noexcept {
  const auto base = static_cast<uint8_t>(0x06U + (4U * ch));
  return {chip.Reg(base), chip.Reg(static_cast<uint8_t>(base + 1U)),
          chip.Reg(static_cast<uint8_t>(base + 2U)), chip.Reg(static_cast<uint8_t>(base + 3U))};
}

} // namespace pca9685_test

#endif // PCA9685_TEST_FIXTURE_HPP
