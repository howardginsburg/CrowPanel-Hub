// ui_theme.cpp -- runtime palette + selectable themes (see ui_theme.h).
#include "ui_theme.h"

// Live palette; seeded with Midnight so any pre-boot draw is valid.
UiPalette g_theme = {
    0x0f1420, 0x141c2e, 0x161d2e, 0x2a3550,
    0xe6ebf5, 0xcdd6ea, 0x9fb0cc, 0x8b97b0, 0xffffff, 0xf6f9ff,
    0x24406a, 0x2f80ed, 0x2f7bff, 0x7fb0ff, 0x7fd1ff, 0x39d98a, 0xffb454,
};

// Field order matches UiPalette:
//   pageBg, cardBg, surface, trackBg,
//   text, textSec, textDim, textMute, white, onAccent,
//   btnBg, accent, accent2, accentLt, accentCy, good, warn
const UiPalette kThemes[THEME_COUNT] = {
    // Midnight -- original dark scheme, unchanged.
    { 0x0f1420, 0x141c2e, 0x161d2e, 0x2a3550,
      0xe6ebf5, 0xcdd6ea, 0x9fb0cc, 0x8b97b0, 0xffffff, 0xf6f9ff,
      0x24406a, 0x2f80ed, 0x2f7bff, 0x7fb0ff, 0x7fd1ff, 0x39d98a, 0xffb454 },
    // Graphite -- neutral, slightly warmer dark.
    { 0x14161a, 0x1e2127, 0x22252c, 0x3a3f49,
      0xeceef2, 0xccd0d8, 0x9aa0ab, 0x828892, 0xffffff, 0xf6f9ff,
      0x3a4250, 0x5b8cff, 0x6f9bff, 0x9cbcff, 0x74d0d6, 0x4bd6a0, 0xffb454 },
    // Daylight -- light, cool. "white" emphasis becomes near-black; accents darken
    // so they read on light surfaces.
    { 0xeef1f6, 0xffffff, 0xf4f7fc, 0xd2d9e6,
      0x1b2430, 0x39465a, 0x5b6675, 0x7a8598, 0x0b1220, 0xf6f9ff,
      0x2f6fe0, 0x2f6fe0, 0x2f7bff, 0x2f6fe0, 0x0e7aa8, 0x1f9d6b, 0xc9791f },
    // Parchment -- light, warm cream with a copper accent and teal links.
    { 0xf3ecdf, 0xfffaf0, 0xf6efe0, 0xddd2bd,
      0x2a2418, 0x4a4030, 0x6b5f48, 0x8a7d63, 0x1a150c, 0xfff6ea,
      0xb06a2c, 0xb06a2c, 0xc2792f, 0xb06a2c, 0x1f7a8c, 0x2f8f4e, 0xc9791f },
};

const char *const kThemeNames[THEME_COUNT] = {
    "Midnight", "Graphite", "Daylight", "Parchment",
};

void ui_theme_apply(uint8_t id) {
    if (id >= THEME_COUNT) id = THEME_MIDNIGHT;
    g_theme = kThemes[id];
}
