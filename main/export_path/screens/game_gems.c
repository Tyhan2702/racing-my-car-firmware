// GEM MATCH — match three on a 7 x 7 board of six gems against a 90 s clock. Tap a gem, then a neighbour, to swap
// them (or touch a gem and swipe up / left / right); a swap that makes no row or column of three is undone. Every
// matched gem is 10 x the chain step (cascades count up as a combo). When no swap is left the board is dealt again.
// Same rules, sizes and colours as the browser copy (web/game-arcade2.js, gemMatch). The board is redrawn only when
// it changes (a swap, the pop of matched gems, the selection); the clock bar above it every frame.

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"

#define N 7
#define C 34                                   // cell size
#define X0 ((GC_W - N * C) / 2)
#define Y0 ((GC_H - N * C) / 2 + 4)
#define TIME 90.0f                             // seconds per game
#define POP 0.18f                              // the matched gems swell for this long, then fall away
#define BG 0x0A0812
#define PI_F 3.14159265f

typedef enum { G_READY, G_RUN, G_OVER } gems_state_t;

static const uint32_t GEMC[6] = {0xFF3030, 0xFFDD00, 0x2FE06B, 0x2F9BFF, 0xFF3DF2, 0xFF8A00};
static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};

static gems_state_t s_state;
static int8_t s_g[N][N];                       // gem kind 0..5 (-1 only while the board collapses)
static bool s_pop[N * N];                      // the cells of the match that is popping
static bool s_popping, s_has_sel, s_has_down, s_redraw, s_combo_on;
static float s_pop_t, s_clock;
static int s_sel_x, s_sel_y, s_down_x, s_down_y, s_score, s_combo, s_shown_score, s_shown_combo;
static lv_obj_t *s_score_l, *s_combo_l, *s_title, *s_hint;
static lv_color_t *s_tb;                       // where the gem shapes draw (the canvas or the icon)
static int s_tw, s_th;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static int rnd6(void) { int v = (int)(gc_rand() * 6); return v >= 6 ? 5 : v; }

// ---------- drawing into s_tb (pixel centres inside the shape are filled) ----------
static void target(lv_color_t *b, int w, int h) { s_tb = b; s_tw = w; s_th = h; }
static void hl(int y, int x0, int x1, lv_color_t c)
{
    if (y < 0 || y >= s_th) return;
    if (x0 < 0) x0 = 0;
    if (x1 > s_tw) x1 = s_tw;
    for (int x = x0; x < x1; x++) s_tb[y * s_tw + x] = c;
}
static void span(int y, float xa, float xb, lv_color_t c) { hl(y, (int)ceilf(xa - 0.5f), (int)floorf(xb - 0.5f) + 1, c); }
static void rectf(float x, float y, float w, float h, lv_color_t c)
{
    for (int py = (int)floorf(y + 0.5f); py < (int)floorf(y + h + 0.5f); py++) span(py, x, x + w, c);
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
static void blend(int x, int y, lv_color_t c, uint8_t a)
{
    if (x < 0 || y < 0 || x >= s_tw || y >= s_th) return;
    s_tb[y * s_tw + x] = lv_color_mix(c, s_tb[y * s_tw + x], a);
}
static void disc_blend(float cx, float cy, float r, lv_color_t c, uint8_t a)   // a translucent disc over what is there
{
    for (int y = (int)floorf(cy - r); y <= (int)ceilf(cy + r); y++) {
        float dy = y + 0.5f - cy;
        if (dy * dy > r * r) continue;
        float h = sqrtf(r * r - dy * dy);
        for (int x = (int)ceilf(cx - h - 0.5f); x < (int)floorf(cx + h - 0.5f) + 1; x++) blend(x, y, c, a);
    }
}
// a filled polygon (convex shapes and the star), even-odd scanlines through the pixel centres
static void poly(const float *px, const float *py, int n, lv_color_t c)
{
    float y0 = py[0], y1 = py[0];
    for (int i = 1; i < n; i++) { if (py[i] < y0) y0 = py[i]; if (py[i] > y1) y1 = py[i]; }
    for (int y = (int)floorf(y0); y <= (int)ceilf(y1); y++) {
        float yc = y + 0.5f, xs[12];
        int k = 0;
        for (int i = 0; i < n && k < 12; i++) {
            int j = (i + 1) % n;
            float a = py[i], b = py[j];
            if ((a <= yc && b > yc) || (b <= yc && a > yc)) xs[k++] = px[i] + (yc - a) / (b - a) * (px[j] - px[i]);
        }
        for (int i = 1; i < k; i++)            // sort the crossings
            for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) { float t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
        for (int i = 0; i + 1 < k; i += 2) span(y, xs[i], xs[i + 1], c);
    }
}

// gem k at (x, y), s = its swell; circle, diamond, square, triangle, hexagon, star, with a 60 % white shine
static void gem(float x, float y, int k, float s, float cell)
{
    lv_color_t c = rgb(GEMC[k]);
    float r = cell * 0.36f * s, px[10], py[10];
    int n = 0;
    if (k == 0) discf(x, y, r, c);
    else if (k == 1) {
        px[0] = x; py[0] = y - r; px[1] = x + r; py[1] = y; px[2] = x; py[2] = y + r; px[3] = x - r; py[3] = y;
        n = 4;
    } else if (k == 2) rectf(x - r * 0.85f, y - r * 0.85f, r * 1.7f, r * 1.7f, c);
    else if (k == 3) {
        px[0] = x; py[0] = y - r; px[1] = x + r; py[1] = y + r * 0.8f; px[2] = x - r; py[2] = y + r * 0.8f;
        n = 3;
    } else if (k == 4) {
        for (int i = 0; i < 6; i++) { float a = i * 2 * PI_F / 6; px[i] = x + cosf(a) * r; py[i] = y + sinf(a) * r; }
        n = 6;
    } else {
        for (int i = 0; i < 10; i++) {
            float a = -PI_F / 2 + i * 2 * PI_F / 10, rr = (i & 1) ? r * 0.5f : r;
            px[i] = x + cosf(a) * rr; py[i] = y + sinf(a) * rr;
        }
        n = 10;
    }
    if (n) poly(px, py, n, c);
    disc_blend(x - r * 0.3f, y - r * 0.35f, r * 0.22f, lv_color_white(), 153);
}

// how much of pixel [p, p+1) lies in [a, b]
static float cover(float a, float b, int p)
{
    float lo = a > p ? a : p, hi = b < p + 1 ? b : p + 1;
    return hi > lo ? hi - lo : 0;
}

// the white 2.5 px frame around the selected cell (stroked on the square inset by 2 px)
static void sel_frame(int cx, int cy)
{
    const float o0 = 2 - 1.25f, o1 = C - 2 + 1.25f, i0 = 2 + 1.25f, i1 = C - 2 - 1.25f;
    for (int y = 0; y < C; y++)
        for (int x = 0; x < C; x++) {
            if (x > 3 && x < C - 4 && y > 3 && y < C - 4) continue;
            float a = cover(o0, o1, x) * cover(o0, o1, y) - cover(i0, i1, x) * cover(i0, i1, y);
            if (a > 0.01f) blend(cx + x, cy + y, lv_color_white(), (uint8_t)(a * 255 + 0.5f));
        }
}

// ---------- the rules ----------
static int matches(bool *m)                    // marks the gems in rows / columns of three or more; returns how many
{
    bool tmp[N * N];
    if (!m) m = tmp;
    memset(m, 0, N * N * sizeof(bool));
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            int v = s_g[y][x];
            if (x < N - 2 && v == s_g[y][x + 1] && v == s_g[y][x + 2]) for (int k = 0; k < 3; k++) m[y * N + x + k] = true;
            if (y < N - 2 && v == s_g[y + 1][x] && v == s_g[y + 2][x]) for (int k = 0; k < 3; k++) m[(y + k) * N + x] = true;
        }
    int n = 0;
    for (int i = 0; i < N * N; i++) n += m[i];
    return n;
}

static void swap(int ax, int ay, int bx, int by) { int8_t t = s_g[ay][ax]; s_g[ay][ax] = s_g[by][bx]; s_g[by][bx] = t; }

static bool any_move(void)
{
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++)
            for (int d = 0; d < 2; d++) {
                int bx = x + (d == 0), by = y + (d == 1);
                if (bx >= N || by >= N) continue;
                swap(x, y, bx, by);
                bool ok = matches(NULL) > 0;
                swap(x, y, bx, by);
                if (ok) return true;
            }
    return false;
}

static void fill(void)                         // a fresh board with no match on it and at least one swap
{
    do {
        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) s_g[y][x] = (int8_t)rnd6();
    } while (matches(NULL) || !any_move());
    s_redraw = true;
}

static void reset(void)
{
    fill();
    s_has_sel = false;
    s_clock = TIME;
    s_score = 0;
    s_combo = 0;
    s_popping = false;
    s_redraw = true;
}

static bool resolve(void)
{
    int n = matches(s_pop);
    if (!n) { s_combo = 0; if (!any_move()) fill(); return false; }
    s_combo++;
    s_score += n * 10 * s_combo;
    s_popping = true;
    s_pop_t = POP;
    s_redraw = true;
    return true;
}

static void collapse(void)                     // the matched gems go, the rest fall, new ones drop in from the top
{
    for (int k = 0; k < N * N; k++) if (s_pop[k]) s_g[k / N][k % N] = -1;
    for (int x = 0; x < N; x++) {
        int8_t col[N];
        int n = 0;
        for (int y = N - 1; y >= 0; y--) if (s_g[y][x] >= 0) col[n++] = s_g[y][x];
        for (int y = N - 1, i = 0; y >= 0; y--, i++) s_g[y][x] = i < n ? col[i] : (int8_t)rnd6();
    }
    s_popping = false;
    s_redraw = true;
    resolve();
}

static void try_swap(int ax, int ay, int bx, int by)
{
    if (abs(ax - bx) + abs(ay - by) != 1 || bx < 0 || by < 0 || bx >= N || by >= N) return;
    swap(ax, ay, bx, by);
    if (!resolve()) swap(ax, ay, bx, by);
    s_has_sel = false;
    s_redraw = true;
}

static void cell_at(lv_point_t p, int *cx, int *cy)
{
    *cx = (int)floorf((float)(p.x - X0) / C);
    *cy = (int)floorf((float)(p.y - Y0) / C);
}
static bool inside(int cx, int cy) { return cx >= 0 && cy >= 0 && cx < N && cy < N; }

// ---------- screen ----------
static void draw_board(void)
{
    target(gc_buf, GC_W, GC_H);
    float s = s_popping ? 1 + 0.4f * (1 - s_pop_t / POP) : 1;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            gc_rect(X0 + x * C, Y0 + y * C, X0 + x * C + C, Y0 + y * C + C, ((x + y) & 1) ? rgb(0x15121F) : rgb(0x1B1728));
            int k = s_g[y][x];
            if (k >= 0) gem(X0 + (x + 0.5f) * C, Y0 + (y + 0.5f) * C, k, s_popping && s_pop[y * N + x] ? s : 1, C);
        }
    if (s_has_sel) sel_frame(X0 + s_sel_x * C, Y0 + s_sel_y * C);
    gc_dirty(X0, Y0, X0 + N * C - 1, Y0 + N * C - 1);
}

static void draw_clock(void)
{
    target(gc_buf, GC_W, GC_H);
    gc_rect(X0, Y0 - 14, X0 + N * C, Y0 - 8, rgb(0x222222));
    rectf(X0, Y0 - 14, N * C * s_clock / TIME, 6, s_clock < 15 ? rgb(0xFF3030) : rgb(0xFFDD00));
    gc_dirty(X0, Y0 - 14, X0 + N * C - 1, Y0 - 9);
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
    gc_fill(rgb(BG));
    s_score_l = gc_label(&ui_font_FontTypoderSize20, 0xFFFFFF, LV_ALIGN_CENTER, 0, Y0 - 34 - GC_H / 2);
    s_combo_l = gc_label(&ui_font_FontTypoderSize16, 0xFFDD00, LV_ALIGN_CENTER, 0, Y0 + N * C + 16 - GC_H / 2);
    lv_obj_add_flag(s_combo_l, LV_OBJ_FLAG_HIDDEN);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -20);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 18);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 8, 0);
    s_has_down = false;
    s_combo_on = false;
    s_shown_score = s_shown_combo = -1;
    reset();
    s_state = G_READY;
    overlay("GEM MATCH", "TAP A GEM, THEN A NEIGHBOUR\nTAP TO START");
    draw_board();
    draw_clock();
    s_redraw = false;
}

static void frame(float dt)
{
    if (s_state == G_RUN) {
        s_clock -= dt;
        bool out = s_clock <= 0;
        if (out) s_clock = 0;
        if (s_popping) {
            s_pop_t -= dt;
            if (s_pop_t <= 0) collapse();
            s_redraw = true;
        }
        if (out) game_over();
        draw_clock();
    }
    if (s_redraw) { draw_board(); s_redraw = false; }
    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
    bool on = s_combo > 1 && s_popping;
    if (on && s_combo != s_shown_combo) { lv_label_set_text_fmt(s_combo_l, "COMBO X%d", s_combo); s_shown_combo = s_combo; }
    if (on != s_combo_on) {
        if (on) lv_obj_clear_flag(s_combo_l, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_combo_l, LV_OBJ_FLAG_HIDDEN);
        s_combo_on = on;
    }
}

static void input(gc_input_t in, lv_point_t at)
{
    if (s_state != G_RUN) {
        if (in != GC_TAP) return;
        if (s_state == G_OVER) { reset(); draw_clock(); }
        s_state = G_RUN;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (s_popping) return;
    if (in == GC_PRESS) { cell_at(at, &s_down_x, &s_down_y); s_has_down = true; return; }
    if ((in == GC_SWIPE_UP || in == GC_SWIPE_LEFT || in == GC_SWIPE_RIGHT) && s_has_down && inside(s_down_x, s_down_y)) {
        int d = in == GC_SWIPE_UP ? 0 : in == GC_SWIPE_RIGHT ? 1 : 3;
        try_swap(s_down_x, s_down_y, s_down_x + DX[d], s_down_y + DY[d]);
        s_has_down = false;
        return;
    }
    if (in != GC_TAP) return;
    int cx, cy;
    cell_at(at, &cx, &cy);
    if (!inside(cx, cy)) { if (s_has_sel) s_redraw = true; s_has_sel = false; return; }
    if (!s_has_sel) { s_has_sel = true; s_sel_x = cx; s_sel_y = cy; }
    else if (s_sel_x == cx && s_sel_y == cy) s_has_sel = false;
    else if (abs(s_sel_x - cx) + abs(s_sel_y - cy) == 1) try_swap(s_sel_x, s_sel_y, cx, cy);
    else { s_sel_x = cx; s_sel_y = cy; }
    s_redraw = true;
}

static void icon(lv_color_t *b, int n)
{
    for (int i = 0; i < n * n; i++) b[i] = rgb(BG);
    int c = n * 10 / 33, x0 = (n - 3 * c) / 2;    // a 3 x 3 corner of the board: three yellow diamonds popping
    static const int8_t K[3][3] = {{4, 0, 2}, {1, 1, 1}, {3, 5, 0}};
    target(b, n, n);
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3; x++) {
            gc_rect_in(b, n, n, x0 + x * c, x0 + y * c, x0 + x * c + c, x0 + y * c + c, ((x + y) & 1) ? rgb(0x15121F) : rgb(0x1B1728));
            gem(x0 + (x + 0.5f) * c, x0 + (y + 0.5f) * c, K[y][x], y == 1 ? 1.25f : 1, c);
        }
    const float o0 = 0.75f, o1 = c - 0.75f, i0 = 3.25f, i1 = c - 3.25f;   // the selection on the bottom-left gem
    for (int y = 0; y < c; y++)
        for (int x = 0; x < c; x++) {
            float a = cover(o0, o1, x) * cover(o0, o1, y) - cover(i0, i1, x) * cover(i0, i1, y);
            if (a > 0.01f) blend(x0 + x, x0 + 2 * c + y, lv_color_white(), (uint8_t)(a * 255 + 0.5f));
        }
}

const game_def_t game_gems = {
    .name = "GEM MATCH", .hint = "TAP A GEM, THEN A NEIGHBOUR\nMATCH THREE IN 90 S", .key = "gems", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
