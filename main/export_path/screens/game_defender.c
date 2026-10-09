// STAR DEFENDER — drag the yellow ship left and right; it fires by itself. Three rows of aliens march side to side
// and step down at each edge, dropping red bombs; three lives, a new faster wave once a wave is cleared, and the game
// ends when the lives run out or the aliens reach the ship. The star field drifts down over the whole screen, so
// every frame is redrawn. Same rules, sizes and colours as the browser copy (web/game-arcade.js, starDefender).

#include "esp_attr.h"
#include <math.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"

#define L 70                   // the ship and the aliens stay between L and R
#define R 290
#define SY 298                 // the ship's y
#define COLS 6
#define ROWS 3
#define N_ALIENS (COLS * ROWS)
#define N_STARS 60
#define MAX_SHOTS 12
#define MAX_BOMBS 24

typedef enum { S_READY, S_RUN, S_OVER } def_state_t;
typedef struct { float x, y; uint8_t r; bool alive; } alien_t;
typedef struct { float x, y; bool alive; } shot_t;

static def_state_t s_state;
static EXT_RAM_BSS_ATTR alien_t s_alien[N_ALIENS];
static EXT_RAM_BSS_ATTR shot_t s_shot[MAX_SHOTS], s_bomb[MAX_BOMBS];
static EXT_RAM_BSS_ATTR float s_star[N_STARS][3];
static float s_sx, s_dir, s_fire, s_drop, s_hurt, s_t;
static int s_wave, s_score, s_left, s_shown_score;
static lv_obj_t *s_score_l, *s_title, *s_hint;

static const uint32_t ALIEN_C[ROWS] = {0xFF3DF2, 0x2FD6FF, 0x2FE06B};
static const int ALIEN_PTS[ROWS] = {30, 20, 10};

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static int px(float v) { return (int)floorf(v + 0.5f); }
static void rectf(float x, float y, float w, float h, lv_color_t c) { gc_rect(px(x), px(y), px(x + w), px(y + h), c); }

// a filled horizontal span: the pixels whose centres lie in xa..xb
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

static void build(void)
{
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) s_alien[r * COLS + c] = (alien_t){.x = 100 + c * 32, .y = 70 + r * 28, .r = (uint8_t)r, .alive = true};
    s_dir = 1;
}

static void reset(void)
{
    s_sx = GC_W / 2; s_wave = 0; s_score = 0; s_left = 3; s_fire = 0; s_drop = 1; s_hurt = 0;
    memset(s_shot, 0, sizeof(s_shot));
    memset(s_bomb, 0, sizeof(s_bomb));
    build();
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
    for (int i = 0; i < N_STARS; i++) { s_star[i][0] = gc_rand() * GC_W; s_star[i][1] = gc_rand() * GC_H; s_star[i][2] = gc_rand(); }
    s_score_l = gc_label(&ui_font_FontTypoderSize20, 0xFFFFFF, LV_ALIGN_CENTER, 0, 30 - GC_H / 2);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -30);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 22);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 10, 0);
    s_t = 0;
    reset();
    s_state = S_READY;
    s_shown_score = -1;
    overlay("STAR DEFENDER", "DRAG TO MOVE\nIT FIRES BY ITSELF\nTOUCH TO START");
}

static void game_over(void)
{
    s_state = S_OVER;
    bool rec = gc_record(s_score);
    long best = (long)gc_best();
    lv_label_set_text(s_title, rec ? "NEW RECORD!" : "GAME OVER");
    if (best) lv_label_set_text_fmt(s_hint, "SCORE %d   BEST %ld\nTAP TO PLAY AGAIN", s_score, best);
    else lv_label_set_text_fmt(s_hint, "SCORE %d\nTAP TO PLAY AGAIN", s_score);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void add(shot_t *arr, int n, float x, float y)
{
    for (int i = 0; i < n; i++) if (!arr[i].alive) { arr[i] = (shot_t){x, y, true}; return; }
}

static void step(float dt)
{
    bool live[N_ALIENS];                                  // the aliens alive at the start of the frame
    int n_live = 0;
    for (int i = 0; i < N_ALIENS; i++) { live[i] = s_alien[i].alive; n_live += live[i]; }

    s_fire -= dt;
    if (s_fire <= 0) { add(s_shot, MAX_SHOTS, s_sx, SY - 14); s_fire = 0.32f; }
    for (int i = 0; i < MAX_SHOTS; i++) {
        if (!s_shot[i].alive) continue;
        s_shot[i].y -= 420 * dt;
        if (s_shot[i].y <= 30) s_shot[i].alive = false;
    }

    float v = (28 + s_wave * 8 + (N_ALIENS - n_live) * 3) * s_dir * dt, mn = 1e9f, mx = -1e9f;
    for (int i = 0; i < N_ALIENS; i++) {
        if (!live[i]) continue;
        s_alien[i].x += v;
        mn = fminf(mn, s_alien[i].x); mx = fmaxf(mx, s_alien[i].x);
    }
    if (n_live && (mn < L + 12 || mx > R - 12)) {          // an edge: turn and step down
        s_dir = -s_dir;
        for (int i = 0; i < N_ALIENS; i++) if (live[i]) { s_alien[i].y += 10; s_alien[i].x += mn < L + 12 ? 2 : -2; }
    }
    for (int s = 0; s < MAX_SHOTS; s++) {
        shot_t *sh = &s_shot[s];
        if (!sh->alive) continue;
        for (int i = 0; i < N_ALIENS; i++) {
            alien_t *a = &s_alien[i];
            if (live[i] && a->alive && fabsf(sh->x - a->x) < 12 && fabsf(sh->y - a->y) < 9) {
                a->alive = false; sh->alive = false; s_score += ALIEN_PTS[a->r];
                break;
            }
        }
    }

    s_drop -= dt;
    if (s_drop <= 0 && n_live) {
        int k = (int)(gc_rand() * n_live);
        if (k >= n_live) k = n_live - 1;
        for (int i = 0; i < N_ALIENS; i++) if (live[i] && k-- == 0) { add(s_bomb, MAX_BOMBS, s_alien[i].x, s_alien[i].y + 8); break; }
        s_drop = fmaxf(0.45f, 1.1f - s_wave * 0.1f);
    }
    for (int i = 0; i < MAX_BOMBS; i++) if (s_bomb[i].alive) s_bomb[i].y += (170 + s_wave * 15) * dt;

    if (s_hurt > 0) s_hurt -= dt;
    else {
        bool hit = false;
        for (int i = 0; i < MAX_BOMBS; i++)
            if (s_bomb[i].alive && fabsf(s_bomb[i].x - s_sx) < 13 && fabsf(s_bomb[i].y - SY) < 10) hit = true;
        if (hit) {
            s_left--; s_hurt = 1.5f;
            memset(s_bomb, 0, sizeof(s_bomb));
            if (s_left <= 0) { game_over(); return; }
        }
    }
    for (int i = 0; i < MAX_BOMBS; i++) if (s_bomb[i].alive && s_bomb[i].y >= GC_H) s_bomb[i].alive = false;

    bool any = false, landed = false;
    for (int i = 0; i < N_ALIENS; i++) { any |= s_alien[i].alive; if (live[i] && s_alien[i].y > SY - 26) landed = true; }
    if (!any) { s_wave++; build(); }
    else if (landed) game_over();
}

static void draw_alien(const alien_t *a)
{
    lv_color_t c = rgb(ALIEN_C[a->r]);
    float x = a->x, y = a->y;
    int b = ((int)floorf(s_t * 3)) & 1;
    rrect(x - 10, y - 7, 20, 13, 6, c);
    rectf(x - 12, y + 2, 4, 6, c);
    rectf(x + 8, y + 2, 4, 6, c);
    rectf(x - 7 + b, y + 6, 3, 4, c);
    rectf(x + 4 - b, y + 6, 3, 4, c);
    discf(x - 4, y - 2, 2.4f, rgb(0x07070C));
    discf(x + 4, y - 2, 2.4f, rgb(0x07070C));
}

static void frame(float dt)
{
    s_t += dt;
    if (s_state == S_RUN) step(dt);

    gc_fill(rgb(0x05040D));
    for (int i = 0; i < N_STARS; i++) {
        float x = s_star[i][0], z = s_star[i][2], yy = fmodf(s_star[i][1] + s_t * 20 * (0.3f + z), GC_H);
        float sz = z > 0.7f ? 2 : 1;
        rectf(x, yy, sz, sz, z > 0.5f ? rgb(0xFFFFFF) : rgb(0x777777));
    }
    gc_rect(L, SY + 16, R, SY + 18, rgb(0x2A2A36));
    for (int i = 0; i < N_ALIENS; i++) if (s_alien[i].alive) draw_alien(&s_alien[i]);
    for (int i = 0; i < MAX_SHOTS; i++) if (s_shot[i].alive) rectf(s_shot[i].x - 1.5f, s_shot[i].y - 8, 3, 10, rgb(0xFFDD00));
    for (int i = 0; i < MAX_BOMBS; i++) if (s_bomb[i].alive) rectf(s_bomb[i].x - 2, s_bomb[i].y - 5, 4, 10, rgb(0xFF3030));
    if (!(s_hurt > 0 && (((int)floorf(s_t * 10)) & 1))) {   // the ship (blinks while hurt): a triangle of rows
        for (int y = SY - 14; y < SY + 10; y++) {
            float h = 15.0f * (y + 0.5f - (SY - 14)) / 24.0f;
            span(y, s_sx - h, s_sx + h, rgb(0xFFDD00));
        }
        rectf(s_sx - 3, SY - 4, 6, 8, rgb(0x111111));
        rectf(s_sx - 12, SY + 10, 6, 3, rgb(0xFF8A00));
        rectf(s_sx + 6, SY + 10, 6, 3, rgb(0xFF8A00));
    }
    for (int i = 0; i < s_left; i++) discf(GC_W / 2 - (s_left - 1) * 9 + i * 18, 336, 5, rgb(0xFFDD00));
    gc_dirty_all();
    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
}

static void input(gc_input_t in, lv_point_t at)
{
    if (s_state != S_RUN) {
        if (in != GC_PRESS) return;
        if (s_state == S_OVER) reset();
        s_state = S_RUN;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (in == GC_PRESS || in == GC_DRAG) {
        float x = at.x;
        s_sx = x < L + 15 ? L + 15 : x > R - 15 ? R - 15 : x;
    }
}

static void icon(lv_color_t *b, int n)
{
    for (int i = 0; i < n * n; i++) b[i] = rgb(0x05040D);
    for (int i = 0; i < 18; i++) {                               // a few stars
        int x = (i * 53 + 11) % n, y = (i * 37 + 7) % n;
        gc_rect_in(b, n, n, x, y, x + 2, y + 2, i & 1 ? rgb(0xFFFFFF) : rgb(0x777777));
    }
    for (int r = 0; r < 3; r++)                                  // three aliens in a row of each colour
        for (int c = 0; c < 3; c++) {
            int x = 30 + c * 36, y = 26 + r * 24;
            lv_color_t col = rgb(ALIEN_C[r]);
            gc_rect_in(b, n, n, x - 11, y - 7, x + 11, y + 6, col);
            gc_rect_in(b, n, n, x - 13, y + 2, x - 9, y + 9, col);
            gc_rect_in(b, n, n, x + 9, y + 2, x + 13, y + 9, col);
            gc_disc_in(b, n, n, x - 4, y - 2, 2, rgb(0x07070C));
            gc_disc_in(b, n, n, x + 4, y - 2, 2, rgb(0x07070C));
        }
    int sx = n / 2, sy = n - 20;
    gc_rect_in(b, n, n, sx - 1, 84, sx + 2, 96, rgb(0xFFDD00));  // a shot
    for (int y = sy - 20; y < sy + 10; y++) {                    // the ship
        int h = 20 * (y - (sy - 20)) / 30;
        gc_rect_in(b, n, n, sx - h, y, sx + h + 1, y + 1, rgb(0xFFDD00));
    }
    gc_rect_in(b, n, n, sx - 4, sy - 6, sx + 4, sy + 4, rgb(0x111111));
    gc_rect_in(b, n, n, sx - 16, sy + 10, sx - 8, sy + 14, rgb(0xFF8A00));
    gc_rect_in(b, n, n, sx + 8, sy + 10, sx + 16, sy + 14, rgb(0xFF8A00));
}

const game_def_t game_defender = {
    .name = "STAR DEFENDER", .hint = "DRAG TO MOVE\nIT FIRES BY ITSELF", .key = "defender", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
