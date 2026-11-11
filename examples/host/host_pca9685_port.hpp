/**
 * @file host_pca9685_port.hpp
 * @brief The host port: the driver's bus hooks over a simulated PCA9685, so the example runs on any
 * PC.
 *
 * `pca9685_test::MockPortBase` (tests/mock_pca9685_bus.hpp) supplies the required bus hooks over
 * the register model the host tests use. This class adds what a real port adds: `DelayUs()` (here
 * it advances the simulated chip's clock instead of waiting) and `Log()` to the console. A port for
 * a real bus on a PC (Linux i2c-dev, a USB bridge) implements the same hooks: see docs/porting.md.
 */
#ifndef PCA9685_HOST_PCA9685_PORT_HPP
#define PCA9685_HOST_PCA9685_PORT_HPP

#include <cstdint>
#include <cstdio>

#include "mock_pca9685.hpp"

/// The host port: simulated chip, real console.
class HostPca9685Port : public pca9685_test::MockPortBase<HostPca9685Port> {
public:
  /// The bus settings a real port would use; checked against the driver's bus requirements
  /// (handbook KB-CPP-123) like any port's.
  static constexpr uint32_t kClockHz = 400'000;
  static_assert(kClockHz <= pca9685::kMaxClockHz, "SCL above the PCA9685's maximum");

  /// Time passes in the simulated chip; nothing sleeps.
  void DelayUs(uint32_t us) noexcept {
    state.chip.Elapse(us);
  }

  /// Driver messages (they carry their own `pca9685:` prefix) go to the console, one line each.
  void Log(pca9685::LogLevel level, const char* message) noexcept {
    (void)level;
    (void)std::puts(message); // plain text: never pass it as a format string
  }
};

#endif // PCA9685_HOST_PCA9685_PORT_HPP
