#include "MqttManager.h"
#include "Version.h"
#include "CommandLog.h"
#include "OtaManager.h"
#include <ArduinoJson.h>

static MqttManager *g_instance = nullptr;

void MqttManager::staticCallback(char *topic, uint8_t *payload, unsigned int length) {
    if (g_instance) g_instance->handleMessage(topic, payload, length);
}

void MqttManager::begin(const AppConfig &cfg, TrumaStatus *status) {
    cfg_ = cfg;
    status_ = status;
    g_instance = this;

    topicSetPrefix_ = "service/" + cfg_.mqttTopicRoot + "/set/";
    topicStatusPrefix_ = "service/" + cfg_.mqttTopicRoot + "/control_status/";

    client_.setServer(cfg_.mqttHost.c_str(), cfg_.mqttPort);
    client_.setCallback(staticCallback);
    // The HA climate discovery payload (topics + Jinja mode templates + the
    // device block) is comfortably larger than PubSubClient's default 256
    // byte buffer and can exceed even 1024 bytes once a longer topic root or
    // device name is configured. If the buffer is too small, publish() just
    // silently drops the message (returns false) - the entity then never
    // shows up in Home Assistant with no obvious error. Use a generous size.
    client_.setBufferSize(2048);
}

bool MqttManager::isConnected() {
    return client_.connected();
}

void MqttManager::reconnect() {
    if (cfg_.mqttHost.length() == 0) return; // not configured yet
    uint32_t now = millis();
    if (now - lastReconnectAttemptMs_ < 5000) return;
    lastReconnectAttemptMs_ = now;

    String clientId = cfg_.deviceName + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    String aliveTopic = topicStatusPrefix_ + "alive";

    bool ok;
    if (cfg_.mqttUser.length() > 0) {
        ok = client_.connect(clientId.c_str(), cfg_.mqttUser.c_str(), cfg_.mqttPassword.c_str(),
                              aliveTopic.c_str(), 0, true, "OFF");
    } else {
        ok = client_.connect(clientId.c_str(), nullptr, nullptr,
                              aliveTopic.c_str(), 0, true, "OFF");
    }

    if (ok) {
        connectedAtMs_ = millis();
        client_.subscribe((topicSetPrefix_ + "#").c_str(), 1);
        client_.subscribe(haStatusTopic_.c_str(), 1);
        client_.publish(aliveTopic.c_str(), "ON", true);
        if (cfg_.haDiscoveryEnabled) publishDiscovery();
    }
}

void MqttManager::loop() {
    if (!client_.connected()) {
        reconnect();
    } else {
        client_.loop();
    }
}

void MqttManager::handleMessage(char *topicC, uint8_t *payload, unsigned int length) {
    String topic(topicC);
    String value;
    value.reserve(length);
    for (unsigned int i = 0; i < length; i++) value += (char)payload[i];

    // Discard anything that arrives right after a (re-)connect: retained /
    // broker-queued messages are delivered immediately upon subscribing, and
    // we must not let a stale command from a previous session act on the
    // aircon after a reboot or reconnect.
    if (millis() - connectedAtMs_ < cfg_.mqttBootDiscardMs) {
        Serial.printf("[MQTT] discarding boot-queued message %s = %s\n", topic.c_str(), value.c_str());
        CommandLog::add("mqtt", "discarded", topic + " = " + value);
        return;
    }

    if (topic == haStatusTopic_) {
        if (value == "online" && cfg_.haDiscoveryEnabled) publishDiscovery();
        return;
    }

    if (!topic.startsWith(topicSetPrefix_)) return;
    String key = topic.substring(topicSetPrefix_.length());

    if (key == "reboot") {
        if (value == "1") {
            Serial.println("[MQTT] reboot requested");
            CommandLog::add("mqtt", "applied", "reboot");
            delay(200);
            ESP.restart();
        }
        return;
    }

    if (key == "ota_install") {
        if (value == "INSTALL") {
            OtaCheckResult last = OtaManager::getLastCheckResult();
            if (last.updateAvailable && last.downloadUrl.length() > 0) {
                bool started = OtaManager::startInstall(last.downloadUrl, last.latestVersion);
                CommandLog::add("mqtt", started ? "applied" : "rejected",
                                 "Update-Installation via Home Assistant angestoßen");
            } else {
                CommandLog::add("mqtt", "rejected", "ota_install: kein Update verfügbar");
            }
        }
        return;
    }

    if (status_->isSettable(key)) {
        if (status_->setByTopic(key, value)) {
            CommandLog::add("mqtt", "applied", key + " = " + value);
        } else {
            Serial.printf("[MQTT] rejected invalid value for %s: %s\n", key.c_str(), value.c_str());
            CommandLog::add("mqtt", "rejected", key + " = " + value);
        }
    } else {
        CommandLog::add("mqtt", "rejected", "unbekannter Schlüssel " + key);
    }
}

void MqttManager::publish(bool onlyChanged) {
    if (!client_.connected()) return;
    std::vector<std::pair<String, String>> pairs;
    status_->collectPublishPairs(pairs, onlyChanged);
    for (auto &kv : pairs) {
        client_.publish((topicStatusPrefix_ + kv.first).c_str(), kv.second.c_str(), true);
        if (kv.first == "target_temp_aircon" || kv.first == "aircon_vent_mode" || kv.first == "aircon_operating_mode" || kv.first == "current_temp_room") {
            Serial.printf("[MQTT] publish %s = %s\n", kv.first.c_str(), kv.second.c_str());
        }
    }
    if (!onlyChanged) {
        client_.publish((topicStatusPrefix_ + "release").c_str(), FW_VERSION, true);
    }
    publishOtaState();
}

// Publishes the Home Assistant "update" entity's state as JSON, using its
// default schema (installed_version/latest_version/in_progress/update_percentage)
// so no value_template is needed on the discovery side. Called on every
// publish cycle so install progress (in_progress/update_percentage) tracks
// live while a background OTA install is running.
void MqttManager::publishOtaState() {
    OtaCheckResult check = OtaManager::getLastCheckResult();
    OtaProgress progress = OtaManager::getProgress();

    JsonDocument d;
    d["installed_version"] = FW_VERSION;
    d["latest_version"] = check.updateAvailable ? check.latestVersion : String(FW_VERSION);
    bool inProgress = (progress.phase == OtaPhase::Downloading || progress.phase == OtaPhase::Installing);
    d["in_progress"] = inProgress;
    if (inProgress && progress.bytesTotal > 0) {
        d["update_percentage"] = (int)((progress.bytesDone * 100) / progress.bytesTotal);
    } else {
        d["update_percentage"] = nullptr;
    }
    String out;
    serializeJson(d, out);
    client_.publish((topicStatusPrefix_ + "ota_state").c_str(), out.c_str(), true);
}


// -----------------------------------------------------------------------------
// Home Assistant MQTT discovery
// -----------------------------------------------------------------------------
namespace {

void addDevice(JsonObject &doc, const String &deviceId, const String &deviceName) {
    JsonObject dev = doc["device"].to<JsonObject>();
    JsonArray ids = dev["identifiers"].to<JsonArray>();
    ids.add(deviceId);
    dev["name"] = deviceName;
    dev["model"] = "Truma Aventa Comfort (2. Gen)";
    dev["manufacturer"] = "inetbox2mqtt (community fork)";
    dev["sw_version"] = FW_VERSION;
}

} // namespace

void MqttManager::publishDiscovery() {
    String deviceId = "inetbox2mqtt_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    String root = cfg_.mqttTopicRoot;
    String st = topicStatusPrefix_;
    String cmd = topicSetPrefix_;

    auto publishEntity = [&](const String &component, const String &objectId, JsonDocument &doc) {
        JsonObject obj = doc.as<JsonObject>();
        addDevice(obj, deviceId, cfg_.deviceName);
        obj["unique_id"] = root + "_" + objectId;
        String payload;
        serializeJson(doc, payload);
        String topic = "homeassistant/" + component + "/" + root + "/" + objectId + "/config";
        bool ok = client_.publish(topic.c_str(), payload.c_str(), true);
        if (!ok) {
            Serial.printf("[MQTT] discovery publish FAILED for %s (payload %u bytes, buffer %u) - entity will be missing in HA\n",
                          topic.c_str(), (unsigned)payload.length(), (unsigned)client_.getBufferSize());
        }
    };

    // --- one-time cleanup ---------------------------------------------------
    // An older firmware version published a plain read-only "aircon_light_level"
    // sensor entity, now superseded by the "light" entity below. Publish an
    // empty retained payload to that old discovery topic so Home Assistant
    // removes the stale duplicate entity from its registry.
    client_.publish(("homeassistant/sensor/" + root + "/aircon_light_level/config").c_str(), "", true);

    // --- alive binary sensor -------------------------------------------------
    {
        JsonDocument d;
        d["name"] = "Alive";
        d["device_class"] = "connectivity";
        d["state_topic"] = st + "alive";
        d["payload_on"] = "ON";
        d["payload_off"] = "OFF";
        publishEntity("binary_sensor", "alive", d);
    }

    // --- simple read-only sensors --------------------------------------------
    struct SensorDef { const char *id; const char *name; const char *devClass; const char *unit; };
    const SensorDef sensors[] = {
        {"release", "Firmware Release", nullptr, nullptr},
        {"clock", "CPplus Clock", nullptr, nullptr},
        {"current_temp_room", "Raumtemperatur", "temperature", "°C"},
    };
    for (auto &s : sensors) {
        JsonDocument d;
        d["name"] = s.name;
        d["state_topic"] = st + String(s.id);
        if (s.devClass) d["device_class"] = s.devClass;
        if (s.unit) d["unit_of_measurement"] = s.unit;
        publishEntity("sensor", s.id, d);
    }

    // --- Aventa aircon as a proper HA climate entity --------------------------
    {
        JsonDocument d;
        d["name"] = "Aventa Aircon";
        d["current_temperature_topic"] = st + "current_temp_aircon";
        d["temperature_command_topic"] = cmd + "target_temp_aircon";
        d["temperature_state_topic"] = st + "target_temp_aircon";
        d["min_temp"] = 16;
        d["max_temp"] = 32;
        d["temp_step"] = 1;
        d["mode_command_topic"] = cmd + "aircon_operating_mode";
        d["mode_state_topic"] = st + "aircon_operating_mode";
        d["mode_state_template"] =
            "{% if value == 'vent' %}fan_only{% elif value == 'hot' %}heat{% else %}{{ value }}{% endif %}";
        d["mode_command_template"] =
            "{% if value == 'fan_only' %}vent{% elif value == 'heat' %}hot{% else %}{{ value }}{% endif %}";
        JsonArray modes = d["modes"].to<JsonArray>();
        modes.add("off"); modes.add("fan_only"); modes.add("cool"); modes.add("heat"); modes.add("auto");
        d["fan_mode_command_topic"] = cmd + "aircon_vent_mode";
        d["fan_mode_state_topic"] = st + "aircon_vent_mode";
        JsonArray fans = d["fan_modes"].to<JsonArray>();
        fans.add("low"); fans.add("mid"); fans.add("high"); fans.add("night"); fans.add("auto");
        publishEntity("climate", "aventa", d);
    }

    // --- Aventa light (on/off + 5 levels) ---------------------------------------
    // Read side (0/20/40/60/80/100 raw -> level 0-5) is CONFIRMED on real
    // hardware. Write side (this entity's command topics) mirrors the exact
    // same buffer byte position on the way out (see TrumaStatus::
    // encodeAirconContent()) but is UNCONFIRMED/untested - the ECU may or
    // may not actually react to it.
    {
        JsonDocument d;
        d["name"] = "Licht";
        d["command_topic"] = cmd + "aircon_light";
        d["state_topic"] = st + "aircon_light_state";
        d["payload_on"] = "ON";
        d["payload_off"] = "OFF";
        d["brightness_command_topic"] = cmd + "aircon_light_level";
        d["brightness_state_topic"] = st + "aircon_light_level";
        d["brightness_scale"] = 5;
        d["on_command_type"] = "last";
        // NOTE: do NOT add "supported_color_modes" here - that key only
        // exists in the MQTT Light JSON schema. This entity uses the
        // *default* (basic) schema, which validates its discovery config
        // strictly and REJECTS THE WHOLE ENTITY if an unknown key like
        // supported_color_modes is present (this was tried in 3.7.1 and
        // broke the entity entirely - no toggle, no brightness, nothing).
        // For the default schema, brightness_command_topic/brightness_state_topic
        // + brightness_scale alone are sufficient for HA to show a slider.
        publishEntity("light", "aircon_light", d);
    }

    // --- reboot button ---------------------------------------------------------
    {
        JsonDocument d;
        d["name"] = "Reboot";
        d["command_topic"] = cmd + "reboot";
        d["payload_press"] = "1";
        d["device_class"] = "restart";
        publishEntity("button", "reboot", d);
    }

    // --- firmware update entity -------------------------------------------------
    // Uses the MQTT "update" component's default JSON schema (installed_version /
    // latest_version / in_progress / update_percentage), published by
    // publishOtaState(). Lets HA show update availability + live install
    // progress, and trigger installation via its built-in "Install" button.
    {
        JsonDocument d;
        d["name"] = "Firmware";
        d["device_class"] = "firmware";
        d["state_topic"] = st + "ota_state";
        d["command_topic"] = cmd + "ota_install";
        d["payload_install"] = "INSTALL";
        publishEntity("update", "firmware", d);
    }

    discoveryPublished_ = true;
}
