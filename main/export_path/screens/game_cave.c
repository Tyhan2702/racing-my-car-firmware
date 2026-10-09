// CAVE FLYER — the yellow arrow flies on its own through a winding cave: hold your finger anywhere to climb, let
// go to drop. Touching the orange-edged walls or a red block ends the run; one point per 8 px flown. The cave gets
// narrower and the flight faster the further you go. The cave scrolls, so every frame is redrawn: 47 columns of
// 8 px kept in a ring (a new one comes in on the right as one leaves on the left), the fading trail, the arrow
// turned with its climb or fall (a filled polygon) and its flame while you hold. Same rules, sizes and colours as
// the browser copy (web/game-arcade3.js, caveFlyer).

#include "esp_attr.h"
#include <math.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"

#define SX 110                 // the ship's x
#define SEG 8                  // column width
#define NCOL (GC_W / SEG + 2)
#define TRAIL 14

typedef enum { C_READY, C_RUN, C_OVER } cave_state_t;
typedef struct { float mid, gap, block; bool has_block; } col_t;

static cave_state_t s_state;
static EXT_RAM_BSS_ATTR col_t s_col[NCOL];
static int s_head;                                // s_col[s_head] is the leftmost column
static float s_y, s_vy, s_scroll, s_speed, s_t;
static bool s_hold;
static int s_score, s_shown_score, s_n_trail;
static EXT_RAM_BSS_ATTR float s_trail[TRAIL][2];
static lv_obj_t *s_score_l, *s_title, *s_hint;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static col_t *col(int i) { return &s_col[(s_head + i) % NCOL]; }
static void span(int y, float xa, float xb, lv_color_t c) { gc_hline(y, (int)ceilf(xa - 0.5f), (int)floorf(xb - 0.5f) + 1, c); }
static void rectf(float x, float y, float w, float h, lv_color_t c)   // canvas fillRect: the pixel centres inside
{
    int y0 = (int)floorf(y + 0.5f), y1 = (int)floorf(y + h + 0.5f);
    if (y0 < 0) y0 = 0;
    if (y1 > GC_H) y1 = GC_H;
    for (int py = y0; py < y1; py++) span(py, x, x + w, c);
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
static void disc_blend(float cx, float cy, float r, lv_color_t c, uint8_t a)   // a translucent disc over what is there
{
    for (int y = (int)floorf(cy - r); y <= (int)ceilf(cy + r); y++) {
        float dy = y + 0.5f - cy;
        if (y < 0 || y >= GC_H || dy * dy > r * r) continue;
        float h = sqrtf(r * r - dy * dy);
        for (int x = (int)ceilf(cx - h - 0.5f); x < (int)floorf(cx + h - 0.5f) + 1; x++)
            if (x >= 0 && x < GC_W) gc_buf[y * GC_W + x] = lv_color_mix(c, gc_buf[y * GC_W + x], a);
    }
}
static void poly(const float *xs, const float *ys, int n, lv_color_t c)   // even-odd, at most 8 corners
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

static col_t new_col(const col_t *prev)
{
    col_t c;
    c.gap = fmaxf(96, 170 - s_score * 0.35f);
    c.mid = clampf(prev->mid + (gc_rand() - 0.5f) * 24, 110, 250);
    c.has_block = gc_rand() < 0.04f + fminf(0.06f, s_score * 0.0004f);
    c.block = c.has_block ? c.mid + (gc_rand() - 0.5f) * c.gap * 0.5f : 0;
    return c;
}

static void reset(void)
{
    s_y = 180; s_vy = 0; s_hold = false; s_scroll = 0; s_speed = 110; s_score = 0; s_n_trail = 0; s_head = 0;
    col_t p = {180, 170, 0, false};
    for (int i = 0; i < NCOL; i++) {
        s_col[i] = p;
        p = new_col(&p);
        p.has_block = false;
    }
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
    s_state = C_OVER;
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
    s_score_l = gc_label(&ui_font_FontTypoderSize24, 0xFFFFFF, LV_ALIGN_CENTER, 0, 40 - GC_H / 2);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -30);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 8, 0);
    s_t = 0;
    reset();
    s_state = C_READY;
    s_shown_score = -1;
    overlay("CAVE FLYER", "HOLD TO CLIMB\nLET GO TO DROP\nTOUCH TO START");
}

static void step(float dt)
{
    s_vy += (s_hold ? -520 : 420) * dt;
    s_vy = clampf(s_vy, -240, 260);
    s_y += s_vy * dt;
    s_scroll += s_speed * dt;
    s_speed = fminf(220, s_speed + 2 * dt);
    while (s_scroll >= SEG) {
        s_scroll -= SEG;
        col_t last = *col(NCOL - 1);
        *col(0) = new_col(&last);                  // the leftmost slot becomes the new rightmost column
        s_head = (s_head + 1) % NCOL;
        s_score++;
    }
    if (s_n_trail == TRAIL) { memmove(s_trail[0], s_trail[1], sizeof(s_trail[0]) * (TRAIL - 1)); s_n_trail--; }
    s_trail[s_n_trail][0] = SX; s_trail[s_n_trail][1] = s_y; s_n_trail++;
    for (int i = 0; i < s_n_trail; i++) s_trail[i][0] -= s_speed * dt;
    int ci = (int)floorf((SX + s_scroll) / SEG);
    if (ci > NCOL - 1) ci = NCOL - 1;
    const col_t *c = col(ci);
    if (s_y - 6 < c->mid - c->gap / 2 || s_y + 6 > c->mid + c->gap / 2 || (c->has_block && fabsf(s_y - c->block) < 14)) game_over();
}

static void frame(float dt)
{
    s_t += dt;
    if (s_state == C_RUN) step(dt);

    gc_fill(rgb(0x0D0716));
    for (int i = 0; i < NCOL; i++) {
        const col_t *c = col(i);
        float x = i * SEG - s_scroll, top = c->mid - c->gap / 2, bot = c->mid + c->gap / 2;
        rectf(x, 0, SEG + 1, top, rgb(0x5A2A1A));
        rectf(x, bot, SEG + 1, GC_H, rgb(0x5A2A1A));
        rectf(x, top - 4, SEG + 1, 4, rgb(0xFF8A00));
        rectf(x, bot, SEG + 1, 4, rgb(0xFF8A00));
        if (c->has_block) rectf(x - 4, c->block - 14, SEG + 8, 28, rgb(0xFF3030));
    }
    for (int i = 0; i < s_n_trail; i++)
        disc_blend(s_trail[i][0], s_trail[i][1], 1 + i * 0.25f, rgb(0xFFDD00), (uint8_t)(i / 28.0f * 255 + 0.5f));
    {                                              // the arrow (14,0) (-10,-8) (-6,0) (-10,8), turned with the climb
        static const float AX[4] = {14, -10, -6, -10}, AY[4] = {0, -8, 0, 8};
        float a = clampf(s_vy / 500, -0.5f, 0.5f), co = cosf(a), si = sinf(a), xs[4], ys[4];
        for (int i = 0; i < 4; i++) { xs[i] = SX + AX[i] * co - AY[i] * si; ys[i] = s_y + AX[i] * si + AY[i] * co; }
        poly(xs, ys, 4, rgb(0xFFDD00));
        if (s_hold && s_state == C_RUN) discf(SX - 12 * co, s_y - 12 * si, 4 + gc_rand() * 2, rgb(0xFF8A00));
    }
    gc_dirty_all();
    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
}

static void input(gc_input_t in, lv_point_t at)
{
    (void)at;
    if (s_state != C_RUN) {
        if (in != GC_PRESS) return;
        if (s_state == C_OVER) reset();
        s_state = C_RUN;
        s_hold = true;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (in == GC_PRESS) s_hold = true;
    else if (in == GC_RELEASE) s_hold = false;
}

static void icon(lv_color_t *b, int n)
{
    for (int i = 0; i < n * n; i++) b[i] = rgb(0x0D0716);
    for (int x = 0; x < n; x += 6) {                             // a winding cave
        float m = n / 2 + sinf(x * 0.05f) * 14, g = 34;
        int top = (int)(m - g), bot = (int)(m + g);
        gc_rect_in(b, n, n, x, 0, x + 6, top, rgb(0x5A2A1A));
        gc_rect_in(b, n, n, x, top - 4, x + 6, top, rgb(0xFF8A00));
        gc_rect_in(b, n, n, x, bot, x + 6, n, rgb(0x5A2A1A));
        gc_rect_in(b, n, n, x, bot, x + 6, bot + 4, rgb(0xFF8A00));
    }
    gc_rect_in(b, n, n, 96, 52, 112, 76, rgb(0xFF3030));          // a red block ahead
    for (int k = 0; k < 6; k++) gc_disc_in(b, n, n, 18 + k * 5, 74 - k, 1 + k / 3, rgb(0x6A5A20 + k * 0x101000));   // trail
    gc_disc_in(b, n, n, 40, 68, 5, rgb(0xFF8A00));                 // flame, then the arrow pointing a little up
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            float dx = x + 0.5f - 56, dy = y + 0.5f - 66;
            float u = (dx * 0.95f + dy * -0.31f) / 1.5f, v = (-dx * -0.31f + dy * 0.95f) / 1.5f;   // u forward, v across
            bool in = u <= 14 && u >= -10 && fabsf(v) <= 8 * (14 - u) / 24 && !(u < -6 && fabsf(v) < (-6 - u) * 8 / 4.0f);
            if (in) b[y * n + x] = rgb(0xFFDD00);
        }
}

const game_def_t game_cave = {
    .name = "CAVE FLYER", .hint = "HOLD TO CLIMB\nLET GO TO DROP", .key = "cave", .unit = "", .wants_release = true,
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
