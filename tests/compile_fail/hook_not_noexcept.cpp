/**
 * @file hook_not_noexcept.cpp
 * @brief Must NOT compile: a WriteRead hook that is not noexcept breaks the port contract, so the
 * port concept rejects it (handbook KB-CPP20-006).
 *
 * EXPECT: I2cPort
 */
#include <cstddef>
#include <cstdint>

#include "pca9685.hpp"

class ThrowingPort : public pca9685::I2cInterface<ThrowingPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    (void)addr;
    (void)data;
    return len > 0U;
  }
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) {
    (void)addr;
    (void)tx;
    (void)rx;
    return tx_len > 0U && rx_len > 0U;
  }
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
};

int main() {
  ThrowingPort port;
  pca9685::PCA9685<ThrowingPort> pwm(port);
  return static_cast<int>(pwm.EnsureInitialized().error);
}
