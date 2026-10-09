// MERGE — slide the tiles of a 4 x 4 board: touch the board's top / left / right / bottom edge (where the finger is
// from the board's middle), or swipe up / left / right. Two equal tiles that meet merge into their sum, which is
// scored; every move that changes the board adds a 2 (or, one time in ten, a 4). The run ends when nothing can move.
// Same rules, sizes and colours as the browser copy (web/game-arcade2.js, merge). The numbers are 16 labels, one on
// each cell; the board is redrawn only after a move, the new tile's pop only in its own cell.

#include "esp_attr.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"

#define N 4
#define C 52                                   // tile size
#define GAP 6
#define SZ (N * C + (N + 1) * GAP)
#define X0 ((GC_W - SZ) / 2)
#define Y0 ((GC_H - SZ) / 2 + 8)
#define BORN 0.15f                             // a new tile grows from half size for this long
#define BG 0x0B0B10
#define BOARD 0x1F1F29
#define PX0 (GC_W / 2 - 125)                   // the start / game-over panel, 250 x 100
#define PY0 (GC_H / 2 - 50)
#define PX1 (GC_W / 2 + 125)
#define PY1 (GC_H / 2 + 50)
#define AX0 (PX0 < X0 ? PX0 : X0)              // the redrawn area: the board and the panel
#define AX1 (PX1 > X0 + SZ ? PX1 : X0 + SZ)

typedef enum { M_READY, M_RUN, M_OVER } merge_state_t;

static merge_state_t s_state;
static EXT_RAM_BSS_ATTR int s_b[N][N], s_shown[N][N], s_score, s_shown_score, s_born_x, s_born_y;
static float s_born_t;
static bool s_born_anim, s_redraw, s_dim;
static EXT_RAM_BSS_ATTR lv_obj_t *s_num[N][N], *s_score_l, *s_title, *s_hint;
static lv_color_t *s_tb;                       // where the shapes draw (the canvas or the icon)
static int s_tw, s_th;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static int cell_cx(int x) { return X0 + GAP + x * (C + GAP) + C / 2; }
static int cell_cy(int y) { return Y0 + GAP + y * (C + GAP) + C / 2; }

static uint32_t tile_color(int v)
{
    switch (v) {
    case 0: return 0x2A2A36;
    case 2: return 0x3A3A46;
    case 4: return 0x4A4A58;
    case 8: return 0xFF8A00;
    case 16: return 0xFF6A00;
    case 32: return 0xFF3030;
    case 64: return 0xE10600;
    case 128: return 0xFFDD00;
    case 256: return 0xFFD000;
    case 512: return 0x2FE06B;
    case 1024: return 0x2FD6FF;
    case 2048: return 0xFF3DF2;
    default: return 0xFFFFFF;
    }
}

// ---------- drawing into s_tb (pixel centres inside the shape are filled) ----------
static void target(lv_color_t *b, int w, int h) { s_tb = b; s_tw = w; s_th = h; }
static void span(int y, float xa, float xb, lv_color_t c, uint8_t a)   // a = 255: solid, else blended over
{
    if (y < 0 || y >= s_th) return;
    int x0 = (int)ceilf(xa - 0.5f), x1 = (int)floorf(xb - 0.5f) + 1;
    if (x0 < 0) x0 = 0;
    if (x1 > s_tw) x1 = s_tw;
    lv_color_t *p = s_tb + y * s_tw;
    for (int x = x0; x < x1; x++) p[x] = a == 255 ? c : lv_color_mix(c, p[x], a);
}
static void rrect(float x, float y, float w, float h, float r, lv_color_t c, uint8_t a)
{
    for (int py = (int)floorf(y); py < (int)ceilf(y + h); py++) {
        float cy = py + 0.5f, d = 0;
        if (cy < y || cy > y + h) continue;
        if (cy < y + r) d = y + r - cy;
        else if (cy > y + h - r) d = cy - (y + h - r);
        float in = d > 0 ? r - sqrtf(fmaxf(0, r * r - d * d)) : 0;
        span(py, x + in, x + w - in, c, a);
    }
}

static float born_scale(int x, int y)
{
    return s_born_t > 0 && s_born_x == x && s_born_y == y ? 1 - s_born_t / BORN * 0.5f : 1;
}
static void tile(int x, int y)
{
    float w = C * born_scale(x, y);
    rrect(cell_cx(x) - w / 2, cell_cy(y) - w / 2, w, w, 8, rgb(tile_color(s_b[y][x])), 255);
}

// the numbers: a label per cell, changed only when the cell's value does; dimmed under the panel like the canvas
static void numbers(void)
{
    bool dim = s_state != M_RUN;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            lv_obj_t *l = s_num[y][x];
            int v = s_b[y][x];
            if (dim != s_dim) {
                bool under = cell_cx(x) > PX0 && cell_cx(x) < PX1 && cell_cy(y) > PY0 && cell_cy(y) < PY1;
                lv_obj_set_style_text_opa(l, dim && under ? 87 : LV_OPA_COVER, 0);
            }
            if (v == s_shown[y][x]) continue;
            s_shown[y][x] = v;
            if (!v) { lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN); continue; }
            lv_obj_set_style_text_font(l, v < 100 ? &ui_font_FontTypoderSize24 : v < 1000 ? &ui_font_FontTypoderSize20 : &ui_font_FontTypoderSize16, 0);
            lv_obj_set_style_text_color(l, lv_color_hex(v >= 128 && v < 512 ? 0x111111 : 0xFFFFFF), 0);
            lv_label_set_text_fmt(l, "%d", v);
            lv_obj_clear_flag(l, LV_OBJ_FLAG_HIDDEN);
        }
    s_dim = dim;
}

static void draw_all(void)
{
    target(gc_buf, GC_W, GC_H);
    gc_rect(AX0, Y0, AX1, Y0 + SZ, rgb(BG));
    rrect(X0, Y0, SZ, SZ, 14, rgb(BOARD), 255);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) tile(x, y);
    if (s_state != M_RUN) rrect(PX0, PY0, PX1 - PX0, PY1 - PY0, 10, lv_color_black(), 168);   // 66 % black
    gc_dirty(AX0, Y0, AX1 - 1, Y0 + SZ - 1);
    numbers();
}

static void draw_born(void)                    // only the new tile's cell, while it grows
{
    if (s_state != M_RUN) { draw_all(); return; }   // under the panel: the panel is redrawn with it
    target(gc_buf, GC_W, GC_H);
    int x0 = cell_cx(s_born_x) - C / 2, y0 = cell_cy(s_born_y) - C / 2;
    gc_rect(x0, y0, x0 + C, y0 + C, rgb(BOARD));
    tile(s_born_x, s_born_y);
    gc_dirty(x0, y0, x0 + C - 1, y0 + C - 1);
}

// ---------- the rules ----------
static void add(void)
{
    int ex[N * N], ey[N * N], n = 0;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) if (!s_b[y][x]) { ex[n] = x; ey[n] = y; n++; }
    if (!n) return;
    int k = (int)(gc_rand() * n);
    if (k >= n) k = n - 1;
    s_b[ey[k]][ex[k]] = gc_rand() < 0.9f ? 2 : 4;
    s_born_x = ex[k]; s_born_y = ey[k]; s_born_t = BORN; s_born_anim = true;
}

static void reset(void)
{
    memset(s_b, 0, sizeof(s_b));
    s_score = 0;
    add();
    add();
    s_redraw = true;
}

static void cell_of(int d, int i, int j, int *x, int *y)   // j-th cell of line i, counted from the side d slides to
{
    if (d == 3) { *x = j; *y = i; }
    else if (d == 1) { *x = N - 1 - j; *y = i; }
    else if (d == 0) { *x = i; *y = j; }
    else { *x = i; *y = N - 1 - j; }
}

static void game_over(void)
{
    s_state = M_OVER;
    bool rec = gc_record(s_score);
    long best = (long)gc_best();
    lv_label_set_text(s_title, rec ? "NEW RECORD!" : "GAME OVER");
    if (best) lv_label_set_text_fmt(s_hint, "SCORE %d   BEST %ld\nTAP TO PLAY AGAIN", s_score, best);
    else lv_label_set_text_fmt(s_hint, "SCORE %d\nTAP TO PLAY AGAIN", s_score);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void slide(int d)                       // 0 up, 1 right, 2 down, 3 left
{
    int nb[N][N];
    bool moved = false;
    memcpy(nb, s_b, sizeof(nb));
    for (int i = 0; i < N; i++) {
        int vals[N], nv = 0, out[N], no = 0;
        for (int j = 0; j < N; j++) {
            int x, y;
            cell_of(d, i, j, &x, &y);
            if (s_b[y][x]) vals[nv++] = s_b[y][x];
        }
        for (int k = 0; k < nv; k++) {
            if (k + 1 < nv && vals[k] == vals[k + 1]) { out[no++] = vals[k] * 2; s_score += vals[k] * 2; k++; }
            else out[no++] = vals[k];
        }
        while (no < N) out[no++] = 0;
        for (int j = 0; j < N; j++) {
            int x, y;
            cell_of(d, i, j, &x, &y);
            if (nb[y][x] != out[j]) moved = true;
            nb[y][x] = out[j];
        }
    }
    if (!moved) return;
    memcpy(s_b, nb, sizeof(nb));
    add();
    s_redraw = true;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            int v = s_b[y][x];
            if (!v || (x + 1 < N && v == s_b[y][x + 1]) || (y + 1 < N && v == s_b[y + 1][x])) return;
        }
    game_over();
}

// ---------- engine ----------
static void overlay(const char *title, const char *hint)
{
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_hint, hint);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void begin(void)
{
    gc_fill(rgb(BG));
    for (int y = 0; y < N; y++)                // the numbers first, so the start / game-over text lies over them
        for (int x = 0; x < N; x++) {
            s_num[y][x] = gc_label(&ui_font_FontTypoderSize24, 0xFFFFFF, LV_ALIGN_CENTER, cell_cx(x) - GC_W / 2, cell_cy(y) + 1 - GC_H / 2);
            lv_obj_add_flag(s_num[y][x], LV_OBJ_FLAG_HIDDEN);
            s_shown[y][x] = 0;
        }
    s_score_l = gc_label(&ui_font_FontTypoderSize20, 0xFFFFFF, LV_ALIGN_CENTER, 0, Y0 - 20 - GC_H / 2);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -20);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 18);   // on the panel in the canvas
    s_shown_score = -1;
    s_dim = false;
    s_born_t = 0;
    reset();
    s_state = M_READY;
    overlay("MERGE", "TOUCH THE BOARD EDGE TO SLIDE\nTAP TO START");
    draw_all();
    s_redraw = false;
}

static void frame(float dt)
{
    if (s_born_t > 0) s_born_t -= dt;
    if (s_redraw) { draw_all(); s_redraw = false; }
    else if (s_born_anim) draw_born();
    if (s_born_t <= 0) s_born_anim = false;    // drawn once more at full size, then left alone
    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
}

static void input(gc_input_t in, lv_point_t at)
{
    if (s_state != M_RUN) {
        if (in != GC_TAP) return;
        if (s_state == M_OVER) reset();
        s_state = M_RUN;
        s_redraw = true;                       // without the panel
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (in == GC_TAP) {                        // the side of the board's middle the finger is on
        int dx = at.x - GC_W / 2, dy = at.y - (Y0 + SZ / 2);
        slide(abs(dx) > abs(dy) ? (dx < 0 ? 3 : 1) : (dy < 0 ? 0 : 2));
    } else if (in == GC_SWIPE_UP) slide(0);
    else if (in == GC_SWIPE_LEFT) slide(3);
    else if (in == GC_SWIPE_RIGHT) slide(1);
}

static void icon(lv_color_t *b, int n)
{
    for (int i = 0; i < n * n; i++) b[i] = rgb(BG);
    target(b, n, n);
    int c = (n - 4 * 6) / 3, g = 6;            // a 3 x 3 corner of the board, climbing up to 2048
    rrect(0, 0, n, n, 14, rgb(BOARD), 255);
    static const int16_t V[3][3] = {{2, 0, 8}, {4, 32, 128}, {16, 512, 2048}};
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3; x++) rrect(g + x * (c + g), g + y * (c + g), c, c, 8, rgb(tile_color(V[y][x])), 255);
    int cx = g + 2 * (c + g) + c / 2, cy = g + 2 * (c + g) + c / 2;   // a white plus on the 2048 tile
    gc_rect_in(b, n, n, cx - 11, cy - 3, cx + 11, cy + 3, rgb(0xFFFFFF));
    gc_rect_in(b, n, n, cx - 3, cy - 11, cx + 3, cy + 11, rgb(0xFFFFFF));
}

const game_def_t game_merge = {
    .name = "MERGE", .hint = "TOUCH THE BOARD EDGE TO SLIDE\nEQUAL TILES MERGE", .key = "merge", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
