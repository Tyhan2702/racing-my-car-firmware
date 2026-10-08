// SIGNAL MEMORY — four coloured arcs around the dial light up in a sequence; touch them back in the same order.
// Every round adds one more signal and plays a little faster. The score is the longest sequence repeated.

#include <math.h>
#include "../ui.h"
#include "game_core.h"
#include "esp_random.h"

#define CX 180
#define CY 180
#define R0 74
#define R1 168
#define MAX_SEQ 64

typedef enum { M_READY, M_SHOW, M_INPUT, M_OVER } mem_state_t;

static const uint32_t DIM[4] = {0x4A0A0A, 0x0A2A4A, 0x4A400A, 0x0A3A16};      // top red, right blue, bottom yellow, left green
static const uint32_t LIT[4] = {0xFF2A2A, 0x2F9BFF, 0xFFDD00, 0x2FE06B};
static mem_state_t s_state;
static uint8_t s_seq[MAX_SEQ];
static int s_len, s_pos, s_lit = -1;
static float s_t, s_step;
static lv_obj_t *s_big, *s_msg, *s_best_l;

static void draw_pad(int i, bool lit)
{
    float a0 = -45.0f + i * 90.0f + 3.0f, a1 = a0 + 84.0f;
    if (a0 < 0) { a0 += 360.0f; a1 += 360.0f; }
    gc_ring(CX, CY, R0, R1, a0, a1, lv_color_hex(lit ? LIT[i] : DIM[i]));
    static const int bx[4][4] = {{50, 10, 310, 120}, {240, 50, 352, 310}, {50, 240, 310, 352}, {8, 50, 120, 310}};
    gc_dirty(bx[i][0], bx[i][1], bx[i][2], bx[i][3]);
}

static void light(int i)
{
    if (s_lit >= 0) draw_pad(s_lit, false);
    s_lit = i;
    if (i >= 0) draw_pad(i, true);
}

static void round_start(void)
{
    if (s_len < MAX_SEQ) s_seq[s_len++] = (uint8_t)(esp_random() % 4);
    s_state = M_SHOW; s_pos = 0; s_t = -0.6f;
    s_step = 0.55f - 0.02f * s_len;                       // faster every round
    if (s_step < 0.25f) s_step = 0.25f;
    lv_label_set_text_fmt(s_big, "%d", s_len);
    lv_label_set_text(s_msg, "WATCH");
}

static void begin(void)
{
    for (int i = 0; i < 4; i++) draw_pad(i, false);
    gc_disc(CX, CY, R0 - 6, lv_color_hex(0x0E0E0E));
    s_big = gc_label(&ui_font_FontTypoderSize44, 0xFFFFFF, LV_ALIGN_CENTER, 0, -8);
    s_msg = gc_label(&ui_font_FontTypoderSize16, 0x9A9A9A, LV_ALIGN_CENTER, 0, 30);
    s_best_l = gc_label(&ui_font_FontTypoderSize16, 0xFFDD00, LV_ALIGN_CENTER, 0, -44);
    lv_label_set_text_fmt(s_best_l, "BEST %ld", (long)gc_best());
    s_state = M_READY; s_len = 0; s_lit = -1;
    lv_label_set_text(s_big, "GO");
    lv_label_set_text(s_msg, "TOUCH TO START");
}

static void frame(float dt)
{
    s_t += dt;
    if (s_state == M_SHOW) {
        if (s_t < 0) return;
        int idx = (int)(s_t / s_step);
        bool on = fmodf(s_t, s_step) < s_step * 0.7f;
        if (idx >= s_len) { light(-1); s_state = M_INPUT; s_pos = 0; lv_label_set_text(s_msg, "YOUR TURN"); return; }
        int want = on ? s_seq[idx] : -1;
        if (want != s_lit) light(want);
    } else if (s_lit >= 0 && s_t > 0.25f && s_state != M_SHOW) {
        light(-1);                                          // a touched pad flashes briefly
    }
}

static void input(gc_input_t in, lv_point_t at)
{
    if (in != GC_PRESS) return;
    if (s_state == M_READY || s_state == M_OVER) { s_len = 0; light(-1); round_start(); return; }
    if (s_state != M_INPUT) return;
    int dx = at.x - CX, dy = at.y - CY;
    if (dx * dx + dy * dy < R0 * R0) return;                // the centre is not a pad
    float a = atan2f((float)dx, (float)-dy) * 57.29578f;    // 0 = up, clockwise
    if (a < 0) a += 360.0f;
    int pad = ((int)((a + 45.0f) / 90.0f)) % 4;
    light(pad); s_t = 0;
    if (pad != s_seq[s_pos]) {
        s_state = M_OVER;
        int score = s_len - 1;
        bool rec = gc_record(score);
        lv_label_set_text_fmt(s_big, "%d", score);
        lv_label_set_text(s_msg, rec ? "NEW RECORD!\nTOUCH TO GO AGAIN" : "WRONG SIGNAL\nTOUCH TO GO AGAIN");
        lv_label_set_text_fmt(s_best_l, "BEST %ld", (long)gc_best());
        return;
    }
    if (++s_pos >= s_len) { lv_label_set_text(s_msg, "NICE!"); round_start(); s_t = -0.8f; }
}

static void icon(lv_color_t *b, int n)
{
    for (int i = 0; i < 4; i++) {
        float a0 = -45.0f + i * 90.0f + 4.0f;
        if (a0 < 0) a0 += 360.0f;
        gc_ring_in(b, n, n, n / 2, n / 2, n / 5, n / 2 - 6, a0, a0 + 82.0f, lv_color_hex(i == 2 ? LIT[i] : DIM[i] + 0x202020));
    }
}

const game_def_t game_memory = {
    .name = "SIGNAL MEMORY", .hint = "WATCH THE SIGNALS\nTOUCH THEM IN ORDER", .key = "memory", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
