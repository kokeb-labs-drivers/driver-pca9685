/**
 * @file test_errors.cpp
 * @brief The error set, the result type, the configuration checks and the arithmetic: every
 * chip-specific error is returned by some call (AGENTS.md, "Error").
 */
#include <cstdint>
#include <cstring>

#include "fixture.hpp"
#include "test_harness.hpp"

namespace {

using pca9685::Config;
using pca9685::Error;
using pca9685_test::MinimalPort;
using pca9685_test::Rig;

/// `v` as the compiler sees a run-time value, so a constexpr function runs instead of folding.
template <typename T>
T Runtime(T v) {
  volatile T copy = v;
  return copy;
}

bool Named(Error e, const char* name) {
  return std::strcmp(pca9685::ToString(e), name) == 0;
}

} // namespace

TEST_CASE("ToString names every error, and Unknown for a value that is not one") {
  CHECK(Named(Error::Ok, "Ok"));
  CHECK(Named(Error::NotInitialized, "NotInitialized"));
  CHECK(Named(Error::InvalidArgument, "InvalidArgument"));
  CHECK(Named(Error::Bus, "Bus"));
  CHECK(Named(Error::Timeout, "Timeout"));
  CHECK(Named(Error::DeviceIdMismatch, "DeviceIdMismatch"));
  CHECK(Named(Error::Crc, "Crc"));
  CHECK(Named(Error::Busy, "Busy"));
  CHECK(Named(Error::DeviceFault, "DeviceFault"));
  CHECK(Named(Error::Unsupported, "Unsupported"));
  CHECK(Named(Error::WriteMismatch, "WriteMismatch"));
  CHECK(Named(Error::NoDevice, "NoDevice"));
  CHECK(Named(Error::DeviceReset, "DeviceReset"));
  CHECK(Named(Error::InvalidReply, "InvalidReply"));
  CHECK(Named(Error::Asleep, "Asleep"));
  CHECK(Named(Error::StateUnknown, "StateUnknown"));
  CHECK(Named(Error::ExternalClockLatched, "ExternalClockLatched"));
  CHECK(Named(pca9685_test::NotAnEnumerator<Error>(99), "Unknown"));
}

TEST_CASE("The base set keeps its values; chip errors start at 16") {
  static_assert(static_cast<int>(Error::Unsupported) == 9);
  static_assert(static_cast<int>(Error::WriteMismatch) == 16);
  static_assert(static_cast<int>(Error::ExternalClockLatched) == 22);
  CHECK(true);
}

TEST_CASE("Result: Success, Failure, the bool conversion") {
  const auto ok = pca9685::Result<int>::Success(7);
  const auto bad = pca9685::Result<int>::Failure(Error::Bus);
  CHECK(ok.ok() && static_cast<bool>(ok) && ok.value == 7);
  CHECK(!bad.ok() && !static_cast<bool>(bad) && bad.value == 0);
  CHECK(pca9685::Result<void>::Success().ok());
  CHECK(!pca9685::Result<void>::Failure(Error::Bus));
}

TEST_CASE("Validate: enumerations, the oscillator and the prescaler range (7.3.5)") {
  CHECK_EQ(pca9685::Validate(Config{}), Error::Ok);
  CHECK_EQ(pca9685::Validate(
               Config{.clock = Runtime(pca9685_test::NotAnEnumerator<pca9685::ClockSource>(2))}),
           Error::InvalidArgument);
  CHECK_EQ(pca9685::Validate(
               Config{.output_drive = pca9685_test::NotAnEnumerator<pca9685::OutputDrive>(5)}),
           Error::InvalidArgument);
  CHECK_EQ(pca9685::Validate(Config{.oscillator_hz = 51'000'000}), Error::InvalidArgument);
  CHECK_EQ(pca9685::Validate(Config{.oscillator_hz = 999'999}), Error::InvalidArgument);
  CHECK_EQ(pca9685::Validate(Config{.pwm_frequency_hz = 24}), Error::Ok);
  CHECK_EQ(pca9685::Validate(Config{.pwm_frequency_hz = 1526}), Error::Ok);
  // Equation 1 rounds: up to 1743 Hz still gives PRE_SCALE 3, which programs 1526 Hz.
  CHECK_EQ(pca9685::Validate(Config{.pwm_frequency_hz = 1743}), Error::Ok);
  CHECK_EQ(pca9685::Validate(Config{.pwm_frequency_hz = 1744}), Error::InvalidArgument);
  CHECK_EQ(pca9685::Validate(Config{.pwm_frequency_hz = 23}), Error::InvalidArgument);
  CHECK_EQ(pca9685::Validate(Config{.pwm_frequency_hz = 20'000}), Error::InvalidArgument);
  // EXTCLK at 50 MHz reaches 3 kHz.
  CHECK_EQ(pca9685::Validate(Config{.pwm_frequency_hz = 3000, .oscillator_hz = 50'000'000}),
           Error::Ok);
}

TEST_CASE("Arithmetic: prescaler, frequency, pulse counts and widths") {
  CHECK_EQ(pca9685::PrescaleFor(200, 25'000'000).value, 30);
  CHECK_EQ(pca9685::FrequencyFor(121, 25'000'000), 50U);
  CHECK_EQ(pca9685::CountsForPulseUs(Runtime(0U), 121, 25'000'000).value, 0);
  CHECK_EQ(pca9685::CountsForPulseUs(1000, 121, 25'000'000).value, 205);
  CHECK_EQ(pca9685::PulseNsForCounts(205, 121, 25'000'000), 1'000'400U);
  CHECK_EQ(pca9685::PulseNsForCounts(1, 0, 0), 1'000'000'000U); // no division by zero
  CHECK(pca9685::PwmForDuty(1, 4095).value.off_count == 0);
}

TEST_CASE("Every chip-specific error is returned by some call") {
  { // WriteMismatch, InvalidReply, NoDevice, DeviceReset, Asleep, StateUnknown,
    // ExternalClockLatched: each also has its own test; here one short path each.
    Rig<MinimalPort> rig;
    REQUIRE(rig.Start());
    rig.Bus().corrupt_transfer = rig.Bus().transfers;
    CHECK_EQ(rig.pwm.SetFullOn(0, pca9685::Verify::ReadBack).error, Error::WriteMismatch);
    REQUIRE(rig.pwm.Sleep().ok());
    CHECK_EQ(rig.pwm.SetFullOn(0).error, Error::Asleep);
    rig.Chip().PowerOnReset();
    CHECK_EQ(rig.pwm.CheckForReset().error, Error::DeviceReset);
  }
  {
    Rig<pca9685_test::FullPort> rig(Config{}, 0x45);
    CHECK_EQ(rig.pwm.EnsureInitialized().error, Error::NoDevice);
  }
  {
    Rig<MinimalPort> rig;
    REQUIRE(rig.Start());
    rig.Bus().read_fault = pca9685_test::ReadFault::Floating;
    rig.Bus().read_fault_from = rig.Bus().transfers;
    CHECK_EQ(rig.pwm.CheckForReset().error, Error::InvalidReply);
  }
  {
    Rig<MinimalPort> rig;
    REQUIRE(rig.Start());
    rig.Bus().lost_ack_transfer = rig.Bus().transfers;
    REQUIRE(!rig.pwm.SetDuty(0, 100).ok());
    CHECK_EQ(rig.pwm.GetChannel(0).error, Error::StateUnknown);
  }
  {
    Rig<MinimalPort> rig;
    rig.Chip().SetReg(0x00, 0x50);
    CHECK_EQ(rig.pwm.EnsureInitialized().error, Error::ExternalClockLatched);
  }
}

TEST_CASE("CheckedConfig carries a configuration proven at build time") {
  constexpr pca9685::CheckedConfig kBoard{pca9685::ServoPreset()};
  static_assert(kBoard.Get().pwm_frequency_hz == 50U);
  CHECK_EQ(kBoard.Get().pwm_frequency_hz, 50U);
}
