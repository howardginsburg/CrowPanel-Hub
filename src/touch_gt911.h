// touch_gt911.h — minimal GT911 capacitive-touch reader over I2C.
// The GT911 self-initialises at power-on, so we only read points here. On v3.0
// boards the reset line is behind the PCA9557 expander; pca9557_reset_gt911()
// performs a best-effort reset pulse if that expander is present.
#pragma once
#include <stdint.h>
#include <stdbool.h>

struct TouchPoint {
    bool     touched;
    uint16_t x;
    uint16_t y;
};

// Initialise the shared I2C bus and the GT911. Safe to call once from setup().
void touch_init();

// Read the first active touch point (scaled to the panel resolution).
TouchPoint touch_read();
