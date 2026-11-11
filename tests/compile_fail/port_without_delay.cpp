/**
 * @file port_without_delay.cpp
 * @brief Must NOT compile: the oscillator start-up wait needs the port's DelayUs().
 *
 * EXPECT: DelayUs
 */
#include "mock_pca9685.hpp"
#include "pca9685.hpp"

class NoDelayPort : public pca9685_test::MockPortBase<NoDelayPort> {};

int main() {
  NoDelayPort port;
  pca9685::PCA9685<NoDelayPort> pwm(port);
  return static_cast<int>(pwm.EnsureInitialized().error);
}
