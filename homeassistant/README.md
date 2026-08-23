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
| `sensor.<root>_current_temp_room` | sensor | Current room temperature |
| `climate.<root>_aventa` | **climate** | Full Aventa Comfort (2. Gen) aircon control |
| `button.<root>_reboot` | button | Restart the ESP32 |

`<root>` is the MQTT topic prefix configured in the web UI (default `truma`).

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
