// App menu (see ui_menu.h). The icons are drawn into one full-screen canvas: round coloured tiles in honeycomb
// slots around the middle, each scaled by its distance from the centre (fisheye). Dragging pans the honeycomb a
// little and it springs back; a tap opens the icon under the finger; a swipe down closes the menu.

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

#define MW 360
#define MH 360
#define ICON_R 54          // radius of the middle icon
#define SLOT 118           // distance between honeycomb neighbours
#define GLYPH_R 48         // the symbols are drawn for this radius and scaled to the icon
#define IDLE_MS 60000      // a minute without a touch on the menu pages -> back to the gauge
#define STAY_MS 4000       // on a theme page this long -> it becomes the page shown at boot

lv_obj_t *ui_ScreenPageMenu;

// ---------- the items ----------
typedef struct {
    const char *key, *name;
    uint32_t color;
    void (*glyph)(lv_color_t *b, int cx, int cy, float s);   // white symbol, s = scale (1 at radius 48)
    void (*open)(void);
} menu_item_t;

static void hline(lv_color_t *b, int y, int x0, int x1, lv_color_t c)
{
    if (y < 0 || y >= MH) return;
    if (x0 < 0) x0 = 0;
    if (x1 > MW) x1 = MW;
    for (int x = x0; x < x1; x++) b[y * MW + x] = c;
}
static void rect(lv_color_t *b, float x0, float y0, float x1, float y1, uint32_t c)
{
    gc_rect_in(b, MW, MH, (int)lroundf(x0), (int)lroundf(y0), (int)lroundf(x1), (int)lroundf(y1), lv_color_hex(c));
}
static void disc(lv_color_t *b, float x, float y, float r, uint32_t c)
{
    gc_disc_in(b, MW, MH, (int)lroundf(x), (int)lroundf(y), (int)lroundf(r), lv_color_hex(c));
}

static void glyph_games(lv_color_t *b, int cx, int cy, float s)        // a game controller
{
    rect(b, cx - 22 * s, cy - 12 * s, cx + 22 * s, cy + 10 * s, 0xFFFFFF);
    disc(b, cx - 22 * s, cy + 2 * s, 12 * s, 0xFFFFFF);
    disc(b, cx + 22 * s, cy + 2 * s, 12 * s, 0xFFFFFF);
    rect(b, cx - 26 * s, cy - 3 * s, cx - 12 * s, cy + 3 * s, 0x6D28D9);
    rect(b, cx - 22 * s, cy - 7 * s, cx - 16 * s, cy + 7 * s, 0x6D28D9);
    disc(b, cx + 16 * s, cy - 3 * s, 4 * s, 0x6D28D9);
    disc(b, cx + 25 * s, cy + 5 * s, 4 * s, 0x6D28D9);
}
static void glyph_obd(lv_color_t *b, int cx, int cy, float s)          // wireless signal
{
    int y = cy + (int)(16 * s);
    for (int k = 0; k < 3; k++) {
        int r0 = (int)((10 + k * 13) * s), r1 = (int)((16 + k * 13) * s);
        gc_ring_in(b, MW, MH, cx, y, r0, r1, 315, 405, lv_color_hex(0xFFFFFF));
    }
    disc(b, cx, y, 5 * s, 0xFFFFFF);
}
static void glyph_ota(lv_color_t *b, int cx, int cy, float s)          // download arrow
{
    rect(b, cx - 6 * s, cy - 26 * s, cx + 6 * s, cy + 2 * s, 0xFFFFFF);
    int top = (int)(cy + 2 * s), h = (int)(20 * s);
    for (int i = 0; i < h; i++) {
        int w = (int)((22 * s) * (1.0f - (float)i / h));
        hline(b, top + i, cx - w, cx + w + 1, lv_color_hex(0xFFFFFF));
    }
    rect(b, cx - 24 * s, cy + 26 * s, cx + 24 * s, cy + 32 * s, 0xFFFFFF);
}
static void glyph_info(lv_color_t *b, int cx, int cy, float s)         // "i"
{
    disc(b, cx, cy - 20 * s, 6 * s, 0xFFFFFF);
    rect(b, cx - 5 * s, cy - 8 * s, cx + 5 * s, cy + 26 * s, 0xFFFFFF);
    rect(b, cx - 11 * s, cy - 8 * s, cx + 5 * s, cy - 3 * s, 0xFFFFFF);
    rect(b, cx - 11 * s, cy + 21 * s, cx + 11 * s, cy + 26 * s, 0xFFFFFF);
}
static void glyph_boot(lv_color_t *b, int cx, int cy, float s)         // play triangle
{
    int h = (int)(24 * s);
    for (int dy = -h; dy <= h; dy++) {
        int w = (int)((h - abs(dy)) * 1.6f);
        hline(b, cy + dy, cx - (int)(12 * s), cx - (int)(12 * s) + w + 1, lv_color_hex(0xFFFFFF));
    }
}

static void glyph_settings(lv_color_t *b, int cx, int cy, float s)     // a gear
{
    for (int k = 0; k < 8; k++) gc_ring_in(b, MW, MH, cx, cy, (int)(17 * s), (int)(28 * s), k * 45.0f, k * 45.0f + 24.0f, lv_color_hex(0xFFFFFF));
    disc(b, cx, cy, 21 * s, 0xFFFFFF);
    disc(b, cx, cy, 9 * s, 0x636366);
}

static void open_games(void);
static void open_settings(void);
static void open_obd(void);
static void open_ota(void);
static void open_info(void);
static void open_boot(void);

static const menu_item_t ITEMS[] = {
    {"games", "GAMES",          0x8B5CF6, glyph_games, open_games},
    {"settings", "SETTINGS",    0x636366, glyph_settings, open_settings},
    {"obd",   "OBD DEVICE",     0x2F9BFF, glyph_obd,   open_obd},
    {"ota",   "WIFI UPDATE",    0x22C55E, glyph_ota,   open_ota},
    {"info",  "VERSION",        0x8E8E93, glyph_info,  open_info},
    {"boot",  "BOOT ANIMATION", 0xFF8A00, glyph_boot,  open_boot},
};
#define ITEM_COUNT (int)(sizeof(ITEMS) / sizeof(ITEMS[0]))
// honeycomb slots, filled so any count looks balanced: middle, the four diagonals, then right and left
static const int8_t SLOTS[7][2] = {{0, 0}, {-1, -2}, {1, -2}, {1, 2}, {-1, 2}, {2, 0}, {-2, 0}};

// ---------- settings (NVS "rmc_ui") ----------
static int s_order[ITEM_COUNT], s_order_n;   // visible items in order

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

void ui_menu_go_home(void)
{
    lv_obj_t *cur = lv_scr_act();
    // our own screens and the game screens free themselves when deleted; system pages are kept for later
    bool ours = cur == ui_ScreenPageMenu || (gc_scr && cur == gc_scr) || (ui_ScreenPageGames && cur == ui_ScreenPageGames) ||
                (s_boot_scr && cur == s_boot_scr) || (ui_ScreenPageMenuSettings && cur == ui_ScreenPageMenuSettings);
    if (theme_page_list_count() > 0) {
        if (!ui_ScreenPageThemeGauge) ui_ScreenPageThemeGauge_screen_init();   // still there when the menu was opened from it
        load_screen(ui_ScreenPageThemeGauge, ours);
    } else {
        if (!ui_ScreenPageGear) ui_ScreenPageGear_screen_init();
        load_screen(ui_ScreenPageGear, ours);
    }
}

// ---------- the menu screen ----------
static lv_color_t *s_buf;
static lv_obj_t *s_canvas, *s_name;
static lv_timer_t *s_anim;
static float s_ox, s_oy, s_start_ox, s_start_oy;
static lv_point_t s_down;
static bool s_pressed, s_moved, s_closing;
static int s_named = -1;

static void slot_pos(int k, float *x, float *y, float *r)
{
    float hx = SLOTS[k][0] * SLOT * 0.5f + s_ox, hy = SLOTS[k][1] * SLOT * 0.433f + s_oy;   // hex: rows 0.866 * SLOT apart
    float d = sqrtf(hx * hx + hy * hy), scale = 1.0f - (d - 30.0f) / 240.0f;
    if (scale > 1.0f) scale = 1.0f;
    if (scale < 0.45f) scale = 0.45f;
    float pull = 1.0f - 0.10f * (d / 180.0f);   // the rim icons sit a little closer, as on a watch
    *x = MW / 2 + hx * pull;
    *y = MH / 2 + hy * pull;
    *r = ICON_R * scale;
}

static void draw(void)
{
    if (!s_buf) return;
    for (int i = 0; i < MW * MH; i++) s_buf[i] = lv_color_hex(0x000000);
    int nearest = -1;
    float best = 1e9f;
    for (int k = 0; k < s_order_n && k < 7; k++) {
        float x, y, r;
        slot_pos(k, &x, &y, &r);
        const menu_item_t *it = &ITEMS[s_order[k]];
        disc(s_buf, x, y, r, it->color);
        it->glyph(s_buf, (int)lroundf(x), (int)lroundf(y), r / GLYPH_R);
        float d = (x - MW / 2) * (x - MW / 2) + (y - MH / 2) * (y - MH / 2);
        if (d < best) { best = d; nearest = k; }
    }
    lv_obj_invalidate(s_canvas);
    if (nearest != s_named) {
        s_named = nearest;
        lv_label_set_text(s_name, nearest >= 0 ? ITEMS[s_order[nearest]].name : "");
    }
}

static void spring(lv_timer_t *t)
{
    (void)t;
    if (s_pressed || (fabsf(s_ox) < 0.5f && fabsf(s_oy) < 0.5f)) {
        if (!s_pressed && (s_ox != 0 || s_oy != 0)) { s_ox = s_oy = 0; draw(); }
        return;
    }
    s_ox *= 0.7f;
    s_oy *= 0.7f;
    draw();
}

static float rubber(float v) { return v > 70 ? 70 + (v - 70) * 0.25f : v < -70 ? -70 + (v + 70) * 0.25f : v; }

static int item_at(lv_point_t p)
{
    for (int k = 0; k < s_order_n && k < 7; k++) {
        float x, y, r;
        slot_pos(k, &x, &y, &r);
        if ((p.x - x) * (p.x - x) + (p.y - y) * (p.y - y) <= (r + 6) * (r + 6)) return s_order[k];
    }
    return -1;
}

static void on_menu(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *in = lv_indev_get_act();
    if (code == LV_EVENT_DELETE) {
        if (s_anim) { lv_timer_del(s_anim); s_anim = NULL; }
        if (s_buf) { heap_caps_free(s_buf); s_buf = NULL; }
        ui_ScreenPageMenu = NULL;
        return;
    }
    if (s_closing) return;
    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(in, &s_down);
        s_start_ox = s_ox; s_start_oy = s_oy;
        s_pressed = true; s_moved = false;
    } else if (code == LV_EVENT_PRESSING && s_pressed) {
        lv_point_t p;
        lv_indev_get_point(in, &p);
        int dx = p.x - s_down.x, dy = p.y - s_down.y;
        if (dx * dx + dy * dy > 144) s_moved = true;
        if (s_moved) { s_ox = rubber(s_start_ox + dx); s_oy = rubber(s_start_oy + dy); draw(); }
    } else if (code == LV_EVENT_GESTURE) {
        if (lv_indev_get_gesture_dir(in) == LV_DIR_BOTTOM) {
            lv_indev_wait_release(in);
            s_closing = true;
            s_pressed = false;
            ui_menu_go_home();
        }
    } else if (code == LV_EVENT_RELEASED) {
        s_pressed = false;
        if (!s_moved) {
            lv_point_t p;
            lv_indev_get_point(in, &p);
            int i = item_at(p);
            if (i >= 0) { s_closing = true; ITEMS[i].open(); }
        }
    }
}

static void ui_ScreenPageMenu_screen_init(void)
{
    load_order();
    s_ox = s_oy = 0; s_pressed = s_moved = s_closing = false; s_named = -1;
    ui_ScreenPageMenu = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ScreenPageMenu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_ScreenPageMenu, lv_color_hex(0x000000), 0);
    lv_obj_add_flag(ui_ScreenPageMenu, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui_ScreenPageMenu, on_menu, LV_EVENT_ALL, NULL);
    s_buf = heap_caps_malloc(MW * MH * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_canvas = lv_canvas_create(ui_ScreenPageMenu);
    if (s_buf) lv_canvas_set_buffer(s_canvas, s_buf, MW, MH, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    s_name = lv_label_create(ui_ScreenPageMenu);
    lv_obj_set_style_text_font(s_name, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(s_name, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_name, LV_ALIGN_BOTTOM_MID, 0, -26);
    lv_label_set_text(s_name, "");
    s_anim = lv_timer_create(spring, 30, NULL);
    draw();
}

void ui_menu_open(void)
{
    if (ui_ScreenPageMenu) return;
    ui_ScreenPageMenu_screen_init();
    if (!s_buf) { lv_obj_del(ui_ScreenPageMenu); gc_toast("NOT ENOUGH MEMORY"); return; }
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
