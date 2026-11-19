/**
 * @file pca9685_registers.hpp
 * @brief PCA9685 register map: addresses, bit fields and reset values (datasheet section 7.3).
 * @copyright Copyright (c) 2026 The driver-pca9685 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Registers are 8 bits wide. The control register (the first byte after the address) points at
 * one; with MODE1.AI set the pointer moves to the next register after every byte, from LED15_OFF_H
 * (0x45) back to MODE1 and from PRE_SCALE (0xFE) back to MODE1 (table 4, remark). With AI clear
 * every byte of a transfer goes to, or comes from, the same register. Addresses 0x46 to 0xF9 are
 * reserved and not acknowledged; 0xFF (TestMode) is reserved and never touched by the driver.
 *
 * Section, table and figure numbers refer to the NXP PCA9685 product data sheet, Rev. 4
 * (16 April 2015).
 */
#ifndef PCA9685_REGISTERS_HPP
#define PCA9685_REGISTERS_HPP

#include <cstdint>

#include "pca9685_detail.hpp"

/** @brief The PCA9685 register map. */
namespace pca9685::reg {

/**
 * @defgroup pca9685_registers Register map
 * @brief Addresses, fields and reset values.
 * @{
 */

inline constexpr uint8_t kMode1 = 0x00;   ///< MODE1 (R/W): restart, clock, AI, sleep, group calls.
inline constexpr uint8_t kMode2 = 0x01;   ///< MODE2 (R/W): output inversion, change, drive, OE.
inline constexpr uint8_t kSubAdr1 = 0x02; ///< SUBADR1 (R/W): I2C subaddress 1 (section 7.3.6).
inline constexpr uint8_t kSubAdr2 = 0x03; ///< SUBADR2 (R/W): I2C subaddress 2.
inline constexpr uint8_t kSubAdr3 = 0x04; ///< SUBADR3 (R/W): I2C subaddress 3.
inline constexpr uint8_t kAllCallAdr = 0x05; ///< ALLCALLADR (R/W): LED All Call address (7.3.7).
inline constexpr uint8_t kLed0OnL = 0x06;    ///< LED0_ON_L (R/W): first of 16 x 4 channel bytes.
inline constexpr uint8_t kLed15OffH = 0x45;  ///< LED15_OFF_H (R/W): the last channel byte.
inline constexpr uint8_t kAllLedOnL = 0xFA;  ///< ALL_LED_ON_L (W): loads every LEDn_ON_L (7.3.4).
inline constexpr uint8_t kAllLedOffH = 0xFD; ///< ALL_LED_OFF_H (W): loads every LEDn_OFF_H.
inline constexpr uint8_t kPreScale = 0xFE;   ///< PRE_SCALE (R/W): PWM frequency; written in SLEEP.

/// Bytes per channel: ON_L, ON_H, OFF_L, OFF_H (table 7).
inline constexpr uint8_t kBytesPerChannel = 4;
/// Number of LED channels.
inline constexpr uint8_t kChannels = 16;

/**
 * @brief The address of LEDn_ON_L of a channel (table 4).
 * @param channel 0 to 15; masked, so any value maps to a channel.
 * @return 0x06 + 4 x channel.
 */
[[nodiscard]] constexpr uint8_t ChannelBase(uint32_t channel) noexcept {
  return static_cast<uint8_t>(uint32_t{kLed0OnL} +
                              (uint32_t{kBytesPerChannel} * (channel & 0x0FU)));
}

/// SUBADR1 to 3 and ALLCALLADR bit 0: read only, reads 0 (tables 9, 10).
inline constexpr uint8_t kSubAdrReadOnly = 0x01;
/// SUBADR1 after a reset (table 9).
inline constexpr uint8_t kSubAdr1Reset = 0xE2;

// MODE1 (table 5).
inline constexpr uint8_t kMode1Restart = 1U << 7; ///< RESTART: reads 1 after a sleep with live PWM;
                                                  ///< writing 1 restarts the channels.
inline constexpr uint8_t kMode1ExtClk = 1U << 6;  ///< EXTCLK: use the EXTCLK pin; sticky.
inline constexpr uint8_t kMode1Ai = 1U << 5;      ///< AI: register auto-increment.
inline constexpr uint8_t kMode1Sleep = 1U << 4;   ///< SLEEP: oscillator off, low-power mode.
inline constexpr uint8_t kMode1Sub1 = 1U << 3;    ///< SUB1: answer I2C subaddress 1.
inline constexpr uint8_t kMode1Sub2 = 1U << 2;    ///< SUB2: answer I2C subaddress 2.
inline constexpr uint8_t kMode1Sub3 = 1U << 1;    ///< SUB3: answer I2C subaddress 3.
inline constexpr uint8_t kMode1AllCall = 1U << 0; ///< ALLCALL: answer the LED All Call address.
/// MODE1 after a power-on or software reset: SLEEP and ALLCALL set (table 5).
inline constexpr uint8_t kMode1Reset = 0x11;

// MODE2 (table 6).
inline constexpr uint8_t kMode2Reserved = 0xE0;  ///< Bits 7..5: reserved, read only, read 0.
inline constexpr uint8_t kMode2Invrt = 1U << 4;  ///< INVRT: output logic inverted (OE = 0 only).
inline constexpr uint8_t kMode2Och = 1U << 3;    ///< OCH: outputs change on ACK, not on STOP.
inline constexpr uint8_t kMode2OutDrv = 1U << 2; ///< OUTDRV: totem pole (1) or open drain (0).
/// MODE2 after a reset: change on STOP, totem pole, not inverted, LEDn = 0 when OE = 1.
inline constexpr uint8_t kMode2Reset = 0x04;
/// MODE2.OUTNE[1:0], the level of every output while OE is high (table 11).
using Mode2Outne = detail::Field<uint8_t, 0U, 2U>;
inline constexpr uint32_t kOutneLow = 0U; ///< OUTNE = 00: LEDn = 0.
inline constexpr uint32_t kOutneHigh =
    1U;                                   ///< OUTNE = 01: LEDn = 1 (totem pole), Hi-Z (open drain).
inline constexpr uint32_t kOutneHiZ = 2U; ///< OUTNE = 10: LEDn = high impedance.

// LEDn_ON_H and LEDn_OFF_H (table 7).
inline constexpr uint8_t kLedHReserved = 0xE0; ///< Bits 7..5 of the _H bytes: read only, read 0.
inline constexpr uint8_t kLedFull = 1U << 4;   ///< Bit 4: full ON (ON_H) or full OFF (OFF_H).
inline constexpr uint8_t kLedCountHigh = 0x0F; ///< Bits 3..0: count bits 11..8.
/// Highest LEDn_ON and LEDn_OFF count: the 12-bit counter runs 0 to 4095 (section 7.3.3).
inline constexpr uint16_t kMaxCount = 4095;
/// LEDn_OFF_H after a reset: full OFF (table 7). The other three bytes reset to 0.
inline constexpr uint8_t kLedOffHReset = 0x10;

// PRE_SCALE (table 8, section 7.3.5).
inline constexpr uint8_t kPreScaleReset = 0x1E; ///< 30: about 200 Hz from the 25 MHz oscillator.
inline constexpr uint8_t kPreScaleMin = 3;      ///< The hardware forces at least 3 (1526 Hz).

// The fields are the bits above, proved at build time (KB-CPP20-012).
static_assert(Mode2Outne::kMask == 0x03U);
static_assert(Mode2Outne::Insert(kMode2Reset, kOutneHiZ) == 0x06U);
static_assert(ChannelBase(0U) == kLed0OnL);
static_assert((ChannelBase(15U) + kBytesPerChannel - 1U) == kLed15OffH);
static_assert(((kAllLedOffH - kAllLedOnL) + 1U) == kBytesPerChannel);
static_assert(((kMode1Reset & kMode1Sleep) != 0U) && ((kMode1Reset & kMode1AllCall) != 0U) &&
              ((kMode1Reset & kMode1Ai) == 0U));

/** @} */

} // namespace pca9685::reg

#endif // PCA9685_REGISTERS_HPP
