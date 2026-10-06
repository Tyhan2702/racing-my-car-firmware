# 烧录与升级

[English](FLASH.en.md) | 简体中文

## 分区表

16MB Flash，权威来源 [partitions.csv](../partitions.csv)：

| 分区 | 偏移 | 大小 | 用途 |
|------|------|------|------|
| nvs | 0x9000 | 24 KB | 用户设置 |
| otadata | 0xF000 | 8 KB | OTA 槽位选择 |
| phy_init | 0x11000 | 4 KB | 射频校准 |
| ota_0 | 0x20000 | 3 MB | 固件槽 A |
| ota_1 | 0x320000 | 3 MB | 固件槽 B |
| theme_0 | 0x620000 | 4 MB | 运行时主题（可空，见 [THEMES.md](THEMES.md)）|
| bootmedia | 0xA20000 | 5.875 MB | 开机动画 |

## USB 烧录

预编译固件在 [firmware/release/](../firmware/release/)，需要
[esptool.py](https://github.com/espressif/esptool)（`pip install esptool`）：

```bash
esptool.py --chip esp32s3 -p PORT -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0      firmware/release/bootloader/bootloader.bin \
  0x8000   firmware/release/partition_table/partition-table.bin \
  0xf000   firmware/release/ota_data_initial.bin \
  0x20000  firmware/release/obd_brz_gauge.bin \
  0xA20000 firmware/release/bootmedia.bin
```

- `bootmedia.bin`（开机动画）可选，不烧只是没有 VIDEO 模式动画
- 可选追加主题：`0x620000 your_theme.bin`
- **从老分区表（bootmedia 在其他地址、无 theme_0）的设备升级必须 USB 全量重刷** ——
  OTA 无法改写分区表本身

## App OTA 升级（双槽回滚）

配套手机 App（`android_app/app-debug.apk`）可以推送固件、开机动画和主题包，
操作入口都在仪表 **版本页 → OTA 按钮**。升级固件时：

1. App 取 `latest.json`，BLE 读设备清单比对硬件兼容性，不匹配会拒绝刷写
2. 新固件写入**当前未运行的 OTA 槽**（`esp_ota_ops`）
3. 设备重启进新槽，**开机 15 秒自检**通过才标记有效；
   新固件在早期启动崩溃 → bootloader 自动回滚到旧槽

开机动画更新是事务式的：先写 `boot_block.txt.new` / `boot_block.bin.new` 暂存，
再原子提交；传输中断不会破坏现有动画。传输期间 RS485 与 ESP-NOW 暂停让出 CPU。

BLE / WiFi 传输的完整协议（服务 UUID、HTTP 端点、请求头）见
[APP_PROTOCOL.md](APP_PROTOCOL.md)。
