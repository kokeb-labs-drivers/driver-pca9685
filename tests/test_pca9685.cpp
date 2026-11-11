/**
 * @file test_pca9685.cpp
 * @brief The driver over the register model: start-up, channels, frequency, sleep and restart, OE
 * and diagnostics, with the exact frames the data sheet asks for.
 */
#include <array>
#include <cstdint>
#include <vector>

#include "fixture.hpp"
#include "test_harness.hpp"

namespace {

using pca9685::Config;
using pca9685::Error;
using pca9685::Pwm;
using pca9685::Verify;
using pca9685_test::FullPort;
using pca9685_test::Level;
using pca9685_test::MinimalPort;
using pca9685_test::Rig;

bool IsWrite(const pca9685_test::Frame& f, const std::vector<uint8_t>& bytes) {
  return f.write && (f.bytes == bytes);
}
bool IsRead(const pca9685_test::Frame& f, uint8_t reg, std::size_t n) {
  return !f.write && (f.bytes.size() == 1U) && (f.bytes[0] == reg) && (f.rx_len == n);
}

} // namespace

// ---- Start-up -----------------------------------------------------------------------------------

TEST_CASE("Construction does no I/O") {
  Rig<MinimalPort> rig;
  CHECK_EQ(rig.Bus().transfers, 0);
  CHECK(!rig.pwm.IsInitialized());
  CHECK_EQ(rig.pwm.GetAddress(), 0x40);
}

TEST_CASE("Start-up: the exact frames, in the data sheet's order") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const auto& log = rig.Bus().log;
  REQUIRE(log.size() == 13U);
  CHECK(IsWrite(log[0], {0xFD, 0x10}));  // ALL_LED_OFF_H: everything off
  CHECK(IsWrite(log[1], {0x01, 0x04}));  // MODE2: OCH = 0, totem pole, OUTNE = 00
  CHECK(IsWrite(log[2], {0xFD, 0x10}));  // off again, now at the STOP
  CHECK(IsRead(log[3], 0x00, 2));        // MODE1 (and the byte after)
  CHECK(IsWrite(log[4], {0x00, 0x30}));  // SLEEP and AI
  CHECK(IsWrite(log[5], {0xFE, 0x1E}));  // PRE_SCALE, 200 Hz
  CHECK(IsWrite(log[6], {0x02, 0x5A}));  // SUBADR1: the reset sentinel
  CHECK(IsRead(log[7], 0x00, 3));        // identity: MODE1, MODE2, SUBADR1
  CHECK(IsRead(log[8], 0xFE, 1));        // PRE_SCALE
  CHECK(IsRead(log[9], 0x06, 64));       // every channel
  CHECK(IsWrite(log[10], {0x00, 0x20})); // wake
  CHECK(IsRead(log[11], 0x00, 3));       // wake check
  CHECK(IsRead(log[12], 0xFE, 1));
  CHECK(!rig.Chip().Answers(0x2D));    // SUB1 stays clear: the sentinel address is inert
  CHECK_EQ(rig.port.delayed_us, 500U); // table 5 note 2
  CHECK(rig.pwm.IsInitialized());
  CHECK(!rig.Chip().Asleep());
  CHECK_EQ(rig.Chip().Reg(0x00), 0x20);
  CHECK_EQ(rig.Chip().startup_pwm_accesses, 0);
  CHECK_EQ(rig.Chip().blocked_prescale_writes, 0);
  CHECK(!rig.Chip().Answers(0x70)); // LED All Call off
  // A second call runs the reset check (one read) and is Ok.
  CHECK(rig.pwm.EnsureInitialized().ok());
  CHECK_EQ(rig.Bus().transfers, 14);
  CHECK(IsRead(rig.Bus().log.back(), 0x00, 3));
  CHECK_EQ(rig.port.bus_up_calls, 1);
  CHECK_EQ(rig.pwm.GetPrescale().value, 0x1E);
  CHECK_EQ(rig.pwm.GetPwmFrequencyHz().value, 197U);
  for (uint8_t ch = 0; ch < 16; ++ch) {
    CHECK(rig.pwm.GetChannel(ch).value == pca9685::PwmOff());
  }
}

TEST_CASE("Start-up with OE: disabled before the first frame, enabled after the last") {
  Rig<FullPort> rig;
  REQUIRE(rig.Start());
  CHECK_EQ(rig.port.gpio_writes, 2);
  CHECK(!rig.Chip().oe_high);
  CHECK(rig.port.log_lines >= 1);
  CHECK_EQ(rig.port.error_lines, 0);
}

TEST_CASE("Start-up switches off a chip an earlier run left running, under every reading") {
  for (uint32_t reading = 0U; reading < 8U; ++reading) {
    Rig<FullPort> rig(pca9685::DirectLedPreset());
    auto& chip = rig.Chip();
    chip.readings.sleep_holds_outputs = (reading & 1U) != 0U;
    chip.readings.ack_needs_all_four = (reading & 2U) != 0U;
    chip.readings.wake_runs_without_restart = (reading & 4U) != 0U;
    chip.SetReg(0x00, 0x20); // awake, AI
    chip.SetReg(0x01, 0x0C); // OCH = 1, totem pole, not inverted
    for (uint32_t ch = 0; ch < 16; ++ch) {
      chip.SetChannel(ch, 0, 0x10, 0, 0); // full ON
    }
    REQUIRE(rig.Start());
    for (uint32_t ch = 0; ch < 16; ++ch) {
      CHECK(!chip.ChannelActive(ch));
      CHECK(pca9685_test::PinOff(chip, ch, rig.pwm.GetConfig()));
    }
    CHECK_EQ(chip.Reg(0x01), 0x12); // INVRT, open drain, OUTNE = 10
  }
}

TEST_CASE("Start-up from a chip asleep with held outputs and RESTART set") {
  Rig<MinimalPort> rig;
  auto& chip = rig.Chip();
  const std::array<uint8_t, 2> wake{0x00, 0x20};
  const std::array<uint8_t, 2> sleep{0x00, 0x30};
  (void)chip.Write(0x40, wake.data(), 2, 2);
  chip.Elapse(500);
  chip.SetChannel(5, 0, 0x10, 0, 0);
  (void)chip.Write(0x40, sleep.data(), 2, 2);
  REQUIRE(chip.Restart());
  REQUIRE(rig.Start());
  CHECK(!chip.Restart()); // the channel write cleared it
  CHECK(!chip.ChannelActive(5));
  CHECK(!rig.pwm.MayBeAsleep());
}

TEST_CASE("Start-up refuses a bad configuration or address with no I/O") {
  {
    Rig<MinimalPort> rig(Config{.pwm_frequency_hz = 5000});
    CHECK_EQ(rig.pwm.EnsureInitialized().error, Error::InvalidArgument);
    CHECK_EQ(rig.Bus().transfers, 0);
  }
  for (const uint8_t address : {uint8_t{0x3F}, uint8_t{0x70}, uint8_t{0x80}}) {
    Rig<MinimalPort> rig(Config{}, address);
    CHECK_EQ(rig.pwm.EnsureInitialized().error, Error::InvalidArgument);
    CHECK_EQ(rig.Bus().transfers, 0);
    CHECK_EQ(rig.port.bus_up_calls, 0);
  }
  Rig<MinimalPort> rig;
  rig.Bus().bus_up_ok = false;
  CHECK_EQ(rig.pwm.EnsureInitialized().error, Error::Bus);
  CHECK_EQ(rig.Bus().transfers, 0);
}

TEST_CASE("Start-up at another address: A5..A0 select 0x40 to 0x7F") {
  Rig<MinimalPort> rig(Config{}, 0x55);
  rig.Chip().own_address = 0x55;
  CHECK(rig.Start());
  Rig<MinimalPort> wrong(Config{}, 0x41);
  CHECK_EQ(wrong.pwm.EnsureInitialized().error, Error::Bus);
}

TEST_CASE("Start-up: EXTCLK set on the chip but the internal clock asked for") {
  Rig<MinimalPort> rig;
  rig.Chip().SetReg(0x00, 0x70); // asleep, EXTCLK, AI
  rig.Chip().SetReg(0x01, 0x04);
  CHECK_EQ(rig.pwm.EnsureInitialized().error, Error::ExternalClockLatched);
  CHECK(!rig.pwm.IsInitialized());
  for (uint32_t ch = 0; ch < 16; ++ch) {
    CHECK(!rig.Chip().ChannelActive(ch)); // switched off before the refusal
  }
}

TEST_CASE("Start-up with EXTCLK: SLEEP first, then SLEEP and EXTCLK; no start-up wait") {
  Rig<MinimalPort> rig(
      Config{.oscillator_hz = 50'000'000, .clock = pca9685::ClockSource::External});
  REQUIRE(rig.Start());
  const auto& log = rig.Bus().log;
  CHECK(IsWrite(log[4], {0x00, 0x30}));
  CHECK(IsWrite(log[5], {0x00, 0x70}));
  CHECK_EQ(rig.Chip().Reg(0x00), 0x60);
  CHECK_EQ(rig.port.delayed_us, 0U);
  // 200 Hz from 50 MHz: PRE_SCALE 60.
  CHECK_EQ(rig.Chip().Reg(0xFE), 60);
  // A chip that already has EXTCLK set needs only the one write.
  Rig<MinimalPort> again(
      Config{.oscillator_hz = 50'000'000, .clock = pca9685::ClockSource::External});
  again.Chip().SetReg(0x00, 0x50);
  REQUIRE(again.Start());
  CHECK(IsWrite(again.Bus().log[4], {0x00, 0x70}));
  CHECK(IsWrite(again.Bus().log[5], {0xFE, 60}));
}

TEST_CASE("Start-up from a chip an earlier run left with OCH = 1: OCH = 0 from then on") {
  for (const bool four : {true, false}) {
    Rig<MinimalPort> rig;
    rig.Chip().readings.ack_needs_all_four = four;
    rig.Chip().SetReg(0x00, 0x20);
    rig.Chip().SetReg(0x01, 0x0C);
    rig.Chip().SetChannel(4, 0, 0x10, 0, 0);
    REQUIRE(rig.Start());
    CHECK_EQ(rig.Chip().Reg(0x01), 0x04);
    CHECK(!rig.Chip().ChannelActive(4));
  }
}

TEST_CASE("Start-up: a failure at any transfer leaves the driver uninitialised; a retry works") {
  for (int i = 0; i < 13; ++i) {
    Rig<MinimalPort> rig;
    rig.Bus().fail_transfer = i;
    CHECK_EQ(rig.pwm.EnsureInitialized().error, Error::Bus);
    CHECK(!rig.pwm.IsInitialized());
    if (i < 3) {
      CHECK(rig.Bus().log.size() == 3U); // the three switch-off frames are all sent
    }
    rig.Bus().fail_transfer = -1;
    CHECK(rig.Start());
  }
}

TEST_CASE("Start-up: the chip does not wake (the wake write lost)") {
  Rig<MinimalPort> rig;
  rig.Bus().drop_transfer = 10;
  CHECK_EQ(rig.pwm.EnsureInitialized().error, Error::WriteMismatch);
  CHECK(!rig.pwm.IsInitialized());
}

TEST_CASE("Start-up: the OE enable write fails") {
  Rig<FullPort> rig;
  rig.port.gpio_ok = false;
  CHECK_EQ(rig.pwm.EnsureInitialized().error, Error::Bus);
  CHECK(!rig.pwm.IsInitialized());
}

TEST_CASE("Every call before EnsureInitialized() returns NotInitialized and sends nothing") {
  Rig<FullPort> rig;
  auto& d = rig.pwm;
  const std::array<Pwm, 2> values{};
  CHECK_EQ(d.SetPwm(0, pca9685::PwmFullOn()).error, Error::NotInitialized);
  CHECK_EQ(d.SetDuty(0, 100).error, Error::NotInitialized);
  CHECK_EQ(d.SetPulseWidthUs(0, 1500).error, Error::NotInitialized);
  CHECK_EQ(d.SetPulseWidthUs(0, 0).error, Error::NotInitialized);
  CHECK_EQ(d.SetFullOn(0).error, Error::NotInitialized);
  CHECK_EQ(d.SetChannelOff(0).error, Error::NotInitialized); // never started: nothing to stop
  CHECK_EQ(d.SetChannels(0, values).error, Error::NotInitialized);
  CHECK_EQ(d.SetChannelsOff(1).error, Error::NotInitialized);
  CHECK_EQ(d.SetAllOff().error, Error::NotInitialized);
  CHECK_EQ(d.GetChannel(0).error, Error::NotInitialized);
  CHECK_EQ(d.ReadChannel(0).error, Error::NotInitialized);
  CHECK_EQ(d.ReadChannels().error, Error::NotInitialized);
  CHECK_EQ(d.EnableOutputs().error, Error::NotInitialized);
  CHECK_EQ(d.SetPwmFrequency(50).error, Error::NotInitialized);
  CHECK_EQ(d.GetPrescale().error, Error::NotInitialized);
  CHECK_EQ(d.GetPwmFrequencyHz().error, Error::NotInitialized);
  CHECK_EQ(d.ReadPrescale().error, Error::NotInitialized);
  CHECK_EQ(d.Sleep().error, Error::NotInitialized);
  CHECK_EQ(d.Wake().error, Error::NotInitialized);
  CHECK_EQ(d.CheckForReset().error, Error::NotInitialized);
  CHECK_EQ(d.ReadRegister(0).error, Error::NotInitialized);
  CHECK_EQ(rig.Bus().transfers, 0);
  // DisableOutputs() is a pin write: it works before the start.
  CHECK(d.DisableOutputs().ok());
  CHECK(rig.Chip().oe_high);
  // Deinitialize() always does its work, even never started: OE high, every channel off, asleep.
  rig.Chip().SetReg(0x00, 0x20); // a chip an earlier run left running
  rig.Chip().SetChannel(2, 0, 0x10, 0, 0);
  CHECK(d.Deinitialize().ok());
  CHECK(!rig.Chip().ChannelActive(2));
  CHECK(rig.Chip().Asleep());
  CHECK(IsWrite(rig.Bus().log[0], {0xFD, 0x10}));
}

// ---- Channels -----------------------------------------------------------------------------------

TEST_CASE("SetDuty: one 4-byte write with the phase delay (section 7.3.3)") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const int before = rig.Bus().transfers;
  CHECK(rig.pwm.SetDuty(3, 819, 409).ok());
  CHECK_EQ(rig.Bus().transfers, before + 1);
  CHECK(IsWrite(rig.Bus().log.back(), {0x12, 0x99, 0x01, 0xCC, 0x04})); // example 1
  CHECK(rig.Chip().ChannelActive(3));
  CHECK(rig.Chip().PinLevel(3) == Level::High);
  const auto value = rig.pwm.GetChannel(3);
  CHECK(value.ok() && value.value.on_count == 409 && value.value.off_count == 1228);
  CHECK(rig.pwm.SetDuty(4, 3686, 3685).ok());
  CHECK(IsWrite(rig.Bus().log.back(), {0x16, 0x65, 0x0E, 0xCB, 0x0C})); // example 2
  CHECK(rig.pwm.SetDuty(4, 0).ok());
  CHECK(!rig.Chip().ChannelActive(4));
  CHECK(rig.pwm.SetDuty(4, pca9685::kFullDuty).ok());
  CHECK(IsWrite(rig.Bus().log.back(), {0x16, 0x00, 0x10, 0x00, 0x00}));
  const int now = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.SetDuty(4, 4097).error, Error::InvalidArgument);
  CHECK_EQ(rig.pwm.SetDuty(4, 10, 4096).error, Error::InvalidArgument);
  CHECK_EQ(rig.pwm.SetDuty(16, 10).error, Error::InvalidArgument);
  CHECK_EQ(rig.Bus().transfers, now);
}

TEST_CASE("SetFullOn, SetChannelOff and SetPwm: frames and outputs (table 7)") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  CHECK(rig.pwm.SetFullOn(15).ok());
  CHECK(IsWrite(rig.Bus().log.back(), {0x42, 0x00, 0x10, 0x00, 0x00}));
  CHECK(rig.Chip().ChannelActive(15));
  CHECK(rig.pwm.SetChannelOff(15).ok());
  CHECK(IsWrite(rig.Bus().log.back(), {0x45, 0x10})); // one data byte: LED15_OFF_H
  CHECK(!rig.Chip().ChannelActive(15));
  // An off value is a switch-off: LEDn_OFF_H = 0x10, whatever else it holds.
  CHECK(rig.pwm.SetPwm(1, Pwm{.on_count = 9000, .off_count = 5, .full_on = true, .full_off = true})
            .ok());
  CHECK(IsWrite(rig.Bus().log.back(), {0x0D, 0x10}));
  const int now = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.SetPwm(1, Pwm{.on_count = 5, .off_count = 5, .full_off = false}).error,
           Error::InvalidArgument);
  CHECK_EQ(rig.pwm.SetPwm(1, Pwm{.on_count = 4096, .full_off = false}).error,
           Error::InvalidArgument);
  CHECK_EQ(rig.pwm.SetFullOn(1, pca9685_test::NotAnEnumerator<Verify>(7)).error,
           Error::InvalidArgument);
  CHECK_EQ(rig.pwm.SetChannelOff(16).error, Error::InvalidArgument);
  CHECK_EQ(rig.Bus().transfers, now);
  // A switch-off with a verify value that is not an enumerator is still sent.
  CHECK(rig.pwm.SetChannelOff(1, pca9685_test::NotAnEnumerator<Verify>(7)).ok());
  CHECK_EQ(rig.Bus().transfers, now + 1);
  CHECK_EQ(rig.pwm.GetChannel(16).error, Error::InvalidArgument);
}

TEST_CASE("SetPulseWidthUs: counts from the programmed prescaler (KB-CPP-128)") {
  Rig<MinimalPort> rig(pca9685::ServoPreset());
  REQUIRE(rig.Start());
  CHECK_EQ(rig.Chip().Reg(0xFE), 121);
  const auto r = rig.pwm.SetPulseWidthUs(0, 1500);
  CHECK(r.ok());
  CHECK_EQ(r.value, 1'498'160U);
  CHECK(IsWrite(rig.Bus().log.back(), {0x06, 0x00, 0x00, 0x33, 0x01})); // 307 counts
  CHECK_EQ(rig.pwm.SetPulseWidthUs(0, 0).value, 0U);
  CHECK(!rig.Chip().ChannelActive(0));
  const int now = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.SetPulseWidthUs(0, 20'000).error, Error::InvalidArgument); // > 4095 counts
  CHECK_EQ(rig.pwm.SetPulseWidthUs(0, 1).error, Error::InvalidArgument);      // < 1 count
  CHECK_EQ(rig.pwm.SetPulseWidthUs(16, 1000).error, Error::InvalidArgument);
  CHECK_EQ(rig.pwm.SetPulseWidthUs(0, 1000, pca9685_test::NotAnEnumerator<Verify>(3)).error,
           Error::InvalidArgument);
  CHECK_EQ(rig.Bus().transfers, now);
  CHECK(rig.pwm.SetPulseWidthUs(2, 2000, Verify::ReadBack).ok());
}

TEST_CASE("SetChannels: one transfer, so with OCH = 0 every channel changes at the STOP") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const std::array<Pwm, 3> values{pca9685::PwmFullOn(), pca9685::PwmOff(),
                                  pca9685::PwmForDuty(2048, 1024).value};
  const int before = rig.Bus().transfers;
  CHECK(rig.pwm.SetChannels(13, values).ok());
  CHECK_EQ(rig.Bus().transfers, before + 2); // the switch-off byte first, then one burst
  CHECK(IsWrite(rig.Bus().log[static_cast<std::size_t>(before)], {0x41, 0x10}));
  CHECK(IsWrite(rig.Bus().log.back(),
                {0x3A, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x04, 0x00, 0x0C}));
  CHECK(rig.Chip().ChannelActive(13) && !rig.Chip().ChannelActive(14) &&
        rig.Chip().ChannelActive(15));
  const std::array<Pwm, 16> all{};
  CHECK(rig.pwm.SetChannels(0, all).ok()); // all off: 16 single-byte switch-offs, no burst
  CHECK_EQ(rig.Bus().log.back().bytes.size(), 2U);
  const std::array<Pwm, 16> mixed{pca9685::PwmFullOn()};
  CHECK(rig.pwm.SetChannels(0, mixed).ok());
  CHECK_EQ(rig.Bus().log.back().bytes.size(), 65U);
  CHECK(rig.Chip().ChannelActive(0));
  CHECK(!rig.Chip().ChannelActive(13) && !rig.Chip().ChannelActive(15));
  const int now = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.SetChannels(14, values).error, Error::InvalidArgument);
  CHECK_EQ(rig.pwm.SetChannels(16, std::span<const Pwm>{values}.first(1)).error,
           Error::InvalidArgument);
  CHECK_EQ(rig.pwm.SetChannels(0, std::span<const Pwm>{}).error, Error::InvalidArgument);
  CHECK_EQ(rig.Bus().transfers, now);
}

TEST_CASE("SetChannels with an invalid value refuses the switch-ons, sends every switch-off") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.SetFullOn(0).ok());
  REQUIRE(rig.pwm.SetFullOn(2).ok());
  const std::array<Pwm, 3> values{pca9685::PwmOff(), pca9685::PwmFullOn(),
                                  Pwm{.on_count = 7, .off_count = 7, .full_off = false}};
  const int before = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.SetChannels(0, values).error, Error::InvalidArgument);
  CHECK_EQ(rig.Bus().transfers, before + 1); // channel 0 only
  CHECK(!rig.Chip().ChannelActive(0));
  CHECK(!rig.Chip().ChannelActive(1)); // refused
  CHECK(rig.Chip().ChannelActive(2));  // untouched
}

TEST_CASE("SetChannelsOff and SetAllOff (sections 7.3.1.1, 7.3.4)") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  for (uint8_t ch = 0; ch < 16; ++ch) {
    REQUIRE(rig.pwm.SetFullOn(ch).ok());
  }
  const int before = rig.Bus().transfers;
  CHECK(rig.pwm.SetChannelsOff(0x8001).ok());
  CHECK_EQ(rig.Bus().transfers, before + 2);
  CHECK(!rig.Chip().ChannelActive(0) && !rig.Chip().ChannelActive(15) &&
        rig.Chip().ChannelActive(1));
  CHECK(rig.pwm.SetChannelsOff(0).ok());
  CHECK_EQ(rig.Bus().transfers, before + 2);
  CHECK(rig.pwm.SetAllOff().ok());
  CHECK(IsWrite(rig.Bus().log.back(), {0xFD, 0x10})); // figure 24
  for (uint8_t ch = 0; ch < 16; ++ch) {
    CHECK(!rig.Chip().ChannelActive(ch));
    CHECK(rig.pwm.GetChannel(ch).value.full_off);
  }
}

TEST_CASE("Verify::ReadBack: MODE1/MODE2 and the channel; a kept-different write is a mismatch") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const int before = rig.Bus().transfers;
  CHECK(rig.pwm.SetDuty(2, 100, 0, Verify::ReadBack).ok());
  CHECK_EQ(rig.Bus().transfers, before + 3);
  CHECK(IsRead(rig.Bus().log[static_cast<std::size_t>(before) + 1U], 0x00, 3));
  CHECK(IsRead(rig.Bus().log.back(), 0x0E, 4));
  rig.Bus().corrupt_transfer = rig.Bus().transfers; // the next write is stored with a bit flipped
  CHECK_EQ(rig.pwm.SetDuty(2, 200, 0, Verify::ReadBack).error, Error::WriteMismatch);
  CHECK_EQ(rig.pwm.GetChannel(2).value.off_count, 0x1C8); // OFF_H bit 0 flipped: the chip's value
  CHECK(rig.pwm.SetChannelOff(2, Verify::ReadBack).ok());
  rig.Bus().corrupt_transfer = rig.Bus().transfers;
  rig.Bus().corrupt_mask = 0x10; // the full OFF bit lost
  CHECK_EQ(rig.pwm.SetChannelOff(2, Verify::ReadBack).error, Error::WriteMismatch);
}

TEST_CASE("ReadChannel and ReadChannels read the chip into the copy") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.Chip().SetChannel(7, 0x00, 0x01, 0x00, 0x02); // written by someone else
  const auto one = rig.pwm.ReadChannel(7);
  CHECK(one.ok() && one.value.on_count == 0x100 && one.value.off_count == 0x200);
  CHECK(rig.pwm.GetChannel(7).value == one.value);
  CHECK_EQ(rig.pwm.ReadChannel(16).error, Error::InvalidArgument);
  const auto all = rig.pwm.ReadChannels();
  CHECK(all.ok() && all.value[7] == one.value && all.value[0] == pca9685::PwmOff());
}

// ---- Frequency, sleep and restart ---------------------------------------------------------------

TEST_CASE("SetPwmFrequency: sleep, PRE_SCALE, read back, wake, restart (7.3.1.1, 7.3.5)") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.SetDuty(0, 2048).ok());
  const std::size_t first = rig.Bus().log.size();
  const uint64_t waited = rig.port.delayed_us;
  CHECK(rig.pwm.SetPwmFrequency(50).ok());
  const auto& log = rig.Bus().log;
  REQUIRE(log.size() == first + 10U);
  CHECK(IsRead(log[first], 0x00, 3));
  CHECK(IsWrite(log[first + 1], {0x00, 0x30}));
  CHECK(IsWrite(log[first + 2], {0xFE, 121}));
  CHECK(IsRead(log[first + 3], 0xFE, 1));
  CHECK(IsWrite(log[first + 4], {0x00, 0x20}));
  CHECK(IsRead(log[first + 5], 0x00, 3));
  CHECK(IsRead(log[first + 6], 0xFE, 1));
  CHECK(IsWrite(log[first + 7], {0x00, 0xA0})); // RESTART: the live channel runs again
  CHECK(IsRead(log[first + 8], 0x00, 3));       // and the wake check once more after it
  CHECK(IsRead(log[first + 9], 0xFE, 1));
  CHECK_EQ(rig.port.delayed_us - waited, 500U);
  CHECK_EQ(rig.Chip().early_restart_writes, 0);
  CHECK_EQ(rig.Chip().blocked_prescale_writes, 0);
  CHECK(rig.Chip().ChannelActive(0));
  CHECK_EQ(rig.Chip().Reg(0xFE), 121);
  CHECK_EQ(rig.pwm.GetPwmFrequencyHz().value, 50U);
  CHECK_EQ(rig.pwm.GetConfig().pwm_frequency_hz, 50U);
  const int now = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.SetPwmFrequency(2000).error, Error::InvalidArgument);
  CHECK_EQ(rig.pwm.SetPwmFrequency(0).error, Error::InvalidArgument);
  CHECK_EQ(rig.Bus().transfers, now);
  CHECK_EQ(rig.pwm.ReadPrescale().value, 121);
}

TEST_CASE("SetPwmFrequency while asleep: no wake") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.Sleep().ok());
  CHECK(rig.pwm.SetPwmFrequency(1000).ok());
  CHECK(rig.Chip().Asleep());
  CHECK_EQ(rig.Chip().Reg(0xFE), 5);
  CHECK(rig.pwm.MayBeAsleep());
}

TEST_CASE("Sleep(OutputsOff): every channel off, then asleep; Wake() leaves them off") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.SetFullOn(4).ok());
  CHECK(rig.pwm.Sleep().ok());
  CHECK(rig.Chip().Asleep());
  CHECK(!rig.Chip().Restart());
  CHECK(!rig.Chip().ChannelActive(4));
  CHECK(rig.pwm.MayBeAsleep());
  CHECK(rig.pwm.SetChannelOff(4).ok()); // the outputs are off: Ok
  CHECK_EQ(rig.pwm.SetFullOn(4).error, Error::Asleep);
  CHECK(!rig.Chip().ChannelActive(4));
  CHECK(rig.pwm.Wake().ok());
  CHECK(rig.Chip().ChannelActive(4)); // the switch-on written while asleep
  CHECK(!rig.pwm.MayBeAsleep());
  CHECK_EQ(rig.pwm.Sleep(pca9685_test::NotAnEnumerator<pca9685::SleepMode>(9)).error,
           Error::InvalidArgument);
}

TEST_CASE("Sleep(HoldOutputs) and Wake(): RESTART restarts the channels, 500 us after SLEEP = 0") {
  for (const bool runs : {true, false}) {
    for (const bool holds : {true, false}) {
      Rig<MinimalPort> rig;
      rig.Chip().readings.wake_runs_without_restart = runs;
      rig.Chip().readings.sleep_holds_outputs = holds;
      REQUIRE(rig.Start());
      REQUIRE(rig.pwm.SetDuty(6, 1000).ok());
      CHECK(rig.pwm.Sleep(pca9685::SleepMode::HoldOutputs).ok());
      CHECK(rig.Chip().Restart());
      CHECK(rig.Chip().ChannelActive(6) == holds);
      CHECK(rig.pwm.Wake().ok());
      CHECK(!rig.Chip().Restart());
      CHECK(rig.Chip().ChannelActive(6));
      CHECK_EQ(rig.Chip().early_restart_writes, 0);
    }
  }
}

TEST_CASE("A switch-off while asleep holding the outputs is sent and returns Asleep") {
  for (const bool holds : {true, false}) {
    Rig<FullPort> rig;
    rig.Chip().readings.sleep_holds_outputs = holds;
    REQUIRE(rig.Start());
    REQUIRE(rig.pwm.SetFullOn(1).ok());
    REQUIRE(rig.pwm.SetFullOn(2).ok());
    REQUIRE(rig.pwm.Sleep(pca9685::SleepMode::HoldOutputs).ok());
    CHECK_EQ(rig.pwm.SetChannelOff(1).error, Error::Asleep);
    CHECK(pca9685::IsOff(pca9685_test::Decode(pca9685_test::Stored(rig.Chip(), 1))));
    CHECK_EQ(rig.pwm.SetAllOff().error, Error::Asleep);
    CHECK_EQ(rig.pwm.SetChannelsOff(0x0004).error, Error::Asleep);
    // OE switches off at once, asleep or not.
    CHECK(rig.pwm.DisableOutputs().ok());
    CHECK(pca9685_test::PinOff(rig.Chip(), 1, rig.pwm.GetConfig()));
    CHECK(rig.pwm.Wake().ok());
    CHECK(!rig.Chip().ChannelActive(1) && !rig.Chip().ChannelActive(2));
    CHECK(rig.pwm.SetChannelOff(1).ok());
  }
}

// ---- Output enable
// --------------------------------------------------------------------------------

TEST_CASE("DisableOutputs and EnableOutputs drive OE; the outputs go to the off level") {
  for (const Config& config :
       {Config{}, pca9685::DirectLedPreset(),
        Config{.output_drive = pca9685::OutputDrive::TotemPole, .invert_outputs = true}}) {
    Rig<FullPort> rig(config);
    REQUIRE(rig.Start());
    REQUIRE(rig.pwm.SetFullOn(9).ok());
    CHECK(!pca9685_test::PinOff(rig.Chip(), 9, config));
    CHECK(rig.pwm.DisableOutputs().ok());
    for (uint32_t ch = 0; ch < 16; ++ch) {
      CHECK(pca9685_test::PinOff(rig.Chip(), ch, config));
    }
    CHECK(rig.pwm.EnableOutputs().ok());
    CHECK(!pca9685_test::PinOff(rig.Chip(), 9, config));
    rig.port.gpio_ok = false;
    CHECK_EQ(rig.pwm.DisableOutputs().error, Error::Bus);
  }
  Rig<MinimalPort> bare;
  CHECK_EQ(bare.pwm.DisableOutputs().error, Error::Unsupported);
  REQUIRE(bare.Start());
  CHECK_EQ(bare.pwm.EnableOutputs().error, Error::Unsupported);
}

// ---- Lifecycle and diagnostics ------------------------------------------------------------------

TEST_CASE("Deinitialize: OE high, every channel off, asleep, uninitialised") {
  Rig<FullPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.SetFullOn(3).ok());
  CHECK(rig.pwm.Deinitialize().ok());
  CHECK(!rig.pwm.IsInitialized());
  CHECK(rig.Chip().oe_high);
  CHECK(rig.Chip().Asleep());
  CHECK(!rig.Chip().ChannelActive(3));
  // A failed OE write still sends the switch-off; a failed switch-off means no sleep.
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.SetFullOn(3).ok());
  rig.port.gpio_ok = false;
  rig.Bus().fail_transfer = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.Deinitialize().error, Error::NoDevice);
  CHECK(!rig.pwm.IsInitialized());
  CHECK(!rig.Chip().Asleep()); // the switch-off failed: no sleep, which would hold the outputs on
  CHECK(rig.Chip().ChannelActive(3));
  rig.port.gpio_ok = true;
  CHECK(rig.pwm.Deinitialize().ok()); // the retry finishes the work
  CHECK(rig.Chip().Asleep());
  CHECK(!rig.Chip().ChannelActive(3));
}

TEST_CASE("Deinitialize: OE high before the bus; an address EnsureInitialized() refuses, no I/O") {
  {
    // OE needs no bus: driven high even when the port cannot bring the bus up.
    Rig<FullPort> rig;
    REQUIRE(rig.Start());
    REQUIRE(!rig.Chip().oe_high);
    rig.Bus().bus_up_ok = false;
    const int before = rig.Bus().transfers;
    CHECK_EQ(rig.pwm.Deinitialize().error, Error::Bus);
    CHECK(rig.Chip().oe_high);
    CHECK_EQ(rig.Bus().transfers, before);
  }
  for (const uint8_t address : {uint8_t{0x3F}, uint8_t{0x70}, uint8_t{0x80}}) {
    // 0x70 is LED All Call at power-up (7.1.2): never a switch-off and sleep of every chip.
    Rig<FullPort> rig(Config{}, address);
    rig.Chip().SetReg(0x00, 0x01); // a chip at power-up, awake: it answers All Call
    CHECK_EQ(rig.pwm.Deinitialize().error, Error::InvalidArgument);
    CHECK_EQ(rig.Bus().transfers, 0);
    CHECK_EQ(rig.port.bus_up_calls, 0);
    CHECK_EQ(rig.port.gpio_writes, 0);
    CHECK(!rig.Chip().Asleep());
  }
}

TEST_CASE("ApplyConfig: stored before the start; the whole start-up again once running") {
  Rig<MinimalPort> rig;
  CHECK(rig.pwm.ApplyConfig(pca9685::ServoPreset()).ok());
  CHECK_EQ(rig.Bus().transfers, 0);
  REQUIRE(rig.Start());
  CHECK_EQ(rig.Chip().Reg(0xFE), 121);
  REQUIRE(rig.pwm.SetFullOn(0).ok());
  CHECK(rig.pwm.ApplyConfig(pca9685::DirectLedPreset()).ok());
  CHECK(rig.pwm.IsInitialized());
  CHECK(!rig.Chip().ChannelActive(0));
  CHECK_EQ(rig.Chip().Reg(0x01), 0x12);
  CHECK_EQ(rig.pwm.ApplyConfig(Config{.pwm_frequency_hz = 0}).error, Error::InvalidArgument);
  CHECK_EQ(rig.pwm.GetConfig().pwm_frequency_hz, 1000U);
}

TEST_CASE("ReadRegister: one read, no check; the last MODE bytes without I/O (KB-CPP-129)") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  CHECK_EQ(rig.pwm.GetLastMode1(), 0x20);
  CHECK_EQ(rig.pwm.GetLastMode2(), 0x04);
  const int before = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.ReadRegister(0x01).value, 0x04);
  CHECK_EQ(rig.pwm.ReadRegister(0x45).value, 0x10);
  CHECK_EQ(rig.pwm.ReadRegister(0xFE).value, 0x1E);
  CHECK_EQ(rig.Bus().transfers, before + 3);
  CHECK_EQ(rig.pwm.ReadRegister(0x46).error, Error::InvalidArgument);
  CHECK_EQ(rig.pwm.ReadRegister(0xFA).error, Error::InvalidArgument);
  CHECK_EQ(rig.pwm.ReadRegister(0xFF).error, Error::InvalidArgument);
  CHECK_EQ(rig.Bus().transfers, before + 3);
  CHECK_EQ(rig.Chip().test_mode_writes, 0);
}

TEST_CASE("No transfer is ever empty and every burst fits (KB-CPP-130)") {
  Rig<FullPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.pwm.SetDuty(1, 10).ok());
  REQUIRE(rig.pwm.SetPwmFrequency(100).ok());
  REQUIRE(rig.pwm.Sleep().ok());
  REQUIRE(rig.pwm.Wake().ok());
  for (const auto& f : rig.Bus().log) {
    CHECK(f.write ? (f.bytes.size() >= 2U && f.bytes.size() <= 65U)
                  : (f.rx_len >= 1U && f.rx_len <= 64U));
    CHECK_EQ(f.addr, 0x40);
  }
}
