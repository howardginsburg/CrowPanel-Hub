// settings.cpp
#include "settings.h"
#include <Preferences.h>
#include <ArduinoJson.h>

static Preferences s_prefs;
static Settings    s_cfg;
static const char *NS = "crowpanel";

// ---- Calendar list <-> compact NVS JSON blob (kept out of the flat FIELDS
// table because it's an array). Stored under one "cals" key. -----------------
static uint32_t cal_hex_to_rgb(const String &h) {
    String s = h; s.trim();
    if (s.startsWith("#")) s = s.substring(1);
    return (uint32_t)strtoul(s.c_str(), nullptr, 16) & 0xFFFFFF;
}
static String cal_rgb_to_hex(uint32_t c) {
    char b[8]; snprintf(b, sizeof(b), "#%06lX", (unsigned long)(c & 0xFFFFFF));
    return String(b);
}
static void cals_to_nvs_json(String &out) {
    DynamicJsonDocument doc(6144);
    JsonArray a = doc.to<JsonArray>();
    for (int i = 0; i < s_cfg.calCount; i++) {
        JsonObject o = a.createNestedObject();
        o["n"] = s_cfg.calendars[i].name;
        o["u"] = s_cfg.calendars[i].url;
        o["c"] = s_cfg.calendars[i].color;
        o["v"] = s_cfg.calendars[i].visible;
    }
    serializeJson(doc, out);
}
static void cals_from_nvs_json(const String &s) {
    DynamicJsonDocument doc(6144);
    if (deserializeJson(doc, s) != DeserializationError::Ok) return;
    int n = 0;
    for (JsonObject o : doc.as<JsonArray>()) {
        if (n >= MAX_CALENDARS) break;
        s_cfg.calendars[n].name    = o["n"].as<String>();
        s_cfg.calendars[n].url     = o["u"].as<String>();
        s_cfg.calendars[n].color   = o["c"] | (uint32_t)0x2d6cdf;
        s_cfg.calendars[n].visible = o["v"] | true;
        n++;
    }
    s_cfg.calCount = n;
}

static void apply_defaults() {
    s_cfg.wifiSsid     = "";
    s_cfg.wifiPass     = "";
    s_cfg.locationName = "";
    s_cfg.homeLat      = 47.6062f;   // Seattle placeholder until configured
    s_cfg.homeLon      = -122.3321f;
    s_cfg.radarRangeNm = 25;
    s_cfg.icsUrl       = "";
    s_cfg.calCount     = 0;
    s_cfg.tickers      = "MSFT,AAPL,NVDA,GOOGL,AMZN";
    s_cfg.useMetric    = true;
    s_cfg.use24hClock  = true;
    s_cfg.photoUrl     = "";
    s_cfg.photoSeconds = 60;
    s_cfg.alertsEnabled    = true;
    s_cfg.alertMinSeverity = 3;      // Severe & above
    s_cfg.alertDismissMin  = 10;
    s_cfg.brightness   = 200;
    s_cfg.dimMinutes   = 5;          // dim after 5 min idle
    s_cfg.dimPercent   = 20;         // dim to 20% of normal brightness
    s_cfg.nightDimEnabled = true;
    s_cfg.nightStartHour  = 22;      // 10pm
    s_cfg.nightEndHour    = 7;       // 7am
    s_cfg.nightDimPercent = 10;      // significantly dim overnight
    s_cfg.pollSeconds  = 60;
    s_cfg.theme        = 0;          // Midnight
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
    { FType::Str,  &s_cfg.icsUrl,           "icsUrl",   "icsUrl",           false, 0, 0 },  // legacy: imported/migrated, no longer exported
    { FType::Str,  &s_cfg.tickers,          "tickers",  "tickers",          true,  0, 0 },
    { FType::Bool, &s_cfg.useMetric,        "metric",   "useMetric",        true,  0, 0 },
    { FType::Bool, &s_cfg.use24hClock,      "clk24",    "use24hClock",      true,  0, 0 },
    { FType::Str,  &s_cfg.photoUrl,         "photoUrl", "photoUrl",         true,  0, 0 },
    { FType::U16,  &s_cfg.photoSeconds,     "photoSec", "photoSeconds",     true,  10, 0 },
    { FType::Bool, &s_cfg.alertsEnabled,    "alrtOn",   "alertsEnabled",    true,  0, 0 },
    { FType::U8,   &s_cfg.alertMinSeverity, "alrtSev",  "alertMinSeverity", true,  0, 0 },  // reset-to-3 handled below
    { FType::U16,  &s_cfg.alertDismissMin,  "alrtDis",  "alertDismissMin",  true,  0, 1440 },
    { FType::U8,   &s_cfg.brightness,       "bright",   "brightness",       true,  0, 0 },
    { FType::U16,  &s_cfg.dimMinutes,       "dimMin",   "dimMinutes",       true,  0, 1440 },
    { FType::U8,   &s_cfg.dimPercent,       "dimPct",   "dimPercent",       true,  0, 0 },  // clamp to 100 below
    { FType::Bool, &s_cfg.nightDimEnabled,  "nightOn",  "nightDimEnabled",  true,  0, 0 },
    { FType::U8,   &s_cfg.nightStartHour,   "nightSt",  "nightStartHour",   true,  0, 0 },  // clamp to 0..23 below
    { FType::U8,   &s_cfg.nightEndHour,     "nightEnd", "nightEndHour",     true,  0, 0 },  // clamp to 0..23 below
    { FType::U8,   &s_cfg.nightDimPercent,  "nightPct", "nightDimPercent",  true,  0, 0 },  // clamp to 100 below
    { FType::U16,  &s_cfg.pollSeconds,      "poll",     "pollSeconds",      true,  20, 0 },
    { FType::U8,   &s_cfg.theme,            "theme",    "theme",            true,  0, 3 },  // 0..THEME_COUNT-1 (UiThemeId)
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
    String cals = s_prefs.getString("cals", "");
    s_prefs.end();

    if (cals.length()) cals_from_nvs_json(cals);
    // Migrate a legacy single URL into the first calendar slot, then persist so
    // it survives even before the user next saves from the portal.
    if (s_cfg.calCount == 0 && s_cfg.icsUrl.length()) {
        s_cfg.calendars[0].name    = "Calendar";
        s_cfg.calendars[0].url     = s_cfg.icsUrl;
        s_cfg.calendars[0].color   = 0x2d6cdf;
        s_cfg.calendars[0].visible = true;
        s_cfg.calCount = 1;
        String out; cals_to_nvs_json(out);
        s_prefs.begin(NS, false); s_prefs.putString("cals", out); s_prefs.end();
    }
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
    String cals; cals_to_nvs_json(cals);
    s_prefs.putString("cals", cals);
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

void settings_set_cal_visible(uint8_t idx, bool on) {
    if (idx >= s_cfg.calCount) return;
    if (s_cfg.calendars[idx].visible == on) return;
    s_cfg.calendars[idx].visible = on;
    String out; cals_to_nvs_json(out);
    s_prefs.begin(NS, false);
    s_prefs.putString("cals", out);
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
    DynamicJsonDocument doc(6144);
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

    // Calendar list: replace wholesale, but keep each slot's on-device
    // visibility by position (the portal doesn't manage visibility).
    if (doc.containsKey("calendars")) {
        bool prevVis[MAX_CALENDARS];
        for (int i = 0; i < MAX_CALENDARS; i++)
            prevVis[i] = (i < s_cfg.calCount) ? s_cfg.calendars[i].visible : true;
        int n = 0;
        for (JsonObject o : doc["calendars"].as<JsonArray>()) {
            if (n >= MAX_CALENDARS) break;
            String name = o["name"].as<String>(); name.trim();
            String url  = o["url"].as<String>();  url.trim();
            if (!name.length() && !url.length()) continue;   // skip blank rows
            if (name.length() > 40)  name = name.substring(0, 40);
            if (url.length()  > 400) url  = url.substring(0, 400);
            String col = o["color"].as<String>(); if (!col.length()) col = "#2d6cdf";
            s_cfg.calendars[n].name    = name;
            s_cfg.calendars[n].url     = url;
            s_cfg.calendars[n].color   = cal_hex_to_rgb(col);
            s_cfg.calendars[n].visible = prevVis[n];
            n++;
        }
        s_cfg.calCount = n;
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
    // dimPercent is a U8 (not covered by the U16 clamp loop); bound it explicitly.
    if (s_cfg.dimPercent > 100) s_cfg.dimPercent = 100;
    if (s_cfg.nightDimPercent > 100) s_cfg.nightDimPercent = 100;
    if (s_cfg.nightStartHour > 23) s_cfg.nightStartHour = 0;
    if (s_cfg.nightEndHour   > 23) s_cfg.nightEndHour   = 0;
    return true;
}

String settings_export_json() {
    DynamicJsonDocument doc(6144);
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
    // Calendar list (name/url/color; visibility is on-device only).
    JsonArray cals = doc.createNestedArray("calendars");
    for (int i = 0; i < s_cfg.calCount; i++) {
        JsonObject o = cals.createNestedObject();
        o["name"]  = s_cfg.calendars[i].name;
        o["url"]   = s_cfg.calendars[i].url;
        o["color"] = cal_rgb_to_hex(s_cfg.calendars[i].color);
    }
    // Derived fields: expose provisioning state, never the secrets themselves.
    doc["wifiConfigured"] = settings_has_wifi();
    doc["pinSet"]         = s_cfg.configPin.length() > 0;
    String out;
    serializeJson(doc, out);
    return out;
}
