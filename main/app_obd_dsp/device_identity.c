#include "esp_app_desc.h"
#include "theme_engine/theme_interface.h"
#include "device_identity.h"

#include <stdio.h>
#include <string.h>

#include "bsp_obd_dsp/lcd_driver/ST77916.h"
#include "esp_log.h"
#include "esp_mac.h"

static const char *TAG = "device_identity";

#ifndef OBD_GAUGE_GIT_BRANCH
#define OBD_GAUGE_GIT_BRANCH "unknown"
#endif

#ifndef OBD_GAUGE_GIT_COUNT
#define OBD_GAUGE_GIT_COUNT 0
#endif

#ifndef OBD_GAUGE_BUILD_TAG
#define OBD_GAUGE_BUILD_TAG "unknown-0-unknown"
#endif

#define OBD_GAUGE_BOARD_NAME        "Waveshare ESP32-S3-Touch-LCD-1.85"
#define OBD_GAUGE_BOARD_VARIANT     "rmc_gauge_185"   // the platform treats the old "obd_brz_gauge" as the same hardware
#define OBD_GAUGE_LCD_NAME          "ST77916"
#define OBD_GAUGE_FLASH_MB          16u
#define OBD_GAUGE_PSRAM_MB          8u
#define OBD_GAUGE_OTA_SLOTS         2u
#define OBD_GAUGE_BOOTMEDIA_SLOTS   1u
#define OBD_GAUGE_BOOTMEDIA_FORMAT   1u

static const device_identity_t s_identity = {
    .board_name = OBD_GAUGE_BOARD_NAME,
    .board_variant = OBD_GAUGE_BOARD_VARIANT,
    .lcd_name = OBD_GAUGE_LCD_NAME,
    .screen_width = EXAMPLE_LCD_WIDTH,
    .screen_height = EXAMPLE_LCD_HEIGHT,
    .color_bits = EXAMPLE_LCD_COLOR_BITS,
    .flash_mb = OBD_GAUGE_FLASH_MB,
    .psram_mb = OBD_GAUGE_PSRAM_MB,
    .ota_slots = OBD_GAUGE_OTA_SLOTS,
    .bootmedia_slots = OBD_GAUGE_BOOTMEDIA_SLOTS,
    .bootmedia_format_version = OBD_GAUGE_BOOTMEDIA_FORMAT,
};

// /ota/info 只返回手机 App 实际会用到的字段（硬件兼容性校验 + build_tag/branch/count），
// 其余字段（project/version/git/built/idf/slot/theme）App 端未读取，删掉以保证 512 字节内不截断。
static char s_manifest_json[640];

const device_identity_t *device_identity_get(void)
{
    return &s_identity;
}

const char *device_identity_manifest_json(void)
{
    // rebuilt on every call: theme_pages changes once the theme is loaded at boot

    // Factory (eFuse) base MAC: the Racing My Car platform registers genuine gauges by it. It is the same value
    // esptool reads over USB, so a gauge flashed and registered by the owner is recognised by the app later.
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);

    int written = snprintf(s_manifest_json, sizeof(s_manifest_json),
             "{"
             "\"device\":{"
             "\"mac\":\"%02x%02x%02x%02x%02x%02x\","
             "\"board\":\"%s\","
             "\"variant\":\"%s\","
             "\"lcd\":\"%s\","
             "\"screen\":{\"w\":%u,\"h\":%u,\"bpp\":%u},"
             "\"flash_mb\":%u,"
             "\"psram_mb\":%u,"
             "\"ota_slots\":%u,"
             "\"bootmedia_slots\":%u,"
             "\"bootmedia_format\":%u"
             "},"
             "\"firmware\":{"
             "\"version\":\"%s\","
             "\"build_tag\":\"%s\","
             "\"branch\":\"%s\","
             "\"count\":%u,"
             "\"features\":[\"factory_reset\",\"locked_boot\",\"theme_zlib\",\"theme_assets_64\"]"
             "},"
             "\"theme_pages\":%u"
             "}",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             s_identity.board_name,
             s_identity.board_variant,
             s_identity.lcd_name,
             s_identity.screen_width,
             s_identity.screen_height,
             s_identity.color_bits,
             s_identity.flash_mb,
             s_identity.psram_mb,
             s_identity.ota_slots,
             s_identity.bootmedia_slots,
             s_identity.bootmedia_format_version,
             esp_app_get_description()->version,   // the release, e.g. v35
             OBD_GAUGE_BUILD_TAG,
             OBD_GAUGE_GIT_BRANCH,
             (unsigned)OBD_GAUGE_GIT_COUNT,
             (unsigned)theme_page_list_count());   // 0: no theme, the platform installs the default themes

    if (written < 0 || written >= (int)sizeof(s_manifest_json)) {
        ESP_LOGE(TAG, "manifest JSON truncated (len=%d, cap=%u)", written, (unsigned)sizeof(s_manifest_json));
    }

    s_manifest_json[sizeof(s_manifest_json) - 1] = '\0';
    return s_manifest_json;
}
