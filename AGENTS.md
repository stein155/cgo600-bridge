# AGENTS.md
## Scope
This repo contains a single CGO600 BLE bridge implementation:
- ESP32 firmware in `firmware/`

## Rule Files
No additional agent/editor rule files currently exist in this repo:
- `.cursorrules`
- `.cursor/rules/`
- `.github/copilot-instructions.md`

## Important Paths
- `firmware/src/main.cpp`: WiFi, OTA, and main orchestration
- `firmware/src/ble_client.h`: BLE scan/connect/session logic
- `firmware/src/bike_state.h`: telemetry model
- `firmware/src/protocol.h`: frame parsing and CRC helpers
- `firmware/src/mqtt_ha.cpp`: MQTT + Home Assistant discovery
- `firmware/src/web_server.cpp`: web API and WebSocket updates
- `firmware/data/`: static web UI files for LittleFS
- `firmware/platformio.ini`: PlatformIO config
- `.github/workflows/firmware.yml`: firmware CI

## Firmware Environment
Install PlatformIO if needed.

PlatformIO environment: `esp32`
Serial monitor speed: `115200`

## Firmware Commands
Preferred commands from repo root:

```bash
pio run -d firmware
pio run -d firmware -t buildfs
pio run -d firmware -t upload
pio run -d firmware -t uploadfs
pio run -d firmware -t upload --upload-port /dev/cu.usbserial-XXXXX
pio run -d firmware -t upload --upload-port cgo600.local
pio device monitor -b 115200
```

CI currently runs:

```bash
pio run -d firmware
pio run -d firmware -t buildfs
```

## Firmware Tests
There is no automated firmware test setup yet.
- No `pio test` workflow is configured
- No embedded unit-test framework is checked in
- No single-test command exists today because there are no firmware tests

Current firmware validation is manual:
1. Build with `pio run -d firmware`.
2. Build the filesystem image with `pio run -d firmware -t buildfs` if web assets changed.
3. Flash hardware if needed.
4. Verify serial logs, BLE connection, MQTT behavior, and web UI.

## Code Style: Firmware C++
- Follow the existing Arduino/C++ style in `firmware/src/`.
- Use 4-space indentation and same-line opening braces.
- Include platform/library headers before local headers.
- Use `PascalCase` for classes/structs.
- Preserve current naming patterns for locals and members, including leading-underscore members.
- Keep protocol constants and macros in their existing uppercase style.
- Prefer bounded buffer operations such as `snprintf` and careful `strncpy` usage.
- Keep serial log prefixes consistent, such as `[BLE]`, `[MQTT]`, `[WEB]`, `[BOOT]`, and `[OTA]`.

## Firmware Error Handling
- Prefer retries, reconnect loops, and explicit status changes.
- Log operational failures to serial output with enough context to diagnose device issues.
- Treat WiFi, BLE, and MQTT behavior as long-running and resilient.

## Naming
- Keep telemetry names stable: `battery`, `speed_kmh`, `avg_speed_kmh`, `total_range_km`, `serial_number`, `light_on`.
- Keep MQTT suffixes stable: `battery`, `speed`, `avg_speed`, `range`, `light`, `serial_number`, `last_connected`.

## Files To Treat Carefully
- `firmware/.pio/`: build output, ignored by Git
- `firmware/data/`: changes usually require `uploadfs` for on-device verification
- `firmware/platformio.ini`: changes affect CI and local firmware builds

## Workflow Notes
- For firmware-only work, at minimum ensure `pio run -d firmware` succeeds.
- For protocol or MQTT changes, keep field IDs, MQTT suffixes, and discovery payloads stable.
- Be explicit that the repo currently has no automated tests or lint gates.
- This setup may be used for live debugging; prefer adding observability that can be read remotely when safe.
- The user allows live firmware builds and uploads for diagnostics and fixes when needed.
- If remote log streaming is available, treat live log inspection as a normal debugging path.
