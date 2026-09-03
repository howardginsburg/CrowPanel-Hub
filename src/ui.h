// ui.h — LVGL launcher shell, top bar, and the full-screen dashboard panels.
#pragma once
#include <Arduino.h>

// Page indices. PAGE_LAUNCHER is the tile grid; the rest are full-screen panels.
enum Page {
    PAGE_LAUNCHER = 0,
    PAGE_WEATHER,
    PAGE_FLIGHTS,
    PAGE_CALENDAR,
    PAGE_TICKERS,
    PAGE_AIR,
    PAGE_PHOTO,
    PAGE_DIAG,
    PAGE_CONFIG,
    PAGE_COUNT
};

// Build the whole UI. Call once after display_init().
void ui_init();

// LVGL is not thread-safe. The network task and the render/loop task both touch
// widgets, so every LVGL access is serialized through a recursive mutex. The
// loop wraps ui_tick()+display_tick() in ui_lock()/ui_unlock(); the data-push
// hooks below lock themselves.
void ui_lock();
void ui_unlock();

// Periodic UI housekeeping (clock, weather animation, live Diag/Calendar values). Cheap.
void ui_tick();

// Switch the visible page programmatically.
void ui_show_page(Page p);

// The currently-visible page (read by the data layer to poll only that tab).
Page ui_active_page();

// ---- Data push hooks (called by the data layer when fresh values arrive) ----
void ui_weather_set(int code, const String &summary, float tempC, int humidity, float windKph, float feelsC);
void ui_weather_error(const String &msg);
void ui_weather_uv_set(float uvIndex);

#define UI_FORECAST_DAYS 5
struct DayForecast { int code; float hiC; float loC; };
void ui_forecast_set(DayForecast *days, int count);

#define UI_MAX_FLIGHTS 15
struct FlightRow {
    String tail; String callsign; String type;
    int altFt; int distNm; int bearing; int track;
    int gs; int vrate; String squawk;
};
void ui_flights_set(FlightRow *rows, int count);
void ui_flights_error(const String &msg);
// Current radar range in NM (zoom override, else the configured value).
int  ui_radar_range_nm();

#define SPARK_N 48
struct TickerRow {
    String symbol; String name;
    double price; double changeAbs; double changePct;
    double winLo; double winHi;
    bool   live; bool valid;
    float  spark[SPARK_N]; int sparkLen;
};
void ui_tickers_set(TickerRow *rows, int count);
void ui_tickers_error(const String &msg);
// Selected chart timeframe index (0=1D .. 4=1Y); read by the data layer.
int  ui_ticker_tf_index();

#define UI_MAX_EVENTS 24
struct CalEvent { long start; long end; bool allDay; String title; String location; };
void ui_calendar_set(CalEvent *events, int count);
void ui_calendar_error(const String &msg);

// ---- Air quality (Open-Meteo US AQI + UV) ----
void ui_air_set(int usAqi, float pm25, float pm10, float o3, float no2);
void ui_air_error(const String &msg);
void ui_air_uv_set(float uvIndex);

// ---- Sun / Moon (Open-Meteo daily + local moon-phase math) ----
void ui_sun_set(int sunriseMin, int sunsetMin, int moonIdx, int illumPct);

// ---- Hourly forecast strip (next 12 hours) ----
#define UI_HOURLY_N 12
struct HourCell { int hour; float tempC; int precipPct; };
void ui_hourly_set(HourCell *cells, int count);

// ---- Photo frame ----
// Called by the data layer after a fetch: repaints the canvas on success, or
// shows the status text (from photo_status()) on failure.
void ui_photo_refresh(bool ok, const char *status);
