// RING DRIFT — made for a round screen: the track runs around the edge of the dial, the car laps it clockwise and
// every touch switches between the inner and the outer lane to miss the cones. Each cone passed is a point, each
// lap +5, and the car keeps getting faster. The track is drawn once into a background copy; each frame only the
// spots under the car and the cones are restored from it and redrawn.

#include "esp_attr.h"
#include <math.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"
#include "esp_heap_caps.h"

#define CX 180
#define CY 180
#define LANE_IN 110
#define LANE_OUT 146
#define TRACK_IN 90
#define TRACK_OUT 166
#define MAX_CONES 10
#define MAX_RECTS 16

typedef enum { D_READY, D_RUN, D_OVER } ring_state_t;
typedef struct { float a; uint8_t lane; bool alive; } cone_t;
typedef struct { int x0, y0, x1, y1; } box_t;

static ring_state_t s_state;
static lv_color_t *s_bg;
static EXT_RAM_BSS_ATTR cone_t s_cone[MAX_CONES];
static EXT_RAM_BSS_ATTR box_t s_prev[MAX_RECTS];
static int s_prev_n;
static float s_angle, s_speed, s_r, s_next_spawn, s_lap_angle;
static int s_lane, s_score;
static lv_obj_t *s_score_l, *s_msg, *s_best_l;

static float lane_r(int lane) { return lane ? LANE_OUT : LANE_IN; }
static float diff(float a, float b) { float d = fmodf(a - b + 540.0f, 360.0f) - 180.0f; return d; }   // a - b in -180..180

static void restore(box_t b)
{
    if (b.x0 < 0) b.x0 = 0;
    if (b.y0 < 0) b.y0 = 0;
    if (b.x1 > GC_W) b.x1 = GC_W;
    if (b.y1 > GC_H) b.y1 = GC_H;
    for (int y = b.y0; y < b.y1; y++) memcpy(gc_buf + y * GC_W + b.x0, s_bg + y * GC_W + b.x0, (b.x1 - b.x0) * sizeof(lv_color_t));
    gc_dirty(b.x0, b.y0, b.x1, b.y1);
}

static void mark(int x, int y, int r)
{
    box_t b = {x - r - 1, y - r - 1, x + r + 2, y + r + 2};
    if (s_prev_n < MAX_RECTS) s_prev[s_prev_n++] = b;
    gc_dirty(b.x0, b.y0, b.x1, b.y1);
}

static void draw_track(void)
{
    lv_color_t *keep = gc_buf;
    gc_buf = s_bg;                                         // draw the track into the background copy
    gc_fill(lv_color_hex(0x000000));
    gc_ring(CX, CY, TRACK_IN, TRACK_OUT, 0, 360, lv_color_hex(0x24242E));
    for (int k = 0; k < 36; k++) {                         // red/white kerbs on both edges
        lv_color_t c = (k & 1) ? lv_color_hex(0xE10600) : lv_color_hex(0xF2F2F2);
        gc_ring(CX, CY, TRACK_OUT, TRACK_OUT + 6, k * 10.0f, k * 10.0f + 10.0f, c);
        gc_ring(CX, CY, TRACK_IN - 6, TRACK_IN, k * 10.0f, k * 10.0f + 10.0f, c);
    }
    for (int k = 0; k < 24; k++)                           // dashed centre line
        gc_ring(CX, CY, 127, 129, k * 15.0f, k * 15.0f + 7.0f, lv_color_hex(0xFFDD00));
    gc_ring(CX, CY, TRACK_IN - 2, TRACK_OUT + 2, 357, 363, lv_color_hex(0xFFFFFF));   // start / finish line
    gc_buf = keep;
    memcpy(gc_buf, s_bg, GC_W * GC_H * sizeof(lv_color_t));
}

static void reset(void)
{
    memset(s_cone, 0, sizeof(s_cone));
    s_angle = 0; s_speed = 90; s_lane = 1; s_r = LANE_OUT; s_next_spawn = 120; s_lap_angle = 0; s_score = 0;
}

static void begin(void)
{
    s_bg = heap_caps_malloc(GC_W * GC_H * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_score_l = gc_label(&ui_font_FontTypoderSize56, 0xFFFFFF, LV_ALIGN_CENTER, 0, -6);
    s_msg = gc_label(&ui_font_FontTypoderSize16, 0x9A9A9A, LV_ALIGN_CENTER, 0, 40);
    s_best_l = gc_label(&ui_font_FontTypoderSize16, 0xFFDD00, LV_ALIGN_CENTER, 0, -44);
    lv_label_set_text_fmt(s_best_l, "BEST %ld", (long)gc_best());
    if (!s_bg) { lv_label_set_text(s_msg, "NOT ENOUGH MEMORY"); return; }
    draw_track();
    reset();
    s_state = D_READY;
    lv_label_set_text(s_score_l, "0");
    lv_label_set_text(s_msg, "TOUCH TO SWITCH LANE\nTOUCH TO START");
    s_prev_n = 0;
}

static void end(void) { if (s_bg) { heap_caps_free(s_bg); s_bg = NULL; } }

static void spawn(void)
{
    for (int i = 0; i < MAX_CONES; i++) {
        if (s_cone[i].alive) continue;
        s_cone[i] = (cone_t){.a = fmodf(s_angle + 150.0f, 360.0f), .lane = (uint8_t)(gc_rand() < 0.5f), .alive = true};
        return;
    }
}

static void over(void)
{
    s_state = D_OVER;
    bool rec = gc_record(s_score);
    lv_label_set_text(s_msg, rec ? "NEW RECORD!\nTOUCH TO GO AGAIN" : "SPUN OUT!\nTOUCH TO GO AGAIN");
    lv_label_set_text_fmt(s_best_l, "BEST %ld", (long)gc_best());
}

static void frame(float dt)
{
    if (!s_bg) return;
    for (int i = 0; i < s_prev_n; i++) restore(s_prev[i]);
    s_prev_n = 0;

    if (s_state == D_RUN) {
        float step = s_speed * dt;
        s_angle = fmodf(s_angle + step, 360.0f);
        s_lap_angle += step;
        if (s_lap_angle >= 360.0f) { s_lap_angle -= 360.0f; s_score += 5; }
        s_speed += 4.0f * dt;                                  // degrees per second, keeps rising
        if (s_speed > 230) s_speed = 230;
        s_next_spawn -= step;
        if (s_next_spawn <= 0) {                               // never closer than the time a lane change takes
            spawn();
            s_next_spawn = 34.0f + gc_rand() * 40.0f;
            if (s_next_spawn < s_speed * 0.32f) s_next_spawn = s_speed * 0.32f;
        }
        for (int i = 0; i < MAX_CONES; i++) {
            cone_t *c = &s_cone[i];
            if (!c->alive) continue;
            float d = diff(c->a, s_angle);
            if (d < -12.0f) { c->alive = false; s_score++; continue; }   // passed
            if (fabsf(d) < 7.0f && fabsf(s_r - lane_r(c->lane)) < 16.0f) { over(); break; }
        }
        lv_label_set_text_fmt(s_score_l, "%d", s_score);
    }
    s_r += (lane_r(s_lane) - s_r) * 0.3f;

    for (int i = 0; i < MAX_CONES; i++) {                    // cones
        if (!s_cone[i].alive) continue;
        float a = s_cone[i].a * 0.0174533f, r = lane_r(s_cone[i].lane);
        int x = CX + (int)(sinf(a) * r), y = CY - (int)(cosf(a) * r);
        gc_disc(x, y, 9, lv_color_hex(0xFF7A00));
        gc_disc(x, y, 4, lv_color_hex(0xFFFFFF));
        mark(x, y, 9);
    }
    float a = s_angle * 0.0174533f;                           // the car, nose pointing along the track
    int x = CX + (int)(sinf(a) * s_r), y = CY - (int)(cosf(a) * s_r);
    gc_disc(x, y, 12, lv_color_hex(0xFFDD00));
    gc_disc(x + (int)(cosf(a) * 6), y + (int)(sinf(a) * 6), 5, lv_color_hex(0x111111));
    mark(x, y, 12);
}

static void input(gc_input_t in, lv_point_t at)
{
    (void)at;
    if (in != GC_PRESS || !s_bg) return;
    if (s_state != D_RUN) {
        if (s_state == D_OVER) { for (int i = 0; i < MAX_CONES; i++) s_cone[i].alive = false; reset(); }
        s_state = D_RUN;
        lv_label_set_text(s_msg, "");
        return;
    }
    s_lane = !s_lane;
}

static void icon(lv_color_t *b, int n)
{
    int c = n / 2;
    gc_ring_in(b, n, n, c, c, n / 2 - 26, n / 2 - 6, 0, 360, lv_color_hex(0x2A2A36));
    for (int k = 0; k < 18; k++)
        gc_ring_in(b, n, n, c, c, n / 2 - 6, n / 2 - 2, k * 20.0f, k * 20.0f + 10.0f, lv_color_hex(0xE10600));
    gc_disc_in(b, n, n, c + (int)(0.7f * (n / 2 - 16)), c - (int)(0.7f * (n / 2 - 16)), 9, lv_color_hex(0xFFDD00));
    gc_disc_in(b, n, n, c - (n / 2 - 16), c, 7, lv_color_hex(0xFF7A00));
    gc_disc_in(b, n, n, c, c + (n / 2 - 22), 7, lv_color_hex(0xFF7A00));
}

const game_def_t game_ring = {
    .name = "RING DRIFT", .hint = "TOUCH TO SWITCH LANE\nMISS THE CONES", .key = "ring", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input, .end = end,
};
