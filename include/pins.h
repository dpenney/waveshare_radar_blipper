/**
 * @file pins.h
 * @brief GPIO pin assignments for the Waveshare ESP32-S3-Knob-Touch-LCD-1.8
 *
 * Board: https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm
 */

#pragma once

// ── Display (SH8601 AMOLED — QSPI) ──────────────────────────────────────────
#define LCD_CS    14   ///< Chip select
#define LCD_SCLK  13   ///< Clock
#define LCD_MOSI  15   ///< SDIO0 (Data 0)
#define LCD_MISO  16   ///< SDIO1 (Data 1)
#define LCD_D2    17   ///< SDIO2 (Data 2)
#define LCD_D3    18   ///< SDIO3 (Data 3)
#define LCD_RST   21   ///< Hardware reset (active LOW)
#define LCD_BL    47   ///< Backlight enable (HIGH = on)

// ── Touch controller (CST816 — I2C, addr 0x15) ───────────────────────────────
#define TOUCH_SDA  11  ///< I2C data
#define TOUCH_SCL  12  ///< I2C clock
#define TOUCH_RST  10  ///< Hardware reset (active LOW, pulse to initialise)
#define TOUCH_INT   9  ///< Interrupt — active LOW open-drain, use INPUT_PULLUP

// ── Rotary encoder (two independent micro-switches) ──────────────────────────
//   A goes LOW while turning CW,  HIGH on release → count UP
//   B goes LOW while turning CCW, HIGH on release → count DOWN
#define ENCODER_A   8  ///< CW  switch
#define ENCODER_B   7  ///< CCW switch
