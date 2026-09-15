#pragma once
// Central UI palette + font tokens. Single source of truth for colors/fonts used
// across ui.cpp. The UI_COL_* tokens are macros over one runtime palette (g_theme)
// so the whole UI can be re-skinned by swapping a struct at boot; call sites are
// unchanged (still lv_color_hex(UI_COL_*)). Only tokens that recur are named here;
// genuinely one-off decorative accents (sun glow, radar sweep, plane trails, sky
// scene, per-metric spark colors) stay as literals at their single use site.

#include <cstdint>
#include "lvgl.h"

// Live palette. 16 chrome colors + onAccent (text over a saturated accent/alert
// fill). Chosen from kThemes[] by ui_theme_apply() before the UI is built.
struct UiPalette {
    uint32_t pageBg, cardBg, surface, trackBg;                       // surfaces
    uint32_t text, textSec, textDim, textMute, white, onAccent;      // text
    uint32_t btnBg, accent, accent2, accentLt, accentCy, good, warn; // accents
};
extern UiPalette g_theme;

// --- Surfaces / backgrounds ---------------------------------------------------
#define UI_COL_PAGE_BG   (g_theme.pageBg)    // page background
#define UI_COL_CARD_BG   (g_theme.cardBg)    // card / tile / row background
#define UI_COL_SURFACE   (g_theme.surface)   // topbar + home tile surface
#define UI_COL_TRACK_BG  (g_theme.trackBg)   // bar / gauge track background

// --- Text ---------------------------------------------------------------------
#define UI_COL_TEXT      (g_theme.text)      // primary text
#define UI_COL_TEXT_SEC  (g_theme.textSec)   // secondary body text
#define UI_COL_TEXT_DIM  (g_theme.textDim)   // dim label text
#define UI_COL_TEXT_MUTE (g_theme.textMute)  // muted / caption text
#define UI_COL_WHITE     (g_theme.white)     // high-emphasis text on a themed surface
#define UI_COL_ON_ACCENT (g_theme.onAccent)  // text on a saturated accent/alert fill (stays light)

// --- Accents ------------------------------------------------------------------
#define UI_COL_BTN_BG    (g_theme.btnBg)     // dark-blue button / radar ring
#define UI_COL_ACCENT    (g_theme.accent)    // primary blue accent (calendar)
#define UI_COL_ACCENT2   (g_theme.accent2)   // secondary blue accent (flights)
#define UI_COL_ACCENT_LT (g_theme.accentLt)  // light-blue accent / outline
#define UI_COL_ACCENT_CY (g_theme.accentCy)  // cyan accent (tags / links)
#define UI_COL_GOOD      (g_theme.good)      // green (good / distance)
#define UI_COL_WARN      (g_theme.warn)      // amber (warning / stale)

// --- Themes -------------------------------------------------------------------
enum UiThemeId : uint8_t {
    THEME_MIDNIGHT = 0,   // default dark (original scheme)
    THEME_GRAPHITE,       // neutral dark
    THEME_DAYLIGHT,       // light, cool
    THEME_PARCHMENT,      // light, warm
    THEME_COUNT
};
extern const UiPalette   kThemes[THEME_COUNT];
extern const char *const kThemeNames[THEME_COUNT];

// Copy kThemes[id] into g_theme (clamped to a valid id). Call before building UI.
void ui_theme_apply(uint8_t id);

// --- Fonts --------------------------------------------------------------------
// Compiled sizes (see lv_conf.h): 12, 14, 16, 20, 28, 48. (22 is NOT compiled.)
#define UI_FONT_XS  (&lv_font_montserrat_12)
#define UI_FONT_SM  (&lv_font_montserrat_14)
#define UI_FONT_MD  (&lv_font_montserrat_16)
#define UI_FONT_LG  (&lv_font_montserrat_20)
#define UI_FONT_XL  (&lv_font_montserrat_28)
#define UI_FONT_XXL (&lv_font_montserrat_48)
