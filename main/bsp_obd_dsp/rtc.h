// The gauge's clock (rtc.c): set from the phone over Bluetooth (racechrono_ble_diy.c 0x000C) or the 主仪表's 'T'
// share frame; kept by a PCF85063 RTC on the I2C bus when the board has one (2.1" @0x51), else only until restart.
// Ported from dash4/dash7 rtc.c, adapted to the fw-fork I2C driver (I2C_GetBusHandle).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
void rtc_start(void);                         // after I2C_Init(): reads the chip into the system time
void rtc_set(int64_t unix_s, int tz_min);     // the phone's time (UTC seconds) and its offset from UTC in minutes
int64_t rtc_unix(void);                       // UTC seconds, 0 while the time is not known
bool rtc_local(int64_t unix_s, struct tm *out);   // unix_s in the phone's time zone; false for 0
int rtc_tz(void);                             // minutes east of UTC (the phone's)
bool rtc_chip(void);                          // a RTC chip was found
