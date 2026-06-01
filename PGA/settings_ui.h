// settings_ui.h - central registry for settings page LVGL rows/bars.
#pragma once
#include <lvgl.h>

namespace settings_ui {

constexpr int N = 15;

constexpr int bar_base_heights[N] = {
  16,  // 0 max volume
  12,  // 1 power-on limit
  12,  // 2 balance
  0,   // 3 input select
  0,   // 4 auto input
  12,  // 5 brightness
  12,  // 6 display timeout
  0,   // 7 theme
  0,   // 8 BLE remote
  0,   // 9 remote key map
  0,   // 10 output mode
  0,   // 11 spectrum auto jump
  0,   // 12 spectrum style
  0,   // 13 firmware version
  0    // 14 IP address
};

inline lv_obj_t** rows() {
  static lv_obj_t* r[N] = {nullptr};
  return r;
}

inline lv_obj_t** bars() {
  static lv_obj_t* b[N] = {nullptr};
  return b;
}

inline void init(
  lv_obj_t* r0, lv_obj_t* r1, lv_obj_t* r2, lv_obj_t* r3,
  lv_obj_t* r4, lv_obj_t* r5, lv_obj_t* r6, lv_obj_t* r7,
  lv_obj_t* r8, lv_obj_t* r9, lv_obj_t* r10, lv_obj_t* r11,
  lv_obj_t* r12, lv_obj_t* r13, lv_obj_t* r14,
  lv_obj_t* b0, lv_obj_t* b1, lv_obj_t* b2,
  lv_obj_t* b5, lv_obj_t* b6) {
  lv_obj_t** r = rows();
  lv_obj_t** b = bars();
  r[0] = r0; r[1] = r1; r[2] = r2; r[3] = r3;
  r[4] = r4; r[5] = r5; r[6] = r6; r[7] = r7;
  r[8] = r8; r[9] = r9; r[10] = r10; r[11] = r11;
  r[12] = r12; r[13] = r13; r[14] = r14;

  b[0] = b0; b[1] = b1; b[2] = b2;
  b[3] = nullptr; b[4] = nullptr;
  b[5] = b5; b[6] = b6;
  b[7] = nullptr; b[8] = nullptr; b[9] = nullptr;
  b[10] = nullptr; b[11] = nullptr; b[12] = nullptr;
  b[13] = nullptr; b[14] = nullptr;
}

}  // namespace settings_ui
