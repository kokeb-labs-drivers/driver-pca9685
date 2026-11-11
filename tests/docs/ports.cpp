/**
 * @file ports.cpp
 * @brief The port with every optional hook, as shown on the Porting page. Compiled, never run.
 */
#include <cstddef>
#include <cstdint>

#include "pca9685.hpp"

bool platform_i2c_init() noexcept;
bool platform_i2c_write(uint8_t addr7, const uint8_t* data, std::size_t len) noexcept;
bool platform_i2c_write_read(uint8_t addr7, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                             std::size_t rx_len) noexcept;
bool platform_i2c_last_was_address_nack() noexcept;
void platform_delay_us(uint32_t us) noexcept;
bool platform_gpio_write(int pin, bool high) noexcept;
void platform_log(const char* line) noexcept;

// --8<-- [start:full-port]
class FullPort : public pca9685::I2cInterface<FullPort> {
public:
  bool EnsureInitialized() noexcept {
    // Drive OE high (outputs disabled) before the pin becomes an output (KB-HW-018).
    return platform_gpio_write(kOeGpio, true) && platform_i2c_init();
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    return platform_i2c_write(addr, data, len); // STOP also after a NACK
  }
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    return platform_i2c_write_read(addr, tx, tx_len, rx, rx_len);
  }

  // Required: the 500 us oscillator start-up after a wake.
  void DelayUs(uint32_t us) noexcept {
    platform_delay_us(us);
  }

  // Optional: OE on a GPIO. ACTIVE means "outputs enabled", which is a low level on the pin.
  bool GpioSet(pca9685::CtrlPin pin, pca9685::GpioSignal signal) noexcept {
    if (pin != pca9685::CtrlPin::OutputEnable) {
      return false;
    }
    return platform_gpio_write(kOeGpio, signal == pca9685::GpioSignal::INACTIVE);
  }

  // Optional: tells an address NACK (nothing there: NoDevice) from a data NACK (Bus).
  bool GetLastFailure(pca9685::I2cFailure& failure) noexcept {
    failure = platform_i2c_last_was_address_nack() ? pca9685::I2cFailure::AddressNack
                                                   : pca9685::I2cFailure::Other;
    return true;
  }

  // Optional: one line per lifecycle event or failure. The driver never formats; the port does.
  void Log(pca9685::LogLevel level, const char* message) noexcept {
    if (level > pca9685::LogLevel::Warning) {
      return;
    }
    platform_log(message); // plain text: never use it as a format string
  }

private:
  static constexpr int kOeGpio = 7;
};
// --8<-- [end:full-port]

// --8<-- [start:output-enable]
pca9685::Error FastOff(pca9685::PCA9685<FullPort>& pwm) {
  // With GpioSet() this drives OE high; a port without it gets Error::Unsupported.
  return pwm.DisableOutputs().error;
}
// --8<-- [end:output-enable]
