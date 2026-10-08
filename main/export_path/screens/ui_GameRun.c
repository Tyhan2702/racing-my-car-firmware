// RMC NIGHT RUN — a small racing game on the gauge's own touch screen, no controller needed.
//
//   tap the left / right half   change lane
//   swipe up                     turbo (uses the turbo meter, refills over time and with blue cells)
//   swipe down                   leave the game
//
// Dodge the traffic in three lanes, pick up yellow tokens for points. The speed keeps rising; the best score is
// kept in NVS. For safety the game only opens with the car standing still and closes itself as soon as the
// car moves (speed from the OBD data cache).
//
// Rendering: one 360x360 RGB565 canvas in PSRAM. The sky is drawn once; every frame redraws only the road area
// below the horizon, row by row, as a classic pseudo-3D road (perspective from 1/distance, alternating kerb and
// lane-dash segments, gentle curves), then the cars and pickups far to near, then the player's car.

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../ui.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "app_obd_dsp/obd_data_cache.h"

#define GW 360
#define GH 360
#define HORIZON 128
#define PLAYER_Y 300
#define PLAYER_D 10.0f                                    // world distance of the player's car
#define K_PROJ ((PLAYER_Y - HORIZON) * PLAYER_D)          // screen y = HORIZON + K_PROJ / distance
#define FAR_D 220.0f                                      // where traffic appears
#define MAX_OBJ 12
#define FRAME_MS 30
#define PARKED_KMH 3                                      // above this the game will not run

typedef enum { G_READY, G_RUN, G_CRASH } game_state_t;
typedef struct { float d; int8_t lane; uint8_t kind; uint32_t color; bool alive; } obj_t;   // kind 0 car, 1 token, 2 turbo cell

static lv_obj_t *s_scr, *s_canvas, *s_score, *s_best, *s_kmh, *s_turbo, *s_title, *s_hint;
static lv_timer_t *s_timer;
static lv_color_t *s_buf;
static game_state_t s_state;
static obj_t s_obj[MAX_OBJ];
static float s_pos, s_kmh_now, s_px, s_curve, s_curve_t, s_spawn, s_turbo_meter, s_turbo_left, s_dist;
static int s_lane, s_score_now, s_best_now, s_shown_score = -1, s_shown_kmh = -1, s_shown_turbo = -1;
static int64_t s_last_us;
static bool s_gestured;

static inline lv_color_t rgb(uint32_t hex) { return lv_color_hex(hex); }
static inline float frand(void) { return (float)(esp_random() & 0xFFFF) / 65535.0f; }

static uint32_t best_load(void)
{
    nvs_handle_t h; uint32_t v = 0;
    if (nvs_open("rmc_game", NVS_READONLY, &h) == ESP_OK) { nvs_get_u32(h, "best", &v); nvs_close(h); }
    return v;
}
static void best_save(uint32_t v)
{
    nvs_handle_t h;
    if (nvs_open("rmc_game", NVS_READWRITE, &h) == ESP_OK) { nvs_set_u32(h, "best", v); nvs_commit(h); nvs_close(h); }
}

static bool car_moving(void)
{
    obd_data_snapshot_t o;
    obd_data_get_snapshot(&o);
    return o.speed > PARKED_KMH;
}

// ---------- drawing into the canvas ----------
static inline void hline(int y, int x0, int x1, lv_color_t c)
{
    if (y < 0 || y >= GH) return;
    if (x0 < 0) x0 = 0;
    if (x1 > GW) x1 = GW;
    lv_color_t *p = s_buf + y * GW;
    for (int x = x0; x < x1; x++) p[x] = c;
}
static void rect(int x0, int y0, int x1, int y1, lv_color_t c)
{
    if (y0 < HORIZON) y0 = HORIZON;
    for (int y = y0; y < y1; y++) hline(y, x0, x1, c);
}
static void disc(int cx, int cy, int r, lv_color_t c)
{
    for (int dy = -r; dy <= r; dy++) {
        int w = (int)sqrtf((float)(r * r - dy * dy));
        if (cy + dy > HORIZON) hline(cy + dy, cx - w, cx + w + 1, c);
    }
}

static float road_cx(int y)    // road centre at screen row y, bending with the current curve
{
    float t = 1.0f - (float)(y - HORIZON) / (GH - HORIZON);
    return GW / 2 + s_curve * t * t * 140.0f;
}
static float road_hw(int y) { return (y - HORIZON) * (150.0f / (PLAYER_Y - HORIZON)); }
static float lane_x(int y, float lane) { return road_cx(y) + lane * road_hw(y) * 0.66f; }

static void draw_sky(void)
{
    for (int y = 0; y < HORIZON; y++) {                       // night gradient into a neon horizon
        float t = (float)y / HORIZON;
        uint8_t r = (uint8_t)(10 + 140 * t * t), g = (uint8_t)(6 + 20 * t), b = (uint8_t)(30 + 90 * t);
        hline(y, 0, GW, lv_color_make(r, g, b));
    }
    for (int i = 0; i < 70; i++) {                              // stars
        int x = esp_random() % GW, y = esp_random() % (HORIZON - 30);
        s_buf[y * GW + x] = rgb(0xFFFFFF);
    }
    for (int dy = -46; dy <= 0; dy++) {                        // striped neon sun sitting on the horizon
        int y = HORIZON - 4 + dy;
        if (dy > -20 && ((-dy) / 4) % 2 == 1) continue;
        int w = (int)sqrtf((float)(46 * 46 - dy * dy));
        float t = (float)(dy + 46) / 46.0f;
        hline(y, GW / 2 - w, GW / 2 + w, lv_color_make(255, (uint8_t)(220 - 160 * t), (uint8_t)(60 + 60 * t)));
    }
}

static void draw_road(void)
{
    const lv_color_t grass[2] = {rgb(0x16082A), rgb(0x1E0C36)}, asphalt[2] = {rgb(0x23232F), rgb(0x282836)};
    const lv_color_t kerb[2] = {rgb(0xE10600), rgb(0xF2F2F2)}, dash = rgb(0xFFDD00), edge = rgb(0xFF2E9A);
    for (int y = HORIZON + 1; y < GH; y++) {
        float d = K_PROJ / (float)(y - HORIZON), z = d + s_pos, hw = road_hw(y), cx = road_cx(y);
        int seg = ((int)(z * 0.25f)) & 1, dseg = ((int)(z * 0.12f)) & 1;
        int kw = (int)(hw * 0.10f) + 1, l = (int)(cx - hw), r = (int)(cx + hw);
        hline(y, 0, GW, grass[seg]);
        hline(y, l - kw - 2, l - kw, edge);                    // neon rails on both sides
        hline(y, r + kw, r + kw + 2, edge);
        hline(y, l - kw, l, kerb[seg]);
        hline(y, r, r + kw, kerb[seg]);
        hline(y, l, r, asphalt[seg]);
        if (dseg) {                                            // lane dashes between the three lanes
            int lw = (int)(hw * 0.025f) + 1;
            for (int s = -1; s <= 1; s += 2) {
                int x = (int)(cx + s * hw / 3.0f);
                hline(y, x - lw, x + lw, dash);
            }
        }
    }
}

static void draw_car(int x, int y, float s, lv_color_t body, bool player)
{
    int w = (int)(64 * s), h = (int)(30 * s), rw = (int)(40 * s), rh = (int)(14 * s);
    if (w < 4) return;
    rect(x - w / 2, y - h, x + w / 2, y, body);                               // body (rear view)
    rect(x - rw / 2, y - h - rh, x + rw / 2, y - h, rgb(0x0A0A0A));          // roof / rear window
    int tl = (int)(10 * s) + 1, th = (int)(5 * s) + 1;
    rect(x - w / 2 + 2, y - h + 3, x - w / 2 + 2 + tl, y - h + 3 + th, rgb(0xFF2020));   // tail lights
    rect(x + w / 2 - 2 - tl, y - h + 3, x + w / 2 - 2, y - h + 3 + th, rgb(0xFF2020));
    rect(x - w / 2 - 2, y - 3, x - w / 2 + (int)(10 * s), y + (int)(4 * s), rgb(0x050505));   // tyres
    rect(x + w / 2 - (int)(10 * s), y - 3, x + w / 2 + 2, y + (int)(4 * s), rgb(0x050505));
    if (player) {
        rect(x - 6, y - h + 10, x + 6, y - h + 16, rgb(0x111111));                // RMC badge
        if (s_turbo_left > 0) {                                                   // turbo flames
            int f = 8 + (esp_random() % 10);
            rect(x - 18, y + 4, x - 10, y + 4 + f, rgb(0xFF8A00));
            rect(x + 10, y + 4, x + 18, y + 4 + f, rgb(0xFF8A00));
            rect(x - 16, y + 4, x - 12, y + 4 + f / 2, rgb(0xFFF2A0));
            rect(x + 12, y + 4, x + 16, y + 4 + f / 2, rgb(0xFFF2A0));
        }
    }
}

static void draw_objects(void)
{
    // far to near, so nearer things cover farther ones
    for (int i = 1; i < MAX_OBJ; i++) {
        obj_t k = s_obj[i]; int j = i - 1;
        while (j >= 0 && s_obj[j].d < k.d) { s_obj[j + 1] = s_obj[j]; j--; }
        s_obj[j + 1] = k;
    }
    for (int i = 0; i < MAX_OBJ; i++) {
        obj_t *o = &s_obj[i];
        if (!o->alive || o->d < 2.0f || o->d > FAR_D) continue;
        int y = HORIZON + (int)(K_PROJ / o->d);
        float s = (float)(y - HORIZON) / (PLAYER_Y - HORIZON);
        int x = (int)lane_x(y, o->lane);
        if (o->kind == 0) draw_car(x, y, s, lv_color_hex(o->color), false);
        else if (o->kind == 1) { disc(x, y - (int)(14 * s), (int)(10 * s) + 1, rgb(0xFFDD00)); disc(x, y - (int)(14 * s), (int)(5 * s), rgb(0xFFF6B0)); }
        else { rect(x - (int)(8 * s), y - (int)(26 * s), x + (int)(8 * s), y, rgb(0x00E5FF)); rect(x - (int)(3 * s), y - (int)(22 * s), x + (int)(3 * s), y - (int)(4 * s), rgb(0xFFFFFF)); }
    }
}

// ---------- game logic ----------
static void spawn(void)
{
    for (int i = 0; i < MAX_OBJ; i++) {
        if (s_obj[i].alive) continue;
        float r = frand();
        static const uint32_t colors[] = {0x2FA8FF, 0xFF3030, 0x30FF60, 0xF2F2F2, 0xFF40FF};
        s_obj[i] = (obj_t){.d = FAR_D, .lane = (int8_t)(esp_random() % 3) - 1,
                           .kind = r < 0.68f ? 0 : r < 0.93f ? 1 : 2, .alive = true};
        if (s_obj[i].kind == 0) {
            s_obj[i].color = colors[esp_random() % 5];
        }
        return;
    }
}

static void set_overlay(const char *title, const char *hint)
{
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_hint, hint);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void reset_run(void)
{
    memset(s_obj, 0, sizeof(s_obj));
    s_pos = 0; s_kmh_now = 120; s_lane = 0; s_px = lane_x(PLAYER_Y, 0); s_curve = 0; s_curve_t = 0;
    s_spawn = 0; s_turbo_meter = 100; s_turbo_left = 0; s_dist = 0; s_score_now = 0;
}

static void start_run(void)
{
    reset_run();
    s_state = G_RUN;
    lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void crash(void)
{
    s_state = G_CRASH;
    if (s_score_now > s_best_now) { s_best_now = s_score_now; best_save((uint32_t)s_best_now); }
    lv_label_set_text_fmt(s_best, "BEST %d", s_best_now);
    set_overlay("CRASH!", "TAP TO RETRY\nSWIPE DOWN TO EXIT");
}

static void leave(void);

static void tick(lv_timer_t *t)
{
    (void)t;
    if (car_moving()) { leave(); return; }               // never while driving
    int64_t now = esp_timer_get_time();
    float dt = (now - s_last_us) / 1e6f;
    s_last_us = now;
    if (dt > 0.1f) dt = 0.1f;

    if (s_state == G_RUN) {
        s_kmh_now += 3.0f * dt;                                  // keeps getting faster
        if (s_kmh_now > 260) s_kmh_now = 260;
        float kmh = s_kmh_now + (s_turbo_left > 0 ? 90 : 0);
        if (s_turbo_left > 0) s_turbo_left -= dt;
        s_turbo_meter += 6.0f * dt;
        if (s_turbo_meter > 100) s_turbo_meter = 100;
        float v = kmh * 0.7f;                                    // world units per second
        s_pos += v * dt;
        s_dist += v * dt;
        s_curve_t += dt * 0.25f;
        s_curve = sinf(s_curve_t) * 0.8f;
        s_spawn -= dt;
        if (s_spawn <= 0) { spawn(); s_spawn = 0.45f + frand() * 0.5f - (s_kmh_now - 120) / 600.0f; }
        float px_target = lane_x(PLAYER_Y, s_lane);
        s_px += (px_target - s_px) * 0.35f;
        for (int i = 0; i < MAX_OBJ; i++) {
            obj_t *o = &s_obj[i];
            if (!o->alive) continue;
            o->d -= v * dt;
            if (o->d < 3.0f) { o->alive = false; continue; }
            if (fabsf(o->d - PLAYER_D) < 1.8f && fabsf(lane_x(PLAYER_Y, o->lane) - s_px) < 44) {
                if (o->kind == 0) { if (s_turbo_left <= 0) { crash(); break; } }   // turbo smashes through
                else if (o->kind == 1) { s_score_now += 100; o->alive = false; }
                else { s_turbo_meter += 40; if (s_turbo_meter > 100) s_turbo_meter = 100; o->alive = false; }
            }
        }
        s_score_now += (int)(v * dt * 0.1f);
    } else {
        s_pos += 40.0f * dt;                                     // slow idle roll behind the menus
        s_px += (lane_x(PLAYER_Y, s_lane) - s_px) * 0.35f;
    }

    draw_road();
    draw_objects();
    draw_car((int)s_px, PLAYER_Y, 1.0f, rgb(0xFFDD00), true);
    lv_area_t a = {0, HORIZON, GW - 1, GH - 1};
    lv_obj_invalidate_area(s_canvas, &a);

    int kmh = (int)(s_kmh_now + (s_turbo_left > 0 ? 90 : 0)), tb = (int)s_turbo_meter;
    if (s_score_now != s_shown_score) { lv_label_set_text_fmt(s_score, "%d", s_score_now); s_shown_score = s_score_now; }
    if (kmh != s_shown_kmh) { lv_label_set_text_fmt(s_kmh, "%d\nKM/H", s_state == G_RUN ? kmh : 0); s_shown_kmh = kmh; }
    if (tb != s_shown_turbo) { lv_label_set_text_fmt(s_turbo, "%d\nTURBO", tb); s_shown_turbo = tb; }
}

static void on_touch(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { s_gestured = false; return; }
    if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        s_gestured = true;
        if (dir == LV_DIR_BOTTOM) { lv_indev_wait_release(lv_indev_get_act()); leave(); return; }
        if (dir == LV_DIR_TOP && s_state == G_RUN && s_turbo_meter >= 50 && s_turbo_left <= 0) {
            s_turbo_meter -= 50; s_turbo_left = 1.5f;
        }
        return;
    }
    if (code == LV_EVENT_RELEASED && !s_gestured) {
        if (s_state != G_RUN) { start_run(); return; }
        lv_point_t p;
        lv_indev_get_point(lv_indev_get_act(), &p);
        if (p.x < GW / 2 && s_lane > -1) s_lane--;
        else if (p.x >= GW / 2 && s_lane < 1) s_lane++;
    }
}

static void on_delete(lv_event_t *e)
{
    (void)e;
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    if (s_buf) { heap_caps_free(s_buf); s_buf = NULL; }
    s_scr = NULL;
}

static void leave(void)
{
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    if (!ui_ScreenPageEasterEgg) ui_ScreenPageEasterEgg_screen_init();
    lv_scr_load_anim(ui_ScreenPageEasterEgg, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);   // deletes the game screen
}

static lv_obj_t *label(const lv_font_t *font, uint32_t color, lv_align_t align, int x, int y)
{
    lv_obj_t *l = lv_label_create(s_scr);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, rgb(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, align, x, y);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static void toast(const char *text)
{
    lv_obj_t *l = lv_label_create(lv_layer_top());
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(l, rgb(0x111111), 0);
    lv_obj_set_style_bg_color(l, rgb(0xFFDD00), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(l, 8, 0);
    lv_obj_set_style_radius(l, 12, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
    lv_obj_del_delayed(l, 1600);
}

void ui_game_open(void)
{
    if (s_scr) return;
    if (car_moving()) { toast("PARK TO PLAY"); return; }
    s_buf = heap_caps_malloc(GW * GH * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_buf) { toast("NOT ENOUGH MEMORY"); return; }

    s_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, rgb(0x000000), 0);
    lv_obj_add_event_cb(s_scr, on_delete, LV_EVENT_DELETE, NULL);

    s_canvas = lv_canvas_create(s_scr);
    lv_canvas_set_buffer(s_canvas, s_buf, GW, GH, LV_IMG_CF_TRUE_COLOR);
    lv_obj_center(s_canvas);
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_scr, on_touch, LV_EVENT_ALL, NULL);

    s_score = label(&ui_font_FontTypoderSize36, 0xFFFFFF, LV_ALIGN_TOP_MID, 0, 34);
    s_best = label(&ui_font_FontTypoderSize16, 0xFFDD00, LV_ALIGN_TOP_MID, 0, 76);
    s_kmh = label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_LEFT_MID, 22, -40);
    s_turbo = label(&ui_font_FontTypoderSize16, 0x00E5FF, LV_ALIGN_RIGHT_MID, -22, -40);
    s_title = label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -14);
    s_hint = label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 34);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 8, 0);

    s_best_now = (int)best_load();
    lv_label_set_text_fmt(s_best, "BEST %d", s_best_now);
    lv_label_set_text(s_score, "0");
    s_shown_score = s_shown_kmh = s_shown_turbo = -1;
    reset_run();
    s_state = G_READY;
    set_overlay("RMC NIGHT RUN", "TAP LEFT / RIGHT: LANE\nSWIPE UP: TURBO\nTAP TO START");

    draw_sky();
    draw_road();
    s_last_us = esp_timer_get_time();
    s_timer = lv_timer_create(tick, FRAME_MS, NULL);
    lv_scr_load_anim(s_scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
}
