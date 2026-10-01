# 车型适配

[English](VEHICLES.en.md) | 简体中文

车型在设置页 **VEHICLE** 滚轮里切换，立即生效。新增车型是**数据驱动**的：
只需编辑 2 个文件，不需要改解析逻辑。

```
默认行为 = OBD2 标准协议（SAE J1979）
只有在 vehicle_custom_config.h 里声明了的车型，才会使用自定义 CAN 规则或油温公式。
```

| 文件 | 作用 |
|---|---|
| [main/app_obd_dsp/vehicle_profiles.c](../main/app_obd_dsp/vehicle_profiles.c) | 车型基础参数（传动比 / 挡位 / 轮胎 / 油温策略 / 协议锁定）|
| [main/app_obd_dsp/vehicle_custom_config.h](../main/app_obd_dsp/vehicle_custom_config.h) | 自定义覆盖（CAN 规则 / 油温公式 / UDS 头切换 / 挡位 DID）|
| [main/app_obd_dsp/vehicle_profiles.h](../main/app_obd_dsp/vehicle_profiles.h) | 结构体定义 + API（本页字段说明的权威来源）|

## 内置车型总表

来源 `vehicle_profiles.c` 的 `s_profiles[]`（设置页按此顺序显示）：

| # | 名称 | 协议 | 标准寻址 | 油温读取 | 挡位 | 增压 | 备注 |
|---|------|------|----------|----------|------|------|------|
| 0 | `OBD2 Generic` | 自动 | 物理 7E0 | `01 5C` | 比值估算 | — | 通用 SAE J1979，传动比为 6MT 占位值 |
| 1 | `ZN/C6 CAN` | 6 锁定 | 物理 7E0 | Toyota `21 01` | 比值估算 | — | **唯一 CAN ATMA 监听**：0x140 节气门、0x360 油温/水温；RPM 走 OBD。已在实车完整验证 |
| 2 | `ZN/C6 PID` | 6 锁定 | 物理 7E0 | Toyota `21 01` | 比值估算 | — | 不支持 ATMA 的廉价适配器用这个（纯 OBD 回退）|
| 3 | `ZD8 OBD` | 6 锁定 | 物理 7E0 | `01 5C` | 比值估算 | — | BRZ 二代 FA24，OBD-only |
| 4 | `ZD8` | 6 锁定 | 物理 7E0 | `01 5C` | 比值估算 | — | 与 #3 配置相同，标准 PID 回退版本 |
| 5 | `MX-5 ND` | 自动 | 物理 7E0 | `22 13 10` 双字节 → `22 11 1F` 回退 | 比值估算 | — | 40ms 超时（马自达 CAN 响应快）|
| 6 | `BMW F/G` | 6 锁定 | 功能 7DF | `01 5C` | **DID `DA2E` 直读**（EGS 变速箱）| ✓ | G20/G21/G22 B48/B58 + ZF 8HP；挡位走 6F1 扩展寻址请求 / 618 过滤接收 |
| 7 | `Supra A90` | 6 锁定 | 功能 7DF | `22 44 02`（°C=raw×0.75−48）→ `01 5C` 回退 | 比值估算（占位）| ✓ | B58/B48；**OBD 油压 DID `4436`**（hPa），替代 ADS1115 |
| 8 | `BMW G OBD` | **7 锁定** | 功能 7DF | `01 5C` → `22 44 02` 回退 | 比值估算 | ✓ | G 系自动检测不稳，锁定协议 7 |
| 9 | `BMW E` | 6 锁定 | 功能 7DF | `22 44 02`（6F1 头）→ `22 58 22` 回退 | 比值估算 | — | E9x/E46/E39；**OBD 油压 DID `586F`**（6F1 头，N55 验证）|
| 10 | `JCW F56` | 自动 | 物理 7E0 | MINI `22 58 22`（A−60）→ `01 5C` 回退 | 比值估算 | ✓ | B48 前驱 |
| 11 | `MINI R55` | 6 锁定 | 功能 7DF | MINI `22 58 22` → `01 5C` 回退 | 比值估算 | ✓ | N14/N18/N16 |
| 12 | `POS 997.2` | 6 锁定 | 物理 7E0 | `01 5C` | 比值估算（PDK 7 速）| — | 987.2/997.2 DFI |
| 13 | `POS 997.1` | 6 锁定 | 物理 7E0 | `01 5C` | 比值估算 | — | 987.1/997.1 M96/M97，齿比为 Gen2 占位 |
| 14 | `GIULIA 2.0T` | 7 锁定 | **29 位功能 `18DB33F1`** | `22 13 02` @ `18DA10F1` | 比值估算 | ✓ | Giorgio 平台；UDS 响应慢，50ms 轮询间隔 |
| 15 | `jeep` | 7 锁定 | 29 位功能 `18DB33F1` | `01 5C` | 比值估算（占位）| — | 通用占位，齿比 / 轮胎待按实际车型修正 |
| 16 | `Honda Integra` | 7 锁定 | 29 位功能 `18DB33F1` | `01 5C` 尝试 | **禁用**（CVT，`gear_count=0`）| ✓ | 思域 11 代平台 L15C7；非 Type R 可能无物理油温传感器 |

说明：

- **挡位列**：优先用 CAN / Mode 22 DID 直读的精确挡位（如 BMW F/G 的 DA2E），没有直读来源时按"转速 ÷ 车速 → 传动比"估算，`gear_count=0`（CVT）则禁用估算
- **油温**有四级回退链（primary → secondary → tertiary → quaternary），主策略连续失败自动切换
- `OBD2 Generic`、`Supra A90`、`MINI R55`、`jeep`、`Honda Integra` 不在 override 表里 —— 它们只用 profile 自带的枚举策略，行为同样是数据驱动的

## 新增车型

### 步骤 1：基础参数（`vehicle_profiles.c` 的 `s_profiles[]` 末尾追加）

```c
{
    .name = "My Car",                    // 显示名称，也是 override 的匹配键
    .final_drive_ratio = 3.73f,          // 主减速比
    .tire_rolling_radius_m = 0.310f,     // 轮胎滚动半径（米）
    .gear_count = 6,                     // 前进挡数；CVT 填 0（禁用挡位估算）
    .gear_ratios = {0, 3.63f, 2.38f, 1.56f, 1.18f, 1.00f, 0.81f},
    .gear_tolerance = 0.15f,             // 挡位识别容差（±15%）
    // 以下可选，省略 = 纯 OBD2 标准 + 自动协议
    .oil_temp_strategy = { ... },        // 油温四级回退链（枚举见 vehicle_profiles.h）
    .has_boost = true,                   // 涡轮车显示增压（标准 PID 01 0B）
    .forced_protocol = 6,                // 锁定 ELM327 协议（0=自动；BMW/本田等检测不稳时锁）
    .obd_functional_addr = true,         // true=ATSH7DF 功能寻址（多数德系）
    .obd_29bit_functional = true,        // 29 位功能广播 ATSH18DB33F1（本田 11 代/FCA）
    .obd_oil_pressure_did = 0x4436,      // Mode 22 油压 DID（hPa），有则替代 ADS1115
    .obd_gear_did = 0xDA2E,              // Mode 22 挡位 DID，有则替代比值估算
    .obd_timeout = 0x0A,                 // ATST 超时（0=默认 0x19；响应快的车调小）
    .poll_gap_ms = 1,                    // 轮询间隔 ms（0=默认 30ms）
    .speed_scale = 1.0f,                 // 车速修正系数（0/不填=1.0）
    .can_broadcast_mode = false,         // true=ATMA CAN 监听（目前仅 ZN/C6 CAN）
},
```

**如果新车只需要标准 OBD2，到这里就结束了。**

### 步骤 2（可选）：自定义覆盖（`vehicle_custom_config.h`）

需要 CAN 广播解码、非标准 UDS 头或特殊油温公式时才做。

#### 2a. CAN 广播解码规则

```c
static const can_rule_t can_rules_mycar[] = {
    // CAN_ID  位偏移  位长  乘数        偏移    通道
    { 0x140,   16,    14,   1.0f,       0.0f,  CH_RPM },
    { 0x360,   16,     8,   1.0f,     -40.0f,  CH_OIL_TEMP },
};
```

- `bit_off` 从 LSB 起算（SAE J1939 风格）：Byte0 bit0 = 0，Byte1 bit0 = 8
- 最终值 = raw × scale + offset
- 可用通道：`CH_RPM` `CH_SPEED` `CH_OIL_TEMP` `CH_COOLANT` `CH_TPS` `CH_LOAD` `CH_INTAKE` `CH_BOOST` `CH_GEAR`

#### 2b. 油温公式

```c
static const oil_formula_t oil_std = {
    OIL_STD_PID, {0x5C}, 1, 0, 1, 1.0f, -40.0f, 0   // 标准 01 5C
};
static const oil_formula_t oil_uds = {
    OIL_UDS_22, {0x13,0x10}, 2, 0, 2, 0.01f, -40.0f, 0  // UDS 22 双字节大端
};
```

| 类型 | 说明 | 命令格式 |
|---|---|---|
| `OIL_STD_PID` | 标准 Mode 01 | `01 XX\r` |
| `OIL_UDS_22` | UDS Mode 22（1–2 字节）| `22 XX XX\r` |
| `OIL_SPECIAL` | 特殊解析（Toyota Mode 21 等遗留，需专用代码）| 自定义 |

#### 2c. 注册 override

```c
{
    .match_name      = "My Car",        // 必须与 vehicle_profiles.c 的 name 完全一致
    .can_rules       = can_rules_mycar, // NULL = 不用 CAN
    .can_rule_count  = 2,
    .oil_primary     = &oil_uds,        // 主油温公式（NULL = 标准 01 5C）
    .oil_secondary   = &oil_std,        // 备用公式（主公式连续失败 5 次后切换）
    .forced_protocol = 6,               // 0 = 自动
    .functional_addr = true,            // true = ATSH7DF
    .obd_timeout     = 0x0F,            // ATST（0 = 默认）
    .has_boost       = true,
    .poll_gap_ms     = 1,               // 0 = 默认 30ms
    // UDS / 挡位查询的特殊头切换（宝马、FCA 等扩展寻址车型用）：
    .uds_header_cmd         = "ATSH18DA10F1\r", // 查油温前临时切换的头，查完恢复
    .obd_gear_header_cmd    = "ATSH6F1\r",      // 查挡位 DID 用的头（NULL=回退 uds_header_cmd 再 7E0）
    .obd_gear_rx_filter_cmd = "ATCRA618\r",     // 挡位应答过滤（NULL=ELM327 默认过滤）
    .obd_gear_raw_frame     = "18 03 22 DA 2E\r", // ATCAF0 裸帧（扩展寻址目标字节+ISO-TP PCI）
},
```

## 运行时查找逻辑

```
vehicle_profile_get_override()
    ├── 找到 override → 使用自定义 CAN 规则 / 油温公式 / UDS 头
    └── 返回 NULL    → 纯 OBD2 标准协议（01 0C/0D/05/5C/0F/04/11/42）
```

## CAN 规则解析器 API

定义在 `vehicle_custom_config.h`（static inline，无独立 .c）：

```c
bool can_extract_bits_le(const uint8_t data[8], uint8_t bit_off,
                         uint8_t bit_len, uint32_t *out);
void can_apply_rules(const can_rule_t *rules, uint8_t count,
                     uint16_t can_id, const uint8_t data[8],
                     float channels[CH_COUNT]);
const char *oil_formula_build_cmd(const oil_formula_t *f, char *buf, size_t buflen);
int16_t oil_formula_parse_resp(const oil_formula_t *f,
                               const uint32_t *resp_data, uint8_t resp_len);
```

## 注意事项

1. **name 必须完全匹配** —— profile 的 `name` 和 override 的 `match_name` 字符串不一致时 override 静默不生效
2. **挡位 DID 与比值估算的关系** —— `obd_gear_did` 直读优先，无应答时回退比值估算；扩展寻址车型还需要配 `obd_gear_header_cmd` 等三个字段（参考 BMW F/G 条目）
3. **双字节公式**按大端组合：`value = data[byte] * 256 + data[byte+1]`
4. **迁移状态**：`elm327_ble_client.c` 里的旧 switch/case 解析仍在运行，与数据表通过 `vehicle_profile_get_override()` 桥接，后续逐步迁移到通用解析器
5. 找未知车型的私有 PID 可以用 [tools 里的伪 ELM327 挖掘流程](DEVELOPMENT.md#工具脚本)，不需要上车
