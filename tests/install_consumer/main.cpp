/**
 * @file main.cpp
 * @brief Builds the driver from the installed package only: headers, version header and .ipp.
 */
#include <cstddef>
#include <cstdint>

#include "pca9685.hpp"

namespace {

// A port that answers every read with 0: enough to instantiate the whole driver template.
class NullPort : public pca9685::I2cInterface<NullPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    (void)addr;
    (void)data;
    (void)len;
    return true;
  }
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    (void)addr;
    (void)tx;
    (void)tx_len;
    for (std::size_t i = 0; i < rx_len; ++i) {
      rx[i] = 0;
    }
    return true;
  }
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
};

} // namespace

int main() {
  NullPort port;
  pca9685::PCA9685<NullPort> pwm(port);
  // Every register reads 0, which no PCA9685 the driver just wrote can return, so
  // EnsureInitialized() must fail with DeviceIdMismatch: proof the template compiled and ran.
  const auto r = pwm.EnsureInitialized();
  return (r.error == pca9685::Error::DeviceIdMismatch && pca9685::GetDriverVersion()[0] != '\0')
             ? 0
             : 1;
}
