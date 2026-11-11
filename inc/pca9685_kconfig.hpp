/**
 * @file pca9685_kconfig.hpp
 * @brief Optional ESP-IDF layer: a `Config` built from the `CONFIG_PCA9685_*` Kconfig options.
 * @copyright Copyright (c) 2026 The driver-pca9685 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Include this header only where you want menuconfig to choose the defaults; the driver never
 * includes it and no driver signature depends on it. Under ESP-IDF the values come from
 * `sdkconfig.h`. Without Kconfig every option falls back to the reset value, so
 * `KconfigDefaults()` equals `Config{}`.
 *
 * Kconfig writes a bool option set to "n" as an absent macro, so an absent option means off.
 *
 * One configuration per program: the inline function below depends on the `CONFIG_` macros, so
 * every translation unit of one program must see the same values (ESP-IDF has one `sdkconfig.h`
 * per build; each host test is its own program). No driver or library header includes this one
 * (handbook KB-CPP20-018).
 */
#ifndef PCA9685_KCONFIG_HPP
#define PCA9685_KCONFIG_HPP

#ifdef __has_include
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

#include "pca9685_types.hpp"

#ifndef CONFIG_PCA9685_PWM_FREQUENCY_HZ
/** PWM frequency when Kconfig does not set it: `Config{}.pwm_frequency_hz`, 200 Hz. */
#define CONFIG_PCA9685_PWM_FREQUENCY_HZ 200
#endif
#ifndef CONFIG_PCA9685_OSCILLATOR_HZ
/** Counter clock when Kconfig does not set it: the 25 MHz typical internal oscillator. */
#define CONFIG_PCA9685_OSCILLATOR_HZ 25000000
#endif

namespace pca9685 {

/**
 * @brief The configuration menuconfig selected, on top of the reset values.
 * @return `Config{}` with the frequency, clock and output options taken from Kconfig.
 */
constexpr Config KconfigDefaults() noexcept {
  Config c{};
  c.pwm_frequency_hz = static_cast<uint32_t>(CONFIG_PCA9685_PWM_FREQUENCY_HZ);
  c.oscillator_hz = static_cast<uint32_t>(CONFIG_PCA9685_OSCILLATOR_HZ);
#if defined(CONFIG_PCA9685_EXTERNAL_CLOCK) && CONFIG_PCA9685_EXTERNAL_CLOCK
  c.clock = ClockSource::External;
#endif
#if defined(CONFIG_PCA9685_OPEN_DRAIN) && CONFIG_PCA9685_OPEN_DRAIN
  c.output_drive = OutputDrive::OpenDrain;
#endif
#if defined(CONFIG_PCA9685_INVERT_OUTPUTS) && CONFIG_PCA9685_INVERT_OUTPUTS
  c.invert_outputs = true;
#endif
  return c;
}

} // namespace pca9685

#endif // PCA9685_KCONFIG_HPP
