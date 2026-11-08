# AGENTS.md
## Scope
This repo contains the Tenways BLE bridge as an ESPHome external component:
- ESPHome component in `esphome/components/tenways/`
- Home Assistant ride blueprint in `homeassistant/blueprints/`
- Web installer in `installer/`

Only Tenways displays belong here. Other displays live in their own repositories and
run next to this component through `scan_coordinator.h`.

## Rule Files
No additional agent/editor rule files currently exist in this repo:
- `.cursorrules`
- `.cursor/rules/`
- `.github/copilot-instructions.md`

## Important Paths
- `esphome/components/tenways/__init__.py`: config schema and codegen
- `esphome/components/tenways/tenways.h/.cpp`: BLE client (discovery, handshake, polling, entities)
- `esphome/components/tenways/protocol.h/.cpp`: Nordic UART and King Meter frames, CRC-16/MODBUS, field decoding
- `esphome/components/tenways/crypto.h/.cpp`: the encrypted session of the CGO800 Plus and CGO600 variants
- `esphome/components/tenways/scan_coordinator.h`: scan pausing shared with other bike components; keep it
  identical to the copies in those repositories
- `esphome/tenways-bridge.yaml`: configuration flashed by the web installer
- `esphome/bike.yaml`: package for one bike as its own Home Assistant device
- `esphome/dev.yaml`: development configuration with two bikes (uses `esphome/secrets.yaml`)
- `installer/`: web installer page and site assembly
- `.github/workflows/esphome.yml`: tests and compile CI; `.github/workflows/installer.yml`: Pages + release assets

## Environment
Install ESPHome (`pip install esphome==2026.10.0`, the version pinned in CI; it needs Python 3.12 or newer).
Copy `esphome/secrets.yaml.example` to `esphome/secrets.yaml` before compiling `esphome/dev.yaml`.

## Commands
Preferred commands from repo root:

```bash
tests/run.sh
./lint.sh                 # clang-format, ruff, yamllint, shellcheck; --fix applies the fixes
esphome compile esphome/dev.yaml
esphome upload esphome/dev.yaml --device <bridge-ip>
esphome logs esphome/dev.yaml --device <bridge-ip>
python3 installer/local_esphome_config.py && esphome compile esphome/.install-ci.yaml
```

`esphome upload` only uploads the last build; run `compile` first after changes.

## Tests
`tests/run.sh` runs the host tests of the protocol code (frames, parsing, encryption); it needs mbedtls
(`libmbedtls-dev`, or `brew install mbedtls@3`). Everything else is validated manually:
1. Run `tests/run.sh` and compile both configurations.
2. Flash hardware if needed.
3. Verify the logs, BLE connection and the Home Assistant entities.

## Code Style
- Follow ESPHome conventions: `snake_case` methods, trailing-underscore protected members, `this->`,
  `ESP_LOG*` with the component tag `tenways`.
- Use 4-space indentation and same-line opening braces (see `.clang-format`).
- Prefer bounded buffer operations such as `snprintf`.
- `BLEClientBase` disables `loop()` while the client is idle; work that must run while no bike is connected
  belongs on the scheduler (`set_timeout`, `set_interval`).

## Naming
- Keep entity keys stable: `battery`, `speed`, `max_speed`, `avg_speed`, `trip_distance`, `trip_duration`,
  `remaining_range`, `odometer`, `power`, `serial_number`, `firmware_version`, `ble_status`, `light`.
- Keep the `tenways_ride` event name of the blueprint stable.

## Files To Treat Carefully
- `esphome/.esphome/`: build output, ignored by Git
- `esphome/secrets.yaml`: local credentials, ignored by Git
- `protocol::BikeState`: changing its layout needs a new `LAST_STATE_MAGIC` in `tenways.cpp`

## Workflow Notes
- At minimum ensure both configurations compile.
- For protocol changes, keep field IDs and entity keys stable.
- Be explicit that only the protocol code has automated tests. `./lint.sh` is the lint gate CI runs; install the
  pinned linters with `pip install -r requirements-lint.txt`.
- This setup may be used for live debugging; prefer adding observability that can be read remotely when safe.
- The user allows live firmware builds and uploads for diagnostics and fixes when needed.
- Live logs are available over WiFi with `esphome logs`; treat them as a normal debugging path.
