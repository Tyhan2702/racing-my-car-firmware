#pragma once
// 多仪表共享 OBD with the 7" / 4" dashes (share_ble.c): a 1.85" SLAVE takes a dash master's values over Bluetooth,
// a 1.85" MASTER serves its own to dashes set to 副仪表 (service 0x1FFD in racechrono_ble_diy.c).
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SHARE_BLE_UUID 0x1FFD
bool share_ble_is_dash(const char *name);          // "RMC - 7 Dash …" / "RMC - 4 Dash …"
void share_ble_client_start(void);                 // SLAVE paired to a dash: link it and keep linking (once)
bool share_ble_has_data(void);                     // its frames within 2 s
bool share_ble_car_off(void);                      // its link answers, its car silent 6 s (the key-off)
const char *share_ble_master_name(void);
void share_ble_on_frame(const uint8_t *b, int n);
int share_ble_frame(int kind, uint8_t *b, int max); // MASTER: frame 'V' / 'G' / 'S' into b, its length (0: none now)
#ifdef __cplusplus
}
#endif
