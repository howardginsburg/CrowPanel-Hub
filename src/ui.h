// ui.h — LVGL sidebar-nav shell and the eight dashboard pages.
#pragma once
#include <Arduino.h>

// Page indices (left sidebar order).
enum Page {
    PAGE_HOME = 0,
    PAGE_FLIGHTS,
    PAGE_CALENDAR,
    PAGE_TICKERS,
    PAGE_AIR,
    PAGE_DIAG,
    PAGE_CONFIG,
    PAGE_COUNT
};

// Build the whole UI. Call once after display_init().
void ui_init();

// Periodic UI housekeeping (clock text, config-tab connection details). Cheap.
void ui_tick();

// Switch the visible page programmatically.
void ui_show_page(Page p);

// The currently-visible page (read by the data layer to poll only that tab).
Page ui_active_page();

// ---- Data push hooks (called by the data layer when fresh values arrive) ----
void ui_weather_set(int code, const String &summary, float tempC, int humidity, float windKph);
void ui_weather_error(const String &msg);

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

#define UI_MAX_EVENTS 12
struct CalEvent { long start; bool allDay; String title; };
void ui_calendar_set(CalEvent *events, int count);
void ui_calendar_error(const String &msg);

// ---- Air quality (Open-Meteo US AQI + PIR motion) ----
void ui_air_set(int usAqi, float pm25, float pm10, float o3, float no2);
void ui_air_error(const String &msg);
void ui_air_motion(bool motion);
void ui_air_uv_set(float uvIndex);

// ---- Sun / Moon (Open-Meteo daily + local moon-phase math) ----
void ui_sun_set(int sunriseMin, int sunsetMin, int moonIdx, int illumPct);

// ---- Hourly forecast strip (next 12 hours) ----
#define UI_HOURLY_N 12
struct HourCell { int hour; float tempC; int precipPct; };
void ui_hourly_set(HourCell *cells, int count);
