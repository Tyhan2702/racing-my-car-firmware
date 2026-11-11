// See rtc.h. Ported from dash4 rtc.c. A PCF85063 at 0x51 on the shared I2C bus is read when it answers (2.1" board);
// without one the time comes from the phone's 0x000C or the 主仪表's 'T' frame, until the gauge restarts.
// The time zone (minutes from UTC) is kept in NVS ui/tz.
#include <string.h>
#include <sys/time.h>
#include "esp_log.h"
#include "nvs.h"
#include "driver/i2c_master.h"
#include "bsp_obd_dsp/i2c_driver/I2C_Driver.h"
#include "bsp_obd_dsp/rtc.h"

#define VALID 1704067200                      // 2024-01-01: before it the clock was never set
static i2c_master_dev_handle_t dev;
static int tz;                                // minutes east of UTC

static int bcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }
static uint8_t tobcd(int v) { return (uint8_t)((v / 10) << 4 | v % 10); }

void rtc_start(void)
{
    nvs_handle_t h; int16_t z = 0;
    if (nvs_open("ui", NVS_READONLY, &h) == ESP_OK) { nvs_get_i16(h, "tz", &z); nvs_close(h); }
    tz = z;
    i2c_master_bus_handle_t bus = I2C_GetBusHandle();
    if (!bus || i2c_master_probe(bus, 0x51, 50) != ESP_OK) { ESP_LOGI("rtc", "no RTC chip: the time comes from the phone"); return; }
    i2c_device_config_t c = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x51, .scl_speed_hz = 400000};
    if (i2c_master_bus_add_device(bus, &c, &dev) != ESP_OK) { dev = NULL; return; }
    uint8_t reg = 0x04, b[7];                 // PCF85063: seconds (bit 7: the clock stopped), minutes, hours, day, weekday, month, year
    if (i2c_master_transmit_receive(dev, &reg, 1, b, 7, 50) != ESP_OK) { i2c_master_bus_rm_device(dev); dev = NULL; return; }
    if (b[0] & 0x80) { ESP_LOGW("rtc", "PCF85063 lost its time"); return; }
    struct tm t = {.tm_sec = bcd(b[0] & 0x7f), .tm_min = bcd(b[1] & 0x7f), .tm_hour = bcd(b[2] & 0x3f), .tm_mday = bcd(b[3] & 0x3f),
                   .tm_mon = bcd(b[5] & 0x1f) - 1, .tm_year = bcd(b[6]) + 100};
    time_t s = mktime(&t);                    // the chip keeps UTC (the gauge never sets TZ: mktime is UTC)
    if (s > VALID) { struct timeval tv = {.tv_sec = s}; settimeofday(&tv, NULL); }
    ESP_LOGI("rtc", "PCF85063: %lld", (long long)s);
}

void rtc_set(int64_t unix_s, int tz_min)
{
    if (unix_s < VALID || tz_min < -14 * 60 || tz_min > 14 * 60) return;
    struct timeval tv = {.tv_sec = (time_t)unix_s}; settimeofday(&tv, NULL);
    if (tz != tz_min) { tz = tz_min; nvs_handle_t h; if (nvs_open("ui", NVS_READWRITE, &h) == ESP_OK) { nvs_set_i16(h, "tz", (int16_t)tz); nvs_commit(h); nvs_close(h); } }
    if (!dev) return;
    time_t s = (time_t)unix_s; struct tm t; gmtime_r(&s, &t);
    uint8_t b[8] = {0x04, tobcd(t.tm_sec), tobcd(t.tm_min), tobcd(t.tm_hour), tobcd(t.tm_mday), (uint8_t)t.tm_wday, tobcd(t.tm_mon + 1), tobcd(t.tm_year % 100)};
    i2c_master_transmit(dev, b, sizeof(b), 50);
}

int64_t rtc_unix(void) { time_t s = time(NULL); return s > VALID ? s : 0; }
bool rtc_local(int64_t unix_s, struct tm *out)
{
    if (unix_s < VALID) return false;
    time_t s = (time_t)(unix_s + tz * 60); gmtime_r(&s, out); return true;
}
int rtc_tz(void) { return tz; }
bool rtc_chip(void) { return dev; }
