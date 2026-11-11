/**
 * @file test_bus_faults.cpp
 * @brief Replies a stuck or floating line can produce are never data (handbook KB-CPP-116,
 * KB-CPP-117), and an absent chip is told from a bus error (KB-CPP-130).
 */
#include <array>
#include <cstdint>
#include <span>

#include "fixture.hpp"
#include "test_harness.hpp"

namespace {

using pca9685::Error;
using pca9685_test::FullPort;
using pca9685_test::MinimalPort;
using pca9685_test::ReadFault;
using pca9685_test::Rig;

} // namespace

TEST_CASE("Start-up refuses SDA stuck low or reads of all ones: the identity check fails") {
  for (const ReadFault fault : {ReadFault::StuckLow, ReadFault::Floating}) {
    Rig<MinimalPort> rig;
    rig.Bus().read_fault = fault;
    CHECK_EQ(rig.pwm.EnsureInitialized().error, Error::DeviceIdMismatch);
    CHECK(!rig.pwm.IsInitialized());
  }
  // The fault only from the identity reads on: MODE1/MODE2, PRE_SCALE, the channels.
  for (const int from : {6, 7, 8}) {
    for (const ReadFault fault : {ReadFault::StuckLow, ReadFault::Floating}) {
      Rig<MinimalPort> rig;
      rig.Bus().read_fault = fault;
      rig.Bus().read_fault_from = from;
      CHECK_EQ(rig.pwm.EnsureInitialized().error, Error::DeviceIdMismatch);
    }
  }
}

TEST_CASE("Start-up: a channel that does not read full OFF is another part") {
  // Both ALL_LED_OFF_H writes acknowledged but not kept: the identity check reads channel 0.
  Rig<MinimalPort> rig;
  rig.Chip().SetReg(0x00, 0x20);
  for (uint32_t ch = 0; ch < 16; ++ch) {
    rig.Chip().SetChannel(ch, 0, 0, 0x80, 0); // running: ON 0, OFF 128
  }
  rig.Bus().drop_transfer = 0;
  rig.Bus().corrupt_transfer = 2;
  rig.Bus().corrupt_mask = 0x10; // 0x10 stored as 0x00
  CHECK_EQ(rig.pwm.EnsureInitialized().error, Error::DeviceIdMismatch);
  CHECK(!rig.pwm.IsInitialized());
}

TEST_CASE("After the start: all zeros read as a reset, all ones as an invalid reply") {
  {
    Rig<MinimalPort> rig;
    REQUIRE(rig.Start());
    rig.Bus().read_fault = ReadFault::StuckLow;
    rig.Bus().read_fault_from = rig.Bus().transfers;
    CHECK_EQ(rig.pwm.CheckForReset().error, Error::DeviceReset); // AI reads 0
  }
  {
    Rig<MinimalPort> rig;
    REQUIRE(rig.Start());
    rig.Bus().read_fault = ReadFault::Floating;
    rig.Bus().read_fault_from = rig.Bus().transfers;
    CHECK_EQ(rig.pwm.CheckForReset().error, Error::InvalidReply); // reserved MODE2 bits
    CHECK(rig.pwm.IsInitialized());
    CHECK_EQ(rig.pwm.GetLastMode2(), 0xFF);
  }
}

TEST_CASE("A channel read of all ones is InvalidReply; all zeros is a value that is off") {
  {
    Rig<MinimalPort> rig;
    REQUIRE(rig.Start());
    rig.Bus().read_fault = ReadFault::Floating;
    rig.Bus().read_fault_from = rig.Bus().transfers + 1; // after the MODE1/MODE2 read
    const auto r = rig.pwm.ReadChannel(0);
    CHECK_EQ(r.error, Error::InvalidReply); // reserved bits set
    CHECK(r.value == pca9685::Pwm{});
    rig.Bus().read_fault_from = rig.Bus().transfers + 1;
    CHECK_EQ(rig.pwm.ReadChannels().error, Error::InvalidReply);
    rig.Bus().read_fault_from = rig.Bus().transfers + 2; // the channel read of a ReadBack
    CHECK_EQ(rig.pwm.SetDuty(0, 10, 0, pca9685::Verify::ReadBack).error, Error::InvalidReply);
  }
  {
    // Equal counts with no full bit: never asserted (figure 9, example 4). A switch-on NACKed
    // part-way can leave that on the chip, so it is a value, and one such channel does not fail a
    // read of all 16. All zeros too, once the MODE1/MODE2/SUBADR1 read agrees.
    Rig<MinimalPort> rig;
    REQUIRE(rig.Start());
    rig.Chip().SetChannel(0, 0, 0, 0, 0);
    const auto r = rig.pwm.ReadChannel(0);
    CHECK(r.ok() && !r.value.full_off && (r.value.on_count == r.value.off_count));
    rig.Chip().SetChannel(5, 0x20, 0, 0x20, 0);
    const auto all = rig.pwm.ReadChannels();
    CHECK(all.ok() && (all.value[5].on_count == 0x20) && all.value[1].full_off);
    CHECK(!rig.Chip().ChannelActive(5));
  }
}

TEST_CASE("SDA stuck low from the channel read on: a failed read, the channel still uncertain") {
  // KB-CPP-116: all zeros is also a legal value, so a second frame decides. Stuck low, the
  // MODE1/MODE2/SUBADR1 read gives zeros as well (AI clear, SUBADR1 not the sentinel).
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.Bus().nack_transfer = rig.Bus().transfers; // a switch-on NACKed: channel 2 uncertain
  rig.Bus().nack_after = 2;
  CHECK_EQ(rig.pwm.SetFullOn(2).error, Error::Bus);
  REQUIRE(rig.pwm.GetUncertainChannels() != 0U);
  const uint16_t uncertain = rig.pwm.GetUncertainChannels();
  rig.Bus().nack_transfer = -1;
  rig.Bus().read_fault = ReadFault::StuckLow;
  rig.Bus().read_fault_from = rig.Bus().transfers + 1; // after the MODE1/MODE2/SUBADR1 read
  CHECK_EQ(rig.pwm.ReadChannel(2).error, Error::InvalidReply);
  CHECK_EQ(rig.pwm.GetUncertainChannels(), uncertain);
  rig.Bus().read_fault_from = rig.Bus().transfers + 1;
  CHECK_EQ(rig.pwm.ReadChannels().error, Error::InvalidReply);
  CHECK_EQ(rig.pwm.GetUncertainChannels(), uncertain);
  CHECK(rig.pwm.IsInitialized()); // one frame never decides a reset (rule 12d)
  CHECK_EQ(rig.pwm.GetChannel(2).error, Error::StateUnknown);
  // A real all-zero channel whose confirming read fails: the read's error, still in doubt.
  rig.Bus().read_fault = ReadFault::None;
  rig.Chip().SetChannel(2, 0, 0, 0, 0);
  rig.Bus().fail_transfer = rig.Bus().transfers + 2; // after the reset check and the channel read
  CHECK_EQ(rig.pwm.ReadChannel(2).error, Error::Bus);
  CHECK_EQ(rig.pwm.GetUncertainChannels(), uncertain);
  rig.Bus().fail_transfer = -1;
  CHECK(rig.pwm.ReadChannel(2).ok()); // confirmed: a value, known again
  CHECK_EQ(rig.pwm.GetChannel(2).error, Error::Ok);
}

TEST_CASE("Nothing at the address: NoDevice with GetLastFailure(), Bus without") {
  Rig<FullPort> full(pca9685::Config{}, 0x41);
  CHECK_EQ(full.pwm.EnsureInitialized().error, Error::NoDevice);
  Rig<MinimalPort> bare(pca9685::Config{}, 0x41);
  CHECK_EQ(bare.pwm.EnsureInitialized().error, Error::Bus);
}

TEST_CASE("A data NACK is Bus even with GetLastFailure()") {
  Rig<FullPort> rig;
  REQUIRE(rig.Start());
  rig.Bus().nack_transfer = rig.Bus().transfers;
  rig.Bus().nack_after = 1;
  CHECK_EQ(rig.pwm.SetFullOn(0).error, Error::Bus);
  CHECK(rig.port.error_lines > 0);
}

TEST_CASE("Every failure on the bus propagates from every call") {
  Rig<FullPort> rig;
  REQUIRE(rig.Start());
  rig.Bus().fail_from = rig.Bus().transfers;
  CHECK_EQ(rig.pwm.CheckForReset().error, Error::NoDevice);
  CHECK_EQ(rig.pwm.ReadChannel(0).error, Error::NoDevice);
  CHECK_EQ(rig.pwm.ReadChannels().error, Error::NoDevice);
  CHECK_EQ(rig.pwm.ReadPrescale().error, Error::NoDevice);
  CHECK_EQ(rig.pwm.ReadRegister(0).error, Error::NoDevice);
  CHECK_EQ(rig.pwm.SetPwmFrequency(100).error, Error::NoDevice);
  CHECK_EQ(rig.pwm.Sleep(pca9685::SleepMode::HoldOutputs).error, Error::NoDevice);
  CHECK_EQ(rig.pwm.Wake().error, Error::NoDevice);
  CHECK_EQ(rig.pwm.SetDuty(0, 10).error, Error::NoDevice);
  CHECK_EQ(rig.pwm.SetPulseWidthUs(0, 1000).error, Error::NoDevice);
  CHECK_EQ(rig.pwm.Deinitialize().error, Error::NoDevice);
  CHECK(!rig.pwm.IsInitialized());
}

TEST_CASE("A ReadPrescale whose PRE_SCALE read fails keeps the old value") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.Bus().fail_transfer = rig.Bus().transfers + 1;
  CHECK_EQ(rig.pwm.ReadPrescale().error, Error::Bus);
  CHECK_EQ(rig.pwm.GetPrescale().value, 0x1E);
}

TEST_CASE("The register helpers refuse an empty or oversize burst before any I/O (KB-CPP-130)") {
  MinimalPort port;
  pca9685::detail::RegisterBus<MinimalPort> bus(port, pca9685::kI2cAddress);
  std::array<uint8_t, 65> big{};
  CHECK_EQ(bus.Read(0x00, std::span<uint8_t>{}), Error::InvalidArgument);
  CHECK_EQ(bus.Read(0x00, std::span<uint8_t>{big}), Error::InvalidArgument);
  CHECK_EQ(bus.Write(0x00, std::span<const uint8_t>{}), Error::InvalidArgument);
  CHECK_EQ(bus.Write(0x00, std::span<const uint8_t>{big}), Error::InvalidArgument);
  CHECK_EQ(port.state.transfers, 0);
}
