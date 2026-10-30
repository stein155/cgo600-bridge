# Contributing

Contributions are welcome. This is a small, independent hobby project.

## Layout

- `esphome/components/ebike/`: the ESPHome component (C++ and Python)
- `esphome/ebike-bridge.yaml`: the configuration the web installer flashes
- `esphome/bike.yaml`, `esphome/dev.yaml`: per-bike package and development configuration
- `homeassistant/blueprints/`: Home Assistant blueprints
- `installer/`: the web installer page and the scripts that publish it
- `docs/protocols.md`: protocol reference

## Build and test

Install [ESPHome](https://esphome.io/guides/installing_esphome) at the version pinned in
`.github/workflows/esphome.yml`, then:

```bash
tests/run.sh                                            # host tests, needs mbedtls
cp esphome/secrets.yaml.example esphome/secrets.yaml    # fill in WiFi and keys
esphome run esphome/dev.yaml                            # compile and flash over USB or WiFi
esphome logs esphome/dev.yaml                           # live logs over WiFi
python3 installer/local_esphome_config.py && esphome compile esphome/.install-ci.yaml
```

Only the protocol code has automated tests. Verify everything else on hardware: the logs, the BLE
connection and the Home Assistant entities. CI runs the host tests and compiles both configurations.

## Pull requests

- Keep changes scoped and explain the *why*.
- Note the manual verification you did and on which display.
- Keep field IDs and entity keys (`battery`, `speed`, `max_speed`, `avg_speed`, `trip_distance`,
  `trip_duration`, `remaining_range`, `odometer`, `power`, `serial_number`, `firmware_version`,
  `ble_status`, `light`) stable; they are the Home Assistant contract.

## Issues

Use the bug report template and include the firmware version, board, display and logs. See `SUPPORT.md`
first.
