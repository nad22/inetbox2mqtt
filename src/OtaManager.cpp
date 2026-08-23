#include "OtaManager.h"
#include "Version.h"
#include "CommandLog.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace {

OtaProgress g_progress;
SemaphoreHandle_t g_mutex = xSemaphoreCreateMutex();
volatile bool g_installRunning = false;
volatile bool g_rebootPending = false;

OtaCheckResult g_lastCheck;
SemaphoreHandle_t g_checkMutex = xSemaphoreCreateMutex();

void setProgress(const OtaProgress &p) {
    if (g_mutex && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_progress = p;
        xSemaphoreGive(g_mutex);
    }
}

struct InstallTaskArgs {
    String url;
    String version;
};

void installTask(void *param) {
    InstallTaskArgs *args = static_cast<InstallTaskArgs *>(param);
    String error;
    bool ok = OtaManager::installFromUrl(args->url, error);

    OtaProgress p;
    p.version = args->version;
    if (ok) {
        p.phase = OtaPhase::Success;
        p.bytesDone = p.bytesTotal;
        g_rebootPending = true;
        CommandLog::add("ota", "applied", "Update auf " + args->version + " installiert, Neustart folgt");
    } else {
        p.phase = OtaPhase::Error;
        p.error = error;
        CommandLog::add("ota", "rejected", "Update auf " + args->version + " fehlgeschlagen: " + error);
    }
    setProgress(p);

    delete args;
    g_installRunning = false;
    vTaskDelete(nullptr);
}

// Small "is a newer than b" comparator for "x.y.z" version strings. Falls
// back to false (not newer) if either string doesn't parse.
bool isNewerVersion(const String &a, const String &b) {
    int aMaj = -1, aMin = 0, aPatch = 0;
    int bMaj = -1, bMin = 0, bPatch = 0;
    sscanf(a.c_str(), "%d.%d.%d", &aMaj, &aMin, &aPatch);
    sscanf(b.c_str(), "%d.%d.%d", &bMaj, &bMin, &bPatch);
    if (aMaj < 0 || bMaj < 0) return false;
    if (aMaj != bMaj) return aMaj > bMaj;
    if (aMin != bMin) return aMin > bMin;
    return aPatch > bPatch;
}

// Picks a plain or TLS client depending on the URL scheme. Certificate
// validation is intentionally disabled (see OtaManager.h) to avoid having to
// maintain a pinned root CA.
WiFiClient &pickClient(const String &url, WiFiClientSecure &secureClient, WiFiClient &plainClient) {
    if (url.startsWith("https://")) {
        secureClient.setInsecure();
        return secureClient;
    }
    return plainClient;
}

OtaCheckResult checkForUpdateImpl(const String &manifestUrl) {
    OtaCheckResult result;
    result.currentVersion = FW_VERSION;

    if (manifestUrl.length() == 0) {
        result.error = "keine Manifest-URL konfiguriert";
        return result;
    }

    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    WiFiClient &client = pickClient(manifestUrl, secureClient, plainClient);

    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    if (!http.begin(client, manifestUrl)) {
        result.error = "Verbindung zur Manifest-URL fehlgeschlagen";
        return result;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        result.error = "HTTP " + String(code);
        http.end();
        return result;
    }

    String body = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err || !doc["version"].is<const char *>() || !doc["url"].is<const char *>()) {
        result.error = "ungültiges Manifest";
        return result;
    }

    result.ok = true;
    result.latestVersion = doc["version"].as<String>();
    result.downloadUrl = doc["url"].as<String>();
    result.updateAvailable = isNewerVersion(result.latestVersion, result.currentVersion);
    return result;
}

}  // namespace

OtaCheckResult OtaManager::checkForUpdate(const String &manifestUrl) {
    OtaCheckResult result = checkForUpdateImpl(manifestUrl);
    if (g_checkMutex && xSemaphoreTake(g_checkMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_lastCheck = result;
        xSemaphoreGive(g_checkMutex);
    }
    return result;
}

OtaCheckResult OtaManager::getLastCheckResult() {
    OtaCheckResult r;
    if (g_checkMutex && xSemaphoreTake(g_checkMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        r = g_lastCheck;
        xSemaphoreGive(g_checkMutex);
    }
    return r;
}

bool OtaManager::installFromUrl(const String &url, String &error) {
    if (url.length() == 0) {
        error = "keine URL angegeben";
        return false;
    }

    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    WiFiClient &client = pickClient(url, secureClient, plainClient);

    httpUpdate.rebootOnUpdate(false);  // caller decides when/whether to reboot
    httpUpdate.onProgress([](int done, int total) {
        OtaProgress p = OtaManager::getProgress();
        p.phase = OtaPhase::Downloading;
        p.bytesDone = (size_t)done;
        p.bytesTotal = (size_t)total;
        setProgress(p);
    });
    httpUpdate.onEnd([]() {
        OtaProgress p = OtaManager::getProgress();
        p.phase = OtaPhase::Installing;
        setProgress(p);
    });
    t_httpUpdate_return ret = httpUpdate.update(client, url);

    switch (ret) {
        case HTTP_UPDATE_OK:
            return true;
        case HTTP_UPDATE_NO_UPDATES:
            error = "keine Aktualisierung notwendig";
            return false;
        case HTTP_UPDATE_FAILED:
        default:
            error = httpUpdate.getLastErrorString();
            return false;
    }
}

bool OtaManager::startInstall(const String &url, const String &version) {
    if (g_installRunning) return false;
    g_installRunning = true;

    OtaProgress p;
    p.phase = OtaPhase::Downloading;
    p.version = version;
    setProgress(p);

    InstallTaskArgs *args = new InstallTaskArgs{url, version};
    // TLS handshake + HTTPClient/Update need a generous stack; matches the
    // AsyncTCP task's own budget (16 KB) to be safe.
    BaseType_t ok = xTaskCreate(installTask, "ota_install", 16384, args, 1, nullptr);
    if (ok != pdPASS) {
        delete args;
        g_installRunning = false;
        OtaProgress fail;
        fail.phase = OtaPhase::Error;
        fail.error = "Task konnte nicht gestartet werden";
        setProgress(fail);
        return false;
    }
    return true;
}

OtaProgress OtaManager::getProgress() {
    OtaProgress p;
    if (g_mutex && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        p = g_progress;
        xSemaphoreGive(g_mutex);
    }
    return p;
}

bool OtaManager::consumeRebootRequest() {
    if (g_rebootPending) {
        g_rebootPending = false;
        return true;
    }
    return false;
}
