// Copied into each gauge's firmware by racing-my-car scripts/vehicle-catalog.mjs: do not edit there.
// The admin console's published cars, sent by the app (vehicle_added.c). VC_BRANDS / VC_BRAND_COUNT (vehicle_catalog.h)
// are the built-in list with these after it.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VC_ADDED_MAX 64                                // cars
#define VC_ADDED_BYTES 16384                           // the JSON the app sends
void vc_added_load(void);                              // at start, before the pick is read
// Replaces the added cars with this JSON list and keeps it; picked = the id of the car picked now (or NULL), found
// again in the new list (*brand / *model; *brand 0xFF when it is gone). False when the JSON is not a list (nothing changes).
bool vc_added_store(const char *json, size_t n, const char *picked, uint8_t *brand, uint8_t *model);
const char *vc_added_hash(void);                       // 8 hex digits (FNV-1a of the JSON kept), "" when none
uint32_t vc_fnv(const void *p, size_t n);
bool vc_find(const char *id, uint8_t *brand, uint8_t *model);
