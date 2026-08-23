#include "OtaManager.h"
#include "Version.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

namespace {

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

}  // namespace

OtaCheckResult OtaManager::checkForUpdate(const String &manifestUrl) {
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

bool OtaManager::installFromUrl(const String &url, String &error) {
    if (url.length() == 0) {
        error = "keine URL angegeben";
        return false;
    }

    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    WiFiClient &client = pickClient(url, secureClient, plainClient);

    httpUpdate.rebootOnUpdate(false);  // caller decides when/whether to reboot
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
