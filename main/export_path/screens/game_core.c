// Game engine and the GAMES page (see game_core.h).
//
// GAMES page: one game at a time — its icon in the middle (tap it to play), name, how to play and the best
// score; ‹ › switch games, the dots show which one. Swiping left/right moves between pages as everywhere else
// on the gauge (Gear <- GAMES -> Info).

#include "esp_attr.h"
#include <math.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"
#include "ui_menu.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "cJSON.h"
#include <stdlib.h>
#include "app_obd_dsp/obd_data_cache.h"

#define FRAME_MS 30
#define PARKED_KMH 3
#define ICON 132

extern const game_def_t game_run, game_ring, game_snake, game_blocks, game_pong, game_bricks, game_hop, game_defender,
    game_road, game_maze,
    game_chase, game_pinball, game_rocks, game_gems, game_stack, game_merge,
    game_maze3d, game_defence, game_bubbles, game_lander, game_cave;
// The position is the game's bit in the installed mask (NVS), so it never changes: retired games leave a NULL slot
// (1 LIGHTS OUT, 2 PERFECT SHIFT, 4 SIGNAL MEMORY) and new games are added at the end.
static const game_def_t *const GAMES[] = {&game_run, NULL, NULL, &game_ring, NULL, &game_snake, &game_blocks, &game_pong,
                                          &game_bricks, &game_hop, &game_defender, &game_road, &game_maze,
                                          &game_chase, &game_pinball, &game_rocks, &game_gems, &game_stack, &game_merge,
                                          &game_maze3d, &game_defence, &game_bubbles, &game_lander, &game_cave};
#define GAME_COUNT (int)(sizeof(GAMES) / sizeof(GAMES[0]))

lv_color_t *gc_buf;
lv_obj_t *gc_scr;
lv_obj_t *ui_ScreenPageGames;

static const game_def_t *s_game;
static lv_obj_t *s_canvas;
static lv_timer_t *s_timer;
static int64_t s_last_us;
static bool s_gestured;
#define MAX_DIRTY 16
static EXT_RAM_BSS_ATTR lv_area_t s_dirty[MAX_DIRTY];
static int s_dirty_n;
static int s_pick;                      // position in the installed list shown on the GAMES page
static EXT_RAM_BSS_ATTR int s_inst[GAME_COUNT], s_inst_n;         // installed games, as indexes into GAMES
static EXT_RAM_BSS_ATTR lv_obj_t *s_icon, *s_name, *s_hint, *s_best, *s_dots[GAME_COUNT], *s_tile, *s_arrows[2], *s_empty;
static lv_color_t *s_icon_buf;

// ---------- installed games (set from the app) ----------
static uint32_t installed_mask(void)
{
    nvs_handle_t h; uint32_t v = 0;
    if (nvs_open("rmc_game", NVS_READONLY, &h) == ESP_OK) { nvs_get_u32(h, "installed", &v); nvs_close(h); }
    return v;
}

char *games_list_json(void)
{
    uint32_t mask = installed_mask();
    cJSON *o = cJSON_CreateObject(), *av = cJSON_AddArrayToObject(o, "available"), *in = cJSON_AddArrayToObject(o, "installed");
    for (int i = 0; i < GAME_COUNT; i++) {
        if (!GAMES[i]) continue;
        cJSON_AddItemToArray(av, cJSON_CreateString(GAMES[i]->key));
        if (mask & (1u << i)) cJSON_AddItemToArray(in, cJSON_CreateString(GAMES[i]->key));
    }
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return text;
}

bool games_install_json(const char *json)
{
    cJSON *o = json ? cJSON_Parse(json) : NULL, *list = o ? cJSON_GetObjectItem(o, "installed") : NULL;
    if (!cJSON_IsArray(list)) { cJSON_Delete(o); return false; }
    uint32_t mask = 0;
    cJSON *v = NULL;
    cJSON_ArrayForEach(v, list) {
        if (!cJSON_IsString(v)) continue;
        for (int i = 0; i < GAME_COUNT; i++) if (GAMES[i] && strcmp(v->valuestring, GAMES[i]->key) == 0) mask |= 1u << i;
    }
    cJSON_Delete(o);
    nvs_handle_t h;
    if (nvs_open("rmc_game", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_u32(h, "installed", mask) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

// ---------- small helpers ----------
float gc_rand(void) { return (float)(esp_random() & 0xFFFF) / 65535.0f; }

static bool car_moving(void)
{
    obd_data_snapshot_t o;
    obd_data_get_snapshot(&o);
    return o.speed > PARKED_KMH;
}

static int32_t best_of(const game_def_t *g)
{
    nvs_handle_t h; int32_t v = 0;
    if (nvs_open("rmc_game", NVS_READONLY, &h) == ESP_OK) { nvs_get_i32(h, g->key, &v); nvs_close(h); }
    return v;
}
int32_t gc_best(void) { return s_game ? best_of(s_game) : 0; }
bool gc_record(int32_t score)
{
    if (!s_game || score <= 0) return false;
    int32_t best = best_of(s_game);
    bool better = best == 0 || (s_game->lower_is_better ? score < best : score > best);
    if (!better) return false;
    nvs_handle_t h;
    if (nvs_open("rmc_game", NVS_READWRITE, &h) == ESP_OK) { nvs_set_i32(h, s_game->key, score); nvs_commit(h); nvs_close(h); }
    return true;
}

void gc_toast(const char *text)
{
    lv_obj_t *l = lv_label_create(lv_layer_top());
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_color(l, lv_color_hex(0xFFDD00), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(l, 8, 0);
    lv_obj_set_style_radius(l, 12, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
    lv_obj_del_delayed(l, 1600);
}

lv_obj_t *gc_label(const lv_font_t *font, uint32_t color, lv_align_t align, int x, int y)
{
    lv_obj_t *l = lv_label_create(gc_scr);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, align, x, y);
    lv_label_set_text(l, "");
    return l;
}

// ---------- drawing ----------
void gc_hline(int y, int x0, int x1, lv_color_t c)
{
    if (!gc_buf || y < 0 || y >= GC_H) return;
    if (x0 < 0) x0 = 0;
    if (x1 > GC_W) x1 = GC_W;
    lv_color_t *p = gc_buf + y * GC_W;
    for (int x = x0; x < x1; x++) p[x] = c;
}
void gc_fill(lv_color_t c) { for (int y = 0; y < GC_H; y++) gc_hline(y, 0, GC_W, c); }
void gc_rect_in(lv_color_t *buf, int w, int h, int x0, int y0, int x1, int y1, lv_color_t c)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) buf[y * w + x] = c;
}
void gc_rect(int x0, int y0, int x1, int y1, lv_color_t c) { if (gc_buf) gc_rect_in(gc_buf, GC_W, GC_H, x0, y0, x1, y1, c); }
void gc_disc_in(lv_color_t *buf, int w, int h, int cx, int cy, int r, lv_color_t c)
{
    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy, dx = (int)sqrtf((float)(r * r - dy * dy));
        if (y < 0 || y >= h) continue;
        int x0 = cx - dx < 0 ? 0 : cx - dx, x1 = cx + dx + 1 > w ? w : cx + dx + 1;
        for (int x = x0; x < x1; x++) buf[y * w + x] = c;
    }
}
void gc_disc(int cx, int cy, int r, lv_color_t c) { if (gc_buf) gc_disc_in(gc_buf, GC_W, GC_H, cx, cy, r, c); }
void gc_ring_in(lv_color_t *buf, int w, int h, int cx, int cy, int r0, int r1, float a0, float a1, lv_color_t c)
{
    // a0..a1 in degrees, 0 at 12 o'clock, clockwise; a1 may exceed 360
    for (int y = cy - r1; y <= cy + r1; y++) {
        if (y < 0 || y >= h) continue;
        for (int x = cx - r1; x <= cx + r1; x++) {
            if (x < 0 || x >= w) continue;
            int dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
            if (d2 < r0 * r0 || d2 > r1 * r1) continue;
            float a = atan2f((float)dx, (float)-dy) * 57.29578f;
            if (a < 0) a += 360.0f;
            if ((a >= a0 && a <= a1) || (a + 360.0f >= a0 && a + 360.0f <= a1)) buf[y * w + x] = c;
        }
    }
}
void gc_ring(int cx, int cy, int r0, int r1, float a0, float a1, lv_color_t c) { if (gc_buf) gc_ring_in(gc_buf, GC_W, GC_H, cx, cy, r0, r1, a0, a1, c); }

void gc_dirty(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > GC_W - 1) x1 = GC_W - 1;
    if (y1 > GC_H - 1) y1 = GC_H - 1;
    if (x1 < x0 || y1 < y0) return;
    if (s_dirty_n < MAX_DIRTY) { s_dirty[s_dirty_n++] = (lv_area_t){x0, y0, x1, y1}; return; }
    lv_area_t *a = &s_dirty[MAX_DIRTY - 1];                  // full: grow the last one
    if (x0 < a->x1) a->x1 = x0;
    if (y0 < a->y1) a->y1 = y0;
    if (x1 > a->x2) a->x2 = x1;
    if (y1 > a->y2) a->y2 = y1;
}
void gc_dirty_all(void) { gc_dirty(0, 0, GC_W - 1, GC_H - 1); }

// ---------- running a game ----------
// Leaving a game: hold a finger still on the screen for 5 seconds, then tap EXIT. Swipes never leave a game (they
// are the games' own controls), so a game is not quit by accident while playing. The game pauses while asking.
#define HOLD_EXIT_US 5000000
#define HOLD_SLOP 24                 // px the finger may wander and still count as holding
static lv_obj_t *s_exit_box;
static int64_t s_hold_us;            // when the finger went down (0: not holding)
static lv_point_t s_hold_at;

static void leave_game(void)
{
    s_exit_box = NULL;               // deleted with the game screen
    s_hold_us = 0;
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    if (!ui_ScreenPageGames) ui_ScreenPageGames_screen_init();
    lv_scr_load_anim(ui_ScreenPageGames, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);   // deletes the game screen
}

static void exit_close(void)
{
    if (s_exit_box) { lv_obj_del(s_exit_box); s_exit_box = NULL; }
    s_last_us = esp_timer_get_time();    // no jump in the game after the pause
}
static void on_exit_btn(lv_event_t *e)
{
    if (lv_event_get_user_data(e)) leave_game();
    else exit_close();
}
static lv_obj_t *exit_btn(lv_obj_t *box, const char *text, uint32_t bg, uint32_t fg, int x, bool leave)
{
    lv_obj_t *b = lv_obj_create(box);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 120, 52);
    lv_obj_align(b, LV_ALIGN_CENTER, x, 46);
    lv_obj_set_style_radius(b, 26, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(b, on_exit_btn, LV_EVENT_CLICKED, leave ? (void *)1 : NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &ui_font_FontTypoderSize20, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_obj_center(l);
    return b;
}
static void exit_ask(void)
{
    if (s_exit_box || !gc_scr) return;
    s_exit_box = lv_obj_create(gc_scr);
    lv_obj_remove_style_all(s_exit_box);
    lv_obj_set_size(s_exit_box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_exit_box, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_exit_box, LV_OPA_80, 0);
    lv_obj_add_flag(s_exit_box, LV_OBJ_FLAG_CLICKABLE);         // touches stop here, not in the game
    lv_obj_clear_flag(s_exit_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_t *t = lv_label_create(s_exit_box);
    lv_label_set_text(t, "EXIT GAME?");
    lv_obj_set_style_text_font(t, &ui_font_FontTypoderSize24, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -30);
    exit_btn(s_exit_box, "CANCEL", 0x2C2C2E, 0xFFFFFF, -66, false);
    exit_btn(s_exit_box, "EXIT", 0xFFDD00, 0x000000, 66, true);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    if (car_moving()) { leave_game(); gc_toast("PARK TO PLAY"); return; }   // never while driving
    int64_t now = esp_timer_get_time();
    if (s_hold_us && now - s_hold_us >= HOLD_EXIT_US) { s_hold_us = 0; s_gestured = true; exit_ask(); }
    if (s_exit_box) { s_last_us = now; return; }                           // paused while asking
    float dt = (now - s_last_us) / 1e6f;
    s_last_us = now;
    if (dt > 0.1f) dt = 0.1f;
    s_game->frame(dt);
    for (int i = 0; i < s_dirty_n; i++) lv_obj_invalidate_area(s_canvas, &s_dirty[i]);
    s_dirty_n = 0;
}

static void on_touch(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (s_exit_box) return;                    // the exit question is open: the game waits
    if (code == LV_EVENT_PRESSED) {
        s_gestured = false;
        lv_point_t p;
        lv_indev_get_point(lv_indev_get_act(), &p);
        s_hold_us = esp_timer_get_time();
        s_hold_at = p;
        if (s_game->input) s_game->input(GC_PRESS, p);
        return;
    }
    if (code == LV_EVENT_PRESSING) {
        lv_point_t p;
        lv_indev_get_point(lv_indev_get_act(), &p);
        if (LV_ABS(p.x - s_hold_at.x) > HOLD_SLOP || LV_ABS(p.y - s_hold_at.y) > HOLD_SLOP) s_hold_us = 0;   // moving: not a hold
        if (s_game->input) s_game->input(GC_DRAG, p);
        return;
    }
    if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        s_gestured = true;
        lv_point_t p = {0, 0};
        s_hold_us = 0;
        if (dir == LV_DIR_BOTTOM) return;      // no swipe leaves a game (hold 5 s instead)
        if (s_game->input) s_game->input(dir == LV_DIR_TOP ? GC_SWIPE_UP : dir == LV_DIR_LEFT ? GC_SWIPE_LEFT : GC_SWIPE_RIGHT, p);
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) s_hold_us = 0;
    if (code == LV_EVENT_RELEASED && s_game && s_game->input) {
        lv_point_t p;
        lv_indev_get_point(lv_indev_get_act(), &p);
        if (s_game->wants_release) s_game->input(GC_RELEASE, p);
        if (!s_gestured) s_game->input(GC_TAP, p);
    }
}

static void on_delete(lv_event_t *e)
{
    (void)e;
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    if (s_game && s_game->end) s_game->end();
    if (gc_buf) { heap_caps_free(gc_buf); gc_buf = NULL; }
    gc_scr = NULL;
    s_game = NULL;
}

static void play(const game_def_t *g)
{
    if (gc_scr) return;
    if (car_moving()) { gc_toast("PARK TO PLAY"); return; }
    gc_buf = heap_caps_malloc(GC_W * GC_H * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!gc_buf) { gc_toast("NOT ENOUGH MEMORY"); return; }
    s_game = g;
    gc_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(gc_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(gc_scr, lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(gc_scr, on_delete, LV_EVENT_DELETE, NULL);
    s_canvas = lv_canvas_create(gc_scr);
    lv_canvas_set_buffer(s_canvas, gc_buf, GC_W, GC_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(gc_scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(gc_scr, on_touch, LV_EVENT_ALL, NULL);
    gc_fill(lv_color_hex(0x000000));
    s_dirty_n = 0;
    g->begin();
    gc_dirty_all();
    s_last_us = esp_timer_get_time();
    s_exit_box = NULL;
    s_hold_us = 0;
    s_timer = lv_timer_create(tick, FRAME_MS, NULL);
    lv_scr_load_anim(gc_scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
    gc_toast("HOLD 5 SEC TO EXIT");
}

// ---------- the GAMES page ----------
static void refresh_installed(void)
{
    uint32_t mask = installed_mask();
    s_inst_n = 0;
    for (int i = 0; i < GAME_COUNT; i++) if (GAMES[i] && (mask & (1u << i))) s_inst[s_inst_n++] = i;
    if (s_pick >= s_inst_n) s_pick = 0;
}

static void show_pick(void)
{
    refresh_installed();
    bool none = s_inst_n == 0;
    lv_obj_t *parts[] = {s_tile, s_arrows[0], s_arrows[1], s_name, s_hint, s_best};
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        if (none) lv_obj_add_flag(parts[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(parts[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (none) lv_obj_clear_flag(s_empty, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    if (s_inst_n < 2) { lv_obj_add_flag(s_arrows[0], LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s_arrows[1], LV_OBJ_FLAG_HIDDEN); }
    for (int i = 0; i < GAME_COUNT; i++) {
        if (i < s_inst_n && s_inst_n > 1) lv_obj_clear_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(i == s_pick ? 0xFFDD00 : 0x444444), 0);
        lv_obj_align(s_dots[i], LV_ALIGN_BOTTOM_MID, (int)((i - (s_inst_n - 1) / 2.0f) * 16), -34);
    }
    if (none || !s_icon_buf) return;
    const game_def_t *g = GAMES[s_inst[s_pick]];
    for (int i = 0; i < ICON * ICON; i++) s_icon_buf[i] = lv_color_hex(0x101010);
    g->icon(s_icon_buf, ICON);
    lv_obj_invalidate(s_icon);
    lv_label_set_text(s_name, g->name);
    lv_label_set_text(s_hint, g->hint);
    int32_t b = best_of(g);
    if (b) lv_label_set_text_fmt(s_best, "BEST %ld%s%s", (long)b, g->unit[0] ? " " : "", g->unit);
    else lv_label_set_text(s_best, "NO RECORD YET");
}

static void on_arrow(lv_event_t *e)
{
    int step = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_inst_n == 0) return;
    s_pick = (s_pick + step + s_inst_n) % s_inst_n;
    show_pick();
}
static void on_icon(lv_event_t *e) { (void)e; if (s_inst_n) play(GAMES[s_inst[s_pick]]); }

static void on_page(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) {
        if (s_icon_buf) { heap_caps_free(s_icon_buf); s_icon_buf = NULL; }
        ui_ScreenPageGames = NULL;
        return;
    }
    if (code == LV_EVENT_SCREEN_LOAD_START) { show_pick(); return; }   // installed games and best scores may have changed
    if (code != LV_EVENT_GESTURE) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT || dir == LV_DIR_BOTTOM) {   // opened from the menu: back to it
        lv_indev_wait_release(lv_indev_get_act());
        ui_menu_open();
    }
}

static lv_obj_t *arrow(lv_obj_t *parent, const char *sym, int x, int step)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, 46, 46);
    lv_obj_align(b, LV_ALIGN_CENTER, x, -22);
    lv_obj_set_style_radius(b, 23, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1C1C1C), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0x3A3A3A), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFDD00), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, on_arrow, LV_EVENT_CLICKED, (void *)(intptr_t)step);
    return b;
}

void ui_ScreenPageGames_screen_init(void)
{
    ui_ScreenPageGames = lv_obj_create(NULL);
    lv_obj_t *scr = ui_ScreenPageGames;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "GAMES");
    lv_obj_set_style_text_font(title, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFDD00), 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    // icon tile: a canvas inside a yellow-rimmed button; tap it to play
    lv_obj_t *tile = s_tile = lv_btn_create(scr);
    lv_obj_set_size(tile, ICON + 10, ICON + 10);
    lv_obj_align(tile, LV_ALIGN_CENTER, 0, -22);
    lv_obj_set_style_radius(tile, 28, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x101010), 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(0xFFDD00), 0);
    lv_obj_set_style_border_width(tile, 2, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_clip_corner(tile, true, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_add_event_cb(tile, on_icon, LV_EVENT_CLICKED, NULL);
    s_icon_buf = heap_caps_malloc(ICON * ICON * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_icon = lv_canvas_create(tile);
    if (s_icon_buf) lv_canvas_set_buffer(s_icon, s_icon_buf, ICON, ICON, LV_IMG_CF_TRUE_COLOR);
    lv_obj_center(s_icon);
    lv_obj_clear_flag(s_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_icon, LV_OBJ_FLAG_EVENT_BUBBLE);

    s_arrows[0] = arrow(scr, LV_SYMBOL_LEFT, -132, -1);
    s_arrows[1] = arrow(scr, LV_SYMBOL_RIGHT, 132, 1);

    // nothing installed yet: point to the app
    s_empty = lv_label_create(scr);
    lv_label_set_text(s_empty, "NO GAMES YET\n\nINSTALL THEM IN THE\nRACING MY CAR APP\n(GAUGE > GAMES)");
    lv_obj_set_style_text_font(s_empty, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(s_empty, lv_color_hex(0x9A9A9A), 0);
    lv_obj_set_style_text_align(s_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_empty);

    s_name = lv_label_create(scr);
    lv_obj_set_style_text_font(s_name, &ui_font_FontTypoderSize24, 0);
    lv_obj_set_style_text_color(s_name, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_name, LV_ALIGN_CENTER, 0, 72);
    s_hint = lv_label_create(scr);
    lv_obj_set_style_text_font(s_hint, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0x9A9A9A), 0);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_hint, LV_ALIGN_CENTER, 0, 104);
    s_best = lv_label_create(scr);
    lv_obj_set_style_text_font(s_best, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(s_best, lv_color_hex(0xFFDD00), 0);
    lv_obj_align(s_best, LV_ALIGN_TOP_MID, 0, 52);

    for (int i = 0; i < GAME_COUNT; i++) {
        s_dots[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(s_dots[i]);
        lv_obj_set_size(s_dots[i], 8, 8);
        lv_obj_set_style_radius(s_dots[i], 4, 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        lv_obj_align(s_dots[i], LV_ALIGN_BOTTOM_MID, (i - (GAME_COUNT - 1) / 2.0f) * 16, -34);
    }
    lv_obj_add_event_cb(scr, on_page, LV_EVENT_ALL, NULL);
    show_pick();
}
