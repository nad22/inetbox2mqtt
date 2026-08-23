#pragma once

#include <Arduino.h>

// LIN checksum used throughout the CPplus <-> inetbox protocol.
// This is NOT the classic 8-bit-rollover LIN checksum: the CPplus firmware
// sums bytes modulo 0xFF (255), inverts the result and maps 0xFF back to 0x00.
inline uint8_t linChecksum(const uint8_t *data, size_t len) {
    uint32_t cs = 0;
    for (size_t i = 0; i < len; i++) {
        cs = (cs + data[i]) % 0xFF;
    }
    cs = (~cs) & 0xFF;
    if (cs == 0xFF) cs = 0;
    return (uint8_t)cs;
}
