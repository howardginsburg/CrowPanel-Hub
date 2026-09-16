// settings.h — persistent configuration (NVS via the Preferences library).
// This is the single source of truth for every user-configurable value; the
// web portal reads and writes these fields and nothing is edited on-device.
#pragma once
#include <Arduino.h>

// A single calendar feed: display name, .ics URL, event color (0xRRGGBB) and
// whether it's currently shown on-device. The list lives in Settings below.
#define MAX_CALENDARS 8
struct CalSource {
    String   name;
    String   url;
    uint32_t color;    // 0xRRGGBB event color
    bool     visible;  // on-device show/hide (persisted)
};

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
    String   icsUrl;           // Legacy single calendar feed (migrated into calendars[0])
    CalSource calendars[MAX_CALENDARS];  // Color-coded calendar feeds
    uint8_t  calCount;         // number of active entries in calendars[]
    String   tickers;          // comma-separated symbols, e.g. "BTC,ETH,MSFT"
    bool     useMetric;        // units: metric vs imperial
    bool     use24hClock;

    // Photo frame
    String   photoUrl;         // JPEG source (blank = built-in nature default)
    uint16_t photoSeconds;     // rotate cadence (>= 10s)

    // Severe weather alerts (US only, NWS)
    bool     alertsEnabled;    // show the severe-weather banner
    uint8_t  alertMinSeverity; // floor: 1=Minor,2=Moderate,3=Severe,4=Extreme
    uint16_t alertDismissMin;  // auto-dismiss after N minutes (0 = never)

    // Device
    uint8_t  brightness;       // 0..255
    uint16_t dimMinutes;       // idle minutes before dimming (0 = disabled)
    uint8_t  dimPercent;       // dim to this % of normal brightness (0..100)
    bool     nightDimEnabled;  // enable time-based night dimming
    uint8_t  nightStartHour;   // local hour [0..23] night dimming begins
    uint8_t  nightEndHour;     // local hour [0..23] night dimming ends (wraps midnight)
    uint8_t  nightDimPercent;  // dim to this % of normal brightness at night (0..100)
    uint16_t pollSeconds;      // data refresh cadence
    uint8_t  theme;            // UI color theme index (UiThemeId; 0 = Midnight)

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

// Toggle a calendar's on-device visibility and persist just the calendar list.
void settings_set_cal_visible(uint8_t idx, bool on);

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
