// App menu (see ui_menu.h): the RMC mark on top and round outlined buttons with their names, like a car's centre
// screen. A tap opens a button, a tap on the mark or a swipe down goes back to the gauge. The platform shows the
// same menu (web/gauge-menu.js). It opens only with a swipe up on a theme page; left/right stays among the themes.
// Without a theme the gauge shows the "no theme" page here instead of the firmware's own gauge pages, which come
// from the upstream project and are not Racing My Car's.

#include "esp_attr.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "../ui.h"
#include "../ui_ext.h"
#include "ui_menu.h"
#include "game_core.h"
#include "theme_engine/theme_interface.h"
#include "app_obd_dsp/ota_wifi_server.h"
#include "app_obd_dsp/boot_block_player.h"
#include "app_obd_dsp/boot_media_mount.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs.h"
#include "cJSON.h"

#define IDLE_MS 60000      // a minute without a touch on the menu pages -> back to the gauge
#define STAY_MS 4000       // on a theme page this long -> it becomes the page shown at boot
#define BTN 76             // button diameter
#define COL 95             // distance between button columns
#define ROW1 128           // button centre rows
#define ROW2 244
#define C_IDLE 0x3A3A3C    // button ring, resting
#define C_TEXT 0x8E8E93    // label, resting
#define C_PICK 0xFFDD00    // the chosen button: ring, icon and label in RMC yellow

lv_obj_t *ui_ScreenPageMenu;

// ---------- the items ----------
LV_IMG_DECLARE(imgMenu_games);
LV_IMG_DECLARE(imgMenu_settings);
LV_IMG_DECLARE(imgMenu_obd);
LV_IMG_DECLARE(imgMenu_ota);
LV_IMG_DECLARE(imgMenu_info);
LV_IMG_DECLARE(imgMenu_boot);

typedef struct {
    const char *key, *name;          // name: the label under the button
    const lv_img_dsc_t *icon;        // 40x40 alpha icon (images/imgMenuIcons.c)
    void (*open)(void);
} menu_item_t;

static void open_games(void);
static void open_settings(void);
static void open_obd(void);
static void open_ota(void);
static void open_info(void);
static void open_boot(void);

// default order, by how often a driver needs them: connect the car, adjust the gauge, play when parked; then the
// occasional ones, with the version last
static const menu_item_t ITEMS[] = {
    {"obd",      "OBD",      &imgMenu_obd,      open_obd},
    {"settings", "SETTINGS", &imgMenu_settings, open_settings},
    {"games",    "GAMES",    &imgMenu_games,    open_games},
    {"boot",     "BOOT",     &imgMenu_boot,     open_boot},
    {"ota",      "UPDATE",   &imgMenu_ota,      open_ota},
    {"info",     "VERSION",  &imgMenu_info,     open_info},
};
#define ITEM_COUNT (int)(sizeof(ITEMS) / sizeof(ITEMS[0]))

// ---------- settings (NVS "rmc_ui") ----------
static EXT_RAM_BSS_ATTR int s_order[ITEM_COUNT], s_order_n;   // visible items in order

static int item_index(const char *key)
{
    for (int i = 0; i < ITEM_COUNT; i++) if (strcmp(ITEMS[i].key, key) == 0) return i;
    return -1;
}

// "games,obd,-ota,info,boot": the order, '-' = hidden. Items the stored list lacks (newer firmware) come last.
static void load_order(void)
{
    char text[96] = "";
    size_t len = sizeof(text);
    nvs_handle_t h;
    if (nvs_open("rmc_ui", NVS_READONLY, &h) == ESP_OK) { if (nvs_get_str(h, "menu", text, &len) != ESP_OK) text[0] = 0; nvs_close(h); }
    bool seen[ITEM_COUNT] = {0};
    s_order_n = 0;
    char *save = NULL;
    for (char *tok = strtok_r(text, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        bool hidden = tok[0] == '-';
        int i = item_index(hidden ? tok + 1 : tok);
        if (i < 0 || seen[i]) continue;
        seen[i] = true;
        if (!hidden) s_order[s_order_n++] = i;
    }
    for (int i = 0; i < ITEM_COUNT; i++) if (!seen[i]) s_order[s_order_n++] = i;
}

char *menu_config_json(void)
{
    load_order();
    char text[96] = "";
    size_t len = sizeof(text);
    nvs_handle_t h;
    if (nvs_open("rmc_ui", NVS_READONLY, &h) == ESP_OK) { if (nvs_get_str(h, "menu", text, &len) != ESP_OK) text[0] = 0; nvs_close(h); }
    cJSON *o = cJSON_CreateObject(), *av = cJSON_AddArrayToObject(o, "available"), *ord = cJSON_AddArrayToObject(o, "order"),
          *hid = cJSON_AddArrayToObject(o, "hidden");
    for (int i = 0; i < ITEM_COUNT; i++) cJSON_AddItemToArray(av, cJSON_CreateString(ITEMS[i].key));
    for (int k = 0; k < s_order_n; k++) cJSON_AddItemToArray(ord, cJSON_CreateString(ITEMS[s_order[k]].key));
    char *save = NULL;
    for (char *tok = strtok_r(text, ",", &save); tok; tok = strtok_r(NULL, ",", &save))
        if (tok[0] == '-' && item_index(tok + 1) >= 0) cJSON_AddItemToArray(hid, cJSON_CreateString(tok + 1));
    char *out = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return out;
}

bool menu_config_set_json(const char *json)
{
    cJSON *o = json ? cJSON_Parse(json) : NULL, *ord = o ? cJSON_GetObjectItem(o, "order") : NULL, *hid = o ? cJSON_GetObjectItem(o, "hidden") : NULL;
    if (!cJSON_IsArray(ord)) { cJSON_Delete(o); return false; }
    char text[96] = "";
    bool used[ITEM_COUNT] = {0};
    cJSON *v = NULL;
    cJSON_ArrayForEach(v, ord) {
        int i = cJSON_IsString(v) ? item_index(v->valuestring) : -1;
        if (i < 0 || used[i]) continue;
        used[i] = true;
        bool hidden = false;
        cJSON *hv = NULL;
        if (cJSON_IsArray(hid)) cJSON_ArrayForEach(hv, hid) if (cJSON_IsString(hv) && strcmp(hv->valuestring, ITEMS[i].key) == 0) hidden = true;
        size_t n = strlen(text);
        snprintf(text + n, sizeof(text) - n, "%s%s%s", n ? "," : "", hidden ? "-" : "", ITEMS[i].key);
    }
    cJSON_Delete(o);
    nvs_handle_t h;
    if (nvs_open("rmc_ui", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_str(h, "menu", text) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

// ---------- remembered theme page ----------
static lv_timer_t *s_stay_timer;
static uint8_t s_stay_index;

static void stay_cb(lv_timer_t *t)
{
    lv_timer_del(t);
    s_stay_timer = NULL;
    if (!ui_ScreenPageThemeGauge || lv_scr_act() != ui_ScreenPageThemeGauge || ui_theme_gauge_page_index != s_stay_index) return;
    const char *id = theme_page_list_at(s_stay_index);
    if (!id) return;
    char saved[64] = "";
    size_t len = sizeof(saved);
    nvs_handle_t h;
    if (nvs_open("rmc_ui", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_get_str(h, "theme_page", saved, &len) != ESP_OK || strcmp(saved, id) != 0) {   // write only on a change
        nvs_set_str(h, "theme_page", id);
        nvs_commit(h);
    }
    nvs_close(h);
}

void ui_menu_theme_shown(void)
{
    s_stay_index = ui_theme_gauge_page_index;
    if (s_stay_timer) lv_timer_reset(s_stay_timer);
    else s_stay_timer = lv_timer_create(stay_cb, STAY_MS, NULL);
}

uint8_t ui_menu_saved_theme_index(void)
{
    char saved[64] = "";
    size_t len = sizeof(saved);
    nvs_handle_t h;
    if (nvs_open("rmc_ui", NVS_READONLY, &h) != ESP_OK) return 0;
    esp_err_t err = nvs_get_str(h, "theme_page", saved, &len);
    nvs_close(h);
    if (err != ESP_OK) return 0;
    for (uint8_t i = 0; i < theme_page_list_count(); i++) {
        const char *id = theme_page_list_at(i);
        if (id && strcmp(id, saved) == 0) return i;
    }
    return 0;
}

static lv_obj_t *s_boot_scr;   // boot animation preview (below)

// ---------- going back to the gauge ----------
static void load_screen(lv_obj_t *scr, bool delete_current)
{
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, delete_current);
}

// ---------- no theme installed ----------
static lv_obj_t *s_none_scr;
static void on_none(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) { s_none_scr = NULL; return; }
    bool up = code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP;
    if (up) lv_indev_wait_release(lv_indev_get_act());
    if (up || code == LV_EVENT_CLICKED) ui_menu_open();   // a swipe up (as on a theme page) or a tap: the menu
}
static void none_screen_init(void)
{
    lv_obj_t *scr = s_none_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(scr, on_none, LV_EVENT_ALL, NULL);
    lv_obj_t *logo = lv_img_create(scr);
    lv_img_set_src(logo, &imgRmcMarkSmall);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -70);
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, "NO THEME YET");
    lv_obj_set_style_text_font(t, &ui_font_FontTypoderSize20, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(C_PICK), 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -18);
    lv_obj_t *h = lv_label_create(scr);
    lv_label_set_text(h, "OPEN THE RACING MY CAR APP\nAND TAP RESTORE DEFAULT THEMES\n\nSWIPE UP FOR THE MENU");
    lv_obj_set_style_text_font(h, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(h, lv_color_hex(0x9A9A9A), 0);
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(h, LV_ALIGN_CENTER, 0, 48);
}

lv_obj_t *ui_menu_home_screen(void)
{
    if (theme_page_list_count() > 0) {
        if (!ui_ScreenPageThemeGauge) ui_ScreenPageThemeGauge_screen_init();
        return ui_ScreenPageThemeGauge;
    }
    if (!s_none_scr) none_screen_init();
    return s_none_scr;
}

static void go_home_cb(lv_timer_t *t) { (void)t; ui_menu_go_home(); }
void ui_menu_go_home_later(uint32_t ms)
{
    lv_timer_t *t = lv_timer_create(go_home_cb, ms, NULL);
    lv_timer_set_repeat_count(t, 1);
}

void ui_menu_go_home(void)
{
    lv_obj_t *cur = lv_scr_act();
    // our own screens and the game screens free themselves when deleted; system pages are kept for later
    bool ours = cur == ui_ScreenPageMenu || (gc_scr && cur == gc_scr) || (ui_ScreenPageGames && cur == ui_ScreenPageGames) ||
                (s_boot_scr && cur == s_boot_scr) || (ui_ScreenPageMenuSettings && cur == ui_ScreenPageMenuSettings);
    lv_obj_t *home = ui_menu_home_screen();   // the theme page is still there when the menu was opened from it
    if (home != cur) load_screen(home, ours);
}


// ---------- the menu screen ----------
// Like a car's centre screen: the RMC mark on top, round outlined buttons in rows of three with their names under
// them. The button last opened is shown in RMC yellow, and so is the one under the finger.
static EXT_RAM_BSS_ATTR lv_obj_t *s_btn[ITEM_COUNT], *s_icon[ITEM_COUNT], *s_label[ITEM_COUNT];
static EXT_RAM_BSS_ATTR int s_btn_item[ITEM_COUNT], s_btn_n;
static int s_last = -1;                 // item last opened from the menu (kept while the gauge runs)
static bool s_closing;

static void paint(int b, bool pick)
{
    lv_obj_set_style_border_color(s_btn[b], lv_color_hex(pick ? C_PICK : C_IDLE), 0);
    lv_obj_set_style_border_width(s_btn[b], pick ? 2 : 1, 0);
    lv_obj_set_style_img_recolor(s_icon[b], lv_color_hex(pick ? C_PICK : 0xFFFFFF), 0);
    lv_obj_set_style_text_color(s_label[b], lv_color_hex(pick ? C_PICK : C_TEXT), 0);
}
static void highlight(int item)
{
    for (int b = 0; b < s_btn_n; b++) paint(b, s_btn_item[b] == item);
}

static void on_button(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int item = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_closing) return;
    if (code == LV_EVENT_PRESSED) highlight(item);
    else if (code == LV_EVENT_PRESS_LOST) highlight(s_last);
    else if (code == LV_EVENT_CLICKED) {
        s_last = item;
        s_closing = true;
        ITEMS[item].open();
    }
}

static void on_logo(lv_event_t *e)
{
    (void)e;
    if (s_closing) return;
    s_closing = true;
    ui_menu_go_home();                  // the RMC mark: back to the gauge
}

static void on_menu(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) { ui_ScreenPageMenu = NULL; return; }
    if (code == LV_EVENT_GESTURE && !s_closing) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir != LV_DIR_BOTTOM) return;
        lv_indev_wait_release(lv_indev_get_act());
        s_closing = true;
        ui_menu_go_home();
    }
}

static void ui_ScreenPageMenu_screen_init(void)
{
    load_order();
    s_closing = false;
    lv_obj_t *scr = ui_ScreenPageMenu = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(scr, on_menu, LV_EVENT_ALL, NULL);

    lv_obj_t *logo = lv_img_create(scr);
    lv_img_set_src(logo, &imgRmcMarkSmall);
    lv_img_set_pivot(logo, 42, 15);
    lv_img_set_zoom(logo, 218);                      // 85 %
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_add_flag(logo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(logo, 12);
    lv_obj_add_event_cb(logo, on_logo, LV_EVENT_CLICKED, NULL);

    s_btn_n = s_order_n < ITEM_COUNT ? s_order_n : ITEM_COUNT;
    if (s_last < 0 && s_btn_n) s_last = s_order[0];
    for (int b = 0; b < s_btn_n; b++) {
        int row = b / 3, in_row = row ? s_btn_n - 3 : (s_btn_n < 3 ? s_btn_n : 3), col = b % 3;
        int x = 180 + (int)((col - (in_row - 1) / 2.0f) * COL), y = row ? ROW2 : ROW1;
        int item = s_btn_item[b] = s_order[b];
        lv_obj_t *btn = s_btn[b] = lv_obj_create(scr);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, BTN, BTN);
        lv_obj_set_pos(btn, x - BTN / 2, y - BTN / 2);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x141416), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_ext_click_area(btn, 8);
        lv_obj_add_event_cb(btn, on_button, LV_EVENT_ALL, (void *)(intptr_t)item);
        lv_obj_t *ic = s_icon[b] = lv_img_create(btn);
        lv_img_set_src(ic, ITEMS[item].icon);
        lv_obj_set_style_img_recolor_opa(ic, LV_OPA_COVER, 0);
        lv_obj_center(ic);
        lv_obj_t *l = s_label[b] = lv_label_create(scr);
        lv_label_set_text(l, ITEMS[item].name);
        lv_obj_set_style_text_font(l, &ui_font_FontTypoderSize16, 0);
        lv_obj_set_width(l, 120);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(l, x - 60, y + BTN / 2 + 6);
    }
    highlight(s_last);
}

void ui_menu_open(void)
{
    if (ui_ScreenPageMenu) return;
    ui_ScreenPageMenu_screen_init();
    // the gauge page stays, so closing the menu is instant; the boot animation preview and SETTINGS are freed
    lv_obj_t *cur = lv_scr_act();
    load_screen(ui_ScreenPageMenu, (s_boot_scr && cur == s_boot_scr) || (ui_ScreenPageMenuSettings && cur == ui_ScreenPageMenuSettings));
}

// leaving the menu for another page: the theme page is rebuilt on the way back (frees its memory meanwhile)
static void leave_to(lv_obj_t **target, void (*init)(void))
{
    if (ui_ScreenPageThemeGauge) { lv_obj_del(ui_ScreenPageThemeGauge); ui_ScreenPageThemeGauge = NULL; }
    if (!*target) init();
    load_screen(*target, true);
}

static void open_games(void) { leave_to(&ui_ScreenPageGames, ui_ScreenPageGames_screen_init); }
static void open_settings(void) { leave_to(&ui_ScreenPageMenuSettings, ui_ScreenPageMenuSettings_screen_init); }
static void open_obd(void)   { leave_to(&ui_ScreenPageBLEScan, ui_ScreenPageBLEScan_screen_init); }
static void open_ota(void)   { leave_to(&ui_ScreenPageOTAMode, ui_ScreenPageOTAMode_screen_init); }
static void open_info(void)  { leave_to(&ui_ScreenPageEasterEgg, ui_ScreenPageEasterEgg_screen_init); }

// ---------- boot animation preview ----------
static lv_timer_t *s_boot_timer;
static int64_t s_boot_start;

static void boot_stop(void)
{
    if (s_boot_timer) { lv_timer_del(s_boot_timer); s_boot_timer = NULL; }
    boot_block_player_destroy();
}

static void boot_tick(lv_timer_t *t)
{
    (void)t;
    boot_block_player_update((uint32_t)((esp_timer_get_time() - s_boot_start) / 1000));
    if (boot_block_player_is_finished()) { boot_stop(); ui_menu_open(); }
}

static void on_boot(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) { boot_stop(); s_boot_scr = NULL; return; }
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_GESTURE) {   // a tap or a swipe: back to the menu
        lv_indev_wait_release(lv_indev_get_act());
        boot_stop();
        ui_menu_open();
    }
}

static void open_boot(void)
{
    boot_block_player_set_paths("/bootmedia/boot_block.txt", "/bootmedia/boot_block.bin");
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_t *canvas = NULL;
    if (!boot_media_mount() || !boot_block_player_create(scr, &canvas)) {
        lv_obj_del(scr);
        s_closing = false;
        gc_toast("NO BOOT ANIMATION YET");
        return;
    }
    if (canvas) lv_obj_clear_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, on_boot, LV_EVENT_ALL, NULL);
    s_boot_scr = scr;
    s_boot_start = esp_timer_get_time();
    s_boot_timer = lv_timer_create(boot_tick, 33, NULL);
    load_screen(scr, true);   // the menu is deleted; finishing or a tap opens it again
}

// ---------- back to the gauge after a minute without a touch ----------
static void idle_cb(lv_timer_t *t)
{
    (void)t;
    if (lv_disp_get_inactive_time(NULL) < IDLE_MS || ui_ext_showroom_is_active()) return;
    lv_obj_t *cur = lv_scr_act();
    if (ui_ScreenPageOTAMode && cur == ui_ScreenPageOTAMode) return;   // never in the middle of an update
    bool menu_page = cur == ui_ScreenPageMenu || (gc_scr && cur == gc_scr) || (ui_ScreenPageGames && cur == ui_ScreenPageGames) ||
                     (ui_ScreenPageBLEScan && cur == ui_ScreenPageBLEScan) || (ui_ScreenPageEasterEgg && cur == ui_ScreenPageEasterEgg) ||
                     (s_boot_scr && cur == s_boot_scr) || (ui_ScreenPageMenuSettings && cur == ui_ScreenPageMenuSettings);
    if (!menu_page || !cur) return;
    if (s_boot_scr && cur == s_boot_scr) boot_stop();
    lv_disp_trig_activity(NULL);     // one return per idle minute
    ui_menu_go_home();
}

void ui_menu_init(void)
{
    static bool done;
    if (done) return;
    done = true;
    lv_timer_create(idle_cb, 1000, NULL);
}
