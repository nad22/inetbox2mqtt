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

// Tries to join the configured WiFi network for a bounded time. If that
// fails (or nothing is configured yet) an Access Point is started so the
// device can still be reached and configured via the web UI.
static void connectWifi() {
    WiFi.mode(WIFI_AP_STA);

    if (g_config.isWifiConfigured()) {
        Serial.printf("[WiFi] connecting to '%s' ...\n", g_config.wifiSsid.c_str());
        WiFi.begin(g_config.wifiSsid.c_str(), g_config.wifiPassword.c_str());
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
            delay(250);
            setLed(PIN_LED_WIFI, (millis() / 250) % 2 == 0);
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] connected, IP=%s\n", WiFi.localIP().toString().c_str());
        WiFi.softAPdisconnect(true);
        setLed(PIN_LED_WIFI, true);
    } else {
        String apName = "inetbox2mqtt-" + String((uint32_t)ESP.getEfuseMac(), HEX);
        Serial.printf("[WiFi] starting fallback AP '%s'\n", apName.c_str());
        WiFi.softAP(apName.c_str(), g_config.apPassword.c_str());
        Serial.printf("[WiFi] AP IP=%s - open http://%s to configure\n",
                      WiFi.softAPIP().toString().c_str(), WiFi.softAPIP().toString().c_str());
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("\ninetbox2mqtt %s starting...\n", FW_VERSION);

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
