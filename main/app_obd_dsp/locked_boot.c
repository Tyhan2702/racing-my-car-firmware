// Racing My Car locked boot animation — see locked_boot.h.
#include "app_obd_dsp/locked_boot.h"
#include "app_obd_dsp/boot_block_player.h"
#include "esp_log.h"
#include "esp_timer.h"

extern const char locked_boot_manifest[];
extern const uint8_t locked_boot_data[];
extern const size_t locked_boot_data_size;

static const char *TAG = "locked_boot";
static volatile bool s_done = false;
static bool s_started = false;
static int64_t s_start_us = 0;
static lv_timer_t *s_timer = NULL;

static void locked_boot_timer_cb(lv_timer_t *t) {
    (void)t;
    uint32_t elapsed_ms = (uint32_t)((esp_timer_get_time() - s_start_us) / 1000);
    boot_block_player_update(elapsed_ms);
    if (boot_block_player_is_finished()) {
        lv_timer_del(s_timer);
        s_timer = NULL;
        // Release the player now: the canvas goes (the static logo underneath shows through) and its buffer is freed,
        // so the owner's boot animation or showroom video starts from a clean player. If the Logo screen was already
        // deleted (e.g. a double tap opened another page), the player has forgotten the canvas and only frees memory.
        boot_block_player_destroy();
        s_done = true;
        ESP_LOGI(TAG, "locked boot animation finished after %u ms", (unsigned)elapsed_ms);
    }
}

void locked_boot_start(lv_obj_t *parent) {
    if (s_started) return;
    s_started = true;
    boot_block_player_set_embedded(locked_boot_manifest, locked_boot_data, locked_boot_data_size);
    lv_obj_t *canvas = NULL;
    bool ok = parent && boot_block_player_create(parent, &canvas);
    // Later players (the owner's boot animation) read the bootmedia partition again.
    boot_block_player_set_embedded(NULL, NULL, 0);
    if (!ok) {
        ESP_LOGW(TAG, "locked boot animation could not start, continuing boot");
        s_done = true;
        return;
    }
    s_start_us = esp_timer_get_time();
    s_timer = lv_timer_create(locked_boot_timer_cb, 33, NULL);
    ESP_LOGI(TAG, "locked boot animation started (%u bytes)", (unsigned)locked_boot_data_size);
}

bool locked_boot_done(void) {
    // Never hold up the boot flow if the Logo screen (and so the animation) was never started.
    return s_done || !s_started;
}
