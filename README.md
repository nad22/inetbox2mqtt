
# inetbox2mqtt (C++ / ESP32 rewrite)

Control your **TRUMA Aventa Comfort (2. Gen)** air-conditioning via MQTT and
Home Assistant.

[![Badge License: MIT](https://img.shields.io/badge/License-MIT-brightgreen.svg)](https://github.com/git/git-scm.com/blob/main/MIT-LICENSE.txt)
&nbsp;
[![Stand With Ukraine](https://raw.githubusercontent.com/vshymanskyy/StandWithUkraine/main/badges/StandWithUkraine.svg)](https://stand-with-ukraine.pp.ua)

> **This is a fork.** The original project, protocol research and the
> MicroPython implementation this rewrite is based on were created by
> Dr. Magnus Christ ([mc0110/inetbox2mqtt](https://github.com/mc0110/inetbox2mqtt)),
> building on [inetbox.py](https://github.com/danielfett/inetbox.py) by
> Daniel Fett and the [WoMoLIN](https://github.com/muccc/WomoLIN) project.
> This fork **completely rewrites the firmware in C++ as a PlatformIO
> project for the ESP32**, and simplifies/streamlines the feature set around
> the Aventa Comfort 2. All credit for the original LIN protocol
> reverse-engineering goes to the upstream authors - please go there for the
> MicroPython version, other TRUMA devices, or hardware not covered here.

## What changed compared to upstream

- **Complete rewrite in C++** as a standard **PlatformIO** project (no more
  MicroPython, no `mip`/OTA bootstrapping, no precompiled `.bin` images).
- **Single ESP32 Dev Kit target.** Other boards (RP2 pico W, WoMoLin
  variants, ...) are not supported by this fork.
- **One firmware mode.** The old "OS mode" (setup/AP) vs. "Normal-Run mode"
  switch (and the associated reboot-to-switch-mode dance) is gone entirely.
  The device always runs its full LIN/MQTT logic *and* always serves a
  configuration/control web page - if no WiFi is configured yet (or the
  configured network can't be reached), it simply also opens a fallback
  Access Point so you can reach that same web page.
- **Focused on the Truma Aventa Comfort (2. Gen)** air-conditioning unit.
  Combi heater / hot-water control and monitoring has been **removed
  entirely** (only the current room temperature is still read from its
  status buffer, since it feeds the Aventa climate entity's current
  temperature); DuoControl and MPU6050 spirit-level add-ons from the
  upstream project were dropped too, to keep the rewrite maintainable.
- **Stale MQTT commands are discarded on (re)connect.** See
  [below](#why-stale-commands-are-discarded).
- **Native Home Assistant `climate` entity** for the aircon instead of a set
  of disconnected `select` helpers.

## Features

- PlatformIO project for a generic **ESP32 Dev Kit** board
- Simulates the TRUMA "inetbox" on the LIN bus (registers with the CPplus,
  reads status, writes commands) - protocol-compatible with CPplus ≥ C4.00.00
- MQTT status/control with Home Assistant MQTT auto-discovery
- Built-in web UI (no app, no cloud) for Wi-Fi/MQTT setup **and** for
  monitoring/controlling the aircon directly from the ESP32
- Automatically falls back to a configuration Access Point when no working
  Wi-Fi connection is available
- Discards MQTT messages that were queued/retained on the broker before boot,
  so the air conditioning can't be driven by a "stuck" command from a
  previous session

## Hardware

You need an **ESP32 Dev Kit** plus a LIN transceiver (e.g. a TJA1020-based
board, or a ready-made module such as the
[WoMoLin lin-interface](https://womolin.de/products/lin-interface/)) wired to
ESP32 **UART2** (`GPIO17` = TX, `GPIO16` = RX). See
[doc/ELECTRIC.md](doc/ELECTRIC.md) for wiring details, and adjust
[`include/Pins.h`](include/Pins.h) if your board uses different pins.

Requires a CPplus with firmware **≥ C4.00.00** (see the original project's
notes on older CPplus versions).

## Getting started

1. Install [PlatformIO](https://platformio.org/) (standalone or the VS Code
   extension).
2. Wire the ESP32 to your LIN transceiver as described in
   [doc/ELECTRIC.md](doc/ELECTRIC.md).
3. Build and flash:

   ```sh
   pio run -e esp32dev -t upload
   pio device monitor
   ```

4. On first boot (or whenever no Wi-Fi is configured / reachable), the ESP32
   opens an open (no-password) Access Point named `inetbox2mqtt-XXXXXX`.
   Connect to it and open `http://192.168.4.1/` in a browser.
5. Go to **Einrichtung** (Setup), enter your WiFi SSID/password and your MQTT
   broker's host/port (and credentials, if needed). Save - the device
   reboots and joins your network.
6. Open the same web UI on your normal network (check your router, or the
   serial monitor, for the assigned IP) to watch status and to control the
   aircon/heater directly, without needing MQTT/Home Assistant at all.

## Registering with the CPplus (INIT process)

Exactly as with the upstream project, the ESP32 must be registered with the
CPplus once:

1. Power the ESP32 with the LIN transceiver connected to the CPplus.
2. On the CPplus, open the menu, select **RESET**, then confirm **PR SET**.
   The display shows a flickering `INIT...`.
3. Once finished, the CPplus INIT menu shows a third entry (`inetbox:
   T23.70.0`) in addition to the TRUMA and CPplus entries - registration is
   complete and persists across reboots of both devices.

If an inetbox/CPplus pairing already existed from a previous device, perform
one INIT **without** this ESP32 connected first (so only 2 entries remain),
then connect this ESP32 and INIT again.

## MQTT topics

Default topic prefix is `truma` (configurable in the web UI); topics are
`service/<prefix>/control_status/<key>` (published by the device) and
`service/<prefix>/set/<key>` (commands you send to it).

### Status (published)

| Topic suffix | Payload | Description |
|---|---|---|
| `alive` | `ON`/`OFF` | LIN/CPplus link active |
| `clock` | `hh:mm` | CPplus clock |
| `release` | `x.y.z` | Firmware version |
| `current_temp_room` | °C | Current room temperature |
| `aircon_operating_mode` | `off`/`vent`/`cool`/`hot`/`auto` | Aventa operating mode |
| `aircon_vent_mode` | `low`/`mid`/`high`/`night`/`auto` | Aventa fan mode |
| `target_temp_aircon` | °C | Aventa target temperature |

### Commands (subscribe / publish to)

| Topic suffix | Payload | Description |
|---|---|---|
| `aircon_operating_mode` | `off`/`vent`/`cool`/`hot`/`auto` | Set Aventa mode |
| `aircon_vent_mode` | `low`/`mid`/`high`/`night`/`auto` | Set Aventa fan mode |
| `target_temp_aircon` | °C (16-32) | Set Aventa target temperature |
| `reboot` | `1` | Reboot the ESP32 |

For the Aventa, only certain `aircon_operating_mode` / `aircon_vent_mode`
combinations make sense (`off`+`low`, `auto`+`auto`,
`vent`/`cool`/`hot`+`low`/`mid`/`high`).

## Why stale commands are discarded

If the ESP32 loses power or network for a while, some MQTT brokers (or
clients publishing with QoS/retain) may have a command message from *before*
the outage waiting for it - e.g. a "turn cooling on" message a phone sent
right before you drove into a tunnel and lost connectivity. Blindly applying
that on reconnect could switch on the air conditioning unexpectedly and
unattended.

To prevent this, the firmware **discards every command message that arrives
within a short grace period (default 4 seconds) after a fresh MQTT
connection**, logging it instead of applying it. Since retained/queued
messages are always delivered immediately after (re-)subscribing, this
reliably filters them out while still reacting normally to anything sent
afterwards. If you rely on retained command topics, avoid retaining them, or
increase `mqttBootDiscardMs` in [`src/AppConfig.h`](src/AppConfig.h).

## Home Assistant

See [homeassistant/README.md](homeassistant/README.md) for the full list of
auto-discovered entities and an example dashboard
([homeassistant/dashboard.yaml](homeassistant/dashboard.yaml)).

## Firmware updates (OTA)

The device can update its own firmware over the air, without a USB/serial
connection, in three ways (all from the **Einrichtung** tab of the web UI):

- **Check & install from the repo** - "Nach Update suchen" fetches a small
  `manifest.json` (URL configurable as *OTA Manifest-URL*, defaults to this
  repo's `main` branch) describing the latest released version and a
  download URL. If it's newer than the running firmware, "Update
  installieren" downloads and flashes it, then reboots.
- **Manual upload** - pick a `firmware.bin` file (e.g. one built locally with
  `pio run -e esp32dev`, found under `.pio/build/esp32dev/firmware.bin`, or
  downloaded from a GitHub Release) and click "Hochladen & installieren" to
  flash it directly, without going through the manifest at all.
- **Event log** - the *Log* tab shows recent commands/events (MQTT, web UI,
  OTA, system boot) so you can confirm an update was applied, or see why a
  command was rejected/discarded. Note that MQTT itself doesn't tell a
  subscriber who published a message - "mqtt" as a source only means the
  command arrived via the broker, not which client sent it.

### Releasing a new version

1. Bump `FW_VERSION` in [`src/Version.h`](src/Version.h).
2. Commit, then tag and push: `git tag v3.1.0 && git push origin v3.1.0`
   (tag must match `v<major>.<minor>.<patch>`).
3. [`.github/workflows/release-firmware.yml`](.github/workflows/release-firmware.yml)
   builds the firmware with PlatformIO, publishes a GitHub Release with
   `firmware.bin` attached, and commits the updated
   [`firmware/manifest.json`](firmware/manifest.json) back to `main` so
   devices in the field pick it up on their next "Nach Update suchen".

### Security note

OTA downloads use HTTPS but **do not validate the server's TLS certificate**
(`WiFiClientSecure::setInsecure()`), to avoid maintaining a pinned root CA
that would need updating as certificates rotate. This protects against
passive eavesdropping but not against an active man-in-the-middle on the
network path to GitHub. If you need stronger guarantees, use the manual
upload flow over a trusted local network instead of the repo-based check.

## Disclaimer

This project simulates a TRUMA inetbox on the LIN bus. It only works with a
CPplus that has **no** real inetbox connected, and does **not** support a
TRUMA iNet X (the inetbox's successor). Test with a clean, properly grounded
electrical setup. As with the upstream project: **no liability or warranty
is given for its use.**

## License

MIT, see [LICENSE](LICENSE) - originally © Dr. Magnus Christ (mc0110), with
modifications for this fork's C++/PlatformIO rewrite.

## Acknowledgements

- [mc0110/inetbox2mqtt](https://github.com/mc0110/inetbox2mqtt) - the
  original project this is forked from, including all LIN protocol details
  reused here.
- [inetbox.py](https://github.com/danielfett/inetbox.py) by Daniel Fett.
- [WoMoLIN](https://github.com/muccc/WomoLIN).

