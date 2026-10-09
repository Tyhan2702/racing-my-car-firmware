// ROAD CROSS — get the yellow runner across seven lanes of traffic to the chequered line: touch above / beside /
// below the runner (or swipe up / left / right) to step that way. +10 for each new row reached, +50 (+10 per level)
// for each crossing, and every crossing makes the traffic faster; three lives. Only the playfield (and the lives
// under it) is redrawn each frame. Same rules, sizes and colours as the browser copy (web/game-arcade.js, roadCross).

#include "esp_attr.h"
#include <math.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"

#define COLS 9
#define ROWS 10
#define C 26                                   // cell size
#define X0 ((GC_W - COLS * C) / 2)
#define Y0 50
#define SPAN (COLS * C)
#define MAX_CARS 24
#define AREA_Y1 345                            // the redrawn area: the playfield down to the lives

typedef enum { X_READY, X_RUN, X_OVER } road_state_t;
typedef struct { float x, len, s; int8_t row, d; uint32_t c; } car_t;
typedef struct { int8_t row, d; float s; int8_t size; } lane_t;    // direction, speed, size: 1 car / 2 truck

static const lane_t LANES[] = {{1, -1, 0.9f, 1}, {2, 1, 1.2f, 2}, {3, -1, 1.0f, 1}, {4, 1, 1.5f, 1},
                               {6, -1, 1.3f, 1}, {7, 1, 0.8f, 2}, {8, -1, 1.1f, 1}};
static const uint32_t CARC[5] = {0xFF3030, 0x2F9BFF, 0xFF3DF2, 0x2FE06B, 0xFF8A00};
static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};

static road_state_t s_state;
static EXT_RAM_BSS_ATTR car_t s_car[MAX_CARS];
static int s_ncar, s_px, s_py, s_top, s_level, s_score, s_left, s_shown_score, s_shown_level;
static float s_hop;
static int s_cx0, s_cy0, s_cx1, s_cy1;                 // clip box for the spans
static lv_obj_t *s_score_l, *s_level_l, *s_title, *s_hint;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static bool safe(int r) { return r == 0 || r == 5 || r == 9; }
static void clip(int x0, int y0, int x1, int y1) { s_cx0 = x0; s_cy0 = y0; s_cx1 = x1; s_cy1 = y1; }

// a filled horizontal span (the pixels whose centres lie in xa..xb), inside the clip box
static void span(int y, float xa, float xb, lv_color_t c)
{
    if (y < s_cy0 || y >= s_cy1) return;
    int a = (int)ceilf(xa - 0.5f), b = (int)floorf(xb - 0.5f) + 1;
    gc_hline(y, a < s_cx0 ? s_cx0 : a, b > s_cx1 ? s_cx1 : b, c);
}
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
static void rrect(float x, float y, float w, float h, float r, lv_color_t c)
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
static void shadow(float cx, float cy, float r)        // a 35 % black disc over what is there
{
    for (int y = (int)floorf(cy - r); y <= (int)ceilf(cy + r); y++) {
        float dy = y + 0.5f - cy;
        if (dy * dy > r * r || y < 0 || y >= GC_H) continue;
        float h = sqrtf(r * r - dy * dy);
        int a = (int)ceilf(cx - h - 0.5f), b = (int)floorf(cx + h - 0.5f) + 1;
        for (int x = a < 0 ? 0 : a; x < b && x < GC_W; x++)
            gc_buf[y * GC_W + x] = lv_color_mix(lv_color_black(), gc_buf[y * GC_W + x], 89);
    }
}

static void build(void)
{
    s_ncar = 0;
    for (unsigned l = 0; l < sizeof(LANES) / sizeof(LANES[0]); l++) {
        const lane_t *ln = &LANES[l];
        int n = ln->size > 1 ? 2 : 3;
        float len = ln->size > 1 ? 2.2f * C : 1.4f * C, gap = (float)SPAN / n;
        for (int i = 0; i < n; i++)
            s_car[s_ncar++] = (car_t){.row = ln->row, .x = i * gap + gc_rand() * gap * 0.4f, .len = len, .d = ln->d, .s = ln->s, .c = CARC[(ln->row + i) % 5]};
    }
}

static void home(void) { s_px = 4; s_py = 9; s_top = 9; s_hop = 0; }
static void reset(void) { s_level = 0; s_score = 0; s_left = 3; build(); home(); }

static void step(int d)
{
    int nx = s_px + DX[d], ny = s_py + DY[d];
    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) return;
    s_px = nx; s_py = ny; s_hop = 0.12f;
    if (s_py < s_top) { s_top = s_py; s_score += 10; }
    if (s_py == 0) { s_score += 50 + s_level * 10; s_level++; home(); }
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
    gc_fill(rgb(0x0B0D10));
    s_score_l = gc_label(&ui_font_FontTypoderSize20, 0xFFFFFF, LV_ALIGN_CENTER, 0, 28 - GC_H / 2);
    s_level_l = gc_label(&ui_font_FontTypoderSize16, 0x9A9A9A, LV_ALIGN_CENTER, 0, 318 - GC_H / 2);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -24);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 22);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 10, 0);
    reset();
    s_state = X_READY;
    s_shown_score = s_shown_level = -1;
    overlay("ROAD CROSS", "TOUCH WHERE TO STEP\nTAP TO START");
}

static void game_over(void)
{
    s_state = X_OVER;
    bool rec = gc_record(s_score);
    long best = (long)gc_best();
    lv_label_set_text(s_title, rec ? "NEW RECORD!" : "GAME OVER");
    if (best) lv_label_set_text_fmt(s_hint, "SCORE %d   BEST %ld\nTAP TO PLAY AGAIN", s_score, best);
    else lv_label_set_text_fmt(s_hint, "SCORE %d\nTAP TO PLAY AGAIN", s_score);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void frame(float dt)
{
    float k = 1 + s_level * 0.15f;
    for (int i = 0; i < s_ncar; i++) {                   // the traffic moves in every state
        car_t *c = &s_car[i];
        c->x += c->d * c->s * k * 40 * dt;
        if (c->x > SPAN) c->x -= SPAN + c->len;
        if (c->x < -c->len) c->x += SPAN + c->len;
    }
    if (s_hop > 0) s_hop -= dt;
    if (s_state == X_RUN && !safe(s_py)) {
        float mid = (s_px + 0.5f) * C;
        for (int i = 0; i < s_ncar; i++) {
            car_t *c = &s_car[i];
            if (c->row == s_py && mid > c->x + 3 && mid < c->x + c->len - 3) {
                s_left--;
                if (s_left <= 0) game_over(); else home();
                break;
            }
        }
    }

    clip(0, 0, GC_W, GC_H);
    gc_rect(X0, Y0, X0 + SPAN, AREA_Y1, rgb(0x0B0D10));
    for (int r = 0; r < ROWS; r++) {
        int y = Y0 + r * C;
        if (r == 0) {
            for (int x = 0; x < COLS * 2; x++)
                for (int q = 0; q < 2; q++)
                    gc_rect(X0 + x * C / 2, y + q * C / 2, X0 + (x + 1) * C / 2, y + (q + 1) * C / 2, ((x + q) & 1) ? rgb(0xFFFFFF) : rgb(0x111111));
        } else if (safe(r)) {
            gc_rect(X0, y, X0 + SPAN, y + C, rgb(0x3A3F47));
        } else {
            gc_rect(X0, y, X0 + SPAN, y + C, rgb(0x22252C));
            if (!safe(r + 1) && r + 1 < ROWS)
                for (int x = 0; x < SPAN; x += 26) gc_rect(X0 + x + 4, y + C - 1, X0 + x + 18, y + C + 1, rgb(0xFFDD00));
        }
    }
    clip(X0, Y0, X0 + SPAN, Y0 + ROWS * C);               // the cars are clipped to the road
    for (int i = 0; i < s_ncar; i++) {
        car_t *c = &s_car[i];
        float x = X0 + c->x, y = Y0 + c->row * C + 4;
        rrect(x, y, c->len, C - 8, 4, rgb(c->c));
        rectf(x + (c->d > 0 ? c->len - 10 : 4), y + 3, 6, C - 14, rgb(0xBFE9FF));
        rectf(x + (c->d > 0 ? c->len - 3 : 0), y + 2, 3, 4, rgb(0xFFF6B0));
    }
    clip(0, 0, GC_W, GC_H);
    float x = X0 + (s_px + 0.5f) * C, y = Y0 + (s_py + 0.5f) * C, lift = s_hop > 0 ? -5 : 0;
    discf(x, y + lift, 9, rgb(0xFFDD00));
    rectf(x - 6, y - 4 + lift, 12, 5, rgb(0x111111));
    shadow(x, y + 11, 5);
    for (int i = 0; i < s_left; i++) discf(GC_W / 2 - (s_left - 1) * 9 + i * 18, 336, 5, rgb(0xFFDD00));
    gc_dirty(X0, Y0, X0 + SPAN - 1, AREA_Y1 - 1);

    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
    if (s_level != s_shown_level) { lv_label_set_text_fmt(s_level_l, "LV %d", s_level + 1); s_shown_level = s_level; }
}

static void input(gc_input_t in, lv_point_t at)
{
    if (s_state != X_RUN) {
        if (in != GC_TAP) return;
        if (s_state == X_OVER) reset();
        s_state = X_RUN;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (in == GC_TAP) {                                  // the direction from the runner to the finger
        float dx = at.x - (X0 + (s_px + 0.5f) * C), dy = at.y - (Y0 + (s_py + 0.5f) * C);
        step(fabsf(dx) > fabsf(dy) ? (dx < 0 ? 3 : 1) : (dy < 0 ? 0 : 2));
    } else if (in == GC_SWIPE_UP) step(0);
    else if (in == GC_SWIPE_LEFT) step(3);
    else if (in == GC_SWIPE_RIGHT) step(1);
}

static void icon(lv_color_t *b, int n)
{
    for (int i = 0; i < n * n; i++) b[i] = rgb(0x0B0D10);
    int c = n / 8;                                               // 8 rows: finish, road, verge, road...
    for (int x = 0; x < n / 8; x++)
        for (int q = 0; q < 2; q++)
            gc_rect_in(b, n, n, x * 8, q * c / 2, x * 8 + 8, (q + 1) * c / 2, ((x + q) & 1) ? rgb(0xFFFFFF) : rgb(0x111111));
    for (int r = 1; r < 8; r++) {
        bool sf = r == 4 || r == 7;
        gc_rect_in(b, n, n, 0, r * c, n, r * c + c, sf ? rgb(0x3A3F47) : rgb(0x22252C));
        if (!sf && r != 3 && r != 6) for (int x = 4; x < n; x += 22) gc_rect_in(b, n, n, x, r * c + c - 1, x + 12, r * c + c + 1, rgb(0xFFDD00));
    }
    gc_rect_in(b, n, n, 10, c + 3, 46, 2 * c - 3, rgb(0xFF3030));       // traffic
    gc_rect_in(b, n, n, 14, c + 5, 20, 2 * c - 5, rgb(0xBFE9FF));
    gc_rect_in(b, n, n, 70, 2 * c + 3, 126, 3 * c - 3, rgb(0x2F9BFF));
    gc_rect_in(b, n, n, 116, 2 * c + 5, 122, 3 * c - 5, rgb(0xBFE9FF));
    gc_rect_in(b, n, n, 30, 5 * c + 3, 66, 6 * c - 3, rgb(0xFF3DF2));
    gc_rect_in(b, n, n, 90, 3 * c + 3, 126, 4 * c - 3, rgb(0x2FE06B));
    gc_disc_in(b, n, n, n / 2, 7 * c + c / 2, 8, rgb(0xFFDD00));         // the runner
    gc_rect_in(b, n, n, n / 2 - 5, 7 * c + c / 2 - 4, n / 2 + 6, 7 * c + c / 2 + 1, rgb(0x111111));
}

const game_def_t game_road = {
    .name = "ROAD CROSS", .hint = "TOUCH WHERE TO STEP\nCROSS THE TRAFFIC", .key = "road", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
