// App menu (see ui_menu.h), drawn like a racing dial into one full-screen canvas: a tachometer bezel with a red
// line, the RMC hub in the middle and the icons as glossy livery-coloured balls in a honeycomb ring around it, each
// scaled by its distance from the centre (fisheye). Dragging pans a little and springs back; touching an icon pops
// it and shows its name in the hub; a tap opens it, a tap on the hub or a swipe down goes back to the gauge.
// The platform draws the same menu (web/gauge-menu.js).

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "../ui.h"
#include "../ui_ext.h"
#include "ui_menu.h"
#include "game_core.h"
#include "theme_engine/theme_interface.h"
#include "app_obd_dsp/ota_wifi_server.h"
#include "app_obd_dsp/boot_block_player.h"
#include "app_obd_dsp/boot_media_mount.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs.h"
#include "cJSON.h"

#define MW 360
#define MH 360
#define RING 118           // distance of the icons from the centre
#define BALL 48            // icon (and hub) radius at fisheye scale 1; the symbols are drawn for this radius
#define IDLE_MS 60000      // a minute without a touch on the menu pages -> back to the gauge
#define STAY_MS 4000       // on a theme page this long -> it becomes the page shown at boot

lv_obj_t *ui_ScreenPageMenu;

// ---------- drawing (everything into one RGB565 buffer; pixel centres inside a shape are filled) ----------
static uint8_t s_alpha = 255;      // < 255: shapes blend over what is there (the symbols' drop shadow)

static uint32_t shade(uint32_t c, float f)   // f > 0 towards white, f < 0 towards black
{
    int ch[3] = {(int)(c >> 16) & 255, (int)(c >> 8) & 255, (int)c & 255};
    for (int i = 0; i < 3; i++) ch[i] = (int)lroundf(f > 0 ? ch[i] + (255 - ch[i]) * f : ch[i] * (1 + f));
    return (uint32_t)(ch[0] << 16 | ch[1] << 8 | ch[2]);
}
static inline void put(lv_color_t *b, int x, int y, lv_color_t c)
{
    if (x < 0 || y < 0 || x >= MW || y >= MH) return;
    b[y * MW + x] = s_alpha == 255 ? c : lv_color_mix(c, b[y * MW + x], s_alpha);
}
static void fdisc(lv_color_t *b, float x, float y, float r, uint32_t col)
{
    lv_color_t c = lv_color_hex(col);
    for (int py = (int)floorf(y - r); py <= (int)ceilf(y + r); py++)
        for (int px = (int)floorf(x - r); px <= (int)ceilf(x + r); px++) {
            float dx = px + 0.5f - x, dy = py + 0.5f - y;
            if (dx * dx + dy * dy <= r * r) put(b, px, py, c);
        }
}
static void capsule(lv_color_t *b, float x0, float y0, float x1, float y1, float w, uint32_t col)   // a thick line, round ends
{
    lv_color_t c = lv_color_hex(col);
    float h = w / 2, vx = x1 - x0, vy = y1 - y0, l2 = vx * vx + vy * vy;
    for (int py = (int)floorf(fminf(y0, y1) - h); py <= (int)ceilf(fmaxf(y0, y1) + h); py++)
        for (int px = (int)floorf(fminf(x0, x1) - h); px <= (int)ceilf(fmaxf(x0, x1) + h); px++) {
            float qx = px + 0.5f - x0, qy = py + 0.5f - y0, u = l2 > 0 ? (qx * vx + qy * vy) / l2 : 0;
            if (u < 0) u = 0;
            if (u > 1) u = 1;
            float ex = qx - u * vx, ey = qy - u * vy;
            if (ex * ex + ey * ey <= h * h) put(b, px, py, c);
        }
}
static void arc(lv_color_t *b, float cx, float cy, float r, float w, float a0, float a1, uint32_t col)   // degrees, 0 = up, clockwise
{
    lv_color_t c = lv_color_hex(col);
    float ro = r + w / 2, ri = r - w / 2, span = a1 - a0;
    for (int py = (int)floorf(cy - ro); py <= (int)ceilf(cy + ro); py++)
        for (int px = (int)floorf(cx - ro); px <= (int)ceilf(cx + ro); px++) {
            float dx = px + 0.5f - cx, dy = py + 0.5f - cy, d2 = dx * dx + dy * dy;
            if (d2 > ro * ro || d2 < ri * ri) continue;
            float a = atan2f(dx, -dy) * 57.29578f - a0;
            a = fmodf(a + 720.0f, 360.0f);
            if (a <= span) put(b, px, py, c);
        }
}
static void poly(lv_color_t *b, const float *xy, int n, uint32_t col)     // convex polygon, absolute points
{
    lv_color_t c = lv_color_hex(col);
    float y0 = xy[1], y1 = xy[1];
    for (int i = 1; i < n; i++) { y0 = fminf(y0, xy[2 * i + 1]); y1 = fmaxf(y1, xy[2 * i + 1]); }
    for (int py = (int)floorf(y0); py <= (int)ceilf(y1); py++) {
        float sy = py + 0.5f, lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < n; i++) {
            float ax = xy[2 * i], ay = xy[2 * i + 1], bx = xy[2 * ((i + 1) % n)], by = xy[2 * ((i + 1) % n) + 1];
            if ((sy < ay) == (sy < by)) continue;
            float x = ax + (sy - ay) * (bx - ax) / (by - ay);
            lo = fminf(lo, x); hi = fmaxf(hi, x);
        }
        for (int px = (int)ceilf(lo - 0.5f); px <= (int)floorf(hi - 0.5f); px++) put(b, px, py, c);
    }
}
static void frect(lv_color_t *b, float x0, float y0, float x1, float y1, uint32_t col)
{
    float q[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
    poly(b, q, 4, col);
}
// a glossy ball: light from the top left, a darker lower right, a light rim
static void ball_lit(lv_color_t *b, float x, float y, float r, uint32_t lit_c, uint32_t mid_c, uint32_t dark_c, uint32_t rim_c)
{
    lv_color_t lit = lv_color_hex(lit_c), mid = lv_color_hex(mid_c), dark = lv_color_hex(dark_c), rim = lv_color_hex(rim_c);
    for (int py = (int)floorf(y - r); py <= (int)ceilf(y + r); py++)
        for (int px = (int)floorf(x - r); px <= (int)ceilf(x + r); px++) {
            float dx = px + 0.5f - x, dy = py + 0.5f - y, d = sqrtf(dx * dx + dy * dy);
            if (d > r || px < 0 || py < 0 || px >= MW || py >= MH) continue;
            float g = sqrtf((dx / r + 0.3f) * (dx / r + 0.3f) + (dy / r + 0.4f) * (dy / r + 0.4f)) / 1.4f;
            lv_color_t c = g < 0.55f ? lv_color_mix(mid, lit, (uint8_t)(255 * g / 0.55f))
                                     : lv_color_mix(dark, mid, (uint8_t)(255 * fminf(1, (g - 0.55f) / 0.45f)));
            if (d > r - 2) c = lv_color_mix(rim, c, 204);
            b[py * MW + px] = c;
        }
}
static void ball(lv_color_t *b, float x, float y, float r, uint32_t base)
{
    ball_lit(b, x, y, r, shade(base, 0.38f), base, shade(base, -0.45f), shade(base, 0.45f));
}

// ---------- the symbols (drawn for radius 48 around cx, cy; ink = symbol colour, bg = ball colour or -1 for the shadow) ----------
#define X(v) (cx + (v) * s)
#define Y(v) (cy + (v) * s)
static void glyph_games(lv_color_t *b, float cx, float cy, float s, uint32_t ink, int64_t bg)      // racing steering wheel
{
    arc(b, cx, cy, 26.5f * s, 7 * s, 0, 360, ink);
    fdisc(b, cx, cy, 7 * s, ink);
    capsule(b, X(-24), cy, X(-6), cy, 6 * s, ink);
    capsule(b, X(6), cy, X(24), cy, 6 * s, ink);
    capsule(b, cx, Y(6), cx, Y(24), 6 * s, ink);
    if (bg >= 0) frect(b, X(-2.5f), Y(-30), X(2.5f), Y(-23), 0xFFDD00);    // the yellow top-centre marker
}
static void glyph_settings(lv_color_t *b, float cx, float cy, float s, uint32_t ink, int64_t bg)   // wrench
{
    capsule(b, X(-15), Y(15), X(8), Y(-8), 9 * s, ink);
    fdisc(b, X(12), Y(-12), 12 * s, ink);
    fdisc(b, X(-16), Y(16), 6 * s, ink);
    if (bg >= 0) fdisc(b, X(18), Y(-18), 6.5f * s, (uint32_t)bg);
}
static void glyph_obd(lv_color_t *b, float cx, float cy, float s, uint32_t ink, int64_t bg)        // OBD-II plug + signal
{
    float q[8] = {X(-22), Y(-6), X(22), Y(-6), X(16), Y(16), X(-16), Y(16)};
    poly(b, q, 4, ink);
    if (bg >= 0) {
        for (int i = 0; i < 4; i++) fdisc(b, X(-12 + i * 8), Y(1), 2.6f * s, (uint32_t)bg);
        for (int i = 0; i < 3; i++) fdisc(b, X(-8 + i * 8), Y(9), 2.6f * s, (uint32_t)bg);
    }
    arc(b, cx, Y(-4), 16 * s, 4 * s, -40, 40, ink);
    arc(b, cx, Y(-4), 24 * s, 4 * s, -40, 40, ink);
}
static void glyph_ota(lv_color_t *b, float cx, float cy, float s, uint32_t ink, int64_t bg)        // download arrow at speed
{
    (void)bg;
    capsule(b, X(6), Y(-24), X(6), Y(4), 10 * s, ink);
    float q[6] = {X(-12), Y(0), X(24), Y(0), X(6), Y(22)};
    poly(b, q, 3, ink);
    capsule(b, X(-28), Y(-14), X(-12), Y(-14), 4 * s, ink);
    capsule(b, X(-32), Y(-2), X(-10), Y(-2), 4 * s, ink);
    capsule(b, X(-26), Y(10), X(-14), Y(10), 4 * s, ink);
}
static void glyph_info(lv_color_t *b, float cx, float cy, float s, uint32_t ink, int64_t bg)       // tachometer
{
    arc(b, cx, Y(4), 23.5f * s, 7 * s, -135, 90, ink);
    arc(b, cx, Y(4), 23.5f * s, 7 * s, 90, 135, bg >= 0 ? 0xFF3B30 : ink);
    capsule(b, cx, Y(4), X(16 * 0.766f), Y(4 - 16 * 0.643f), 4 * s, ink);
    fdisc(b, cx, Y(4), 5 * s, ink);
}
static void glyph_boot(lv_color_t *b, float cx, float cy, float s, uint32_t ink, int64_t bg)       // chequered flag
{
    capsule(b, X(-18), Y(-24), X(-18), Y(26), 4 * s, bg >= 0 ? 0x111111 : ink);
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 4; j++) {
            float y = -22 + j * 8 + sinf(i * 1.2f) * 2;
            frect(b, X(-16 + i * 8), Y(y), X(-8 + i * 8), Y(y + 8), bg >= 0 ? ((i + j) % 2 ? 0x111111 : 0xFFFFFF) : ink);
        }
}
#undef X
#undef Y

// ---------- the items ----------
typedef struct {
    const char *key, *name;
    uint32_t color, ink;
    void (*glyph)(lv_color_t *b, float cx, float cy, float s, uint32_t ink, int64_t bg);
    void (*open)(void);
} menu_item_t;

static void open_games(void);
static void open_settings(void);
static void open_obd(void);
static void open_ota(void);
static void open_info(void);
static void open_boot(void);

static const menu_item_t ITEMS[] = {   // racing livery colours
    {"games",    "GAMES",          0xE10600, 0xFFFFFF, glyph_games,    open_games},
    {"settings", "SETTINGS",       0x4A4F5A, 0xFFFFFF, glyph_settings, open_settings},
    {"obd",      "OBD DEVICE",     0x1E6BFF, 0xFFFFFF, glyph_obd,      open_obd},
    {"ota",      "WIFI UPDATE",    0x00A651, 0xFFFFFF, glyph_ota,      open_ota},
    {"info",     "VERSION",        0x6E3BFF, 0xFFFFFF, glyph_info,     open_info},
    {"boot",     "BOOT ANIMATION", 0xFFC400, 0x111111, glyph_boot,     open_boot},
};
#define ITEM_COUNT (int)(sizeof(ITEMS) / sizeof(ITEMS[0]))

// ---------- settings (NVS "rmc_ui") ----------
static int s_order[ITEM_COUNT], s_order_n;   // visible items in order

static int item_index(const char *key)
{
    for (int i = 0; i < ITEM_COUNT; i++) if (strcmp(ITEMS[i].key, key) == 0) return i;
    return -1;
}

// "games,obd,-ota,info,boot": the order, '-' = hidden. Items the stored list lacks (newer firmware) come last.
static void load_order(void)
{
    char text[96] = "";
    size_t len = sizeof(text);
    nvs_handle_t h;
    if (nvs_open("rmc_ui", NVS_READONLY, &h) == ESP_OK) { if (nvs_get_str(h, "menu", text, &len) != ESP_OK) text[0] = 0; nvs_close(h); }
    bool seen[ITEM_COUNT] = {0};
    s_order_n = 0;
    char *save = NULL;
    for (char *tok = strtok_r(text, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        bool hidden = tok[0] == '-';
        int i = item_index(hidden ? tok + 1 : tok);
        if (i < 0 || seen[i]) continue;
        seen[i] = true;
        if (!hidden) s_order[s_order_n++] = i;
    }
    for (int i = 0; i < ITEM_COUNT; i++) if (!seen[i]) s_order[s_order_n++] = i;
}

char *menu_config_json(void)
{
    load_order();
    char text[96] = "";
    size_t len = sizeof(text);
    nvs_handle_t h;
    if (nvs_open("rmc_ui", NVS_READONLY, &h) == ESP_OK) { if (nvs_get_str(h, "menu", text, &len) != ESP_OK) text[0] = 0; nvs_close(h); }
    cJSON *o = cJSON_CreateObject(), *av = cJSON_AddArrayToObject(o, "available"), *ord = cJSON_AddArrayToObject(o, "order"),
          *hid = cJSON_AddArrayToObject(o, "hidden");
    for (int i = 0; i < ITEM_COUNT; i++) cJSON_AddItemToArray(av, cJSON_CreateString(ITEMS[i].key));
    for (int k = 0; k < s_order_n; k++) cJSON_AddItemToArray(ord, cJSON_CreateString(ITEMS[s_order[k]].key));
    char *save = NULL;
    for (char *tok = strtok_r(text, ",", &save); tok; tok = strtok_r(NULL, ",", &save))
        if (tok[0] == '-' && item_index(tok + 1) >= 0) cJSON_AddItemToArray(hid, cJSON_CreateString(tok + 1));
    char *out = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return out;
}

bool menu_config_set_json(const char *json)
{
    cJSON *o = json ? cJSON_Parse(json) : NULL, *ord = o ? cJSON_GetObjectItem(o, "order") : NULL, *hid = o ? cJSON_GetObjectItem(o, "hidden") : NULL;
    if (!cJSON_IsArray(ord)) { cJSON_Delete(o); return false; }
    char text[96] = "";
    bool used[ITEM_COUNT] = {0};
    cJSON *v = NULL;
    cJSON_ArrayForEach(v, ord) {
        int i = cJSON_IsString(v) ? item_index(v->valuestring) : -1;
        if (i < 0 || used[i]) continue;
        used[i] = true;
        bool hidden = false;
        cJSON *hv = NULL;
        if (cJSON_IsArray(hid)) cJSON_ArrayForEach(hv, hid) if (cJSON_IsString(hv) && strcmp(hv->valuestring, ITEMS[i].key) == 0) hidden = true;
        size_t n = strlen(text);
        snprintf(text + n, sizeof(text) - n, "%s%s%s", n ? "," : "", hidden ? "-" : "", ITEMS[i].key);
    }
    cJSON_Delete(o);
    nvs_handle_t h;
    if (nvs_open("rmc_ui", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_str(h, "menu", text) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

// ---------- remembered theme page ----------
static lv_timer_t *s_stay_timer;
static uint8_t s_stay_index;

static void stay_cb(lv_timer_t *t)
{
    lv_timer_del(t);
    s_stay_timer = NULL;
    if (!ui_ScreenPageThemeGauge || lv_scr_act() != ui_ScreenPageThemeGauge || ui_theme_gauge_page_index != s_stay_index) return;
    const char *id = theme_page_list_at(s_stay_index);
    if (!id) return;
    char saved[64] = "";
    size_t len = sizeof(saved);
    nvs_handle_t h;
    if (nvs_open("rmc_ui", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_get_str(h, "theme_page", saved, &len) != ESP_OK || strcmp(saved, id) != 0) {   // write only on a change
        nvs_set_str(h, "theme_page", id);
        nvs_commit(h);
    }
    nvs_close(h);
}

void ui_menu_theme_shown(void)
{
    s_stay_index = ui_theme_gauge_page_index;
    if (s_stay_timer) lv_timer_reset(s_stay_timer);
    else s_stay_timer = lv_timer_create(stay_cb, STAY_MS, NULL);
}

uint8_t ui_menu_saved_theme_index(void)
{
    char saved[64] = "";
    size_t len = sizeof(saved);
    nvs_handle_t h;
    if (nvs_open("rmc_ui", NVS_READONLY, &h) != ESP_OK) return 0;
    esp_err_t err = nvs_get_str(h, "theme_page", saved, &len);
    nvs_close(h);
    if (err != ESP_OK) return 0;
    for (uint8_t i = 0; i < theme_page_list_count(); i++) {
        const char *id = theme_page_list_at(i);
        if (id && strcmp(id, saved) == 0) return i;
    }
    return 0;
}

static lv_obj_t *s_boot_scr;   // boot animation preview (below)

// ---------- going back to the gauge ----------
static void load_screen(lv_obj_t *scr, bool delete_current)
{
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, delete_current);
}

void ui_menu_go_home(void)
{
    lv_obj_t *cur = lv_scr_act();
    // our own screens and the game screens free themselves when deleted; system pages are kept for later
    bool ours = cur == ui_ScreenPageMenu || (gc_scr && cur == gc_scr) || (ui_ScreenPageGames && cur == ui_ScreenPageGames) ||
                (s_boot_scr && cur == s_boot_scr) || (ui_ScreenPageMenuSettings && cur == ui_ScreenPageMenuSettings);
    if (theme_page_list_count() > 0) {
        if (!ui_ScreenPageThemeGauge) ui_ScreenPageThemeGauge_screen_init();   // still there when the menu was opened from it
        load_screen(ui_ScreenPageThemeGauge, ours);
    } else {
        if (!ui_ScreenPageGear) ui_ScreenPageGear_screen_init();
        load_screen(ui_ScreenPageGear, ours);
    }
}

// ---------- the menu screen ----------
static lv_color_t *s_buf, *s_bg;       // the canvas, and the dial background drawn once (gradient + tachometer bezel)
static lv_obj_t *s_canvas, *s_logo, *s_name;
static lv_timer_t *s_anim;
static float s_ox, s_oy, s_start_ox, s_start_oy;
static lv_point_t s_down;
static bool s_pressed, s_moved, s_closing;
static int s_hot = -1;                  // item under the finger (pops, its name shows in the hub)
static const char *s_named;

// fisheye: the middle is largest, the ring a little smaller, the rim small (web/gauge-menu.js fisheye())
static float fisheye(float d)
{
    float k = d <= RING ? 1.15f - 0.2f * d / RING : 0.95f - (d - RING) / 150.0f;
    return k < 0.4f ? 0.4f : k > 1.15f ? 1.15f : k;
}
static void place(float x, float y, float *px, float *py, float *r)
{
    float hx = x + s_ox, hy = y + s_oy, d = sqrtf(hx * hx + hy * hy), pull = 1.0f - 0.06f * (d / 180.0f);
    *px = MW / 2 + hx * pull;
    *py = MH / 2 + hy * pull;
    *r = BALL * fisheye(d);
}
static void icon_pos(int k, float *x, float *y, float *r)   // k-th visible icon on the ring, the first at the top
{
    int n = s_order_n < 6 ? s_order_n : 6;
    float a = (-90.0f + k * 360.0f / n) * 0.0174533f;
    place(cosf(a) * RING, sinf(a) * RING, x, y, r);
}

static void draw_bg(void)
{
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) {
            float dx = x + 0.5f - 180, dy = y + 0.5f - 162, t = sqrtf(dx * dx + dy * dy) / 216.0f;
            s_bg[y * MW + x] = lv_color_mix(lv_color_hex(0x050506), lv_color_hex(0x1D1D24), (uint8_t)(255 * (t > 1 ? 1 : t)));
        }
    for (int i = 0; i < 48; i++) {                       // tachometer bezel, the last ticks red
        float a = (-135.0f + i * 270.0f / 47) * 0.0174533f, r0 = i % 4 == 0 ? 158 : 166;
        uint32_t c = i >= 40 ? 0xFF3B30 : i % 4 == 0 ? 0xD8D8D8 : 0x6A6A6A;
        capsule(s_bg, 180 + sinf(a) * r0, 180 - cosf(a) * r0, 180 + sinf(a) * 175, 180 - cosf(a) * 175, i % 4 == 0 ? 3 : 2, c);
    }
    arc(s_bg, 180, 180, 178, 2, 0, 360, 0xFFDD00);
}

static void draw(void)
{
    if (!s_buf || !s_bg) return;
    memcpy(s_buf, s_bg, MW * MH * sizeof(lv_color_t));
    float hx, hy, hr;
    place(0, 0, &hx, &hy, &hr);                          // the RMC hub
    ball_lit(s_buf, hx, hy, hr, 0x34343A, 0x1E1E22, 0x0B0B0D, 0x34343A);
    arc(s_buf, hx, hy, hr - 3.5f, 3, 0, 360, 0xFFDD00);
    for (int k = 0; k < s_order_n && k < 6; k++) {
        float x, y, r;
        icon_pos(k, &x, &y, &r);
        const menu_item_t *it = &ITEMS[s_order[k]];
        float sc = r / BALL * (s_order[k] == s_hot ? 1.12f : 1.0f);
        ball(s_buf, x, y, BALL * sc, it->color);
        s_alpha = 90;                                    // drop shadow of the symbol
        it->glyph(s_buf, x + 2 * sc, y + 2 * sc, sc, 0x000000, -1);
        s_alpha = 255;
        it->glyph(s_buf, x, y, sc, it->ink, it->color);
    }
    lv_obj_invalidate(s_canvas);
    float h = hr / BALL;                                 // the mark and the label follow the hub
    lv_img_set_zoom(s_logo, (uint16_t)(256 * h * 0.9f));
    lv_obj_set_pos(s_logo, (lv_coord_t)(hx - 42), (lv_coord_t)(hy - 15 - 9 * h));
    const char *name = s_hot >= 0 ? ITEMS[s_hot].name : "MENU";
    if (name != s_named) { s_named = name; lv_label_set_text(s_name, name); }
    lv_obj_set_pos(s_name, (lv_coord_t)(hx - 90), (lv_coord_t)(hy + 8 * h));
}

static void spring(lv_timer_t *t)
{
    (void)t;
    if (s_pressed || (fabsf(s_ox) < 0.5f && fabsf(s_oy) < 0.5f)) {
        if (!s_pressed && (s_ox != 0 || s_oy != 0)) { s_ox = s_oy = 0; draw(); }
        return;
    }
    s_ox *= 0.7f;
    s_oy *= 0.7f;
    draw();
}

static float rubber(float v) { return v > 70 ? 70 + (v - 70) * 0.25f : v < -70 ? -70 + (v + 70) * 0.25f : v; }

#define HUB -2
static int item_at(lv_point_t p)   // item index, HUB, or -1
{
    for (int k = 0; k < s_order_n && k < 6; k++) {
        float x, y, r;
        icon_pos(k, &x, &y, &r);
        if ((p.x - x) * (p.x - x) + (p.y - y) * (p.y - y) <= (r + 6) * (r + 6)) return s_order[k];
    }
    float x, y, r;
    place(0, 0, &x, &y, &r);
    return (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y) <= r * r ? HUB : -1;
}

static void on_menu(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *in = lv_indev_get_act();
    if (code == LV_EVENT_DELETE) {
        if (s_anim) { lv_timer_del(s_anim); s_anim = NULL; }
        if (s_buf) { heap_caps_free(s_buf); s_buf = NULL; }
        if (s_bg) { heap_caps_free(s_bg); s_bg = NULL; }
        ui_ScreenPageMenu = NULL;
        return;
    }
    if (s_closing) return;
    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(in, &s_down);
        s_start_ox = s_ox; s_start_oy = s_oy;
        s_pressed = true; s_moved = false;
        int i = item_at(s_down);
        s_hot = i >= 0 ? i : -1;
        if (s_hot >= 0) draw();
    } else if (code == LV_EVENT_PRESSING && s_pressed) {
        lv_point_t p;
        lv_indev_get_point(in, &p);
        int dx = p.x - s_down.x, dy = p.y - s_down.y;
        if (dx * dx + dy * dy > 144) { s_moved = true; s_hot = -1; }
        if (s_moved) { s_ox = rubber(s_start_ox + dx); s_oy = rubber(s_start_oy + dy); draw(); }
    } else if (code == LV_EVENT_GESTURE) {
        if (lv_indev_get_gesture_dir(in) == LV_DIR_BOTTOM) {
            lv_indev_wait_release(in);
            s_closing = true;
            s_pressed = false;
            ui_menu_go_home();
        }
    } else if (code == LV_EVENT_RELEASED) {
        s_pressed = false;
        int hot = s_hot;
        s_hot = -1;
        if (!s_moved) {
            lv_point_t p;
            lv_indev_get_point(in, &p);
            int i = item_at(p);
            if (i == HUB) { s_closing = true; ui_menu_go_home(); return; }    // the hub: back to the gauge
            if (i >= 0) { s_closing = true; ITEMS[i].open(); return; }
        }
        if (hot >= 0) draw();
    }
}

static void ui_ScreenPageMenu_screen_init(void)
{
    load_order();
    s_ox = s_oy = 0; s_pressed = s_moved = s_closing = false; s_hot = -1; s_named = NULL;
    ui_ScreenPageMenu = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ScreenPageMenu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_ScreenPageMenu, lv_color_hex(0x000000), 0);
    lv_obj_add_flag(ui_ScreenPageMenu, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui_ScreenPageMenu, on_menu, LV_EVENT_ALL, NULL);
    s_buf = heap_caps_malloc(MW * MH * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_bg = heap_caps_malloc(MW * MH * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_canvas = lv_canvas_create(ui_ScreenPageMenu);
    if (s_buf) lv_canvas_set_buffer(s_canvas, s_buf, MW, MH, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    s_logo = lv_img_create(ui_ScreenPageMenu);           // the RMC mark in the hub
    lv_img_set_src(s_logo, &imgRmcMarkSmall);
    lv_img_set_pivot(s_logo, 42, 15);
    lv_obj_clear_flag(s_logo, LV_OBJ_FLAG_CLICKABLE);
    s_name = lv_label_create(ui_ScreenPageMenu);
    lv_obj_set_style_text_font(s_name, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(s_name, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_name, 180);
    lv_obj_clear_flag(s_name, LV_OBJ_FLAG_CLICKABLE);
    s_anim = lv_timer_create(spring, 30, NULL);
    if (s_bg) draw_bg();
    draw();
}

void ui_menu_open(void)
{
    if (ui_ScreenPageMenu) return;
    ui_ScreenPageMenu_screen_init();
    if (!s_buf || !s_bg) { lv_obj_del(ui_ScreenPageMenu); gc_toast("NOT ENOUGH MEMORY"); return; }
    // the gauge page stays, so closing the menu is instant; the boot animation preview and SETTINGS are freed
    lv_obj_t *cur = lv_scr_act();
    load_screen(ui_ScreenPageMenu, (s_boot_scr && cur == s_boot_scr) || (ui_ScreenPageMenuSettings && cur == ui_ScreenPageMenuSettings));
}

// leaving the menu for another page: the theme page is rebuilt on the way back (frees its memory meanwhile)
static void leave_to(lv_obj_t **target, void (*init)(void))
{
    if (ui_ScreenPageThemeGauge) { lv_obj_del(ui_ScreenPageThemeGauge); ui_ScreenPageThemeGauge = NULL; }
    if (!*target) init();
    load_screen(*target, true);
}

static void open_games(void) { leave_to(&ui_ScreenPageGames, ui_ScreenPageGames_screen_init); }
static void open_settings(void) { leave_to(&ui_ScreenPageMenuSettings, ui_ScreenPageMenuSettings_screen_init); }
static void open_obd(void)   { leave_to(&ui_ScreenPageBLEScan, ui_ScreenPageBLEScan_screen_init); }
static void open_ota(void)   { leave_to(&ui_ScreenPageOTAMode, ui_ScreenPageOTAMode_screen_init); }
static void open_info(void)  { leave_to(&ui_ScreenPageEasterEgg, ui_ScreenPageEasterEgg_screen_init); }

// ---------- boot animation preview ----------
static lv_timer_t *s_boot_timer;
static int64_t s_boot_start;

static void boot_stop(void)
{
    if (s_boot_timer) { lv_timer_del(s_boot_timer); s_boot_timer = NULL; }
    boot_block_player_destroy();
}

static void boot_tick(lv_timer_t *t)
{
    (void)t;
    boot_block_player_update((uint32_t)((esp_timer_get_time() - s_boot_start) / 1000));
    if (boot_block_player_is_finished()) { boot_stop(); ui_menu_open(); }
}

static void on_boot(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) { boot_stop(); s_boot_scr = NULL; return; }
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_GESTURE) {   // a tap or a swipe: back to the menu
        lv_indev_wait_release(lv_indev_get_act());
        boot_stop();
        ui_menu_open();
    }
}

static void open_boot(void)
{
    boot_block_player_set_paths("/bootmedia/boot_block.txt", "/bootmedia/boot_block.bin");
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_t *canvas = NULL;
    if (!boot_media_mount() || !boot_block_player_create(scr, &canvas)) {
        lv_obj_del(scr);
        s_closing = false;
        gc_toast("NO BOOT ANIMATION YET");
        return;
    }
    if (canvas) lv_obj_clear_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, on_boot, LV_EVENT_ALL, NULL);
    s_boot_scr = scr;
    s_boot_start = esp_timer_get_time();
    s_boot_timer = lv_timer_create(boot_tick, 33, NULL);
    load_screen(scr, true);   // the menu is deleted; finishing or a tap opens it again
}

// ---------- back to the gauge after a minute without a touch ----------
static void idle_cb(lv_timer_t *t)
{
    (void)t;
    if (lv_disp_get_inactive_time(NULL) < IDLE_MS || ui_ext_showroom_is_active()) return;
    lv_obj_t *cur = lv_scr_act();
    if (ui_ScreenPageOTAMode && cur == ui_ScreenPageOTAMode) return;   // never in the middle of an update
    bool menu_page = cur == ui_ScreenPageMenu || (gc_scr && cur == gc_scr) || (ui_ScreenPageGames && cur == ui_ScreenPageGames) ||
                     (ui_ScreenPageBLEScan && cur == ui_ScreenPageBLEScan) || (ui_ScreenPageEasterEgg && cur == ui_ScreenPageEasterEgg) ||
                     (s_boot_scr && cur == s_boot_scr) || (ui_ScreenPageMenuSettings && cur == ui_ScreenPageMenuSettings);
    if (!menu_page || !cur) return;
    if (s_boot_scr && cur == s_boot_scr) boot_stop();
    lv_disp_trig_activity(NULL);     // one return per idle minute
    ui_menu_go_home();
}

void ui_menu_init(void)
{
    static bool done;
    if (done) return;
    done = true;
    lv_timer_create(idle_cb, 1000, NULL);
}
