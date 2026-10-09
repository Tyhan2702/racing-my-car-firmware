// SETTINGS from the app menu: one tall rounded card per setting in a list that scrolls up and down, snapping a card
// to the middle of the dial; cards away from the middle fade, as on a watch. Swipe left or right goes back to the
// menu. Every setting is one the gauge already stores (nvs_storage) and takes effect at once unless noted.

#include <stdio.h>
#include <stdlib.h>
#include "../ui.h"
#include "ui_menu.h"
#include "game_core.h"
#include "bsp_obd_dsp/nvs_storage.h"
#include "bsp_obd_dsp/espnow_link.h"
#include "bsp_obd_dsp/lcd_driver/ST77916.h"
#include "app_obd_dsp/vehicle_profiles.h"
#include "esp_system.h"

#define CARD_W 252
#define C_CARD 0x1C1C1E
#define C_DIM 0x9A9A9A
#define C_ON 0x22C55E
#define C_ACCENT 0xFFDD00

lv_obj_t *ui_ScreenPageMenuSettings;
static lv_obj_t *s_list, *s_bright_val, *s_vehicle_val, *s_rpm_val, *s_confirm_btn;
static lv_timer_t *s_confirm_timer;

static void save(const nvs_user_cfg_t *cfg) { nvs_cfg_set(cfg); }   // nvs_cfg_set writes only when something changed

// ---------- building blocks ----------
static lv_obj_t *card(const char *title, int h)
{
    lv_obj_t *c = lv_obj_create(s_list);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, CARD_W, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 22, 0);
    lv_obj_set_style_pad_hor(c, 16, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_SNAPPABLE);
    lv_obj_t *t = lv_label_create(c);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(C_DIM), 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 12);
    return c;
}

static lv_obj_t *value_label(lv_obj_t *parent, lv_align_t align, int x, int y, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(l, align, x, y);
    return l;
}

static lv_obj_t *round_btn(lv_obj_t *parent, const char *sym, lv_align_t align, int x, int y, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, 44, 44);
    lv_obj_set_style_radius(b, 22, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_align(b, align, x, y);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_font(l, &ui_font_FontTypoderSize24, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

static void style_switch(lv_obj_t *sw, bool on)
{
    lv_obj_set_size(sw, 64, 34);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(C_ON), LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
}

// ---------- brightness ----------
static void on_bright(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int v = lv_slider_get_value(sl);
    if (v < 10) v = 10;
    lv_label_set_text_fmt(s_bright_val, "%d%%", v);
    Set_Backlight((uint8_t)v);                                   // live while dragging
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {             // stored once, when the finger lifts
        nvs_user_cfg_t cfg = *nvs_cfg_get();
        cfg.brightness_day = (uint8_t)v;
        save(&cfg);
    }
}

// ---------- vehicle ----------
static void show_vehicle(void)
{
    const vehicle_profile_t *p = vehicle_profile_get(nvs_cfg_get()->vehicle_profile_idx);
    lv_label_set_text(s_vehicle_val, p && p->name ? p->name : "OBD2");
}
static void on_vehicle(lv_event_t *e)
{
    int step = (int)(intptr_t)lv_event_get_user_data(e);
    uint8_t n = 0;
    vehicle_profile_get_all(&n);
    if (!n) return;
    nvs_user_cfg_t cfg = *nvs_cfg_get();
    cfg.vehicle_profile_idx = (uint8_t)((cfg.vehicle_profile_idx + n + step) % n);
    save(&cfg);
    vehicle_profile_set_active(cfg.vehicle_profile_idx);         // gear detection uses it right away
    show_vehicle();
}

// ---------- shift light ----------
static void show_rpm(void)
{
    uint16_t v = nvs_cfg_get()->rpm_warn_threshold;
    lv_label_set_text_fmt(s_rpm_val, "%u", (unsigned)(v ? v : 6000));
}
static void on_rpm(lv_event_t *e)
{
    int step = (int)(intptr_t)lv_event_get_user_data(e);
    nvs_user_cfg_t cfg = *nvs_cfg_get();
    int v = (cfg.rpm_warn_threshold ? cfg.rpm_warn_threshold : 6000) + step;
    if (v < 1000) v = 1000;
    if (v > 9000) v = 9000;
    cfg.rpm_warn_threshold = (uint16_t)v;
    save(&cfg);
    if (cfg.rpm_warn_linked_en && cfg.device_role != ESPNOW_ROLE_STANDALONE) espnow_link_broadcast_threshold((uint16_t)v);
    show_rpm();
}
static void on_flash(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    nvs_user_cfg_t cfg = *nvs_cfg_get();
    cfg.rpm_warn_anim_en = on ? 1 : 0;
    if (on) cfg.rpm_warn_linked_en = 0;                          // the flash and linked flash never run together
    save(&cfg);
}

// ---------- RaceChrono ----------
static void on_rc(lv_event_t *e)
{
    nvs_user_cfg_t cfg = *nvs_cfg_get();
    cfg.rc_enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0;
    save(&cfg);
    gc_toast("APPLIES AFTER A RESTART");
}

// ---------- actions that need a second tap ----------
typedef void (*action_fn)(void);
static void do_reset_trip(void) { nvs_stat_reset_trip(); gc_toast("TRIP RESET"); }
static void do_restart(void) { esp_restart(); }

static void confirm_expire(lv_timer_t *t)
{
    lv_timer_del(t);
    s_confirm_timer = NULL;
    if (s_confirm_btn) {
        lv_obj_t *l = lv_obj_get_child(s_confirm_btn, 0);
        lv_label_set_text(l, (const char *)lv_obj_get_user_data(s_confirm_btn));
        lv_obj_set_style_bg_color(s_confirm_btn, lv_color_hex(0x3A3A3C), 0);
        s_confirm_btn = NULL;
    }
}
static void on_action(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_target(e);
    action_fn fn = (action_fn)lv_event_get_user_data(e);
    if (s_confirm_btn == b) {                                    // second tap within 3 s: do it
        if (s_confirm_timer) confirm_expire(s_confirm_timer);
        fn();
        return;
    }
    if (s_confirm_timer) confirm_expire(s_confirm_timer);
    s_confirm_btn = b;
    lv_label_set_text(lv_obj_get_child(b, 0), "TAP AGAIN");
    lv_obj_set_style_bg_color(b, lv_color_hex(0xB91C1C), 0);
    s_confirm_timer = lv_timer_create(confirm_expire, 3000, NULL);
}
static void action_card(const char *title, const char *label, action_fn fn)
{
    lv_obj_t *c = card(title, 96);
    lv_obj_t *b = lv_btn_create(c);
    lv_obj_set_size(b, CARD_W - 32, 42);
    lv_obj_set_style_radius(b, 21, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_user_data(b, (void *)label);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, &ui_font_FontTypoderSize16, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, on_action, LV_EVENT_CLICKED, (void *)fn);
}

// ---------- the list ----------
static void fade(void)
{
    lv_area_t la;
    lv_obj_get_coords(s_list, &la);
    int mid = (la.y1 + la.y2) / 2;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(s_list); i++) {
        lv_obj_t *c = lv_obj_get_child(s_list, i);
        lv_area_t a;
        lv_obj_get_coords(c, &a);
        int d = abs((a.y1 + a.y2) / 2 - mid);
        int opa = 255 - d * 3 / 2;
        lv_obj_set_style_opa(c, (lv_opa_t)(opa < 60 ? 60 : opa > 255 ? 255 : opa), 0);
    }
}
static void on_scroll(lv_event_t *e) { (void)e; fade(); }

static void on_screen(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) {
        if (s_confirm_timer) { lv_timer_del(s_confirm_timer); s_confirm_timer = NULL; }
        s_confirm_btn = NULL;
        ui_ScreenPageMenuSettings = NULL;
        return;
    }
    if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {         // back to the menu
            lv_indev_wait_release(lv_indev_get_act());
            ui_menu_open();
        }
    }
}

void ui_ScreenPageMenuSettings_screen_init(void)
{
    const nvs_user_cfg_t *cfg = nvs_cfg_get();
    lv_obj_t *scr = ui_ScreenPageMenuSettings = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(scr, on_screen, LV_EVENT_ALL, NULL);

    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, 360, 360);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_list, 12, 0);
    lv_obj_set_style_pad_ver(s_list, 120, 0);                   // the first and last cards can reach the middle
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(s_list, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_list, on_scroll, LV_EVENT_SCROLL, NULL);

    lv_obj_t *title = lv_label_create(s_list);                   // scrolls away with the list
    lv_label_set_text(title, "SETTINGS");
    lv_obj_set_style_text_font(title, &ui_font_FontTypoderSize24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(C_ACCENT), 0);

    // brightness
    lv_obj_t *c = card("BRIGHTNESS", 104);
    s_bright_val = value_label(c, LV_ALIGN_TOP_RIGHT, 0, 8, &ui_font_FontTypoderSize24);
    lv_label_set_text_fmt(s_bright_val, "%d%%", cfg->brightness_day < 10 ? 100 : cfg->brightness_day);
    lv_obj_t *sl = lv_slider_create(c);
    lv_obj_set_size(sl, CARD_W - 52, 14);
    lv_obj_align(sl, LV_ALIGN_BOTTOM_MID, 0, -26);
    lv_slider_set_range(sl, 10, 100);
    lv_slider_set_value(sl, cfg->brightness_day < 10 ? 100 : cfg->brightness_day, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 8, LV_PART_KNOB);
    lv_obj_clear_flag(sl, LV_OBJ_FLAG_GESTURE_BUBBLE);          // dragging the slider is not a swipe back
    lv_obj_add_flag(sl, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_set_ext_click_area(sl, 14);
    lv_obj_add_event_cb(sl, on_bright, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, on_bright, LV_EVENT_RELEASED, NULL);

    // vehicle
    c = card("VEHICLE", 96);
    round_btn(c, "<", LV_ALIGN_BOTTOM_LEFT, -6, -10, on_vehicle, (void *)(intptr_t)-1);
    round_btn(c, ">", LV_ALIGN_BOTTOM_RIGHT, 6, -10, on_vehicle, (void *)(intptr_t)1);
    s_vehicle_val = value_label(c, LV_ALIGN_BOTTOM_MID, 0, -22, &ui_font_FontTypoderSize16);
    lv_label_set_long_mode(s_vehicle_val, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_vehicle_val, CARD_W - 140);
    lv_obj_set_style_text_align(s_vehicle_val, LV_TEXT_ALIGN_CENTER, 0);
    show_vehicle();

    // shift light: rpm and flash
    c = card("SHIFT LIGHT RPM", 96);
    round_btn(c, "-", LV_ALIGN_BOTTOM_LEFT, -6, -10, on_rpm, (void *)(intptr_t)-250);
    round_btn(c, "+", LV_ALIGN_BOTTOM_RIGHT, 6, -10, on_rpm, (void *)(intptr_t)250);
    s_rpm_val = value_label(c, LV_ALIGN_BOTTOM_MID, 0, -16, &ui_font_FontTypoderSize24);
    show_rpm();

    c = card("SHIFT LIGHT FLASH", 64);
    lv_obj_t *sw = lv_switch_create(c);
    style_switch(sw, cfg->rpm_warn_anim_en);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(sw, on_flash, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align(lv_obj_get_child(c, 0), LV_ALIGN_LEFT_MID, 0, 0);

    c = card("RACECHRONO", 64);
    sw = lv_switch_create(c);
    style_switch(sw, cfg->rc_enabled);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(sw, on_rc, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align(lv_obj_get_child(c, 0), LV_ALIGN_LEFT_MID, 0, 0);

    action_card("TRIP", "RESET TRIP", do_reset_trip);
    action_card("GAUGE", "RESTART", do_restart);

    lv_obj_t *hint = lv_label_create(s_list);
    lv_label_set_text(hint, "SWIPE LEFT / RIGHT: BACK");
    lv_obj_set_style_text_font(hint, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);

    lv_obj_update_layout(scr);
    lv_obj_scroll_to_view(lv_obj_get_child(s_list, 1), LV_ANIM_OFF);   // brightness first, in the middle
    fade();
}
