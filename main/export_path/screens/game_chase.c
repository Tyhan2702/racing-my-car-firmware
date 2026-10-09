// HIGHWAY CHASE — a top-down highway: drag to steer the yellow car between the four lanes, pass the traffic and
// pick up the red fuel cans before the tank runs dry. The road speeds up all the time; the score is the distance.
// The whole road scrolls, so every frame is redrawn. Same rules, sizes and colours as the browser copy
// (web/game-arcade2.js, highwayChase).

#include <math.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"

#define L 84                   // the road
#define R 276
#define PY 286                 // your car's y
#define CW 22                  // car size
#define CH 40
#define MAX_CARS 32
#define MAX_CANS 6
#define X_MIN (L + CW / 2 + 2)
#define X_MAX (R - CW / 2 - 2)

typedef enum { C_READY, C_RUN, C_OVER } chase_state_t;
typedef struct { float x, y, v; uint32_t c; } car_t;
typedef struct { float x, y; } can_t;

static const uint32_t CARC[5] = {0xFF3030, 0x2F9BFF, 0x2FE06B, 0xFF3DF2, 0xF2F2F2};
static const float LANES[4] = {L + 24, L + 72, R - 72, R - 24};

static chase_state_t s_state;
static car_t s_car[MAX_CARS];
static can_t s_can[MAX_CANS];
static int s_n_car, s_n_can;
static float s_px, s_tx, s_speed, s_dist, s_fuel, s_spawn, s_can_t, s_scroll;
static int s_shown_dist, s_shown_kmh;
static lv_obj_t *s_dist_l, *s_kmh_l, *s_fuel_l, *s_title, *s_hint;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static int px(float v) { return (int)floorf(v + 0.5f); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static void rectf(float x, float y, float w, float h, lv_color_t c) { gc_rect(px(x), px(y), px(x + w), px(y + h), c); }

// a filled horizontal span: the pixels whose centres lie in xa..xb
static void span(int y, float xa, float xb, lv_color_t c) { gc_hline(y, (int)ceilf(xa - 0.5f), (int)floorf(xb - 0.5f) + 1, c); }

static void rrect(float x, float y, float w, float h, float r, lv_color_t c)   // rounded rectangle
{
    for (int py = (int)floorf(y); py < (int)ceilf(y + h); py++) {
        float cy = py + 0.5f, d = 0;
        if (cy < y || cy > y + h) continue;
        if (cy < y + r) d = y + r - cy;
        else if (cy > y + h - r) d = cy - (y + h - r);
        float in = d > 0 ? r - sqrtf(fmaxf(0, r * r - d * d)) : 0;
        span(py, x + in, x + w - in, c);
    }
}

static int rand_lane(void)
{
    int k = (int)(gc_rand() * 4);
    return k > 3 ? 3 : k;
}

static void reset(void)
{
    s_px = s_tx = GC_W / 2;
    s_n_car = s_n_can = 0;
    s_speed = 160; s_dist = 0; s_fuel = 100; s_spawn = 0.6f; s_can_t = 4; s_scroll = 0;
}

static void overlay(const char *title, const char *hint)
{
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_hint, hint);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static int score(void) { return (int)floorf(s_dist / 10); }

static void game_over(void)
{
    s_state = C_OVER;
    int sc = score();
    bool rec = gc_record(sc);
    long best = (long)gc_best();
    lv_label_set_text(s_title, rec ? "NEW RECORD!" : "GAME OVER");
    if (best) lv_label_set_text_fmt(s_hint, "SCORE %d   BEST %ld\nTAP TO PLAY AGAIN", sc, best);
    else lv_label_set_text_fmt(s_hint, "SCORE %d\nTAP TO PLAY AGAIN", sc);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void begin(void)
{
    s_fuel_l = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, -84, 50 - GC_H / 2);   // right edge by the bar
    lv_label_set_text(s_fuel_l, "FUEL");
    s_dist_l = gc_label(&ui_font_FontTypoderSize20, 0xFFFFFF, LV_ALIGN_CENTER, 0, 74 - GC_H / 2);
    s_kmh_l = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 334 - GC_H / 2);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -20);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 18);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 8, 0);
    reset();
    s_state = C_READY;
    s_shown_dist = s_shown_kmh = -1;
    overlay("HIGHWAY CHASE", "DRAG TO STEER - GRAB FUEL\nTOUCH TO START");
}

static void step(float dt)
{
    s_speed = fminf(380, s_speed + 6 * dt);
    s_dist += s_speed * dt;
    s_scroll = fmodf(s_scroll + s_speed * dt, 40);
    s_fuel -= dt * 4.2f;
    s_px += (s_tx - s_px) * fminf(1, dt * 10);

    s_spawn -= dt;
    if (s_spawn <= 0) {
        float x = LANES[rand_lane()];
        bool blocked = false;
        for (int i = 0; i < s_n_car; i++) if (s_car[i].x == x && s_car[i].y < 20) blocked = true;
        if (!blocked && s_n_car < MAX_CARS) {
            float v = 0.35f + gc_rand() * 0.3f;
            int k = (int)(gc_rand() * 5);
            s_car[s_n_car++] = (car_t){.x = x, .y = -30, .v = v, .c = CARC[k > 4 ? 4 : k]};
        }
        s_spawn = fmaxf(0.35f, 0.9f - s_speed / 900) + gc_rand() * 0.3f;
    }
    s_can_t -= dt;
    if (s_can_t <= 0) {
        if (s_n_can < MAX_CANS) s_can[s_n_can++] = (can_t){.x = LANES[rand_lane()], .y = -20};
        s_can_t = 5 + gc_rand() * 3;
    }
    for (int i = 0; i < s_n_car; i++) s_car[i].y += s_speed * s_car[i].v * dt;
    for (int i = 0; i < s_n_can; i++) s_can[i].y += s_speed * dt;
    int n = 0;                                            // drop what left the screen (keeping the order)
    for (int i = 0; i < s_n_car; i++) if (s_car[i].y < GC_H + 40) s_car[n++] = s_car[i];
    s_n_car = n;
    n = 0;
    for (int i = 0; i < s_n_can; i++) if (s_can[i].y < GC_H + 20) s_can[n++] = s_can[i];
    s_n_can = n;
    for (int i = 0; i < s_n_can; i++)
        if (fabsf(s_can[i].x - s_px) < 20 && fabsf(s_can[i].y - PY) < 26) { s_fuel = fminf(100, s_fuel + 35); s_can[i].y = GC_H + 99; }
    bool hit = false;
    for (int i = 0; i < s_n_car; i++) if (fabsf(s_car[i].x - s_px) < CW - 2 && fabsf(s_car[i].y - PY) < CH - 4) hit = true;
    if (hit || s_fuel <= 0) game_over();
}

static void draw_car(float x, float y, uint32_t c, bool me)
{
    const lv_color_t glass = rgb(0x10131A), tyre = rgb(0x111111);
    rrect(x - CW / 2, y - CH / 2, CW, CH, 5, rgb(c));
    rectf(x - CW / 2 + 3, y - CH / 2 + (me ? 7 : 24), CW - 6, 8, glass);
    rectf(x - CW / 2 + 3, y - CH / 2 + (me ? 26 : 6), CW - 6, 6, glass);
    rectf(x - CW / 2 - 2, y - CH / 2 + 6, 3, 8, tyre);
    rectf(x + CW / 2 - 1, y - CH / 2 + 6, 3, 8, tyre);
    rectf(x - CW / 2 - 2, y + CH / 2 - 14, 3, 8, tyre);
    rectf(x + CW / 2 - 1, y + CH / 2 - 14, 3, 8, tyre);
}

static void frame(float dt)
{
    if (s_state == C_RUN) step(dt);

    gc_fill(rgb(0x1D3B1F));                               // grass with darker stripes
    for (float y = -40 + s_scroll; y < GC_H; y += 40) {
        rectf(0, y, L - 8, 20, rgb(0x234A26));
        rectf(R + 8, y + 20, GC_W - R - 8, 20, rgb(0x234A26));
    }
    gc_rect(L, 0, R, GC_H, rgb(0x2A2D34));
    for (float y = -40 + s_scroll; y < GC_H; y += 40) {   // kerbs and lane lines
        rectf(L - 8, y, 8, 20, rgb(0xE10600));
        rectf(L - 8, y + 20, 8, 20, rgb(0xF2F2F2));
        rectf(R, y, 8, 20, rgb(0xF2F2F2));
        rectf(R, y + 20, 8, 20, rgb(0xE10600));
        rectf(L + 48 - 2, y + 4, 4, 20, rgb(0xCFCFCF));
        rectf(GC_W / 2 - 2, y + 4, 4, 20, rgb(0xFFDD00));
        rectf(R - 48 - 2, y + 4, 4, 20, rgb(0xCFCFCF));
    }
    for (int i = 0; i < s_n_can; i++) {                   // fuel cans with an F
        float x = s_can[i].x, y = s_can[i].y;
        rectf(x - 8, y - 10, 16, 20, rgb(0xFF3030));
        rectf(x - 4, y - 14, 8, 5, rgb(0xFF3030));
        rectf(x - 3, y - 3, 2, 9, rgb(0xFFFFFF));
        rectf(x - 3, y - 3, 6, 2, rgb(0xFFFFFF));
        rectf(x - 3, y + 1, 5, 2, rgb(0xFFFFFF));
    }
    for (int i = 0; i < s_n_car; i++) draw_car(s_car[i].x, s_car[i].y, s_car[i].c, false);
    draw_car(s_px, PY, 0xFFDD00, true);
    gc_rect(GC_W / 2 - 50, 46, GC_W / 2 + 60, 54, rgb(0x111111));   // the fuel gauge
    rectf(GC_W / 2 - 50, 46, 110 * clampf(s_fuel, 0, 100) / 100, 8, rgb(s_fuel < 25 ? 0xFF3030 : 0x2FE06B));
    gc_dirty_all();

    int d = score(), kmh = (int)lroundf(s_speed * 0.6f);
    if (d != s_shown_dist) { lv_label_set_text_fmt(s_dist_l, "%d", d); s_shown_dist = d; }
    if (kmh != s_shown_kmh) { lv_label_set_text_fmt(s_kmh_l, "%d KM/H", kmh); s_shown_kmh = kmh; }
}

static void input(gc_input_t in, lv_point_t at)
{
    if (s_state != C_RUN) {
        if (in != GC_PRESS) return;
        if (s_state == C_OVER) reset();
        s_state = C_RUN;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (in == GC_PRESS || in == GC_DRAG) s_tx = clampf(at.x, X_MIN, X_MAX);
}

static void icon(lv_color_t *b, int n)
{
    int l = 30, r = n - 30;
    for (int i = 0; i < n * n; i++) b[i] = rgb(0x1D3B1F);
    for (int y = -10; y < n; y += 40) {
        gc_rect_in(b, n, n, 0, y, l - 6, y + 20, rgb(0x234A26));
        gc_rect_in(b, n, n, r + 6, y + 20, n, y + 40, rgb(0x234A26));
    }
    gc_rect_in(b, n, n, l, 0, r, n, rgb(0x2A2D34));
    for (int y = -10; y < n; y += 24) {
        gc_rect_in(b, n, n, l - 6, y, l, y + 12, rgb(0xE10600));
        gc_rect_in(b, n, n, l - 6, y + 12, l, y + 24, rgb(0xF2F2F2));
        gc_rect_in(b, n, n, r, y, r + 6, y + 12, rgb(0xF2F2F2));
        gc_rect_in(b, n, n, r, y + 12, r + 6, y + 24, rgb(0xE10600));
        gc_rect_in(b, n, n, n / 2 - 2, y + 2, n / 2 + 2, y + 14, rgb(0xFFDD00));
    }
    struct { int x, y; uint32_t c; bool me; } cars[3] = {{l + 18, 30, 0xFF3030, false}, {r - 18, 56, 0x2F9BFF, false}, {l + 18, 98, 0xFFDD00, true}};
    for (int k = 0; k < 3; k++) {
        int x = cars[k].x, y = cars[k].y;
        gc_rect_in(b, n, n, x - 13, y - 14, x - 10, y - 6, rgb(0x111111));
        gc_rect_in(b, n, n, x + 10, y - 14, x + 13, y - 6, rgb(0x111111));
        gc_rect_in(b, n, n, x - 13, y + 6, x - 10, y + 14, rgb(0x111111));
        gc_rect_in(b, n, n, x + 10, y + 6, x + 13, y + 14, rgb(0x111111));
        gc_rect_in(b, n, n, x - 11, y - 18, x + 11, y + 18, rgb(cars[k].c));
        gc_rect_in(b, n, n, x - 9, y - 20, x + 9, y + 20, rgb(cars[k].c));
        int g1 = cars[k].me ? 7 : 24, g2 = cars[k].me ? 26 : 6;
        gc_rect_in(b, n, n, x - 8, y - 20 + g1, x + 8, y - 12 + g1, rgb(0x10131A));
        gc_rect_in(b, n, n, x - 8, y - 20 + g2, x + 8, y - 14 + g2, rgb(0x10131A));
    }
}

const game_def_t game_chase = {
    .name = "HIGHWAY CHASE", .hint = "DRAG TO STEER\nGRAB THE FUEL CANS", .key = "chase", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
