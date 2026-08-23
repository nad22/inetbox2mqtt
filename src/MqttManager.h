#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "AppConfig.h"
#include "TrumaStatus.h"

// -----------------------------------------------------------------------------
// MqttManager
//
// Talks to the MQTT broker (e.g. Home Assistant's built-in Mosquitto add-on),
// publishes status under service/<root>/control_status/<key> and reacts to
// commands under service/<root>/set/<key>. It also publishes the Home
// Assistant MQTT-discovery configuration for all entities.
//
// Safety feature (explicitly requested): any command message that arrives
// within `mqttBootDiscardMs` of a (re)connect is dropped without being
// applied. Since retained / broker-queued messages are delivered immediately
// after (re-)subscribing, this reliably prevents a stale "turn on" command
// from a previous session from silently driving the aircon after a reboot
// or a temporary network outage.
// -----------------------------------------------------------------------------
class MqttManager {
public:
    void begin(const AppConfig &cfg, TrumaStatus *status);
    void loop();
    bool isConnected();

    // Publish either the full snapshot or only fields that changed.
    void publish(bool onlyChanged);

private:
    AppConfig cfg_;
    TrumaStatus *status_ = nullptr;
    WiFiClient wifiClient_;
    PubSubClient client_{wifiClient_};

    String topicSetPrefix_;
    String topicStatusPrefix_;
    String haStatusTopic_ = "homeassistant/status";

    uint32_t connectedAtMs_ = 0;
    uint32_t lastReconnectAttemptMs_ = 0;
    uint32_t lastPublishMs_ = 0;
    bool discoveryPublished_ = false;

    void reconnect();
    void handleMessage(char *topic, uint8_t *payload, unsigned int length);
    void publishDiscovery();

    static void staticCallback(char *topic, uint8_t *payload, unsigned int length);
};
