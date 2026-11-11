/**
 * @file main.cpp
 * @brief The PCA9685 driver on your PC: start a simulated chip, fade an LED, centre a servo.
 *
 * No hardware and no SDK: the host port (host_pca9685_port.hpp) connects the real driver to the
 * register model the host tests use. The same calls run on an MCU with that MCU's port
 * (examples/esp32/ for ESP-IDF). Exit code 0 when every call succeeded.
 */
#include <cstdint>
#include <cstdio>

#include "host_pca9685_port.hpp"
#include "pca9685.hpp"

int main() {
  HostPca9685Port port;
  pca9685::PCA9685<HostPca9685Port> pwm(port, pca9685::kI2cAddress, pca9685::ServoPreset());

  std::printf("PCA9685 driver %s on the host\n", pca9685::GetDriverVersion());
  if (const auto r = pwm.EnsureInitialized(); !r) {
    std::printf("EnsureInitialized failed: %s\n", pca9685::ToString(r.error));
    return 1;
  }
  std::printf("PWM %u Hz (PRE_SCALE %u)\n", static_cast<unsigned>(pwm.GetPwmFrequencyHz().value),
              static_cast<unsigned>(pwm.GetPrescale().value));

  for (uint16_t duty = 0; duty <= pca9685::kFullDuty; duty = static_cast<uint16_t>(duty + 1024)) {
    if (const auto r = pwm.SetDuty(0, duty); !r) {
      std::printf("SetDuty failed: %s\n", pca9685::ToString(r.error));
      return 1;
    }
    std::printf("LED0 duty %u/4096\n", static_cast<unsigned>(duty));
  }
  const auto width = pwm.SetPulseWidthUs(1, 1500);
  if (!width) {
    std::printf("SetPulseWidthUs failed: %s\n", pca9685::ToString(width.error));
    return 1;
  }
  std::printf("servo on LED1: %u ns pulse\n", static_cast<unsigned>(width.value));
  if (const auto r = pwm.CheckForReset(); !r) {
    std::printf("CheckForReset: %s\n", pca9685::ToString(r.error));
    return 1;
  }
  return pwm.Deinitialize().ok() ? 0 : 1;
}
