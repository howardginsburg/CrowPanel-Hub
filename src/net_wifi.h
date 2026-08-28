// net_wifi.h — Wi-Fi lifecycle + the 3-layer re-provisioning model.
//   1. Web form (Config tab QR -> crowpanel.local) writes creds to NVS.
//   2. Auto fallback: can't join saved network -> start the AP captive portal.
//   3. Hardware failsafe: hold BOOT ~5s at power-on -> wipe creds -> AP portal.
#pragma once
#include <Arduino.h>

enum class NetState {
    Booting,
    Connecting,
    Connected,     // joined the configured LAN (Config tab shows crowpanel.local)
    Portal,        // AP captive portal active (screen shows join instructions)
};

// Check the BOOT button at power-on; if held, wipe Wi-Fi creds (failsafe layer 3).
void net_check_factory_reset();

// Bring up networking. Starts a station connection if provisioned, otherwise the
// AP captive portal. Non-blocking beyond the initial connect attempt.
void net_begin();

// Drive reconnect/fallback state. Call periodically from loop().
void net_tick();

// Force the device into the AP captive portal (e.g. after "Forget Wi-Fi").
void net_start_portal();

NetState net_state();
String   net_ssid();      // connected SSID or the AP SSID in portal mode
String   net_ip();        // station IP, or 192.168.4.1 in portal mode
String   net_hostname();  // e.g. "crowpanel.local"
