#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
// OtaManager
//
// Firmware updates "über Repo": inetbox2mqtt fetches a small JSON manifest
// (produced by .github/workflows/release-firmware.yml on every version tag
// and committed to firmware/manifest.json) that points at the matching
// firmware.bin GitHub Release asset, compares versions, and - only when the
// user explicitly triggers it from the web UI - downloads and flashes it via
// the ESP32 Update/OTA partition mechanism.
//
// Security note: HTTPS downloads use WiFiClientSecure::setInsecure(), i.e.
// the server certificate is NOT validated. This avoids having to maintain a
// pinned root CA (which expires/rotates), at the cost of trusting whatever
// answers on the configured manifest/firmware host. Only point this at a
// host/URL you trust.
// -----------------------------------------------------------------------------
struct OtaCheckResult {
    bool ok = false;
    String error;
    String currentVersion;
    String latestVersion;
    String downloadUrl;
    bool updateAvailable = false;
};

class OtaManager {
public:
    static OtaCheckResult checkForUpdate(const String &manifestUrl);

    // Downloads and flashes the firmware at `url` into the inactive OTA
    // partition, blocking until done. Returns true on success (caller is
    // responsible for rebooting); returns false and fills `error` otherwise.
    static bool installFromUrl(const String &url, String &error);
};
