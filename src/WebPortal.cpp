#include "WebPortal.h"
#include "WebAssets.h"
#include "Version.h"
#include "CommandLog.h"
#include "OtaManager.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Update.h>

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
    doc["otaManifestUrl"] = cfg_->otaManifestUrl;
    doc["fwVersion"] = FW_VERSION;
    // Passwords are intentionally never sent back to the browser.
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void WebPortal::handleGetLog(AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonArray arr = doc["entries"].to<JsonArray>();
    std::vector<LogEntry> entries;
    CommandLog::collect(entries);
    uint32_t now = millis();
    for (auto &e : entries) {
        JsonObject o = arr.add<JsonObject>();
        o["age"] = (now - e.uptimeMs) / 1000;
        o["source"] = e.source;
        o["status"] = e.status;
        o["message"] = e.message;
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void WebPortal::handleOtaCheck(AsyncWebServerRequest *request) {
    OtaCheckResult r = OtaManager::checkForUpdate(cfg_->otaManifestUrl);
    JsonDocument doc;
    doc["ok"] = r.ok;
    doc["error"] = r.error;
    doc["currentVersion"] = r.currentVersion;
    doc["latestVersion"] = r.latestVersion;
    doc["downloadUrl"] = r.downloadUrl;
    doc["updateAvailable"] = r.updateAvailable;
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
            if (doc["otaManifestUrl"].is<const char *>()) cfg_->otaManifestUrl = doc["otaManifestUrl"].as<String>();

            AppConfigStore::save(*cfg_);
            CommandLog::add("web", "applied", "Konfiguration gespeichert, Neustart folgt");
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
                CommandLog::add("web", "rejected", key + " = " + value);
                request->send(400, "application/json", "{\"error\":\"invalid key or value\"}");
                return;
            }
            CommandLog::add("web", "applied", key + " = " + value);
            request->send(200, "application/json", "{\"ok\":true}");
        });

    server_.on("/api/log", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleGetLog(request);
    });

    server_.on("/api/ota/check", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleOtaCheck(request);
    });

    server_.on(
        "/api/ota/install", HTTP_POST, [](AsyncWebServerRequest *request) {},
        nullptr,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err || !doc["url"].is<const char *>()) {
                request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
                return;
            }
            String url = doc["url"].as<String>();
            Serial.printf("[OTA] installing from %s ...\n", url.c_str());
            String error;
            bool ok = OtaManager::installFromUrl(url, error);
            JsonDocument resp;
            resp["ok"] = ok;
            if (!ok) resp["error"] = error;
            String out;
            serializeJson(resp, out);
            request->send(ok ? 200 : 500, "application/json", out);
            if (ok) {
                Serial.println("[OTA] install ok, rebooting");
                CommandLog::add("ota", "applied", "Update von " + url + " installiert");
                g_rebootRequested = true;
            } else {
                Serial.printf("[OTA] install failed: %s\n", error.c_str());
                CommandLog::add("ota", "rejected", "Update von " + url + " fehlgeschlagen: " + error);
            }
        });

    server_.on(
        "/api/ota/upload", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            bool ok = !Update.hasError();
            AsyncWebServerResponse *response =
                request->beginResponse(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
            response->addHeader("Connection", "close");
            request->send(response);
            if (ok) {
                CommandLog::add("ota", "applied", "manueller Upload installiert");
                g_rebootRequested = true;
            } else {
                CommandLog::add("ota", "rejected", "manueller Upload fehlgeschlagen");
            }
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0) {
                Serial.printf("[OTA] manual upload start: %s\n", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            }
            if (len && Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("[OTA] manual upload success, %u bytes\n", (unsigned)(index + len));
                } else {
                    Update.printError(Serial);
                }
            }
        });

    server_.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"ok\":true}");
        CommandLog::add("web", "applied", "reboot");
        g_rebootRequested = true;
    });

    server_.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not found");
    });

    server_.begin();
}
