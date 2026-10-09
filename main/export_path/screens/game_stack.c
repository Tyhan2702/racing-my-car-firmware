// STACK UP — a block slides across the top of the tower: tap to drop it. What overhangs the block below is cut off
// and falls away, a drop within 4 px keeps the full width (PERFECT!); missing the tower ends the run. Each block is a
// point and makes the next one faster (150 px/s, +7 a block, up to 360). Same rules, sizes and colours as the browser
// copy (web/game-arcade2.js, stackUp). The whole scene is drawn while the camera moves; otherwise only the sliding
// block's row and the falling piece are redrawn.

#include "esp_attr.h"
#include <math.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"

#define BH 18                                  // block height
#define BASE 300                               // y of the first block
#define W0 150                                 // its width
#define RING 128                               // the top blocks kept (fewer than 20 are ever on screen)
#define HUES 180                               // hue (i * 14 + 40) % 360 repeats every 180 blocks

typedef enum { S_READY, S_RUN, S_OVER } stack_state_t;

static stack_state_t s_state;
static EXT_RAM_BSS_ATTR float s_tx[RING], s_tw[RING];           // block i at [i % RING]
static int s_n, s_dir, s_score, s_shown_score, s_cut_i, s_drawn_cam;
static float s_cx, s_cw, s_speed, s_cam, s_perfect, s_cut_x, s_cut_w, s_cut_y, s_cut_drawn;
static bool s_cut, s_cut_shown, s_full, s_perfect_on;
static EXT_RAM_BSS_ATTR lv_color_t s_bg[GC_H], s_col[HUES], s_hi[HUES];
static int s_x0, s_y0, s_x1, s_y1;             // clip box
static lv_obj_t *s_score_l, *s_perfect_l, *s_title, *s_hint;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static uint32_t mixh(uint32_t a, uint32_t b, float t)   // a blended toward b by t (canvas alpha)
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) out |= (uint32_t)(((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t + 0.5f) << sh;
    return out;
}
static uint32_t hsl(float h, float s, float l)  // CSS hsl() -> 0xRRGGBB
{
    float c = (1 - fabsf(2 * l - 1)) * s, hp = h / 60, x = c * (1 - fabsf(fmodf(hp, 2) - 1)), m = l - c / 2, r = 0, g = 0, b = 0;
    switch ((int)hp % 6) {
    case 0: r = c; g = x; break;
    case 1: r = x; g = c; break;
    case 2: g = c; b = x; break;
    case 3: g = x; b = c; break;
    case 4: r = x; b = c; break;
    default: r = c; b = x; break;
    }
    return (uint32_t)((r + m) * 255 + 0.5f) << 16 | (uint32_t)((g + m) * 255 + 0.5f) << 8 | (uint32_t)((b + m) * 255 + 0.5f);
}
static int hue_i(int i) { return i % HUES; }

// ---------- drawing, clipped to s_x0..s_x1 x s_y0..s_y1 ----------
static void clip(int x0, int y0, int x1, int y1)
{
    s_x0 = x0 < 0 ? 0 : x0; s_y0 = y0 < 0 ? 0 : y0;
    s_x1 = x1 > GC_W ? GC_W : x1; s_y1 = y1 > GC_H ? GC_H : y1;
}
static void span(int y, float xa, float xb, lv_color_t c)
{
    if (y < s_y0 || y >= s_y1) return;
    int a = (int)ceilf(xa - 0.5f), b = (int)floorf(xb - 0.5f) + 1;
    gc_hline(y, a < s_x0 ? s_x0 : a, b > s_x1 ? s_x1 : b, c);
}
static void rectf(float x, float y, float w, float h, lv_color_t c)
{
    int a = (int)floorf(y + 0.5f), b = (int)floorf(y + h + 0.5f);
    if (a < s_y0) a = s_y0;
    if (b > s_y1) b = s_y1;
    for (int py = a; py < b; py++) span(py, x, x + w, c);
}

static int cam_px(void) { return (int)floorf(s_cam + 0.5f); }   // every edge moves with the camera's whole pixels
static int y_of(int i) { return BASE - i * BH + cam_px(); }

static void block(float x, int y, float w, int i, bool shine)
{
    rectf(x, y, w, BH - 1, s_col[hue_i(i)]);
    if (shine) rectf(x, y, w, 3, s_hi[hue_i(i)]);
}

static void scene(int x0, int y0, int x1, int y1)   // the gradient sky, the ground, the tower, the cut piece, the slider
{
    clip(x0, y0, x1, y1);
    if (s_x1 <= s_x0 || s_y1 <= s_y0) return;
    for (int y = s_y0; y < s_y1; y++) gc_hline(y, s_x0, s_x1, s_bg[y]);
    rectf(0, y_of(0) + BH, GC_W, GC_H, rgb(0x14101E));
    for (int i = s_n > RING ? s_n - RING : 0; i < s_n; i++) {
        int y = y_of(i);
        if (y > GC_H || y < -BH) continue;
        block(s_tx[i % RING], y, s_tw[i % RING], i, true);
    }
    if (s_cut) rectf(s_cut_x, y_of(s_cut_i) + s_cut_y, s_cut_w, BH - 1, s_col[hue_i(s_cut_i)]);
    if (s_state != S_OVER) block(s_cx, y_of(s_n), s_cw, s_n, true);
    gc_dirty(s_x0, s_y0, s_x1 - 1, s_y1 - 1);
}

// ---------- the rules ----------
static void next(void)
{
    float w = s_tw[(s_n - 1) % RING];
    bool from_left = s_n % 2 == 1;
    s_cx = from_left ? 20 : GC_W - 20 - w;
    s_cw = w;
    s_dir = from_left ? 1 : -1;
}

static void push(float x, float w) { s_tx[s_n % RING] = x; s_tw[s_n % RING] = w; s_n++; }

static void reset(void)
{
    s_n = 0;
    push(GC_W / 2 - W0 / 2, W0);
    s_dir = 1; s_speed = 150; s_score = 0; s_cam = 0; s_cut = false;
    next();
    s_full = true;
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
    s_state = S_OVER;
    bool rec = gc_record(s_score);
    long best = (long)gc_best();
    lv_label_set_text(s_title, rec ? "NEW RECORD!" : "GAME OVER");
    if (best) lv_label_set_text_fmt(s_hint, "SCORE %d   BEST %ld\nTAP TO PLAY AGAIN", s_score, best);
    else lv_label_set_text_fmt(s_hint, "SCORE %d\nTAP TO PLAY AGAIN", s_score);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    s_full = true;
}

static void drop(void)
{
    float tx = s_tx[(s_n - 1) % RING], tw = s_tw[(s_n - 1) % RING];
    float l = fmaxf(s_cx, tx), r = fminf(s_cx + s_cw, tx + tw);
    if (r - l <= 0) { game_over(); return; }
    float x = s_cx, w = s_cw;
    if (fabsf(s_cx - tx) < 4) { x = tx; w = tw; s_perfect = 0.4f; }
    else {
        float nl = fmaxf(s_cx, tx), nr = fminf(s_cx + s_cw, tx + tw);
        s_cut = true;
        s_cut_x = s_cx < tx ? s_cx : nr;
        s_cut_w = s_cw - (nr - nl);
        s_cut_y = 0;
        s_cut_i = s_n;
        x = nl; w = nr - nl;
    }
    push(x, w);
    s_score++;
    s_speed = fminf(360, s_speed + 7);
    next();
    s_full = true;
}

// ---------- engine ----------
static void begin(void)
{
    for (int y = 0; y < GC_H; y++) s_bg[y] = rgb(mixh(0x0F1A3A, 0x3A1A4A, (y + 0.5f) / GC_H));   // the sky gradient
    for (int i = 0; i < HUES; i++) {
        uint32_t c = hsl((float)((i * 14 + 40) % 360), 0.85f, 0.55f);
        s_col[i] = rgb(c);
        s_hi[i] = rgb(mixh(c, 0xFFFFFF, 0.3f));
    }
    s_score_l = gc_label(&ui_font_FontTypoderSize36, 0xFFFFFF, LV_ALIGN_CENTER, 0, 52 - GC_H / 2);
    s_perfect_l = gc_label(&ui_font_FontTypoderSize16, 0xFFDD00, LV_ALIGN_CENTER, 0, 88 - GC_H / 2);
    lv_label_set_text(s_perfect_l, "PERFECT!");
    lv_obj_add_flag(s_perfect_l, LV_OBJ_FLAG_HIDDEN);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -20);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 18);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 8, 0);
    s_perfect = 0;
    s_perfect_on = false;
    s_cut_shown = false;
    s_shown_score = -1;
    reset();
    s_state = S_READY;
    overlay("STACK UP", "TAP TO DROP THE BLOCK\nTAP TO START");
    scene(0, 0, GC_W, GC_H);
    s_drawn_cam = cam_px();
    s_full = false;
}

static void frame(float dt)
{
    int slider_y = y_of(s_n);
    if (s_state == S_RUN) {
        s_cx += s_dir * s_speed * dt;
        if (s_cx < 20) { s_cx = 20; s_dir = -s_dir; }
        if (s_cx + s_cw > GC_W - 20) { s_cx = GC_W - 20 - s_cw; s_dir = -s_dir; }
    }
    if (s_perfect > 0) s_perfect -= dt;
    if (s_cut) { s_cut_y += dt * 260; if (s_cut_y > GC_H) s_cut = false; }
    float goal = fmaxf(0, (s_n - 8) * BH);
    s_cam += (goal - s_cam) * fminf(1, dt * 6);

    if (s_full || cam_px() != s_drawn_cam) {
        scene(0, 0, GC_W, GC_H);
        s_full = false;
    } else {
        if (s_state == S_RUN) scene(0, slider_y, GC_W, slider_y + BH);   // the slider's row (it never changes row here)
        if (s_cut || s_cut_shown) {                                       // the falling piece, from where it was drawn
            float y = s_cut ? y_of(s_cut_i) + s_cut_y : GC_H;
            scene((int)floorf(s_cut_x) - 1, (int)floorf(s_cut_drawn) - 1, (int)ceilf(s_cut_x + s_cut_w) + 1, (int)ceilf(y + BH) + 1);
        }
    }
    s_drawn_cam = cam_px();
    s_cut_shown = s_cut;
    if (s_cut) s_cut_drawn = y_of(s_cut_i) + s_cut_y;

    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
    bool on = s_perfect > 0;
    if (on != s_perfect_on) {
        if (on) lv_obj_clear_flag(s_perfect_l, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_perfect_l, LV_OBJ_FLAG_HIDDEN);
        s_perfect_on = on;
    }
}

static void input(gc_input_t in, lv_point_t at)
{
    (void)at;
    if (in != GC_TAP) return;
    if (s_state != S_RUN) {
        if (s_state == S_OVER) reset();
        s_state = S_RUN;
        s_full = true;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    drop();
}

static void icon(lv_color_t *b, int n)
{
    for (int y = 0; y < n; y++) gc_rect_in(b, n, n, 0, y, n, y + 1, rgb(mixh(0x0F1A3A, 0x3A1A4A, (y + 0.5f) / n)));
    int bh = n / 8, y0 = n - 10 - bh;              // a tower of five, a cut piece falling, the next block sliding in
    gc_rect_in(b, n, n, 0, y0 + bh, n, n, rgb(0x14101E));
    static const int16_t X[6] = {26, 34, 34, 40, 40, 58}, W[6] = {80, 72, 66, 60, 60, 60};
    for (int i = 0; i < 6; i++) {
        uint32_t c = hsl((float)((i * 14 + 40) % 360), 0.85f, 0.55f);
        int y = y0 - i * bh - (i == 5 ? 6 : 0);
        gc_rect_in(b, n, n, X[i], y, X[i] + W[i], y + bh - 1, rgb(c));
        gc_rect_in(b, n, n, X[i], y, X[i] + W[i], y + 3, rgb(mixh(c, 0xFFFFFF, 0.3f)));
    }
    gc_rect_in(b, n, n, 100, y0 - 2 * bh + 10, 106, y0 - bh + 9, rgb(hsl(68, 0.85f, 0.55f)));
}

const game_def_t game_stack = {
    .name = "STACK UP", .hint = "TAP TO DROP THE BLOCK\nWHAT OVERHANGS IS CUT OFF", .key = "stack", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
