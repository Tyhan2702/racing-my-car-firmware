#pragma once
// The app menu: the RMC mark in the middle of a ring of eight round buttons (OBD, DATA, TEST, SETTINGS, GAMES, BOOT, UPDATE, VERSION, a fixed order set by
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
void ui_menu_open(void);              // swipe down on a gauge page
void ui_menu_open_test_from_gauge(void); // swipe up on a gauge page: TEST, whose back returns to the gauge
extern bool ui_accel_from_gauge;
bool ui_accel_ready(void);             // TEST page shows READY (armed): no idle return       // TEST was opened from the gauge (back goes home, not to the menu)
void ui_menu_boot_preview_stop(void); // key-off is about to play: stop the boot animation preview
void ui_menu_go_home(void);           // back to the remembered theme page (or the "no theme" page)
void ui_menu_go_home_later(uint32_t ms);
lv_obj_t *ui_menu_home_screen(void);  // that page, built if needed (boot)
void ui_menu_theme_shown(void);       // a theme page was just built: remember it once the driver stays on it
uint8_t ui_menu_saved_theme_index(void);   // boot: index of the remembered theme page (0 if none / gone)

