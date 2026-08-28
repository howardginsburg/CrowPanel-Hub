// board_pins.h
// Central pin + hardware constants for the CrowPanel ESP32 HMI 5.0" (DIS07050H).
// Verified against the Elecrow 5.0" 800x480 example. If you have a DIFFERENT panel
// size (e.g. the 7.0" DIS08070H) these values are WRONG — that board uses a
// different RGB pin map and timing.
#pragma once

// ---------------------------------------------------------------------------
// Display: 800x480 16-bit parallel RGB (RGB565), driver ILI6122/ILI5960
// ---------------------------------------------------------------------------
#define LCD_WIDTH   800
#define LCD_HEIGHT  480

// RGB565 data bus (esp_lcd data-line order: d0..d4 = Blue, d5..d10 = Green, d11..d15 = Red)
#define LCD_B0  8
#define LCD_B1  3
#define LCD_B2  46
#define LCD_B3  9
#define LCD_B4  1
#define LCD_G0  5
#define LCD_G1  6
#define LCD_G2  7
#define LCD_G3  15
#define LCD_G4  16
#define LCD_G5  4
#define LCD_R0  45
#define LCD_R1  48
#define LCD_R2  47
#define LCD_R3  21
#define LCD_R4  14

// RGB control lines
#define LCD_DE     40
#define LCD_VSYNC  41
#define LCD_HSYNC  39
#define LCD_PCLK   0

// RGB timing (from the Elecrow 5.0" reference config)
#define LCD_HSYNC_FRONT_PORCH  8
#define LCD_HSYNC_PULSE_WIDTH  4
#define LCD_HSYNC_BACK_PORCH   43
#define LCD_VSYNC_FRONT_PORCH  8
#define LCD_VSYNC_PULSE_WIDTH  4
#define LCD_VSYNC_BACK_PORCH   12
// 12 MHz is the pixel clock the Elecrow factory example uses for this panel,
// paired with the esp_lcd bounce buffer (see display.cpp). The bounce buffer is
// the real cure for the shimmer (it prefetches lines into SRAM so the RGB FIFO
// never starves on PSRAM latency); the matching 12 MHz clock keeps us on the exact
// validated factory timing.
#define LCD_PCLK_HZ            12000000

// Backlight (PWM)
#define PIN_BACKLIGHT  2

// ---------------------------------------------------------------------------
// I2C bus (shared: GT911 touch, PCA9557 IO expander)
// ---------------------------------------------------------------------------
#define PIN_I2C_SDA  19
#define PIN_I2C_SCL  20

#define GT911_ADDR      0x5D   // GT911 primary address (0x14 is the alternate)
#define PCA9557_ADDR    0x18   // IO expander (touch reset timing on v3.0 boards)

// ---------------------------------------------------------------------------
// microSD (SPI)
// ---------------------------------------------------------------------------
#define PIN_SD_MOSI  11
#define PIN_SD_MISO  13
#define PIN_SD_CLK   12
#define PIN_SD_CS    10

// ---------------------------------------------------------------------------
// I2S audio out (onboard speaker + amp)
// ---------------------------------------------------------------------------
#define PIN_I2S_LRCLK  18
#define PIN_I2S_BCLK   42
#define PIN_I2S_DIN    17

// ---------------------------------------------------------------------------
// Exposed connectors / misc
// ---------------------------------------------------------------------------
#define PIN_GPIO_D     38   // Grove digital header -> PIR (screen wake/dim)
#define PIN_UART0_RX   44   // Grove UART header -> GPS RX (UART0 is shared with the
                            // serial console: native USB-CDC is unavailable because
                            // the ESP32-S3 USB pins GPIO19/20 are used by the I2C bus)
#define PIN_UART0_TX   43
#define PIN_BOOT_BTN   0    // BOOT button: hold ~5s at power-on = Wi-Fi factory reset
