// ROCK GUARD — the yellow ship sits in the middle of the dial and keeps firing where you touch (or drag). Rocks fly
// in from the rim; a big one (20 points) splits into two small ones (50 points each) that fly off faster. A rock
// that reaches the ship costs a life (the ship blinks, safe, for 1.2 s); three lives. Rocks come quicker and
// faster with time. Everything moves, so every frame is redrawn; the rocks (9-point lumps) and the turned ship are
// filled polygons. Same rules, sizes and colours as the browser copy (web/game-arcade2.js, rockGuard).

#include "esp_attr.h"
#include <math.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"

#define CX 180
#define CY 180
#define SR 12                  // the ship's radius for hits
#define PI_F 3.14159265f
#define TAU_F (2 * PI_F)
#define MAX_SHOTS 12
#define MAX_ROCKS 48
#define N_STARS 50
#define RPTS 9

typedef enum { G_READY, G_RUN, G_OVER } rocks_state_t;
typedef struct { float x, y, vx, vy; } shot_t;
typedef struct { float x, y, r, vx, vy, spin, pts[RPTS]; bool dead; } rock_t;

static rocks_state_t s_state;
static EXT_RAM_BSS_ATTR shot_t s_shot[MAX_SHOTS];
static EXT_RAM_BSS_ATTR rock_t s_rock[MAX_ROCKS];
static int s_n_shot, s_n_rock;
static float s_aim, s_spawn, s_fire, s_hurt, s_t, s_lvl;
static int s_score, s_left, s_shown_score;
static EXT_RAM_BSS_ATTR int16_t s_star[N_STARS][2];
static lv_color_t s_ring_c;
static lv_obj_t *s_score_l, *s_title, *s_hint;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static int px(float v) { return (int)floorf(v + 0.5f); }
static void span(int y, float xa, float xb, lv_color_t c) { gc_hline(y, (int)ceilf(xa - 0.5f), (int)floorf(xb - 0.5f) + 1, c); }
static lv_color_t mix(uint32_t a, uint32_t b, float t)   // a blended toward b by t (canvas alpha)
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) out |= (uint32_t)(((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t + 0.5f) << sh;
    return rgb(out);
}

static void discf(float cx, float cy, float r, lv_color_t c)
{
    for (int y = (int)floorf(cy - r); y <= (int)ceilf(cy + r); y++) {
        float dy = y + 0.5f - cy;
        if (dy * dy > r * r) continue;
        float h = sqrtf(r * r - dy * dy);
        span(y, cx - h, cx + h, c);
    }
}

static void ringf(float cx, float cy, float r0, float r1, lv_color_t c)   // a full ring, r0..r1
{
    for (int y = (int)floorf(cy - r1); y <= (int)ceilf(cy + r1); y++) {
        if (y < 0 || y >= GC_H) continue;
        for (int x = (int)floorf(cx - r1); x <= (int)ceilf(cx + r1); x++) {
            if (x < 0 || x >= GC_W) continue;
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d2 = dx * dx + dy * dy;
            if (d2 >= r0 * r0 && d2 <= r1 * r1) gc_buf[y * GC_W + x] = c;
        }
    }
}

// a filled polygon (even-odd, sampled at pixel centres), at most 12 corners
static void poly(const float *xs, const float *ys, int n, lv_color_t c)
{
    float y0 = ys[0], y1 = ys[0];
    for (int i = 1; i < n; i++) { y0 = fminf(y0, ys[i]); y1 = fmaxf(y1, ys[i]); }
    int ya = (int)floorf(y0), yb = (int)ceilf(y1);
    if (ya < 0) ya = 0;
    if (yb > GC_H - 1) yb = GC_H - 1;
    for (int y = ya; y <= yb; y++) {
        float yc = y + 0.5f, cross[12];
        int m = 0;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            if ((ys[i] <= yc && yc < ys[j]) || (ys[j] <= yc && yc < ys[i]))
                if (m < 12) cross[m++] = xs[i] + (yc - ys[i]) * (xs[j] - xs[i]) / (ys[j] - ys[i]);
        }
        for (int a = 1; a < m; a++)                       // sort the crossings
            for (int b = a; b > 0 && cross[b - 1] > cross[b]; b--) { float t = cross[b]; cross[b] = cross[b - 1]; cross[b - 1] = t; }
        for (int k = 0; k + 1 < m; k += 2) span(y, cross[k], cross[k + 1], c);
    }
}

static void add_rock(float x, float y, float r, float a, float s)
{
    if (s_n_rock >= MAX_ROCKS) return;
    rock_t *k = &s_rock[s_n_rock++];
    *k = (rock_t){.x = x, .y = y, .r = r, .vx = cosf(a) * s, .vy = sinf(a) * s, .spin = gc_rand() * TAU_F};
    for (int i = 0; i < RPTS; i++) k->pts[i] = 0.75f + gc_rand() * 0.3f;
}

static void reset(void)
{
    s_aim = -PI_F / 2; s_n_shot = s_n_rock = 0;
    s_spawn = 0.5f; s_fire = 0; s_score = 0; s_left = 3; s_hurt = 0; s_lvl = 0;
}

static void overlay(const char *title, const char *hint)
{
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_hint, hint);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void game_over(void)
{
    s_state = G_OVER;
    bool rec = gc_record(s_score);
    long best = (long)gc_best();
    lv_label_set_text(s_title, rec ? "NEW RECORD!" : "GAME OVER");
    if (best) lv_label_set_text_fmt(s_hint, "SCORE %d   BEST %ld\nTAP TO PLAY AGAIN", s_score, best);
    else lv_label_set_text_fmt(s_hint, "SCORE %d\nTAP TO PLAY AGAIN", s_score);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void begin(void)
{
    for (int i = 0; i < N_STARS; i++) {                   // the fixed star field (a sunflower spiral)
        float a = i * 2.39996f, rr = 20 + ((i * 53) % 160);
        s_star[i][0] = (int16_t)px(CX + cosf(a) * rr);
        s_star[i][1] = (int16_t)px(CY + sinf(a) * rr);
    }
    s_ring_c = mix(0x04030A, 0xFFDD00, 0.25f);
    s_score_l = gc_label(&ui_font_FontTypoderSize20, 0xFFFFFF, LV_ALIGN_CENTER, 0, 40 - GC_H / 2);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -20);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 18);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 8, 0);
    s_t = 0;
    reset();
    s_state = G_READY;
    s_shown_score = -1;
    overlay("ROCK GUARD", "TOUCH WHERE TO SHOOT\nTOUCH TO START");
}

static void step(float dt)
{
    s_lvl += dt;
    s_fire -= dt;
    if (s_fire <= 0) {
        if (s_n_shot < MAX_SHOTS)
            s_shot[s_n_shot++] = (shot_t){CX + cosf(s_aim) * 18, CY + sinf(s_aim) * 18, cosf(s_aim) * 380, sinf(s_aim) * 380};
        s_fire = 0.22f;
    }
    s_spawn -= dt;
    if (s_spawn <= 0) {
        float a = gc_rand() * TAU_F, toward = a + PI_F + (gc_rand() - 0.5f) * 0.6f;
        add_rock(CX + cosf(a) * 190, CY + sinf(a) * 190, 18, toward, 40 + s_lvl * 0.9f + gc_rand() * 25);
        s_spawn = fmaxf(0.55f, 1.6f - s_lvl * 0.012f);
    }
    int n = 0;
    for (int i = 0; i < s_n_shot; i++) {
        shot_t *s = &s_shot[i];
        s->x += s->vx * dt; s->y += s->vy * dt;
        if (hypotf(s->x - CX, s->y - CY) < 190) s_shot[n++] = *s;
    }
    s_n_shot = n;
    for (int i = 0; i < s_n_rock; i++) { rock_t *r = &s_rock[i]; r->x += r->vx * dt; r->y += r->vy * dt; r->spin += dt; }

    for (int si = 0; si < s_n_shot; si++) {
        shot_t *s = &s_shot[si];
        for (int ri = 0; ri < s_n_rock; ri++) {            // s_n_rock grows as big rocks split (seen by later shots)
            rock_t *r = &s_rock[ri];
            if (r->dead || hypotf(s->x - r->x, s->y - r->y) >= r->r) continue;
            r->dead = true; s->x = 9999; s_score += r->r > 12 ? 20 : 50;
            if (r->r > 12) {
                float a = atan2f(r->vy, r->vx), v = hypotf(r->vx, r->vy) * 1.2f, x = r->x, y = r->y;
                add_rock(x, y, 10, a + 0.6f, v);
                add_rock(x, y, 10, a - 0.6f, v);
            }
        }
    }
    n = 0;
    for (int i = 0; i < s_n_shot; i++) if (s_shot[i].x < 9000) s_shot[n++] = s_shot[i];
    s_n_shot = n;

    if (s_hurt > 0) s_hurt -= dt;
    for (int i = 0; i < s_n_rock; i++) {
        rock_t *r = &s_rock[i];
        if (r->dead || hypotf(r->x - CX, r->y - CY) >= r->r + SR) continue;
        r->dead = true;
        if (s_hurt <= 0) {
            s_left--; s_hurt = 1.2f;
            if (s_left <= 0) game_over();
        }
    }
    n = 0;
    for (int i = 0; i < s_n_rock; i++)
        if (!s_rock[i].dead && hypotf(s_rock[i].x - CX, s_rock[i].y - CY) < 240) s_rock[n++] = s_rock[i];
    s_n_rock = n;
}

static void draw_ship(void)   // the arrowhead (18,0) (-10,11) (-5,0) (-10,-11) turned to the aim, a dark cockpit
{
    static const float SX[4] = {18, -10, -5, -10}, SY[4] = {0, 11, 0, -11};
    float co = cosf(s_aim), si = sinf(s_aim), xs[4], ys[4];
    for (int i = 0; i < 4; i++) { xs[i] = CX + SX[i] * co - SY[i] * si; ys[i] = CY + SX[i] * si + SY[i] * co; }
    poly(xs, ys, 4, rgb(0xFFDD00));
    discf(CX + 2 * co, CY + 2 * si, 3.5f, rgb(0x111111));
}

static void frame(float dt)
{
    s_t += dt;
    if (s_state == G_RUN) step(dt);

    gc_fill(rgb(0x04030A));
    for (int i = 0; i < N_STARS; i++) gc_rect(s_star[i][0], s_star[i][1], s_star[i][0] + 1, s_star[i][1] + 1, rgb(i % 3 ? 0x555555 : 0xBBBBBB));
    ringf(CX, CY, 33, 35, s_ring_c);
    for (int i = 0; i < s_n_rock; i++) {
        rock_t *r = &s_rock[i];
        float xs[RPTS], ys[RPTS];
        for (int k = 0; k < RPTS; k++) {
            float a = r->spin + k * TAU_F / RPTS;
            xs[k] = r->x + cosf(a) * r->r * r->pts[k];
            ys[k] = r->y + sinf(a) * r->r * r->pts[k];
        }
        poly(xs, ys, RPTS, rgb(r->r > 12 ? 0x8A7A6A : 0xB09A82));
        discf(r->x - r->r * 0.3f, r->y - r->r * 0.2f, r->r * 0.22f, rgb(0x6A5C50));
    }
    for (int i = 0; i < s_n_shot; i++) discf(s_shot[i].x, s_shot[i].y, 2.5f, rgb(0xFFDD00));
    if (!(s_hurt > 0 && (((int)floorf(s_t * 10)) & 1))) draw_ship();
    for (int i = 0; i < s_left; i++) discf(GC_W / 2 - (s_left - 1) * 9 + i * 18, 326, 5, rgb(0xFFDD00));
    gc_dirty_all();
    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
}

static void input(gc_input_t in, lv_point_t at)
{
    if (s_state != G_RUN) {
        if (in != GC_PRESS) return;
        if (s_state == G_OVER) reset();
        s_state = G_RUN;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (in == GC_PRESS || in == GC_DRAG) s_aim = atan2f((float)at.y - CY, (float)at.x - CX);
}

static void icon(lv_color_t *b, int n)
{
    for (int i = 0; i < n * n; i++) b[i] = rgb(0x04030A);
    for (int i = 0; i < 24; i++) {                               // stars
        float a = i * 2.39996f, rr = 10 + ((i * 53) % 56);
        int x = n / 2 + (int)(cosf(a) * rr), y = n / 2 + (int)(sinf(a) * rr);
        gc_rect_in(b, n, n, x, y, x + 2, y + 2, rgb(i % 3 ? 0x555555 : 0xBBBBBB));
    }
    gc_ring_in(b, n, n, n / 2, n / 2, 24, 26, 0, 360, mix(0x04030A, 0xFFDD00, 0.25f));
    gc_disc_in(b, n, n, 30, 30, 18, rgb(0x8A7A6A));               // a big rock and a small one
    gc_disc_in(b, n, n, 40, 18, 8, rgb(0x8A7A6A));
    gc_disc_in(b, n, n, 24, 26, 5, rgb(0x6A5C50));
    gc_disc_in(b, n, n, 104, 100, 11, rgb(0xB09A82));
    gc_disc_in(b, n, n, 101, 98, 3, rgb(0x6A5C50));
    for (int k = 0; k < 4; k++) gc_disc_in(b, n, n, 56 - k * 6, 56 - k * 6, 2, rgb(0xFFDD00));   // shots up-left
    for (int y = 0; y < n; y++)                                  // the ship pointing up-left (the shots' way)
        for (int x = 0; x < n; x++) {
            float dx = x + 0.5f - n / 2.0f, dy = y + 0.5f - n / 2.0f;
            float u = -(dx + dy) * 0.7071f, v = (dx - dy) * 0.7071f;   // u forward, v across
            u /= 1.4f; v /= 1.4f;
            bool in = u <= 18 && u >= -10 && fabsf(v) <= 11 * (18 - u) / 28 && !(u < -5 && fabsf(v) < (-5 - u) * 11 / 5.0f);
            if (in) b[y * n + x] = rgb(0xFFDD00);
            if ((u - 2) * (u - 2) + v * v <= 3.5f * 3.5f) b[y * n + x] = rgb(0x111111);
        }
}

const game_def_t game_rocks = {
    .name = "ROCK GUARD", .hint = "TOUCH WHERE TO SHOOT\nBIG ROCKS SPLIT", .key = "rocks", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
