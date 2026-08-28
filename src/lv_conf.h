// lv_conf.h  — LVGL 8.3.x configuration for the CrowPanel 5.0" dashboard.
// Enabled via -D LV_CONF_INCLUDE_SIMPLE and -I src in platformio.ini.
#pragma once
#include <stdint.h>

/*==================== COLOR ====================*/
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0   // esp_lcd writes native RGB565 straight into the
                             // framebuffer (data_gpio_nums map B/G/R to the
                             // matching bit lanes), so no byte swap is needed.
#define LV_COLOR_SCREEN_TRANSP 0
#define LV_COLOR_MIX_ROUND_OFS 0
#define LV_COLOR_CHROMA_KEY lv_color_hex(0x00ff00)

/*==================== MEMORY ====================*/
// The LVGL object/draw pool lives in PSRAM (via Arduino's ps_malloc), NOT in
// internal DRAM. Internal RAM is the scarce resource here: the Wi-Fi/mbedTLS
// stack needs DMA-capable internal buffers, and the hardware-AES path must
// allocate one per TLS record. An 80 KB internal LVGL pool starved it, so the
// large (~400 KB) chunked HTTPS calendar feed was truncated mid-stream
// ("esp-aes: Failed to allocate memory") and no events landed in-window.
// Routing LVGL to PSRAM frees ~80 KB internal AND removes the fixed-pool
// exhaustion that caused the 15-aircraft flights freeze (PSRAM has MBs free).
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE "Arduino.h"
#define LV_MEM_CUSTOM_ALLOC   ps_malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC ps_realloc
#define LV_MEM_BUF_MAX_NUM 16
#define LV_MEMCPY_MEMSET_STD 0

/*==================== HAL ====================*/
#define LV_DISP_DEF_REFR_PERIOD 16
#define LV_INDEV_DEF_READ_PERIOD 16
// Provide millis() as the LVGL tick source.
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#define LV_DPI_DEF 130

/*==================== FEATURES ====================*/
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_LOG 0

/*==================== DRAWING ====================*/
// Quality knobs: the LVGL heap now lives in PSRAM (LV_MEM_CUSTOM -> ps_malloc),
// so the small cache/dither buffers below are effectively free. Caches speed up
// repeated rounded-rect/circle/image draws; smoother gradients (3 stops + error
// diffusion dithering) remove the banding on the Home sky backdrop and cards.
#define LV_DRAW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 8
#define LV_CIRCLE_CACHE_SIZE 8
#define LV_IMG_CACHE_DEF_SIZE 8
#define LV_GRADIENT_MAX_STOPS 3
#define LV_DITHER_GRADIENT 1
#define LV_DISP_ROT_MAX_BUF (10 * 1024)

/*==================== FONTS ====================*/
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16
#define LV_FONT_FMT_TXT_LARGE 0
#define LV_USE_FONT_COMPRESSED 0

/*==================== TEXT ====================*/
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_"
#define LV_TXT_COLOR_CMD "#"
#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*==================== WIDGETS ====================*/
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CANVAS 1
#define LV_USE_CHECKBOX 1
#define LV_USE_DROPDOWN 1
#define LV_USE_IMG 1
#define LV_USE_LABEL 1
#define LV_LABEL_TEXT_SELECTION 1
#define LV_LABEL_LONG_TXT_HINT 1
#define LV_USE_LINE 1
#define LV_USE_ROLLER 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_TEXTAREA 1
#define LV_USE_TABLE 1

/*==================== EXTRA COMPONENTS ====================*/
#define LV_USE_CHART 1
#define LV_USE_LIST 1
#define LV_USE_MENU 1
#define LV_USE_METER 1
#define LV_USE_MSGBOX 1
#define LV_USE_SPINBOX 1
#define LV_USE_SPINNER 1
#define LV_USE_TABVIEW 1
#define LV_USE_TILEVIEW 1
#define LV_USE_WIN 1
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*==================== THEMES ====================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80

/*==================== OTHERS ====================*/
#define LV_USE_SNAPSHOT 0
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
