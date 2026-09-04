// photo.cpp — network photo frame decoder.
//
// Downloads a JPEG over HTTPS into a PSRAM scratch buffer, then decodes it with
// JPEGDEC directly into a PSRAM RGB565 framebuffer that ui.cpp binds to an LVGL
// canvas. The image is scaled down (power-of-two) to fit and centred; smaller
// images get a black border, larger ones are clipped.
#include "photo.h"
#include "settings.h"
#include "net_wifi.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <esp_heap_caps.h>

// Default keyless nature source. Returns a fresh random nature photo per request
// (302-redirects to a CDN image), so rotation shows a different photo each time.
static const char *PHOTO_DEFAULT_URL = "https://loremflickr.com/800/436/nature";

// Scratch buffer for the raw JPEG bytes. 512 KB comfortably holds an 800x436
// nature JPEG (typically 60-200 KB).
#define PHOTO_JPEG_MAX (512 * 1024)

static uint16_t *s_fb   = nullptr;   // RGB565 framebuffer (PHOTO_W * PHOTO_H)
static uint8_t  *s_jpg  = nullptr;   // JPEG download scratch
static char      s_status[48] = "Loading nature photo...";

static JPEGDEC   s_jpeg;
static int       s_offX = 0;         // centring offsets applied in the callback
static int       s_offY = 0;

uint16_t *photo_buffer() { return s_fb; }
int photo_width()  { return PHOTO_W; }
int photo_height() { return PHOTO_H; }
const char *photo_status() { return s_status; }

void photo_init() {
    if (!s_fb) {
        s_fb = (uint16_t *)ps_malloc((size_t)PHOTO_W * PHOTO_H * sizeof(uint16_t));
        if (s_fb) {
            // Dark neutral fill so the panel isn't garbage before the first load.
            for (size_t i = 0; i < (size_t)PHOTO_W * PHOTO_H; i++) s_fb[i] = 0x1082; // ~0x0f1420
        }
    }
    if (!s_jpg) s_jpg = (uint8_t *)ps_malloc(PHOTO_JPEG_MAX);
}

// JPEGDEC block callback: copy one decoded MCU block into the framebuffer,
// applying the centring offset and clipping to the panel bounds.
static int photo_draw_cb(JPEGDRAW *pDraw) {
    for (int row = 0; row < pDraw->iHeight; row++) {
        int dy = pDraw->y + row + s_offY;
        if (dy < 0 || dy >= PHOTO_H) continue;
        const uint16_t *src = pDraw->pPixels + row * pDraw->iWidth;
        uint16_t *dst = s_fb + (size_t)dy * PHOTO_W;
        for (int col = 0; col < pDraw->iWidth; col++) {
            int dx = pDraw->x + col + s_offX;
            if (dx < 0 || dx >= PHOTO_W) continue;
            dst[dx] = src[col];
        }
    }
    return 1;   // continue decoding
}

// Download the URL into s_jpg. Returns the byte count, or 0 on failure.
static size_t photo_download(const String &url) {
    WiFiClientSecure client;
    HTTPClient https;
    HttpOpts opts; opts.readTimeoutMs = 10000; opts.followRedirects = true;
    opts.userAgent = "CrowPanel/1.0";
    if (!http_begin(client, https, url, opts)) {
        snprintf(s_status, sizeof(s_status), "Bad URL");
        return 0;
    }

    int code = https.GET();
    if (code != HTTP_CODE_OK) {
        snprintf(s_status, sizeof(s_status), "HTTP %d", code);
        https.end();
        return 0;
    }

    int len = https.getSize();          // -1 when chunked
    Stream *stream = https.getStreamPtr();
    size_t got = 0;
    uint32_t last = millis();
    while (https.connected()) {
        size_t avail = stream->available();
        if (avail) {
            size_t room = PHOTO_JPEG_MAX - got;
            if (room == 0) { snprintf(s_status, sizeof(s_status), "Too large"); https.end(); return 0; }
            int n = stream->readBytes(s_jpg + got, avail < room ? avail : room);
            if (n > 0) { got += n; last = millis(); }
        }
        if (len > 0 && got >= (size_t)len) break;
        if (millis() - last > 8000) break;     // stall guard
        if (!avail) delay(1);
    }
    https.end();

    if (got < 100) { snprintf(s_status, sizeof(s_status), "Empty response"); return 0; }
    return got;
}

bool photo_fetch() {
    if (net_state() != NetState::Connected) { snprintf(s_status, sizeof(s_status), "Offline"); return false; }
    if (!s_fb || !s_jpg) { snprintf(s_status, sizeof(s_status), "No memory"); return false; }

    String url = settings().photoUrl; url.trim();
    if (!url.length()) url = PHOTO_DEFAULT_URL;

    size_t len = photo_download(url);
    if (!len) return false;

    if (s_jpeg.openRAM(s_jpg, (int)len, photo_draw_cb) == 0) {
        snprintf(s_status, sizeof(s_status), "Not a JPEG");
        return false;
    }
    s_jpeg.setPixelType(RGB565_LITTLE_ENDIAN);   // matches LV_COLOR_16_SWAP=0

    int iw = s_jpeg.getWidth();
    int ih = s_jpeg.getHeight();

    // Pick the largest power-of-two downscale that fits the panel.
    int opt = 0, sc = 1;
    while ((iw / sc > PHOTO_W || ih / sc > PHOTO_H) && sc < 8) sc *= 2;
    switch (sc) {
        case 2: opt = JPEG_SCALE_HALF;    break;
        case 4: opt = JPEG_SCALE_QUARTER; break;
        case 8: opt = JPEG_SCALE_EIGHTH;  break;
        default: opt = 0;                 break;
    }
    int dw = iw / sc, dh = ih / sc;
    s_offX = (PHOTO_W - dw) / 2;
    s_offY = (PHOTO_H - dh) / 2;

    // Clear to black first so borders (or a partial decode) look intentional.
    memset(s_fb, 0, (size_t)PHOTO_W * PHOTO_H * sizeof(uint16_t));

    int ok = s_jpeg.decode(0, 0, opt);
    s_jpeg.close();
    if (ok != 1) {
        snprintf(s_status, sizeof(s_status), "Decode failed");
        return false;
    }

    snprintf(s_status, sizeof(s_status), "OK");
    return true;
}
