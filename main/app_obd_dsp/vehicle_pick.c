// 车型: see vehicle_pick.h. Mirrors web/vehicle-catalog.js (carLabel, profileOf, pickOfProfile).
#include <stdio.h>
#include <string.h>
#include "vehicle_pick.h"
#include "vehicle_profiles.h"
#include "bsp_obd_dsp/nvs_storage.h"
#include "vehicle_added.h"

int vehicle_profile_find(const char *name)
{
    uint8_t n = 0; const vehicle_profile_t *all = vehicle_profile_get_all(&n);
    for (int i = 0; i < n && name; i++) if (all[i].name && !strcmp(all[i].name, name)) return i;
    return -1;
}
static const vc_model_t *model_of(vehicle_pick_t p)
{
    if (p.obd2 || p.brand >= VC_BRAND_COUNT || p.model >= VC_BRANDS[p.brand].count) return NULL;
    return &VC_BRANDS[p.brand].models[p.model];
}
// the first car read by a profile: how a choice from before (a profile only) becomes a car
static vehicle_pick_t pick_of_profile(const char *name)
{
    vehicle_pick_t p = {.obd2 = true};
    if (!name || !strcmp(name, VC_OBD2_PROFILE)) return p;
    for (int b = 0; b < VC_BRAND_COUNT; b++) for (int m = 0; m < VC_BRANDS[b].count; m++) {
        const vc_model_t *x = &VC_BRANDS[b].models[m];
        bool main = x->profile && !strcmp(x->profile, name), alt = x->alt && !strcmp(x->alt, name);
        if (main || alt) return (vehicle_pick_t){.brand = b, .model = m, .year = x->from, .alt = alt && !main};
    }
    return p;
}
vehicle_pick_t vehicle_pick_get(void)
{
    const nvs_user_cfg_t *c = nvs_cfg_get();
    if (c->car_set == 1) return (vehicle_pick_t){.obd2 = true};
    if (c->car_set == 2) { vehicle_pick_t p = {.brand = c->car_brand, .model = c->car_model, .year = c->car_year, .alt = c->car_alt}; if (model_of(p)) return p; }
    const vehicle_profile_t *v = vehicle_profile_get(c->vehicle_profile_idx);
    return pick_of_profile(v ? v->name : NULL);
}
void vehicle_pick_set(vehicle_pick_t p)
{
    const vc_model_t *m = model_of(p);
    if (!m) p.obd2 = true;
    const char *name = p.obd2 ? VC_OBD2_PROFILE : p.alt && m->alt ? m->alt : m->profile;
    int idx = vehicle_profile_find(name);
    nvs_user_cfg_t cfg = *nvs_cfg_get();
    cfg.car_set = p.obd2 ? 1 : 2; cfg.car_brand = p.brand; cfg.car_model = p.model; cfg.car_year = p.year; cfg.car_alt = p.alt;
    if (idx >= 0) cfg.vehicle_profile_idx = (uint8_t)idx;
    nvs_cfg_set(&cfg);
    if (idx >= 0) vehicle_profile_set_active((uint8_t)idx);
}
bool vehicle_cars_store(const char *json, size_t n)
{
    vehicle_pick_t p = vehicle_pick_get();
    const vc_model_t *m = model_of(p); uint8_t b = 0, md = 0;
    if (!vc_added_store(json, n, m ? m->id : NULL, &b, &md)) return false;
    if (!m) return true;
    if (b == 0xFF) p = (vehicle_pick_t){.obd2 = true};                // the car was taken off the list
    else { p.brand = b; p.model = md; }
    vehicle_pick_set(p);
    return true;
}
const char *vehicle_model_name(const vc_model_t *m, bool zh) { return zh && m->name_zh ? m->name_zh : m->name; }
void vehicle_pick_label(vehicle_pick_t p, bool zh, char *out, size_t n)
{
    const vc_model_t *m = model_of(p);
    if (!m) { snprintf(out, n, "%s", zh ? "OBD2 标准" : "OBD2 STANDARD"); return; }
    char year[8] = ""; if (m->from && p.year) snprintf(year, sizeof(year), "%u ", (unsigned)p.year);
    snprintf(out, n, "%s%s %s%s", year, VC_BRANDS[p.brand].name, vehicle_model_name(m, zh), p.alt ? (zh ? " · 备用读法" : " · FALLBACK") : "");
}
