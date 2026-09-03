// display.cpp
// Tear-free LVGL bring-up driving the ESP32-S3 parallel-RGB panel through the
// published Espressif esp_lcd driver (num_fbs=2 double framebuffer in PSRAM).
// LVGL runs in full_refresh, rendering the whole frame into the back framebuffer,
// and the buffer swap is synchronised to VSYNC so no partially drawn frame is ever
// scanned out. Touch stays on the existing GT911 driver; backlight uses the
// Arduino LEDC API.
#include "display.h"
#include "touch_gt911.h"
#include "board_pins.h"
#include <Arduino.h>
#include <lvgl.h>
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static esp_lcd_panel_handle_t s_panel = nullptr;
static lv_color_t *s_fb0 = nullptr;
static lv_color_t *s_fb1 = nullptr;
static lv_color_t *volatile s_front_fb = nullptr;   // framebuffer last presented (for screenshots)
static lv_disp_draw_buf_t s_draw_buf;

// VSYNC frame counter, incremented by the panel's VSYNC ISR. flush presents a
// framebuffer with esp_lcd_panel_draw_bitmap and then waits for this counter to
// advance, i.e. for the queued buffer to actually reach the screen. This is the
// exact tear-free swap the Elecrow factory driver uses (presentFrameBuffer): draw
// first, then wait one VSYNC. No semaphores needed.
static volatile uint32_t s_vsync_count = 0;
static volatile uint32_t s_frame_count = 0;    // full frames presented (Diag FPS)

static bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t panel,
                               const esp_lcd_rgb_panel_event_data_t *edata,
                               void *user_ctx) {
    (void)panel; (void)edata; (void)user_ctx;
    ++s_vsync_count;
    return false;
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    (void)area;
    // In full_refresh LVGL renders the whole frame into the back framebuffer, so
    // both framebuffers stay fully coherent. Present it, then wait one VSYNC so the
    // buffer is actually on screen before LVGL starts drawing the next frame into
    // the other buffer (matches the factory presentFrameBuffer: draw, then wait).
    if (lv_disp_flush_is_last(drv)) {
        uint32_t count = s_vsync_count;
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_WIDTH, LCD_HEIGHT, color_p);
        s_front_fb = color_p;                                    // now the on-screen buffer
        ++s_frame_count;                                         // for Diag render FPS
        int64_t timeout_at = esp_timer_get_time() + 50 * 1000;   // 50 ms guard
        while (s_vsync_count == count) {
            if (esp_timer_get_time() >= timeout_at) break;
            taskYIELD();
        }
    }
    lv_disp_flush_ready(drv);
}

uint32_t display_frame_count() { return s_frame_count; }

// The framebuffer currently on screen (native RGB565, LCD_WIDTH*LCD_HEIGHT). Read
// under ui_lock() so LVGL isn't mid-swap. Returns nullptr before the first frame.
const void *display_front_framebuffer() { return (const void *)s_front_fb; }

static void touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    TouchPoint p = touch_read();
    if (p.touched) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = p.x;
        data->point.y = p.y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void display_set_brightness(uint8_t level) {
    ledcWrite(PIN_BACKLIGHT, level);
}

static void panel_init() {
    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src = LCD_CLK_SRC_PLL160M;           // factory driver clock source
    cfg.timings = {
        .pclk_hz           = LCD_PCLK_HZ,
        .h_res             = LCD_WIDTH,
        .v_res             = LCD_HEIGHT,
        .hsync_pulse_width = LCD_HSYNC_PULSE_WIDTH,
        .hsync_back_porch  = LCD_HSYNC_BACK_PORCH,
        .hsync_front_porch = LCD_HSYNC_FRONT_PORCH,
        .vsync_pulse_width = LCD_VSYNC_PULSE_WIDTH,
        .vsync_back_porch  = LCD_VSYNC_BACK_PORCH,
        .vsync_front_porch = LCD_VSYNC_FRONT_PORCH,
    };
    cfg.timings.flags.hsync_idle_low  = 0;
    cfg.timings.flags.vsync_idle_low  = 0;
    cfg.timings.flags.de_idle_high    = 0;
    cfg.timings.flags.pclk_active_neg = 1;       // matches the Elecrow factory config
    cfg.timings.flags.pclk_idle_high  = 0;

    cfg.data_width            = 16;
    cfg.bits_per_pixel        = 16;
    cfg.num_fbs               = 2;               // double framebuffer in PSRAM
    cfg.bounce_buffer_size_px = LCD_WIDTH * 10;  // prefetch 10 lines into internal SRAM so the panel
                                                 // FIFO never starves on PSRAM latency -- this is what
                                                 // eliminates the constant pclk-independent shimmer
    cfg.dma_burst_size        = 64;              // factory DMA burst length

    cfg.hsync_gpio_num = LCD_HSYNC;
    cfg.vsync_gpio_num = LCD_VSYNC;
    cfg.de_gpio_num    = LCD_DE;
    cfg.pclk_gpio_num  = LCD_PCLK;
    cfg.disp_gpio_num  = -1;

    // FB bit lanes: bits 0..4 = Blue, 5..10 = Green, 11..15 = Red (LVGL native RGB565).
    cfg.data_gpio_nums[0]  = LCD_B0;
    cfg.data_gpio_nums[1]  = LCD_B1;
    cfg.data_gpio_nums[2]  = LCD_B2;
    cfg.data_gpio_nums[3]  = LCD_B3;
    cfg.data_gpio_nums[4]  = LCD_B4;
    cfg.data_gpio_nums[5]  = LCD_G0;
    cfg.data_gpio_nums[6]  = LCD_G1;
    cfg.data_gpio_nums[7]  = LCD_G2;
    cfg.data_gpio_nums[8]  = LCD_G3;
    cfg.data_gpio_nums[9]  = LCD_G4;
    cfg.data_gpio_nums[10] = LCD_G5;
    cfg.data_gpio_nums[11] = LCD_R0;
    cfg.data_gpio_nums[12] = LCD_R1;
    cfg.data_gpio_nums[13] = LCD_R2;
    cfg.data_gpio_nums[14] = LCD_R3;
    cfg.data_gpio_nums[15] = LCD_R4;

    cfg.flags.fb_in_psram = 1;

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &s_panel));

    esp_lcd_rgb_panel_event_callbacks_t cbs = {};
    cbs.on_vsync = on_vsync;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(s_panel, &cbs, nullptr));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(s_panel, 2,
                                                       (void **)&s_fb0, (void **)&s_fb1));
}

void display_init() {
    // Backlight PWM (Arduino 3.x LEDC API).
    ledcAttach(PIN_BACKLIGHT, 44100, 8);

    panel_init();
    display_set_brightness(200);

    lv_init();

    // Use the two driver framebuffers directly as LVGL's draw buffers. full_refresh
    // (the published esp_lvgl_port double-buffer tear-free mode) makes LVGL render
    // the WHOLE frame into the back buffer before every VSYNC swap, so both
    // framebuffers are always fully coherent. This eliminates the buffer-alternation
    // shimmer that direct_mode produced (direct_mode only redraws changed regions
    // and relies on cross-frame area joining to keep both buffers in sync, which was
    // showing as a constant, pclk-independent screen shake).
    lv_disp_draw_buf_init(&s_draw_buf, s_fb0, s_fb1, LCD_WIDTH * LCD_HEIGHT);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = LCD_WIDTH;
    disp_drv.ver_res      = LCD_HEIGHT;
    disp_drv.flush_cb     = flush_cb;
    disp_drv.draw_buf     = &s_draw_buf;
    disp_drv.full_refresh = 1;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_cb;
    lv_indev_drv_register(&indev_drv);

    Serial.println("[display] LVGL ready (800x480, esp_lcd num_fbs=2, tear-free)");
}

void display_tick() {
    lv_timer_handler();
}
