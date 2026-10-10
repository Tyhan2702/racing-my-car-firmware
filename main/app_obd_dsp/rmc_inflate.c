// See rmc_inflate.h.
#include "rmc_inflate.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#ifdef SIMULATOR
#include <zlib.h>
#else
#include "rom/miniz.h"
#endif

void *rmc_inflate(const uint8_t *z, size_t z_len, size_t raw_len)
{
    uint8_t *out = heap_caps_malloc(raw_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out) out = heap_caps_malloc(raw_len, MALLOC_CAP_8BIT);
    if (!out) return NULL;
#ifdef SIMULATOR
    uLongf n = raw_len;
    bool ok = uncompress(out, &n, z, z_len) == Z_OK && n == raw_len;
#else
    // the decompressor state is ~11 KB: on the heap, not on the caller's stack
    tinfl_decompressor *d = heap_caps_malloc(sizeof(*d), MALLOC_CAP_8BIT);
    bool ok = false;
    if (d) {
        tinfl_init(d);
        size_t in_n = z_len, out_n = raw_len;
        tinfl_status st = tinfl_decompress(d, z, &in_n, out, out, &out_n,
                                           TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        ok = st == TINFL_STATUS_DONE && out_n == raw_len;
        heap_caps_free(d);
    }
#endif
    if (!ok) {
        ESP_LOGE("rmc_inflate", "inflate failed (%u -> %u bytes)", (unsigned)z_len, (unsigned)raw_len);
        heap_caps_free(out);
        return NULL;
    }
    return out;
}
