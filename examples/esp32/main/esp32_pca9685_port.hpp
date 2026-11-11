/**
 * @file esp32_pca9685_port.hpp
 * @brief ESP-IDF ports for the PCA9685 driver: i2c_master for the bus, esp_rom delays, ESP_LOG for
 * the log hook, and a GPIO for OE.
 *
 * Two ports, one per board wiring:
 *
 * - `Esp32Pca9685Port`: OE on a GPIO, driven high (outputs disabled) before the pin becomes an
 *   output; the driver gets `GpioSet()`, so `DisableOutputs()` works.
 * - `Esp32Pca9685PortOeTied`: OE tied to VSS on the board; no `GpioSet()` hook, so the driver
 *   does without OE and `DisableOutputs()` returns `Unsupported` (docs/hardware.md).
 *
 * Start from this file for any ESP-IDF target. The pins are in Esp32Pca9685Pins; the defaults
 * match the wiring table in examples/esp32/README.md (ESP32-C6 DevKitC-1).
 */
#ifndef PCA9685_ESP32_PCA9685_PORT_HPP
#define PCA9685_ESP32_PCA9685_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "pca9685.hpp"

/// The port's default SCL clock, checked against the driver's bus requirements (KB-CPP-123).
inline constexpr uint32_t kEsp32Pca9685ClockHz = 400'000;
static_assert(kEsp32Pca9685ClockHz <= pca9685::kMaxClockHz, "SCL above the PCA9685's maximum");
static_assert(pca9685::kRepeatedStart, "this port reads with i2c_master_transmit_receive");

/// Pins and bus settings of the ESP-IDF ports.
struct Esp32Pca9685Pins {
  i2c_port_num_t port = I2C_NUM_0;
  int sda = 6;
  int scl = 7;
  int oe = 11;                              ///< OE's GPIO; used by `Esp32Pca9685Port` only.
  uint32_t clock_hz = kEsp32Pca9685ClockHz; ///< Up to `pca9685::kMaxClockHz`.
  int timeout_ms = 10;                      ///< Per transaction; the chip's bus time-out is 25 ms.
};

/// The bus, delay and log hooks both ports share. Neither has a GetLastFailure() hook: i2c_master
/// reports a NACK without saying whether the address or a data byte was refused, so an absent chip
/// is `Error::Bus` with these ports (docs/porting.md).
template <typename Derived>
class Esp32Pca9685PortBase : public pca9685::I2cInterface<Derived> {
public:
  explicit Esp32Pca9685PortBase(const Esp32Pca9685Pins& pins) noexcept : pins_(pins) {}

  bool EnsureInitialized() noexcept {
    if (bus_ != nullptr) {
      return true;
    }
    if (!static_cast<Derived*>(this)->initPins()) {
      return false;
    }
    i2c_master_bus_config_t bus{};
    bus.i2c_port = pins_.port;
    bus.sda_io_num = static_cast<gpio_num_t>(pins_.sda);
    bus.scl_io_num = static_cast<gpio_num_t>(pins_.scl);
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true; // weak: fit the board's pull-ups (docs/hardware.md)
    if (i2c_new_master_bus(&bus, &bus_) != ESP_OK) {
      bus_ = nullptr;
      return false;
    }
    return true;
  }

  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    i2c_master_dev_handle_t dev = device(addr);
    return dev != nullptr && i2c_master_transmit(dev, data, len, pins_.timeout_ms) == ESP_OK;
  }

  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    i2c_master_dev_handle_t dev = device(addr);
    return dev != nullptr &&
           i2c_master_transmit_receive(dev, tx, tx_len, rx, rx_len, pins_.timeout_ms) == ESP_OK;
  }

  void DelayUs(uint32_t us) noexcept {
    esp_rom_delay_us(us);
  }

  void Log(pca9685::LogLevel level, const char* message) noexcept {
    const esp_log_level_t esp_level = level == pca9685::LogLevel::Error     ? ESP_LOG_ERROR
                                      : level == pca9685::LogLevel::Warning ? ESP_LOG_WARN
                                      : level == pca9685::LogLevel::Info    ? ESP_LOG_INFO
                                                                            : ESP_LOG_DEBUG;
    esp_log_write(esp_level, kTag, "%s\n", message); // plain text, never a format
  }

protected:
  const Esp32Pca9685Pins& pins() const noexcept {
    return pins_;
  }

private:
  // The device handle for one 7-bit address, created on first use (one chip per port in this
  // example).
  i2c_master_dev_handle_t device(uint8_t addr) noexcept {
    if (device_ != nullptr && device_addr_ == addr) {
      return device_;
    }
    if (device_ != nullptr) {
      i2c_master_bus_rm_device(device_);
      device_ = nullptr;
    }
    i2c_device_config_t cfg{};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = addr;
    cfg.scl_speed_hz = pins_.clock_hz;
    if (i2c_master_bus_add_device(bus_, &cfg, &device_) != ESP_OK) {
      device_ = nullptr;
      return nullptr;
    }
    device_addr_ = addr;
    return device_;
  }

  static constexpr const char* kTag = "pca9685";
  Esp32Pca9685Pins pins_;
  i2c_master_bus_handle_t bus_ = nullptr;
  i2c_master_dev_handle_t device_ = nullptr;
  uint8_t device_addr_ = 0;
};

/// OE on a GPIO (`Esp32Pca9685Pins::oe`), with the `GpioSet()` hook.
class Esp32Pca9685Port : public Esp32Pca9685PortBase<Esp32Pca9685Port> {
public:
  explicit Esp32Pca9685Port(const Esp32Pca9685Pins& pins = {}) noexcept
      : Esp32Pca9685PortBase<Esp32Pca9685Port>(pins) {}

  /// OE: ACTIVE (outputs enabled) is the pin low; INACTIVE is the pin high.
  bool GpioSet(pca9685::CtrlPin pin, pca9685::GpioSignal signal) noexcept {
    if (pin != pca9685::CtrlPin::OutputEnable) {
      return false;
    }
    const uint32_t level = (signal == pca9685::GpioSignal::INACTIVE) ? 1U : 0U;
    return gpio_set_level(static_cast<gpio_num_t>(pins().oe), level) == ESP_OK;
  }

  /// OE high (outputs disabled) is set before the pin becomes an output, so it never drives low
  /// for a moment (handbook KB-HW-018).
  bool initPins() noexcept {
    const auto pin = static_cast<gpio_num_t>(pins().oe);
    if (gpio_set_level(pin, 1U) != ESP_OK) {
      return false;
    }
    gpio_config_t cfg{};
    cfg.pin_bit_mask = 1ULL << static_cast<uint32_t>(pins().oe);
    cfg.mode = GPIO_MODE_OUTPUT;
    return (gpio_config(&cfg) == ESP_OK) && (gpio_set_level(pin, 1U) == ESP_OK);
  }
};

/// OE tied to VSS on the board: no `GpioSet()` hook, so the driver never needs the pin.
class Esp32Pca9685PortOeTied : public Esp32Pca9685PortBase<Esp32Pca9685PortOeTied> {
public:
  explicit Esp32Pca9685PortOeTied(const Esp32Pca9685Pins& pins = {}) noexcept
      : Esp32Pca9685PortBase<Esp32Pca9685PortOeTied>(pins) {}

  /// No control pin to prepare.
  bool initPins() noexcept {
    return true;
  }
};

#endif // PCA9685_ESP32_PCA9685_PORT_HPP
