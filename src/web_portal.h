// web_portal.h — async HTTP config server (captive portal + LAN config).
#pragma once

// Start the web server + endpoints. Call once after networking is up.
void web_portal_begin();

// True once the user has submitted new Wi-Fi credentials via the portal, so the
// main loop can trigger a reconnect. Reading it clears the flag.
bool web_portal_consume_wifi_changed();
