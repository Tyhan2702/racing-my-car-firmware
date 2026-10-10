// BLE Scan & Select Page
// Shows saved device (with delete) + a list of discovered BLE devices
//
// Two use cases depending on device_role:
//  - MASTER/STANDALONE: scan and connect to an OBD ELM327 adapter (original logic unchanged)
//  - SLAVE: scan and pair with the triple-gauge master ("RMC - 1.85 Gauge XXYY" broadcast), see gauge_pair_ble_client.c

#include "../ui.h"
#include "ui_menu.h"
#include "ui_rmc_style.h"
#include "bsp_obd_dsp/elm327_ble_client.h"
#include "bsp_obd_dsp/gauge_pair_ble_client.h"
#include "bsp_obd_dsp/espnow_link.h"
#include "bsp_obd_dsp/nvs_storage.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG_BLE_UI = "ble_scan_ui";

// UI elements (local)
static lv_obj_t *s_list = NULL;             // scanned device list
static lv_obj_t *s_label_status = NULL;     // status label
static lv_obj_t *s_spinner = NULL;          // scan spinner
static lv_obj_t *s_saved_panel = NULL;      // saved device panel
static lv_obj_t *s_label_saved_hdr = NULL;  // "SAVED" sub-header
static lv_obj_t *s_saved_name_lbl = NULL;   // saved device name label
static bool s_scanning = false;
static bool s_slave_mode = false;           // true=slave pairing with a master, false=OBD device scan (original logic)

// Slave mode: parallel table mapping scan list button index -> corresponding device MAC (one-to-one with the s_list child order)
static uint8_t s_gauge_macs[GAUGE_PAIR_SCAN_MAX_DEVICES][6];
static int s_gauge_mac_count = 0;

// OBD device scan: same as above, records the MAC of each list item; after selection connects by exact MAC (avoids misconnecting to a same-name device)
static uint8_t s_obd_macs[BLE_SCAN_MAX_DEVICES][6];
static int s_obd_mac_count = 0;

// Forward declarations
static void start_scan(void);
static void on_device_selected(lv_event_t *e);
static void on_saved_device_delete(lv_event_t *e);
static void on_pair_result(bool ok, const char *name, const uint8_t mac[6]);

// A found device: a dark rounded row like the SETTINGS cards, outlined in RMC yellow while pressed
static void style_item(lv_obj_t *btn)
{
    lv_obj_set_height(btn, 46);
    lv_obj_set_style_bg_color(btn, lv_color_hex(RMC_CARD), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 2, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(RMC_YELLOW), LV_STATE_PRESSED);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_FULL, LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(btn, 16, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(btn, &ui_font_FontTypoderSize16, 0);
    lv_obj_t *l = lv_obj_get_child(btn, 0);
    if (l) lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
}

// Mutex for LVGL (defined in main)
extern SemaphoreHandle_t lvgl_mux;
static inline bool lvgl_lock_ui(int timeout_ms) {
    return xSemaphoreTake(lvgl_mux, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}
static inline void lvgl_unlock_ui(void) {
    xSemaphoreGive(lvgl_mux);
}

// BLE scan callback (called in the BT thread, LVGL must be updated thread-safely) -- OBD device scan (MASTER/STANDALONE)
static void scan_result_cb(const ble_scan_result_t *dev, int total_count) {
    if (!s_list) return;

    if (lvgl_lock_ui(100)) {
        // Check whether a device with the same name is already in the list
        uint32_t child_cnt = lv_obj_get_child_cnt(s_list);
        for (uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t *btn = lv_obj_get_child(s_list, i);
            lv_obj_t *lbl = lv_obj_get_child(btn, 0);
            if (lbl && strcmp(lv_label_get_text(lbl), dev->name) == 0) {
                lvgl_unlock_ui();
                return; // already exists
            }
        }
        if (s_obd_mac_count >= BLE_SCAN_MAX_DEVICES) {
            lvgl_unlock_ui();
            return;
        }

        // Add new device button
        lv_obj_t *btn = lv_list_add_btn(s_list, NULL, dev->name);
        style_item(btn);
        lv_obj_add_event_cb(btn, on_device_selected, LV_EVENT_CLICKED, NULL);

        memcpy(s_obd_macs[s_obd_mac_count], dev->addr, 6);
        s_obd_mac_count++;

        lv_label_set_text_fmt(s_label_status, "FOUND %d", total_count);
        lvgl_unlock_ui();
    }
}

// BLE scan callback -- slave pairing with a master (SLAVE); only devices with the "RMC - 1.85 Gauge" (or older "SkyGauge" prefix are received (see the filter in gauge_pair_ble_client.c)
static void scan_result_cb_gauge(const gauge_pair_scan_result_t *dev, int total_count) {
    if (!s_list) return;

    if (lvgl_lock_ui(100)) {
        uint32_t child_cnt = lv_obj_get_child_cnt(s_list);
        for (uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t *btn = lv_obj_get_child(s_list, i);
            lv_obj_t *lbl = lv_obj_get_child(btn, 0);
            if (lbl && strcmp(lv_label_get_text(lbl), dev->name) == 0) {
                lvgl_unlock_ui();
                return; // already exists
            }
        }
        if (s_gauge_mac_count >= GAUGE_PAIR_SCAN_MAX_DEVICES) {
            lvgl_unlock_ui();
            return;
        }

        lv_obj_t *btn = lv_list_add_btn(s_list, NULL, dev->name);
        style_item(btn);
        lv_obj_add_event_cb(btn, on_device_selected, LV_EVENT_CLICKED, NULL);

        memcpy(s_gauge_macs[s_gauge_mac_count], dev->addr, 6);
        s_gauge_mac_count++;

        lv_label_set_text_fmt(s_label_status, "FOUND %d", total_count);
        lvgl_unlock_ui();
    }
}

// A device was tapped/selected
static void on_device_selected(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (!lbl) return;

    const char *name = lv_label_get_text(lbl);

    if (s_slave_mode) {
        uint32_t idx = lv_obj_get_index(btn);
        if (idx >= (uint32_t)s_gauge_mac_count) return;
        uint8_t mac[6];
        memcpy(mac, s_gauge_macs[idx], 6);

        ESP_LOGI(TAG_BLE_UI, "Selected master: %s", name);
        gauge_pair_ble_scan_stop();
        s_scanning = false;

        lv_label_set_text(s_label_status, "PAIRING...");
        if (s_spinner) lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
        gauge_pair_ble_connect(mac, name, on_pair_result);
        return;
    }

    ESP_LOGI(TAG_BLE_UI, "Selected BLE device: %s", name);

    uint32_t idx = lv_obj_get_index(btn);
    if (idx >= (uint32_t)s_obd_mac_count) return;
    uint8_t mac[6];
    memcpy(mac, s_obd_macs[idx], 6);

    elm327_ble_scan_only_stop();
    s_scanning = false;

    nvs_user_cfg_t cfg = *nvs_cfg_get();
    strncpy(cfg.ble_device_name, name, sizeof(cfg.ble_device_name) - 1);
    cfg.ble_device_name[sizeof(cfg.ble_device_name) - 1] = '\0';
    memcpy(cfg.ble_obd_mac, mac, 6);
    cfg.protocol = 0;   // a new adapter (maybe on another car): look for the protocol again
    nvs_cfg_set(&cfg);

    // Refresh the saved device panel immediately
    if (s_saved_name_lbl) lv_label_set_text(s_saved_name_lbl, name);
    if (s_saved_panel)    lv_obj_clear_flag(s_saved_panel,    LV_OBJ_FLAG_HIDDEN);
    if (s_label_saved_hdr) lv_obj_clear_flag(s_label_saved_hdr, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text_fmt(s_label_status, "Connecting: %s", name);
    if (s_spinner) lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);

    elm327_ble_connect_by_addr(mac, name);
    ui_menu_go_home_later(800);   // back to the theme (never the firmware's own pages)
}

// BLE pairing result callback (called in the BT task context, lvgl_lock required)
static void on_pair_result(bool ok, const char *name, const uint8_t mac[6]) {
    if (!lvgl_lock_ui(200)) return;

    if (ok) {
        espnow_link_bind_master(mac);

        nvs_user_cfg_t cfg = *nvs_cfg_get();
        strncpy(cfg.ble_device_name, name ? name : "", sizeof(cfg.ble_device_name) - 1);
        cfg.ble_device_name[sizeof(cfg.ble_device_name) - 1] = '\0';
        nvs_cfg_set(&cfg);
        ESP_LOGI(TAG_BLE_UI, "Paired with master: %s", cfg.ble_device_name);

        if (s_saved_name_lbl) lv_label_set_text(s_saved_name_lbl, cfg.ble_device_name);
        if (s_saved_panel)    lv_obj_clear_flag(s_saved_panel,    LV_OBJ_FLAG_HIDDEN);
        if (s_label_saved_hdr) lv_obj_clear_flag(s_label_saved_hdr, LV_OBJ_FLAG_HIDDEN);

        lv_label_set_text(s_label_status, "Paired!");
        ui_menu_go_home_later(800);
    } else {
        ESP_LOGW(TAG_BLE_UI, "Pairing failed, rescanning");
        lv_label_set_text(s_label_status, "Pair failed, retrying...");
        // When the native BLE scan window (15s) expires, no callback notifies here, so s_scanning stays true.
        // This is a place where a forced rescan is intended, so reset it before calling to avoid being blocked by the dedup check in start_scan().
        s_scanning = false;
        start_scan();
    }

    lvgl_unlock_ui();
}

// Delete the saved device
static void on_saved_device_delete(lv_event_t *e) {
    if (s_slave_mode) {
        espnow_link_unbind_master();
        nvs_user_cfg_t cfg = *nvs_cfg_get();
        cfg.ble_device_name[0] = '\0';
        nvs_cfg_set(&cfg);
        ESP_LOGI(TAG_BLE_UI, "Unbound saved master");
    } else {
        // Forget the adapter in the BLE client too (otherwise it reconnects to it right away), drop the link
        elm327_ble_forget_target();
        nvs_user_cfg_t cfg = *nvs_cfg_get();
        cfg.ble_device_name[0] = '\0';
        memset(cfg.ble_obd_mac, 0, sizeof(cfg.ble_obd_mac));
        nvs_cfg_set(&cfg);
        ESP_LOGI(TAG_BLE_UI, "Saved BLE device cleared");
    }

    if (s_saved_panel)    lv_obj_add_flag(s_saved_panel,    LV_OBJ_FLAG_HIDDEN);
    if (s_label_saved_hdr) lv_obj_add_flag(s_label_saved_hdr, LV_OBJ_FLAG_HIDDEN);
    if (s_label_status)   lv_label_set_text(s_label_status, "SAVED DEVICE REMOVED");

    s_scanning = false;   // the native scan window expiry does not reset via callback; reset before forcing a rescan
    start_scan();         // list nearby devices again so a new one can be picked straight away
}

static uint32_t s_scan_started_ms;
static lv_timer_t *s_page_timer;

static void start_scan(void) {
    if (s_scanning) return;
    s_scanning = true;
    s_scan_started_ms = lv_tick_get();

    if (s_list) lv_obj_clean(s_list);
    if (s_label_status) lv_label_set_text(s_label_status, "SCANNING...");
    if (s_spinner) lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);

    if (s_slave_mode) {
        s_gauge_mac_count = 0;
        gauge_pair_ble_scan_start(15, scan_result_cb_gauge);
    } else {
        s_obd_mac_count = 0;
        elm327_ble_scan_only_start(15, scan_result_cb);
    }
}

// Saved device row from NVS (the page is built once and shown again later, so it is refreshed on every visit).
static void refresh_saved(void)
{
    if (s_slave_mode) return;
    const nvs_user_cfg_t *cfg = nvs_cfg_get();
    bool has = cfg->ble_device_name[0] != '\0';
    if (s_saved_name_lbl) lv_label_set_text(s_saved_name_lbl, has ? cfg->ble_device_name : "");
    if (s_saved_panel) { if (has) lv_obj_clear_flag(s_saved_panel, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s_saved_panel, LV_OBJ_FLAG_HIDDEN); }
    if (s_label_saved_hdr) { if (has) lv_obj_clear_flag(s_label_saved_hdr, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s_label_saved_hdr, LV_OBJ_FLAG_HIDDEN); }
}

// Twice a second: whether the saved adapter is connected, and the end of the 15 s list scan.
static void page_tick(lv_timer_t *t)
{
    (void)t;
    if (lv_scr_act() != ui_ScreenPageBLEScan) return;
    if (!s_slave_mode && s_label_saved_hdr && nvs_cfg_get()->ble_device_name[0] != '\0')
        { bool on = elm327_ble_is_connected(); lv_label_set_text(s_label_saved_hdr, on ? "SAVED - CONNECTED" : "SAVED - CONNECTING..."); lv_obj_set_style_text_color(s_label_saved_hdr, lv_color_hex(on ? RMC_GREEN : RMC_DIM), 0); }
    if (s_scanning && lv_tick_elaps(s_scan_started_ms) > 15500) {
        s_scanning = false;
        if (!s_slave_mode) elm327_ble_scan_only_stop();
        if (s_spinner) lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
        if (s_label_status) lv_label_set_text(s_label_status, "TAP SCAN TO LOOK AGAIN");
    }
}

// Every visit: show the saved device as it is now and list the devices nearby again.
static void on_page_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SCREEN_LOAD_START) {
        refresh_saved();
        s_scanning = false;
        start_scan();
    } else if (code == LV_EVENT_DELETE && s_page_timer) {
        lv_timer_del(s_page_timer);
        s_page_timer = NULL;
    }
}

static void on_rescan(lv_event_t *e)
{
    (void)e;
    if (!s_slave_mode) elm327_ble_scan_only_stop();
    s_scanning = false;
    start_scan();
}

void ui_ScreenPageBLEScan_screen_init(void)
{
    s_slave_mode = (nvs_cfg_get()->device_role == ESPNOW_ROLE_SLAVE);

    // Racing My Car look (ui_rmc_style.h): title, the saved device in a card, then the devices nearby
    lv_obj_t *scr = ui_ScreenPageBLEScan = lv_obj_create(NULL);
    rmc_screen(scr);
    rmc_title(scr, s_slave_mode ? "FIND MASTER" : "OBD DEVICE", 36);

    s_label_status = lv_label_create(scr);
    lv_label_set_text(s_label_status, "SCANNING...");
    lv_obj_set_style_text_font(s_label_status, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(s_label_status, lv_color_hex(RMC_DIM), 0);
    lv_obj_align(s_label_status, LV_ALIGN_TOP_MID, 0, 66);

    // ==== SAVED DEVICE ====
    const nvs_user_cfg_t *saved_cfg = nvs_cfg_get();
    bool has_saved;
    if (s_slave_mode) {
        const uint8_t *bound_mac = espnow_link_get_bound_master_mac();
        has_saved = (bound_mac[0] | bound_mac[1] | bound_mac[2] | bound_mac[3] | bound_mac[4] | bound_mac[5]) != 0;
    } else {
        has_saved = (saved_cfg->ble_device_name[0] != '\0');
    }
    s_saved_panel = rmc_card(scr, RMC_CARD_W, 62);
    lv_obj_align(s_saved_panel, LV_ALIGN_TOP_MID, 0, 92);
    s_label_saved_hdr = lv_label_create(s_saved_panel);
    lv_label_set_text(s_label_saved_hdr, "SAVED");
    lv_obj_set_style_text_font(s_label_saved_hdr, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(s_label_saved_hdr, lv_color_hex(RMC_DIM), 0);
    lv_obj_align(s_label_saved_hdr, LV_ALIGN_TOP_LEFT, 0, 8);
    s_saved_name_lbl = lv_label_create(s_saved_panel);
    lv_label_set_text(s_saved_name_lbl, has_saved ? saved_cfg->ble_device_name : "");
    lv_label_set_long_mode(s_saved_name_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_saved_name_lbl, RMC_CARD_W - 32 - 48);
    lv_obj_set_style_text_font(s_saved_name_lbl, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(s_saved_name_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_saved_name_lbl, LV_ALIGN_BOTTOM_LEFT, 0, -9);
    lv_obj_t *del_btn = lv_obj_create(s_saved_panel);                // forget it: a round grey x
    lv_obj_remove_style_all(del_btn);
    lv_obj_set_size(del_btn, 38, 38);
    lv_obj_set_style_radius(del_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(del_btn, lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_bg_color(del_btn, lv_color_hex(0xFF453A), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(del_btn, LV_OPA_COVER, 0);
    lv_obj_add_flag(del_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(del_btn, 8);
    lv_obj_align(del_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *del_lbl = lv_label_create(del_btn);
    lv_label_set_text(del_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(del_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(del_lbl);
    lv_obj_add_event_cb(del_btn, on_saved_device_delete, LV_EVENT_CLICKED, NULL);
    if (!has_saved) { lv_obj_add_flag(s_saved_panel, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s_label_saved_hdr, LV_OBJ_FLAG_HIDDEN); }

    // ==== NEARBY ====
    lv_obj_t *label_nearby = lv_label_create(scr);
    lv_label_set_text(label_nearby, "NEARBY");
    lv_obj_set_style_text_font(label_nearby, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(label_nearby, lv_color_hex(RMC_DIM), 0);
    lv_obj_align(label_nearby, LV_ALIGN_TOP_LEFT, 70, 170);
    s_spinner = rmc_spinner(scr, 18);
    lv_obj_align(s_spinner, LV_ALIGN_TOP_LEFT, 148, 169);
    lv_obj_t *scan = lv_obj_create(scr);                              // SCAN: look again
    lv_obj_remove_style_all(scan);
    lv_obj_set_size(scan, 84, 30);
    lv_obj_set_style_radius(scan, 15, 0);
    lv_obj_set_style_border_width(scan, 1, 0);
    lv_obj_set_style_border_color(scan, lv_color_hex(RMC_YELLOW), 0);
    lv_obj_set_style_bg_color(scan, lv_color_hex(RMC_YELLOW), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(scan, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_flag(scan, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(scan, 10);
    lv_obj_align(scan, LV_ALIGN_TOP_RIGHT, -70, 164);
    lv_obj_add_event_cb(scan, on_rescan, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_l = lv_label_create(scan);
    lv_label_set_text(scan_l, "SCAN");
    lv_obj_set_style_text_font(scan_l, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(scan_l, lv_color_hex(RMC_YELLOW), 0);
    lv_obj_set_style_text_color(scan_l, lv_color_hex(0x000000), LV_STATE_PRESSED);
    lv_obj_center(scan_l);

    s_list = lv_list_create(scr);
    lv_obj_set_size(s_list, RMC_CARD_W, 106);   // stays inside the round screen
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 202);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, 8, 0);
    lv_obj_set_style_radius(s_list, 0, 0);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);

    lv_obj_add_event_cb(scr, ui_event_ble_scan_background, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(scr, on_page_event, LV_EVENT_ALL, NULL);   // rescans on every visit
    if (!s_page_timer) s_page_timer = lv_timer_create(page_tick, 500, NULL);
}
