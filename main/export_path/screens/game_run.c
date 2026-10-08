// NIGHT RUN — dodge the traffic on a neon road. Tap the left / right half to change lane, swipe up for turbo.
// Pseudo-3D road redrawn row by row below the horizon each frame (the sky is drawn once); traffic, yellow tokens
// (+100) and blue turbo cells come toward the player's yellow Racing My Car car; the speed keeps rising.

#include <math.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"
#include "esp_random.h"

#define HORIZON 128
#define PLAYER_Y 300
#define PLAYER_D 10.0f
#define K_PROJ ((PLAYER_Y - HORIZON) * PLAYER_D)
#define FAR_D 220.0f
#define MAX_OBJ 12

typedef enum { R_READY, R_RUN, R_CRASH } run_state_t;
typedef struct { float d; int8_t lane; uint8_t kind; uint32_t color; bool alive; } obj_t;   // kind 0 car, 1 token, 2 turbo

static run_state_t s_state;
static obj_t s_obj[MAX_OBJ];
static float s_pos, s_kmh, s_px, s_curve, s_curve_t, s_spawn, s_turbo, s_turbo_left;
static int s_lane, s_score, s_shown_score, s_shown_kmh, s_shown_turbo;
static lv_obj_t *s_score_l, *s_best_l, *s_kmh_l, *s_turbo_l, *s_title, *s_hint;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static void rect(int x0, int y0, int x1, int y1, lv_color_t c) { gc_rect(x0, y0 < HORIZON ? HORIZON : y0, x1, y1, c); }
static float road_cx(int y) { float t = 1.0f - (float)(y - HORIZON) / (GC_H - HORIZON); return GC_W / 2 + s_curve * t * t * 140.0f; }
static float road_hw(int y) { return (y - HORIZON) * (150.0f / (PLAYER_Y - HORIZON)); }
static float lane_x(int y, float lane) { return road_cx(y) + lane * road_hw(y) * 0.66f; }

static void draw_sky(void)
{
    for (int y = 0; y < HORIZON; y++) {
        float t = (float)y / HORIZON;
        gc_hline(y, 0, GC_W, lv_color_make((uint8_t)(10 + 140 * t * t), (uint8_t)(6 + 20 * t), (uint8_t)(30 + 90 * t)));
    }
    for (int i = 0; i < 70; i++) gc_buf[(esp_random() % (HORIZON - 30)) * GC_W + esp_random() % GC_W] = rgb(0xFFFFFF);
    for (int dy = -46; dy <= 0; dy++) {
        if (dy > -20 && ((-dy) / 4) % 2 == 1) continue;
        int w = (int)sqrtf((float)(46 * 46 - dy * dy));
        float t = (float)(dy + 46) / 46.0f;
        gc_hline(HORIZON - 4 + dy, GC_W / 2 - w, GC_W / 2 + w, lv_color_make(255, (uint8_t)(220 - 160 * t), (uint8_t)(60 + 60 * t)));
    }
}

static void draw_road(void)
{
    const lv_color_t grass[2] = {rgb(0x16082A), rgb(0x1E0C36)}, asphalt[2] = {rgb(0x23232F), rgb(0x282836)};
    const lv_color_t kerb[2] = {rgb(0xE10600), rgb(0xF2F2F2)}, dash = rgb(0xFFDD00), edge = rgb(0xFF2E9A);
    for (int y = HORIZON + 1; y < GC_H; y++) {
        float d = K_PROJ / (float)(y - HORIZON), z = d + s_pos, hw = road_hw(y), cx = road_cx(y);
        int seg = ((int)(z * 0.25f)) & 1, dseg = ((int)(z * 0.12f)) & 1, kw = (int)(hw * 0.10f) + 1, l = (int)(cx - hw), r = (int)(cx + hw);
        gc_hline(y, 0, GC_W, grass[seg]);
        gc_hline(y, l - kw - 2, l - kw, edge);
        gc_hline(y, r + kw, r + kw + 2, edge);
        gc_hline(y, l - kw, l, kerb[seg]);
        gc_hline(y, r, r + kw, kerb[seg]);
        gc_hline(y, l, r, asphalt[seg]);
        if (dseg) {
            int lw = (int)(hw * 0.025f) + 1;
            for (int s = -1; s <= 1; s += 2) { int x = (int)(cx + s * hw / 3.0f); gc_hline(y, x - lw, x + lw, dash); }
        }
    }
}

static void draw_car(int x, int y, float s, lv_color_t body, bool player)
{
    int w = (int)(64 * s), h = (int)(30 * s), rw = (int)(40 * s), rh = (int)(14 * s);
    if (w < 4) return;
    rect(x - w / 2, y - h, x + w / 2, y, body);
    rect(x - rw / 2, y - h - rh, x + rw / 2, y - h, rgb(0x0A0A0A));
    int tl = (int)(10 * s) + 1, th = (int)(5 * s) + 1;
    rect(x - w / 2 + 2, y - h + 3, x - w / 2 + 2 + tl, y - h + 3 + th, rgb(0xFF2020));
    rect(x + w / 2 - 2 - tl, y - h + 3, x + w / 2 - 2, y - h + 3 + th, rgb(0xFF2020));
    rect(x - w / 2 - 2, y - 3, x - w / 2 + (int)(10 * s), y + (int)(4 * s), rgb(0x050505));
    rect(x + w / 2 - (int)(10 * s), y - 3, x + w / 2 + 2, y + (int)(4 * s), rgb(0x050505));
    if (player) {
        rect(x - 6, y - h + 10, x + 6, y - h + 16, rgb(0x111111));
        if (s_turbo_left > 0) {
            int f = 8 + (esp_random() % 10);
            rect(x - 18, y + 4, x - 10, y + 4 + f, rgb(0xFF8A00));
            rect(x + 10, y + 4, x + 18, y + 4 + f, rgb(0xFF8A00));
        }
    }
}

static void draw_objects(void)
{
    for (int i = 1; i < MAX_OBJ; i++) {                     // far to near
        obj_t k = s_obj[i]; int j = i - 1;
        while (j >= 0 && s_obj[j].d < k.d) { s_obj[j + 1] = s_obj[j]; j--; }
        s_obj[j + 1] = k;
    }
    for (int i = 0; i < MAX_OBJ; i++) {
        obj_t *o = &s_obj[i];
        if (!o->alive || o->d < 2.0f || o->d > FAR_D) continue;
        int y = HORIZON + (int)(K_PROJ / o->d), x = (int)lane_x(y, o->lane);
        float s = (float)(y - HORIZON) / (PLAYER_Y - HORIZON);
        if (o->kind == 0) draw_car(x, y, s, rgb(o->color), false);
        else if (o->kind == 1) { gc_disc(x, y - (int)(14 * s), (int)(10 * s) + 1, rgb(0xFFDD00)); gc_disc(x, y - (int)(14 * s), (int)(5 * s), rgb(0xFFF6B0)); }
        else { rect(x - (int)(8 * s), y - (int)(26 * s), x + (int)(8 * s), y, rgb(0x00E5FF)); rect(x - (int)(3 * s), y - (int)(22 * s), x + (int)(3 * s), y - (int)(4 * s), rgb(0xFFFFFF)); }
    }
}

static void spawn(void)
{
    static const uint32_t colors[] = {0x2FA8FF, 0xFF3030, 0x30FF60, 0xF2F2F2, 0xFF40FF};
    for (int i = 0; i < MAX_OBJ; i++) {
        if (s_obj[i].alive) continue;
        float r = gc_rand();
        s_obj[i] = (obj_t){.d = FAR_D, .lane = (int8_t)(esp_random() % 3) - 1, .kind = r < 0.68f ? 0 : r < 0.93f ? 1 : 2,
                           .color = colors[esp_random() % 5], .alive = true};
        return;
    }
}

static void overlay(const char *title, const char *hint)
{
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_hint, hint);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void reset_run(void)
{
    memset(s_obj, 0, sizeof(s_obj));
    s_pos = 0; s_kmh = 120; s_lane = 0; s_px = lane_x(PLAYER_Y, 0); s_curve = 0; s_curve_t = 0;
    s_spawn = 0; s_turbo = 100; s_turbo_left = 0; s_score = 0;
}

static void begin(void)
{
    s_score_l = gc_label(&ui_font_FontTypoderSize36, 0xFFFFFF, LV_ALIGN_TOP_MID, 0, 34);
    s_best_l = gc_label(&ui_font_FontTypoderSize16, 0xFFDD00, LV_ALIGN_TOP_MID, 0, 76);
    s_kmh_l = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_LEFT_MID, 22, -40);
    s_turbo_l = gc_label(&ui_font_FontTypoderSize16, 0x00E5FF, LV_ALIGN_RIGHT_MID, -22, -40);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -14);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 34);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 8, 0);
    lv_label_set_text_fmt(s_best_l, "BEST %ld", (long)gc_best());
    lv_label_set_text(s_score_l, "0");
    s_shown_score = s_shown_kmh = s_shown_turbo = -1;
    reset_run();
    s_state = R_READY;
    overlay("NIGHT RUN", "TAP LEFT / RIGHT: LANE\nSWIPE UP: TURBO\nTAP TO START");
    draw_sky();
}

static void crash(void)
{
    s_state = R_CRASH;
    if (gc_record(s_score)) overlay("NEW RECORD!", "TAP TO RETRY\nSWIPE DOWN TO EXIT");
    else overlay("CRASH!", "TAP TO RETRY\nSWIPE DOWN TO EXIT");
    lv_label_set_text_fmt(s_best_l, "BEST %ld", (long)gc_best());
}

static void frame(float dt)
{
    if (s_state == R_RUN) {
        s_kmh += 3.0f * dt;
        if (s_kmh > 260) s_kmh = 260;
        float kmh = s_kmh + (s_turbo_left > 0 ? 90 : 0), v = kmh * 0.7f;
        if (s_turbo_left > 0) s_turbo_left -= dt;
        s_turbo += 6.0f * dt;
        if (s_turbo > 100) s_turbo = 100;
        s_pos += v * dt;
        s_curve_t += dt * 0.25f;
        s_curve = sinf(s_curve_t) * 0.8f;
        s_spawn -= dt;
        if (s_spawn <= 0) { spawn(); s_spawn = 0.45f + gc_rand() * 0.5f - (s_kmh - 120) / 600.0f; }
        s_px += (lane_x(PLAYER_Y, s_lane) - s_px) * 0.35f;
        for (int i = 0; i < MAX_OBJ; i++) {
            obj_t *o = &s_obj[i];
            if (!o->alive) continue;
            o->d -= v * dt;
            if (o->d < 3.0f) { o->alive = false; continue; }
            if (fabsf(o->d - PLAYER_D) < 1.8f && fabsf(lane_x(PLAYER_Y, o->lane) - s_px) < 44) {
                if (o->kind == 0) { if (s_turbo_left <= 0) { crash(); break; } }
                else if (o->kind == 1) { s_score += 100; o->alive = false; }
                else { s_turbo += 40; if (s_turbo > 100) s_turbo = 100; o->alive = false; }
            }
        }
        if (s_state == R_RUN) s_score += (int)(v * dt * 0.1f);
    } else {
        s_pos += 40.0f * dt;
        s_px += (lane_x(PLAYER_Y, s_lane) - s_px) * 0.35f;
    }
    draw_road();
    draw_objects();
    draw_car((int)s_px, PLAYER_Y, 1.0f, rgb(0xFFDD00), true);
    gc_dirty(0, HORIZON, GC_W - 1, GC_H - 1);
    int kmh = s_state == R_RUN ? (int)(s_kmh + (s_turbo_left > 0 ? 90 : 0)) : 0, tb = (int)s_turbo;
    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
    if (kmh != s_shown_kmh) { lv_label_set_text_fmt(s_kmh_l, "%d\nKM/H", kmh); s_shown_kmh = kmh; }
    if (tb != s_shown_turbo) { lv_label_set_text_fmt(s_turbo_l, "%d\nTURBO", tb); s_shown_turbo = tb; }
}

static void input(gc_input_t in, lv_point_t at)
{
    if (in == GC_TAP) {
        if (s_state != R_RUN) {
            reset_run();
            s_state = R_RUN;
            lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        if (at.x < GC_W / 2 && s_lane > -1) s_lane--;
        else if (at.x >= GC_W / 2 && s_lane < 1) s_lane++;
    } else if (in == GC_SWIPE_UP && s_state == R_RUN && s_turbo >= 50 && s_turbo_left <= 0) {
        s_turbo -= 50;
        s_turbo_left = 1.5f;
    }
}

static void icon(lv_color_t *b, int n)
{
    for (int y = 0; y < n; y++) {                                // sky to road
        lv_color_t c = y < n * 0.45f ? lv_color_make((uint8_t)(20 + 150 * y / n), 10, (uint8_t)(50 + 80 * y / n)) : rgb(0x1E0C36);
        for (int x = 0; x < n; x++) b[y * n + x] = c;
    }
    gc_disc_in(b, n, n, n / 2, (int)(n * 0.45f), n / 6, rgb(0xFF7A3D));
    int top = (int)(n * 0.45f);
    for (int y = top; y < n; y++) {                              // road in perspective with kerbs
        float t = (float)(y - top) / (n - top);
        int hw = (int)(6 + t * n * 0.42f), cx = n / 2;
        lv_color_t k = ((y / 4) & 1) ? rgb(0xE10600) : rgb(0xF2F2F2);
        gc_rect_in(b, n, n, cx - hw - 4, y, cx - hw, y + 1, k);
        gc_rect_in(b, n, n, cx + hw, y, cx + hw + 4, y + 1, k);
        gc_rect_in(b, n, n, cx - hw, y, cx + hw, y + 1, rgb(0x26263A));
    }
    gc_rect_in(b, n, n, n / 2 - 18, n - 34, n / 2 + 18, n - 16, rgb(0xFFDD00));   // the yellow car
    gc_rect_in(b, n, n, n / 2 - 11, n - 42, n / 2 + 11, n - 34, rgb(0x0A0A0A));
    gc_rect_in(b, n, n, n / 2 - 16, n - 31, n / 2 - 10, n - 28, rgb(0xFF2020));
    gc_rect_in(b, n, n, n / 2 + 10, n - 31, n / 2 + 16, n - 28, rgb(0xFF2020));
}

const game_def_t game_run = {
    .name = "NIGHT RUN", .hint = "TAP LEFT / RIGHT TO DODGE\nSWIPE UP FOR TURBO", .key = "run", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
