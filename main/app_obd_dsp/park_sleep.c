// Parked sleep (see park_sleep.h).

#include "park_sleep.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/rtc_io.h"
#include "nvs.h"
#include "lvgl.h"
#include "obd_data_cache.h"
#include "ota_wifi_server.h"
#include "bsp_obd_dsp/nvs_storage.h"
#include "bsp_obd_dsp/lcd_driver/ST77916.h"
#include "bsp_obd_dsp/touch_driver/CST816.h"

#define IDLE_US        (180LL * 1000000)   // engine off and untouched this long -> sleep
#define CHECK_US       (15LL * 1000000)    // a silent (timer) wake looks for a running engine this long
#define WAKE_EVERY_US  (30ULL * 1000000)   // timer wake period while parked
#define CHARGING_MV    13200               // the alternator charges above this: the engine runs

static const char *TAG = "park";
static bool s_enabled, s_silent;
static int64_t s_off_since;

void park_init(void)
{
    uint8_t v = 0;
    nvs_handle_t h;
    if (nvs_open("rmc_ui", NVS_READONLY, &h) == ESP_OK) { nvs_get_u8(h, "park_sleep", &v); nvs_close(h); }
    s_enabled = v != 0;
    s_silent = s_enabled && esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
    if (s_silent) ESP_LOGI(TAG, "timer wake: screen stays dark until the engine runs");
}

bool park_silent(void) { return s_silent; }
bool park_enabled(void) { return s_enabled; }

void park_set_enabled(bool on)
{
    s_enabled = on;
    s_off_since = 0;
    nvs_handle_t h;
    if (nvs_open("rmc_ui", NVS_READWRITE, &h) == ESP_OK) { nvs_set_u8(h, "park_sleep", on ? 1 : 0); nvs_commit(h); nvs_close(h); }
}

static bool engine_running(void)
{
    obd_data_snapshot_t o;
    obd_data_get_snapshot(&o);
    return o.rpm > 0 || o.bat_mv >= CHARGING_MV;
}

static void sleep_now(void)
{
    ESP_LOGI(TAG, "parked: deep sleep (touch or %llus timer wakes)", WAKE_EVERY_US / 1000000);
    Set_Backlight(0);
    rtc_gpio_pullup_en((gpio_num_t)I2C_Touch_INT_IO);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)I2C_Touch_INT_IO, 0);   // the touch controller pulls INT low on a touch
    esp_sleep_enable_timer_wakeup(WAKE_EVERY_US);
    esp_deep_sleep_start();
}

void park_tick(void)
{
    if (!s_enabled) return;
    int64_t now = esp_timer_get_time();
    if (s_silent) {
        if (engine_running()) {                                   // the car was started: show the gauge
            s_silent = false;
            uint8_t b = nvs_cfg_get()->brightness_day;
            Set_Backlight(b < 10 ? 100 : b);
            ESP_LOGI(TAG, "engine running: screen on");
        } else if (now > CHECK_US) {
            sleep_now();
        }
        return;
    }
    if (engine_running() || lv_disp_get_inactive_time(NULL) < IDLE_US / 1000 || ota_wifi_server_is_busy() ||
        ota_wifi_server_get_state() != OTA_WIFI_STATE_IDLE) {
        s_off_since = 0;
        return;
    }
    if (!s_off_since) s_off_since = now;
    else if (now - s_off_since > IDLE_US) sleep_now();
}
