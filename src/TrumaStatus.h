#pragma once

#include <Arduino.h>
#include <vector>

// -----------------------------------------------------------------------------
// TrumaStatus
//
// C++ port of the relevant parts of inetboxapp.py / conversions.py, scoped to
// the Truma Aventa Comfort (2. Gen) air-conditioning unit. Combi heater /
// hot-water control has been removed entirely, including the room
// temperature reading that used to come from the heater's status buffer (no
// physical heater unit exists on this LIN bus, so it never carried real data).
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
    bool hasClockUpload() const { return uploadClock_ > 0; }
    bool hasAnyUpload() const { return hasAirconUpload() || hasClockUpload(); }

    // Requests that the given wall-clock time be written to the CPplus's own
    // clock buffer (0x0A,0x14) on the next LIN poll cycle. Called once after
    // boot once a valid NTP time is available (see main.cpp).
    void requestClockSync(uint8_t hour, uint8_t minute, uint8_t second);

    // Builds the 7 LIN frames (each including its trailing checksum byte) for
    // the pending aircon/clock command buffer, if any (aircon takes priority).
    std::vector<std::vector<uint8_t>> buildPendingWriteFrames();

    int &uploadWaitCounter() { return uploadWait_; }

private:
    // raw values
    uint8_t commandCounter_ = 1;

    uint8_t airconOperatingModeRaw_ = 0;   // 0 off,4 vent,5 cool,6 hot,7 auto
    uint8_t airconVentModeRaw_ = 114;      // 113 low,114 mid,115 high,116 night,119 auto
    uint16_t targetTempAirconRaw_ = 2990;  // ~= 26 degC
    uint8_t airconOn_ = 1;
    uint16_t currentTempAirconRaw_ = 0;    // "actual" temp measured at the Aventa unit itself
    uint16_t currentTempRoomRaw_ = 0;      // "actual" temp measured at the room's wall sensor
    // Aventa light: NOT part of the legacy python source or havanti's fork,
    // found empirically (2026-08-24) by sniffing this same 0x12,0x35 buffer
    // while toggling the physical light on the CPplus panel - p[8..9]
    // (LE 16-bit) is 0 (off) or 20/40/60/80/100 for light level 1-5
    // (CONFIRMED by user on real hardware). Also used as the write-side
    // desired value (see encodeAirconContent()) - writing back to the same
    // byte position it was read from is UNCONFIRMED/untested on real
    // hardware, needs on-vehicle verification.
    uint16_t lightRaw_ = 0;
    // Remembers the last non-zero light level (1-5) so an "on" command that
    // arrives without an explicit level (e.g. HA's light on_command_type
    // "last") knows which level to restore.
    uint8_t lastNonZeroLightLevel_ = 1;

    uint16_t clockRaw_ = 0;
    uint8_t clockModeRaw_ = 0;  // 0 = 24h, 1 = 12h - echoed back unchanged when writing the clock

    uint8_t pendingClockHour_ = 0, pendingClockMinute_ = 0, pendingClockSecond_ = 0;

    bool alive_ = false;
    uint32_t aliveWindowStartMs_ = 0;
    bool aliveSeenInWindow_ = false;
    static const uint32_t ALIVE_WINDOW_MS = 60000; // matches the ~60s alive heartbeat of the original firmware

    // publish-pending flags, one per published field, indexed by name below
    struct Flag { bool pending = false; };
    // We keep an explicit small table instead of a map for speed/simplicity.
    Flag fAirconOperatingMode_, fAirconVentMode_, fTargetTempAircon_, fClock_, fAlive_;
    Flag fCurrentTempAircon_, fCurrentTempRoom_;
    Flag fLight_, fLightState_;

    int uploadAircon_ = 0;  // mirrors python "upload02_buffer" countdown
    int uploadClock_ = 0;   // same countdown scheme, for the clock write buffer
    int uploadWait_ = 1;    // mirrors python "upload_wait"

    std::vector<uint8_t> encodeAirconContent();
    std::vector<uint8_t> encodeClockContent();
    std::vector<std::vector<uint8_t>> buildTransferFrames(uint8_t headerHi, uint8_t headerLo,
                                                           std::vector<uint8_t> content);
};
