// Long-press a theme page → "Delete this theme?" → the theme that owns the page is removed from the theme
// package (theme_stack.c) and the gauge restarts, now showing the remaining themes. The last theme cannot be deleted.
// The prompt is an overlay on the top layer so it does not disturb the page underneath while it is open.

#include <string.h>
#include "../ui.h"
#include "esp_system.h"
#include "theme_engine/theme_interface.h"
#include "app_obd_dsp/theme_stack.h"

static lv_obj_t *s_overlay;
static lv_obj_t *s_status;
static char s_page_id[40];

static void close_prompt(void)
{
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = NULL;
        s_status = NULL;
    }
}

static void restart_cb(lv_timer_t *t)
{
    (void)t;
    esp_restart();
}

static void on_cancel(lv_event_t *e)
{
    (void)e;
    close_prompt();
}

static void on_delete(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_add_state(btn, LV_STATE_DISABLED);
    int left = -1;
    bool ok = theme_stack_remove_page_owner(s_page_id, &left);
    if (s_status) {
        lv_label_set_text(s_status, ok ? "Deleted. Restarting..." : left == 0 ? "Last theme: it stays" : "Could not delete");
    }
    if (!ok && left == 0) lv_obj_clear_state(btn, LV_STATE_DISABLED);
    if (ok) {
        lv_timer_t *t = lv_timer_create(restart_cb, 800, NULL);
        lv_timer_set_repeat_count(t, 1);
    }
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, uint32_t color, lv_coord_t x, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, 110, 40);
    lv_obj_align(b, LV_ALIGN_CENTER, x, 62);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(b, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(b, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &ui_font_FontTypoderSize16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

void ui_theme_delete_prompt(void)
{
    if (s_overlay) return;
    const char *page_id = theme_page_list_at(ui_theme_gauge_page_index);
    if (!page_id) return;
    strncpy(s_page_id, page_id, sizeof(s_page_id) - 1);
    s_page_id[sizeof(s_page_id) - 1] = '\0';

    // dim the page and catch every touch while the prompt is open
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_overlay);
    lv_label_set_text(title, "DELETE THEME?");
    lv_obj_set_style_text_font(title, &ui_font_FontTypoderSize16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFDD00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -60);

    s_status = lv_label_create(s_overlay);
    lv_label_set_text_fmt(s_status, "Theme page %u of %u", (unsigned)(ui_theme_gauge_page_index + 1), (unsigned)theme_page_list_count());
    lv_obj_set_style_text_font(s_status, &ui_font_FontTypoderSize16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xAAAAAA), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, -22);

    button(s_overlay, "Cancel", 0x444444, -62, on_cancel);
    button(s_overlay, "Delete", 0xD32F2F, 62, on_delete);
}

// Theme widgets only show data, but LVGL makes many of them clickable by default (bars, plain panels). A press on
// one of those keeps its LONG_PRESSED to itself, and themes are often full-screen, so the page never saw the
// long-press. Making every widget on the page pass touches through lets the page get it wherever it is pressed
// (swipes kept working only because gestures bubble up by default).
void ui_theme_page_touch_through(lv_obj_t *obj)
{
    uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, i);
        lv_obj_clear_flag(child, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(child, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
        ui_theme_page_touch_through(child);
    }
}

void ui_event_theme_gauge_long_press(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        ui_theme_delete_prompt();
    }
}
