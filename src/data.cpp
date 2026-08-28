// data.cpp — Open-Meteo weather + adsb.fi flights, all keyless HTTPS.
#include "data.h"
#include "ui.h"
#include "settings.h"
#include "net_wifi.h"
#include "board_pins.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

static bool     s_timeStarted = false;
static bool     s_flightsDirty = false;   // one-off flights refresh requested
static bool     s_tickersDirty = false;   // one-off tickers refresh requested
static long     s_utcOffset = 0x7fffffff;   // sentinel: no local offset applied yet

void data_request_flights() { s_flightsDirty = true; }
void data_request_tickers() { s_tickersDirty = true; }

// -------------------------------------------------------------------- time ---
void data_begin_time() {
    // Seed with UTC via NTP; the local offset is applied once the first weather
    // poll returns utc_offset_seconds for the configured location.
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    s_timeStarted = true;
}

// Apply the location's UTC offset (seconds east of UTC, DST-inclusive) to the
// clock. Re-applying on each poll self-corrects daylight-saving transitions.
static void apply_utc_offset(long offsetSec) {
    if (offsetSec == s_utcOffset) return;
    s_utcOffset = offsetSec;
    configTime(offsetSec, 0, "pool.ntp.org", "time.nist.gov");
}

// ------------------------------------------------------------------ helpers ---
static bool http_get_json(const String &url, JsonDocument &doc,
                          const JsonDocument *filter = nullptr,
                          const char *userAgent = nullptr) {
    WiFiClientSecure client;
    client.setInsecure();               // skip cert validation (keyless public APIs)
    client.setHandshakeTimeout(8);      // cap TLS stalls; the 120s default froze the UI
    HTTPClient https;
    https.setConnectTimeout(8000);      // bound the TCP connect too
    https.setTimeout(8000);
    if (userAgent) https.setUserAgent(userAgent);   // some APIs (Yahoo) 403 without one
    if (!https.begin(client, url)) { Serial.println("[data] https.begin() failed"); return false; }

    int code = https.GET();
    if (code != HTTP_CODE_OK) {
        String body = https.getString();
        Serial.printf("[data] HTTP %d  heap=%u  %.80s\n       body: %.180s\n",
                      code, ESP.getFreeHeap(), url.c_str(), body.c_str());
        https.end();
        return false;
    }

    DeserializationError err = filter
        ? deserializeJson(doc, https.getString(), DeserializationOption::Filter(*filter))
        : deserializeJson(doc, https.getString());
    https.end();
    if (err) Serial.printf("[data] JSON error: %s  heap=%u\n", err.c_str(), ESP.getFreeHeap());
    return err == DeserializationError::Ok;
}

static const char *weather_summary(int code) {
    if (code == 0) return "Clear sky";
    if (code <= 2) return "Partly cloudy";
    if (code == 3) return "Overcast";
    if (code <= 48) return "Fog";
    if (code <= 57) return "Drizzle";
    if (code <= 67) return "Rain";
    if (code <= 77) return "Snow";
    if (code <= 82) return "Rain showers";
    if (code <= 86) return "Snow showers";
    return "Thunderstorm";
}

static double deg2rad(double d) { return d * M_PI / 180.0; }

// Great-circle distance (nautical miles) + initial bearing (degrees).
static void geo(double lat1, double lon1, double lat2, double lon2,
                int &distNm, int &bearing) {
    const double R_NM = 3440.065;
    double dlat = deg2rad(lat2 - lat1);
    double dlon = deg2rad(lon2 - lon1);
    double a = sin(dlat / 2) * sin(dlat / 2) +
               cos(deg2rad(lat1)) * cos(deg2rad(lat2)) * sin(dlon / 2) * sin(dlon / 2);
    distNm = (int)lround(R_NM * 2 * atan2(sqrt(a), sqrt(1 - a)));

    double y = sin(dlon) * cos(deg2rad(lat2));
    double x = cos(deg2rad(lat1)) * sin(deg2rad(lat2)) -
               sin(deg2rad(lat1)) * cos(deg2rad(lat2)) * cos(dlon);
    int brg = (int)lround(atan2(y, x) * 180.0 / M_PI);
    bearing = (brg + 360) % 360;
}

// Minutes since local midnight from an ISO "YYYY-MM-DDThh:mm" string (-1 if absent).
static int iso_hm_to_min(const char *iso) {
    if (!iso) return -1;
    const char *t = strchr(iso, 'T');
    if (!t) return -1;
    int h = 0, m = 0;
    if (sscanf(t + 1, "%d:%d", &h, &m) != 2) return -1;
    return h * 60 + m;
}

// Moon phase from a reference new moon (2000-01-06 18:14 UTC) and the synodic month.
// idx: 0=new,1=wax cres,2=first qtr,3=wax gib,4=full,5=wan gib,6=last qtr,7=wan cres.
static void moon_phase(time_t now, int &idx, int &illumPct) {
    const double SYN = 29.530588853;
    double frac = fmod((now - 947182440.0) / 86400.0 / SYN, 1.0);
    if (frac < 0) frac += 1.0;
    idx = ((int)floor(frac * 8.0 + 0.5)) & 7;
    illumPct = (int)lround(50.0 * (1.0 - cos(2.0 * M_PI * frac)));
}

// ------------------------------------------------------------------ weather ---
static void poll_weather() {
    char url[512];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,relative_humidity_2m,wind_speed_10m,weather_code"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,uv_index_max"
        "&hourly=temperature_2m,precipitation_probability"
        "&forecast_days=5&forecast_hours=12&timezone=auto",
        settings().homeLat, settings().homeLon);
    Serial.printf("[weather] poll lat=%.4f lon=%.4f\n", settings().homeLat, settings().homeLon);

    // Filter to just the timezone offset (which drives the clock), the
    // current-conditions block, and the daily forecast. Without a filter the
    // full response — with its units/timezone strings — can overflow the buffer
    // and silently drop utc_offset_seconds, leaving the clock stuck on UTC.
    StaticJsonDocument<320> filter;
    filter["utc_offset_seconds"] = true;
    filter["current"]            = true;
    filter["daily"]              = true;
    filter["hourly"]             = true;

    // Enlarged response (daily + 12-hour hourly). Kept static/off-stack so the
    // ~6 KB document doesn't crowd the loop task's stack during the TLS handshake.
    static StaticJsonDocument<6144> doc;
    doc.clear();
    if (!http_get_json(url, doc, &filter)) { ui_weather_error("Weather unavailable"); return; }

    // Sync the clock to the location's local time (DST-inclusive).
    if (doc.containsKey("utc_offset_seconds"))
        apply_utc_offset(doc["utc_offset_seconds"].as<long>());

    JsonObject cur = doc["current"];
    if (cur.isNull()) {
        Serial.print("[weather] parse error — no 'current'. filtered doc=");
        serializeJson(doc, Serial);
        Serial.printf("\n          url=%s\n", url);
        ui_weather_error("Weather parse error");
        return;
    }
    float t   = cur["temperature_2m"] | 0.0f;
    int   h   = cur["relative_humidity_2m"] | 0;
    float w   = cur["wind_speed_10m"] | 0.0f;
    int   code= cur["weather_code"] | 0;
    Serial.printf("[weather] OK  %.1fC  hum %d%%  off=%ld\n", t, h,
                  doc["utc_offset_seconds"].as<long>());
    ui_weather_set(code, weather_summary(code), t, h, w);

    // 5-day forecast strip.
    JsonObject daily = doc["daily"];
    if (!daily.isNull()) {
        JsonArray dc = daily["weather_code"];
        JsonArray dh = daily["temperature_2m_max"];
        JsonArray dl = daily["temperature_2m_min"];
        DayForecast days[UI_FORECAST_DAYS];
        int n = 0;
        for (int i = 0; i < UI_FORECAST_DAYS && i < (int)dc.size(); i++) {
            days[n].code = dc[i] | 0;
            days[n].hiC  = dh[i] | 0.0f;
            days[n].loC  = dl[i] | 0.0f;
            n++;
        }
        if (n) ui_forecast_set(days, n);

        // Sunrise/sunset (local wall-clock strings), today's max UV, moon phase.
        int   srMin = iso_hm_to_min(daily["sunrise"][0] | (const char *)nullptr);
        int   ssMin = iso_hm_to_min(daily["sunset"][0]  | (const char *)nullptr);
        float uvMax = daily["uv_index_max"][0] | -1.0f;
        int   moonIdx, moonPct; moon_phase(time(nullptr), moonIdx, moonPct);
        ui_sun_set(srMin, ssMin, moonIdx, moonPct);
        if (uvMax >= 0) ui_air_uv_set(uvMax);
        Serial.printf("[weather] sun %d..%d min  uvMax=%.1f  moon=%d/%d%%\n",
                      srMin, ssMin, uvMax, moonIdx, moonPct);
    }

    // Next 12 hours (forecast_hours=12 anchors at the current hour).
    JsonObject hourly = doc["hourly"];
    if (!hourly.isNull()) {
        JsonArray ht = hourly["time"];
        JsonArray hT = hourly["temperature_2m"];
        JsonArray hP = hourly["precipitation_probability"];
        HourCell hc[UI_HOURLY_N];
        int hn = 0;
        for (int i = 0; i < UI_HOURLY_N && i < (int)ht.size(); i++) {
            int mn = iso_hm_to_min(ht[i] | (const char *)nullptr);
            hc[hn].hour      = mn >= 0 ? mn / 60 : 0;
            hc[hn].tempC     = hT[i] | 0.0f;
            hc[hn].precipPct = hP[i] | 0;
            hn++;
        }
        if (hn) ui_hourly_set(hc, hn);
    }
}

// Air quality for the same location as weather (Open-Meteo, keyless).
static void poll_air() {
    char url[400];
    snprintf(url, sizeof(url),
        "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f"
        "&current=us_aqi,pm2_5,pm10,ozone,nitrogen_dioxide",
        settings().homeLat, settings().homeLon);
    Serial.printf("[air] poll lat=%.4f lon=%.4f\n", settings().homeLat, settings().homeLon);

    StaticJsonDocument<256> filter;
    filter["current"] = true;

    StaticJsonDocument<1024> doc;
    if (!http_get_json(url, doc, &filter)) { ui_air_error("Air quality unavailable"); return; }

    JsonObject cur = doc["current"];
    if (cur.isNull()) { ui_air_error("Air quality parse error"); return; }
    int   aqi  = cur["us_aqi"] | 0;
    float pm25 = cur["pm2_5"] | 0.0f;
    float pm10 = cur["pm10"] | 0.0f;
    float o3   = cur["ozone"] | 0.0f;
    float no2  = cur["nitrogen_dioxide"] | 0.0f;
    Serial.printf("[air] OK  us_aqi=%d pm25=%.1f pm10=%.1f o3=%.1f no2=%.1f\n",
                  aqi, pm25, pm10, o3, no2);
    ui_air_set(aqi, pm25, pm10, o3, no2);
}

// ------------------------------------------------------------------ flights ---
static const int MAX_FLIGHTS = 15;

static void poll_flights() {
    int rangeNm = ui_radar_range_nm();
    char url[160];
    snprintf(url, sizeof(url),
        "https://opendata.adsb.fi/api/v3/lat/%.4f/lon/%.4f/dist/%d",
        settings().homeLat, settings().homeLon, rangeNm);
    Serial.printf("[flights] poll lat=%.4f lon=%.4f radar=%dNM\n",
                  settings().homeLat, settings().homeLon, rangeNm);

    // Keep only the fields we render.
    StaticJsonDocument<256> filter;
    JsonObject ac = filter["ac"].createNestedObject();
    ac["r"] = true; ac["flight"] = true; ac["t"] = true;
    ac["alt_baro"] = true; ac["lat"] = true; ac["lon"] = true; ac["track"] = true;
    ac["gs"] = true; ac["baro_rate"] = true; ac["squawk"] = true;

    DynamicJsonDocument doc(24576);
    if (!http_get_json(url, doc, &filter)) { ui_flights_error("Flights unavailable"); return; }

    JsonArray arr = doc["ac"];
    if (arr.isNull()) { Serial.println("[flights] no 'ac' array in response"); ui_flights_error("No aircraft data"); return; }

    FlightRow rows[MAX_FLIGHTS];
    int count = 0;
    for (JsonObject a : arr) {
        if (count >= MAX_FLIGHTS) break;
        if (!a.containsKey("lat") || !a.containsKey("lon")) continue;

        FlightRow r;
        const char *tail = a["r"] | "";
        const char *call = a["flight"] | "";
        r.tail = strlen(tail) ? String(tail) : (strlen(call) ? String(call) : String("(n/a)"));
        r.tail.trim();
        r.callsign = String(call); r.callsign.trim();
        r.type = String((const char *)(a["t"] | "-"));

        // alt_baro may be the string "ground".
        if (a["alt_baro"].is<const char *>()) r.altFt = 0;
        else r.altFt = a["alt_baro"] | 0;

        geo(settings().homeLat, settings().homeLon,
            a["lat"].as<double>(), a["lon"].as<double>(), r.distNm, r.bearing);
        r.track  = a.containsKey("track") ? (int)(a["track"].as<double>() + 0.5) : -1;
        r.gs     = a.containsKey("gs") ? (int)lround(a["gs"].as<double>()) : -1;
        r.vrate  = a.containsKey("baro_rate") ? (int)a["baro_rate"].as<long>() : -99999;
        r.squawk = String((const char *)(a["squawk"] | ""));
        rows[count++] = r;
    }

    // Simple insertion sort by distance (small N).
    for (int i = 1; i < count; i++) {
        FlightRow key = rows[i]; int j = i - 1;
        while (j >= 0 && rows[j].distNm > key.distNm) { rows[j + 1] = rows[j]; j--; }
        rows[j + 1] = key;
    }
    Serial.printf("[flights] %d aircraft rendered\n", count);
    ui_flights_set(rows, count);
}

// ------------------------------------------------------------------ tickers ---
static const int MAX_TICKERS = 8;

// Yahoo chart range/interval presets, indexed by ui_ticker_tf_index() (0..4).
struct TfPreset { const char *range; const char *interval; };
static const TfPreset TF[] = {
    {"1d",  "5m"},   // 1D
    {"5d",  "30m"},  // 5D
    {"1mo", "1d"},   // 1M
    {"6mo", "1d"},   // 6M
    {"1y",  "1wk"},  // 1Y
};
static const int TF_COUNT = sizeof(TF) / sizeof(TF[0]);

static void poll_tickers() {
    // Parse the comma-separated stock symbol list (e.g. "MSFT,AAPL,NVDA").
    TickerRow rows[MAX_TICKERS];
    int n = 0;
    String csv = settings().tickers;
    int start = 0;
    while (n < MAX_TICKERS && start <= (int)csv.length()) {
        int comma = csv.indexOf(',', start);
        String tok = (comma < 0) ? csv.substring(start) : csv.substring(start, comma);
        tok.trim(); tok.toUpperCase();
        if (tok.length()) {
            rows[n].symbol   = tok;
            rows[n].name     = "";
            rows[n].price    = 0.0;
            rows[n].changeAbs= 0.0;
            rows[n].changePct= 0.0;
            rows[n].winLo    = 0.0;
            rows[n].winHi    = 0.0;
            rows[n].live     = false;
            rows[n].valid    = false;
            rows[n].sparkLen = 0;
            n++;
        }
        if (comma < 0) break;
        start = comma + 1;
    }
    if (n == 0) { ui_tickers_error("No stocks configured"); return; }

    int tf = ui_ticker_tf_index();
    if (tf < 0 || tf >= TF_COUNT) tf = 0;
    time_t now = time(nullptr);

    // One keyless Yahoo Finance chart request per symbol.
    for (int i = 0; i < n; i++) {
        String url = "https://query1.finance.yahoo.com/v8/finance/chart/" +
                     rows[i].symbol + "?interval=" + TF[tf].interval +
                     "&range=" + TF[tf].range;

        StaticJsonDocument<512> filter;
        JsonObject r = filter["chart"]["result"][0].to<JsonObject>();
        r["indicators"]["quote"][0]["close"] = true;
        JsonObject m = r["meta"].to<JsonObject>();
        m["regularMarketPrice"] = true;
        m["chartPreviousClose"] = true;
        m["shortName"]          = true;
        m["currentTradingPeriod"]["regular"]["start"] = true;
        m["currentTradingPeriod"]["regular"]["end"]   = true;

        DynamicJsonDocument doc(12288);
        if (!http_get_json(url, doc, &filter, "Mozilla/5.0")) { delay(300); continue; }
        JsonObject meta = doc["chart"]["result"][0]["meta"];
        if (meta.isNull()) { delay(300); continue; }

        double price = meta["regularMarketPrice"] | 0.0;
        double prev  = meta["chartPreviousClose"] | 0.0;
        rows[i].price = price;
        rows[i].name  = (const char *)(meta["shortName"] | "");

        long rs = meta["currentTradingPeriod"]["regular"]["start"] | 0L;
        long re = meta["currentTradingPeriod"]["regular"]["end"]   | 0L;
        rows[i].live = (rs > 0 && re > 0 && now >= rs && now <= re);

        // Downsample the close[] series into the sparkline, skipping null gaps.
        JsonArray close = doc["chart"]["result"][0]["indicators"]["quote"][0]["close"];
        double lo = 1e18, hi = -1e18, first = 0.0, last = 0.0;
        bool haveFirst = false;
        int len = close.isNull() ? 0 : close.size();
        int k = 0;
        for (int slot = 0; slot < SPARK_N && len > 0; slot++) {
            // Map each output slot to a source index across the whole window.
            int idx = (len <= SPARK_N)
                      ? (slot < len ? slot : -1)
                      : (int)((int64_t)slot * (len - 1) / (SPARK_N - 1));
            if (idx < 0) break;
            JsonVariant v = close[idx];
            if (v.isNull()) continue;
            double c = v.as<double>();
            rows[i].spark[k++] = (float)c;
            if (c < lo) lo = c;
            if (c > hi) hi = c;
            if (!haveFirst) { first = c; haveFirst = true; }
            last = c;
        }
        rows[i].sparkLen = k;
        if (haveFirst) {
            if (price <= 0.0) price = last, rows[i].price = last;  // fallback if meta blank
            if (price < lo) lo = price;
            if (price > hi) hi = price;
            rows[i].winLo = lo;
            rows[i].winHi = hi;
            // Trend baseline: previous close for 1D, else the window's first point.
            double base = (tf == 0 && prev > 0.0) ? prev : first;
            rows[i].changeAbs = price - base;
            rows[i].changePct = (base > 0.0) ? (price - base) / base * 100.0 : 0.0;
            rows[i].valid = true;
        }
        delay(300);
    }

    ui_tickers_set(rows, n);
}

// ----------------------------------------------------------------- calendar ---
static const int  CAL_MAX = 96;
static const int  CAL_HORIZON_DAYS = 21;
static String     s_lastCalUrl = "\x01";   // sentinel: forces the first fetch

// Days-from-civil (Howard Hinnant) -> UTC epoch seconds for a Y/M/D H:M:S.
static long ymd_to_epoch(int y, int mo, int d, int h, int mi, int s) {
    y -= mo <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + (long)doe - 719468;
    return days * 86400L + h * 3600L + mi * 60L + s;
}

// Parse an ICS DATE / DATE-TIME value (after the ':') into a UTC epoch. Floating
// and TZID-local times are shifted by the device offset (no VTIMEZONE support).
static long parse_ics_time(const String &v, bool &allDay) {
    allDay = false;
    if (v.length() == 8 && v.indexOf('T') < 0) {
        int y = v.substring(0, 4).toInt(), mo = v.substring(4, 6).toInt(),
            d = v.substring(6, 8).toInt();
        allDay = true;
        long off = (s_utcOffset == 0x7fffffff) ? 0 : s_utcOffset;
        return ymd_to_epoch(y, mo, d, 0, 0, 0) - off;
    }
    int t = v.indexOf('T');
    if (t < 0 || v.length() < (unsigned)t + 7) return 0;
    int y = v.substring(0, 4).toInt(), mo = v.substring(4, 6).toInt(),
        d = v.substring(6, 8).toInt();
    int h = v.substring(t + 1, t + 3).toInt(), mi = v.substring(t + 3, t + 5).toInt(),
        s = v.substring(t + 5, t + 7).toInt();
    long e = ymd_to_epoch(y, mo, d, h, mi, s);
    if (!v.endsWith("Z")) {
        long off = (s_utcOffset == 0x7fffffff) ? 0 : s_utcOffset;
        e -= off;                       // floating/local wall time -> UTC
    }
    return e;
}

static String ics_unescape(String s) {
    s.replace("\\n", " ");
    s.replace("\\N", " ");
    s.replace("\\,", ",");
    s.replace("\\;", ";");
    s.replace("\\\\", "\\");
    s.trim();
    return s;
}

struct Rrule { char freq; int interval; long until; int count; uint8_t byday; bool has; };

static void parse_rrule(const String &s, Rrule &rr) {
    rr = {0, 1, 0, 0, 0, false};
    if (!s.length()) return;
    int start = 0;
    while (start < (int)s.length()) {
        int sc = s.indexOf(';', start);
        String tok = (sc < 0) ? s.substring(start) : s.substring(start, sc);
        int eq = tok.indexOf('=');
        if (eq > 0) {
            String k = tok.substring(0, eq); k.toUpperCase();
            String v = tok.substring(eq + 1);
            if (k == "FREQ") {
                v.toUpperCase();
                rr.freq = v.startsWith("DAILY") ? 'D' : v.startsWith("WEEKLY") ? 'W'
                        : v.startsWith("MONTHLY") ? 'M' : v.startsWith("YEARLY") ? 'Y' : 0;
            } else if (k == "INTERVAL") rr.interval = v.toInt();
            else if (k == "COUNT") rr.count = v.toInt();
            else if (k == "UNTIL") { bool ad; rr.until = parse_ics_time(v, ad); }
            else if (k == "BYDAY") {
                v.toUpperCase();
                const char *dn[7] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
                for (int d = 0; d < 7; d++) if (v.indexOf(dn[d]) >= 0) rr.byday |= (1 << d);
            }
        }
        if (sc < 0) break;
        start = sc + 1;
    }
    if (rr.freq == 0) rr.has = false; else rr.has = true;
    if (rr.interval <= 0) rr.interval = 1;
}

// Recurring masters are buffered and expanded after the full feed is read, so
// RECURRENCE-ID overrides / EXDATE cancellations (which may appear after the
// master in the file) can suppress the right occurrence.
struct RawMaster { String uid; String title; long dtstart; bool allDay; Rrule rr; };
struct Suppress  { String uid; long epoch; };
static const int MAST_MAX = 160;
static const int SUPP_MAX = 256;
static RawMaster s_mast[MAST_MAX]; static int s_mastN = 0;
static Suppress  s_supp[SUPP_MAX]; static int s_suppN = 0;

static bool cal_suppressed(const String &uid, long epoch) {
    for (int i = 0; i < s_suppN; i++)
        if (s_supp[i].epoch == epoch && s_supp[i].uid == uid) return true;
    return false;
}

static void cal_add(CalEvent *ev, int &n, long start, bool allDay,
                    const String &title, long lo, long hi) {
    if (start < lo || start > hi || n >= CAL_MAX) return;
    ev[n].start = start; ev[n].allDay = allDay; ev[n].title = title; n++;
}

// Expand a recurring master into occurrences within [lo, hi], skipping any
// occurrence cancelled by EXDATE or replaced by a RECURRENCE-ID override.
static void cal_expand(CalEvent *ev, int &n, const RawMaster &m, long lo, long hi) {
    long ds = m.dtstart; bool allDay = m.allDay;
    const String &title = m.title; const Rrule &rr = m.rr;
    if (!rr.has) {
        if (!cal_suppressed(m.uid, ds)) cal_add(ev, n, ds, allDay, title, lo, hi);
        return;
    }
    int made = 0, guard = 0;
    if (rr.freq == 'D') {
        for (long occ = ds; occ <= hi && guard < 800; occ += (long)rr.interval * 86400, guard++) {
            if (rr.count > 0 && made >= rr.count) break;
            if (rr.until > 0 && occ > rr.until) break;
            if (!cal_suppressed(m.uid, occ)) cal_add(ev, n, occ, allDay, title, lo, hi);
            made++;
        }
    } else if (rr.freq == 'W') {
        uint8_t days = rr.byday;
        long dsDay = ds / 86400;
        int dsWd = (int)(((dsDay % 7) + 4) % 7);
        if (days == 0) days = (uint8_t)(1 << dsWd);
        long sunday0 = (dsDay - dsWd) * 86400;
        long tod = ds - dsDay * 86400;
        for (int w = 0; guard < 800; w++, guard++) {
            long weekSun = sunday0 + (long)w * rr.interval * 7 * 86400;
            if (weekSun > hi) break;
            if (rr.count > 0 && made >= rr.count) break;
            for (int wd = 0; wd < 7; wd++) {
                if (!(days & (1 << wd))) continue;
                long occ = weekSun + (long)wd * 86400 + tod;
                if (occ < ds) continue;
                if (rr.until > 0 && occ > rr.until) return;
                if (rr.count > 0 && made >= rr.count) return;
                if (!cal_suppressed(m.uid, occ)) cal_add(ev, n, occ, allDay, title, lo, hi);
                made++;
            }
        }
    } else if (rr.freq == 'M') {
        struct tm tmv; time_t tt = (time_t)ds; gmtime_r(&tt, &tmv);
        int y = tmv.tm_year + 1900, mo = tmv.tm_mon + 1, d = tmv.tm_mday,
            h = tmv.tm_hour, mi = tmv.tm_min, s = tmv.tm_sec;
        while (guard++ < 240) {
            long occ = ymd_to_epoch(y, mo, d, h, mi, s);
            if (occ > hi) break;
            if (rr.count > 0 && made >= rr.count) break;
            if (rr.until > 0 && occ > rr.until) break;
            if (occ >= ds) { if (!cal_suppressed(m.uid, occ)) cal_add(ev, n, occ, allDay, title, lo, hi); made++; }
            mo += rr.interval; while (mo > 12) { mo -= 12; y++; }
        }
    } else if (rr.freq == 'Y') {
        struct tm tmv; time_t tt = (time_t)ds; gmtime_r(&tt, &tmv);
        int y = tmv.tm_year + 1900, mo = tmv.tm_mon + 1, d = tmv.tm_mday,
            h = tmv.tm_hour, mi = tmv.tm_min, s = tmv.tm_sec;
        while (guard++ < 60) {
            long occ = ymd_to_epoch(y, mo, d, h, mi, s);
            if (occ > hi) break;
            if (rr.count > 0 && made >= rr.count) break;
            if (rr.until > 0 && occ > rr.until) break;
            if (occ >= lo) { if (!cal_suppressed(m.uid, occ)) cal_add(ev, n, occ, allDay, title, lo, hi); made++; }
            y += rr.interval;
        }
    }
}

static void poll_calendar() {
    String url = settings().icsUrl; url.trim();
    if (!url.length()) { ui_calendar_error("No calendar URL configured"); return; }
    if (url.startsWith("webcal://")) url = "https://" + url.substring(9);

    long now = time(nullptr);
    if (now < 1600000000L) { ui_calendar_error("Waiting for clock sync..."); return; }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    https.setTimeout(15000);
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    https.setUserAgent("Mozilla/5.0");
    if (!https.begin(client, url)) { ui_calendar_error("Calendar: connect failed"); return; }
    https.addHeader("Accept-Encoding", "identity");   // never gzip — we can't inflate
    const char *hdrKeys[] = {"Content-Encoding", "Content-Type", "Transfer-Encoding"};
    https.collectHeaders(hdrKeys, 3);
    int code = https.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[cal] HTTP %d  %.90s\n", code, url.c_str());
        char m[40]; snprintf(m, sizeof(m), "Calendar HTTP %d", code);
        ui_calendar_error(m); https.end(); return;
    }
    Serial.printf("[cal] CT=%s  CE=%s  TE=%s  len=%d\n",
                  https.header("Content-Type").c_str(),
                  https.header("Content-Encoding").c_str(),
                  https.header("Transfer-Encoding").c_str(),
                  https.getSize());
    { String ct = https.header("Content-Type"); ct.toLowerCase();
      if (ct.indexOf("html") >= 0) {
          ui_calendar_error("URL is a web page, not an .ics feed - use the private iCal address");
          https.end(); return;
      } }
    WiFiClient *st = https.getStreamPtr();
    int  bodyLen  = https.getSize();                // -1 => chunked / unknown
    bool chunked  = (bodyLen < 0);
    long chunkRem = chunked ? 0 : bodyLen;          // bytes left in current chunk
    bool bodyDone = false;

    // Off the stack: this array + a TLS handshake would blow the loop stack.
    static CalEvent cand[CAL_MAX]; int nc = 0;
    long off = (s_utcOffset == 0x7fffffff) ? 0 : s_utcOffset;
    long lo = ((now + off) / 86400) * 86400 - off;   // start of today, local
    long hi = now + (long)CAL_HORIZON_DAYS * 86400;

    uint32_t started = millis();
    size_t   totalBytes = 0;
    int      vevents = 0;
    String   preview;

    // Blocking single-byte read with a short idle timeout.
    auto rawByte = [&](int &c) -> bool {
        uint32_t t0 = millis();
        while (st->available() == 0) {
            if (!st->connected() && st->available() == 0) return false;
            if (millis() - t0 > 4000) return false;
            delay(1);
        }
        c = st->read();
        return c >= 0;
    };
    // Read a CRLF/LF-terminated line directly from the raw socket (chunk headers).
    auto rawLine = [&](String &out) -> bool {
        out = ""; int c; bool got = false;
        while (rawByte(c)) { got = true;
            if (c == '\n') break;
            if (c != '\r' && out.length() < 128) out += (char)c;
        }
        return got;
    };
    // One decoded body byte, transparently de-chunking when needed.
    auto contentByte = [&](int &c) -> bool {
        if (!chunked) {
            if (chunkRem <= 0) return false;
            if (!rawByte(c)) return false;
            chunkRem--; totalBytes++;
            if (preview.length() < 120) preview += (char)((c >= 32 && c < 127) ? c : '.');
            return true;
        }
        if (bodyDone) return false;
        if (chunkRem <= 0) {
            String hdr;
            do { if (!rawLine(hdr)) { bodyDone = true; return false; } hdr.trim(); }
            while (hdr.length() == 0);
            int semi = hdr.indexOf(';'); if (semi >= 0) hdr = hdr.substring(0, semi);
            long sz = strtol(hdr.c_str(), nullptr, 16);
            if (sz <= 0) { bodyDone = true; return false; }   // last chunk
            chunkRem = sz;
        }
        if (!rawByte(c)) return false;
        chunkRem--; totalBytes++;
        if (preview.length() < 120) preview += (char)((c >= 32 && c < 127) ? c : '.');
        if (chunkRem == 0) { int t; rawByte(t); if (t == '\r') rawByte(t); }  // trailing CRLF
        return true;
    };
    // One physical (unfolded) body line.
    auto physLine = [&](String &out) -> bool {
        out = ""; int c; bool got = false;
        while (contentByte(c)) { got = true;
            if (c == '\n') break;
            if (c != '\r') { out += (char)c; if (out.length() > 8192) break; }
        }
        return got;
    };

    // RFC-5545 line unfolding via one-line look-ahead.
    String held; bool haveHeld = false;
    auto nextLogical = [&](String &out) -> bool {
        String cur;
        if (haveHeld) { cur = held; haveHeld = false; }
        else if (!physLine(cur)) return false;
        while (true) {
            String nxt;
            if (!physLine(nxt)) { out = cur; return true; }
            if (nxt.length() && (nxt[0] == ' ' || nxt[0] == '\t')) cur += nxt.substring(1);
            else { held = nxt; haveHeld = true; out = cur; return true; }
        }
    };

    s_mastN = 0; s_suppN = 0;
    bool inEvent = false, haveStart = false;
    String summary, dtstart, rrule, uid, recurId, exdate;

    String line;
    while (nextLogical(line)) {
        if (millis() - started > 25000 || totalBytes > 2000000) break;
        yield();
        if (line.startsWith("BEGIN:VEVENT")) {
            inEvent = true; haveStart = false;
            summary = ""; dtstart = ""; rrule = ""; uid = ""; recurId = ""; exdate = "";
            vevents++;
            continue;
        }
        if (line.startsWith("END:VEVENT")) {
            if (inEvent && haveStart) {
                bool allDay = false;
                long ds = parse_ics_time(dtstart, allDay);
                Rrule rr; parse_rrule(rrule, rr);
                // EXDATE(s): cancel specific occurrences of this UID.
                if (exdate.length() && uid.length()) {
                    int p = 0;
                    while (p < (int)exdate.length() && s_suppN < SUPP_MAX) {
                        int c = exdate.indexOf(',', p);
                        String one = (c < 0) ? exdate.substring(p) : exdate.substring(p, c);
                        one.trim(); bool ad;
                        long e = parse_ics_time(one, ad);
                        if (e > 0) { s_supp[s_suppN].uid = uid; s_supp[s_suppN].epoch = e; s_suppN++; }
                        if (c < 0) break; p = c + 1;
                    }
                }
                if (recurId.length()) {
                    // Moved/override instance: suppress the master's original slot,
                    // then add the rescheduled occurrence itself.
                    bool ad; long rid = parse_ics_time(recurId, ad);
                    if (rid > 0 && uid.length() && s_suppN < SUPP_MAX) {
                        s_supp[s_suppN].uid = uid; s_supp[s_suppN].epoch = rid; s_suppN++;
                    }
                    if (ds > 0) cal_add(cand, nc, ds, allDay, ics_unescape(summary), lo, hi);
                } else if (rr.has) {
                    if (ds > 0 && ds <= hi && (rr.until == 0 || rr.until >= lo) && s_mastN < MAST_MAX) {
                        s_mast[s_mastN].uid = uid; s_mast[s_mastN].title = ics_unescape(summary);
                        s_mast[s_mastN].dtstart = ds; s_mast[s_mastN].allDay = allDay;
                        s_mast[s_mastN].rr = rr; s_mastN++;
                    }
                } else if (ds > 0) {
                    cal_add(cand, nc, ds, allDay, ics_unescape(summary), lo, hi);
                }
            }
            inEvent = false;
            continue;
        }
        if (!inEvent) continue;
        int colon = line.indexOf(':');
        if (colon < 0) continue;
        String name = line.substring(0, colon);
        String value = line.substring(colon + 1);
        String base = name; int semi = name.indexOf(';');
        if (semi >= 0) base = name.substring(0, semi);
        base.toUpperCase();
        if (base == "SUMMARY") summary = value;
        else if (base == "DTSTART") { dtstart = value; haveStart = true; }
        else if (base == "RRULE") rrule = value;
        else if (base == "UID") uid = value;
        else if (base == "RECURRENCE-ID") recurId = value;
        else if (base == "EXDATE") { if (exdate.length()) exdate += ","; exdate += value; }
    }
    https.end();
    for (int i = 0; i < s_mastN; i++) cal_expand(cand, nc, s_mast[i], lo, hi);
    Serial.printf("[cal] bytes=%u vevents=%d masters=%d suppress=%d in-window=%d now=%ld\n",
                  (unsigned)totalBytes, vevents, s_mastN, s_suppN, nc, now);
    Serial.printf("[cal] head: %s\n", preview.c_str());

    // Sort ascending by start time (small n, insertion sort).
    for (int i = 1; i < nc; i++) {
        CalEvent key = cand[i]; int j = i - 1;
        while (j >= 0 && cand[j].start > key.start) { cand[j + 1] = cand[j]; j--; }
        cand[j + 1] = key;
    }
    if (nc == 0) { ui_calendar_error("No upcoming events"); return; }
    for (int i = 0; i < nc; i++) {
        time_t t = (time_t)cand[i].start; struct tm lt; localtime_r(&t, &lt);
        Serial.printf("[cal] #%d %04d-%02d-%02d %02d:%02d ad=%d '%s'\n", i,
                      lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min,
                      (int)cand[i].allDay, cand[i].title.c_str());
    }
    ui_calendar_set(cand, nc);
}

// --------------------------------------------------------------------- tick ---
// ----------------------------------------------------------------- tick ---
// One poller per tab. Only the focused tab's source is refreshed; DIAG/CONFIG
// have none. Switching tabs forces an immediate sync (see lastMs = 0 below).
struct PagePoll { void (*poll)(); uint32_t lastMs; };
static PagePoll s_poll[PAGE_COUNT] = {
    /* HOME     */ { poll_weather,  0 },
    /* FLIGHTS  */ { poll_flights,  0 },
    /* CALENDAR */ { poll_calendar, 0 },
    /* TICKERS  */ { poll_tickers,  0 },
    /* AIR      */ { poll_air,      0 },
    /* DIAG     */ { nullptr,       0 },
    /* CONFIG   */ { nullptr,       0 },
};
static Page s_lastActivePage = PAGE_COUNT;   // invalid -> first tick syncs Home

// Calendar refetches on a slower (15 min) cadence, needs a synced clock, and
// reacts to a changed .ics URL — extra gates the generic timer doesn't cover.
static void poll_calendar_gate(PagePoll &pp) {
    if (time(nullptr) <= 1600000000L) return;   // wait for a synced clock
    String calUrl = settings().icsUrl; calUrl.trim();
    bool urlChanged = (calUrl != s_lastCalUrl);
    if (!calUrl.length()) {
        if (urlChanged) { s_lastCalUrl = calUrl; ui_calendar_error("No calendar URL configured"); }
        return;
    }
    if (pp.lastMs == 0 || urlChanged ||
        millis() - pp.lastMs >= 15UL * 60UL * 1000UL) {
        s_lastCalUrl = calUrl;
        poll_calendar();
        pp.lastMs = millis();   // count the cadence from completion, not start
    }
}

void data_tick() {
    if (net_state() != NetState::Connected) return;
    if (!s_timeStarted) data_begin_time();

    // PIR motion drives the Air page indicator; update only on change. Cheap.
    static int s_lastPir = -1;
    int pir = digitalRead(PIN_GPIO_D);
    if (pir != s_lastPir) { s_lastPir = pir; ui_air_motion(pir == HIGH); }

    // Out-of-band one-off refreshes (radar zoom / ticker timeframe changed).
    // These only fire while their page is visible, so poll now and reset that
    // page's cadence so the generic timer below doesn't double-fetch.
    if (s_flightsDirty) { s_flightsDirty = false; poll_flights(); s_poll[PAGE_FLIGHTS].lastMs = millis(); }
    if (s_tickersDirty) { s_tickersDirty = false; poll_tickers(); s_poll[PAGE_TICKERS].lastMs = millis(); }

    // Poll only the source behind the focused tab. A tab change forces an
    // immediate sync; otherwise it refreshes on the configured cadence.
    Page active = ui_active_page();
    if (active != s_lastActivePage) {
        s_lastActivePage = active;
        s_poll[active].lastMs = 0;   // sync-on-focus
    }

    PagePoll &pp = s_poll[active];
    if (!pp.poll) return;                             // DIAG / CONFIG: nothing
    if (active == PAGE_CALENDAR) { poll_calendar_gate(pp); return; }

    uint32_t interval = (uint32_t)settings().pollSeconds * 1000UL;
    if (active == PAGE_FLIGHTS && interval > 10000UL) interval = 10000UL;   // live radar refreshes faster
    if (pp.lastMs == 0 || millis() - pp.lastMs >= interval) {
        pp.poll();
        pp.lastMs = millis();   // count the cadence from completion, so a slow
                                // poll can't immediately re-trigger itself
    }
}
