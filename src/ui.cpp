// ui.cpp — sidebar navigation shell + page construction.
#include "ui.h"
#include "settings.h"
#include "net_wifi.h"
#include "display.h"
#include "data.h"
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
static const int SIDEBAR_W = 150;
static const int RADAR_PX  = 400;

static lv_obj_t *s_pages[PAGE_COUNT];
static lv_obj_t *s_navBtns[PAGE_COUNT];
static Page      s_active = PAGE_HOME;

// Widgets we update at runtime
static lv_obj_t *s_clockTime;
static lv_obj_t *s_clockDate;
static lv_obj_t *s_weatherBody;
static lv_obj_t *s_wxIcon;
static lv_obj_t *s_wxTemp;
static lv_obj_t *s_wxLoc;
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
static bool      s_flightsShowMap = true;
static int       s_radarRangeNm = 0;                 // 0 = follow settings
static FlightRow s_flightRows[UI_MAX_FLIGHTS];
static int       s_flightCount = 0;
static lv_coord_t s_planePx[UI_MAX_FLIGHTS];         // radar-local hit targets
static lv_coord_t s_planePy[UI_MAX_FLIGHTS];
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
static lv_obj_t *s_calRow[UI_MAX_EVENTS];
static lv_obj_t *s_calWhen[UI_MAX_EVENTS];
static lv_obj_t *s_calTitle[UI_MAX_EVENTS];
static lv_obj_t *s_airStatus;                        // air-quality source line
static lv_obj_t *s_airAqi;                            // big US AQI number
static lv_obj_t *s_airCat;                            // AQI category text
static lv_obj_t *s_airPm25;
static lv_obj_t *s_airPm10;
static lv_obj_t *s_airO3;
static lv_obj_t *s_airNo2;
static lv_obj_t *s_airMotion;                        // PIR status
static lv_obj_t *s_airUv;                             // UV index value (Air page)
static lv_obj_t *s_sunLabel;                          // sunrise/sunset row (Home)
static lv_obj_t *s_moonLabel;                         // moon phase + illumination (Home)
static lv_obj_t *s_hrCell[UI_HOURLY_N];              // hourly strip cells (Home)
static lv_obj_t *s_hrHour[UI_HOURLY_N];
static lv_obj_t *s_hrTemp[UI_HOURLY_N];
static lv_obj_t *s_hrPrecip[UI_HOURLY_N];
static lv_obj_t *s_diagLbl;                           // diagnostics multiline body
static lv_obj_t *s_cfgState;
static lv_obj_t *s_cfgDetails;
static lv_obj_t *s_cfgQr;

static const char *PAGE_TITLES[PAGE_COUNT] = {
    "Home", "Flights", "Calendar", "Tickers", "Air", "Diag", "Config"
};
static const char *PAGE_ICONS[PAGE_COUNT] = {
    LV_SYMBOL_HOME, LV_SYMBOL_UP, LV_SYMBOL_LIST,
    LV_SYMBOL_CHARGE, LV_SYMBOL_EYE_OPEN, LV_SYMBOL_DRIVE, LV_SYMBOL_SETTINGS
};

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------
static void nav_event_cb(lv_event_t *e) {
    Page p = (Page)(intptr_t)lv_event_get_user_data(e);
    ui_show_page(p);
}
void ui_show_page(Page p) {
    if (p < 0 || p >= PAGE_COUNT) return;
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (i == p) lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        if (i == p) lv_obj_add_state(s_navBtns[i], LV_STATE_CHECKED);
        else        lv_obj_clear_state(s_navBtns[i], LV_STATE_CHECKED);
    }
    s_active = p;
}

Page ui_active_page() { return s_active; }

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static lv_obj_t *make_page(lv_obj_t *parent) {
    lv_obj_t *pg = lv_obj_create(parent);
    lv_obj_set_size(pg, LV_HOR_RES - SIDEBAR_W, LV_VER_RES);
    lv_obj_set_pos(pg, SIDEBAR_W, 0);
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

static void wx_sun(lv_obj_t *cv, int cx, int cy, int r, lv_color_t col) {
    lv_draw_line_dsc_t l; lv_draw_line_dsc_init(&l);
    l.color = col; l.width = 3; l.opa = LV_OPA_COVER; l.round_start = 1; l.round_end = 1;
    for (int a = 0; a < 360; a += 45) {
        float rad = a * DEG2RAD;
        lv_point_t p[2] = {
            {(lv_coord_t)(cx + cosf(rad) * (r + 3)), (lv_coord_t)(cy + sinf(rad) * (r + 3))},
            {(lv_coord_t)(cx + cosf(rad) * (r + 9)), (lv_coord_t)(cy + sinf(rad) * (r + 9))}};
        lv_canvas_draw_line(cv, p, 2, &l);
    }
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = col; d.bg_opa = LV_OPA_COVER; d.radius = LV_RADIUS_CIRCLE;
    lv_canvas_draw_rect(cv, cx - r, cy - r, 2 * r, 2 * r, &d);
}

static void wx_cloud(lv_obj_t *cv, int cx, int cy, int s, lv_color_t col) {
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

static void wx_rain(lv_obj_t *cv, int cx, int cy) {
    lv_draw_line_dsc_t l; lv_draw_line_dsc_init(&l);
    l.color = lv_color_hex(0x5aa0ff); l.width = 2; l.opa = LV_OPA_COVER;
    for (int k = -1; k <= 1; k++) {
        int x = cx + k * 8;
        lv_point_t p[2] = {{(lv_coord_t)(x + 3), (lv_coord_t)cy}, {(lv_coord_t)(x - 1), (lv_coord_t)(cy + 8)}};
        lv_canvas_draw_line(cv, p, 2, &l);
    }
}

static void wx_snow(lv_obj_t *cv, int cx, int cy) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_white(); d.bg_opa = LV_OPA_COVER; d.radius = LV_RADIUS_CIRCLE;
    for (int k = -1; k <= 1; k++) lv_canvas_draw_rect(cv, cx + k * 8 - 2, cy + 2, 4, 4, &d);
}

static void wx_fog(lv_obj_t *cv, int cx, int cy, int s) {
    lv_draw_line_dsc_t l; lv_draw_line_dsc_init(&l);
    l.color = lv_color_hex(0x8b97b0); l.width = 3; l.opa = LV_OPA_COVER; l.round_start = 1; l.round_end = 1;
    for (int k = 0; k < 3; k++) {
        int y = cy + k * 7;
        lv_point_t p[2] = {{(lv_coord_t)(cx - s), (lv_coord_t)y}, {(lv_coord_t)(cx + s), (lv_coord_t)y}};
        lv_canvas_draw_line(cv, p, 2, &l);
    }
}

static void wx_bolt(lv_obj_t *cv, int cx, int cy) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(0xffd23f); d.bg_opa = LV_OPA_COVER;
    lv_point_t z[6] = {
        {(lv_coord_t)(cx + 2), (lv_coord_t)cy},        {(lv_coord_t)(cx - 6), (lv_coord_t)(cy + 9)},
        {(lv_coord_t)(cx - 1), (lv_coord_t)(cy + 9)},  {(lv_coord_t)(cx - 4), (lv_coord_t)(cy + 17)},
        {(lv_coord_t)(cx + 7), (lv_coord_t)(cy + 5)},  {(lv_coord_t)(cx + 1), (lv_coord_t)(cy + 5)}};
    lv_canvas_draw_polygon(cv, z, 6, &d);
}

// Draw the icon for a weather code, centered at (cx,cy) with half-size s.
static void wx_draw(lv_obj_t *cv, int cx, int cy, int s, int code) {
    lv_color_t sun     = lv_color_hex(0xffd23f);
    lv_color_t cloud   = lv_color_hex(0xc7d0e0);
    lv_color_t cloudDk = lv_color_hex(0x9aa7bf);
    switch (wx_type(code)) {
        case WX_CLEAR:  wx_sun(cv, cx, cy, s * 55 / 100, sun); break;
        case WX_PARTLY: wx_sun(cv, cx - s * 35 / 100, cy - s * 30 / 100, s * 32 / 100, sun);
                        wx_cloud(cv, cx + s * 12 / 100, cy + s * 22 / 100, s * 42 / 100, cloud); break;
        case WX_CLOUD:  wx_cloud(cv, cx, cy, s * 52 / 100, cloud); break;
        case WX_FOG:    wx_cloud(cv, cx, cy - s * 18 / 100, s * 48 / 100, cloud);
                        wx_fog(cv, cx, cy + s * 42 / 100, s * 55 / 100); break;
        case WX_DRIZZLE:
        case WX_RAIN:   wx_cloud(cv, cx, cy - s * 22 / 100, s * 48 / 100, cloudDk);
                        wx_rain(cv, cx, cy + s * 40 / 100); break;
        case WX_SNOW:   wx_cloud(cv, cx, cy - s * 22 / 100, s * 48 / 100, cloud);
                        wx_snow(cv, cx, cy + s * 40 / 100); break;
        case WX_STORM:  wx_cloud(cv, cx, cy - s * 22 / 100, s * 48 / 100, cloudDk);
                        wx_bolt(cv, cx, cy + s * 30 / 100); break;
    }
}

// ---------------------------------------------------------------------------
// Page builders
// ---------------------------------------------------------------------------
static void build_home(lv_obj_t *pg) {
    // Clock + date (top-left).
    s_clockTime = lv_label_create(pg);
    lv_label_set_text(s_clockTime, "--:--");
    lv_obj_set_style_text_font(s_clockTime, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_clockTime, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_clockTime, LV_ALIGN_TOP_LEFT, 0, 6);

    s_clockDate = lv_label_create(pg);
    lv_label_set_text(s_clockDate, "Waiting for time sync...");
    lv_obj_set_style_text_font(s_clockDate, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_clockDate, lv_color_hex(0x8b97b0), 0);
    lv_obj_align(s_clockDate, LV_ALIGN_TOP_LEFT, 2, 66);

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
    lv_obj_set_style_text_align(s_wxLoc, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_wxLoc, LV_ALIGN_TOP_RIGHT, 0, 74);

    s_weatherBody = lv_label_create(pg);
    lv_label_set_text(s_weatherBody, "Loading conditions...");
    lv_obj_set_style_text_font(s_weatherBody, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_weatherBody, lv_color_hex(0xcdd6ea), 0);
    lv_obj_set_style_text_align(s_weatherBody, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_weatherBody, LV_ALIGN_TOP_RIGHT, 0, 100);

    // 5-day forecast strip (bottom): one card per day.
    const int CW    = LV_HOR_RES - SIDEBAR_W - 36;
    const int gap   = 8;
    const int cardW = (CW - gap * (UI_FORECAST_DAYS - 1)) / UI_FORECAST_DAYS;
    const int cardH = 132;
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

    // --- Sun/moon summary + hourly strip (middle band, above the 5-day cards) ---
    s_sunLabel = lv_label_create(pg);
    lv_label_set_text(s_sunLabel, "Sunrise --   Sunset --");
    lv_obj_set_style_text_font(s_sunLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_sunLabel, lv_color_hex(0xcdd6ea), 0);
    lv_obj_align(s_sunLabel, LV_ALIGN_TOP_LEFT, 0, 150);

    s_moonLabel = lv_label_create(pg);
    lv_label_set_text(s_moonLabel, "Moon --");
    lv_obj_set_style_text_font(s_moonLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_moonLabel, lv_color_hex(0x8b97b0), 0);
    lv_obj_align(s_moonLabel, LV_ALIGN_TOP_LEFT, 0, 174);

    const int CWh  = LV_HOR_RES - SIDEBAR_W - 36;
    const int hgap = 4;
    const int hcW  = (CWh - hgap * (UI_HOURLY_N - 1)) / UI_HOURLY_N;
    const int hcH  = 84;
    const int hcY  = 202;
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

static void flights_toggle_cb(lv_event_t *e) {
    s_flightsShowMap = !s_flightsShowMap;
    if (s_flightsShowMap) {
        lv_obj_clear_flag(s_flightsRadar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_flightsTable, LV_OBJ_FLAG_HIDDEN);
        if (s_zoomIn)  lv_obj_clear_flag(s_zoomIn, LV_OBJ_FLAG_HIDDEN);
        if (s_zoomOut) lv_obj_clear_flag(s_zoomOut, LV_OBJ_FLAG_HIDDEN);
        if (s_radarRangeLbl) lv_obj_clear_flag(s_radarRangeLbl, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_flightsToggleLbl, LV_SYMBOL_LIST " Table");
    } else {
        lv_obj_add_flag(s_flightsRadar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_flightsTable, LV_OBJ_FLAG_HIDDEN);
        if (s_zoomIn)  lv_obj_add_flag(s_zoomIn, LV_OBJ_FLAG_HIDDEN);
        if (s_zoomOut) lv_obj_add_flag(s_zoomOut, LV_OBJ_FLAG_HIDDEN);
        if (s_radarRangeLbl) lv_obj_add_flag(s_radarRangeLbl, LV_OBJ_FLAG_HIDDEN);
        if (s_flightCard) lv_obj_add_flag(s_flightCard, LV_OBJ_FLAG_HIDDEN);
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

    // Table (hidden until toggled).
    s_flightsTable = lv_table_create(pg);
    lv_obj_align(s_flightsTable, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_size(s_flightsTable, LV_HOR_RES - SIDEBAR_W - 36, LV_VER_RES - 72);
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
    tf_restyle();

    // Scrollable card list.
    s_tkList = lv_obj_create(pg);
    lv_obj_set_size(s_tkList, CW, LV_VER_RES - 44);
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
// Sidebar
// ---------------------------------------------------------------------------
static void build_sidebar(lv_obj_t *scr) {
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, SIDEBAR_W, LV_VER_RES);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x161d2e), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 8, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(bar);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, 50);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x161d2e), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2f7bff), LV_STATE_CHECKED);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_add_event_cb(btn, nav_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text_fmt(lbl, "%s  %s", PAGE_ICONS[i], PAGE_TITLES[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);

        s_navBtns[i] = btn;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
static void build_calendar(lv_obj_t *pg) {
    const int CW = LV_HOR_RES - SIDEBAR_W - 36;

    s_calStatus = lv_label_create(pg);
    lv_label_set_text(s_calStatus, "Loading calendar...");
    lv_obj_set_style_text_color(s_calStatus, lv_color_hex(0x8b97b0), 0);
    lv_obj_set_style_text_font(s_calStatus, &lv_font_montserrat_12, 0);
    lv_obj_align(s_calStatus, LV_ALIGN_TOP_LEFT, 0, 6);

    s_calList = lv_obj_create(pg);
    lv_obj_set_size(s_calList, CW, LV_VER_RES - 40);
    lv_obj_align(s_calList, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_opa(s_calList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_calList, 0, 0);
    lv_obj_set_style_pad_all(s_calList, 0, 0);
    lv_obj_set_scroll_dir(s_calList, LV_DIR_VER);

    const int rowH = 46, gap = 6, rowW = CW - 12;
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

        lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---------------------------------------------------------------------------
// Air quality (US AQI color bands, EPA)
// ---------------------------------------------------------------------------
static const uint32_t AQI_COLORS[6] = {
    0x00e400, 0xffff00, 0xff7e00, 0xff0000, 0x8f3f97, 0x7e0023 };
static const char *AQI_CATS[6] = {
    "Good", "Moderate", "Unhealthy for sensitive", "Unhealthy",
    "Very unhealthy", "Hazardous" };
static int aqi_band(int aqi) {
    if (aqi <= 50)  return 0;
    if (aqi <= 100) return 1;
    if (aqi <= 150) return 2;
    if (aqi <= 200) return 3;
    if (aqi <= 300) return 4;
    return 5;
}

static void build_air(lv_obj_t *pg) {
    const int CW = LV_HOR_RES - SIDEBAR_W - 36;

    s_airStatus = lv_label_create(pg);
    lv_label_set_text(s_airStatus, "Loading air quality...");
    lv_obj_set_style_text_color(s_airStatus, lv_color_hex(0x8b97b0), 0);
    lv_obj_set_style_text_font(s_airStatus, &lv_font_montserrat_12, 0);
    lv_obj_align(s_airStatus, LV_ALIGN_TOP_LEFT, 0, 6);

    s_airAqi = lv_label_create(pg);
    lv_label_set_text(s_airAqi, "--");
    lv_obj_set_style_text_font(s_airAqi, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_airAqi, lv_color_hex(0xe6ebf5), 0);
    lv_obj_align(s_airAqi, LV_ALIGN_TOP_LEFT, 0, 30);

    lv_obj_t *aqiUnit = lv_label_create(pg);
    lv_label_set_text(aqiUnit, "US AQI");
    lv_obj_set_style_text_font(aqiUnit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(aqiUnit, lv_color_hex(0x8b97b0), 0);
    lv_obj_align_to(aqiUnit, s_airAqi, LV_ALIGN_OUT_RIGHT_BOTTOM, 12, -10);

    s_airCat = lv_label_create(pg);
    lv_label_set_text(s_airCat, "");
    lv_obj_set_style_text_font(s_airCat, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_airCat, lv_color_hex(0xe6ebf5), 0);
    lv_obj_align(s_airCat, LV_ALIGN_TOP_LEFT, 0, 96);

    // UV index card (top-right).
    lv_obj_t *uvCard = lv_obj_create(pg);
    lv_obj_set_size(uvCard, 150, 92);
    lv_obj_align(uvCard, LV_ALIGN_TOP_RIGHT, 0, 30);
    lv_obj_set_style_bg_color(uvCard, lv_color_hex(0x141c2e), 0);
    lv_obj_set_style_border_width(uvCard, 0, 0);
    lv_obj_set_style_radius(uvCard, 8, 0);
    lv_obj_clear_flag(uvCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *uvName = lv_label_create(uvCard);
    lv_label_set_text(uvName, "UV Index");
    lv_obj_set_style_text_font(uvName, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(uvName, lv_color_hex(0x8b97b0), 0);
    lv_obj_align(uvName, LV_ALIGN_TOP_MID, 0, 4);

    s_airUv = lv_label_create(uvCard);
    lv_label_set_text(s_airUv, "--");
    lv_obj_set_style_text_font(s_airUv, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_airUv, lv_color_hex(0xe6ebf5), 0);
    lv_obj_align(s_airUv, LV_ALIGN_BOTTOM_MID, 0, -6);

    lv_obj_t **slots[4] = { &s_airPm25, &s_airPm10, &s_airO3, &s_airNo2 };
    const char *names[4] = { "PM2.5", "PM10", "Ozone", "NO2" };
    const int rowH = 40, gap = 6, y0 = 138, rowW = CW - 12;
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

    s_airMotion = lv_label_create(pg);
    lv_label_set_text(s_airMotion, "Motion: --");
    lv_obj_set_style_text_font(s_airMotion, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_airMotion, lv_color_hex(0x8b97b0), 0);
    lv_obj_align(s_airMotion, LV_ALIGN_TOP_LEFT, 0, y0 + 4 * (rowH + gap) + 10);
}

static void build_diag(lv_obj_t *pg) {
    lv_obj_t *title = lv_label_create(pg);
    lv_label_set_text(title, "Diagnostics");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xe6ebf5), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 2);

    s_diagLbl = lv_label_create(pg);
    lv_label_set_text(s_diagLbl, "Collecting device stats...");
    lv_obj_set_style_text_font(s_diagLbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_diagLbl, lv_color_hex(0xcdd6ea), 0);
    lv_obj_set_style_text_line_space(s_diagLbl, 6, 0);
    lv_obj_align(s_diagLbl, LV_ALIGN_TOP_LEFT, 0, 40);
}

void ui_init() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f1420), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_sidebar(scr);

    for (int i = 0; i < PAGE_COUNT; i++) s_pages[i] = make_page(scr);
    build_home(s_pages[PAGE_HOME]);
    build_flights(s_pages[PAGE_FLIGHTS]);
    build_calendar(s_pages[PAGE_CALENDAR]);
    build_tickers(s_pages[PAGE_TICKERS]);
    build_air(s_pages[PAGE_AIR]);
    build_diag(s_pages[PAGE_DIAG]);
    build_config(s_pages[PAGE_CONFIG]);

    ui_show_page(PAGE_HOME);
}

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
        "Free heap:  %u KB\n"
        "Free PSRAM: %u KB\n"
        "Wi-Fi:  %s\n"
        "SSID:   %s\n"
        "RSSI:   %d dBm\n"
        "IP:     %s\n"
        "MAC:    %s\n"
        "Reset:  %s\n"
        "Firmware: %s %s\n"
        "LVGL: %d.%d.%d",
        (unsigned long)dd, (unsigned long)hh, (unsigned long)mm, (unsigned long)ss,
        (unsigned)(heap / 1024), (unsigned)(psram / 1024),
        online ? "connected" : "offline",
        online ? WiFi.SSID().c_str() : "-",
        online ? (int)WiFi.RSSI() : 0,
        online ? WiFi.localIP().toString().c_str() : "-",
        WiFi.macAddress().c_str(),
        reset_reason_str(esp_reset_reason()),
        __DATE__, __TIME__,
        LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    lv_label_set_text(s_diagLbl, b);
}

void ui_tick() {
    static uint32_t last = 0;
    if (millis() - last < 500) return;
    last = millis();

    // Surface the right screen automatically as the network state changes:
    // entering the setup hotspot jumps to Config (join instructions + QR),
    // and coming online returns to the clock.
    static NetState prevState = NetState::Booting;
    NetState st = net_state();
    if (st != prevState) {
        if (st == NetState::Portal)         ui_show_page(PAGE_CONFIG);
        else if (st == NetState::Connected) ui_show_page(PAGE_HOME);
        prevState = st;
    }

    update_clock();
    update_config_page();
    update_diag_page();
}

// ---------------------------------------------------------------------------
// Data push hooks
// ---------------------------------------------------------------------------
void ui_weather_set(int code, const String &summary, float tempC, int humidity, float windKph) {
    float temp = tempC * 9.0f / 5.0f + 32.0f;
    float wind = windKph * 0.621371f;
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
    if (s_weatherBody) {
        char b[128];
        snprintf(b, sizeof(b), "%s\nHumidity %d%%\nWind %.0f mph",
                 summary.c_str(), humidity, wind);
        lv_label_set_text(s_weatherBody, b);
    }
    if (s_wxIcon) {
        lv_canvas_fill_bg(s_wxIcon, lv_color_hex(0x0f1420), LV_OPA_COVER);
        wx_draw(s_wxIcon, 48, 48, 38, code);
    }
}

void ui_weather_error(const String &msg) {
    if (s_weatherBody) lv_label_set_text(s_weatherBody, msg.c_str());
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

    lv_draw_rect_dsc_t arrow; lv_draw_rect_dsc_init(&arrow);
    arrow.bg_color = lv_color_hex(0xffb020); arrow.bg_opa = LV_OPA_COVER;
    lv_draw_rect_dsc_t dot; lv_draw_rect_dsc_init(&dot);
    dot.bg_color = lv_color_hex(0xffb020); dot.bg_opa = LV_OPA_COVER; dot.radius = LV_RADIUS_CIRCLE;
    lv_draw_label_dsc_t plbl; lv_draw_label_dsc_init(&plbl);
    plbl.color = lv_color_hex(0xe6ebf5); plbl.font = &lv_font_montserrat_12;

    for (int i = 0; i < UI_MAX_FLIGHTS; i++) s_planePx[i] = -10000;   // reset hit targets
    for (int i = 0; i < s_flightCount; i++) {
        int dist = s_flightRows[i].distNm;
        if (dist > range) continue;                 // outside the current zoom
        float rr = (float)dist / (float)range * (float)radiusPx;
        float br = s_flightRows[i].bearing * DEG2RAD;
        int px = cx + (int)lroundf(rr * sinf(br));
        int py = cy - (int)lroundf(rr * cosf(br));
        s_planePx[i] = (lv_coord_t)px; s_planePy[i] = (lv_coord_t)py;

        int trk = s_flightRows[i].track;
        if (trk >= 0) {
            float th = trk * DEG2RAD;
            float fx = sinf(th), fy = -cosf(th);   // heading forward vector (north up)
            float gx = -fy,      gy = fx;          // perpendicular (right of heading)
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
    if (s_flightsStatus) lv_label_set_text(s_flightsStatus, st);

    draw_radar();
}

void ui_flights_error(const String &msg) {
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
}

void ui_tickers_error(const String &msg) {
    if (s_tickersStatus) lv_label_set_text(s_tickersStatus, msg.c_str());
}

void ui_calendar_set(CalEvent *events, int count) {
    if (!s_calList) return;
    if (count > UI_MAX_EVENTS) count = UI_MAX_EVENTS;
    static const char *WD[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    bool h24 = settings().use24hClock;

    for (int i = 0; i < UI_MAX_EVENTS; i++) {
        if (!s_calRow[i]) continue;
        if (i >= count) { lv_obj_add_flag(s_calRow[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_clear_flag(s_calRow[i], LV_OBJ_FLAG_HIDDEN);

        time_t t = (time_t)events[i].start;
        struct tm tm; localtime_r(&t, &tm);
        int wd = tm.tm_wday; if (wd < 0 || wd > 6) wd = 0;
        char when[40];
        if (events[i].allDay) {
            snprintf(when, sizeof(when), "%s %d/%d\nAll day", WD[wd], tm.tm_mon + 1, tm.tm_mday);
        } else if (h24) {
            snprintf(when, sizeof(when), "%s %d/%d\n%02d:%02d",
                     WD[wd], tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
        } else {
            int h12 = tm.tm_hour % 12; if (h12 == 0) h12 = 12;
            snprintf(when, sizeof(when), "%s %d/%d\n%d:%02d %s",
                     WD[wd], tm.tm_mon + 1, tm.tm_mday, h12, tm.tm_min,
                     tm.tm_hour < 12 ? "AM" : "PM");
        }
        lv_label_set_text(s_calWhen[i], when);
        lv_label_set_text(s_calTitle[i],
            events[i].title.length() ? events[i].title.c_str() : "(no title)");
    }

    char st[48];
    snprintf(st, sizeof(st), "%d upcoming event%s", count, count == 1 ? "" : "s");
    lv_label_set_text(s_calStatus, st);
}

void ui_calendar_error(const String &msg) {
    for (int i = 0; i < UI_MAX_EVENTS; i++)
        if (s_calRow[i]) lv_obj_add_flag(s_calRow[i], LV_OBJ_FLAG_HIDDEN);
    if (s_calStatus) lv_label_set_text(s_calStatus, msg.c_str());
}

void ui_air_set(int usAqi, float pm25, float pm10, float o3, float no2) {
    int band = aqi_band(usAqi);
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
    }
}

void ui_air_error(const String &msg) {
    if (s_airStatus) lv_label_set_text(s_airStatus, msg.c_str());
}

void ui_air_motion(bool motion) {
    if (!s_airMotion) return;
    lv_label_set_text(s_airMotion, motion ? "Motion: detected" : "Motion: none");
    lv_obj_set_style_text_color(s_airMotion,
        lv_color_hex(motion ? 0x36d399 : 0x8b97b0), 0);
}

void ui_air_uv_set(float uvIndex) {
    if (!s_airUv) return;
    uint32_t col;
    if      (uvIndex < 3)  col = 0x00e400;   // low
    else if (uvIndex < 6)  col = 0xffff00;   // moderate
    else if (uvIndex < 8)  col = 0xff7e00;   // high
    else if (uvIndex < 11) col = 0xff0000;   // very high
    else                   col = 0x8f3f97;   // extreme
    char b[8]; snprintf(b, sizeof(b), "%.0f", uvIndex);
    lv_label_set_text(s_airUv, b);
    lv_obj_set_style_text_color(s_airUv, lv_color_hex(col), 0);
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

void ui_sun_set(int sunriseMin, int sunsetMin, int moonIdx, int illumPct) {
    if (s_sunLabel) {
        char sr[12], ss[12], b[48];
        fmt_hm(sunriseMin, sr, sizeof(sr));
        fmt_hm(sunsetMin,  ss, sizeof(ss));
        snprintf(b, sizeof(b), "Sunrise %s   Sunset %s", sr, ss);
        lv_label_set_text(s_sunLabel, b);
    }
    if (s_moonLabel) {
        if (moonIdx < 0 || moonIdx > 7) moonIdx = 0;
        char b[48];
        snprintf(b, sizeof(b), "Moon: %s (%d%%)", MOON_NAMES[moonIdx], illumPct);
        lv_label_set_text(s_moonLabel, b);
    }
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
