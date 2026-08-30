// settings.h — persistent configuration (NVS via the Preferences library).
// This is the single source of truth for every user-configurable value; the
// web portal reads and writes these fields and nothing is edited on-device.
#pragma once
#include <Arduino.h>

struct Settings {
    // Wi-Fi
    String   wifiSsid;
    String   wifiPass;

    // Location (fixed coordinate — geocoded from a place name or entered directly)
    String   locationName;     // e.g. "Seattle, WA"
    float    homeLat;
    float    homeLon;

    // Feature config
    uint16_t radarRangeNm;     // Flights tab search radius (<= 250 NM)
    String   icsUrl;           // Calendar .ics feed
    String   tickers;          // comma-separated symbols, e.g. "BTC,ETH,MSFT"
    bool     useMetric;        // units: metric vs imperial
    bool     use24hClock;

    // Photo frame
    String   photoUrl;         // JPEG source (blank = built-in nature default)
    uint16_t photoSeconds;     // rotate cadence (>= 10s)

    // Device
    uint8_t  brightness;       // 0..255
    uint16_t pollSeconds;      // data refresh cadence

    // On-device view state (not exposed in the web portal)
    uint8_t  tickerTf;         // Ticker timeframe index (0=1D .. 4=1Y)
    uint8_t  calView;          // Calendar view (0=List,1=Day,2=Week,3=Month)
    uint8_t  lastPanel;        // Last-selected panel (Page index) to restore on boot

    // Web portal PIN gate (empty = disabled)
    String   configPin;
};

// Load settings from NVS (populates defaults on first boot). Call once, early.
void settings_load();

// Persist the current settings to NVS.
void settings_save();

// Persist a single on-device view preference (cheap NVS write, no full save).
void settings_set_ticker_tf(uint8_t idx);
void settings_set_cal_view(uint8_t view);
void settings_set_last_panel(uint8_t panel);

// Access the live settings instance.
Settings &settings();

// True once valid Wi-Fi credentials exist (i.e. the device is provisioned).
bool settings_has_wifi();

// Clear ONLY the Wi-Fi credentials (used by the reset paths). Keeps everything else.
void settings_clear_wifi();

// Import a config.json blob (optional SD seed / web upload). Returns true on success.
bool settings_import_json(const String &json);

// Export the current settings as a JSON string (used by the web portal API).
String settings_export_json();
