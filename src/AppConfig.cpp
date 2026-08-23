#include "AppConfig.h"
#include <Preferences.h>

static const char *NS = "inetbox";

AppConfig AppConfigStore::load() {
    AppConfig cfg;
    Preferences prefs;
    prefs.begin(NS, true);
    cfg.wifiSsid          = prefs.getString("wifi_ssid", "");
    cfg.wifiPassword      = prefs.getString("wifi_pass", "");
    cfg.mqttHost          = prefs.getString("mqtt_host", "");
    cfg.mqttPort          = prefs.getUShort("mqtt_port", 1883);
    cfg.mqttUser          = prefs.getString("mqtt_user", "");
    cfg.mqttPassword      = prefs.getString("mqtt_pass", "");
    cfg.mqttTopicRoot     = prefs.getString("topic_root", "truma");
    cfg.deviceName        = prefs.getString("dev_name", "inetbox2mqtt");
    cfg.haDiscoveryEnabled = prefs.getBool("ha_disco", true);
    cfg.otaManifestUrl    = prefs.getString("ota_url", cfg.otaManifestUrl);
    cfg.mqttBootDiscardMs = prefs.getUInt("boot_discard", 4000);
    prefs.end();
    return cfg;
}

void AppConfigStore::save(const AppConfig &cfg) {
    Preferences prefs;
    prefs.begin(NS, false);
    prefs.putString("wifi_ssid", cfg.wifiSsid);
    prefs.putString("wifi_pass", cfg.wifiPassword);
    prefs.putString("mqtt_host", cfg.mqttHost);
    prefs.putUShort("mqtt_port", cfg.mqttPort);
    prefs.putString("mqtt_user", cfg.mqttUser);
    prefs.putString("mqtt_pass", cfg.mqttPassword);
    prefs.putString("topic_root", cfg.mqttTopicRoot);
    prefs.putString("dev_name", cfg.deviceName);
    prefs.putBool("ha_disco", cfg.haDiscoveryEnabled);
    prefs.putString("ota_url", cfg.otaManifestUrl);
    prefs.putUInt("boot_discard", cfg.mqttBootDiscardMs);
    prefs.end();
}

void AppConfigStore::reset() {
    Preferences prefs;
    prefs.begin(NS, false);
    prefs.clear();
    prefs.end();
}
