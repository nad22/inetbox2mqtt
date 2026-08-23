// inetbox2mqtt - C++ / PlatformIO rewrite for ESP32
//
// Simulates a TRUMA "inetbox" on the LIN bus so that a TRUMA CPplus panel
// talks to this ESP32 instead of a real inetbox. Status is published to an
// MQTT broker (with Home Assistant auto-discovery) and can also be watched /
// controlled from a built-in web page - there is only ever this one mode,
// unlike the legacy MicroPython "OS mode" / "normal mode" split.
//
// See README.md for wiring, setup and MQTT topic documentation.

#include <Arduino.h>
#include <WiFi.h>

#include "Pins.h"
#include "AppConfig.h"
#include "TrumaStatus.h"
#include "LinBus.h"
#include "MqttManager.h"
#include "WebPortal.h"
#include "CommandLog.h"
#include "Version.h"

static AppConfig g_config;
static TrumaStatus g_status;
static LinBus g_lin;
static MqttManager g_mqtt;
static WebPortal g_web;

static uint32_t g_lastFullPublishMs = 0;
static uint32_t g_lastChangedPublishMs = 0;
static const uint32_t FULL_PUBLISH_INTERVAL_MS = 10000;
static const uint32_t CHANGED_PUBLISH_INTERVAL_MS = 2000;

static void setLed(int pin, bool on) {
    if (pin < 0) return;
    digitalWrite(pin, on ? HIGH : LOW);
}

// Logs the low-level disconnect reason from the WiFi driver. This is far
// more informative than WiFi.status() alone - in particular a wrong
// password usually shows up here as AUTH_EXPIRE(2)/AUTH_FAIL(202) or
// 4WAY_HANDSHAKE_TIMEOUT(15), while an unreachable/too-weak AP shows up as
// NO_AP_FOUND(201) or BEACON_TIMEOUT(200).
static void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        Serial.printf("[WiFi] disconnected, reason=%d (2/202=auth/password rejected, "
                      "15/204=handshake timeout - often also a wrong password, "
                      "200=beacon timeout - AP out of range/wrong band, 201=SSID not found)\n",
                      (int)info.wifi_sta_disconnected.reason);
    }
}

// Tries to join the configured WiFi network for a bounded time. If that
// fails (or nothing is configured yet) an Access Point is started so the
// device can still be reached and configured via the web UI.
static void connectWifi() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.persistent(false);  // don't let the WiFi driver's own NVS blob fight with our AppConfig
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);  // avoid modem-sleep related connection hiccups
    WiFi.onEvent(onWifiEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    if (g_config.isWifiConfigured()) {
        Serial.printf("[WiFi] connecting to '%s' (password length=%u) ...\n",
                      g_config.wifiSsid.c_str(), g_config.wifiPassword.length());
        WiFi.disconnect(true, false);
        delay(100);
        WiFi.begin(g_config.wifiSsid.c_str(), g_config.wifiPassword.c_str());
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
            delay(250);
            setLed(PIN_LED_WIFI, (millis() / 250) % 2 == 0);
        }
        if (WiFi.status() != WL_CONNECTED) {
            Serial.printf("[WiFi] connect failed, status=%d (see WiFiType.h wl_status_t; "
                          "3=connected, 4=wrong password, 1/6=no such network)\n",
                          (int)WiFi.status());
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] connected, IP=%s\n", WiFi.localIP().toString().c_str());
        WiFi.softAPdisconnect(true);
        setLed(PIN_LED_WIFI, true);
    } else {
        String apName = "inetbox2mqtt-" + String((uint32_t)ESP.getEfuseMac(), HEX);
        Serial.printf("[WiFi] starting fallback AP '%s' (open, no password)\n", apName.c_str());
        WiFi.softAP(apName.c_str());
        Serial.printf("[WiFi] AP IP=%s - open http://%s to configure\n",
                      WiFi.softAPIP().toString().c_str(), WiFi.softAPIP().toString().c_str());
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("\ninetbox2mqtt %s starting...\n", FW_VERSION);
    CommandLog::add("system", "info", String("Firmware gestartet, Version ") + FW_VERSION);

    if (PIN_LED_WIFI >= 0) pinMode(PIN_LED_WIFI, OUTPUT);
    if (PIN_LED_MQTT >= 0) pinMode(PIN_LED_MQTT, OUTPUT);

    g_config = AppConfigStore::load();

    connectWifi();

    g_lin.begin(&g_status, PIN_LED_LIN);
    g_mqtt.begin(g_config, &g_status);
    g_web.begin(g_config, &g_status, &g_lin, &g_mqtt);

    Serial.println("[setup] done, entering main loop");
}

void loop() {
    // LIN bus: must be serviced as fast as possible, it is a soft-realtime
    // protocol without any built-in buffering beyond a handful of bytes.
    g_lin.loop();

    g_mqtt.loop();
    setLed(PIN_LED_MQTT, g_mqtt.isConnected());

    uint32_t now = millis();
    if (now - g_lastFullPublishMs >= FULL_PUBLISH_INTERVAL_MS) {
        g_lastFullPublishMs = now;
        g_mqtt.publish(false);
    } else if (now - g_lastChangedPublishMs >= CHANGED_PUBLISH_INTERVAL_MS) {
        g_lastChangedPublishMs = now;
        g_mqtt.publish(true);
    }

    if (g_web.consumeRebootRequest()) {
        Serial.println("[main] reboot requested via web UI");
        delay(300);
        ESP.restart();
    }
}
