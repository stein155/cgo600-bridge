# Support

This is a community firmware project with best-effort support.

Before opening an issue:

- Flash the latest release.
- Restart the ESP32 and the bike display.
- Verify the ESP32 is on the same network as your MQTT broker.
- Check serial logs at `115200` baud if possible.
- Confirm your bike advertises a BLE name containing `CGO600`.

Useful information for bug reports:

- Firmware release or commit SHA
- ESP32 board type and USB serial chip
- Flashing method, USB or OTA
- Home Assistant and MQTT broker versions
- Screenshot of the web status page
- Serial logs from boot through the failure
- Whether the issue started after a config or firmware change
