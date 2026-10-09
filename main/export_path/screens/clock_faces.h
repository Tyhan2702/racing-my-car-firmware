#pragma once
// Clock and calendar faces, installed from the platform (GET/POST /ota/clocks) and shown with the gauge themes:
// swiping left past the last theme page goes through the installed faces, then on to the version page. The menu's
// TIME button opens the first one. A face the driver stays on is shown again at boot, like a theme page.

#include <stdbool.h>
#include "lvgl.h"

extern lv_obj_t *ui_ScreenPageClock;

int clock_installed_count(void);
void ui_clock_show_installed(int k, bool from_right);   // k-th installed face; from_right: arrived by swiping right
void ui_clock_open_from_menu(void);                     // the first installed face, or the classic one
bool ui_clock_boot(void);                               // boot into the remembered face; true when it did

char *clocks_list_json(void);                // {"available":[…],"installed":[…]} (malloc'd)
bool clocks_install_json(const char *json);  // {"installed":["analog","month",…]} in swipe order
