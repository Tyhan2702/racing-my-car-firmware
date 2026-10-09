// Clock and calendar faces (see clock_faces.h). Each face is a few LVGL objects updated by a 250 ms timer when the
// second changes; the calendar redraws its grid when the day changes. Until the phone has set the clock (the gauge
// lost power and the RTC lost time) the faces show dashes and ask for a sync instead of a wrong time.
// The platform draws the same faces as previews (web/clock-faces.js).

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../ui.h"
#include "clock_faces.h"
#include "ui_menu.h"
#include "theme_engine/theme_interface.h"
#include "app_obd_dsp/gauge_time.h"
#include "nvs.h"
#include "cJSON.h"

#define C_PICK 0xFFDD00
#define C_DIM 0x8E8E93
#define C_LINE 0x3A3A3C
#define STAY_MS 4000
#define PI_F 3.14159265f

lv_obj_t *ui_ScreenPageClock;

static const char *const MON[] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
static const char *const DAY[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};

// ---------- small builders ----------
static lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, uint32_t c, lv_align_t a, int x, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, "");
    lv_obj_align(l, a, x, y);
    return l;
}
static lv_obj_t *hint(lv_obj_t *p, int y)    // shown while the clock is not set
{
    lv_obj_t *l = label(p, &ui_font_FontTypoderSize16, C_PICK, LV_ALIGN_CENTER, 0, y);
    lv_label_set_text(l, "OPEN THE RACING MY CAR\nAPP TO SET THE TIME");
    return l;
}
static lv_obj_t *mark(lv_obj_t *p, int y, uint16_t zoom)
{
    lv_obj_t *m = lv_img_create(p);
    lv_img_set_src(m, &imgRmcMarkSmall);
    lv_img_set_pivot(m, 42, 15);
    lv_img_set_zoom(m, zoom);
    lv_obj_align(m, LV_ALIGN_CENTER, 0, y);
    return m;
}
static void show(lv_obj_t *o, bool on) { if (o) { if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); } }
static void short_date(char *b, size_t n, const struct tm *t)   // "FRI 16 OCT"
{
    snprintf(b, n, "%.3s %d %.3s", DAY[t->tm_wday], t->tm_mday, MON[t->tm_mon]);
}

// ---------- CLASSIC: analog hands, hour ticks, date window ----------
static lv_obj_t *s_hand[3], *s_hint, *s_date, *s_cap;
static lv_point_t s_pts[3][2];
static void hand_points(int i, float deg, float r0, float r1)
{
    float a = deg * PI_F / 180.0f, sx = sinf(a), cy = cosf(a);
    s_pts[i][0] = (lv_point_t){(lv_coord_t)(180 + sx * r0), (lv_coord_t)(180 - cy * r0)};
    s_pts[i][1] = (lv_point_t){(lv_coord_t)(180 + sx * r1), (lv_coord_t)(180 - cy * r1)};
    lv_line_set_points(s_hand[i], s_pts[i], 2);
}
static lv_obj_t *line(lv_obj_t *p, int w, uint32_t c)
{
    lv_obj_t *l = lv_line_create(p);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_color(l, lv_color_hex(c), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_size(l, 360, 360);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}
static void analog_build(lv_obj_t *s)
{
    static lv_point_t ticks[60][2];
    for (int i = 0; i < 60; i++) {
        bool hour = i % 5 == 0;
        float a = i * 6 * PI_F / 180.0f, r0 = hour ? 146 : 160, r1 = 168;
        ticks[i][0] = (lv_point_t){(lv_coord_t)(180 + sinf(a) * r0), (lv_coord_t)(180 - cosf(a) * r0)};
        ticks[i][1] = (lv_point_t){(lv_coord_t)(180 + sinf(a) * r1), (lv_coord_t)(180 - cosf(a) * r1)};
        lv_obj_t *t = line(s, hour ? 5 : 2, hour ? 0xFFFFFF : 0x4A4A4E);
        lv_line_set_points(t, ticks[i], 2);
    }
    mark(s, -72, 180);
    lv_obj_t *win = lv_obj_create(s);
    lv_obj_remove_style_all(win);
    lv_obj_set_size(win, 48, 30);
    lv_obj_align(win, LV_ALIGN_CENTER, 96, 0);
    lv_obj_set_style_radius(win, 8, 0);
    lv_obj_set_style_bg_color(win, lv_color_hex(0x141416), 0);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(win, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_border_width(win, 1, 0);
    s_date = label(win, &ui_font_FontTypoderSize20, C_PICK, LV_ALIGN_CENTER, 0, 1);
    s_hand[0] = line(s, 8, 0xFFFFFF);    // hour
    s_hand[1] = line(s, 6, 0xFFFFFF);    // minute
    s_hand[2] = line(s, 2, C_PICK);      // second
    s_cap = lv_obj_create(s);
    lv_obj_remove_style_all(s_cap);
    lv_obj_set_size(s_cap, 14, 14);
    lv_obj_center(s_cap);
    lv_obj_set_style_radius(s_cap, 7, 0);
    lv_obj_set_style_bg_color(s_cap, lv_color_hex(C_PICK), 0);
    lv_obj_set_style_bg_opa(s_cap, LV_OPA_COVER, 0);
    s_hint = hint(s, 70);
}
static void analog_tick(const struct tm *t, bool ok)
{
    for (int i = 0; i < 3; i++) show(s_hand[i], ok);
    show(s_hint, !ok);
    if (!ok) { lv_label_set_text(s_date, "--"); return; }
    hand_points(0, (t->tm_hour % 12) * 30.0f + t->tm_min * 0.5f, -12, 88);
    hand_points(1, t->tm_min * 6.0f + t->tm_sec * 0.1f, -14, 132);
    hand_points(2, t->tm_sec * 6.0f, -24, 150);
    lv_label_set_text_fmt(s_date, "%d", t->tm_mday);
}

// ---------- BIG DIGITS: hours over minutes ----------
static lv_obj_t *s_hh, *s_mm, *s_line1;
static void digital_build(lv_obj_t *s)
{
    s_line1 = label(s, &ui_font_FontTypoderSize16, C_DIM, LV_ALIGN_TOP_MID, 0, 34);
    s_hh = label(s, &ui_font_FontTypoderSize140, 0xFFFFFF, LV_ALIGN_CENTER, 0, -56);
    s_mm = label(s, &ui_font_FontTypoderSize140, C_PICK, LV_ALIGN_CENTER, 0, 58);
    s_hint = hint(s, 138);
}
static void digital_tick(const struct tm *t, bool ok)
{
    char b[24];
    show(s_hint, !ok);
    if (!ok) { lv_label_set_text(s_hh, ""); lv_label_set_text(s_mm, ""); lv_label_set_text(s_line1, ""); return; }   // the 140 px font has digits only
    lv_label_set_text_fmt(s_hh, "%02d", t->tm_hour);
    lv_label_set_text_fmt(s_mm, "%02d", t->tm_min);
    short_date(b, sizeof(b), t);
    lv_label_set_text(s_line1, b);
}

// ---------- SECONDS RING: seconds run round the rim ----------
static lv_obj_t *s_arc, *s_time, *s_line2;
static void ring_build(lv_obj_t *s)
{
    s_arc = lv_arc_create(s);
    lv_obj_set_size(s_arc, 340, 340);
    lv_obj_center(s_arc);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_range(s_arc, 0, 60);
    lv_obj_set_style_arc_width(s_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0x1C1C1E), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(C_PICK), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_arc, 0, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    mark(s, -64, 180);
    s_time = label(s, &ui_font_FontTypoderSize56, 0xFFFFFF, LV_ALIGN_CENTER, 0, 0);
    s_line2 = label(s, &ui_font_FontTypoderSize20, C_DIM, LV_ALIGN_CENTER, 0, 50);
    s_hint = hint(s, 96);
}
static void ring_tick(const struct tm *t, bool ok)
{
    char b[24];
    show(s_hint, !ok);
    lv_arc_set_value(s_arc, ok ? t->tm_sec : 0);
    if (!ok) { lv_label_set_text(s_time, "--:--"); lv_label_set_text(s_line2, ""); return; }
    lv_label_set_text_fmt(s_time, "%02d:%02d", t->tm_hour, t->tm_min);
    short_date(b, sizeof(b), t);
    lv_label_set_text(s_line2, b);
}

// ---------- MONTH: calendar grid, today in yellow ----------
static lv_obj_t *s_title, *s_cell[42], *s_small;
static int s_shown_day = -1;
static void month_build(lv_obj_t *s)
{
    s_title = label(s, &ui_font_FontTypoderSize20, C_PICK, LV_ALIGN_TOP_MID, 0, 50);
    static const char *const W[] = {"S", "M", "T", "W", "T", "F", "S"};
    for (int c = 0; c < 7; c++) {
        lv_obj_t *l = label(s, &ui_font_FontTypoderSize16, C_DIM, LV_ALIGN_TOP_MID, (c - 3) * 34, 86);
        lv_label_set_text(l, W[c]);
    }
    for (int i = 0; i < 42; i++) {
        lv_obj_t *l = s_cell[i] = label(s, &ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_TOP_MID, (i % 7 - 3) * 34, 114 + i / 7 * 30);
        lv_obj_set_size(l, 28, 26);
        lv_obj_set_style_pad_top(l, 4, 0);
        lv_obj_set_style_radius(l, 13, 0);
        lv_obj_set_style_bg_color(l, lv_color_hex(C_PICK), 0);
    }
    s_small = label(s, &ui_font_FontTypoderSize20, 0xFFFFFF, LV_ALIGN_TOP_MID, 0, 300);
    s_hint = hint(s, 0);
    s_shown_day = -1;
}
static int month_days(int year, int mon)   // mon 0-11
{
    static const int D[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return mon == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 29 : D[mon];
}
static void month_tick(const struct tm *t, bool ok)
{
    show(s_hint, !ok);
    for (int i = 0; i < 42; i++) show(s_cell[i], ok);
    if (!ok) { lv_label_set_text(s_title, "CALENDAR"); lv_label_set_text(s_small, ""); s_shown_day = -1; return; }
    lv_label_set_text_fmt(s_small, "%02d:%02d", t->tm_hour, t->tm_min);
    int key = (t->tm_year * 12 + t->tm_mon) * 32 + t->tm_mday;
    if (key == s_shown_day) return;
    s_shown_day = key;
    lv_label_set_text_fmt(s_title, "%s %d", MON[t->tm_mon], 1900 + t->tm_year);
    int first = ((t->tm_wday - (t->tm_mday - 1) % 7) + 7) % 7, n = month_days(1900 + t->tm_year, t->tm_mon);
    for (int i = 0; i < 42; i++) {
        int d = i - first + 1;
        bool today = d == t->tm_mday, in = d >= 1 && d <= n;
        if (in) lv_label_set_text_fmt(s_cell[i], "%d", d); else lv_label_set_text(s_cell[i], "");
        lv_obj_set_style_bg_opa(s_cell[i], today ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(s_cell[i], lv_color_hex(today ? 0x111111 : (i % 7 == 0 ? C_DIM : 0xFFFFFF)), 0);
    }
}

// ---------- DAY: today's date as a big number ----------
static lv_obj_t *s_wday, *s_dnum, *s_my, *s_dtime;
static void day_build(lv_obj_t *s)
{
    s_wday = label(s, &ui_font_FontTypoderSize24, C_DIM, LV_ALIGN_CENTER, 0, -98);
    s_dnum = label(s, &ui_font_FontTypoderSize140, 0xFFFFFF, LV_ALIGN_CENTER, 0, -14);
    s_my = label(s, &ui_font_FontTypoderSize20, C_PICK, LV_ALIGN_CENTER, 0, 64);
    s_dtime = label(s, &ui_font_FontTypoderSize24, 0xFFFFFF, LV_ALIGN_CENTER, 0, 112);
    s_hint = hint(s, 112);
}
static void day_tick(const struct tm *t, bool ok)
{
    show(s_hint, !ok);
    show(s_dtime, ok);
    if (!ok) { lv_label_set_text(s_wday, "CALENDAR"); lv_label_set_text(s_dnum, ""); lv_label_set_text(s_my, ""); return; }
    lv_label_set_text(s_wday, DAY[t->tm_wday]);
    lv_label_set_text_fmt(s_dnum, "%d", t->tm_mday);
    lv_label_set_text_fmt(s_my, "%s %d", MON[t->tm_mon], 1900 + t->tm_year);
    lv_label_set_text_fmt(s_dtime, "%02d:%02d", t->tm_hour, t->tm_min);
}

// ---------- the faces ----------
typedef struct { const char *key; void (*build)(lv_obj_t *); void (*tick)(const struct tm *, bool); } face_t;
static const face_t FACES[] = {   // NVS keys = platform ids (web/clock-faces.js)
    {"analog",  analog_build,  analog_tick},
    {"digital", digital_build, digital_tick},
    {"ring",    ring_build,    ring_tick},
    {"month",   month_build,   month_tick},
    {"day",     day_build,     day_tick},
};
#define FACE_COUNT (int)(sizeof(FACES) / sizeof(FACES[0]))

static int s_inst[FACE_COUNT], s_inst_n;
static int face_index(const char *key)
{
    for (int i = 0; i < FACE_COUNT; i++) if (strcmp(FACES[i].key, key) == 0) return i;
    return -1;
}
static void load_installed(void)
{
    char text[96] = "";
    size_t len = sizeof(text);
    nvs_handle_t h;
    if (nvs_open("rmc_ui", NVS_READONLY, &h) == ESP_OK) { if (nvs_get_str(h, "clocks", text, &len) != ESP_OK) text[0] = 0; nvs_close(h); }
    s_inst_n = 0;
    char *save = NULL;
    for (char *tok = strtok_r(text, ",", &save); tok && s_inst_n < FACE_COUNT; tok = strtok_r(NULL, ",", &save)) {
        int i = face_index(tok);
        bool dup = false;
        for (int k = 0; k < s_inst_n; k++) dup |= s_inst[k] == i;
        if (i >= 0 && !dup) s_inst[s_inst_n++] = i;
    }
}
int clock_installed_count(void) { load_installed(); return s_inst_n; }

char *clocks_list_json(void)
{
    load_installed();
    cJSON *o = cJSON_CreateObject(), *av = cJSON_AddArrayToObject(o, "available"), *in = cJSON_AddArrayToObject(o, "installed");
    for (int i = 0; i < FACE_COUNT; i++) cJSON_AddItemToArray(av, cJSON_CreateString(FACES[i].key));
    for (int k = 0; k < s_inst_n; k++) cJSON_AddItemToArray(in, cJSON_CreateString(FACES[s_inst[k]].key));
    char *out = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return out;
}
bool clocks_install_json(const char *json)
{
    cJSON *o = json ? cJSON_Parse(json) : NULL, *list = o ? cJSON_GetObjectItem(o, "installed") : NULL;
    if (!cJSON_IsArray(list)) { cJSON_Delete(o); return false; }
    char text[96] = "";
    cJSON *v = NULL;
    cJSON_ArrayForEach(v, list) {
        if (!cJSON_IsString(v) || face_index(v->valuestring) < 0 || strstr(text, v->valuestring)) continue;
        size_t n = strlen(text);
        snprintf(text + n, sizeof(text) - n, "%s%s", n ? "," : "", v->valuestring);
    }
    cJSON_Delete(o);
    nvs_handle_t h;
    if (nvs_open("rmc_ui", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_str(h, "clocks", text) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

// ---------- the clock screen ----------
static int s_face, s_pos;          // face shown; its place among the installed ones (-1: opened from the menu, not installed)
static int s_last_sec = -1;
static lv_timer_t *s_stay;

static void tick(lv_timer_t *t)
{
    lv_obj_t *scr = (lv_obj_t *)t->user_data;
    if (scr != ui_ScreenPageClock) return;
    struct tm now;
    bool ok = gauge_time_local(&now);
    int sec = ok ? now.tm_sec + now.tm_min * 60 : -2;
    if (sec == s_last_sec) return;
    s_last_sec = sec;
    FACES[s_face].tick(&now, ok);
}

static void stay_cb(lv_timer_t *t)   // the driver stayed on this face: show it again at boot
{
    lv_timer_del(t);
    s_stay = NULL;
    if (!ui_ScreenPageClock || lv_scr_act() != ui_ScreenPageClock || s_pos < 0) return;
    char id[24], saved[64] = "";
    snprintf(id, sizeof(id), "clock:%s", FACES[s_face].key);
    size_t len = sizeof(saved);
    nvs_handle_t h;
    if (nvs_open("rmc_ui", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_get_str(h, "theme_page", saved, &len) != ESP_OK || strcmp(saved, id) != 0) { nvs_set_str(h, "theme_page", id); nvs_commit(h); }
    nvs_close(h);
}

static void to_theme_end(void)   // right of the first face: the last theme page (or the Gear page without a theme)
{
    uint8_t n = theme_page_list_count();
    if (n > 0) {
        ui_theme_gauge_page_index = n - 1;
        if (ui_ScreenPageThemeGauge) { lv_obj_del(ui_ScreenPageThemeGauge); ui_ScreenPageThemeGauge = NULL; }
        ui_ScreenPageThemeGauge_screen_init();
        lv_scr_load_anim(ui_ScreenPageThemeGauge, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    } else {
        if (!ui_ScreenPageGear) ui_ScreenPageGear_screen_init();
        lv_scr_load_anim(ui_ScreenPageGear, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    }
}

static void on_clock(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *scr = lv_event_get_current_target(e);
    if (code == LV_EVENT_DELETE) {
        lv_timer_t *t = (lv_timer_t *)lv_obj_get_user_data(scr);
        if (t) lv_timer_del(t);
        if (ui_ScreenPageClock == scr) ui_ScreenPageClock = NULL;
        return;
    }
    if (code != LV_EVENT_GESTURE || scr != ui_ScreenPageClock) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    lv_indev_wait_release(lv_indev_get_act());
    if (dir == LV_DIR_TOP) { ui_menu_open(); return; }
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) return;
    if (s_pos < 0) { ui_menu_go_home(); return; }
    load_installed();
    if (dir == LV_DIR_LEFT) {
        if (s_pos + 1 < s_inst_n) ui_clock_show_installed(s_pos + 1, false);
        else {                                         // past the last face: the version page, as after the themes
            if (!ui_ScreenPageEasterEgg) ui_ScreenPageEasterEgg_screen_init();
            lv_scr_load_anim(ui_ScreenPageEasterEgg, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
        }
    } else {
        if (s_pos > 0) ui_clock_show_installed(s_pos - 1, true);
        else to_theme_end();
    }
}

static void show_face(int face, int pos, bool delete_current)
{
    lv_obj_t *cur = lv_scr_act();
    if (cur == ui_ScreenPageThemeGauge) ui_ScreenPageThemeGauge = NULL;   // deleted with the load below
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    s_face = face;
    s_pos = pos;
    s_last_sec = -1;
    FACES[face].build(scr);
    lv_timer_t *t = lv_timer_create(tick, 250, scr);
    lv_obj_set_user_data(scr, t);
    lv_obj_add_event_cb(scr, on_clock, LV_EVENT_ALL, NULL);
    ui_ScreenPageClock = scr;
    tick(t);
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, delete_current);
    if (s_stay) lv_timer_reset(s_stay); else s_stay = lv_timer_create(stay_cb, STAY_MS, NULL);
}

static bool deletable(lv_obj_t *cur)   // screens that free themselves (or are rebuilt) when deleted
{
    return cur && (cur == ui_ScreenPageThemeGauge || cur == ui_ScreenPageClock || cur == ui_ScreenPageMenu);
}

void ui_clock_show_installed(int k, bool from_right)
{
    (void)from_right;
    load_installed();
    if (k < 0 || k >= s_inst_n) return;
    show_face(s_inst[k], k, deletable(lv_scr_act()));
}

void ui_clock_open_from_menu(void)
{
    load_installed();
    if (s_inst_n) show_face(s_inst[0], 0, deletable(lv_scr_act()));
    else show_face(0, -1, deletable(lv_scr_act()));   // nothing installed yet: the classic face
}

bool ui_clock_boot(void)
{
    char saved[64] = "";
    size_t len = sizeof(saved);
    nvs_handle_t h;
    if (nvs_open("rmc_ui", NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t err = nvs_get_str(h, "theme_page", saved, &len);
    nvs_close(h);
    if (err != ESP_OK || strncmp(saved, "clock:", 6) != 0) return false;
    load_installed();
    int face = face_index(saved + 6);
    for (int k = 0; k < s_inst_n; k++)
        if (s_inst[k] == face) { show_face(face, k, true); return true; }   // the boot logo is deleted
    return false;
}
