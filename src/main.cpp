// main.cpp — CrowPanel 5.0" Desk Command Center entry point.
#include <Arduino.h>
#include "board_pins.h"
#include "settings.h"
#include "display.h"
#include "touch_gt911.h"
#include "ui.h"
#include "net_wifi.h"
#include "web_portal.h"
#include "data.h"

// The default 8 KB loop stack overflows when poll_calendar()'s frame overlaps a
// TLS handshake (mbedtls is stack-hungry); give loopTask room.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[boot] CrowPanel 5.0\" Desk Command Center");

    // Layer-3 Wi-Fi failsafe: hold BOOT at power-on to wipe credentials.
    net_check_factory_reset();

    settings_load();

    touch_init();
    display_init();
    display_set_brightness(settings().brightness);
    ui_init();

    net_begin();
    web_portal_begin();

    Serial.println("[boot] ready");
}

void loop() {
    net_tick();

    // A portal-side Wi-Fi credential change should trigger a reconnect.
    if (web_portal_consume_wifi_changed()) {
        Serial.println("[main] Wi-Fi settings changed -> restarting to apply");
        delay(400);
        ESP.restart();
    }

    data_tick();
    ui_tick();
    display_tick();
    delay(5);
}
