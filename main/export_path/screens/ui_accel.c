// TEST from the app menu: acceleration times from the car's own speed (OBD), like a performance meter.
//   0-100 KM/H   splits 0-60 / 60-80 / 80-100
//   60-160 KM/H  splits 60-100 / 100-140 / 140-160 (started from a standstill too, timed from 60)
// A test starts only with the car standing (0 km/h): 3, 2, 1, GO, and the clock runs from the moment the wheels
// turn (no reaction time, like Dragy / VBOX). Speed is asked as fast as the car answers while a test runs and the
// times are interpolated between replies, shown to 0.01 s. Holding a finger on the screen for 5 s and tapping EXIT
// leaves a running test. The best run and the last five of each test are kept (NVS "rmc_accel").
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_attr.h"
#include "nvs.h"
#include "../ui.h"
#include "ui_menu.h"
#include "ui_rmc_style.h"
#include "game_core.h"
#include "bsp_obd_dsp/elm327_ble_client.h"
#include "bsp_obd_dsp/nvs_storage.h"
#include "app_obd_dsp/obd_data_cache.h"

lv_obj_t *ui_ScreenPageAccel;

#define C_RED 0xFF4D4D
#define RUNS 5

typedef struct {
    const char *name, *key;
    uint8_t from;                 // timed from this speed (0: the moment the wheels turn)
    uint8_t mark[3];              // split ends; the last is the finish
    const char *split[3];
} accel_test_t;
static const accel_test_t TESTS[2] = {
    {"0-100 KM/H", "t0", 0, {60, 80, 100}, {"0-60", "60-80", "80-100"}},
    {"60-160 KM/H", "t1", 60, {100, 140, 160}, {"60-100", "100-140", "140-160"}},
};

// times in 0.01 s: [0] total, [1..3] splits
typedef struct {
    uint16_t best[4];
    uint8_t n;                    // runs kept in last[] (newest first)
    uint8_t pad;
    uint16_t last[RUNS][4];
} accel_rec_t;
static accel_rec_t s_rec[2];

static void rec_load(void)
{
    nvs_handle_t h;
    memset(s_rec, 0, sizeof(s_rec));
    if (nvs_open("rmc_accel", NVS_READONLY, &h) != ESP_OK) return;
    for (int i = 0; i < 2; i++) {
        size_t len = sizeof(s_rec[i]);
        if (nvs_get_blob(h, TESTS[i].key, &s_rec[i], &len) != ESP_OK || len != sizeof(s_rec[i])) memset(&s_rec[i], 0, sizeof(s_rec[i]));
    }
    nvs_close(h);
}
static bool rec_add(int t, const uint16_t times[4])   // true: a new best
{
    accel_rec_t *r = &s_rec[t];
    memmove(r->last[1], r->last[0], sizeof(r->last[0]) * (RUNS - 1));
    memcpy(r->last[0], times, sizeof(r->last[0]));
    if (r->n < RUNS) r->n++;
    bool best = !r->best[0] || times[0] < r->best[0];
    if (best) memcpy(r->best, times, sizeof(r->best));
    nvs_handle_t h;
    if (nvs_open("rmc_accel", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, TESTS[t].key, r, sizeof(*r));
        nvs_commit(h);
        nvs_close(h);
    }
    return best;
}

// ---------- speed samples (written by the OBD task, read by the UI timer) ----------
typedef struct { int64_t us; float v; } sample_t;
static EXT_RAM_BSS_ATTR sample_t s_ring[128];
static volatile uint32_t s_head;
static uint32_t s_tail;
static void on_speed(float kmh, int64_t us)
{
    uint32_t h = s_head;
    s_ring[h & 127].us = us;
    s_ring[h & 127].v = kmh;
    s_head = h + 1;
}

// ---------- the TEST page ----------
static lv_obj_t *s_card[2], *s_card_sub[2], *s_status;
static lv_timer_t *s_page_timer;

static bool is_slave(void) { return nvs_cfg_get()->device_role == 1; }
// "" when a test can start, else why not
static const char *not_ready(void)
{
    if (is_slave()) return "USE THE MAIN GAUGE";
    if (!elm327_ble_is_connected()) return "OBD NOT CONNECTED";
    if (obd_data_get_speed() > 0) return "STOP THE CAR TO START";
    return "";
}
static void fmt_time(char *o, size_t ol, uint16_t cs) { snprintf(o, ol, "%u.%02u", cs / 100, cs % 100); }

static void page_refresh(lv_timer_t *t)
{
    (void)t;
    const char *why = not_ready();
    if (!why[0]) lv_label_set_text_fmt(s_status, "READY  %d KM/H", obd_data_get_speed());
    else lv_label_set_text(s_status, why);
    lv_obj_set_style_text_color(s_status, lv_color_hex(!why[0] ? RMC_GREEN : strcmp(why, "STOP THE CAR TO START") ? RMC_DIM : RMC_YELLOW), 0);
    for (int i = 0; i < 2; i++) lv_obj_set_style_border_color(s_card[i], lv_color_hex(!why[0] ? RMC_YELLOW : 0x3A3A3C), 0);
}

static void run_start(int test);
static void on_card(lv_event_t *e)
{
    const char *why = not_ready();
    if (why[0]) { gc_toast(why); return; }
    run_start((int)(intptr_t)lv_event_get_user_data(e));
}
static void history_open(lv_event_t *e);

static void on_page(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) {
        if (s_page_timer) { lv_timer_del(s_page_timer); s_page_timer = NULL; }
        ui_ScreenPageAccel = NULL;
        return;
    }
    if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {         // back to the menu
            lv_indev_wait_release(lv_indev_get_act());
            ui_menu_open();
        }
    }
}

void ui_ScreenPageAccel_screen_init(void)
{
    rec_load();
    lv_obj_t *scr = ui_ScreenPageAccel = lv_obj_create(NULL);
    rmc_screen(scr);
    lv_obj_add_event_cb(scr, on_page, LV_EVENT_ALL, NULL);
    rmc_title(scr, "ACCEL TEST", 40);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *c = s_card[i] = rmc_card(scr, 264, 80);
        lv_obj_align(c, LV_ALIGN_TOP_MID, 0, 84 + i * 92);
        lv_obj_set_style_border_width(c, 1, 0);
        lv_obj_set_style_border_opa(c, LV_OPA_COVER, 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, on_card, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *n = lv_label_create(c);
        lv_label_set_text(n, TESTS[i].name);
        lv_obj_set_style_text_font(n, &ui_font_FontTypoderSize24, 0);
        lv_obj_set_style_text_color(n, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 12);
        lv_obj_t *s = s_card_sub[i] = lv_label_create(c);
        lv_obj_set_style_text_font(s, &ui_font_FontTypoderSize16, 0);
        lv_obj_set_style_text_color(s, lv_color_hex(RMC_DIM), 0);
        lv_obj_align(s, LV_ALIGN_BOTTOM_MID, 0, -12);
        if (s_rec[i].n) {
            char b[12], l[12];
            fmt_time(b, sizeof(b), s_rec[i].best[0]);
            fmt_time(l, sizeof(l), s_rec[i].last[0][0]);
            lv_label_set_text_fmt(s, "BEST %s  LAST %s", b, l);
        } else lv_label_set_text(s, "TAP TO START");
    }
    s_status = lv_label_create(scr);
    lv_obj_set_style_text_font(s_status, &ui_font_FontTypoderSize16, 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 272);
    lv_obj_t *h = rmc_pill(scr, "HISTORY", false, 130, history_open);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 296);
    page_refresh(NULL);
    s_page_timer = lv_timer_create(page_refresh, 300, NULL);
}

// ---------- history ----------
static lv_obj_t *s_hist;
static void on_hist(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) { s_hist = NULL; return; }
    if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
            lv_indev_wait_release(lv_indev_get_act());
            ui_ScreenPageAccel_screen_init();
            lv_scr_load_anim(ui_ScreenPageAccel, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
        }
    }
}
static void hist_draw(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part == LV_PART_ITEMS && dsc->label_dsc) dsc->label_dsc->flag |= LV_TEXT_FLAG_RECOLOR;
}
static void history_open(lv_event_t *e)
{
    (void)e;
    lv_obj_t *scr = s_hist = lv_obj_create(NULL);
    rmc_screen(scr);
    lv_obj_add_event_cb(scr, on_hist, LV_EVENT_ALL, NULL);
    rmc_title(scr, "HISTORY", 40);
    lv_obj_t *t = lv_table_create(scr);
    lv_obj_set_size(t, 290, 252);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 82);
    lv_table_set_col_cnt(t, 1);
    lv_table_set_col_width(t, 0, 290);
    lv_obj_set_style_bg_opa(t, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_style_pad_all(t, 0, 0);
    lv_obj_set_style_bg_color(t, lv_color_hex(0x000000), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_side(t, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    lv_obj_set_style_border_color(t, lv_color_hex(RMC_CARD), LV_PART_ITEMS);
    lv_obj_set_style_border_width(t, 1, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(t, 6, LV_PART_ITEMS);
    lv_obj_set_style_text_font(t, &ui_font_FontTypoderSize16, LV_PART_ITEMS);
    lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, LV_PART_ITEMS);
    lv_obj_set_scrollbar_mode(t, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(t, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(t, hist_draw, LV_EVENT_DRAW_PART_BEGIN, NULL);
    uint16_t row = 0;
    char a[12], b[12], c[12], d[12];
    for (int i = 0; i < 2; i++) {
        const accel_rec_t *r = &s_rec[i];
        lv_table_set_row_cnt(t, row + 1);
        if (r->n) {
            fmt_time(a, sizeof(a), r->best[0]);
            lv_table_set_cell_value_fmt(t, row++, 0, "#FFDD00 %s#\n#8E8E93 BEST# %s S", TESTS[i].name, a);
        } else lv_table_set_cell_value_fmt(t, row++, 0, "#FFDD00 %s#\n#48484A NO RUN YET#", TESTS[i].name);
        for (int k = 0; k < r->n; k++) {
            fmt_time(a, sizeof(a), r->last[k][0]);
            fmt_time(b, sizeof(b), r->last[k][1]);
            fmt_time(c, sizeof(c), r->last[k][2]);
            fmt_time(d, sizeof(d), r->last[k][3]);
            lv_table_set_row_cnt(t, row + 1);
            lv_table_set_cell_value_fmt(t, row++, 0, "%s S\n#8E8E93 %s %s  %s %s#\n#8E8E93 %s %s#", a, TESTS[i].split[0], b,
                                        TESTS[i].split[1], c, TESTS[i].split[2], d);   // a colour code ends at a line end
        }
    }
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);   // deletes the TEST page
}

// ---------- a run ----------
typedef enum { R_COUNT, R_GO, R_RUN, R_DONE, R_FAIL } run_state_t;
static lv_obj_t *s_run, *s_big, *s_mid, *s_small, *s_hint, *s_exit_box, *s_hold_label;
static lv_timer_t *s_run_timer;
static run_state_t s_state;
static int s_test;
static int64_t s_t_start;             // countdown start
static int64_t s_zero_us;             // last sample at a standstill
static int64_t s_t0;                  // timing start (0 = not yet)
static int64_t s_mark_us[4];          // [0]: crossing of 'from' (60-160), [1..3]: the marks
static uint8_t s_marks_done;
static sample_t s_prev, s_first;       // previous sample; first sample in motion
static bool s_have_prev, s_have_first;
static float s_vmax, s_v;
static int64_t s_last_sample_us;
static uint16_t s_times[4];
static bool s_best;

// hold 5 s, then EXIT (as in the games)
#define HOLD_EXIT_US 5000000
#define HOLD_SLOP 30
#define HOLD_GAP_US 400000
#define HOLD_SHOW_US 1000000
static int64_t s_hold_us, s_up_us;
static lv_point_t s_hold_at;
static void hold_stop(void)
{
    s_hold_us = s_up_us = 0;
    if (s_hold_label) { lv_obj_del(s_hold_label); s_hold_label = NULL; }
}
static void hold_show(int64_t held)
{
    if (held < HOLD_SHOW_US) return;
    if (!s_hold_label) {
        s_hold_label = lv_label_create(lv_layer_top());
        lv_obj_set_style_text_font(s_hold_label, &ui_font_FontTypoderSize20, 0);
        lv_obj_set_style_text_color(s_hold_label, lv_color_hex(0x111111), 0);
        lv_obj_set_style_bg_color(s_hold_label, lv_color_hex(RMC_YELLOW), 0);
        lv_obj_set_style_bg_opa(s_hold_label, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(s_hold_label, 14, 0);
        lv_obj_set_style_pad_ver(s_hold_label, 8, 0);
        lv_obj_set_style_radius(s_hold_label, 18, 0);
        lv_obj_align(s_hold_label, LV_ALIGN_TOP_MID, 0, 40);
    }
    int left = (int)((HOLD_EXIT_US - held + 999999) / 1000000);
    lv_label_set_text_fmt(s_hold_label, "EXIT IN %d", left < 1 ? 1 : left);
}

static void run_leave(void)
{
    hold_stop();
    s_exit_box = NULL;
    ui_ScreenPageAccel_screen_init();
    lv_scr_load_anim(ui_ScreenPageAccel, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);   // deletes the run screen
}
static void on_exit_btn(lv_event_t *e)
{
    if (lv_event_get_user_data(e)) { run_leave(); return; }
    if (s_exit_box) { lv_obj_del(s_exit_box); s_exit_box = NULL; }   // the test went on meanwhile
}
static void exit_btn(const char *text, uint32_t bg, uint32_t fg, int x, bool leave)
{
    lv_obj_t *b = lv_obj_create(s_exit_box);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 120, 52);
    lv_obj_align(b, LV_ALIGN_CENTER, x, 46);
    lv_obj_set_style_radius(b, 26, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, on_exit_btn, LV_EVENT_CLICKED, leave ? (void *)1 : NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &ui_font_FontTypoderSize20, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_obj_center(l);
}
static void exit_ask(void)
{
    if (s_exit_box) return;
    s_exit_box = lv_obj_create(s_run);
    lv_obj_remove_style_all(s_exit_box);
    lv_obj_set_size(s_exit_box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_exit_box, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_exit_box, LV_OPA_80, 0);
    lv_obj_add_flag(s_exit_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_exit_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = lv_label_create(s_exit_box);
    lv_label_set_text(t, "EXIT TEST?");
    lv_obj_set_style_text_font(t, &ui_font_FontTypoderSize24, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -30);
    exit_btn("CANCEL", 0x2C2C2E, 0xFFFFFF, -66, false);
    exit_btn("EXIT", RMC_YELLOW, 0x000000, 66, true);
}

static void show(const char *big, const lv_font_t *font, uint32_t color, const char *mid, const char *small, const char *hint)
{
    lv_label_set_text(s_big, big);
    lv_obj_set_style_text_font(s_big, font, 0);
    lv_obj_set_style_text_color(s_big, lv_color_hex(color), 0);
    lv_label_set_text(s_mid, mid);
    lv_label_set_text(s_small, small);
    lv_label_set_text(s_hint, hint);
}

static void finish(bool ok, const char *why)
{
    elm327_set_fast_speed(false);
    elm327_set_speed_tap(NULL);
    hold_stop();
    if (s_exit_box) { lv_obj_del(s_exit_box); s_exit_box = NULL; }
    if (!ok) {
        s_state = R_FAIL;
        show("FAILED", &ui_font_FontTypoderSize44, C_RED, why, "", "TAP TO CONTINUE");
        return;
    }
    s_state = R_DONE;
    const accel_test_t *T = &TESTS[s_test];
    int64_t from = T->from ? s_mark_us[0] : s_t0;
    int64_t prev = from;
    for (int i = 0; i < 3; i++) {
        int64_t d = (s_mark_us[i + 1] - prev + 5000) / 10000;
        s_times[i + 1] = (uint16_t)(d > 65535 ? 65535 : d);
        prev = s_mark_us[i + 1];
    }
    int64_t tot = (s_mark_us[3] - from + 5000) / 10000;
    s_times[0] = (uint16_t)(tot > 65535 ? 65535 : tot);
    s_best = rec_add(s_test, s_times);
    char t[16], a[12], b[12], c[12], sp[96];
    fmt_time(t, sizeof(t), s_times[0]);
    strcat(t, " S");
    fmt_time(a, sizeof(a), s_times[1]);
    fmt_time(b, sizeof(b), s_times[2]);
    fmt_time(c, sizeof(c), s_times[3]);
    snprintf(sp, sizeof(sp), "%s  %s\n%s  %s\n%s  %s", T->split[0], a, T->split[1], b, T->split[2], c);
    show(t, &ui_font_FontTypoderSize56, RMC_YELLOW, s_best ? "NEW BEST" : T->name, sp, "TAP TO CONTINUE");
    lv_obj_set_style_text_color(s_mid, lv_color_hex(s_best ? RMC_GREEN : RMC_DIM), 0);
}

// the time a speed was crossed, between two samples
static int64_t cross(const sample_t *a, const sample_t *b, float x)
{
    if (b->v <= a->v) return b->us;
    return a->us + (int64_t)((x - a->v) / (b->v - a->v) * (float)(b->us - a->us));
}

static void take(const sample_t *s)
{
    const accel_test_t *T = &TESTS[s_test];
    s_v = s->v;
    s_last_sample_us = s->us;
    if (s->v < 0.5f) {
        if (s_state <= R_GO) s_zero_us = s->us;
    } else if (s_state == R_COUNT) {
        finish(false, "FALSE START");                         // moved before GO
        return;
    } else if (!s_t0) {
        // first moments in motion: the start is where the speed line through the first moving sample and one a few
        // km/h later meets 0 (whole km/h steps make two neighbouring samples a poor slope), never before the last 0
        if (!s_have_first) { s_first = *s; s_have_first = true; s_state = R_RUN; }
        else if (s->us > s_first.us && (s->v >= s_first.v + 4.0f || s->us - s_first.us > 600000)) {
            float a = (s->v - s_first.v) / (float)(s->us - s_first.us);   // km/h per µs
            int64_t t0 = a > 0 ? s_first.us - (int64_t)(s_first.v / a) : (s_first.us + s_zero_us) / 2;
            if (s_zero_us && t0 < s_zero_us) t0 = s_zero_us;
            if (t0 > s_first.us) t0 = s_first.us;
            s_t0 = t0;
        }
    }
    if (s_state == R_RUN && s_have_prev) {
        if (T->from && !s_mark_us[0] && s_prev.v < T->from && s->v >= T->from) s_mark_us[0] = cross(&s_prev, s, T->from);
        while (s_marks_done < 3 && s_prev.v < T->mark[s_marks_done] && s->v >= T->mark[s_marks_done]) {
            s_mark_us[1 + s_marks_done] = cross(&s_prev, s, T->mark[s_marks_done]);
            s_marks_done++;
        }
        if (s->v > s_vmax) s_vmax = s->v;
        if (s_marks_done == 3 && s_t0) { finish(true, NULL); return; }
        if (s_vmax - s->v > 8.0f) { finish(false, "SPEED DROPPED"); return; }
    }
    s_prev = *s;
    s_have_prev = true;
}

static void run_tick(lv_timer_t *t)
{
    (void)t;
    int64_t now = esp_timer_get_time();
    if (s_hold_us && s_up_us && now - s_up_us >= HOLD_GAP_US) hold_stop();
    else if (s_hold_us && !s_up_us && s_state <= R_RUN) {
        if (now - s_hold_us >= HOLD_EXIT_US) { hold_stop(); exit_ask(); }
        else hold_show(now - s_hold_us);
    }
    if (s_state >= R_DONE) return;
    uint32_t head = s_head;
    if (head - s_tail > 128) s_tail = head - 128;
    while (s_tail != head && s_state < R_DONE) take(&s_ring[s_tail++ & 127]);
    if (s_state >= R_DONE) return;
    if (!elm327_ble_is_connected() || (s_last_sample_us && now - s_last_sample_us > 2000000)) { finish(false, "NO OBD DATA"); return; }

    char big[16], mid[32];
    const accel_test_t *T = &TESTS[s_test];
    int64_t el = now - s_t_start;
    if (s_state == R_COUNT) {
        if (el >= 3000000) { s_state = R_GO; s_t_start = now; }
        else {
            snprintf(big, sizeof(big), "%d", 3 - (int)(el / 1000000));
            show(big, &ui_font_FontTypoderSize140, 0xFFFFFF, T->name, "", "HOLD TO EXIT");
            return;
        }
    }
    if (s_state == R_GO) {
        if (now - s_t_start > 30000000) { finish(false, "NO START"); return; }
        if (!s_last_sample_us && now - s_t_start > 3000000) { finish(false, "NO OBD DATA"); return; }   // no speed at all
        show("GO", &ui_font_FontTypoderSize56, RMC_GREEN, T->name, "", "HOLD TO EXIT");
        return;
    }
    // running
    int64_t from = T->from ? s_mark_us[0] : s_t0;
    if (now - (s_t0 ? s_t0 : now) > 60000000) { finish(false, "TOO SLOW"); return; }
    snprintf(mid, sizeof(mid), "%d KM/H", (int)(s_v + 0.5f));
    if (!from) {
        if (T->from) snprintf(big, sizeof(big), "%d", (int)(s_v + 0.5f));
        else strcpy(big, "0.00");
        show(big, &ui_font_FontTypoderSize56, 0xFFFFFF, T->from ? "TO 60 KM/H" : mid, "", "HOLD TO EXIT");
        return;
    }
    int64_t cs = (now - from) / 10000;
    snprintf(big, sizeof(big), "%lld.%02lld", (long long)(cs / 100), (long long)(cs % 100));
    char sp[96] = "";
    int64_t prev = from;
    for (int i = 0; i < s_marks_done; i++) {
        int64_t d = (s_mark_us[i + 1] - prev) / 10000;
        prev = s_mark_us[i + 1];
        size_t k = strlen(sp);
        snprintf(sp + k, sizeof(sp) - k, "%s%s  %lld.%02lld", k ? "\n" : "", T->split[i], (long long)(d / 100), (long long)(d % 100));
    }
    show(big, &ui_font_FontTypoderSize56, 0xFFFFFF, mid, sp, "HOLD TO EXIT");
}

static void on_run(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) {
        if (s_run_timer) { lv_timer_del(s_run_timer); s_run_timer = NULL; }
        elm327_set_fast_speed(false);
        elm327_set_speed_tap(NULL);
        hold_stop();
        s_exit_box = NULL;
        s_run = NULL;
        return;
    }
    if (s_exit_box) return;
    if (code == LV_EVENT_PRESSED) {
        lv_point_t p;
        lv_indev_get_point(lv_indev_get_act(), &p);
        int64_t now = esp_timer_get_time();
        if (!(s_hold_us && s_up_us && now - s_up_us < HOLD_GAP_US)) { hold_stop(); s_hold_us = now; s_hold_at = p; }
        s_up_us = 0;
    } else if (code == LV_EVENT_PRESSING) {
        lv_point_t p;
        lv_indev_get_point(lv_indev_get_act(), &p);
        if (s_hold_us && (LV_ABS(p.x - s_hold_at.x) > HOLD_SLOP || LV_ABS(p.y - s_hold_at.y) > HOLD_SLOP)) hold_stop();
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_hold_us) s_up_us = esp_timer_get_time();
        if (code == LV_EVENT_RELEASED && s_state >= R_DONE) run_leave();    // result shown: a tap goes back
    }
}

static lv_obj_t *run_label(const lv_font_t *f, uint32_t color, int y)
{
    lv_obj_t *l = lv_label_create(s_run);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    lv_label_set_text(l, "");
    return l;
}

static void run_start(int test)
{
    s_test = test;
    s_state = R_COUNT;
    s_t_start = esp_timer_get_time();
    s_zero_us = s_t0 = 0;
    memset(s_mark_us, 0, sizeof(s_mark_us));
    s_marks_done = 0;
    s_have_prev = s_have_first = false;
    s_vmax = s_v = 0;
    s_last_sample_us = 0;
    s_tail = s_head;
    hold_stop();
    s_exit_box = NULL;

    lv_obj_t *scr = s_run = lv_obj_create(NULL);
    rmc_screen(scr);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, on_run, LV_EVENT_ALL, NULL);
    s_big = run_label(&ui_font_FontTypoderSize140, 0xFFFFFF, 70);
    lv_obj_align(s_big, LV_ALIGN_CENTER, 0, -40);
    s_mid = run_label(&ui_font_FontTypoderSize24, RMC_DIM, 0);
    lv_obj_align(s_mid, LV_ALIGN_CENTER, 0, 38);
    s_small = run_label(&ui_font_FontTypoderSize16, 0xFFFFFF, 0);
    lv_obj_align(s_small, LV_ALIGN_CENTER, 0, 90);
    s_hint = run_label(&ui_font_FontTypoderSize16, 0x5A5A5E, 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -28);
    elm327_set_speed_tap(on_speed);
    elm327_set_fast_speed(true);
    run_tick(NULL);
    s_run_timer = lv_timer_create(run_tick, 20, NULL);
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 150, 0, true);   // deletes the TEST page
}
