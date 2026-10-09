// SNAKE — the classic on an 18 x 18 board in the middle of the dial. Touch where the snake should go (above /
// below / left / right of its head) or swipe up / left / right; the walls and its own tail end the run. Each apple
// is a point and makes it faster (6 steps a second, up to 14). Same rules as the browser copy (web/game-arcade.js).
// The screen around the board is drawn once; the board is redrawn only when the snake steps or a run starts.

#include "esp_attr.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"

#define N 18                                   // cells per side
#define C 14                                   // cell size in pixels
#define X0 ((GC_W - N * C) / 2)
#define Y0 ((GC_H - N * C) / 2)
#define MAX_LEN (N * N)

typedef enum { S_READY, S_RUN, S_OVER } snake_state_t;

static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};   // 0 up, 1 right, 2 down, 3 left

static snake_state_t s_state;
static EXT_RAM_BSS_ATTR int8_t s_bx[MAX_LEN], s_by[MAX_LEN];   // the body, [0] = head
static int s_len, s_dir, s_next, s_fx, s_fy, s_score, s_shown_score;
static float s_t, s_speed;
static bool s_redraw;
static lv_obj_t *s_score_l, *s_title, *s_hint;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static int rnd(int n) { int v = (int)(gc_rand() * n); return v >= n ? n - 1 : v; }

// a filled rectangle with rounded corners of radius r
static void rrect(int x, int y, int w, int h, int r, lv_color_t c)
{
    for (int j = 0; j < h; j++) {
        int d = j < r ? r - 1 - j : (j >= h - r ? j - (h - r) : -1), in = 0;   // rows into a corner
        if (d >= 0) { float dy = d + 0.5f; in = r - (int)(sqrtf((float)(r * r) - dy * dy) + 0.5f); }
        gc_hline(y + j, x + in, x + w - in, c);
    }
}

// free cell: on the board and not on the body (skip_tail: the tail moves away this step)
static bool is_free(int x, int y, bool skip_tail)
{
    if (x < 0 || y < 0 || x >= N || y >= N) return false;
    int n = skip_tail ? s_len - 1 : s_len;
    for (int i = 0; i < n; i++) if (s_bx[i] == x && s_by[i] == y) return false;
    return true;
}

static void place(void)
{
    if (s_len >= MAX_LEN) return;              // board full: nowhere left for an apple
    do { s_fx = rnd(N); s_fy = rnd(N); } while (!is_free(s_fx, s_fy, false));
}

static void reset(void)
{
    for (int i = 0; i < 4; i++) { s_bx[i] = (int8_t)(6 - i); s_by[i] = 9; }
    s_len = 4;
    s_dir = s_next = 1;
    s_t = 0; s_speed = 6; s_score = 0;
    place();
    s_redraw = true;
}

static void steer(int d) { if (d != (s_dir + 2) % 4) s_next = d; }

static void overlay(const char *title, const char *hint)
{
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_hint, hint);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void over(void)
{
    s_state = S_OVER;
    bool rec = gc_record(s_score);
    int32_t best = gc_best();
    lv_label_set_text(s_title, rec ? "NEW RECORD!" : "GAME OVER");
    if (best) lv_label_set_text_fmt(s_hint, "SCORE %d   BEST %ld\nTAP TO PLAY AGAIN", s_score, (long)best);
    else lv_label_set_text_fmt(s_hint, "SCORE %d\nTAP TO PLAY AGAIN", s_score);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void draw_board(void)
{
    for (int y = 0; y < N; y++)                // checkered board
        for (int x = 0; x < N; x++)
            gc_rect(X0 + x * C, Y0 + y * C, X0 + x * C + C, Y0 + y * C + C, ((x + y) & 1) ? rgb(0x0E141B) : rgb(0x111922));
    gc_disc(X0 + s_fx * C + C / 2, Y0 + s_fy * C + C / 2, 6, rgb(0xFF3030));   // the apple, with a shine
    gc_disc(X0 + s_fx * C + 5, Y0 + s_fy * C + 5, 2, rgb(0xFFB0B0));
    for (int i = 0; i < s_len; i++) {
        lv_color_t c = i ? ((i & 1) ? rgb(0x22C55E) : rgb(0x2FE06B)) : rgb(0xFFDD00);
        rrect(X0 + s_bx[i] * C + 1, Y0 + s_by[i] * C + 1, C - 2, C - 2, 4, c);
    }
    int hx = X0 + s_bx[0] * C + C / 2, hy = Y0 + s_by[0] * C + C / 2, dx = DX[s_dir], dy = DY[s_dir];   // eyes
    gc_disc(hx + dx * 3 - dy * 3, hy + dy * 3 - dx * 3, 2, rgb(0x111111));
    gc_disc(hx + dx * 3 + dy * 3, hy + dy * 3 + dx * 3, 2, rgb(0x111111));
    gc_dirty(X0, Y0, X0 + N * C - 1, Y0 + N * C - 1);
}

static void begin(void)
{
    gc_fill(rgb(0x05070A));
    gc_rect(X0 - 2, Y0 - 2, X0 + N * C + 2, Y0 + N * C + 2, rgb(0xFFDD00));   // yellow frame, board drawn over it
    s_score_l = gc_label(&ui_font_FontTypoderSize20, 0xFFFFFF, LV_ALIGN_CENTER, 0, 32 - GC_H / 2);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -20);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 18);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 8, 0);
    lv_label_set_text(s_score_l, "0");
    s_shown_score = 0;
    reset();
    s_state = S_READY;
    overlay("SNAKE", "TOUCH WHERE TO GO\nTOUCH TO START");
    draw_board();
}

static void frame(float dt)
{
    if (s_state == S_RUN) {
        s_t += dt;
        float step = 1.0f / s_speed;
        while (s_t >= step && s_state == S_RUN) {
            s_t -= step;
            s_dir = s_next;
            int nx = s_bx[0] + DX[s_dir], ny = s_by[0] + DY[s_dir];
            if (!is_free(nx, ny, true)) { over(); break; }
            bool eat = nx == s_fx && ny == s_fy;
            if (eat && s_len >= MAX_LEN) { over(); break; }   // the whole board is snake
            int keep = eat ? s_len : s_len - 1;               // eating grows by one, else the tail moves up
            memmove(s_bx + 1, s_bx, keep);
            memmove(s_by + 1, s_by, keep);
            s_bx[0] = (int8_t)nx; s_by[0] = (int8_t)ny;
            if (eat) {
                s_len++;
                s_score++;
                s_speed = fminf(14.0f, 6.0f + s_score * 0.25f);
                place();
            }
            s_redraw = true;
        }
    }
    if (s_redraw) { draw_board(); s_redraw = false; }
    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
}

static void input(gc_input_t in, lv_point_t at)
{
    if (in != GC_PRESS && in != GC_SWIPE_UP && in != GC_SWIPE_DOWN && in != GC_SWIPE_LEFT && in != GC_SWIPE_RIGHT) return;
    if (s_state != S_RUN) {
        if (in != GC_PRESS) return;
        if (s_state == S_OVER) reset();
        s_state = S_RUN;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (in == GC_PRESS) {                      // the side of the head the finger is on
        int dx = at.x - (X0 + s_bx[0] * C + C / 2), dy = at.y - (Y0 + s_by[0] * C + C / 2);
        steer(abs(dx) > abs(dy) ? (dx < 0 ? 3 : 1) : (dy < 0 ? 0 : 2));
    } else steer(in == GC_SWIPE_UP ? 0 : in == GC_SWIPE_RIGHT ? 1 : in == GC_SWIPE_DOWN ? 2 : 3);
}

static void icon(lv_color_t *b, int n)
{
    int c = n / 8, x0 = (n - 6 * c) / 2;      // a 6 x 6 board
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 6; x++)
            gc_rect_in(b, n, n, x0 + x * c, x0 + y * c, x0 + x * c + c, x0 + y * c + c, ((x + y) & 1) ? rgb(0x0E141B) : rgb(0x111922));
    static const int8_t body[][2] = {{4, 1}, {3, 1}, {2, 1}, {1, 1}, {1, 2}, {1, 3}, {2, 3}, {3, 3}};
    for (int i = 0; i < (int)(sizeof(body) / sizeof(body[0])); i++) {
        lv_color_t col = i ? ((i & 1) ? rgb(0x22C55E) : rgb(0x2FE06B)) : rgb(0xFFDD00);
        gc_rect_in(b, n, n, x0 + body[i][0] * c + 1, x0 + body[i][1] * c + 1, x0 + body[i][0] * c + c - 1, x0 + body[i][1] * c + c - 1, col);
    }
    gc_disc_in(b, n, n, x0 + 4 * c + c / 2 + 3, x0 + c + c / 2 - 3, 2, rgb(0x111111));
    gc_disc_in(b, n, n, x0 + 4 * c + c / 2 + 3, x0 + c + c / 2 + 3, 2, rgb(0x111111));
    gc_disc_in(b, n, n, x0 + 4 * c + c / 2, x0 + 4 * c + c / 2, c * 2 / 5, rgb(0xFF3030));
}

const game_def_t game_snake = {
    .name = "SNAKE", .hint = "TOUCH WHERE TO GO\nMISS THE WALLS AND TAIL", .key = "snake", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
