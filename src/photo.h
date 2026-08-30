// photo.h — network photo frame. Downloads a JPEG over HTTPS and decodes it
// (JPEGDEC) straight into a PSRAM RGB565 framebuffer that the UI binds to an
// LVGL canvas. Rotation cadence lives in settings().photoSeconds.
#pragma once
#include <Arduino.h>

// Full-bleed canvas below the 44px top bar (800 x 436).
#define PHOTO_W 800
#define PHOTO_H 436

// Allocate the PSRAM framebuffer (and download scratch). Call once before the
// UI binds the canvas. Safe to call more than once.
void photo_init();

// The RGB565 framebuffer the UI canvas points at. Never freed after init.
uint16_t *photo_buffer();
int photo_width();
int photo_height();

// Download the configured (or default) image and decode it into the buffer.
// Returns true on success; the buffer is left untouched on failure so the
// previous photo stays on screen. Blocking (runs on the main loop).
bool photo_fetch();

// Human-readable status of the last fetch ("OK", "HTTP 404", "Decode failed"...).
const char *photo_status();
