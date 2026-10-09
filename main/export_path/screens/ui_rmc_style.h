#pragma once
// Racing My Car look for system pages, the same as the menu and its SETTINGS (ui_menu.c, ui_menu_settings.c): black
// screen, title in RMC yellow, dark rounded cards with grey labels and white values, pill buttons.
#include "lvgl.h"

#define RMC_YELLOW 0xFFDD00
#define RMC_CARD   0x1C1C1E
#define RMC_DIM    0x9A9A9A
#define RMC_GREEN  0x22C55E
#define RMC_CARD_W 252

void rmc_screen(lv_obj_t *scr);                                       // black, not scrollable
lv_obj_t *rmc_title(lv_obj_t *scr, const char *text, int y);          // yellow, centred at the top
lv_obj_t *rmc_card(lv_obj_t *parent, int w, int h);                   // dark rounded panel
lv_obj_t *rmc_row(lv_obj_t *card, const char *label, int y);          // grey label left, returns the white value (right)
lv_obj_t *rmc_pill(lv_obj_t *parent, const char *text, bool primary, int w, lv_event_cb_t cb);   // yellow or dark button
lv_obj_t *rmc_hint(lv_obj_t *scr, const char *text);                  // small grey line at the bottom
lv_obj_t *rmc_spinner(lv_obj_t *parent, int size);                    // yellow busy ring
