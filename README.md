# CrowPanel 5.0" — Desk Command Center

A touchscreen desk dashboard for the **Elecrow CrowPanel ESP32 HMI 5.0"** display,
built with **PlatformIO + LVGL**. It shows the time, local weather, **live aircraft
flying nearby (with tail numbers)**, your calendar, market tickers, and air/environment
readings — all configured from a phone or laptop through a built-in web page (no code
edits, no re-flashing to change settings).

> **Status:** working prototype. Board bring-up (display, touch, LVGL shell), Wi-Fi
> provisioning, the web config portal, and every data tab — clock/weather, flights radar,
> calendar, tickers, and air/UV — are implemented.

> 🛠️ **All the technical detail** — pin map, display driver, module design, data flow,
> build config — lives in **[architecture.md](architecture.md)**. This README is the
> high-level tour.

---

## The board

**Elecrow CrowPanel ESP32 HMI 5.0"** — module marking **DIS07050H**.

| | |
|---|---|
| **MCU** | ESP32-S3-WROOM-1-N4R8 (Xtensa dual-core @ 240 MHz) |
| **Flash / PSRAM** | 4 MB flash, **8 MB Octal PSRAM** |
| **Wireless** | Wi-Fi 2.4 GHz b/g/n + BLE 5.0 |
| **Display** | 5.0" 800×480 TFT-LCD, 16-bit parallel RGB (RGB565) |
| **Touch** | GT911 capacitive, I²C |
| **Extras** | microSD, I²S audio (speaker connector), PWM backlight, battery input + charging |
| **Buttons** | BOOT, RST |

> ⚠️ **This is the 5.0" board (DIS07050H), *not* the 7.0" (DIS08070H)** — the two boards
> use different RGB pin maps and timings, so their display configs are not interchangeable.

- Elecrow wiki: <https://www.elecrow.com/wiki/esp32-display-502727-intelligent-touch-screen-wi-fi-ble-560.html>
- Elecrow GitHub (examples): <https://github.com/Elecrow-RD/CrowPanel-5.0-HMI-ESP32-Display-800x480>

---

## What it does

A left-sidebar, multi-page LVGL dashboard:

| Tab | Shows | Data source |
|---|---|---|
| **Home** | Big local time + date, current weather, sun/moon, hourly outlook | NTP + Open-Meteo |
| **Flights** | Live radar of nearby aircraft — tail number, type, altitude, distance | adsb.fi |
| **Calendar** | Upcoming events | your `.ics` feed |
| **Tickers** | Stock / crypto prices with sparklines | Yahoo Finance |
| **Air** | US air-quality index + UV | Open-Meteo |
| **Diag** | Uptime, heap/PSRAM, Wi-Fi, reset reason, firmware | on-device |
| **Config** | Where to configure the device (URL + QR) | — |

All data APIs are **keyless**. Flight data is credited to **adsb.fi** (non-commercial use).

---

## Configuring it

Everything is set from a **web page** — nothing is typed on the touchscreen. On first
boot the device starts a **`CrowPanel-setup`** Wi-Fi hotspot with a captive portal; join
it from your phone (the screen shows a QR code) and enter your home Wi-Fi and location.
After that, open **`http://crowpanel.local`** from any device on your network to change
any setting. Hold **BOOT (~5 s at power-on)** to wipe Wi-Fi and return to setup.

The full provisioning model and web-portal internals are in
[architecture.md](architecture.md).

---

## Build & flash

**Prerequisites:** [VS Code](https://code.visualstudio.com/) with the
[PlatformIO IDE](https://platformio.org/install/ide?install=vscode) extension (or the
PlatformIO CLI).

```sh
pio run                 # build
pio run -t upload       # flash over USB-C
pio device monitor      # serial console @ 115200 (UART0 via the onboard CH340 bridge)
```

On first boot the screen shows the **Config** tab with setup instructions — join the
`CrowPanel-setup` hotspot to finish. Toolchain and build-flag specifics are in
[architecture.md](architecture.md).

---

## Credits & license

- Board & examples: **Elecrow** ([wiki](https://www.elecrow.com/wiki/), [GitHub](https://github.com/Elecrow-RD))
- Flight data: **[adsb.fi](https://adsb.fi/)** (credit required, non-commercial)
- Weather / air / UV: **[Open-Meteo](https://open-meteo.com/)**
- Tickers: **Yahoo Finance** chart API
- **[LVGL](https://lvgl.io/)**, **[ArduinoJson](https://arduinojson.org/)**,
  **[ESPAsyncWebServer](https://github.com/mathieucarbou/ESPAsyncWebServer)**,
  **[pioarduino](https://github.com/pioarduino/platform-espressif32)** (Arduino-ESP32 / ESP-IDF)

License: MIT (add a `LICENSE` file to confirm).
