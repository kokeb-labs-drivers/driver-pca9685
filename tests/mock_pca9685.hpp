/**
 * @file mock_pca9685.hpp
 * @brief Register model of the PCA9685 for the host tests, written from the data sheet alone, and
 * the mock I2C ports over it (mock_pca9685_bus.hpp).
 *
 * The model keeps its own register map with the data sheet's references; it does not include the
 * driver's register header, and test_model.cpp cross-checks the two maps with static_assert. It
 * changes state on its own, as the data sheet says the chip does:
 *
 * - a power-on reset or a software reset (general call 06h) at any moment loads every reset value
 *   (sections 7.5, 7.6): MODE1 = 0x11, asleep, every channel full OFF;
 * - the control register auto-increments only with MODE1.AI, wrapping 0x45 -> 0x00 and
 *   0xFE -> 0x00; reserved addresses are not acknowledged (table 4);
 * - PRE_SCALE ignores a write while SLEEP is 0, and holds at least 3 (table 4 note 1, 7.3.5);
 * - EXTCLK is set only by a write with SLEEP already set, and is sticky (table 5);
 * - RESTART sets when the chip sleeps with a live channel, a write of 1 restarts the channels
 *   only 500 us after SLEEP was cleared, and a channel write clears it (section 7.3.1.1);
 * - written channel bytes reach the outputs at the STOP (OCH = 0) or on the ACK of the fourth
 *   byte of a channel (OCH = 1); the bytes before a NACK are stored (section 7.3.2);
 * - the outputs: full OFF over full ON, INVRT, OUTDRV, and OE with OUTNE (tables 6, 7, 11);
 * - the group addresses: LED All Call answers at power-up, the subaddresses when enabled.
 *
 * Where the data sheet leaves a question open, `Readings` offers both answers, and the safety
 * tests run under each:
 *
 * - `sleep_holds_outputs`: while asleep an output keeps the level it had (true) or goes inactive;
 * - `ack_needs_all_four`: with OCH = 1 a channel changes only once all four of its registers were
 *   written (true), or on every byte;
 * - `wake_runs_without_restart`: after a wake whose RESTART was not written the channels run
 *   (true), or stay stopped until a RESTART write or a channel write.
 *
 * A clock advances with every transfer (`kFrameUs`) and every `Elapse()`.
 */
#ifndef PCA9685_MOCK_PCA9685_HPP
#define PCA9685_MOCK_PCA9685_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace pca9685_test {

/// The model's own register map (data sheet table 4 to table 10).
namespace model_reg {
inline constexpr uint8_t kMode1 = 0x00;         ///< MODE1, table 5.
inline constexpr uint8_t kMode2 = 0x01;         ///< MODE2, table 6.
inline constexpr uint8_t kSubAdr1 = 0x02;       ///< SUBADR1, table 9.
inline constexpr uint8_t kAllCallAdr = 0x05;    ///< ALLCALLADR, table 10.
inline constexpr uint8_t kLed0OnL = 0x06;       ///< LED0_ON_L, table 7.
inline constexpr uint8_t kLastLed = 0x45;       ///< LED15_OFF_H.
inline constexpr uint8_t kAllLedOnL = 0xFA;     ///< ALL_LED_ON_L, table 8.
inline constexpr uint8_t kAllLedOffH = 0xFD;    ///< ALL_LED_OFF_H.
inline constexpr uint8_t kPreScale = 0xFE;      ///< PRE_SCALE, table 8.
inline constexpr uint8_t kTestMode = 0xFF;      ///< TestMode, reserved.
inline constexpr uint8_t kRestart = 0x80;       ///< MODE1 bit 7.
inline constexpr uint8_t kExtClk = 0x40;        ///< MODE1 bit 6.
inline constexpr uint8_t kAi = 0x20;            ///< MODE1 bit 5.
inline constexpr uint8_t kSleep = 0x10;         ///< MODE1 bit 4.
inline constexpr uint8_t kSub1 = 0x08;          ///< MODE1 bit 3.
inline constexpr uint8_t kAllCall = 0x01;       ///< MODE1 bit 0.
inline constexpr uint8_t kInvrt = 0x10;         ///< MODE2 bit 4.
inline constexpr uint8_t kOch = 0x08;           ///< MODE2 bit 3.
inline constexpr uint8_t kOutDrv = 0x04;        ///< MODE2 bit 2.
inline constexpr uint8_t kOutneMask = 0x03;     ///< MODE2 bits 1..0.
inline constexpr uint8_t kFull = 0x10;          ///< LEDn_ON_H / LEDn_OFF_H bit 4.
inline constexpr uint8_t kMode1Reset = 0x11;    ///< Table 5 defaults.
inline constexpr uint8_t kMode2Reset = 0x04;    ///< Table 6 defaults.
inline constexpr uint8_t kPreScaleReset = 0x1E; ///< Table 8 default.
inline constexpr uint8_t kGeneralCall = 0x00;   ///< Section 7.6: SWRST address.
inline constexpr uint8_t kSwrstByte = 0x06;     ///< Section 7.6: SWRST data byte 1.
inline constexpr uint32_t kStartUs = 500U;      ///< Table 5 note 2.
} // namespace model_reg

/// The two readings of each question the data sheet leaves open.
struct Readings {
  bool sleep_holds_outputs = true;       ///< Asleep, an output keeps its level.
  bool ack_needs_all_four = true;        ///< OCH = 1: a channel changes after its fourth byte.
  bool wake_runs_without_restart = true; ///< Woken without RESTART, the channels run.
};

/// The level of an output pin.
enum class Level : uint8_t { Low, High, HiZ };

class Pca9685Model {
public:
  static constexpr uint32_t kFrameUs = 10U; ///< Time one transfer takes.

  Readings readings{};
  uint8_t own_address = 0x40; ///< A5..A0 strapping.
  bool oe_high = false;       ///< The OE pin: high disables the outputs.

  // Counters the tests read.
  int resets = 0;                  ///< Power-on and software resets.
  int blocked_prescale_writes = 0; ///< PRE_SCALE writes ignored while awake.
  int early_restart_writes = 0;    ///< RESTART written within 500 us of the wake.
  int test_mode_writes = 0;        ///< Writes to the reserved TestMode register.
  int startup_pwm_accesses = 0;    ///< Channel register access within 500 us of the wake.
  int mode1_writes = 0;            ///< Writes to MODE1.

  Pca9685Model() noexcept {
    loadResetValues();
  }

  /// Power-on reset (section 7.5) or software reset (section 7.6): every reset value.
  void PowerOnReset() noexcept {
    loadResetValues();
    ++resets;
  }

  void Elapse(uint32_t us) noexcept {
    now_us_ += us;
  }
  [[nodiscard]] uint64_t Now() const noexcept {
    return now_us_;
  }

  /// Whether the chip acknowledges `addr`: its own address, or an enabled group address.
  [[nodiscard]] bool Answers(uint8_t addr) const noexcept {
    const uint8_t m1 = regs_[model_reg::kMode1];
    if (addr == own_address) {
      return true;
    }
    if (((m1 & model_reg::kAllCall) != 0U) && (addr == (regs_[model_reg::kAllCallAdr] >> 1U))) {
      return true;
    }
    for (uint32_t i = 0U; i < 3U; ++i) {
      const auto bit = static_cast<uint8_t>(model_reg::kSub1 >> i);
      if (((m1 & bit) != 0U) && (addr == (regs_[model_reg::kSubAdr1 + i] >> 1U))) {
        return true;
      }
    }
    return false;
  }

  /// A write transaction: START, `addr` + W, the payload bytes the master sends (at most `limit`
  /// of them, then the master gives up), STOP. Returns how many payload bytes were acknowledged
  /// before the first NACK; -1 for an address NACK. The bytes before a NACK are stored, and the
  /// STOP applies them (OCH = 0).
  int Write(uint8_t addr, const uint8_t* data, std::size_t len, std::size_t limit) noexcept {
    now_us_ += kFrameUs;
    if (addr == model_reg::kGeneralCall) {
      return generalCall(data, len, limit);
    }
    if (!Answers(addr)) {
      return -1;
    }
    const std::size_t sent = (len < limit) ? len : limit;
    int acked = 0;
    pwm_written_ = false;
    for (std::size_t i = 0U; i < sent; ++i) {
      if (i == 0U) {
        if (reserved(data[0])) {
          break; // a reserved address is not acknowledged
        }
        pointer_ = data[0];
      } else {
        writeRegister(pointer_, data[i]);
        advance();
      }
      ++acked;
    }
    stop();
    return acked;
  }

  /// A combined transaction: the control register, repeated START, `n` bytes read. False for an
  /// address NACK or a reserved register.
  bool Read(uint8_t addr, uint8_t reg, uint8_t* rx, std::size_t n) noexcept {
    now_us_ += kFrameUs;
    if (!Answers(addr) || reserved(reg)) {
      return false;
    }
    pointer_ = reg;
    for (std::size_t i = 0U; i < n; ++i) {
      rx[i] = readRegister(pointer_);
      advance();
    }
    return true;
  }

  // ---- What the tests inspect -----------------------------------------------------------------

  [[nodiscard]] uint8_t Reg(uint8_t r) const noexcept {
    return regs_[r];
  }
  /// Poke a register directly (another master, or a test set-up); no side effects.
  void SetReg(uint8_t r, uint8_t v) noexcept {
    regs_[r] = v;
  }
  [[nodiscard]] bool Asleep() const noexcept {
    return (regs_[model_reg::kMode1] & model_reg::kSleep) != 0U;
  }
  [[nodiscard]] bool Restart() const noexcept {
    return restart_;
  }
  [[nodiscard]] bool Stopped() const noexcept {
    return stopped_;
  }

  /// Whether channel `ch`'s output logic is asserted, before INVRT and OE.
  [[nodiscard]] bool ChannelActive(uint32_t ch) const noexcept {
    if (Asleep()) {
      return held_[ch & 0x0FU];
    }
    if (stopped_) {
      return false;
    }
    return liveActive(ch);
  }

  /// The level of output LEDn.
  [[nodiscard]] Level PinLevel(uint32_t ch) const noexcept {
    const uint8_t m2 = regs_[model_reg::kMode2];
    const bool totem = (m2 & model_reg::kOutDrv) != 0U;
    if (oe_high) {
      const uint32_t outne = m2 & model_reg::kOutneMask;
      if (outne == 0U) {
        return Level::Low;
      }
      return ((outne == 1U) && totem) ? Level::High : Level::HiZ;
    }
    const bool high = ChannelActive(ch) != ((m2 & model_reg::kInvrt) != 0U);
    if (!high) {
      return Level::Low;
    }
    return totem ? Level::High : Level::HiZ;
  }

  /// The four live (effective) bytes of channel `ch`.
  [[nodiscard]] std::array<uint8_t, 4> Live(uint32_t ch) const noexcept {
    return live_[ch & 0x0FU];
  }

  /// Load channel `ch` with a value and make it live at once (a test set-up: a running chip).
  void SetChannel(uint32_t ch, uint8_t on_l, uint8_t on_h, uint8_t off_l, uint8_t off_h) noexcept {
    const uint32_t b = base(ch);
    regs_[b] = on_l;
    regs_[b + 1U] = on_h & 0x1FU;
    regs_[b + 2U] = off_l;
    regs_[b + 3U] = off_h & 0x1FU;
    syncAllLive();
  }

private:
  void loadResetValues() noexcept {
    regs_.fill(0U);
    regs_[model_reg::kMode1] = model_reg::kMode1Reset;
    regs_[model_reg::kMode2] = model_reg::kMode2Reset;
    regs_[0x02] = 0xE2U;
    regs_[0x03] = 0xE4U;
    regs_[0x04] = 0xE8U;
    regs_[model_reg::kAllCallAdr] = 0xE0U;
    for (uint32_t ch = 0U; ch < 16U; ++ch) {
      regs_[base(ch) + 3U] = model_reg::kFull;
    }
    regs_[model_reg::kPreScale] = model_reg::kPreScaleReset;
    syncAllLive();
    held_.fill(false);
    pending_.fill(0U);
    restart_ = false;
    stopped_ = false;
    pointer_ = 0U;
  }

  static constexpr uint32_t base(uint32_t ch) noexcept {
    return model_reg::kLed0OnL + (4U * (ch & 0x0FU));
  }

  static constexpr bool reserved(uint8_t r) noexcept {
    return (r > model_reg::kLastLed) && (r < model_reg::kAllLedOnL);
  }

  [[nodiscard]] bool liveActive(uint32_t ch) const noexcept {
    const auto& b = live_[ch & 0x0FU];
    if ((b[3] & model_reg::kFull) != 0U) {
      return false; // full OFF takes precedence (table 7)
    }
    if ((b[1] & model_reg::kFull) != 0U) {
      return true;
    }
    const uint32_t on = (static_cast<uint32_t>(b[1] & 0x0FU) << 8U) | b[0];
    const uint32_t off = (static_cast<uint32_t>(b[3] & 0x0FU) << 8U) | b[2];
    return on != off; // equal counts: never asserted (figure 9, example 4)
  }

  void syncAllLive() noexcept {
    for (uint32_t ch = 0U; ch < 16U; ++ch) {
      for (uint32_t i = 0U; i < 4U; ++i) {
        live_[ch][i] = regs_[base(ch) + i];
      }
    }
  }

  void advance() noexcept {
    if ((regs_[model_reg::kMode1] & model_reg::kAi) == 0U) {
      return; // AI clear: every byte goes to the same register
    }
    if ((pointer_ == model_reg::kLastLed) || (pointer_ == model_reg::kPreScale) ||
        (pointer_ == model_reg::kTestMode)) {
      pointer_ = 0U;
    } else {
      ++pointer_;
    }
  }

  [[nodiscard]] uint8_t readRegister(uint8_t r) noexcept {
    if (isPwm(r)) {
      noteStartupAccess();
    }
    if ((r >= model_reg::kAllLedOnL) && (r <= model_reg::kAllLedOffH)) {
      return 0U; // write only (table 8)
    }
    if (r == model_reg::kMode1) {
      return static_cast<uint8_t>((regs_[r] & 0x7FU) | (restart_ ? model_reg::kRestart : 0U));
    }
    return regs_[r];
  }

  static constexpr bool isPwm(uint8_t r) noexcept {
    return ((r >= model_reg::kLed0OnL) && (r <= model_reg::kLastLed)) ||
           ((r >= model_reg::kAllLedOnL) && (r <= model_reg::kAllLedOffH));
  }

  void noteStartupAccess() noexcept {
    const bool internal = (regs_[model_reg::kMode1] & model_reg::kExtClk) == 0U;
    if (!Asleep() && internal && ((now_us_ - wake_us_) < model_reg::kStartUs)) {
      ++startup_pwm_accesses;
    }
  }

  void writeRegister(uint8_t r, uint8_t v) noexcept {
    if (r == model_reg::kMode1) {
      writeMode1(v);
    } else if (r == model_reg::kMode2) {
      regs_[r] = v & 0x1FU; // bits 7..5 read only
    } else if (r <= model_reg::kAllCallAdr) {
      regs_[r] = v & 0xFEU; // bit 0 read only (tables 9, 10)
    } else if (r <= model_reg::kLastLed) {
      writeChannelByte((r - model_reg::kLed0OnL) / 4U, (r - model_reg::kLed0OnL) % 4U, v);
    } else if (r <= model_reg::kAllLedOffH) {
      for (uint32_t ch = 0U; ch < 16U; ++ch) {
        writeChannelByte(ch, r - model_reg::kAllLedOnL, v);
      }
    } else if (r == model_reg::kPreScale) {
      if (!Asleep()) {
        ++blocked_prescale_writes; // blocked while SLEEP is 0 (table 4 note 1)
      } else {
        regs_[r] = (v < 3U) ? uint8_t{3U} : v; // the hardware forces at least 3 (7.3.5)
      }
    } else {
      ++test_mode_writes;
    }
  }

  void writeChannelByte(uint32_t ch, uint32_t idx, uint8_t v) noexcept {
    noteStartupAccess();
    const bool high = (idx == 1U) || (idx == 3U);
    regs_[base(ch) + idx] = high ? static_cast<uint8_t>(v & 0x1FU) : v;
    pwm_written_ = true;
    if ((regs_[model_reg::kMode2] & model_reg::kOch) == 0U) {
      return; // OCH = 0: applied at the STOP
    }
    pending_[ch] = static_cast<uint8_t>(pending_[ch] | (1U << idx));
    if (!readings.ack_needs_all_four || (pending_[ch] == 0x0FU)) {
      live_[ch][idx] = regs_[base(ch) + idx];
      if (pending_[ch] == 0x0FU) {
        for (uint32_t i = 0U; i < 4U; ++i) {
          live_[ch][i] = regs_[base(ch) + i];
        }
        pending_[ch] = 0U;
        channelWriteClearsRestart();
      }
    }
  }

  void channelWriteClearsRestart() noexcept {
    restart_ = false; // section 7.3.1.1, "other actions", 3 and 4
    stopped_ = false;
  }

  void writeMode1(uint8_t v) noexcept {
    ++mode1_writes;
    const bool was_asleep = Asleep();
    const bool sleep = (v & model_reg::kSleep) != 0U;
    bool ext = (regs_[model_reg::kMode1] & model_reg::kExtClk) != 0U;
    if (((v & model_reg::kExtClk) != 0U) && was_asleep && sleep) {
      ext = true; // sticky; set only with SLEEP already set (table 5)
    }
    if (!was_asleep && sleep) {
      bool any = false;
      for (uint32_t ch = 0U; ch < 16U; ++ch) {
        const bool active = ChannelActive(ch);
        held_[ch] = readings.sleep_holds_outputs && active;
        any = any || active;
      }
      restart_ = restart_ || any;
    }
    if (was_asleep && !sleep) {
      wake_us_ = now_us_;
      stopped_ = restart_ && !readings.wake_runs_without_restart;
      held_.fill(false);
    }
    regs_[model_reg::kMode1] =
        static_cast<uint8_t>((static_cast<uint32_t>(v) & 0x3FU & ~uint32_t{model_reg::kExtClk}) |
                             (ext ? uint32_t{model_reg::kExtClk} : 0U));
    if (((v & model_reg::kRestart) != 0U) && restart_ && !sleep) {
      if (ext || ((now_us_ - wake_us_) >= model_reg::kStartUs)) {
        restart_ = false;
        stopped_ = false;
      } else {
        ++early_restart_writes;
      }
    }
  }

  void stop() noexcept {
    if (pwm_written_ && ((regs_[model_reg::kMode2] & model_reg::kOch) == 0U)) {
      syncAllLive();
      pending_.fill(0U); // every stored byte is live now
      channelWriteClearsRestart();
    }
    pwm_written_ = false;
  }

  int generalCall(const uint8_t* data, std::size_t len, std::size_t limit) noexcept {
    // Section 7.6: exactly one byte, 06h, then STOP; anything else is not acknowledged.
    if ((len >= 1U) && (limit >= 1U) && (data[0] == model_reg::kSwrstByte)) {
      if (len == 1U) {
        PowerOnReset();
      }
      return 1;
    }
    return 0;
  }

  std::array<uint8_t, 256> regs_{};
  std::array<std::array<uint8_t, 4>, 16> live_{};
  std::array<bool, 16> held_{};
  std::array<uint8_t, 16> pending_{};
  uint8_t pointer_ = 0U;
  bool restart_ = false;
  bool stopped_ = false;
  bool pwm_written_ = false;
  uint64_t now_us_ = 0U;
  uint64_t wake_us_ = 0U;
};

} // namespace pca9685_test

#include "mock_pca9685_bus.hpp"

#endif // PCA9685_MOCK_PCA9685_HPP
