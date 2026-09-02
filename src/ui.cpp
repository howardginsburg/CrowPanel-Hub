// ui.cpp — sidebar navigation shell + page construction.
#include "ui.h"
#include "settings.h"
#include "net_wifi.h"
#include "display.h"
#include "data.h"
#include "photo.h"
#include <lvgl.h>
#include <qrcode.h>
#include <time.h>
#include <math.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_heap_caps.h>

#define DEG2RAD 0.017453292519943295f

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------
static const int TOPBAR_H  = 44;                     // always-visible top bar (hosts the clock)
#define PAGE_H (LV_VER_RES - TOPBAR_H)               // usable panel height below the top bar
static const int SIDEBAR_W = 0;                      // legacy width offset (no sidebar now; full width)
static const int RADAR_PX  = 368;   // fits the 400px content height below the status row

static lv_obj_t *s_pages[PAGE_COUNT];
static lv_obj_t *s_topbar;
static lv_obj_t *s_topbarBack;
static lv_obj_t *s_topbarTitle;
static lv_obj_t *s_topWifi;                          // Wi-Fi signal bars (top bar)
static lv_obj_t *s_topWifiBar[4];
static Page      s_active = PAGE_LAUNCHER;

// Widgets we update at runtime
static lv_obj_t *s_clockTime;
static lv_obj_t *s_clockDate;
static lv_obj_t *s_weatherBody;
static lv_obj_t *s_wxDetail;                          // right column: feels-like + UV
static lv_obj_t *s_wxIcon;
static int       s_wxCode  = -1;                     // current weather code (for animation)
static uint32_t  s_wxFrame = 0;                       // weather glyph animation frame
static lv_obj_t *s_wxTemp;
static lv_obj_t *s_wxLoc;
static String    s_wxSummary;                         // conditions text (for body recompose)
static int       s_wxHum     = 0;                     // humidity %% (for body recompose)
static float     s_wxWindMph = 0;                     // wind mph (for body recompose)
static float     s_wxFeelsF  = -1000;                 // apparent temp F (-1000 = none yet)
static float     s_wxUvIdx   = -1;                    // daily max UV (-1 = none yet)
static lv_obj_t *s_fcCard[UI_FORECAST_DAYS];
static lv_obj_t *s_fcIcon[UI_FORECAST_DAYS];
static lv_obj_t *s_fcDay[UI_FORECAST_DAYS];
static lv_obj_t *s_fcTemp[UI_FORECAST_DAYS];
static DayForecast s_forecast[UI_FORECAST_DAYS];
static int       s_forecastCount = 0;
static lv_obj_t *s_flightsTable;
static lv_obj_t *s_flightsStatus;
static lv_obj_t *s_flightsRadar;
static lv_obj_t *s_flightsToggleLbl;
static lv_obj_t *s_zoomIn;
static lv_obj_t *s_zoomOut;
static lv_obj_t *s_radarRangeLbl;
static lv_obj_t *s_flightCard;
static lv_obj_t *s_flightCardLbl;
static lv_obj_t *s_flightHero;                       // nearest-aircraft callout (map view)
static lv_obj_t *s_heroCallsign;
static lv_obj_t *s_heroDist;
static lv_obj_t *s_heroBody;
static bool      s_flightsShowMap = true;
static int       s_radarRangeNm = 0;                 // 0 = follow settings
static FlightRow s_flightRows[UI_MAX_FLIGHTS];
static int       s_flightCount = 0;
static lv_coord_t s_planePx[UI_MAX_FLIGHTS];         // radar-local hit targets
static lv_coord_t s_planePy[UI_MAX_FLIGHTS];
static float      s_sweepDeg = 0.0f;                 // radar sweep-arm angle (deg)
static lv_timer_t *s_radarTimer = nullptr;           // animates the sweep
#define TRAIL_N 12
struct PlaneTrail {                                  // per-aircraft flown path
    String   id;
    float    xnm[TRAIL_N], ynm[TRAIL_N];             // positions NM from home (N-up)
    uint8_t  len;
    bool     used;
    uint32_t lastSeen;
};
static PlaneTrail s_trails[UI_MAX_FLIGHTS];
static uint32_t   s_trailPoll = 0;
static void      draw_radar();
static lv_obj_t *s_tickersStatus;
static lv_obj_t *s_tkList;                           // scroll container for cards
static lv_obj_t *s_tkCard[8];
static lv_obj_t *s_tkSym[8];
static lv_obj_t *s_tkName[8];
static lv_obj_t *s_tkState[8];
static lv_obj_t *s_tkPrice[8];
static lv_obj_t *s_tkChange[8];
static lv_obj_t *s_tkSpark[8];                       // sparkline canvases
static lv_obj_t *s_tkBar[8];                         // window range bar
static lv_obj_t *s_tkBarDot[8];                      // marker on the range bar
static lv_obj_t *s_tkLo[8];
static lv_obj_t *s_tkHi[8];
static lv_obj_t *s_tfBtn[5];                         // timeframe selector buttons
static int       s_tfIndex = 0;                      // 0=1D .. 4=1Y
static lv_obj_t *s_calList;                          // calendar scroll container
static lv_obj_t *s_calStatus;
static lv_obj_t *s_calHero;                          // "up next" hero card
static lv_obj_t *s_calHeroTag;                       // NOW / UP NEXT pill
static lv_obj_t *s_calHeroTitle;
static lv_obj_t *s_calHeroWhen;                      // absolute time + live countdown
static lv_obj_t *s_calRow[UI_MAX_EVENTS];
static lv_obj_t *s_calWhen[UI_MAX_EVENTS];
static lv_obj_t *s_calTitle[UI_MAX_EVENTS];

// Calendar views (List / Day / Week / Month), selectable like the ticker timeframe.
enum { CAL_LIST = 0, CAL_DAY, CAL_WEEK, CAL_MONTH };
#define CAL_CACHE_N 96                               // full event cache (decoupled from the row pool)
static CalEvent  s_calAll[CAL_CACHE_N];              // all fetched events, sorted ascending
static int       s_calAllCount = 0;
static int       s_calRowMap[UI_MAX_EVENTS];         // visible row -> cache index
static int       s_calView   = CAL_LIST;
static long      s_calAnchor = 0;                    // an instant inside the shown period (0 = today)
static lv_obj_t *s_calViewBtn[4];                    // List/Day/Week/Month selector
static lv_obj_t *s_calNav;                           // prev / period / next / today header
static lv_obj_t *s_calPeriodLbl;                     // current period caption
static lv_obj_t *s_calGrid;                          // month-grid container
static lv_obj_t *s_calDow[7];                        // month weekday headers
static lv_obj_t *s_calCell[42];                      // month day cells
static lv_obj_t *s_calCellNum[42];                   // day-of-month labels
static lv_obj_t *s_calCellCnt[42];                   // per-day event count badge
static long      s_calCellEpoch[42];                 // noon epoch of each month cell
static lv_obj_t *s_calCard;                          // tap-to-open event detail popup
static lv_obj_t *s_calCardTitle;
static lv_obj_t *s_calCardBody;
static int       s_calHeroIdx = -1;                  // event index shown by the hero
static lv_obj_t *s_airStatus;                        // air-quality source line
static lv_obj_t *s_airAqiArc;                         // US AQI ring gauge
static lv_obj_t *s_airAqi;                            // AQI number (gauge center)
static lv_obj_t *s_airCat;                            // AQI category text
static lv_obj_t *s_airPm25;
static lv_obj_t *s_airPm10;
static lv_obj_t *s_airO3;
static lv_obj_t *s_airNo2;
static lv_obj_t *s_airUvArc;                          // UV ring gauge
static lv_obj_t *s_airUv;                             // UV number (gauge center)
static lv_obj_t *s_sunLabel;                          // sunrise/sunset row (Home)
static lv_obj_t *s_moonLabel;                         // moon phase + illumination (Home)
static lv_obj_t *s_skyCanvas;                         // day/night sky gradient (Home clock)
static int       s_srMin = -1, s_ssMin = -1;          // sunrise/sunset (local minutes)
static int       s_moonIdx = -1, s_moonPct = 0;       // moon phase index + illumination %
static lv_obj_t *s_sunRiseLbl;                        // sunrise time, sun-path panel corner
static lv_obj_t *s_sunSetLbl;                         // sunset time, sun-path panel corner
static lv_obj_t *s_hrCell[UI_HOURLY_N];              // hourly strip cells (Home)
static lv_obj_t *s_hrHour[UI_HOURLY_N];
static lv_obj_t *s_hrTemp[UI_HOURLY_N];
static lv_obj_t *s_hrPrecip[UI_HOURLY_N];
static lv_obj_t *s_diagLbl;                           // diagnostics multiline body
static lv_obj_t *s_diagHeapSpark;                     // free-heap trend canvas (Diag)
static lv_obj_t *s_diagRssiSpark;                     // Wi-Fi RSSI trend canvas (Diag)
static lv_obj_t *s_diagTempSpark, *s_diagTempVal;     // SoC die-temperature trend (Diag)
static lv_obj_t *s_diagFpsSpark,  *s_diagFpsVal;      // render-FPS trend (Diag)
static lv_obj_t *s_diagHeapVal;                       // current heap KB (Diag)
static lv_obj_t *s_diagRssiVal;                       // current RSSI dBm (Diag)
static lv_obj_t *s_diagRamBar,   *s_diagRamVal;       // internal-RAM usage bar (Diag)
static lv_obj_t *s_diagPsramBar, *s_diagPsramVal;     // PSRAM usage bar (Diag)
static lv_obj_t *s_diagFlashBar, *s_diagFlashVal;     // flash/sketch usage bar (Diag)
static lv_obj_t *s_diagSigBar[4];                     // Wi-Fi signal strength bars (Diag)
static lv_obj_t *s_diagSigTxt;                        // signal quality label (Diag)
#define DIAG_N 120                                    // 120 samples @ 500ms = 60s window
#define DSP_W  400
#define DSP_H  56
static float s_heapHist[DIAG_N];
static float s_rssiHist[DIAG_N];
static float s_tempHist[DIAG_N];                      // SoC die temperature (deg F)
static float s_fpsHist[DIAG_N];                       // measured render FPS
static int   s_diagCount = 0;

// Offline resilience (#1): once a source has shown good data, a later fetch
// failure keeps the last-good screen instead of blanking it — only the status
// line flips to an amber "offline / last update" note.
static bool s_wxHave = false, s_flightsHave = false, s_airHave = false;
static bool s_tickersHave = false, s_calHave = false, s_photoHave = false;
static lv_obj_t *s_pageSpin[PAGE_COUNT] = { nullptr };   // per-page loading spinners
static lv_obj_t *s_cfgState;
static lv_obj_t *s_cfgDetails;
static lv_obj_t *s_cfgQr;

// Photo frame
static lv_obj_t *s_photoCanvas;
static lv_obj_t *s_photoStatus;

static const char *PAGE_TITLES[PAGE_COUNT] = {
    "Home", "Weather", "Flights", "Calendar", "Tickers", "Air", "Photo", "Diag", "Config"
};
static const char *PAGE_ICONS[PAGE_COUNT] = {
    LV_SYMBOL_HOME, LV_SYMBOL_HOME, LV_SYMBOL_UP, LV_SYMBOL_LIST,
    LV_SYMBOL_CHARGE, LV_SYMBOL_EYE_OPEN, LV_SYMBOL_IMAGE, LV_SYMBOL_DRIVE, LV_SYMBOL_SETTINGS
};

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------
static void replay_page_anims(Page p);   // re-sweep gauges/bars when a panel gains focus

// Has a data page already received good data? (non-data pages report true so
// they never show a loading spinner.)
static bool page_has_data(Page p) {
    switch (p) {
        case PAGE_WEATHER:  return s_wxHave;
        case PAGE_FLIGHTS:  return s_flightsHave;
        case PAGE_CALENDAR: return s_calHave;
        case PAGE_TICKERS:  return s_tickersHave;
        case PAGE_AIR:      return s_airHave;
        case PAGE_PHOTO:    return s_photoHave;
        default:            return true;
    }
}

static void page_set_loading(Page p, bool on) {
    lv_obj_t *sp = (p >= 0 && p < PAGE_COUNT) ? s_pageSpin[p] : nullptr;
    if (!sp) return;
    if (on) { lv_obj_clear_flag(sp, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(sp); }
    else      lv_obj_add_flag(sp, LV_OBJ_FLAG_HIDDEN);
}

void ui_show_page(Page p) {
    if (p < 0 || p >= PAGE_COUNT) return;
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (i == p) lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    bool launcher = (p == PAGE_LAUNCHER);
    if (s_topbarBack) {
        if (launcher) lv_obj_add_flag(s_topbarBack, LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_clear_flag(s_topbarBack, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_topbarTitle) lv_label_set_text(s_topbarTitle, PAGE_TITLES[p]);
    s_active = p;
    if (!launcher) settings_set_last_panel((uint8_t)p);
    // Show a loading spinner while the freshly-focused data page has no data yet.
    page_set_loading(p, !page_has_data(p) && net_state() == NetState::Connected);
    replay_page_anims(p);
}

Page ui_active_page() { return s_active; }

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static lv_obj_t *make_page(lv_obj_t *parent) {
    lv_obj_t *pg = lv_obj_create(parent);
    lv_obj_set_size(pg, LV_HOR_RES, PAGE_H);
    lv_obj_set_pos(pg, 0, TOPBAR_H);
    lv_obj_set_style_border_width(pg, 0, 0);
    lv_obj_set_style_radius(pg, 0, 0);
    lv_obj_set_style_bg_color(pg, lv_color_hex(0x0f1420), 0);
    lv_obj_set_style_pad_all(pg, 18, 0);
    return pg;
}

static lv_obj_t *placeholder_body(lv_obj_t *pg, const char *txt) {
    lv_obj_t *l = lv_label_create(pg);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(0x8b97b0), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 8);
    return l;
}

// ---------------------------------------------------------------------------
// Weather icons — simple vector art drawn onto a canvas (no icon font needed).
// ---------------------------------------------------------------------------
enum WxType { WX_CLEAR, WX_PARTLY, WX_CLOUD, WX_FOG, WX_DRIZZLE, WX_RAIN, WX_SNOW, WX_STORM };
static WxType wx_type(int code) {
    if (code == 0)  return WX_CLEAR;
    if (code <= 2)  return WX_PARTLY;
    if (code == 3)  return WX_CLOUD;
    if (code <= 48) return WX_FOG;
    if (code <= 57) return WX_DRIZZLE;
    if (code <= 67) return WX_RAIN;
    if (code <= 77) return WX_SNOW;
    if (code <= 82) return WX_RAIN;
    if (code <= 86) return WX_SNOW;
    return WX_STORM;
}

static void wx_sun(lv_obj_t *cv, int cx, int cy, int r, lv_color_t col, int phase) {
    lv_draw_line_dsc_t l; lv_draw_line_dsc_init(&l);
    l.color = col; l.width = 3; l.opa = LV_OPA_COVER; l.round_start = 1; l.round_end = 1;
    float sway  = sinf(phase * 0.15f) * (6.0f * DEG2RAD);   // rays rock gently
    int   pulse = (int)lroundf(sinf(phase * 0.30f) * 2.0f); // and breathe in/out
    for (int a = 0; a < 360; a += 45) {
        float rad = a * DEG2RAD + sway;
        lv_point_t p[2] = {
            {(lv_coord_t)(cx + cosf(rad) * (r + 3)), (lv_coord_t)(cy + sinf(rad) * (r + 3))},
            {(lv_coord_t)(cx + cosf(rad) * (r + 9 + pulse)), (lv_coord_t)(cy + sinf(rad) * (r + 9 + pulse))}};
        lv_canvas_draw_line(cv, p, 2, &l);
    }
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = col; d.bg_opa = LV_OPA_COVER; d.radius = LV_RADIUS_CIRCLE;
    lv_canvas_draw_rect(cv, cx - r, cy - r, 2 * r, 2 * r, &d);
}

static void wx_cloud(lv_obj_t *cv, int cx, int cy, int s, lv_color_t col, int phase) {
    cx += (int)lroundf(sinf(phase * 0.12f) * 3.0f);        // slow horizontal drift
    lv_draw_rect_dsc_t c; lv_draw_rect_dsc_init(&c);
    c.bg_color = col; c.bg_opa = LV_OPA_COVER; c.radius = LV_RADIUS_CIRCLE;
    int p = s * 7 / 10;
    lv_canvas_draw_rect(cv, cx - s - p / 2, cy - p / 2, p, p, &c);   // left puff
    lv_canvas_draw_rect(cv, cx + s - p / 2, cy - p / 2, p, p, &c);   // right puff
    lv_canvas_draw_rect(cv, cx - s / 2,     cy - s,     s, s, &c);   // top puff
    lv_draw_rect_dsc_t b; lv_draw_rect_dsc_init(&b);
    b.bg_color = col; b.bg_opa = LV_OPA_COVER; b.radius = s / 2;
    lv_canvas_draw_rect(cv, cx - s - p / 2, cy - p / 4, 2 * s + p, p, &b);  // base slab
}

static void wx_rain(lv_obj_t *cv, int cx, int cy, int phase) {
    lv_draw_line_dsc_t l; lv_draw_line_dsc_init(&l);
    l.color = lv_color_hex(0x5aa0ff); l.width = 2; l.opa = LV_OPA_COVER;
    for (int k = -1; k <= 1; k++) {
        int x   = cx + k * 8;
        int off = (phase * 3 + (k + 1) * 6) % 16;          // drops fall + recycle
        int y   = cy - 2 + off;
        lv_point_t p[2] = {{(lv_coord_t)(x + 3), (lv_coord_t)y}, {(lv_coord_t)(x - 1), (lv_coord_t)(y + 8)}};
        lv_canvas_draw_line(cv, p, 2, &l);
    }
}

static void wx_snow(lv_obj_t *cv, int cx, int cy, int phase) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_white(); d.bg_opa = LV_OPA_COVER; d.radius = LV_RADIUS_CIRCLE;
    for (int k = -1; k <= 1; k++) {
        int off  = (phase * 2 + (k + 1) * 6) % 16;         // flakes drift down
        int sway = (int)lroundf(sinf(phase * 0.35f + k) * 2.0f);
        lv_canvas_draw_rect(cv, cx + k * 8 - 2 + sway, cy - 2 + off, 4, 4, &d);
    }
}

static void wx_fog(lv_obj_t *cv, int cx, int cy, int s, int phase) {
    lv_draw_line_dsc_t l; lv_draw_line_dsc_init(&l);
    l.color = lv_color_hex(0x8b97b0); l.width = 3; l.opa = LV_OPA_COVER; l.round_start = 1; l.round_end = 1;
    for (int k = 0; k < 3; k++) {
        int y  = cy + k * 7;
        int dx = (int)lroundf(sinf(phase * 0.15f + k) * 4.0f);   // layers slide
        lv_point_t p[2] = {{(lv_coord_t)(cx - s + dx), (lv_coord_t)y}, {(lv_coord_t)(cx + s + dx), (lv_coord_t)y}};
        lv_canvas_draw_line(cv, p, 2, &l);
    }
}

static void wx_bolt(lv_obj_t *cv, int cx, int cy, int phase) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(0xffd23f); d.bg_opa = LV_OPA_COVER;
    if ((phase > 0) && (((phase / 4) % 5) == 0)) d.bg_opa = LV_OPA_40;   // occasional flicker
    lv_point_t z[6] = {
        {(lv_coord_t)(cx + 2), (lv_coord_t)cy},        {(lv_coord_t)(cx - 6), (lv_coord_t)(cy + 9)},
        {(lv_coord_t)(cx - 1), (lv_coord_t)(cy + 9)},  {(lv_coord_t)(cx - 4), (lv_coord_t)(cy + 17)},
        {(lv_coord_t)(cx + 7), (lv_coord_t)(cy + 5)},  {(lv_coord_t)(cx + 1), (lv_coord_t)(cy + 5)}};
    lv_canvas_draw_polygon(cv, z, 6, &d);
}

// Draw the icon for a weather code, centered at (cx,cy) with half-size s.
// phase advances the animation (0 = static, used by the forecast thumbnails).
static void wx_draw(lv_obj_t *cv, int cx, int cy, int s, int code, int phase = 0) {
    lv_color_t sun     = lv_color_hex(0xffd23f);
    lv_color_t cloud   = lv_color_hex(0xc7d0e0);
    lv_color_t cloudDk = lv_color_hex(0x9aa7bf);
    switch (wx_type(code)) {
        case WX_CLEAR:  wx_sun(cv, cx, cy, s * 55 / 100, sun, phase); break;
        case WX_PARTLY: wx_sun(cv, cx - s * 35 / 100, cy - s * 30 / 100, s * 32 / 100, sun, phase);
                        wx_cloud(cv, cx + s * 12 / 100, cy + s * 22 / 100, s * 42 / 100, cloud, phase); break;
        case WX_CLOUD:  wx_cloud(cv, cx, cy, s * 52 / 100, cloud, phase); break;
        case WX_FOG:    wx_cloud(cv, cx, cy - s * 18 / 100, s * 48 / 100, cloud, phase);
                        wx_fog(cv, cx, cy + s * 42 / 100, s * 55 / 100, phase); break;
        case WX_DRIZZLE:
        case WX_RAIN:   wx_cloud(cv, cx, cy - s * 22 / 100, s * 48 / 100, cloudDk, phase);
                        wx_rain(cv, cx, cy + s * 40 / 100, phase); break;
        case WX_SNOW:   wx_cloud(cv, cx, cy - s * 22 / 100, s * 48 / 100, cloud, phase);
                        wx_snow(cv, cx, cy + s * 40 / 100, phase); break;
        case WX_STORM:  wx_cloud(cv, cx, cy - s * 22 / 100, s * 48 / 100, cloudDk, phase);
                        wx_bolt(cv, cx, cy + s * 30 / 100, phase); break;
    }
}

// ---------------------------------------------------------------------------
// Page builders
// ---------------------------------------------------------------------------

// Day/night sky gradient behind the Home clock. -----------------------------
#define SKY_W 480
#define SKY_H 104
static uint32_t sky_mix(uint32_t a, uint32_t b, float t) {
    int ar = (a >> 16) & 0xff, ag = (a >> 8) & 0xff, ab = a & 0xff;
    int br = (b >> 16) & 0xff, bg = (b >> 8) & 0xff, bb = b & 0xff;
    int r = ar + (int)lroundf((br - ar) * t);
    int g = ag + (int)lroundf((bg - ag) * t);
    int c = ab + (int)lroundf((bb - ab) * t);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)c;
}
static void sky_disc(int cx, int cy, int r, uint32_t col) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(col); d.bg_opa = LV_OPA_COVER; d.radius = LV_RADIUS_CIRCLE;
    lv_canvas_draw_rect(s_skyCanvas, cx - r, cy - r, 2 * r, 2 * r, &d);
}
static void sky_stars() {
    static const uint16_t SX[] = { 22, 60, 96, 140, 176, 212, 250, 292, 314, 46, 122, 202, 276, 84, 160, 236, 300, 30, 336, 372, 410, 446, 356, 428 };
    static const uint8_t  SY[] = { 18, 40, 12,  54,  24,  48,  16,  38,  58, 70,  96,  84, 104, 122, 132, 110, 66, 128,  30,  92,  50, 116, 132,  22 };
    lv_draw_rect_dsc_t s; lv_draw_rect_dsc_init(&s);
    s.bg_color = lv_color_hex(0xdfe6f5); s.bg_opa = LV_OPA_70; s.radius = LV_RADIUS_CIRCLE;
    for (unsigned k = 0; k < sizeof(SX) / sizeof(SX[0]); k++)
        if (SY[k] < SKY_H - 34)                        // keep stars above the horizon line
            lv_canvas_draw_rect(s_skyCanvas, SX[k], SY[k], 2, 2, &s);
}
// Phase-accurate moon: lit/dark split by an elliptical terminator from the
// illuminated fraction (correct crescent AND gibbous), waxing lit on the right.
static void sky_moon(int cx, int cy, int r, int idx, int pct) {
    float f = pct / 100.0f; if (f < 0) f = 0; if (f > 1) f = 1;
    float cosT = 1.0f - 2.0f * f;                       // +1 new .. -1 full
    bool waxing = (idx >= 0 && idx <= 4);
    const uint32_t lit = 0xeef2ff, dark = 0x1b2740;
    for (int dy = -r; dy <= r; dy++) {
        int w2 = r * r - dy * dy; if (w2 < 0) continue;
        float w = sqrtf((float)w2);
        float xt = cosT * w;                            // terminator x on this row
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > r * r) continue;
            int px = cx + dx, py = cy + dy;
            if (px < 0 || px >= SKY_W || py < 0 || py >= SKY_H) continue;
            bool isLit = waxing ? (dx >= xt) : (dx <= -xt);
            lv_canvas_set_px_color(s_skyCanvas, px, py, lv_color_hex(isLit ? lit : dark));
        }
    }
}
static int sky_now_min() {
    time_t now = time(nullptr);
    if (now < 100000) return -1;
    struct tm t; localtime_r(&now, &t);
    return t.tm_hour * 60 + t.tm_min;
}
static void draw_sky(int nowMin) {
    if (!s_skyCanvas) return;
    const int TW = 45;                                 // twilight half-window (min)
    const uint32_t N_TOP = 0x070b1c, N_BOT = 0x131b2e; // night
    const uint32_t W_TOP = 0x243a6e, W_BOT = 0xef9f57; // dawn
    const uint32_t D_TOP = 0x1e5fbf, D_BOT = 0x88baee; // day
    const uint32_t K_TOP = 0x2a2450, K_BOT = 0xe8794a; // dusk
    uint32_t aT = N_TOP, aB = N_BOT, bT = N_TOP, bB = N_BOT; float t = 0;
    int sr = s_srMin, ss = s_ssMin;
    if (sr >= 0 && ss >= 0 && ss > sr) {
        if      (nowMin < sr - TW || nowMin > ss + TW) { }                                   // night
        else if (nowMin < sr)      { aT=N_TOP;aB=N_BOT; bT=W_TOP;bB=W_BOT; t=(float)(nowMin-(sr-TW))/TW; }
        else if (nowMin < sr + TW) { aT=W_TOP;aB=W_BOT; bT=D_TOP;bB=D_BOT; t=(float)(nowMin-sr)/TW; }
        else if (nowMin <= ss - TW){ aT=D_TOP;aB=D_BOT; bT=D_TOP;bB=D_BOT; t=0; }             // day
        else if (nowMin < ss)      { aT=D_TOP;aB=D_BOT; bT=K_TOP;bB=K_BOT; t=(float)(nowMin-(ss-TW))/TW; }
        else                       { aT=K_TOP;aB=K_BOT; bT=N_TOP;bB=N_BOT; t=(float)(nowMin-ss)/TW; }
    }
    if (t < 0) t = 0; if (t > 1) t = 1;
    uint32_t topC = sky_mix(aT, bT, t), botC = sky_mix(aB, bB, t);

    // Weather-aware backdrop (#5): during day/twilight, blend the clear-sky
    // gradient toward an overcast palette matching the current condition. Purely
    // procedural (zero flash) and colour-only, so there is no layout risk. Deep
    // night and "no sun data" keep the clear/starfield look untouched.
    float cloudK = 0;
    if (s_wxCode >= 0 && sr >= 0 && ss >= 0) {
        uint32_t oT = 0, oB = 0;
        switch (wx_type(s_wxCode)) {
            case WX_CLEAR:                            break;   // leave the blue sky
            case WX_PARTLY:  oT=0x6f80a0; oB=0xaebccf; cloudK=0.26f; break;
            case WX_CLOUD:   oT=0x5f6b80; oB=0x97a4ba; cloudK=0.55f; break;
            case WX_FOG:     oT=0x8a94a4; oB=0xc2cad6; cloudK=0.60f; break;
            case WX_DRIZZLE: oT=0x566072; oB=0x8793a6; cloudK=0.55f; break;
            case WX_RAIN:    oT=0x49525f; oB=0x74808f; cloudK=0.62f; break;
            case WX_SNOW:    oT=0x8f99a8; oB=0xcdd6e2; cloudK=0.50f; break;
            case WX_STORM:   oT=0x333a47; oB=0x59616f; cloudK=0.72f; break;
        }
        // Fade the tint in across twilight so day<->night stays smooth, and drop
        // it entirely at deep night (base is already dark; graying it looks wrong).
        float day = 1.0f;
        if      (nowMin < sr) day = (float)(nowMin - (sr - TW)) / TW;
        else if (nowMin > ss) day = (float)((ss + TW) - nowMin) / TW;
        if (day < 0) day = 0; if (day > 1) day = 1;
        cloudK *= day;
        if (cloudK > 0) {
            topC = sky_mix(topC, oT, cloudK);
            botC = sky_mix(botC, oB, cloudK);
        }
    }

    lv_draw_line_dsc_t g; lv_draw_line_dsc_init(&g); g.width = 1;
    for (int y = 0; y < SKY_H; y++) {
        g.color = lv_color_hex(sky_mix(topC, botC, (float)y / (SKY_H - 1)));
        lv_point_t p[2] = {{0, (lv_coord_t)y}, {(lv_coord_t)(SKY_W - 1), (lv_coord_t)y}};
        lv_canvas_draw_line(s_skyCanvas, p, 2, &g);
    }

    // Sun-path panel: a dotted daytime trajectory from the sunrise horizon to the
    // sunset horizon, with the sun (day) or a phase-shaded moon (night) riding it.
    const int ax0 = 42, ax1 = SKY_W - 42;
    const int baseY = SKY_H - 30;                       // horizon (room for corner times)
    const int arcH  = baseY - 16;                       // trajectory peak height

    lv_draw_line_dsc_t hl; lv_draw_line_dsc_init(&hl);
    hl.width = 1; hl.color = lv_color_hex(0x9fb0cc); hl.opa = LV_OPA_30;
    lv_point_t hp[2] = {{8, (lv_coord_t)baseY}, {(lv_coord_t)(SKY_W - 8), (lv_coord_t)baseY}};
    lv_canvas_draw_line(s_skyCanvas, hp, 2, &hl);

    bool isDay = (sr >= 0 && ss >= 0 && nowMin >= sr && nowMin <= ss && ss > sr);
    lv_draw_rect_dsc_t dd; lv_draw_rect_dsc_init(&dd);
    dd.radius = LV_RADIUS_CIRCLE;
    dd.bg_color = lv_color_hex(isDay ? 0xff9e3d : 0xffd27a);   // bold gold by day, soft by night
    dd.bg_opa   = isDay ? LV_OPA_80 : LV_OPA_40;
    int dsz = isDay ? 3 : 2;
    for (int i = 0; i <= 40; i++) {
        float fr = i / 40.0f;
        int x = ax0 + (int)lroundf(fr * (ax1 - ax0));
        int y = baseY - (int)lroundf(sinf(fr * 3.14159265f) * arcH);
        lv_canvas_draw_rect(s_skyCanvas, x - dsz / 2, y - dsz / 2, dsz, dsz, &dd);
    }
    sky_disc(ax0, baseY, 3, 0xffd27a);                 // sunrise horizon marker
    sky_disc(ax1, baseY, 3, 0xff9d5c);                 // sunset horizon marker

    if (sr >= 0 && ss >= 0 && nowMin >= sr && nowMin <= ss && ss > sr) {
        float fr = (float)(nowMin - sr) / (ss - sr);
        int dx = ax0 + (int)lroundf(fr * (ax1 - ax0));
        int dy = baseY - (int)lroundf(sinf(fr * 3.14159265f) * arcH);
        sky_disc(dx, dy, 10, sky_mix(0xffe08a, 0x9aa3b2, cloudK));   // sun glow
        sky_disc(dx, dy, 7,  sky_mix(0xfff2c4, 0xb8bfca, cloudK));   // sun core
    } else if (sr >= 0 && ss >= 0) {
        float fr = (nowMin > ss) ? (float)(nowMin - ss) / ((sr + 1440) - ss)
                                 : (float)(nowMin + 1440 - ss) / ((sr + 1440) - ss);
        int dx = ax0 + (int)lroundf(fr * (ax1 - ax0));
        int dy = baseY - (int)lroundf(sinf(fr * 3.14159265f) * arcH * 0.85f);
        sky_stars();
        sky_moon(dx, dy, 11, s_moonIdx, s_moonPct);
    } else {
        sky_stars();
    }
}

static void build_home(lv_obj_t *pg) {
    // Sun-path panel (top-left): day/night gradient with the sun/moon riding a
    // dotted trajectory, sunrise/sunset times pinned to the horizons.
    static lv_color_t *skyBuf = nullptr;
    if (!skyBuf) skyBuf = (lv_color_t *)heap_caps_malloc(
        SKY_W * SKY_H * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    s_skyCanvas = lv_canvas_create(pg);
    lv_canvas_set_buffer(s_skyCanvas, skyBuf, SKY_W, SKY_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(s_skyCanvas, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(s_skyCanvas, 10, 0);
    lv_obj_set_style_clip_corner(s_skyCanvas, true, 0);
    draw_sky(sky_now_min());

    s_sunRiseLbl = lv_label_create(s_skyCanvas);
    lv_label_set_text(s_sunRiseLbl, "--");
    lv_obj_set_style_text_font(s_sunRiseLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_sunRiseLbl, lv_color_hex(0xffe0a8), 0);
    lv_obj_set_style_bg_color(s_sunRiseLbl, lv_color_hex(0x05070e), 0);
    lv_obj_set_style_bg_opa(s_sunRiseLbl, LV_OPA_40, 0);
    lv_obj_set_style_pad_hor(s_sunRiseLbl, 5, 0);
    lv_obj_set_style_pad_ver(s_sunRiseLbl, 1, 0);
    lv_obj_set_style_radius(s_sunRiseLbl, 4, 0);
    lv_obj_align(s_sunRiseLbl, LV_ALIGN_BOTTOM_LEFT, 6, -4);

    s_sunSetLbl = lv_label_create(s_skyCanvas);
    lv_label_set_text(s_sunSetLbl, "--");
    lv_obj_set_style_text_font(s_sunSetLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_sunSetLbl, lv_color_hex(0xffc09a), 0);
    lv_obj_set_style_bg_color(s_sunSetLbl, lv_color_hex(0x05070e), 0);
    lv_obj_set_style_bg_opa(s_sunSetLbl, LV_OPA_40, 0);
    lv_obj_set_style_pad_hor(s_sunSetLbl, 5, 0);
    lv_obj_set_style_pad_ver(s_sunSetLbl, 1, 0);
    lv_obj_set_style_radius(s_sunSetLbl, 4, 0);
    lv_obj_align(s_sunSetLbl, LV_ALIGN_BOTTOM_RIGHT, -6, -4);

    // Current conditions (top-right): drawn icon + big temperature + details.
    static lv_color_t *wxBuf = nullptr;
    const int WX_PX = 96;
    if (!wxBuf) wxBuf = (lv_color_t *)heap_caps_malloc(
        WX_PX * WX_PX * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    s_wxIcon = lv_canvas_create(pg);
    lv_canvas_set_buffer(s_wxIcon, wxBuf, WX_PX, WX_PX, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(s_wxIcon, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_fill_bg(s_wxIcon, lv_color_hex(0x0f1420), LV_OPA_COVER);

    s_wxTemp = lv_label_create(pg);
    lv_label_set_text(s_wxTemp, "--");
    lv_obj_set_style_text_font(s_wxTemp, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_wxTemp, lv_color_hex(0xe6ebf5), 0);
    lv_obj_align(s_wxTemp, LV_ALIGN_TOP_RIGHT, -104, 12);

    s_wxLoc = lv_label_create(pg);
    lv_label_set_text(s_wxLoc, "");
    lv_obj_set_style_text_font(s_wxLoc, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_wxLoc, lv_color_hex(0x8b97b0), 0);
    lv_obj_set_style_text_align(s_wxLoc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_wxLoc, LV_ALIGN_TOP_MID, 303, 74);   // centered under temp + icon cluster

    s_weatherBody = lv_label_create(pg);
    lv_label_set_text(s_weatherBody, "Loading conditions...");
    lv_obj_set_style_text_font(s_weatherBody, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_weatherBody, lv_color_hex(0xcdd6ea), 0);
    lv_obj_set_style_text_align(s_weatherBody, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_weatherBody, LV_ALIGN_TOP_RIGHT, -158, 100);

    s_wxDetail = lv_label_create(pg);                 // right column (feels-like + UV)
    lv_label_set_text(s_wxDetail, "");
    lv_obj_set_style_text_font(s_wxDetail, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_wxDetail, lv_color_hex(0xcdd6ea), 0);
    lv_obj_set_style_text_align(s_wxDetail, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_wxDetail, LV_ALIGN_TOP_RIGHT, 0, 100);

    // 5-day forecast strip (bottom): one card per day.
    const int CW    = LV_HOR_RES - SIDEBAR_W - 36;
    const int gap   = 8;
    const int cardW = (CW - gap * (UI_FORECAST_DAYS - 1)) / UI_FORECAST_DAYS;
    const int cardH = 118;
    const int IC    = 56;
    static lv_color_t *fcBuf[UI_FORECAST_DAYS] = {nullptr};
    for (int i = 0; i < UI_FORECAST_DAYS; i++) {
        lv_obj_t *card = lv_obj_create(pg);
        lv_obj_set_size(card, cardW, cardH);
        lv_obj_align(card, LV_ALIGN_BOTTOM_LEFT, i * (cardW + gap), 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x141c2e), 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_pad_all(card, 4, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        s_fcCard[i] = card;

        s_fcDay[i] = lv_label_create(card);
        lv_label_set_text(s_fcDay[i], "--");
        lv_obj_set_style_text_font(s_fcDay[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_fcDay[i], lv_color_hex(0x9fb0cc), 0);
        lv_obj_align(s_fcDay[i], LV_ALIGN_TOP_MID, 0, 2);

        if (!fcBuf[i]) fcBuf[i] = (lv_color_t *)heap_caps_malloc(
            IC * IC * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
        s_fcIcon[i] = lv_canvas_create(card);
        lv_canvas_set_buffer(s_fcIcon[i], fcBuf[i], IC, IC, LV_IMG_CF_TRUE_COLOR);
        lv_obj_align(s_fcIcon[i], LV_ALIGN_CENTER, 0, -4);
        lv_canvas_fill_bg(s_fcIcon[i], lv_color_hex(0x141c2e), LV_OPA_COVER);

        s_fcTemp[i] = lv_label_create(card);
        lv_label_set_text(s_fcTemp[i], "-/-");
        lv_obj_set_style_text_font(s_fcTemp[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_fcTemp[i], lv_color_hex(0xe6ebf5), 0);
        lv_obj_align(s_fcTemp[i], LV_ALIGN_BOTTOM_MID, 0, 0);
    }

    // --- Astro summary (daylight + live countdown, moon phase) + hourly strip ---
    s_sunLabel = lv_label_create(pg);
    lv_label_set_text(s_sunLabel, "Daylight --");
    lv_obj_set_style_text_font(s_sunLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_sunLabel, lv_color_hex(0xcdd6ea), 0);
    lv_obj_align(s_sunLabel, LV_ALIGN_TOP_LEFT, 0, 110);

    s_moonLabel = lv_label_create(pg);
    lv_label_set_text(s_moonLabel, "Moon phase: --");
    lv_obj_set_style_text_font(s_moonLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_moonLabel, lv_color_hex(0x8b97b0), 0);
    lv_obj_align(s_moonLabel, LV_ALIGN_TOP_LEFT, 0, 132);

    const int CWh  = LV_HOR_RES - SIDEBAR_W - 36;
    const int hgap = 4;
    const int hcW  = (CWh - hgap * (UI_HOURLY_N - 1)) / UI_HOURLY_N;
    const int hcH  = 98;
    const int hcY  = 162;
    for (int i = 0; i < UI_HOURLY_N; i++) {
        lv_obj_t *c = lv_obj_create(pg);
        lv_obj_set_size(c, hcW, hcH);
        lv_obj_align(c, LV_ALIGN_TOP_LEFT, i * (hcW + hgap), hcY);
        lv_obj_set_style_bg_color(c, lv_color_hex(0x141c2e), 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_set_style_radius(c, 6, 0);
        lv_obj_set_style_pad_all(c, 2, 0);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        s_hrCell[i] = c;

        s_hrHour[i] = lv_label_create(c);
        lv_label_set_text(s_hrHour[i], "--");
        lv_obj_set_style_text_font(s_hrHour[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_hrHour[i], lv_color_hex(0x9fb0cc), 0);
        lv_obj_align(s_hrHour[i], LV_ALIGN_TOP_MID, 0, 2);

        s_hrTemp[i] = lv_label_create(c);
        lv_label_set_text(s_hrTemp[i], "--");
        lv_obj_set_style_text_font(s_hrTemp[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_hrTemp[i], lv_color_hex(0xe6ebf5), 0);
        lv_obj_align(s_hrTemp[i], LV_ALIGN_CENTER, 0, 0);

        s_hrPrecip[i] = lv_label_create(c);
        lv_label_set_text(s_hrPrecip[i], "");
        lv_obj_set_style_text_font(s_hrPrecip[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_hrPrecip[i], lv_color_hex(0x7fd1ff), 0);
        lv_obj_align(s_hrPrecip[i], LV_ALIGN_BOTTOM_MID, 0, -2);
    }
}

static int radar_range() {
    if (s_radarRangeNm > 0) return s_radarRangeNm;
    int r = settings().radarRangeNm;
    return r > 0 ? r : 25;
}
int ui_radar_range_nm() { return radar_range(); }

static void update_radar_range_lbl() {
    if (!s_radarRangeLbl) return;
    char b[24]; snprintf(b, sizeof(b), "%d NM", radar_range());
    lv_label_set_text(s_radarRangeLbl, b);
}

static const char *compass8(int deg) {
    static const char *C[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int d = ((deg % 360) + 360) % 360;
    return C[((d + 22) / 45) % 8];
}

// Populate the nearest-aircraft callout (closest by distance); hidden in table view / no traffic.
static void update_flight_hero() {
    if (!s_flightHero) return;
    if (!s_flightsShowMap || s_flightCount <= 0) {
        lv_obj_add_flag(s_flightHero, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    int best = 0;
    for (int i = 1; i < s_flightCount; i++)
        if (s_flightRows[i].distNm < s_flightRows[best].distNm) best = i;
    const FlightRow &f = s_flightRows[best];

    const char *id = f.tail.length() ? f.tail.c_str()
                   : (f.callsign.length() ? f.callsign.c_str() : "(unknown)");
    lv_label_set_text(s_heroCallsign, id);
    char d[16]; snprintf(d, sizeof(d), "%d NM", f.distNm);
    lv_label_set_text(s_heroDist, d);

    char alt[16];
    if (f.altFt >= 18000) snprintf(alt, sizeof(alt), "FL%03d", f.altFt / 100);
    else                  snprintf(alt, sizeof(alt), "%d ft", f.altFt);
    char spd[12];
    if (f.gs >= 0) snprintf(spd, sizeof(spd), "%d kt", f.gs);
    else           snprintf(spd, sizeof(spd), "-- kt");
    char body[80];
    snprintf(body, sizeof(body), "%s %03d\n%s\n%s\n%s",
             compass8(f.bearing), f.bearing,
             f.type.length() ? f.type.c_str() : "--", alt, spd);
    lv_label_set_text(s_heroBody, body);
    lv_obj_clear_flag(s_flightHero, LV_OBJ_FLAG_HIDDEN);
}

static void flights_toggle_cb(lv_event_t *e) {
    s_flightsShowMap = !s_flightsShowMap;
    if (s_flightsShowMap) {
        lv_obj_clear_flag(s_flightsRadar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_flightsTable, LV_OBJ_FLAG_HIDDEN);
        if (s_zoomIn)  lv_obj_clear_flag(s_zoomIn, LV_OBJ_FLAG_HIDDEN);
        if (s_zoomOut) lv_obj_clear_flag(s_zoomOut, LV_OBJ_FLAG_HIDDEN);
        if (s_radarRangeLbl) lv_obj_clear_flag(s_radarRangeLbl, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_flightsToggleLbl, LV_SYMBOL_LIST " Table");
        update_flight_hero();
    } else {
        lv_obj_add_flag(s_flightsRadar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_flightsTable, LV_OBJ_FLAG_HIDDEN);
        if (s_zoomIn)  lv_obj_add_flag(s_zoomIn, LV_OBJ_FLAG_HIDDEN);
        if (s_zoomOut) lv_obj_add_flag(s_zoomOut, LV_OBJ_FLAG_HIDDEN);
        if (s_radarRangeLbl) lv_obj_add_flag(s_radarRangeLbl, LV_OBJ_FLAG_HIDDEN);
        if (s_flightCard) lv_obj_add_flag(s_flightCard, LV_OBJ_FLAG_HIDDEN);
        if (s_flightHero) lv_obj_add_flag(s_flightHero, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_flightsToggleLbl, LV_SYMBOL_UP " Map");
    }
}

static void card_close_cb(lv_event_t *e) {
    if (s_flightCard) lv_obj_add_flag(s_flightCard, LV_OBJ_FLAG_HIDDEN);
}

static void show_flight_card(int i) {
    if (i < 0 || i >= s_flightCount || !s_flightCard) return;
    const FlightRow &r = s_flightRows[i];
    String s = "#" + String(i + 1) + "  " + r.tail + "\n";
    if (r.callsign.length() && r.callsign != r.tail) s += "Call  " + r.callsign + "\n";
    s += "Type  " + (r.type.length() ? r.type : String("-")) + "\n";
    s += "Alt   " + String(r.altFt) + " ft\n";
    if (r.vrate > -90000)
        s += "V/S   " + String(r.vrate > 0 ? "+" : "") + String(r.vrate) + " fpm\n";
    if (r.gs >= 0) s += "Spd   " + String(r.gs) + " kt\n";
    s += "Dist  " + String(r.distNm) + " NM   Brg " + String(r.bearing) + "\n";
    if (r.track >= 0) s += "Track " + String(r.track) + "\n";
    if (r.squawk.length()) s += "Sqwk  " + r.squawk;
    lv_label_set_text(s_flightCardLbl, s.c_str());
    lv_obj_clear_flag(s_flightCard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_flightCard);
}

static void radar_click_cb(lv_event_t *e) {
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev || !s_flightsRadar) return;
    lv_point_t p; lv_indev_get_point(indev, &p);
    lv_area_t a; lv_obj_get_coords(s_flightsRadar, &a);
    int lx = (int)p.x - a.x1, ly = (int)p.y - a.y1;
    int best = -1; long bestd2 = 26L * 26L;   // tap tolerance ~26 px
    for (int i = 0; i < s_flightCount; i++) {
        if (s_planePx[i] < -1000) continue;
        long dx = lx - s_planePx[i], dy = ly - s_planePy[i];
        long d2 = dx * dx + dy * dy;
        if (d2 <= bestd2) { bestd2 = d2; best = i; }
    }
    if (best >= 0) show_flight_card(best);
    else if (s_flightCard) lv_obj_add_flag(s_flightCard, LV_OBJ_FLAG_HIDDEN);
}

static const int RADAR_RANGES[] = {5, 10, 15, 25, 50, 100, 150, 250};
static void radar_zoom(int dir) {
    int cur = radar_range(), idx = 0, bestd = 100000;
    for (int i = 0; i < 8; i++) { int d = abs(RADAR_RANGES[i] - cur); if (d < bestd) { bestd = d; idx = i; } }
    idx += dir; if (idx < 0) idx = 0; if (idx > 7) idx = 7;
    s_radarRangeNm = RADAR_RANGES[idx];
    if (s_flightCard) lv_obj_add_flag(s_flightCard, LV_OBJ_FLAG_HIDDEN);
    update_radar_range_lbl();
    draw_radar();
    data_request_flights();   // widen/narrow the fetch on the next loop tick
}
static void zoom_in_cb(lv_event_t *e)  { radar_zoom(-1); }   // + = tighter range
static void zoom_out_cb(lv_event_t *e) { radar_zoom(+1); }

// Advances the radar sweep only while the Flights map view is on screen.
static void radar_sweep_timer_cb(lv_timer_t *t) {
    if (ui_active_page() != PAGE_FLIGHTS || !s_flightsShowMap) return;
    if (!s_flightsRadar || lv_obj_has_flag(s_flightsRadar, LV_OBJ_FLAG_HIDDEN)) return;
    s_sweepDeg += 6.0f;
    if (s_sweepDeg >= 360.0f) s_sweepDeg -= 360.0f;
    draw_radar();
}

static void build_flights(lv_obj_t *pg) {
    s_flightsStatus = lv_label_create(pg);
    lv_label_set_text(s_flightsStatus, "Scanning the sky...   data: adsb.fi");
    lv_obj_set_style_text_color(s_flightsStatus, lv_color_hex(0x8b97b0), 0);
    lv_obj_set_style_text_font(s_flightsStatus, &lv_font_montserrat_12, 0);
    lv_obj_align(s_flightsStatus, LV_ALIGN_TOP_LEFT, 0, 8);

    // View toggle (top-right): radar map <-> table.
    lv_obj_t *tbtn = lv_btn_create(pg);
    lv_obj_align(tbtn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(tbtn, lv_color_hex(0x2f7bff), 0);
    lv_obj_add_event_cb(tbtn, flights_toggle_cb, LV_EVENT_CLICKED, nullptr);
    s_flightsToggleLbl = lv_label_create(tbtn);
    lv_label_set_text(s_flightsToggleLbl, LV_SYMBOL_LIST " Table");
    lv_obj_set_style_text_font(s_flightsToggleLbl, &lv_font_montserrat_14, 0);
    lv_obj_center(s_flightsToggleLbl);

    // Radar canvas (default view). Buffer lives in PSRAM.
    static lv_color_t *radarBuf = nullptr;
    if (!radarBuf) radarBuf = (lv_color_t *)heap_caps_malloc(
        RADAR_PX * RADAR_PX * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    s_flightsRadar = lv_canvas_create(pg);
    lv_canvas_set_buffer(s_flightsRadar, radarBuf, RADAR_PX, RADAR_PX, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(s_flightsRadar, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_add_flag(s_flightsRadar, LV_OBJ_FLAG_CLICKABLE);       // tap a plane for details
    lv_obj_add_event_cb(s_flightsRadar, radar_click_cb, LV_EVENT_CLICKED, nullptr);

    // Zoom controls + current range (right gutter, map view only).
    s_radarRangeLbl = lv_label_create(pg);
    lv_obj_set_style_text_color(s_radarRangeLbl, lv_color_hex(0x8b97b0), 0);
    lv_obj_set_style_text_font(s_radarRangeLbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_radarRangeLbl, LV_ALIGN_BOTTOM_RIGHT, 0, -96);

    s_zoomIn = lv_btn_create(pg);
    lv_obj_set_size(s_zoomIn, 44, 40);
    lv_obj_align(s_zoomIn, LV_ALIGN_BOTTOM_RIGHT, 0, -46);
    lv_obj_set_style_bg_color(s_zoomIn, lv_color_hex(0x24406a), 0);
    lv_obj_add_event_cb(s_zoomIn, zoom_in_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *ziLbl = lv_label_create(s_zoomIn);
    lv_label_set_text(ziLbl, LV_SYMBOL_PLUS);
    lv_obj_center(ziLbl);

    s_zoomOut = lv_btn_create(pg);
    lv_obj_set_size(s_zoomOut, 44, 40);
    lv_obj_align(s_zoomOut, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(s_zoomOut, lv_color_hex(0x24406a), 0);
    lv_obj_add_event_cb(s_zoomOut, zoom_out_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *zoLbl = lv_label_create(s_zoomOut);
    lv_label_set_text(zoLbl, LV_SYMBOL_MINUS);
    lv_obj_center(zoLbl);

    // Flight detail card (popup, hidden until a plane is tapped).
    s_flightCard = lv_obj_create(pg);
    lv_obj_set_size(s_flightCard, 214, 208);
    lv_obj_align(s_flightCard, LV_ALIGN_CENTER, 0, -6);
    lv_obj_set_style_bg_color(s_flightCard, lv_color_hex(0x141c2e), 0);
    lv_obj_set_style_border_color(s_flightCard, lv_color_hex(0x2f7bff), 0);
    lv_obj_set_style_border_width(s_flightCard, 2, 0);
    lv_obj_set_style_radius(s_flightCard, 8, 0);
    lv_obj_set_style_pad_all(s_flightCard, 10, 0);
    lv_obj_clear_flag(s_flightCard, LV_OBJ_FLAG_SCROLLABLE);
    s_flightCardLbl = lv_label_create(s_flightCard);
    lv_label_set_text(s_flightCardLbl, "");
    lv_obj_set_style_text_color(s_flightCardLbl, lv_color_hex(0xe6ebf5), 0);
    lv_obj_set_style_text_font(s_flightCardLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_flightCardLbl, 160);
    lv_label_set_long_mode(s_flightCardLbl, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_flightCardLbl, LV_ALIGN_TOP_LEFT, 0, 4);
    lv_obj_t *xBtn = lv_btn_create(s_flightCard);
    lv_obj_set_size(xBtn, 26, 26);
    lv_obj_align(xBtn, LV_ALIGN_TOP_RIGHT, 4, -4);
    lv_obj_set_style_bg_color(xBtn, lv_color_hex(0x2f7bff), 0);
    lv_obj_add_event_cb(xBtn, card_close_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *xLbl = lv_label_create(xBtn);
    lv_label_set_text(xLbl, LV_SYMBOL_CLOSE);
    lv_obj_center(xLbl);
    lv_obj_add_flag(s_flightCard, LV_OBJ_FLAG_HIDDEN);

    // Nearest-aircraft callout (left gutter, shown only in map view).
    s_flightHero = lv_obj_create(pg);
    lv_obj_set_size(s_flightHero, 104, 150);
    lv_obj_align(s_flightHero, LV_ALIGN_TOP_LEFT, 0, 44);
    lv_obj_set_style_bg_color(s_flightHero, lv_color_hex(0x141c2e), 0);
    lv_obj_set_style_border_color(s_flightHero, lv_color_hex(0x2f7bff), 0);
    lv_obj_set_style_border_width(s_flightHero, 1, 0);
    lv_obj_set_style_radius(s_flightHero, 8, 0);
    lv_obj_set_style_pad_all(s_flightHero, 8, 0);
    lv_obj_clear_flag(s_flightHero, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *htag = lv_label_create(s_flightHero);
    lv_label_set_text(htag, "NEAREST");
    lv_obj_set_style_text_font(htag, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(htag, lv_color_hex(0x7fd1ff), 0);
    lv_obj_align(htag, LV_ALIGN_TOP_LEFT, 0, 0);

    s_heroCallsign = lv_label_create(s_flightHero);
    lv_label_set_text(s_heroCallsign, "--");
    lv_obj_set_style_text_font(s_heroCallsign, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_heroCallsign, lv_color_hex(0xe6ebf5), 0);
    lv_obj_align(s_heroCallsign, LV_ALIGN_TOP_LEFT, 0, 18);

    s_heroDist = lv_label_create(s_flightHero);
    lv_label_set_text(s_heroDist, "--");
    lv_obj_set_style_text_font(s_heroDist, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_heroDist, lv_color_hex(0x39d98a), 0);
    lv_obj_align(s_heroDist, LV_ALIGN_TOP_LEFT, 0, 42);

    s_heroBody = lv_label_create(s_flightHero);
    lv_label_set_text(s_heroBody, "");
    lv_obj_set_style_text_font(s_heroBody, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_heroBody, lv_color_hex(0xcdd6ea), 0);
    lv_obj_set_style_text_line_space(s_heroBody, 4, 0);
    lv_obj_set_width(s_heroBody, 88);
    lv_label_set_long_mode(s_heroBody, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_heroBody, LV_ALIGN_TOP_LEFT, 0, 68);

    lv_obj_add_flag(s_flightHero, LV_OBJ_FLAG_HIDDEN);

    // Table (hidden until toggled).
    s_flightsTable = lv_table_create(pg);
    lv_obj_align(s_flightsTable, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_size(s_flightsTable, LV_HOR_RES - SIDEBAR_W - 36, PAGE_H - 72);
    lv_table_set_col_cnt(s_flightsTable, 6);
    lv_table_set_row_cnt(s_flightsTable, 1);
    lv_table_set_cell_value(s_flightsTable, 0, 0, "#");
    lv_table_set_cell_value(s_flightsTable, 0, 1, "Tail");
    lv_table_set_cell_value(s_flightsTable, 0, 2, "Type");
    lv_table_set_cell_value(s_flightsTable, 0, 3, "Alt ft");
    lv_table_set_cell_value(s_flightsTable, 0, 4, "Dist");
    lv_table_set_cell_value(s_flightsTable, 0, 5, "Brg");
    lv_table_set_col_width(s_flightsTable, 0, 44);
    lv_table_set_col_width(s_flightsTable, 1, 120);
    lv_table_set_col_width(s_flightsTable, 2, 80);
    lv_table_set_col_width(s_flightsTable, 3, 100);
    lv_table_set_col_width(s_flightsTable, 4, 90);
    lv_table_set_col_width(s_flightsTable, 5, 70);

    lv_obj_add_flag(s_flightsTable, LV_OBJ_FLAG_HIDDEN);   // map is the default view
    update_radar_range_lbl();
    draw_radar();

    if (!s_radarTimer) s_radarTimer = lv_timer_create(radar_sweep_timer_cb, 70, nullptr);
}

// ------------------------------------------------------------ tickers page ---
static const char *TF_LABELS[5] = {"1D", "5D", "1M", "6M", "1Y"};
#define SP_W  180
#define SP_H  60
#define TK_CARD_H 92
#define BAR_X 512
#define BAR_Y 46
#define BAR_W 88

int ui_ticker_tf_index() { return s_tfIndex; }

static void tf_restyle() {
    for (int i = 0; i < 5; i++) {
        if (!s_tfBtn[i]) continue;
        bool on = (i == s_tfIndex);
        lv_obj_set_style_bg_color(s_tfBtn[i], lv_color_hex(on ? 0x2d6cdf : 0x1c2740), 0);
    }
}

static void tf_click_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == s_tfIndex) return;
    s_tfIndex = idx;
    settings_set_ticker_tf((uint8_t)idx);   // remember across reboots
    tf_restyle();
    data_request_tickers();          // refetch this timeframe on the next tick
}

static void draw_sparkline(int i, TickerRow &r) {
    lv_obj_t *cv = s_tkSpark[i];
    if (!cv) return;
    lv_canvas_fill_bg(cv, lv_color_hex(0x141c2e), LV_OPA_COVER);
    int len = r.sparkLen;
    if (len < 2) return;
    double span = r.winHi - r.winLo;
    if (span < 1e-9) span = 1.0;
    static lv_point_t pts[SPARK_N];
    const int m = 4;                 // vertical margin
    for (int j = 0; j < len; j++) {
        int x = (int)((int64_t)j * (SP_W - 1) / (len - 1));
        double f = (r.spark[j] - r.winLo) / span;
        int y = (SP_H - 1 - m) - (int)(f * (SP_H - 1 - 2 * m));
        pts[j].x = (lv_coord_t)x;
        pts[j].y = (lv_coord_t)y;
    }
    bool up = r.changeAbs >= 0;

    // Translucent area fill under the curve (per-column so concave shapes fill right).
    lv_draw_rect_dsc_t fill; lv_draw_rect_dsc_init(&fill);
    fill.bg_color = lv_color_hex(up ? 0x39d98a : 0xff5c5c);
    fill.bg_opa   = LV_OPA_20;
    const int baseY = SP_H - 1;
    for (int s = 0; s < len - 1; s++) {
        int xa = pts[s].x, ya = pts[s].y, xb = pts[s + 1].x, yb = pts[s + 1].y;
        if (xb <= xa) continue;
        for (int x = (s == 0 ? xa : xa + 1); x <= xb; x++) {
            int y = ya + (yb - ya) * (x - xa) / (xb - xa);
            if (y < baseY) lv_canvas_draw_rect(cv, x, y, 1, baseY - y + 1, &fill);
        }
    }

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(up ? 0x39d98a : 0xff5c5c);
    dsc.width = 2;
    dsc.round_start = dsc.round_end = 1;
    lv_canvas_draw_line(cv, pts, len, &dsc);
}

static void build_tickers(lv_obj_t *pg) {
    const int CW = LV_HOR_RES - SIDEBAR_W - 36;

    s_tickersStatus = lv_label_create(pg);
    lv_label_set_text(s_tickersStatus, "Loading quotes...");
    lv_obj_set_style_text_color(s_tickersStatus, lv_color_hex(0x8b97b0), 0);
    lv_obj_set_style_text_font(s_tickersStatus, &lv_font_montserrat_12, 0);
    lv_obj_align(s_tickersStatus, LV_ALIGN_TOP_LEFT, 0, 6);

    // Timeframe selector row (top-right).
    const int bw = 54, bh = 30, bgap = 6;
    for (int i = 0; i < 5; i++) {
        lv_obj_t *b = lv_obj_create(pg);
        lv_obj_set_size(b, bw, bh);
        lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -(4 - i) * (bw + bgap), 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, tf_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, TF_LABELS[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xe6ebf5), 0);
        lv_obj_center(l);
        s_tfBtn[i] = b;
    }
    s_tfIndex = settings().tickerTf;
    if (s_tfIndex < 0 || s_tfIndex > 4) s_tfIndex = 0;
    tf_restyle();

    // Scrollable card list.
    s_tkList = lv_obj_create(pg);
    lv_obj_set_size(s_tkList, CW, PAGE_H - 44);
    lv_obj_align(s_tkList, LV_ALIGN_TOP_LEFT, 0, 40);
    lv_obj_set_style_bg_opa(s_tkList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tkList, 0, 0);
    lv_obj_set_style_pad_all(s_tkList, 0, 0);
    lv_obj_set_scroll_dir(s_tkList, LV_DIR_VER);

    static lv_color_t *spBuf[8] = {nullptr};
    const int cardW = CW - 12;       // leave room for the scrollbar
    for (int i = 0; i < 8; i++) {
        lv_obj_t *c = lv_obj_create(s_tkList);
        lv_obj_set_size(c, cardW, TK_CARD_H);
        lv_obj_set_pos(c, 0, i * (TK_CARD_H + 8));
        lv_obj_set_style_bg_color(c, lv_color_hex(0x141c2e), 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_set_style_radius(c, 8, 0);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        s_tkCard[i] = c;

        s_tkSym[i] = lv_label_create(c);
        lv_obj_set_style_text_font(s_tkSym[i], &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(s_tkSym[i], lv_color_hex(0xffffff), 0);
        lv_obj_align(s_tkSym[i], LV_ALIGN_TOP_LEFT, 6, 4);

        s_tkName[i] = lv_label_create(c);
        lv_obj_set_style_text_font(s_tkName[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_tkName[i], lv_color_hex(0x8b97b0), 0);
        lv_label_set_long_mode(s_tkName[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_tkName[i], 150);
        lv_obj_align(s_tkName[i], LV_ALIGN_TOP_LEFT, 6, 42);

        s_tkState[i] = lv_label_create(c);
        lv_obj_set_style_text_font(s_tkState[i], &lv_font_montserrat_12, 0);
        lv_obj_align(s_tkState[i], LV_ALIGN_TOP_LEFT, 6, 64);

        s_tkPrice[i] = lv_label_create(c);
        lv_obj_set_style_text_font(s_tkPrice[i], &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(s_tkPrice[i], lv_color_hex(0xe6ebf5), 0);
        lv_obj_align(s_tkPrice[i], LV_ALIGN_TOP_LEFT, 172, 8);

        s_tkChange[i] = lv_label_create(c);
        lv_obj_set_style_text_font(s_tkChange[i], &lv_font_montserrat_16, 0);
        lv_obj_align(s_tkChange[i], LV_ALIGN_TOP_LEFT, 172, 50);

        if (!spBuf[i]) spBuf[i] = (lv_color_t *)heap_caps_malloc(
            SP_W * SP_H * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
        s_tkSpark[i] = lv_canvas_create(c);
        lv_canvas_set_buffer(s_tkSpark[i], spBuf[i], SP_W, SP_H, LV_IMG_CF_TRUE_COLOR);
        lv_obj_align(s_tkSpark[i], LV_ALIGN_TOP_LEFT, 322, 14);
        lv_canvas_fill_bg(s_tkSpark[i], lv_color_hex(0x141c2e), LV_OPA_COVER);

        s_tkBar[i] = lv_obj_create(c);
        lv_obj_set_size(s_tkBar[i], BAR_W, 6);
        lv_obj_set_style_bg_color(s_tkBar[i], lv_color_hex(0x2a3550), 0);
        lv_obj_set_style_border_width(s_tkBar[i], 0, 0);
        lv_obj_set_style_radius(s_tkBar[i], 3, 0);
        lv_obj_clear_flag(s_tkBar[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(s_tkBar[i], LV_ALIGN_TOP_LEFT, BAR_X, BAR_Y);

        s_tkBarDot[i] = lv_obj_create(c);
        lv_obj_set_size(s_tkBarDot[i], 10, 10);
        lv_obj_set_style_bg_color(s_tkBarDot[i], lv_color_hex(0xffffff), 0);
        lv_obj_set_style_border_width(s_tkBarDot[i], 0, 0);
        lv_obj_set_style_radius(s_tkBarDot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(s_tkBarDot[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(s_tkBarDot[i], LV_ALIGN_TOP_LEFT, BAR_X, BAR_Y - 2);

        s_tkLo[i] = lv_label_create(c);
        lv_obj_set_style_text_font(s_tkLo[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_tkLo[i], lv_color_hex(0x8b97b0), 0);
        lv_obj_align(s_tkLo[i], LV_ALIGN_TOP_LEFT, BAR_X, BAR_Y + 12);

        s_tkHi[i] = lv_label_create(c);
        lv_obj_set_style_text_font(s_tkHi[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_tkHi[i], lv_color_hex(0x8b97b0), 0);
        lv_obj_align(s_tkHi[i], LV_ALIGN_TOP_LEFT, BAR_X, BAR_Y + 26);

        lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    }
}

// -- Config / Info page: shows WHERE the web config lives (never edits on-device) --
static void build_config(lv_obj_t *pg) {
    s_cfgState = lv_label_create(pg);
    lv_obj_set_style_text_font(s_cfgState, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_cfgState, lv_color_hex(0x7fd1ff), 0);
    lv_obj_align(s_cfgState, LV_ALIGN_TOP_LEFT, 0, 8);

    s_cfgDetails = lv_label_create(pg);
    lv_label_set_long_mode(s_cfgDetails, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_cfgDetails, LV_HOR_RES - SIDEBAR_W - 220);
    lv_obj_set_style_text_color(s_cfgDetails, lv_color_hex(0xcdd6ea), 0);
    lv_obj_set_style_text_font(s_cfgDetails, &lv_font_montserrat_16, 0);
    lv_obj_align(s_cfgDetails, LV_ALIGN_TOP_LEFT, 0, 44);

    // QR canvas (right side). Sized for a version-3 code scaled x5 + quiet zone.
    static lv_color_t *qrBuf = nullptr;
    const int QR_PX = 210;
    if (!qrBuf) qrBuf = (lv_color_t *)heap_caps_malloc(
        QR_PX * QR_PX * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    s_cfgQr = lv_canvas_create(pg);
    lv_canvas_set_buffer(s_cfgQr, qrBuf, QR_PX, QR_PX, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(s_cfgQr, LV_ALIGN_TOP_RIGHT, 0, 8);
}

// Render a URL into the config-page QR canvas.
static void render_qr(const String &text) {
    if (!s_cfgQr) return;
    const int QR_PX = 210;
    lv_canvas_fill_bg(s_cfgQr, lv_color_white(), LV_OPA_COVER);

    QRCode qr;
    // qrcode_getBufferSize(4) = ((4*4+17)^2 + 7) / 8 = 137 bytes (runtime fn, not constexpr).
    static uint8_t qrData[137];
    if (qrcode_initText(&qr, qrData, 4, ECC_MEDIUM, text.c_str()) != 0) return;

    const int quiet = 4;
    int modules = qr.size + quiet * 2;
    int scale = QR_PX / modules;
    if (scale < 1) scale = 1;
    int origin = (QR_PX - modules * scale) / 2;

    for (int y = 0; y < qr.size; y++) {
        for (int x = 0; x < qr.size; x++) {
            if (!qrcode_getModule(&qr, x, y)) continue;
            int px = origin + (x + quiet) * scale;
            int py = origin + (y + quiet) * scale;
            for (int dy = 0; dy < scale; dy++)
                for (int dx = 0; dx < scale; dx++)
                    lv_canvas_set_px_color(s_cfgQr, px + dx, py + dy, lv_color_black());
        }
    }
}

// ---------------------------------------------------------------------------
// Top bar + launcher
// ---------------------------------------------------------------------------
static void topbar_back_cb(lv_event_t *e) { ui_show_page(PAGE_LAUNCHER); }

// Map an RSSI (dBm) to a 1-4 bar count + a quality color.
static int rssi_to_bars(int rssi, uint32_t *col) {
    if      (rssi >= -55) { *col = 0x39d98a; return 4; }
    else if (rssi >= -65) { *col = 0x39d98a; return 3; }
    else if (rssi >= -75) { *col = 0xffb347; return 2; }
    else                  { *col = 0xff5c5c; return 1; }
}

// Re-anchor the clock cluster right-to-left so the date never clips as its
// width changes: [ date ][ time ][ wifi ] pinned to the right edge.
static void topbar_relayout() {
    if (!s_topWifi) return;
    lv_obj_align(s_topWifi, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_align_to(s_clockTime, s_topWifi, LV_ALIGN_OUT_LEFT_MID, -12, 0);
    lv_obj_align_to(s_clockDate, s_clockTime, LV_ALIGN_OUT_LEFT_MID, -10, 0);
}

// Light the top-bar Wi-Fi bars per current signal (grey when offline).
static void update_topbar_wifi() {
    if (!s_topWifiBar[0]) return;
    int bars = 0; uint32_t col = 0x8b97b0;
    if (net_state() == NetState::Connected) bars = rssi_to_bars((int)WiFi.RSSI(), &col);
    for (int i = 0; i < 4; i++)
        lv_obj_set_style_bg_color(s_topWifiBar[i], lv_color_hex(i < bars ? col : 0x2a3550), 0);
}

// Always-visible top strip: back/home button + panel title (left), global clock (right).
static void build_topbar(lv_obj_t *scr) {
    s_topbar = lv_obj_create(scr);
    lv_obj_set_size(s_topbar, LV_HOR_RES, TOPBAR_H);
    lv_obj_set_pos(s_topbar, 0, 0);
    lv_obj_set_style_bg_color(s_topbar, lv_color_hex(0x161d2e), 0);
    lv_obj_set_style_border_width(s_topbar, 0, 0);
    lv_obj_set_style_radius(s_topbar, 0, 0);
    lv_obj_set_style_pad_all(s_topbar, 0, 0);
    lv_obj_clear_flag(s_topbar, LV_OBJ_FLAG_SCROLLABLE);

    s_topbarBack = lv_btn_create(s_topbar);
    lv_obj_set_size(s_topbarBack, 58, TOPBAR_H - 10);
    lv_obj_align(s_topbarBack, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_style_bg_color(s_topbarBack, lv_color_hex(0x24406a), 0);
    lv_obj_set_style_radius(s_topbarBack, 8, 0);
    lv_obj_add_event_cb(s_topbarBack, topbar_back_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *bl = lv_label_create(s_topbarBack);
    lv_label_set_text(bl, LV_SYMBOL_HOME);
    lv_obj_center(bl);

    s_topbarTitle = lv_label_create(s_topbar);
    lv_label_set_text(s_topbarTitle, PAGE_TITLES[PAGE_LAUNCHER]);
    lv_obj_set_style_text_font(s_topbarTitle, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_topbarTitle, lv_color_hex(0xe6ebf5), 0);
    lv_obj_align(s_topbarTitle, LV_ALIGN_LEFT_MID, 76, 0);

    // Wi-Fi signal bars, pinned to the far right.
    s_topWifi = lv_obj_create(s_topbar);
    lv_obj_set_size(s_topWifi, 26, 22);
    lv_obj_set_style_bg_opa(s_topWifi, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_topWifi, 0, 0);
    lv_obj_set_style_pad_all(s_topWifi, 0, 0);
    lv_obj_clear_flag(s_topWifi, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 4; i++) {
        int bw = 4, bgap = 3, h = 8 + i * 4;   // ascending 8/12/16/20 px
        s_topWifiBar[i] = lv_obj_create(s_topWifi);
        lv_obj_set_size(s_topWifiBar[i], bw, h);
        lv_obj_align(s_topWifiBar[i], LV_ALIGN_BOTTOM_LEFT, i * (bw + bgap), 0);
        lv_obj_set_style_border_width(s_topWifiBar[i], 0, 0);
        lv_obj_set_style_radius(s_topWifiBar[i], 1, 0);
        lv_obj_set_style_bg_color(s_topWifiBar[i], lv_color_hex(0x2a3550), 0);
        lv_obj_clear_flag(s_topWifiBar[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    // Global clock (right): muted date + bold time, same font so it reads cleanly.
    // Positioned in topbar_relayout() right-to-left so the date never clips.
    s_clockDate = lv_label_create(s_topbar);
    lv_label_set_text(s_clockDate, "");
    lv_obj_set_style_text_font(s_clockDate, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_clockDate, lv_color_hex(0x8b97b0), 0);

    s_clockTime = lv_label_create(s_topbar);
    lv_label_set_text(s_clockTime, "--:--");
    lv_obj_set_style_text_font(s_clockTime, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_clockTime, lv_color_hex(0xffffff), 0);

    topbar_relayout();
    update_topbar_wifi();
}

// Defer the page switch briefly so the tile's release glow is visible before we navigate.
static void launcher_nav_cb(lv_timer_t *t) {
    ui_show_page((Page)(intptr_t)t->user_data);
}

static void launcher_tile_cb(lv_event_t *e) {
    Page p = (Page)(intptr_t)lv_event_get_user_data(e);
    lv_timer_t *t = lv_timer_create(launcher_nav_cb, 160, (void *)(intptr_t)p);
    lv_timer_set_repeat_count(t, 1);
}

// Home tile grid: one tile per panel (everything except the launcher itself).
static void build_launcher(lv_obj_t *pg) {
    lv_obj_set_flex_flow(pg, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(pg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(pg, 16, 0);
    lv_obj_set_style_pad_column(pg, 16, 0);
    lv_obj_clear_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

    // Shared tile styles: darken + accent outline glow on press, eased for a tactile feel.
    // (Avoids transform_zoom, which renders blank on this full-refresh RGB panel.)
    // Fast ramp-up on press, slow fade-out on release so the glow lingers into the nav.
    static lv_style_t s_tileDef, s_tilePr;
    static lv_style_transition_dsc_t s_tileTrIn, s_tileTrOut;
    static bool s_tileStyleInit = false;
    if (!s_tileStyleInit) {
        static const lv_style_prop_t props[] = { LV_STYLE_BG_COLOR, LV_STYLE_OUTLINE_WIDTH,
                                                 LV_STYLE_OUTLINE_OPA, LV_STYLE_PROP_INV };
        lv_style_transition_dsc_init(&s_tileTrIn,  props, lv_anim_path_ease_out,  80, 0, NULL);
        lv_style_transition_dsc_init(&s_tileTrOut, props, lv_anim_path_ease_out, 320, 0, NULL);
        lv_style_init(&s_tileDef);
        lv_style_set_bg_color(&s_tileDef, lv_color_hex(0x161d2e));
        lv_style_set_outline_color(&s_tileDef, lv_color_hex(0x7fb0ff));
        lv_style_set_outline_width(&s_tileDef, 0);
        lv_style_set_outline_opa(&s_tileDef, LV_OPA_TRANSP);
        lv_style_set_outline_pad(&s_tileDef, 0);
        lv_style_set_transition(&s_tileDef, &s_tileTrOut);   // fade-out on release
        lv_style_init(&s_tilePr);
        lv_style_set_bg_color(&s_tilePr, lv_color_hex(0x24406a));
        lv_style_set_outline_width(&s_tilePr, 3);
        lv_style_set_outline_opa(&s_tilePr, LV_OPA_COVER);
        lv_style_set_transition(&s_tilePr, &s_tileTrIn);     // quick light-up on press
        s_tileStyleInit = true;
    }

    for (int p = PAGE_LAUNCHER + 1; p < PAGE_COUNT; p++) {
        lv_obj_t *tile = lv_obj_create(pg);
        lv_obj_set_size(tile, 168, 132);
        lv_obj_add_style(tile, &s_tileDef, 0);
        lv_obj_add_style(tile, &s_tilePr, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_radius(tile, 12, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tile, launcher_tile_cb, LV_EVENT_CLICKED, (void *)(intptr_t)p);

        lv_obj_t *ic = lv_label_create(tile);
        lv_label_set_text(ic, PAGE_ICONS[p]);
        lv_obj_set_style_text_font(ic, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(ic, lv_color_hex(0x7fb0ff), 0);
        lv_obj_align(ic, LV_ALIGN_CENTER, 0, -18);

        lv_obj_t *lb = lv_label_create(tile);
        lv_label_set_text(lb, PAGE_TITLES[p]);
        lv_obj_set_style_text_font(lb, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lb, lv_color_hex(0xe6ebf5), 0);
        lv_obj_align(lb, LV_ALIGN_CENTER, 0, 24);
    }
}

// ---------------------------------------------------------------------------
// Photo frame — a full-bleed LVGL canvas backed by the PSRAM RGB565 buffer that
// the data layer decodes JPEGs into. A status label overlays load/error text.
// ---------------------------------------------------------------------------
static void build_photo(lv_obj_t *pg) {
    lv_obj_set_style_pad_all(pg, 0, 0);            // full-bleed, no inset
    lv_obj_set_style_bg_color(pg, lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

    photo_init();

    s_photoCanvas = lv_canvas_create(pg);
    lv_canvas_set_buffer(s_photoCanvas, photo_buffer(),
                         photo_width(), photo_height(), LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(s_photoCanvas, LV_ALIGN_TOP_LEFT, 0, 0);

    s_photoStatus = lv_label_create(pg);
    lv_label_set_text(s_photoStatus, "Loading nature photo\u2026");
    lv_obj_set_style_text_font(s_photoStatus, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_photoStatus, lv_color_hex(0xe6ebf5), 0);
    lv_obj_set_style_bg_color(s_photoStatus, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_photoStatus, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(s_photoStatus, 8, 0);
    lv_obj_set_style_radius(s_photoStatus, 6, 0);
    lv_obj_align(s_photoStatus, LV_ALIGN_CENTER, 0, 0);
}

void ui_photo_refresh(bool ok, const char *status) {
    if (!s_photoCanvas) return;
    s_photoHave = true;
    page_set_loading(PAGE_PHOTO, false);
    if (ok) {
        if (s_photoStatus) lv_obj_add_flag(s_photoStatus, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(s_photoCanvas);
    } else if (s_photoStatus) {
        lv_label_set_text(s_photoStatus, status && status[0] ? status : "Photo unavailable");
        lv_obj_clear_flag(s_photoStatus, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(s_photoStatus, LV_ALIGN_CENTER, 0, 0);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
// Format a duration in seconds to a compact human string ("30 min", "1h 15m").
static void cal_fmt_dur(long sec, char *out, size_t n) {
    if (sec <= 0) { out[0] = 0; return; }
    long m = sec / 60;
    if      (m < 60)      snprintf(out, n, "%ld min", m);
    else if (m % 60 == 0) snprintf(out, n, "%ldh", m / 60);
    else if (m < 1440)    snprintf(out, n, "%ldh %ldm", m / 60, m % 60);
    else                  snprintf(out, n, "%ld day%s", m / 1440, m / 1440 == 1 ? "" : "s");
}

static void cal_close_cb(lv_event_t *e) {
    if (s_calCard) lv_obj_add_flag(s_calCard, LV_OBJ_FLAG_HIDDEN);
}

// Populate and open the detail popup for cached event index `idx`.
static void cal_show_detail(int idx) {
    if (!s_calCard || idx < 0 || idx >= s_calAllCount) return;
    const CalEvent &e = s_calAll[idx];
    lv_label_set_text(s_calCardTitle, e.title.length() ? e.title.c_str() : "(no title)");

    time_t st = (time_t)e.start; struct tm t; localtime_r(&st, &t);
    time_t now = time(nullptr);
    bool h24 = settings().use24hClock;

    char body[256]; int p = 0;
    char dbuf[48]; strftime(dbuf, sizeof(dbuf), "%A, %B %d", &t);
    p += snprintf(body + p, sizeof(body) - p, "%s\n", dbuf);

    if (e.allDay) {
        p += snprintf(body + p, sizeof(body) - p, "All day\n");
    } else {
        struct tm s1; localtime_r(&st, &s1);
        char t1[14];
        if (h24) snprintf(t1, sizeof(t1), "%02d:%02d", s1.tm_hour, s1.tm_min);
        else { int h = s1.tm_hour % 12; if (!h) h = 12;
               snprintf(t1, sizeof(t1), "%d:%02d %s", h, s1.tm_min, s1.tm_hour < 12 ? "AM" : "PM"); }
        if (e.end > e.start) {
            time_t et = (time_t)e.end; struct tm s2; localtime_r(&et, &s2);
            char t2[14];
            if (h24) snprintf(t2, sizeof(t2), "%02d:%02d", s2.tm_hour, s2.tm_min);
            else { int h = s2.tm_hour % 12; if (!h) h = 12;
                   snprintf(t2, sizeof(t2), "%d:%02d %s", h, s2.tm_min, s2.tm_hour < 12 ? "AM" : "PM"); }
            char dur[20]; cal_fmt_dur((long)e.end - (long)e.start, dur, sizeof(dur));
            p += snprintf(body + p, sizeof(body) - p, "%s - %s  (%s)\n", t1, t2, dur);
        } else {
            p += snprintf(body + p, sizeof(body) - p, "%s\n", t1);
        }
    }

    if (now > 100000) {
        long diff = (long)e.start - (long)now;
        char rel[48];
        if (!e.allDay && e.end > 0 && now >= e.start && now < e.end) {
            long left = (long)e.end - (long)now;
            if (left < 3600) snprintf(rel, sizeof(rel), "In progress - ends in %ld min", left / 60);
            else snprintf(rel, sizeof(rel), "In progress - ends in %ldh %02ldm", left / 3600, (left % 3600) / 60);
        } else if (diff <= 60 && diff > -60) snprintf(rel, sizeof(rel), "Starting now");
        else if (diff < 0) {
            long ago = -diff;
            if      (ago < 3600)  snprintf(rel, sizeof(rel), "Started %ld min ago", ago / 60);
            else if (ago < 86400) snprintf(rel, sizeof(rel), "Started %ldh ago", ago / 3600);
            else                  snprintf(rel, sizeof(rel), "%ld days ago", ago / 86400);
        } else if (diff < 3600)  snprintf(rel, sizeof(rel), "Starts in %ld min", diff / 60);
        else if (diff < 86400)   snprintf(rel, sizeof(rel), "Starts in %ldh %02ldm", diff / 3600, (diff % 3600) / 60);
        else                     snprintf(rel, sizeof(rel), "Starts in %ld days", diff / 86400);
        p += snprintf(body + p, sizeof(body) - p, "%s\n", rel);
    }

    if (e.location.length())
        snprintf(body + p, sizeof(body) - p, LV_SYMBOL_GPS " %s", e.location.c_str());

    lv_label_set_text(s_calCardBody, body);
    lv_obj_clear_flag(s_calCard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_calCard);
}

static void cal_row_cb(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < UI_MAX_EVENTS) cal_show_detail(s_calRowMap[i]);
}

static void cal_hero_cb(lv_event_t *e) { cal_show_detail(s_calHeroIdx); }

// ---------------------------------------------------------------------------
// Calendar view engine (List / Day / Week / Month + period navigation)
// ---------------------------------------------------------------------------
static void update_cal_hero();                       // hero repaint shares the render path
static const char *CAL_VIEW_LABELS[4] = {"List", "Day", "Week", "Month"};

static void cal_view_restyle() {
    for (int i = 0; i < 4; i++) {
        if (!s_calViewBtn[i]) continue;
        bool on = (i == s_calView);
        lv_obj_set_style_bg_color(s_calViewBtn[i], lv_color_hex(on ? 0x2d6cdf : 0x1c2740), 0);
    }
}

// Fill one visible list row from a cached event (weekday + date + time, plus title).
static void cal_fill_row(int r, const CalEvent &e) {
    static const char *WD[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    bool h24 = settings().use24hClock;
    time_t t = (time_t)e.start; struct tm tm; localtime_r(&t, &tm);
    int wd = tm.tm_wday; if (wd < 0 || wd > 6) wd = 0;
    char when[40];
    if (e.allDay)
        snprintf(when, sizeof(when), "%s %d/%d\nAll day", WD[wd], tm.tm_mon + 1, tm.tm_mday);
    else if (h24)
        snprintf(when, sizeof(when), "%s %d/%d\n%02d:%02d", WD[wd], tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    else { int h12 = tm.tm_hour % 12; if (!h12) h12 = 12;
        snprintf(when, sizeof(when), "%s %d/%d\n%d:%02d %s", WD[wd], tm.tm_mon + 1, tm.tm_mday,
                 h12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM"); }
    lv_label_set_text(s_calWhen[r], when);
    lv_label_set_text(s_calTitle[r], e.title.length() ? e.title.c_str() : "(no title)");
    lv_obj_clear_flag(s_calRow[r], LV_OBJ_FLAG_HIDDEN);
}

// Advance an instant by whole months, staying mid-month/noon to dodge DST + rollover.
static long cal_add_months(long t, int delta) {
    time_t tt = (time_t)t; struct tm tm; localtime_r(&tt, &tm);
    tm.tm_mon += delta; tm.tm_mday = 15; tm.tm_hour = 12; tm.tm_min = tm.tm_sec = 0;
    return (long)mktime(&tm);
}

// Compute the [lo,hi) epoch window and a header caption for the current view/anchor.
static void cal_period(long &lo, long &hi, char *label, size_t ln) {
    static const char *MON[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const char *WD[7]   = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    time_t base = (s_calAnchor > 100000) ? (time_t)s_calAnchor : time(nullptr);
    struct tm tm; localtime_r(&base, &tm);
    if (s_calView == CAL_DAY) {
        struct tm d = tm; d.tm_hour = d.tm_min = d.tm_sec = 0;
        lo = (long)mktime(&d); hi = lo + 86400L;
        snprintf(label, ln, "%s, %s %d", WD[d.tm_wday], MON[d.tm_mon], d.tm_mday);
    } else if (s_calView == CAL_WEEK) {
        struct tm d = tm; d.tm_hour = d.tm_min = d.tm_sec = 0;
        long day0 = (long)mktime(&d);
        lo = day0 - d.tm_wday * 86400L; hi = lo + 7 * 86400L;
        time_t a = (time_t)lo, b = (time_t)(hi - 86400L);
        struct tm ta, tb; localtime_r(&a, &ta); localtime_r(&b, &tb);
        snprintf(label, ln, "%s %d - %s %d", MON[ta.tm_mon], ta.tm_mday, MON[tb.tm_mon], tb.tm_mday);
    } else {   // month window (also used to caption Month view)
        struct tm f = tm; f.tm_mday = 1; f.tm_hour = f.tm_min = f.tm_sec = 0;
        lo = (long)mktime(&f);
        struct tm nx = f; nx.tm_mon += 1; hi = (long)mktime(&nx);
        snprintf(label, ln, "%s %d", MON[f.tm_mon], f.tm_year + 1900);
    }
}

// Paint the 42-cell month grid: day numbers, in/out-of-month shading, today accent,
// and a per-day event count. Also records each cell's noon epoch for tap-to-Day.
static void cal_render_month() {
    time_t base = (s_calAnchor > 100000) ? (time_t)s_calAnchor : time(nullptr);
    struct tm fm; localtime_r(&base, &fm);
    fm.tm_mday = 1; fm.tm_hour = 12; fm.tm_min = fm.tm_sec = 0;
    mktime(&fm);                                     // normalize -> tm_wday of the 1st
    int firstWd = fm.tm_wday, thisMon = fm.tm_mon;

    time_t now = time(nullptr); struct tm nm; localtime_r(&now, &nm);
    auto serial = [](const struct tm &t) { return (t.tm_year + 1900) * 400 + t.tm_mon * 31 + t.tm_mday; };
    int todaySerial = serial(nm);

    int cellSerial[42];
    for (int i = 0; i < 42; i++) {
        struct tm c = fm; c.tm_mday = 1 - firstWd + i; c.tm_hour = 12; c.tm_min = c.tm_sec = 0;
        time_t ct = mktime(&c);                       // normalizes across month bounds
        s_calCellEpoch[i] = (long)ct;
        cellSerial[i] = serial(c);
        bool inMonth = (c.tm_mon == thisMon);
        bool today   = (cellSerial[i] == todaySerial);
        char dn[6]; snprintf(dn, sizeof(dn), "%d", c.tm_mday);
        lv_label_set_text(s_calCellNum[i], dn);
        lv_obj_set_style_text_color(s_calCellNum[i], lv_color_hex(inMonth ? 0xe6ebf5 : 0x54607a), 0);
        lv_obj_set_style_bg_color(s_calCell[i],
            lv_color_hex(today ? 0x1f3358 : (inMonth ? 0x141c2e : 0x0f1524)), 0);
        lv_obj_set_style_border_width(s_calCell[i], today ? 2 : 0, 0);
    }

    int cnt[42]; for (int i = 0; i < 42; i++) cnt[i] = 0;
    for (int i = 0; i < s_calAllCount; i++) {
        time_t et = (time_t)s_calAll[i].start; struct tm em; localtime_r(&et, &em);
        int es = serial(em);
        for (int k = 0; k < 42; k++) if (cellSerial[k] == es) { cnt[k]++; break; }
    }
    for (int k = 0; k < 42; k++) {
        if (cnt[k] > 0) { char b[8]; snprintf(b, sizeof(b), "%d", cnt[k]);
                          lv_label_set_text(s_calCellCnt[k], b); }
        else            lv_label_set_text(s_calCellCnt[k], "");
    }
}

// Filter the cache into the row pool (List/Day/Week) or the grid (Month), and update
// the header (status caption for List, period navigator for the dated views).
static void cal_render() {
    if (!s_calList) return;
    cal_view_restyle();

    bool isList  = (s_calView == CAL_LIST);
    bool isMonth = (s_calView == CAL_MONTH);

    if (s_calStatus) { if (isList) lv_obj_clear_flag(s_calStatus, LV_OBJ_FLAG_HIDDEN);
                       else        lv_obj_add_flag(s_calStatus, LV_OBJ_FLAG_HIDDEN); }
    if (s_calNav)    { if (isList) lv_obj_add_flag(s_calNav, LV_OBJ_FLAG_HIDDEN);
                       else        lv_obj_clear_flag(s_calNav, LV_OBJ_FLAG_HIDDEN); }
    for (int i = 0; i < 7; i++) if (s_calDow[i]) {
        if (isMonth) lv_obj_clear_flag(s_calDow[i], LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(s_calDow[i], LV_OBJ_FLAG_HIDDEN); }
    if (s_calGrid) { if (isMonth) lv_obj_clear_flag(s_calGrid, LV_OBJ_FLAG_HIDDEN);
                     else         lv_obj_add_flag(s_calGrid, LV_OBJ_FLAG_HIDDEN); }

    if (isMonth) {
        lv_obj_add_flag(s_calList, LV_OBJ_FLAG_HIDDEN);
        if (s_calHero) lv_obj_add_flag(s_calHero, LV_OBJ_FLAG_HIDDEN);
        long lo, hi; char hdr[48]; cal_period(lo, hi, hdr, sizeof(hdr));
        if (s_calPeriodLbl) lv_label_set_text(s_calPeriodLbl, hdr);
        cal_render_month();
        return;
    }

    lv_obj_clear_flag(s_calList, LV_OBJ_FLAG_HIDDEN);
    if (isList) {
        lv_obj_set_y(s_calList, 132);
        lv_obj_set_height(s_calList, PAGE_H - 132 - 8);
    } else {
        if (s_calHero) lv_obj_add_flag(s_calHero, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(s_calList, 44);
        lv_obj_set_height(s_calList, PAGE_H - 44 - 8);
    }

    long lo = 0, hi = 0; char hdr[48] = "";
    if (!isList) { cal_period(lo, hi, hdr, sizeof(hdr));
                   if (s_calPeriodLbl) lv_label_set_text(s_calPeriodLbl, hdr); }

    int row = 0;
    for (int i = 0; i < s_calAllCount && row < UI_MAX_EVENTS; i++) {
        if (!isList) { long s = (long)s_calAll[i].start; if (s < lo || s >= hi) continue; }
        cal_fill_row(row, s_calAll[i]);
        s_calRowMap[row] = i;
        row++;
    }
    for (int r = row; r < UI_MAX_EVENTS; r++)
        if (s_calRow[r]) lv_obj_add_flag(s_calRow[r], LV_OBJ_FLAG_HIDDEN);

    if (isList) {
        char st[48];
        snprintf(st, sizeof(st), "%d upcoming event%s", s_calAllCount, s_calAllCount == 1 ? "" : "s");
        if (s_calStatus) lv_label_set_text(s_calStatus, st);
        update_cal_hero();
    } else if (row == 0 && s_calPeriodLbl) {
        char e2[64]; snprintf(e2, sizeof(e2), "%s  -  no events", hdr);
        lv_label_set_text(s_calPeriodLbl, e2);
    }
}

static void cal_view_click_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == s_calView) return;
    s_calView = idx;
    settings_set_cal_view((uint8_t)idx);    // remember across reboots
    if (idx != CAL_LIST && s_calAnchor < 100000) s_calAnchor = (long)time(nullptr);
    cal_render();
}

static void cal_nav_cb(lv_event_t *e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);      // -1 prev, +1 next
    long base = (s_calAnchor > 100000) ? s_calAnchor : (long)time(nullptr);
    if      (s_calView == CAL_DAY)   base += dir * 86400L;
    else if (s_calView == CAL_WEEK)  base += dir * 7 * 86400L;
    else if (s_calView == CAL_MONTH) base = cal_add_months(base, dir);
    s_calAnchor = base;
    cal_render();
}

static void cal_today_cb(lv_event_t *e) {
    (void)e; s_calAnchor = (long)time(nullptr); cal_render();
}

static void cal_cell_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= 42) return;
    s_calAnchor = s_calCellEpoch[idx];
    s_calView = CAL_DAY;
    cal_render();
}

static void build_calendar(lv_obj_t *pg) {
    const int CW = LV_HOR_RES - SIDEBAR_W - 36;

    s_calView = settings().calView;                 // restore saved view
    if (s_calView < CAL_LIST || s_calView > CAL_MONTH) s_calView = CAL_LIST;

    s_calStatus = lv_label_create(pg);
    lv_label_set_text(s_calStatus, "Loading calendar...");
    lv_obj_set_style_text_color(s_calStatus, lv_color_hex(0x8b97b0), 0);
    lv_obj_set_style_text_font(s_calStatus, &lv_font_montserrat_12, 0);
    lv_obj_align(s_calStatus, LV_ALIGN_TOP_LEFT, 0, 6);

    // View selector (List / Day / Week / Month), top-right, like the ticker timeframe.
    {
        const int bw = 56, bh = 28, bgap = 6;
        for (int i = 0; i < 4; i++) {
            lv_obj_t *b = lv_obj_create(pg);
            lv_obj_set_size(b, bw, bh);
            lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -(3 - i) * (bw + bgap), 0);
            lv_obj_set_style_border_width(b, 0, 0);
            lv_obj_set_style_radius(b, 6, 0);
            lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(b, cal_view_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            lv_obj_t *l = lv_label_create(b);
            lv_label_set_text(l, CAL_VIEW_LABELS[i]);
            lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(l, lv_color_hex(0xe6ebf5), 0);
            lv_obj_center(l);
            s_calViewBtn[i] = b;
        }
    }

    // Period navigator (prev / caption / next / Today), shown for Day/Week/Month.
    s_calNav = lv_obj_create(pg);
    lv_obj_set_size(s_calNav, 372, 30);
    lv_obj_align(s_calNav, LV_ALIGN_TOP_LEFT, 0, 2);
    lv_obj_set_style_bg_opa(s_calNav, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_calNav, 0, 0);
    lv_obj_set_style_pad_all(s_calNav, 0, 0);
    lv_obj_clear_flag(s_calNav, LV_OBJ_FLAG_SCROLLABLE);
    {
        lv_obj_t *pv = lv_btn_create(s_calNav);
        lv_obj_set_size(pv, 34, 28);
        lv_obj_align(pv, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(pv, lv_color_hex(0x1c2740), 0);
        lv_obj_add_event_cb(pv, cal_nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(-1));
        lv_obj_t *pl = lv_label_create(pv); lv_label_set_text(pl, LV_SYMBOL_LEFT); lv_obj_center(pl);

        s_calPeriodLbl = lv_label_create(s_calNav);
        lv_label_set_text(s_calPeriodLbl, "");
        lv_obj_set_style_text_font(s_calPeriodLbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s_calPeriodLbl, lv_color_hex(0xe6ebf5), 0);
        lv_obj_align(s_calPeriodLbl, LV_ALIGN_LEFT_MID, 42, 0);

        lv_obj_t *nx = lv_btn_create(s_calNav);
        lv_obj_set_size(nx, 32, 28);
        lv_obj_align(nx, LV_ALIGN_LEFT_MID, 264, 0);
        lv_obj_set_style_bg_color(nx, lv_color_hex(0x1c2740), 0);
        lv_obj_add_event_cb(nx, cal_nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(1));
        lv_obj_t *nl = lv_label_create(nx); lv_label_set_text(nl, LV_SYMBOL_RIGHT); lv_obj_center(nl);

        lv_obj_t *td = lv_btn_create(s_calNav);
        lv_obj_set_size(td, 66, 28);
        lv_obj_align(td, LV_ALIGN_LEFT_MID, 302, 0);
        lv_obj_set_style_bg_color(td, lv_color_hex(0x2d6cdf), 0);
        lv_obj_add_event_cb(td, cal_today_cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *tl = lv_label_create(td); lv_label_set_text(tl, "Today");
        lv_obj_set_style_text_font(tl, &lv_font_montserrat_14, 0); lv_obj_center(tl);
    }
    lv_obj_add_flag(s_calNav, LV_OBJ_FLAG_HIDDEN);

    const int rowW = CW - 12;

    // "Up next" hero: the next event with a live countdown, above the scroll list.
    const int heroH = 84;
    s_calHero = lv_obj_create(pg);
    lv_obj_set_size(s_calHero, rowW, heroH);
    lv_obj_align(s_calHero, LV_ALIGN_TOP_LEFT, 0, 40);
    lv_obj_set_style_bg_color(s_calHero, lv_color_hex(0x18294a), 0);
    lv_obj_set_style_border_width(s_calHero, 4, 0);
    lv_obj_set_style_border_side(s_calHero, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(s_calHero, lv_color_hex(0x2f80ed), 0);
    lv_obj_set_style_radius(s_calHero, 10, 0);
    lv_obj_set_style_pad_all(s_calHero, 0, 0);
    lv_obj_clear_flag(s_calHero, LV_OBJ_FLAG_SCROLLABLE);

    s_calHeroTag = lv_label_create(s_calHero);
    lv_label_set_text(s_calHeroTag, "UP NEXT");
    lv_obj_set_style_text_font(s_calHeroTag, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_calHeroTag, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_color(s_calHeroTag, lv_color_hex(0x2f80ed), 0);
    lv_obj_set_style_bg_opa(s_calHeroTag, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_calHeroTag, 8, 0);
    lv_obj_set_style_pad_ver(s_calHeroTag, 2, 0);
    lv_obj_set_style_radius(s_calHeroTag, 6, 0);
    lv_obj_align(s_calHeroTag, LV_ALIGN_TOP_LEFT, 14, 10);

    s_calHeroTitle = lv_label_create(s_calHero);
    lv_label_set_text(s_calHeroTitle, "");
    lv_obj_set_style_text_font(s_calHeroTitle, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_calHeroTitle, lv_color_hex(0xffffff), 0);
    lv_label_set_long_mode(s_calHeroTitle, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_calHeroTitle, rowW - 28);
    lv_obj_align(s_calHeroTitle, LV_ALIGN_TOP_LEFT, 14, 34);

    s_calHeroWhen = lv_label_create(s_calHero);
    lv_label_set_text(s_calHeroWhen, "");
    lv_obj_set_style_text_font(s_calHeroWhen, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_calHeroWhen, lv_color_hex(0x9fd0ff), 0);
    lv_obj_align(s_calHeroWhen, LV_ALIGN_TOP_LEFT, 14, 60);

    lv_obj_add_flag(s_calHero, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_calHero, cal_hero_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(s_calHero, LV_OBJ_FLAG_HIDDEN);

    const int listY = 40 + heroH + 8;
    s_calList = lv_obj_create(pg);
    lv_obj_set_size(s_calList, CW, PAGE_H - listY - 8);
    lv_obj_align(s_calList, LV_ALIGN_TOP_LEFT, 0, listY);
    lv_obj_set_style_bg_opa(s_calList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_calList, 0, 0);
    lv_obj_set_style_pad_all(s_calList, 0, 0);
    lv_obj_set_scroll_dir(s_calList, LV_DIR_VER);

    const int rowH = 46, gap = 6;
    for (int i = 0; i < UI_MAX_EVENTS; i++) {
        lv_obj_t *r = lv_obj_create(s_calList);
        lv_obj_set_size(r, rowW, rowH);
        lv_obj_set_pos(r, 0, i * (rowH + gap));
        lv_obj_set_style_bg_color(r, lv_color_hex(0x141c2e), 0);
        lv_obj_set_style_border_width(r, 0, 0);
        lv_obj_set_style_radius(r, 8, 0);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        s_calRow[i] = r;

        s_calWhen[i] = lv_label_create(r);
        lv_obj_set_style_text_font(s_calWhen[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_calWhen[i], lv_color_hex(0x7fd1ff), 0);
        lv_obj_align(s_calWhen[i], LV_ALIGN_LEFT_MID, 6, 0);

        s_calTitle[i] = lv_label_create(r);
        lv_obj_set_style_text_font(s_calTitle[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s_calTitle[i], lv_color_hex(0xe6ebf5), 0);
        lv_label_set_long_mode(s_calTitle[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_calTitle[i], rowW - 160);
        lv_obj_align(s_calTitle[i], LV_ALIGN_LEFT_MID, 140, 0);

        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(r, cal_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
    }

    // Month grid: 7-wide weekday header + 42 day cells (hidden until Month view).
    {
        const int cellW = CW / 7;
        const int gridY = 64;
        const int cellH = (PAGE_H - gridY - 8) / 6;
        static const char *DOW[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        for (int c = 0; c < 7; c++) {
            lv_obj_t *d = lv_label_create(pg);
            lv_label_set_text(d, DOW[c]);
            lv_obj_set_style_text_font(d, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(d, lv_color_hex(0x8b97b0), 0);
            lv_obj_set_width(d, cellW);
            lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(d, LV_ALIGN_TOP_LEFT, c * cellW, 44);
            lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
            s_calDow[c] = d;
        }
        s_calGrid = lv_obj_create(pg);
        lv_obj_set_size(s_calGrid, CW, PAGE_H - gridY - 6);
        lv_obj_align(s_calGrid, LV_ALIGN_TOP_LEFT, 0, gridY);
        lv_obj_set_style_bg_opa(s_calGrid, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_calGrid, 0, 0);
        lv_obj_set_style_pad_all(s_calGrid, 0, 0);
        lv_obj_clear_flag(s_calGrid, LV_OBJ_FLAG_SCROLLABLE);
        for (int i = 0; i < 42; i++) {
            int rr = i / 7, cc = i % 7;
            lv_obj_t *cell = lv_obj_create(s_calGrid);
            lv_obj_set_size(cell, cellW - 4, cellH - 4);
            lv_obj_set_pos(cell, cc * cellW, rr * cellH);
            lv_obj_set_style_bg_color(cell, lv_color_hex(0x141c2e), 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_border_color(cell, lv_color_hex(0x2f80ed), 0);
            lv_obj_set_style_radius(cell, 6, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(cell, cal_cell_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            s_calCell[i] = cell;

            lv_obj_t *num = lv_label_create(cell);
            lv_label_set_text(num, "");
            lv_obj_set_style_text_font(num, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(num, lv_color_hex(0xe6ebf5), 0);
            lv_obj_align(num, LV_ALIGN_TOP_LEFT, 5, 3);
            s_calCellNum[i] = num;

            lv_obj_t *cnt = lv_label_create(cell);
            lv_label_set_text(cnt, "");
            lv_obj_set_style_text_font(cnt, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(cnt, lv_color_hex(0x7fd1ff), 0);
            lv_obj_align(cnt, LV_ALIGN_BOTTOM_RIGHT, -5, -3);
            s_calCellCnt[i] = cnt;
        }
        lv_obj_add_flag(s_calGrid, LV_OBJ_FLAG_HIDDEN);
    }

    // Event detail popup (hidden until a row or the hero is tapped).
    s_calCard = lv_obj_create(pg);
    lv_obj_set_size(s_calCard, 400, 220);
    lv_obj_align(s_calCard, LV_ALIGN_CENTER, 0, -6);
    lv_obj_set_style_bg_color(s_calCard, lv_color_hex(0x141c2e), 0);
    lv_obj_set_style_border_color(s_calCard, lv_color_hex(0x2f80ed), 0);
    lv_obj_set_style_border_width(s_calCard, 2, 0);
    lv_obj_set_style_radius(s_calCard, 12, 0);
    lv_obj_set_style_pad_all(s_calCard, 16, 0);
    lv_obj_clear_flag(s_calCard, LV_OBJ_FLAG_SCROLLABLE);

    s_calCardTitle = lv_label_create(s_calCard);
    lv_label_set_text(s_calCardTitle, "");
    lv_obj_set_style_text_font(s_calCardTitle, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_calCardTitle, lv_color_hex(0xffffff), 0);
    lv_label_set_long_mode(s_calCardTitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_calCardTitle, 330);
    lv_obj_align(s_calCardTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    s_calCardBody = lv_label_create(s_calCard);
    lv_label_set_text(s_calCardBody, "");
    lv_obj_set_style_text_font(s_calCardBody, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_calCardBody, lv_color_hex(0xcdd6ea), 0);
    lv_obj_set_style_text_line_space(s_calCardBody, 4, 0);
    lv_label_set_long_mode(s_calCardBody, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_calCardBody, 366);
    lv_obj_align(s_calCardBody, LV_ALIGN_TOP_LEFT, 0, 54);

    lv_obj_t *cx = lv_btn_create(s_calCard);
    lv_obj_set_size(cx, 26, 26);
    lv_obj_align(cx, LV_ALIGN_TOP_RIGHT, 4, -4);
    lv_obj_set_style_bg_color(cx, lv_color_hex(0x2f80ed), 0);
    lv_obj_add_event_cb(cx, cal_close_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cxL = lv_label_create(cx);
    lv_label_set_text(cxL, LV_SYMBOL_CLOSE);
    lv_obj_center(cxL);

    lv_obj_add_flag(s_calCard, LV_OBJ_FLAG_HIDDEN);

    cal_render();                                   // apply the restored view + layout
}

// ---------------------------------------------------------------------------
// Air quality (US AQI color bands, EPA)
// ---------------------------------------------------------------------------
static const uint32_t AQI_COLORS[6] = {
    0x00e400, 0xffff00, 0xff7e00, 0xff0000, 0x8f3f97, 0x7e0023 };
static const char *AQI_CATS[6] = {
    "Good", "Moderate", "Sensitive", "Unhealthy",
    "Very Unhealthy", "Hazardous" };
static int aqi_band(int aqi) {
    if (aqi <= 50)  return 0;
    if (aqi <= 100) return 1;
    if (aqi <= 150) return 2;
    if (aqi <= 200) return 3;
    if (aqi <= 300) return 4;
    return 5;
}

// A non-interactive 270-degree ring gauge (arc with hidden knob) used for the
// AQI and UV dials. Caller sets the value/indicator color and adds center labels.
static lv_obj_t *make_ring(lv_obj_t *parent, int size, int arcW, int maxVal) {
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_set_size(a, size, size);
    lv_arc_set_rotation(a, 135);
    lv_arc_set_bg_angles(a, 0, 270);
    lv_arc_set_range(a, 0, maxVal);
    lv_arc_set_value(a, 0);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(a, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, arcW, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, lv_color_hex(0x24304a), LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, arcW, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(0x39d98a), LV_PART_INDICATOR);
    return a;
}

static void build_air(lv_obj_t *pg) {
    const int CW = LV_HOR_RES - SIDEBAR_W - 36;

    s_airStatus = lv_label_create(pg);
    lv_label_set_text(s_airStatus, "Loading air quality...");
    lv_obj_set_style_text_color(s_airStatus, lv_color_hex(0x8b97b0), 0);
    lv_obj_set_style_text_font(s_airStatus, &lv_font_montserrat_12, 0);
    lv_obj_align(s_airStatus, LV_ALIGN_TOP_LEFT, 0, 2);

    // US AQI ring gauge (left).
    s_airAqiArc = make_ring(pg, 180, 15, 300);
    lv_obj_align(s_airAqiArc, LV_ALIGN_TOP_LEFT, 4, 14);

    s_airAqi = lv_label_create(s_airAqiArc);
    lv_label_set_text(s_airAqi, "--");
    lv_obj_set_style_text_font(s_airAqi, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_airAqi, lv_color_hex(0xe6ebf5), 0);
    lv_obj_align(s_airAqi, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *aqiUnit = lv_label_create(s_airAqiArc);
    lv_label_set_text(aqiUnit, "US AQI");
    lv_obj_set_style_text_font(aqiUnit, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(aqiUnit, lv_color_hex(0x8b97b0), 0);
    lv_obj_align(aqiUnit, LV_ALIGN_CENTER, 0, 26);

    s_airCat = lv_label_create(pg);
    lv_label_set_text(s_airCat, "");
    lv_obj_set_width(s_airCat, 168);
    lv_obj_set_style_text_align(s_airCat, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_airCat, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_airCat, lv_color_hex(0xe6ebf5), 0);
    lv_obj_align(s_airCat, LV_ALIGN_TOP_LEFT, 6, 198);

    // UV index ring gauge (right).
    s_airUvArc = make_ring(pg, 128, 13, 12);
    lv_obj_align(s_airUvArc, LV_ALIGN_TOP_RIGHT, -8, 24);

    s_airUv = lv_label_create(s_airUvArc);
    lv_label_set_text(s_airUv, "--");
    lv_obj_set_style_text_font(s_airUv, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_airUv, lv_color_hex(0xe6ebf5), 0);
    lv_obj_align(s_airUv, LV_ALIGN_CENTER, 0, -6);

    lv_obj_t *uvName = lv_label_create(s_airUvArc);
    lv_label_set_text(uvName, "UV");
    lv_obj_set_style_text_font(uvName, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(uvName, lv_color_hex(0x8b97b0), 0);
    lv_obj_align(uvName, LV_ALIGN_CENTER, 0, 20);

    lv_obj_t **slots[4] = { &s_airPm25, &s_airPm10, &s_airO3, &s_airNo2 };
    const char *names[4] = { "PM2.5", "PM10", "Ozone", "NO2" };
    const int rowH = 38, gap = 6, y0 = 224, rowW = CW - 12;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *r = lv_obj_create(pg);
        lv_obj_set_size(r, rowW, rowH);
        lv_obj_set_pos(r, 0, y0 + i * (rowH + gap));
        lv_obj_set_style_bg_color(r, lv_color_hex(0x141c2e), 0);
        lv_obj_set_style_border_width(r, 0, 0);
        lv_obj_set_style_radius(r, 8, 0);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *nm = lv_label_create(r);
        lv_label_set_text(nm, names[i]);
        lv_obj_set_style_text_font(nm, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(nm, lv_color_hex(0x8b97b0), 0);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 10, 0);

        lv_obj_t *val = lv_label_create(r);
        lv_label_set_text(val, "-- ug/m3");
        lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(val, lv_color_hex(0xe6ebf5), 0);
        lv_obj_align(val, LV_ALIGN_RIGHT_MID, -12, 0);
        *slots[i] = val;
    }
}

// Autoscaling line + area-fill sparkline for a diag metric (reuses the ticker look).
static void draw_diag_spark(lv_obj_t *cv, const float *data, int count,
                            bool autoscale, float loFixed, float hiFixed, uint32_t color,
                            int w = DSP_W, int h = DSP_H) {
    if (!cv) return;
    lv_canvas_fill_bg(cv, lv_color_hex(0x141c2e), LV_OPA_COVER);
    if (count < 2) return;
    float lo = loFixed, hi = hiFixed;
    if (autoscale) {
        lo = 1e30f; hi = -1e30f;
        for (int j = 0; j < count; j++) { if (data[j] < lo) lo = data[j]; if (data[j] > hi) hi = data[j]; }
        float pad = (hi - lo) * 0.15f; if (pad < 1.0f) pad = 1.0f;
        lo -= pad; hi += pad;
    }
    float span = hi - lo; if (span < 1e-6f) span = 1.0f;
    static lv_point_t pts[DIAG_N];
    const int m = 4;
    for (int j = 0; j < count; j++) {
        int x = (int)((int64_t)j * (w - 1) / (count - 1));
        float f = (data[j] - lo) / span; if (f < 0) f = 0; if (f > 1) f = 1;
        int y = (h - 1 - m) - (int)(f * (h - 1 - 2 * m));
        pts[j].x = (lv_coord_t)x; pts[j].y = (lv_coord_t)y;
    }
    lv_draw_rect_dsc_t fill; lv_draw_rect_dsc_init(&fill);
    fill.bg_color = lv_color_hex(color); fill.bg_opa = LV_OPA_20;
    const int baseY = h - 1;
    for (int s = 0; s < count - 1; s++) {
        int xa = pts[s].x, ya = pts[s].y, xb = pts[s + 1].x, yb = pts[s + 1].y;
        if (xb <= xa) continue;
        for (int x = (s == 0 ? xa : xa + 1); x <= xb; x++) {
            int y = ya + (yb - ya) * (x - xa) / (xb - xa);
            if (y < baseY) lv_canvas_draw_rect(cv, x, y, 1, baseY - y + 1, &fill);
        }
    }
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.color = lv_color_hex(color); d.width = 2; d.round_start = d.round_end = 1;
    lv_canvas_draw_line(cv, pts, count, &d);
}

// Sampled every ui_tick (even off-page) so opening Diag shows recent history.
static void diag_sample() {
    bool online = net_state() == NetState::Connected;
    float heapKb = ESP.getFreeHeap() / 1024.0f;
    float rssi   = online ? (float)WiFi.RSSI() : -100.0f;   // floor when offline
    float tempF  = temperatureRead() * 9.0f / 5.0f + 32.0f; // ESP32-S3 on-die sensor

    // Render FPS since the last sample: frames presented / elapsed seconds.
    static uint32_t lastFrames = 0, lastMs = 0;
    uint32_t nowMs = millis(), frames = display_frame_count();
    float fps = 0.0f;
    if (lastMs && nowMs > lastMs) fps = (frames - lastFrames) * 1000.0f / (nowMs - lastMs);
    lastFrames = frames; lastMs = nowMs;

    if (s_diagCount < DIAG_N) {
        s_heapHist[s_diagCount] = heapKb;
        s_rssiHist[s_diagCount] = rssi;
        s_tempHist[s_diagCount] = tempF;
        s_fpsHist[s_diagCount]  = fps;
        s_diagCount++;
    } else {
        memmove(s_heapHist, s_heapHist + 1, (DIAG_N - 1) * sizeof(float));
        memmove(s_rssiHist, s_rssiHist + 1, (DIAG_N - 1) * sizeof(float));
        memmove(s_tempHist, s_tempHist + 1, (DIAG_N - 1) * sizeof(float));
        memmove(s_fpsHist,  s_fpsHist + 1,  (DIAG_N - 1) * sizeof(float));
        s_heapHist[DIAG_N - 1] = heapKb;
        s_rssiHist[DIAG_N - 1] = rssi;
        s_tempHist[DIAG_N - 1] = tempF;
        s_fpsHist[DIAG_N - 1]  = fps;
    }
}

// A labeled horizontal usage bar (name + track + value) for the Diag resource block.
static lv_obj_t *make_stat_bar(lv_obj_t *pg, const char *name, int x, int y, lv_obj_t **valOut) {
    lv_obj_t *nm = lv_label_create(pg);
    lv_label_set_text(nm, name);
    lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(nm, lv_color_hex(0xcdd6ea), 0);
    lv_obj_align(nm, LV_ALIGN_TOP_LEFT, x, y);

    lv_obj_t *bar = lv_bar_create(pg);
    lv_obj_set_size(bar, 150, 12);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, x + 58, y + 3);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x2a3550), LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    lv_obj_set_style_anim_time(bar, 400, 0);           // ease fill changes
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);

    lv_obj_t *v = lv_label_create(pg);
    lv_label_set_text(v, "--");
    lv_obj_set_style_text_font(v, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(v, lv_color_hex(0x8b97b0), 0);
    lv_obj_align(v, LV_ALIGN_TOP_LEFT, x + 214, y + 1);
    *valOut = v;
    return bar;
}

// Set a usage bar: pct drives fill + green/amber/red pressure color, txt is the caption.
static void set_stat_bar(lv_obj_t *bar, lv_obj_t *val, int pct, const char *txt) {
    if (!bar) return;
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    lv_bar_set_value(bar, pct, LV_ANIM_ON);
    uint32_t c = pct < 70 ? 0x39d98a : (pct < 85 ? 0xffb347 : 0xff5c5c);
    lv_obj_set_style_bg_color(bar, lv_color_hex(c), LV_PART_INDICATOR);
    if (val) lv_label_set_text(val, txt);
}

static void build_diag(lv_obj_t *pg) {
    lv_obj_t *title = lv_label_create(pg);
    lv_label_set_text(title, "Diagnostics");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xe6ebf5), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 2);

    // ---- Left column: device text stats + board-only trend charts. ----
    const int LX = 0, LSW = 340, LSH = 56;

    s_diagLbl = lv_label_create(pg);
    lv_label_set_text(s_diagLbl, "Collecting device stats...");
    lv_obj_set_style_text_font(s_diagLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_diagLbl, lv_color_hex(0xcdd6ea), 0);
    lv_obj_set_style_text_line_space(s_diagLbl, 4, 0);
    lv_obj_align(s_diagLbl, LV_ALIGN_TOP_LEFT, LX, 34);

    // ESP32-S3 on-die temperature sensor trend (canvas bufs live in PSRAM).
    static lv_color_t *tBuf = nullptr, *fBuf = nullptr;

    lv_obj_t *tt = lv_label_create(pg);
    lv_label_set_text(tt, "Die temp (F)");
    lv_obj_set_style_text_font(tt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tt, lv_color_hex(0x8b97b0), 0);
    lv_obj_align(tt, LV_ALIGN_TOP_LEFT, LX, 206);
    s_diagTempVal = lv_label_create(pg);
    lv_label_set_text(s_diagTempVal, "--");
    lv_obj_set_style_text_font(s_diagTempVal, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_diagTempVal, lv_color_hex(0xffb454), 0);
    lv_obj_align(s_diagTempVal, LV_ALIGN_TOP_LEFT, LX + LSW - 56, 204);
    if (!tBuf) tBuf = (lv_color_t *)heap_caps_malloc(
        LSW * LSH * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    s_diagTempSpark = lv_canvas_create(pg);
    lv_canvas_set_buffer(s_diagTempSpark, tBuf, LSW, LSH, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(s_diagTempSpark, LV_ALIGN_TOP_LEFT, LX, 228);
    lv_canvas_fill_bg(s_diagTempSpark, lv_color_hex(0x141c2e), LV_OPA_COVER);

    // Measured render FPS trend.
    lv_obj_t *ft = lv_label_create(pg);
    lv_label_set_text(ft, "Render (FPS)");
    lv_obj_set_style_text_font(ft, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ft, lv_color_hex(0x8b97b0), 0);
    lv_obj_align(ft, LV_ALIGN_TOP_LEFT, LX, 296);
    s_diagFpsVal = lv_label_create(pg);
    lv_label_set_text(s_diagFpsVal, "--");
    lv_obj_set_style_text_font(s_diagFpsVal, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_diagFpsVal, lv_color_hex(0xa78bfa), 0);
    lv_obj_align(s_diagFpsVal, LV_ALIGN_TOP_LEFT, LX + LSW - 56, 294);
    if (!fBuf) fBuf = (lv_color_t *)heap_caps_malloc(
        LSW * LSH * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    s_diagFpsSpark = lv_canvas_create(pg);
    lv_canvas_set_buffer(s_diagFpsSpark, fBuf, LSW, LSH, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(s_diagFpsSpark, LV_ALIGN_TOP_LEFT, LX, 318);
    lv_canvas_fill_bg(s_diagFpsSpark, lv_color_hex(0x141c2e), LV_OPA_COVER);

    // ---- Right column: live heap + RSSI trends, resource bars, signal. ----
    static lv_color_t *heapBuf = nullptr, *rssiBuf = nullptr;
    const int RX = 360;

    lv_obj_t *ht = lv_label_create(pg);
    lv_label_set_text(ht, "Free heap (KB)");
    lv_obj_set_style_text_font(ht, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ht, lv_color_hex(0x8b97b0), 0);
    lv_obj_align(ht, LV_ALIGN_TOP_LEFT, RX, 34);
    s_diagHeapVal = lv_label_create(pg);
    lv_label_set_text(s_diagHeapVal, "--");
    lv_obj_set_style_text_font(s_diagHeapVal, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_diagHeapVal, lv_color_hex(0x39d98a), 0);
    lv_obj_align(s_diagHeapVal, LV_ALIGN_TOP_LEFT, RX + DSP_W - 70, 32);
    if (!heapBuf) heapBuf = (lv_color_t *)heap_caps_malloc(
        DSP_W * DSP_H * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    s_diagHeapSpark = lv_canvas_create(pg);
    lv_canvas_set_buffer(s_diagHeapSpark, heapBuf, DSP_W, DSP_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(s_diagHeapSpark, LV_ALIGN_TOP_LEFT, RX, 58);
    lv_canvas_fill_bg(s_diagHeapSpark, lv_color_hex(0x141c2e), LV_OPA_COVER);

    lv_obj_t *rt = lv_label_create(pg);
    lv_label_set_text(rt, "Wi-Fi RSSI (dBm)");
    lv_obj_set_style_text_font(rt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rt, lv_color_hex(0x8b97b0), 0);
    lv_obj_align(rt, LV_ALIGN_TOP_LEFT, RX, 128);
    s_diagRssiVal = lv_label_create(pg);
    lv_label_set_text(s_diagRssiVal, "--");
    lv_obj_set_style_text_font(s_diagRssiVal, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_diagRssiVal, lv_color_hex(0x5aa9ff), 0);
    lv_obj_align(s_diagRssiVal, LV_ALIGN_TOP_LEFT, RX + DSP_W - 90, 126);
    if (!rssiBuf) rssiBuf = (lv_color_t *)heap_caps_malloc(
        DSP_W * DSP_H * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    s_diagRssiSpark = lv_canvas_create(pg);
    lv_canvas_set_buffer(s_diagRssiSpark, rssiBuf, DSP_W, DSP_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(s_diagRssiSpark, LV_ALIGN_TOP_LEFT, RX, 152);
    lv_canvas_fill_bg(s_diagRssiSpark, lv_color_hex(0x141c2e), LV_OPA_COVER);

    // Resource usage bars (internal RAM, PSRAM, flash) under the sparklines.
    s_diagRamBar   = make_stat_bar(pg, "RAM",   RX, 228, &s_diagRamVal);
    s_diagPsramBar = make_stat_bar(pg, "PSRAM", RX, 262, &s_diagPsramVal);
    s_diagFlashBar = make_stat_bar(pg, "Flash", RX, 296, &s_diagFlashVal);

    // Wi-Fi signal strength: 4 ascending bars + quality caption.
    lv_obj_t *sl = lv_label_create(pg);
    lv_label_set_text(sl, "Signal");
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sl, lv_color_hex(0x8b97b0), 0);
    lv_obj_align(sl, LV_ALIGN_TOP_LEFT, RX, 330);
    const int bw = 16, bgap = 8, baseY = 396;
    for (int i = 0; i < 4; i++) {
        int h = 14 + i * 14;
        s_diagSigBar[i] = lv_obj_create(pg);
        lv_obj_set_size(s_diagSigBar[i], bw, h);
        lv_obj_align(s_diagSigBar[i], LV_ALIGN_TOP_LEFT, RX + 64 + i * (bw + bgap), baseY - h);
        lv_obj_set_style_border_width(s_diagSigBar[i], 0, 0);
        lv_obj_set_style_radius(s_diagSigBar[i], 3, 0);
        lv_obj_set_style_bg_color(s_diagSigBar[i], lv_color_hex(0x2a3550), 0);
    }
    s_diagSigTxt = lv_label_create(pg);
    lv_label_set_text(s_diagSigTxt, "--");
    lv_obj_set_style_text_font(s_diagSigTxt, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_diagSigTxt, lv_color_hex(0x8b97b0), 0);
    lv_obj_align(s_diagSigTxt, LV_ALIGN_TOP_LEFT, RX + 64 + 4 * (bw + bgap) + 14, baseY - 26);
}

void ui_init() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f1420), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_topbar(scr);

    for (int i = 0; i < PAGE_COUNT; i++) s_pages[i] = make_page(scr);
    build_launcher(s_pages[PAGE_LAUNCHER]);
    build_home(s_pages[PAGE_WEATHER]);
    build_flights(s_pages[PAGE_FLIGHTS]);
    build_calendar(s_pages[PAGE_CALENDAR]);
    build_tickers(s_pages[PAGE_TICKERS]);
    build_air(s_pages[PAGE_AIR]);
    build_photo(s_pages[PAGE_PHOTO]);
    build_diag(s_pages[PAGE_DIAG]);
    build_config(s_pages[PAGE_CONFIG]);

    // A hidden accent-colored loading spinner centered on each data page.
    const Page dataPages[] = { PAGE_WEATHER, PAGE_FLIGHTS, PAGE_CALENDAR,
                               PAGE_TICKERS, PAGE_AIR, PAGE_PHOTO };
    for (Page p : dataPages) {
        lv_obj_t *sp = lv_spinner_create(s_pages[p], 700, 60);   // 0.7s/rev, 60deg arc
        lv_obj_set_size(sp, 56, 56);
        lv_obj_center(sp);
        lv_obj_set_style_arc_width(sp, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_width(sp, 5, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(sp, lv_color_hex(0x263041), LV_PART_MAIN);
        lv_obj_set_style_arc_color(sp, lv_color_hex(0x7fb0ff), LV_PART_INDICATOR);
        lv_obj_add_flag(sp, LV_OBJ_FLAG_HIDDEN);
        s_pageSpin[p] = sp;
    }

    // Restore the last-selected panel (first boot / bad value -> launcher).
    Page start = (Page)settings().lastPanel;
    if (start <= PAGE_LAUNCHER || start >= PAGE_COUNT) start = PAGE_LAUNCHER;
    ui_show_page(start);
}

static void update_cal_hero() {
    if (!s_calHero) return;
    if (s_calView != CAL_LIST) { lv_obj_add_flag(s_calHero, LV_OBJ_FLAG_HIDDEN); return; }
    time_t now = time(nullptr);
    if (now < 100000 || s_calAllCount == 0) {
        lv_obj_add_flag(s_calHero, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    // First event that hasn't started yet (list is sorted ascending; allow a 60s grace).
    int idx = -1;
    for (int i = 0; i < s_calAllCount; i++) {
        if ((long)s_calAll[i].start > (long)now - 60) { idx = i; break; }
    }
    if (idx < 0) { lv_obj_add_flag(s_calHero, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_clear_flag(s_calHero, LV_OBJ_FLAG_HIDDEN);
    s_calHeroIdx = idx;

    const CalEvent &e = s_calAll[idx];
    long diff = (long)e.start - (long)now;                 // seconds until start

    bool nowish = diff <= 60;
    uint32_t accent = nowish ? 0x1f9d57 : 0x2f80ed;
    lv_label_set_text(s_calHeroTag, nowish ? "NOW" : "UP NEXT");
    lv_obj_set_style_bg_color(s_calHeroTag, lv_color_hex(accent), 0);
    lv_obj_set_style_border_color(s_calHero, lv_color_hex(accent), 0);
    lv_label_set_text(s_calHeroTitle, e.title.length() ? e.title.c_str() : "(no title)");

    char cd[24];
    if      (diff <= 60)   snprintf(cd, sizeof(cd), "now");
    else if (diff < 3600)  snprintf(cd, sizeof(cd), "in %ld min", diff / 60);
    else if (diff < 86400) snprintf(cd, sizeof(cd), "in %ldh %02ldm", diff / 3600, (diff % 3600) / 60);
    else                   snprintf(cd, sizeof(cd), "in %ld days", diff / 86400);

    static const char *WD[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    time_t et = (time_t)e.start; struct tm tm; localtime_r(&et, &tm);
    time_t nt = now;            struct tm nm; localtime_r(&nt, &nm);
    int dayDelta = (tm.tm_year == nm.tm_year) ? (tm.tm_yday - nm.tm_yday)
                                              : (tm.tm_year > nm.tm_year ? 9 : -9);
    char day[16];
    if      (dayDelta == 0) snprintf(day, sizeof(day), "Today");
    else if (dayDelta == 1) snprintf(day, sizeof(day), "Tomorrow");
    else { int wd = tm.tm_wday; if (wd < 0 || wd > 6) wd = 0;
           snprintf(day, sizeof(day), "%s %d/%d", WD[wd], tm.tm_mon + 1, tm.tm_mday); }

    char when[56];
    if (e.allDay) {
        snprintf(when, sizeof(when), "%s  -  All day", day);
    } else {
        char tbuf[12];
        if (settings().use24hClock) snprintf(tbuf, sizeof(tbuf), "%02d:%02d", tm.tm_hour, tm.tm_min);
        else { int h12 = tm.tm_hour % 12; if (h12 == 0) h12 = 12;
               snprintf(tbuf, sizeof(tbuf), "%d:%02d %s", h12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM"); }
        snprintf(when, sizeof(when), "%s %s  -  %s", day, tbuf, cd);
    }
    lv_label_set_text(s_calHeroWhen, when);
    lv_obj_set_style_text_color(s_calHeroWhen,
                                lv_color_hex(diff < 600 ? 0xffd166 : 0x9fd0ff), 0);
}

static void update_sun_labels();

static void update_clock() {
    time_t now = time(nullptr);
    if (now < 100000) return;   // not synced yet
    struct tm t;
    localtime_r(&now, &t);
    char buf[16];
    if (settings().use24hClock) strftime(buf, sizeof(buf), "%H:%M", &t);
    else                        strftime(buf, sizeof(buf), "%I:%M %p", &t);
    lv_label_set_text(s_clockTime, buf);
    char dbuf[48];
    strftime(dbuf, sizeof(dbuf), "%A, %B %d", &t);
    lv_label_set_text(s_clockDate, dbuf);
    topbar_relayout();
    draw_sky(t.tm_hour * 60 + t.tm_min);
    update_sun_labels();
}

static void update_config_page() {
    if (s_active != PAGE_CONFIG) return;   // only refresh when visible

    NetState st = net_state();
    if (st == NetState::Portal) {
        lv_label_set_text(s_cfgState, "Setup needed - join the hotspot");
        String d = "1. On your phone, join Wi-Fi network:\n     \"" + net_ssid() + "\"\n"
                   "2. The setup page opens automatically,\n     or visit http://" + net_ip() +
                   "\n3. Enter your home Wi-Fi details.\n\nScan the code to open setup:";
        lv_label_set_text(s_cfgDetails, d.c_str());
        render_qr("http://" + net_ip());
    } else if (st == NetState::Connected) {
        lv_label_set_text(s_cfgState, "Connected");
        String url = "http://" + net_hostname();
        String d = "Network:  " + net_ssid() + "\nIP:  " + net_ip() +
                   "\n\nEdit any setting from a browser on\nyour network:\n     " + url +
                   "\n     (or http://" + net_ip() + " )\n\nScan to open:";
        lv_label_set_text(s_cfgDetails, d.c_str());
        render_qr(url);
    } else {
        lv_label_set_text(s_cfgState, "Connecting...");
        lv_label_set_text(s_cfgDetails, "Joining your Wi-Fi network...");
    }
}

static const char *reset_reason_str(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "Power-on";
        case ESP_RST_SW:        return "Software";
        case ESP_RST_PANIC:     return "Panic/crash";
        case ESP_RST_INT_WDT:   return "Int watchdog";
        case ESP_RST_TASK_WDT:  return "Task watchdog";
        case ESP_RST_WDT:       return "Other watchdog";
        case ESP_RST_BROWNOUT:  return "Brownout";
        case ESP_RST_DEEPSLEEP: return "Deep-sleep wake";
        case ESP_RST_EXT:       return "External";
        default:                return "Unknown";
    }
}

static void update_diag_page() {
    if (s_active != PAGE_DIAG || !s_diagLbl) return;

    uint32_t up = millis() / 1000;
    uint32_t dd = up / 86400; up %= 86400;
    uint32_t hh = up / 3600;  up %= 3600;
    uint32_t mm = up / 60;    uint32_t ss = up % 60;

    size_t heap  = ESP.getFreeHeap();
    size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    bool   online = net_state() == NetState::Connected;

    char b[512];
    snprintf(b, sizeof(b),
        "Uptime:   %lud %02lu:%02lu:%02lu\n"
        "Wi-Fi:  %s\n"
        "SSID:   %s\n"
        "IP:     %s\n"
        "MAC:    %s\n"
        "Reset:  %s\n"
        "Firmware: %s %s\n"
        "LVGL: %d.%d.%d",
        (unsigned long)dd, (unsigned long)hh, (unsigned long)mm, (unsigned long)ss,
        online ? "connected" : "offline",
        online ? WiFi.SSID().c_str() : "-",
        online ? WiFi.localIP().toString().c_str() : "-",
        WiFi.macAddress().c_str(),
        reset_reason_str(esp_reset_reason()),
        __DATE__, __TIME__,
        LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    lv_label_set_text(s_diagLbl, b);

    // Live trend sparklines: heap autoscales; RSSI is fixed to a readable dBm band.
    draw_diag_spark(s_diagHeapSpark, s_heapHist, s_diagCount, true, 0, 0, 0x39d98a);
    draw_diag_spark(s_diagRssiSpark, s_rssiHist, s_diagCount, false, -90.0f, -30.0f, 0x5aa9ff);
    // Board-only trends: die temperature + render FPS (autoscaled, narrower canvas).
    draw_diag_spark(s_diagTempSpark, s_tempHist, s_diagCount, true, 0, 0, 0xffb454, 340, 56);
    draw_diag_spark(s_diagFpsSpark,  s_fpsHist,  s_diagCount, true, 0, 0, 0xa78bfa, 340, 56);
    char v[24];
    snprintf(v, sizeof(v), "%u KB", (unsigned)(heap / 1024));
    lv_label_set_text(s_diagHeapVal, v);
    if (online) { snprintf(v, sizeof(v), "%d", (int)WiFi.RSSI()); lv_label_set_text(s_diagRssiVal, v); }
    else        { lv_label_set_text(s_diagRssiVal, "--"); }
    if (s_diagCount > 0) {
        snprintf(v, sizeof(v), "%.0fF", s_tempHist[s_diagCount - 1]);
        lv_label_set_text(s_diagTempVal, v);
        snprintf(v, sizeof(v), "%.1f", s_fpsHist[s_diagCount - 1]);
        lv_label_set_text(s_diagFpsVal, v);
    }

    // Resource usage bars.
    char t[28];
    size_t heapTot = ESP.getHeapSize();
    int ramPct = heapTot ? (int)(100 - ((uint64_t)heap * 100 / heapTot)) : 0;
    snprintf(t, sizeof(t), "%u/%u KB", (unsigned)((heapTot - heap) / 1024), (unsigned)(heapTot / 1024));
    set_stat_bar(s_diagRamBar, s_diagRamVal, ramPct, t);

    size_t psTot = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    int psPct = psTot ? (int)(100 - ((uint64_t)psram * 100 / psTot)) : 0;
    snprintf(t, sizeof(t), "%.1f/%.1f MB", (psTot - psram) / 1048576.0, psTot / 1048576.0);
    set_stat_bar(s_diagPsramBar, s_diagPsramVal, psPct, t);

    size_t flUsed = ESP.getSketchSize();
    size_t flTot  = flUsed + ESP.getFreeSketchSpace();
    int flPct = flTot ? (int)((uint64_t)flUsed * 100 / flTot) : 0;
    snprintf(t, sizeof(t), "%.1f/%.1f MB", flUsed / 1048576.0, flTot / 1048576.0);
    set_stat_bar(s_diagFlashBar, s_diagFlashVal, flPct, t);

    // Wi-Fi signal strength bars + quality caption.
    int bars = 0; const char *q = "offline"; uint32_t sc = 0x8b97b0;
    if (online) {
        int r = (int)WiFi.RSSI();
        if      (r >= -55) { bars = 4; q = "Excellent"; sc = 0x39d98a; }
        else if (r >= -65) { bars = 3; q = "Good";      sc = 0x39d98a; }
        else if (r >= -75) { bars = 2; q = "Fair";      sc = 0xffb347; }
        else               { bars = 1; q = "Weak";      sc = 0xff5c5c; }
    }
    for (int i = 0; i < 4; i++)
        lv_obj_set_style_bg_color(s_diagSigBar[i], lv_color_hex(i < bars ? sc : 0x2a3550), 0);
    lv_label_set_text(s_diagSigTxt, q);
    lv_obj_set_style_text_color(s_diagSigTxt, lv_color_hex(sc), 0);
}

static void animate_weather() {
    if (!s_wxIcon || s_wxCode < 0) return;
    lv_canvas_fill_bg(s_wxIcon, lv_color_hex(0x0f1420), LV_OPA_COVER);
    wx_draw(s_wxIcon, 48, 48, 38, s_wxCode, (int)s_wxFrame);
}

void ui_tick() {
    // Weather-panel glyph animates faster than the 500ms housekeeping tick.
    static uint32_t lastAnim = 0;
    if (s_active == PAGE_WEATHER && s_wxCode >= 0 && millis() - lastAnim >= 150) {
        lastAnim = millis();
        s_wxFrame++;
        animate_weather();
    }

    static uint32_t last = 0;
    if (millis() - last < 500) return;
    last = millis();

    // Surface the setup screen automatically when entering the hotspot (join
    // instructions + QR). Coming online leaves the user on their chosen panel.
    static NetState prevState = NetState::Booting;
    NetState st = net_state();
    if (st != prevState) {
        if (st == NetState::Portal) ui_show_page(PAGE_CONFIG);
        prevState = st;
    }

    diag_sample();
    update_clock();
    update_topbar_wifi();
    update_config_page();
    update_diag_page();
    if (s_active == PAGE_CALENDAR) update_cal_hero();
}

// ---------------------------------------------------------------------------
// Data push hooks
// ---------------------------------------------------------------------------
// Recompose the two-column conditions block: left column = summary/humidity/wind,
// right column = feels-like + UV. Same font so the columns read as one block.
static void compose_wx_body() {
    if (s_weatherBody) {
        char b[128];
        snprintf(b, sizeof(b), "%s\nHumidity %d%%\nWind %.0f mph",
                 s_wxSummary.c_str(), s_wxHum, s_wxWindMph);
        lv_label_set_text(s_weatherBody, b);
    }
    if (s_wxDetail) {
        char d[96]; int n = 0;
        if (s_wxFeelsF > -999.0f)
            n += snprintf(d + n, sizeof(d) - n, "Feels like %.0fF", s_wxFeelsF);
        if (s_wxUvIdx >= 0) {
            const char *band = s_wxUvIdx < 3 ? "Low" : s_wxUvIdx < 6 ? "Moderate"
                             : s_wxUvIdx < 8 ? "High" : s_wxUvIdx < 11 ? "Very High" : "Extreme";
            snprintf(d + n, sizeof(d) - n, "%sUV %.0f %s", n ? "\n" : "", s_wxUvIdx, band);
        }
        lv_label_set_text(s_wxDetail, d);
    }
}

void ui_weather_set(int code, const String &summary, float tempC, int humidity, float windKph, float feelsC) {
    float temp = tempC * 9.0f / 5.0f + 32.0f;
    if (s_wxTemp) {
        char t[16]; snprintf(t, sizeof(t), "%.0fF", temp);
        lv_label_set_text(s_wxTemp, t);
    }
    if (s_wxLoc) {
        const String &loc = settings().locationName;
        if (loc.length()) {
            lv_label_set_text(s_wxLoc, loc.c_str());
        } else {
            char l[48]; snprintf(l, sizeof(l), "%.3f, %.3f",
                                 settings().homeLat, settings().homeLon);
            lv_label_set_text(s_wxLoc, l);
        }
    }
    s_wxSummary = summary;
    s_wxHum     = humidity;
    s_wxWindMph = windKph * 0.621371f;
    s_wxFeelsF  = feelsC * 9.0f / 5.0f + 32.0f;
    compose_wx_body();
    s_wxCode = code;
    s_wxHave = true;                                   // enable offline last-good (#1)
    page_set_loading(PAGE_WEATHER, false);
    animate_weather();
}

void ui_weather_error(const String &msg) {
    page_set_loading(PAGE_WEATHER, false);
    if (s_wxHave) return;                               // keep last-good conditions when offline (#1)
    if (s_weatherBody) lv_label_set_text(s_weatherBody, msg.c_str());
}

void ui_weather_uv_set(float uvIndex) {
    s_wxUvIdx = uvIndex;
    compose_wx_body();
}

void ui_forecast_set(DayForecast *days, int count) {
    if (count > UI_FORECAST_DAYS) count = UI_FORECAST_DAYS;
    s_forecastCount = count;
    for (int i = 0; i < count; i++) s_forecast[i] = days[i];

    time_t now = time(nullptr);
    bool haveTime = now > 100000;
    struct tm t;
    if (haveTime) localtime_r(&now, &t);
    static const char *WD[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    for (int i = 0; i < UI_FORECAST_DAYS; i++) {
        if (!s_fcCard[i]) continue;
        if (i >= count) { lv_obj_add_flag(s_fcCard[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_clear_flag(s_fcCard[i], LV_OBJ_FLAG_HIDDEN);

        char d[8];
        if (i == 0)          snprintf(d, sizeof(d), "Today");
        else if (haveTime)   snprintf(d, sizeof(d), "%s", WD[(t.tm_wday + i) % 7]);
        else                 snprintf(d, sizeof(d), "+%d", i);
        lv_label_set_text(s_fcDay[i], d);

        lv_canvas_fill_bg(s_fcIcon[i], lv_color_hex(0x141c2e), LV_OPA_COVER);
        wx_draw(s_fcIcon[i], 28, 28, 22, s_forecast[i].code);

        float hi = s_forecast[i].hiC * 9.0f / 5.0f + 32.0f;
        float lo = s_forecast[i].loC * 9.0f / 5.0f + 32.0f;
        char tt[16]; snprintf(tt, sizeof(tt), "%.0f/%.0f", hi, lo);
        lv_label_set_text(s_fcTemp[i], tt);
    }
}

// Home-centered polar plot: planes placed by bearing + distance from settings home.
// LVGL 8.3 lv_canvas_draw_arc hangs on full-circle draws at larger radii, so
// the radar range rings are plotted point-by-point instead.
static void canvas_ring(lv_obj_t *cv, int cx, int cy, int r, lv_color_t col) {
    if (r < 2) return;
    int steps = (int)(6.2831853f * r) + 4;
    for (int s = 0; s < steps; s++) {
        float a = (float)s / (float)steps * 6.2831853f;
        float ca = cosf(a), sa = sinf(a);
        for (int rr = r - 1; rr <= r; rr++) {          // ~2px ring
            int x = cx + (int)lroundf(ca * rr);
            int y = cy + (int)lroundf(sa * rr);
            if (x >= 0 && x < RADAR_PX && y >= 0 && y < RADAR_PX)
                lv_canvas_set_px_color(cv, x, y, col);
        }
    }
}

static inline String flight_id(const FlightRow &r) {
    return r.tail.length() ? r.tail : r.callsign;
}
static int trail_find(const String &id) {
    for (int k = 0; k < UI_MAX_FLIGHTS; k++)
        if (s_trails[k].used && s_trails[k].id == id) return k;
    return -1;
}

static void draw_radar() {
    if (!s_flightsRadar) return;
    lv_obj_t *cv = s_flightsRadar;
    const int cx = RADAR_PX / 2, cy = RADAR_PX / 2;
    const int radiusPx = RADAR_PX / 2 - 24;   // leave room for compass + ring labels

    lv_canvas_fill_bg(cv, lv_color_hex(0x0a1120), LV_OPA_COVER);

    lv_color_t ringCol = lv_color_hex(0x24406a);
    lv_draw_line_dsc_t ldsc; lv_draw_line_dsc_init(&ldsc);
    ldsc.color = lv_color_hex(0x1a2c48); ldsc.width = 1; ldsc.opa = LV_OPA_COVER;
    lv_draw_label_dsc_t rlbl; lv_draw_label_dsc_init(&rlbl);
    rlbl.color = lv_color_hex(0x6f7d99); rlbl.font = &lv_font_montserrat_12;

    int range = radar_range();
    for (int k = 1; k <= 3; k++) {
        int rr = radiusPx * k / 3;
        canvas_ring(cv, cx, cy, rr, ringCol);
        char rl[8]; snprintf(rl, sizeof(rl), "%d", range * k / 3);
        lv_canvas_draw_text(cv, cx + 3, cy - rr - 2, 40, &rlbl, rl);
    }
    lv_point_t vpts[2] = {{(lv_coord_t)cx, (lv_coord_t)(cy - radiusPx)}, {(lv_coord_t)cx, (lv_coord_t)(cy + radiusPx)}};
    lv_canvas_draw_line(cv, vpts, 2, &ldsc);
    lv_point_t hpts[2] = {{(lv_coord_t)(cx - radiusPx), (lv_coord_t)cy}, {(lv_coord_t)(cx + radiusPx), (lv_coord_t)cy}};
    lv_canvas_draw_line(cv, hpts, 2, &ldsc);

    lv_draw_label_dsc_t cdsc; lv_draw_label_dsc_init(&cdsc);
    cdsc.color = lv_color_hex(0x8b97b0); cdsc.font = &lv_font_montserrat_14;
    lv_canvas_draw_text(cv, cx - 5,  cy - radiusPx - 18, 16, &cdsc, "N");
    lv_canvas_draw_text(cv, cx - 5,  cy + radiusPx + 2,  16, &cdsc, "S");
    lv_canvas_draw_text(cv, cx + radiusPx + 4,  cy - 9,  16, &cdsc, "E");
    lv_canvas_draw_text(cv, cx - radiusPx - 16, cy - 9,  16, &cdsc, "W");

    // Rotating sweep arm with a fading phosphor trail (north-up, same convention
    // as the plane bearings below).
    lv_draw_line_dsc_t sweep; lv_draw_line_dsc_init(&sweep);
    sweep.width = 2; sweep.round_start = 1; sweep.round_end = 1;
    for (int t = 5; t >= 0; t--) {
        float ang = (s_sweepDeg - t * 7.0f) * DEG2RAD;
        int ex = cx + (int)lroundf(sinf(ang) * radiusPx);
        int ey = cy - (int)lroundf(cosf(ang) * radiusPx);
        sweep.color = lv_color_hex(0x33ff88);
        sweep.opa   = (lv_opa_t)(70 + (5 - t) * 34);   // faint tail -> bright arm
        lv_point_t sp[2] = {{(lv_coord_t)cx, (lv_coord_t)cy}, {(lv_coord_t)ex, (lv_coord_t)ey}};
        lv_canvas_draw_line(cv, sp, 2, &sweep);
    }

    lv_draw_rect_dsc_t arrow; lv_draw_rect_dsc_init(&arrow);
    arrow.bg_color = lv_color_hex(0xffb020); arrow.bg_opa = LV_OPA_COVER;
    lv_draw_rect_dsc_t dot; lv_draw_rect_dsc_init(&dot);
    dot.bg_color = lv_color_hex(0xffb020); dot.bg_opa = LV_OPA_COVER; dot.radius = LV_RADIUS_CIRCLE;
    lv_draw_label_dsc_t plbl; lv_draw_label_dsc_init(&plbl);
    plbl.color = lv_color_hex(0xe6ebf5); plbl.font = &lv_font_montserrat_12;
    lv_draw_line_dsc_t lead; lv_draw_line_dsc_init(&lead);   // velocity leader line
    lead.width = 2; lead.round_start = 1; lead.round_end = 1;
    lead.color = lv_color_hex(0xffb020); lead.opa = LV_OPA_50;

    for (int i = 0; i < UI_MAX_FLIGHTS; i++) s_planePx[i] = -10000;   // reset hit targets
    for (int i = 0; i < s_flightCount; i++) {
        int dist = s_flightRows[i].distNm;
        if (dist > range) continue;                 // outside the current zoom
        float rr = (float)dist / (float)range * (float)radiusPx;
        float br = s_flightRows[i].bearing * DEG2RAD;
        int px = cx + (int)lroundf(rr * sinf(br));
        int py = cy - (int)lroundf(rr * cosf(br));
        s_planePx[i] = (lv_coord_t)px; s_planePy[i] = (lv_coord_t)py;

        // Flown-path breadcrumb trail (older segments fade out).
        int tk = trail_find(flight_id(s_flightRows[i]));
        if (tk >= 0 && s_trails[tk].len >= 2) {
            const PlaneTrail &tr = s_trails[tk];
            float sc = (float)radiusPx / (float)range;
            lv_draw_line_dsc_t trl; lv_draw_line_dsc_init(&trl);
            trl.width = 2; trl.round_start = 1; trl.round_end = 1;
            trl.color = lv_color_hex(0x35708f);
            for (int p = 1; p < tr.len; p++) {
                lv_point_t tp[2] = {
                    {(lv_coord_t)(cx + (int)lroundf(tr.xnm[p - 1] * sc)),
                     (lv_coord_t)(cy - (int)lroundf(tr.ynm[p - 1] * sc))},
                    {(lv_coord_t)(cx + (int)lroundf(tr.xnm[p]     * sc)),
                     (lv_coord_t)(cy - (int)lroundf(tr.ynm[p]     * sc))}
                };
                trl.opa = (lv_opa_t)(40 + (215 * p) / (tr.len - 1));
                lv_canvas_draw_line(cv, tp, 2, &trl);
            }
        }

        int trk = s_flightRows[i].track;
        if (trk >= 0) {
            float th = trk * DEG2RAD;
            float fx = sinf(th), fy = -cosf(th);   // heading forward vector (north up)
            float gx = -fy,      gy = fx;          // perpendicular (right of heading)

            // Leader line: project ~2 min ahead at current ground speed.
            int gs = s_flightRows[i].gs;
            if (gs > 0) {
                float leadPx = (gs / 60.0f * 2.0f) / (float)range * (float)radiusPx;
                if (leadPx > radiusPx) leadPx = radiusPx;
                if (leadPx > 4.0f) {
                    lv_point_t lp[2] = {
                        {(lv_coord_t)px, (lv_coord_t)py},
                        {(lv_coord_t)(px + (int)lroundf(fx * leadPx)),
                         (lv_coord_t)(py + (int)lroundf(fy * leadPx))}
                    };
                    lv_canvas_draw_line(cv, lp, 2, &lead);
                }
            }

            lv_point_t tri[3] = {
                {(lv_coord_t)(px + lroundf(fx * 9)),                   (lv_coord_t)(py + lroundf(fy * 9))},
                {(lv_coord_t)(px - lroundf(fx * 6) + lroundf(gx * 5)), (lv_coord_t)(py - lroundf(fy * 6) + lroundf(gy * 5))},
                {(lv_coord_t)(px - lroundf(fx * 6) - lroundf(gx * 5)), (lv_coord_t)(py - lroundf(fy * 6) - lroundf(gy * 5))},
            };
            lv_canvas_draw_polygon(cv, tri, 3, &arrow);
        } else {
            lv_canvas_draw_rect(cv, px - 3, py - 3, 6, 6, &dot);
        }

        char lbl[24];
        snprintf(lbl, sizeof(lbl), "%d %s", i + 1, s_flightRows[i].tail.c_str());
        lv_canvas_draw_text(cv, px + 6, py - 7, 90, &plbl, lbl);
    }

    lv_draw_rect_dsc_t home; lv_draw_rect_dsc_init(&home);
    home.bg_color = lv_color_hex(0x7fd1ff); home.bg_opa = LV_OPA_COVER; home.radius = LV_RADIUS_CIRCLE;
    lv_canvas_draw_rect(cv, cx - 4, cy - 4, 8, 8, &home);
}

void ui_flights_set(FlightRow *rows, int count) {
    if (count > UI_MAX_FLIGHTS) count = UI_MAX_FLIGHTS;
    s_flightCount = count;
    for (int i = 0; i < count; i++) s_flightRows[i] = rows[i];

    // Record flown-path history in zoom-independent NM coords, keyed by identity.
    s_trailPoll++;
    for (int i = 0; i < count; i++) {
        String id = flight_id(rows[i]);
        if (!id.length()) continue;
        float br = rows[i].bearing * DEG2RAD;
        float xn = rows[i].distNm * sinf(br);
        float yn = rows[i].distNm * cosf(br);
        int k = trail_find(id);
        if (k < 0) {
            for (int j = 0; j < UI_MAX_FLIGHTS && k < 0; j++)
                if (!s_trails[j].used) k = j;
            if (k < 0) {                       // evict least-recently-seen slot
                k = 0; uint32_t oldest = s_trails[0].lastSeen;
                for (int j = 1; j < UI_MAX_FLIGHTS; j++)
                    if (s_trails[j].lastSeen < oldest) { oldest = s_trails[j].lastSeen; k = j; }
            }
            s_trails[k].used = true; s_trails[k].id = id; s_trails[k].len = 0;
        }
        PlaneTrail &tr = s_trails[k];
        bool moved = tr.len == 0 ||
                     fabsf(xn - tr.xnm[tr.len - 1]) > 0.05f ||
                     fabsf(yn - tr.ynm[tr.len - 1]) > 0.05f;
        if (moved) {
            if (tr.len >= TRAIL_N) {
                for (int p = 1; p < TRAIL_N; p++) { tr.xnm[p - 1] = tr.xnm[p]; tr.ynm[p - 1] = tr.ynm[p]; }
                tr.len = TRAIL_N - 1;
            }
            tr.xnm[tr.len] = xn; tr.ynm[tr.len] = yn; tr.len++;
        }
        tr.lastSeen = s_trailPoll;
    }
    for (int j = 0; j < UI_MAX_FLIGHTS; j++)    // drop trails for departed aircraft
        if (s_trails[j].used && s_trails[j].lastSeen != s_trailPoll) {
            s_trails[j].used = false; s_trails[j].id = "";
        }

    if (s_flightsTable) {
        lv_table_set_row_cnt(s_flightsTable, count + 1);
        for (int i = 0; i < count; i++) {
            char n[8]; snprintf(n, sizeof(n), "%d", i + 1);
            lv_table_set_cell_value(s_flightsTable, i + 1, 0, n);
            lv_table_set_cell_value(s_flightsTable, i + 1, 1, rows[i].tail.c_str());
            lv_table_set_cell_value(s_flightsTable, i + 1, 2, rows[i].type.c_str());
            char a[12]; snprintf(a, sizeof(a), "%d", rows[i].altFt);
            lv_table_set_cell_value(s_flightsTable, i + 1, 3, a);
            char d[12]; snprintf(d, sizeof(d), "%d NM", rows[i].distNm);
            lv_table_set_cell_value(s_flightsTable, i + 1, 4, d);
            char b[12]; snprintf(b, sizeof(b), "%d", rows[i].bearing);
            lv_table_set_cell_value(s_flightsTable, i + 1, 5, b);
        }
    }

    char st[64];
    snprintf(st, sizeof(st), "%d aircraft nearby   data: adsb.fi", count);
    if (s_flightsStatus) {
        lv_label_set_text(s_flightsStatus, st);
        lv_obj_set_style_text_color(s_flightsStatus, lv_color_hex(0x8b97b0), 0);
    }
    s_flightsHave = true;                              // enable offline last-good (#1)
    page_set_loading(PAGE_FLIGHTS, false);

    draw_radar();
    update_flight_hero();
}

void ui_flights_error(const String &msg) {
    page_set_loading(PAGE_FLIGHTS, false);
    if (s_flightsHave) {                               // keep radar/table; just flag staleness (#1)
        if (s_flightsStatus) {
            char st[80]; snprintf(st, sizeof(st), "%s  (offline - last update)", msg.c_str());
            lv_label_set_text(s_flightsStatus, st);
            lv_obj_set_style_text_color(s_flightsStatus, lv_color_hex(0xffb454), 0);
        }
        return;
    }
    if (s_flightsStatus) lv_label_set_text(s_flightsStatus, msg.c_str());
}

void ui_tickers_set(TickerRow *rows, int count) {
    if (!s_tkList) return;
    if (count > 8) count = 8;
    int ok = 0;
    for (int i = 0; i < 8; i++) {
        if (!s_tkCard[i]) continue;
        if (i >= count) { lv_obj_add_flag(s_tkCard[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_clear_flag(s_tkCard[i], LV_OBJ_FLAG_HIDDEN);
        TickerRow &r = rows[i];

        lv_label_set_text(s_tkSym[i], r.symbol.c_str());
        lv_label_set_text(s_tkName[i], r.name.length() ? r.name.c_str() : " ");
        lv_label_set_text(s_tkState[i], r.live ? "LIVE" : "CLOSED");
        lv_obj_set_style_text_color(s_tkState[i],
            lv_color_hex(r.live ? 0x39d98a : 0x8b97b0), 0);

        if (!r.valid) {
            lv_label_set_text(s_tkPrice[i], "--");
            lv_label_set_text(s_tkChange[i], "");
            lv_canvas_fill_bg(s_tkSpark[i], lv_color_hex(0x141c2e), LV_OPA_COVER);
            lv_label_set_text(s_tkLo[i], "");
            lv_label_set_text(s_tkHi[i], "");
            continue;
        }
        ok++;

        char p[16]; snprintf(p, sizeof(p), "$%.2f", r.price);
        lv_label_set_text(s_tkPrice[i], p);

        bool up = r.changeAbs >= 0;
        char ch[48];
        snprintf(ch, sizeof(ch), "%s %+.2f (%+.2f%%)",
                 up ? LV_SYMBOL_UP : LV_SYMBOL_DOWN, r.changeAbs, r.changePct);
        lv_label_set_text(s_tkChange[i], ch);
        lv_obj_set_style_text_color(s_tkChange[i],
            lv_color_hex(up ? 0x39d98a : 0xff5c5c), 0);

        draw_sparkline(i, r);

        char lo[16], hi[16];
        snprintf(lo, sizeof(lo), "L $%.2f", r.winLo);
        snprintf(hi, sizeof(hi), "H $%.2f", r.winHi);
        lv_label_set_text(s_tkLo[i], lo);
        lv_label_set_text(s_tkHi[i], hi);

        double span = r.winHi - r.winLo;
        if (span < 1e-9) span = 1.0;
        double frac = (r.price - r.winLo) / span;
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        int dx = BAR_X + (int)(frac * (BAR_W - 10));
        lv_obj_align(s_tkBarDot[i], LV_ALIGN_TOP_LEFT, dx, BAR_Y - 2);
    }

    char st[64];
    snprintf(st, sizeof(st), "%d/%d quotes  %s  Yahoo", ok, count, TF_LABELS[s_tfIndex]);
    lv_label_set_text(s_tickersStatus, st);
    s_tickersHave = true;
    page_set_loading(PAGE_TICKERS, false);
}

void ui_tickers_error(const String &msg) {
    page_set_loading(PAGE_TICKERS, false);
    if (s_tickersStatus) lv_label_set_text(s_tickersStatus, msg.c_str());
}

void ui_calendar_set(CalEvent *events, int count) {
    if (!s_calList) return;
    if (count > CAL_CACHE_N) count = CAL_CACHE_N;
    s_calAllCount = count;
    for (int i = 0; i < count; i++) s_calAll[i] = events[i];
    if (s_calCard) lv_obj_add_flag(s_calCard, LV_OBJ_FLAG_HIDDEN);   // avoid a stale popup
    s_calHave = true;
    page_set_loading(PAGE_CALENDAR, false);
    cal_render();
}

void ui_calendar_error(const String &msg) {
    page_set_loading(PAGE_CALENDAR, false);
    for (int i = 0; i < UI_MAX_EVENTS; i++)
        if (s_calRow[i]) lv_obj_add_flag(s_calRow[i], LV_OBJ_FLAG_HIDDEN);
    s_calAllCount = 0;
    if (s_calHero) lv_obj_add_flag(s_calHero, LV_OBJ_FLAG_HIDDEN);
    if (s_calGrid) lv_obj_add_flag(s_calGrid, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 7; i++) if (s_calDow[i]) lv_obj_add_flag(s_calDow[i], LV_OBJ_FLAG_HIDDEN);
    if (s_calNav) lv_obj_add_flag(s_calNav, LV_OBJ_FLAG_HIDDEN);
    if (s_calCard) lv_obj_add_flag(s_calCard, LV_OBJ_FLAG_HIDDEN);
    if (s_calStatus) { lv_obj_clear_flag(s_calStatus, LV_OBJ_FLAG_HIDDEN);
                       lv_label_set_text(s_calStatus, msg.c_str()); }
}

// Ease an arc's indicator from its current value to a target for a smooth sweep.
static void arc_set_value_cb(void *arc, int32_t v) { lv_arc_set_value((lv_obj_t *)arc, (int16_t)v); }
static void anim_arc_to(lv_obj_t *arc, int target) {
    if (!arc) return;
    int cur = lv_arc_get_value(arc);
    if (cur == target) return;
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, arc_set_value_cb);
    lv_anim_set_values(&a, cur, target);
    lv_anim_set_time(&a, 500);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

// Re-sweep an arc/bar from zero to its current value (for a reveal on panel focus).
static void arc_replay(lv_obj_t *arc) {
    if (!arc) return;
    int target = lv_arc_get_value(arc);
    lv_arc_set_value(arc, 0);
    anim_arc_to(arc, target);
}
static void bar_replay(lv_obj_t *bar) {
    if (!bar) return;
    int target = lv_bar_get_value(bar);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_bar_set_value(bar, target, LV_ANIM_ON);
}
static void replay_page_anims(Page p) {
    if (p == PAGE_AIR) {
        arc_replay(s_airAqiArc);
        arc_replay(s_airUvArc);
    } else if (p == PAGE_DIAG) {
        bar_replay(s_diagRamBar);
        bar_replay(s_diagPsramBar);
        bar_replay(s_diagFlashBar);
    }
}

void ui_air_set(int usAqi, float pm25, float pm10, float o3, float no2) {
    int band = aqi_band(usAqi);
    if (s_airAqiArc) {
        int v = usAqi; if (v < 0) v = 0; if (v > 300) v = 300;
        anim_arc_to(s_airAqiArc, v);
        lv_obj_set_style_arc_color(s_airAqiArc, lv_color_hex(AQI_COLORS[band]), LV_PART_INDICATOR);
    }
    if (s_airAqi) {
        char a[8]; snprintf(a, sizeof(a), "%d", usAqi);
        lv_label_set_text(s_airAqi, a);
        lv_obj_set_style_text_color(s_airAqi, lv_color_hex(AQI_COLORS[band]), 0);
    }
    if (s_airCat) {
        lv_label_set_text(s_airCat, AQI_CATS[band]);
        lv_obj_set_style_text_color(s_airCat, lv_color_hex(AQI_COLORS[band]), 0);
    }
    char v[24];
    if (s_airPm25) { snprintf(v, sizeof(v), "%.1f ug/m3", pm25); lv_label_set_text(s_airPm25, v); }
    if (s_airPm10) { snprintf(v, sizeof(v), "%.1f ug/m3", pm10); lv_label_set_text(s_airPm10, v); }
    if (s_airO3)   { snprintf(v, sizeof(v), "%.1f ug/m3", o3);   lv_label_set_text(s_airO3, v); }
    if (s_airNo2)  { snprintf(v, sizeof(v), "%.1f ug/m3", no2);  lv_label_set_text(s_airNo2, v); }
    if (s_airStatus) {
        const String &loc = settings().locationName;
        char st[64];
        if (loc.length()) snprintf(st, sizeof(st), "%s   data: Open-Meteo", loc.c_str());
        else snprintf(st, sizeof(st), "%.3f, %.3f   data: Open-Meteo",
                      settings().homeLat, settings().homeLon);
        lv_label_set_text(s_airStatus, st);
        lv_obj_set_style_text_color(s_airStatus, lv_color_hex(0x8b97b0), 0);
    }
    s_airHave = true;                                  // enable offline last-good (#1)
    page_set_loading(PAGE_AIR, false);
}

void ui_air_error(const String &msg) {
    page_set_loading(PAGE_AIR, false);
    if (s_airHave) {                                   // keep gauges/values; flag staleness (#1)
        if (s_airStatus) {
            char st[80]; snprintf(st, sizeof(st), "%s  (offline - last update)", msg.c_str());
            lv_label_set_text(s_airStatus, st);
            lv_obj_set_style_text_color(s_airStatus, lv_color_hex(0xffb454), 0);
        }
        return;
    }
    if (s_airStatus) lv_label_set_text(s_airStatus, msg.c_str());
}

void ui_air_uv_set(float uvIndex) {
    uint32_t col;
    if      (uvIndex < 3)  col = 0x00e400;   // low
    else if (uvIndex < 6)  col = 0xffff00;   // moderate
    else if (uvIndex < 8)  col = 0xff7e00;   // high
    else if (uvIndex < 11) col = 0xff0000;   // very high
    else                   col = 0x8f3f97;   // extreme
    if (s_airUvArc) {
        int v = (int)lroundf(uvIndex); if (v < 0) v = 0; if (v > 12) v = 12;
        anim_arc_to(s_airUvArc, v);
        lv_obj_set_style_arc_color(s_airUvArc, lv_color_hex(col), LV_PART_INDICATOR);
    }
    if (s_airUv) {
        char b[8]; snprintf(b, sizeof(b), "%.0f", uvIndex);
        lv_label_set_text(s_airUv, b);
        lv_obj_set_style_text_color(s_airUv, lv_color_hex(col), 0);
    }
}

// Format minutes-since-midnight as a clock string honoring the 12/24h setting.
static void fmt_hm(int minOfDay, char *out, size_t n) {
    if (minOfDay < 0) { snprintf(out, n, "--"); return; }
    int h = minOfDay / 60, m = minOfDay % 60;
    if (settings().use24hClock) { snprintf(out, n, "%02d:%02d", h, m); return; }
    const char *ap = h < 12 ? "AM" : "PM";
    int h12 = h % 12; if (h12 == 0) h12 = 12;
    snprintf(out, n, "%d:%02d %s", h12, m, ap);
}

static const char *MOON_NAMES[8] = {
    "New", "Waxing Crescent", "First Quarter", "Waxing Gibbous",
    "Full", "Waning Gibbous", "Last Quarter", "Waning Crescent" };

// Refresh the sun-path corner times, the daylight length + live next-event
// countdown, and the moon phase caption from the last sun poll + current time.
static void update_sun_labels() {
    if (s_srMin < 0 || s_ssMin < 0) return;
    char sr[12], ss[12];
    fmt_hm(s_srMin, sr, sizeof(sr));
    fmt_hm(s_ssMin, ss, sizeof(ss));
    if (s_sunRiseLbl) lv_label_set_text(s_sunRiseLbl, sr);
    if (s_sunSetLbl)  lv_label_set_text(s_sunSetLbl, ss);

    int dl = s_ssMin - s_srMin; if (dl < 0) dl += 1440;
    int now = sky_now_min();
    char line[72];
    if (now >= 0) {
        const char *evt; int mins;
        if      (now < s_srMin) { evt = "Sunrise in"; mins = s_srMin - now; }
        else if (now < s_ssMin) { evt = "Sunset in";  mins = s_ssMin - now; }
        else                    { evt = "Sunrise in"; mins = (s_srMin + 1440) - now; }
        snprintf(line, sizeof(line), "Daylight %dh %02dm   -   %s %dh %02dm",
                 dl / 60, dl % 60, evt, mins / 60, mins % 60);
    } else {
        snprintf(line, sizeof(line), "Daylight %dh %02dm", dl / 60, dl % 60);
    }
    if (s_sunLabel) lv_label_set_text(s_sunLabel, line);

    if (s_moonLabel) {
        int idx = s_moonIdx; if (idx < 0 || idx > 7) idx = 0;
        char mb[64];
        snprintf(mb, sizeof(mb), "Moon phase: %s   -   %d%% illuminated", MOON_NAMES[idx], s_moonPct);
        lv_label_set_text(s_moonLabel, mb);
    }
}

void ui_sun_set(int sunriseMin, int sunsetMin, int moonIdx, int illumPct) {
    s_srMin = sunriseMin; s_ssMin = sunsetMin;
    s_moonIdx = moonIdx; s_moonPct = illumPct;
    draw_sky(sky_now_min());
    update_sun_labels();
}

void ui_hourly_set(HourCell *cells, int count) {
    if (count > UI_HOURLY_N) count = UI_HOURLY_N;
    for (int i = 0; i < UI_HOURLY_N; i++) {
        if (!s_hrCell[i]) continue;
        if (i >= count) { lv_obj_add_flag(s_hrCell[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_clear_flag(s_hrCell[i], LV_OBJ_FLAG_HIDDEN);

        char h[8];
        int hr = cells[i].hour;
        if (settings().use24hClock) snprintf(h, sizeof(h), "%02d", hr);
        else {
            int h12 = hr % 12; if (h12 == 0) h12 = 12;
            snprintf(h, sizeof(h), "%d%c", h12, hr < 12 ? 'a' : 'p');
        }
        lv_label_set_text(s_hrHour[i], h);

        float f = cells[i].tempC * 9.0f / 5.0f + 32.0f;
        char t[8]; snprintf(t, sizeof(t), "%.0f", f);
        lv_label_set_text(s_hrTemp[i], t);

        char p[8];
        if (cells[i].precipPct > 0) snprintf(p, sizeof(p), "%d%%", cells[i].precipPct);
        else p[0] = '\0';
        lv_label_set_text(s_hrPrecip[i], p);
    }
}
