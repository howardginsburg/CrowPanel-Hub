// settings.cpp
#include "settings.h"
#include <Preferences.h>
#include <ArduinoJson.h>

static Preferences s_prefs;
static Settings    s_cfg;
static const char *NS = "crowpanel";

static void apply_defaults() {
    s_cfg.wifiSsid     = "";
    s_cfg.wifiPass     = "";
    s_cfg.locationName = "";
    s_cfg.homeLat      = 47.6062f;   // Seattle placeholder until configured
    s_cfg.homeLon      = -122.3321f;
    s_cfg.radarRangeNm = 25;
    s_cfg.icsUrl       = "";
    s_cfg.tickers      = "MSFT,AAPL,NVDA,GOOGL,AMZN";
    s_cfg.useMetric    = true;
    s_cfg.use24hClock  = true;
    s_cfg.photoUrl     = "";
    s_cfg.photoSeconds = 60;
    s_cfg.alertsEnabled    = true;
    s_cfg.alertMinSeverity = 3;      // Severe & above
    s_cfg.alertDismissMin  = 10;
    s_cfg.brightness   = 200;
    s_cfg.pollSeconds  = 60;
    s_cfg.tickerTf     = 0;
    s_cfg.calView      = 0;
    s_cfg.lastPanel    = 0;
    s_cfg.configPin    = "";
}

// --- Field-descriptor table: single source of truth for persistence + JSON. ---
// Each field lists its NVS key and (optional) JSON key once, so load/save/import/
// export are table-driven loops instead of 4 hand-synced blocks. Defaults live in
// apply_defaults(); privacy/derived export values (wifiConfigured, pinSet) and the
// alertMinSeverity reset are handled explicitly below. ptr points into s_cfg, so
// the table must follow its definition.
enum class FType : uint8_t { Str, Bool, U8, U16, F32 };
struct FieldDesc {
    FType       type;
    void       *ptr;         // address of the member in s_cfg
    const char *nvs;         // NVS key (every field persists)
    const char *json;        // JSON key, or nullptr for on-device-only fields
    bool        jsonExport;  // emit in export_json (false = privacy-derived instead)
    uint16_t    lo, hi;      // clamp bounds applied on import (0 = unbounded)
};

static const FieldDesc FIELDS[] = {
    { FType::Str,  &s_cfg.wifiSsid,         "wifiSsid", "wifiSsid",         true,  0, 0 },
    { FType::Str,  &s_cfg.wifiPass,         "wifiPass", "wifiPass",         false, 0, 0 },  // imported, never exported
    { FType::Str,  &s_cfg.locationName,     "locName",  "locationName",     true,  0, 0 },
    { FType::F32,  &s_cfg.homeLat,          "lat",      "homeLat",          true,  0, 0 },
    { FType::F32,  &s_cfg.homeLon,          "lon",      "homeLon",          true,  0, 0 },
    { FType::U16,  &s_cfg.radarRangeNm,     "radarNm",  "radarRangeNm",     true,  0, 250 },
    { FType::Str,  &s_cfg.icsUrl,           "icsUrl",   "icsUrl",           true,  0, 0 },
    { FType::Str,  &s_cfg.tickers,          "tickers",  "tickers",          true,  0, 0 },
    { FType::Bool, &s_cfg.useMetric,        "metric",   "useMetric",        true,  0, 0 },
    { FType::Bool, &s_cfg.use24hClock,      "clk24",    "use24hClock",      true,  0, 0 },
    { FType::Str,  &s_cfg.photoUrl,         "photoUrl", "photoUrl",         true,  0, 0 },
    { FType::U16,  &s_cfg.photoSeconds,     "photoSec", "photoSeconds",     true,  10, 0 },
    { FType::Bool, &s_cfg.alertsEnabled,    "alrtOn",   "alertsEnabled",    true,  0, 0 },
    { FType::U8,   &s_cfg.alertMinSeverity, "alrtSev",  "alertMinSeverity", true,  0, 0 },  // reset-to-3 handled below
    { FType::U16,  &s_cfg.alertDismissMin,  "alrtDis",  "alertDismissMin",  true,  0, 1440 },
    { FType::U8,   &s_cfg.brightness,       "bright",   "brightness",       true,  0, 0 },
    { FType::U16,  &s_cfg.pollSeconds,      "poll",     "pollSeconds",      true,  20, 0 },
    { FType::U8,   &s_cfg.tickerTf,         "tickTf",   nullptr,            false, 0, 0 },
    { FType::U8,   &s_cfg.calView,          "calView",  nullptr,            false, 0, 0 },
    { FType::U8,   &s_cfg.lastPanel,        "lastPage", nullptr,            false, 0, 0 },
    { FType::Str,  &s_cfg.configPin,        "pin",      "configPin",        false, 0, 0 },  // imported, exported as pinSet
};

void settings_load() {
    apply_defaults();
    s_prefs.begin(NS, true);   // read-only
    for (const FieldDesc &f : FIELDS) {
        switch (f.type) {
            case FType::Str:  *(String *)f.ptr   = s_prefs.getString(f.nvs, *(String *)f.ptr); break;
            case FType::Bool: *(bool *)f.ptr     = s_prefs.getBool  (f.nvs, *(bool *)f.ptr); break;
            case FType::U8:   *(uint8_t *)f.ptr  = s_prefs.getUChar (f.nvs, *(uint8_t *)f.ptr); break;
            case FType::U16:  *(uint16_t *)f.ptr = s_prefs.getUShort(f.nvs, *(uint16_t *)f.ptr); break;
            case FType::F32:  *(float *)f.ptr    = s_prefs.getFloat (f.nvs, *(float *)f.ptr); break;
        }
    }
    s_prefs.end();
}

void settings_save() {
    s_prefs.begin(NS, false);  // read-write
    for (const FieldDesc &f : FIELDS) {
        switch (f.type) {
            case FType::Str:  s_prefs.putString(f.nvs, *(String *)f.ptr); break;
            case FType::Bool: s_prefs.putBool  (f.nvs, *(bool *)f.ptr); break;
            case FType::U8:   s_prefs.putUChar (f.nvs, *(uint8_t *)f.ptr); break;
            case FType::U16:  s_prefs.putUShort(f.nvs, *(uint16_t *)f.ptr); break;
            case FType::F32:  s_prefs.putFloat (f.nvs, *(float *)f.ptr); break;
        }
    }
    s_prefs.end();
}

void settings_set_ticker_tf(uint8_t idx) {
    if (s_cfg.tickerTf == idx) return;
    s_cfg.tickerTf = idx;
    s_prefs.begin(NS, false);
    s_prefs.putUChar("tickTf", idx);
    s_prefs.end();
}

void settings_set_cal_view(uint8_t view) {
    if (s_cfg.calView == view) return;
    s_cfg.calView = view;
    s_prefs.begin(NS, false);
    s_prefs.putUChar("calView", view);
    s_prefs.end();
}

void settings_set_last_panel(uint8_t panel) {
    if (s_cfg.lastPanel == panel) return;
    s_cfg.lastPanel = panel;
    s_prefs.begin(NS, false);
    s_prefs.putUChar("lastPage", panel);
    s_prefs.end();
}

Settings &settings() { return s_cfg; }

bool settings_has_wifi() { return s_cfg.wifiSsid.length() > 0; }

void settings_clear_wifi() {
    s_cfg.wifiSsid = "";
    s_cfg.wifiPass = "";
    s_prefs.begin(NS, false);
    s_prefs.remove("wifiSsid");
    s_prefs.remove("wifiPass");
    s_prefs.end();
}

bool settings_import_json(const String &json) {
    StaticJsonDocument<1536> doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return false;

    for (const FieldDesc &f : FIELDS) {
        if (!f.json || !doc.containsKey(f.json)) continue;   // on-device fields skip JSON
        switch (f.type) {
            case FType::Str:  *(String *)f.ptr   = doc[f.json].as<String>(); break;
            case FType::Bool: *(bool *)f.ptr     = doc[f.json].as<bool>(); break;
            case FType::U8:   *(uint8_t *)f.ptr  = doc[f.json].as<uint8_t>(); break;
            case FType::U16:  *(uint16_t *)f.ptr = doc[f.json].as<uint16_t>(); break;
            case FType::F32:  *(float *)f.ptr    = doc[f.json].as<float>(); break;
        }
    }

    // Clamp numeric fields to their table bounds (0 = unbounded).
    for (const FieldDesc &f : FIELDS) {
        if (f.type != FType::U16) continue;
        uint16_t &v = *(uint16_t *)f.ptr;
        if (f.lo && v < f.lo) v = f.lo;
        if (f.hi && v > f.hi) v = f.hi;
    }
    // Severity is reset (not clamped) to the default when out of range.
    if (s_cfg.alertMinSeverity < 1 || s_cfg.alertMinSeverity > 4) s_cfg.alertMinSeverity = 3;
    return true;
}

String settings_export_json() {
    StaticJsonDocument<1536> doc;
    for (const FieldDesc &f : FIELDS) {
        if (!f.json || !f.jsonExport) continue;   // skip on-device + privacy fields
        switch (f.type) {
            case FType::Str:  doc[f.json] = *(String *)f.ptr; break;
            case FType::Bool: doc[f.json] = *(bool *)f.ptr; break;
            case FType::U8:   doc[f.json] = *(uint8_t *)f.ptr; break;
            case FType::U16:  doc[f.json] = *(uint16_t *)f.ptr; break;
            case FType::F32:  doc[f.json] = *(float *)f.ptr; break;
        }
    }
    // Derived fields: expose provisioning state, never the secrets themselves.
    doc["wifiConfigured"] = settings_has_wifi();
    doc["pinSet"]         = s_cfg.configPin.length() > 0;
    String out;
    serializeJson(doc, out);
    return out;
}
