#pragma once
// Gauge interface language (SETTINGS > LANGUAGE): English or Chinese. Every fixed text of the menu, settings, DATA,
// TEST, update and pairing pages goes through TR(); Chinese glyphs come from the Noto Sans SC fonts that the Typoder
// fonts fall back to (fonts/ui_font_Zh*.c, only the characters the table uses). Theme pages are the theme's own.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { UI_LANG_EN = 0, UI_LANG_ZH = 1 };
uint8_t ui_lang_get(void);
void ui_lang_set(uint8_t lang);          // stored on the gauge (nvs_user_cfg_t.language)
const char *ui_tr(const char *en);       // the Chinese text when the language is Chinese and the table has it, else en
#define TR(s) ui_tr(s)

#ifdef __cplusplus
}
#endif
