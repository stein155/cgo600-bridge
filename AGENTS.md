# AGENTS.md
## Scope
This repo contains the CGO600 BLE bridge as an ESPHome external component:
- ESPHome component in `esphome/components/cgo/`
- Web installer in `installer/`

## Rule Files
No additional agent/editor rule files currently exist in this repo:
- `.cursorrules`
- `.cursor/rules/`
- `.github/copilot-instructions.md`

## Important Paths
- `esphome/components/cgo/__init__.py`: config schema, codegen, embedding of the dashboard
- `esphome/components/cgo/cgo.h/.cpp`: BLE client (discovery, handshake, polling, entities)
- `esphome/components/cgo/cgo_protocol.h/.cpp`: frames, CRC-16/MODBUS, field decoding
- `esphome/components/cgo/ride_log.h/.cpp`: ride inference and NVS storage
- `esphome/components/cgo/web_ui.h/.cpp`: dashboard and JSON API on ESPHome's web server
- `esphome/components/cgo/web/`: dashboard sources (`js/`, `css/`, static files)
- `esphome/components/cgo/web_assets.py`: bundles and gzips `web/` at compile time
- `esphome/cgo600-install.yaml`: configuration flashed by the web installer
- `esphome/cgo600.yaml`: development configuration (uses `esphome/secrets.yaml`)
- `installer/`: web installer page and site assembly
- `.github/workflows/esphome.yml`: compile CI; `.github/workflows/installer.yml`: Pages + release assets

## Environment
Install ESPHome (`pip install esphome==2026.6.5`, the version pinned in CI).
Copy `esphome/secrets.yaml.example` to `esphome/secrets.yaml` before compiling `esphome/cgo600.yaml`.

## Commands
Preferred commands from repo root:

```bash
esphome compile esphome/cgo600.yaml
esphome upload esphome/cgo600.yaml --device <bridge-ip>
esphome logs esphome/cgo600.yaml --device <bridge-ip>
python3 installer/local_esphome_config.py && esphome compile esphome/.install-ci.yaml
python3 esphome/components/cgo/web_assets.py
```

`esphome upload` only uploads the last build; run `compile` first after changes.

CI compiles `esphome/cgo600.yaml` and the install configuration.

## Tests
There is no automated test setup. Validation is manual:
1. Compile both configurations.
2. Flash hardware if needed.
3. Verify the logs, BLE connection, Home Assistant entities, the dashboard and the ride history.

## Code Style
- Follow ESPHome conventions: `snake_case` methods, trailing-underscore protected members, `this->`,
  `ESP_LOG*` with the component tags (`cgo`, `cgo.rides`, `cgo.web`).
- Use 4-space indentation and same-line opening braces (see `.clang-format`).
- Prefer bounded buffer operations such as `snprintf`.
- Code running on the HTTP server task must not touch BLE or NVS directly; `defer()` it to the main loop.
- ESPHome's ESP-IDF web server only sends status codes 200, 404 and 409; anything else becomes 500.

## Naming
- Keep entity keys stable: `battery`, `speed`, `max_speed`, `avg_speed`, `trip_distance`, `trip_duration`,
  `remaining_range`, `range`, `serial_number`, `ble_status`, `light`.
- Keep the dashboard JSON fields and the `esphome.cgo_ride` event name stable.

## Files To Treat Carefully
- `esphome/.esphome/`: build output, ignored by Git
- `esphome/secrets.yaml`: local credentials, ignored by Git
- `esphome/components/cgo/ride_log.h`: changing the `Store` layout needs a new `STORE_MAGIC`,
  which discards stored rides

## Workflow Notes
- At minimum ensure both configurations compile.
- For protocol changes, keep field IDs and entity keys stable.
- Be explicit that the repo currently has no automated tests or lint gates.
- This setup may be used for live debugging; prefer adding observability that can be read remotely when safe.
- The user allows live firmware builds and uploads for diagnostics and fixes when needed.
- Live logs are available over WiFi with `esphome logs`; treat them as a normal debugging path.
