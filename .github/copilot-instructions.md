# Copilot instructions — CrowPanel ESP32 desk dashboard

Firmware for the **Elecrow CrowPanel ESP32 HMI 5.0"** (module **DIS07050H**): an
800×480 parallel-RGB touchscreen dashboard built with **PlatformIO + LVGL**. Read the
[README](../README.md) for the full hardware/pin reference and the display deep-dive.

## Hardware / toolchain facts (don't re-derive these)
- **Board:** ESP32-S3-WROOM-1-**N4R8** — 4 MB flash, **8 MB Octal PSRAM** (`qio_opi`).
- **Display:** 800×480 16-bit RGB565 parallel panel, no frame RAM (must be streamed
  from PSRAM continuously). Touch: GT911 over I²C (`0x5D`).
- **Serial console is on UART0** via the onboard CH340 bridge. Native USB-CDC is
  **impossible** here — the ESP32-S3 USB pins GPIO19/20 are used by the I²C bus. Do not
  suggest `ARDUINO_USB_CDC_ON_BOOT=1`.
- **Toolchain:** pioarduino fork = **Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5** (this is the
  latest; `esp_lcd` ships with it). LVGL **8.3.11**. This core 3.x is required for the
  `esp_lcd` RGB driver (double framebuffer + bounce buffer).
- The board does **not** auto-reboot when the serial monitor attaches — the user must
  physically press **RST**. Serial-only outcomes can only be verified by the user.

## The display driver (the hard-won part — see README §5)
- Driven by the **`esp_lcd` RGB panel driver** (NOT LovyanGFX — that was removed). All
  logic is in [src/display.cpp](../src/display.cpp); pins/timing in
  [src/board_pins.h](../src/board_pins.h).
- The "shaky"/shimmering screen was **RGB FIFO starvation from PSRAM latency**, cured by
  a **bounce buffer**. The validated factory config (do not change without cause):
  - `clk_src = LCD_CLK_SRC_PLL160M`, `num_fbs = 2`, `flags.fb_in_psram = 1`
  - `bounce_buffer_size_px = LCD_WIDTH * 10`  ← the actual fix
  - `dma_burst_size = 64`, pixel clock **12 MHz**
- Tear-free swap = **draw first, then wait one VSYNC** via a counter (`s_vsync_count`),
  the `esp_lcd` equivalent of the factory `presentFrameBuffer()`. LVGL runs
  `full_refresh` with the two framebuffers as draw buffers.
- Lowering the pixel clock, adding framebuffers, or changing LVGL render mode do NOT fix
  shimmer — only the bounce buffer does. `full_refresh` alone made it worse.

## Working conventions
- **Latest libraries rule:** stay on the latest published libraries unless there's a
  known bug. Do **not** vendor/patch/fork libraries — use published code and write only
  the minimal custom glue (e.g. the ~6-line VSYNC counter swap).
- Don't reintroduce LovyanGFX or `lgfx_crowpanel.h`.
- Do **not** create markdown files to document changes. Update the README when docs are
  explicitly requested.
- `pio.exe` is **not on PATH**. Invoke it as:
  `$pio = Join-Path $HOME ".platformio\penv\Scripts\pio.exe"; & $pio ...`
- **Always set the package proxy for builds:**
  `$proxy="https://packagefeedproxy.microsoft.io/pypi/simple/"; $env:PIP_INDEX_URL=$proxy; $env:UV_INDEX_URL=$proxy; $env:UV_DEFAULT_INDEX=$proxy`
- **Uploads additionally need UTF-8:** `$env:PYTHONUTF8="1"; $env:PYTHONIOENCODING="utf-8"`.
- **Kill the serial monitor terminal before uploading** (it holds COM3). Upload takes
  ~160 s and hard-resets the board via RTS.
- Run terminal commands **one at a time** (never parallel).

## Build / flash / monitor
```powershell
$pio = Join-Path $HOME ".platformio\penv\Scripts\pio.exe"
$proxy="https://packagefeedproxy.microsoft.io/pypi/simple/"; $env:PIP_INDEX_URL=$proxy; $env:UV_INDEX_URL=$proxy; $env:UV_DEFAULT_INDEX=$proxy

# build
& $pio run -e crowpanel-50

# upload (kill any monitor first; UTF-8 required)
$env:PYTHONUTF8="1"; $env:PYTHONIOENCODING="utf-8"
& $pio run -e crowpanel-50 -t upload

# serial monitor @ 115200 on COM3 (user must tap RST to see boot)
& $pio device monitor -e crowpanel-50 --port COM3
```

## Project layout
- [src/main.cpp](../src/main.cpp) — boot, factory-reset check, main loop
- [src/display.cpp](../src/display.cpp) — esp_lcd panel + LVGL + tear-free flush
- [src/touch_gt911.cpp](../src/touch_gt911.cpp) — GT911 reader + PCA9557 reset
- [src/net_wifi.cpp](../src/net_wifi.cpp) — Wi-Fi lifecycle + 3-layer provisioning
- [src/web_portal.cpp](../src/web_portal.cpp) / [src/web_page.h](../src/web_page.h) — async config server + captive portal
- [src/settings.cpp](../src/settings.cpp) — NVS-backed settings + JSON import/export
- [src/ui.cpp](../src/ui.cpp) — sidebar nav + pages + QR rendering
- [src/data.cpp](../src/data.cpp) — NTP, Open-Meteo weather, adsb.fi flights

## Known deferred items (don't touch unless asked)
- Startup DNS `-54` stalls — candidate fix is static DNS (8.8.8.8 / 1.1.1.1) in
  [src/net_wifi.cpp](../src/net_wifi.cpp).
- Clock timezone is currently applied only on a successful weather poll — decouple it
  from the weather path if asked.
