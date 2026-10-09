// Theme-provided gauge page. Entered by boot (if theme is loaded) or by
// swiping down from the Gear page. Which page is built is driven by
// ui_theme_gauge_page_index, set by the gesture handlers before requesting
// a rebuild — see ui_event_theme_gauge_background in ui.c and boot logic in
// ui_ext.c. The actual widget tree comes entirely from theme_create_page();
// this file just wires it into the existing screen/navigation conventions.

#include "../ui.h"
#include "theme_engine/theme_interface.h"
#include "ui_menu.h"

void ui_ScreenPageThemeGauge_screen_init(void)
{
    const char *page_id = theme_page_list_at(ui_theme_gauge_page_index);
    ui_ScreenPageThemeGauge = page_id ? theme_create_page(page_id) : NULL;
    if (!ui_ScreenPageThemeGauge) {
        // Theme unloaded or page missing after all — fall back to a blank
        // screen rather than leaving the pointer NULL (which would make
        // _ui_screen_change() call this init function again on every swipe).
        ui_ScreenPageThemeGauge = lv_obj_create(NULL);
        lv_obj_clear_flag(ui_ScreenPageThemeGauge, LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_add_event_cb(ui_ScreenPageThemeGauge, ui_event_theme_gauge_background, LV_EVENT_GESTURE, NULL);
    // long-press anywhere on the page: delete the theme this page belongs to (ui_ThemeDelete.c)
    lv_obj_add_flag(ui_ScreenPageThemeGauge, LV_OBJ_FLAG_CLICKABLE);
    ui_theme_page_touch_through(ui_ScreenPageThemeGauge);
    lv_obj_add_event_cb(ui_ScreenPageThemeGauge, ui_event_theme_gauge_long_press, LV_EVENT_LONG_PRESSED, NULL);
    ui_menu_theme_shown();   // staying on this page makes it the one shown at boot
}
