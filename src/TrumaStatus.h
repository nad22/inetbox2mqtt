#pragma once

#include <Arduino.h>
#include <vector>

// -----------------------------------------------------------------------------
// TrumaStatus
//
// C++ port of the relevant parts of inetboxapp.py / conversions.py, scoped to
// the Truma Aventa Comfort (2. Gen) air-conditioning unit. Combi heater /
// hot-water control has been removed entirely; the only value still read
// from the heater's status buffer is the current room temperature, which
// feeds the Aventa climate entity's "current_temperature".
//
// It owns:
//   - the current status values (raw + human readable),
//   - the "dirty" flags that decide whether a value must be published on MQTT,
//   - the logic to decode status buffers received from the CPplus and to
//     encode the write-buffer used to command the aircon.
// -----------------------------------------------------------------------------
class TrumaStatus {
public:
    TrumaStatus();

    // ---- Called by LinBus when a full status buffer has been received ----
    // bufId: the 2 raw id bytes taken from the decoded CPplus buffer.
    void applyStatusBuffer(uint8_t bufIdHi, uint8_t bufIdLo, const uint8_t *payload, size_t len);

    // ---- Called by MQTT / Web UI to change a value ----
    // key matches the MQTT topic suffix (e.g. "target_temp_aircon").
    // Returns true if the key was recognised and the value was valid.
    bool setByTopic(const String &key, const String &value);

    // Returns true if the given key is a known/settable control topic.
    bool isSettable(const String &key) const;

    // ---- Publishing ----
    // Appends (topic-suffix, value) pairs for every field that changed since
    // the last call (onlyChanged = true) or for all fields (onlyChanged = false).
    void collectPublishPairs(std::vector<std::pair<String, String>> &out, bool onlyChanged);

    // ---- Alive / watchdog handling (mirrors the D8/0x18 poll based watchdog) ----
    void markAlivePoll();     // a 0x18(raw 0xD8) poll frame was seen
    void evaluateAliveWindow();   // called periodically to roll ON/OFF over the poll window
    bool isAlive() const { return alive_; }

    // ---- Outgoing buffer construction ----
    bool hasAirconUpload() const { return uploadAircon_ > 0; }
    bool hasAnyUpload() const { return hasAirconUpload(); }

    // Builds the 7 LIN frames (each including its trailing checksum byte) for
    // the pending aircon command buffer, if any.
    std::vector<std::vector<uint8_t>> buildPendingWriteFrames();

    int &uploadWaitCounter() { return uploadWait_; }

private:
    // raw values
    uint8_t commandCounter_ = 1;
    uint16_t currentTempRoomRaw_ = 0;

    uint8_t airconOperatingModeRaw_ = 0;   // 0 off,4 vent,5 cool,6 hot,7 auto
    uint8_t airconVentModeRaw_ = 114;      // 113 low,114 mid,115 high,116 night,119 auto
    uint16_t targetTempAirconRaw_ = 2990;  // ~= 26 degC
    uint8_t airconOn_ = 1;

    uint16_t clockRaw_ = 0;

    bool alive_ = false;
    uint32_t aliveWindowStartMs_ = 0;
    bool aliveSeenInWindow_ = false;
    static const uint32_t ALIVE_WINDOW_MS = 60000; // matches the ~60s alive heartbeat of the original firmware

    // publish-pending flags, one per published field, indexed by name below
    struct Flag { bool pending = false; };
    // We keep an explicit small table instead of a map for speed/simplicity.
    Flag fCurrentTempRoom_,
        fAirconOperatingMode_, fAirconVentMode_, fTargetTempAircon_, fClock_, fAlive_;

    int uploadAircon_ = 0;  // mirrors python "upload02_buffer" countdown
    int uploadWait_ = 1;    // mirrors python "upload_wait"

    std::vector<uint8_t> encodeAirconContent();
    std::vector<std::vector<uint8_t>> buildTransferFrames(uint8_t headerHi, uint8_t headerLo,
                                                           std::vector<uint8_t> content);
};
