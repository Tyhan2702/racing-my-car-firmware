// Date and time (see gauge_time.h). PCF85063 registers: 0x04 seconds (bit 7 = OS, the oscillator stopped / time
// lost), 0x05 minutes, 0x06 hours (24 h), 0x07 day, 0x08 weekday, 0x09 month, 0x0A year 00-99; all BCD. The RTC
// holds UTC; the phone's time-zone offset is kept in NVS ("rmc_ui"/"tz_min").

#include "gauge_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "esp_log.h"
#include "nvs.h"
#include "cJSON.h"
#include "bsp_obd_dsp/i2c_driver/I2C_Driver.h"
#include "bsp_obd_dsp/racechrono_ble_diy.h"

#define RTC_ADDR 0x51
static const char *TAG = "gauge_time";
static bool s_valid, s_rtc;
static int16_t s_zone = 480;     // until the phone tells: UTC+8 (Malaysia)

static uint8_t bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static uint8_t unbcd(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 15)); }

static int64_t days_from_civil(int y, unsigned m, unsigned d)   // days since 1970-01-01 (Howard Hinnant's algorithm)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static void load_zone(void)
{
    nvs_handle_t h;
    if (nvs_open("rmc_ui", NVS_READONLY, &h) == ESP_OK) { nvs_get_i16(h, "tz_min", &s_zone); nvs_close(h); }
}

void gauge_time_init(void)
{
    load_zone();
    uint8_t r[7];
    if (I2C_Read(RTC_ADDR, 0x04, r, sizeof(r)) != ESP_OK) { ESP_LOGW(TAG, "no RTC answer"); return; }
    s_rtc = true;
    if (r[0] & 0x80) { ESP_LOGI(TAG, "RTC lost power: waiting for a sync from the app"); return; }
    int year = 2000 + unbcd(r[6]), month = unbcd(r[5] & 0x1F), day = unbcd(r[3] & 0x3F);
    if (year < 2025 || month < 1 || month > 12 || day < 1 || day > 31) return;
    int64_t t = days_from_civil(year, (unsigned)month, (unsigned)day) * 86400 + unbcd(r[2] & 0x3F) * 3600 + unbcd(r[1] & 0x7F) * 60 + unbcd(r[0] & 0x7F);
    struct timeval tv = {.tv_sec = (time_t)t, .tv_usec = 0};
    settimeofday(&tv, NULL);
    s_valid = true;
    ESP_LOGI(TAG, "time from RTC: %04d-%02d-%02d %02d:%02d UTC", year, month, day, unbcd(r[2] & 0x3F), unbcd(r[1] & 0x7F));
}

bool gauge_time_valid(void) { return s_valid; }
bool gauge_time_rtc_found(void) { return s_rtc; }
int16_t gauge_time_zone_minutes(void) { return s_zone; }

bool gauge_time_local(struct tm *out)
{
    if (!s_valid) return false;
    time_t t = time(NULL) + (time_t)s_zone * 60;
    gmtime_r(&t, out);
    return true;
}

void gauge_time_set(int64_t utc, int16_t zone)
{
    if (utc < 1735689600LL) return;                  // before 2025: not a real phone clock
    if (zone < -720 || zone > 840) zone = 480;
    struct timeval tv = {.tv_sec = (time_t)utc, .tv_usec = 0};
    settimeofday(&tv, NULL);
    s_valid = true;
    if (zone != s_zone) {
        s_zone = zone;
        nvs_handle_t h;
        if (nvs_open("rmc_ui", NVS_READWRITE, &h) == ESP_OK) { nvs_set_i16(h, "tz_min", zone); nvs_commit(h); nvs_close(h); }
    }
    struct tm u;
    time_t t = (time_t)utc;
    gmtime_r(&t, &u);
    uint8_t r[7] = {bcd((uint8_t)u.tm_sec), bcd((uint8_t)u.tm_min), bcd((uint8_t)u.tm_hour), bcd((uint8_t)u.tm_mday),
                    (uint8_t)u.tm_wday, bcd((uint8_t)(u.tm_mon + 1)), bcd((uint8_t)(u.tm_year % 100))};   // OS bit cleared
    if (I2C_Write(RTC_ADDR, 0x04, r, sizeof(r)) == ESP_OK) s_rtc = true;
    racechrono_ble_diy_set_time_beacon(false);   // stop asking for the time
    ESP_LOGI(TAG, "time set from the phone (zone %+d min)", zone);
}

char *gauge_time_json(void)
{
    char *out = malloc(96);
    if (out) snprintf(out, 96, "{\"epoch\":%lld,\"tz\":%d,\"valid\":%s,\"rtc\":%s}", (long long)time(NULL), s_zone,
                      s_valid ? "true" : "false", s_rtc ? "true" : "false");
    return out;
}

bool gauge_time_set_json(const char *json)
{
    cJSON *o = json ? cJSON_Parse(json) : NULL, *e = o ? cJSON_GetObjectItem(o, "epoch") : NULL, *z = o ? cJSON_GetObjectItem(o, "tz") : NULL;
    bool ok = cJSON_IsNumber(e) && e->valuedouble > 1735689600.0;
    if (ok) gauge_time_set((int64_t)e->valuedouble, cJSON_IsNumber(z) ? (int16_t)z->valuedouble : s_zone);
    cJSON_Delete(o);
    return ok;
}
