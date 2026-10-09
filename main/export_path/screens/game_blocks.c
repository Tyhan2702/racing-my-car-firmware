// BLOCK DROP — falling blocks on a 10 x 20 well. Tap the left / right side to move, the middle to turn, swipe
// left / right to move and swipe up to drop. Full rows clear (100 / 300 / 500 / 800 x level), a hard drop scores 2 per
// row, every 10 lines is a level and the blocks fall faster. Same rules as the browser copy (web/game-arcade.js).
// The well and the NEXT box are redrawn every frame; the rest of the screen is drawn once.

#include "esp_attr.h"
#include <string.h>
#include "../ui.h"
#include "game_core.h"

#define CW 10
#define CH 20
#define C 14
#define X0 110
#define Y0 40
#define BG 0x07080C
#define WELL 0x0D0F16
#define NX 264                                 // the NEXT preview
#define NY 96

typedef enum { B_READY, B_RUN, B_OVER } blocks_state_t;
typedef struct { int k, n, x, y; int8_t cx[4], cy[4]; } piece_t;

static const char *const SHAPES[7][4] = {
    {"....", "####", "....", "...."}, {"##", "##"}, {".#.", "###", "..."}, {".##", "##.", "..."},
    {"##.", ".##", "..."}, {"#..", "###", "..."}, {"..#", "###", "..."},
};
static const uint8_t SHAPE_N[7] = {4, 2, 3, 3, 3, 3, 3};
static const uint32_t BCOL[7] = {0x2FD6FF, 0xFFDD00, 0xB04DFF, 0x2FE06B, 0xFF3030, 0x2F6BFF, 0xFF8A00};
static const int SCORES[5] = {0, 100, 300, 500, 800};

static blocks_state_t s_state;
static EXT_RAM_BSS_ATTR int8_t s_grid[CH][CW];                  // -1 empty, else the colour of the block
static piece_t s_cur;
static int s_nxt, s_score, s_lines, s_level, s_shown_score, s_shown_lines, s_shown_level;
static float s_t, s_flash;
static EXT_RAM_BSS_ATTR lv_color_t s_col[7], s_hi[7], s_ghost[7], s_ghost_hi[7];
static lv_obj_t *s_score_l, *s_lines_l, *s_level_l, *s_title, *s_hint;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static int rnd(int n) { int v = (int)(gc_rand() * n); return v >= n ? n - 1 : v; }
static uint32_t mixh(uint32_t a, uint32_t b, float t)   // a blended toward b by t (canvas alpha)
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) out |= (uint32_t)(((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t + 0.5f) << sh;
    return out;
}
static lv_color_t mix(uint32_t a, uint32_t b, float t) { return rgb(mixh(a, b, t)); }
static void fixed_label(int x, int y, uint32_t color, const char *text)   // a label that never changes
{
    lv_label_set_text(gc_label(&ui_font_FontTypoderSize16, color, LV_ALIGN_CENTER, x - GC_W / 2, y - GC_H / 2), text);
}

static int cells_of(int k, int8_t *cx, int8_t *cy)
{
    int m = 0;
    for (int y = 0; y < SHAPE_N[k]; y++)
        for (int x = 0; SHAPES[k][y][x]; x++)
            if (SHAPES[k][y][x] == '#' && m < 4) { cx[m] = (int8_t)x; cy[m] = (int8_t)y; m++; }
    return m;
}

static piece_t piece(int k)
{
    piece_t p = {.k = k, .n = SHAPE_N[k], .x = k == 1 ? 4 : 3, .y = k == 0 ? -1 : 0};
    cells_of(k, p.cx, p.cy);
    return p;
}

static bool hits(const int8_t *cx, const int8_t *cy, int px, int py)
{
    for (int i = 0; i < 4; i++) {
        int gx = px + cx[i], gy = py + cy[i];
        if (gx < 0 || gx >= CW || gy >= CH || (gy >= 0 && s_grid[gy][gx] >= 0)) return true;
    }
    return false;
}

static void overlay(const char *title, const char *hint)
{
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_hint, hint);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void over(void)
{
    s_state = B_OVER;
    bool rec = gc_record(s_score);
    int32_t best = gc_best();
    lv_label_set_text(s_title, rec ? "NEW RECORD!" : "GAME OVER");
    if (best) lv_label_set_text_fmt(s_hint, "SCORE %d   BEST %ld\nTAP TO PLAY AGAIN", s_score, (long)best);
    else lv_label_set_text_fmt(s_hint, "SCORE %d\nTAP TO PLAY AGAIN", s_score);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void spawn(void)
{
    s_cur = piece(s_nxt);
    s_nxt = rnd(7);
    if (hits(s_cur.cx, s_cur.cy, s_cur.x, s_cur.y)) over();   // no room at the top
}

static void reset(void)
{
    memset(s_grid, -1, sizeof(s_grid));
    s_score = 0; s_lines = 0; s_level = 0; s_t = 0;
    s_nxt = rnd(7);
    spawn();
}

static void move(int d) { if (!hits(s_cur.cx, s_cur.cy, s_cur.x + d, s_cur.y)) s_cur.x += d; }

static void turn(void)                         // clockwise, nudged sideways if it does not fit
{
    int8_t rx[4], ry[4];
    for (int i = 0; i < 4; i++) { rx[i] = (int8_t)(s_cur.n - 1 - s_cur.cy[i]); ry[i] = s_cur.cx[i]; }
    static const int kick[5] = {0, -1, 1, -2, 2};
    for (int j = 0; j < 5; j++) {
        if (hits(rx, ry, s_cur.x + kick[j], s_cur.y)) continue;
        memcpy(s_cur.cx, rx, 4); memcpy(s_cur.cy, ry, 4);
        s_cur.x += kick[j];
        return;
    }
}

static void lock(void)
{
    for (int i = 0; i < 4; i++)
        if (s_cur.y + s_cur.cy[i] >= 0) s_grid[s_cur.y + s_cur.cy[i]][s_cur.x + s_cur.cx[i]] = (int8_t)s_cur.k;
    int full = 0, w = CH - 1;                  // keep the rows that are not full, packed to the bottom
    for (int y = CH - 1; y >= 0; y--) {
        bool f = true;
        for (int x = 0; x < CW; x++) if (s_grid[y][x] < 0) { f = false; break; }
        if (f) { full++; continue; }
        if (w != y) memcpy(s_grid[w], s_grid[y], CW);
        w--;
    }
    for (; w >= 0; w--) memset(s_grid[w], -1, CW);
    if (full) {
        s_lines += full;
        s_score += SCORES[full] * (s_level + 1);
        s_level = s_lines / 10;
        s_flash = 0.25f;
    }
    spawn();
}

static void drop(void)
{
    int d = 0;
    while (!hits(s_cur.cx, s_cur.cy, s_cur.x, s_cur.y + 1)) { s_cur.y++; d++; }
    s_score += 2 * d;
    lock();
    s_t = 0;
}

static void cell(int x, int y, int k, int s, bool ghost)
{
    gc_rect(x + 1, y + 1, x + s - 1, y + s - 1, ghost ? s_ghost[k] : s_col[k]);
    gc_rect(x + 1, y + 1, x + s - 1, y + 4, ghost ? s_ghost_hi[k] : s_hi[k]);   // the shine on top
}

static void begin(void)
{
    for (int k = 0; k < 7; k++) {              // block colours, their shine, and the faint landing shadow
        s_col[k] = rgb(BCOL[k]);
        s_hi[k] = mix(BCOL[k], 0xFFFFFF, 0.28f);
        s_ghost[k] = mix(WELL, BCOL[k], 0.22f);
        s_ghost_hi[k] = mix(mixh(WELL, BCOL[k], 0.22f), 0xFFFFFF, 0.28f * 0.22f);
    }
    gc_fill(rgb(BG));
    fixed_label(284, 82, 0x9A9A9A, "NEXT");
    fixed_label(72, 140, 0x9A9A9A, "SCORE");
    fixed_label(72, 196, 0x9A9A9A, "LINES");
    s_score_l = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 72 - GC_W / 2, 160 - GC_H / 2);
    s_lines_l = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 72 - GC_W / 2, 216 - GC_H / 2);
    s_level_l = gc_label(&ui_font_FontTypoderSize16, 0xFFDD00, LV_ALIGN_CENTER, 284 - GC_W / 2, 170 - GC_H / 2);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -20);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 18);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 8, 0);
    s_shown_score = s_shown_lines = s_shown_level = -1;
    s_flash = 0;
    reset();
    s_state = B_READY;
    overlay("BLOCK DROP", "SIDES: MOVE  MIDDLE: TURN\nSWIPE UP: DROP - TAP TO START");
}

static void frame(float dt)
{
    if (s_state == B_RUN) {
        s_t += dt;
        float fall = 0.8f - 0.07f * s_level;
        if (fall < 0.1f) fall = 0.1f;
        if (s_t >= fall) {
            s_t = 0;
            if (hits(s_cur.cx, s_cur.cy, s_cur.x, s_cur.y + 1)) lock();
            else s_cur.y++;
        }
    }
    if (s_flash > 0) s_flash -= dt;

    // the well: frame (yellow while rows clear), the settled blocks, the landing shadow and the falling piece
    gc_rect(X0 - 3, Y0 - 3, X0 + CW * C + 3, Y0 + CH * C + 3, s_flash > 0 ? rgb(0xFFDD00) : rgb(0x2A2A36));
    gc_rect(X0, Y0, X0 + CW * C, Y0 + CH * C, rgb(WELL));
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++)
            if (s_grid[y][x] >= 0) cell(X0 + x * C, Y0 + y * C, s_grid[y][x], C, false);
    if (s_state != B_OVER) {
        int gy = s_cur.y;
        while (!hits(s_cur.cx, s_cur.cy, s_cur.x, gy + 1)) gy++;
        for (int i = 0; i < 4; i++)
            if (gy + s_cur.cy[i] >= 0) cell(X0 + (s_cur.x + s_cur.cx[i]) * C, Y0 + (gy + s_cur.cy[i]) * C, s_cur.k, C, true);
        for (int i = 0; i < 4; i++)
            if (s_cur.y + s_cur.cy[i] >= 0) cell(X0 + (s_cur.x + s_cur.cx[i]) * C, Y0 + (s_cur.y + s_cur.cy[i]) * C, s_cur.k, C, false);
    }
    gc_dirty(X0 - 3, Y0 - 3, X0 + CW * C + 2, Y0 + CH * C + 2);

    gc_rect(NX - 4, NY - 4, NX + 46, NY + 24, rgb(BG));   // the next piece
    int8_t cx[4], cy[4];
    cells_of(s_nxt, cx, cy);
    for (int i = 0; i < 4; i++) cell(NX + cx[i] * 10, NY + cy[i] * 10, s_nxt, 10, false);
    gc_dirty(NX - 4, NY - 4, NX + 45, NY + 23);

    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
    if (s_lines != s_shown_lines) { lv_label_set_text_fmt(s_lines_l, "%d", s_lines); s_shown_lines = s_lines; }
    if (s_level != s_shown_level) { lv_label_set_text_fmt(s_level_l, "LV %d", s_level + 1); s_shown_level = s_level; }
}

static void input(gc_input_t in, lv_point_t at)
{
    if (s_state != B_RUN) {
        if (in != GC_TAP) return;
        if (s_state == B_OVER) reset();
        s_state = B_RUN;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (in == GC_TAP) {
        if (at.x < GC_W / 2 - 40) move(-1);
        else if (at.x > GC_W / 2 + 40) move(1);
        else turn();
    } else if (in == GC_SWIPE_LEFT) move(-1);
    else if (in == GC_SWIPE_RIGHT) move(1);
    else if (in == GC_SWIPE_UP) drop();
}

static void icon(lv_color_t *b, int n)
{
    int c = n / 7, x0 = (n - 5 * c) / 2, y0 = n - 12 - 6 * c;   // a 5 x 6 well
    gc_rect_in(b, n, n, x0 - 3, y0 - 3, x0 + 5 * c + 3, y0 + 6 * c + 3, rgb(0x2A2A36));
    gc_rect_in(b, n, n, x0, y0, x0 + 5 * c, y0 + 6 * c, rgb(WELL));
    static const int8_t cells[][3] = {                 // x, y, colour
        {1, 0, 2}, {0, 1, 2}, {1, 1, 2}, {2, 1, 2},    // a T falling
        {0, 4, 6}, {0, 5, 6}, {1, 5, 6}, {2, 5, 4}, {3, 5, 4}, {3, 4, 4}, {4, 4, 0}, {4, 5, 0}, {2, 4, 1}, {1, 4, 1},
    };
    for (int i = 0; i < (int)(sizeof(cells) / sizeof(cells[0])); i++) {
        int x = x0 + cells[i][0] * c, y = y0 + cells[i][1] * c, k = cells[i][2];
        gc_rect_in(b, n, n, x + 1, y + 1, x + c - 1, y + c - 1, rgb(BCOL[k]));
        gc_rect_in(b, n, n, x + 1, y + 1, x + c - 1, y + 5, mix(BCOL[k], 0xFFFFFF, 0.28f));
    }
}

const game_def_t game_blocks = {
    .name = "BLOCK DROP", .hint = "TAP SIDES TO MOVE, MIDDLE TO TURN\nSWIPE UP TO DROP", .key = "blocks", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
