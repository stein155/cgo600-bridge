# CGO600 Smart Bridge

ESP32 firmware, built on [ESPHome](https://esphome.io), that reads live telemetry from a **Tenways CGO600** e-bike over Bluetooth Low Energy and brings it into **Home Assistant**. The bridge also serves its own multilingual web dashboard (installable as a PWA) for live monitoring, light control and a ride history. Once installed, no computer is needed.

> "TENWAYS" and "CGO600" are trademarks of TENWAYS. This is an unofficial community project, not affiliated with or endorsed by TENWAYS — use at your own risk.

## Supported bikes

| Bike | Status |
|---|---|
| CGO600 | **Supported** |
| CGO600 Pro, CGO600 PLUS, CGO600 Pro-C, CGO600 Pro_T | **Experimental**, untested |
| CGO800S, CGO800 Plus | **Experimental**: connects, data not confirmed yet |
| CGO009, CGO009L, CGO009_D | **Experimental**, untested |
| Longtail Duo, Longtail Duo_D | **Experimental**, untested |
| AGO AIR, AGO AIR-BELT_D, Wayfarer_B, CGO600_D | **Experimental**, untested |
| AGO, AGOX, AGOT, CARGO, CGO COMPACT_D, CGO001 | **Experimental**, untested |
| DM | Not supported |
| Bosch Smart System bikes | Not supported (own Bosch protocol) |

Only the CGO600 is supported. The other bikes are detected automatically and get the requests the Tenways app uses for them, including its encrypted mode, but reading data and light control have not been confirmed on any of them yet. Logs from such a bike are very welcome in [issue #2](https://github.com/stein155/cgo600-bridge/issues/2).

## Motivation

I own a Tenways CGO600 and run a smart home built around Home Assistant. I like to integrate as many devices as possible into Home Assistant, including devices that do not expose a ready-made integration.

For this project I reverse engineered the bike's Bluetooth protocol and built an ESP32 bridge for it. In my setup the ESP32 is mounted in the shed where the bike is parked. When the bike connects on departure or arrival, the bridge reads the latest data and reports it to Home Assistant.

## Screenshots

| Dashboard | Rides | Configuration |
|---|---|---|
| ![Dashboard](docs/dashboard.jpeg) | ![Rides](docs/rides.jpeg) | ![Config](docs/config.jpeg) |

## Features

- **Home Assistant entities** through the native ESPHome API, discovered automatically
- **Web dashboard** on the bridge: battery, range, light, lifetime totals and ride history
- **Ride history** inferred from check-ins, stored on the ESP32 and sent to Home Assistant as events
- **Light control** from Home Assistant and the dashboard
- **No pairing**: the bike is found by name or by its Bluetooth service, no MAC address needed

## Installation

### Install from the browser

Open the **[web installer](https://stein155.github.io/cgo600-bridge/)** in Chrome or Edge on a desktop computer, connect the ESP32 over USB and click *Install*.

1. After flashing, enter your WiFi details in the installer. If you skip this, connect to the *CGO600 Bridge* hotspot instead.
2. Click *Visit device* to open the dashboard and bookmark it. The address (IP and `.local` name) is also shown under *Settings → System*.
3. Add the bridge in Home Assistant under *Settings → Devices & services*; it is usually discovered automatically.
4. Switch the bike on near the bridge; it connects by itself.

Updates are installed over WiFi from the ESPHome dashboard. In the ESPHome add-on, *Adopt* the bridge to customise its configuration.

### Add to your own ESPHome configuration

The `cgo` component can be added to any ESP32 configuration that uses the `esp-idf` framework:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/stein155/cgo600-bridge
      path: esphome/components
    components: [cgo]

time:
  - platform: sntp   # wall-clock time for the ride history

esp32_ble_tracker:
  scan_parameters:
    active: true
    continuous: true
    interval: 320ms
    window: 160ms

cgo:
  device_names: [CGO600]          # part of the advertised name, any case (default: every Tenways model)
  poll_interval: 1s               # 500ms to 10s (default: 1s)
  max_rides: 50                   # rides kept on the device, 1 to 50 (default: 20)

sensor:
  - platform: cgo
    battery:
      name: Battery
    range:
      name: Total range

switch:
  - platform: cgo
    light:
      name: Light
```

[`esphome/cgo600-install.yaml`](esphome/cgo600-install.yaml) is the complete configuration the web installer flashes, and [`esphome/cgo600.yaml`](esphome/cgo600.yaml) is a development example. Copy `esphome/secrets.yaml.example` to `esphome/secrets.yaml` to compile it locally.

## Home Assistant

| Platform | Key | Unit |
|---|---|---|
| `sensor` | `battery` | % |
| `sensor` | `speed`, `max_speed`, `avg_speed` | km/h |
| `sensor` | `trip_distance`, `remaining_range`, `range` | km |
| `sensor` | `trip_duration` | s |
| `sensor` | `power` | W |
| `text_sensor` | `serial_number`, `firmware_version`, `ble_status` | — |
| `switch` | `light` | — |

All keys are optional. `range` is the bike's total odometer. `power` and `firmware_version` stay empty on bikes that do not report them; the bridge asks for the power until the bike answers once, then polls it with the other telemetry.

Every inferred ride fires the `on_ride` trigger with `distance_km`, `duration_s` and `battery_used`. The ready-made configuration forwards it as a Home Assistant event, which you can use in automations:

```yaml
cgo:
  on_ride:
    - homeassistant.event:
        event: esphome.cgo_ride
        data:
          distance_km: !lambda 'return str_sprintf("%.1f", distance_km);'
```

(`api: homeassistant_services: true` is required for `homeassistant.event`.)

## Web dashboard

The dashboard is embedded in the firmware and served at the bridge's address. The bridge settings come from the ESPHome configuration, so the dashboard shows them read-only. It keeps the display preferences, the ride history and a restart button. Because the dashboard serves `/`, it cannot be combined with ESPHome's own `web_server` component; set `web_ui: false` to use that instead.

The dashboard talks to a small JSON API:

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/state` | Live telemetry and connection status |
| GET | `/api/config` | Configuration, address and firmware version |
| GET | `/api/rides` | Stored rides |
| POST | `/api/light?on=0\|1` | Switch the light |
| POST | `/api/rides/clear` | Delete all rides |
| POST | `/api/rides/import?start=…&end=…&distance=…&duration=…` | Import one ride (epoch seconds, km, s) |
| POST | `/api/restart` | Restart the bridge |

## Ride history

The bridge only sees the bike when it connects, typically on departure or arrival, so it infers rides from the time and counter difference between two check-ins. Each ride records its window, distance, duration, battery delta and average/max speed. Rides are stored in the ESP32's flash and survive updates.

## Bluetooth notes

The component uses one BLE connection slot, which counts towards `esp32_ble: max_connections`. While the bike is connected, it pauses scanning and requests a relaxed connection interval with a 10 s supervision timeout, because scanning during the connection caused regular disconnects.

## Upgrading to the `cgo` component name

The component was renamed from `cgo600` to `cgo` now that it also supports the CGO800. This is a breaking change for existing installations:

1. Before updating, save your rides: open `http://<bridge>/api/rides` and save the JSON. The ride history is stored under a new key and starts empty after the update.
2. In your ESPHome configuration, replace `components: [cgo600]` with `components: [cgo]`, the `cgo600:` block with `cgo:`, and every `platform: cgo600` with `platform: cgo`. Configurations still using `cgo600` no longer compile.
3. Update Home Assistant automations that listen for `esphome.cgo600_ride` to `esphome.cgo_ride`, and the `event:` in your `on_ride` action.
4. After updating, import the saved rides under *Settings → Ride history → Import rides*. The dashboard's language and theme are reset once.

## Migrating from the standalone firmware

Earlier versions were standalone PlatformIO firmware with MQTT; the last release of it is still available on the releases page. To move over:

1. Save your rides from the old dashboard: open `http://<bridge>/api/rides` and save the JSON.
2. Install with the web installer over USB. Alternatively, flash `cgo600-bridge-ota.bin` from the release over WiFi with the old firmware's OTA (`espota.py -i <bridge-ip> -f cgo600-bridge-ota.bin`).
3. In the new dashboard, go to *Settings → Ride history → Import rides* and select the saved file.
4. Remove the old MQTT entities from Home Assistant.

## BLE protocol

The CGO600 exposes a Nordic UART Service (NUS) over BLE. Commands are written to the RX characteristic; telemetry replies arrive as notifications on the TX characteristic. The protocol details below come from reverse engineering and may differ across bike/display firmware versions.

### Frame format

```
Read  0x52 [len] [field_ids...] [CRC16_lo] [CRC16_hi] 0x45
Write 0x57 [len] [payload...]   [CRC16_lo] [CRC16_hi] 0x45
```

The checksum is CRC-16/MODBUS (polynomial `0xA001` reflected, initial value `0xFFFF`), computed over all bytes from the start byte up to the CRC. Requests carry it low byte first; frames from the bike carry it high byte first.

### Field IDs

| ID | Field | Decoding |
|---|---|---|
| `0x07` | Serial number | ASCII string |
| `0x11` | Speed | uint16 BE ÷ 10 → km/h |
| `0x12` | Max speed | uint16 BE ÷ 10 → km/h |
| `0x13` | Average speed | uint16 BE ÷ 10 → km/h |
| `0x14` | Trip distance | uint16 BE ÷ 10 → km |
| `0x15` | Trip duration | uint16 BE → seconds |
| `0x16` | Remaining range | uint16 BE ÷ 10 → km (`0xFFFF` = unknown) |
| `0x17` | Total range (odometer) | uint16 BE ÷ 10 → km |
| `0x1A` | Battery | uint8 → % |
| `0x21` | Light | uint8 (0 = off, 1 = on) |

### King Meter (CGO800 and others)

Newer displays expose a King Meter service (`43480001-f001-4b49-4e47-204d45544552`) instead of NUS. Requests are written to `43480002-…`; replies arrive as notifications on `43480003-…`. The bridge has a profile for each driver in the Tenways app and follows how the app talks to each model; the Longtail Duo layout was first captured by [tenways-ble](https://github.com/Styx85/tenways-ble).

```
[type] 0x00 [len] [count] [payload...] [CRC16] [CRC16] 0x45
```

`type` is `0x62` (read) or `0x63` (write), `len` counts `count` and the payload, and `count` is the number of keys. A read request lists keys; the reply carries `[key] [size] [value]` entries with big-endian values. A write carries one `[key] [size] [value]` entry. The checksum is the same CRC-16/MODBUS. The CGO800 sends it low byte first, every other King Meter model high byte first, and a display ignores requests in the wrong order.

| Profile | Models (advertised name starts with) | Info keys | Status keys |
|---|---|---|---|
| CGO800 | `CGO800` | `04 05` | `05 06 07 08 09 0A 0B 0C 0F 11 23` |
| CGO009 | `CGO009` | `04 05 70 4F 50` | `06 07 08 09 11 12 0A 0B 0C 0F 23 4F 50` |
| Longtail Duo | `Longtail Duo` | `04 05 4F 70` | `06 07 08 09 11 0A 0B 0C 0F 23 4F` |
| AGO AIR | `AGO AIR`, `Wayfarer_B`, `CGO600_D` | `04 05 4F 76` | as Longtail Duo |
| AGO | anything else, and bikes without a name | `04 05` | `05 06 07 08 09 11 0A 0B 0C 0F 23` |

AGO AIR models and every model whose name ends in `_D` are written with response, like the app does. When a bike does not answer, the bridge first tries the encryption handshake and then the other CRC order.

| Key | Field | Decoding |
|---|---|---|
| `0x04` | Firmware version | dotted bytes (`01 02 03` = 1.2.3); CGO800: ASCII |
| `0x05` | Serial number | ASCII string |
| `0x06` | Speed | uint16 ÷ 10 → km/h |
| `0x07` | Max speed | uint16 ÷ 10 → km/h |
| `0x08` | Average speed | uint16 ÷ 10 → km/h |
| `0x09` | Trip distance | uint32 ÷ 10 → km |
| `0x0A` | Ride time | uint32 → seconds |
| `0x0B` | Remaining range | uint16 → km; 655 or 32767 = none (CGO800: any byte ≥ `0x80` = none) |
| `0x0C` | Total range (odometer) | uint32 ÷ 10 → km (CGO800: whole km) |
| `0x0F` | Battery | uint8 → % |
| `0x11` | Light | uint8 (0 = off, 1 = on); written to switch the light |
| `0x23` | Power | uint16 → W (above 3000 ignored) |

#### Encryption

Some displays (seen on the CGO800 Plus) answer plain requests only with an `AT+NAME=…` error and expect an encrypted session first. The bridge tries this handshake first on CGO800 bikes, and on other bikes when they do not answer or report such an error; it continues unencrypted when the display does not reply to it:

1. The bridge writes 16 random bytes under key `0xD8`; the display replies with its own 16 random bytes under the same key.
2. Both sides derive a 32-byte AES key with HKDF-SHA256: the bridge's random is the salt, a fixed secret from the Tenways app the input key, and the display's random the info.
3. The bridge reads key `0xD2`, the 16-byte IV.
4. From then on every frame is sent and received as an AES-256-CBC packet (PKCS#7 padding, the same IV for every packet):

```
0xA5 [0x80 | len_hi] [len_lo] [seq_hi] [seq_lo] [ciphertext] [CRC16_hi] [CRC16_lo] 0x5A
```

`len` is the ciphertext length, `seq` counts packets from 1 and the CRC is CRC-16/MODBUS over `0xA5` up to the ciphertext. Writes to an encrypted display use write with response.

A notification that starts with `AT+` is an error report from the display. Like the Tenways app, the bridge reconnects after the second one.

The first 20 requests and notifications of every King Meter connection are logged at DEBUG, and a summary of the traffic (requests, notifications, frames, CRC errors, skipped bytes, errors) every 30 s. Add `logger: logs: cgo: VERBOSE` to log every raw notification.

## Repository layout

```
esphome/
  cgo600-install.yaml       Ready-made configuration flashed by the web installer
  cgo600.yaml               Development example configuration
  components/cgo/
    __init__.py             Configuration schema and code generation
    cgo.h/.cpp              BLE client: discovery, handshake, polling, entities
    cgo_protocol.h/.cpp     Frames, CRC and field decoding
    ride_log.h/.cpp         Ride inference and flash storage
    web_ui.h/.cpp           Dashboard and JSON API on the ESPHome web server
    web_assets.py           Bundles and gzips web/ into the firmware
    web/                    Dashboard sources (no framework)
      js/                   App modules, bundled into app.js in a fixed order
      css/                  Styles, bundled into style.css in filename order
    sensor.py, text_sensor.py, switch.py   Entity platforms
installer/
  index.html                Web installer page (esp-web-tools), published to GitHub Pages
  build_site.py             Assembles the page, manifest and firmware image
  local_esphome_config.py   Points the install config at the local component for CI builds
```

### Dashboard development

Edit the sources in `esphome/components/cgo/web/`. They are bundled and embedded on every `esphome compile`. A new JS file must be added to `JS_ORDER` in `web_assets.py`; CSS files are bundled in filename order. Run `python3 esphome/components/cgo/web_assets.py` to see the bundle sizes.
