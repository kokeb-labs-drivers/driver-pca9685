/**
 * @file test_kconfig.cpp
 * @brief KconfigDefaults(): the defaults without Kconfig, and menuconfig values when they are set.
 *
 * Built twice: without CONFIG_ macros (KCONFIG_SET undefined) and with the values an sdkconfig.h
 * would define (KCONFIG_SET=1, see tests/CMakeLists.txt).
 */
#include "pca9685_kconfig.hpp"
#include "test_harness.hpp"

#ifdef KCONFIG_SET
TEST_CASE("Kconfig values reach the Config") {
  constexpr pca9685::Config kC = pca9685::KconfigDefaults();
  CHECK_EQ(kC.pwm_frequency_hz, 50U);
  CHECK_EQ(kC.oscillator_hz, 26'500'000U);
  CHECK(kC.clock == pca9685::ClockSource::External);
  CHECK(kC.output_drive == pca9685::OutputDrive::OpenDrain);
  CHECK(kC.invert_outputs);
  CHECK_EQ(pca9685::Validate(kC), pca9685::Error::Ok);
  const pca9685::Config run = pca9685::KconfigDefaults(); // the same at run time
  CHECK_EQ(run.pwm_frequency_hz, kC.pwm_frequency_hz);
}
#else
TEST_CASE("Without Kconfig, KconfigDefaults() equals Config{}") {
  constexpr pca9685::Config kC = pca9685::KconfigDefaults();
  constexpr pca9685::Config kD{};
  CHECK_EQ(kC.pwm_frequency_hz, kD.pwm_frequency_hz);
  CHECK_EQ(kC.oscillator_hz, kD.oscillator_hz);
  CHECK(kC.clock == kD.clock);
  CHECK(kC.output_drive == kD.output_drive);
  CHECK_EQ(kC.invert_outputs, kD.invert_outputs);
  const pca9685::Config run = pca9685::KconfigDefaults(); // the same at run time
  CHECK_EQ(run.oscillator_hz, kD.oscillator_hz);
}
#endif
