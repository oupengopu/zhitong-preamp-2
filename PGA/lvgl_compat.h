// LVGL 兼容头文件 — 补齐 lambda 中缺失的 LVGL C API 声明
// ESPHome 2026 的 LVGL managed component 使用 opaque lv_obj_t (PIMPL 模式)，
// struct _lv_obj_t 仅在 .c 文件中定义，用户 lambda 不可见。
//
// 核心原则：
//   ESPHome 生成的非复合 widget (bar/label/led/obj) 是 lv_obj_t* 全局变量，
//   因此 id(widget) 直接返回 lv_obj_t*，不需要 ->obj_ 间接访问。
//   移除所有 ->obj_ 后，函数调用仅需函数声明 + 指针前向声明即可编译。
//
// 本头文件提供：
//   1. #include <lvgl.h> 获得类型前向声明 (lv_obj_t 等)
//   2. 显式包含核心头文件 (部分 managed_components 会裁剪 widget 头)
//   3. extern "C" 声明所有用到的 LVGL C API 函数
//      (managed_components 未包含单独的 widget 头文件，但函数已在库中链接)
#pragma once

#define LV_CONF_SKIP 1
#include <lvgl.h>

// ── 显式包含核心头文件以提供 lv_obj_t 完整定义 ──
#include <src/core/lv_obj.h>
#include <src/core/lv_obj_pos.h>
#include <src/core/lv_obj_style.h>
#include <src/core/lv_obj_tree.h>
#include <src/core/lv_obj_scroll.h>

// ── 手动声明所需的 LVGL C API ──
// managed_components 中无单独的 widget 头文件 (src/widgets/ 不存在),
// 但 lv_label_set_text / lv_bar_set_value 等函数已在 LVGL 库中链接,
// 只需提供符合 LVGL ABI 的 C 链接声明即可。
extern "C" {

// src/widgets/lv_label.h
void lv_label_set_text(lv_obj_t * obj, const char * text);

// src/widgets/lv_bar.h
int32_t lv_bar_get_value(const lv_obj_t * obj);
void lv_bar_set_value(lv_obj_t * obj, int32_t value, lv_anim_enable_t anim);

// src/widgets/lv_led.h
void lv_led_set_brightness(lv_obj_t * led, uint8_t bright);
void lv_led_set_color(lv_obj_t * led, lv_color_t color);

// src/core/lv_obj_pos.h
void lv_obj_set_x(lv_obj_t * obj, int32_t x);
void lv_obj_set_y(lv_obj_t * obj, int32_t y);
void lv_obj_set_width(lv_obj_t * obj, int32_t w);
void lv_obj_set_height(lv_obj_t * obj, int32_t h);

// src/core/lv_obj_style.h
void lv_obj_set_style_text_color(lv_obj_t * obj, lv_color_t color, uint32_t sel);
void lv_obj_set_style_bg_color(lv_obj_t * obj, lv_color_t color, uint32_t sel);
void lv_obj_set_style_bg_opa(lv_obj_t * obj, lv_opa_t opa, uint32_t sel);
void lv_obj_set_style_border_width(lv_obj_t * obj, int32_t width, uint32_t sel);
void lv_obj_set_style_border_color(lv_obj_t * obj, lv_color_t color, uint32_t sel);
void lv_obj_set_style_border_opa(lv_obj_t * obj, lv_opa_t opa, uint32_t sel);
void lv_obj_set_style_border_side(lv_obj_t * obj, lv_border_side_t side, uint32_t sel);
void lv_obj_set_style_radius(lv_obj_t * obj, int32_t value, uint32_t sel);
void lv_obj_set_style_pad_all(lv_obj_t * obj, int32_t value, uint32_t sel);
void lv_obj_set_style_pad_row(lv_obj_t * obj, int32_t value, uint32_t sel);
void lv_obj_set_style_pad_column(lv_obj_t * obj, int32_t value, uint32_t sel);
void lv_obj_set_style_opa(lv_obj_t * obj, lv_opa_t opa, uint32_t sel);
lv_result_t lv_obj_invalidate(const lv_obj_t * obj);

// src/core/lv_obj_scroll.h
void lv_obj_scroll_to_view(lv_obj_t * obj, lv_anim_enable_t anim_en);

// src/core/lv_scr.h
lv_obj_t * lv_scr_act(void);
void lv_scr_load(lv_obj_t * scr);
void lv_scr_load_anim(lv_obj_t * scr, lv_scr_load_anim_t anim_type, uint32_t time, uint32_t delay, bool auto_del);
void lv_refr_now(lv_display_t * disp);

}

// ── 主题色辅助函数 (消除 YAML 中 7 处重复 switch-case) ──
inline lv_color_t get_theme_accent(int theme) {
  lv_color_t colors[] = {
    lv_color_hex(0x10B981), lv_color_hex(0xEF4444), lv_color_hex(0x7DD3FC),
    lv_color_hex(0x8B5CF6), lv_color_hex(0x0071E3), lv_color_hex(0xCD9B4A),
    lv_color_hex(0x06B6D4), lv_color_hex(0xF97316)
  };
  return colors[theme >= 0 && theme < 8 ? theme : 0];
}

// 主题色名称数组 (font_cn 已包含这些字符)
inline const char* get_theme_name(int theme) {
  static const char* names[] = {"翠绿", "赤红", "冰蓝", "紫色", "麦景图蓝", "金嗓子金", "柏林青", "南瓜橙"};
  return names[theme >= 0 && theme < 8 ? theme : 0];
}

// 二级页面统一样式：设置 / BLE 遥控 / 按键映射 / 诊断页。
// 目标是跟 preview/index.html 的 Secondary pages v2 保持同一套暗色仪表菜单风格。
namespace secondary_ui {

constexpr int ROW_W = 414;
constexpr int ROW_H = 40;
constexpr int TITLE_H = 42;
constexpr int DEBUG_H = 34;

inline void page(lv_obj_t* obj) {
  if (!obj) return;
  lv_obj_set_style_bg_color(obj, lv_color_hex(0x050A12), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(obj, 7, LV_PART_MAIN);
  lv_obj_set_style_pad_row(obj, 5, LV_PART_MAIN);
  lv_obj_set_style_pad_column(obj, 0, LV_PART_MAIN);
}

inline void row(lv_obj_t* obj, lv_color_t accent, bool focused, bool dim = false, bool active = false, int height = ROW_H) {
  if (!obj) return;
  lv_obj_set_width(obj, ROW_W);
  lv_obj_set_height(obj, height);
  lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(obj, 5, LV_PART_MAIN);
  lv_obj_set_style_bg_color(obj, active ? lv_color_hex(0x142033) :
                                focused ? lv_color_hex(0x101827) :
                                dim ? lv_color_hex(0x060C16) : lv_color_hex(0x08121E), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, focused || active ? LV_OPA_60 : (dim ? LV_OPA_40 : LV_OPA_50), LV_PART_MAIN);
  lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_FULL, LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(obj, focused || active ? accent : lv_color_hex(0x263244), LV_PART_MAIN);
  lv_obj_set_style_border_opa(obj, focused || active ? LV_OPA_50 : LV_OPA_20, LV_PART_MAIN);
}

inline void title(lv_obj_t* obj, lv_color_t accent) {
  row(obj, accent, false, false, false, TITLE_H);
  lv_obj_set_style_bg_color(obj, lv_color_hex(0x08121E), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_80, LV_PART_MAIN);
  lv_obj_set_style_border_color(obj, lv_color_hex(0x263244), LV_PART_MAIN);
  lv_obj_set_style_border_opa(obj, LV_OPA_30, LV_PART_MAIN);
}

}  // namespace secondary_ui
