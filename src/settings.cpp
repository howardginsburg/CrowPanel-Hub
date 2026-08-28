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
    s_cfg.brightness   = 200;
    s_cfg.pollSeconds  = 60;
    s_cfg.tickerTf     = 0;
    s_cfg.calView      = 0;
    s_cfg.configPin    = "";
}

void settings_load() {
    apply_defaults();
    s_prefs.begin(NS, true);   // read-only
    s_cfg.wifiSsid     = s_prefs.getString("wifiSsid", s_cfg.wifiSsid);
    s_cfg.wifiPass     = s_prefs.getString("wifiPass", s_cfg.wifiPass);
    s_cfg.locationName = s_prefs.getString("locName",  s_cfg.locationName);
    s_cfg.homeLat      = s_prefs.getFloat ("lat",      s_cfg.homeLat);
    s_cfg.homeLon      = s_prefs.getFloat ("lon",      s_cfg.homeLon);
    s_cfg.radarRangeNm = s_prefs.getUShort("radarNm",  s_cfg.radarRangeNm);
    s_cfg.icsUrl       = s_prefs.getString("icsUrl",   s_cfg.icsUrl);
    s_cfg.tickers      = s_prefs.getString("tickers",  s_cfg.tickers);
    s_cfg.useMetric    = s_prefs.getBool  ("metric",   s_cfg.useMetric);
    s_cfg.use24hClock  = s_prefs.getBool  ("clk24",    s_cfg.use24hClock);
    s_cfg.brightness   = s_prefs.getUChar ("bright",   s_cfg.brightness);
    s_cfg.pollSeconds  = s_prefs.getUShort("poll",     s_cfg.pollSeconds);
    s_cfg.tickerTf     = s_prefs.getUChar ("tickTf",   s_cfg.tickerTf);
    s_cfg.calView      = s_prefs.getUChar ("calView",  s_cfg.calView);
    s_cfg.configPin    = s_prefs.getString("pin",      s_cfg.configPin);
    s_prefs.end();
}

void settings_save() {
    s_prefs.begin(NS, false);  // read-write
    s_prefs.putString("wifiSsid", s_cfg.wifiSsid);
    s_prefs.putString("wifiPass", s_cfg.wifiPass);
    s_prefs.putString("locName",  s_cfg.locationName);
    s_prefs.putFloat ("lat",      s_cfg.homeLat);
    s_prefs.putFloat ("lon",      s_cfg.homeLon);
    s_prefs.putUShort("radarNm",  s_cfg.radarRangeNm);
    s_prefs.putString("icsUrl",   s_cfg.icsUrl);
    s_prefs.putString("tickers",  s_cfg.tickers);
    s_prefs.putBool  ("metric",   s_cfg.useMetric);
    s_prefs.putBool  ("clk24",    s_cfg.use24hClock);
    s_prefs.putUChar ("bright",   s_cfg.brightness);
    s_prefs.putUShort("poll",     s_cfg.pollSeconds);
    s_prefs.putUChar ("tickTf",   s_cfg.tickerTf);
    s_prefs.putUChar ("calView",  s_cfg.calView);
    s_prefs.putString("pin",      s_cfg.configPin);
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
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return false;

    if (doc.containsKey("wifiSsid"))     s_cfg.wifiSsid     = doc["wifiSsid"].as<String>();
    if (doc.containsKey("wifiPass"))     s_cfg.wifiPass     = doc["wifiPass"].as<String>();
    if (doc.containsKey("locationName")) s_cfg.locationName = doc["locationName"].as<String>();
    if (doc.containsKey("homeLat"))      s_cfg.homeLat      = doc["homeLat"].as<float>();
    if (doc.containsKey("homeLon"))      s_cfg.homeLon      = doc["homeLon"].as<float>();
    if (doc.containsKey("radarRangeNm")) s_cfg.radarRangeNm = doc["radarRangeNm"].as<uint16_t>();
    if (doc.containsKey("icsUrl"))       s_cfg.icsUrl       = doc["icsUrl"].as<String>();
    if (doc.containsKey("tickers"))      s_cfg.tickers      = doc["tickers"].as<String>();
    if (doc.containsKey("useMetric"))    s_cfg.useMetric    = doc["useMetric"].as<bool>();
    if (doc.containsKey("use24hClock"))  s_cfg.use24hClock  = doc["use24hClock"].as<bool>();
    if (doc.containsKey("brightness"))   s_cfg.brightness   = doc["brightness"].as<uint8_t>();
    if (doc.containsKey("pollSeconds"))  s_cfg.pollSeconds  = doc["pollSeconds"].as<uint16_t>();
    if (doc.containsKey("configPin"))    s_cfg.configPin    = doc["configPin"].as<String>();

    // Clamp to safe ranges.
    if (s_cfg.radarRangeNm > 250) s_cfg.radarRangeNm = 250;
    if (s_cfg.pollSeconds  < 20)  s_cfg.pollSeconds  = 20;
    return true;
}

String settings_export_json() {
    StaticJsonDocument<1024> doc;
    doc["wifiSsid"]      = s_cfg.wifiSsid;
    // Never expose the stored Wi-Fi password to the client.
    doc["wifiConfigured"]= settings_has_wifi();
    doc["locationName"]  = s_cfg.locationName;
    doc["homeLat"]       = s_cfg.homeLat;
    doc["homeLon"]       = s_cfg.homeLon;
    doc["radarRangeNm"]  = s_cfg.radarRangeNm;
    doc["icsUrl"]        = s_cfg.icsUrl;
    doc["tickers"]       = s_cfg.tickers;
    doc["useMetric"]     = s_cfg.useMetric;
    doc["use24hClock"]   = s_cfg.use24hClock;
    doc["brightness"]    = s_cfg.brightness;
    doc["pollSeconds"]   = s_cfg.pollSeconds;
    doc["pinSet"]        = s_cfg.configPin.length() > 0;
    String out;
    serializeJson(doc, out);
    return out;
}
