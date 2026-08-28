// touch_gt911.cpp
#include "touch_gt911.h"
#include "board_pins.h"
#include <Arduino.h>
#include <Wire.h>

// GT911 register map (subset)
static const uint16_t GT911_REG_STATUS  = 0x814E;  // buffer status + touch count
static const uint16_t GT911_REG_POINT1  = 0x814F;  // point1: [id, xL,xH, yL,yH, sL,sH]
static const uint16_t GT911_REG_PRODUCT = 0x8140;  // 4-byte product id

// PCA9557 IO-expander: IO0 -> GT911 reset, IO1 -> GT911 INT (per the V3.0 board).
static const uint8_t PCA9557_OUTPUT = 0x01;
static const uint8_t PCA9557_CONFIG = 0x03;

// The INT level at reset-release selects the GT911 address, so probe both.
static const uint8_t GT911_ADDRS[] = { 0x5D, 0x14 };

static uint8_t s_addr = 0;   // discovered GT911 address (0 = not present)

// --- PCA9557 expander access -------------------------------------------------
static uint8_t pca9557_read(uint8_t reg) {
    Wire.beginTransmission(PCA9557_ADDR);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom((int)PCA9557_ADDR, 1);
    return Wire.available() ? Wire.read() : 0xFF;
}

static void pca9557_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(PCA9557_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

// Carrier-board GT911 reset: hold RESET(IO0)+INT(IO1) low, release RESET high
// while INT stays low (selects I2C address 0x5D), then free INT as an input.
// Matches Elecrow's factory V3.0 sequence; the previous code drove INT high at
// release, which enrolled the GT911 at 0x14 (or unreliably) so it was "not found".
static void gt911_reset() {
    pca9557_write(PCA9557_CONFIG, 0xFC);                                // IO0,IO1 outputs
    pca9557_write(PCA9557_OUTPUT, pca9557_read(PCA9557_OUTPUT) & 0xFC); // reset+int LOW
    delay(20);
    pca9557_write(PCA9557_OUTPUT, pca9557_read(PCA9557_OUTPUT) | 0x01); // release reset HIGH
    delay(100);
    pca9557_write(PCA9557_CONFIG, pca9557_read(PCA9557_CONFIG) | 0x02); // INT back to input
}

static bool gt911_read_at(uint8_t addr, uint16_t reg, uint8_t *buf, size_t len) {
    Wire.beginTransmission(addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, (int)len) != (int)len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

static void gt911_clear_status() {
    Wire.beginTransmission(s_addr);
    Wire.write((uint8_t)(GT911_REG_STATUS >> 8));
    Wire.write((uint8_t)(GT911_REG_STATUS & 0xFF));
    Wire.write((uint8_t)0);
    Wire.endTransmission();
}

void touch_init() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
    gt911_reset();

    uint8_t id[4];
    for (uint8_t addr : GT911_ADDRS) {
        if (gt911_read_at(addr, GT911_REG_PRODUCT, id, sizeof(id))) {
            s_addr = addr;
            Serial.printf("[touch] GT911 found at 0x%02X, ID %.4s\n", addr, id);
            return;
        }
    }
    Serial.println("[touch] GT911 not found (0x5D/0x14)");
}

TouchPoint touch_read() {
    TouchPoint p{false, 0, 0};
    if (!s_addr) return p;

    uint8_t status = 0;
    if (!gt911_read_at(s_addr, GT911_REG_STATUS, &status, 1)) return p;

    // Bit7 = buffer ready; low nibble = number of touch points.
    if (!(status & 0x80)) return p;
    uint8_t points = status & 0x0F;

    uint8_t d[7];
    bool valid = points > 0 && points <= 5 &&
                 gt911_read_at(s_addr, GT911_REG_POINT1, d, sizeof(d));

    // Always acknowledge the frame so the GT911 can publish the next sample.
    gt911_clear_status();
    if (!valid) return p;

    // d[0]=track id, d[1..2]=x (little-endian), d[3..4]=y (little-endian).
    uint16_t x = (uint16_t)(d[1] | (d[2] << 8));
    uint16_t y = (uint16_t)(d[3] | (d[4] << 8));
    if (x >= LCD_WIDTH)  x = LCD_WIDTH - 1;
    if (y >= LCD_HEIGHT) y = LCD_HEIGHT - 1;
    p.x = x;
    p.y = y;
    p.touched = true;
    return p;
}
