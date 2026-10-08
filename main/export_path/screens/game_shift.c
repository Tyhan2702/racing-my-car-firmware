// PERFECT SHIFT — the revs climb on a tachometer; touch the screen to shift up inside the green window.
// Dead centre is PERFECT (+100), the rest of the window GOOD (+50), too early is a bogged shift (+10, the next gear
// starts lower). Hitting the 9000 rpm limiter ends the run. After 6th gear the lap is done: the window narrows
// and the revs climb faster. Ten shift lights across the top fill up like a steering-wheel display.

#include <math.h>
#include "../ui.h"
#include "game_core.h"

#define CX 180
#define CY 196
#define R0 122
#define R1 150
#define A_START 225.0f                 // 0 rpm at 7:30 …
#define A_SPAN 270.0f                  // … 9000 rpm at 4:30
#define RPM_MAX 9000.0f

typedef enum { S_READY, S_RUN, S_OVER } shift_state_t;

static shift_state_t s_state;
static float s_rpm, s_rate, s_win_lo, s_win_hi, s_flash;
static int s_gear, s_lap, s_score, s_lights_drawn = -1;
static float s_drawn_angle;
static lv_obj_t *s_gear_l, *s_rpm_l, *s_score_l, *s_msg, *s_best_l;

static float ang(float rpm) { return A_START + A_SPAN * rpm / RPM_MAX; }

static void draw_dial(void)
{
    gc_ring(CX, CY, R0, R1, A_START, A_START + A_SPAN, lv_color_hex(0x262626));               // track
    gc_ring(CX, CY, R0, R1, ang(8200), ang(RPM_MAX), lv_color_hex(0x5A0A0A));                  // red line
    gc_ring(CX, CY, R0 - 10, R0 - 4, ang(s_win_lo), ang(s_win_hi), lv_color_hex(0x2FE06B));    // shift window
    gc_ring(CX, CY, R0 - 10, R0 - 4, A_START, ang(s_win_lo) - 0.5f, lv_color_hex(0x000000));
    gc_ring(CX, CY, R0 - 10, R0 - 4, ang(s_win_hi) + 0.5f, A_START + A_SPAN, lv_color_hex(0x000000));
    for (int k = 0; k <= 9; k++) {                                                              // 1000 rpm ticks
        float a = (ang(k * 1000.0f) - 90.0f) * 0.0174533f;
        gc_disc(CX + (int)(cosf(a) * (R1 + 9)), CY + (int)(sinf(a) * (R1 + 9)), 2, lv_color_hex(k >= 8 ? 0xFF3030 : 0x9A9A9A));
    }
    gc_dirty(CX - R1 - 14, CY - R1 - 14, CX + R1 + 14, CY + R1 + 14);
    s_drawn_angle = A_START;
}

static void draw_needle_arc(void)
{
    float a = ang(s_rpm);
    if (a < s_drawn_angle) {                                   // revs fell (shift): clear and redraw the fill
        gc_ring(CX, CY, R0, R1, A_START, A_START + A_SPAN, lv_color_hex(0x262626));
        gc_ring(CX, CY, R0, R1, ang(8200), ang(RPM_MAX), lv_color_hex(0x5A0A0A));
        s_drawn_angle = A_START;
    }
    if (a > s_drawn_angle + 0.3f) {
        lv_color_t c = s_rpm >= s_win_lo ? (s_rpm > s_win_hi ? lv_color_hex(0xFF3030) : lv_color_hex(0x2FE06B)) : lv_color_hex(0xFFDD00);
        gc_ring(CX, CY, R0, R1, s_drawn_angle, a, c);
        s_drawn_angle = a;
    }
    gc_dirty(CX - R1 - 2, CY - R1 - 2, CX + R1 + 2, CY + R1 + 2);
}

static void draw_lights(void)
{
    int lit = (int)(10 * (s_rpm - 4000) / (s_win_hi - 4000));
    if (lit < 0) lit = 0;
    if (lit > 10) lit = 10;
    bool blink = s_rpm >= s_win_lo && ((int)(s_flash * 10) & 1);
    if (lit == s_lights_drawn && !blink) return;
    for (int i = 0; i < 10; i++) {
        uint32_t on = i < 4 ? 0x2FE06B : i < 7 ? 0xFFDD00 : i < 9 ? 0xFF3030 : 0x2F8CFF;
        bool show = i < lit && !(blink && lit >= 10);
        gc_disc(CX - 99 + i * 22, 52, 8, lv_color_hex(show ? on : 0x202020));
    }
    gc_dirty(CX - 110, 42, CX + 110, 62);
    s_lights_drawn = blink ? -1 : lit;
}

static void new_lap(void)
{
    s_gear = 1; s_rpm = 1500;
    float width = 700.0f - 90.0f * (s_lap - 1);
    if (width < 260) width = 260;
    s_win_lo = 7600 - width / 2; s_win_hi = 7600 + width / 2;
    s_rate = 4200.0f + 600.0f * (s_lap - 1);                  // rpm per second in 1st gear
    draw_dial();
}

static void begin(void)
{
    s_gear_l = gc_label(&ui_font_FontTypoderSize56, 0xFFFFFF, LV_ALIGN_CENTER, 0, 10);
    s_rpm_l = gc_label(&ui_font_FontTypoderSize16, 0x9A9A9A, LV_ALIGN_CENTER, 0, 74);
    s_score_l = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -66);
    s_msg = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_BOTTOM_MID, 0, -40);
    s_best_l = gc_label(&ui_font_FontTypoderSize16, 0x9A9A9A, LV_ALIGN_TOP_MID, 0, 18);
    lv_label_set_text_fmt(s_best_l, "BEST %ld", (long)gc_best());
    s_state = S_READY; s_lap = 1; s_score = 0;
    new_lap();
    lv_label_set_text(s_gear_l, "N");
    lv_label_set_text(s_msg, "TOUCH IN THE GREEN TO SHIFT\nTOUCH TO START");
    lv_label_set_text(s_score_l, "");
    s_lights_drawn = -1;
    draw_lights();
}

static void over(const char *why)
{
    s_state = S_OVER;
    bool rec = gc_record(s_score);
    lv_label_set_text_fmt(s_msg, "%s  %s\nTOUCH TO GO AGAIN", why, rec ? "NEW RECORD!" : "");
    lv_label_set_text_fmt(s_best_l, "BEST %ld", (long)gc_best());
}

static void frame(float dt)
{
    s_flash += dt;
    if (s_state == S_RUN) {
        s_rpm += s_rate * dt / (1.0f + 0.22f * (s_gear - 1));   // taller gears climb slower
        if (s_rpm >= RPM_MAX) { s_rpm = RPM_MAX; over("OVER-REV!"); }
        lv_label_set_text_fmt(s_rpm_l, "%d RPM", (int)s_rpm);
    }
    draw_needle_arc();
    draw_lights();
}

static void input(gc_input_t in, lv_point_t at)
{
    (void)at;
    if (in != GC_PRESS) return;
    if (s_state != S_RUN) {
        if (s_state == S_OVER) { s_lap = 1; s_score = 0; new_lap(); }
        s_state = S_RUN;
        lv_label_set_text(s_msg, "");
        lv_label_set_text_fmt(s_gear_l, "%d", s_gear);
        lv_label_set_text_fmt(s_score_l, "%d", s_score);
        return;
    }
    const char *word;
    float drop;
    if (s_rpm >= s_win_lo && s_rpm <= s_win_hi) {
        bool perfect = fabsf(s_rpm - (s_win_lo + s_win_hi) / 2) < (s_win_hi - s_win_lo) * 0.2f;
        s_score += perfect ? 100 : 50;
        word = perfect ? "PERFECT!" : "GOOD";
        drop = 0.66f;
    } else {
        s_score += 10;
        word = "TOO EARLY";
        drop = 0.5f;                                           // bogged: the next gear starts low
    }
    if (s_gear >= 6) {
        s_lap++;
        s_score += 200;
        lv_label_set_text_fmt(s_msg, "LAP %d  +200", s_lap - 1);
        new_lap();
    } else {
        s_gear++;
        s_rpm *= drop;
        lv_label_set_text(s_msg, word);
    }
    lv_label_set_text_fmt(s_gear_l, "%d", s_gear);
    lv_label_set_text_fmt(s_score_l, "%d", s_score);
}

static void icon(lv_color_t *b, int n)
{
    int cx = n / 2, cy = n / 2 + 6;
    gc_ring_in(b, n, n, cx, cy, n / 3, n / 3 + 10, 225, 495, lv_color_hex(0x2A2A2A));
    gc_ring_in(b, n, n, cx, cy, n / 3, n / 3 + 10, 225, 420, lv_color_hex(0xFFDD00));
    gc_ring_in(b, n, n, cx, cy, n / 3 - 8, n / 3 - 3, 405, 440, lv_color_hex(0x2FE06B));
    for (int i = 0; i < 6; i++) gc_disc_in(b, n, n, n / 2 - 35 + i * 14, 18, 5, lv_color_hex(i < 2 ? 0x2FE06B : i < 4 ? 0xFFDD00 : 0xFF3030));
    gc_rect_in(b, n, n, cx - 9, cy - 16, cx + 9, cy + 16, lv_color_hex(0xFFFFFF));          // a bold "1"
    gc_rect_in(b, n, n, cx - 9, cy - 16, cx + 2, cy - 8, lv_color_hex(0x101010));
    gc_rect_in(b, n, n, cx - 9, cy - 8, cx - 1, cy + 16, lv_color_hex(0x101010));
}

const game_def_t game_shift = {
    .name = "PERFECT SHIFT", .hint = "TOUCH IN THE GREEN ZONE\nTO SHIFT UP", .key = "shift", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
