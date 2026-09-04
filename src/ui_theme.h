#pragma once
// Central UI palette + font tokens. Single source of truth for colors/fonts used
// across ui.cpp. Values are raw RGB; wrap at the call site: lv_color_hex(UI_COL_*).
// Only tokens that recur are named here; genuinely one-off decorative accents
// (sun glow, radar sweep, plane trails, per-metric spark colors) stay as literals
// at their single use site on purpose.

#include <cstdint>
#include "lvgl.h"

// --- Surfaces / backgrounds ---------------------------------------------------
constexpr uint32_t UI_COL_PAGE_BG   = 0x0f1420;   // page background
constexpr uint32_t UI_COL_CARD_BG   = 0x141c2e;   // card / tile / row background
constexpr uint32_t UI_COL_SURFACE   = 0x161d2e;   // topbar + home tile surface
constexpr uint32_t UI_COL_TRACK_BG  = 0x2a3550;   // bar / gauge track background

// --- Text ---------------------------------------------------------------------
constexpr uint32_t UI_COL_TEXT      = 0xe6ebf5;   // primary text
constexpr uint32_t UI_COL_TEXT_SEC  = 0xcdd6ea;   // secondary body text
constexpr uint32_t UI_COL_TEXT_DIM  = 0x9fb0cc;   // dim label text
constexpr uint32_t UI_COL_TEXT_MUTE = 0x8b97b0;   // muted / caption text
constexpr uint32_t UI_COL_WHITE     = 0xffffff;   // pure white emphasis

// --- Accents ------------------------------------------------------------------
constexpr uint32_t UI_COL_BTN_BG    = 0x24406a;   // dark-blue button / radar ring
constexpr uint32_t UI_COL_ACCENT    = 0x2f80ed;   // primary blue accent (calendar)
constexpr uint32_t UI_COL_ACCENT2   = 0x2f7bff;   // secondary blue accent (flights)
constexpr uint32_t UI_COL_ACCENT_LT = 0x7fb0ff;   // light-blue accent / outline
constexpr uint32_t UI_COL_ACCENT_CY = 0x7fd1ff;   // cyan accent (tags / links)
constexpr uint32_t UI_COL_GOOD      = 0x39d98a;   // green (good / distance)
constexpr uint32_t UI_COL_WARN      = 0xffb454;   // amber (warning / stale)

// --- Fonts --------------------------------------------------------------------
// Compiled sizes (see lv_conf.h): 12, 14, 16, 20, 28, 48. (22 is NOT compiled.)
#define UI_FONT_XS  (&lv_font_montserrat_12)
#define UI_FONT_SM  (&lv_font_montserrat_14)
#define UI_FONT_MD  (&lv_font_montserrat_16)
#define UI_FONT_LG  (&lv_font_montserrat_20)
#define UI_FONT_XL  (&lv_font_montserrat_28)
#define UI_FONT_XXL (&lv_font_montserrat_48)
