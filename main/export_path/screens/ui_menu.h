#pragma once
// The app menu: the RMC mark and eight round buttons (OBD, DATA, TEST, SETTINGS, GAMES, BOOT, UPDATE, VERSION, a fixed order set by
// Racing My Car), opened by swiping up on a theme page and closed by swiping down. It also remembers the theme page the driver settles on (shown again at boot) and
// brings the gauge back to it after a minute without a touch on the menu pages.

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

extern lv_obj_t *ui_ScreenPageMenu;
extern lv_obj_t *ui_ScreenPageMenuSettings;   // SETTINGS (ui_menu_settings.c)
void ui_ScreenPageMenuSettings_screen_init(void);
extern lv_obj_t *ui_ScreenPageObdData;        // DATA: everything the car gives, live (ui_obd_data.c)
void ui_ScreenPageObdData_screen_init(void);
extern lv_obj_t *ui_ScreenPageAccel;          // TEST: 0-100 / 60-160 km/h acceleration times (ui_accel.c)
void ui_ScreenPageAccel_screen_init(void);

void ui_menu_init(void);              // once, after the UI is built: starts the idle check
void ui_menu_open(void);              // swipe up on a gauge page
void ui_menu_go_home(void);           // back to the remembered theme page (or the "no theme" page)
void ui_menu_go_home_later(uint32_t ms);
lv_obj_t *ui_menu_home_screen(void);  // that page, built if needed (boot)
void ui_menu_theme_shown(void);       // a theme page was just built: remember it once the driver stays on it
uint8_t ui_menu_saved_theme_index(void);   // boot: index of the remembered theme page (0 if none / gone)

