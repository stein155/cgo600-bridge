# Support

This is a community firmware project with best-effort support.

Before opening an issue:

- Install the latest release from the [web installer](https://stein155.github.io/cgo600-bridge/) or update over WiFi.
- Restart the ESP32 and the bike display.
- Check the logs (`esphome logs`, the ESPHome dashboard, or USB serial at `115200` baud).
- Confirm your bike advertises a BLE name containing `CGO600`.
- If the `.local` address does not open, use the bridge's IP address from your router or Home Assistant.

Useful information for bug reports:

- Firmware release or commit SHA, and the ESPHome version
- ESP32 board type
- How it was installed (web installer, ESPHome dashboard, own configuration)
- Home Assistant version
- Screenshot of the dashboard
- Logs from boot through the failure
- Whether the issue started after a configuration or firmware change
