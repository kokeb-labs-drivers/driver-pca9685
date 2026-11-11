/**
 * @file get_last_failure_misdeclared.cpp
 * @brief Must NOT compile: an optional GetLastFailure hook with the wrong signature is rejected,
 * not ignored (handbook KB-CPP20-007): a `void` hook could not say that it cannot tell.
 *
 * EXPECT: GetLastFailure hook must be
 */
#include <cstdint>

#include "mock_pca9685.hpp"
#include "pca9685.hpp"

class VoidFailurePort : public pca9685_test::MockPortBase<VoidFailurePort> {
public:
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
  void GetLastFailure(pca9685::I2cFailure& failure) noexcept {
    failure = pca9685::I2cFailure::Other;
  }
};

int main() {
  VoidFailurePort port;
  pca9685::PCA9685<VoidFailurePort> pwm(port);
  return static_cast<int>(pwm.EnsureInitialized().error);
}
