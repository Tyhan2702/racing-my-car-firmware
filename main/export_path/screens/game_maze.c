// MAZE CHASE — eat every dot in the maze while three chasers hunt you: touch where to go (above / beside / below
// the yellow muncher) or swipe up / left / right. Dots are 10, big dots 50 and turn the chasers blue for 6 s, when
// catching them scores 200, 400, 600. Three lives; a cleared maze refills and the chasers get faster. Only the maze
// (and the lives under it) is redrawn each frame. Same rules, sizes and colours as the browser copy
// (web/game-arcade.js, mazeChase, with the MAZE layout).

#include "esp_attr.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"

#define N 15
#define C 16                                   // cell size
#define X0 ((GC_W - N * C) / 2)
#define Y0 ((GC_H - N * C) / 2)
#define AREA_Y1 330                            // the redrawn area: the maze down to the lives
#define PI_F 3.14159265f

static const char *const MAZE[N] = {
    "###############", "#.............#", "#o##.#####.##o#", "#.............#", "#.##.#.#.#.##.#",
    "#....#...#....#", "####.#####.####", "#.............#", "####.#.#.#.####", "#.............#",
    "#.##.#####.##.#", "#o.#.......#.o#", "##.#.#.#.#.#.##", "#.............#", "###############",
};
static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
static const uint32_t GHOST_C[3] = {0xFF3030, 0xFF7AD9, 0x2FD6FF};

typedef enum { M_READY, M_RUN, M_OVER } maze_state_t;
typedef struct { int cx, cy, dir; float p; } ent_t;    // cell, direction (-1 = standing), progress to the next cell

static maze_state_t s_state;
static EXT_RAM_BSS_ATTR uint8_t s_dots[N][N];                           // 0 none, 1 dot, 2 big dot
static EXT_RAM_BSS_ATTR ent_t s_pac, s_ghost[3];
static int s_want, s_level, s_score, s_left, s_eaten, s_shown_score, s_ghost_i;
static float s_fright, s_t;
static lv_obj_t *s_score_l, *s_title, *s_hint;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static bool open_at(int x, int y) { return x >= 0 && y >= 0 && x < N && y < N && MAZE[y][x] != '#'; }
static ent_t ent(int x, int y, int dir) { return (ent_t){x, y, dir, 0}; }

static void span(int y, float xa, float xb, lv_color_t c) { gc_hline(y, (int)ceilf(xa - 0.5f), (int)floorf(xb - 0.5f) + 1, c); }
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
    if (x < 0 || y < 0 || x >= GC_W || y >= GC_H) return;
    gc_buf[y * GC_W + x] = lv_color_mix(c, gc_buf[y * GC_W + x], a);
}

static void place(void)
{
    s_pac = ent(7, 13, 3); s_want = 3;
    s_ghost[0] = ent(6, 7, 3); s_ghost[1] = ent(7, 7, 0); s_ghost[2] = ent(8, 7, 1);
    s_fright = 0; s_eaten = 0;
}
static void fill(void)
{
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) s_dots[y][x] = MAZE[y][x] == '.' ? 1 : MAZE[y][x] == 'o' ? 2 : 0;
    s_dots[13][7] = 0;
}
static void reset(void) { s_level = 0; s_score = 0; s_left = 3; fill(); place(); }

static void at(const ent_t *e, float *x, float *y)     // where it is now, in cells
{
    if (e->dir < 0) { *x = e->cx; *y = e->cy; return; }
    *x = e->cx + DX[e->dir] * e->p; *y = e->cy + DY[e->dir] * e->p;
}

static void pac_decide(ent_t *e)
{
    if (open_at(e->cx + DX[s_want], e->cy + DY[s_want])) e->dir = s_want;
    else if (e->dir >= 0 && !open_at(e->cx + DX[e->dir], e->cy + DY[e->dir])) e->dir = -1;
    int v = s_dots[e->cy][e->cx];
    if (v) {
        s_score += v == 2 ? 50 : 10;
        s_dots[e->cy][e->cx] = 0;
        if (v == 2) { s_fright = 6; s_eaten = 0; }
    }
}

static void ghost_decide(ent_t *g)
{
    int i = s_ghost_i, opts[4], n = 0, back = (g->dir + 2) % 4;
    for (int d = 0; d < 4; d++) if (open_at(g->cx + DX[d], g->cy + DY[d]) && d != back) opts[n++] = d;
    if (!n) opts[n++] = back;
    if (s_fright > 0 || (i == 2 && gc_rand() < 0.5f)) {  // blue (or the third one half the time): wander
        int k = (int)(gc_rand() * n);
        g->dir = opts[k >= n ? n - 1 : k];
        return;
    }
    int pd = s_pac.dir < 0 ? 0 : s_pac.dir;                 // the second one aims 4 cells ahead of the muncher
    float tx = s_pac.cx + (i == 1 ? DX[pd] * 4 : 0), ty = s_pac.cy + (i == 1 ? DY[pd] * 4 : 0);
    int best = opts[0];
    for (int k = 0; k < n; k++) {
        int d = opts[k];
        if (hypotf(g->cx + DX[d] - tx, g->cy + DY[d] - ty) < hypotf(g->cx + DX[best] - tx, g->cy + DY[best] - ty)) best = d;
    }
    g->dir = best;
}

static void advance(ent_t *e, float dist, void (*decide)(ent_t *))
{
    while (dist > 0) {
        if (e->p == 0) {
            decide(e);
            if (e->dir < 0 || !open_at(e->cx + DX[e->dir], e->cy + DY[e->dir])) { e->dir = -1; return; }
        }
        float s = fminf(dist, 1 - e->p);
        e->p += s; dist -= s;
        if (e->p >= 1 - 1e-6f) { e->cx += DX[e->dir]; e->cy += DY[e->dir]; e->p = 0; }
    }
}

static void overlay(const char *title, const char *hint)
{
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_hint, hint);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void begin(void)
{
    gc_fill(rgb(0x05050A));
    s_score_l = gc_label(&ui_font_FontTypoderSize20, 0xFFFFFF, LV_ALIGN_CENTER, 0, 40 - GC_H / 2);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -24);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 22);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 10, 0);
    s_t = 0;
    reset();
    s_state = M_READY;
    s_shown_score = -1;
    overlay("MAZE CHASE", "TOUCH WHERE TO GO\nTOUCH TO START");
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

static void step(float dt)
{
    if (s_pac.dir >= 0 && s_want == (s_pac.dir + 2) % 4 && s_pac.p > 0) {   // turning back mid-cell
        s_pac.cx += DX[s_pac.dir]; s_pac.cy += DY[s_pac.dir]; s_pac.p = 1 - s_pac.p; s_pac.dir = s_want;
    }
    advance(&s_pac, 5.2f * dt, pac_decide);
    if (s_fright > 0) s_fright -= dt;
    for (s_ghost_i = 0; s_ghost_i < 3; s_ghost_i++) advance(&s_ghost[s_ghost_i], (s_fright > 0 ? 2.6f : 4 + s_level * 0.3f) * dt, ghost_decide);
    float px, py;
    at(&s_pac, &px, &py);
    for (int i = 0; i < 3; i++) {
        float gx, gy;
        at(&s_ghost[i], &gx, &gy);
        if (hypotf(gx - px, gy - py) > 0.7f) continue;
        if (s_fright > 0) { s_eaten++; s_score += 200 * s_eaten; s_ghost[i] = ent(7, 7, 0); }
        else {
            s_left--;
            if (s_left <= 0) game_over(); else place();
            break;
        }
    }
    bool any = false;
    for (int y = 0; y < N && !any; y++) for (int x = 0; x < N; x++) if (s_dots[y][x]) { any = true; break; }
    if (!any) { s_level++; fill(); place(); }
}

static void draw_ghost(int i)
{
    float gx, gy;
    at(&s_ghost[i], &gx, &gy);
    float X = X0 + (gx + 0.5f) * C, Y = Y0 + (gy + 0.5f) * C;
    lv_color_t c = s_fright > 0 ? ((s_fright < 1.5f && (((int)floorf(s_t * 8)) & 1)) ? rgb(0xFFFFFF) : rgb(0x2F6BFF)) : rgb(GHOST_C[i]);
    // a half disc on top (r 7 around Y-1), straight sides, three teeth along the bottom (Y+4 .. Y+7)
    for (int y = (int)floorf(Y - 8); y < (int)ceilf(Y + 7); y++) {
        float cy = y + 0.5f;
        if (cy < Y - 1) {
            float dy = cy - (Y - 1);
            if (dy * dy > 49) continue;
            float h = sqrtf(49 - dy * dy);
            span(y, X - h, X + h, c);
        } else if (cy <= Y + 4) {
            span(y, X - 7, X + 7, c);
        } else {
            for (int x = (int)floorf(X - 7); x < (int)ceilf(X + 7); x++) {
                float u = (X + 7 - (x + 0.5f)) / (7.0f / 3.0f);
                if (u < 0 || u > 6) continue;
                int k = (int)u;
                float f = u - k, bottom = (k & 1) ? Y + 4 + 3 * f : Y + 7 - 3 * f;
                if (cy <= bottom) gc_hline(y, x, x + 1, c);
            }
        }
    }
    discf(X - 3, Y - 2, 2.4f, rgb(0xFFFFFF));
    discf(X + 3, Y - 2, 2.4f, rgb(0xFFFFFF));
    discf(X - 2.4f, Y - 2, 1.2f, rgb(0x111122));
    discf(X + 3.6f, Y - 2, 1.2f, rgb(0x111122));
}

static void draw_pac(void)
{
    float px, py;
    at(&s_pac, &px, &py);
    float X = X0 + (px + 0.5f) * C, Y = Y0 + (py + 0.5f) * C, m = (sinf(s_t * 16) + 1) * 0.32f, r = 7.5f;
    static const float FACE[4] = {-PI_F / 2, 0, PI_F / 2, PI_F};
    float face = FACE[s_pac.dir < 0 ? 0 : s_pac.dir];
    for (int y = (int)floorf(Y - r); y <= (int)ceilf(Y + r); y++) {
        for (int x = (int)floorf(X - r); x <= (int)ceilf(X + r); x++) {
            float dx = x + 0.5f - X, dy = y + 0.5f - Y;
            if (dx * dx + dy * dy > r * r) continue;
            float d = atan2f(dy, dx) - face;                  // skip the open mouth
            while (d > PI_F) d -= 2 * PI_F;
            while (d < -PI_F) d += 2 * PI_F;
            if (fabsf(d) < m) continue;
            gc_hline(y, x, x + 1, rgb(0xFFDD00));
        }
    }
}

static void frame(float dt)
{
    s_t += dt;
    if (s_state == M_RUN) step(dt);

    const lv_color_t wall = rgb(0x1B1F3A), edge = rgb(0xFFDD00);
    gc_rect(X0, Y0, X0 + N * C, AREA_Y1, rgb(0x05050A));
    bool blink = ((int)floorf(s_t * 4)) & 1;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            int X = X0 + x * C, Y = Y0 + y * C;
            if (MAZE[y][x] == '#') {
                gc_rect(X, Y, X + C, Y + C, wall);
                // 1.5 px yellow edges toward the open cells, centred on the cell border: 75 % on both pixel rows
                for (int d = 0; d < 4; d++) {
                    if (!open_at(x + DX[d], y + DY[d])) continue;
                    for (int k = 0; k < C; k++) {
                        if (d == 0) { blend(X + k, Y - 1, edge, 191); blend(X + k, Y, edge, 191); }
                        else if (d == 2) { blend(X + k, Y + C - 1, edge, 191); blend(X + k, Y + C, edge, 191); }
                        else if (d == 1) { blend(X + C - 1, Y + k, edge, 191); blend(X + C, Y + k, edge, 191); }
                        else { blend(X - 1, Y + k, edge, 191); blend(X, Y + k, edge, 191); }
                    }
                }
            } else if (s_dots[y][x] == 1) discf(X + C / 2, Y + C / 2, 2, rgb(0xFFD9A0));
            else if (s_dots[y][x] == 2 && blink) discf(X + C / 2, Y + C / 2, 5, rgb(0xFFFFFF));
        }
    for (int i = 0; i < 3; i++) draw_ghost(i);
    draw_pac();
    for (int i = 0; i < s_left; i++) discf(GC_W / 2 - (s_left - 1) * 9 + i * 18, 322, 5, rgb(0xFFDD00));
    gc_dirty(X0, Y0, X0 + N * C - 1, AREA_Y1 - 1);
    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
}

static void input(gc_input_t in, lv_point_t at_pt)
{
    if (in != GC_PRESS && in != GC_SWIPE_UP && in != GC_SWIPE_DOWN && in != GC_SWIPE_LEFT && in != GC_SWIPE_RIGHT) return;
    if (s_state != M_RUN) {
        if (in != GC_PRESS) return;
        if (s_state == M_OVER) reset();
        s_state = M_RUN;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (in == GC_PRESS) {                                 // the direction from the muncher to the finger
        float px, py;
        at(&s_pac, &px, &py);
        float dx = at_pt.x - (X0 + (px + 0.5f) * C), dy = at_pt.y - (Y0 + (py + 0.5f) * C);
        s_want = fabsf(dx) > fabsf(dy) ? (dx < 0 ? 3 : 1) : (dy < 0 ? 0 : 2);
    } else s_want = in == GC_SWIPE_UP ? 0 : in == GC_SWIPE_RIGHT ? 1 : in == GC_SWIPE_DOWN ? 2 : 3;
}

static void icon(lv_color_t *b, int n)
{
    for (int i = 0; i < n * n; i++) b[i] = rgb(0x05050A);
    int c = n / 8;                                               // a corner of the maze: walls with yellow edges
    static const char *const ICON[8] = {"########", "#......#", "#.##.#.#", "#.#....#", "#.#.##.#", "#......#", "#.##.#.#", "########"};
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int X = 2 + x * c, Y = 2 + y * c;
            if (ICON[y][x] == '#') {
                gc_rect_in(b, n, n, X, Y, X + c, Y + c, rgb(0x1B1F3A));
                if (y > 0 && ICON[y - 1][x] != '#') gc_rect_in(b, n, n, X, Y, X + c, Y + 2, rgb(0xFFDD00));
                if (y < 7 && ICON[y + 1][x] != '#') gc_rect_in(b, n, n, X, Y + c - 2, X + c, Y + c, rgb(0xFFDD00));
                if (x > 0 && ICON[y][x - 1] != '#') gc_rect_in(b, n, n, X, Y, X + 2, Y + c, rgb(0xFFDD00));
                if (x < 7 && ICON[y][x + 1] != '#') gc_rect_in(b, n, n, X + c - 2, Y, X + c, Y + c, rgb(0xFFDD00));
            } else gc_disc_in(b, n, n, X + c / 2, Y + c / 2, 2, rgb(0xFFD9A0));
        }
    // the muncher (mouth open to the right) and a red chaser
    int mx = 2 + c + c / 2 + c, my = 2 + c + c / 2, r = 13;
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r && !(x > 0 && abs(y) * 10 < x * 8)) gc_rect_in(b, n, n, mx + x, my + y, mx + x + 1, my + y + 1, rgb(0xFFDD00));
    int gx = 2 + 5 * c + c / 2, gy = 2 + 5 * c + c / 2;
    gc_disc_in(b, n, n, gx, gy - 2, 12, rgb(0xFF3030));
    gc_rect_in(b, n, n, gx - 12, gy - 2, gx + 13, gy + 12, rgb(0xFF3030));
    for (int k = 0; k < 3; k++) gc_rect_in(b, n, n, gx - 12 + k * 9 + 3, gy + 9, gx - 12 + k * 9 + 6, gy + 12, rgb(0x05050A));
    gc_disc_in(b, n, n, gx - 5, gy - 3, 4, rgb(0xFFFFFF));
    gc_disc_in(b, n, n, gx + 5, gy - 3, 4, rgb(0xFFFFFF));
    gc_disc_in(b, n, n, gx - 4, gy - 3, 2, rgb(0x111122));
    gc_disc_in(b, n, n, gx + 6, gy - 3, 2, rgb(0x111122));
}

const game_def_t game_maze = {
    .name = "MAZE CHASE", .hint = "TOUCH WHERE TO GO\nEAT EVERY DOT", .key = "maze", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
