#pragma once

// BLE HID Host — 蓝牙物理遥控器支持
// ESP32-S3 作为 BLE Client (Central) + HID Host
// 扫描、配对、连接标准 BLE HID 遥控器（键盘/媒体键）
// 解析 HID 报告，通过 FreeRTOS 队列桥接到 ESPHome 主循环
//
// 使用 ESP-IDF Bluedroid API，与现有 esp32_ble_server 共存（双角色）

#ifndef UNIT_TEST
#include "esphome.h"
#include "esphome/components/esp32_ble/ble.h"
#include "esphome/components/esp32_ble/ble_scan_result.h"
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_gattc_api.h>
#include <esp_gatt_common_api.h>
#include <cstring>
#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#define BLE_HID_MAX_DEVICES     10
#define BLE_HID_NAME_LEN        32
#define BLE_HID_QUEUE_DEPTH     16
#define BLE_HID_SCAN_DURATION   10       // seconds

// HID Service/Char UUIDs (16-bit)
#define HID_SERVICE_UUID        0x1812
#define HID_REPORT_UUID         0x2A4D
#define HID_BOOT_KB_INPUT_UUID  0x2A22
#define HID_BOOT_MOUSE_INPUT_UUID 0x2A33
#define BLE_HID_MAX_REPORTS     8       // HID Service 下最多输入 Report 特征值数

// Consumer page usage IDs (mapped from HID reports)
#define CONSUMER_VOLUME_UP      0xE9
#define CONSUMER_VOLUME_DOWN    0xEA
#define CONSUMER_MUTE           0xE2
#define CONSUMER_PLAY_PAUSE     0xCD
#define CONSUMER_NEXT_TRACK     0xB5
#define CONSUMER_PREV_TRACK     0xB6
#define CONSUMER_POWER          0x30

// Keyboard scan codes (boot protocol)
#define KEY_VOLUME_UP           0x80
#define KEY_VOLUME_DOWN         0x81
#define KEY_MUTE                0x7F

// Battery Service
#define BATTERY_SERVICE_UUID    0x180F
#define BATTERY_LEVEL_UUID      0x2A19

namespace ble_hid_host {

// ── HID 事件类型 (队列中传递) ──
enum HidEventType : uint8_t {
    HID_EVT_NONE           = 0x00,
    HID_EVT_VOLUME_UP      = 0x01,
    HID_EVT_VOLUME_DOWN    = 0x02,
    HID_EVT_MUTE           = 0x03,
    HID_EVT_PLAY           = 0x04,
    HID_EVT_PAUSE          = 0x05,
    HID_EVT_NEXT_TRACK     = 0x06,
    HID_EVT_PREV_TRACK     = 0x07,
    HID_EVT_POWER          = 0x08,
    HID_EVT_CYCLE_INPUT    = 0x09,
    HID_EVT_CONNECTED      = 0x10,  // 内部事件
    HID_EVT_DISCONNECTED   = 0x11,  // 内部事件
    HID_EVT_SCAN_DONE      = 0x12,  // 内部事件
};

struct HidEvent {
    HidEventType type;
    uint32_t timestamp_ms;  // for dedup
};

struct RawFingerprint {
    uint16_t handle = 0;
    uint16_t len = 0;
    uint32_t hash = 0;
    bool valid = false;
    bool learnable = false;
};

enum NormalizedKind : uint8_t {
    NK_NONE = 0,
    NK_SHORT_KEY,
    NK_AXIS_DIR,
    NK_AXIS_CENTER,
    NK_AXIS_POINT,
    NK_RAW_FALLBACK,
};

enum NormalizedValue : uint8_t {
    NV_NONE = 0,
    NV_NAV_UP = 1,
    NV_NAV_DOWN = 2,
    NV_NAV_LEFT = 3,
    NV_NAV_RIGHT = 4,
    NV_NAV_CENTER = 5,
    NV_VOLUME_UP = 11,
    NV_VOLUME_DOWN = 12,
    NV_MUTE_TOGGLE = 13,
};

struct NormalizedFingerprint {
    uint16_t handle = 0;
    uint16_t len = 0;
    uint32_t hash = 0;
    uint8_t kind = NK_NONE;
    uint8_t value = NV_NONE;
    uint16_t aux = 0;
    bool valid = false;
    bool learnable = false;
};

struct LearnedKey {
    bool enabled = false;
    uint16_t handle = 0;
    uint16_t len = 0;
    uint32_t hash = 0;
    uint8_t kind = NK_NONE;
    uint8_t value = NV_NONE;
    uint16_t aux = 0;
    HidEventType action = HID_EVT_NONE;
    uint32_t last_emit_ms = 0;
};

struct TrendWindow {
    uint32_t last_trigger_time = 0;
    bool tracking = false;
    int16_t start_x = 0;
    int16_t start_y = 0;
};

// ── BLE 状态 ──
enum BLEState : uint8_t {
    BLE_IDLE         = 0,
    BLE_SCANNING     = 1,
    BLE_CONNECTING   = 2,
    BLE_CONNECTED    = 3,
};

// ── 发现的设备信息 ──
struct DeviceInfo {
    char     name[BLE_HID_NAME_LEN];
    uint8_t  bda[6];
    esp_ble_addr_type_t addr_type;
    int8_t   rssi;
    bool     has_hid;
};

// ═══════════════════════════════════════════════════
// 内部状态 — Meyers Singleton（ODR 安全，线程安全初始化）
// ═══════════════════════════════════════════════════
struct HidState {
    // BLE 状态
    BLEState        state = BLE_IDLE;
    DeviceInfo      discovered[BLE_HID_MAX_DEVICES] = {};
    int             discovered_count = 0;
    uint8_t         peer_bda[6] = {0};
    esp_ble_addr_type_t peer_addr_type = BLE_ADDR_TYPE_PUBLIC;
    uint16_t        conn_id = 0;
    uint16_t        hid_svc_start = 0;
    uint16_t        hid_svc_end = 0;
    uint16_t        report_char_handles[BLE_HID_MAX_REPORTS] = {0};
    uint16_t        report_cccd_handles[BLE_HID_MAX_REPORTS] = {0};
    int             report_count = 0;
    int             report_cccds_written = 0;
    char            connected_name[BLE_HID_NAME_LEN] = {0};
    uint32_t        last_key_ms = 0;
    bool            auto_reconnect = true;
    bool            reconnect_pending = false;
    int             reconnect_retries = 0;
    uint32_t        reconnect_next_ms = 0;
    bool            paired = false;
    bool            cccd_ready = false;
    bool            auth_cmpl = false;

    // 队列
    QueueHandle_t   hid_queue = nullptr;
    uint32_t        dropped_events = 0;

    // GATT 客户端
    esp_gatt_if_t   gattc_if = 0;
    bool            registered = false;

    // 扫描
    bool            scan_active = false;
    bool            scan_start_pending = false;
    bool            connect_pending = false;
    uint8_t         pending_bda[6] = {0};
    esp_ble_addr_type_t pending_addr_type = BLE_ADDR_TYPE_PUBLIC;
    char            pending_name[BLE_HID_NAME_LEN] = {0};

    // 电池电量
    int             battery_level = -1;     // -1 = unknown
    uint16_t        batt_svc_start = 0;
    uint16_t        batt_svc_end = 0;
    uint16_t        batt_char_handle = 0;
    uint16_t        batt_cccd_handle = 0;

    // 原始 HID 事件 for debug viewer
    uint16_t        last_raw_page = 0;
    uint16_t        last_raw_usage = 0;
    uint8_t         last_raw_value = 0;
    bool            has_raw_event = false;
    NormalizedFingerprint last_raw_fingerprint = {};
    bool            has_raw_fingerprint = false;
    bool            learning_capture_active = false;
    LearnedKey      learned[10] = {};
    TrendWindow     trend = {};

    // ── 同步原语：互斥量 (BLE 回调线程 vs ESPHome 主循环) ──
    SemaphoreHandle_t mux = nullptr;

    // ── 异步 setup() 状态追踪 ──
    uint32_t        setup_start_ms = 0;
    bool            setup_done = false;
    bool            registration_warned = false;
    esphome::esp32_ble::ESP32BLE* ble_parent = nullptr;
    bool            gattc_register_requested = false;
};

// C++11 线程安全的函数局部静态 (Meyers Singleton)
inline HidState& S() {
    static HidState s;
    return s;
}

// ── 前向声明 ──
static void _ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void _ble_gap_scan_cb(const esphome::esp32_ble::BLEScanResult& result);
static void _ble_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
static void _handle_disconnect_event(int reason, const char* source);
static void _schedule_reconnect_from_snapshot(bool auto_reconnect_save, int reconnect_retries_save);
static void _open_pending_connection();

// ── 辅助: 全零 BDA 常量 (替代 C99 复合字面量) ──
static const uint8_t ZERO_BDA[6] = {0};

static bool _request_gattc_app_register() {
    if (S().registered || S().gattc_register_requested) return true;
    esp_err_t ret = esp_ble_gattc_app_register(1);  // app_id 1 (server uses default 0)
    if (ret != ESP_OK) {
        ESP_LOGW("ble_hid", "GATTC app register deferred/failed: %s", esp_err_to_name(ret));
        return false;
    }
    S().gattc_register_requested = true;
    S().setup_start_ms = millis();
    return true;
}

// ── 辅助: 判断 name 是否为 BDA 回退格式 "XX:XX:XX:XX:XX:XX" ──
static bool _is_bda_name(const char* s) {
    if (s == nullptr || strlen(s) != 17) return false;
    return s[2] == ':' && s[5] == ':' && s[8] == ':' && s[11] == ':' && s[14] == ':';
}

// ═══════════════════════════════════════════════════
// AD 数据解析
// ═══════════════════════════════════════════════════
static void _parse_adv_data(const uint8_t* data, uint8_t len,
                            char* out_name, int name_len,
                            bool* out_has_hid) {
    *out_name = '\0';
    *out_has_hid = false;

    int pos = 0;
    while (pos < len) {
        uint8_t field_len = data[pos];
        if (field_len == 0 || pos + field_len >= len) break;
        uint8_t field_type = data[pos + 1];
        const uint8_t* field_data = &data[pos + 2];
        uint8_t field_data_len = field_len - 1;

        switch (field_type) {
            case 0x09: // Complete Local Name
                if (field_data_len > 0) {
                    int copy = field_data_len < (name_len - 1) ? field_data_len : (name_len - 1);
                    memcpy(out_name, field_data, copy);
                    out_name[copy] = '\0';
                }
                break;
            case 0x08: // Shortened Local Name (fallback)
                if (out_name[0] == '\0' && field_data_len > 0) {
                    int copy = field_data_len < (name_len - 1) ? field_data_len : (name_len - 1);
                    memcpy(out_name, field_data, copy);
                    out_name[copy] = '\0';
                }
                break;
            case 0x03: // Complete 16-bit UUIDs
            case 0x02: // Incomplete 16-bit UUIDs
                for (int i = 0; i + 1 < field_data_len; i += 2) {
                    uint16_t uuid = field_data[i] | (field_data[i + 1] << 8);
                    if (uuid == HID_SERVICE_UUID) { *out_has_hid = true; break; }
                }
                break;
            case 0x07: // Complete 128-bit UUIDs
            case 0x06: // Incomplete 128-bit UUIDs
                if (field_data_len >= 16) {
                    const uint8_t hid_128[16] = {
                        0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                        0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00
                    };
                    if (memcmp(field_data, hid_128, 16) == 0) { *out_has_hid = true; }
                }
                break;
        }
        pos += field_len + 1;
    }
}

static bool _is_learnable_raw_report(const uint8_t* data, uint16_t len) {
    if (data == nullptr || len == 0) return false;

    bool all_zero = true;
    for (uint16_t i = 0; i < len; i++) {
        if (data[i] != 0) { all_zero = false; break; }
    }
    if (all_zero) return false;

    bool keyboard_release = false;
    if (len == 8 || len == 9) {
        int key_start = (len == 9) ? 3 : 2;
        int key_end = (len == 9) ? 9 : 8;
        keyboard_release = true;
        for (int i = key_start; i < key_end; i++) {
            if (data[i] != 0) { keyboard_release = false; break; }
        }
        if (keyboard_release) return false;
    }

    bool consumer_release =
        (len == 2 && data[0] == 0x00 && data[1] == 0x00) ||
        (len == 3 && data[1] == 0x00 && data[2] == 0x00) ||
        (len == 4 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x00);
    if (consumer_release) return false;

    return true;
}

static RawFingerprint _make_raw_fingerprint(uint16_t handle, const uint8_t* data, uint16_t len) {
    RawFingerprint fp;
    fp.handle = handle;
    fp.len = len;
    fp.valid = data != nullptr && len > 0;
    fp.learnable = fp.valid && _is_learnable_raw_report(data, len);

    uint32_t hash = 2166136261UL;  // FNV-1a over handle + len + report bytes
    auto mix = [&](uint8_t b) {
        hash ^= b;
        hash *= 16777619UL;
    };
    mix((uint8_t)(handle & 0xFF));
    mix((uint8_t)(handle >> 8));
    mix((uint8_t)(len & 0xFF));
    mix((uint8_t)(len >> 8));
    if (data != nullptr) {
        for (uint16_t i = 0; i < len; i++) mix(data[i]);
    }
    fp.hash = hash == 0 ? 1 : hash;
    return fp;
}

static int32_t _abs32(int32_t v) {
    return v < 0 ? -v : v;
}

static uint32_t _encode_normalized_hash(uint8_t kind, uint8_t value, uint16_t aux) {
    return 0xA5000000UL | ((uint32_t)kind << 16) | ((uint32_t)value << 8) | (uint32_t)(aux & 0xFF);
}

static NormalizedFingerprint _make_normalized_fingerprint(uint16_t handle, uint16_t len,
                                                          uint8_t kind, uint8_t value,
                                                          uint16_t aux = 0) {
    NormalizedFingerprint fp;
    fp.handle = handle;
    fp.len = len;
    fp.kind = kind;
    fp.value = value;
    fp.aux = aux;
    fp.valid = kind != NK_NONE && value != NV_NONE;
    fp.learnable = fp.valid;
    fp.hash = _encode_normalized_hash(kind, value, aux);
    return fp;
}

static NormalizedFingerprint _make_raw_fallback_fingerprint(const RawFingerprint& raw) {
    NormalizedFingerprint fp;
    fp.handle = raw.handle;
    fp.len = raw.len;
    fp.kind = NK_RAW_FALLBACK;
    fp.value = (uint8_t)(raw.hash & 0xFF);
    fp.aux = (uint16_t)((raw.hash >> 8) & 0xFFFF);
    fp.hash = raw.hash;
    fp.valid = raw.valid;
    fp.learnable = raw.learnable;
    return fp;
}

static NormalizedFingerprint _normalize_report(uint16_t handle, const uint8_t* data, uint16_t len) {
    NormalizedFingerprint none;
    none.handle = handle;
    none.len = len;
    if (data == nullptr || len == 0) return none;

    if (len == 2) {
        if (data[1] == 0x20) {
            return _make_normalized_fingerprint(handle, len, NK_SHORT_KEY, NV_VOLUME_DOWN);
        }
        if (data[0] == 0x40 || data[0] == 0x80) {
            return _make_normalized_fingerprint(handle, len, NK_SHORT_KEY, NV_MUTE_TOGGLE);
        }
        RawFingerprint raw = _make_raw_fingerprint(handle, data, len);
        if (!raw.learnable) return none;
        return _make_raw_fallback_fingerprint(raw);
    }

    if (len == 10) {
        int16_t x = (int16_t)(data[1] | (data[2] << 8));
        int16_t y = (int16_t)(data[3] | (data[4] << 8));
        bool is_active = (data[0] == 0x83);
        uint32_t now = millis();
        TrendWindow& trend = S().trend;

        if (_abs32((int32_t)x - 0x0E46) < 160 && _abs32((int32_t)y - 0x04CA) < 160) {
            trend.tracking = false;
            return _make_normalized_fingerprint(handle, len, NK_AXIS_POINT, NV_VOLUME_UP);
        }

        if (is_active && _abs32((int32_t)x - 0x0800) < 180 && _abs32((int32_t)y - 0x0666) < 180) {
            trend.tracking = false;
            return _make_normalized_fingerprint(handle, len, NK_AXIS_CENTER, NV_NAV_CENTER);
        }

        if (now - trend.last_trigger_time < 150) {
            return none;
        }

        if (!trend.tracking) {
            if (is_active && (_abs32((int32_t)x - 0x0800) > 180 || _abs32((int32_t)y - 0x0666) > 180)) {
                trend.tracking = true;
                trend.start_x = x;
                trend.start_y = y;
            }
            return none;
        }

        if (!is_active) {
            trend.tracking = false;
            return none;
        }

        int32_t dx = (int32_t)x - (int32_t)trend.start_x;
        int32_t dy = (int32_t)y - (int32_t)trend.start_y;
        const int32_t TRIGGER_THRESHOLD = 800;
        if (_abs32(dy) > _abs32(dx) && _abs32(dy) > TRIGGER_THRESHOLD) {
            trend.tracking = false;
            trend.last_trigger_time = now;
            return _make_normalized_fingerprint(handle, len, NK_AXIS_DIR, dy > 0 ? NV_NAV_UP : NV_NAV_DOWN);
        }
        if (_abs32(dx) >= _abs32(dy) && _abs32(dx) > TRIGGER_THRESHOLD) {
            trend.tracking = false;
            trend.last_trigger_time = now;
            return _make_normalized_fingerprint(handle, len, NK_AXIS_DIR, dx > 0 ? NV_NAV_LEFT : NV_NAV_RIGHT);
        }
        return none;
    }

    RawFingerprint raw = _make_raw_fingerprint(handle, data, len);
    if (!raw.learnable) return none;
    return _make_raw_fallback_fingerprint(raw);
}

static HidEventType _match_learned_key(const NormalizedFingerprint& fp) {
    if (!fp.valid || !fp.learnable) return HID_EVT_NONE;
    uint32_t now = millis();
    for (int action = (int)HID_EVT_VOLUME_UP; action <= (int)HID_EVT_CYCLE_INPUT; action++) {
        LearnedKey& key = S().learned[action];
        if (!key.enabled) continue;
        if (key.handle != fp.handle || key.len != fp.len) continue;
        if (key.kind != fp.kind || key.value != fp.value || key.aux != fp.aux) continue;
        if (key.kind == NK_RAW_FALLBACK && key.hash != fp.hash) continue;

        uint32_t gap_ms = (key.action == HID_EVT_VOLUME_UP || key.action == HID_EVT_VOLUME_DOWN) ? 80 : 250;
        if (now - key.last_emit_ms < gap_ms) return HID_EVT_NONE;
        key.last_emit_ms = now;
        ESP_LOGI("ble_hid", "HID learned normalized match action=%d handle=0x%04x len=%u kind=%u value=%u hash=0x%08x",
                 (int)key.action, fp.handle, fp.len, fp.kind, fp.value, (unsigned)fp.hash);
        return key.action;
    }
    return HID_EVT_NONE;
}

// ═══════════════════════════════════════════════════
// HID 报告解析 — 基于市售 BLE 遥控器实测码
// ═══════════════════════════════════════════════════
// 注释标记: [标准]=USB HID 规范定义, [兼容]=廉价遥控器实测有效, [自定义]=特定遥控器
//
// 参考资料: USB HID Usage Tables v1.22
//   Consumer Page (0x0C): 0xE9=VolInc 0xEA=VolDec 0xE2=Mute
//                         0xCD=Play/Pause 0xB5=Next 0xB6=Prev 0x30=Power
//   Keyboard Page (0x07): 0x28=Enter 0x29=Esc 0x2C=Space
//                         0x4F→0x52=←↑↓→  0x66=Power 0x68=F13
//   廉价遥控器兼容码: 将 Consumer 码塞入 Keyboard 报告 (0xE9/0xEA/0xCD/0xB5/0xB6/0x30)
// HID reports are learned-only. Legacy preset parsing was removed to avoid ambiguous remote conflicts.

static void _queue_event_from_task(HidEventType type) {
    if (S().hid_queue == nullptr) return;
    HidEvent evt;
    evt.type = type;
    evt.timestamp_ms = millis();
    if (xQueueSend(S().hid_queue, &evt, 0) != pdTRUE) {
        // Atomic increment (spinlock not needed for counter)
        S().dropped_events++;
        uint32_t dropped = S().dropped_events;
        if (dropped == 1 || (dropped % 100) == 0) {
            ESP_LOGW("ble_hid", "队列满, 丢弃事件 type=%d (累计 %lu)", type, (unsigned long)dropped);
        }
    }
}

static void _schedule_reconnect_from_snapshot(bool auto_reconnect_save, int reconnect_retries_save) {
    if (!auto_reconnect_save) return;

    uint32_t delay_ms;
    if (reconnect_retries_save < 3) {
        delay_ms = 2000 + (reconnect_retries_save * 3000);
    } else {
        delay_ms = 60000;
    }

    if (xSemaphoreTake(S().mux, 0)) {
        S().reconnect_pending = true;
        S().reconnect_next_ms = millis() + delay_ms;
        S().reconnect_retries = reconnect_retries_save + 1;
        // 防溢出: 如果重试次数异常高(如运行数月后累积)，限制在安全范围
        if (S().reconnect_retries > 1000) S().reconnect_retries = 4;
        xSemaphoreGive(S().mux);
    }

    ESP_LOGI("ble_hid", "将在 %d 秒后重试连接 (第 %d 次)...",
             reconnect_retries_save < 3 ? 2 + reconnect_retries_save * 3 : 60,
             reconnect_retries_save + 1);
}

static void _handle_disconnect_event(int reason, const char* source) {
    bool should_emit = false;
    bool auto_reconnect_save = false;
    int reconnect_retries_save = 0;

    if (xSemaphoreTake(S().mux, 0)) {
        should_emit = (S().state != BLE_IDLE) || (S().conn_id != 0) ||
                      (S().report_count > 0) || S().cccd_ready ||
                      (S().connected_name[0] != '\0');
        auto_reconnect_save = S().auto_reconnect;
        reconnect_retries_save = S().reconnect_retries;

        if (should_emit) {
            S().state = BLE_IDLE;
            S().conn_id = 0;
            S().connect_pending = false;
            S().hid_svc_start = 0;
            S().hid_svc_end = 0;
            memset(S().report_char_handles, 0, sizeof(S().report_char_handles));
            memset(S().report_cccd_handles, 0, sizeof(S().report_cccd_handles));
            S().report_count = 0;
            S().report_cccds_written = 0;
            S().cccd_ready = false;
            S().auth_cmpl = false;
            S().connected_name[0] = '\0';
            S().battery_level = -1;
            S().batt_svc_start = 0;
            S().batt_svc_end = 0;
            S().batt_char_handle = 0;
            S().batt_cccd_handle = 0;
        }

        xSemaphoreGive(S().mux);
    }

    if (!should_emit) return;

    ESP_LOGI("ble_hid", "已断开 (%s, reason=%d)", source, reason);
    _queue_event_from_task(HID_EVT_DISCONNECTED);
    _schedule_reconnect_from_snapshot(auto_reconnect_save, reconnect_retries_save);
}

static void _open_pending_connection() {
    uint8_t bda[6] = {0};
    esp_ble_addr_type_t addr_type = BLE_ADDR_TYPE_PUBLIC;
    char name[BLE_HID_NAME_LEN] = {0};
    esp_gatt_if_t gattc_if = 0;
    bool valid = false;

    if (xSemaphoreTake(S().mux, 0)) {
        bool busy = (S().state == BLE_CONNECTING || S().state == BLE_CONNECTED);
        if (S().connect_pending && !busy && S().registered && S().gattc_if > 0) {
            memcpy(bda, S().pending_bda, 6);
            addr_type = S().pending_addr_type;
            strncpy(name, S().pending_name, BLE_HID_NAME_LEN - 1);
            name[BLE_HID_NAME_LEN - 1] = '\0';
            gattc_if = S().gattc_if;

            S().connect_pending = false;
            memcpy(S().peer_bda, bda, 6);
            S().peer_addr_type = addr_type;
            strncpy(S().connected_name, name, BLE_HID_NAME_LEN - 1);
            S().connected_name[BLE_HID_NAME_LEN - 1] = '\0';
            S().paired = false;
            S().cccd_ready = false;
            S().auth_cmpl = false;
            S().state = BLE_CONNECTING;
            S().reconnect_retries = 0;
            valid = memcmp(bda, ZERO_BDA, 6) != 0;
        }
        xSemaphoreGive(S().mux);
    }

    if (!valid) return;

    ESP_LOGI("ble_hid", "正在连接 %s...", name);
    esp_err_t ret = esp_ble_gattc_open(gattc_if, bda, addr_type, true);
    if (ret != ESP_OK) {
        ESP_LOGW("ble_hid", "GATT open 请求失败: %s", esp_err_to_name(ret));
        _handle_disconnect_event((int) ret, "open_request");
    }
}

// ═══════════════════════════════════════════════════
// GAP 回调 — 扫描结果处理
// ═══════════════════════════════════════════════════
static void _ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_SCAN_RESULT_EVT: {
            if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break;

            // Parse advertising data and scan response. Many tiny HID remotes put
            // the human-readable name in scan response instead of the primary AD.
            char name[BLE_HID_NAME_LEN] = {0};
            char scan_name[BLE_HID_NAME_LEN] = {0};
            bool has_hid = false;
            bool scan_has_hid = false;
            _parse_adv_data(param->scan_rst.ble_adv, param->scan_rst.adv_data_len,
                           name, sizeof(name), &has_hid);
            if (param->scan_rst.scan_rsp_len > 0) {
                _parse_adv_data(param->scan_rst.ble_adv + param->scan_rst.adv_data_len,
                               param->scan_rst.scan_rsp_len,
                               scan_name, sizeof(scan_name), &scan_has_hid);
                if (name[0] == '\0' && scan_name[0] != '\0') {
                    strncpy(name, scan_name, BLE_HID_NAME_LEN - 1);
                    name[BLE_HID_NAME_LEN - 1] = '\0';
                }
                has_hid = has_hid || scan_has_hid;
            }

            // Filter: accept HID devices, or devices with a name (scan response may contain HID UUID)
            if (!has_hid && name[0] == '\0') break;

            // Lock and update shared device list
            if (xSemaphoreTake(S().mux, 0)) {

                // Dedup by BDA (always, even when list is full — updates RSSI)
                bool found = false;
                for (int i = 0; i < S().discovered_count; i++) {
                    if (memcmp(S().discovered[i].bda, param->scan_rst.bda, 6) == 0) {
                        if (param->scan_rst.rssi > S().discovered[i].rssi) {
                            S().discovered[i].rssi = param->scan_rst.rssi;
                            // Re-sort: bubble up if this device is now stronger than previous
                            while (i > 0 && S().discovered[i].rssi > S().discovered[i-1].rssi) {
                                DeviceInfo tmp = S().discovered[i];
                                S().discovered[i] = S().discovered[i-1];
                                S().discovered[i-1] = tmp;
                                i--;
                            }
                        }
                        if (has_hid) S().discovered[i].has_hid = true;
                        // Scan response may carry the full name (initial AD often has BDA only)
                        if (name[0] != '\0' && _is_bda_name(S().discovered[i].name)) {
                            strncpy(S().discovered[i].name, name, BLE_HID_NAME_LEN - 1);
                        }
                        found = true;
                        break;
                    }
                }

                if (!found && S().discovered_count < BLE_HID_MAX_DEVICES && (has_hid || name[0] != '\0')) {
                    // Insert sorted by RSSI (strongest first)
                    int insert_pos = S().discovered_count;
                    for (int i = 0; i < S().discovered_count; i++) {
                        if (param->scan_rst.rssi > S().discovered[i].rssi) {
                            insert_pos = i;
                            break;
                        }
                    }
                    // Shift down
                    for (int i = S().discovered_count; i > insert_pos; i--) {
                        S().discovered[i] = S().discovered[i - 1];
                    }
                    // Store
                    memcpy(S().discovered[insert_pos].bda, param->scan_rst.bda, 6);
                    S().discovered[insert_pos].rssi = param->scan_rst.rssi;
                    S().discovered[insert_pos].addr_type = param->scan_rst.ble_addr_type;
                    S().discovered[insert_pos].has_hid = has_hid;
                    if (name[0] != '\0') {
                        strncpy(S().discovered[insert_pos].name, name, BLE_HID_NAME_LEN - 1);
                    } else {
                        snprintf(S().discovered[insert_pos].name, BLE_HID_NAME_LEN,
                                "%02X:%02X:%02X:%02X:%02X:%02X",
                                param->scan_rst.bda[0], param->scan_rst.bda[1],
                                param->scan_rst.bda[2], param->scan_rst.bda[3],
                                param->scan_rst.bda[4], param->scan_rst.bda[5]);
                    }
                    S().discovered_count++;
                }

                xSemaphoreGive(S().mux);
            }
            break;
        }

        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
            bool pending = false;
            if (xSemaphoreTake(S().mux, 0)) {
                pending = S().scan_start_pending;
                xSemaphoreGive(S().mux);
            }
            if (!pending) break;

            esp_err_t ret = esp_ble_gap_start_scanning(BLE_HID_SCAN_DURATION);
            if (ret != ESP_OK) {
                ESP_LOGE("ble_hid", "启动扫描失败: %s", esp_err_to_name(ret));
                if (xSemaphoreTake(S().mux, 0)) {
                    S().scan_start_pending = false;
                    S().scan_active = false;
                    if (S().state == BLE_SCANNING) S().state = BLE_IDLE;
                    xSemaphoreGive(S().mux);
                }
            }
            break;
        }

        case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
            if (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                if (xSemaphoreTake(S().mux, 0)) {
                    S().scan_start_pending = false;
                    S().scan_active = true;
                    S().state = BLE_SCANNING;
                    xSemaphoreGive(S().mux);
                }
                ESP_LOGI("ble_hid", "开始扫描 (%ds)...", BLE_HID_SCAN_DURATION);
            } else {
                ESP_LOGW("ble_hid", "扫描启动失败 (status=%d)", param->scan_start_cmpl.status);
                if (xSemaphoreTake(S().mux, 0)) {
                    S().scan_start_pending = false;
                    S().scan_active = false;
                    if (S().state == BLE_SCANNING) S().state = BLE_IDLE;
                    xSemaphoreGive(S().mux);
                }
            }
            break;

        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
            {
            bool should_emit_done = false;
            bool should_open_pending = false;
            if (xSemaphoreTake(S().mux, 0)) {
                S().scan_active = false;
                S().scan_start_pending = false;
                if (S().state == BLE_SCANNING) {
                    S().state = BLE_IDLE;
                    should_emit_done = true;
                }
                should_open_pending = S().connect_pending;
                xSemaphoreGive(S().mux);
            }
            if (should_emit_done) _queue_event_from_task(HID_EVT_SCAN_DONE);
            if (should_open_pending) _open_pending_connection();
            break;
            }

        // ── 安全握手: 商业遥控器连接后会主动请求配对 ──
        case ESP_GAP_BLE_SEC_REQ_EVT:
            ESP_LOGI("ble_hid", "收到安全请求, 同意配对");
            esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
            break;

        // ── 配对完成事件 (仅当 _cccd_ready 也满足时设置 _paired) ──
        case ESP_GAP_BLE_AUTH_CMPL_EVT: {
            esp_ble_auth_cmpl_t auth = param->ble_security.auth_cmpl;
            if (auth.success) {
                if (xSemaphoreTake(S().mux, 0)) {
                    S().auth_cmpl = true;
                    if (S().cccd_ready && !S().paired) {
                        S().paired = true;
                        S().reconnect_retries = 0;
                    }
                    xSemaphoreGive(S().mux);
                }
                ESP_LOGI("ble_hid", "配对完成 (success=%d)", auth.success);
            } else {
                ESP_LOGW("ble_hid", "配对失败 (reason=0x%x)", auth.fail_reason);
            }
            break;
        }

        default:
            break;
    }
}

// ═══════════════════════════════════════════════════
// GATT Client 回调 — 连接/服务发现/HID通知
// ═══════════════════════════════════════════════════
static void _ble_gap_scan_cb(const esphome::esp32_ble::BLEScanResult& result) {
    esp_ble_gap_cb_param_t param = {};
    param.scan_rst.search_evt = (esp_gap_search_evt_t) result.search_evt;
    memcpy(param.scan_rst.bda, result.bda, sizeof(param.scan_rst.bda));
    param.scan_rst.ble_addr_type = (esp_ble_addr_type_t) result.ble_addr_type;
    param.scan_rst.rssi = result.rssi;
    param.scan_rst.adv_data_len = result.adv_data_len;
    param.scan_rst.scan_rsp_len = result.scan_rsp_len;
    size_t adv_len = (size_t) result.adv_data_len + (size_t) result.scan_rsp_len;
    if (adv_len > sizeof(param.scan_rst.ble_adv)) adv_len = sizeof(param.scan_rst.ble_adv);
    memcpy(param.scan_rst.ble_adv, result.ble_adv, adv_len);
    _ble_gap_cb(ESP_GAP_BLE_SCAN_RESULT_EVT, &param);
}

static void _ble_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) {
    switch (event) {
        case ESP_GATTC_REG_EVT:
            if (param->reg.status == ESP_GATT_OK) {
                if (xSemaphoreTake(S().mux, 0)) {
                    S().gattc_if = gattc_if;
                    S().registered = true;
                    xSemaphoreGive(S().mux);
                }
                ESP_LOGI("ble_hid", "GATT Client 注册成功 (app_id=%d)", param->reg.app_id);
            }
            break;

        case ESP_GATTC_OPEN_EVT:
            if (param->open.status == ESP_GATT_OK) {
                uint8_t local_bda[6] = {0};
                uint16_t local_conn_id = param->open.conn_id;
                if (xSemaphoreTake(S().mux, 0)) {
                    S().conn_id = param->open.conn_id;
                    memcpy(S().peer_bda, param->open.remote_bda, 6);
                    memcpy(local_bda, param->open.remote_bda, 6);
                    S().state = BLE_CONNECTING;
                    S().hid_svc_end = 0;
                    memset(S().report_char_handles, 0, sizeof(S().report_char_handles));
                    memset(S().report_cccd_handles, 0, sizeof(S().report_cccd_handles));
                    S().report_count = 0;
                    S().report_cccds_written = 0;
                    xSemaphoreGive(S().mux);
                }
                esp_err_t sec_ret = esp_ble_set_encryption(local_bda, ESP_BLE_SEC_ENCRYPT_NO_MITM);
                if (sec_ret != ESP_OK) {
                    ESP_LOGW("ble_hid", "请求 BLE 加密失败: %s", esp_err_to_name(sec_ret));
                }
                ESP_LOGI("ble_hid", "已连接, 开始服务发现...");
                esp_ble_gattc_search_service(gattc_if, local_conn_id, nullptr);
            } else {
                ESP_LOGW("ble_hid", "连接失败 (status=%d)", param->open.status);
                _handle_disconnect_event(param->open.status, "open");
            }
            break;

        case ESP_GATTC_CLOSE_EVT:
            _handle_disconnect_event(param->close.reason, "close");
            break;

        case ESP_GATTC_SEARCH_RES_EVT: {
            // Check if this service is HID (0x1812) or Battery (0x180F)
            uint16_t svc_uuid16 = 0;
            if (param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16) {
                svc_uuid16 = param->search_res.srvc_id.uuid.uuid.uuid16;
            }
            if (svc_uuid16 == HID_SERVICE_UUID) {
                uint16_t s = param->search_res.start_handle;
                uint16_t e = param->search_res.end_handle;
                if (xSemaphoreTake(S().mux, 0)) {
                    S().hid_svc_start = s;
                    S().hid_svc_end   = e;
                    xSemaphoreGive(S().mux);
                }
                ESP_LOGD("ble_hid", "发现 HID Service (0x%04x - 0x%04x)", s, e);
            } else if (svc_uuid16 == BATTERY_SERVICE_UUID) {
                uint16_t s = param->search_res.start_handle;
                uint16_t e = param->search_res.end_handle;
                if (xSemaphoreTake(S().mux, 0)) {
                    S().batt_svc_start = s;
                    S().batt_svc_end   = e;
                    xSemaphoreGive(S().mux);
                }
                ESP_LOGD("ble_hid", "发现 Battery Service (0x%04x - 0x%04x)", s, e);
            }
            break;
        }

        case ESP_GATTC_SEARCH_CMPL_EVT: {
            // Snapshot shared state under lock for later API calls
            uint16_t local_conn_id = 0;
            uint8_t local_peer_bda[6] = {0};
            uint16_t local_hid_start = 0, local_hid_end = 0;
            uint16_t local_batt_start = 0, local_batt_end = 0;
            if (xSemaphoreTake(S().mux, 0)) {
                local_conn_id = S().conn_id;
                memcpy(local_peer_bda, S().peer_bda, 6);
                local_hid_start = S().hid_svc_start;
                local_hid_end = S().hid_svc_end;
                local_batt_start = S().batt_svc_start;
                local_batt_end = S().batt_svc_end;
                xSemaphoreGive(S().mux);
            } else {
                break;
            }

            if (local_hid_start > 0 && local_hid_end > 0) {
                ESP_LOGI("ble_hid", "服务发现完成, 枚举 HID Report 特征值...");

                // ── 使用局部变量收集数据，之后统一加锁写入 S() ──
                int local_report_count = 0;
                uint16_t local_handles[BLE_HID_MAX_REPORTS] = {0};
                uint16_t local_cccds[BLE_HID_MAX_REPORTS] = {0};
                int local_batt_char = 0;
                int local_batt_cccd = 0;

                // ── 构造 UUID 对象 (避免 C++ 复合字面量) ──
                esp_bt_uuid_t uuid_hid_report;
                uuid_hid_report.len = ESP_UUID_LEN_16;
                uuid_hid_report.uuid.uuid16 = HID_REPORT_UUID;

                esp_bt_uuid_t uuid_cccd;
                uuid_cccd.len = ESP_UUID_LEN_16;
                uuid_cccd.uuid.uuid16 = 0x2902;

                esp_bt_uuid_t uuid_boot_kb;
                uuid_boot_kb.len = ESP_UUID_LEN_16;
                uuid_boot_kb.uuid.uuid16 = HID_BOOT_KB_INPUT_UUID;

                esp_bt_uuid_t uuid_boot_mouse;
                uuid_boot_mouse.len = ESP_UUID_LEN_16;
                uuid_boot_mouse.uuid.uuid16 = HID_BOOT_MOUSE_INPUT_UUID;

                esp_bt_uuid_t uuid_battery;
                uuid_battery.len = ESP_UUID_LEN_16;
                uuid_battery.uuid.uuid16 = BATTERY_LEVEL_UUID;

                // Enumerate notifiable input Report characteristics (0x2A4D).
                // HID output/feature reports may share UUID 0x2A4D but cannot emit keys.
                {
                    esp_gattc_char_elem_t results[BLE_HID_MAX_REPORTS];
                    uint16_t count = BLE_HID_MAX_REPORTS;
                    esp_gatt_status_t status = esp_ble_gattc_get_char_by_uuid(
                        gattc_if, local_conn_id, local_hid_start, local_hid_end,
                        uuid_hid_report,
                        results, &count);
                    if (status == ESP_GATT_OK || status == ESP_GATT_MORE) {
                        for (int i = 0; i < count; i++) {
                            bool can_notify = (results[i].properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) ||
                                              (results[i].properties & ESP_GATT_CHAR_PROP_BIT_INDICATE);
                            if (!can_notify) {
                                ESP_LOGI("ble_hid", "  skip Report char_handle=0x%04x props=0x%02x (not input notify)",
                                         results[i].char_handle, results[i].properties);
                                continue;
                            }
                            int dst = local_report_count++;
                            local_handles[dst] = results[i].char_handle;
                            ESP_LOGI("ble_hid", "  Input Report[%d] char_handle=0x%04x props=0x%02x",
                                     dst, results[i].char_handle, results[i].properties);
                            esp_gattc_descr_elem_t descr;
                            uint16_t d_count = 1;
                            if (esp_ble_gattc_get_descr_by_char_handle(
                                gattc_if, local_conn_id, results[i].char_handle,
                                uuid_cccd,
                                &descr, &d_count) == ESP_GATT_OK && d_count > 0) {
                                local_cccds[dst] = descr.handle;
                                ESP_LOGD("ble_hid", "    CCCD handle=0x%04x", descr.handle);
                            }
                        }
                    }
                }

                // Fallback: Boot Keyboard Input (0x2A22)
                if (local_report_count == 0) {
                    esp_gattc_char_elem_t char_result;
                    uint16_t count = 1;
                    esp_gatt_status_t status = esp_ble_gattc_get_char_by_uuid(
                        gattc_if, local_conn_id, local_hid_start, local_hid_end,
                        uuid_boot_kb,
                        &char_result, &count);
                    if (status == ESP_GATT_OK && count > 0) {
                        local_handles[0] = char_result.char_handle;
                        local_report_count = 1;
                        ESP_LOGI("ble_hid", "  回退: 使用 Boot KB Input (handle=0x%04x)", char_result.char_handle);
                        esp_gattc_descr_elem_t descr;
                        uint16_t d_count = 1;
                        if (esp_ble_gattc_get_descr_by_char_handle(
                            gattc_if, local_conn_id, char_result.char_handle,
                            uuid_cccd,
                            &descr, &d_count) == ESP_GATT_OK && d_count > 0) {
                            local_cccds[0] = descr.handle;
                        }
                    }
                }

                // Fallback: Boot Mouse Input (0x2A33), common on small media/camera remotes
                if (local_report_count == 0) {
                    esp_gattc_char_elem_t char_result;
                    uint16_t count = 1;
                    esp_gatt_status_t status = esp_ble_gattc_get_char_by_uuid(
                        gattc_if, local_conn_id, local_hid_start, local_hid_end,
                        uuid_boot_mouse,
                        &char_result, &count);
                    if (status == ESP_GATT_OK && count > 0) {
                        local_handles[0] = char_result.char_handle;
                        local_report_count = 1;
                        ESP_LOGI("ble_hid", "  fallback: Boot Mouse Input handle=0x%04x", char_result.char_handle);
                        esp_gattc_descr_elem_t descr;
                        uint16_t d_count = 1;
                        if (esp_ble_gattc_get_descr_by_char_handle(
                            gattc_if, local_conn_id, char_result.char_handle,
                            uuid_cccd,
                            &descr, &d_count) == ESP_GATT_OK && d_count > 0) {
                            local_cccds[0] = descr.handle;
                        }
                    }
                }

                // Battery Service discovery
                if (local_batt_start > 0 && local_batt_end > 0) {
                    esp_gattc_char_elem_t batt_char;
                    uint16_t b_count = 1;
                    if (esp_ble_gattc_get_char_by_uuid(
                        gattc_if, local_conn_id, local_batt_start, local_batt_end,
                        uuid_battery,
                        &batt_char, &b_count) == ESP_GATT_OK && b_count > 0) {
                        local_batt_char = batt_char.char_handle;
                        esp_gattc_descr_elem_t batt_descr;
                        uint16_t bd_count = 1;
                        if (esp_ble_gattc_get_descr_by_char_handle(
                            gattc_if, local_conn_id, local_batt_char,
                            uuid_cccd,
                            &batt_descr, &bd_count) == ESP_GATT_OK && bd_count > 0) {
                            local_batt_cccd = batt_descr.handle;
                        }
                    }
                }

                // ── 统一写入共享状态 (加锁) ──
                if (xSemaphoreTake(S().mux, 0)) {
                    S().report_count = local_report_count;
                    for (int i = 0; i < local_report_count; i++) {
                        S().report_char_handles[i] = local_handles[i];
                        S().report_cccd_handles[i] = local_cccds[i];
                    }
                    S().batt_char_handle = local_batt_char;
                    S().batt_cccd_handle = local_batt_cccd;
                    xSemaphoreGive(S().mux);
                }

                // ── API 调用 (用局部数据，无需持锁) ──
                for (int i = 0; i < local_report_count; i++) {
                    esp_ble_gattc_register_for_notify(gattc_if, local_peer_bda, local_handles[i]);
                }

                if (local_report_count == 0) {
                    ESP_LOGW("ble_hid", "未找到 HID Report characteristic, 强制断开");
                    esp_ble_gattc_close(gattc_if, local_conn_id);
                }

                if (local_batt_char <= 0) {
                    ESP_LOGI("ble_hid", "Battery Level char not found; remote battery is unknown");
                }
                if (local_batt_char > 0) {
                    esp_ble_gattc_register_for_notify(gattc_if, local_peer_bda, local_batt_char);
                    esp_ble_gattc_read_char(gattc_if, local_conn_id, local_batt_char, ESP_GATT_AUTH_REQ_NONE);
                    ESP_LOGI("ble_hid", "Battery Level read requested");
                    ESP_LOGI("ble_hid", "找到 Battery Level char (handle=0x%04x)", local_batt_char);
                }
            } else {
                ESP_LOGW("ble_hid", "未找到 HID Service, 断开连接");
                esp_ble_gattc_close(gattc_if, local_conn_id);
            }
            break;
        }

        case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
            uint16_t handle = param->reg_for_notify.handle;

            // 快照共享状态 (加锁读取，之后在锁外比对)
            int local_report_count = 0;
            uint16_t local_handles[BLE_HID_MAX_REPORTS] = {0};
            uint16_t local_cccds[BLE_HID_MAX_REPORTS] = {0};
            uint16_t local_batt_char = 0;
            uint16_t local_batt_cccd = 0;
            uint16_t local_conn_id = 0;
            if (xSemaphoreTake(S().mux, 0)) {
                local_report_count = S().report_count;
                for (int i = 0; i < local_report_count && i < BLE_HID_MAX_REPORTS; i++) {
                    local_handles[i] = S().report_char_handles[i];
                    local_cccds[i] = S().report_cccd_handles[i];
                }
                local_batt_char = S().batt_char_handle;
                local_batt_cccd = S().batt_cccd_handle;
                local_conn_id = S().conn_id;
                xSemaphoreGive(S().mux);
            }

            bool is_report = false;
            for (int i = 0; i < local_report_count; i++) {
                if (handle == local_handles[i]) { is_report = true; break; }
            }
            if (param->reg_for_notify.status != ESP_GATT_OK) {
                ESP_LOGW("ble_hid", "注册 HID notify 失败 handle=0x%04x status=%d",
                         handle, param->reg_for_notify.status);
                if (is_report && local_conn_id > 0) esp_ble_gattc_close(gattc_if, local_conn_id);
                break;
            }

            uint16_t cccd_handle = 0;
            int report_idx = -1;
            for (int i = 0; i < local_report_count; i++) {
                if (handle == local_handles[i]) {
                    cccd_handle = local_cccds[i];
                    report_idx = i;
                    break;
                }
            }
            if (handle == local_batt_char) cccd_handle = local_batt_cccd;
            if (cccd_handle > 0) {
                uint8_t cccd_val[2] = {0x01, 0x00};
                esp_ble_gattc_write_char_descr(
                    gattc_if, local_conn_id, cccd_handle,
                    2, cccd_val,
                    ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
            } else if (report_idx >= 0) {
                // Report has no CCCD — count as already enabled
                bool all_ready = false;
                int log_written = 0, log_count = 0;
                if (xSemaphoreTake(S().mux, 0)) {
                    S().report_cccds_written++;
                    log_written = S().report_cccds_written;
                    log_count = S().report_count;
                    all_ready = (log_written == log_count);
                    if (all_ready) {
                        S().state = BLE_CONNECTED;
                        S().cccd_ready = true;
                        S().connect_pending = false;
                        S().auto_reconnect = true;
                        if (S().auth_cmpl) { S().paired = true; S().reconnect_retries = 0; }
                    }
                    xSemaphoreGive(S().mux);
                }
                ESP_LOGD("ble_hid", "Report[%d] 无 CCCD, 计为已启用 (%d/%d)",
                         report_idx, log_written, log_count);
                if (all_ready) {
                    ESP_LOGI("ble_hid", "所有 Report 已就绪, BLE HID 连接完成");
                    _queue_event_from_task(HID_EVT_CONNECTED);
                }
            }
            break;
        }

        case ESP_GATTC_WRITE_DESCR_EVT:
            if (param->write.status == ESP_GATT_OK) {
                // Snapshot shared state under lock
                int local_report_count = 0;
                uint16_t local_cccds[BLE_HID_MAX_REPORTS] = {0};
                uint16_t local_batt_cccd = 0;
                if (xSemaphoreTake(S().mux, 0)) {
                    local_report_count = S().report_count;
                    for (int i = 0; i < local_report_count && i < BLE_HID_MAX_REPORTS; i++) {
                        local_cccds[i] = S().report_cccd_handles[i];
                    }
                    local_batt_cccd = S().batt_cccd_handle;
                    xSemaphoreGive(S().mux);
                }

                // Match handle using local data
                int report_idx = -1;
                for (int i = 0; i < local_report_count; i++) {
                    if (param->write.handle == local_cccds[i]) {
                        report_idx = i;
                        break;
                    }
                }

                if (report_idx >= 0) {
                    bool all_ready = false;
                    int log_written = 0, log_count = 0;
                    if (xSemaphoreTake(S().mux, 0)) {
                        S().report_cccds_written++;
                        log_written = S().report_cccds_written;
                        log_count = S().report_count;
                        all_ready = (log_written == log_count);
                        if (all_ready) {
                            S().state = BLE_CONNECTED;
                            S().cccd_ready = true;
                            S().connect_pending = false;
                            S().auto_reconnect = true;
                            if (S().auth_cmpl) { S().paired = true; S().reconnect_retries = 0; }
                        }
                        xSemaphoreGive(S().mux);
                    }
                    ESP_LOGD("ble_hid", "Report[%d] CCCD 已启用 (%d/%d)",
                             report_idx, log_written, log_count);
                    if (all_ready) {
                        ESP_LOGI("ble_hid", "所有 Report CCCD 已启用, BLE HID 就绪");
                        _queue_event_from_task(HID_EVT_CONNECTED);
                    }
                } else if (param->write.handle == local_batt_cccd && local_batt_cccd > 0) {
                    ESP_LOGD("ble_hid", "Battery CCCD 已启用");
                }
            } else {
                int local_report_count = 0;
                uint16_t local_cccds[BLE_HID_MAX_REPORTS] = {0};
                uint16_t local_batt_cccd = 0;
                uint16_t local_conn_id = 0;
                if (xSemaphoreTake(S().mux, 0)) {
                    local_report_count = S().report_count;
                    for (int i = 0; i < local_report_count && i < BLE_HID_MAX_REPORTS; i++) {
                        local_cccds[i] = S().report_cccd_handles[i];
                    }
                    local_batt_cccd = S().batt_cccd_handle;
                    local_conn_id = S().conn_id;
                    xSemaphoreGive(S().mux);
                }
                bool is_report_cccd = false;
                for (int i = 0; i < local_report_count; i++) {
                    if (param->write.handle == local_cccds[i]) {
                        is_report_cccd = true;
                        break;
                    }
                }
                ESP_LOGW("ble_hid", "%s CCCD 写入失败 handle=0x%04x status=%d",
                         is_report_cccd ? "HID" : (param->write.handle == local_batt_cccd ? "Battery" : "Unknown"),
                         param->write.handle, param->write.status);
                if (is_report_cccd && local_conn_id > 0) {
                    esp_ble_gattc_close(gattc_if, local_conn_id);
                }
            }
            break;

        case ESP_GATTC_READ_CHAR_EVT:
            if (param->read.status == ESP_GATT_OK && param->read.value_len > 0) {
                uint16_t local_batt_char;
                if (xSemaphoreTake(S().mux, 0)) {
                    local_batt_char = S().batt_char_handle;
                    xSemaphoreGive(S().mux);
                } else {
                    break;
                }
                if (param->read.handle == local_batt_char) {
                    int level = -1;
                    if (xSemaphoreTake(S().mux, 0)) {
                        S().battery_level = param->read.value[0];
                        level = S().battery_level;
                        xSemaphoreGive(S().mux);
                    }
                    ESP_LOGD("ble_hid", "电池电量: %d%%", level);
                }
            }
            break;

        case ESP_GATTC_NOTIFY_EVT: {
            uint16_t n_handle = param->notify.handle;
            uint16_t n_len = param->notify.value_len;
            const uint8_t* n_val = param->notify.value;
            char hex[64] = {0};
            int hex_pos = 0;
            uint16_t hex_len = n_len < 16 ? n_len : 16;
            for (uint16_t i = 0; i < hex_len && hex_pos < (int)sizeof(hex) - 4; i++) {
                hex_pos += snprintf(hex + hex_pos, sizeof(hex) - hex_pos, "%s%02X", i ? " " : "", n_val[i]);
            }

            // Snapshot batt_char_handle under lock
            uint16_t local_batt_char;
            if (xSemaphoreTake(S().mux, 0)) {
                local_batt_char = S().batt_char_handle;
                xSemaphoreGive(S().mux);
            } else {
                break;
            }

            // ── Battery level notification ──
            if (n_handle == local_batt_char && n_len > 0) {
                ESP_LOGI("ble_hid", "Battery level notify: %d%%", (int)n_val[0]);
                int level = -1;
                if (xSemaphoreTake(S().mux, 0)) {
                    S().battery_level = n_val[0];
                    level = S().battery_level;
                    xSemaphoreGive(S().mux);
                }
                ESP_LOGD("ble_hid", "电池电量通知: %d%%", level);
                break;
            }

            // ── Capture raw HID event for debug viewer ──
            NormalizedFingerprint fp = _normalize_report(n_handle, n_val, n_len);
            if (xSemaphoreTake(S().mux, 0)) {
                S().has_raw_event = false;
                if (fp.valid && fp.learnable) {
                    if (S().learning_capture_active && !S().has_raw_fingerprint) {
                        S().last_raw_fingerprint = fp;
                        S().has_raw_fingerprint = true;
                    } else if (!S().learning_capture_active) {
                        S().last_raw_fingerprint = fp;
                        S().has_raw_fingerprint = true;
                    }
                }
                if (n_len == 8 || n_len == 9) {
                    // Keyboard boot report, optionally prefixed by Report ID
                    int key_start = (n_len == 9) ? 3 : 2;
                    int key_end   = (n_len == 9) ? 9 : 8;
                    S().last_raw_page = 0x07;
                    S().last_raw_usage = 0;
                    for (int i = key_start; i < key_end; i++) {
                        if (n_val[i] != 0) { S().last_raw_usage = n_val[i]; break; }
                    }
                    S().last_raw_value = S().last_raw_usage ? 1 : 0;
                    S().has_raw_event = true;
                } else if (n_len == 2) {
                    // 2-byte consumer report
                    S().last_raw_page = 0x0C;
                    S().last_raw_usage = n_val[0] | (n_val[1] << 8);
                    S().last_raw_value = 1;
                    S().has_raw_event = true;
                } else if (n_len == 3) {
                    // 3-byte consumer report (with report ID)
                    S().last_raw_page = 0x0C;
                    S().last_raw_usage = n_val[1] | (n_val[2] << 8);
                    S().last_raw_value = 1;
                    S().has_raw_event = true;
                } else if (n_len == 4) {
                    // 4-byte consumer report (Report ID + usage + value)
                    S().last_raw_page = 0x0C;
                    S().last_raw_usage = n_val[1] | (n_val[2] << 8);
                    S().last_raw_value = n_val[3];
                    S().has_raw_event = true;
                } else {
                    // Unknown/vendor report: keep enough info for field diagnostics
                    S().last_raw_page = 0xFFFF;
                    S().last_raw_usage = n_len;
                    S().last_raw_value = n_len > 0 ? n_val[0] : 0;
                    S().has_raw_event = true;
                }
                xSemaphoreGive(S().mux);
            }

            // ── Parse HID report ──
            // learned-only mode: physical remote control actions must come
            // from the user-learned raw fingerprint table. Built-in HID
            // presets are intentionally bypassed to avoid key conflicts.
            uint32_t now = millis();
            HidEventType evt_type = HID_EVT_NONE;
            bool learned_match = false;
            if (xSemaphoreTake(S().mux, 0)) {
                evt_type = _match_learned_key(fp);
                learned_match = evt_type != HID_EVT_NONE;
                xSemaphoreGive(S().mux);
            }
            ESP_LOGI("ble_hid", "HID notify handle=0x%04x len=%u data=[%s]%s evt=%d",
                     n_handle, n_len, hex, n_len > 16 ? "..." : "", (int)evt_type);
            if (evt_type != HID_EVT_NONE) {
                bool should_queue = false;
                if (xSemaphoreTake(S().mux, 0)) {
                    if (now - S().last_key_ms >= 30) {
                        S().last_key_ms = now;
                        should_queue = true;
                    }
                    xSemaphoreGive(S().mux);
                }
                if (should_queue) {
                    _queue_event_from_task(evt_type);
                }
            }
            break;
        }

        case ESP_GATTC_CONNECT_EVT:
            // 使用 OPEN_EVT 跟踪连接建立
            break;

        case ESP_GATTC_DISCONNECT_EVT:
            _handle_disconnect_event(param->disconnect.reason, "disconnect");
            break;

        default:
            break;
    }
}

// ═══════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════

static bool setup(esphome::esp32_ble::ESP32BLE *ble = nullptr) {
    if (S().registered || S().setup_done) return true;

    // 创建互斥量 (必须在注册 BLE 回调之前，因为回调可能异步触发)
    S().mux = xSemaphoreCreateMutex();
    if (S().mux == nullptr) {
        ESP_LOGE("ble_hid", "互斥量创建失败");
        return false;
    }

    // Create FreeRTOS queue
    S().hid_queue = xQueueCreate(BLE_HID_QUEUE_DEPTH, sizeof(HidEvent));
    if (S().hid_queue == nullptr) {
        ESP_LOGE("ble_hid", "队列创建失败");
        vSemaphoreDelete(S().mux);
        S().mux = nullptr;
        return false;
    }

    S().ble_parent = ble;

    // Register callbacks
    esp_err_t ret = ESP_OK;
    if (ble != nullptr) {
        ble->add_gap_event_callback([](esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
            _ble_gap_cb(event, param);
        });
        ble->add_gap_scan_event_callback([](const esphome::esp32_ble::BLEScanResult& result) {
            _ble_gap_scan_cb(result);
        });
        ble->add_gattc_event_callback([](esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param) {
            _ble_gattc_cb(event, gattc_if, param);
        });
        ESP_LOGI("ble_hid", "BLE HID Host using ESPHome BLE dispatcher");
    } else {
    ret = esp_ble_gap_register_callback(_ble_gap_cb);
    if (ret != ESP_OK) {
        ESP_LOGE("ble_hid", "GAP callback 注册失败: %s", esp_err_to_name(ret));
        vQueueDelete(S().hid_queue); S().hid_queue = nullptr;
        vSemaphoreDelete(S().mux);   S().mux = nullptr;
        return false;
    }

    ret = esp_ble_gattc_register_callback(_ble_gattc_cb);
    if (ret != ESP_OK) {
        ESP_LOGE("ble_hid", "GATTC callback 注册失败: %s", esp_err_to_name(ret));
        vQueueDelete(S().hid_queue); S().hid_queue = nullptr;
        vSemaphoreDelete(S().mux);   S().mux = nullptr;
        return false;
    }

    // Register application (async — app ID assigned in ESP_GATTC_REG_EVT)
    }

    if (ble == nullptr || ble->is_active()) {
        _request_gattc_app_register();
    }

    /*
    ret = esp_ble_gattc_app_register(1);  // app_id 1 (server uses default 0)
    if (ret != ESP_OK) {
        ESP_LOGE("ble_hid", "GATTC app register 失败: %s", esp_err_to_name(ret));
        vQueueDelete(S().hid_queue); S().hid_queue = nullptr;
        vSemaphoreDelete(S().mux);   S().mux = nullptr;
        return false;
    }
    */

    // Set security parameters (Just Works pairing)
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
    S().setup_done = true;

    // 异步等待注册 — 不阻塞主循环，在 poll() 中检查超时
    S().setup_done = true;
    S().setup_start_ms = millis();

    ESP_LOGI("ble_hid", "BLE HID Host 初始化已启动 (异步等待 GATT 注册...)");
    return true;
}

static void start_scan() {
    if (!S().registered) {
        ESP_LOGW("ble_hid", "未注册，无法扫描");
        return;
    }

    bool was_active = false;
    bool was_pending = false;
    if (xSemaphoreTake(S().mux, 0)) {
        was_active = S().scan_active;
        was_pending = S().scan_start_pending;
        if (was_active || was_pending) {
            S().scan_active = false;
            S().scan_start_pending = false;
            S().state = BLE_IDLE;
        }
        xSemaphoreGive(S().mux);
    } else {
        return;
    }
    if (was_active) esp_ble_gap_stop_scanning();
    if (was_active || was_pending) return;

    // Set scan params: active, reduced duty cycle (~25% for lower power)
    if (xSemaphoreTake(S().mux, 0)) {
        S().state = BLE_SCANNING;
        S().scan_active = false;
        S().scan_start_pending = true;
        S().discovered_count = 0;
        xSemaphoreGive(S().mux);
    } else {
        return;
    }

    esp_ble_scan_params_t scan_params = {};
    scan_params.scan_type          = BLE_SCAN_TYPE_ACTIVE;
    scan_params.own_addr_type      = BLE_ADDR_TYPE_PUBLIC;
    scan_params.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    scan_params.scan_interval      = 0x00C0;
    scan_params.scan_window        = 0x0030;
    esp_err_t ret = esp_ble_gap_set_scan_params(&scan_params);
    if (ret != ESP_OK) {
        ESP_LOGE("ble_hid", "设置扫描参数失败: %s", esp_err_to_name(ret));
        if (xSemaphoreTake(S().mux, 0)) {
            S().scan_start_pending = false;
            S().scan_active = false;
            if (S().state == BLE_SCANNING) S().state = BLE_IDLE;
            xSemaphoreGive(S().mux);
        }
        return;
    }

    ESP_LOGI("ble_hid", "扫描参数已提交，等待控制器开始扫描...");
}

static void stop_scan() {
    if (xSemaphoreTake(S().mux, 0)) {
        bool was_active = S().scan_active;
        bool was_pending = S().scan_start_pending;
        if (was_active || was_pending) {
            S().scan_active = false;
            S().scan_start_pending = false;
            S().state = BLE_IDLE;
        }
        xSemaphoreGive(S().mux);
        if (was_active) {
            esp_ble_gap_stop_scanning();
        }
    }
}

static void connect(int index) {
    if (!(S().registered && S().gattc_if > 0)) return;

    bool valid = false;
    bool was_active = false;
    bool busy = false;
    if (xSemaphoreTake(S().mux, 0)) {
        busy = (S().state == BLE_CONNECTING || S().state == BLE_CONNECTED || S().connect_pending);
        if (!busy && index >= 0 && index < S().discovered_count) {
            memcpy(S().pending_bda, S().discovered[index].bda, 6);
            S().pending_addr_type = S().discovered[index].addr_type;
            strncpy(S().pending_name, S().discovered[index].name, BLE_HID_NAME_LEN - 1);
            S().pending_name[BLE_HID_NAME_LEN - 1] = '\0';
            S().connect_pending = true;
            S().auto_reconnect = false;
            S().reconnect_pending = false;
            S().reconnect_retries = 0;
            was_active = S().scan_active;
            if (S().scan_active || S().scan_start_pending) {
                S().scan_active = false;
                S().scan_start_pending = false;
                if (S().state == BLE_SCANNING) S().state = BLE_IDLE;
            }
            valid = true;
        }
        xSemaphoreGive(S().mux);
    } else {
        return;
    }

    if (busy) {
        ESP_LOGW("ble_hid", "connect request ignored while busy");
        return;
    }
    if (!valid) return;

    if (was_active) {
        esp_ble_gap_stop_scanning();
    } else {
        _open_pending_connection();
    }
    return;
}

static void connect_by_bda(const uint8_t* bda) {
    if (!(S().registered && S().gattc_if > 0 && bda != nullptr)) return;

    bool valid = false;
    bool was_active = false;
    bool busy = false;
    if (xSemaphoreTake(S().mux, 0)) {
        busy = (S().state == BLE_CONNECTING || S().state == BLE_CONNECTED || S().connect_pending);
        if (!busy && memcmp(bda, ZERO_BDA, 6) != 0) {
            memcpy(S().pending_bda, bda, 6);
            S().pending_addr_type = S().peer_addr_type;
            snprintf(S().pending_name, BLE_HID_NAME_LEN,
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
            S().connect_pending = true;
            was_active = S().scan_active;
            if (S().scan_active || S().scan_start_pending) {
                S().scan_active = false;
                S().scan_start_pending = false;
                if (S().state == BLE_SCANNING) S().state = BLE_IDLE;
            }
            valid = true;
        }
        xSemaphoreGive(S().mux);
    } else {
        return;
    }

    if (busy || !valid) return;

    if (was_active) {
        esp_ble_gap_stop_scanning();
    } else {
        _open_pending_connection();
    }
    return;
}

static void disconnect() {
    if (xSemaphoreTake(S().mux, 0)) {
        S().auto_reconnect = false;
        S().reconnect_pending = false;
        S().reconnect_retries = 0;
        S().connect_pending = false;
        if (S().state == BLE_CONNECTED && S().gattc_if > 0) {
            uint16_t conn = S().conn_id;
            esp_gatt_if_t gif = S().gattc_if;
            xSemaphoreGive(S().mux);
            esp_ble_gattc_close(gif, conn);
        } else {
            S().paired = false;
            S().state = BLE_IDLE;
            xSemaphoreGive(S().mux);
        }
    }
}

static void reconnect_enable() {
    if (xSemaphoreTake(S().mux, 0)) {
        S().auto_reconnect = true;
        S().reconnect_retries = 0;
        S().connect_pending = false;
        S().reconnect_pending = true;
        S().reconnect_next_ms = millis() + 500;  // 500ms 后 poll() 触发首次连接
        xSemaphoreGive(S().mux);
    }
}

// ═══════════════════════════════════════════════════
// Status queries (called from main loop)
// ═══════════════════════════════════════════════════

static void poll() {
    if (S().mux == nullptr) return;

    // 异步注册超时检测
if (!S().registered && S().setup_done) {
        if (!S().gattc_register_requested &&
            (S().ble_parent == nullptr || S().ble_parent->is_active())) {
            _request_gattc_app_register();
        }
        if (!S().registration_warned && millis() - S().setup_start_ms > 2000) {
            ESP_LOGE("ble_hid", "GATT Client 注册超时 (2s)");
            S().registration_warned = true;
        }
    }

    // Handle reconnect timer (snapshot shared state under lock)
    bool should_reconnect = false;
    uint8_t saved_bda[6];
    if (xSemaphoreTake(S().mux, 0)) {
        if (S().reconnect_pending && S().auto_reconnect && S().state == BLE_IDLE) {
            uint32_t now = millis();
            if (now >= S().reconnect_next_ms) {
                S().reconnect_pending = false;
                memcpy(saved_bda, S().peer_bda, 6);
                should_reconnect = memcmp(saved_bda, ZERO_BDA, 6) != 0;
            }
        }
        xSemaphoreGive(S().mux);
    }
    if (should_reconnect) {
        connect_by_bda(saved_bda);
    }
}

static bool has_event() {
    if (S().hid_queue == nullptr) return false;
    UBaseType_t waiting = uxQueueMessagesWaiting(S().hid_queue);
    return waiting > 0;
}

static HidEvent pop_event() {
    HidEvent evt = {HID_EVT_NONE, 0};
    if (S().hid_queue == nullptr) return evt;
    xQueueReceive(S().hid_queue, &evt, 0);
    return evt;
}

static BLEState get_state() {
    if (S().mux == nullptr) return BLE_IDLE;
    BLEState s = BLE_IDLE;
    if (xSemaphoreTake(S().mux, 0)) {
        s = S().state;
        xSemaphoreGive(S().mux);
    }
    return s;
}

static bool is_connected() { return get_state() == BLE_CONNECTED; }

static bool is_scanning() { return get_state() == BLE_SCANNING; }

// ── 便捷访问器 (供 ESPHome YAML lambda 使用) ──
static int get_discovered_device_count() {
    if (S().mux == nullptr) return 0;
    int c = 0;
    if (xSemaphoreTake(S().mux, 0)) {
        c = S().discovered_count;
        xSemaphoreGive(S().mux);
    }
    return c;
}

// Thread-safe: copy-out under lock (never returns dangling pointer)
static bool get_discovered_device(int i, DeviceInfo& out) {
    if (S().mux == nullptr) return false;
    bool ok = false;
    if (xSemaphoreTake(S().mux, 0)) {
        if (i >= 0 && i < S().discovered_count) {
            out = S().discovered[i];
            ok = true;
        }
        xSemaphoreGive(S().mux);
    }
    return ok;
}

static std::string get_peer_bda_string() {
    if (S().mux == nullptr) return "";
    if (xSemaphoreTake(S().mux, 0)) {
        bool ok = (S().paired || S().state == BLE_CONNECTED) &&
                  memcmp(S().peer_bda, ZERO_BDA, 6) != 0;
        if (!ok) { xSemaphoreGive(S().mux); return ""; }
        char buf[24];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X|%u",
                 S().peer_bda[0], S().peer_bda[1], S().peer_bda[2],
                 S().peer_bda[3], S().peer_bda[4], S().peer_bda[5],
                 (unsigned)S().peer_addr_type);
        xSemaphoreGive(S().mux);
        return std::string(buf);
    }
    return "";
}

static bool set_peer_bda_from_string(const char* str);

static bool set_peer_bda_from_string(const std::string& str) {
    return set_peer_bda_from_string(str.c_str());
}

static std::string get_connected_name() {
    if (S().mux == nullptr) return "";
    if (xSemaphoreTake(S().mux, 0)) {
        bool connected = (S().state == BLE_CONNECTED);
        std::string name;
        if (connected && S().connected_name[0] != '\0')
            name = std::string(S().connected_name);
        xSemaphoreGive(S().mux);
        return name;
    }
    return "";
}

// ── 电池电量 ──
static int get_battery_level() {
    if (S().mux == nullptr) return -1;
    int level = -1;
    if (xSemaphoreTake(S().mux, 0)) {
        level = S().battery_level;
        xSemaphoreGive(S().mux);
    }
    return level;
}

static std::string get_battery_level_string() {
    int level = get_battery_level();
    if (level < 0) return "未知";
    if (level > 100) return "未知";
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", level);
    return std::string(buf);
}

// ── 原始 HID 事件 (调试用) ──
static std::string get_last_raw_event_string() {
    if (S().mux == nullptr) return "无";
    if (xSemaphoreTake(S().mux, 0)) {
        if (!S().has_raw_event) { xSemaphoreGive(S().mux); return "无"; }
        char buf[48];
        snprintf(buf, sizeof(buf), "Page=0x%04X Usage=0x%04X Val=%u",
                 S().last_raw_page, S().last_raw_usage, S().last_raw_value);
        xSemaphoreGive(S().mux);
        return std::string(buf);
    }
    return "无";
}

static bool has_raw_event() {
    if (S().mux == nullptr) return false;
    bool v = false;
    if (xSemaphoreTake(S().mux, 0)) {
        v = S().has_raw_event;
        xSemaphoreGive(S().mux);
    }
    return v;
}

static void clear_raw_event() {
    if (S().mux == nullptr) return;
    if (xSemaphoreTake(S().mux, 0)) {
        S().has_raw_event = false;
        S().has_raw_fingerprint = false;
        S().last_raw_fingerprint = NormalizedFingerprint{};
        S().learning_capture_active = false;
        xSemaphoreGive(S().mux);
    }
}

static void begin_learning_capture() {
    if (S().mux == nullptr) return;
    if (xSemaphoreTake(S().mux, 0)) {
        S().has_raw_event = false;
        S().has_raw_fingerprint = false;
        S().last_raw_fingerprint = NormalizedFingerprint{};
        S().learning_capture_active = true;
        S().trend.tracking = false;
        xSemaphoreGive(S().mux);
    }
}

static bool get_last_normalized_fingerprint(uint16_t* handle, uint16_t* len,
                                            uint8_t* kind, uint8_t* value,
                                            uint16_t* aux, uint32_t* hash) {
    if (S().mux == nullptr || handle == nullptr || len == nullptr ||
        kind == nullptr || value == nullptr || aux == nullptr || hash == nullptr) return false;
    bool ok = false;
    if (xSemaphoreTake(S().mux, 0)) {
        ok = S().has_raw_fingerprint && S().last_raw_fingerprint.valid && S().last_raw_fingerprint.learnable;
        if (ok) {
            *handle = S().last_raw_fingerprint.handle;
            *len = S().last_raw_fingerprint.len;
            *kind = S().last_raw_fingerprint.kind;
            *value = S().last_raw_fingerprint.value;
            *aux = S().last_raw_fingerprint.aux;
            *hash = S().last_raw_fingerprint.hash;
            S().has_raw_fingerprint = false;
            S().learning_capture_active = false;
        }
        xSemaphoreGive(S().mux);
    }
    return ok;
}

static void set_learned_key(HidEventType action, uint16_t handle, uint16_t len,
                            uint8_t kind, uint8_t value, uint16_t aux, uint32_t hash) {
    if (S().mux == nullptr) return;
    int idx = (int)action;
    if (idx < (int)HID_EVT_VOLUME_UP || idx > (int)HID_EVT_CYCLE_INPUT) return;
    if (xSemaphoreTake(S().mux, 0)) {
        LearnedKey& key = S().learned[(int)action];
        key.enabled = (handle != 0 && len != 0 && kind != NK_NONE && value != NV_NONE);
        key.handle = handle;
        key.len = len;
        key.hash = hash;
        key.kind = kind;
        key.value = value;
        key.aux = aux;
        key.action = action;
        key.last_emit_ms = 0;
        xSemaphoreGive(S().mux);
    }
}

static void clear_learned_key(HidEventType action) {
    if (S().mux == nullptr) return;
    int idx = (int)action;
    if (idx < (int)HID_EVT_VOLUME_UP || idx > (int)HID_EVT_CYCLE_INPUT) return;
    if (xSemaphoreTake(S().mux, 0)) {
        S().learned[idx] = LearnedKey{};
        xSemaphoreGive(S().mux);
    }
}

static void clear_all_learned_keys() {
    if (S().mux == nullptr) return;
    if (xSemaphoreTake(S().mux, 0)) {
        for (int i = (int)HID_EVT_VOLUME_UP; i <= (int)HID_EVT_CYCLE_INPUT; i++) {
            S().learned[i] = LearnedKey{};
        }
        xSemaphoreGive(S().mux);
    }
}

static uint16_t get_raw_event_page() {
    if (S().mux == nullptr) return 0;
    uint16_t v = 0;
    if (xSemaphoreTake(S().mux, 0)) {
        v = S().last_raw_page;
        xSemaphoreGive(S().mux);
    }
    return v;
}

static uint16_t get_raw_event_usage() {
    if (S().mux == nullptr) return 0;
    uint16_t v = 0;
    if (xSemaphoreTake(S().mux, 0)) {
        v = S().last_raw_usage;
        xSemaphoreGive(S().mux);
    }
    return v;
}

// ═══════════════════════════════════════════════════
// BDA 持久化 (hex string ↔ uint8[6])
// ═══════════════════════════════════════════════════

static bool get_peer_bda_string(char* out, size_t out_len) {
    if (S().mux == nullptr) return false;
    if (xSemaphoreTake(S().mux, 0)) {
        bool ok = (S().paired || S().state == BLE_CONNECTED) &&
                  memcmp(S().peer_bda, ZERO_BDA, 6) != 0;
        if (!ok) { xSemaphoreGive(S().mux); return false; }
        snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X|%u",
                 S().peer_bda[0], S().peer_bda[1], S().peer_bda[2],
                 S().peer_bda[3], S().peer_bda[4], S().peer_bda[5],
                 (unsigned)S().peer_addr_type);
        xSemaphoreGive(S().mux);
        return true;
    }
    return false;
}

static bool set_peer_bda_from_string(const char* str) {
    if (str == nullptr || strlen(str) < 17) return false;
    unsigned int b[6], t = 0;
    int parsed = sscanf(str, "%02X:%02X:%02X:%02X:%02X:%02X|%u",
                        &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &t);
    if (parsed < 6) return false;
    if (S().mux == nullptr) return false;
    bool ok = false;
    if (xSemaphoreTake(S().mux, 0)) {
        for (int i = 0; i < 6; i++) S().peer_bda[i] = (uint8_t)b[i];
        if (parsed == 7) S().peer_addr_type = (esp_ble_addr_type_t)t;
        S().paired = true;
        S().auto_reconnect = true;
        S().reconnect_retries = 0;
        ok = true;
        xSemaphoreGive(S().mux);
    }
    return ok;
}

static bool has_paired_device() {
    if (S().mux == nullptr) return false;
    bool v = false;
    if (xSemaphoreTake(S().mux, 0)) {
        v = S().paired && memcmp(S().peer_bda, ZERO_BDA, 6) != 0;
        xSemaphoreGive(S().mux);
    }
    return v;
}

static void clear_paired() {
    if (S().mux == nullptr) return;
    if (xSemaphoreTake(S().mux, 0)) {
        memset(S().peer_bda, 0, 6);
        S().peer_addr_type = BLE_ADDR_TYPE_PUBLIC;
        S().paired = false;
        S().cccd_ready = false;
        S().auth_cmpl = false;
        S().auto_reconnect = false;
        S().connected_name[0] = '\0';
        xSemaphoreGive(S().mux);
    }
}

}  // namespace ble_hid_host
