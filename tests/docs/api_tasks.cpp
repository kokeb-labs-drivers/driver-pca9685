/**
 * @file api_tasks.cpp
 * @brief The API guide's examples, compiled by the host build (never run).
 */
#include <array>
#include <cstdio>

#include "my_port.hpp"
#include "pca9685.hpp"

using Pwm = pca9685::PCA9685<MyPort>;

// --8<-- [start:lifecycle]
pca9685::Error BringUp(Pwm& pwm) {
  // The one entry point: starts the chip on the first call, then returns Ok with no I/O. After a
  // failure the next call starts again from the beginning, switching every output off first.
  const auto r = pwm.EnsureInitialized();
  if (!r) {
    std::printf("PCA9685 init: %s\n", pca9685::ToString(r.error));
  }
  return r.error;
}
// --8<-- [end:lifecycle]

// --8<-- [start:servo]
pca9685::Error CentreServos(Pwm& pwm) {
  // ServoPreset(): 50 Hz. Pulse widths use the programmed prescaler and Config::oscillator_hz.
  for (uint8_t ch = 0; ch < 4; ++ch) {
    const auto r = pwm.SetPulseWidthUs(ch, 1500); // returns the programmed width in ns
    if (!r) {
      return r.error;
    }
  }
  return pca9685::Error::Ok;
}
// --8<-- [end:servo]

// --8<-- [start:bank]
pca9685::Error RgbTogether(Pwm& pwm, uint16_t r, uint16_t g, uint16_t b) {
  // One transfer: with OCH = 0 (the default) the three channels change together at the STOP.
  // Staggered phases spread the switching edges (section 7.3.3).
  const std::array<pca9685::Pwm, 3> rgb{pca9685::PwmForDuty(r, 0).value,
                                        pca9685::PwmForDuty(g, 1365).value,
                                        pca9685::PwmForDuty(b, 2730).value};
  return pwm.SetChannels(0, rgb).error; // a bad value: switch-ons refused, switch-offs sent
}
// --8<-- [end:bank]

// --8<-- [start:reset]
pca9685::Error Supervise(Pwm& pwm) {
  // Once per control period: a reset the driver has not seen leaves every channel off and the
  // chip asleep, and the channel calls cannot tell (the chip acknowledges them).
  const auto r = pwm.CheckForReset();
  if (r.error == pca9685::Error::DeviceReset) {
    return pwm.EnsureInitialized().error; // then write the channels again
  }
  return r.error;
}
// --8<-- [end:reset]

// --8<-- [start:errors]
const char* Describe(pca9685::Error e) {
  switch (e) {
  case pca9685::Error::Bus:
  case pca9685::Error::NoDevice:
  case pca9685::Error::DeviceIdMismatch:
    return "check SDA, SCL, the pull-ups, VDD and the A0 to A5 address";
  case pca9685::Error::DeviceReset:
    return "the chip reset (supply dip): EnsureInitialized() again";
  case pca9685::Error::StateUnknown:
    return "a write failed: read the channel back or write it again";
  case pca9685::Error::Asleep:
    return "written; the outputs follow after Wake()";
  default:
    return pca9685::ToString(e);
  }
}
// --8<-- [end:errors]

// --8<-- [start:emergency]
void EmergencyStop(Pwm& pwm) {
  // OE first: a pin write, no I2C, within 60 ns. Then every channel off over I2C.
  if (auto r = pwm.DisableOutputs(); !r) {
    std::printf("OE: %s\n", pca9685::ToString(r.error));
  }
  if (auto r = pwm.SetAllOff(); !r) {
    std::printf("ALL_LED off: %s\n", pca9685::ToString(r.error));
  }
}
// --8<-- [end:emergency]

// --8<-- [start:deinit]
void PowerDown(Pwm& pwm) {
  if (auto r = pwm.Deinitialize(); !r) {
    std::printf("PCA9685 shutdown write failed: %s\n", pca9685::ToString(r.error));
  }
}
// --8<-- [end:deinit]
