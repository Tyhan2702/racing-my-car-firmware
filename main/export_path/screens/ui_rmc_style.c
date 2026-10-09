// Racing My Car look for system pages (see ui_rmc_style.h).
#include "../ui.h"
#include "ui_rmc_style.h"

void rmc_screen(lv_obj_t *scr)
{
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
}

lv_obj_t *rmc_title(lv_obj_t *scr, const char *text, int y)
{
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, text);
    lv_obj_set_style_text_font(t, &ui_font_FontTypoderSize24, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(RMC_YELLOW), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, y);
    return t;
}

lv_obj_t *rmc_card(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(RMC_CARD), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 22, 0);
    lv_obj_set_style_pad_hor(c, 16, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return c;
}

lv_obj_t *rmc_row(lv_obj_t *card, const char *label, int y)
{
    lv_obj_t *l = lv_label_create(card);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(RMC_DIM), 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, y);
    lv_obj_t *v = lv_label_create(card);
    lv_label_set_text(v, "");
    lv_obj_set_style_text_font(v, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(v, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);                          // one line, "..." when too long
    lv_obj_set_size(v, RMC_CARD_W - 32 - 100, lv_font_get_line_height(&ui_font_FontTypoderSize16));
    lv_obj_align(v, LV_ALIGN_TOP_RIGHT, 0, y);
    return v;
}

lv_obj_t *rmc_pill(lv_obj_t *parent, const char *text, bool primary, int w, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, 48);
    lv_obj_set_style_radius(b, 24, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(primary ? RMC_YELLOW : 0x2C2C2E), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(RMC_YELLOW), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 2, LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_ext_click_area(b, 8);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &ui_font_FontTypoderSize20, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(primary ? 0x000000 : 0xFFFFFF), 0);
    lv_obj_center(l);
    return b;
}

lv_obj_t *rmc_hint(lv_obj_t *scr, const char *text)
{
    lv_obj_t *h = lv_label_create(scr);
    lv_label_set_text(h, text);
    lv_obj_set_style_text_font(h, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(h, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(h, LV_ALIGN_BOTTOM_MID, 0, -30);
    return h;
}

lv_obj_t *rmc_spinner(lv_obj_t *parent, int size)
{
    lv_obj_t *s = lv_spinner_create(parent, 1000, 70);
    lv_obj_set_size(s, size, size);
    lv_obj_set_style_arc_color(s, lv_color_hex(RMC_YELLOW), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s, lv_color_hex(0x2C2C2E), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s, 3, LV_PART_MAIN);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
    return s;
}
