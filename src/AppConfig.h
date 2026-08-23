#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
// Persistent runtime configuration (stored in NVS via the Preferences library).
// This replaces the old "credentials file + OS-mode / normal-mode" switch of the
// MicroPython version: there is only ever one firmware mode. If no WiFi
// credentials are stored (or the connection fails), the device falls back to
// a configuration Access-Point while the same web server keeps running.
// -----------------------------------------------------------------------------
struct AppConfig {
    // WiFi (station)
    String wifiSsid;
    String wifiPassword;

    // MQTT broker
    String mqttHost;
    uint16_t mqttPort = 1883;
    String mqttUser;
    String mqttPassword;
    String mqttTopicRoot = "truma";      // -> service/<root>/...
    String deviceName = "inetbox2mqtt";  // used as MQTT client id / HA device name

    bool haDiscoveryEnabled = true;

    // Manifest used by the built-in OTA updater (see src/OtaManager.h and
    // .github/workflows/release-firmware.yml). Point this at your own fork's
    // manifest.json if you maintain one.
    String otaManifestUrl = "https://raw.githubusercontent.com/nad22/inetbox2mqtt/main/firmware/manifest.json";

    // POSIX TZ string used for the one-time NTP time sync that sets the
    // CPplus's own clock after boot (see main.cpp). Default is Central
    // European Time with automatic DST (Germany/Austria/Switzerland).
    String ntpTimezone = "CET-1CEST,M3.5.0,M10.5.0/3";

    // Time (ms) after a successful MQTT (re)connect during which incoming
    // command messages are discarded. This prevents stale/queued broker
    // messages from unintentionally driving the aircon right after boot.
    uint32_t mqttBootDiscardMs = 4000;

    bool isWifiConfigured() const { return wifiSsid.length() > 0; }
};

class AppConfigStore {
public:
    static AppConfig load();
    static void save(const AppConfig &cfg);
    static void reset();
};
