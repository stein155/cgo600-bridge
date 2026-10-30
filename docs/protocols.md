# BLE protocols

Reference for the three transports the bridge speaks, reverse engineered from the Tenways app (2.18.0) and the BIKEGO plus app (1.6.1). Details may differ across display firmware versions.

## Nordic UART (CGO600)

The CGO600 exposes the Nordic UART service (`6e400001-b5a3-f393-e0a9-e50e24dcca9e`). Requests go to `…0002`; replies arrive as notifications on `…0003`.

### Frames

```
Read  0x52 [len] [field_ids...] [CRC16_lo] [CRC16_hi] 0x45
Write 0x57 [len] [payload...]   [CRC16_lo] [CRC16_hi] 0x45
```

The checksum is CRC-16/MODBUS (polynomial `0xA001` reflected, initial value `0xFFFF`), computed over all bytes from the start byte up to the CRC. Requests carry it low byte first; frames from the bike carry it high byte first.

### Fields

| ID | Field | Decoding |
|---|---|---|
| `0x07` | Serial number | ASCII string |
| `0x11` | Speed | uint16 BE ÷ 10 → km/h |
| `0x12` | Max speed | uint16 BE ÷ 10 → km/h |
| `0x13` | Average speed | uint16 BE ÷ 10 → km/h |
| `0x14` | Trip distance | uint16 BE ÷ 10 → km |
| `0x15` | Trip duration | uint16 BE → seconds |
| `0x16` | Remaining range | uint16 BE ÷ 10 → km (`0xFFFF` = unknown) |
| `0x17` | Odometer | uint16 BE ÷ 10 → km |
| `0x1A` | Battery | uint8 → % |
| `0x21` | Light | uint8 (0 = off, 1 = on) |

## King Meter (CGO800 and others)

Newer displays expose a King Meter service (`43480001-f001-4b49-4e47-204d45544552`) instead of NUS. Requests are written to `43480002-…`; replies arrive as notifications on `43480003-…`. The bridge has a profile for each driver in the Tenways app and follows how the app talks to each model; the Longtail Duo layout was first captured by [tenways-ble](https://github.com/Styx85/tenways-ble).

```
[type] 0x00 [len] [count] [payload...] [CRC16] [CRC16] 0x45
```

`type` is `0x62` (read) or `0x63` (write), `len` counts `count` and the payload, and `count` is the number of keys. A read request lists keys; the reply carries `[key] [size] [value]` entries with big-endian values. A write carries one `[key] [size] [value]` entry. The checksum is the same CRC-16/MODBUS. The CGO800 sends it low byte first, every other King Meter model high byte first, and a display ignores requests in the wrong order.

| Profile | Models (advertised name starts with) | Info keys | Status keys |
|---|---|---|---|
| CGO800 | `CGO800` | `04 05` | `05 06 07 08 09 0A 0B 0C 0F 11 23` |
| CGO009 | `CGO009` | `04 05 70 4F 50` | `06 07 08 09 11 12 0A 0B 0C 0F 23 4F 50` |
| Longtail Duo | `Longtail Duo` | `04 05 4F 70` | `06 07 08 09 11 0A 0B 0C 0F 23 4F` |
| AGO AIR | `AGO AIR`, `Wayfarer_B`, `CGO600_D` | `04 05 4F 76` | as Longtail Duo |
| AGO | anything else, and bikes without a name | `04 05` | `05 06 07 08 09 11 0A 0B 0C 0F 23` |

AGO AIR models and every model whose name ends in `_D` are written with response, like the app does. When a bike does not answer, the bridge first tries the encryption handshake and then the other CRC order.

| Key | Field | Decoding |
|---|---|---|
| `0x04` | Firmware version | dotted bytes (`01 02 03` = 1.2.3); CGO800: ASCII |
| `0x05` | Serial number | ASCII string |
| `0x06` | Speed | uint16 ÷ 10 → km/h |
| `0x07` | Max speed | uint16 ÷ 10 → km/h |
| `0x08` | Average speed | uint16 ÷ 10 → km/h |
| `0x09` | Trip distance | uint32 ÷ 10 → km |
| `0x0A` | Ride time | uint32 → seconds |
| `0x0B` | Remaining range | uint16 → km; 655 or 32767 = none (CGO800: any byte ≥ `0x80` = none) |
| `0x0C` | Odometer | uint32 ÷ 10 → km (CGO800: whole km) |
| `0x0F` | Battery | uint8 → % |
| `0x11` | Light | uint8 (0 = off, 1 = on); written to switch the light |
| `0x23` | Power | uint16 → W (above 3000 ignored) |

### Encryption

Some displays (seen on the CGO800 Plus) answer plain requests only with an `AT+NAME=…` error and expect an encrypted session first. The bridge tries this handshake first on CGO800 bikes, and on other bikes when they do not answer or report such an error; it continues unencrypted when the display does not reply to it:

1. The bridge writes 16 random bytes under key `0xD8`; the display replies with its own 16 random bytes under the same key.
2. Both sides derive a 32-byte AES key with HKDF-SHA256: the bridge's random is the salt, a fixed secret from the Tenways app the input key, and the display's random the info.
3. The bridge reads key `0xD2`, the 16-byte IV.
4. From then on every frame is sent and received as an AES-256-CBC packet (PKCS#7 padding, the same IV for every packet):

```
0xA5 [0x80 | len_hi] [len_lo] [seq_hi] [seq_lo] [ciphertext] [CRC16_hi] [CRC16_lo] 0x5A
```

`len` is the ciphertext length, `seq` counts packets from 1 and the CRC is CRC-16/MODBUS over `0xA5` up to the ciphertext. Writes to an encrypted display use write with response.

A notification that starts with `AT+` is an error report from the display. Like the Tenways app, the bridge reconnects after the second one.

The first 20 requests and notifications of every King Meter connection are logged at DEBUG, and a summary of the traffic (requests, notifications, frames, CRC errors, skipped bytes, errors) every 30 s. Add `logger: logs: ebike: VERBOSE` to log every raw notification.

## Microchip transparent UART (B06)

B06 displays, set up with the BIKEGO plus app, expose the Microchip transparent UART service (`49535343-fe7d-4ae5-8fa9-9fafd205e455`). Frames are written to `49535343-8841-…` and arrive as notifications on `49535343-1e4d-…`:

```
0xA5 [len] [cmd] [payload...] [CRC16_lo] [CRC16_hi] 0x0D
```

`len` counts the payload; the checksum is CRC-16/XMODEM (polynomial `0x1021`, initial value `0`) over `len` up to the payload. After subscribing, the bridge sends `20 01`; the display answers `20` with a 4-byte challenge, and the bridge returns `21` with that challenge, zero-padded to 16 bytes and encrypted with AES-128-ECB under a key fixed in the app. The display confirms with `21 00`. It then pushes its data without being polled:

| Cmd | Payload | Decoding |
|---|---|---|
| `0x41` | ≥ 29 bytes | speed, max speed (uint16 LE ÷ 10), average speed (uint16 LE ÷ 100), two bytes the app calls range but never shows (not used), ride time (uint32 LE, s), trip and odometer (uint32 LE ÷ 100, km), then gear, light, mode, error code and other states as single bytes |
| `0x42` | ≥ 6 bytes | battery (%), then voltage, current (uint16 BE, raw) and charging |
| `0x40` | | version: a protocol type byte, then the software version as ASCII (request: empty payload) |
| `0x43` | `02 0E 01 [0\|1]` | switches the light (request) |

The display advertises as `B06_BLE`. A real display sends longer frames than the app decodes (46 bytes for `0x41`, 99 for `0x42`); only the fields above are used.
