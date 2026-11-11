/**
 * @file my_port.hpp
 * @brief The I2C port shown on the Quickstart and Porting pages, compiled by the host build.
 *
 * Every platform call goes through the `platform_*` functions declared below; a real port replaces
 * them with its SDK (ESP-IDF, STM32 HAL, Zephyr, Linux i2c-dev).
 */
#ifndef PCA9685_MY_PORT_HPP
#define PCA9685_MY_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "pca9685_i2c_interface.hpp"

// Your SDK's calls. Declared only: the docs build checks that the port compiles.
bool platform_i2c_init() noexcept; // up to 1 MHz (Fm+)
bool platform_i2c_write(uint8_t addr7, const uint8_t* data,
                        std::size_t len) noexcept; // START .. STOP
bool platform_i2c_write_read(uint8_t addr7, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                             std::size_t rx_len) noexcept; // repeated START
void platform_delay_us(uint32_t us) noexcept;              // busy-wait or sleep

// --8<-- [start:port]
class MyPort : public pca9685::I2cInterface<MyPort> {
public:
  // Bring the bus up once; later calls return true at once.
  bool EnsureInitialized() noexcept {
    if (!ready_) {
      ready_ = platform_i2c_init();
    }
    return ready_;
  }

  // One write transaction: START, address + W, the bytes (register first), STOP.
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    return platform_i2c_write(addr, data, len);
  }

  // Write then read with a repeated START in between.
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    return platform_i2c_write_read(addr, tx, tx_len, rx, rx_len);
  }

  // Wait at least `us` microseconds: the 500 us oscillator start-up depends on it.
  void DelayUs(uint32_t us) noexcept {
    platform_delay_us(us);
  }

private:
  bool ready_ = false;
};
// --8<-- [end:port]

#endif // PCA9685_MY_PORT_HPP
