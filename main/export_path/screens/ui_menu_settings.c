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
#include "app_obd_dsp/vehicle_pick.h"
#include "bsp_obd_dsp/elm327_ble_client.h"
#include "esp_system.h"
#include "../ui_lang.h"

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
    lv_label_set_text(t, TR(title));
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

// ---------- vehicle: OBD2 标准, or 专车专用 by brand → model → year (→ 读法), as on the phone and the 7" dash ----------
static lv_obj_t *s_pick;                                          // the picker page over the settings, while open
static vehicle_pick_t s_at;                                       // the brand / model / year picked so far
static void show_vehicle(void)
{
    char b[96]; vehicle_pick_label(vehicle_pick_get(), ui_lang_get() == UI_LANG_ZH, b, sizeof(b));
    lv_label_set_text(s_vehicle_val, b);
}
enum { PK_ROOT, PK_BRAND, PK_MODEL, PK_YEAR, PK_ALT };
static void picker(int step);
static void pick_done(vehicle_pick_t p)
{
    vehicle_pick_set(p);                                          // the profile: gear detection uses it right away
    elm327_ble_request_reinit(true);                              // and the OBD link: header, protocol, timeouts
    if (s_pick) { lv_obj_del(s_pick); s_pick = NULL; }
    show_vehicle();
}
static void on_pick(lv_event_t *e)
{
    intptr_t v = (intptr_t)lv_event_get_user_data(e);
    int step = (int)(v >> 16), i = (int)(v & 0xffff);
    const vc_model_t *m;
    switch (step) {
    case PK_ROOT: if (i == 0) pick_done((vehicle_pick_t){.obd2 = true}); else picker(PK_BRAND); break;
    case PK_BRAND: s_at = (vehicle_pick_t){.brand = (uint8_t)i}; picker(PK_MODEL); break;
    case PK_MODEL: s_at.model = (uint8_t)i; s_at.year = 0; m = &VC_BRANDS[s_at.brand].models[i];
        if (m->from) picker(PK_YEAR); else if (m->alt) picker(PK_ALT); else pick_done(s_at);
        break;
    case PK_YEAR: s_at.year = (uint16_t)i; m = &VC_BRANDS[s_at.brand].models[s_at.model];
        if (m->alt) picker(PK_ALT); else { s_at.alt = 0; pick_done(s_at); }
        break;
    case PK_ALT: s_at.alt = (uint8_t)i; pick_done(s_at); break;
    }
}
static void on_pick_back(lv_event_t *e)
{
    int step = (int)(intptr_t)lv_event_get_user_data(e);
    const vc_model_t *m = &VC_BRANDS[s_at.brand].models[s_at.model];
    if (step == PK_ROOT) { lv_obj_del(s_pick); s_pick = NULL; return; }
    picker(step == PK_BRAND ? PK_ROOT : step == PK_MODEL ? PK_BRAND : step == PK_YEAR ? PK_MODEL : m->from ? PK_YEAR : PK_MODEL);
}
static void on_pick_gesture(lv_event_t *e) { lv_indev_wait_release(lv_indev_get_act()); }   // a swipe here is not "back to the menu"
static void pick_row(lv_obj_t *list, const char *title, const char *note, bool cur, int step, int i)
{
    lv_obj_t *r = lv_btn_create(list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, CARD_W, note ? 62 : 50);
    lv_obj_set_style_bg_color(r, lv_color_hex(C_CARD), 0); lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, 18, 0); lv_obj_set_style_pad_hor(r, 16, 0);
    lv_obj_set_style_border_color(r, lv_color_hex(C_ACCENT), 0); lv_obj_set_style_border_width(r, cur ? 2 : 0, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_SNAPPABLE);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_t *t = lv_label_create(r);
    lv_label_set_text(t, title); lv_label_set_long_mode(t, LV_LABEL_LONG_DOT); lv_obj_set_size(t, CARD_W - 32, 22);   // one line, … when longer
    lv_obj_set_style_text_font(t, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(cur ? C_ACCENT : 0xFFFFFF), 0);
    lv_obj_align(t, note ? LV_ALIGN_TOP_LEFT : LV_ALIGN_LEFT_MID, 0, note ? 10 : 0);
    if (note) {
        lv_obj_t *n = lv_label_create(r);
        lv_label_set_text(n, note); lv_label_set_long_mode(n, LV_LABEL_LONG_DOT); lv_obj_set_size(n, CARD_W - 32, 22);
        lv_obj_set_style_text_font(n, &ui_font_FontTypoderSize16, 0);
        lv_obj_set_style_text_color(n, lv_color_hex(C_DIM), 0);
        lv_obj_align(n, LV_ALIGN_BOTTOM_LEFT, 0, -9);
    }
    lv_obj_add_event_cb(r, on_pick, LV_EVENT_CLICKED, (void *)(intptr_t)(step << 16 | i));
}
static void picker(int step)
{
    bool zh = ui_lang_get() == UI_LANG_ZH;
    vehicle_pick_t now = vehicle_pick_get();
    if (s_pick) lv_obj_del(s_pick);
    s_pick = lv_obj_create(ui_ScreenPageMenuSettings);
    lv_obj_remove_style_all(s_pick);
    lv_obj_set_size(s_pick, 360, 360);
    lv_obj_set_style_bg_color(s_pick, lv_color_hex(0x000000), 0); lv_obj_set_style_bg_opa(s_pick, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_pick, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_pick, on_pick_gesture, LV_EVENT_GESTURE, NULL);
    lv_obj_t *list = lv_obj_create(s_pick);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 360, 360);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 10, 0); lv_obj_set_style_pad_ver(list, 70, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER); lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_GESTURE_BUBBLE);
    // the title and where we are (scroll away with the list), then 返回 as its own row
    static const char *const titles[] = {"VEHICLE", "BRAND", "MODEL", "YEAR", "READING"};
    lv_obj_t *t = lv_label_create(list);
    lv_label_set_text(t, TR(titles[step]));
    lv_obj_set_style_text_font(t, &ui_font_FontTypoderSize24, 0); lv_obj_set_style_text_color(t, lv_color_hex(C_ACCENT), 0);
    const vc_brand_t *b = step >= PK_MODEL ? &VC_BRANDS[s_at.brand] : NULL;
    const vc_model_t *m = step >= PK_YEAR ? &b->models[s_at.model] : NULL;
    if (b) {
        char where[96]; snprintf(where, sizeof(where), "%s%s%s", b->name, m ? " > " : "", m ? vehicle_model_name(m, zh) : "");
        lv_obj_t *w = lv_label_create(list); lv_label_set_text(w, where); lv_label_set_long_mode(w, LV_LABEL_LONG_DOT); lv_obj_set_width(w, CARD_W);
        lv_obj_set_style_text_align(w, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(w, &ui_font_FontTypoderSize16, 0); lv_obj_set_style_text_color(w, lv_color_hex(C_DIM), 0);
    }
    lv_obj_t *back = round_btn(list, "<", LV_ALIGN_CENTER, 0, 0, on_pick_back, (void *)(intptr_t)step);
    lv_obj_clear_flag(back, LV_OBJ_FLAG_GESTURE_BUBBLE);
    char buf[96], note[96];
    if (step == PK_ROOT) {
        pick_row(list, TR("OBD2 STANDARD"), TR("EVERY CAR"), now.obd2, PK_ROOT, 0);
        pick_row(list, TR("CAR-SPECIFIC"), TR("BY MODEL & YEAR"), !now.obd2, PK_ROOT, 1);
    } else if (step == PK_BRAND) {
        for (int i = 0; i < VC_BRAND_COUNT; i++) pick_row(list, VC_BRANDS[i].name, NULL, !now.obd2 && now.brand == i, PK_BRAND, i);
    } else if (step == PK_MODEL) {
        for (int i = 0; i < b->count; i++) {
            const vc_model_t *x = &b->models[i];
            const char *code = zh && x->code_zh ? x->code_zh : x->code, *nt = zh && x->note_zh ? x->note_zh : x->note;
            snprintf(buf, sizeof(buf), "%s", vehicle_model_name(x, zh));     // the code and the years on the line below
            if (x->from) snprintf(note, sizeof(note), "%s%s%u-%u", code ? code : "", code ? " · " : "", (unsigned)x->from, (unsigned)x->to);
            else snprintf(note, sizeof(note), "%s", nt ? nt : "");
            pick_row(list, buf, note[0] ? note : NULL, !now.obd2 && now.brand == s_at.brand && now.model == i, PK_MODEL, i);
        }
    } else if (step == PK_YEAR) {
        for (int y = m->from; y <= m->to; y++) { snprintf(buf, sizeof(buf), "%d", y); pick_row(list, buf, NULL, !now.obd2 && now.brand == s_at.brand && now.model == s_at.model && now.year == y, PK_YEAR, y); }
    } else {
        pick_row(list, TR("STANDARD (BEST)"), m->profile, true, PK_ALT, 0);
        pick_row(list, TR("FALLBACK"), m->alt, false, PK_ALT, 1);
    }
    // the current choice in view
    lv_obj_update_layout(list);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(list); i++) {
        lv_obj_t *c = lv_obj_get_child(list, i);
        if (lv_obj_get_style_border_width(c, 0) > 0) { lv_obj_scroll_to_view(c, LV_ANIM_OFF); break; }
    }
}
static void on_vehicle(lv_event_t *e) { picker(PK_ROOT); }
#ifdef SIMULATOR
void ui_settings_sim_pick(int step, int brand, int model) { s_at = (vehicle_pick_t){.brand = (uint8_t)brand, .model = (uint8_t)model}; picker(step); }   // the simulator's shots
#endif

// ---------- several gauges: mode, position, boot intro (were the firmware's own MULTI-GAUGE page) ----------
static lv_obj_t *s_mode_val, *s_pos_val, *s_intro_val, *s_mode_note;
static const char *const MODE_NOTES[] = {"READS THE CAR", "NO OBD (SLAVE)", "READS THE CAR"};   // what each mode does with OBD
static const char *const MODES[] = {"MASTER", "SLAVE", "ALONE"};    // device_role 0/1/2
static const char *const INTROS[] = {"OFF", "VIDEO", "VIDEO"};      // intro_enable 0/(1)/2 (VIDEO: the app's boot animation)
static void show_multi(void)
{
    uint8_t role = nvs_cfg_get()->device_role, intro = nvs_intro_enable_get(), pos = nvs_device_position_get();
    lv_label_set_text(s_mode_val, TR(MODES[role <= 2 ? role : 2]));
    lv_label_set_text(s_mode_note, TR(MODE_NOTES[role <= 2 ? role : 2]));
    lv_obj_set_style_text_color(s_mode_note, lv_color_hex(role == ESPNOW_ROLE_SLAVE ? C_ACCENT : C_DIM), 0);
    lv_label_set_text_fmt(s_pos_val, "%u", (unsigned)(pos >= 1 && pos <= 3 ? pos : 1));
    lv_label_set_text(s_intro_val, TR(INTROS[intro <= 2 ? intro : 2]));
}
static void on_mode(lv_event_t *e)
{
    int step = (int)(intptr_t)lv_event_get_user_data(e);
    nvs_user_cfg_t cfg = *nvs_cfg_get();
    cfg.device_role = (uint8_t)(((cfg.device_role <= 2 ? cfg.device_role : 2) + 3 + step) % 3);
    save(&cfg);
    show_multi();
    gc_toast("RESTART TO APPLY");            // Bluetooth and the gauge link start by role at boot
}
static void on_pos(lv_event_t *e)
{
    int step = (int)(intptr_t)lv_event_get_user_data(e);
    uint8_t pos = nvs_device_position_get();
    pos = (uint8_t)(((pos >= 1 && pos <= 3 ? pos : 1) - 1 + 3 + step) % 3 + 1);
    nvs_device_position_set(pos);
    show_multi();
}
static void on_intro(lv_event_t *e)
{
    int step = (int)(intptr_t)lv_event_get_user_data(e);
    uint8_t v = nvs_intro_enable_get();
    (void)step;
    nvs_intro_enable_set(v == 0 ? 2 : 0);   // OFF <-> VIDEO
    show_multi();
}
static lv_obj_t *choice_card(const char *title, lv_event_cb_t cb)
{
    lv_obj_t *c = card(title, 96);
    round_btn(c, "<", LV_ALIGN_BOTTOM_LEFT, -6, -10, cb, (void *)(intptr_t)-1);
    round_btn(c, ">", LV_ALIGN_BOTTOM_RIGHT, 6, -10, cb, (void *)(intptr_t)1);
    return value_label(c, LV_ALIGN_BOTTOM_MID, 0, -18, &ui_font_FontTypoderSize20);
}

// ---------- language: the settings page is rebuilt at once in the new language (the other pages are built when opened) ----------
static lv_obj_t *s_lang_val;
static int s_focus = 1;                                          // the card scrolled to the middle when the page opens
static void show_lang(void) { lv_label_set_text(s_lang_val, ui_lang_get() == UI_LANG_ZH ? "中文" : "ENGLISH"); }
static void rebuild(lv_timer_t *t)
{
    lv_timer_del(t);
    lv_obj_t *old = ui_ScreenPageMenuSettings;
    s_focus = 2;                                                 // back on the language card
    ui_ScreenPageMenuSettings_screen_init();
    s_focus = 1;
    lv_scr_load(ui_ScreenPageMenuSettings);
    if (old) lv_obj_del(old);
}
static void on_lang(lv_event_t *e)
{
    (void)e;
    ui_lang_set(ui_lang_get() == UI_LANG_ZH ? UI_LANG_EN : UI_LANG_ZH);
    lv_timer_create(rebuild, 0, NULL);                           // not from inside the button's own event
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
    lv_label_set_text(lv_obj_get_child(b, 0), TR("TAP AGAIN"));
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
    label = TR(label);
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
        if (lv_event_get_target(e) != ui_ScreenPageMenuSettings) return;   // the old page after a language change
        if (s_confirm_timer) { lv_timer_del(s_confirm_timer); s_confirm_timer = NULL; }
        s_confirm_btn = NULL; s_pick = NULL;
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
    lv_label_set_text(title, TR("SETTINGS"));
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

    // language
    c = card("LANGUAGE", 96);
    round_btn(c, "<", LV_ALIGN_BOTTOM_LEFT, -6, -10, on_lang, NULL);
    round_btn(c, ">", LV_ALIGN_BOTTOM_RIGHT, 6, -10, on_lang, NULL);
    s_lang_val = value_label(c, LV_ALIGN_BOTTOM_MID, 0, -18, &ui_font_FontTypoderSize20);
    show_lang();

    // vehicle
    c = card("VEHICLE", 96);                                     // tap: OBD2 标准 / brand → model → year
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, on_vehicle, LV_EVENT_CLICKED, NULL);
    round_btn(c, ">", LV_ALIGN_BOTTOM_RIGHT, 6, -10, on_vehicle, NULL);
    s_vehicle_val = value_label(c, LV_ALIGN_BOTTOM_LEFT, 0, -10, &ui_font_FontTypoderSize16);
    lv_label_set_long_mode(s_vehicle_val, LV_LABEL_LONG_DOT);
    lv_obj_set_size(s_vehicle_val, CARD_W - 80, 44);                // two lines: "2014 Toyota" / "Corolla Altis"
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

    s_mode_val = choice_card("GAUGE MODE", on_mode);
    lv_obj_set_height(lv_obj_get_parent(s_mode_val), 118);           // a line more: what the mode does with OBD
    s_mode_note = lv_label_create(lv_obj_get_parent(s_mode_val));       // a slave never connects the OBD adapter
    lv_obj_set_style_text_font(s_mode_note, &ui_font_FontTypoderSize16, 0);
    lv_obj_align(s_mode_note, LV_ALIGN_TOP_LEFT, 0, 34);
    s_pos_val = choice_card("GAUGE POSITION", on_pos);
    s_intro_val = choice_card("BOOT INTRO", on_intro);
    show_multi();

    action_card("TRIP", "RESET TRIP", do_reset_trip);
    action_card("GAUGE", "RESTART", do_restart);

    lv_obj_t *hint = lv_label_create(s_list);
    lv_label_set_text(hint, TR("SWIPE LEFT / RIGHT: BACK"));
    lv_obj_set_style_text_font(hint, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);

    lv_obj_update_layout(scr);
    lv_obj_scroll_to_view(lv_obj_get_child(s_list, s_focus), LV_ANIM_OFF);   // brightness first, in the middle (language after a change)
    fade();
}
