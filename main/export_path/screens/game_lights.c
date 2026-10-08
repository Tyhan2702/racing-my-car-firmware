// LIGHTS OUT — the F1 start: five red lights come on one by one, then go out after a random wait. Touch the
// screen the moment they go out; the reaction time is the score (lower is better). Touching while the lights are
// still on is a jump start. Timing uses the press itself (GC_PRESS), not the release.

#include "../ui.h"
#include "game_core.h"
#include "esp_timer.h"

#define PODS 5
#define POD_Y 150
#define POD_R 20
#define POD_GAP 58

typedef enum { L_READY, L_ARMING, L_WAIT_GO, L_GO, L_RESULT } lights_state_t;

static lights_state_t s_state;
static int s_on, s_drawn = -1;
static float s_t, s_hold;
static int64_t s_go_us;
static lv_obj_t *s_big, *s_msg, *s_best_l;

static void draw_pods(int on)
{
    for (int i = 0; i < PODS; i++) {
        int cx = GC_W / 2 + (i - 2) * POD_GAP;
        gc_rect(cx - 26, POD_Y - 56, cx + 26, POD_Y + 30, lv_color_hex(0x161616));           // housing
        gc_disc(cx, POD_Y - 26, POD_R - 6, lv_color_hex(0x262626));                          // upper (unused) lamp
        gc_disc(cx, POD_Y, POD_R, i < on ? lv_color_hex(0xFF1A1A) : lv_color_hex(0x3A0606));  // red lamp
        if (i < on) gc_disc(cx - 6, POD_Y - 6, 5, lv_color_hex(0xFF9A9A));                   // shine
    }
    gc_dirty(GC_W / 2 - 2 * POD_GAP - 28, POD_Y - 58, GC_W / 2 + 2 * POD_GAP + 28, POD_Y + 32);
    s_drawn = on;
}

static void show(const char *big, const char *msg)
{
    lv_label_set_text(s_big, big);
    lv_label_set_text(s_msg, msg);
}

static void best_line(void)
{
    int32_t b = gc_best();
    if (b) lv_label_set_text_fmt(s_best_l, "BEST %ld MS", (long)b);
    else lv_label_set_text(s_best_l, "NO RECORD YET");
}

static void begin(void)
{
    s_big = gc_label(&ui_font_FontTypoderSize40, 0xFFFFFF, LV_ALIGN_CENTER, 0, 66);
    s_msg = gc_label(&ui_font_FontTypoderSize16, 0x9A9A9A, LV_ALIGN_CENTER, 0, 112);
    s_best_l = gc_label(&ui_font_FontTypoderSize16, 0xFFDD00, LV_ALIGN_TOP_MID, 0, 44);
    best_line();
    s_state = L_READY;
    show("READY?", "TOUCH WHEN THE\nLIGHTS GO OUT");
    draw_pods(0);
}

static void frame(float dt)
{
    s_t += dt;
    if (s_state == L_ARMING) {
        int want = (int)(s_t / 0.8f) + 1;                   // one more light every 0.8 s
        if (want > PODS) { s_state = L_WAIT_GO; s_t = 0; s_hold = 0.4f + gc_rand() * 2.4f; want = PODS; }
        if (want != s_drawn) draw_pods(want);
    } else if (s_state == L_WAIT_GO && s_t >= s_hold) {
        s_state = L_GO;
        s_go_us = esp_timer_get_time();
        draw_pods(0);
        show("", "");
    } else if (s_state == L_GO && s_t > 3.0f) {            // no touch for 3 s after lights out
        s_state = L_RESULT;
        show("TOO SLOW", "TOUCH TO TRY AGAIN");
    }
}

static void input(gc_input_t in, lv_point_t at)
{
    (void)at;
    if (in != GC_PRESS) return;
    if (s_state == L_READY || s_state == L_RESULT) {
        s_state = L_ARMING; s_t = 0;
        show("", "WAIT FOR IT...");
        draw_pods(0);
    } else if (s_state == L_ARMING || s_state == L_WAIT_GO) {
        s_state = L_RESULT;
        draw_pods(0);
        show("JUMP START!", "TOUCH TO TRY AGAIN");
    } else if (s_state == L_GO) {
        int ms = (int)((esp_timer_get_time() - s_go_us) / 1000);
        s_state = L_RESULT;
        bool best = gc_record(ms);
        lv_label_set_text_fmt(s_big, "%d MS", ms);
        lv_label_set_text(s_msg, best ? "NEW RECORD!  TOUCH TO GO AGAIN"
                                 : ms < 200 ? "F1 DRIVER!  TOUCH TO GO AGAIN"
                                 : ms < 280 ? "QUICK!  TOUCH TO GO AGAIN"
                                 : "TOUCH TO GO AGAIN");
        best_line();
    }
}

static void icon(lv_color_t *b, int n)
{
    for (int i = 0; i < 5; i++) {
        int cx = n / 2 + (i - 2) * (n / 6);
        gc_rect_in(b, n, n, cx - n / 14, n / 2 - n / 4, cx + n / 14, n / 2 + n / 6, lv_color_hex(0x222222));
        gc_disc_in(b, n, n, cx, n / 2, n / 18, i < 3 ? lv_color_hex(0xFF1A1A) : lv_color_hex(0x3A0606));
    }
}

const game_def_t game_lights = {
    .name = "LIGHTS OUT", .hint = "TOUCH THE MOMENT\nALL 5 LIGHTS GO OUT", .key = "lights", .unit = "MS",
    .lower_is_better = true, .icon = icon, .begin = begin, .frame = frame, .input = input,
};
