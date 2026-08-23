#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "AppConfig.h"
#include "TrumaStatus.h"
#include "LinBus.h"
#include "MqttManager.h"

// -----------------------------------------------------------------------------
// WebPortal
//
// Always-on built-in web server (no separate "OS mode"): serves the
// configuration + control single-page app on both the fallback Access Point
// and the normal WiFi station connection.
// -----------------------------------------------------------------------------
class WebPortal {
public:
    void begin(AppConfig &cfg, TrumaStatus *status, LinBus *lin, MqttManager *mqtt);

    // Returns true once (and only once) when a reboot was requested via the
    // web UI / API, so main.cpp can restart the device from the main loop
    // instead of from within an async request callback.
    bool consumeRebootRequest();

private:
    AsyncWebServer server_{80};
    AppConfig *cfg_ = nullptr;
    TrumaStatus *status_ = nullptr;
    LinBus *lin_ = nullptr;
    MqttManager *mqtt_ = nullptr;

    void handleGetStatus(AsyncWebServerRequest *request);
    void handleGetConfig(AsyncWebServerRequest *request);
    void handleGetLog(AsyncWebServerRequest *request);
    void handleOtaCheck(AsyncWebServerRequest *request);
    void handleOtaStatus(AsyncWebServerRequest *request);
};
