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

static String heatingModeToString(uint8_t v) {
    if (v == 0) return "off";
    if (v == 1) return "eco";
    if (v == 10) return "high";
    return "UNKNOWN(" + String(v) + ")";
}
static bool stringToHeatingMode(const String &s, uint8_t &out) {
    if (s == "off") { out = 0; return true; }
    if (s == "eco") { out = 1; return true; }
    if (s == "high") { out = 10; return true; }
    return false;
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

static String energyMixCodeToString(uint8_t v) {
    switch (v & 0x03) {
        case 0b00: return "none";
        case 0b01: return "gas";
        case 0b10: return "electricity";
        case 0b11: return "mix";
    }
    return "UNKNOWN";
}
static bool stringToEnergyMixCode(const String &s, uint8_t &out) {
    if (s == "none") { out = 0b00; return true; }
    if (s == "gas") { out = 0b01; return true; }
    if (s == "electricity") { out = 0b10; return true; }
    if (s == "mix") { out = 0b11; return true; }
    return false;
}

static bool stringToElPowerCode(const String &s, uint16_t &out) {
    long v = s.toInt();
    if (v == 0 || v == 900 || v == 1800) { out = (uint16_t)v; return true; }
    return false;
}

static String operatingStatusToString(uint8_t v) {
    switch (v) {
        case 0: return "Off";
        case 1: return "WARNING";
        case 4: return "start/cool down";
        case 5: return "On(5)";
        case 6: return "On(6)";
        case 7: return "On(7)";
        default: return "On(" + String(v) + ")";
    }
}

static String errorCodeToString(uint16_t raw) {
    int code = (raw / 256) * 100 + (raw % 256);
    return String(code);
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
    // STATUS_BUFFER_HEADER_RECV_STATUS = 0x14, 0x33  (Combi heater status)
    if (bufIdHi == 0x14 && bufIdLo == 0x33) {
        targetTempRoomRaw_    = readLE(p, len, 2, 2); fTargetTempRoom_.pending = true;
        heatingModeRaw_       = readLE(p, len, 4, 1); fHeatingMode_.pending = true;
        elPowerLevelRaw_      = readLE(p, len, 6, 2); fElPowerLevel_.pending = true;
        targetTempWaterRaw_   = readLE(p, len, 8, 2); fTargetTempWater_.pending = true;
        energyMixRaw_         = readLE(p, len, 12, 1); fEnergyMix_.pending = true;
        currentTempWaterRaw_  = readLE(p, len, 14, 2); fCurrentTempWater_.pending = true;
        currentTempRoomRaw_   = readLE(p, len, 16, 2); fCurrentTempRoom_.pending = true;
        operatingStatusRaw_   = readLE(p, len, 18, 1); fOperatingStatus_.pending = true;
        errorCodeRaw_         = readLE(p, len, 19, 2); fErrorCode_.pending = true;
        return;
    }
    // STATUS_BUFFER_HEADER_04 = 0x12, 0x35 (Aventa aircon status)
    if (bufIdHi == 0x12 && bufIdLo == 0x35) {
        airconOperatingModeRaw_ = readLE(p, len, 2, 1); fAirconOperatingMode_.pending = true;
        airconVentModeRaw_      = readLE(p, len, 4, 1); fAirconVentMode_.pending = true;
        targetTempAirconRaw_    = readLE(p, len, 6, 2); fTargetTempAircon_.pending = true;
        return;
    }
    // STATUS_BUFFER_HEADER_03 = 0x0A, 0x15 (clock + display)
    if (bufIdHi == 0x0A && bufIdLo == 0x15) {
        clockRaw_ = readLE(p, len, 2, 2); fClock_.pending = true;
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
    return key == "target_temp_room" || key == "target_temp_water" ||
           key == "heating_mode" || key == "el_power_level" || key == "energy_mix" ||
           key == "aircon_operating_mode" || key == "aircon_vent_mode" || key == "target_temp_aircon";
}

bool TrumaStatus::setByTopic(const String &key, const String &value) {
    bool heaterField = false;
    bool airconField = false;

    if (key == "target_temp_room") {
        targetTempRoomRaw_ = decimalToTempCode(value.toFloat(), true);
        fTargetTempRoom_.pending = true; heaterField = true;
    } else if (key == "target_temp_water") {
        targetTempWaterRaw_ = decimalToTempCode(value.toFloat(), true);
        fTargetTempWater_.pending = true; heaterField = true;
    } else if (key == "heating_mode") {
        uint8_t v;
        if (!stringToHeatingMode(value, v)) return false;
        heatingModeRaw_ = v; fHeatingMode_.pending = true; heaterField = true;
    } else if (key == "el_power_level") {
        uint16_t v;
        if (!stringToElPowerCode(value, v)) return false;
        elPowerLevelRaw_ = v; fElPowerLevel_.pending = true; heaterField = true;
    } else if (key == "energy_mix") {
        uint8_t v;
        if (!stringToEnergyMixCode(value, v)) return false;
        energyMixRaw_ = v; fEnergyMix_.pending = true; heaterField = true;
    } else if (key == "aircon_operating_mode") {
        uint8_t v;
        if (!stringToAirconOperatingMode(value, v)) return false;
        airconOperatingModeRaw_ = v; fAirconOperatingMode_.pending = true; airconField = true;
        airconOn_ = (v != 0) ? 1 : 0;
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

    if (heaterField) uploadHeater_ = 2;
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
    push(fTargetTempRoom_, "target_temp_room", String(tempCodeToDecimal(targetTempRoomRaw_), 1));
    push(fTargetTempWater_, "target_temp_water", String(tempCodeToDecimal(targetTempWaterRaw_), 1));
    push(fHeatingMode_, "heating_mode", heatingModeToString(heatingModeRaw_));
    push(fElPowerLevel_, "el_power_level", String(elPowerLevelRaw_));
    push(fEnergyMix_, "energy_mix", energyMixCodeToString(energyMixRaw_));
    push(fCurrentTempWater_, "current_temp_water", String(tempCodeToDecimal(currentTempWaterRaw_), 1));
    push(fCurrentTempRoom_, "current_temp_room", String(tempCodeToDecimal(currentTempRoomRaw_), 1));
    push(fOperatingStatus_, "operating_status", operatingStatusToString(operatingStatusRaw_));
    push(fErrorCode_, "error_code", errorCodeToString(errorCodeRaw_));
    push(fAirconOperatingMode_, "aircon_operating_mode", airconOperatingModeToString(airconOperatingModeRaw_));
    push(fAirconVentMode_, "aircon_vent_mode", airconVentModeToString(airconVentModeRaw_));
    push(fTargetTempAircon_, "target_temp_aircon", String(tempCodeToDecimal(targetTempAirconRaw_), 1));
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
std::vector<uint8_t> TrumaStatus::encodeHeaterContent() {
    commandCounter_ = (commandCounter_ + 1) % 0xFF;
    std::vector<uint8_t> c(26, 0);
    c[0] = commandCounter_;
    c[1] = 0; // checksum placeholder
    c[2] = targetTempRoomRaw_ & 0xFF;
    c[3] = (targetTempRoomRaw_ >> 8) & 0xFF;
    c[4] = heatingModeRaw_;
    c[5] = 0;
    c[6] = elPowerLevelRaw_ & 0xFF;
    c[7] = (elPowerLevelRaw_ >> 8) & 0xFF;
    c[8] = targetTempWaterRaw_ & 0xFF;
    c[9] = (targetTempWaterRaw_ >> 8) & 0xFF;
    c[10] = elPowerLevelRaw_ & 0xFF;
    c[11] = (elPowerLevelRaw_ >> 8) & 0xFF;
    c[12] = energyMixRaw_;
    c[13] = energyMixRaw_;
    // c[14..25] stay zero (dummy)
    return c;
}

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

std::vector<std::vector<uint8_t>> TrumaStatus::buildPendingWriteFrames() {
    if (uploadAircon_ > 0) {
        uploadAircon_--;
        return buildTransferFrames(0x0C, 0x34, encodeAirconContent());
    }
    if (uploadHeater_ > 0) {
        uploadHeater_--;
        return buildTransferFrames(0x0C, 0x32, encodeHeaterContent());
    }
    return {};
}
