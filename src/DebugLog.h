#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
// DebugLog
//
// Single runtime switch (persisted in AppConfig::debugLogging, toggled from
// the web UI's Einrichtung tab without requiring a reboot) that gates the
// verbose LIN/MQTT/WLAN diagnostic Serial output which used to be always-on.
//
// Why this matters for LIN stability: printing a hex dump of every status
// buffer / unknown frame over USB-serial (115200 baud) takes several
// milliseconds each time, and it used to happen unconditionally on every
// single received buffer. Since LinBus::loop() is a soft-realtime,
// millis()-timeout-based byte parser, that blocking Serial I/O could itself
// delay servicing the next incoming LIN byte long enough to miss it -
// making the bus look "unstable" when the real cause was our own logging.
// Keeping this off by default (and only enabling it while actively
// debugging) removes that self-inflicted jitter from normal operation.
// -----------------------------------------------------------------------------
class DebugLog {
public:
    static void setEnabled(bool en) { enabled_ = en; }
    static bool enabled() { return enabled_; }

private:
    static bool enabled_;
};
