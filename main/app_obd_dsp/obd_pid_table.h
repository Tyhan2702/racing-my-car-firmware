#pragma once
// Mode 01 PIDs of SAE J1979 / ISO 15031-5 with their names, units and formulas, for the DATA page (menu → DATA):
// every value the car answers is shown there, the others as "-".
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t pid;
    uint8_t kind;          // formula (obd_pid_table.c)
    uint8_t len;           // data bytes used
    const char *name;      // short, upper case (gauge font)
} obd_pid_info_t;

const obd_pid_info_t *obd_pid_info(uint8_t pid);      // NULL: not in the table (shown as "PID XX" in hex)
size_t obd_pid_count(void);
const obd_pid_info_t *obd_pid_at(size_t i);
// Formats a reply's data bytes (after "41 PID") as "123 KPA"; false when there are too few bytes
bool obd_pid_format(uint8_t pid, const uint8_t *d, uint8_t n, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
