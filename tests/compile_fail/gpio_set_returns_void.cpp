/**
 * @file gpio_set_returns_void.cpp
 * @brief Must NOT compile: an optional hook with the wrong signature is rejected, not ignored.
 *
 * EXPECT: GpioSet hook must be
 */
#include "mock_pca9685.hpp"
#include "pca9685.hpp"

class VoidGpioPort : public pca9685_test::MockPortBase<VoidGpioPort> {
public:
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
  void GpioSet(pca9685::CtrlPin pin, pca9685::GpioSignal signal) noexcept {
    (void)pin;
    (void)signal;
  }
};

int main() {
  VoidGpioPort port;
  pca9685::PCA9685<VoidGpioPort> pwm(port);
  return static_cast<int>(pwm.EnsureInitialized().error);
}
