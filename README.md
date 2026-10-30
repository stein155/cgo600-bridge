# E-bike Bridge

ESPHome firmware that turns an ESP32 into a Bluetooth bridge between e-bike displays and
[Home Assistant](https://www.home-assistant.io). It connects to one or more bikes on its own, with no
pairing or MAC addresses, and publishes battery, speed, trips, odometer and the light as Home Assistant
entities. Rides and distance per day, week or month are derived in Home Assistant from the odometer.

> Unofficial community project, not affiliated with or endorsed by any bike or display maker. Use at your
> own risk.

## Supported displays

| Display | Status |
|---|---|
| Tenways CGO600 | **Tested** |
| B06 display (BIKEGO plus app), e.g. with a Bafang BBS01 mid-drive | **Tested** |
| Tenways CGO600 Pro, PLUS, Pro-C, Pro_T | Experimental |
| Tenways CGO800S, CGO800 Plus | Experimental: connects, data unconfirmed |
| Tenways CGO009, Longtail Duo, AGO AIR, Wayfarer_B, CGO600_D, AGO, AGOX, AGOT, CARGO, CGO COMPACT_D, CGO001 | Experimental |
| Tenways DM, Bosch Smart System | Not supported |

Experimental displays get the requests their official app uses but have not been confirmed on a real
bike. Logs are welcome in [issue #2](https://github.com/stein155/ebike-bridge/issues/2). The
[protocol reference](docs/protocols.md) documents all three transports.

## Install

Flash an ESP32 from the browser with the [web installer](https://stein155.github.io/ebike-bridge/), then add
the bridge in Home Assistant under *Settings → Devices & services*. The installed firmware handles one bike
and finds it by name.

To build it yourself, use the component from this repository:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/stein155/ebike-bridge
      path: esphome/components
    components: [ebike]

ebike:
```

See [`esphome/ebike-bridge.yaml`](esphome/ebike-bridge.yaml) for the complete configuration with all
entities.

## Multiple bikes

Every bike gets its own `ebike` block, and takes one of the ESP32's three BLE connections. Include
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
      device_name: B06
```

A block connects only to bikes no other block holds, and the bridge keeps scanning until every bike is
connected.

## Connection time

By default a bike stays connected for as long as it is in range. Some displays, such as the B06, stay on while
connected. `max_connection_time` disconnects after reading the bike for that long, and connects again only once
the bike has not advertised for a minute, that is after it was switched off or ridden away:

```yaml
ebike:
  max_connection_time: 30s   # default: unlimited
```

With the `bike.yaml` package, extend the bike's block:

```yaml
ebike:
  - id: !extend bike_2
    max_connection_time: 30s
```

`ble_status` reads `paused` while the bridge waits for the bike to leave.

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

**Rides.** The [ride blueprint](homeassistant/blueprints/automation/ebike_rides.yaml) logs every ride of a bike
as an event in a calendar, such as a [local calendar](https://www.home-assistant.io/integrations/local_calendar/),
which keeps it permanently. It works from the odometer, which never resets: a ride is counted even when the
bridge missed the arrival, and rides between two sightings of the bike count as one. Each ride has its start and
end, distance, duration, average speed and battery use; it also fires the `ebike_ride` event and can update a text
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

- The component is now `ebike` (was `cgo`); `cgo_id` is now `ebike_id`.
- `range` is now `odometer`. `web_ui`, `max_rides`, `on_ride` and the `esphome.cgo_ride` event are gone,
  together with the dashboard, the JSON API and the ride history on the bridge. Rides are derived in Home
  Assistant instead.
- The hostname changes from `cgo600-bridge-xxxxxx` to `ebike-bridge-xxxxxx`. Home Assistant keeps the device
  and its entity IDs; the bikes of the `bike.yaml` package appear as separate devices.
- WiFi credentials set through the installer or the hotspot are kept. A bridge whose credentials were
  compiled into its old firmware falls back to the *E-bike Bridge* hotspot after the update.

## Development

```bash
tests/run.sh                          # host tests of the protocol code (needs mbedtls)
esphome compile esphome/dev.yaml      # two bikes; copy esphome/secrets.yaml.example first
esphome logs esphome/dev.yaml --device <bridge-ip>
```

CI compiles `esphome/dev.yaml` and the installer configuration. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE)
