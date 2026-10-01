# 烧录与 OTA 升级

Flash 分区布局、USB 烧录、以及配套手机 App 的 BLE / WiFi OTA 对接协议。
普通用户看前两节就够；后面的 HTTP API 章节面向 App 开发者。

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

## 升级机制（双槽回滚）

1. App 从发布目录取 `latest.json`，BLE 读设备清单比对硬件兼容性
2. 兼容则把新固件写入**当前未运行的 OTA 槽**（`esp_ota_ops`）
3. 设备重启进新槽，**开机 15 秒自检**通过后才 `esp_ota_mark_app_valid_cancel_rollback()`
4. 新固件在早期启动崩溃 → bootloader 自动回滚到旧槽

开机动画更新是事务式的：先写 `boot_block.txt.new` / `boot_block.bin.new` 暂存，
再原子提交；传输中断可恢复旧动画。传输期间 RS485 与 ESP-NOW 暂停让出 CPU。

## 进入 OTA 模式

设备端：**版本页 → OTA 按钮**。进入后：

- 发布 OTA BLE 服务（`0x1FFB`）
- 启动 WiFi SoftAP：SSID `OBD-Gauge-OTA-XXXX`（MAC 后两字节）、密码 `obd2024`、
  IP `192.168.4.1`、端口 80
- 每次会话生成随机 16 位 hex token，用于 HTTP 鉴权

## BLE 设备清单服务（只读）

App 刷写前用它做硬件匹配校验：

- Service UUID：`0x1FFA`，Characteristic UUID：`0x0001`
- 载荷：UTF-8 JSON（上限 512 字节，只返回 App 实际使用的字段）

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

`build_tag` 格式为 `<分支>-<提交数>-<短哈希>`，编译时由 `main/CMakeLists.txt` 从 git
注入。App 用 `firmware.count` 与 `latest.json` 比较判断新旧。
硬件字段与所选 release 不一致时 App 应拒绝刷写。

## BLE OTA 服务

- Service UUID `0x1FFB`；控制特征 `0x0001`、数据特征 `0x0002`、状态特征 `0x0003`
- 控制包：Magic `OTA1`；命令 `begin` / `end` / `cancel`；目标 `firmware` 或 `bootmedia`；
  小端 u32 长度；32 字节原始 SHA256；长度前缀 UTF-8 文件名
- 控制命令 `4` = 启动 WiFi OTA 模式（推荐走 WiFi 传大文件），设备通过状态特征回 JSON：

```json
{"ssid":"OBD-Gauge-OTA-ABCD","password":"obd2024","ip":"192.168.4.1","token":"a1b2c3d4e5f6a7b8","port":80}
```

BLE 状态特征同步 WiFi OTA 进度：`wifi-starting` / `wifi-ready` / `wifi-receiving` /
`wifi-done` / `wifi-error`。

## WiFi OTA HTTP API

App 连上设备 SoftAP 后走 HTTP（所有端点均支持 OPTIONS 预检）。

**公共请求头**：`X-OTA-Token: <BLE 握手拿到的 16 位 hex>`

| 端点 | 方法 | 说明 |
|------|------|------|
| `/ota/discover` | GET | 局域网发现 |
| `/ota/info` | GET | 设备清单 JSON（同 BLE `0x1FFA`）|
| `/ota/status` | GET | `{"state":"ready\|receiving\|done\|error","received":N,"expected":M}` |
| `/ota/firmware` | POST | 固件上传 |
| `/ota/bootmedia/prepare` | POST | 开机动画预检 |
| `/ota/bootmedia` | POST | 开机动画上传 |
| `/ota/theme/prepare` | POST | 主题预检（返回挂载状态，不擦除）|
| `/ota/theme` | POST | 主题分块上传 |
| `/ota/theme/erase` | POST | 擦除主题分区，下次开机回退默认主题 |

各上传端点的专有请求头：

- `/ota/firmware`：`X-OTA-SHA256`（64 位 hex）、`X-OTA-Size`（字节），Body 为固件二进制
- `/ota/bootmedia`：`X-OTA-SHA256`、`X-OTA-Size`（manifest+bin 总大小）、
  `X-OTA-Manifest-Size`，Body 为 `[manifest][bin]` 拼接
- `/ota/theme`：`X-OTA-SHA256`、`X-OTA-Size`（≤4MB）、`X-OTA-Offset`（块偏移）、
  `X-Last`（`1`=最后一块），Body 为 `theme.bin` 分块数据

**安全性**：token 每会话随机；SoftAP WPA2-PSK；全部负载 SHA256 校验。
主题打包格式见 [THEMES.md](THEMES.md#运行时主题theme_0-分区)。

## 开机动画编辑规则（App 端）

- 画布 360×360，比例锁 1:1；建议圆形预览遮罩（圆屏四角裁切）
- 手机端先裁切起止时间，再编码上传
- 编码包必须放得进 bootmedia 分区（5.875 MB）
- 设备端只保留一个生效动画槽；App 可存多个草稿，只上传选中的那个

## 发布流程（固件维护者）

`tools/release.sh` 一键发版：激活 ESP-IDF 环境（eim）→ 提交源码（**必须先提交**，
`count` 取 git 提交数）→ `idf.py build` → `tools/gen_release.py` 把
`build/` 产物拷进 `firmware/release/` 并重写 `latest.json`（每个文件记 sha256/size）→
提交并推送。

App 侧发布目录最小布局：

```text
/releases/
  latest.json
  firmware/    obd_brz_gauge.bin · partition-table.bin · bootloader.bin · ota_data_initial.bin
  bootmedia/   bootmedia.bin
```

`latest.json` 由 `gen_release.py` 生成；release 二进制有变动时要重新生成，否则
App 会拿旧 manifest 比对新固件。
