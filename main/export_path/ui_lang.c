// See ui_lang.h. The table is English → Chinese; a text missing from it shows in English.
#include "ui_lang.h"
#include <string.h>
#include "bsp_obd_dsp/nvs_storage.h"

static const char *const s_zh[][2] = {
    // menu
    {"OBD", "OBD"}, {"DATA", "数据"}, {"TEST", "测试"}, {"SET", "设置"}, {"GAMES", "游戏"}, {"BOOT", "开机"}, {"UPDATE", "更新"}, {"VERSION", "版本"},
    {"NO THEME YET", "还没有主题"}, {"NO BOOT ANIMATION YET", "还没有开机动画"},
    {"OPEN THE RACING MY CAR APP\nAND TAP RESTORE DEFAULT THEMES\n\nSWIPE UP FOR THE MENU", "打开 RACING MY CAR APP\n点恢复默认主题\n\n向上滑动打开菜单"},
    // settings
    {"SETTINGS", "设置"}, {"BRIGHTNESS", "亮度"}, {"VEHICLE", "车型"}, {"LANGUAGE", "语言"}, {"GAUGE MODE", "仪表模式"}, {"GAUGE POSITION", "仪表位置"},
    {"BOOT INTRO", "开机动画"}, {"SHIFT LIGHT RPM", "换挡提示转速"}, {"SHIFT LIGHT FLASH", "换挡闪烁"}, {"RACECHRONO", "RACECHRONO"},
    {"TRIP", "里程"}, {"RESET TRIP", "清零里程"}, {"TRIP RESET", "里程已清零"}, {"GAUGE", "仪表"}, {"RESTART", "重启仪表"},
    {"MASTER", "主仪表"}, {"SLAVE", "副仪表"}, {"ALONE", "单独使用"}, {"OFF", "关闭"}, {"VIDEO", "视频"},
    {"RESTART TO APPLY", "重启后生效"}, {"APPLIES AFTER A RESTART", "重启后生效"}, {"SWIPE LEFT / RIGHT: BACK", "左右滑动返回"}, {"TAP AGAIN", "再点一次确认"},
    // DATA page
    {"LIVE DATA", "实时数据"}, {"PROTOCOL", "协议"}, {"VIN", "车架号"}, {"BATTERY", "电瓶电压"}, {"AFR", "空燃比"}, {"OIL TEMP", "机油温度"},
    {"OIL PRESSURE", "机油压力"}, {"GEAR", "档位"}, {"TRANS TEMP", "变速箱温度"}, {"KNOCK RETARD", "爆震延迟"}, {"FUEL LEVEL", "油量"},
    {"FUEL PRESSURE", "燃油压力"}, {"DOORS", "车门"}, {"CLOSED", "全部关闭"}, {"OBD NOT CONNECTED", "OBD 未连接"}, {"AUTO", "自动"},
    {"%d OF %d READ", "已读取 %d / %d 项"}, {"OPEN%s%s%s%s%s", "未关%s%s%s%s%s"},
    // mode 01 values (obd_pid_table.c)
    {"MIL / CODES", "故障码状态"}, {"FUEL SYSTEM", "燃油系统状态"}, {"ENGINE LOAD", "发动机负荷"}, {"COOLANT TEMP", "水温"},
    {"STFT BANK 1", "短期燃油修正 1"}, {"LTFT BANK 1", "长期燃油修正 1"}, {"STFT BANK 2", "短期燃油修正 2"}, {"LTFT BANK 2", "长期燃油修正 2"},
    {"MAP", "进气压力"}, {"RPM", "转速"}, {"SPEED", "车速"}, {"TIMING ADVANCE", "点火提前角"}, {"INTAKE TEMP", "进气温度"}, {"MAF", "空气流量"},
    {"THROTTLE", "节气门"}, {"SECONDARY AIR", "二次空气"}, {"O2 SENSORS", "氧传感器位置"},
    {"O2 B1 S1", "氧传感器 1-1"}, {"O2 B1 S2", "氧传感器 1-2"}, {"O2 B1 S3", "氧传感器 1-3"}, {"O2 B1 S4", "氧传感器 1-4"},
    {"O2 B2 S1", "氧传感器 2-1"}, {"O2 B2 S2", "氧传感器 2-2"}, {"O2 B2 S3", "氧传感器 2-3"}, {"O2 B2 S4", "氧传感器 2-4"},
    {"OBD STANDARD", "OBD 标准"}, {"O2 SENSORS 2", "氧传感器位置 2"}, {"AUX INPUT", "辅助输入"}, {"RUN TIME", "本次运转时间"},
    {"KM WITH MIL ON", "故障灯亮后里程"}, {"RAIL PRESS REL", "油轨相对压力"}, {"RAIL PRESSURE", "油轨压力"},
    {"LAMBDA S1 V", "宽域氧 1 电压"}, {"LAMBDA S2 V", "宽域氧 2 电压"}, {"LAMBDA S3 V", "宽域氧 3 电压"}, {"LAMBDA S4 V", "宽域氧 4 电压"},
    {"LAMBDA S5 V", "宽域氧 5 电压"}, {"LAMBDA S6 V", "宽域氧 6 电压"}, {"LAMBDA S7 V", "宽域氧 7 电压"}, {"LAMBDA S8 V", "宽域氧 8 电压"},
    {"EGR COMMANDED", "指令 EGR"}, {"EGR ERROR", "EGR 误差"}, {"EVAP PURGE", "炭罐净化"}, {"WARM-UPS SINCE CLR", "清码后暖车次数"},
    {"KM SINCE CLR", "清码后里程"}, {"EVAP VAPOR", "炭罐蒸汽压力"}, {"BARO PRESSURE", "大气压力"},
    {"LAMBDA S1 MA", "宽域氧 1 电流"}, {"LAMBDA S2 MA", "宽域氧 2 电流"}, {"LAMBDA S3 MA", "宽域氧 3 电流"}, {"LAMBDA S4 MA", "宽域氧 4 电流"},
    {"LAMBDA S5 MA", "宽域氧 5 电流"}, {"LAMBDA S6 MA", "宽域氧 6 电流"}, {"LAMBDA S7 MA", "宽域氧 7 电流"}, {"LAMBDA S8 MA", "宽域氧 8 电流"},
    {"CAT TEMP B1 S1", "三元催化温度 1-1"}, {"CAT TEMP B2 S1", "三元催化温度 2-1"}, {"CAT TEMP B1 S2", "三元催化温度 1-2"}, {"CAT TEMP B2 S2", "三元催化温度 2-2"},
    {"MONITORS", "自检状态"}, {"MODULE VOLTAGE", "电脑电压"}, {"ABSOLUTE LOAD", "绝对负荷"}, {"COMMANDED LAMBDA", "指令空燃比"},
    {"REL THROTTLE", "相对节气门"}, {"AMBIENT TEMP", "环境温度"}, {"THROTTLE B", "节气门 B"}, {"THROTTLE C", "节气门 C"},
    {"PEDAL D", "油门踏板 D"}, {"PEDAL E", "油门踏板 E"}, {"PEDAL F", "油门踏板 F"}, {"CMD THROTTLE", "指令节气门"},
    {"MIN WITH MIL ON", "故障灯亮后时间"}, {"MIN SINCE CLR", "清码后时间"}, {"FUEL TYPE", "燃料类型"}, {"ETHANOL", "乙醇含量"},
    {"EVAP ABS PRESS", "炭罐绝对压力"}, {"EVAP PRESS", "炭罐压力"}, {"STFT O2 B1", "氧传感器短期修正 1"}, {"LTFT O2 B1", "氧传感器长期修正 1"},
    {"STFT O2 B2", "氧传感器短期修正 2"}, {"LTFT O2 B2", "氧传感器长期修正 2"}, {"RAIL ABS PRESS", "油轨绝对压力"}, {"REL PEDAL", "相对油门踏板"},
    {"HYBRID BATTERY", "混合动力电池"}, {"INJECTION TIMING", "喷油正时"}, {"FUEL RATE", "油耗"}, {"EMISSION STD", "排放标准"},
    {"DEMAND TORQUE", "需求扭矩"}, {"ACTUAL TORQUE", "实际扭矩"}, {"REFERENCE TORQUE", "参考扭矩"}, {"TORQUE POINTS", "扭矩点"},
    {"MAF SENSORS", "空气流量传感器"}, {"COOLANT SENSORS", "水温传感器"}, {"INTAKE SENSORS", "进气温度传感器"}, {"EGR TEMP", "EGR 温度"},
    {"THROTTLE ACTUATOR", "节气门执行器"}, {"TURBO INLET", "涡轮进气"}, {"BOOST CONTROL", "增压控制"}, {"EXHAUST PRESS", "排气压力"},
    {"TURBO RPM", "涡轮转速"}, {"TURBO A TEMP", "涡轮 A 温度"}, {"TURBO B TEMP", "涡轮 B 温度"}, {"CAC TEMP", "中冷温度"},
    {"EGT BANK 1", "排气温度 1"}, {"EGT BANK 2", "排气温度 2"}, {"DPF 1", "颗粒捕集器 1"}, {"DPF 2", "颗粒捕集器 2"}, {"DPF TEMP", "颗粒捕集器温度"},
    {"ENGINE RUN TIME", "发动机运转时间"}, {"NOX SENSOR", "氮氧传感器"}, {"MANIFOLD SURFACE", "进气歧管表面温度"}, {"THROTTLE G", "节气门 G"},
    {"FRICTION TORQUE", "摩擦扭矩"}, {"HYBRID SYSTEM", "混合动力系统"}, {"FUEL RATE ENG", "发动机油耗"}, {"CYL FUEL RATE", "单缸油耗"},
    {"TRANS GEAR", "变速箱档位"}, {"ODOMETER", "总里程"},
    // acceleration test
    {"ACCEL TEST", "加速测试"}, {"HISTORY", "历史成绩"}, {"TAP TO START", "点击开始"}, {"TAP TO CONTINUE", "点击继续"}, {"HOLD TO EXIT", "长按退出"},
    {"USE THE MAIN GAUGE", "请用主仪表测试"}, {"STOP THE CAR TO START", "停车后才能开始"}, {"READY  %d KM/H", "就绪  %d KM/H"},
    {"BEST %s  LAST %s", "最佳 %s  上次 %s"}, {"EXIT IN %d", "%d 秒后退出"}, {"EXIT TEST?", "退出测试？"}, {"CANCEL", "取消"}, {"EXIT", "退出"},
    {"FAILED", "失败"}, {"NEW BEST", "新纪录"}, {"FALSE START", "提前起步"}, {"SPEED DROPPED", "车速下降"}, {"NO OBD DATA", "没有 OBD 数据"},
    {"NO START", "没有起步"}, {"TOO SLOW", "太慢了"}, {"TO 60 KM/H", "加速到 60 KM/H"}, {"GO", "开始"},
    {"#FFDD00 %s#\n#8E8E93 BEST# %s S", "#FFDD00 %s#\n#8E8E93 最佳# %s 秒"}, {"#FFDD00 %s#\n#48484A NO RUN YET#", "#FFDD00 %s#\n#48484A 还没有成绩#"},
    // Wi-Fi update
    {"WI-FI UPDATE", "Wi-Fi 更新"}, {"WI-FI", "Wi-Fi"}, {"PASSWORD", "密码"}, {"STARTING WI-FI...", "正在开启 Wi-Fi…"},
    {"WI-FI DID NOT START", "Wi-Fi 未能开启"}, {"SWIPE TO EXIT", "滑动退出"}, {"FIRMWARE %s", "固件 %s"},
    // pairing
    {"FIND MASTER", "寻找主仪表"}, {"OBD DEVICE", "OBD 接头"}, {"NEARBY", "附近设备"}, {"SAVED", "已保存"}, {"SCAN", "扫描"},
    {"SCANNING...", "扫描中…"}, {"PAIRING...", "配对中…"}, {"SAVED - CONNECTED", "已保存 · 已连接"}, {"SAVED - CONNECTING...", "已保存 · 连接中…"},
    {"SAVED DEVICE REMOVED", "已删除保存的设备"}, {"TAP SCAN TO LOOK AGAIN", "点扫描重新寻找"}, {"FOUND %d", "找到 %d 个"},
    {"Connecting: %s", "正在连接：%s"}, {"Paired!", "配对成功"}, {"Pair failed, retrying...", "配对失败，正在重试…"},
    // gauge info
    {"GAUGE INFO", "仪表信息"}, {"MODE", "模式"}, {"STATUS", "状态"}, {"FIRMWARE", "固件"}, {"LINKED", "已联动"}, {"WAITING", "等待中"},
    {"NOT SET", "未设置"}, {"CONNECTED", "已连接"}, {"OFFLINE", "离线"},
    // theme delete
    {"DELETE THEME?", "删除主题？"}, {"Cancel", "取消"}, {"Delete", "删除"}, {"Deleted. Restarting...", "已删除，正在重启…"},
    {"Last theme: it stays", "最后一个主题，不能删除"}, {"Theme page %u of %u", "主题页 %u / %u"},
    // overlays and games
    {"NO SIGNAL", "无信号"}, {"DOOR OPEN", "车门未关"}, {"DOORS OPEN", "车门未关"}, {"TRUNK OPEN", "后备箱未关"},
    {"EXIT GAME?", "退出游戏？"}, {"PARK TO PLAY", "停车后才能玩"}, {"NOT ENOUGH MEMORY", "内存不足"}, {"HOLD 5 SEC TO EXIT", "长按 5 秒退出"},
    {"NO RECORD YET", "还没有纪录"}, {"NO GAMES YET\n\nINSTALL THEM IN THE\nRACING MY CAR APP\n(GAUGE > GAMES)", "还没有游戏\n\n请在 RACING MY CAR APP\n安装（仪表 > 游戏）"},
    {"BEST %ld%s%s", "最佳 %ld%s%s"},
};

uint8_t ui_lang_get(void) { return nvs_cfg_get()->language == UI_LANG_ZH ? UI_LANG_ZH : UI_LANG_EN; }
void ui_lang_set(uint8_t lang)
{
    nvs_user_cfg_t cfg = *nvs_cfg_get();
    cfg.language = lang == UI_LANG_ZH ? UI_LANG_ZH : UI_LANG_EN;
    nvs_cfg_set(&cfg);
}
const char *ui_tr(const char *en)
{
    if (!en || ui_lang_get() != UI_LANG_ZH) return en;
    for (size_t i = 0; i < sizeof(s_zh) / sizeof(s_zh[0]); i++) if (!strcmp(s_zh[i][0], en)) return s_zh[i][1];
    return en;
}
