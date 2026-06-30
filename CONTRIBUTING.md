# Contributing

Thanks for your interest in improving the CGO600 Bridge. This is a small,
independent hobby project — contributions are welcome.

## Project layout

- `firmware/src/` — ESP32 firmware (C++/Arduino)
- `firmware/data/` — embedded web UI (served from LittleFS)
- `firmware/platformio.ini` — build configuration
- `AGENTS.md` — detailed conventions for the firmware and important paths

## Building & flashing

Install [PlatformIO](https://platformio.org/) (`pip install platformio`), then
from the repo root:

```bash
pio run -d firmware                 # build firmware
pio run -d firmware -t buildfs      # build the LittleFS web-UI image
pio run -d firmware -t upload       # flash firmware over USB
pio run -d firmware -t uploadfs     # flash web UI (needed after data/ changes)
pio device monitor -b 115200        # serial logs
```

Over-the-air flashing works after the first USB flash:
`pio run -d firmware -t upload --upload-port cgo600.local`.

## Code style

- C++ follows the conventions in `AGENTS.md` and the repo `.clang-format`
  (4-space indent, same-line braces, pointers bound to the type). Existing files
  are not bulk-reformatted; please keep diffs focused and match the surrounding code.
- Keep telemetry field names and MQTT topic suffixes stable — they are part of the
  Home Assistant contract (see `AGENTS.md` → Naming).
- Keep serial log prefixes consistent: `[BLE]`, `[MQTT]`, `[WEB]`, `[BOOT]`, `[OTA]`, `[CFG]`.

## Testing

There is no automated firmware test suite. Validation is manual:

1. `pio run -d firmware` must succeed.
2. `pio run -d firmware -t buildfs` if web assets changed.
3. Flash to hardware and verify serial logs, BLE connection, MQTT, and the web UI.

CI builds the firmware and the filesystem image, and runs a (non-blocking)
`clang-format` check.

## Pull requests

- Keep changes scoped and explain the *why* in the description.
- Note any manual verification you performed (build output, on-device behavior).
- For protocol/MQTT changes, confirm field IDs, MQTT suffixes, and discovery payloads stay stable.

## Reporting issues

Please use the bug report template and include firmware version, board, and serial
logs. See `SUPPORT.md` for troubleshooting first.
