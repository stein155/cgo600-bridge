# Tenways Bridge

ESPHome firmware that turns an ESP32 into a Bluetooth bridge between Tenways e-bike displays and
[Home Assistant](https://www.home-assistant.io). It connects to one or more bikes on its own, with no
pairing or MAC addresses, and publishes battery, speed, trips, odometer and the light as Home Assistant
entities. Rides and distance per day, week or month are derived in Home Assistant from the odometer.

> Unofficial community project, not affiliated with or endorsed by Tenways. Use at your own risk.

## Supported displays

| Display | Status |
|---|---|
| Tenways CGO600 | **Tested** |
| Tenways CGO600 Pro, PLUS, Pro-C, Pro_T | Experimental |
| Tenways CGO800S, CGO800 Plus | Experimental: connects, data unconfirmed |
| Tenways CGO009, Longtail Duo, AGO AIR, Wayfarer_B, CGO600_D, AGO, AGOX, AGOT, CARGO, CGO COMPACT_D, CGO001 | Experimental |

Experimental displays get the requests the Tenways app uses but have not been confirmed on a real bike. Logs
are welcome in [issue #2](https://github.com/stein155/tenways-bridge/issues/2). The
[protocol reference](docs/protocols.md) documents both transports.

## Install

Flash an ESP32 from the browser with the [web installer](https://stein155.github.io/tenways-bridge/), then add
the bridge in Home Assistant under *Settings → Devices & services*. The installed firmware handles one bike
and finds it by name.

To build it yourself, use the component from this repository:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/stein155/tenways-bridge
      path: esphome/components
    components: [tenways]

tenways:
```

See [`esphome/tenways-bridge.yaml`](esphome/tenways-bridge.yaml) for the complete configuration with all
entities.

## Multiple bikes

Every bike gets its own `tenways` block, and takes one of the ESP32's three BLE connections. Include
[`esphome/bike.yaml`](esphome/bike.yaml) once per bike to add it as a separate device in Home Assistant:

```yaml
packages:
  bike_1: !include
    file: bike.yaml
    vars:
      id: bike_1           # unique; also keys the bike's state in flash
      name: Bike 1         # device name in Home Assistant
      device_name: CGO600  # part of the bike's Bluetooth name
  bike_2: !include
    file: bike.yaml
    vars:
      id: bike_2
      name: Bike 2
      device_name: CGO800
```

A block connects only to bikes no other block holds. Scanning keeps running as long as any bike is still
searching, so a bike that is in range for only a few seconds is still found; it pauses once every bike is
connected. Other BLE bike components that ship the same `scan_coordinator.h` take part in this too, so they can
run on the same ESP32.

## Home Assistant

| Platform | Key | Unit |
|---|---|---|
| `sensor` | `battery` | % |
| `sensor` | `speed`, `max_speed`, `avg_speed` | km/h |
| `sensor` | `trip_distance`, `remaining_range` | km |
| `sensor` | `odometer` | km (total increasing) |
| `sensor` | `trip_duration` | s |
| `sensor` | `power` | W |
| `text_sensor` | `serial_number`, `firmware_version`, `ble_status` | |
| `switch` | `light` | |

**Rides.** The [ride blueprint](homeassistant/blueprints/automation/tenways_rides.yaml) logs every ride of a bike
as an event in a calendar, such as a [local calendar](https://www.home-assistant.io/integrations/local_calendar/),
which keeps it permanently. It works from the odometer, which never resets: a ride is counted even when the
bridge missed the arrival, and rides between two sightings of the bike count as one. Each ride has its start and
end, distance, duration, average speed and battery use; it also fires the `tenways_ride` event and can update a text
helper with the last ride. Per bike, create a text helper (maximum length 255) for the blueprint's state, create
an automation from the blueprint, and run it once. A calendar card in list view shows the rides.

**Distance per period.** The odometer is a `total_increasing` sensor, so Home Assistant keeps long-term
statistics for it. A statistics graph card shows the distance per day, week or month:

```yaml
type: statistics-graph
entities: [sensor.bike_1_odometer]
stat_types: [change]
period: week
chart_type: bar
```

## Upgrading from cgo600-bridge

Version 2 is a breaking release:

- The component is now `tenways` (was `cgo`); `cgo_id` is now `tenways_id`.
- `range` is now `odometer`. `web_ui`, `max_rides`, `on_ride` and the `esphome.cgo_ride` event are gone,
  together with the dashboard, the JSON API and the ride history on the bridge. Rides are derived in Home
  Assistant instead.
- The hostname changes from `cgo600-bridge-xxxxxx` to `tenways-bridge-xxxxxx`. Home Assistant keeps the device
  and its entity IDs; the bikes of the `bike.yaml` package appear as separate devices.
- WiFi credentials set through the installer or the hotspot are kept. A bridge whose credentials were
  compiled into its old firmware falls back to the *Tenways Bridge* hotspot after the update.

## Development

```bash
tests/run.sh                          # host tests of the protocol code (needs mbedtls)
esphome compile esphome/dev.yaml      # two bikes; copy esphome/secrets.yaml.example first
esphome logs esphome/dev.yaml --device <bridge-ip>
```

CI compiles `esphome/dev.yaml` and the installer configuration. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE)
