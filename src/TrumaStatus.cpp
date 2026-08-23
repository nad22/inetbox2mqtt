#include "TrumaStatus.h"
#include "LinChecksum.h"
#include <cstring>
#include <cmath>

// -----------------------------------------------------------------------------
// Conversion helpers (port of conversions.py)
// -----------------------------------------------------------------------------
static float tempCodeToDecimal(uint16_t code) {
    if (code == 0xAAA || code == 0xAAAA || code == 0x0000) return 0.0f;
    return roundf((code / 10.0f - 273.0f) * 10.0f) / 10.0f;
}

static uint16_t decimalToTempCode(float decimal, bool hasValue) {
    if (!hasValue || decimal < 5.0f) return 0x00;
    return (uint16_t)lroundf((decimal + 273.0f) * 10.0f);
}

static String airconVentModeToString(uint8_t v) {
    switch (v) {
        case 113: return "low";
        case 114: return "mid";
        case 115: return "high";
        case 116: return "night";
        case 119: return "auto";
        default: return "UNKNOWN(" + String(v) + ")";
    }
}
static bool stringToAirconVentMode(const String &s, uint8_t &out) {
    if (s == "low") { out = 113; return true; }
    if (s == "mid") { out = 114; return true; }
    if (s == "high") { out = 115; return true; }
    if (s == "night") { out = 116; return true; }
    if (s == "auto") { out = 119; return true; }
    return false;
}

static String airconOperatingModeToString(uint8_t v) {
    switch (v) {
        case 0: return "off";
        case 4: return "vent";
        case 5: return "cool";
        case 6: return "hot";
        case 7: return "auto";
        default: return "UNKNOWN(" + String(v) + ")";
    }
}
static bool stringToAirconOperatingMode(const String &s, uint8_t &out) {
    if (s == "off") { out = 0; return true; }
    if (s == "vent") { out = 4; return true; }
    if (s == "cool") { out = 5; return true; }
    if (s == "hot") { out = 6; return true; }
    if (s == "auto") { out = 7; return true; }
    return false;
}

static String clockToString(uint16_t raw) {
    int m = raw / 256;
    int h = raw - (m * 256);
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    return String(buf);
}

// -----------------------------------------------------------------------------
// helpers for bounds-safe little-endian reads out of a possibly short buffer
// -----------------------------------------------------------------------------
static uint32_t readLE(const uint8_t *buf, size_t len, size_t offset, size_t n) {
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        size_t idx = offset + i;
        uint8_t b = (idx < len) ? buf[idx] : 0;
        v |= ((uint32_t)b) << (8 * i);
    }
    return v;
}

TrumaStatus::TrumaStatus() {}

// -----------------------------------------------------------------------------
// Incoming buffer decoding
// -----------------------------------------------------------------------------
void TrumaStatus::applyStatusBuffer(uint8_t bufIdHi, uint8_t bufIdLo, const uint8_t *p, size_t len) {
    // Temporary diagnostic: log every status buffer the CPplus pushes to us,
    // so it's possible to check via the serial monitor whether the Aventa
    // aircon status buffer (0x12,0x35) is actually being received at all,
    // and with which raw byte values, when current_temp_room / target_temp
    // stay stuck at 0.
    {
        String hex;
        for (size_t i = 0; i < len; i++) { char b[4]; snprintf(b, sizeof(b), "%02X ", p[i]); hex += b; }
        Serial.printf("[STATUS] buffer %02X,%02X (%u bytes): %s\n", bufIdHi, bufIdLo, (unsigned)len, hex.c_str());
    }
    // STATUS_BUFFER_HEADER_RECV_STATUS = 0x14, 0x33 (Combi heater status buffer)
    // is intentionally ignored entirely - no physical heater unit exists on
    // this LIN bus, so it never carried anything but placeholder zeros, and
    // heater/hot-water control was removed.
    // STATUS_BUFFER_HEADER_04 = 0x12, 0x35 (Aventa aircon status)
    if (bufIdHi == 0x12 && bufIdLo == 0x35) {
        airconOperatingModeRaw_ = readLE(p, len, 2, 1); fAirconOperatingMode_.pending = true;
        airconVentModeRaw_      = readLE(p, len, 4, 1); fAirconVentMode_.pending = true;
        // The unit itself reports target_raw == 0 whenever it is off (mode
        // 0) - there simply is no active setpoint while off. Publishing that
        // 0 verbatim makes the HA climate card's target temperature flash to
        // 0 deg C every time the mode is off, which looks like a bug. Keep
        // showing the last known real setpoint instead, like a normal
        // thermostat does, and only adopt a fresh value once the unit
        // reports a real (non-zero) one again.
        uint16_t rawTarget = readLE(p, len, 6, 2);
        if (rawTarget != 0) { targetTempAirconRaw_ = rawTarget; fTargetTempAircon_.pending = true; }
        // Confirmed against github.com/havanti/esphome-truma's StatusFrameAirconManual
        // struct: this same 18-byte buffer also carries two "actual" temperature
        // readings - current_temp_aircon (measured at the unit itself, struct
        // offset 8-9) and current_temp_room (the wall-mounted room sensor,
        // struct offset 16-17) - no separate heater buffer is needed for these.
        currentTempAirconRaw_ = readLE(p, len, 10, 2); fCurrentTempAircon_.pending = true;
        currentTempRoomRaw_ = readLE(p, len, 18, 2); fCurrentTempRoom_.pending = true;
        Serial.printf("[STATUS] aircon status decoded: mode=%u vent=%u target_raw=%u current_aircon_raw=%u current_room_raw=%u\n",
                      airconOperatingModeRaw_, airconVentModeRaw_, targetTempAirconRaw_, currentTempAirconRaw_, currentTempRoomRaw_);
        return;
    }
    // STATUS_BUFFER_HEADER_03 = 0x0A, 0x15 (clock)
    // Byte layout (confirmed against github.com/havanti/esphome-truma's
    // StatusFrameClock, which this project's own legacy source only decoded
    // partially): hour, minute, second, display_1(=0x1), display_2(=0x1),
    // display_3, clock_mode(0=24h/1=12h), clock_source, display_4, display_5.
    if (bufIdHi == 0x0A && bufIdLo == 0x15) {
        clockRaw_ = readLE(p, len, 2, 2); fClock_.pending = true;
        clockModeRaw_ = (uint8_t)readLE(p, len, 8, 1);
        return;
    }
    // STATUS_BUFFER_HEADER_02 (0x02,0x0D, command-counter echo) and the timer
    // buffer (0x18,0x3D) are intentionally ignored - they carry no state that
    // this firmware exposes.
}

// -----------------------------------------------------------------------------
// Outgoing set() from MQTT / web UI
// -----------------------------------------------------------------------------
bool TrumaStatus::isSettable(const String &key) const {
    return key == "aircon_operating_mode" || key == "aircon_vent_mode" || key == "target_temp_aircon";
}

bool TrumaStatus::setByTopic(const String &key, const String &value) {
    bool airconField = false;

    if (key == "aircon_operating_mode") {
        uint8_t v;
        if (!stringToAirconOperatingMode(value, v)) return false;
        airconOperatingModeRaw_ = v; fAirconOperatingMode_.pending = true; airconField = true;
        // NOTE: aircon_on is intentionally left untouched here. In the
        // original (working) firmware this field was always constant 1 and
        // "off" was signalled purely via aircon_operating_mode == 0; deriving
        // it from the mode (0 when off) made the write frame contain a byte
        // combination the real Aventa ECU never accepted, so "off" silently
        // had no effect.
    } else if (key == "aircon_vent_mode") {
        uint8_t v;
        if (!stringToAirconVentMode(value, v)) return false;
        airconVentModeRaw_ = v; fAirconVentMode_.pending = true; airconField = true;
    } else if (key == "target_temp_aircon") {
        targetTempAirconRaw_ = decimalToTempCode(value.toFloat(), true);
        fTargetTempAircon_.pending = true; airconField = true;
    } else {
        return false;
    }

    if (airconField) uploadAircon_ = 2;
    uploadWait_ = 3; // wait for 3 idle poll cycles to collect further commands, like the original firmware
    return true;
}

// -----------------------------------------------------------------------------
// Publishing
// -----------------------------------------------------------------------------
void TrumaStatus::collectPublishPairs(std::vector<std::pair<String, String>> &out, bool onlyChanged) {
    auto push = [&](Flag &f, const char *name, const String &val) {
        if (!onlyChanged || f.pending) {
            out.push_back({String(name), val});
            f.pending = false;
        }
    };

    push(fAlive_, "alive", alive_ ? "ON" : "OFF");
    push(fClock_, "clock", clockToString(clockRaw_));
    push(fAirconOperatingMode_, "aircon_operating_mode", airconOperatingModeToString(airconOperatingModeRaw_));
    push(fAirconVentMode_, "aircon_vent_mode", airconVentModeToString(airconVentModeRaw_));
    push(fTargetTempAircon_, "target_temp_aircon", String(tempCodeToDecimal(targetTempAirconRaw_), 1));
    push(fCurrentTempAircon_, "current_temp_aircon", String(tempCodeToDecimal(currentTempAirconRaw_), 1));
    push(fCurrentTempRoom_, "current_temp_room", String(tempCodeToDecimal(currentTempRoomRaw_), 1));
}

// -----------------------------------------------------------------------------
// Alive / watchdog handling
// -----------------------------------------------------------------------------
void TrumaStatus::markAlivePoll() {
    aliveSeenInWindow_ = true;
}

void TrumaStatus::evaluateAliveWindow() {
    uint32_t now = millis();
    if (now - aliveWindowStartMs_ < ALIVE_WINDOW_MS) return;
    aliveWindowStartMs_ = now;
    bool wasAlive = alive_;
    alive_ = aliveSeenInWindow_;
    aliveSeenInWindow_ = false;
    if (wasAlive != alive_) fAlive_.pending = true;
}

// -----------------------------------------------------------------------------
// Write-buffer construction
// -----------------------------------------------------------------------------
std::vector<uint8_t> TrumaStatus::encodeAirconContent() {
    commandCounter_ = (commandCounter_ + 1) % 0xFF;
    std::vector<uint8_t> c(26, 0);
    c[0] = commandCounter_;
    c[1] = 0; // checksum placeholder
    c[2] = airconOperatingModeRaw_;
    c[3] = 0;
    c[4] = airconVentModeRaw_;
    c[5] = airconOn_;
    c[6] = targetTempAirconRaw_ & 0xFF;
    c[7] = (targetTempAirconRaw_ >> 8) & 0xFF;
    // c[8..25] stay zero (dummy)
    return c;
}

std::vector<uint8_t> TrumaStatus::encodeClockContent() {
    commandCounter_ = (commandCounter_ + 1) % 0xFF;
    std::vector<uint8_t> c(26, 0);
    c[0] = commandCounter_;
    c[1] = 0; // checksum placeholder
    c[2] = pendingClockHour_;
    c[3] = pendingClockMinute_;
    c[4] = pendingClockSecond_;
    c[5] = 0x01; // display_1 - must be 0x1
    c[6] = 0x01; // display_2 - must be 0x1
    c[7] = 0;    // display_3
    c[8] = clockModeRaw_; // echo the mode (24h/12h) last reported by the CPplus
    c[9] = 0;    // clock_source
    c[10] = 0;   // display_4
    c[11] = 0;   // display_5
    // c[12..25] stay zero (unused - message length 0x0A only covers c[2..11])
    return c;
}

std::vector<std::vector<uint8_t>> TrumaStatus::buildTransferFrames(uint8_t headerHi, uint8_t headerLo,
                                                                    std::vector<uint8_t> content) {
    // full = preamble(8) + header(2) + content(26) = 36 bytes
    static const uint8_t PREAMBLE[8] = {0x00, 0x00, 0x22, 0xFF, 0xFF, 0xFF, 0x54, 0x01};
    uint8_t full[36];
    memcpy(full, PREAMBLE, 8);
    full[8] = headerHi;
    full[9] = headerLo;
    memcpy(full + 10, content.data(), 26);

    // checksum covers bytes [6:36) (30 bytes), with content[1] still zero
    uint8_t cs = linChecksum(full + 6, 30);
    full[11] = cs; // = header(2) + content[1]

    std::vector<std::vector<uint8_t>> frames;
    frames.push_back({0x03, 0x10, 0x29, 0xFA, 0x00, 0x1F, 0x00, 0x1E});
    for (int i = 0; i < 6; i++) {
        std::vector<uint8_t> f;
        f.push_back(0x03);
        f.push_back(0x21 + i);
        for (int b = 0; b < 6; b++) f.push_back(full[i * 6 + b]);
        frames.push_back(f);
    }
    for (auto &f : frames) {
        uint8_t chk = linChecksum(f.data(), f.size());
        f.push_back(chk);
    }
    return frames;
}

void TrumaStatus::requestClockSync(uint8_t hour, uint8_t minute, uint8_t second) {
    pendingClockHour_ = hour;
    pendingClockMinute_ = minute;
    pendingClockSecond_ = second;
    uploadClock_ = 2;
    uploadWait_ = 3;
}

std::vector<std::vector<uint8_t>> TrumaStatus::buildPendingWriteFrames() {
    if (uploadAircon_ > 0) {
        uploadAircon_--;
        return buildTransferFrames(0x0C, 0x34, encodeAirconContent());
    }
    if (uploadClock_ > 0) {
        uploadClock_--;
        return buildTransferFrames(0x0A, 0x14, encodeClockContent());
    }
    return {};
}
