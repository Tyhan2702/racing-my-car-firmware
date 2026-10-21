// Copied into each gauge's firmware by racing-my-car scripts/vehicle-catalog.mjs: do not edit there.
// The cars published from the admin console (Admin → 专车专用车型, vehicle_cars) on the gauge: the app sends them as
// JSON (the rows the phone reads, oldest first) whenever it connects and the gauge's list is older; they are kept in NVS
// ("vcars") and put after the built-in list the way web/vehicle-catalog.js withAdded() does (new models at the end of
// their brand, new brands at the end), so a car has the same brand / model index on the gauge as on the phone.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "vehicle_catalog.h"
#include "vehicle_added.h"

static const char *TAG = "vcars";
const vc_brand_t *vc_brands = VC_BUILTIN;
int vc_brand_count = VC_BUILTIN_COUNT;
static char s_hash[9] = "";
static void *s_mem;                                   // the current added list: one block, freed when replaced

uint32_t vc_fnv(const void *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (const uint8_t *b = p; n--; b++) h = (h ^ *b) * 16777619u;
    return h;
}
const char *vc_added_hash(void) { return s_hash; }

static const char *str(const cJSON *o, const char *k, size_t max)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) && v->valuestring[0] && strlen(v->valuestring) <= max ? v->valuestring : NULL;
}
static int num(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) && v->valueint >= 1950 && v->valueint <= 2100 ? v->valueint : 0;
}
static bool known(const char *id)
{
    for (int b = 0; b < VC_BUILTIN_COUNT; b++) for (int m = 0; m < VC_BUILTIN[b].count; m++) if (!strcmp(VC_BUILTIN[b].models[m].id, id)) return true;
    return false;
}

// Builds the merged list from the JSON in one block (brands, then models, then the strings); false when it is not a list.
static bool build(const char *json, size_t len)
{
    cJSON *a = cJSON_ParseWithLength(json, len);
    if (!cJSON_IsArray(a)) { cJSON_Delete(a); return false; }
    int rows = cJSON_GetArraySize(a); if (rows > VC_ADDED_MAX) rows = VC_ADDED_MAX;
    size_t strs = 0;
    for (int i = 0; i < rows; i++) strs += 8 * 82;                    // each of the 8 strings at most 81 bytes (checked below)
    size_t sz = (VC_BUILTIN_COUNT + rows) * sizeof(vc_brand_t), msz = 0;
    for (int b = 0; b < VC_BUILTIN_COUNT; b++) msz += VC_BUILTIN[b].count;
    msz = (msz + rows) * sizeof(vc_model_t);
    uint8_t *mem = calloc(1, sz + msz + strs);
    if (!mem) { cJSON_Delete(a); return false; }
    vc_brand_t *brands = (vc_brand_t *)mem; vc_model_t *models = (vc_model_t *)(mem + sz); char *sp = (char *)(mem + sz + msz);
    // each brand's models are kept together: count the added ones per brand first
    int nb = VC_BUILTIN_COUNT, extra[VC_BUILTIN_COUNT + VC_ADDED_MAX] = {0}, where[VC_ADDED_MAX];
    const char *bname[VC_ADDED_MAX];
    for (int i = 0; i < rows; i++) {
        const cJSON *r = cJSON_GetArrayItem(a, i);
        const char *id = str(r, "id", 61), *brand = str(r, "brand", 40);
        where[i] = -1;
        if (!id || !brand || !str(r, "name", 60) || !str(r, "profile", 40) || known(id)) continue;
        bool dup = false;
        for (int j = 0; j < i && !dup; j++) dup = where[j] >= 0 && !strcmp(str(cJSON_GetArrayItem(a, j), "id", 61), id);
        if (dup) continue;
        int b = 0;
        while (b < nb && strcasecmp(b < VC_BUILTIN_COUNT ? VC_BUILTIN[b].name : bname[b - VC_BUILTIN_COUNT], brand)) b++;
        if (b == nb) bname[nb++ - VC_BUILTIN_COUNT] = brand;
        where[i] = b; extra[b]++;
    }
    vc_model_t *mp = models;
    for (int b = 0; b < nb; b++) {
        bool built = b < VC_BUILTIN_COUNT;
        int own = built ? VC_BUILTIN[b].count : 0;
        if (own) memcpy(mp, VC_BUILTIN[b].models, own * sizeof(vc_model_t));
        brands[b] = (vc_brand_t){.models = mp, .count = (uint8_t)(own + extra[b])};
        mp += own;
        const char *name = built ? VC_BUILTIN[b].name : bname[b - VC_BUILTIN_COUNT];
        if (built) brands[b].name = name; else { brands[b].name = sp; sp += sprintf(sp, "%s", name) + 1; }
        for (int i = 0; i < rows; i++) {
            if (where[i] != b) continue;
            const cJSON *r = cJSON_GetArrayItem(a, i);
            const char **f[] = {&mp->id, &mp->name, &mp->code, &mp->note, &mp->profile, &mp->alt};
            const char *k[] = {"id", "name", "code", "note", "profile", "alt"};
            for (int j = 0; j < 6; j++) { const char *v = str(r, k[j], 80); if (v) { *f[j] = sp; sp += sprintf(sp, "%s", v) + 1; } }
            mp->name_zh = mp->name; mp->code_zh = mp->code; mp->note_zh = mp->note;   // as the admin typed them
            mp->from = num(r, "year_from"); mp->to = num(r, "year_to");
            if (mp->to < mp->from) mp->to = mp->from;
            mp++;
        }
    }
    cJSON_Delete(a);
    void *old = s_mem;
    vc_brands = brands; vc_brand_count = nb; s_mem = mem;
    free(old);                                        // shortcut: assumes no screen holds a model pointer across a push (pushes come from the app)
    return true;
}

void vc_added_load(void)
{
    nvs_handle_t h; size_t n = 0;
    if (nvs_open("vcars", NVS_READONLY, &h) != ESP_OK) return;
    char *json = NULL;
    if (nvs_get_blob(h, "json", NULL, &n) == ESP_OK && n && n <= VC_ADDED_BYTES && (json = malloc(n)) && nvs_get_blob(h, "json", json, &n) == ESP_OK && build(json, n))
        snprintf(s_hash, sizeof(s_hash), "%08lx", (unsigned long)vc_fnv(json, n));
    free(json); nvs_close(h);
    ESP_LOGI(TAG, "%d brands (%s)", vc_brand_count, s_hash[0] ? s_hash : "built-in only");
}

bool vc_find(const char *id, uint8_t *brand, uint8_t *model)
{
    for (int b = 0; id && b < vc_brand_count; b++) for (int m = 0; m < vc_brands[b].count; m++)
        if (vc_brands[b].models[m].id && !strcmp(vc_brands[b].models[m].id, id)) { *brand = b; *model = m; return true; }
    return false;
}

bool vc_added_store(const char *json, size_t n, const char *picked, uint8_t *brand, uint8_t *model)
{
    if (n > VC_ADDED_BYTES) return false;
    char keep[64] = "";
    if (picked) snprintf(keep, sizeof(keep), "%s", picked);       // the model's id string lives in the block build() frees
    if (!build(json, n)) return false;
    nvs_handle_t h;
    esp_err_t err = nvs_open("vcars", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "json", json, n);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) {   // not kept: back to the stored list and its hash, so the app sends the list again next time
        ESP_LOGE(TAG, "cars list (%u B) not saved: %s", (unsigned)n, esp_err_to_name(err));
        s_hash[0] = 0;
        vc_added_load();
        if (!s_hash[0]) build("[]", 2);
        return false;
    }
    snprintf(s_hash, sizeof(s_hash), "%08lx", (unsigned long)vc_fnv(json, n));
    ESP_LOGI(TAG, "now %d brands (%s)", vc_brand_count, s_hash);
    if (keep[0] && !vc_find(keep, brand, model)) *brand = 0xFF;
    return true;
}
