# Flash & Update

English | [简体中文](FLASH.md)

## Partition table

16 MB flash; the authoritative source is [partitions.csv](../partitions.csv):

| Partition | Offset | Size | Purpose |
|-----------|--------|------|---------|
| nvs | 0x9000 | 24 KB | User settings |
| otadata | 0xF000 | 8 KB | OTA slot selection |
| phy_init | 0x11000 | 4 KB | RF calibration |
| ota_0 | 0x20000 | 3 MB | Firmware slot A |
| ota_1 | 0x320000 | 3 MB | Firmware slot B |
| theme_0 | 0x620000 | 4 MB | Runtime theme (optional, see [THEMES.en.md](THEMES.en.md))|
| bootmedia | 0xA20000 | 5.875 MB | Boot animation |

## USB flashing

Pre-built firmware lives in [firmware/release/](../firmware/release/); you
need [esptool.py](https://github.com/espressif/esptool) (`pip install esptool`):

```bash
esptool.py --chip esp32s3 -p PORT -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0      firmware/release/bootloader/bootloader.bin \
  0x8000   firmware/release/partition_table/partition-table.bin \
  0xf000   firmware/release/ota_data_initial.bin \
  0x20000  firmware/release/obd_brz_gauge.bin \
  0xA20000 firmware/release/bootmedia.bin
```

- `bootmedia.bin` (boot animation) is optional — without it you just don't get
  the VIDEO mode animation
- Optionally append a theme: `0x620000 your_theme.bin`
- **Upgrading a device on an old partition table (bootmedia elsewhere, no
  theme_0) requires a full USB reflash** — OTA cannot rewrite the partition
  table itself

## App OTA upgrade (dual-slot rollback)

The companion phone app (`android_app/app-debug.apk`) can push firmware, boot
animations and theme packages; the entry point on the device is always
**version page → OTA button**. For a firmware update:

1. The app fetches `latest.json` and reads the device manifest over BLE to
   check hardware compatibility; a mismatch aborts the flash
2. The new firmware is written to the **currently inactive OTA slot**
   (`esp_ota_ops`)
3. The device reboots into the new slot; the slot is marked valid only after
   a **15-second boot self-check** — if the new firmware crashes early, the
   bootloader rolls back to the previous slot

Boot-animation updates are transactional: the device stages
`boot_block.txt.new` / `boot_block.bin.new` first, then commits atomically;
an interrupted transfer never corrupts the running animation. RS485 and
ESP-NOW are paused during the transfer to free the CPU.

The full transfer protocol (service UUIDs, HTTP endpoints, headers) is in
[APP_PROTOCOL.en.md](APP_PROTOCOL.en.md).
