#include "MqttManager.h"
#include "Version.h"
#include "CommandLog.h"
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
    client_.setBufferSize(1024);
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
    }
    if (!onlyChanged) {
        client_.publish((topicStatusPrefix_ + "release").c_str(), FW_VERSION, true);
    }
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
        client_.publish(topic.c_str(), payload.c_str(), true);
    };

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
        {"current_temp_room", "Room Temperature", "temperature", "°C"},
        {"current_temp_water", "Water Temperature", "temperature", "°C"},
        {"operating_status", "Heater Operating Status", nullptr, nullptr},
        {"error_code", "Heater Error Code", nullptr, nullptr},
    };
    for (auto &s : sensors) {
        JsonDocument d;
        d["name"] = s.name;
        d["state_topic"] = st + String(s.id);
        if (s.devClass) d["device_class"] = s.devClass;
        if (s.unit) d["unit_of_measurement"] = s.unit;
        publishEntity("sensor", s.id, d);
    }

    // --- heater (Combi) controls ---------------------------------------------
    {
        JsonDocument d;
        d["name"] = "Room Target Temperature";
        d["command_topic"] = cmd + "target_temp_room";
        d["state_topic"] = st + "target_temp_room";
        d["min"] = 5;
        d["max"] = 30;
        d["step"] = 1;
        d["unit_of_measurement"] = "°C";
        d["mode"] = "slider";
        publishEntity("number", "target_temp_room", d);
    }
    {
        JsonDocument d;
        d["name"] = "Water Heater Mode";
        d["command_topic"] = cmd + "target_temp_water";
        d["state_topic"] = st + "target_temp_water";
        JsonArray opt = d["options"].to<JsonArray>();
        opt.add("0"); opt.add("40"); opt.add("60"); opt.add("200");
        publishEntity("select", "target_temp_water", d);
    }
    {
        JsonDocument d;
        d["name"] = "Heating Fan Mode";
        d["command_topic"] = cmd + "heating_mode";
        d["state_topic"] = st + "heating_mode";
        JsonArray opt = d["options"].to<JsonArray>();
        opt.add("off"); opt.add("eco"); opt.add("high");
        publishEntity("select", "heating_mode", d);
    }
    {
        JsonDocument d;
        d["name"] = "Energy Mix";
        d["command_topic"] = cmd + "energy_mix";
        d["state_topic"] = st + "energy_mix";
        JsonArray opt = d["options"].to<JsonArray>();
        opt.add("none"); opt.add("gas"); opt.add("electricity"); opt.add("mix");
        publishEntity("select", "energy_mix", d);
    }
    {
        JsonDocument d;
        d["name"] = "Electrical Power Level";
        d["command_topic"] = cmd + "el_power_level";
        d["state_topic"] = st + "el_power_level";
        JsonArray opt = d["options"].to<JsonArray>();
        opt.add("0"); opt.add("900"); opt.add("1800");
        publishEntity("select", "el_power_level", d);
    }

    // --- Aventa aircon as a proper HA climate entity --------------------------
    {
        JsonDocument d;
        d["name"] = "Aventa Aircon";
        d["current_temperature_topic"] = st + "current_temp_room";
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

    // --- reboot button ---------------------------------------------------------
    {
        JsonDocument d;
        d["name"] = "Reboot";
        d["command_topic"] = cmd + "reboot";
        d["payload_press"] = "1";
        d["device_class"] = "restart";
        publishEntity("button", "reboot", d);
    }

    discoveryPublished_ = true;
}
