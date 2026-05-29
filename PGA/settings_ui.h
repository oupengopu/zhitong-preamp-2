// settings_ui.h — 设置页 UI 控件数组集中管理
// 将分散在三个 lambda 中的 rows[] / bars[] 定义抽取为全局对象，
// 避免新增设置项时需要在多处同步修改 const int N = 11; 及数组内容。
//
// 用法：
//   在首次需要访问设置页控件的 lambda 中调用一次 init()（幂等），
//   之后通过 rows() / bars() / bar_base_heights 访问。
#pragma once
#include <lvgl.h>

namespace settings_ui {

constexpr int N = 11;

constexpr int bar_base_heights[N] = {
  16,  // 0  最大音量 (特殊，比其它可调项高)
  12,  // 1  开机上限
  12,  // 2  声道平衡
  0,   // 3  输入选择 (无 bar)
  0,   // 4  输入自动切换 (无 bar)
  12,  // 5  屏幕亮度
  12,  // 6  息屏超时
  0,   // 7  主题色彩 (无 bar)
  0,   // 8  蓝牙遥控 (导航项, 无 bar)
  0,   // 9  固件版本 (只读, 无 bar)
  0    // 10 IP地址 (只读, 无 bar)
};

// 返回 rows / bars 指针数组（Meyers Singleton，线程安全）
inline lv_obj_t** rows() {
  static lv_obj_t* r[N] = {nullptr};
  return r;
}

inline lv_obj_t** bars() {
  static lv_obj_t* b[N] = {nullptr};
  return b;
}

// 一次性初始化所有控件指针。幂等——第二次调用直接返回。
inline void init(
  lv_obj_t* r0, lv_obj_t* r1, lv_obj_t* r2, lv_obj_t* r3,
  lv_obj_t* r4, lv_obj_t* r5, lv_obj_t* r6, lv_obj_t* r7,
  lv_obj_t* r8, lv_obj_t* r9, lv_obj_t* r10,
  lv_obj_t* b0, lv_obj_t* b1, lv_obj_t* b2,
  lv_obj_t* b5, lv_obj_t* b6) {
  static bool done = false;
  if (done) return;
  lv_obj_t** r = rows();
  lv_obj_t** b = bars();
  r[0] = r0; r[1] = r1; r[2] = r2; r[3] = r3;
  r[4] = r4; r[5] = r5; r[6] = r6; r[7] = r7;
  r[8] = r8; r[9] = r9; r[10] = r10;
  b[0] = b0; b[1] = b1; b[2] = b2;
  b[3] = nullptr; b[4] = nullptr;
  b[5] = b5; b[6] = b6;
  b[7] = nullptr; b[8] = nullptr; b[9] = nullptr;
  b[10] = nullptr;
  done = true;
}

} // namespace settings_ui
