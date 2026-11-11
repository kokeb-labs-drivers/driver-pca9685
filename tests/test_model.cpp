/**
 * @file test_model.cpp
 * @brief The chip model against the data sheet, and its register map against the driver's
 * (handbook KB-TST-030).
 *
 * tests/mock_pca9685.hpp is written from the data sheet without the driver's headers, so a
 * constant the driver gets wrong is not copied into the model. The static_asserts make the two
 * maps agree, or the build fails; the test cases pin the model's own behaviour to the sections it
 * cites, so the driver tests run against the chip the data sheet describes.
 */
#include <array>
#include <cstdint>

#include "fixture.hpp"
#include "test_harness.hpp"

namespace {

namespace chip = pca9685_test::model_reg;
namespace reg = pca9685::reg;
using pca9685_test::Level;
using pca9685_test::Pca9685Model;

// --8<-- [start:cross-check]
// Register addresses (table 4).
static_assert(chip::kMode1 == reg::kMode1 && chip::kMode2 == reg::kMode2 &&
              chip::kSubAdr1 == reg::kSubAdr1 && chip::kAllCallAdr == reg::kAllCallAdr);
static_assert(chip::kLed0OnL == reg::kLed0OnL && chip::kLastLed == reg::kLed15OffH &&
              chip::kAllLedOnL == reg::kAllLedOnL && chip::kAllLedOffH == reg::kAllLedOffH &&
              chip::kPreScale == reg::kPreScale);
// MODE1 and MODE2 bits (tables 5, 6), the full bits (table 7).
static_assert(chip::kRestart == reg::kMode1Restart && chip::kExtClk == reg::kMode1ExtClk &&
              chip::kAi == reg::kMode1Ai && chip::kSleep == reg::kMode1Sleep &&
              chip::kSub1 == reg::kMode1Sub1 && chip::kAllCall == reg::kMode1AllCall);
static_assert(chip::kInvrt == reg::kMode2Invrt && chip::kOch == reg::kMode2Och &&
              chip::kOutDrv == reg::kMode2OutDrv && chip::kOutneMask == reg::Mode2Outne::kMask &&
              chip::kFull == reg::kLedFull);
// Reset values and timing (tables 5, 6, 8; table 5 note 2).
static_assert(chip::kMode1Reset == reg::kMode1Reset && chip::kMode2Reset == reg::kMode2Reset &&
              chip::kPreScaleReset == reg::kPreScaleReset);
static_assert(chip::kStartUs == pca9685::kOscillatorStartUs);
// The LED All Call address at power-up: ALLCALLADR E0h, 7-bit 0x70 (section 7.1.2).
static_assert((0xE0U >> 1U) == pca9685::kAllCallAddress);
// --8<-- [end:cross-check]

constexpr uint8_t kAddr = 0x40;

/// Write `bytes` (control register first) to the model.
template <std::size_t N>
int Send(Pca9685Model& m, const std::array<uint8_t, N>& bytes, uint8_t addr = kAddr) {
  return m.Write(addr, bytes.data(), bytes.size(), bytes.size());
}

uint8_t ReadOne(Pca9685Model& m, uint8_t r) {
  uint8_t v = 0;
  (void)m.Read(kAddr, r, &v, 1);
  return v;
}

} // namespace

TEST_CASE("Model: reset values (tables 5 to 10)") {
  Pca9685Model m;
  CHECK_EQ(ReadOne(m, chip::kMode1), 0x11);
  CHECK_EQ(ReadOne(m, chip::kMode2), 0x04);
  CHECK_EQ(ReadOne(m, 0x02), 0xE2);
  CHECK_EQ(ReadOne(m, 0x03), 0xE4);
  CHECK_EQ(ReadOne(m, 0x04), 0xE8);
  CHECK_EQ(ReadOne(m, 0x05), 0xE0);
  for (uint32_t ch = 0; ch < 16; ++ch) {
    const auto b = pca9685_test::Stored(m, ch);
    CHECK(b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0x10);
    CHECK(!m.ChannelActive(ch));
    CHECK(m.PinLevel(ch) == Level::Low); // POR: outputs low (section 1)
  }
  CHECK_EQ(ReadOne(m, chip::kPreScale), 0x1E);
  CHECK(m.Asleep());
}

TEST_CASE("Model: auto-increment only with AI; wraps 0x45 and 0xFE to MODE1 (table 4)") {
  Pca9685Model m;
  // AI clear: all four bytes land in LED0_ON_L.
  CHECK_EQ(Send(m, std::array<uint8_t, 5>{0x06, 0x11, 0x12, 0x13, 0x14}), 5);
  CHECK_EQ(m.Reg(0x06), 0x14);
  CHECK_EQ(m.Reg(0x07), 0x00);
  CHECK_EQ(m.Reg(0x09), 0x10);
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x31}), 2); // AI, SLEEP, ALLCALL
  std::array<uint8_t, 3> rx{};
  CHECK(m.Read(kAddr, 0x44, rx.data(), rx.size()));
  CHECK_EQ(rx[2], 0x31); // 0x44, 0x45, then MODE1
  CHECK(m.Read(kAddr, 0xFE, rx.data(), 2));
  CHECK_EQ(rx[1], 0x31); // PRE_SCALE, then MODE1
}

TEST_CASE("Model: reserved addresses are not acknowledged (table 4)") {
  Pca9685Model m;
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x46, 0x00}), 0);
  uint8_t v = 0;
  CHECK(!m.Read(kAddr, 0xF9, &v, 1));
  CHECK(m.Read(kAddr, 0xFA, &v, 1));
  CHECK_EQ(v, 0); // ALL_LED registers are write only
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0xFF, 0x00}), 2);
  CHECK_EQ(m.test_mode_writes, 1);
}

TEST_CASE("Model: PRE_SCALE takes a write only asleep, and at least 3 (7.3.5)") {
  Pca9685Model m;
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0xFE, 0x01}), 2);
  CHECK_EQ(m.Reg(0xFE), 3);
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x20}), 2); // wake
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0xFE, 0x79}), 2); // acknowledged, ignored
  CHECK_EQ(m.Reg(0xFE), 3);
  CHECK_EQ(m.blocked_prescale_writes, 1);
}

TEST_CASE("Model: EXTCLK only with SLEEP already set, and sticky until reset (table 5)") {
  Pca9685Model m;
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x20}), 2); // awake
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x60}), 2); // EXTCLK while awake: ignored
  CHECK_EQ(m.Reg(0) & 0x40, 0);
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x30}), 2); // sleep
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x70}), 2); // SLEEP and EXTCLK
  CHECK_EQ(m.Reg(0) & 0x40, 0x40);
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x20}), 2); // a 0 does not clear it
  CHECK_EQ(m.Reg(0) & 0x40, 0x40);
  m.PowerOnReset();
  CHECK_EQ(m.Reg(0) & 0x40, 0);
}

TEST_CASE("Model: RESTART sets on a sleep with a live channel and restarts after 500 us") {
  for (const bool runs : {true, false}) {
    Pca9685Model m;
    m.readings.wake_runs_without_restart = runs;
    CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x20}), 2);
    m.Elapse(500);
    m.SetChannel(0, 0, 0, 0x00, 0x08); // 50 %
    CHECK(m.ChannelActive(0));
    CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x30}), 2); // sleep
    CHECK(m.Restart());
    CHECK_EQ(ReadOne(m, 0) & 0x80, 0x80);
    CHECK(m.ChannelActive(0));                                // held (sleep_holds_outputs)
    CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x20}), 2); // wake
    CHECK(m.ChannelActive(0) == runs);
    CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0xA0}), 2); // RESTART too early
    CHECK_EQ(m.early_restart_writes, 1);
    CHECK(m.Restart());
    m.Elapse(500);
    CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0xA0}), 2);
    CHECK(!m.Restart());
    CHECK(m.ChannelActive(0));
  }
}

TEST_CASE("Model: asleep, an output holds its level or goes inactive (both readings)") {
  for (const bool holds : {true, false}) {
    Pca9685Model m;
    m.readings.sleep_holds_outputs = holds;
    CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x20}), 2);
    m.SetChannel(3, 0, 0x10, 0, 0); // full ON
    CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x30}), 2);
    CHECK(m.ChannelActive(3) == holds);
    // A channel write while asleep reaches the registers, not the output.
    CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x15, 0x10}), 2);
    CHECK(m.ChannelActive(3) == holds);
    CHECK(!m.Restart()); // the channel write cleared RESTART (7.3.1.1, other actions 3)
    CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x20}), 2);
    CHECK(!m.ChannelActive(3));
  }
}

TEST_CASE("Model: OCH = 0 applies a transfer at its STOP, also after a NACK (7.3.2)") {
  Pca9685Model m;
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x20}), 2);
  const std::array<uint8_t, 5> on{0x06, 0x00, 0x10, 0x00, 0x00};
  CHECK_EQ(m.Write(kAddr, on.data(), on.size(), 3), 3); // NACK after ON_H
  CHECK(m.Live(0)[1] == 0x10);                          // the bytes before the NACK went live
  CHECK(m.Live(0)[3] == 0x10);
  CHECK(!m.ChannelActive(0)); // full OFF still set: its precedence keeps the channel off
}

TEST_CASE("Model: OCH = 1 changes a channel on its fourth ACK, or per byte (both readings)") {
  for (const bool four : {true, false}) {
    Pca9685Model m;
    m.readings.ack_needs_all_four = four;
    CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x20}), 2);
    CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x01, 0x0C}), 2); // OCH = 1
    const std::array<uint8_t, 5> on{0x06, 0x00, 0x10, 0x00, 0x00};
    CHECK_EQ(m.Write(kAddr, on.data(), on.size(), 4), 4); // three data bytes, then NACK
    CHECK(m.Live(0)[1] == (four ? 0x00 : 0x10));
    CHECK_EQ(Send(m, on), 5);
    CHECK(m.ChannelActive(0));
  }
}

TEST_CASE("Model: ALL_LED loads every channel (7.3.4); full OFF wins over full ON (table 7)") {
  Pca9685Model m;
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x20}), 2);
  CHECK_EQ(Send(m, std::array<uint8_t, 5>{0xFA, 0x00, 0x10, 0x00, 0x00}), 5);
  for (uint32_t ch = 0; ch < 16; ++ch) {
    CHECK(m.ChannelActive(ch));
  }
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0xFD, 0x10}), 2);
  for (uint32_t ch = 0; ch < 16; ++ch) {
    CHECK(!m.ChannelActive(ch));
  }
}

TEST_CASE("Model: outputs through INVRT, OUTDRV and OE with OUTNE (tables 6, 11)") {
  Pca9685Model m;
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x20}), 2);
  m.SetChannel(0, 0, 0x10, 0, 0); // on
  CHECK(m.PinLevel(0) == Level::High);
  CHECK(m.PinLevel(1) == Level::Low);
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x01, 0x10}), 2); // INVRT, open drain
  CHECK(m.PinLevel(0) == Level::Low);
  CHECK(m.PinLevel(1) == Level::HiZ);
  m.oe_high = true;
  CHECK(m.PinLevel(0) == Level::Low);                       // OUTNE = 00
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x01, 0x15}), 2); // OUTNE = 01, totem pole
  CHECK(m.PinLevel(0) == Level::High);
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x01, 0x11}), 2); // OUTNE = 01, open drain
  CHECK(m.PinLevel(0) == Level::HiZ);
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x01, 0x16}), 2); // OUTNE = 10
  CHECK(m.PinLevel(0) == Level::HiZ);
}

TEST_CASE("Model: group addresses and the software reset (7.1.2, 7.1.3, 7.6)") {
  Pca9685Model m;
  CHECK(m.Answers(0x70));                                   // LED All Call at power-up
  CHECK(!m.Answers(0x71));                                  // subaddresses off at power-up
  CHECK_EQ(Send(m, std::array<uint8_t, 2>{0x00, 0x08}), 2); // SUB1 on, ALLCALL off
  CHECK(m.Answers(0x71));
  CHECK(!m.Answers(0x70));
  m.Elapse(1);
  const std::array<uint8_t, 2> wrong{0x07, 0x00};
  CHECK_EQ(m.Write(0x00, wrong.data(), 1, 1), 0); // not 06h: not acknowledged
  CHECK_EQ(m.resets, 0);
  const std::array<uint8_t, 1> swrst{0x06};
  CHECK_EQ(m.Write(0x00, swrst.data(), 1, 1), 1);
  CHECK_EQ(m.resets, 1);
  CHECK(m.Answers(0x70));
  CHECK_EQ(m.Reg(0), 0x11);
}
