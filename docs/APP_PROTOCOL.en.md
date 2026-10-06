# App Integration Protocol

English | [简体中文](APP_PROTOCOL.md)

The BLE services and WiFi HTTP API the device exposes in OTA mode. For the
user-facing flashing and update flow see [FLASH.en.md](FLASH.en.md).

## What the device exposes in OTA mode

After entering OTA mode via **version page → OTA button**:

- The OTA BLE service (`0x1FFB`)
- A WiFi SoftAP: SSID `OBD-Gauge-OTA-XXXX` (last two MAC bytes), password
  `obd2024`, IP `192.168.4.1`, port 80
- A fresh random 16-hex-char token per session, used for HTTP authentication

## BLE device-manifest service (read-only 0x1FFA)

Used by the app for hardware compatibility checks before flashing:

- Service UUID: `0x1FFA`, characteristic UUID: `0x0001`
- Payload: UTF-8 JSON (512-byte cap; only fields the app actually uses)

```json
{
  "device": {
    "board": "Waveshare ESP32-S3-Touch-LCD-1.85",
    "variant": "obd_brz_gauge",
    "lcd": "ST77916",
    "screen": { "w": 360, "h": 360, "bpp": 16 },
    "flash_mb": 16,
    "psram_mb": 8,
    "ota_slots": 2,
    "bootmedia_slots": 1,
    "bootmedia_format": 1
  },
  "firmware": {
    "build_tag": "main-124-abcdef123456",
    "branch": "main",
    "count": 124
  }
}
```

`build_tag` is `<branch>-<commit-count>-<short-hash>`, injected from git by
`main/CMakeLists.txt` at build time. The app compares `firmware.count`
against `latest.json` to decide whether an update exists. If any hardware
field differs from the selected release, the app must refuse to flash.

## BLE OTA service (0x1FFB)

- Service UUID `0x1FFB`; control characteristic `0x0001`, data `0x0002`,
  status `0x0003`
- Control packets: magic `OTA1`; commands `begin` / `end` / `cancel`;
  target `firmware` or `bootmedia`; little-endian u32 size; raw 32-byte
  SHA256; length-prefixed UTF-8 filename
- Control command `4` = start WiFi OTA mode (faster for large payloads); the
  device replies on the status characteristic with JSON:

```json
{"ssid":"OBD-Gauge-OTA-ABCD","password":"obd2024","ip":"192.168.4.1","token":"a1b2c3d4e5f6a7b8","port":80}
```

The BLE status characteristic mirrors WiFi OTA progress: `wifi-starting` /
`wifi-ready` / `wifi-receiving` / `wifi-done` / `wifi-error`.

## WiFi OTA HTTP API

Once the app has joined the device SoftAP, transfers go over HTTP (every
endpoint also answers OPTIONS preflight).

**Common header**: `X-OTA-Token: <16-hex token from the BLE handshake>`

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/ota/discover` | GET | LAN discovery |
| `/ota/info` | GET | Device manifest JSON (same as BLE `0x1FFA`)|
| `/ota/status` | GET | `{"state":"ready\|receiving\|done\|error","received":N,"expected":M}` |
| `/ota/firmware` | POST | Firmware upload |
| `/ota/bootmedia/prepare` | POST | Boot-animation preflight |
| `/ota/bootmedia` | POST | Boot-animation upload |
| `/ota/theme/prepare` | POST | Theme preflight (returns mount status, no erase)|
| `/ota/theme` | POST | Chunked theme upload |
| `/ota/theme/erase` | POST | Erase the theme partition; falls back to the default theme on next boot |

Per-endpoint upload headers:

- `/ota/firmware`: `X-OTA-SHA256` (64-char hex), `X-OTA-Size` (bytes); body is
  the firmware binary
- `/ota/bootmedia`: `X-OTA-SHA256`, `X-OTA-Size` (manifest+bin total),
  `X-OTA-Manifest-Size`; body is `[manifest][bin]` concatenated
- `/ota/theme`: `X-OTA-SHA256`, `X-OTA-Size` (≤4MB), `X-OTA-Offset` (chunk
  offset), `X-Last` (`1` = final chunk); body is a `theme.bin` chunk

**Security**: the token is random per session; the SoftAP uses WPA2-PSK;
every payload is SHA256-verified. Theme packaging is described in
[THEMES.en.md](THEMES.en.md).

## Boot-animation editor rules (app side)

- 360×360 canvas, aspect locked to 1:1; a circular preview mask is
  recommended (the round screen clips the corners)
- Trim start/end on the phone before encoding and uploading
- The encoded package must fit the bootmedia partition (5.875 MB)
- Only one animation slot is active on the device; the app may keep several
  drafts locally but uploads only the selected one
