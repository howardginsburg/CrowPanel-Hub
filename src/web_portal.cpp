// web_portal.cpp
#include "web_portal.h"
#include "web_page.h"
#include "settings.h"
#include "net_wifi.h"
#include "display.h"
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>

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

static void handle_get_config(AsyncWebServerRequest *req) {
    if (!pin_ok(req)) { req->send(401, "application/json", "{\"error\":\"pin\"}"); return; }
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

    if (!pin_ok(req)) { req->send(401, "application/json", "{\"error\":\"pin\"}"); return; }

    String prevSsid = settings().wifiSsid;
    String prevPass = settings().wifiPass;
    if (!settings_import_json(body)) {
        req->send(400, "application/json", "{\"error\":\"json\"}");
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
        req->send_P(200, "text/html", CONFIG_PAGE);
    }
};

void web_portal_begin() {
    s_server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send_P(200, "text/html", CONFIG_PAGE);
    });
    s_server.on("/api/config", HTTP_GET, handle_get_config);
    s_server.on("/api/scan",   HTTP_GET, handle_scan);
    s_server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest *req) {}, nullptr, handle_post_config);

    // Common captive-portal probe URLs -> redirect to the page.
    auto probe = [](AsyncWebServerRequest *req) { req->send_P(200, "text/html", CONFIG_PAGE); };
    s_server.on("/generate_204", HTTP_GET, probe);   // Android
    s_server.on("/hotspot-detect.html", HTTP_GET, probe); // Apple
    s_server.on("/ncsi.txt", HTTP_GET, probe);       // Windows

    s_server.addHandler(new CaptiveHandler());
    s_server.onNotFound([](AsyncWebServerRequest *req) {
        req->send_P(200, "text/html", CONFIG_PAGE);
    });

    s_server.begin();
    Serial.println("[web] config portal listening on :80");
}

bool web_portal_consume_wifi_changed() {
    if (!s_wifiChanged) return false;
    s_wifiChanged = false;
    return true;
}
