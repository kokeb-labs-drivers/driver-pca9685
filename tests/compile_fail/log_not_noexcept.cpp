/**
 * @file log_not_noexcept.cpp
 * @brief Must NOT compile: an optional Log hook that is not noexcept is rejected, not ignored
 * (handbook KB-CPP20-007).
 *
 * EXPECT: Log hook must be
 */
#include <cstdint>

#include "mock_pca9685.hpp"
#include "pca9685.hpp"

class ThrowingLogPort : public pca9685_test::MockPortBase<ThrowingLogPort> {
public:
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
  void Log(pca9685::LogLevel level, const char* message) {
    (void)level;
    (void)message;
  }
};

int main() {
  ThrowingLogPort port;
  pca9685::PCA9685<ThrowingLogPort> pwm(port);
  return static_cast<int>(pwm.EnsureInitialized().error);
}
