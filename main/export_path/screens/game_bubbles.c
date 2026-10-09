// BUBBLE POP — hold and drag to aim the launcher at the bottom, lift your finger to shoot. The bubble bounces off
// the side walls and sticks to the nearest free cell of the hex grid; three or more of a colour touching pop (10
// each) and any bubbles left hanging from nothing drop away (20 each). Every 6th shot the ceiling brings a new row
// down; a bubble below the red line ends the game. Everything is redrawn every frame. Same rules, sizes and colours
// as the browser copy (web/game-arcade3.js, bubblePop).

#include <math.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"

#define PI_F 3.14159265f
#define R 13
#define COLS 9
#define L (180.0f - COLS * R - R / 2.0f)          // the playfield's left edge (56.5)
#define TOP 48
#define ROWH (R * 1.74f)
#define SX 180
#define SY 312
#define LOSE 268
#define MAXR 12                                   // rows never pass 11 (a shot on row 9 ends the game)
#define MAX_POP 160
#define MAX_FALL 256
#define FIELD_W (COLS * 2 * R + R)

typedef enum { B_READY, B_RUN, B_OVER } bubbles_state_t;
typedef struct { float x, y, t; int8_t v; } pop_t;
typedef struct { float x, y, vy; int8_t v; } fall_t;

static const uint32_t COLC[5] = {0xFF3030, 0xFFDD00, 0x2FE06B, 0x2F9BFF, 0xFF3DF2};

static bubbles_state_t s_state;
static int8_t s_g[MAXR][COLS];                    // colour 0..4, -1 = empty
static int s_rows, s_offset, s_cur, s_nxt, s_shots, s_score, s_shown_score, s_n_pop, s_n_fall;
static bool s_shot_on, s_aiming;
static float s_shot_x, s_shot_y, s_shot_vx, s_shot_vy, s_aim, s_t;
static int s_shot_v;
static pop_t s_pop[MAX_POP];
static fall_t s_fall[MAX_FALL];
static lv_color_t s_hl[5], s_lose_c;
static lv_obj_t *s_score_l, *s_title, *s_hint;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static int rndn(int n) { int v = (int)(gc_rand() * n); return v >= n ? n - 1 : v; }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static lv_color_t mix(uint32_t a, uint32_t b, float t)   // a blended toward b by t (canvas alpha)
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) out |= (uint32_t)(((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t + 0.5f) << sh;
    return rgb(out);
}
static void span(int y, float xa, float xb, lv_color_t c) { gc_hline(y, (int)ceilf(xa - 0.5f), (int)floorf(xb - 0.5f) + 1, c); }
static void rectf(float x, float y, float w, float h, lv_color_t c)
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
static void poly(const float *xs, const float *ys, int n, lv_color_t c)   // convex, at most 8 corners
{
    float y0 = ys[0], y1 = ys[0];
    for (int i = 1; i < n; i++) { y0 = fminf(y0, ys[i]); y1 = fmaxf(y1, ys[i]); }
    for (int y = (int)floorf(y0); y <= (int)ceilf(y1); y++) {
        float yc = y + 0.5f, cr[8];
        int k = 0;
        for (int i = 0; i < n && k < 8; i++) {
            int j = (i + 1) % n;
            float a = ys[i], b = ys[j];
            if ((a <= yc && b > yc) || (b <= yc && a > yc)) cr[k++] = xs[i] + (yc - a) / (b - a) * (xs[j] - xs[i]);
        }
        for (int i = 1; i < k; i++)
            for (int j = i; j > 0 && cr[j - 1] > cr[j]; j--) { float t = cr[j]; cr[j] = cr[j - 1]; cr[j - 1] = t; }
        for (int i = 0; i + 1 < k; i += 2) span(y, cr[i], cr[i + 1], c);
    }
}

static float cell_x(int r, int c) { return L + R + c * 2 * R + ((r + s_offset) % 2 ? R : 0); }
static float cell_y(int r) { return TOP + R + r * ROWH; }

// a bubble: the colour with a 55 % white shine up-left
static void bub(float x, float y, int v, float r)
{
    discf(x, y, r, rgb(COLC[v]));
    discf(x - r * 0.35f, y - r * 0.35f, r * 0.3f, s_hl[v]);
}

static int colours(int *out)   // the colours on the board, in the order they first appear
{
    int n = 0;
    bool seen[5] = {false};
    for (int r = 0; r < s_rows; r++)
        for (int c = 0; c < COLS; c++) {
            int v = s_g[r][c];
            if (v >= 0 && !seen[v]) { seen[v] = true; out[n++] = v; }
        }
    return n;
}

static int nbrs(int r, int c, int out[6][2])
{
    static const int8_t ODD[6][2] = {{0, -1}, {0, 1}, {-1, 0}, {-1, 1}, {1, 0}, {1, 1}};
    static const int8_t EVEN[6][2] = {{0, -1}, {0, 1}, {-1, -1}, {-1, 0}, {1, -1}, {1, 0}};
    const int8_t (*d)[2] = (r + s_offset) % 2 ? ODD : EVEN;
    int n = 0;
    for (int i = 0; i < 6; i++) {
        int rr = r + d[i][0], cc = c + d[i][1];
        if (rr >= 0 && cc >= 0 && cc < COLS && rr < s_rows) { out[n][0] = rr; out[n][1] = cc; n++; }
    }
    return n;
}

static void reset(void)
{
    s_offset = 0;
    memset(s_g, -1, sizeof(s_g));
    s_rows = 6;
    for (int r = 0; r < 6; r++) for (int c = 0; c < COLS; c++) s_g[r][c] = (int8_t)rndn(4);
    s_cur = rndn(4); s_nxt = rndn(4);
    s_shot_on = false; s_aim = -PI_F / 2; s_aiming = false; s_shots = 0; s_score = 0; s_n_pop = s_n_fall = 0;
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
    s_state = B_OVER;
    bool rec = gc_record(s_score);
    long best = (long)gc_best();
    lv_label_set_text(s_title, rec ? "NEW RECORD!" : "GAME OVER");
    if (best) lv_label_set_text_fmt(s_hint, "SCORE %d   BEST %ld\nTAP TO PLAY AGAIN", s_score, best);
    else lv_label_set_text_fmt(s_hint, "SCORE %d\nTAP TO PLAY AGAIN", s_score);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void settle(int r, int c, int v)
{
    if (r >= MAXR) r = MAXR - 1;                  // never (see MAXR)
    while (s_rows <= r) { memset(s_g[s_rows], -1, COLS); s_rows++; }
    s_g[r][c] = (int8_t)v;
    static int16_t q[MAXR * COLS];
    static bool seen[MAXR * COLS];
    int nb[6][2], nq = 0;
    memset(seen, 0, sizeof(seen));
    q[nq++] = (int16_t)(r * COLS + c); seen[r * COLS + c] = true;
    for (int i = 0; i < nq; i++) {                // the same colour, touching
        int k = nbrs(q[i] / COLS, q[i] % COLS, nb);
        for (int j = 0; j < k; j++) {
            int id = nb[j][0] * COLS + nb[j][1];
            if (s_g[nb[j][0]][nb[j][1]] == v && !seen[id]) { seen[id] = true; q[nq++] = (int16_t)id; }
        }
    }
    if (nq >= 3) {
        for (int i = 0; i < nq; i++) {
            int rr = q[i] / COLS, cc = q[i] % COLS;
            if (s_n_pop < MAX_POP) s_pop[s_n_pop++] = (pop_t){cell_x(rr, cc), cell_y(rr), 0.25f, s_g[rr][cc]};
            s_g[rr][cc] = -1;
        }
        s_score += nq * 10;
        memset(seen, 0, sizeof(seen));             // now: what still hangs from the ceiling
        nq = 0;
        if (s_rows)
            for (int cc = 0; cc < COLS; cc++) if (s_g[0][cc] >= 0) { seen[cc] = true; q[nq++] = (int16_t)cc; }
        for (int i = 0; i < nq; i++) {
            int k = nbrs(q[i] / COLS, q[i] % COLS, nb);
            for (int j = 0; j < k; j++) {
                int id = nb[j][0] * COLS + nb[j][1];
                if (s_g[nb[j][0]][nb[j][1]] >= 0 && !seen[id]) { seen[id] = true; q[nq++] = (int16_t)id; }
            }
        }
        for (int rr = 0; rr < s_rows; rr++)
            for (int cc = 0; cc < COLS; cc++) {
                if (s_g[rr][cc] < 0 || seen[rr * COLS + cc]) continue;
                if (s_n_fall < MAX_FALL) s_fall[s_n_fall++] = (fall_t){cell_x(rr, cc), cell_y(rr), 0, s_g[rr][cc]};
                s_g[rr][cc] = -1;
                s_score += 20;
            }
    }
    while (s_rows) {                              // drop empty rows at the bottom
        bool empty = true;
        for (int cc = 0; cc < COLS; cc++) if (s_g[s_rows - 1][cc] >= 0) empty = false;
        if (!empty) break;
        s_rows--;
    }
    s_shots++;
    int cs[5], ncs;
    if (s_shots % 6 == 0) {                       // the ceiling comes down a row
        ncs = colours(cs);
        int8_t row[COLS];
        for (int cc = 0; cc < COLS; cc++) row[cc] = (int8_t)(ncs ? cs[rndn(ncs)] : rndn(4));
        if (s_rows >= MAXR) s_rows = MAXR - 1;    // never (see MAXR)
        memmove(s_g[1], s_g[0], sizeof(s_g[0]) * s_rows);
        memcpy(s_g[0], row, COLS);
        s_rows++;
        s_offset = (s_offset + 1) % 2;
    }
    if (!s_rows) {
        for (int cc = 0; cc < COLS; cc++) s_g[0][cc] = (int8_t)rndn(5);
        s_rows = 1;
    }
    if (s_rows && cell_y(s_rows - 1) + R > LOSE) game_over();
    ncs = colours(cs);
    s_cur = s_nxt;
    bool has = false;
    for (int i = 0; i < ncs; i++) if (cs[i] == s_cur) has = true;
    if (ncs && !has) s_cur = cs[rndn(ncs)];
    s_nxt = ncs ? cs[rndn(ncs)] : rndn(4);
}

static void fire(void)
{
    if (s_shot_on) return;
    s_shot_on = true;
    s_shot_x = SX; s_shot_y = SY; s_shot_vx = cosf(s_aim) * 620; s_shot_vy = sinf(s_aim) * 620; s_shot_v = s_cur;
}

static void aim_at(float x, float y)
{
    s_aim = clampf(atan2f(fminf(y, SY - 10) - SY, x - SX), -PI_F + 0.2f, -0.2f);
}

static void begin(void)
{
    for (int i = 0; i < 5; i++) s_hl[i] = mix(COLC[i], 0xFFFFFF, 0.55f);
    s_lose_c = mix(0x11122A, 0xFF3030, 0.5f);
    s_score_l = gc_label(&ui_font_FontTypoderSize20, 0xFFFFFF, LV_ALIGN_CENTER, 0, 24 - GC_H / 2);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -30);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 8, 0);
    s_t = 0;
    reset();
    s_state = B_READY;
    s_shown_score = -1;
    overlay("BUBBLE POP", "HOLD AND DRAG TO AIM\nLIFT TO SHOOT\nTOUCH TO START");
}

static void step(float dt)
{
    for (int s = 0; s < 4 && s_shot_on; s++) {
        s_shot_x += s_shot_vx * dt / 4; s_shot_y += s_shot_vy * dt / 4;
        if (s_shot_x < L + R) { s_shot_x = L + R; s_shot_vx = fabsf(s_shot_vx); }
        if (s_shot_x > L + COLS * 2 * R) { s_shot_x = L + COLS * 2 * R; s_shot_vx = -fabsf(s_shot_vx); }
        bool hit = s_shot_y <= TOP + R;
        for (int r = 0; r < s_rows && !hit; r++)
            for (int c = 0; c < COLS; c++)
                if (s_g[r][c] >= 0 && hypotf(cell_x(r, c) - s_shot_x, cell_y(r) - s_shot_y) < 2 * R - 3) { hit = true; break; }
        if (hit) {                                 // stick to the nearest free cell (one row below the last counts)
            int br = 0, bc = 0;
            float bd = 1e9f;
            for (int r = 0; r <= s_rows && r < MAXR; r++)
                for (int c = 0; c < COLS; c++) {
                    if (r < s_rows && s_g[r][c] >= 0) continue;
                    float d = hypotf(cell_x(r, c) - s_shot_x, cell_y(r) - s_shot_y);
                    if (d < bd) { bd = d; br = r; bc = c; }
                }
            s_shot_on = false;
            settle(br, bc, s_shot_v);
        }
    }
    int n = 0;
    for (int i = 0; i < s_n_fall; i++) {
        fall_t *f = &s_fall[i];
        f->vy += 900 * dt; f->y += f->vy * dt;
        if (f->y < GC_H + 20) s_fall[n++] = *f;
    }
    s_n_fall = n;
    n = 0;
    for (int i = 0; i < s_n_pop; i++) {
        s_pop[i].t -= dt;
        if (s_pop[i].t > 0) s_pop[n++] = s_pop[i];
    }
    s_n_pop = n;
}

static void aim_line(void)   // dashed 4 on / 6 off, 2 px wide, 120 px long, half-white over what is there
{
    float co = cosf(s_aim), si = sinf(s_aim), ex = SX + co * 120, ey = SY + si * 120;
    int x0 = (int)floorf(fminf(SX, ex)) - 2, x1 = (int)ceilf(fmaxf(SX, ex)) + 2;
    int y0 = (int)floorf(fminf(SY, ey)) - 2, y1 = (int)ceilf(fmaxf(SY, ey)) + 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > GC_W - 1) x1 = GC_W - 1;
    if (y1 > GC_H - 1) y1 = GC_H - 1;
    lv_color_t white = rgb(0xFFFFFF);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - SX, dy = y + 0.5f - SY, u = dx * co + dy * si, v = -dx * si + dy * co;
            if (u < 0 || u > 120 || fabsf(v) > 1 || fmodf(u, 10) >= 4) continue;
            gc_buf[y * GC_W + x] = lv_color_mix(white, gc_buf[y * GC_W + x], 128);
        }
}

static void frame(float dt)
{
    s_t += dt;
    if (s_state == B_RUN) step(dt);

    gc_fill(rgb(0x0A0A18));
    rectf(L, 0, FIELD_W, GC_H, rgb(0x11122A));
    rectf(L, TOP - 4, FIELD_W, 4, rgb(0xFFDD00));
    rectf(L, LOSE, FIELD_W, 1, s_lose_c);
    for (int r = 0; r < s_rows; r++)
        for (int c = 0; c < COLS; c++) if (s_g[r][c] >= 0) bub(cell_x(r, c), cell_y(r), s_g[r][c], R - 1);
    for (int i = 0; i < s_n_pop; i++) bub(s_pop[i].x, s_pop[i].y, s_pop[i].v, (R - 1) * (1 + (0.25f - s_pop[i].t) * 2));
    for (int i = 0; i < s_n_fall; i++) bub(s_fall[i].x, s_fall[i].y, s_fall[i].v, R - 1);
    if (s_state == B_RUN && s_aiming) aim_line();
    {                                              // the barrel (10 x 26, 4 px out from the centre), turned to the aim
        static const float BX[4] = {-5, 5, 5, -5}, BY[4] = {-30, -30, -4, -4};
        float a = s_aim + PI_F / 2, co = cosf(a), si = sinf(a), xs[4], ys[4];
        for (int i = 0; i < 4; i++) { xs[i] = SX + BX[i] * co - BY[i] * si; ys[i] = SY + BX[i] * si + BY[i] * co; }
        poly(xs, ys, 4, rgb(0x555555));
    }
    discf(SX, SY, 15, rgb(0x2A2A3A));
    if (!s_shot_on) bub(SX, SY, s_cur, R - 1);
    else bub(s_shot_x, s_shot_y, s_shot_v, R - 1);
    bub(SX + 38, SY + 8, s_nxt, 8);
    gc_dirty_all();
    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
}

static void input(gc_input_t in, lv_point_t at)
{
    if (s_state != B_RUN) {
        if (in != GC_PRESS) return;
        if (s_state == B_OVER) reset();
        s_state = B_RUN;
        s_aiming = false;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (in == GC_PRESS || in == GC_DRAG) { s_aiming = true; aim_at(at.x, at.y); }
    else if (in == GC_RELEASE && s_aiming) { s_aiming = false; fire(); }
}

static void icon(lv_color_t *b, int n)
{
    for (int i = 0; i < n * n; i++) b[i] = rgb(0x0A0A18);
    gc_rect_in(b, n, n, 10, 0, n - 10, n, rgb(0x11122A));
    gc_rect_in(b, n, n, 10, 14, n - 10, 18, rgb(0xFFDD00));
    static const int8_t ROWS[3][5] = {{0, 1, 3, 3, 2}, {1, 1, 4, 2}, {0, 3, 2}};
    static const int NR[3] = {5, 4, 3};
    for (int r = 0; r < 3; r++)                   // three rows of bubbles, each one shifted half a bubble
        for (int c = 0; c < NR[r]; c++) {
            int x = 22 + r * 11 + c * 22, y = 30 + r * 19, v = ROWS[r][c];
            gc_disc_in(b, n, n, x, y, 10, rgb(COLC[v]));
            gc_disc_in(b, n, n, x - 4, y - 4, 3, mix(COLC[v], 0xFFFFFF, 0.55f));
        }
    for (int k = 0; k < 4; k++) gc_rect_in(b, n, n, 66 - k * 4, 100 - k * 10 - 4, 66 - k * 4 + 3, 100 - k * 10, rgb(0x8A8A9A));   // the aim
    gc_rect_in(b, n, n, n / 2 - 4, 98, n / 2 + 4, 112, rgb(0x555555));
    gc_disc_in(b, n, n, n / 2, 116, 13, rgb(0x2A2A3A));
    gc_disc_in(b, n, n, n / 2, 116, 10, rgb(COLC[1]));
    gc_disc_in(b, n, n, n / 2 - 4, 112, 3, mix(COLC[1], 0xFFFFFF, 0.55f));
}

const game_def_t game_bubbles = {
    .name = "BUBBLE POP", .hint = "DRAG TO AIM\nLIFT TO SHOOT", .key = "bubbles", .unit = "", .wants_release = true,
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
