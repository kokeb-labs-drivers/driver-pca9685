/**
 * @file pca9685_basic_example.cpp
 * @brief Start the PCA9685 from the menuconfig values, fade LED0 up and down and sweep a servo on
 * LED1, checking for a chip reset once per step.
 *
 * Wiring: examples/esp32/README.md. Expected output: one "initialised" line, then the duty and the
 * pulse width every 100 ms. Every Result is checked and logged by name.
 */
#include <cstdint>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp32_pca9685_port.hpp"
#include "pca9685.hpp"
#include "pca9685_kconfig.hpp"

namespace {
constexpr const char* kTag = "pca9685_example";
constexpr uint8_t kLed = 0;
constexpr uint8_t kServo = 1;

bool Step(pca9685::PCA9685<Esp32Pca9685Port>& pwm, uint16_t duty, uint32_t width_us) {
  if (const auto r = pwm.CheckForReset(); !r) {
    ESP_LOGW(kTag, "CheckForReset: %s", pca9685::ToString(r.error));
    return false;
  }
  if (const auto r = pwm.SetDuty(kLed, duty); !r) {
    ESP_LOGW(kTag, "SetDuty: %s", pca9685::ToString(r.error));
    return false;
  }
  const auto w = pwm.SetPulseWidthUs(kServo, width_us);
  if (!w) {
    ESP_LOGW(kTag, "SetPulseWidthUs: %s", pca9685::ToString(w.error));
    return false;
  }
  ESP_LOGI(kTag, "duty %u/4096, servo %lu ns", static_cast<unsigned>(duty),
           static_cast<unsigned long>(w.value));
  return true;
}
} // namespace

// The port for a board with OE tied to VSS builds too: the driver instantiated over it, unused
// here.
template class pca9685::PCA9685<Esp32Pca9685PortOeTied>;
static_assert(!pca9685::detail::HasGpioSet<Esp32Pca9685PortOeTied>);
static_assert(pca9685::detail::HasGpioSet<Esp32Pca9685Port>);

extern "C" void app_main(void) {
  static Esp32Pca9685Port port; // static: the driver keeps a reference
  static pca9685::PCA9685<Esp32Pca9685Port> pwm(port, pca9685::kI2cAddress,
                                                pca9685::KconfigDefaults()); // no I/O

  ESP_LOGI(kTag, "PCA9685 driver %s", pca9685::GetDriverVersion());
  uint32_t step = 0;
  while (true) {
    const auto init = pwm.EnsureInitialized(); // Ok at once once started
    if (!init) {
      ESP_LOGE(kTag, "EnsureInitialized failed: %s", pca9685::ToString(init.error));
      (void)pwm.DisableOutputs(); // OE high: the outputs off whatever the bus does
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    const uint32_t phase = step % 40U; // 0..39: up, then down
    const uint32_t level = (phase < 20U) ? phase : (40U - phase);
    const auto duty = static_cast<uint16_t>(level * 204U); // 0 to 4080 of 4096
    const uint32_t width_us = 1000U + (level * 50U);       // 1.0 to 2.0 ms
    if (!Step(pwm, duty, width_us)) {
      (void)pwm.SetAllOff(); // never refused; EnsureInitialized() runs again after a reset
    }
    ++step;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
