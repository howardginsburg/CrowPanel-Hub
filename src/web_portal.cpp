// web_portal.cpp
#include "web_portal.h"
#include "web_page.h"
#include "settings.h"
#include "net_wifi.h"
#include "display.h"
#include "board_pins.h"
#include "ui.h"
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

static AsyncWebServer s_server(80);
static volatile bool  s_wifiChanged = false;

// Simple optional PIN gate: require ?pin=XXXX (or X-Config-Pin header) when set.
static bool pin_ok(AsyncWebServerRequest *req) {
    const String &pin = settings().configPin;
    if (pin.length() == 0) return true;
    if (req->hasParam("pin") && req->getParam("pin")->value() == pin) return true;
    if (req->hasHeader("X-Config-Pin") && req->getHeader("X-Config-Pin")->value() == pin) return true;
    return false;
}

// Serve the config/captive page (root, captive probes, and unknown-host fallback).
static void send_config_page(AsyncWebServerRequest *req) {
    req->send(200, "text/html", CONFIG_PAGE);
}

// Uniform JSON error body: {"error":"<code>"}.
static void send_json_error(AsyncWebServerRequest *req, int status, const char *err) {
    req->send(status, "application/json", String("{\"error\":\"") + err + "\"}");
}

// Wrap a GET handler with the optional PIN gate.
static ArRequestHandlerFunction with_pin(ArRequestHandlerFunction fn) {
    return [fn](AsyncWebServerRequest *req) {
        if (!pin_ok(req)) { send_json_error(req, 401, "pin"); return; }
        fn(req);
    };
}

static void handle_get_config(AsyncWebServerRequest *req) {
    req->send(200, "application/json", settings_export_json());
}

static void handle_scan(AsyncWebServerRequest *req) {
    int n = WiFi.scanComplete();
    const bool fresh = req->hasParam("fresh");
    // Only START a scan when nothing is cached (-2 FAILED) or the user explicitly
    // asked to refresh. NEVER auto-restart after handing back results: a scan in
    // AP_STA mode forces the softAP off-channel for a few seconds and disconnects
    // the phone that's viewing this page (Android then bounces to Wi-Fi settings).
    if (n == WIFI_SCAN_FAILED || (fresh && n != WIFI_SCAN_RUNNING)) {
        WiFi.scanDelete();
        WiFi.scanNetworks(true);
        n = WIFI_SCAN_RUNNING;
    }
    StaticJsonDocument<2048> doc;
    JsonObject root = doc.to<JsonObject>();
    root["scanning"] = (n < 0);
    JsonArray arr = root.createNestedArray("nets");
    for (int i = 0; i < n && i < 20; i++) {
        JsonObject o = arr.createNestedObject();
        o["ssid"] = WiFi.SSID(i);
        o["rssi"] = WiFi.RSSI(i);
    }
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
}

// Body handler for POST /api/config (JSON).
static void handle_post_config(AsyncWebServerRequest *req, uint8_t *data, size_t len,
                               size_t index, size_t total) {
    static String body;
    if (index == 0) body = "";
    body.concat((const char *)data, len);
    if (index + len != total) return;   // wait for the full body

    if (!pin_ok(req)) { send_json_error(req, 401, "pin"); return; }

    String prevSsid = settings().wifiSsid;
    String prevPass = settings().wifiPass;
    if (!settings_import_json(body)) {
        send_json_error(req, 400, "json");
        return;
    }
    settings_save();
    display_set_brightness(settings().brightness);

    if (settings().wifiSsid != prevSsid || settings().wifiPass != prevPass) {
        s_wifiChanged = true;
    }
    req->send(200, "application/json", "{\"ok\":true}");
}

// Captive-portal helper: send any unknown host to the config page.
class CaptiveHandler : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest *req) const override {
        return net_state() == NetState::Portal;
    }
    void handleRequest(AsyncWebServerRequest *req) override {
        send_config_page(req);
    }
};

// GET /screenshot.bmp -> the live screen as a pixel-perfect 24-bit BMP. Grabs the
// on-screen framebuffer (under the LVGL lock so it isn't mid buffer-swap) and
// expands native RGB565 to bottom-up BGR888. Buffer is a one-time PSRAM alloc
// reused across requests (screenshots are a single-user debug feature).
static void handle_screenshot(AsyncWebServerRequest *req) {
    const uint32_t W = LCD_WIDTH, H = LCD_HEIGHT;
    const uint32_t rowBytes = W * 3;                 // 800*3 = 2400, already 4-byte aligned
    const uint32_t pixBytes = rowBytes * H;
    const uint32_t HDR = 54;                          // 14-byte file + 40-byte info header
    const uint32_t total = HDR + pixBytes;

    static uint8_t *buf = nullptr;
    if (!buf) buf = (uint8_t *)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!buf) { req->send(503, "text/plain", "no psram for screenshot"); return; }

    memset(buf, 0, HDR);
    auto put32 = [&](uint32_t o, uint32_t v) { buf[o]=v; buf[o+1]=v>>8; buf[o+2]=v>>16; buf[o+3]=v>>24; };
    auto put16 = [&](uint32_t o, uint16_t v) { buf[o]=v; buf[o+1]=v>>8; };
    buf[0] = 'B'; buf[1] = 'M';
    put32(2, total); put32(10, HDR);
    put32(14, 40); put32(18, W); put32(22, H); put16(26, 1); put16(28, 24);
    put32(34, pixBytes); put32(38, 2835); put32(42, 2835);   // 72 DPI

    ui_lock();
    const uint16_t *fb = (const uint16_t *)display_front_framebuffer();
    if (fb) {
        for (uint32_t y = 0; y < H; y++) {
            const uint16_t *src = fb + (uint32_t)y * W;
            uint8_t *dst = buf + HDR + (uint32_t)(H - 1 - y) * rowBytes;   // BMP is bottom-up
            for (uint32_t x = 0; x < W; x++) {
                uint16_t p = src[x];
                uint8_t r5 = (p >> 11) & 0x1F, g6 = (p >> 5) & 0x3F, b5 = p & 0x1F;
                *dst++ = (b5 << 3) | (b5 >> 2);      // B
                *dst++ = (g6 << 2) | (g6 >> 4);      // G
                *dst++ = (r5 << 3) | (r5 >> 2);      // R
            }
        }
    }
    ui_unlock();

    if (!fb) { req->send(503, "text/plain", "no frame yet"); return; }

    AsyncWebServerResponse *res = req->beginResponse(200, "image/bmp", buf, total);
    res->addHeader("Content-Disposition", "inline; filename=\"crowpanel.bmp\"");
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
}

void web_portal_begin() {
    s_server.on("/", HTTP_GET, send_config_page);
    s_server.on("/api/config", HTTP_GET, with_pin(handle_get_config));
    s_server.on("/api/scan",   HTTP_GET, handle_scan);
    s_server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest *req) {}, nullptr, handle_post_config);
    s_server.on("/screenshot.bmp", HTTP_GET, with_pin(handle_screenshot));

    // Common captive-portal probe URLs (Android/Apple/Windows) -> the config page.
    const char *probes[] = { "/generate_204", "/hotspot-detect.html", "/ncsi.txt" };
    for (const char *p : probes) s_server.on(p, HTTP_GET, send_config_page);

    s_server.addHandler(new CaptiveHandler());
    s_server.onNotFound(send_config_page);

    s_server.begin();
    Serial.println("[web] config portal listening on :80");
}

bool web_portal_consume_wifi_changed() {
    if (!s_wifiChanged) return false;
    s_wifiChanged = false;
    return true;
}
