# CGO600 Smart Bridge

ESP32 firmware, built on [ESPHome](https://esphome.io), that reads live telemetry from a **Tenways CGO600** e-bike over Bluetooth Low Energy and brings it into **Home Assistant**. The bridge also serves its own multilingual web dashboard (installable as a PWA) for live monitoring, light control and a ride history. Once installed, no computer is needed.

> "TENWAYS" and "CGO600" are trademarks of TENWAYS. This is an unofficial community project, not affiliated with or endorsed by TENWAYS — use at your own risk.

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
- **Bluetooth proxy** for other BLE devices near the bike
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

The `cgo600` component can be added to any ESP32 configuration that uses the `esp-idf` framework:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/stein155/cgo600-bridge
      path: esphome/components
    components: [cgo600]

time:
  - platform: sntp   # wall-clock time for the ride history

esp32_ble_tracker:
  scan_parameters:
    active: true
    continuous: true
    interval: 320ms
    window: 160ms

cgo600:
  device_name: CGO600   # substring of the advertised name (default: CGO600)
  poll_interval: 1s     # 500ms to 10s (default: 1s)
  max_rides: 50         # rides kept on the device, 1 to 50 (default: 20)

sensor:
  - platform: cgo600
    battery:
      name: Battery
    range:
      name: Total range

switch:
  - platform: cgo600
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
| `text_sensor` | `serial_number`, `ble_status` | — |
| `switch` | `light` | — |

All keys are optional. `range` is the bike's total odometer.

Every inferred ride fires the `on_ride` trigger with `distance_km`, `duration_s` and `battery_used`. The ready-made configuration forwards it as a Home Assistant event, which you can use in automations:

```yaml
cgo600:
  on_ride:
    - homeassistant.event:
        event: esphome.cgo600_ride
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

The component uses one BLE connection slot. Alongside a `bluetooth_proxy`, keep the total within `esp32_ble: max_connections` (default 3), for example `connection_slots: 2` for the proxy.

While the bike is connected, the component pauses scanning and requests a relaxed connection interval with a 10 s supervision timeout. Scanning during the connection caused regular disconnects. As a result, a proxy receives no advertisements while the bike is connected. Set `pause_scan_while_connected: false` if the proxy matters more than a stable bike connection.

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

## Repository layout

```
esphome/
  cgo600-install.yaml       Ready-made configuration flashed by the web installer
  cgo600.yaml               Development example configuration
  components/cgo600/
    __init__.py             Configuration schema and code generation
    cgo600.h/.cpp           BLE client: discovery, handshake, polling, entities
    cgo600_protocol.h/.cpp  Frames, CRC and field decoding
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

Edit the sources in `esphome/components/cgo600/web/`. They are bundled and embedded on every `esphome compile`. A new JS file must be added to `JS_ORDER` in `web_assets.py`; CSS files are bundled in filename order. Run `python3 esphome/components/cgo600/web_assets.py` to see the bundle sizes.
