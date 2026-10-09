// DARK MAZE 3D — a first-person maze: you walk forward all the time, drag left / right to turn, tap to fire. Find
// the green exit before the clock runs out (100 points plus 5 a second left; a bigger maze next); the red drones
// hunt you once you are near (25 points a shot drone, a life if one reaches you). Three lives; the clock running
// out costs one too (and gives 30 s more). A raycaster: 180 columns, 2 pixels wide, grey stone brick walls,
// drones as discs behind a per-column depth check. Everything moves, so every frame is redrawn. Same rules, sizes
// and colours as the browser copy (web/game-arcade3.js, darkMaze).

#include <math.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"

#define COLS 180
#define FOV 1.05f
#define SPEED 1.6f
#define TURN 0.012f
#define MAXN 17
#define MAX_DRONES 160
#define MAX_SHOTS 32
#define MAX_RUNS 18                            // per column: sky, wall / mortar pieces, floor
#define PI_F 3.14159265f
#define TAU_F (2 * PI_F)

typedef enum { D_READY, D_RUN, D_OVER } dm_state_t;
typedef struct { float x, y, a; bool hp; } drone_t;
typedef struct { float x, y, a, d; bool dead; } shot_t;

static dm_state_t s_state;
static uint8_t s_map[MAXN][MAXN];
static int s_n, s_level, s_score, s_left;
static float s_px, s_py, s_pa, s_clock, s_hurt, s_flash, s_t;
static drone_t s_drone[MAX_DRONES];
static shot_t s_shot[MAX_SHOTS];
static int s_n_drone, s_n_shot;
static bool s_has_last;
static int s_last_x;
static bool s_tint;                            // the red hurt flash: every colour drawn under it is pre-blended

static float s_off_cos[COLS], s_off_sin[COLS], s_zbuf[COLS];
static lv_color_t s_row[GC_H], s_row_hurt[GC_H];   // the sky / floor gradients
static int16_t s_run_end[COLS][MAX_RUNS];
static uint8_t s_run_kind[COLS][MAX_RUNS];     // 0 sky / floor, 1 stone, 2 mortar
static lv_color_t s_wall_c[COLS], s_mortar_c[COLS];

static lv_obj_t *s_score_l, *s_clock_l, *s_lv_l, *s_title, *s_hint;
static int s_shown_score, s_shown_clock, s_shown_lv, s_shown_red;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static int ipx(float v) { return (int)floorf(clampf(v, -10000, 10000) + 0.5f); }
static float sgnf(float v) { return v > 0 ? 1.f : v < 0 ? -1.f : 0.f; }
static float wrapf(float a) { return a - TAU_F * floorf((a + PI_F) / TAU_F); }   // -PI .. PI
static int rnd_int(int n) { int k = (int)(gc_rand() * n); return k >= n ? n - 1 : k < 0 ? 0 : k; }

// a colour, under the red flash when it is on (canvas: rgba(255,0,0,.3) over everything)
static lv_color_t tcol(float r, float g, float b)
{
    if (s_tint) { r = r * 0.7f + 255 * 0.3f; g *= 0.7f; b *= 0.7f; }
    return lv_color_make((uint8_t)clampf(r + 0.5f, 0, 255), (uint8_t)clampf(g + 0.5f, 0, 255), (uint8_t)clampf(b + 0.5f, 0, 255));
}
static lv_color_t thex(uint32_t h) { return tcol((h >> 16) & 0xFF, (h >> 8) & 0xFF, h & 0xFF); }

// ---------- drawing (pixel centres inside the shape are filled) ----------
static void span(int y, float xa, float xb, lv_color_t c)
{
    gc_hline(y, (int)ceilf(clampf(xa, -1000, 1000) - 0.5f), (int)floorf(clampf(xb, -1000, 1000) - 0.5f) + 1, c);
}
static void discf(float cx, float cy, float r, lv_color_t c)
{
    int ya = (int)floorf(clampf(cy - r, -1, GC_H)), yb = (int)ceilf(clampf(cy + r, -1, GC_H));
    for (int y = ya < 0 ? 0 : ya; y <= yb && y < GC_H; y++) {
        float dy = y + 0.5f - cy;
        if (dy * dy > r * r) continue;
        float h = sqrtf(r * r - dy * dy);
        span(y, cx - h, cx + h, c);
    }
}
static void rectf(float x, float y, float w, float h, lv_color_t c) { gc_rect(ipx(x), ipx(y), ipx(x + w), ipx(y + h), c); }
static void blend_rect(float x, float y, float w, float h, lv_color_t c, uint8_t a)
{
    int x0 = ipx(x), y0 = ipx(y), x1 = ipx(x + w), y1 = ipx(y + h);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > GC_W) x1 = GC_W;
    if (y1 > GC_H) y1 = GC_H;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) gc_buf[yy * GC_W + xx] = lv_color_mix(c, gc_buf[yy * GC_W + xx], a);
}
static void poly(const float *xs, const float *ys, int n, lv_color_t c)   // filled, even-odd, at most 8 corners
{
    float y0 = ys[0], y1 = ys[0];
    for (int i = 1; i < n; i++) { y0 = fminf(y0, ys[i]); y1 = fmaxf(y1, ys[i]); }
    int ya = (int)floorf(y0), yb = (int)ceilf(y1);
    if (ya < 0) ya = 0;
    if (yb > GC_H - 1) yb = GC_H - 1;
    for (int y = ya; y <= yb; y++) {
        float yc = y + 0.5f, cross[8];
        int m = 0;
        for (int i = 0, j = n - 1; i < n; j = i++)
            if ((ys[i] <= yc && yc < ys[j]) || (ys[j] <= yc && yc < ys[i]))
                if (m < 8) cross[m++] = xs[i] + (yc - ys[i]) * (xs[j] - xs[i]) / (ys[j] - ys[i]);
        for (int a = 1; a < m; a++)
            for (int b = a; b > 0 && cross[b - 1] > cross[b]; b--) { float t = cross[b]; cross[b] = cross[b - 1]; cross[b - 1] = t; }
        for (int k = 0; k + 1 < m; k += 2) span(y, cross[k], cross[k + 1], c);
    }
}

// ---------- the maze ----------
static int cell(int x, int y) { return x < 0 || y < 0 || x >= s_n || y >= s_n ? 1 : s_map[y][x]; }
static int wall(float x, float y) { return cell((int)floorf(clampf(x, -2, MAXN + 2)), (int)floorf(clampf(y, -2, MAXN + 2))); }

static void build(void)
{
    s_n = 9 + 2 * (s_level < 4 ? s_level : 4);
    int n = s_n;
    memset(s_map, 1, sizeof s_map);
    static uint8_t stack[MAXN * MAXN][2];      // depth-first carving from (1,1)
    int sp = 0;
    stack[sp][0] = 1; stack[sp][1] = 1; sp++;
    s_map[1][1] = 0;
    static const int8_t D[4][2] = {{2, 0}, {-2, 0}, {0, 2}, {0, -2}};
    while (sp) {
        int x = stack[sp - 1][0], y = stack[sp - 1][1], opts[4], k = 0;
        for (int d = 0; d < 4; d++) {
            int nx = x + D[d][0], ny = y + D[d][1];
            if (nx > 0 && ny > 0 && nx < n - 1 && ny < n - 1 && s_map[ny][nx]) opts[k++] = d;
        }
        if (!k) { sp--; continue; }
        int d = opts[rnd_int(k)];
        s_map[y + D[d][1] / 2][x + D[d][0] / 2] = 0;
        s_map[y + D[d][1]][x + D[d][0]] = 0;
        if (sp < MAXN * MAXN) { stack[sp][0] = (uint8_t)(x + D[d][0]); stack[sp][1] = (uint8_t)(y + D[d][1]); sp++; }
    }
    for (int i = 0; i < n; i++) {              // a few extra openings (loops)
        int x = 1 + 2 * rnd_int((n - 1) / 2), y = 1 + 2 * rnd_int((n - 1) / 2);
        int dx = gc_rand() < 0.5f ? 1 : 0, dy = 1 - dx;
        int tx = x + dx, ty = y + dy;
        if (tx > 0 && ty > 0 && tx < n - 1 && ty < n - 1 && tx < MAXN && ty < MAXN && s_map[ty][tx] == 1) s_map[ty][tx] = 0;
    }
    s_map[n - 2][n - 2] = 2;
    s_px = 1.5f; s_py = 1.5f;
    s_pa = s_map[1][2] == 0 ? 0 : PI_F / 2;
    static uint8_t free_c[MAXN * MAXN][2];
    int nf = 0;
    for (int y = 1; y < n - 1; y++)
        for (int x = 1; x < n - 1; x++)
            if (!s_map[y][x] && x + y > 6) { free_c[nf][0] = (uint8_t)x; free_c[nf][1] = (uint8_t)y; nf++; }
    s_n_drone = 0;
    for (int i = 0; i < 2 + s_level && nf && s_n_drone < MAX_DRONES; i++) {
        int k = rnd_int(nf), x = free_c[k][0], y = free_c[k][1];
        memmove(free_c[k], free_c[k + 1], (size_t)(nf - k - 1) * 2);   // splice it out
        nf--;
        s_drone[s_n_drone++] = (drone_t){x + 0.5f, y + 0.5f, gc_rand() * TAU_F, true};
    }
    s_n_shot = 0;
    s_clock = 60 + s_level * 10;
}

static void reset(void) { s_level = 0; s_score = 0; s_left = 3; s_hurt = 0; s_flash = 0; build(); }

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

static void fire(void)
{
    if (s_n_shot < MAX_SHOTS) s_shot[s_n_shot++] = (shot_t){s_px, s_py, s_pa, 0, false};
    s_flash = 0.08f;
}

static void step(float dt)
{
    s_clock -= dt;
    if (s_hurt > 0) s_hurt -= dt;
    if (s_flash > 0) s_flash -= dt;
    // always walking forward; each axis stops at a wall (0.22 cells before it)
    const float R = 0.22f;
    float co = cosf(s_pa), si = sinf(s_pa), st = SPEED * dt, nx = s_px + co * st, ny = s_py + si * st;
    if (wall(nx + sgnf(co) * R, s_py) != 1) s_px = nx;
    if (wall(s_px, ny + sgnf(si) * R) != 1) s_py = ny;
    if (wall(s_px, s_py) == 2) { s_score += 100 + (int)floorf(s_clock) * 5; s_level++; build(); }

    for (int i = 0; i < s_n_shot; i++) {
        shot_t *s = &s_shot[i];
        s->d += dt * 9; s->x += cosf(s->a) * dt * 9; s->y += sinf(s->a) * dt * 9;
        if (wall(s->x, s->y) == 1) s->dead = true;
        for (int k = 0; k < s_n_drone; k++) {
            drone_t *d = &s_drone[k];
            if (!s->dead && d->hp && hypotf(d->x - s->x, d->y - s->y) < 0.35f) { d->hp = false; s->dead = true; s_score += 25; }
        }
    }
    int n = 0;
    for (int i = 0; i < s_n_shot; i++) if (!s_shot[i].dead && s_shot[i].d < 12) s_shot[n++] = s_shot[i];
    s_n_shot = n;

    for (int k = 0; k < s_n_drone; k++) {
        drone_t *d = &s_drone[k];
        if (!d->hp) continue;
        bool near = hypotf(d->x - s_px, d->y - s_py) < 4;
        float want = near ? atan2f(s_py - d->y, s_px - d->x) : d->a, v = dt * (near ? 0.9f + s_level * 0.08f : 0.6f);
        float nx2 = d->x + cosf(want) * v, ny2 = d->y + sinf(want) * v;
        if (wall(nx2, ny2) != 1) { d->x = nx2; d->y = ny2; }
        else d->a = gc_rand() * TAU_F;
        if (hypotf(d->x - s_px, d->y - s_py) < 0.4f && s_hurt <= 0) {
            s_hurt = 1.5f; d->hp = false;
            s_left--;
            if (s_left <= 0) { game_over(); return; }
        }
    }
    if (s_clock <= 0) {
        s_left--;
        if (s_left <= 0) game_over(); else s_clock = 30;
    }
}

// ---------- drawing a frame ----------
static int row_of(float a) { int r = (int)ceilf(clampf(a, -1, GC_H + 1) - 0.5f); return r < 0 ? 0 : r > GC_H ? GC_H : r; }   // first pixel row at / below a

static void cast(void)                         // every column: the ray, its depth, and its runs of sky / stone / mortar / floor
{
    float co = cosf(s_pa), si = sinf(s_pa);
    for (int c = 0; c < COLS; c++) {
        float dx = co * s_off_cos[c] - si * s_off_sin[c], dy = si * s_off_cos[c] + co * s_off_sin[c];
        int mx = (int)floorf(s_px), my = (int)floorf(s_py);
        float ddx = dx == 0 ? 1e30f : fabsf(1 / dx), ddy = dy == 0 ? 1e30f : fabsf(1 / dy);
        int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1, side = 0, hit = 0;
        float sdx = (dx < 0 ? s_px - mx : mx + 1 - s_px) * ddx, sdy = (dy < 0 ? s_py - my : my + 1 - s_py) * ddy;
        for (int i = 0; i < 64 && !hit; i++) {
            if (sdx < sdy) { sdx += ddx; mx += sx; side = 0; }
            else { sdy += ddy; my += sy; side = 1; }
            hit = cell(mx, my);
        }
        float raw = side ? sdy - ddy : sdx - ddx;
        raw = clampf(raw, 0, 1000);
        float dist = raw * s_off_cos[c], h = fminf(GC_H * 2, GC_H / fmaxf(0.05f, dist) * 0.9f);
        s_zbuf[c] = dist;
        float shade = clampf(1 - dist / 9, 0.15f, 1) * (side ? 0.75f : 1), top = GC_H / 2.f - h / 2;
        if (hit == 2) s_wall_c[c] = tcol(roundf(47 * shade), roundf(224 * shade), roundf(107 * shade));
        else s_wall_c[c] = tcol(roundf(132 * shade), roundf(140 * shade), roundf(160 * shade));
        s_mortar_c[c] = tcol(roundf(40 * shade), roundf(42 * shade), roundf(50 * shade));
        int y0 = row_of(top), y1 = row_of(top + h);
        // the mortar pieces: three lines at the quarters, and the joints (offset by half a brick every other row)
        int ms[7], me[7], nm = 0;
        if (hit == 1) {
            float w = side ? s_px + raw * dx : s_py + raw * dy, wx = w - floorf(w), mh = fmaxf(1, h / 40);
            for (int k = 1; k < 4; k++) { float a = top + h * k / 4 - mh / 2; ms[nm] = row_of(a); me[nm] = row_of(a + mh); nm++; }
            for (int k = 0; k < 4; k++) {
                float j = wx + (k & 1) * 0.5f;
                j -= floorf(j);
                if (j < 0.04f) { ms[nm] = row_of(top + h * k / 4); me[nm] = row_of(top + h * (k + 1) / 4); nm++; }
            }
            for (int a = 1; a < nm; a++)        // by where they start
                for (int b = a; b > 0 && ms[b - 1] > ms[b]; b--) {
                    int t = ms[b]; ms[b] = ms[b - 1]; ms[b - 1] = t;
                    t = me[b]; me[b] = me[b - 1]; me[b - 1] = t;
                }
        }
        int16_t *end = s_run_end[c];
        uint8_t *kind = s_run_kind[c];
        int r = 0, at = y0;
        end[r] = (int16_t)y0; kind[r++] = 0;
        for (int i = 0; i < nm; i++) {
            int a = ms[i] < at ? at : ms[i], b = me[i] > y1 ? y1 : me[i];
            if (b <= a) continue;
            if (a > at) { end[r] = (int16_t)a; kind[r++] = 1; }
            end[r] = (int16_t)b; kind[r++] = 2;
            at = b;
        }
        if (y1 > at) { end[r] = (int16_t)y1; kind[r++] = 1; }
        end[r] = GC_H; kind[r++] = 0;
    }
}

static void paint_walls(void)                  // row by row (the canvas is in PSRAM): the runs of each column
{
    uint8_t cur[COLS];
    memset(cur, 0, sizeof cur);
    const lv_color_t *rows = s_tint ? s_row_hurt : s_row;
    for (int y = 0; y < GC_H; y++) {
        lv_color_t *p = gc_buf + y * GC_W, bg = rows[y];
        for (int c = 0; c < COLS; c++) {
            int k = cur[c];
            while (y >= s_run_end[c][k]) k++;
            cur[c] = (uint8_t)k;
            int kd = s_run_kind[c][k];
            lv_color_t v = kd == 0 ? bg : kd == 1 ? s_wall_c[c] : s_mortar_c[c];
            p[2 * c] = v;
            p[2 * c + 1] = v;
        }
    }
}

static void draw_drones(void)                  // the farthest first
{
    int idx[MAX_DRONES], m = 0;
    float dd[MAX_DRONES];
    for (int i = 0; i < s_n_drone; i++) if (s_drone[i].hp) { idx[m] = i; dd[i] = hypotf(s_drone[i].x - s_px, s_drone[i].y - s_py); m++; }
    for (int a = 1; a < m; a++)
        for (int b = a; b > 0 && dd[idx[b - 1]] < dd[idx[b]]; b--) { int t = idx[b]; idx[b] = idx[b - 1]; idx[b - 1] = t; }
    lv_color_t eye = thex(((int)floorf(s_t * 6)) & 1 ? 0xFFDD00 : 0xFF8A00);
    for (int i = 0; i < m; i++) {
        const drone_t *d = &s_drone[idx[i]];
        float dx = d->x - s_px, dy = d->y - s_py, dist = dd[idx[i]], ang = wrapf(atan2f(dy, dx) - s_pa);
        if (fabsf(ang) > FOV / 2 + 0.2f || dist < 0.2f) continue;
        float sxp = (ang / FOV + 0.5f) * GC_W, size = GC_H / dist * 0.45f;
        int col = (int)floorf(sxp / 2);
        if (col >= 0 && col < COLS && s_zbuf[col] < dist) continue;
        float y = GC_H / 2.f + size * 0.1f + sinf(s_t * 4 + d->x) * size * 0.08f;
        discf(sxp, y, size * 0.5f, thex(0xFF3030));
        discf(sxp, y, size * 0.28f, thex(0x2A0000));
        discf(sxp, y, size * 0.14f, eye);
        rectf(sxp - size * 0.75f, y - size * 0.06f, size * 1.5f, size * 0.12f, thex(0x666666));
    }
}

static void frame(float dt)
{
    s_t += dt;
    if (s_state == D_RUN) step(dt);

    s_tint = s_hurt > 1.2f;
    cast();
    paint_walls();
    draw_drones();
    for (int i = 0; i < s_n_shot; i++) {
        const shot_t *s = &s_shot[i];
        float dx = s->x - s_px, dy = s->y - s_py, dist = hypotf(dx, dy);
        if (dist < 0.3f) continue;
        float ang = wrapf(atan2f(dy, dx) - s_pa);
        discf((ang / FOV + 0.5f) * GC_W, GC_H / 2.f + GC_H / dist * 0.12f, fmaxf(2, 10 / dist), thex(0xFFDD00));
    }
    // the crosshair (60 % white, the middle twice), the gun, the muzzle flash
    lv_color_t white = thex(0xFFFFFF);
    blend_rect(GC_W / 2 - 1, GC_H / 2 - 8, 2, 16, white, 153);
    blend_rect(GC_W / 2 - 8, GC_H / 2 - 1, 16, 2, white, 153);
    static const float GX[4] = {GC_W / 2 - 18, GC_W / 2 - 10, GC_W / 2 + 10, GC_W / 2 + 18}, GY[4] = {GC_H, GC_H - 62, GC_H - 62, GC_H};
    poly(GX, GY, 4, thex(0x2A2A2A));
    rectf(GC_W / 2 - 4, GC_H - 70, 8, 10, thex(0x555555));
    if (s_flash > 0) discf(GC_W / 2, GC_H - 72, 12, thex(0xFFDD00));
    s_tint = false;
    // the minimap and the lives (over the red flash)
    float ms = fminf(4, 56.f / s_n), mx = GC_W / 2.f - s_n * ms / 2, my = 26;
    for (int y = 0; y < s_n; y++)
        for (int x = 0; x < s_n; x++) {
            if (!s_map[y][x]) continue;
            if (s_map[y][x] == 2) rectf(mx + x * ms, my + y * ms, ms, ms, rgb(0x2FE06B));
            else blend_rect(mx + x * ms, my + y * ms, ms, ms, rgb(0xFFDD00), 115);
        }
    discf(mx + s_px * ms, my + s_py * ms, 2, rgb(0xFFFFFF));
    for (int i = 0; i < s_left; i++) discf(GC_W / 2 - (s_left - 1) * 9 + i * 18, 320, 5, rgb(0xFFDD00));
    gc_dirty_all();

    int clk = (int)ceilf(clampf(s_clock, -1000, 100000)), red = s_clock < 10;
    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
    if (clk != s_shown_clock) { lv_label_set_text_fmt(s_clock_l, "%dS", clk); s_shown_clock = clk; }
    if (red != s_shown_red) { lv_obj_set_style_text_color(s_clock_l, rgb(red ? 0xFF3030 : 0xFFFFFF), 0); s_shown_red = red; }
    if (s_level != s_shown_lv) { lv_label_set_text_fmt(s_lv_l, "LV %d", s_level + 1); s_shown_lv = s_level; }
}

static void begin(void)
{
    for (int c = 0; c < COLS; c++) { float o = ((float)c / COLS - 0.5f) * FOV; s_off_cos[c] = cosf(o); s_off_sin[c] = sinf(o); }
    for (int y = 0; y < GC_H; y++) {           // sky #05050F -> #20183A, floor #1A1A1A -> #3A3A3A
        bool sky = y < GC_H / 2;
        float f = ((sky ? y : y - GC_H / 2) + 0.5f) / (GC_H / 2.f);
        float r = sky ? 0x05 + (0x20 - 0x05) * f : 0x1A + (0x3A - 0x1A) * f;
        float g = sky ? 0x05 + (0x18 - 0x05) * f : 0x1A + (0x3A - 0x1A) * f;
        float b = sky ? 0x0F + (0x3A - 0x0F) * f : 0x1A + (0x3A - 0x1A) * f;
        s_tint = false; s_row[y] = tcol(r, g, b);
        s_tint = true; s_row_hurt[y] = tcol(r, g, b);
    }
    s_tint = false;
    s_score_l = gc_label(&ui_font_FontTypoderSize20, 0xFFFFFF, LV_ALIGN_CENTER, -90, 96 - GC_H / 2);
    s_clock_l = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 90, 96 - GC_H / 2);
    s_lv_l = gc_label(&ui_font_FontTypoderSize16, 0xFFDD00, LV_ALIGN_CENTER, 0, 300 - GC_H / 2);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -30);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 22);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 8, 0);
    s_t = 0;
    s_has_last = false;
    reset();
    s_state = D_READY;
    s_shown_score = s_shown_clock = s_shown_lv = s_shown_red = -1;
    overlay("DARK MAZE 3D", "DRAG TO TURN - TAP TO FIRE\nFIND THE GREEN EXIT\nTAP TO START");
}

static void input(gc_input_t in, lv_point_t at)
{
    if (s_state != D_RUN) {
        if (in != GC_TAP) return;
        if (s_state == D_OVER) reset();
        s_state = D_RUN;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (in == GC_PRESS) { s_last_x = at.x; s_has_last = true; }
    else if (in == GC_DRAG && s_has_last) { s_pa = wrapf(s_pa + (at.x - s_last_x) * TURN); s_last_x = at.x; }
    else if (in == GC_TAP) fire();
    else if (in == GC_RELEASE) s_has_last = false;
}

static void icon(lv_color_t *b, int n)         // down a corridor: brick walls each side, the green exit, a drone
{
    for (int y = 0; y < n; y++) {
        bool sky = y < n / 2;
        float f = ((sky ? y : y - n / 2) + 0.5f) / (n / 2.f);
        lv_color_t c = sky ? lv_color_make((uint8_t)(5 + 27 * f), (uint8_t)(5 + 19 * f), (uint8_t)(15 + 43 * f))
                           : lv_color_make((uint8_t)(26 + 32 * f), (uint8_t)(26 + 32 * f), (uint8_t)(26 + 32 * f));
        for (int x = 0; x < n; x++) b[y * n + x] = c;
    }
    int ex = n * 3 / 8, ew = n / 4;            // the far end: the exit
    for (int x = 0; x < n; x++) {
        int d = x < ex ? ex - x : x >= ex + ew ? x - (ex + ew - 1) : 0;   // how far toward the edge (perspective)
        float h = d ? ew + d * 2.0f : ew * 1.0f;
        if (h > n * 2) h = n * 2;
        int top = (int)(n / 2 - h / 2), bot = (int)(n / 2 + h / 2);
        float shade = d ? 0.45f + 0.55f * d / (n * 3 / 8.f) : 0.8f;
        lv_color_t wc = d ? lv_color_make((uint8_t)(132 * shade), (uint8_t)(140 * shade), (uint8_t)(160 * shade))
                          : lv_color_make((uint8_t)(47 * shade), (uint8_t)(224 * shade), (uint8_t)(107 * shade));
        lv_color_t mc = lv_color_make((uint8_t)(40 * shade), (uint8_t)(42 * shade), (uint8_t)(50 * shade));
        for (int y = top < 0 ? 0 : top; y < bot && y < n; y++) {
            bool mortar = false;
            if (d) {
                int q = (int)((y - top) * 4 / h);
                float fy = (y - top) * 4 / h - q;
                if (fy < 0.06f && q > 0) mortar = true;
                int jx = (d / 9 + (q & 1)) % 3;
                if (jx == 0 && d % 9 == 0) mortar = true;
            }
            b[y * n + x] = mortar ? mc : wc;
        }
    }
    int cx = n / 2, cy = n / 2 + 2;            // a drone in the middle
    gc_disc_in(b, n, n, cx, cy, 14, rgb(0xFF3030));
    gc_disc_in(b, n, n, cx, cy, 8, rgb(0x2A0000));
    gc_disc_in(b, n, n, cx, cy, 4, rgb(0xFFDD00));
    gc_rect_in(b, n, n, cx - 21, cy - 2, cx + 21, cy + 2, rgb(0x666666));
    for (int y = n - 30; y < n; y++) {         // the gun
        float w = 5 + (y - (n - 30)) * 6 / 30.f;
        gc_rect_in(b, n, n, (int)(cx - w), y, (int)(cx + w), y + 1, rgb(0x2A2A2A));
    }
    gc_rect_in(b, n, n, cx - 3, n - 34, cx + 3, n - 28, rgb(0x555555));
}

const game_def_t game_maze3d = {
    .name = "DARK MAZE 3D", .hint = "DRAG TO TURN - TAP TO FIRE\nFIND THE GREEN EXIT", .key = "maze3d", .unit = "",
    .wants_release = true,
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
