#pragma once
// The app menu: round icons in a honeycomb with a fisheye (the middle icon is largest), opened by swiping up on a
// gauge page and closed by swiping down. Which icons it shows, and in what order, is set from the Racing My Car
// platform (GET/POST /ota/menu). It also remembers the theme page the driver settles on (shown again at boot) and
// brings the gauge back to it after a minute without a touch on the menu pages.

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

extern lv_obj_t *ui_ScreenPageMenu;
extern lv_obj_t *ui_ScreenPageMenuSettings;   // SETTINGS (ui_menu_settings.c)
void ui_ScreenPageMenuSettings_screen_init(void);

void ui_menu_init(void);              // once, after the UI is built: starts the idle check
void ui_menu_open(void);              // swipe up on a gauge page
void ui_menu_go_home(void);           // back to the remembered theme page (or the "no theme" page)
void ui_menu_go_home_later(uint32_t ms);
lv_obj_t *ui_menu_home_screen(void);  // that page, built if needed (boot)
void ui_menu_theme_shown(void);       // a theme page was just built: remember it once the driver stays on it
uint8_t ui_menu_saved_theme_index(void);   // boot: index of the remembered theme page (0 if none / gone)

char *menu_config_json(void);         // {"available":[…],"order":[…],"hidden":[…]} (malloc'd, free() it)
bool menu_config_set_json(const char *json);   // {"order":[…],"hidden":[…]} -> saved; false on bad input
