// DATA from the app menu: everything the car gives, live, in one list that scrolls up and down, so an owner sees what
// a theme can show before designing one. Values the car does not give read "-". While the page is open the OBD poller
// also walks every PID the car lists (elm327_scan_*), the brand methods of the chosen VEHICLE, the adapter's
// voltmeter and the VIN. A swipe left or right goes back to the menu.
#include <stdio.h>
#include <string.h>
#include "../ui.h"
#include "ui_menu.h"
#include "ui_rmc_style.h"
#include "bsp_obd_dsp/elm327_ble_client.h"
#include "app_obd_dsp/obd_data_cache.h"
#include "app_obd_dsp/obd_pid_table.h"
#include "app_obd_dsp/vehicle_profiles.h"

lv_obj_t *ui_ScreenPageObdData;

enum { D_VEHICLE = 0, D_PROTOCOL, D_VIN, D_BATTERY, D_AFR, D_OIL_T, D_OIL_P, D_GEAR, D_TRANS, D_KNOCK, D_FUEL_L, D_FUEL_P,
       D_DOORS, D_COUNT };
static const char *const s_dname[D_COUNT] = {"VEHICLE", "PROTOCOL", "VIN", "BATTERY", "AIR FUEL RATIO", "OIL TEMP",
                                             "OIL PRESSURE", "GEAR", "TRANS TEMP", "KNOCK RETARD", "FUEL LEVEL",
                                             "FUEL PRESSURE", "DOORS"};
#define MAX_ROWS 240
static int16_t s_row[MAX_ROWS];          // < 0: derived value -(id+1); else a mode 01 PID
static uint16_t s_rows;
static lv_obj_t *s_table, *s_sub;
static lv_timer_t *s_timer;

static void derived(int id, char *o, size_t ol)
{
    const vehicle_profile_t *vp = vehicle_profile_get_active();
    int32_t v;
    strcpy(o, "-");
    switch (id) {
    case D_VEHICLE: snprintf(o, ol, "%s", vp && vp->name ? vp->name : "-"); break;
    case D_PROTOCOL: {
        static const char *const p[] = {"AUTO", "J1850 PWM", "J1850 VPW", "ISO 9141", "KWP SLOW", "KWP FAST",
                                        "CAN 11 500K", "CAN 29 500K", "CAN 11 250K", "CAN 29 250K"};
        uint8_t n = elm327_active_protocol();
        if (elm327_ble_is_connected()) snprintf(o, ol, "%s", n < 10 ? p[n] : "?");
        break;
    }
    case D_VIN: if (elm327_scan_vin()[0]) snprintf(o, ol, "%s", elm327_scan_vin()); break;
    case D_BATTERY: if ((v = obd_data_get_bat_mv()) > 0) snprintf(o, ol, "%.2f V", v / 1000.0f); break;
    case D_AFR: if ((v = obd_data_get_afr_x100()) > 0) snprintf(o, ol, "%.2f", v / 100.0f); break;
    case D_OIL_T: if ((v = obd_data_get_oil_temp()) > -40) snprintf(o, ol, "%ld C", (long)v); break;
    case D_OIL_P: if ((v = obd_data_get_oil_pressure_x10()) >= 0) snprintf(o, ol, "%.1f BAR", v / 10.0f); break;
    case D_GEAR:
        v = obd_data_get_gear();
        if (v == -1) strcpy(o, "R");
        else if (v == 0) strcpy(o, "N");
        else if (v > 0 && v < 127) snprintf(o, ol, "%ld", (long)v);
        break;
    case D_TRANS: if ((v = obd_data_get_ext(OBD_EXT_TRANS_TEMP)) != OBD_EXT_INVALID) snprintf(o, ol, "%ld C", (long)v); break;
    case D_KNOCK: if ((v = obd_data_get_ext(OBD_EXT_KNOCK)) != OBD_EXT_INVALID) snprintf(o, ol, "%.1f DEG", v / 10.0f); break;
    case D_FUEL_L: if ((v = obd_data_get_ext(OBD_EXT_FUEL_LEVEL)) != OBD_EXT_INVALID) snprintf(o, ol, "%ld %%", (long)v); break;
    case D_FUEL_P: if ((v = obd_data_get_ext(OBD_EXT_FUEL_PRESSURE)) != OBD_EXT_INVALID) snprintf(o, ol, "%ld KPA", (long)v); break;
    case D_DOORS:
        if ((v = obd_data_get_doors()) == 0) strcpy(o, "CLOSED");
        else if (v > 0) snprintf(o, ol, "OPEN%s%s%s%s%s", v & 16 ? " D" : "", v & 8 ? " P" : "", v & 4 ? " RR" : "",
                                 v & 2 ? " RL" : "", v & 1 ? " T" : "");
        break;
    }
}

static void add_row(int16_t r, const char *name)
{
    if (s_rows >= MAX_ROWS) return;
    s_row[s_rows] = r;
    lv_table_set_row_cnt(s_table, s_rows + 1);
    lv_table_set_cell_value(s_table, s_rows, 0, name);
    lv_table_set_cell_value(s_table, s_rows, 1, "-");
    s_rows++;
}

static void refresh(lv_timer_t *t)
{
    (void)t;
    // PIDs the car lists that the table has no name for: shown in hex
    for (int pid = 1; pid <= 0xE0; pid++) {
        if (pid % 0x20 == 0 || obd_pid_info((uint8_t)pid) || elm327_scan_supported((uint8_t)pid) != 1) continue;
        bool have = false;
        for (uint16_t i = 0; i < s_rows && !have; i++) have = s_row[i] == pid;
        if (!have) { char n[12]; snprintf(n, sizeof(n), "PID %02X", pid); add_row((int16_t)pid, n); }
    }
    int read = 0, total = 0;
    for (uint16_t i = 0; i < s_rows; i++) {
        char v[40] = "-";
        if (s_row[i] < 0) derived(-s_row[i] - 1, v, sizeof(v));
        else {
            uint8_t d[14], n = 0;
            if (!elm327_scan_get((uint8_t)s_row[i], d, &n) || !obd_pid_format((uint8_t)s_row[i], d, n, v, sizeof(v))) strcpy(v, "-");
        }
        if (s_row[i] != -(D_VEHICLE + 1) && s_row[i] != -(D_PROTOCOL + 1)) { total++; read += strcmp(v, "-") != 0; }
        const char *cur = lv_table_get_cell_value(s_table, i, 1);
        if (!cur || strcmp(cur, v) != 0) lv_table_set_cell_value(s_table, i, 1, v);
    }
    if (!elm327_ble_is_connected()) lv_label_set_text(s_sub, "OBD NOT CONNECTED");
    else lv_label_set_text_fmt(s_sub, "%d OF %d READ", read, total);
}

// "-" in grey, values in white, names in grey
static void on_draw(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part != LV_PART_ITEMS || !dsc->label_dsc) return;
    uint32_t row = dsc->id / 2, col = dsc->id % 2;
    const char *v = lv_table_get_cell_value(s_table, (uint16_t)row, (uint16_t)col);
    bool live = col == 1 && v && strcmp(v, "-") != 0;
    dsc->label_dsc->color = lv_color_hex(live ? 0xFFFFFF : RMC_DIM);
    if (col == 1) dsc->label_dsc->align = LV_TEXT_ALIGN_RIGHT;
}

static void on_screen(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) {
        if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
        elm327_scan_set_active(false);
        ui_ScreenPageObdData = NULL;
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

void ui_ScreenPageObdData_screen_init(void)
{
    lv_obj_t *scr = ui_ScreenPageObdData = lv_obj_create(NULL);
    rmc_screen(scr);
    lv_obj_add_event_cb(scr, on_screen, LV_EVENT_ALL, NULL);
    rmc_title(scr, "LIVE DATA", 30);
    s_sub = lv_label_create(scr);
    lv_obj_set_style_text_font(s_sub, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(s_sub, lv_color_hex(RMC_DIM), 0);
    lv_obj_align(s_sub, LV_ALIGN_TOP_MID, 0, 62);

    s_table = lv_table_create(scr);
    lv_obj_set_size(s_table, 300, 262);
    lv_obj_align(s_table, LV_ALIGN_TOP_MID, 0, 88);
    lv_table_set_col_cnt(s_table, 2);
    lv_table_set_col_width(s_table, 0, 150);
    lv_table_set_col_width(s_table, 1, 150);
    lv_obj_set_style_bg_opa(s_table, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_table, 0, 0);
    lv_obj_set_style_pad_all(s_table, 0, 0);
    lv_obj_set_style_bg_color(s_table, lv_color_hex(0x000000), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_table, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_side(s_table, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_table, lv_color_hex(RMC_CARD), LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_table, 1, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(s_table, 7, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(s_table, 4, LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_table, &ui_font_FontTypoderSize16, LV_PART_ITEMS);
    lv_obj_set_scrollbar_mode(s_table, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_table, LV_DIR_VER);
    lv_obj_add_flag(s_table, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_table, on_draw, LV_EVENT_DRAW_PART_BEGIN, NULL);

    s_rows = 0;
    for (int d = 0; d < D_COUNT; d++) add_row((int16_t)(-(d + 1)), s_dname[d]);
    for (size_t i = 0; i < obd_pid_count(); i++) {
        const obd_pid_info_t *p = obd_pid_at(i);
        add_row(p->pid, p->name);
    }
    elm327_scan_set_active(true);
    refresh(NULL);
    s_timer = lv_timer_create(refresh, 700, NULL);
}
