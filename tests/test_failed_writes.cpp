/**
 * @file test_failed_writes.cpp
 * @brief A write whose transfer failed may have taken effect, in whole or in part; a write the chip
 * acknowledged may have been dropped; and no uncertainty ever refuses a switch-off.
 */
#include <array>
#include <cstddef>
#include <cstdint>

#include "fixture.hpp"
#include "test_harness.hpp"

namespace {

using pca9685::Config;
using pca9685::Error;
using pca9685::Pwm;
using pca9685_test::FullPort;
using pca9685_test::MinimalPort;
using pca9685_test::Rig;

} // namespace

TEST_CASE("A NACK part-way through a channel write: the bytes before it go live at the STOP, "
          "the channel unknown") {
  for (std::size_t after = 1; after < 5; ++after) {
    Rig<FullPort> rig;
    REQUIRE(rig.Start());
    REQUIRE(rig.pwm.SetDuty(2, 1000).ok());
    rig.Bus().nack_transfer = rig.Bus().transfers;
    rig.Bus().nack_after = after; // the control register, then ON_L, ON_H, OFF_L
    CHECK_EQ(rig.pwm.SetFullOn(2).error, Error::Bus);
    // After ON_H (after >= 3) the chip runs full ON with the old OFF bytes: a mixed value.
    CHECK(rig.Chip().ChannelActive(2));
    CHECK_EQ(rig.pwm.GetUncertainChannels(), 0x0004);
    CHECK_EQ(rig.pwm.GetChannel(2).error, Error::StateUnknown);
    CHECK_EQ(rig.pwm.EnableOutputs().error, Error::StateUnknown); // a switch-on of every channel
    // ReadChannel makes it known again, with what the chip holds.
    const auto read = rig.pwm.ReadChannel(2);
    CHECK(read.ok() && (read.value.full_on == (after >= 3)));
    CHECK_EQ(rig.pwm.GetUncertainChannels(), 0);
    CHECK(rig.pwm.EnableOutputs().ok());
  }
}

TEST_CASE("A lost ACK: the write took effect, the driver treats the channel as unknown") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.Bus().lost_ack_transfer = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.SetFullOn(9).error, Error::Bus);
  CHECK(rig.Chip().ChannelActive(9));
  CHECK_EQ(rig.pwm.GetChannel(9).error, Error::StateUnknown);
  // A successful write makes it known again.
  CHECK(rig.pwm.SetChannelOff(9).ok());
  CHECK(rig.pwm.GetChannel(9).value == pca9685::PwmOff());
}

TEST_CASE("An address NACK proves nothing arrived: NoDevice, the copy kept") {
  Rig<FullPort> rig;
  REQUIRE(rig.Start());
  rig.Bus().fail_transfer = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.SetFullOn(9).error, Error::NoDevice);
  CHECK_EQ(rig.pwm.GetUncertainChannels(), 0);
  CHECK(rig.pwm.GetChannel(9).value == pca9685::PwmOff());
  rig.Bus().fail_transfer = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.SetAllOff().error, Error::NoDevice);
  CHECK_EQ(rig.pwm.GetUncertainChannels(), 0);
}

TEST_CASE("A switch-off is one data byte: stored or not, it never makes a channel unknown") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.SetFullOn(3).ok());
  rig.Bus().fail_transfer = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.SetAllOff().error, Error::Bus);
  CHECK_EQ(rig.pwm.GetUncertainChannels(), 0); // as before (on), or off: the copy keeps "on"
  CHECK(rig.pwm.GetChannel(3).value.full_on);
  rig.Bus().lost_ack_transfer = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.SetChannelOff(3).error, Error::Bus);
  CHECK(!rig.Chip().ChannelActive(3)); // it was stored; the copy still assumes the riskier "on"
  CHECK(rig.pwm.SetAllOff().ok());
  CHECK(rig.pwm.GetChannel(3).value.full_off);
}

TEST_CASE("A NACK at any byte of any switch-off never makes a channel more active (B1)") {
  // A phase-shifted channel: on from count 4000 to 4095. A 4-byte off burst NACKed after ON_H
  // would have left ON = 0 with the old OFF = 4095: nearly 100 % on.
  const auto duty = [](const pca9685_test::Pca9685Model& chip) -> uint32_t {
    const auto b = chip.Live(0);
    if (((b[3] & 0x10U) != 0U)) {
      return 0U;
    }
    if (((b[1] & 0x10U) != 0U)) {
      return 4096U;
    }
    const uint32_t on = ((b[1] & 0x0FU) << 8U) | b[0];
    const uint32_t off = ((b[3] & 0x0FU) << 8U) | b[2];
    return (on == off) ? 0U : ((off - on) & 0x0FFFU);
  };
  for (int call = 0; call < 6; ++call) {
    for (std::size_t after = 1; after < 3; ++after) {
      Rig<MinimalPort> rig;
      REQUIRE(rig.Start());
      REQUIRE(rig.pwm.SetPwm(0, Pwm{.on_count = 4000, .off_count = 4095, .full_off = false}).ok());
      const uint32_t before = duty(rig.Chip());
      rig.Bus().nack_transfer = rig.Bus().transfers;
      rig.Bus().nack_after = after; // 1: only the register address; 2: never (one data byte)
      const std::array<Pwm, 1> off{pca9685::PwmOff()};
      Error e = Error::Ok;
      if (call == 0) {
        e = rig.pwm.SetChannelOff(0).error;
      } else if (call == 1) {
        e = rig.pwm.SetAllOff().error;
      } else if (call == 2) {
        e = rig.pwm.SetChannelsOff(0x0001).error;
      } else if (call == 3) {
        e = rig.pwm.Sleep(pca9685::SleepMode::OutputsOff).error;
      } else if (call == 4) {
        e = rig.pwm.Deinitialize().error;
      } else {
        e = rig.pwm.SetChannels(0, off).error;
      }
      CHECK(duty(rig.Chip()) <= before);
      CHECK((e != Error::Ok) || (duty(rig.Chip()) == 0U));
    }
  }
}

TEST_CASE("Switch-offs are sent whatever the driver doubts") {
  // Each doubt the driver can hold, and the switch-off still reaches the chip.
  for (int doubt = 0; doubt < 4; ++doubt) {
    Rig<MinimalPort> rig;
    REQUIRE(rig.Start());
    REQUIRE(rig.pwm.SetFullOn(1).ok());
    REQUIRE(rig.pwm.SetFullOn(2).ok());
    if (doubt == 0) { // channel 1 unknown
      rig.Bus().lost_ack_transfer = rig.Bus().transfers;
      REQUIRE(!rig.pwm.SetDuty(1, 5).ok());
    } else if (doubt == 1) {                             // the prescaler unknown
      rig.Bus().fail_transfer = rig.Bus().transfers + 2; // the PRE_SCALE write
      REQUIRE(!rig.pwm.SetPwmFrequency(100).ok());
    } else if (doubt == 2) { // a wake check pending that keeps failing
      REQUIRE(rig.pwm.Sleep(pca9685::SleepMode::HoldOutputs).ok());
      rig.Bus().fail_transfer = rig.Bus().transfers + 2;
      REQUIRE(!rig.pwm.Wake().ok());
    } else { // asleep holding the outputs
      REQUIRE(rig.pwm.Sleep(pca9685::SleepMode::HoldOutputs).ok());
    }
    rig.Bus().fail_transfer = -1;
    rig.Bus().lost_ack_transfer = -1;
    const int before = rig.Bus().transfers;
    const auto r = rig.pwm.SetChannelOff(1);
    CHECK(rig.Bus().transfers > before);
    CHECK(pca9685::IsOff(pca9685_test::Decode(pca9685_test::Stored(rig.Chip(), 1))));
    CHECK((r.error == Error::Ok) || (r.error == Error::Asleep));
    CHECK((r.error == Error::Ok) == !rig.Chip().ChannelActive(1));
    CHECK(rig.pwm.SetChannelsOff(0x0004).error == r.error);
    CHECK(rig.pwm.SetAllOff().error == r.error);
  }
}

TEST_CASE("A switch-off whose wake check fails is still sent first") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.SetFullOn(1).ok());
  REQUIRE(rig.pwm.Sleep(pca9685::SleepMode::HoldOutputs).ok());
  rig.Bus().fail_from = rig.Bus().transfers + 2;
  REQUIRE(!rig.pwm.Wake().ok());
  rig.Bus().fail_from = -1;
  rig.Bus().fail_transfer = rig.Bus().transfers + 1; // the check, after the off write
  CHECK_EQ(rig.pwm.SetChannelOff(1).error, Error::Bus);
  CHECK(pca9685::IsOff(pca9685_test::Decode(pca9685_test::Stored(rig.Chip(), 1))));
}

TEST_CASE("SetChannelsOff keeps going after a failed write") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  for (uint8_t ch = 0; ch < 4; ++ch) {
    REQUIRE(rig.pwm.SetFullOn(ch).ok());
  }
  rig.Bus().fail_transfer = rig.Bus().transfers + 1; // channel 1
  CHECK_EQ(rig.pwm.SetChannelsOff(0x000F).error, Error::Bus);
  CHECK(!rig.Chip().ChannelActive(0) && rig.Chip().ChannelActive(1) &&
        !rig.Chip().ChannelActive(2) && !rig.Chip().ChannelActive(3));
  CHECK(rig.pwm.GetChannel(1).value.full_on); // not stored: as before
}

TEST_CASE("A bank call refused by a failing wake check still sends its switch-offs") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.SetFullOn(4).ok());
  REQUIRE(rig.pwm.Sleep().ok());
  rig.Bus().fail_from = rig.Bus().transfers + 2;
  REQUIRE(!rig.pwm.Wake().ok());
  rig.Bus().fail_from = -1;
  rig.Bus().fail_transfer = rig.Bus().transfers + 1; // the check, after the switch-off byte
  const std::array<Pwm, 2> values{pca9685::PwmFullOn(), pca9685::PwmOff()};
  CHECK_EQ(rig.pwm.SetChannels(4, values).error, Error::Bus);
  CHECK(pca9685::IsOff(pca9685_test::Decode(pca9685_test::Stored(rig.Chip(), 5))));
  CHECK(!rig.Chip().ChannelActive(4));
  // An all-off bank call is sent first and runs the check after it.
  const std::array<Pwm, 2> offs{};
  rig.Bus().fail_transfer = -1;
  CHECK(rig.pwm.SetChannels(4, offs).ok());
}

TEST_CASE("A refused bank call whose switch-off write fails still returns the refusal") {
  Rig<FullPort> rig;
  REQUIRE(rig.Start());
  const std::array<Pwm, 2> values{Pwm{.on_count = 1, .off_count = 1, .full_off = false},
                                  pca9685::PwmOff()};
  rig.Bus().fail_transfer = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.SetChannels(0, values).error, Error::InvalidArgument);
  CHECK(rig.port.log_lines > 0);
}

TEST_CASE("A failed PRE_SCALE write: the prescaler unknown, pulse widths refused, the chip woken") {
  Rig<MinimalPort> rig(pca9685::ServoPreset());
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.SetPulseWidthUs(0, 1500).ok());
  rig.Bus().lost_ack_transfer = rig.Bus().transfers + 2;
  CHECK_EQ(rig.pwm.SetPwmFrequency(60).error, Error::Bus);
  CHECK(!rig.Chip().Asleep()); // woken again: asleep it would hold the outputs
  // The wake check read PRE_SCALE, so the prescaler is known again, as the chip holds it.
  CHECK_EQ(rig.pwm.GetPrescale().value, rig.Chip().Reg(0xFE));
  // Now a PRE_SCALE read-back that fails too.
  rig.Bus().fail_transfer = rig.Bus().transfers + 3;
  rig.Bus().fail_from = rig.Bus().transfers + 3;
  CHECK_EQ(rig.pwm.SetPwmFrequency(70).error, Error::Bus);
  rig.Bus().fail_from = -1;
  rig.Bus().fail_transfer = -1;
  CHECK_EQ(rig.pwm.GetPrescale().error, Error::StateUnknown);
  CHECK_EQ(rig.pwm.GetPwmFrequencyHz().error, Error::StateUnknown);
  const int before = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.SetPulseWidthUs(0, 1500).error, Error::StateUnknown);
  CHECK_EQ(rig.Bus().transfers, before);
  // The bus died before the wake too: the chip is asleep, holding its outputs. The switch-off is
  // still sent; its wake check reads PRE_SCALE (known again) and finds SLEEP set.
  CHECK_EQ(rig.pwm.SetPulseWidthUs(0, 0).error, Error::Asleep);
  CHECK(rig.Bus().transfers > before);
  CHECK(rig.pwm.GetPrescale().ok());
  CHECK(rig.pwm.Wake().ok());
  CHECK(rig.pwm.SetPulseWidthUs(0, 1500).ok());
}

TEST_CASE("ReadPrescale makes the prescaler known again") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.Sleep().ok());
  rig.Bus().lost_ack_transfer = rig.Bus().transfers + 1; // PRE_SCALE, asleep: no wake after
  CHECK_EQ(rig.pwm.SetPwmFrequency(100).error, Error::Bus);
  CHECK_EQ(rig.pwm.GetPrescale().error, Error::StateUnknown);
  CHECK_EQ(rig.pwm.ReadPrescale().value, 0x3C);
  CHECK_EQ(rig.pwm.GetPrescale().value, 0x3C);
}

TEST_CASE("PRE_SCALE ignores a write while awake: the read-back finds it (table 4 note 1)") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.Bus().drop_transfer = rig.Bus().transfers + 1; // the sleep write, acknowledged and lost
  CHECK_EQ(rig.pwm.SetPwmFrequency(100).error, Error::WriteMismatch);
  CHECK_EQ(rig.Chip().blocked_prescale_writes, 1);
  CHECK_EQ(rig.pwm.GetPrescale().value, 0x1E); // the chip's value
  CHECK_EQ(rig.pwm.GetConfig().pwm_frequency_hz, 200U);
}

TEST_CASE("A failed sleep write in SetPwmFrequency: nothing more written, the chip woken") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.Bus().fail_transfer = rig.Bus().transfers + 1;
  CHECK_EQ(rig.pwm.SetPwmFrequency(100).error, Error::Bus);
  CHECK_EQ(rig.Chip().Reg(0xFE), 0x1E);
  CHECK(!rig.Chip().Asleep());
  CHECK(!rig.pwm.MayBeAsleep());
}

TEST_CASE("A failed sleep or wake write leaves the driver assuming the riskier state") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.Bus().lost_ack_transfer = rig.Bus().transfers + 1; // the MODE1 write: asleep after all
  CHECK_EQ(rig.pwm.Sleep(pca9685::SleepMode::HoldOutputs).error, Error::Bus);
  CHECK(rig.pwm.MayBeAsleep());
  CHECK_EQ(rig.pwm.SetChannelOff(0).error, Error::Asleep);
  CHECK(rig.pwm.Wake().ok());
  // A wake write lost: the check sees SLEEP still 1.
  REQUIRE(rig.pwm.Sleep().ok());
  rig.Bus().drop_transfer = rig.Bus().transfers + 1;
  CHECK_EQ(rig.pwm.Wake().error, Error::WriteMismatch);
  CHECK(rig.pwm.MayBeAsleep());
  CHECK(rig.pwm.Wake().ok());
}

TEST_CASE("A failed off write in Sleep(OutputsOff) does not put the chip to sleep") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.SetFullOn(0).ok());
  rig.Bus().fail_transfer = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.Sleep().error, Error::Bus);
  CHECK(!rig.Chip().Asleep());
  rig.Bus().fail_transfer = rig.Bus().transfers + 1; // the reset check read
  CHECK_EQ(rig.pwm.Sleep().error, Error::Bus);
  CHECK(!rig.Chip().Asleep());
}

TEST_CASE("A failed RESTART write is reported; the channels stay stopped, not on") {
  Rig<MinimalPort> rig;
  rig.Chip().readings.wake_runs_without_restart = false;
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.SetFullOn(0).ok());
  REQUIRE(rig.pwm.Sleep(pca9685::SleepMode::HoldOutputs).ok());
  rig.Bus().fail_transfer = rig.Bus().transfers + 4; // read, wake, check x2, RESTART
  CHECK_EQ(rig.pwm.Wake().error, Error::Bus);
  CHECK(!rig.Chip().ChannelActive(0));
}

TEST_CASE("Start-up: a failed switch-off frame does not stop the others") {
  for (int i = 0; i < 3; ++i) {
    Rig<FullPort> rig;
    rig.Chip().SetReg(0x00, 0x20);
    rig.Chip().SetChannel(0, 0, 0x10, 0, 0);
    rig.Bus().fail_transfer = i;
    CHECK_EQ(rig.pwm.EnsureInitialized().error, Error::NoDevice);
    CHECK_EQ(rig.Bus().log.size(), 3U);
    CHECK(!rig.Chip().ChannelActive(0));
  }
  Rig<FullPort> rig;
  rig.port.gpio_ok = false;
  CHECK_EQ(rig.pwm.EnsureInitialized().error, Error::Bus);
  CHECK_EQ(rig.Bus().log.size(), 3U);
}

TEST_CASE("Start-up: a NACK in the identity reads") {
  for (int i = 6; i < 9; ++i) {
    Rig<MinimalPort> rig;
    rig.Bus().fail_transfer = i;
    CHECK_EQ(rig.pwm.EnsureInitialized().error, Error::Bus);
  }
  Rig<MinimalPort> ext(
      Config{.oscillator_hz = 50'000'000, .clock = pca9685::ClockSource::External});
  ext.Bus().fail_transfer = 5; // the EXTCLK write
  CHECK_EQ(ext.pwm.EnsureInitialized().error, Error::Bus);
}

TEST_CASE("A bank write that fails: Bus, every channel of it unknown") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const std::array<Pwm, 3> values{pca9685::PwmFullOn(), pca9685::PwmOff(), pca9685::PwmFullOn()};
  rig.Bus().lost_ack_transfer = rig.Bus().transfers + 1; // the burst, after the switch-off byte
  CHECK_EQ(rig.pwm.SetChannels(2, values).error, Error::Bus);
  CHECK_EQ(rig.pwm.GetUncertainChannels(), 0x001C);
}

TEST_CASE("A pending wake check that fails refuses EnableOutputs and SetPwmFrequency") {
  Rig<FullPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.Sleep().ok());
  rig.Bus().fail_from = rig.Bus().transfers + 2;
  REQUIRE(!rig.pwm.Wake().ok());
  const int before = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.EnableOutputs().error, Error::NoDevice);
  CHECK_EQ(rig.pwm.SetPwmFrequency(100).error, Error::NoDevice);
  CHECK_EQ(rig.Bus().transfers, before + 2); // the two checks, nothing else
  CHECK_EQ(rig.port.gpio_writes, 2);         // OE untouched since the start-up
}

TEST_CASE("After a failed wake write, a read alone never clears the held doubt (rule 12d)") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.SetFullOn(0).ok());
  REQUIRE(rig.pwm.Sleep(pca9685::SleepMode::HoldOutputs).ok());
  // A channel write while asleep clears RESTART (section 7.3.1.1), so no RESTART write follows.
  REQUIRE(rig.pwm.SetChannelOff(1).error == Error::Asleep);
  REQUIRE(!rig.Chip().Restart());
  rig.Bus().fail_transfer = rig.Bus().transfers + 1; // the wake write
  CHECK_EQ(rig.pwm.Wake().error, Error::Bus);
  // A read now shows SLEEP = 0 (a line glitch, or another master): one frame, no proof.
  rig.Chip().SetReg(0x00, 0x20);
  CHECK(rig.pwm.CheckForReset().ok());
  rig.Chip().SetReg(0x00, 0x30); // the chip was asleep after all, holding channel 0 on
  CHECK_EQ(rig.pwm.SetChannelOff(0).error, Error::Asleep);
  CHECK(rig.pwm.MayBeAsleep());
  CHECK(rig.pwm.Wake().ok());
  CHECK(rig.pwm.SetChannelOff(0).ok());
  CHECK(!rig.Chip().ChannelActive(0));
}
