// PADDLE DUEL — you (bottom, yellow) against the gauge (top, red). Drag your paddle; where the ball meets it sets
// the angle and every hit is 5% faster. Each point you win counts, three misses end it, and the gauge's paddle gets
// quicker with your score. Same rules as the browser copy (web/game-arcade.js). The court (with the lives under it)
// is redrawn every frame; the rest of the screen is drawn once.

#include <math.h>
#include "../ui.h"
#include "game_core.h"

#define L 80                                   // the court
#define R 280
#define T 50
#define B 310
#define PY 294                                 // your paddle's centre line
#define CPU_Y 66                               // the gauge's paddle
#define PW 56
#define BR 6
#define LIVES_Y 330
#define BG 0x06080D
#define AX0 (L - 4)                            // the area redrawn each frame
#define AY0 40
#define AX1 (R + 4)
#define AY1 (LIVES_Y + 8)

typedef enum { P_READY, P_RUN, P_OVER } pong_state_t;

static pong_state_t s_state;
static float s_px, s_cx, s_bx, s_by, s_vx, s_vy, s_speed, s_wait;
static int s_score, s_miss, s_shown_score;
static lv_obj_t *s_score_l, *s_big_l, *s_title, *s_hint;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

// a filled rectangle with rounded corners of radius r
static void rrect(int x, int y, int w, int h, int r, lv_color_t c)
{
    for (int j = 0; j < h; j++) {
        int d = j < r ? r - 1 - j : (j >= h - r ? j - (h - r) : -1), in = 0;   // rows into a corner
        if (d >= 0) { float dy = d + 0.5f; in = r - (int)(sqrtf((float)(r * r) - dy * dy) + 0.5f); }
        gc_hline(y + j, x + in, x + w - in, c);
    }
}

static void serve(void)
{
    s_bx = GC_W / 2; s_by = GC_H / 2;
    s_vx = (gc_rand() < 0.5f ? -1 : 1) * 70.0f; s_vy = 150;
    s_speed = hypotf(70, 150);
    s_wait = 0.8f;
}

static void reset(void)
{
    s_px = s_cx = GC_W / 2;
    s_score = 0; s_miss = 0;
    serve();
}

// off the paddle at p: the further from its centre, the steeper; down = away from the gauge's paddle
static void hit(float p, float y, bool down)
{
    float off = clampf((s_bx - p) / (PW / 2), -1, 1);
    s_speed = fminf(420, s_speed * 1.05f);
    float a = off * 1.05f;
    s_vx = sinf(a) * s_speed;
    s_vy = (down ? 1 : -1) * cosf(a) * s_speed;
    s_by = y;
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
    s_state = P_OVER;
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
    gc_fill(rgb(BG));
    s_big_l = gc_label(&ui_font_FontTypoderSize44, 0xFFDD00, LV_ALIGN_CENTER, 0, 40);   // faint score on the court
    lv_obj_set_style_text_opa(s_big_l, 46, 0);
    s_score_l = gc_label(&ui_font_FontTypoderSize20, 0xFFFFFF, LV_ALIGN_CENTER, 0, 30 - GC_H / 2);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -20);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 18);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 8, 0);
    lv_label_set_text(s_score_l, "0");
    lv_label_set_text(s_big_l, "0");
    s_shown_score = 0;
    reset();
    s_state = P_READY;
    overlay("PADDLE DUEL", "DRAG YOUR PADDLE\nTOUCH TO START");
}

static void frame(float dt)
{
    if (s_state == P_RUN) {
        float cpu = 150.0f + s_score * 12, goal = s_vy < 0 ? s_bx : GC_W / 2;   // the gauge follows the ball coming to it
        s_cx += clampf(goal - s_cx, -cpu * dt, cpu * dt);
        s_cx = clampf(s_cx, L + PW / 2, R - PW / 2);
        if (s_wait > 0) s_wait -= dt;
        else {
            s_bx += s_vx * dt; s_by += s_vy * dt;
            if (s_bx < L + BR) { s_bx = L + BR; s_vx = fabsf(s_vx); }
            if (s_bx > R - BR) { s_bx = R - BR; s_vx = -fabsf(s_vx); }
            if (s_vy > 0 && s_by + BR >= PY - 4 && s_by < PY + 6 && fabsf(s_bx - s_px) < PW / 2 + BR) hit(s_px, PY - 4 - BR, false);
            if (s_vy < 0 && s_by - BR <= CPU_Y + 4 && s_by > CPU_Y - 6 && fabsf(s_bx - s_cx) < PW / 2 + BR) hit(s_cx, CPU_Y + 4 + BR, true);
            if (s_by > B) {                    // you missed
                s_miss++;
                serve();
                if (s_miss >= 3) over();
            } else if (s_by < T) {             // the gauge missed: a point, served toward it
                s_score++;
                serve();
                s_vy = -s_vy;
            }
        }
    }

    gc_rect(AX0, AY0, AX1, AY1, rgb(BG));
    gc_rect(L, T, R, B, rgb(0x0C1220));
    lv_color_t line = rgb(0x2A3550);           // 2 px border on the court's edge, dashed centre line
    gc_rect(L - 1, T - 1, R + 1, T + 1, line);
    gc_rect(L - 1, B - 1, R + 1, B + 1, line);
    gc_rect(L - 1, T - 1, L + 1, B + 1, line);
    gc_rect(R - 1, T - 1, R + 1, B + 1, line);
    for (int x = L; x < R; x += 16) gc_rect(x, GC_H / 2 - 1, x + 8 < R ? x + 8 : R, GC_H / 2 + 1, line);
    rrect((int)(s_cx - PW / 2 + 0.5f), CPU_Y - 4, PW, 8, 4, rgb(0xFF3030));
    rrect((int)(s_px - PW / 2 + 0.5f), PY - 4, PW, 8, 4, rgb(0xFFDD00));
    gc_disc((int)(s_bx + 0.5f), (int)(s_by + 0.5f), BR, rgb(0xFFFFFF));
    int lives = 3 - s_miss;
    for (int i = 0; i < lives; i++) gc_disc(GC_W / 2 - (lives - 1) * 9 + i * 18, LIVES_Y, 5, rgb(0xFFDD00));
    gc_dirty(AX0, AY0, AX1 - 1, AY1 - 1);

    if (s_score != s_shown_score) {
        lv_label_set_text_fmt(s_score_l, "%d", s_score);
        lv_label_set_text_fmt(s_big_l, "%d", s_score);
        s_shown_score = s_score;
    }
}

static void input(gc_input_t in, lv_point_t at)
{
    if (s_state != P_RUN) {
        if (in != GC_PRESS) return;
        if (s_state == P_OVER) reset();
        s_state = P_RUN;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (in == GC_PRESS || in == GC_DRAG) s_px = clampf(at.x, L + PW / 2, R - PW / 2);
}

static void icon(lv_color_t *b, int n)
{
    int l = 22, r = n - 22, t = 12, bt = n - 12;
    gc_rect_in(b, n, n, l - 2, t - 2, r + 2, bt + 2, rgb(0x2A3550));
    gc_rect_in(b, n, n, l, t, r, bt, rgb(0x0C1220));
    for (int x = l; x < r; x += 12) gc_rect_in(b, n, n, x, n / 2 - 1, x + 6 < r ? x + 6 : r, n / 2 + 1, rgb(0x2A3550));
    gc_rect_in(b, n, n, n / 2 - 2, t + 8, n / 2 + 26, t + 16, rgb(0xFF3030));
    gc_rect_in(b, n, n, n / 2 - 30, bt - 16, n / 2 + 2, bt - 8, rgb(0xFFDD00));
    gc_disc_in(b, n, n, n / 2 + 14, n / 2 + 18, 7, rgb(0xFFFFFF));
}

const game_def_t game_pong = {
    .name = "PADDLE DUEL", .hint = "DRAG YOUR PADDLE\nBEAT THE GAUGE", .key = "pong", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
