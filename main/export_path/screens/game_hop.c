// SKY HOP — touch to hop and fly the yellow bird through the gaps between the green pipes; one point per gap and
// the pipes speed up a little each time. The whole screen scrolls, so every frame is redrawn: the sunset sky row by
// row (colours worked out once), the city skyline, the pipes and the bird (tilted with its speed, painted per pixel).
// Same rules, sizes and colours as the browser copy (web/game-arcade.js, skyHop).

#include <math.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"

#define BX 130                 // the bird's x
#define R 11                   // the bird's radius
#define PW 46                  // pipe width
#define GAP 112                // opening between the top and bottom pipe
#define GRAV 1000.0f
#define HOP (-300.0f)
#define MAX_PIPES 6
#define CITY_N ((GC_W + 21) / 22)

typedef enum { H_READY, H_RUN, H_OVER } hop_state_t;
typedef struct { float x, g; bool done, alive; } pipe_t;

static hop_state_t s_state;
static pipe_t s_pipe[MAX_PIPES];
static float s_y, s_vy, s_speed, s_t, s_city[CITY_N];
static int s_score, s_shown_score, s_last;           // s_last: newest pipe (-1 = none)
static lv_color_t s_sky[GC_H];
static lv_obj_t *s_score_l, *s_title, *s_hint;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static int px(float v) { return (int)floorf(v + 0.5f); }
static void rectf(float x, float y, float w, float h, lv_color_t c) { gc_rect(px(x), px(y), px(x + w), px(y + h), c); }

static void sky_rows(void)                             // the gradient #1b1046 -> #b2366f (60 %) -> #ff9a3d
{
    for (int y = 0; y < GC_H; y++) {
        float t = (y + 0.5f) / GC_H;
        float r, g, b;
        if (t < 0.6f) { float k = t / 0.6f; r = 0x1b + (0xb2 - 0x1b) * k; g = 0x10 + (0x36 - 0x10) * k; b = 0x46 + (0x6f - 0x46) * k; }
        else { float k = (t - 0.6f) / 0.4f; r = 0xb2 + (0xff - 0xb2) * k; g = 0x36 + (0x9a - 0x36) * k; b = 0x6f + (0x3d - 0x6f) * k; }
        s_sky[y] = lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
}

static void overlay(const char *title, const char *hint)
{
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_hint, hint);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void reset(void)
{
    s_y = GC_H / 2; s_vy = 0; s_speed = 120; s_score = 0; s_t = 0; s_last = -1;
    memset(s_pipe, 0, sizeof(s_pipe));
}

static void begin(void)
{
    for (int i = 0; i < CITY_N; i++) s_city[i] = 40 + gc_rand() * 60;
    sky_rows();
    s_score_l = gc_label(&ui_font_FontTypoderSize28, 0xFFFFFF, LV_ALIGN_CENTER, 0, 46 - GC_H / 2);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -24);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 22);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 10, 0);
    reset();
    s_state = H_READY;
    s_shown_score = -1;
    overlay("SKY HOP", "TOUCH TO HOP\nTOUCH TO START");
}

static void game_over(void)
{
    s_state = H_OVER;
    bool rec = gc_record(s_score);
    long best = (long)gc_best();
    lv_label_set_text(s_title, rec ? "NEW RECORD!" : "GAME OVER");
    if (best) lv_label_set_text_fmt(s_hint, "SCORE %d   BEST %ld\nTAP TO PLAY AGAIN", s_score, best);
    else lv_label_set_text_fmt(s_hint, "SCORE %d\nTAP TO PLAY AGAIN", s_score);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void spawn(void)
{
    for (int i = 0; i < MAX_PIPES; i++) {
        if (s_pipe[i].alive) continue;
        s_pipe[i] = (pipe_t){.x = GC_W + 10, .g = 110 + gc_rand() * 140, .alive = true};
        s_last = i;
        return;
    }
}

static void draw_bird(void)
{
    // the browser draws it rotated by the tilt: body disc, flapping wing ellipse, eye, beak; here each pixel near
    // the bird is turned back into the bird's own frame (u forward, v down) and the topmost part wins
    float tilt = s_vy / 500.0f;
    if (tilt < -0.5f) tilt = -0.5f;
    if (tilt > 0.9f) tilt = 0.9f;
    float co = cosf(tilt), si = sinf(tilt), wy = 3 + sinf(s_t * 20) * 2;
    const lv_color_t body = rgb(0xFFDD00), wing = rgb(0xFFB000), white = rgb(0xFFFFFF), pupil = rgb(0x111111), beak = rgb(0xFF6A00);
    int y0 = px(s_y) - 18, y1 = px(s_y) + 18;
    for (int y = y0; y <= y1; y++) {
        if (y < 0 || y >= GC_H) continue;
        for (int x = BX - 18; x <= BX + 18; x++) {
            float dx = x + 0.5f - BX, dy = y + 0.5f - s_y;
            float u = dx * co + dy * si, v = -dx * si + dy * co;
            const lv_color_t *c = NULL;
            float bu = u - 9;
            if (bu >= 0 && v >= bu * 2.0f / 7.0f && v <= 5 - bu * 3.0f / 7.0f) c = &beak;
            else if ((u - 6) * (u - 6) + (v + 4) * (v + 4) <= 1.6f * 1.6f) c = &pupil;
            else if ((u - 5) * (u - 5) + (v + 4) * (v + 4) <= 3.4f * 3.4f) c = &white;
            else if ((u + 3) * (u + 3) / 49.0f + (v - wy) * (v - wy) / 16.0f <= 1.0f) c = &wing;
            else if (u * u + v * v <= R * R) c = &body;
            if (c) gc_buf[y * GC_W + x] = *c;
        }
    }
}

static void frame(float dt)
{
    s_t += dt;
    if (s_state == H_RUN) {
        s_vy += GRAV * dt;
        s_y += s_vy * dt;
        if (s_last < 0 || !s_pipe[s_last].alive || s_pipe[s_last].x < GC_W - 170) spawn();
        for (int i = 0; i < MAX_PIPES; i++) {
            pipe_t *p = &s_pipe[i];
            if (!p->alive) continue;
            p->x -= s_speed * dt;
            if (!p->done && p->x + PW < BX - R) { p->done = true; s_score++; s_speed = s_speed + 2 > 200 ? 200 : s_speed + 2; }
        }
        for (int i = 0; i < MAX_PIPES; i++) if (s_pipe[i].alive && s_pipe[i].x <= -PW - 4) s_pipe[i].alive = false;
        bool dead = s_y < 24 || s_y > 336;
        for (int i = 0; i < MAX_PIPES && !dead; i++) {
            pipe_t *p = &s_pipe[i];
            if (p->alive && BX + R > p->x && BX - R < p->x + PW && (s_y - R < p->g - GAP / 2 || s_y + R > p->g + GAP / 2)) dead = true;
        }
        if (dead) game_over();
    } else if (s_state == H_READY) {
        s_y = GC_H / 2 + sinf(s_t * 3) * 8;
    }

    for (int y = 0; y < GC_H; y++) gc_hline(y, 0, GC_W, s_sky[y]);
    for (int i = 0; i < CITY_N; i++) rectf(i * 22, GC_H - s_city[i], 20, s_city[i], rgb(0x120A2A));
    for (int i = 0; i < MAX_PIPES; i++) {
        pipe_t *p = &s_pipe[i];
        if (!p->alive) continue;
        float top = p->g - GAP / 2, bot = p->g + GAP / 2;
        rectf(p->x, 0, PW, top, rgb(0x2FE06B));
        rectf(p->x + 6, 0, 6, top, rgb(0x8CFF9E));
        rectf(p->x, bot, PW, GC_H - bot, rgb(0x2FE06B));
        rectf(p->x + 6, bot, 6, GC_H - bot, rgb(0x8CFF9E));
        rectf(p->x - 4, top - 14, PW + 8, 14, rgb(0x22C55E));
        rectf(p->x - 4, bot, PW + 8, 14, rgb(0x22C55E));
    }
    draw_bird();
    gc_dirty_all();
    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
}

static void input(gc_input_t in, lv_point_t at)
{
    (void)at;
    if (in != GC_PRESS) return;
    if (s_state != H_RUN) {
        if (s_state == H_OVER) reset();
        s_state = H_RUN;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    }
    s_vy = HOP;
}

static void icon(lv_color_t *b, int n)
{
    for (int y = 0; y < n; y++) {                                // the sunset
        float t = (float)y / n;
        lv_color_t c = t < 0.6f ? lv_color_make((uint8_t)(0x1b + 0x97 * t / 0.6f), (uint8_t)(0x10 + 0x26 * t / 0.6f), (uint8_t)(0x46 + 0x29 * t / 0.6f))
                                : lv_color_make((uint8_t)(0xb2 + 0x4d * (t - 0.6f) / 0.4f), (uint8_t)(0x36 + 0x64 * (t - 0.6f) / 0.4f), (uint8_t)(0x6f - 0x32 * (t - 0.6f) / 0.4f));
        for (int x = 0; x < n; x++) b[y * n + x] = c;
    }
    for (int x = 0; x < n; x += 14) gc_rect_in(b, n, n, x, n - 18 - (x * 7) % 22, x + 12, n, rgb(0x120A2A));
    int px0 = n - 46, gy = n / 2 + 6;                            // a pipe pair with its gap
    gc_rect_in(b, n, n, px0, 0, px0 + 28, gy - 30, rgb(0x2FE06B));
    gc_rect_in(b, n, n, px0 + 4, 0, px0 + 8, gy - 30, rgb(0x8CFF9E));
    gc_rect_in(b, n, n, px0 - 3, gy - 38, px0 + 31, gy - 30, rgb(0x22C55E));
    gc_rect_in(b, n, n, px0, gy + 30, px0 + 28, n, rgb(0x2FE06B));
    gc_rect_in(b, n, n, px0 + 4, gy + 30, px0 + 8, n, rgb(0x8CFF9E));
    gc_rect_in(b, n, n, px0 - 3, gy + 30, px0 + 31, gy + 38, rgb(0x22C55E));
    int bx = n / 2 - 18, by = n / 2;                             // the bird
    gc_disc_in(b, n, n, bx, by, 16, rgb(0xFFDD00));
    gc_disc_in(b, n, n, bx - 5, by + 5, 7, rgb(0xFFB000));
    gc_disc_in(b, n, n, bx + 7, by - 6, 5, rgb(0xFFFFFF));
    gc_disc_in(b, n, n, bx + 9, by - 6, 2, rgb(0x111111));
    for (int k = 0; k < 8; k++) gc_rect_in(b, n, n, bx + 13, by - 1 + k / 2, bx + 24 - k, by + k / 2, rgb(0xFF6A00));
}

const game_def_t game_hop = {
    .name = "SKY HOP", .hint = "TOUCH TO HOP\nFLY THROUGH THE GAPS", .key = "hop", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
