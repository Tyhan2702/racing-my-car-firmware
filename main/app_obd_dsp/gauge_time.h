#pragma once
// Date and time for the clock faces. Kept by the board's PCF85063 RTC (I2C 0x51) while the gauge has power, and set
// from the phone whenever the Racing My Car app or platform connects (BLE control "OTA1"+5, or POST /ota/time),
// with the phone's time zone. The RTC shares the gauge's supply: after the gauge was off the RTC reports lost time
// (its OS flag) and the clock faces ask for a sync instead of showing a wrong time.

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

void gauge_time_init(void);                       // at boot, after I2C_Init: RTC -> system clock when it kept time
bool gauge_time_valid(void);                      // the clock is right (RTC kept time, or set since boot)
bool gauge_time_rtc_found(void);                  // the PCF85063 answered on the I2C bus
bool gauge_time_local(struct tm *out);            // local time (phone's time zone); false while not valid
int16_t gauge_time_zone_minutes(void);            // offset from UTC, e.g. 480 for Malaysia
void gauge_time_set(int64_t utc_seconds, int16_t zone_minutes);   // from the phone: system clock, RTC and zone
char *gauge_time_json(void);                      // {"epoch":…,"tz":…,"valid":…,"rtc":…} (malloc'd)
bool gauge_time_set_json(const char *json);       // {"epoch":…,"tz":…}
