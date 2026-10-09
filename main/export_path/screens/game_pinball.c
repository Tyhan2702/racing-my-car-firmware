// PINBALL — the dial's rim is the table. Touch the left or right half of the screen to flip that flipper. The
// three bumpers score 100, the three targets at the top 250 (all three down: +1000 and they come back). Three balls.
// The ball moves in 6 steps per frame (gravity, the rim, the two magenta guides, the flippers with their kick, the
// bumpers and targets). Everything is redrawn every frame: the radial background as rings of the same colour worked
// out once, the rim arc, the thick lines (guides, flippers) per pixel by their distance to the segment. Same rules,
// sizes and colours as the browser copy (web/game-arcade2.js, pinball).

#include <math.h>
#include <string.h>
#include "../ui.h"
#include "game_core.h"

#define CX 180
#define CY 180
#define RIM 172
#define BR 7                   // the ball's radius
#define G 420.0f               // gravity
#define FL 54                  // flipper length
#define PI_F 3.14159265f
#define MAX_BANDS 64           // background rings of one colour
#define BG_RMAX 256            // beyond the screen's corners

typedef enum { B_READY, B_RUN, B_OVER } pin_state_t;
typedef struct { float x, y, vx, vy; } ball_t;
typedef struct { int r; lv_color_t c; } band_t;   // from radius r (to the next band's r)

static const float PIV[2][2] = {{118, 300}, {242, 300}};
static const float REST[2] = {0.52f, PI_F - 0.52f}, UP[2] = {-0.42f, PI_F + 0.42f};
static const float GUIDES[2][4] = {{30, 262, 118, 300}, {330, 262, 242, 300}};
static const float BUMP[3][3] = {{180, 118, 18}, {124, 172, 15}, {236, 172, 15}};
static const float TGT_X[3] = {150, 180, 210};

static pin_state_t s_state;
static ball_t s_ball;
static float s_ang[2], s_up[2], s_flash[3], s_wait, s_t;
static bool s_target[3];
static int s_score, s_left, s_shown_score;
static band_t s_band[MAX_BANDS];
static int s_n_band;
static lv_obj_t *s_score_l, *s_title, *s_hint;

static inline lv_color_t rgb(uint32_t h) { return lv_color_hex(h); }
static int px(float v) { return (int)floorf(v + 0.5f); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static void rectf(float x, float y, float w, float h, lv_color_t c) { gc_rect(px(x), px(y), px(x + w), px(y + h), c); }
static void span(int y, float xa, float xb, lv_color_t c) { gc_hline(y, (int)ceilf(xa - 0.5f), (int)floorf(xb - 0.5f) + 1, c); }

static void discf(float cx, float cy, float r, lv_color_t c)
{
    for (int y = (int)floorf(cy - r); y <= (int)ceilf(cy + r); y++) {
        float dy = y + 0.5f - cy;
        if (dy * dy > r * r) continue;
        float h = sqrtf(r * r - dy * dy);
        span(y, cx - h, cx + h, c);
    }
}

// a thick line from (x0,y0) to (x1,y1), w wide: round ends (canvas lineCap 'round') or cut square at the ends
static void thick(float x0, float y0, float x1, float y1, float w, bool round, lv_color_t c)
{
    float hw = w / 2, dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
    int xa = (int)floorf(fminf(x0, x1) - hw), xb = (int)ceilf(fmaxf(x0, x1) + hw);
    int ya = (int)floorf(fminf(y0, y1) - hw), yb = (int)ceilf(fmaxf(y0, y1) + hw);
    if (xa < 0) xa = 0;
    if (ya < 0) ya = 0;
    if (xb > GC_W - 1) xb = GC_W - 1;
    if (yb > GC_H - 1) yb = GC_H - 1;
    if (l2 <= 0) return;
    for (int y = ya; y <= yb; y++)
        for (int x = xa; x <= xb; x++) {
            float qx = x + 0.5f - x0, qy = y + 0.5f - y0, u = (qx * dx + qy * dy) / l2;
            if (!round && (u < 0 || u > 1)) continue;
            u = clampf(u, 0, 1);
            float ex = qx - u * dx, ey = qy - u * dy;
            if (ex * ex + ey * ey <= hw * hw) gc_buf[y * GC_W + x] = c;
        }
}

static lv_color_t grad(float r)   // createRadialGradient(CX,CY,20, CX,CY,190): #1c1640 -> #07060f
{
    float k = clampf((r - 20) / 170, 0, 1);
    return lv_color_make((uint8_t)(0x1c + (0x07 - 0x1c) * k + 0.5f), (uint8_t)(0x16 + (0x06 - 0x16) * k + 0.5f),
                         (uint8_t)(0x40 + (0x0f - 0x40) * k + 0.5f));
}

static void make_bands(void)
{
    s_n_band = 0;
    for (int r = 0; r < BG_RMAX; r++) {
        lv_color_t c = grad(r + 0.5f);
        if (s_n_band && s_band[s_n_band - 1].c.full == c.full) continue;
        if (s_n_band == MAX_BANDS) break;
        s_band[s_n_band++] = (band_t){r, c};
    }
}

// the first pixel left of the centre inside radius r on the row whose centre is dy from CY (CX when none)
static int edge(int r, float dy)
{
    float d = (float)r * r - dy * dy;
    if (d <= 0) return CX;
    int e = (int)ceilf(CX - sqrtf(d) - 0.5f);
    return e < 0 ? 0 : e;
}

static void background(void)   // each pixel once: per row, the left part of every band and its mirror
{
    for (int y = 0; y < GC_H; y++) {
        float dy = y + 0.5f - CY;
        int in = CX;                                       // edge of the band inside the current one
        for (int k = 0; k < s_n_band; k++) {
            int out = k + 1 < s_n_band ? edge(s_band[k + 1].r, dy) : 0;
            if (out < in) {
                gc_hline(y, out, in, s_band[k].c);
                gc_hline(y, 2 * CX - in, 2 * CX - out, s_band[k].c);
            }
            in = out;
            if (out == 0) break;
        }
    }
}

static void rim(void)   // 4 px stroke at radius RIM + 2, over the top from the left guide's end to the right one's
{
    const float ro = RIM + 4, ri = RIM, k = 82.0f / hypotf(150, 82);   // the bottom gap: sin(angle) > k
    lv_color_t c = rgb(0xFFDD00);
    for (int y = (int)floorf(CY - ro); y <= (int)ceilf(CY + ro); y++) {
        if (y < 0 || y >= GC_H) continue;
        float dy = y + 0.5f - CY;
        if (fabsf(dy) > ro) continue;
        float ho = sqrtf(ro * ro - dy * dy), hi = fabsf(dy) < ri ? sqrtf(ri * ri - dy * dy) : 0;
        int x0 = (int)floorf(CX - ho), x1 = (int)ceilf(CX - hi);   // the left part; the right one mirrors it
        for (int side = 0; side < 2; side++)
            for (int i = x0; i <= x1; i++) {
                int x = side ? 2 * CX - 1 - i : i;
                if (x < 0 || x >= GC_W) continue;
                float dx = x + 0.5f - CX, d2 = dx * dx + dy * dy;
                if (d2 < ri * ri || d2 > ro * ro) continue;
                if (dy > 0 && dy * dy > k * k * d2) continue;
                gc_buf[y * GC_W + x] = c;
            }
    }
}

static void tip(int i, float *tx, float *ty)
{
    *tx = PIV[i][0] + cosf(s_ang[i]) * FL;
    *ty = PIV[i][1] + sinf(s_ang[i]) * FL;
}

static void new_ball(void)
{
    float x = 180 + (gc_rand() - 0.5f) * 40;            // in this order (as the browser draws them)
    s_ball = (ball_t){.x = x, .y = 60, .vx = (gc_rand() - 0.5f) * 120, .vy = 0};
    s_wait = 0.6f;
}

static void reset(void)
{
    s_score = 0; s_left = 3;
    for (int i = 0; i < 2; i++) { s_ang[i] = REST[i]; s_up[i] = 0; }
    for (int i = 0; i < 3; i++) { s_target[i] = true; s_flash[i] = 0; }
    new_ball();
}

// the ball against a segment: pushed out to BR + 2, bounced, and kicked along the normal by a rising flipper
static void seg(float x0, float y0, float x1, float y1, bool kick)
{
    float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
    float u = clampf(((s_ball.x - x0) * dx + (s_ball.y - y0) * dy) / l2, 0, 1);
    float qx = x0 + u * dx, qy = y0 + u * dy, ex = s_ball.x - qx, ey = s_ball.y - qy, d = hypotf(ex, ey);
    if (d >= BR + 2 || d == 0) return;
    float nx = ex / d, ny = ey / d;
    s_ball.x = qx + nx * (BR + 2); s_ball.y = qy + ny * (BR + 2);
    float vn = s_ball.vx * nx + s_ball.vy * ny;
    if (vn < 0) { s_ball.vx -= 1.55f * vn * nx; s_ball.vy -= 1.55f * vn * ny; }
    if (kick) { float k = fmaxf(0, 560 * u); s_ball.vx += nx * k; s_ball.vy += ny * k; }
}

static void overlay(const char *title, const char *hint)
{
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_hint, hint);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void game_over(void)
{
    s_state = B_OVER;
    bool rec = gc_record(s_score);
    long best = (long)gc_best();
    lv_label_set_text(s_title, rec ? "NEW RECORD!" : "GAME OVER");
    if (best) lv_label_set_text_fmt(s_hint, "SCORE %d   BEST %ld\nTAP TO PLAY AGAIN", s_score, best);
    else lv_label_set_text_fmt(s_hint, "SCORE %d\nTAP TO PLAY AGAIN", s_score);
    lv_obj_clear_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void begin(void)
{
    make_bands();
    s_score_l = gc_label(&ui_font_FontTypoderSize24, 0xFFFFFF, LV_ALIGN_CENTER, 0, 236 - GC_H / 2);
    s_title = gc_label(&ui_font_FontTypoderSize24, 0xFFDD00, LV_ALIGN_CENTER, 0, -30);
    s_hint = gc_label(&ui_font_FontTypoderSize16, 0xFFFFFF, LV_ALIGN_CENTER, 0, 22);
    lv_obj_set_style_bg_color(s_hint, rgb(0x000000), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_hint, 6, 0);
    lv_obj_set_style_radius(s_hint, 10, 0);
    s_t = 0;
    reset();
    s_state = B_READY;
    s_shown_score = -1;
    overlay("PINBALL", "TOUCH LEFT / RIGHT HALF\nTO FLIP\nTOUCH TO START");
}

static void physics(float dt)
{
    for (int s = 0; s < 6; s++) {
        float h = dt / 6;
        s_ball.vy += G * h;
        float sp = hypotf(s_ball.vx, s_ball.vy);
        if (sp > 900) { s_ball.vx *= 900 / sp; s_ball.vy *= 900 / sp; }
        s_ball.x += s_ball.vx * h; s_ball.y += s_ball.vy * h;
        float dx = s_ball.x - CX, dy = s_ball.y - CY, d = hypotf(dx, dy);
        if (d > RIM - BR && s_ball.y < 262) {              // the rim
            float nx = dx / d, ny = dy / d;
            s_ball.x = CX + nx * (RIM - BR); s_ball.y = CY + ny * (RIM - BR);
            float vn = s_ball.vx * nx + s_ball.vy * ny;
            if (vn > 0) { s_ball.vx -= 1.7f * vn * nx; s_ball.vy -= 1.7f * vn * ny; }
        }
        for (int g = 0; g < 2; g++) seg(GUIDES[g][0], GUIDES[g][1], GUIDES[g][2], GUIDES[g][3], false);
        for (int i = 0; i < 2; i++) {
            float tx, ty;
            tip(i, &tx, &ty);
            seg(PIV[i][0], PIV[i][1], tx, ty, s_up[i] > 0.18f);
        }
        for (int i = 0; i < 3; i++) {
            float bx = BUMP[i][0], by = BUMP[i][1], r = BUMP[i][2], ex = s_ball.x - bx, ey = s_ball.y - by, dd = hypotf(ex, ey);
            if (dd < r + BR && dd > 0) {
                float nx = ex / dd, ny = ey / dd;
                s_ball.x = bx + nx * (r + BR); s_ball.y = by + ny * (r + BR);
                s_ball.vx = nx * 420; s_ball.vy = ny * 420;
                s_score += 100; s_flash[i] = 0.15f;
            }
        }
        for (int i = 0; i < 3; i++) {
            if (s_target[i] && fabsf(s_ball.x - TGT_X[i]) < 11 && fabsf(s_ball.y - 64) < 9) {
                s_target[i] = false; s_score += 250; s_ball.vy = fabsf(s_ball.vy);
                if (!s_target[0] && !s_target[1] && !s_target[2]) { s_score += 1000; s_target[0] = s_target[1] = s_target[2] = true; }
            }
        }
        if (s_ball.y > GC_H + 10) {                       // drained
            s_left--;
            if (s_left <= 0) { game_over(); break; }
            new_ball();
            break;
        }
    }
}

static void frame(float dt)
{
    s_t += dt;
    for (int i = 0; i < 2; i++) {
        if (s_up[i] > 0) s_up[i] -= dt;
        float goal = s_up[i] > 0 ? UP[i] : REST[i];
        s_ang[i] += (goal - s_ang[i]) * fminf(1, dt * 30);
    }
    for (int i = 0; i < 3; i++) if (s_flash[i] > 0) s_flash[i] -= dt;
    if (s_state == B_RUN) {
        if (s_wait > 0) s_wait -= dt;
        else physics(dt);
    }

    background();
    rim();
    for (int g = 0; g < 2; g++) thick(GUIDES[g][0], GUIDES[g][1], GUIDES[g][2], GUIDES[g][3], 5, false, rgb(0xFF3DF2));
    for (int i = 0; i < 3; i++) {
        float x = BUMP[i][0], y = BUMP[i][1], r = BUMP[i][2];
        discf(x, y, r + 3, rgb(s_flash[i] > 0 ? 0xFFFFFF : 0xFF3030));
        discf(x, y, r - 3, rgb(0xFFDD00));
        discf(x, y, r - 9, rgb(0xFF3030));
    }
    for (int i = 0; i < 3; i++) rectf(TGT_X[i] - 9, 58, 18, 10, rgb(s_target[i] ? 0x2FD6FF : 0x1F2A40));
    for (int i = 0; i < 2; i++) {
        float tx, ty;
        tip(i, &tx, &ty);
        thick(PIV[i][0], PIV[i][1], tx, ty, 11, true, rgb(0xF2F2F2));
    }
    for (int i = 0; i < 2; i++) discf(PIV[i][0], PIV[i][1], 5, rgb(0xFF3030));
    discf(s_ball.x, s_ball.y, BR, rgb(0xE8E8E8));
    discf(s_ball.x - 2, s_ball.y - 2, 2.5f, rgb(0xFFFFFF));
    for (int i = 0; i < s_left; i++) discf(GC_W / 2 - (s_left - 1) * 9 + i * 18, 262, 5, rgb(0xFFDD00));
    gc_dirty_all();
    if (s_score != s_shown_score) { lv_label_set_text_fmt(s_score_l, "%d", s_score); s_shown_score = s_score; }
}

static void input(gc_input_t in, lv_point_t at)
{
    if (in != GC_PRESS) return;
    if (s_state != B_RUN) {
        if (s_state == B_OVER) reset();
        s_state = B_RUN;
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    s_up[at.x < GC_W / 2 ? 0 : 1] = 0.28f;
}

static void icon(lv_color_t *b, int n)
{
    float c = n / 2.0f;
    for (int y = 0; y < n; y++)                                  // the dark table, lighter in the middle
        for (int x = 0; x < n; x++) {
            float dx = x + 0.5f - c, dy = y + 0.5f - c;
            b[y * n + x] = grad(sqrtf(dx * dx + dy * dy) * 2.2f);
        }
    gc_ring_in(b, n, n, n / 2, n / 2, n / 2 - 6, n / 2 - 3, 240, 480, rgb(0xFFDD00));   // the rim over the top
    gc_disc_in(b, n, n, n / 2, 40, 17, rgb(0xFF3030));            // a bumper
    gc_disc_in(b, n, n, n / 2, 40, 11, rgb(0xFFDD00));
    gc_disc_in(b, n, n, n / 2, 40, 5, rgb(0xFF3030));
    for (int k = 0; k <= 28; k++) {                              // the two flippers, raised and resting
        gc_disc_in(b, n, n, 30 + k, 104 - k / 3, 5, rgb(0xF2F2F2));
        gc_disc_in(b, n, n, n - 30 - k, 104 + k / 2, 5, rgb(0xF2F2F2));
    }
    gc_disc_in(b, n, n, 30, 104, 3, rgb(0xFF3030));
    gc_disc_in(b, n, n, n - 30, 104, 3, rgb(0xFF3030));
    gc_disc_in(b, n, n, 58, 76, 7, rgb(0xE8E8E8));                // the ball
    gc_disc_in(b, n, n, 56, 74, 2, rgb(0xFFFFFF));
}

const game_def_t game_pinball = {
    .name = "PINBALL", .hint = "TOUCH LEFT / RIGHT HALF\nTO FLIP THE FLIPPERS", .key = "pinball", .unit = "",
    .icon = icon, .begin = begin, .frame = frame, .input = input,
};
