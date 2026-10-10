// ================================================================
//  ui_ext.c — hand-written extension logic for ui.c
//
//  Migration plan: gradually move the hand-written logic from ui.c my_timerMain() into this file.
//  Migrate one module at a time (boot_anim / sweep / rpm_warn);
//  move the corresponding static variables along with it, ui.c accesses them via the API in ui_ext.h.
//
//  Migration completed:
//  - disp_item system → ui_disp_item.c/h (data-item metadata and helpers)
//  - rpm warning flash → ui_ext_rpm_flash_tick()
//  - sweep animation → ui_ext_sweep_*()
//  - boot animation (the owner's video) → ui_ext_boot_video_tick() / ui_ext_intro_tick()
// ================================================================

#include "ui_ext.h"
#include "ui.h"
#include "bsp_obd_dsp/nvs_storage.h"
#include "bsp_obd_dsp/espnow_link.h"
#include "bsp_obd_dsp/lcd_driver/ST77916.h"
#include "app_obd_dsp/obd_data_cache.h"
#include "app_obd_dsp/vehicle_profiles.h"
#include "app_obd_dsp/boot_block_player.h"
#include "screens/ui_menu.h"
#include "app_obd_dsp/boot_media_mount.h"
#include "theme_engine/theme_interface.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui_ext";

/* ================================================================
 *  Sweep animation state
 *  Sweep progress only advances inside the LVGL task; the master's espnow TX task
 *  broadcasts it read-only via ui_sweep_get_step().
 *  Values: 0=off, 1~SWEEP_TOTAL=sweep animation running (slaves mirror it as-is),
 * ================================================================ */
static volatile int  s_sweep_step = 0;
static int  s_sweep_bl_last = -1;       // backlight (%) already applied during sweep, -1=not sweeping; write LEDC only on change, restore configured brightness at the end
static bool s_sweep_pending = false;    // BLE connected while the Logo was showing; trigger after the Logo goes away
static bool s_prev_ble_connected = false;

/* ================================================================
 *  Boot / video / intro state
 * ================================================================ */
static volatile int s_intro_step = 0;
static int64_t s_boot_start_us = 0;
static volatile bool s_boot_video_active = false;
static volatile bool s_boot_video_done = false;
static volatile bool s_boot_video_ready = false;
static int64_t s_boot_video_start_us = 0;
static lv_obj_t *s_boot_video_screen = NULL;
static lv_timer_t *s_boot_video_timer = NULL;
static bool    s_boot_done = false;

// Video sync signals between gauges (reuse intro_step)
#define VIDEO_SYNC_READY  250
#define VIDEO_SYNC_PLAY   251

// "NO SIGNAL" overlay state
static lv_obj_t *s_no_signal_lbl = NULL;

// Forward declarations
static void boot_enter_default_page(void);

/* ================================================================
 *  Boot / video helpers
 * ================================================================ */

// Dedicated high-rate timer for video (33ms ≈ 30fps)
static void boot_video_timer_cb(lv_timer_t *t)
{
    if (!s_boot_video_active) return;
    int64_t now_us = esp_timer_get_time();
    uint32_t elapsed_ms = (uint32_t)((now_us - s_boot_video_start_us) / 1000);
    boot_block_player_update(elapsed_ms);
    if (boot_block_player_is_finished()) {
        ESP_LOGD(TAG, "Boot video finished");
        s_boot_video_active = false;
        s_boot_video_ready = false;
        boot_block_player_destroy();
        if (s_boot_video_timer) { lv_timer_del(s_boot_video_timer); s_boot_video_timer = NULL; }

        {
            // go straight to the theme
            s_boot_video_done = true;
            boot_enter_default_page();
            // Keep bootmedia mounted for OTA
            // lv_scr_load_anim(..., true) in boot_enter_default_page already sets auto_del;
            // LVGL frees the old screen automatically once the transition ends. Do NOT lv_obj_del
            // manually here — touching the freed object mid-animation crashes (LoadProhibited).
            s_boot_video_screen = NULL;
        }
    }
}

// Jump to the BLE scan page and mark the boot flow done (shared by slaves not bound to a master, and masters/standalone units with no OBD device configured)
static void boot_goto_ble_scan(void)
{
    if (ui_ScreenPageBLEScan == NULL) ui_ScreenPageBLEScan_screen_init();
    lv_scr_load_anim(ui_ScreenPageBLEScan, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, true);
    ui_ScreenPageLogo = NULL;
    imageLogo = NULL;
    s_boot_done = true;
}

// Enter the default page when boot finishes (called on Logo timeout or after the boot animation completes)
static void boot_enter_default_page(void)
{
    const nvs_user_cfg_t *pg_cfg = nvs_cfg_get();

    // If a custom theme with pages is loaded, boot directly into the first theme page
    uint8_t page_count = theme_page_list_count();
    if (page_count > 0) {
        ui_theme_gauge_page_index = ui_menu_saved_theme_index();   // the theme page the driver last settled on
        if (ui_ScreenPageThemeGauge) {
            lv_obj_del(ui_ScreenPageThemeGauge);
            ui_ScreenPageThemeGauge = NULL;
        }
        ui_ScreenPageThemeGauge_screen_init();
        lv_scr_load_anim(ui_ScreenPageThemeGauge, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, true);
        ui_ScreenPageLogo = NULL;
        imageLogo = NULL;
        s_boot_done = true;
        if(s_sweep_pending) { s_sweep_pending = false; s_sweep_step = 1; }
        return;
    }

    if (pg_cfg->device_role == ESPNOW_ROLE_SLAVE) {
        // Slave: not yet bound to a master → go to the BLE page to pair (ui_ScreenPageBLEScan enters "FIND MASTER" mode automatically per role);
        // if already bound, do nothing and fall through to the same default_page branch as the master, showing gauge data directly.
        const uint8_t *bound_mac = espnow_link_get_bound_master_mac();
        bool bound = (bound_mac[0] | bound_mac[1] | bound_mac[2] | bound_mac[3] | bound_mac[4] | bound_mac[5]) != 0;
        if (!bound) {
            boot_goto_ble_scan();
            return;
        }
    } else if (pg_cfg->ble_device_name[0] == '\0') {
        // First flash with no saved device: go straight to the BLE scan page and let the user pick manually
        boot_goto_ble_scan();
        return;
    }

    // no theme: the "no theme" page (ui_menu.c), never the firmware's own gauge pages (default_page is not used)
    lv_scr_load_anim(ui_menu_home_screen(), LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, true);
    ui_ScreenPageLogo = NULL;
    imageLogo = NULL;
    s_boot_done = true;
    if(s_sweep_pending) { s_sweep_pending = false; s_sweep_step = 1; }  // a sweep deferred while the boot animation played fires now, as the default page loads
}

/* ================================================================
 *  Public state accessors (declared in ui.h, used by espnow/elm327/my_timerMain)
 * ================================================================ */

int  ui_sweep_get_step(void) { return s_sweep_step; }
int  ui_intro_get_step(void) { return s_intro_step; }

// The master's sweep progress arrives via the app_event queue; slaves mirror it as-is (no self-increment; driven by
// the master's per-frame broadcast), keeping the backlight flash in sync with the master.
void ui_sweep_set_from_sync(int sweep_step) {
    s_sweep_step = (sweep_step > 0 && sweep_step <= SWEEP_TOTAL) ? sweep_step : 0;
}

// Boot timing sync between gauges: the master broadcasts s_intro_step, slaves follow (255 = boot done → enter page).
void ui_intro_set_step(int step) {
    s_intro_step = step;
}

/* ================================================================
 *  Sweep API (called from my_timerMain)
 * ================================================================ */

bool ui_ext_sweep_active(void)
{
    return s_sweep_step > 0 && s_sweep_step <= SWEEP_TOTAL;
}

int ui_ext_sweep_get_step(void)
{
    return s_sweep_step;
}

void ui_ext_sweep_trigger(bool ble_now, bool is_slave)
{
    if (is_slave) return;
    if (ble_now && !s_prev_ble_connected) {
        if (s_boot_done) {
            s_sweep_step = 1;       // boot animations all finished, sweep immediately
        } else {
            s_sweep_pending = true; // boot animation still playing; defer and fire when the default page loads
        }
    }
    s_prev_ble_connected = ble_now;
}

float ui_ext_sweep_tick(bool is_slave, uint8_t configured_brightness)
{
    if (!ui_ext_sweep_active()) {
        /* Sweep just ended (both master/slaves on the step→0 tick): restore configured brightness;
           happens together with switching back to real values, done once */
        if (s_sweep_bl_last >= 0) {
            uint8_t d = configured_brightness;
            if (d < 10) d = 100;   // 0/not configured → 100, same as the boot backlight
            Set_Backlight(d);
            s_sweep_bl_last = -1;
        }
        return -1.0f;
    }

    int step = s_sweep_step;
    float ratio;
    if (step <= SWEEP_STEPS_UP) {
        ratio = (float)step / (float)SWEEP_STEPS_UP;    // 0→1 ramp
    } else {
        ratio = 1.0f;   // hold at max (hold phase)
    }

    /* Backlight: sweep+peak hold = minimum; after the peak, one flash (high 0.2s → low 0.2s).
       Write LEDC only on the tick where brightness changes. */
    int bl;
    if      (step <= SWEEP_STEPS_UP + SWEEP_STEPS_HOLD)                     bl = SWEEP_BL_MIN; // ramp+hold
    else if (step <= SWEEP_STEPS_UP + SWEEP_STEPS_HOLD + SWEEP_STEPS_FLASH) bl = SWEEP_BL_MAX; // high flash segment
    else                                                                   bl = SWEEP_BL_MIN; // low flash segment
    if (bl != s_sweep_bl_last) { Set_Backlight(bl); s_sweep_bl_last = bl; }

    if (!is_slave) {   // master advances itself; the slave's step is set by the master's broadcast, no self-increment (stays in sync)
        s_sweep_step++;
        if (s_sweep_step > SWEEP_TOTAL) s_sweep_step = 0; // animation done
    }
    return ratio;
}

/* ================================================================
 *  Boot animation API (called from my_timerMain)
 * ================================================================ */

// Video boot mode (INTRO == 2): prepare/play the boot_block video, syncing master/slaves.
// Returns true when my_timerMain should return early (still showing the Logo or the video is playing).
bool ui_ext_boot_video_tick(void)
{
    uint8_t intro_val = nvs_intro_enable_get();
    bool want_video = (intro_val == 2);
    if (s_boot_done || s_boot_video_done || !want_video) return false;

    // show the Logo for 1 second first, then enter the video
    static int64_t s_video_logo_start_us = 0;
    if (s_video_logo_start_us == 0) s_video_logo_start_us = esp_timer_get_time();
    int64_t logo_el_ms = (esp_timer_get_time() - s_video_logo_start_us) / 1000;
    if (logo_el_ms < 1000) return true;  // Logo still showing, skip

    // Phase 1: prepare the video (single app-flashed boot_block slot)
    if (!s_boot_video_ready && !s_boot_video_active && s_boot_video_screen == NULL) {
        boot_block_player_set_paths("/bootmedia/boot_block.txt", "/bootmedia/boot_block.bin");

        if (boot_media_mount()) {
            ESP_LOGI(TAG, "boot_media_mount() succeeded, attempting to create player");
            s_boot_video_screen = lv_obj_create(NULL);
            lv_obj_set_style_bg_color(s_boot_video_screen, lv_color_black(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_boot_video_screen, 255, LV_PART_MAIN);
            lv_obj_set_style_border_width(s_boot_video_screen, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(s_boot_video_screen, 360, LV_PART_MAIN);
            lv_obj_clear_flag(s_boot_video_screen, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t *canvas = NULL;
            if (boot_block_player_create(s_boot_video_screen, &canvas)) {
                // no lv_scr_load yet: keep the Logo screen, switch only when playback starts
                s_boot_video_ready = true;
                ESP_LOGI(TAG, "Boot video ready (logo kept)");
            } else {
                ESP_LOGE(TAG, "boot_block_player_create() failed - boot animation will be skipped");
                lv_obj_del(s_boot_video_screen);
                s_boot_video_screen = NULL;
                s_boot_video_done = true;
            }
        } else {
            ESP_LOGE(TAG, "boot_media_mount() failed - bootmedia partition not accessible");
            s_boot_video_done = true;
        }
    }
    // Phase 2: wait for the sync signal before playing
    if (s_boot_video_ready && !s_boot_video_active) {
        bool should_start = false;
        uint8_t role = nvs_cfg_get()->device_role;
        if (role == ESPNOW_ROLE_STANDALONE) {
            // standalone: play directly
            should_start = true;
        } else if (role == ESPNOW_ROLE_MASTER) {
            // master: broadcast READY → wait for slaves to come online → wait 800ms prep → broadcast PLAY + play itself
            // with no slave after a 5s timeout, play anyway
            static int64_t s_master_ready_us = 0;
            static bool s_master_slaves_seen = false;
            if (s_master_ready_us == 0) {
                s_master_ready_us = esp_timer_get_time();
                s_intro_step = VIDEO_SYNC_READY;
            }
            if (!s_master_slaves_seen && espnow_master_online_slaves() > 0) {
                s_master_slaves_seen = true;
                s_master_ready_us = esp_timer_get_time();  // restart timing, wait 800ms from now
            }
            int64_t wait_ms = (esp_timer_get_time() - s_master_ready_us) / 1000;
            if ((s_master_slaves_seen && wait_ms >= 800) || wait_ms > 5000) {
                s_intro_step = VIDEO_SYNC_PLAY;
                should_start = true;
                s_master_ready_us = 0;
                s_master_slaves_seen = false;
            }
        } else {
            // slave: wait for the PLAY signal, 5s timeout as fallback
            static int64_t s_slave_wait_us = 0;
            if (s_slave_wait_us == 0) s_slave_wait_us = esp_timer_get_time();
            int64_t wait_ms = (esp_timer_get_time() - s_slave_wait_us) / 1000;
            int intro = s_intro_step;
            if (intro >= VIDEO_SYNC_PLAY || wait_ms > 5000) {
                should_start = true;
                s_slave_wait_us = 0;
            }
        }
        if (should_start) {
            lv_scr_load(s_boot_video_screen);  // switch screens only now, keeping the Logo until the last moment
            s_boot_video_active = true;
            s_boot_video_start_us = esp_timer_get_time();
            s_boot_video_timer = lv_timer_create(boot_video_timer_cb, 33, NULL);
            ESP_LOGD(TAG, "Boot video started");
        }
    }
    if (s_boot_video_active || s_boot_video_ready) return true;
    return false;
}

// Boot flow: after the Logo (and the owner's boot animation, if any), enter the theme. The master and standalone
// gauges go on after 1 s; a slave waits for the master's signal (255), at most 5 s.
void ui_ext_intro_tick(bool is_slave)
{
    if (s_boot_done) return;
    int64_t now_us = esp_timer_get_time();
    if (s_boot_start_us == 0) s_boot_start_us = now_us;
    int64_t boot_el = now_us - s_boot_start_us;
    if (s_intro_step == 0 && boot_el > (is_slave ? 5000000 : 1000000)) s_intro_step = 255;
    if (s_intro_step == 255) boot_enter_default_page();
}

/* ================================================================
 *  "NO SIGNAL" overlay: shown on top of gauge pages when the master's OBD BLE
 *  disconnects / a slave receives no master data.
 *  Disconnection is reflected instantly (ble_now already means "connected"/"data within
 *  the last ~2s"), no extra timing needed.
 * ================================================================ */
void ui_ext_no_signal_update(bool signal_ok)
{
    static bool s_no_signal_visible = false;
    lv_obj_t *act = lv_scr_act();
    // only warn on the pages that actually display gauge data; settings/scan/boot-animation pages don't need it
    bool on_gauge_page = (act == ui_ScreenPageTemp || act == ui_ScreenPageInfo ||
                           act == ui_ScreenPageOilPressure || act == ui_ScreenPageNeedle ||
                           act == ui_ScreenPageGear || act == ui_ScreenPageRpm ||
                           act == ui_ScreenPageSpeed);
    bool show = s_boot_done && on_gauge_page && !signal_ok;

    if (show) {
        if (!s_no_signal_lbl) {
            s_no_signal_lbl = lv_label_create(lv_layer_top());
            lv_obj_clear_flag(s_no_signal_lbl, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_text_font(s_no_signal_lbl, &ui_font_FontTypoderSize16, LV_PART_MAIN);
            lv_obj_set_style_text_color(s_no_signal_lbl, lv_color_hex(0xFF4D4D), LV_PART_MAIN);
            lv_obj_set_style_bg_color(s_no_signal_lbl, lv_color_hex(0x000000), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_no_signal_lbl, 160, LV_PART_MAIN);
            lv_obj_set_style_pad_hor(s_no_signal_lbl, 10, LV_PART_MAIN);
            lv_obj_set_style_pad_ver(s_no_signal_lbl, 4, LV_PART_MAIN);
            lv_obj_set_style_radius(s_no_signal_lbl, 6, LV_PART_MAIN);
            lv_label_set_text(s_no_signal_lbl, "NO SIGNAL");
            lv_obj_align(s_no_signal_lbl, LV_ALIGN_TOP_MID, 0, 34);
        }
        if (!s_no_signal_visible) {
            lv_obj_clear_flag(s_no_signal_lbl, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_no_signal_lbl);
            s_no_signal_visible = true;
        }
    } else if (s_no_signal_lbl && s_no_signal_visible) {
        lv_obj_add_flag(s_no_signal_lbl, LV_OBJ_FLAG_HIDDEN);
        s_no_signal_visible = false;
    }
}

/* ================================================================
 *  Lifecycle
 * ================================================================ */

void ui_ext_init(void)
{
}

void ui_ext_tick(void)
{
    // The boot-animation / sweep / rpm-warn logic previously planned to live
    // here now runs inside my_timerMain through the dedicated ui_ext_* functions above,
    // so the old ui_ext_tick() hook is intentionally empty. Kept as a no-op hook for the
    // ui_event_easter_egg_background / OTA-screen call sites.
}

/* ================================================================
 *  RPM over-limit flash warning  (migrated from ui.c my_timerMain)
 *
 *  Strobes red/black when RPM >= warn threshold, plus the triple-gauge
 *  linked ramp: the band [threshold-1000, threshold] is split into thirds
 *  and gauges 1/2/3 (per their position) ramp black→red in turn, all three
 *  strobing together at the threshold. Each gauge computes locally from its
 *  own position + the same ESP-NOW-synced RPM, so no extra inter-gauge
 *  communication is needed and they stay in sync naturally.
 * ================================================================ */

// RPM warning test mode
volatile int s_rpm_flash_test_ticks = 0;
void ui_rpm_flash_test_start(void) { s_rpm_flash_test_ticks = 60; }  // ~2s test flash
// Note: the RPM ramp for the multi-gauge linked test is driven centrally by the master (espnow_link.c writes the RPM override layer and broadcasts it);
//     this unit only renders per its own position, no local simulation, keeping all three gauges in sync.

static bool s_rpm_flash_red = false;  // flash state (red/black toggle, drives the background color)
static bool s_rpm_flashing = false;   // whether strobing right now (stable flag, drives the timer's fast flash; does not toggle with red/black)
static bool s_rpm_link_ramp = false;  // multi-gauge linked flash: this unit is inside its ramp segment
static bool s_rpm_link_bg = false;    // multi-gauge linked flash: background is taken by the linked red (must restore theme bg when leaving)

bool ui_ext_rpm_is_flashing(void) { return s_rpm_flashing; }
bool ui_ext_rpm_link_ramp_active(void) { return s_rpm_link_ramp; }

// Whether any RPM flash mode could be active (used by my_timerMain's OBD snapshot gate)
bool ui_ext_rpm_warn_possible(void)
{
    const nvs_user_cfg_t *cfg = nvs_cfg_get();
    return cfg->rpm_warn_anim_en ||
           (cfg->device_role != ESPNOW_ROLE_STANDALONE &&
            (espnow_link_linked_en() || espnow_link_linktest_active()));
}

void ui_ext_rpm_flash_tick(uint16_t usRpm, bool in_sweep)
{
    const nvs_user_cfg_t *user_cfg = nvs_cfg_get();
    uint16_t warn_thresh = user_cfg->rpm_warn_threshold; // already clamped to [1000,...]/default 6000 in nvs_storage_init()
    // Linked flash and FLASH ANIM are mutually exclusive (the settings page ensures at most one is on); with linked on, the at-threshold strobe does not depend on anim_en.
    // While a link test is running (linktest_active), this unit renders even if LINKED FLASH is off locally, so the all-gauge sync test works.
    // espnow_link_linked_en(): master = local NVS, slave = mirrored from master's broadcast (bit2 in flags).
    // This ensures slaves render the gradient without needing manual NVS config on each unit.
    bool linked_on = (espnow_link_linked_en() || espnow_link_linktest_active()) &&
                     (user_cfg->device_role != ESPNOW_ROLE_STANDALONE);

    // The linked-test RPM ramp is written by the master into the RPM override layer and broadcast via ESP-NOW; usRpm is the synced value, identical on all three gauges
    bool over = (!in_sweep && (user_cfg->rpm_warn_anim_en || linked_on) && usRpm >= warn_thresh);
    // Test mode: force trigger
    if (s_rpm_flash_test_ticks > 0) { over = true; s_rpm_flash_test_ticks--; }

    // Stable flag: stays true while strobing, driving the timer to flash at RPM_FLASH_PERIOD_MS (independent of the red/black toggle)
    s_rpm_flashing = over;

    // Background image flash: img1→black→img2→black→img3→black loop; UI widgets keep showing above the image
    if (over) {
        s_rpm_flash_red = !s_rpm_flash_red;
        lv_obj_t *scr = lv_scr_act();
        // Drop the dial-face artwork while strobing — a background image
        // covers bg_color, so the flash would otherwise be invisible.
        lv_obj_set_style_bg_img_src(scr, NULL, LV_PART_MAIN);
        lv_obj_set_style_bg_color(scr, s_rpm_flash_red ? lv_color_hex(UI_SEM_FLASH) : lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(scr, 255, LV_PART_MAIN);
    } else {
        if (s_rpm_flash_red) {
            lv_obj_t *scr = lv_scr_act();
            ui_helpers_style_screen_bg(scr);   // restore theme bg + dial face
            lv_obj_set_style_bg_opa(scr, 255, LV_PART_MAIN);
            s_rpm_flash_red = false;
        }
    }

    // ---- Multi-gauge linked strobe (triple-gauge mode) ----
    {
        bool linked_drawn = false;
        if (linked_on && !over && !in_sweep && warn_thresh > 1000 && usRpm < warn_thresh) {
            uint32_t base = (uint32_t)warn_thresh - 1000;   // start RPM = threshold minus 1000 rpm
            uint8_t pos = nvs_device_position_get();        // this unit's position 1/2/3
            if (pos >= 1 && pos <= 3) {
                uint32_t seg_start = base + 1000u * (pos - 1) / 3;
                uint32_t seg_end   = base + 1000u * pos / 3;
                lv_obj_t *scr = lv_scr_act();
                if (usRpm >= seg_end) {
                    // this unit's segment completed: solid red
                    lv_obj_set_style_bg_img_src(scr, NULL, LV_PART_MAIN);
                    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFF0000), LV_PART_MAIN);
                    lv_obj_set_style_bg_opa(scr, 255, LV_PART_MAIN);
                    s_rpm_link_ramp = false;
                    linked_drawn = true;
                } else if (usRpm > seg_start && seg_end > seg_start) {
                    // inside this unit's segment: full-screen black→red ramp, alpha rising linearly with RPM
                    uint8_t alpha = (uint8_t)(((uint32_t)usRpm - seg_start) * 255 / (seg_end - seg_start));
                    lv_obj_set_style_bg_img_src(scr, NULL, LV_PART_MAIN);
                    lv_obj_set_style_bg_color(scr,
                        lv_color_mix(lv_color_hex(0xFF0000), lv_color_hex(0x000000), alpha), LV_PART_MAIN);
                    lv_obj_set_style_bg_opa(scr, 255, LV_PART_MAIN);
                    s_rpm_link_ramp = true;
                    linked_drawn = true;
                }
            }
        }
        if (linked_drawn) {
            s_rpm_link_bg = true;
        } else if (!over) {
            // during over (strobe) the flash logic owns the background, don't touch it here; otherwise, leaving the linked red must restore black
            s_rpm_link_ramp = false;
            if (s_rpm_link_bg) {
                lv_obj_t *scr = lv_scr_act();
                ui_helpers_style_screen_bg(scr);   // restore theme bg + dial face
                lv_obj_set_style_bg_opa(scr, 255, LV_PART_MAIN);
                s_rpm_link_bg = false;
            }
        }
    }
}
