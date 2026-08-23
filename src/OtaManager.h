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

// Progress/state of a background install, polled by the web UI (and used to
// decide when the main loop should actually reboot) while the AsyncTCP task
// remains free to keep serving other requests.
enum class OtaPhase {
    Idle,
    Downloading,
    Installing,
    Success,
    Error
};

struct OtaProgress {
    OtaPhase phase = OtaPhase::Idle;
    String version;      // version being installed, for display
    size_t bytesDone = 0;
    size_t bytesTotal = 0;
    String error;
};

class OtaManager {
public:
    static OtaCheckResult checkForUpdate(const String &manifestUrl);

    // Thread-safe snapshot of the most recent checkForUpdate() result,
    // regardless of whether it was triggered at boot, via the web UI, or
    // via MQTT. Used to show update availability on the status page and as
    // a Home Assistant "update" entity without re-fetching the manifest.
    static OtaCheckResult getLastCheckResult();

    // Downloads and flashes the firmware at `url` into the inactive OTA
    // partition, blocking until done. Returns true on success (caller is
    // responsible for rebooting); returns false and fills `error` otherwise.
    static bool installFromUrl(const String &url, String &error);

    // Starts installFromUrl() on a dedicated background FreeRTOS task so the
    // AsyncTCP task stays free to answer other requests (incl. progress
    // polls) for the whole download+flash duration. Returns false if an
    // install is already running. `version` is only used for display.
    static bool startInstall(const String &url, const String &version);

    // Thread-safe snapshot of the current background install's progress.
    static OtaProgress getProgress();

    // True exactly once after a background install finished successfully;
    // the caller (main loop) should then actually reboot.
    static bool consumeRebootRequest();
};

