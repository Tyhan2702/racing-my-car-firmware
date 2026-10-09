// BRICK BREAK — drag the paddle, keep the ball up and clear the 8 x 5 wall (top row 50 points down to 10 at the
// bottom). The ball waits on the paddle until a tap launches it; where it meets the paddle sets the angle. Each
// cleared wall is a new, faster one; three balls. Same rules as the browser copy (web/game-arcade.js). The field
// (with the balls left under it) is redrawn every frame; the rest of the screen is drawn once.

#include <math.h>
#include <stdbool.h>
#include "../ui.h"
#include "game_core.h"

#define L 78                                   // the field
#define R 282
#define T 48
#define B 318
#define PY 292                                 // the paddle's centre line
#define PW 50
#define BR 5
#define COLS 8
#define ROWS 5
#define BW ((float)(R - L) / COLS)
#define BH 12
#define BY 72
#define LIVES_Y 334
#define BG 0x07070C
#define AX0 (L - 4)                            // the area redrawn each frame
#define AY0 40
#define AX1 (R + 4)
#define AY1 (LIVES_Y + 8)

typedef enum { K_READY, K_RUN, K_OVER } bricks_state_t;
typedef struct { float x; int y, r; bool alive; } brick_t;

static const uint32_t ROWC[ROWS] = {0xFF3030, 0xFF8A00, 0xFFDD00, 0x2FE06B, 0x2F9BFF};
static const int PTS[ROWS] = {50, 40, 30, 20, 10};

static bricks_state_t s_state;
static brick_t s_brick[ROWS * COLS];
static float s_px, s_bx, s_by, s_vx, s_vy;
static bool s_stuck, s_shown_launch;
static int s_lv, s_score, s_left, s_shown_score;
static lv_color_t s_hi[ROWS];
static lv_obj_t *s_score_l, *s_launch_l, *s_title, *s_hint;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static lv_color_t mix(uint32_t a, uint32_t b, float t)   // a blended toward b by t (canvas alpha)
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) out |= (uint32_t)(((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t + 0.5f) << sh;
    return rgb(out);
}

// a filled rectangle with rounded corners of radius r
static void rrect(int x, int y, int w, int h, int r, lv_color_t c)
{
    for (int j = 0; j < h; j++) {
        int d = j < r ? r - 1 - j : (j >= h - r ? j - (h - r) : -1), in = 0;   // rows into a corner
        if (d >= 0) { float dy = d + 0.5f; in = r - (int)(sqrtf((float)(r * r) - dy * dy) + 0.5f); }
        gc_hline(y + j, x + in, x + w - in, c);
    }
}

static void build(void)
{
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            s_brick[r * COLS + c] = (brick_t){.x = L + c * BW, .y = BY + r * (BH + 3), .r = r, .alive = true};
}

static void new_ball(void) { s_stuck = true; s_bx = s_px; s_by = PY - 6 - BR; s_vx = s_vy = 0; }

static void reset(void)
{
    s_px = GC_W / 2;
    s_lv = 0; s_score = 0; s_left = 3;
    build();
    new_ball();
}

static void launch(void)
{
    if (!s_stuck) return;
    s_stuck = false;
    float s = 210.0f + s_lv * 20;
    s_vx = s * 0.45f; s_vy = -s * 0.89f;
}

static void overlay(const char *title, const char *hint)
{
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_hint, hint);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void over(void)
{
    s_state = K_OVER;
    bool rec = gc_record(s_score);
    int32_t best = gc_best();
    lv_label_set_text(s_title, rec ? "NEW RECORD!" : "GAME OVER");
    if (best) lv_label_set_text_fmt(s_hint, "SCORE %d   BEST %ld\nTAP TO PLAY AGAIN", s_score, (long)best);
    else lv_label_set_text_fmt(s_hint, "SCORE %d\nTAP TO PLAY AGAIN", s_score);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void begin(void)
{
    for (int r = 0; r < ROWS; r++) s_hi[r] = mix(ROWC[r], 0xFFFFFF, 0.3f);
    gc_fill(rgb(BG));
    s_score_l = gc_label(&ui_font_FontTypoderSize20, 0xFFFFFF, LV_ALIGN_CENTER, 0, 30 - GC_H / 2);
    s_launch_l = gc_label(&ui_font_FontTypoderSize16, 0x9A9A9A, LV_ALIGN_CENTER, 0, 252 - GC_H / 2);
    lv_label_set_text(s_launch_l, "TAP TO LAUNCH");
    lv_obj_add_flag(s_launch_l, LV_OBJ_FLAG_HIDDEN);
    s_shown_launch = false;
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -20);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 18);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 8, 0);
    lv_label_set_text(s_score_l, "0");
    s_shown_score = 0;
    reset();
    s_state = K_READY;
    overlay("BRICK BREAK", "DRAG THE PADDLE\nTOUCH TO START");
}

static void frame(float dt)
{
    if (s_state == K_RUN) {
        if (s_stuck) { s_bx = s_px; s_by = PY - 6 - BR; }
        else {
            float ox = s_bx, oy = s_by;
            s_bx += s_vx * dt; s_by += s_vy * dt;
            if (s_bx < L + BR) { s_bx = L + BR; s_vx = fabsf(s_vx); }
            if (s_bx > R - BR) { s_bx = R - BR; s_vx = -fabsf(s_vx); }
            if (s_by < T + BR) { s_by = T + BR; s_vy = fabsf(s_vy); }
            if (s_vy > 0 && s_by + BR >= PY - 5 && oy + BR <= PY - 3 && fabsf(s_bx - s_px) < PW / 2 + BR) {
                float s = fminf(380, hypotf(s_vx, s_vy) * 1.015f), off = clampf((s_bx - s_px) / (PW / 2), -1, 1);
                s_vx = sinf(off) * s; s_vy = -cosf(off) * s;   // the angle comes from where it hit the paddle
                s_by = PY - 5 - BR;
            }
            for (int i = 0; i < ROWS * COLS; i++) {          // at most one brick per frame
                brick_t *b = &s_brick[i];
                if (!b->alive || s_bx < b->x - BR || s_bx > b->x + BW + BR || s_by < b->y - BR || s_by > b->y + BH + BR) continue;
                b->alive = false;
                s_score += PTS[b->r];
                if (ox >= b->x && ox <= b->x + BW) s_vy = -s_vy;   // came from above / below, else from the side
                else s_vx = -s_vx;
                break;
            }
            bool any = false;
            for (int i = 0; i < ROWS * COLS; i++) if (s_brick[i].alive) { any = true; break; }
            if (!any) { s_lv++; build(); new_ball(); }
            if (s_by > B) {                    // the ball is lost
                s_left--;
                if (s_left <= 0) over();
                else new_ball();
            }
        }
    }

    gc_rect(AX0, AY0, AX1, AY1, rgb(BG));
    gc_rect(L - 2, T - 2, R + 2, B + 2, rgb(0xFFDD00));   // 2 px yellow frame around the field
    gc_rect(L, T, R, B, rgb(0x0F0F18));
    for (int i = 0; i < ROWS * COLS; i++) {
        brick_t *b = &s_brick[i];
        if (!b->alive) continue;
        int x0 = (int)(b->x + 2.0f), x1 = (int)(b->x + BW - 1.0f);
        gc_rect(x0, b->y, x1, b->y + BH, rgb(ROWC[b->r]));
        gc_rect(x0, b->y, x1, b->y + 3, s_hi[b->r]);
    }
    int px = (int)(s_px + 0.5f);
    rrect(px - PW / 2, PY - 5, PW, 9, 4, rgb(0xE8E8E8));
    gc_rect(px - PW / 2 + 6, PY - 3, px + PW / 2 - 6, PY, rgb(0xFFDD00));
    gc_disc((int)(s_bx + 0.5f), (int)(s_by + 0.5f), BR, rgb(0xFFFFFF));
    for (int i = 0; i < s_left; i++) gc_disc(GC_W / 2 - (s_left - 1) * 9 + i * 18, LIVES_Y, 5, rgb(0xFFDD00));
    gc_dirty(AX0, AY0, AX1 - 1, AY1 - 1);

    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
    bool show = s_state == K_RUN && s_stuck;
    if (show != s_shown_launch) {
        if (show) lv_obj_clear_flag(s_launch_l, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_launch_l, LV_OBJ_FLAG_HIDDEN);
        s_shown_launch = show;
    }
}

static void input(gc_input_t in, lv_point_t at)
{
    if (s_state != K_RUN) {
        if (in != GC_PRESS) return;
        if (s_state == K_OVER) reset();
        s_state = K_RUN;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (in == GC_PRESS || in == GC_DRAG) s_px = clampf(at.x, L + PW / 2, R - PW / 2);
    if (in == GC_TAP) launch();
}

static void icon(lv_color_t *b, int n)
{
    int l = 14, r = n - 14, t = 14, bt = n - 10;
    gc_rect_in(b, n, n, l - 2, t - 2, r + 2, bt + 2, rgb(0xFFDD00));
    gc_rect_in(b, n, n, l, t, r, bt, rgb(0x0F0F18));
    int bw = (r - l) / 4;
    for (int row = 0; row < 4; row++)
        for (int c = 0; c < 4; c++) {
            if (row == 3 && c == 2) continue;  // one brick already broken
            int x = l + c * bw, y = t + 8 + row * 14;
            gc_rect_in(b, n, n, x + 2, y, x + bw - 2, y + 11, rgb(ROWC[row]));
            gc_rect_in(b, n, n, x + 2, y, x + bw - 2, y + 3, mix(ROWC[row], 0xFFFFFF, 0.3f));
        }
    gc_rect_in(b, n, n, n / 2 - 22, bt - 14, n / 2 + 22, bt - 6, rgb(0xE8E8E8));
    gc_rect_in(b, n, n, n / 2 - 16, bt - 12, n / 2 + 16, bt - 9, rgb(0xFFDD00));
    gc_disc_in(b, n, n, n / 2 + 10, bt - 32, 6, rgb(0xFFFFFF));
}

const game_def_t game_bricks = {
    .name = "BRICK BREAK", .hint = "DRAG THE PADDLE, TAP TO LAUNCH\nCLEAR EVERY BRICK", .key = "bricks", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
