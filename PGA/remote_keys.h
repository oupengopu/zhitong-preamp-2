// remote_keys.h — BLE 遥控器按键功能映射配置
// 允许用户将物理遥控器上的每个按键映射到任意功能
// 映射表存储在 NVS 全局变量中，默认恒等映射
#pragma once
#include <string>

namespace remote_keys {

// 动作 ID (与 ble_hid_host::HidEventType 对齐)
// 0=禁用, 1-9=实体遥控核心键。扩展划动/拍照类动作已移除，避免学习页混乱。
constexpr int ACTION_NONE       = 0;
constexpr int ACTION_VOL_UP     = 1;
constexpr int ACTION_VOL_DOWN   = 2;
constexpr int ACTION_MUTE       = 3;
constexpr int ACTION_PLAY       = 4;
constexpr int ACTION_PAUSE      = 5;
constexpr int ACTION_NEXT       = 6;
constexpr int ACTION_PREV       = 7;
constexpr int ACTION_POWER      = 8;
constexpr int ACTION_CYCLE_INPUT = 9;

constexpr int ACTION_COUNT = 10;  // 0=禁用 + 1-9=有效动作

/// 获取动作名称 (用于 UI 显示)
inline std::string get_action_name(int action) {
    switch (action) {
        case 0:  return "禁用";
        case 1:  return "音量+";
        case 2:  return "音量-";
        case 3:  return "静音";
        case 4:  return "播放";
        case 5:  return "暂停";
        case 6:  return "下一曲";
        case 7:  return "上一曲";
        case 8:  return "电源";
        case 9:  return "切换输入";
        default: return "未知";
    }
}

}  // namespace remote_keys
