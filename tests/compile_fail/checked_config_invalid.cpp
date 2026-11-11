/**
 * @file checked_config_invalid.cpp
 * @brief Must NOT compile: a CheckedConfig built from a configuration that Validate() rejects fails
 * the build, naming the error (handbook KB-CPP20-013).
 *
 * EXPECT: ConfigIsInvalid
 */
#include "pca9685_types.hpp"

int main() {
  // 5 kHz needs a prescaler below 3 from the 25 MHz oscillator: Validate() rejects it.
  constexpr pca9685::CheckedConfig kBoard{pca9685::Config{.pwm_frequency_hz = 5000}};
  return static_cast<int>(kBoard.Get().pwm_frequency_hz);
}
