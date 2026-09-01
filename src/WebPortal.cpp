#include "WebPortal.h"
#include "WebAssets.h"
#include "Version.h"
#include "CommandLog.h"
#include "DebugLog.h"
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
    // "lin" reflects the rolling ~60s alive-poll window (TrumaStatus::isAlive()),
    // so it correctly goes back to false if the bus later falls silent.
    // "linRegistered" is sticky for the whole boot session (true from the
    // first successful alive-poll onward) and distinguishes "never completed
    // the CPplus init handshake this boot" from "was fine, then went quiet".
    doc["lin"] = status_->isAlive();
    doc["linRegistered"] = lin_->isRegistered();
    doc["linUnknownFrames"] = lin_->unknownFrameCount();

    OtaCheckResult lastCheck = OtaManager::getLastCheckResult();
    doc["updateAvailable"] = lastCheck.updateAvailable;
    doc["latestVersion"] = lastCheck.latestVersion;

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
    doc["ntpTimezone"] = cfg_->ntpTimezone;
    doc["debugLogging"] = cfg_->debugLogging;
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

static const char *otaPhaseToString(OtaPhase phase) {
    switch (phase) {
        case OtaPhase::Downloading: return "downloading";
        case OtaPhase::Installing: return "installing";
        case OtaPhase::Success: return "success";
        case OtaPhase::Error: return "error";
        default: return "idle";
    }
}

void WebPortal::handleOtaStatus(AsyncWebServerRequest *request) {
    OtaProgress p = OtaManager::getProgress();
    JsonDocument doc;
    doc["phase"] = otaPhaseToString(p.phase);
    doc["version"] = p.version;
    doc["bytesDone"] = (uint32_t)p.bytesDone;
    doc["bytesTotal"] = (uint32_t)p.bytesTotal;
    if (p.phase == OtaPhase::Error) doc["error"] = p.error;
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
            if (doc["ntpTimezone"].is<const char *>()) cfg_->ntpTimezone = doc["ntpTimezone"].as<String>();

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

    // Separate from /api/config on purpose: toggling debug logging should
    // take effect immediately and not force a reboot (unlike WiFi/MQTT
    // changes), since it's meant to be flipped on quickly while an
    // intermittent LIN/MQTT/WLAN issue is actively being investigated.
    server_.on(
        "/api/debug", HTTP_POST, [](AsyncWebServerRequest *request) {},
        nullptr,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err || !doc["enabled"].is<bool>()) {
                request->send(400, "application/json", "{\"error\":\"invalid json\"}");
                return;
            }
            cfg_->debugLogging = doc["enabled"].as<bool>();
            DebugLog::setEnabled(cfg_->debugLogging);
            AppConfigStore::save(*cfg_);
            CommandLog::add("web", "applied", String("Debug-Logs ") + (cfg_->debugLogging ? "aktiviert" : "deaktiviert"));
            request->send(200, "application/json", "{\"ok\":true}");
        });

    server_.on("/api/ota/check", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleOtaCheck(request);
    });

    server_.on("/api/ota/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleOtaStatus(request);
    });

    server_.on(
        "/api/ota/install", HTTP_POST, [](AsyncWebServerRequest *request) {},
        nullptr,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index + len != total) return;  // wait for the (small) JSON body to fully arrive
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err || !doc["url"].is<const char *>()) {
                request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
                return;
            }
            String url = doc["url"].as<String>();
            String version = doc["version"].is<const char *>() ? doc["version"].as<String>() : String("");
            Serial.printf("[OTA] starting background install from %s ...\n", url.c_str());
            bool started = OtaManager::startInstall(url, version);
            JsonDocument resp;
            resp["ok"] = started;
            if (!started) resp["error"] = "Installation läuft bereits";
            String out;
            serializeJson(resp, out);
            request->send(started ? 200 : 409, "application/json", out);
            if (started) {
                CommandLog::add("ota", "info", "Update-Installation von " + url + " gestartet");
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
