// USB mailbox: settings the platform writes over a USB cable (web/usb-flash.js mailboxFiles), where it cannot reach
// NVS. One 4 KB flash sector outside every partition (between phy_init and ota_0) holds "RMCBOX1\n" + JSON +
// NUL, e.g. {"games":{"installed":[…]}}. At boot it is applied like the Wi-Fi POST /ota/games, then erased so it is
// applied once.
#include <string.h>
#include <stdlib.h>
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "usb_mailbox.h"
#include "export_path/screens/game_core.h"

#define BOX_ADDR 0x1F000
#define BOX_SIZE 0x1000
static const char *TAG = "usb_box";
static const char MAGIC[] = "RMCBOX1\n";

static void apply(cJSON *o, const char *key, bool (*set)(const char *))
{
    cJSON *v = cJSON_GetObjectItem(o, key);
    if (!cJSON_IsObject(v)) return;
    char *text = cJSON_PrintUnformatted(v);
    bool ok = text && set(text);
    ESP_LOGI(TAG, "%s from USB: %s", key, ok ? "saved" : "rejected");
    free(text);
}

void usb_mailbox_apply(void)
{
    char *buf = heap_caps_malloc(BOX_SIZE + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(BOX_SIZE + 1);
    if (!buf) return;
    if (esp_flash_read(NULL, buf, BOX_ADDR, BOX_SIZE) != ESP_OK || memcmp(buf, MAGIC, sizeof(MAGIC) - 1) != 0) { free(buf); return; }
    buf[BOX_SIZE] = '\0';
    cJSON *o = cJSON_Parse(buf + sizeof(MAGIC) - 1);
    if (o) {
        apply(o, "games", games_install_json);
        cJSON_Delete(o);
    } else {
        ESP_LOGW(TAG, "unreadable mailbox, cleared");
    }
    esp_flash_erase_region(NULL, BOX_ADDR, BOX_SIZE);
    free(buf);
}
