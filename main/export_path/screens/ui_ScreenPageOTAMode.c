// OTA Mode Page
// Shows the device ready for OTA update, with SoftAP + HTTP server running.
// The companion app connects via WiFi (password: 88888888), reads device info
// from GET /ota/info, then uploads firmware/bootmedia via HTTP POST.
// No BLE involvement.

#include "../ui.h"
#include "../ui_ext.h"
#include "ui_rmc_style.h"
#include "app_obd_dsp/device_identity.h"
#include "app_obd_dsp/ota_wifi_server.h"
#include "bsp_obd_dsp/rs485_brake_temp.h"
#include "bsp_obd_dsp/elm327_ble_client.h"
#include "bsp_obd_dsp/espnow_link.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

#ifndef OBD_GAUGE_BUILD_TAG
#define OBD_GAUGE_BUILD_TAG "unknown"
#endif

static const char *TAG = "ota_mode";

// UI elements exposed for timer updates
lv_obj_t *ui_ScreenPageOTAMode = NULL;
lv_obj_t *ui_LabelOTAModeStatus = NULL;
lv_obj_t *ui_LabelOTAModeVersion = NULL;

static void ui_event_ota_mode_background(lv_event_t *e);
static lv_obj_t *s_ssid;   // the hotspot name, filled in once Wi-Fi is up

void ui_ScreenPageOTAMode_screen_init(void)
{
    // Racing My Car look (ui_rmc_style.h): title, busy ring, the hotspot in a card, the state under it
    lv_obj_t *scr = ui_ScreenPageOTAMode = lv_obj_create(NULL);
    rmc_screen(scr);
    rmc_title(scr, "WI-FI UPDATE", 52);
    lv_obj_t *busy = rmc_spinner(scr, 34);
    lv_obj_align(busy, LV_ALIGN_TOP_MID, 0, 92);

    lv_obj_t *card = rmc_card(scr, RMC_CARD_W, 82);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 10);
    s_ssid = rmc_row(card, "WI-FI", 14);
    lv_label_set_text(s_ssid, "OBD-Gauge-OTA");
    lv_obj_t *pw = rmc_row(card, "PASSWORD", 46);
    lv_label_set_text(pw, "88888888");
    lv_obj_set_style_text_color(pw, lv_color_hex(RMC_YELLOW), 0);

    ui_LabelOTAModeStatus = lv_label_create(scr);
    lv_label_set_text(ui_LabelOTAModeStatus, "STARTING WI-FI...");
    lv_obj_set_style_text_font(ui_LabelOTAModeStatus, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(ui_LabelOTAModeStatus, lv_color_hex(RMC_DIM), 0);
    lv_obj_set_style_text_align(ui_LabelOTAModeStatus, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ui_LabelOTAModeStatus, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui_LabelOTAModeStatus, 270);
    lv_obj_align(ui_LabelOTAModeStatus, LV_ALIGN_CENTER, 0, 74);

    ui_LabelOTAModeVersion = lv_label_create(scr);   // the build, small: the app checks it over Wi-Fi anyway
    lv_label_set_text(ui_LabelOTAModeVersion, OBD_GAUGE_BUILD_TAG);
    lv_label_set_long_mode(ui_LabelOTAModeVersion, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(ui_LabelOTAModeVersion, 200);
    lv_obj_set_style_text_font(ui_LabelOTAModeVersion, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(ui_LabelOTAModeVersion, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_align(ui_LabelOTAModeVersion, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui_LabelOTAModeVersion, LV_ALIGN_BOTTOM_MID, 0, -64);
    rmc_hint(scr, "SWIPE TO EXIT");

    lv_obj_add_event_cb(scr, ui_event_ota_mode_background, LV_EVENT_ALL, NULL);

    rs485_brake_temp_pause();

    // Drop the ELM327 BLE link for the duration of the OTA: the GATT client keeps
    // re-scanning/reconnecting in the background and steals the single 2.4GHz
    // radio from the SoftAP, stalling the upload mid-transfer.
    elm327_ble_pause_for_ota();

    // Give the single 2.4 GHz radio to the OTA SoftAP. Keep the WiFi driver
    // itself alive (the SoftAP reuses it), but stop the ESP-NOW broadcast /
    // presence tasks and deinit only ESP-NOW. Otherwise the master continues a
    // broadcast every 100 ms while TCP is trying to fill the small RX pool.
    espnow_link_pause_for_ota();

    // The RaceChrono advert also periodically takes the radio away from WiFi.
    // OTA mode does not need BLE, and all exit paths reboot, so release it fully.
    ota_wifi_server_release_bt();

    // Start WiFi SoftAP + HTTP server immediately (no BLE handshake needed)
    ESP_LOGI(TAG, "Starting WiFi OTA server from OTA mode screen");
    ota_wifi_info_t info = {0};
    bool started = ota_wifi_server_start(&info, NULL);
    if (started && info.ssid[0] && s_ssid) lv_label_set_text(s_ssid, info.ssid);   // the real name, not OBD-Gauge-OTA-xxxx
    if (!started) {
        ESP_LOGE(TAG, "Failed to start WiFi OTA server");
        rs485_brake_temp_resume();
        if (ui_LabelOTAModeStatus) {
            lv_label_set_text(ui_LabelOTAModeStatus, "WI-FI DID NOT START");
            lv_obj_set_style_text_color(ui_LabelOTAModeStatus, lv_color_hex(0xFF453A), 0);
        }
    }
}


// Leaving OTA mode: entering it released the BT controller and quiesced
// ESP-NOW. That is irreversible for this session, so BLE — and with it the OBD
// link — can only come back through a reset.
// Uploads already end in esp_restart(); this covers the "looked and swiped away"
// case so the gauge is never left BLE-dead.
static void exit_ota_mode(void)
{
    ESP_LOGI(TAG, "Exiting OTA mode, stopping WiFi server");
    ota_wifi_server_stop();
    rs485_brake_temp_resume();

    ESP_LOGI(TAG, "Rebooting to restore BLE (BT controller was released for the OTA radio)");
    // Give the WiFi teardown and the log line a moment to drain.
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_restart();
}

// Any swipe exits OTA mode.  Both former destinations (EasterEgg / Settings)
// are replaced by a reboot, which lands on the normal boot flow.
static void ui_event_ota_mode_background(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT ||
            dir == LV_DIR_TOP  || dir == LV_DIR_BOTTOM) {
            lv_indev_wait_release(lv_indev_get_act());
            exit_ota_mode();
        }
    }

    ui_ext_tick();
}
