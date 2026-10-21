// Copied into each gauge's firmware by racing-my-car scripts/vehicle-catalog.mjs: do not edit there.
// The admin console's published cars, sent by the app (vehicle_added.c). VC_BRANDS / VC_BRAND_COUNT (vehicle_catalog.h)
// are the built-in list with these after it.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VC_ADDED_MAX 64                                // cars
// The JSON the app sends, kept in NVS (24 KB = 6 pages, one kept free: ~630 entries of 32 B). Rewriting a blob keeps
// the old copy until the new one is written, so 2 x 6 KB (~390 entries) plus the settings, Wi-Fi PHY calibration and
// BLE bonds still fit. ~40 published cars at ~150 B each.
#define VC_ADDED_BYTES 6144
void vc_added_load(void);                              // at start, before the pick is read
// Replaces the added cars with this JSON list and keeps it; picked = the id of the car picked now (or NULL), found
// again in the new list (*brand / *model; *brand 0xFF when it is gone). False when the JSON is not a list (nothing changes).
bool vc_added_store(const char *json, size_t n, const char *picked, uint8_t *brand, uint8_t *model);
const char *vc_added_hash(void);                       // 8 hex digits (FNV-1a of the JSON kept), "" when none
uint32_t vc_fnv(const void *p, size_t n);
bool vc_find(const char *id, uint8_t *brand, uint8_t *model);
