// 多仪表共享 OBD with the 7" / 4" dashes (their obd_share.c, ~/.rmc-tools/dash7): one OBD adapter for every dash in the
// car. The 1.85" keeps its own MULTI-GAUGE roles: a SLAVE paired (BLE SCAN page) to a "RMC - 7 Dash" / "RMC - 4 Dash"
// takes that dash's values over Bluetooth (service 0x1FFD) instead of a 1.85" master's ESP-NOW; a MASTER also serves
// 0x1FFD (racechrono_ble_diy.c) so the 7" / 4" set to 副仪表 can show its values (share_ble_frame builds the frames).
//
// Frames (0x1FFD / 0x0001 notify), little-endian, the first byte the kind (the dashes' obd_share.c is the reference):
//   'V' n × [key u8, value f32, age ms u16]   'G' lever[8] gear[8] ecu i8, ages u16 ×3   'M' n × [sig u8, f32, age, txt[8]]
//   'S' flags u8 (1 car, 2 link), rps u8, quiet ms u32, doors i8, vin[17], PIDs listed [32], proto [24]   'P' (DATA, not used here)
#include "share_ble.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_bt_defs.h"
#include "app_obd_dsp/obd_data_cache.h"
#include "bsp_obd_dsp/nvs_storage.h"
#include "bsp_obd_dsp/elm327_ble_client.h"

static const char *TAG = "share_ble";
// the dashes' value keys (dash.h)
enum { V_RPM, V_SPEED, V_COOLANT, V_IAT, V_LOAD, V_TPS, V_TIMING, V_MAP, V_VOLT, V_FUEL, V_LAMBDA, V_OIL,
       V_TRANS, V_OILP, V_KNOCK, V_FUELP, V_STFT, V_LTFT, V_MAF, V_ETHANOL, V_O2, V_EGT, V_DTC, V_ATRV, V_COUNT };
#define SHARE_APP_ID 0x55
#define STALE_MS 2000                          // a value the master holds older than this is not shown as fresh here

bool share_ble_is_dash(const char *name) { return name && (!strncmp(name, "RMC - 7 Dash", 12) || !strncmp(name, "RMC - 4 Dash", 12)); }
static int32_t rnd(float v) { return (int32_t)lroundf(v); }

// ======== the SLAVE: a 7" / 4" master's frames into the cache
static volatile int64_t s_rx_us, s_s_us; static volatile uint8_t s_flags; static volatile uint32_t s_quiet_ms;
static char s_name[32];
bool share_ble_has_data(void) { return s_rx_us && esp_timer_get_time() - s_rx_us < 2000000; }
bool share_ble_car_off(void) { return share_ble_has_data() && (s_flags & 2) && s_quiet_ms > 6000; }   // the master's link answers, its car silent 6 s
const char *share_ble_master_name(void) { return share_ble_has_data() ? s_name : ""; }

static void apply_value(int k, float v)
{
    switch (k) {
    case V_RPM: obd_data_set_rpm((uint16_t)(v < 0 ? 0 : v > 65535 ? 65535 : rnd(v))); break;
    case V_SPEED: obd_data_set_speed((uint8_t)(v < 0 ? 0 : v > 255 ? 255 : rnd(v))); break;
    case V_COOLANT: obd_data_set_coolant_temp((int16_t)rnd(v)); break;
    case V_IAT: obd_data_set_intake_temp((int16_t)rnd(v)); break;
    case V_OIL: obd_data_set_oil_temp((int16_t)rnd(v)); break;
    case V_LOAD: obd_data_set_load_pct((int16_t)rnd(v)); break;
    case V_TPS: obd_data_set_tps((int16_t)rnd(v)); break;
    case V_VOLT: obd_data_set_bat_mv(rnd(v * 1000)); break;
    case V_LAMBDA: obd_data_set_afr_x100((int16_t)rnd(v * 1470)); break;   // the 1.85" shows AFR (λ × 14.7)
    case V_OILP: obd_data_set_oil_pressure_x10((int16_t)rnd(v / 10)); break;   // kPa → 0.1 bar
    case V_MAP: { int32_t b = rnd((v - 100) / 10); obd_data_set_boost_x10((int16_t)(b < 0 ? 0 : b)); obd_data_set_ext(OBD_EXT_MAP, rnd(v)); break; }   // as the 1.85"'s own MAP
    case V_FUEL: obd_data_set_ext(OBD_EXT_FUEL_LEVEL, rnd(v)); break;
    case V_TIMING: obd_data_set_ext(OBD_EXT_IGN_ADV, rnd(v * 10)); break;
    case V_FUELP: obd_data_set_ext(OBD_EXT_FUEL_PRESSURE, rnd(v)); break;
    case V_LTFT: obd_data_set_ext(OBD_EXT_LTFT, rnd(v * 10)); break;
    case V_STFT: obd_data_set_ext(OBD_EXT_STFT, rnd(v * 10)); break;
    case V_MAF: obd_data_set_ext(OBD_EXT_MAF, rnd(v * 100)); break;
    case V_ETHANOL: obd_data_set_ext(OBD_EXT_ETHANOL, rnd(v)); break;
    case V_O2: obd_data_set_ext(OBD_EXT_O2, rnd(v * 1000)); break;
    case V_EGT: obd_data_set_ext(OBD_EXT_EGT, rnd(v)); break;
    case V_TRANS: obd_data_set_ext(OBD_EXT_TRANS_TEMP, rnd(v)); break;
    case V_DTC: obd_data_set_ext(OBD_EXT_DTC_COUNT, rnd(v)); break;
    case V_KNOCK: obd_data_set_ext(OBD_EXT_KNOCK, rnd(v * 10)); break;
    }
}
// the dash's gear text / lever → the 1.85"'s gear (-3 D, -2 P, -1 R, 0 N, 1… the gear, 127 none)
static int8_t gear_of(const char *g, const char *l)
{
    if (g[0] >= '1' && g[0] <= '9') return (int8_t)(g[0] - '0');
    const char *w = g[0] ? g : l;
    switch (w[0]) { case 'P': return -2; case 'R': return -1; case 'N': return 0; case 'D': case 'S': case 'M': return -3; }
    return 127;
}
void share_ble_on_frame(const uint8_t *b, int n)
{
    int64_t now = esp_timer_get_time(); s_rx_us = now;
    switch (n ? b[0] : 0) {
    case 'V': for (int i = 0, at = 2; i < b[1] && at + 7 <= n; i++, at += 7) {
        float v; memcpy(&v, b + at + 1, 4); uint16_t age = b[at + 5] | b[at + 6] << 8;
        if (age < STALE_MS) apply_value(b[at], v);                       // stale stays stale (the key-off reads the RPM's age)
    } break;
    case 'G': if (n >= 24) {
        char l[9], g[9]; memcpy(l, b + 1, 8); l[8] = 0; memcpy(g, b + 9, 8); g[8] = 0;
        uint16_t la = b[18] | b[19] << 8, ga = b[20] | b[21] << 8;
        obd_data_set_gear(gear_of(ga < STALE_MS ? g : "", la < STALE_MS ? l : ""));
    } break;
    case 'S': if (n >= 81) {
        s_flags = b[1]; memcpy((void *)&s_quiet_ms, b + 3, 4); s_s_us = now;
        if ((int8_t)b[7] >= 0) obd_data_set_doors((uint8_t)b[7]);
    } break;
    }
}

static esp_gap_ble_cb_t s_prev_gap_cb; static esp_gattc_cb_t s_prev_gattc_cb;
static esp_gatt_if_t s_if = ESP_GATT_IF_NONE;
static volatile bool s_open, s_opening; static uint16_t s_conn = 0xFFFF, s_lo, s_hi, s_h_frames;
static uint8_t s_bda[6]; static volatile int64_t s_open_t;
static void gap_cb(esp_gap_ble_cb_event_t e, esp_ble_gap_cb_param_t *p) { if (s_prev_gap_cb) s_prev_gap_cb(e, p); }
static void gattc_cb(esp_gattc_cb_event_t e, esp_gatt_if_t gif, esp_ble_gattc_cb_param_t *p)
{
    if (e == ESP_GATTC_REG_EVT) { if (p->reg.app_id == SHARE_APP_ID) s_if = gif; else if (s_prev_gattc_cb) s_prev_gattc_cb(e, gif, p); return; }
    if (gif != s_if) { if (s_prev_gattc_cb) s_prev_gattc_cb(e, gif, p); return; }
    switch (e) {
    case ESP_GATTC_OPEN_EVT:
        s_opening = false;
        if (p->open.status != ESP_GATT_OK) { ESP_LOGI(TAG, "master not there (%d)", p->open.status); break; }
        s_conn = p->open.conn_id; s_open = true; s_lo = s_hi = s_h_frames = 0;
        esp_ble_gattc_send_mtu_req(gif, s_conn);
        break;
    case ESP_GATTC_CFG_MTU_EVT: esp_ble_gattc_search_service(gif, s_conn, NULL); break;
    case ESP_GATTC_SEARCH_RES_EVT:
        if (p->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 && p->search_res.srvc_id.uuid.uuid.uuid16 == SHARE_BLE_UUID) { s_lo = p->search_res.start_handle; s_hi = p->search_res.end_handle; }
        break;
    case ESP_GATTC_SEARCH_CMPL_EVT: {
        if (!s_lo) { ESP_LOGW(TAG, "no share service: is the dash set to 主仪表?"); esp_ble_gattc_close(gif, s_conn); break; }
        esp_gattc_char_elem_t ch[4]; uint16_t cnt = 4;
        if (esp_ble_gattc_get_all_char(gif, s_conn, s_lo, s_hi, ch, &cnt, 0) == ESP_GATT_OK)
            for (int i = 0; i < cnt; i++) if (ch[i].uuid.len == ESP_UUID_LEN_16 && ch[i].uuid.uuid.uuid16 == 0x0001) s_h_frames = ch[i].char_handle;
        if (!s_h_frames) { esp_ble_gattc_close(gif, s_conn); break; }
        esp_ble_gattc_register_for_notify(gif, s_bda, s_h_frames);
        break; }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        esp_gattc_descr_elem_t d; uint16_t cnt = 1; uint16_t cccd = s_h_frames + 1;
        esp_bt_uuid_t u = {.len = ESP_UUID_LEN_16, .uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG};
        if (esp_ble_gattc_get_descr_by_char_handle(gif, s_conn, s_h_frames, u, &d, &cnt) == ESP_GATT_OK && cnt) cccd = d.handle;
        uint8_t on[2] = {1, 0};
        esp_ble_gattc_write_char_descr(gif, s_conn, cccd, sizeof(on), on, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
        ESP_LOGI(TAG, "linked to the 主仪表 %s", s_name);
        break; }
    case ESP_GATTC_NOTIFY_EVT: if (p->notify.handle == s_h_frames) share_ble_on_frame(p->notify.value, p->notify.value_len); break;
    case ESP_GATTC_DISCONNECT_EVT: case ESP_GATTC_CLOSE_EVT: s_open = false; s_opening = false; s_conn = 0xFFFF; break;
    default: break;
    }
}
static void client_task(void *a)
{
    for (;; vTaskDelay(pdMS_TO_TICKS(1000))) {
        int64_t now = esp_timer_get_time();
        if (s_open && s_rx_us && now - s_rx_us > 6000000 && now - s_open_t > 10000000) { ESP_LOGW(TAG, "master silent: linking again"); esp_ble_gattc_close(s_if, s_conn); continue; }
        if (s_open || s_opening || s_if == ESP_GATT_IF_NONE) continue;
        s_opening = true; s_open_t = now;
        if (esp_ble_gattc_open(s_if, s_bda, BLE_ADDR_TYPE_PUBLIC, true) != ESP_OK) s_opening = false;
        for (int i = 0; i < 8 && s_opening; i++) vTaskDelay(pdMS_TO_TICKS(500));
        if (s_opening) { s_opening = false; esp_ble_gattc_close(s_if, 0); }  // shortcut: an open that never answers is dropped after 4 s
    }
}
void share_ble_client_start(void)
{
    static bool started;
    const nvs_user_cfg_t *cfg = nvs_cfg_get();
    if (!share_ble_is_dash(cfg->ble_device_name)) return;               // paired to a 1.85": its ESP-NOW, as before
    memcpy(s_bda, cfg->espnow_master_mac, 6); snprintf(s_name, sizeof(s_name), "%s", cfg->ble_device_name);
    if (started) { if (s_open) esp_ble_gattc_close(s_if, s_conn); return; }   // a new pick: the task links it
    started = true;
    elm327_ble_ensure_stack_init();
    s_prev_gap_cb = esp_ble_gap_get_callback(); s_prev_gattc_cb = esp_ble_gattc_get_callback();
    esp_ble_gap_register_callback(gap_cb); esp_ble_gattc_register_callback(gattc_cb);
    esp_ble_gattc_app_register(SHARE_APP_ID);
    xTaskCreate(client_task, "share_ble", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "SLAVE of the dash %s", s_name);
}

// ======== the MASTER: the cache as the dashes' frames (racechrono_ble_diy.c notifies them every 40 ms / 0.5 s)
static int put_v(uint8_t *b, int n, int k, float v, bool ok) { if (!ok) return n; b[n] = k; memcpy(b + n + 1, &v, 4); b[n + 5] = 0; b[n + 6] = 0; b[1]++; return n + 7; }
int share_ble_frame(int kind, uint8_t *b, int max)
{
    if (max < 182) return 0;
    if (kind == 'V') {
        int n = 2; b[0] = 'V'; b[1] = 0;
        bool fresh = obd_data_rpm_age_ms() < STALE_MS;
        if (!fresh) return 0;                                            // the car silent: the slaves' values grow old as here
        n = put_v(b, n, V_RPM, obd_data_get_rpm(), true);
        n = put_v(b, n, V_SPEED, obd_data_get_speed(), true);
        n = put_v(b, n, V_COOLANT, obd_data_get_coolant_temp(), true);
        n = put_v(b, n, V_IAT, obd_data_get_intake_temp(), true);
        n = put_v(b, n, V_OIL, obd_data_get_oil_temp(), obd_data_get_oil_temp() > -40);
        n = put_v(b, n, V_LOAD, obd_data_get_load_pct(), true);
        n = put_v(b, n, V_TPS, obd_data_get_tps(), true);
        n = put_v(b, n, V_VOLT, obd_data_get_bat_mv() / 1000.0f, obd_data_get_bat_mv() > 0);
        int16_t afr = obd_data_get_afr_x100(); n = put_v(b, n, V_LAMBDA, afr / 1470.0f, afr > 0);
        int16_t op = obd_data_get_oil_pressure_x10(); n = put_v(b, n, V_OILP, op * 10.0f, op >= 0);
        static const struct { int k; obd_ext_t e; float div; } X[] = {{V_MAP, OBD_EXT_MAP, 1}, {V_FUEL, OBD_EXT_FUEL_LEVEL, 1}, {V_TIMING, OBD_EXT_IGN_ADV, 10},
            {V_FUELP, OBD_EXT_FUEL_PRESSURE, 1}, {V_LTFT, OBD_EXT_LTFT, 10}, {V_STFT, OBD_EXT_STFT, 10}, {V_MAF, OBD_EXT_MAF, 100}, {V_ETHANOL, OBD_EXT_ETHANOL, 1},
            {V_O2, OBD_EXT_O2, 1000}, {V_EGT, OBD_EXT_EGT, 1}, {V_TRANS, OBD_EXT_TRANS_TEMP, 1}, {V_DTC, OBD_EXT_DTC_COUNT, 1}, {V_KNOCK, OBD_EXT_KNOCK, 10}};
        for (size_t i = 0; i < sizeof(X) / sizeof(X[0]); i++) { int32_t v = obd_data_get_ext(X[i].e); n = put_v(b, n, X[i].k, v / X[i].div, v != OBD_EXT_INVALID); }
        return n;
    }
    if (kind == 'G') {
        memset(b, 0, 24); b[0] = 'G'; int8_t g = obd_data_get_gear();
        if (g >= 1 && g <= 9) b[9] = '0' + g; else if (g == 0) b[9] = 'N'; else if (g == -1) b[1] = 'R'; else if (g == -2) b[1] = 'P'; else if (g == -3) b[1] = 'D';
        b[17] = 0; uint16_t ok = obd_data_rpm_age_ms() < STALE_MS && g != 127 ? 0 : 65535;
        b[18] = b[20] = ok & 255; b[19] = b[21] = ok >> 8; b[22] = b[23] = 255;
        return 24;
    }
    if (kind == 'S') {
        memset(b, 0, 81); b[0] = 'S';
        bool up = elm327_ble_is_connected() && obd_data_rpm_age_ms() < STALE_MS;
        b[1] = (up ? 1 : 0) | (elm327_ble_is_connected() ? 2 : 0);
        uint32_t q = elm327_ble_is_connected() && obd_data_rpm_age_ms() > STALE_MS && obd_data_rpm_age_ms() != UINT32_MAX ? obd_data_rpm_age_ms() : 0; memcpy(b + 3, &q, 4);
        b[7] = (uint8_t)(int8_t)obd_data_get_doors();
        snprintf((char *)b + 57, 24, "ELM327");
        return 81;
    }
    return 0;
}
