/**
 * @file configuration.cpp
 * @brief The Configuration page's examples, compiled by the host build (never run).
 */
#include "my_port.hpp"
#include "pca9685.hpp"
#include "pca9685_kconfig.hpp"

// --8<-- [start:custom]
pca9685::Config ZenerLedConfig() {
  pca9685::Config c{};                              // the reset values: 200 Hz, totem pole
  c.output_drive = pca9685::OutputDrive::OpenDrain; // LEDs with Zener diodes (section 10, Q5)
  c.pwm_frequency_hz = 1000;                        // PRE_SCALE 5
  c.oscillator_hz = 25'000'000;                     // or the measured value of this board
  return c;                                         // pca9685::Validate(c) checks it offline
}

pca9685::Error StartLeds(MyPort& port) {
  static pca9685::PCA9685<MyPort> pwm(port, 0x41, ZenerLedConfig()); // A0 high; no I/O
  return pwm.EnsureInitialized().error; // InvalidArgument if the config is out of range
}
// --8<-- [end:custom]

// --8<-- [start:checked]
// Rejected at build time, naming ConfigIsInvalid, if Validate() fails.
constexpr pca9685::CheckedConfig kBoard{pca9685::Config{.pwm_frequency_hz = 50}};
// --8<-- [end:checked]

pca9685::Error StartChecked(MyPort& port) {
  static pca9685::PCA9685<MyPort> pwm(port, pca9685::kI2cAddress, kBoard.Get());
  return pwm.EnsureInitialized().error;
}

// --8<-- [start:kconfig]
pca9685::Error StartFromMenuconfig(MyPort& port) {
  static pca9685::PCA9685<MyPort> pwm(port, pca9685::kI2cAddress, pca9685::KconfigDefaults());
  return pwm.EnsureInitialized().error;
}
// --8<-- [end:kconfig]
