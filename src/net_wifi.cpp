// net_wifi.cpp
#include "net_wifi.h"
#include "settings.h"
#include "board_pins.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <DNSServer.h>
#include <ESPmDNS.h>

static const char *AP_SSID   = "CrowPanel-setup";
static const char *HOSTNAME  = "crowpanel";
static const IPAddress AP_IP(192, 168, 4, 1);
static const uint32_t CONNECT_TIMEOUT_MS = 15000;
static const uint32_t RETRY_INTERVAL_MS  = 30000;

static DNSServer s_dns;
static NetState  s_state = NetState::Booting;
static uint32_t  s_connectStart = 0;
static uint32_t  s_lastRetry = 0;

bool http_begin(WiFiClientSecure &client, HTTPClient &https,
                const String &url, const HttpOpts &opts) {
    client.setInsecure();               // keyless public APIs, no cert pinning
    client.setHandshakeTimeout(8);      // cap TLS stalls; the 120s default froze the UI
    https.setConnectTimeout(8000);      // bound the TCP connect too
    https.setTimeout(opts.readTimeoutMs);
    if (opts.userAgent) https.setUserAgent(opts.userAgent);
    if (opts.followRedirects) https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    return https.begin(client, url);
}

void net_check_factory_reset() {
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
    if (digitalRead(PIN_BOOT_BTN) != LOW) return;   // not held

    // Confirm a sustained ~5s hold so a momentary press doesn't wipe creds.
    uint32_t start = millis();
    while (digitalRead(PIN_BOOT_BTN) == LOW) {
        if (millis() - start >= 5000) {
            Serial.println("[net] BOOT held 5s -> Wi-Fi factory reset");
            settings_clear_wifi();
            return;
        }
        delay(50);
    }
}

void net_start_portal() {
    Serial.println("[net] starting AP captive portal");
    // AP_STA (not AP-only): the AP hosts the setup page while the station
    // interface stays available so /api/scan can list nearby networks.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID);
    WiFi.setTxPower(WIFI_POWER_13dBm);   // same brownout guard as the station path
    WiFi.scanNetworks(true);       // kick off a first async scan right away
    s_dns.start(53, "*", AP_IP);   // catch-all DNS -> phones open the portal
    s_state = NetState::Portal;
}

static void start_station() {
    Serial.printf("[net] connecting to \"%s\"\n", settings().wifiSsid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(settings().wifiSsid.c_str(), settings().wifiPass.c_str());
    // Cap TX power: at full power the WiFi-TX current spike during the TLS
    // handshake sags the 3.3V rail (RGB panel + backlight already loading it)
    // and browns the board out -> POWERON reset loop on the first HTTPS poll.
    // 13 dBm is ~6.5 dB below the default: enough link margin to move data,
    // low enough to avoid the brownout.
    WiFi.setTxPower(WIFI_POWER_13dBm);
    s_state = NetState::Connecting;
    s_connectStart = millis();
}

static void on_connected() {
    s_state = NetState::Connected;
    Serial.printf("[net] connected, IP %s\n", WiFi.localIP().toString().c_str());
    if (MDNS.begin(HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[net] mDNS: http://%s.local\n", HOSTNAME);
    }
}

void net_begin() {
    if (settings_has_wifi()) {
        start_station();
    } else {
        net_start_portal();
    }
}

void net_tick() {
    switch (s_state) {
        case NetState::Portal:
            s_dns.processNextRequest();
            break;

        case NetState::Connecting:
            if (WiFi.status() == WL_CONNECTED) {
                on_connected();
            } else if (millis() - s_connectStart > CONNECT_TIMEOUT_MS) {
                // Layer 2: couldn't join the saved network -> captive portal.
                Serial.println("[net] connect timeout -> AP fallback");
                net_start_portal();
            }
            break;

        case NetState::Connected:
            if (WiFi.status() != WL_CONNECTED) {
                s_state = NetState::Connecting;
                s_connectStart = millis();
                s_lastRetry = millis();
                WiFi.reconnect();
            }
            break;

        case NetState::Booting:
            break;
    }
}

NetState net_state()   { return s_state; }
String   net_hostname(){ return String(HOSTNAME) + ".local"; }

String net_ssid() {
    return (s_state == NetState::Portal) ? String(AP_SSID) : settings().wifiSsid;
}

String net_ip() {
    if (s_state == NetState::Portal)   return AP_IP.toString();
    if (s_state == NetState::Connected)return WiFi.localIP().toString();
    return "0.0.0.0";
}
