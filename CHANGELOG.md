# 更新日志

[English](CHANGELOG.en.md) | 简体中文

最新的在最上面。只记录行为变化和烧录方式变化；纯重构和注释整理请查 git 历史。

---

## 文档体系重建（中英双语）

本分支相对 main 的全部改动汇总为一次文档重建，不涉及任何固件行为：

- **重构**：根 README 只做入口（定位、快速开始、索引、3D 模型、许可证），细节按受众拆成七份主题文档——`USER_GUIDE`（使用）、`FLASH`（烧录与升级，用户向）、`APP_PROTOCOL`（App 对接协议，开发者向）、`VEHICLES`（车型适配）、`THEMES`（主题，四份旧文档合并）、`TROUBLESHOOTING`（排障，三份旧文档合并）、`DEVELOPMENT`（开发指南，含发布流程）；`firmware/`、`themes/`、`theme_store/` 三个目录 README 精简为指引页。删除 12 份过时文档，包括描述已不存在的双分支世界的 `docs/BRANCH_COMPARISON.md`
- **修正危险的过时信息**：旧文档的部分表格仍把 bootmedia 烧到 `0x620000`——该地址现在是 theme_0 主题分区，照旧文档操作会毁掉主题分区；统一为 bootmedia `0xA20000` / 主题 `0x620000`，以 `partitions.csv` 为准。同时修正车型数量（12 → **17**）、`model/Subaru/brz_zc6/` 路径、"app 分区 4MB"（实为 3MB）等过期信息
- **双语**：11 份文档各配 1:1 镜像英文版（`X.md` ↔ `X.en.md`），标题下带语言切换行，英文版内部互链走 `.en.md`；中文为事实源，双语维护规则见 `DEVELOPMENT.md`
- **文档性代码注释修正**：`partitions.csv` bootmedia 容量注释 7.875MB → 5.875MB；`ads1115_oil_pressure.h` 头注释由"直连 ESP32 ADC"改为 ADS1115 I2C ADC；`make_boot_block.py` docstring 补 v2 格式；`one_shot.py` / `analyze_proble.py` 修正错误文件名 `analyze_probe.py`

## 主题引擎修复与 BMW 挡位直读

- BMW F/G 车型通过 EGS 扩展寻址 DID `DA2E` 直读当前挡位
  （请求头 `ATSH6F1`、接收过滤 `ATCRA618`），替代传动比估算
- 主题数据快照加入进气温度（IAT），修复 OBD 挡位页在无数据时显示 "--" 的问题
- 主题引擎回滚到 build 118（条件规则引擎引入的回归）
- 修正 CST816 触摸 I2C 引脚，触摸走独立总线

## 三连表联动转速报警同步

- 主表把 `rpm_warn_linked_en` 配置随广播同步给从表，三块表无需逐块设置
- 联动模式下三表按表位依次亮起、到阈值全体闪烁

## 新增车型（内置 17 个）

- 新增 `Supra A90`（B58，OBD 油压 DID `4436` 替代 ADS1115）、`BMW E`
  （N55 油温 `4402`/`5822` 与油压 `586F` 走 `6F1` 头）、`MINI R55`、`jeep`、
  `Honda Integra`（29 位功能寻址 `18DB33F1`，CVT 禁用挡位估算）
- 完整能力表见 `docs/VEHICLES.md`

## V2/V3 硬件变体（tai_ji_xiao_pai 24/25）

- 新增编译期硬件版本选择（menuconfig → OBD DSP Configuration）：
  V1 微雪板（默认）/ V2 新板 A（ST77916 v1 初始化序列）/ V3 新板 B（v2 序列），
  V2/V3 为直连 GPIO、无 TCA9554 / ADS1115

## 主题分区系统（theme_0）

- 新增 4MB `theme_0` 分区（`0x620000`）承载运行时主题：manifest + layout.json +
  360×360 表盘/表环素材，开机由 `theme_engine` 加载，损坏自动回退内置默认主题
- **bootmedia 从 `0x620000` 移到 `0xA20000`，容量 9.875MB → 5.875MB**（实际用量约 312KB）
- WiFi OTA 新增 `/ota/theme`、`/ota/theme/prepare`、`/ota/theme/erase` 端点；
  BLE 侧主题传输暂未实现
- 主题声明的页面替换内置表盘页进入轮播环（省 40–60 KB RAM）；
  `logo` / `intro` / `boot_video` 为受保护页，永不主题化
- ⚠️ 老分区表设备升级必须**一次性 USB 全量重刷**，OTA 无法改写分区表

## 设备端 OTA：BLE 服务 + WiFi 传输

- 新增 **OTA 模式页**（**版本页 OTA 按钮进入**）：发布 OTA BLE 服务（`0x1FFB`）并启动
  WiFi SoftAP（`OBD-Gauge-OTA-XXXX`，密码 `obd2024`），HTTP 端点接收
  SHA256 校验的固件与开机动画
- 固件写入备用 OTA 槽，启动 15 秒自检通过才标记有效，早期崩溃由 bootloader 回滚
- 开机动画更新事务化：`boot_block.txt.new`/`bin.new` 暂存后原子提交，中断可恢复；
  传输期间 RS485 与 ESP-NOW 暂停
- 版本页显示固件 build tag（分支-提交数-短哈希，编译时注入）

## RaceChrono 开关与开机动画模式简化

- 设置页新增 **RACECHRONO** 开关：关闭时进入最小 BLE 模式（仅 Info + OTA 服务，
  不广播），开启恢复完整 RaceChrono + 配对 + OTA 服务
- 开机动画模式简化为 **OFF / RACE / VIDEO**；VIDEO 播放手机 App 刷入的自制动画
  （替代旧的 REI/SHINJI/ASUKA 三个槽位，旧值自动迁移到 VIDEO）

## OBD 数据链路修复

- ELM327 客户端改为**单线程轮询**，不再混跑 OBD/CAN 并行请求
- 刹车温度 / 油压报警节流为 30 秒一次
- 数据中断自愈：重初始化 + 自动重连，上车通电无需手动重连

## OTA 双槽布局与设备清单

- 分区表改为 `ota_0` + `ota_1`（各 3MB）+ `bootmedia`，支持升级失败回滚
- 只读 BLE 设备清单服务（`0x1FFA`）暴露硬件与构建信息，App 刷写前做兼容校验；
  清单精简为 App 实际使用的字段，确保 512 字节内不截断

## 主题数据化（编译期 TOML 主题）

- 主题从 C 代码改为**数据声明**：`themes/` 下的 `theme.toml` + 素材 PNG，
  `tools/gen_themes.py` 在 CMake configure 阶段生成 `ui_theme_generated.c`
- 8 个装饰色角色 + 3 类可选素材（表框/指针/表盘），总预算 1536 KB
- `registry.txt` 槽位表只能追加，防止 OTA 后在用设备被静默换肤
- 语义色（报警红等）全局固定，任何主题不可改
