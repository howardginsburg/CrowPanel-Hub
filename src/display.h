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

// Idle + night auto-dim. Pump display_dim_tick() once per loop() (under
// ui_lock()). It resolves two layers into one backlight target: idle-dim (to
// settings().dimPercent after settings().dimMinutes of no touch; 0 = disabled)
// and night-dim (to settings().nightDimPercent during the configured local
// hours). A touch wakes to normal brightness and returns to the dim level once
// idle again. display_wake() restores full brightness on demand.
void display_dim_tick();
void display_wake();

// Full frames presented to the panel so far (one per LVGL flush). The Diag page
// samples this to compute an on-device render FPS.
uint32_t display_frame_count();

// Pointer to the framebuffer currently scanned out (native RGB565, packed
// LCD_WIDTH*LCD_HEIGHT). Read it under ui_lock() so LVGL is not mid buffer-swap.
// nullptr until the first frame has been presented.
const void *display_front_framebuffer();
