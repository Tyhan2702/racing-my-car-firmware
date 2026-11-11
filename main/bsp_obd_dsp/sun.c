// See sun.h. Ported from the 7"/4" dashes' sun.c. The board has no light sensor, so day/night is from 日出日落:
// night is the sun lower than 3° under the horizon (dusk), at the place last known (GPS, or the 主仪表's over
// 多仪表共享); never known: night 19:15 to 07:00 local time (Malaysia's dusk and dawn, near the same all year).
#include <math.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "bsp_obd_dsp/rtc.h"
#include "bsp_obd_dsp/sun.h"
#include "bsp_obd_dsp/nvs_storage.h"

void Set_Backlight(uint8_t Light);            // lcd_driver (ST7701.h / ST77916.h)
static int ui_bright(void) { uint8_t b = nvs_cfg_get()->brightness_day; return b < 10 ? 100 : b; }   // 屏幕亮度 (白天)
static void ui_bright_apply(int v) { Set_Backlight((uint8_t)v); }

static bool on, placed;
static volatile bool lit = true;
static int night_v = 30, shown = -1;          // shown: the night state last applied
static double la, lo;

static void save_i32(const char *k, int32_t v) { nvs_handle_t h; if (nvs_open("ui", NVS_READWRITE, &h) == ESP_OK) { nvs_set_i32(h, k, v); nvs_commit(h); nvs_close(h); } }
static void apply(void) { if (lit) ui_bright_apply(ui_bright_now()); }

double sun_height(int64_t unix_s, double lat, double lon)
{
    const double R = M_PI / 180, d = unix_s / 86400.0 - 10957.5;      // days from J2000 (2000-01-01 12:00 UTC)
    double g = (357.529 + 0.98560028 * d) * R, q = 280.459 + 0.98564736 * d;
    double L = (q + 1.915 * sin(g) + 0.020 * sin(2 * g)) * R, e = (23.439 - 0.00000036 * d) * R;
    double ra = atan2(cos(e) * sin(L), cos(L)), dec = asin(sin(e) * sin(L));
    double ha = fmod(280.46061837 + 360.98564736629 * d, 360) * R + lon * R - ra;   // the sun's hour angle here
    return asin(sin(lat * R) * sin(dec) + cos(lat * R) * cos(dec) * cos(ha)) / R;
}

int sun_night(void)
{
    int64_t t = rtc_unix(); if (!t) return -1;
    if (placed) return sun_height(t, la, lo) < -3;
    struct tm m; rtc_local(t, &m); int min = m.tm_hour * 60 + m.tm_min;
    return min >= 19 * 60 + 15 || min < 7 * 60;
}
int ui_bright_now(void) { return on && sun_night() == 1 ? night_v : ui_bright(); }

bool sun_auto(void) { return on; }
void sun_auto_set(bool v) { on = v; save_i32("abri", v); shown = -1; apply(); }
int sun_night_bright(void) { return night_v; }
void sun_night_bright_set(int v) { night_v = v < 10 ? 10 : v > 100 ? 100 : v; save_i32("bnight", night_v); }
void sun_screen(bool v) { lit = v; }
bool sun_place(double *lat, double *lon) { if (placed) { *lat = la; *lon = lo; } return placed; }
void sun_place_set(double lat, double lon)
{
    if (!(fabs(lat) <= 90 && fabs(lon) <= 180) || (lat == 0 && lon == 0)) return;
    if (placed && fabs(lat - la) < 0.2 && fabs(lon - lo) < 0.2) return;   // ~20 km: the sun moves a minute at most
    la = lat; lo = lon; placed = true;
    save_i32("lat", (int32_t)lround(lat * 1e4)); save_i32("lon", (int32_t)lround(lon * 1e4));
}

static void task(void *a)
{
    for (;;) {
        int n = sun_night();
        if (on && n != shown) { shown = n; apply(); }                // only when day turns night (a slider stays as moved)
        vTaskDelay(pdMS_TO_TICKS(20000));
    }
}
void sun_start(void)
{
    nvs_handle_t h; int32_t a = 0, b = 30, x = 0, y = 0;
    if (nvs_open("ui", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, "abri", &a); nvs_get_i32(h, "bnight", &b);
        if (nvs_get_i32(h, "lat", &x) == ESP_OK && nvs_get_i32(h, "lon", &y) == ESP_OK) { la = x / 1e4; lo = y / 1e4; placed = true; }
        nvs_close(h);
    }
    on = a; night_v = b < 10 ? 10 : b > 100 ? 100 : b;
    xTaskCreate(task, "sun", 3072, NULL, 2, NULL);
}
