// display.h — LVGL bring-up on top of the esp_lcd RGB panel driver + GT911.
#pragma once
#include <stdint.h>

// Initialise the esp_lcd RGB panel (double framebuffer + bounce buffer in PSRAM),
// allocate the LVGL draw buffers, and register the LVGL display + touch input
// drivers. Call once from setup().
void display_init();

// Pump LVGL timers/rendering. Call frequently from loop().
void display_tick();

// Backlight brightness, 0..255. Persisted brightness is applied by the caller.
void display_set_brightness(uint8_t level);

// Full frames presented to the panel so far (one per LVGL flush). The Diag page
// samples this to compute an on-device render FPS.
uint32_t display_frame_count();
