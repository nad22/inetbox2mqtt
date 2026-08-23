# Home Assistant integration

This firmware ships **zero-configuration MQTT discovery**: as soon as it connects to
your MQTT broker it publishes `homeassistant/.../config` messages for every
entity below. If Home Assistant is connected to the same broker, all entities
appear automatically under one device named after the *Gerätename* you chose
during setup (default: `inetbox2mqtt`). Nothing in this folder needs to be
installed for basic operation - it only helps you build a nicer dashboard.

Discovery is re-sent automatically whenever Home Assistant announces itself
(`homeassistant/status` = `online`), so restarting HA (or its Mosquitto
add-on) is enough to get the entities back if they were ever purged.

## Entities published by the firmware

| Entity | Type | Notes |
|---|---|---|
| `binary_sensor.<root>_alive` | binary_sensor | LIN/CPplus link is active |
| `sensor.<root>_release` | sensor | Firmware version |
| `sensor.<root>_clock` | sensor | Clock reported by the CPplus panel |
| `climate.<root>_aventa` | **climate** | Full Aventa Comfort (2. Gen) aircon control |
| `button.<root>_reboot` | button | Restart the ESP32 |
| `update.<root>_firmware` | **update** | Firmware update availability, live install progress, and an "Install" button |

`<root>` is the MQTT topic prefix configured in the web UI (default `truma`).

## Firmware updates

The device checks the configured OTA manifest URL for a new firmware version
once after every boot, and also whenever you press "Nach Update suchen" in the
web UI. The result is exposed as the `update.<root>_firmware` entity: its
"Install" button in Home Assistant triggers the same background install as
the web UI, and its state reflects live progress (downloading/installing)
while the update is being applied - the device reboots automatically once
done.

## The Aventa climate entity

The `climate.aventa` entity maps directly onto the Aventa's own vocabulary:

| Home Assistant HVAC mode | Truma `aircon_operating_mode` |
|---|---|
| `off` | `off` |
| `fan_only` | `vent` |
| `cool` | `cool` |
| `heat` | `hot` |
| `auto` | `auto` |

| Home Assistant fan mode | Truma `aircon_vent_mode` |
|---|---|
| `low` | `low` |
| `mid` | `mid` |
| `high` | `high` |
| `night` | `night` |
| `auto` | `auto` |

Target temperature range is 16-32°C in 1°C steps (the Aventa itself typically
only accepts 20-30°C - out-of-range values are simply ignored by the CPplus).

## Example dashboard card

See [dashboard.yaml](dashboard.yaml) for a ready-to-use Lovelace view
containing a thermostat card for the Aventa aircon plus a glance card for
connection/status. Import it via **Settings → Dashboards → Edit
Dashboard → Raw configuration editor**, or copy individual cards into an
existing dashboard.

## Manual / advanced setups

If you prefer not to rely on discovery (e.g. you manage all entities via
`configuration.yaml`), you can subscribe to the plain MQTT topics documented
in the [root README](../README.md#mqtt-topics) instead and build your own
entities/automations around them.
