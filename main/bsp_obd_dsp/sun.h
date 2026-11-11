// 自动亮度 (sun.c): 屏幕亮度 (白天) by day, 夜间亮度 after dusk, from the sun at the gauge's place and time.
// Ported from the 7"/4" dashes' sun.c (same algorithm), adapted to the fw-fork brightness (Set_Backlight / brightness_day).
#pragma once
#include <stdbool.h>
#include <stdint.h>
void sun_start(void);                         // after rtc_start(): loads the settings, checks every 20 s
bool sun_auto(void);                          // 自动亮度 on (NVS ui/abri)
void sun_auto_set(bool on);
int sun_night_bright(void);                   // 夜间亮度, 10…100 (NVS ui/bnight)
void sun_night_bright_set(int v);
void sun_place_set(double lat, double lon);   // the GPS's or the 主仪表's (NVS ui/lat ui/lon, kept when it moved)
bool sun_place(double *lat, double *lon);
double sun_height(int64_t unix_s, double lat, double lon);   // the sun over the horizon, degrees
int sun_night(void);                          // 1 night, 0 day, -1 the time is not known (no phone, no 主仪表 since the start)
int ui_bright_now(void);                      // what the screen shows now: 夜间亮度 at night with 自动亮度 on, else 屏幕亮度
void sun_screen(bool lit);                    // the backlight on / off (off is brightness 0: never lit from here)
