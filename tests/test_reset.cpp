/**
 * @file test_reset.cpp
 * @brief Resets the chip goes through on its own (sections 7.5, 7.6), and that the driver never
 * writes its cached state over a reset chip or lets one run on reset values.
 */
#include <array>
#include <cstdint>

#include "fixture.hpp"
#include "test_harness.hpp"

namespace {

using pca9685::Config;
using pca9685::Error;
using pca9685_test::FullPort;
using pca9685_test::MinimalPort;
using pca9685_test::Rig;

/// A software reset by the general call (section 7.6), as another master would send it.
void GeneralCallReset(pca9685_test::Pca9685Model& chip) {
  const std::array<uint8_t, 1> swrst{0x06};
  (void)chip.Write(0x00, swrst.data(), swrst.size(), swrst.size());
}

} // namespace

TEST_CASE("CheckForReset: Ok on a chip that kept its state; one 2-byte read") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const int before = rig.Bus().transfers;
  CHECK(rig.pwm.CheckForReset().ok());
  CHECK_EQ(rig.Bus().transfers, before + 1);
  CHECK(rig.pwm.IsInitialized());
}

TEST_CASE("A power-on reset (MODE1 back at 0x11, AI = 0) is DeviceReset; the driver restarts") {
  Rig<FullPort> rig(pca9685::ServoPreset());
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.SetPulseWidthUs(0, 1500).ok());
  rig.Chip().PowerOnReset();
  CHECK(!rig.Chip().ChannelActive(0)); // the reset state is off and asleep
  CHECK_EQ(rig.pwm.CheckForReset().error, Error::DeviceReset);
  CHECK(!rig.pwm.IsInitialized());
  CHECK_EQ(rig.pwm.SetPulseWidthUs(0, 1500).error, Error::NotInitialized);
  CHECK(rig.Start());
  CHECK_EQ(rig.Chip().Reg(0xFE), 121);
  CHECK(rig.pwm.SetPulseWidthUs(0, 1500).ok());
  CHECK(rig.Chip().ChannelActive(0));
}

TEST_CASE("A software reset by the general call is found the same way") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  GeneralCallReset(rig.Chip());
  CHECK_EQ(rig.Chip().resets, 1);
  CHECK_EQ(rig.pwm.CheckForReset().error, Error::DeviceReset);
}

TEST_CASE("Sleep, Wake and SetPwmFrequency find a reset before they write MODE1 from the copy") {
  for (int call = 0; call < 3; ++call) {
    Rig<MinimalPort> rig;
    REQUIRE(rig.Start());
    REQUIRE(rig.pwm.SetFullOn(0).ok());
    rig.Chip().PowerOnReset();
    const int mode1_writes = rig.Chip().mode1_writes;
    Error e = Error::Ok;
    if (call == 0) {
      e = rig.pwm.Sleep(pca9685::SleepMode::HoldOutputs).error;
    } else if (call == 1) {
      e = rig.pwm.Wake().error;
    } else {
      e = rig.pwm.SetPwmFrequency(60).error;
    }
    CHECK_EQ(e, Error::DeviceReset);
    CHECK_EQ(rig.Chip().mode1_writes, mode1_writes); // nothing written over the reset chip
    CHECK(rig.Chip().Asleep());
    CHECK(!rig.Chip().ChannelActive(0));
    CHECK(!rig.pwm.IsInitialized());
  }
}

TEST_CASE("Verify::ReadBack and ReadChannel find a reset (a 4-byte read needs AI)") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.Chip().PowerOnReset();
  CHECK_EQ(rig.pwm.SetDuty(1, 100, 0, pca9685::Verify::ReadBack).error, Error::DeviceReset);
  REQUIRE(rig.Start());
  rig.Chip().PowerOnReset();
  CHECK_EQ(rig.pwm.ReadChannel(1).error, Error::DeviceReset);
  REQUIRE(rig.Start());
  rig.Chip().PowerOnReset();
  CHECK_EQ(rig.pwm.ReadChannels().error, Error::DeviceReset);
  REQUIRE(rig.Start());
  rig.Chip().PowerOnReset();
  CHECK_EQ(rig.pwm.ReadPrescale().error, Error::DeviceReset);
}

TEST_CASE("A reset the driver has not seen leaves every output off: the chip is asleep") {
  for (const bool holds : {true, false}) {
    Rig<MinimalPort> rig;
    rig.Chip().readings.sleep_holds_outputs = holds;
    REQUIRE(rig.Start());
    REQUIRE(rig.pwm.SetFullOn(2).ok());
    rig.Chip().PowerOnReset();
    // The chip acknowledges the writes, so the calls cannot tell; with AI clear every byte lands
    // in LEDn_ON_L, and asleep no output can switch on (table 5 note 4).
    CHECK(rig.pwm.SetFullOn(3).ok());
    CHECK(rig.pwm.SetDuty(4, 2048).ok());
    const std::array<pca9685::Pwm, 2> bank{pca9685::PwmFullOn(), pca9685::PwmFullOn()};
    CHECK(rig.pwm.SetChannels(5, bank).ok());
    for (uint32_t ch = 0; ch < 16; ++ch) {
      CHECK(!rig.Chip().ChannelActive(ch));
    }
    CHECK(rig.Chip().Asleep());
    CHECK_EQ(rig.pwm.CheckForReset().error, Error::DeviceReset);
  }
}

TEST_CASE("A reset just before the wake write is caught by the wake check (MODE2, PRE_SCALE)") {
  // SetPwmFrequency(): read, sleep, PRE_SCALE, read back, then the wake at transfer 4.
  for (const Config& config : {pca9685::DirectLedPreset(), pca9685::ServoPreset()}) {
    Rig<MinimalPort> rig(config);
    REQUIRE(rig.Start());
    REQUIRE(rig.pwm.SetDuty(0, 100).ok());
    rig.Bus().reset_before = rig.Bus().transfers + 4;
    CHECK_EQ(rig.pwm.SetPwmFrequency(config.pwm_frequency_hz).error, Error::DeviceReset);
    CHECK(!rig.pwm.IsInitialized());
    CHECK(!rig.Chip().ChannelActive(0)); // awake, but every channel at its reset value: off
  }
}

TEST_CASE("With the reset configuration, SUBADR1 still shows a reset the wake write masked") {
  Rig<MinimalPort> rig; // Config{}: MODE2 and PRE_SCALE are the reset values, AI is masked
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.SetDuty(0, 100).ok());
  rig.Bus().reset_before = rig.Bus().transfers + 4; // before the wake write
  CHECK_EQ(rig.pwm.SetPwmFrequency(200).error, Error::DeviceReset);
  CHECK(!rig.pwm.IsInitialized());
  CHECK(!rig.Chip().ChannelActive(0));
}

TEST_CASE("A reset before any transfer of a session is reported, with the reset configuration") {
  // The session of the review probe: every offset, Config{} and ServoPreset().
  const auto session = [](pca9685::PCA9685<MinimalPort>& d) {
    bool reset = false;
    reset = reset || (d.SetDuty(0, 2048).error == Error::DeviceReset);
    reset = reset || (d.Sleep(pca9685::SleepMode::HoldOutputs).error == Error::DeviceReset);
    reset = reset || (d.Wake().error == Error::DeviceReset);
    reset = reset || (d.SetDuty(1, 1000).error == Error::DeviceReset);
    reset = reset || (d.SetPwmFrequency(200).error == Error::DeviceReset);
    reset = reset || (d.SetChannelOff(1).error == Error::DeviceReset);
    return reset || (d.CheckForReset().error == Error::DeviceReset);
  };
  for (const Config& config : {Config{}, pca9685::ServoPreset()}) {
    for (int k = 0; k < 30; ++k) {
      Rig<MinimalPort> rig(config);
      REQUIRE(rig.Start());
      rig.Bus().reset_before = rig.Bus().transfers + k;
      const bool found = session(rig.pwm);
      if (rig.Chip().resets > 0) {
        CHECK(found);
        CHECK(!rig.Chip().ChannelActive(0) && !rig.Chip().ChannelActive(1));
      }
    }
  }
}

TEST_CASE("One corrupted read is not a reset: a second read decides (rule 12d)") {
  for (std::size_t byte = 0; byte < 3; ++byte) {
    Rig<FullPort> rig;
    REQUIRE(rig.Start());
    REQUIRE(rig.pwm.SetFullOn(0).ok());
    rig.Bus().corrupt_read = rig.Bus().transfers;
    rig.Bus().corrupt_read_byte = byte;
    rig.Bus().corrupt_read_mask = (byte == 0) ? uint8_t{0x20} : uint8_t{0x04}; // AI, OUTDRV/SUBADR
    // Either the second read clears it (Ok), or the frame is inconsistent (InvalidReply): never a
    // reset, so the driver stays started and nothing is switched off.
    const Error e = rig.pwm.CheckForReset().error;
    CHECK((e == Error::Ok) || (e == Error::InvalidReply));
    CHECK(rig.pwm.IsInitialized());
    CHECK(rig.Chip().ChannelActive(0));
  }
}

TEST_CASE("After a found reset the switch-offs and Deinitialize still work (B2)") {
  Rig<FullPort> rig(pca9685::DirectLedPreset());
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.SetFullOn(0).ok());
  rig.Chip().PowerOnReset();
  CHECK_EQ(rig.pwm.CheckForReset().error, Error::DeviceReset);
  CHECK(rig.Chip().oe_high); // acted: OE high and ALL_LED_OFF_H
  CHECK(!rig.pwm.IsInitialized());
  rig.Chip().SetChannel(3, 0, 0x10, 0, 0); // whatever the chip holds now
  rig.Chip().SetReg(0x00, 0x20);
  int before = rig.Bus().transfers;
  CHECK(rig.pwm.SetChannelOff(3).ok());
  CHECK_EQ(rig.Bus().transfers, before + 1);
  CHECK(!rig.Chip().ChannelActive(3));
  rig.Chip().SetChannel(4, 0, 0x10, 0, 0);
  CHECK(rig.pwm.SetChannelsOff(0x0010).ok());
  CHECK(!rig.Chip().ChannelActive(4));
  rig.Chip().SetChannel(5, 0, 0x10, 0, 0);
  CHECK(rig.pwm.SetAllOff().ok());
  CHECK(!rig.Chip().ChannelActive(5));
  const std::array<pca9685::Pwm, 1> off{pca9685::PwmOff()};
  rig.Chip().SetChannel(6, 0, 0x10, 0, 0);
  CHECK(rig.pwm.SetChannels(6, off).ok());
  CHECK(!rig.Chip().ChannelActive(6));
  CHECK_EQ(rig.pwm.SetFullOn(6).error, Error::NotInitialized); // switch-ons wait for a start
  rig.Chip().SetChannel(7, 0, 0x10, 0, 0);
  rig.Chip().oe_high = false;
  before = rig.Bus().transfers;
  CHECK(rig.pwm.Deinitialize().ok());
  CHECK(rig.Bus().transfers > before);
  CHECK(rig.Chip().oe_high);
  CHECK(rig.Chip().Asleep());
  for (uint32_t ch = 0; ch < 16; ++ch) {
    CHECK(!rig.Chip().ChannelActive(ch));
  }
}

TEST_CASE("EnableOutputs checks for a reset before it enables OE") {
  Rig<FullPort> rig(pca9685::DirectLedPreset());
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.DisableOutputs().ok());
  rig.Chip().PowerOnReset();
  CHECK_EQ(rig.pwm.EnableOutputs().error, Error::DeviceReset);
  CHECK(rig.Chip().oe_high);
}

TEST_CASE("EnsureInitialized on a started driver checks for a reset, and starts again after one") {
  Rig<MinimalPort> rig(pca9685::ServoPreset());
  REQUIRE(rig.Start());
  rig.Chip().PowerOnReset();
  CHECK(rig.pwm.EnsureInitialized().ok());
  CHECK_EQ(rig.Chip().Reg(0xFE), 121);
  CHECK(!rig.Chip().Asleep());
  rig.Bus().fail_transfer = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.EnsureInitialized().error, Error::Bus);
  CHECK(rig.pwm.IsInitialized());
}

TEST_CASE("A reset during the start-up is reported, never taken for a started chip") {
  for (int i = 3; i < 13; ++i) {
    Rig<MinimalPort> rig(pca9685::ServoPreset());
    rig.Bus().reset_before = i;
    const auto r = rig.pwm.EnsureInitialized();
    if (r.ok()) {
      // Only a reset the later steps rewrite can end in Ok, and then the chip runs the
      // configuration.
      CHECK(i <= 6); // before the SUBADR1 write
      CHECK_EQ(rig.Chip().Reg(0xFE), 121);
      CHECK_EQ(rig.Chip().Reg(0x00), 0x20);
      CHECK_EQ(rig.Chip().Reg(0x01), 0x04);
    } else {
      CHECK(!rig.pwm.IsInitialized());
    }
  }
}

TEST_CASE("EXTCLK lost to a reset is DeviceReset") {
  Rig<MinimalPort> rig(
      Config{.oscillator_hz = 40'000'000, .clock = pca9685::ClockSource::External});
  REQUIRE(rig.Start());
  rig.Chip().SetReg(0x00, 0x20); // EXTCLK cleared, AI kept: only a reset clears the sticky bit
  CHECK_EQ(rig.pwm.CheckForReset().error, Error::DeviceReset);
}

TEST_CASE("A failed wake check stays pending: switch-ons refused, switch-offs sent") {
  Rig<FullPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.SetFullOn(0).ok());
  REQUIRE(rig.pwm.Sleep(pca9685::SleepMode::HoldOutputs).ok());
  // Wake(): read MODE1, write MODE1, then the check fails.
  rig.Bus().fail_from = rig.Bus().transfers + 2;
  CHECK_EQ(rig.pwm.Wake().error, Error::NoDevice);
  rig.Bus().fail_from = -1;
  rig.Bus().fail_transfer = rig.Bus().transfers; // the check fails once more
  const int before = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.SetFullOn(1).error, Error::NoDevice);
  CHECK_EQ(rig.Bus().transfers, before + 1); // the check only, no channel write
  CHECK(!rig.Chip().ChannelActive(1));
  CHECK_EQ(rig.pwm.EnableOutputs().error, Error::Ok); // the check runs now and passes
  rig.Bus().fail_transfer = -1;
  CHECK(rig.pwm.SetChannelOff(0).ok());
  CHECK(!rig.Chip().ChannelActive(0));
}

TEST_CASE("A pending wake check finds a reset that happened after the wake write") {
  Rig<MinimalPort> rig(pca9685::ServoPreset());
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.Sleep().ok());
  rig.Bus().fail_transfer = rig.Bus().transfers + 2; // the wake check's first read
  CHECK_EQ(rig.pwm.Wake().error, Error::Bus);
  rig.Chip().PowerOnReset();
  // A switch-off still goes out, then the check runs and finds the reset.
  CHECK_EQ(rig.pwm.SetChannelOff(3).error, Error::DeviceReset);
  CHECK(!rig.pwm.IsInitialized());
}

TEST_CASE("CheckForReset runs a pending wake check") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.Sleep().ok());
  rig.Bus().fail_transfer = rig.Bus().transfers + 3; // the wake check's PRE_SCALE read
  CHECK_EQ(rig.pwm.Wake().error, Error::Bus);
  CHECK(rig.pwm.CheckForReset().ok());
  CHECK(!rig.pwm.MayBeAsleep());
}

TEST_CASE("The group addresses are off once started: another master cannot write the chip") {
  Rig<MinimalPort> rig;
  const std::array<uint8_t, 5> on{0x06, 0x00, 0x10, 0x00, 0x00};
  CHECK_EQ(rig.Chip().Write(0x70, on.data(), on.size(), on.size()), 5); // at power-up it answers
  REQUIRE(rig.Start());
  CHECK(!rig.Chip().ChannelActive(0)); // the start-up switched it off
  CHECK_EQ(rig.Chip().Write(0x70, on.data(), on.size(), on.size()), -1);
  CHECK(!rig.Chip().ChannelActive(0));
}

TEST_CASE("A reset just before the RESTART write is caught: that write wakes the chip too") {
  for (const Config& config : {pca9685::DirectLedPreset(), pca9685::ServoPreset()}) {
    Rig<MinimalPort> rig(config);
    REQUIRE(rig.Start());
    REQUIRE(rig.pwm.SetFullOn(0).ok());
    REQUIRE(rig.pwm.Sleep(pca9685::SleepMode::HoldOutputs).ok());
    // Wake(): read, wake write, check (2 reads), then the RESTART write at transfer 4.
    rig.Bus().reset_before = rig.Bus().transfers + 4;
    CHECK_EQ(rig.pwm.Wake().error, Error::DeviceReset);
    CHECK(!rig.pwm.IsInitialized());
    CHECK(!rig.Chip().ChannelActive(0));
  }
}
