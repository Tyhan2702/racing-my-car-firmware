// MOON LANDER — hold a finger on the screen to fire the engine: it pushes the ship away from the finger (finger
// below = up, finger left = right). Land on the blinking yellow pad slower than 42 down and 26 sideways: 100
// points plus 2 per unit of fuel left plus 50 a level, then a new landscape with a smaller pad and more gravity.
// Fuel is short; three lives, a crash costs one. Everything is redrawn every frame. Same rules, sizes and colours
// as the browser copy (web/game-arcade3.js, moonLander).

#include "esp_attr.h"
#include <math.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"

#define SR 9                   // the ship's radius
#define NSEG 24                // the ground: 25 points, 15 px apart
#define PI_F 3.14159265f
#define TAU_F (2 * PI_F)

typedef enum { L_READY, L_RUN, L_OVER } lander_state_t;
typedef enum { R_NONE, R_LANDED, R_CRASH } result_t;

static lander_state_t s_state;
static EXT_RAM_BSS_ATTR float s_gx[NSEG + 1], s_gy[NSEG + 1], s_pad0, s_pad1, s_pady;
static EXT_RAM_BSS_ATTR int16_t s_gtop[GC_W];   // the first ground pixel row of each column
static float s_x, s_y, s_vx, s_vy, s_fuel, s_rt, s_t, s_tx, s_ty;
static bool s_thrust;
static int s_level, s_score, s_left;
static result_t s_result;
static lv_obj_t *s_score_l, *s_v_l, *s_fuel_l, *s_res_l, *s_title, *s_hint;
static int s_shown_score, s_shown_v, s_shown_safe, s_shown_res;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static int ipx(float v) { return (int)floorf(clampf(v, -10000, 10000) + 0.5f); }

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

static float ground_y(float x)
{
    for (int i = 0; i < NSEG; i++)
        if (x >= s_gx[i] && x <= s_gx[i + 1]) return s_gy[i] + (s_gy[i + 1] - s_gy[i]) * (x - s_gx[i]) / (s_gx[i + 1] - s_gx[i]);
    return 330;
}

static void build(void)
{
    int pi = 4 + (int)(gc_rand() * (NSEG - 10)), pw = 4 - s_level / 2;
    if (pi > NSEG - 7) pi = NSEG - 7;
    if (pw < 2) pw = 2;
    float y = 290;
    for (int i = 0; i <= NSEG; i++) {
        s_gx[i] = i * (float)GC_W / NSEG;
        if (i > pi && i <= pi + pw) s_gy[i] = s_gy[i - 1];
        else { y = clampf(y + (gc_rand() - 0.5f) * 50, 220, 320); s_gy[i] = y; }
    }
    s_pad0 = s_gx[pi]; s_pad1 = s_gx[pi + pw]; s_pady = s_gy[pi];
    for (int i = pi; i <= pi + pw; i++) s_gy[i] = s_pady;
    for (int x = 0; x < GC_W; x++) {           // the filled ground under the line, sampled at pixel centres
        int r = (int)ceilf(ground_y(x + 0.5f) - 0.5f);
        s_gtop[x] = (int16_t)(r < 0 ? 0 : r > GC_H ? GC_H : r);
    }
    s_x = 60 + gc_rand() * 240; s_y = 70; s_vx = (gc_rand() - 0.5f) * 50; s_vy = 0;
    s_fuel = 100; s_thrust = false; s_result = R_NONE;
}

static void reset(void) { s_level = 0; s_score = 0; s_left = 3; build(); }

static void overlay(const char *title, const char *hint)
{
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_hint, hint);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void game_over(void)
{
    s_state = L_OVER;
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
    float g = 22 + s_level * 3;
    if (s_result != R_NONE) {
        s_rt -= dt;
        if (s_rt <= 0) {
            if (s_result == R_LANDED) { s_level++; build(); }
            else if (s_left <= 0) game_over();
            else build();
        }
        return;
    }
    float ax = 0, ay = g;
    if (s_thrust && s_fuel > 0) {
        float dx = s_x - s_tx, dy = s_y - s_ty, d = hypotf(dx, dy);
        if (d == 0) d = 1;
        ax += dx / d * 62; ay += dy / d * 62;
        s_fuel = fmaxf(0, s_fuel - dt * 9);
    }
    s_vx += ax * dt; s_vy += ay * dt; s_x += s_vx * dt; s_y += s_vy * dt;
    if (s_x < 20 || s_x > GC_W - 20) { s_vx = -s_vx * 0.5f; s_x = clampf(s_x, 20, GC_W - 20); }
    s_y = fmaxf(s_y, -1e6f);
    if (s_y + SR >= ground_y(s_x)) {
        bool ok = s_x - SR >= s_pad0 && s_x + SR <= s_pad1 && s_vy < 42 && fabsf(s_vx) < 26;
        s_y = ground_y(s_x) - SR;
        if (ok) { s_result = R_LANDED; s_score += 100 + (int)roundf(s_fuel) * 2 + s_level * 50; }
        else { s_result = R_CRASH; s_left--; }
        s_rt = 1.6f; s_thrust = false;
    }
}

static void frame(float dt)
{
    s_t += dt;
    if (s_state == L_RUN) step(dt);

    gc_fill(rgb(0x03030A));
    for (int i = 0; i < 60; i++) { int x = (i * 97) % GC_W, y = (i * 53) % 200; gc_rect(x, y, x + 1, y + 1, rgb(i % 4 ? 0x666666 : 0xCCCCCC)); }
    discf(290, 70, 22, rgb(0x2A5AA0));         // the earth
    discf(284, 64, 7, rgb(0x3F7AD0));
    lv_color_t rock = rgb(0x4A4A52);
    for (int x = 0; x < GC_W; x++) gc_rect(x, s_gtop[x], x + 1, GC_H, rock);
    rectf(s_pad0, s_pady - 2, s_pad1 - s_pad0, 4, rgb(((int)floorf(s_t * 3)) & 1 ? 0xFFDD00 : 0xFFB000));
    float x = s_x, y = s_y;
    if (s_result == R_CRASH) {
        for (int i = 0; i < 10; i++) {
            float a = i * TAU_F / 10, r = (1.6f - s_rt) * 40;
            discf(x + cosf(a) * r, y + sinf(a) * r, 3, rgb(i & 1 ? 0xFF8A00 : 0xFF3030));
        }
    } else {
        if (s_thrust && s_fuel > 0) {          // the flame, out toward the finger
            float dx = s_tx - x, dy = s_ty - y, d = hypotf(dx, dy);
            if (d == 0) d = 1;
            for (int k = 0; k < 3; k++) {
                float ox = 12 + k * 6 + gc_rand() * 4, oy = 12 + k * 6 + gc_rand() * 4;
                discf(x + dx / d * ox, y + dy / d * oy, 5 - k, rgb(k ? 0xFF8A00 : 0xFFF6B0));
            }
        }
        discf(x, y, SR, rgb(0xE8E8E8));
        discf(x, y - 2, 4, rgb(0x2F9BFF));
        rectf(x - SR - 3, y + 4, 4, 8, rgb(0xFFDD00));
        rectf(x + SR - 1, y + 4, 4, 8, rgb(0xFFDD00));
    }
    gc_rect(GC_W / 2 - 50, 46, GC_W / 2 + 50, 53, rgb(0x222222));   // the fuel gauge
    rectf(GC_W / 2 - 50, 46, s_fuel, 7, rgb(s_fuel < 25 ? 0xFF3030 : 0x2FE06B));
    for (int i = 0; i < s_left; i++) discf(GC_W / 2 - (s_left - 1) * 9 + i * 18, 342, 5, rgb(0xFFDD00));
    gc_dirty_all();

    int v = (int)roundf(clampf(hypotf(s_vx, s_vy), 0, 1e6f)), safe = s_vy < 42 && fabsf(s_vx) < 26, res = (int)s_result;
    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
    if (v != s_shown_v) { lv_label_set_text_fmt(s_v_l, "V %d", v); s_shown_v = v; }
    if (safe != s_shown_safe) { lv_obj_set_style_text_color(s_v_l, rgb(safe ? 0x2FE06B : 0xFF3030), 0); s_shown_safe = safe; }
    if (res != s_shown_res) {
        if (s_result == R_NONE) lv_obj_add_flag(s_res_l, LV_OBJ_FLAG_HIDDEN);
        else {
            lv_label_set_text(s_res_l, s_result == R_LANDED ? "NICE LANDING!" : "CRASHED");
            lv_obj_set_style_text_color(s_res_l, rgb(s_result == R_LANDED ? 0x2FE06B : 0xFF3030), 0);
            lv_obj_clear_flag(s_res_l, LV_OBJ_FLAG_HIDDEN);
        }
        s_shown_res = res;
    }
}

static void begin(void)
{
    s_score_l = gc_label(&ui_font_FontTypoderSize20, 0xFFFFFF, LV_ALIGN_CENTER, 0, 28 - GC_H / 2);
    s_fuel_l = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, -84, 50 - GC_H / 2);   // left of the bar
    lv_label_set_text(s_fuel_l, "FUEL");
    s_v_l = gc_label(&ui_font_FontTypoderSize16, 0x2FE06B, LV_ALIGN_CENTER, 0, 68 - GC_H / 2);
    s_res_l = gc_label(&ui_font_FontTypoderSize20, 0x2FE06B, LV_ALIGN_CENTER, 0, 140 - GC_H / 2);
    lv_obj_add_flag(s_res_l, LV_OBJ_FLAG_HIDDEN);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -36);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 8, 0);
    s_t = 0;
    reset();
    s_state = L_READY;
    s_shown_score = s_shown_v = s_shown_safe = -1;
    s_shown_res = (int)R_NONE;
    overlay("MOON LANDER", "HOLD: ENGINE PUSHES\nAWAY FROM FINGER\nLAND SOFTLY ON THE PAD\nTOUCH TO START");
}

static void input(gc_input_t in, lv_point_t at)
{
    if (s_state != L_RUN) {
        if (in != GC_PRESS) return;
        if (s_state == L_OVER) reset();
        s_state = L_RUN;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (in == GC_PRESS || in == GC_DRAG) { s_thrust = true; s_tx = at.x; s_ty = at.y; }
    else if (in == GC_RELEASE) s_thrust = false;
}

static void icon(lv_color_t *b, int n)         // the ship over the pad, engine on, the ground and the earth
{
    for (int i = 0; i < n * n; i++) b[i] = rgb(0x03030A);
    for (int i = 0; i < 18; i++) { int x = (i * 97) % n, y = (i * 53) % (n / 2); gc_rect_in(b, n, n, x, y, x + 2, y + 2, rgb(i % 4 ? 0x666666 : 0xCCCCCC)); }
    gc_disc_in(b, n, n, n - 26, 24, 16, rgb(0x2A5AA0));
    gc_disc_in(b, n, n, n - 30, 20, 5, rgb(0x3F7AD0));
    static const int GY[7] = {96, 88, 104, 104, 104, 92, 100};
    int seg = (n + 5) / 6;
    for (int x = 0; x < n; x++) {
        int i = x / seg;
        if (i > 5) i = 5;
        float f = (float)(x - i * seg) / seg;
        int top = (int)(GY[i] + (GY[i + 1] - GY[i]) * f) * n / 132;
        gc_rect_in(b, n, n, x, top, x + 1, n, rgb(0x4A4A52));
    }
    int px0 = 2 * seg, px1 = 4 * seg, py = 104 * n / 132;
    gc_rect_in(b, n, n, px0, py - 2, px1, py + 2, rgb(0xFFDD00));
    int cx = (px0 + px1) / 2, cy = 60 * n / 132, r = 14;
    gc_disc_in(b, n, n, cx, cy + r + 14, 7, rgb(0xFF8A00));   // the flame under it
    gc_disc_in(b, n, n, cx, cy + r + 6, 6, rgb(0xFFF6B0));
    gc_disc_in(b, n, n, cx, cy, r, rgb(0xE8E8E8));
    gc_disc_in(b, n, n, cx, cy - 3, 6, rgb(0x2F9BFF));
    gc_rect_in(b, n, n, cx - r - 4, cy + 6, cx - r + 2, cy + 18, rgb(0xFFDD00));
    gc_rect_in(b, n, n, cx + r - 2, cy + 6, cx + r + 4, cy + 18, rgb(0xFFDD00));
}

const game_def_t game_lander = {
    .name = "MOON LANDER", .hint = "HOLD: ENGINE PUSHES AWAY\nLAND SOFTLY ON THE PAD", .key = "lander", .unit = "",
    .wants_release = true,
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
