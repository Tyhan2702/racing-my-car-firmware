#pragma once
// Inflates zlib data compiled into the firmware (boot logo, locked boot animation) into PSRAM, with the miniz
// inflater in the ESP32-S3 ROM (no flash cost).
#include <stddef.h>
#include <stdint.h>

// raw_len bytes in a new PSRAM buffer (free it with heap_caps_free), or NULL when the data is bad or memory is short
void *rmc_inflate(const uint8_t *z, size_t z_len, size_t raw_len);
