/**
 * @file not_a_port.cpp
 * @brief Must NOT compile: the driver is constrained by the port concept.
 *
 * EXPECT: Port
 */
#include "pca9685.hpp"

struct NotAPort {
  bool EnsureInitialized() noexcept {
    return true;
  }
};

int main() {
  NotAPort port;
  pca9685::PCA9685<NotAPort> pwm(port);
  return static_cast<int>(pwm.EnsureInitialized().error);
}
