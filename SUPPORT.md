# Support

Community project with best-effort support. Before opening an issue:

- Update to the latest release from the [web installer](https://stein155.github.io/ebike-bridge/) or over WiFi.
- Restart the ESP32 and the bike display, and close the bike's own app: a display accepts one connection.
- Check the logs (`esphome logs`, the ESPHome dashboard, or USB serial at `115200` baud).
- Check that the bike's Bluetooth name matches `device_names`; the defaults are in
  `esphome/components/ebike/__init__.py`.

Include in a bug report: firmware release or commit, ESPHome and Home Assistant versions, ESP32 board,
display model, how it was installed, and logs from boot through the failure.
