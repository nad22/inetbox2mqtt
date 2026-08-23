#include "WebPortal.h"
#include "WebAssets.h"
#include "Version.h"
#include <ArduinoJson.h>
#include <WiFi.h>

static volatile bool g_rebootRequested = false;

bool WebPortal::consumeRebootRequest() {
    if (g_rebootRequested) {
        g_rebootRequested = false;
        return true;
    }
    return false;
}

void WebPortal::handleGetStatus(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["version"] = FW_VERSION;
    doc["wifi"] = (WiFi.getMode() == WIFI_AP) ? ("AP: " + WiFi.softAPIP().toString())
                                               : (WiFi.isConnected() ? WiFi.localIP().toString() : "getrennt");
    doc["mqtt"] = mqtt_->isConnected();
    doc["lin"] = lin_->isRegistered();

    JsonObject values = doc["values"].to<JsonObject>();
    std::vector<std::pair<String, String>> pairs;
    status_->collectPublishPairs(pairs, false);
    for (auto &kv : pairs) values[kv.first] = kv.second;

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void WebPortal::handleGetConfig(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["wifiSsid"] = cfg_->wifiSsid;
    doc["mqttHost"] = cfg_->mqttHost;
    doc["mqttPort"] = cfg_->mqttPort;
    doc["mqttUser"] = cfg_->mqttUser;
    doc["mqttTopicRoot"] = cfg_->mqttTopicRoot;
    doc["deviceName"] = cfg_->deviceName;
    // Passwords are intentionally never sent back to the browser.
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void WebPortal::begin(AppConfig &cfg, TrumaStatus *status, LinBus *lin, MqttManager *mqtt) {
    cfg_ = &cfg;
    status_ = status;
    lin_ = lin;
    mqtt_ = mqtt;

    server_.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", INDEX_HTML);
    });

    server_.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleGetStatus(request);
    });

    server_.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleGetConfig(request);
    });

    server_.on(
        "/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {},
        nullptr,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err) {
                request->send(400, "application/json", "{\"error\":\"invalid json\"}");
                return;
            }
            if (doc["wifiSsid"].is<const char *>()) cfg_->wifiSsid = doc["wifiSsid"].as<String>();
            cfg_->wifiSsid.trim();
            if (doc["wifiPassword"].is<const char *>() && doc["wifiPassword"].as<String>().length() > 0) {
                cfg_->wifiPassword = doc["wifiPassword"].as<String>();
                cfg_->wifiPassword.trim();
            }
            if (doc["mqttHost"].is<const char *>()) cfg_->mqttHost = doc["mqttHost"].as<String>();
            if (doc["mqttPort"].is<int>()) cfg_->mqttPort = doc["mqttPort"].as<int>();
            if (doc["mqttUser"].is<const char *>()) cfg_->mqttUser = doc["mqttUser"].as<String>();
            if (doc["mqttPassword"].is<const char *>() && doc["mqttPassword"].as<String>().length() > 0)
                cfg_->mqttPassword = doc["mqttPassword"].as<String>();
            if (doc["mqttTopicRoot"].is<const char *>()) cfg_->mqttTopicRoot = doc["mqttTopicRoot"].as<String>();
            if (doc["deviceName"].is<const char *>()) cfg_->deviceName = doc["deviceName"].as<String>();

            AppConfigStore::save(*cfg_);
            request->send(200, "application/json", "{\"ok\":true}");
            g_rebootRequested = true;
        });

    server_.on(
        "/api/set", HTTP_POST, [](AsyncWebServerRequest *request) {},
        nullptr,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err || !doc["key"].is<const char *>() || !doc["value"].is<const char *>()) {
                request->send(400, "application/json", "{\"error\":\"invalid json\"}");
                return;
            }
            String key = doc["key"].as<String>();
            String value = doc["value"].as<String>();
            if (!status_->isSettable(key) || !status_->setByTopic(key, value)) {
                request->send(400, "application/json", "{\"error\":\"invalid key or value\"}");
                return;
            }
            request->send(200, "application/json", "{\"ok\":true}");
        });

    server_.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"ok\":true}");
        g_rebootRequested = true;
    });

    server_.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not found");
    });

    server_.begin();
}
