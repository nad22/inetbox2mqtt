#pragma once

#include <Arduino.h>
#include <vector>

// -----------------------------------------------------------------------------
// CommandLog
//
// Small in-memory ring buffer recording commands/events so the web UI can
// show a live log of what changed the device and why - tagged with where it
// came from ("mqtt", "web", "ota" or "system").
//
// Caveat: the MQTT protocol itself does not tell a subscriber which client
// published a message. "mqtt" therefore only means "this arrived via the
// MQTT broker", not which external application/user ultimately triggered it.
// -----------------------------------------------------------------------------
struct LogEntry {
    uint32_t uptimeMs = 0;
    String source;   // "mqtt", "web", "ota", "system"
    String status;   // "applied", "rejected", "discarded", "info"
    String message;
};

class CommandLog {
public:
    static const size_t CAPACITY = 40;

    static void add(const String &source, const String &status, const String &message);
    static void collect(std::vector<LogEntry> &out);
};
