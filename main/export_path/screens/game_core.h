#pragma once
// Games on the gauge's touch screen (no controller): the shared engine every game runs in, and the GAMES page.
// A game draws into one full-screen RGB565 canvas (gc_buf, PSRAM), marks what changed with gc_dirty(), and gets
// taps and swipes. The engine adds the rest: swipe down leaves, the car must stand still (the game closes itself
// when it moves), best scores live in NVS.

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#define GC_W 360
#define GC_H 360

// GC_PRESS: the moment a finger touches (timing games); GC_TAP: released without swiping (games that also use swipes)
typedef enum { GC_PRESS, GC_TAP, GC_SWIPE_UP, GC_SWIPE_LEFT, GC_SWIPE_RIGHT } gc_input_t;

typedef struct {
    const char *name;            // shown on the GAMES page, e.g. "NIGHT RUN"
    const char *hint;            // how to play, one or two short lines
    const char *key;             // NVS key of the best score (<= 15 chars)
    const char *unit;            // after the best score, e.g. "MS" (may be "")
    bool lower_is_better;        // reaction times: the smallest is best
    void (*icon)(lv_color_t *buf, int size);          // paints the GAMES page icon (size x size)
    void (*begin)(void);                               // canvas and screen ready: build labels, reset
    void (*frame)(float dt);                           // every ~30 ms: update and draw
    void (*input)(gc_input_t in, lv_point_t at);       // a tap (with position) or a swipe up/left/right
    void (*end)(void);                                 // optional: free what begin() allocated
} game_def_t;

extern lv_color_t *gc_buf;       // GC_W x GC_H pixels of the running game
extern lv_obj_t *gc_scr;         // the running game's screen (for labels)

// drawing into gc_buf (all clip to the screen)
void gc_fill(lv_color_t c);
void gc_hline(int y, int x0, int x1, lv_color_t c);
void gc_rect(int x0, int y0, int x1, int y1, lv_color_t c);
void gc_disc(int cx, int cy, int r, lv_color_t c);
void gc_ring(int cx, int cy, int r0, int r1, float a0, float a1, lv_color_t c);   // angles in degrees, 0 = up, clockwise
void gc_dirty(int x0, int y0, int x1, int y1);   // up to 16 separate areas per frame (more are merged)
void gc_dirty_all(void);
// the same shapes into any buffer (icons)
void gc_disc_in(lv_color_t *buf, int w, int h, int cx, int cy, int r, lv_color_t c);
void gc_rect_in(lv_color_t *buf, int w, int h, int x0, int y0, int x1, int y1, lv_color_t c);
void gc_ring_in(lv_color_t *buf, int w, int h, int cx, int cy, int r0, int r1, float a0, float a1, lv_color_t c);

lv_obj_t *gc_label(const lv_font_t *font, uint32_t color, lv_align_t align, int x, int y);
float gc_rand(void);
int32_t gc_best(void);                 // best score of the running game (0 = none yet)
bool gc_record(int32_t score);         // stores the score if it beats the best; true when it did
void gc_toast(const char *text);

// Games are installed from the Racing My Car app (GET/POST /ota/games): none are shown until the owner installs
// them. The installed set lives in NVS ("rmc_game"/"installed", bit i = game i of the list in game_core.c).
char *games_list_json(void);              // {"available":["run",…],"installed":[…]} (malloc'd, free() it)
bool games_install_json(const char *json); // {"installed":["run","lights",…]} -> saved; false on bad input

void ui_ScreenPageGames_screen_init(void);   // the GAMES page (page carousel: Gear -> GAMES -> Info)
extern lv_obj_t *ui_ScreenPageGames;
