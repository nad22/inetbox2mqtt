#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <vector>
#include "TrumaStatus.h"
#include "Pins.h"

// -----------------------------------------------------------------------------
// LinBus
//
// C++ port of lin.py: implements the ESP32 side of the (emulated) LIN slave
// that answers the TRUMA CPplus. The CPplus is the LIN master; this firmware
// impersonates the "inetbox" LIN slave node, faithfully reproducing the exact
// byte sequences of the original MicroPython implementation (registration
// handshake, heartbeat, alive-poll (raw PID 0xD8) and the 6-frame buffer
// up-/download used to read status and to write Aventa aircon commands).
// -----------------------------------------------------------------------------
class LinBus {
public:
    void begin(TrumaStatus *status, int linLedPin = -1);

    // Must be called as often as possible from the main loop().
    void loop();

    // true once the CPplus has been observed polling us (registration done)
    bool isRegistered() const { return registered_; }

    // Count of 12-byte frames (or 6-frame buffer downloads) that did not
    // match any known pattern since boot. A rising count while "registered"
    // stays false during the init handshake is a strong signal that the
    // CPplus IS sending something but this firmware isn't recognizing it
    // (bus noise, an unexpected variant, or wrong byte content) - useful for
    // diagnosing "CPplus doesn't see the ESP as an inetbox" reports without
    // needing a serial monitor attached in the vehicle.
    uint32_t unknownFrameCount() const { return unknownFrameCount_; }

private:
    HardwareSerial serial_{LIN_UART_NUM};
    TrumaStatus *status_ = nullptr;
    int linLedPin_ = -1;
    bool registered_ = false;
    uint32_t unknownFrameCount_ = 0;

    std::vector<std::vector<uint8_t>> responseQueue_;

    void sendAnswer(const uint8_t *data, size_t len);
    void sendAnswer(std::initializer_list<uint8_t> data);
    void enqueueResponse(std::vector<uint8_t> frame);
    void answerQueuedRequest();
    void handleAlivePoll();
    void handleKnownFrame(const uint8_t frame[12]);
    void generateInetUpload();
    void setLinLed(bool on);
    void toggleLinLed();
};
