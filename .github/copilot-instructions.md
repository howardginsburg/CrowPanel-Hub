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

## Threading model (Phase 2 — non-blocking fetches)
- **Two cores, split by job.** The Arduino `loopTask` (core 1) owns all LVGL rendering
  (`lv_timer_handler` via `display_tick`). A dedicated **net task pinned to core 0**
  (`data_task_start()` in [src/data.cpp](../src/data.cpp), 20 KB stack, priority 1) runs
  `data_tick()` — every blocking HTTPS fetch (weather, flights, tickers, calendar). The
  Wi-Fi driver also lives on core 0. This keeps the UI fluid while fetches block.
- **All widget mutation is serialized through a recursive LVGL mutex.** `ui_lock()` /
  `ui_unlock()` (+ RAII `UiLock`) in [src/ui.cpp](../src/ui.cpp); every public `ui_*`
  setter takes the lock. `loop()` wraps `ui_tick()`/`display_tick()` in the lock and
  delays *outside* it. Any code touching LVGL objects from the net task MUST hold it.
- **`yield()` does NOT feed the task watchdog.** It only switches to equal-or-higher
  priority tasks, so it never runs the lower-priority core-0 IDLE task the WDT watches. A
  long CPU-bound loop on the net task (e.g. the ~777 KB / 2000-event calendar ICS parse)
  must sprinkle a real `vTaskDelay(1)` periodically (e.g. every 64 lines) or it starves
  IDLE0 and reboots. Arduino's `loopTask` auto-feeds the WDT each `loop()`, so moving
  long work onto a custom task can reintroduce this.

## LVGL UI conventions (hard-won)
- **Solid color swatches/bars: use `lv_bar` (value 100%, color via `LV_PART_INDICATOR`),
  not a plain `lv_obj`.** The default theme's base `lv_obj` style has a subtle
  gradient/bevel that dithers into mottled two-tone speckle (worst on small
  high-contrast fills). `bg_opa=COVER`, `radius=0`, and `bg_grad_dir=NONE` do NOT fix
  it; `lv_bar` parts are styled flat and render clean.
- **Zero the padding on card-style `lv_obj` containers** (`lv_obj_set_style_pad_all(c, 0,
  0)`), else the theme's default padding shrinks the content area and clips the
  bottom-most child.
- **The bundled Montserrat fonts have no Unicode ellipsis glyph** — use ASCII `...`, not
  `\u2026`, or it renders as a tofu box.

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
- **Kill the serial monitor terminal before uploading** (it holds the COM port). Upload
  takes ~80–160 s and hard-resets the board via RTS (so no RST tap needed after upload).
- **The CH340 COM port re-enumerates** (has been both COM3 and COM4). Don't assume —
  run `& $pio device list` to find the current port, then pass `--upload-port <COMx>`.
- Run terminal commands **one at a time** (never parallel).

## Build / flash / monitor
```powershell
$pio = Join-Path $HOME ".platformio\penv\Scripts\pio.exe"
$proxy="https://packagefeedproxy.microsoft.io/pypi/simple/"; $env:PIP_INDEX_URL=$proxy; $env:UV_INDEX_URL=$proxy; $env:UV_DEFAULT_INDEX=$proxy

# build
& $pio run -e crowpanel-50

# upload (kill any monitor first; UTF-8 required; check the port with `pio device list`)
$env:PYTHONUTF8="1"; $env:PYTHONIOENCODING="utf-8"
& $pio run -e crowpanel-50 -t upload --upload-port COM4

# serial monitor @ 115200 (user must tap RST to see boot; port may be COM3 or COM4)
& $pio device monitor -e crowpanel-50 --port COM4
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
