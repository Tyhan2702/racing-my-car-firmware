// Several themes in the one theme partition. The Racing My Car app installs a single package whose manifest lists
// every page of every theme the owner keeps on the gauge, plus "rmc_themes": [{id, name, pages:[page ids]}] saying
// which pages belong to which theme. This file reads that list (GET /ota/theme/list, so the app stays in step) and
// removes one theme when the owner long-presses one of its pages on the gauge.
//
// Removing rewrites only the 16 KB manifest at the start of the partition: the theme's pages and its "rmc_themes"
// entry go, the image/font bytes of the other themes stay where they are. When no page is left the manifest is
// erased and the gauge falls back to its built-in theme. A power cut during the rewrite can only leave an
// unreadable manifest, which the theme loader also treats as "no theme".

#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "theme_stack.h"

#define MANIFEST_SIZE (16 * 1024)

static const char *TAG = "theme_stack";

static const esp_partition_t *theme_partition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x82, "theme_0");
}

/** The manifest as cJSON, or NULL when the partition is missing, empty or unreadable. */
static cJSON *read_manifest(const esp_partition_t *p)
{
    if (!p) return NULL;
    char *buf = heap_caps_malloc(MANIFEST_SIZE + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(MANIFEST_SIZE + 1);
    if (!buf) return NULL;
    cJSON *json = NULL;
    if (esp_partition_read(p, 0, buf, MANIFEST_SIZE) == ESP_OK) {
        size_t end = 0;
        while (end < MANIFEST_SIZE && buf[end] != '\0' && (uint8_t)buf[end] != 0xFF) end++;
        buf[end] = '\0';
        if (end > 0) json = cJSON_Parse(buf);
    }
    free(buf);
    return json;
}

static cJSON *theme_pages(cJSON *manifest)
{
    cJSON *pages = cJSON_GetObjectItem(manifest, "pages");
    cJSON *list = pages ? cJSON_GetObjectItem(pages, "theme_pages") : NULL;
    return cJSON_IsArray(list) ? list : NULL;
}

char *theme_stack_list_json(void)
{
    cJSON *manifest = read_manifest(theme_partition());
    cJSON *out = cJSON_CreateObject();
    cJSON *themes = cJSON_AddArrayToObject(out, "themes");
    int pages = 0;
    if (manifest) {
        cJSON *list = theme_pages(manifest);
        pages = list ? cJSON_GetArraySize(list) : 0;
        cJSON *stack = cJSON_GetObjectItem(manifest, "rmc_themes");
        cJSON *t = NULL;
        cJSON_ArrayForEach(t, stack) {
            cJSON *id = cJSON_GetObjectItem(t, "id");
            if (!cJSON_IsString(id)) continue;
            cJSON *e = cJSON_CreateObject();
            cJSON_AddStringToObject(e, "id", id->valuestring);
            cJSON *name = cJSON_GetObjectItem(t, "name");
            if (cJSON_IsString(name)) cJSON_AddStringToObject(e, "name", name->valuestring);
            cJSON_AddItemToArray(themes, e);
        }
    }
    cJSON_AddNumberToObject(out, "pages", pages);
    char *text = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    if (manifest) cJSON_Delete(manifest);
    return text;
}

static bool string_in(cJSON *array, const char *value)
{
    cJSON *v = NULL;
    cJSON_ArrayForEach(v, array) {
        if (cJSON_IsString(v) && strcmp(v->valuestring, value) == 0) return true;
    }
    return false;
}

bool theme_stack_remove_page_owner(const char *page_id, int *left)
{
    if (left) *left = -1;
    const esp_partition_t *p = theme_partition();
    cJSON *manifest = read_manifest(p);
    cJSON *list = manifest ? theme_pages(manifest) : NULL;
    if (!list || !page_id) {
        if (manifest) cJSON_Delete(manifest);
        return false;
    }

    // the theme that owns this page; without the app's list, just this page
    cJSON *stack = cJSON_GetObjectItem(manifest, "rmc_themes");
    cJSON *owner_pages = NULL;
    int owner = -1, i = 0;
    cJSON *t = NULL;
    cJSON_ArrayForEach(t, stack) {
        cJSON *pages = cJSON_GetObjectItem(t, "pages");
        if (cJSON_IsArray(pages) && string_in(pages, page_id)) { owner = i; owner_pages = pages; break; }
        i++;
    }
    for (int k = cJSON_GetArraySize(list) - 1; k >= 0; k--) {
        cJSON *page = cJSON_GetArrayItem(list, k);
        cJSON *id = cJSON_GetObjectItem(page, "id");
        if (!cJSON_IsString(id)) continue;
        if (owner_pages ? string_in(owner_pages, id->valuestring) : strcmp(id->valuestring, page_id) == 0) {
            cJSON_DeleteItemFromArray(list, k);
        }
    }
    if (owner >= 0) cJSON_DeleteItemFromArray(stack, owner);
    int remaining = cJSON_GetArraySize(list);
    if (left) *left = remaining;

    bool ok = false;
    if (remaining == 0) {
        ok = esp_partition_erase_range(p, 0, MANIFEST_SIZE) == ESP_OK;
        ESP_LOGI(TAG, "last theme removed, back to the built-in theme (%s)", ok ? "ok" : "erase failed");
    } else {
        char *text = cJSON_PrintUnformatted(manifest);
        size_t len = text ? strlen(text) : 0;
        uint8_t *buf = (text && len < MANIFEST_SIZE) ? heap_caps_malloc(MANIFEST_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
        if (!buf && text && len < MANIFEST_SIZE) buf = malloc(MANIFEST_SIZE);
        if (buf) {
            memset(buf, 0xFF, MANIFEST_SIZE);
            memcpy(buf, text, len);
            buf[len] = 0;   // the loader reads up to the first NUL
            ok = esp_partition_erase_range(p, 0, MANIFEST_SIZE) == ESP_OK &&
                 esp_partition_write(p, 0, buf, MANIFEST_SIZE) == ESP_OK;
            free(buf);
        }
        free(text);
        ESP_LOGI(TAG, "removed the theme of page '%s', %d page(s) left (%s)", page_id, remaining, ok ? "ok" : "write failed");
    }
    cJSON_Delete(manifest);
    return ok;
}
