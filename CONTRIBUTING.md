# Contributing

Thanks for your interest in improving the CGO600 Bridge. This is a small,
independent hobby project — contributions are welcome.

## Project layout

- `esphome/components/cgo/` — the ESPHome external component (C++ and Python)
- `esphome/components/cgo/web/` — dashboard sources, bundled and embedded
  into the firmware on every compile
- `esphome/cgo600-install.yaml` — the configuration the web installer flashes
- `installer/` — the web installer page and the scripts that publish it
- `AGENTS.md` — detailed conventions and important paths

## Building & flashing

Install [ESPHome](https://esphome.io/guides/installing_esphome) (the CI uses
the version pinned in `.github/workflows/esphome.yml`), then:

```bash
cp esphome/secrets.yaml.example esphome/secrets.yaml   # fill in WiFi and keys
esphome compile esphome/cgo600.yaml
esphome run esphome/cgo600.yaml                         # flash over USB or WiFi
esphome logs esphome/cgo600.yaml                        # live logs over WiFi
```

To build the configuration the web installer ships, with the local component:

```bash
python3 installer/local_esphome_config.py
esphome compile esphome/.install-ci.yaml
```

## Code style

- C++ follows ESPHome conventions (`snake_case` methods, trailing-underscore
  protected members, `this->`) with 4-space indentation; see `.clang-format`.
- Keep entity keys stable: `battery`, `speed`, `max_speed`, `avg_speed`,
  `trip_distance`, `trip_duration`, `remaining_range`, `range`,
  `serial_number`, `ble_status`, `light`. They are part of the Home Assistant contract.
- Log with ESPHome's `ESP_LOG*` macros and the component's tags (`cgo`,
  `cgo.rides`, `cgo.web`).

## Testing

There is no automated test suite. Validation is manual:

1. `esphome compile` must succeed for `esphome/cgo600.yaml` and the install configuration.
2. Flash to hardware and verify the logs, the BLE connection, the Home Assistant
   entities and the dashboard.

CI compiles both configurations.

## Pull requests

- Keep changes scoped and explain the *why* in the description.
- Note any manual verification you performed (build output, on-device behavior).
- For protocol changes, confirm field IDs and entity keys stay stable.

## Reporting issues

Please use the bug report template and include the firmware version, board and
logs. See `SUPPORT.md` for troubleshooting first.
