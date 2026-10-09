// CITY DEFENCE — red missiles fall from the sky toward the four blue cities; touch where to burst and the yellow
// base fires an interceptor there. Its burst grows to 32 px and shrinks again, and every missile inside it is gone
// (25 points; bursts of missiles that reach the ground count too). A missile that lands wrecks the city under it.
// A wave ends when its missiles are all gone: 100 per city left and 5 per unused shot, then the next wave has more,
// faster missiles and a bit more ammo. All cities gone ends the game. Everything moves, so every frame is redrawn
// (the night sky row by row, colours worked out once; trails blended over it). Same rules, sizes and colours as
// the browser copy (web/game-arcade3.js, cityDefence).

#include <math.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"

#define PI_F 3.14159265f
#define BASE_X 180
#define BASE_Y 318
#define GROUND 318
#define N_CITY 4
#define MAX_MIS 64
#define MAX_INT 32
#define MAX_BOOM 96

typedef enum { D_READY, D_RUN, D_OVER } defence_state_t;
typedef struct { float sx, sy, x, y, vx, vy; bool dead; } missile_t;
typedef struct { float x, y, tx, ty, vx, vy, left; } inter_t;
typedef struct { float x, y, r, t; bool enemy; } boom_t;

static const int16_t CITY[N_CITY][2] = {{96, 312}, {264, 312}, {130, 322}, {230, 322}};

static defence_state_t s_state;
static bool s_city[N_CITY];
static missile_t s_mis[MAX_MIS];
static inter_t s_int[MAX_INT];
static boom_t s_boom[MAX_BOOM];
static int s_n_mis, s_n_int, s_n_boom;
static int s_wave, s_to_launch, s_ammo, s_score, s_shown_score, s_shown_wave, s_shown_ammo;
static float s_launch_t, s_t;
static lv_color_t s_sky[GC_H];
static lv_obj_t *s_score_l, *s_info_l, *s_title, *s_hint;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static int rndn(int n) { int v = (int)(gc_rand() * n); return v >= n ? n - 1 : v; }
static void span(int y, float xa, float xb, lv_color_t c) { gc_hline(y, (int)ceilf(xa - 0.5f), (int)floorf(xb - 0.5f) + 1, c); }
static void rectf(float x, float y, float w, float h, lv_color_t c)   // canvas fillRect: the pixel centres inside
{
    for (int py = (int)floorf(y + 0.5f); py < (int)floorf(y + h + 0.5f); py++) span(py, x, x + w, c);
}
static void discf(float cx, float cy, float r, lv_color_t c)
{
    if (r <= 0) return;
    for (int y = (int)floorf(cy - r); y <= (int)ceilf(cy + r); y++) {
        float dy = y + 0.5f - cy;
        if (dy * dy > r * r) continue;
        float h = sqrtf(r * r - dy * dy);
        span(y, cx - h, cx + h, c);
    }
}
static void blend(int x, int y, lv_color_t c, uint8_t a)
{
    if (x < 0 || y < 0 || x >= GC_W || y >= GC_H) return;
    gc_buf[y * GC_W + x] = lv_color_mix(c, gc_buf[y * GC_W + x], a);
}
// a straight line w px thick (butt ends), each pixel blended once: along the long axis one column (or row) at a time
static void line_blend(float x0, float y0, float x1, float y1, float w, lv_color_t c, uint8_t a)
{
    float dx = x1 - x0, dy = y1 - y0, len = hypotf(dx, dy);
    if (len < 0.01f) return;
    if (fabsf(dx) >= fabsf(dy)) {
        float hw = w / 2 * len / fabsf(dx);
        int xa = (int)ceilf(fminf(x0, x1) - 0.5f), xb = (int)floorf(fmaxf(x0, x1) - 0.5f);
        if (xa < 0) xa = 0;
        if (xb > GC_W - 1) xb = GC_W - 1;
        for (int x = xa; x <= xb; x++) {
            float yc = y0 + (x + 0.5f - x0) * dy / dx;
            for (int y = (int)ceilf(yc - hw - 0.5f); y <= (int)floorf(yc + hw - 0.5f); y++) blend(x, y, c, a);
        }
    } else {
        float hw = w / 2 * len / fabsf(dy);
        int ya = (int)ceilf(fminf(y0, y1) - 0.5f), yb = (int)floorf(fmaxf(y0, y1) - 0.5f);
        if (ya < 0) ya = 0;
        if (yb > GC_H - 1) yb = GC_H - 1;
        for (int y = ya; y <= yb; y++) {
            float xc = x0 + (y + 0.5f - y0) * dx / dy;
            for (int x = (int)ceilf(xc - hw - 0.5f); x <= (int)floorf(xc + hw - 0.5f); x++) blend(x, y, c, a);
        }
    }
}
// a filled triangle (scanlines through the pixel centres)
static void tri(const float *xs, const float *ys, lv_color_t c)
{
    float y0 = fminf(ys[0], fminf(ys[1], ys[2])), y1 = fmaxf(ys[0], fmaxf(ys[1], ys[2]));
    for (int y = (int)floorf(y0); y <= (int)ceilf(y1); y++) {
        float yc = y + 0.5f, cr[3];
        int k = 0;
        for (int i = 0; i < 3; i++) {
            int j = (i + 1) % 3;
            float a = ys[i], b = ys[j];
            if ((a <= yc && b > yc) || (b <= yc && a > yc)) cr[k++] = xs[i] + (yc - a) / (b - a) * (xs[j] - xs[i]);
        }
        if (k == 2) span(y, fminf(cr[0], cr[1]), fmaxf(cr[0], cr[1]), c);
    }
}

static void sky_rows(void)   // the gradient #05030f -> #2a1240
{
    for (int y = 0; y < GC_H; y++) {
        float t = (y + 0.5f) / GC_H;
        s_sky[y] = lv_color_make((uint8_t)(0x05 + (0x2a - 0x05) * t + 0.5f), (uint8_t)(0x03 + (0x12 - 0x03) * t + 0.5f),
                                 (uint8_t)(0x0f + (0x40 - 0x0f) * t + 0.5f));
    }
}

static void start_wave(void)
{
    s_to_launch = 6 + s_wave * 2;
    s_launch_t = 0.5f;
    s_ammo = 12 + s_wave * 2;
    s_n_mis = s_n_int = s_n_boom = 0;
}

static void reset(void)
{
    for (int i = 0; i < N_CITY; i++) s_city[i] = true;
    s_wave = 0;
    s_score = 0;
    start_wave();
}

static void launch(void)
{
    int live[N_CITY], n = 0;
    for (int i = 0; i < N_CITY; i++) if (s_city[i]) live[n++] = i;
    if (!n) return;
    int tg = live[rndn(n)];
    float a = -PI_F / 2 + (gc_rand() - 0.5f) * 1.6f, sx = 180 + cosf(a) * 178, sy = 180 + sinf(a) * 178;
    float sp = 26 + s_wave * 5 + gc_rand() * 10, d = hypotf(CITY[tg][0] - sx, CITY[tg][1] - sy);
    if (s_n_mis >= MAX_MIS) return;
    s_mis[s_n_mis++] = (missile_t){sx, sy, sx, sy, (CITY[tg][0] - sx) / d * sp, (CITY[tg][1] - sy) / d * sp, false};
}

static void shoot(float x, float y)
{
    if (s_ammo <= 0 || y > GROUND - 10) return;
    float d = hypotf(x - BASE_X, y - BASE_Y);
    if (d < 0.01f || s_n_int >= MAX_INT) return;
    s_ammo--;
    s_int[s_n_int++] = (inter_t){BASE_X, BASE_Y, x, y, (x - BASE_X) / d * 420, (y - BASE_Y) / d * 420, d / 420};
}

static void add_boom(float x, float y, bool enemy)
{
    if (s_n_boom < MAX_BOOM) s_boom[s_n_boom++] = (boom_t){x, y, 0, 0, enemy};
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
    s_state = D_OVER;
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
    sky_rows();
    s_score_l = gc_label(&ui_font_FontTypoderSize20, 0xFFFFFF, LV_ALIGN_CENTER, 0, 40 - GC_H / 2);
    s_info_l = gc_label(&ui_font_FontTypoderSize16, 0x9A9A9A, LV_ALIGN_CENTER, 0, 64 - GC_H / 2);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -20);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 18);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 8, 0);
    s_t = 0;
    reset();
    s_state = D_READY;
    s_shown_score = s_shown_wave = s_shown_ammo = -1;
    overlay("CITY DEFENCE", "TOUCH WHERE TO BURST\nTOUCH TO START");
}

static void step(float dt)
{
    s_launch_t -= dt;
    if (s_to_launch > 0 && s_launch_t <= 0) {
        launch();
        s_to_launch--;
        s_launch_t = fmaxf(0.5f, 2 - s_wave * 0.15f) * (0.6f + gc_rand() * 0.8f);
    }
    for (int i = 0; i < s_n_mis; i++) { s_mis[i].x += s_mis[i].vx * dt; s_mis[i].y += s_mis[i].vy * dt; }
    int n = 0;
    for (int i = 0; i < s_n_int; i++) {
        inter_t *it = &s_int[i];
        it->x += it->vx * dt; it->y += it->vy * dt; it->left -= dt;
        if (it->left <= 0) add_boom(it->tx, it->ty, false);
        else s_int[n++] = *it;
    }
    s_n_int = n;
    for (int i = 0; i < s_n_boom; i++) {
        boom_t *b = &s_boom[i];
        b->t += dt;
        b->r = b->t < 0.45f ? 32 * b->t / 0.45f : 32 * (1 - (b->t - 0.45f) / 0.5f);
        for (int k = 0; k < s_n_mis; k++) {
            missile_t *m = &s_mis[k];
            if (!m->dead && hypotf(m->x - b->x, m->y - b->y) < b->r) { m->dead = true; s_score += 25; }
        }
    }
    n = 0;
    for (int i = 0; i < s_n_boom; i++) if (s_boom[i].t < 0.95f) s_boom[n++] = s_boom[i];
    s_n_boom = n;
    for (int i = 0; i < s_n_mis; i++) {
        missile_t *m = &s_mis[i];
        if (m->dead || m->y < GROUND - 6) continue;
        m->dead = true;
        add_boom(m->x, m->y, true);
        for (int c = 0; c < N_CITY; c++) if (fabsf(CITY[c][0] - m->x) < 18) s_city[c] = false;
    }
    n = 0;
    for (int i = 0; i < s_n_mis; i++) if (!s_mis[i].dead) s_mis[n++] = s_mis[i];
    s_n_mis = n;
    int alive = 0;
    for (int c = 0; c < N_CITY; c++) alive += s_city[c];
    if (!alive) game_over();
    else if (!s_to_launch && !s_n_mis && !s_n_boom) {
        s_score += alive * 100 + s_ammo * 5;
        s_wave++;
        start_wave();
    }
}

static void frame(float dt)
{
    s_t += dt;
    if (s_state == D_RUN) step(dt);

    for (int y = 0; y < GC_H; y++) gc_hline(y, 0, GC_W, s_sky[y]);
    gc_rect(0, GROUND, GC_W, GC_H, rgb(0x3A2A12));
    for (int i = 0; i < N_CITY; i++) {
        int x = CITY[i][0], y = CITY[i][1];
        if (s_city[i]) {
            gc_rect(x - 14, y - 10, x - 6, y, rgb(0x2FD6FF));
            gc_rect(x - 5, y - 16, x + 5, y, rgb(0x2F9BFF));
            gc_rect(x + 6, y - 8, x + 14, y, rgb(0x2FD6FF));
        } else gc_rect(x - 14, y - 3, x + 14, y, rgb(0x555555));
    }
    static const float BXS[3] = {BASE_X - 16, BASE_X, BASE_X + 16}, BYS[3] = {BASE_Y + 4, BASE_Y - 14, BASE_Y + 4};
    tri(BXS, BYS, rgb(0xFFDD00));
    for (int i = 0; i < s_n_mis; i++) {
        missile_t *m = &s_mis[i];
        line_blend(m->sx, m->sy, m->x, m->y, 1.5f, rgb(0xFF3030), 153);
        discf(m->x, m->y, 2.5f, rgb(0xFFFFFF));
    }
    for (int i = 0; i < s_n_int; i++) {
        inter_t *it = &s_int[i];
        line_blend(BASE_X, BASE_Y, it->x, it->y, 1.5f, rgb(0xFFDD00), 179);
        rectf(it->tx - 3, it->ty - 0.5f, 6, 1, rgb(0xFFFFFF));
        rectf(it->tx - 0.5f, it->ty - 3, 1, 6, rgb(0xFFFFFF));
    }
    lv_color_t blink = rgb(((int)floorf(s_t * 20)) & 1 ? 0xFFFFFF : 0xFFDD00);
    for (int i = 0; i < s_n_boom; i++) discf(s_boom[i].x, s_boom[i].y, fmaxf(0, s_boom[i].r), s_boom[i].enemy ? rgb(0xFF8A00) : blink);
    gc_dirty_all();
    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
    if (s_wave != s_shown_wave || s_ammo != s_shown_ammo) {
        lv_label_set_text_fmt(s_info_l, "WAVE %d   AMMO %d", s_wave + 1, s_ammo);
        s_shown_wave = s_wave; s_shown_ammo = s_ammo;
    }
}

static void input(gc_input_t in, lv_point_t at)
{
    if (in != GC_PRESS) return;
    if (s_state != D_RUN) {
        if (s_state == D_OVER) reset();
        s_state = D_RUN;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    shoot(at.x, at.y);
}

static void icon(lv_color_t *b, int n)
{
    for (int y = 0; y < n; y++) {                                // the night sky
        float t = (float)y / n;
        lv_color_t c = lv_color_make((uint8_t)(0x05 + 0x25 * t), (uint8_t)(0x03 + 0x0f * t), (uint8_t)(0x0f + 0x31 * t));
        for (int x = 0; x < n; x++) b[y * n + x] = c;
    }
    int g = n - 24;
    gc_rect_in(b, n, n, 0, g, n, n, rgb(0x3A2A12));               // ground, two cities, the base
    for (int k = 0; k < 2; k++) {
        int x = k ? n - 26 : 26;
        gc_rect_in(b, n, n, x - 18, g - 13, x - 8, g, rgb(0x2FD6FF));
        gc_rect_in(b, n, n, x - 6, g - 21, x + 6, g, rgb(0x2F9BFF));
        gc_rect_in(b, n, n, x + 8, g - 10, x + 18, g, rgb(0x2FD6FF));
    }
    for (int y = 0; y < 22; y++) gc_rect_in(b, n, n, n / 2 - y * 20 / 22, g - 18 + y, n / 2 + y * 20 / 22 + 1, g - 17 + y, rgb(0xFFDD00));
    for (int k = 0; k < 2; k++) {                                // two missile trails from the top
        float x0 = k ? 104 : 30, x1 = k ? 84 : 48, y1 = k ? 66 : 48;
        for (int s = 0; s <= 60; s++) {
            float u = s / 60.0f;
            int x = (int)(x0 + (x1 - x0) * u), y = (int)(y1 * u);
            gc_rect_in(b, n, n, x, y, x + 2, y + 2, rgb(0xC02A2E));
        }
        gc_disc_in(b, n, n, (int)x1, (int)y1, 3, rgb(0xFFFFFF));
    }
    for (int s = 0; s <= 40; s++) {                              // an interceptor's line and its burst
        int x = n / 2 + (int)(-24 * s / 40.0f), y = (g - 18) + (int)((50 - (g - 18)) * s / 40.0f);
        gc_rect_in(b, n, n, x, y, x + 2, y + 2, rgb(0xC0A60A));
    }
    gc_disc_in(b, n, n, n / 2 - 24, 50, 17, rgb(0xFFDD00));
    gc_disc_in(b, n, n, n / 2 - 24, 50, 9, rgb(0xFFFFFF));
}

const game_def_t game_defence = {
    .name = "CITY DEFENCE", .hint = "TOUCH WHERE TO BURST\nSAVE THE CITIES", .key = "defence", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
