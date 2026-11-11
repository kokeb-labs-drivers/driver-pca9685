/**
 * @file delay_us_narrowed.cpp
 * @brief Must NOT compile: a DelayUs hook with a narrower parameter would truncate long waits, so
 * the driver rejects it instead of using it (handbook KB-CPP20-007).
 *
 * EXPECT: DelayUs hook must be
 */
#include <cstdint>

#include "mock_pca9685.hpp"
#include "pca9685.hpp"

class ShortDelayPort : public pca9685_test::MockPortBase<ShortDelayPort> {
public:
  void DelayUs(uint16_t us) noexcept {
    (void)us;
  }
};

int main() {
  ShortDelayPort port;
  pca9685::PCA9685<ShortDelayPort> pwm(port);
  return static_cast<int>(pwm.EnsureInitialized().error);
}
