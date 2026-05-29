// remote_keys.h — BLE 遥控器按键功能映射配置
// 允许用户将物理遥控器上的每个按键映射到任意功能
// 映射表存储在 NVS 全局变量中，默认恒等映射
#pragma once
#include <cstring>

namespace remote_keys {

// 动作 ID (与 ble_hid_host::HidEventType 对齐)
// 0=禁用, 1-8=标准媒体键, 9-15=扩展键(方向/拍照/确定)
constexpr int ACTION_NONE       = 0;
constexpr int ACTION_VOL_UP     = 1;
constexpr int ACTION_VOL_DOWN   = 2;
constexpr int ACTION_MUTE       = 3;
constexpr int ACTION_PLAY_PAUSE = 4;
constexpr int ACTION_NEXT       = 5;
constexpr int ACTION_PREV       = 6;
constexpr int ACTION_POWER      = 7;
constexpr int ACTION_CYCLE_INPUT = 8;
constexpr int ACTION_SWIPE_UP   = 9;
constexpr int ACTION_SWIPE_DOWN = 10;
constexpr int ACTION_SWIPE_LEFT = 11;
constexpr int ACTION_SWIPE_RIGHT = 12;
constexpr int ACTION_CAMERA     = 13;
constexpr int ACTION_CAMERA_SWITCH = 14;
constexpr int ACTION_OK         = 15;

constexpr int ACTION_COUNT = 15;

/// 获取动作名称 (用于 UI 显示)
inline const char* get_action_name(int action) {
    switch (action) {
        case 0:  return "禁用";
        case 1:  return "音量+";
        case 2:  return "音量-";
        case 3:  return "静音";
        case 4:  return "播放/暂停";
        case 5:  return "下一曲";
        case 6:  return "上一曲";
        case 7:  return "电源";
        case 8:  return "切换输入";
        case 9:  return "上划";
        case 10: return "下划";
        case 11: return "左划";
        case 12: return "右划";
        case 13: return "拍照";
        case 14: return "切换镜头";
        case 15: return "确定";
        default: return "未知";
    }
}

}  // namespace remote_keys
