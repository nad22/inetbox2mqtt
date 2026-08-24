#include "LinBus.h"
#include "LinChecksum.h"
#include <cstring>

// -----------------------------------------------------------------------------
// Known fixed-length (12 byte) request frames and their canned responses.
// This is a direct port of the `cmd_ctrl` dictionary in the original lin.py.
// Frame layout: 00 55 <PID> <8 data bytes> <checksum>
// -----------------------------------------------------------------------------
namespace {

struct FrameRule {
    uint8_t pattern[12];
    const uint8_t *response; // 8 data bytes, checksum appended automatically; nullptr = no answer
    size_t responseLen;      // length of response (without checksum), 0 if none
    bool isUploadTrigger;    // true for the "BA request" that starts a buffer upload
};

const uint8_t RESP_B2[] = {0x03, 0x06, 0xf2, 0x17, 0x46, 0x00, 0x1f, 0x00};
const uint8_t RESP_B0[] = {0x03, 0x01, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff};
const uint8_t RESP_HEARTBEAT[] = {0x03, 0x02, 0xf9, 0x00, 0xff, 0xff, 0xff, 0xff};
const uint8_t RESP_BUFFER_ACK[] = {0x03, 0x01, 0xfb, 0xff, 0xff, 0xff, 0xff, 0xff};

const FrameRule kFrameRules[] = {
    // B2 - response request (initialisation started)
    {{0x00, 0x55, 0x3c, 0x7f, 0x06, 0xb2, 0x00, 0x17, 0x46, 0x00, 0x1f, 0x4b}, RESP_B2, 8, false},
    // NAD 03 response - ack (no answer expected)
    {{0x00, 0x55, 0x03, 0xaa, 0x0a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x48}, nullptr, 0, false},
    // B2 - identifier for NAD 03
    {{0x00, 0x55, 0x3c, 0x03, 0x06, 0xb2, 0x20, 0x17, 0x46, 0x00, 0x1f, 0xa7}, RESP_B2, 8, false},
    // B2 - initializer for NAD 03 -> starts registration
    {{0x00, 0x55, 0x3c, 0x03, 0x06, 0xb2, 0x22, 0x17, 0x46, 0x00, 0x1f, 0xa5}, RESP_B2, 8, false},
    // B0 - init finalized -> registration complete
    {{0x00, 0x55, 0x3c, 0x7f, 0x06, 0xb0, 0x17, 0x46, 0x00, 0x1f, 0x03, 0x4a}, RESP_B0, 8, false},
    // Heartbeat for NAD 03
    {{0x00, 0x55, 0x3c, 0x03, 0x05, 0xb9, 0x00, 0x1f, 0x00, 0x00, 0xff, 0x1f}, RESP_HEARTBEAT, 8, false},
    // Frame 1 of a 6-frame buffer transfer announcement (no answer)
    {{0x00, 0x55, 0x3c, 0x03, 0x10, 0x29, 0xbb, 0x00, 0x1f, 0x00, 0x1e, 0xca}, nullptr, 0, false},
    // BA request: CPplus asks us to upload our buffer (aircon command)
    {{0x00, 0x55, 0x3c, 0x03, 0x10, 0x0b, 0xba, 0x00, 0x1f, 0x00, 0x1e, 0xe9}, nullptr, 0, true},
};

constexpr uint8_t kBufTransPrefix[4] = {0x00, 0x55, 0x3c, 0x03};

} // namespace

void LinBus::begin(TrumaStatus *status, int linLedPin) {
    status_ = status;
    linLedPin_ = linLedPin;
    if (linLedPin_ >= 0) {
        pinMode(linLedPin_, OUTPUT);
        setLinLed(false);
    }
    serial_.begin(LIN_UART_BAUD, SERIAL_8N1, LIN_UART_RX_PIN, LIN_UART_TX_PIN);
}

void LinBus::setLinLed(bool on) {
    if (linLedPin_ >= 0) digitalWrite(linLedPin_, on ? HIGH : LOW);
}

void LinBus::toggleLinLed() {
    if (linLedPin_ >= 0) digitalWrite(linLedPin_, !digitalRead(linLedPin_));
}

void LinBus::sendAnswer(const uint8_t *data, size_t len) {
    serial_.write(data, len);
    serial_.flush();
    toggleLinLed();
}

void LinBus::sendAnswer(std::initializer_list<uint8_t> data) {
    std::vector<uint8_t> v(data);
    sendAnswer(v.data(), v.size());
}

void LinBus::enqueueResponse(std::vector<uint8_t> frame) {
    responseQueue_.push_back(std::move(frame));
}

void LinBus::answerQueuedRequest() {
    if (responseQueue_.empty()) return;
    std::vector<uint8_t> frame = responseQueue_.front();
    responseQueue_.erase(responseQueue_.begin());
    sendAnswer(frame.data(), frame.size());
}

void LinBus::generateInetUpload() {
    if (!status_->hasAnyUpload()) return;
    auto frames = status_->buildPendingWriteFrames();
    for (auto &f : frames) enqueueResponse(f);
}

// Raw PID 0xD8 corresponds to PID 0x18 with parity bits: this is the periodic
// "are you alive / do you have something to send" poll from the CPplus.
void LinBus::handleAlivePoll() {
    registered_ = true;
    status_->markAlivePoll();
    setLinLed(true);

    int &waitCounter = status_->uploadWaitCounter();
    bool wantsToSend = false;
    if (waitCounter <= 0) {
        wantsToSend = status_->hasAnyUpload();
    }

    if (wantsToSend) {
        waitCounter = 4;
        sendAnswer({0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x27});
    } else {
        sendAnswer({0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28});
        if (waitCounter > 0) waitCounter--;
    }
}

void LinBus::handleKnownFrame(const uint8_t frame[12]) {
    for (const auto &rule : kFrameRules) {
        if (memcmp(rule.pattern, frame, 12) == 0) {
            if (rule.isUploadTrigger) {
                generateInetUpload();
                return;
            }
            if (rule.response != nullptr) {
                uint8_t out[9];
                memcpy(out, rule.response, rule.responseLen);
                out[rule.responseLen] = linChecksum(rule.response, rule.responseLen);
                sendAnswer(out, rule.responseLen + 1);
            }
            return;
        }
    }
    // Unknown frame: this firmware doesn't react to it, but log it raw so
    // the bus can be sniffed for not-yet-reverse-engineered commands (e.g.
    // the Aventa light on/off/level, which the CPplus panel might send as
    // its own distinct 12-byte frame rather than through the aircon status
    // buffer). Capture a serial log with the light off, then one with it
    // toggled on, and diff the [SNIFF] lines to find the frame(s) that
    // differ. Temporary diagnostic, same idea as the [STATUS] dump below.
    {
        String hex;
        for (int i = 0; i < 12; i++) { char b[4]; snprintf(b, sizeof(b), "%02X ", frame[i]); hex += b; }
        Serial.printf("[SNIFF] unknown frame: %s\n", hex.c_str());
    }
}

void LinBus::loop() {
    status_->evaluateAliveWindow();

    if (serial_.available() <= 0) return;

    // Scan the byte stream for the LIN sync byte (0x55). The physical LIN
    // break condition is not decoded separately here (as with the original
    // firmware); we simply look for 0x55 and treat it as "break + sync".
    int b = serial_.read();
    while (b != 0x55) {
        if (serial_.available() <= 0) return;
        b = serial_.read();
    }

    // wait (briefly) for the PID byte
    uint32_t startWait = millis();
    while (serial_.available() <= 0) {
        if (millis() - startWait > 20) return; // avoid an indefinite stall
    }
    uint8_t rawPid = serial_.read();

    uint8_t frame[12];
    frame[0] = 0x00;
    frame[1] = 0x55;
    frame[2] = rawPid;

    if (rawPid == 0xd8) {
        handleAlivePoll();
        return;
    }
    if (rawPid == 0x7d) {
        answerQueuedRequest();
        return;
    }

    // Every other recognised frame is 12 bytes total: wait for the remaining
    // 9 bytes (8 data bytes + checksum).
    startWait = millis();
    while (serial_.available() < 9) {
        if (millis() - startWait > 20) return; // frame did not complete in time
    }
    for (int i = 0; i < 9; i++) frame[3 + i] = serial_.read();

    // Multi-frame buffer download from the CPplus (status buffers)
    if (memcmp(frame, kBufTransPrefix, 4) == 0 && frame[4] >= 0x21 && frame[4] <= 0x26) {
        static uint8_t segments[6][6];
        int idx = frame[4] - 0x21;
        memcpy(segments[idx], &frame[5], 6);
        if (frame[4] == 0x26) {
            // Assemble the 5 usable segments (30 bytes) gathered so far.
            uint8_t buf[30];
            for (int i = 0; i < 5; i++) memcpy(&buf[i * 6], segments[i], 6);
            static const uint8_t kPreamble[8] = {0x00, 0x00, 0x22, 0xff, 0xff, 0xff, 0x54, 0x01};
            if (memcmp(buf, kPreamble, 8) == 0) {
                uint8_t bufIdHi = buf[8];
                uint8_t bufIdLo = buf[9];
                status_->applyStatusBuffer(bufIdHi, bufIdLo, &buf[10], sizeof(buf) - 10);
                std::vector<uint8_t> ack(RESP_BUFFER_ACK, RESP_BUFFER_ACK + 8);
                ack.push_back(linChecksum(ack.data(), 8));
                enqueueResponse(ack);
            } else {
                // Same 6-frame transfer shape, but not the aircon-status
                // preamble this firmware knows about - log it raw so a
                // different kind of download (e.g. carrying a light state)
                // isn't silently dropped during a bus sniff.
                String hex;
                for (int i = 0; i < 30; i++) { char b[4]; snprintf(b, sizeof(b), "%02X ", buf[i]); hex += b; }
                Serial.printf("[SNIFF] buffer download, unknown preamble: %s\n", hex.c_str());
            }
        }
        return;
    }

    handleKnownFrame(frame);
}
