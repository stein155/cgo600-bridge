# v1.0.0

First release of the CGO600 ESP32 BLE bridge: firmware for a Tenways CGO600 e-bike with MQTT support, Home Assistant auto-discovery, OTA updates, and a built-in web dashboard.

## Features

### Web dashboard
- Live dashboard served directly from the ESP32
- BLE and MQTT status indicators
- On-device configuration for MQTT and BLE settings
- OTA-friendly workflow with separate LittleFS web assets

### Telemetry
- Battery percentage
- Current speed (km/h)
- Max speed (km/h)
- Average speed (km/h)
- Trip distance (km)
- Trip duration (seconds)
- Remaining range (km)
- Total range (km)
- Light on/off
- Serial number
- Last connected timestamp (UTC ISO 8601)

### MQTT / Home Assistant
- Publish telemetry to any MQTT broker
- Home Assistant auto-discovery so entities appear automatically
- Availability tracking with Last Will and Testament
- Stable per-sensor state topics for clean HA integration

### Connectivity
- BLE scanning with name matching
- Device address caching for faster reconnection
- Auto-retry with backoff
- WiFi setup portal for first-time provisioning
- OTA firmware upload over the local network

## Build

```bash
pio run -d firmware
pio run -d firmware -t buildfs
```

## Tested on
- ESP32 DevKit style boards
- Home Assistant via MQTT autodiscovery
- macOS for local flashing and OTA
