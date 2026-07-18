# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

---

**34. Montserrat 字体切为本地文件**

编译时 Google Fonts 在线下载 Montserrat 字体超时，导致编译失败。

**修复**: 将 8 个 Montserrat 字型（8/12/16/21/24/27/30/36）从 `type: web` + `url:` 改为 `file: "fonts/Montserrat-Regular.ttf"`。

改动文件: `智能前级蓝牙2.0.yaml` 中 8 处 font 定义。


**35. ESPHome 2026.7.0 兼容: platformio_options → build_flags**

platformio_options.build_flags 在 ESPHome 2026.12.0 会移除支持。
将 build_flags 从 platformio_options 下移到 esphome: 直接子级。

改动文件: `智能前级蓝牙2.0.yaml` (esphome: 段)

**36. NV3007 黑屏修复: 显式 spi_mode: 0**

ESPHome 2026.6.4+ 在无 CS 引脚时默认 SPI mode 改为 MODE3,
NV3007 需要 MODE0, 导致初始化失败 → 有背光无显示。

修复: mipi_spi display 配置加 spi_mode: 0。

改动文件: `智能前级蓝牙2.0.yaml` (display: 段)

**37. 防爆音路径必须先软降再硬切**

开启静音、进入待机、变压器/直通输出模式切换都不能直接操作硬件静音或继电器。
必须先让 `send_volume_to_pga` 把 PGA2311 渐变到 -96dB, 等 `current_db <= -95.5f`
后, 再执行 `pga2311::set_volume(0,0)`、`mute_switch` 或 `relay_out`。

已修复入口:
- `soft_mute_switch.turn_on_action`: 先软降到 0, 再打开硬件静音
- `enter_standby`: `power_transitioning` 期间允许音量渐变, 软降完成后再关屏/待机
- `switch_output_mode`: 输出模式切换使用软降 -> 硬静音 -> 切 GPA4 -> 恢复音量
- HA `set_mute` 服务必须调用 `soft_mute_switch`, 不要直接切 `mute_switch`

改动文件: `智能前级蓝牙2.0.yaml` (switch/script/api 段)


## 强制性规则

**每次修改代码后，必须同步更新以下项目文档：**
  1. `CHANGELOG.txt` — 添加新版本/修改条目，写明改动内容、原因、涉及文件
  2. `AGENTS.md` — 如果涉及架构变更、新增功能、新硬件配置，必须更新对应章节
  3. 其他文档 — 如果 README.md 或 CLAUDE.md 有相关内容，一并更新

> 任何代码改动（包括修复 bug、新增功能、重构、UI 调整）都必须有对应的文档更新。这是强制性要求，不可跳过。

---

# 智能前级2.0 — 项目概览

## 项目简介

基于 ESP32-S3 + ESPHome 的 Hi-Fi 音频前级放大器。具备 4 路输入切换 (CD/DAC/PC/AUX)、PGA2311 音量控制、MSGEQ7 七段频谱分析、2.79 寸 TFT 彩屏显示 (LVGL)、MCP23017 I2C GPIO 扩展、温度保护等功能。

**固件版本:** v2.1.0  
**MCU:** ESP32-S3 @ 240MHz  
**框架:** ESPHome 2026.6.4 + LVGL v9.x managed component  
**仓库:** https://github.com/oupengopu/zhitong-preamp-2

---

## 硬件引脚分配

### PGA2311 音量芯片 (SPI)
| 信号 | GPIO | 说明 |
|------|------|------|
| CS | IO42 | 片选 (手动控制) |
| CLK | IO41 | SPI 时钟 (4MHz) |
| SDI (MOSI) | IO2 | 数据输入 |
| SDO | N/C | 不接 |

协议: CS 拉低 → 发 16bit (高8位=右声道, 低8位=左声道) → CS 拉高。值范围 0~255 (0=静音, 192≈0dB, 每步 0.5dB)。

### MSGEQ7 频谱分析器
| 信号 | GPIO | ADC通道 |
|------|------|---------|
| RESET | IO1 | — |
| STROBE | IO39 | — |
| R_OUT (右声道) | IO3 | ADC1_CH2 |
| L_OUT (左声道) | IO9 | ADC1_CH8 |

频段: 63Hz / 160Hz / 400Hz / 1kHz / 2.5kHz / 6.25kHz / 16kHz

### NTC 温度传感器
| 信号 | GPIO | ADC通道 |
|------|------|---------|
| NTC | IO10 | ADC1_CH9 (6dB衰减) |

电路: 3.3V → 10kΩ(上拉) → ADC → NTC(9.4kΩ@25°C) → GND  
NTC 参数: B=3950, 参考电阻 9.4kΩ@25°C

### TFT 显示屏 (2.79", NV3007 驱动)
| 信号 | GPIO |
|------|------|
| SPI MOSI | IO4 |
| SPI SCLK | IO5 |
| DC | IO6 |
| RESX | IO15 |
| BLK (背光) | IO7 (PWM) |
| CS | GND (常选) |

分辨率: 428×142 (实际像素), 渲染 2x 缩放 → 856×284

### 编码器
| 信号 | GPIO |
|------|------|
| A | IO17 |
| B | IO18 |
| D (按键) | IO8 (20ms 消抖) |

型号: EC11E18244A5，36 定位 / 18 脉冲。ESPHome `rotary_encoder` 使用 `resolution: 2`，使每个机械定位点对应 1 个软件步进。

### LED 指示灯
| 信号 | GPIO |
|------|------|
| LED | IO16 (PWM, LEDC 1kHz) |

电路: GPIO16 → 1K → NPN基极, 基极10K下拉到GND, 集电极100R→LED阴极, LED阳极→+5V  
三极管: MMBT3904 (SOT-23), 开关速度 ns 级, 1kHz PWM 绰绰有余

### I2C 总线 (MCP23017)
| 信号 | GPIO |
|------|------|
| SDA | IO38 |
| SCL | IO40 |
| INTA | IO11 |
| INTB | IO12 |

### MCP23017 扩展 (地址 0x27)
| 引脚 | 功能 | 备注 |
|------|------|------|
| GPA0 | 继电器1 (CD输入) | GPA0=继电器1, GPA1=继电器2, GPA2=继电器3, GPA3=继电器4 |
| GPA1 | 继电器2 (DAC输入) | |
| GPA2 | 继电器3 (PC输入) | |
| GPA3 | 继电器4 (AUX输入) | |
| GPA6 | 硬件静音 | **高电平=开声, 低电平=静音** |
| GPB0 | 音频检测 CD | GPB0=CD, GPB1=DAC, GPB2=PC, GPB3=AUX |
| GPB1 | 音频检测 DAC | |
| GPB2 | 音频检测 PC | |
| GPB3 | 音频检测 AUX | |
| INTA | 中断输出 Port A | 接 ESP32-S3 IO11 (继电器, 不产生中断) |
| INTB | 中断输出 Port B | **接 ESP32-S3 IO12 (音频检测输入中断, YAML interrupt_pin)** |

### 未使用的 GPIO
| GPIO | 说明 |
|------|------|
| IO45 | MSGEQ7 RESET旧引脚 (1K下拉) |
| IO46 | GND 启动模式 (不接) |

---

## 项目文件结构

```
智能前级2.0/
├── 智能前级蓝牙2.0.yaml    # 主配置文件 (~5220行, ESPHome + LVGL UI)
├── PGA/                   # C++ 驱动头文件目录
│   ├── pga2311.h          # PGA2311 SPI 驱动 (C++ namespace)
│   ├── msgeq7.h           # MSGEQ7 频谱 + NTC 温度驱动 (C++ namespace)
│   ├── ble_hid_host.h     # BLE HID Host 蓝牙遥控器驱动 (C++ namespace)
│   ├── lvgl_compat.h      # LVGL C API 声明补齐 (opaque lv_obj_t 兼容)
│   ├── settings_ui.h      # 设置页 UI 控件指针数组管理
│   └── remote_keys.h      # BLE 遥控器按键映射动作名查询
├── secrets.yaml           # WiFi/API/OTA 密钥
├── AGENTS.md              # 本文件
├── CHANGELOG.txt          # 变更历史
├── fonts/                 # 字体文件
│   ├── Montserrat-Regular.ttf
│   ├── NotoSansSC-Regular.ttf
│   └── Roboto-Regular.ttf
├── icons/                 # SVG 图标 (18 个)
├── preview/               # Web 预览模拟器
│   ├── index.html         # 2x 缩放 TFT 模拟器 (HTML/CSS/JS)
│   └── .Codex/launch.json
├── .esphome/              # ESPHome 编译缓存
└── 资料/                  # 硬件文档 (PDF 数据手册等)
```

---

## 软件架构

### 核心模块

1. **PGA2311** (`PGA/pga2311.h`) — SPI 音量芯片驱动
   - ESP-IDF SPI3_HOST, 4MHz, mode 0
   - 去重写入 (仅值变化时发送)
   - CS 手动控制, 16-bit 传输 (R 高8位, L 低8位)

2. **MSGEQ7** (`PGA/msgeq7.h`) — 频谱分析 + NTC 温度
   - ADC1 oneshot API (ESP-IDF 5.x), 12-bit
   - 双速率平滑 (上升 0.35, 下降 0.12)
   - 帧率无关峰值衰减 (powf 解耦)
   - 零漂校准, 噪声门限, 频段增益补偿
   - 线程安全: 临界区保护共享数据, `get_frame()` 快照读取
   - SpectrumFrame: combined[7], left[7], right[7], peak[7], peak_l[7], peak_r[7], temperature

3. **LVGL 兼容层** (`PGA/lvgl_compat.h`)
   - ESPHome managed component 使用 opaque lv_obj_t (PIMPL)
   - extern "C" 声明所需 LVGL C API (label/bar/led 函数)
   - 非复合 widget 直接是 lv_obj_t* 全局变量, 无需 `->obj_`

4. **ESPHome YAML** — 主配置 (~5700 行)
   - 4 个核心 LVGL 页面: splash_page, main_page, spectrum_page, settings_page
   - 3 个子页面: ble_remote_page(蓝牙遥控), remote_keys_page(遥控器按键映射), debug_page(诊断信息)
   - 页面切换: 主页面/频谱/设置/蓝牙遥控/遥控器按键/诊断信息
   - 音频检测 (MCP23017 GPB0~GPB3, active-low + inverted, 100ms 确认 / 500ms 断连)
   - 输入自动切换 (CD > DAC > PC > AUX 优先级, 支持手动覆盖)
   - 编码器: 旋转→音量, 单击→页面循环, 长按→待机
   - 显示超时自动调低亮度 (可配置 1~60 分钟, 不熄屏)

### 关键全局变量
| 变量 | 类型 | 说明 |
|------|------|------|
| `volume_val` | int | 当前音量 (0~255) |
| `current_input` | int | 当前输入 (0=CD, 1=DAC, 2=PC, 3=AUX) |
| `max_volume` | int | 最大音量限制 (10~255) |
| `power_on_limit` | int | 开机音量上限 (10~255) |
| `balance` | int | 当前输入的平衡 (-20~20) |
| `input_bal_0~3` | int | 四路输入独立平衡槽 (CD/DAC/PC/AUX, -20~20) |
| `soft_mute` | bool | 软静音标志 |
| `standby` | bool | 待机状态 |
| `current_db` | float | 当前实际 dB |
| `target_db` | float | 目标 dB (渐变更新的目标) |
| `theme` | int | 颜色主题索引 (0~7) |
| `display_timeout_min` | int | 显示超时分钟数 |
| `display_brightness` | int | 显示亮度 (0~100) |
| `last_manual_input_ms` | uint32_t | 手动选择输入的时间戳 (0=自动模式) |
| `system_ready` | bool | 系统就绪标志 |
| `switching_input` | bool | 正在切换输入 |
| `audio_cd/dac/pc/aux` | binary_sensor | 各输入音频检测状态 |
| `settings_focus_idx` | int | 设置页焦点索引 (0~11) |
| `settings_active_idx` | int | 设置页调节模式索引 (-1=浏览模式) |
| `keymap_focus_idx` | int | 遥控器按键页焦点索引 (0-7=动作映射, 8=学习按键, 9=重置学习, 10=返回) |
| `keymap_active_idx` | int | 遥控器按键页调节模式索引 (-1=浏览, ≥0=调节中) |
| `key_learn_select_idx` | int | 学习按键行当前选择的源按键动作 (1~8) |
| `key_learn_pick_mode` | bool | 学习按键行的选择模式标志 |
| `key_map_1~key_map_8` | int | 8 个标准按键的动作映射 (NVS 持久化, 默认恒等) |
| `key_map_9~key_map_14` | int | 6 个扩展键(SWIPE/CAMERA)的动作映射 (NVS, 默认恒等) |
| `learned_key_1~8_handle/len/hash` | int/uint32_t | BLE 遥控器 8 个核心源按键的 raw 指纹学习表 (NVS) |
| `key_learn_waiting` | bool | 遥控器按键页学习模式等待状态 |
| `key_learn_target` | int | 当前正在学习的源按键动作 (1~8) |
| `ble_remote_scanning` | bool | BLE 遥控扫描中 |
| `ble_remote_connected` | bool | BLE 遥控已连接 |
| `ble_remote_device_count` | int | 发现的 BLE 设备数 |
| `ble_remote_focus_idx` | int | BLE 遥控页焦点索引 |
| `ble_remote_peer_bda` | std::string | 已配对 BLE 遥控 BDA (NVS 持久化) |
| `ble_remote_battery` | int | BLE 遥控电池电量 (-1=未知) |
| `ble_remote_scan_status` | std::string | BLE 遥控页状态文字 |

### 关键脚本
| 脚本 | 说明 |
|------|------|
| `switch_input` | 切换输入: 软静音→切继电器→取消静音 |
| `apply_theme` | 应用主题色到 LVGL 控件（LED/分隔线/音量条/频谱条/设置页 bar/主题色点） |
| `auto_switch_input` | 自动输入选择 (8s 等待, 优先级 CD>DAC>PC>AUX, 无信号则保持) |
| `display_idle_timer` | 显示超时管理 (可配置分钟数, 调低亮度而非关屏) |
| `exit_standby` | 退出待机: 渐变亮度 (800ms) → 应用主题 → 恢复音量 → 自动切换 |
| `factory_reset` | 恢复出厂: 重置全局变量 → 显示提示 → 清除 WiFi → 重启 (编码器按住 10s 触发) |

### 颜色主题 (8 种，受世界名机启发)
| 索引 | 名称 | 颜色 | 灵感来源 |
|------|------|------|---------|
| 0 | 翠绿 | 0x10B981 | 高保真经典绿 |
| 1 | 赤红 | 0xEF4444 | 贵丰 (Gryphon) 雄狮红 |
| 2 | 冰蓝 | 0x7DD3FC | 冷光冰蓝 |
| 3 | 紫色 | 0x8B5CF6 | 马克莱文森 (Mark Levinson) 高贵紫 |
| 4 | 麦景图蓝 | 0x0071E3 | 麦景图 (McIntosh) 表头蓝 |
| 5 | 金嗓子金 | 0xCD9B4A | 金嗓子 (Accuphase) 香槟金 |
| 6 | 孔雀青 | 0x0F766E | 深孔雀蓝绿，高级冷调且与冰蓝明显区分 |
| 7 | 南瓜橙 | 0xF97316 | 南瓜 (Nagra) 仪表橙 |

---

## 核心功能逻辑

### 频谱分析 (50ms interval)
- 调用 `msgeq7::read()` 读取 MSGEQ7 七段频谱
- 调用 `msgeq7::read_ntc()` 读取 NTC 温度 (带中值滤波 + IIR)
- 使用 `msgeq7::get_frame()` 获取 SpectrumFrame 快照
- 更新 LVGL 频谱条 (L/R 独立, 含 peak 保持线)
- 更新 VU 电平条 (L/R 独立, 每声道 7 段平均)
- 更新 LED 信号强度 (取 L/R 较大值: >80 常亮, >5 呼吸, 否则微光)
- 约 20Hz 刷新率

### 频谱页自动切换 (基于 MSGEQ7 信号)
- 取代旧版 MCP23017 引脚电平检测，使用 MSGEQ7 真实频谱信号
- **主页面 → 频谱页**: MSGEQ7 信号存在且稳定 ≥15 秒后自动跳转
- **频谱页 → 主页面**: 信号消失 ≥10 秒后自动返回
- 静音/待机/系统未就绪时不自动跳转
- 设置页/子页面不受影响
- 编码器单击手动切换不受影响（频谱→主页、主页→设置）

### 音量渐变 (50ms interval)
- 当前 dB 向目标 dB 逼近 (每步 ~0.78dB, 约 4 步/秒)
- 写入 PGA2311 芯片 (R/L 独立, 平衡偏移)
- 更新 LVGL 音量条/数值

### 温度保护 (50ms interval)
- 触发温度: ≥72°C → 强制待机保护
- 恢复温度: <55°C 才能退出保护
- 每 10 秒读取一次 NTC (间隔计数)

### 输入自动切换
- 优先级: CD(0) > DAC(1) > PC(2) > AUX(3)
- 手动选择后: 保持当前输入直到信号丢失, 然后恢复自动切换
- 待机恢复/重启后: 自动启动

### LVGL 页面结构
| 页面 | 内容 |
|------|------|
| splash_page | 开机闪屏: "Zhitong Audio" 品牌标识 (montserrat_36, 翠绿) + "智能前级2.0" 副标题 + 版本号, 文字 500ms 渐显 |
| main_page | 状态栏(输入/自动切换/温度/音量/图标) + 大音量 + 输入选择 + 音量条 + 平衡/静音指示 + BLE遥控电池条 |
| spectrum_page | 左右声道 7 段频谱条 + 峰值保持线 + VU 电平条 + 频率标签 + 输入/温度 |
| ble_remote_page | 蓝牙遥控管理: 扫描/设备列表/连接/断开/电池电量/HID调试 |
| remote_keys_page | 遥控器按键映射: 8 个标准源按键 + 学习/重置/返回，浏览/调节双模式，单击循环切换映射目标(0-15) |
| settings_page | 单列可滚动列表，15 项：最大音量/开机上限/.../频谱跳转/频谱样式/固件版本(只读)/IP地址(只读) |
| debug_page | 诊断信息: CPU占用率、内部堆(320KB)/PSRAM(8MB)实时监控、低水位标记、运行时间 |

---

## 操作说明

### 编码器旋转
| 当前页面 | 效果 |
|---------|------|
| 主页 | 调节音量（屏幕休眠时自动恢复亮度，不再弹出大字音量浮层） |
| 频谱页 | 调音量 + 自动返回主页 |
| 设置页（浏览模式） | 移动焦点切换设置项（无限循环） |
| 设置页（调节模式） | 调整当前设置项的值（bar/数值实时更新） |
| BLE 遥控页 | 移动焦点：扫描按钮/设备槽(动态)/返回按钮（无限循环，跳过隐藏行） |
| 遥控器按键页（浏览） | 移动焦点（11 行循环：0-7 动作映射, 8 学习按键, 9 重置学习, 10 返回） |
| 遥控器按键页（调节） | 0-7 行旋转循环切换映射目标；第 8 行旋转选择要学习的源按键 |

### 编码器按键（单击）
| 当前状态 | 效果 |
|---------|------|
| 屏幕休眠 | 恢复亮度 |
| 主页 | → 进入设置页 |
| 频谱页 | → 返回主页 |
| 设置页（浏览模式） | → 进入调节模式（bar 高度 6→12px，背景更亮 0x25374F）；只读项(固件版本/IP地址)无反应；蓝牙遥控行→跳转 BLE 遥控页；遥控器按键行→跳转 remote_keys_page |
| 设置页（浏览模式, 固件版本行） | 快速单击 3 次 → 跳转 debug_page（500ms 窗口） |
| 设置页（调节模式） | → 退回浏览模式（bar 恢复 base 高度，确认修改） |
| BLE 遥控页 | → 激活焦点项：扫描/停止扫描，连接设备，断开（留在蓝牙页），返回设置 |
| 遥控器按键页（浏览, 0-7 行） | → 进入动作映射调节模式（当前行背景变亮 0x25374F） |
| 遥控器按键页（浏览, 学习按键行） | → 进入学习目标选择模式；旋转选择音量+/音量-/静音/播放暂停/下一曲/上一曲/电源/切换输入，再单击开始等待实体遥控器按键 |
| 遥控器按键页（浏览, 重置学习行） | → 清空 8 个实体遥控 raw 学习指纹，不改动作映射 |
| 遥控器按键页（调节） | → 退出调节模式（确认修改，背景恢复） |
| 遥控器按键页（返回行） | → 返回设置页 |

### 编码器按键（双击）
| 当前页面 | 效果 |
|---------|------|
| 主页 / 频谱页 | 切换静音 |
| 设置页 / 蓝牙遥控页 / 遥控器按键页 / 诊断页 | 无动作 |

### 编码器按键（长按 1.5s）
| 状态 | 效果 |
|------|------|
| 设置页 | 返回主页面 |
| 蓝牙遥控页 | 返回主页面 |
| 遥控器按键页 | 返回主页面 |
| 诊断页 | 返回主页面 |
| 主页面/频谱页（工作状态）| 进入待机 |
| 待机状态 | 唤醒（800ms 渐变亮度）|

### 编码器按键（超长按 10s — 恢复出厂设置）
| 条件 | 效果 |
|------|------|
| 任何状态按住 5 秒 | 屏幕底部显示橙色提示"继续按住恢复出厂设置..." |
| 任何状态按住 10 秒 | 执行恢复出厂：重置所有设置到默认值 → 显示"已重置，请连接热点" → 清除 WiFi 记忆 → 重启进入 AP 配网模式 |
| 中途松手 | 取消操作，隐藏提示 |

> 恢复出厂会重置以下设置到默认值：音量(160)、输入源(CD)、各输入独立平衡(0)、最大音量(255 PGA全量程)、开机上限(192 PGA 0dB)、息屏超时(5分钟)、亮度(80%)、主题(翠绿)、自动输入切换(开)、各输入独立音量(160)、按键映射(恒等: 1-8→1-8, 9-14→9-14)。

### LED 指示灯状态 (IO16, PWM)
| 状态 | LED 行为 | 亮度 | 说明 |
|------|----------|------|------|
| 正常开机 (WiFi已连) | 常亮 | 70% | 正常工作指示 |
| 静音中 | 常亮 | 35% | 半亮区分静音状态 |
| 未连 WiFi | 闪烁, 1s 周期 | 70% ↔ 0% | 500ms 亮 / 500ms 灭 |
| 开机闪屏 | 渐亮 | 0% → 70%, 1秒渐变 | 配合开机仪式感 |
| 待机 <1小时 | 呼吸灯, 3s 周期 | 5% ↔ 55% | 正弦波柔呼吸 |
| 待机 >1小时 | 5分钟平滑脉冲, 2s 宽 | 0% → 30% → 0% | 深夜省电不扰眠 |
| 温度保护 (≥72°C) | 快闪, 200ms 周期 | 80% ↔ 0% | 警示异常 |
| 工厂重置按住 5~10s | 加速闪烁 | 80% ↔ 0%, 500ms→100ms | 按住越久闪越快 |

优先级: 温度保护 > 工厂重置(5s+) > 待机 > 闪屏渐亮 > 未连WiFi > 静音 > 正常

### 显示超时
- 无操作后（默认 5 分钟，设置页可调 1~60 分钟）背光调至 50% 微光
- **不熄屏** — 保持显示内容可见，维持高级音响质感
- 旋转编码器或单击恢复用户设定亮度
- 设置项名称为"息屏超时"，实际为调低亮度而非关屏
- 代码位置: `display_idle_timer` 中 `call.set_brightness(0.50f)`。不要改回 10%/3%，实机机箱内太暗看不清

### 音量调节
- 主页旋转编码器直接调节音量，不再弹出大字音量浮层
- 频谱页旋转编码器调节音量后自动返回主页
- 四路输入使用独立音量槽 `input_vol_0~3`。调音量时保存当前输入；切换输入时先保存 `active_input`，再加载目标输入音量
- 四路输入使用独立平衡槽 `input_bal_0~3`。调声道平衡时保存当前输入；切换输入时跟随 `active_input` 保存旧输入平衡，再加载目标输入平衡
- `send_volume_to_pga` 内的安全保存必须避开 `switching_input`，否则切输入淡出阶段会把旧输入音量写入新输入槽，表现为"每个输入独立音量失效"

### 自动输入切换
- 优先级：**CD > DAC > PC > AUX**
- 当前输入信号丢失后，等待 **8 秒**看是否恢复（曲目间隙不误切）
- 8 秒内信号恢复 → 保持当前输入
- 8 秒后仍未恢复 → 按优先级切换到有信号的输入
- **所有输入均无信号 → 保持当前不动**
- 手动选择输入后保持该通道，信号丢失后恢复自动切换
- 待机恢复/重启后自动启动

### 温度保护
- ≥72°C 强制待机，屏幕显示红色 **"保护!"**
- <55°C 退出保护，恢复正常显示

### 待机
- 彻底关屏 + 静音 + 暂停 LVGL，WiFi 保持连接
- 唤醒时 800ms 渐变亮度恢复 → 应用主题 → 恢复音量 → 自动切换输入

### 开机闪屏 (Splash Screen)
- 上电后立即显示 splash_page: 中央 "Zhitong Audio" 品牌标识 (翠绿 0x10B981)
- **文字渐显动画**: 首帧微光 15/255 → 500ms 内平滑升至全亮
- **500ms 二次 show_splash_boot 是真机白屏兜底**: NV3007/背光初始化偶发白屏时需要重新显示 splash_page，并重置 fade_frame；不要再按"避免闪一下"删除
- 最短展示 **3 秒**，WiFi 连接就绪后以 **FADE_IN 300ms** 过渡到主页面
- 若 WiFi 10 秒内未连接，自动超时切到主页面（不卡死）
- splash_page 仅开机显示一次，过渡后不再使用

### 设置页交互（单列列表，图标化，15 项）
- **布局**：单列可滚动列表，每行 ~90px，含 24px MDI 图标 + 18px 标签 + 进度条(bar) + 16px 数值
- **图标**：每行左侧 24px 彩色 MDI 图标，rows 0-12 跟随主题色，rows 13-14 固定灰色 `0x6B7280`
- **焦点指示**：左边界 3px 主题色竖条 + 背景色 `0x1E2D42`
- **只读行(固件版本/IP地址)**：无焦点条，背景 `0x111827`，图标灰色
- **导航行(蓝牙遥控/遥控器按键)**：背景 `0x111827`，支持焦点高亮 + 点击跳转对应子页面
- **行高 ~90px → 约 3 行可见，超出需滚动**

**浏览模式**（`settings_active_idx = -1`）：
- 旋转编码器 → 移动焦点（无限循环）
- 自动滚动（`lv_obj_scroll_to_view`）确保焦点项可见
- 单击可调项(0-7、10-12) → 进入调节模式；导航项(8-9) → 跳转子页面

**调节模式**（`settings_active_idx = idx`）：
- 选中行 bar 高度 12px (row 0 为 16px) → 24px，背景 `0x25374F`
- 旋转编码器 → 调值，bar/数值实时更新，立即生效
- 单击 → 退回浏览模式（bar 恢复 base 高度：row 0=16px, rows 1-5=12px）
- 只读项(13,14) 单击无反应

**设置项一览**（15 项）：
| # | 名称 | 图标 | 控件 | 范围 | 说明 |
|---|------|------|------|------|------|
| 0 | 最大音量 | mdi-volume-high | bar | 10~255 | 音量上限，保护扬声器 |
| 1 | 开机上限 | mdi-restart | bar | 10~255 | 开机时不超过的值 |
| 2 | 声道平衡 | mdi-tune-vertical | bar | -20~20 | 映射到 0~40 |
| 3 | 输入选择 | mdi-swap-horizontal-bold | label | CD/DAC/PC/AUX | 循环切换 |
| 4 | 输入自动切换 | mdi-swap-horizontal | label | 自动/手动 | 开关自动输入切换 |
| 5 | 屏幕亮度 | mdi-brightness-6 | bar | 0~100 | 实时生效 |
| 6 | 息屏超时 | mdi-timer-outline | bar | 1~60 分钟 | 调低亮度而非关屏 |
| 7 | 主题色彩 | mdi-palette | label+色点 | 8 种主题色 | 循环切换，实时生效 |
| 8 | 蓝牙遥控 | mdi-bluetooth | label | 已连接/未连接 | 导航到蓝牙遥控页 |
| 9 | 遥控器按键 | mdi-keyboard-settings | label | 已映射 | 导航到遥控器按键映射页 |
| 10 | 输出模式 | mdi-swap-vertical-bold | label | 变压器/直通 | 切换输出模式 |
| 11 | 频谱跳转 | mdi-chart-timeline-variant | label | 禁用/5~60秒 | 主屏自动跳转频谱屏时间 |
| 12 | 频谱样式 | mdi-chart-bar | label+色点 | 5 种样式 | 经典柱状/镜像频谱/LED点阵/示波线/蓝表VU |
| 13 | 固件版本 | mdi-information-outline | label (只读) | — | 显示当前固件版本，三击进入诊断页 |
| 14 | IP地址 | mdi-ip-network | label (只读) | — | 显示设备 IP |

### 蓝牙遥控 (BLE HID Host)
- 图标 mdi-bluetooth，标签 "蓝牙遥控"，右侧状态 "已连接"/"未连接"
- **状态栏指示**：主页面右上角双蓝牙图标——大号 `mdi_icons_27` 为手机 APP BLE Server 连接指示，小号 `mdi_icons_12` + 微型电池条（14×7px bar）为 BLE 遥控器连接及电量（耳机电量风格：>50% 主题色、>20% 黄色、<20% 红色）
- 点击 → 跳转 `ble_remote_page`（设置页 Row 8）

### 遥控器按键映射 (Remote Keys Page)
- 图标 mdi-keyboard-settings，标签 "遥控器按键"，右侧 "已映射"
- 点击 → 跳转 `remote_keys_page`（设置页 Row 9）
- **8 个源按键**：音量+/音量-/静音/播放暂停/下一曲/上一曲/电源/切换输入
- **专门学习行**：新增"学习按键"行，负责给 8 个源按键录入实体遥控器 raw HID 指纹；旧的 8 个源按键行只负责选择动作映射，不再承担学习入口
- **重置学习行**：新增"重置学习"行，只清空 8 个 raw HID 指纹，保留 8 个动作映射；便宜遥控器二义性码较多，学习混乱时优先用这一项清表后重学
- **动作映射交互**：旋转选中 0-7 行 → 单击进入调节 → 旋转循环 16 个映射目标 → 单击确认
- **学习交互**：旋转选中"学习按键"行 → 单击进入选择模式 → 旋转选择要学习的源按键 → 再单击进入"学习中" → 按一次 BLE 实体遥控器按键 → 保存 raw HID 指纹；成功显示"已学习: <键名>"约 1.5 秒
- **映射目标**：禁用/音量+/音量-/静音/播放暂停/下一曲/上一曲/电源/切换输入/上划/下划/左划/右划/拍照/切换镜头/确定
- 映射值 NVS 持久化，工厂重置恢复恒等
- raw 学习表同样 NVS 持久化，工厂重置会清空；运行时只查学习表，匹配不到直接忽略，不再走内置 `_parse_report()` 兼容逻辑

### BLE 遥控页
| 行 | 内容 | 说明 |
|----|------|------|
| 0 | 标题 "蓝牙遥控" | 翠绿 18px font_cn |
| 1 | 连接状态 | "未连接" / "扫描中..." / "已连接: xxx 电池: XX%" |
| 1.5 | 电池电量 | 独立行显示百分比，<20% 红色 |
| 2 | 扫描按钮 | 点击开始/停止扫描 |
| 3-5 | 设备槽 (最多3个) | 显示名称 + RSSI，点击连接 |
| 6 | 原始 HID 事件调试 | 连接时显示最近一次 HID 报告 (Page/Usage/Val) |
| 7 | 断开/返回按钮 | 已连接→断开（留在蓝牙页），未连接→返回设置页 |

---

## BLE HID Host 物理遥控器

基于 `PGA/ble_hid_host.h` (namespace `ble_hid_host`)，ESP-IDF Bluedroid GAP + GATT Client，ESP32-S3 同时运行 BLE Server（已有）+ BLE Client（双角色）。

### BLE HID 自学习模式（当前固件规则）
- 当前固件的实体遥控动作只使用自学习 RawFingerprint 表。HID notify 到来后先记录原始报文供学习页使用，再调用 `_match_learned_key(fp)`；匹配不到学习表就直接忽略，不再走内置标准码/兼容码/二义性码预设。
- 旧版 `[80 00]`、`[40 00]`、0x0027 坐标包、Consumer Usage 等预设码仅作为历史调试资料保留；不要再把它们直接映射成音量、静音、上一首/下一首等动作，否则会重新引入“静音短按先加音量”“长按减音量变静音”等冲突。
- 学习入口在“遥控器按键”页的专用学习行中选择动作，再按实体遥控对应按键保存。原先每个动作行双击学习的方案已废弃。

### 架构要点
- 头文件 only，静态函数，风格同 `PGA/pga2311.h`/`PGA/msgeq7.h`
- FreeRTOS 队列桥接 BLE 回调线程 → ESPHome 主循环
- BDA 持久化到 NVS（`ble_remote_peer_bda`，`restore_value=true`）
- 断开自动重试 3 次，间隔逐次增大
- 待机时 BLE 连接保持，遥控器电源键可唤醒

### BLE 遥控器配对流程
1. 设置页 → 点击 "蓝牙遥控" → 进入 ble_remote_page
2. 点击 "开始扫描" → 10s 主动扫描 → 列表显示发现的 HID 设备
3. 旋转编码器选择设备 → 单击连接 → "正在连接..." → "已连接: <设备名>"
4. BDA 自动保存，下次开机自动重连

### 兼容设备
标准 BLE HID 设备（服务 UUID 0x1812），如蓝牙媒体遥控器、自拍遥控器、PPT 翻页器。**不支持**经典蓝牙 (Bluetooth Classic) 遥控器。

### BLE HID 遥控器键位映射

代码中所有码带标注: `[标准]`=USB HID 规范定义, `[兼容]`=廉价遥控器实测有效, `[自定义]`=特定遥控器。
以下码表仅用于识别旧日志/调试资料，当前固件不再直接使用这些预设码触发动作；实体遥控必须先在“遥控器按键”页自学习后才会生效。

#### Consumer 报文 (2/3字节, `uint16_t usage`, Usage Page 0x0C)
| Usage ID | 标注 | 功能 | 事件 |
|----------|------|------|------|
| 0xE9 | 标准 | 音量+ | `HID_EVT_VOLUME_UP` |
| 0xEA | 标准 | 音量− | `HID_EVT_VOLUME_DOWN` |
| 0xE2 | 标准 | 静音 | `HID_EVT_MUTE` |
| 0xCD | 标准 | 播放/暂停 | `HID_EVT_PLAY_PAUSE` |
| 0xB5 | 标准 | 下一曲 | `HID_EVT_NEXT_TRACK` |
| 0xB6 | 标准 | 上一曲 | `HID_EVT_PREV_TRACK` |
| 0x30 | 标准 | 电源 | `HID_EVT_POWER` |
| 0x233 | 自定义 | 切换输入 | `HID_EVT_CYCLE_INPUT` |

#### Keyboard Boot 报文 (8字节, `bytes 2-7 uint8_t key`, Usage Page 0x07)
| Key Code | 标注 | USB 实际含义 | 功能 | 事件 |
|----------|------|-------------|------|------|
| 0xE9,0x80 | 兼容 | Consumer 码借壳 | 音量+ | VOLUME_UP |
| 0xEA,0x81 | 兼容 | Consumer 码借壳 | 音量− | VOLUME_DOWN |
| 0xE2,0x7F | 兼容 | Consumer 码借壳 | 静音 | MUTE |
| 0xCD | 兼容 | Consumer 码借壳 | 播放/暂停 | PLAY_PAUSE |
| 0xB5 | 兼容 | Consumer 码借壳 | 下一曲 | NEXT |
| 0xB6 | 兼容 | Consumer 码借壳 | 上一曲 | PREV |
| 0x30,0x66 | 兼容+标准 | Kbd 3 / Kbd Power | 电源 | POWER |
| 0x68 | 标准 | F13 | 切换输入 | CYCLE_INPUT |
| 0x28 | 标准 | Enter | 确定 | OK |
| 0x2C | 标准 | Space | 拍照(自拍快门) | CAMERA |
| 0x52,0x4B | 标准 | ↑ / PageUp | 上划 | SWIPE_UP |
| 0x51,0x4E | 标准 | ↓ / PageDown | 下划 | SWIPE_DOWN |
| 0x50 | 标准 | ← | 左划 | SWIPE_LEFT |
| 0x4F | 标准 | → | 右划 | SWIPE_RIGHT |
| 0x8C | 自定义 | International1 | 切换镜头(预留) | CAMERA_SWITCH |

> ⚠️ Keyboard `0x30` = 数字键 '3'，标准键盘按 3 会误触电源事件。这是廉价遥控器兼容性代价，`ble_hid_host.h` L270 保留此映射。Consumer 报文的 `0x233` = 563 (>255) 仅 `uint16_t` 可容纳，Keyboard 报文用 `0x68`(F13) 替代。

防抖 30ms，全零释放帧忽略，长按时反复触发。新增事件 SWIPE_UP(9)~OK(15) 默认仅日志记录 + HA 推送，可通过遥控器按键页映射为音量/静音等有效动作。

#### 实测蓝牙遥控器特殊报文
- raw 学习模式使用 `handle + len + FNV-1a(data)` 作为指纹，只支持 8 个核心源按键 (1~8)，不会替代完整 HID Report Map 解析器
- 学习时过滤全零/释放帧；学习成功后清空当前 HID 队列，避免学习那一下同时触发功能
- `[40 00]` 是二义性报文: 单发忽略，900ms 内连续重复才按音量加处理，避免静音短按附近的杂帧误加音量
- `[80 00]` 是二义性报文: 短按/单发按静音处理，连续重复/长按按音量减处理
- `[80 00]` 后面的 `[00 00]` 释放帧**不能立即判定为静音**，否则长按音量减会被误触发成连续静音
- 旧版 `pending_ambig80_mute` / `suppress_power_until_ms` 逻辑已废弃；当前不再用预设码直接触发实体动作
- `0x0027` 十字/触摸类报文中，`[00 33 0B AC 04 ...]` 为下一首，`[00 CD 04 AC 04 ...]` 为上一首，仅作为旧日志识别资料；实际控制必须先自学习
- 长按音量加/减结束时遥控器可能发出 0x0027 的伪电源包；自学习模式下未学习电源键时不会误待机

### 三路同步机制
编码器旋钮、BLE 遥控器、HA 三路都汇聚到同一个 `volume_val` + `send_volume_to_pga`：

```
编码器旋转 → volume_val += diff → volume_number→publish_state() → HA
                                  → send_volume_to_pga → PGA2311 芯片
                                                        → volume_number→publish_state() → HA

BLE 遥控器 → volume_val++ → send_volume_to_pga → PGA2311 芯片
                                                → volume_number→publish_state() → HA

HA 滑条   → volume_number.set_action → volume_val 转换 → send_volume_to_pga → PGA2311
```

`send_volume_to_pga` 脚本尾部同时发布 `volume_number`、`balance_number`、`media_volume` 到 HA，确保所有状态同步。

### 新增功能（对比 fsievers22/esphome-ble-remote）
1. **电池电量读取** — 发现 Battery Service (0x180F) + Battery Level characteristic (0x2A19)，CCCD 注册通知 + 初始读取，在 ble_remote_page 显示百分比
2. **HID 原始事件调试查看器** — NOTIFY_EVT 中捕获 `_last_raw_page/_usage/_value`，`has_raw_event()` / `get_last_raw_event_string()` 供 YAML lambda 和 LVGL 显示
3. **Home Assistant 事件推送** — `ble_hid_event_text` text_sensor，`on_value` → `homeassistant.event: esphome.hid_events`，按键数据随事件推送

---

## 编译和运行

### 配置验证（先验证再编译）
```bash
esphome config 智能前级蓝牙2.0.yaml
```

### 编译
```bash
esphome compile 智能前级蓝牙2.0.yaml
```

### 上传
```bash
esphome run 智能前级蓝牙2.0.yaml
```

### Web 预览 (TFT 模拟器)
```bash
cd preview && py -3.11 -m http.server 8084
```
然后在浏览器打开 `http://localhost:8084`，显示 2x 缩放 (856×284) 的 TFT 模拟。

### 预览服务器配置
`.Codex/launch.json` 中配置了 preview-server，可通过 `preview_start` 工具启动。

---

## 项目核心

### ⚠️ `send_volume_to_pga` — 本项目最核心的脚本
此脚本是整个固件的**心脏**，v1.7.2 已经过多轮论证和优化。**绝对禁止修改此脚本**，任何对此脚本的改动（如移除 while 循环、改为 interval 步进等）都会破坏音量控制的稳定性和时序。

> **设计说明**: 此脚本使用 `while` + `- delay: 15ms` 进行音量渐变。
> **脚本中的 `- delay:` 并非阻塞** — ESPHome 的脚本引擎基于异步协程（Coroutine），执行到 `- delay: 15ms` 时调度器会释放 CPU 并 `yield` 给主循环，WiFi 栈、BLE 通信和 `interval: 50ms` 在此期间仍会正常运转。
> 
> **为何观察到的 interval 执行会延迟？** 根本原因不是主循环被阻塞，而是在 while 循环中高频度（每 15ms 一次）抢占 SPI3 总线调用 `pga2311::set_volume()`，同时 `interval: 50ms` 也在读取 ADC 和刷新 LVGL。这种高频异步任务交替引发了**CPU 缓存抖动（Cache Thrashing）**以及 ESPHome 内核任务队列的拥挤，导致 interval 的实际调度时间点被推迟。
>
> **结论**: 此脚本当前的设计是安全且经过验证的，不会触发 Watchdog 或断开 WiFi。文档保留"禁止修改"的警告，但纠正对阻塞机制的解释。

---

## 关键注意事项

1. **freetype-py 中文路径问题**: Windows 下 `FT_New_Face` 无法加载含中文的绝对路径。修复方案是回退到 `FT_New_Memory_Face` (读取文件到内存再加载)。此补丁需在 freetype-py 升级后重新应用。

2. **LVGL opaque 类型**: ESPHome 2026 managed component 的 lv_obj_t 是 opaque 类型。非复合 widget (bar/label/led/obj) 的 `id()` 直接返回 `lv_obj_t*`，不需要 `->obj_` 间接访问。复合 widget (dropdown/roller) 返回 `LvDropdownType*` 等类型, 需要先强转 `(lv_obj_t*)`。所有 LVGL C API 函数需在 `lvgl_compat.h` 的 `extern "C"` 块中声明。

3. **MCP23017 中断配置**: 配置为 Open-Drain + Active-Low (`open_drain_interrupt: true`)，ESP32-S3 端用内部上拉，防电平冲突。**音频检测输入端建议加硬件 RC 低通滤波**——音频信号临界点的高频抖动会引发中断风暴。若测试中遇到"某路输入突然不检测"的现象，疑似 MCP23017 中断锁死（INT 保持低电平不复位），需 I2C 读取 GPIO/INTCAP 寄存器手动清除。

4. **ESP32-S3 NTC 校准**: 使用 `ADC_ATTEN_DB_6` + `curve_fitting` 校准方案。6dB 衰减虽量程较小 (0~2.5V) 但线性远优于 12dB, 对室内前级足够。

5. **MSGEQ7 时序**: 3.3V 供电时输出建立需 36-40μs (vs 5V 的 18μs), STROBE 低脉冲和 RESET 脉冲均已放宽到 40-100μs。

6. **音频检测反转**: MCP23017 的音频检测 binary_sensor 配置为 INPUT_PULLUP, 且有 `inverted: true`（检测到信号时引脚被拉低）。

7. **继电器映射**: 继电器1(CD)->GPA0, 继电器2(DAC)->GPA1, 继电器3(PC)->GPA2, 继电器4(AUX)->GPA3。

8. **MCP23017 地址**: 0x27, I2C bus_a (IO38/IO40)。

9. **PGA2311 SPI 时钟**: 4MHz (数据手册最高 6.25MHz, 留余量)。

10. **YAML 文件名含中文字符**: 主配置文件 `智能前级蓝牙2.0.yaml` 含中文与空格。ESPHome/ESP-IDF 工具链在 Windows 下可能遇到路径编码问题（如 freetype-py 的 FT_New_Face 无法加载含中文路径）。git 操作时注意文件名编码，编译时使用完整路径。

11. **standby_switch 命名约定**: HA 实体名"待机模式"，但 ON=设备运行中，OFF=待机模式。`turn_off_action` → 进入待机，`turn_on_action` → 退出待机。新增待机相关逻辑时严格遵守此约定。

12. **硬件静音 GPA6 极性**: 必须牢记硬件是 **高电平=开声, 低电平=静音**。固件里的 `mute_switch` 使用 `inverted: true`，让 HA 的"静音-HW"开关语义变成 ON=输出低电平=静音，OFF=输出高电平=开声。以后修改静音逻辑时不要把 `switch.turn_on` 当成开声。

13. **font_cn_small 字体覆盖**: 作为 `default_font`，任何新增 UI 文字（尤其是 BLE 页状态文本）必须确保字符已加入 glyphs 列表（当前约 90 字）。缺字导致 LVGL 渲染空白框。此字体与 font_cn (18px) 独立维护，需分别添加。

14. **YAML 嵌套 if/then/else 缩进陷阱**: ESPHome YAML 中 `- if:` 的 `condition:`/`then:`/`else:` 必须同缩进层级（比 `- if:` 多 4 空格）。`else:` 比 `condition:` 少 2 空格会导致解析器将 `else:` 和后续 `- if:` 误识别为同一 action 条目的两个 key，报错 "Cannot have two actions in one item. Key 'if' overrides 'else'!"。修复时注意 `else:` 本身 + 其下方整个子块的缩进联动。

15. **`remote_keys::get_action_name()` 返回类型**: 必须返回 `std::string` 而非 `const char*`。ESPHome 的 `text: !lambda` 代码生成器会在返回值上自动调用 `.c_str()`，对 `const char*` 再调 `.c_str()` 无效（编译报错 "request for member 'c_str' in ... which is of non-class type 'const char*'"）。直接传给 `lv_label_set_text()` 时需要手动 `.c_str()`。

16. **pga2311.h 与 msgeq7.h 变量风格统一**: 两者均使用 `inline` 变量（C++17 ODR 安全），禁止使用 `static`。`static` 会在多翻译单元场景下产生独立副本，导致 `_last_r/_last_l` 去重失效和 SPI 设备重复初始化。

17. **MSGEQ7 零漂校准前提假设**: 校准在上电时执行，假设此时无音频输入。如果用户先开音响再开前级，校准值会被污染导致频谱幅度偏低。无运行时恢复机制（代码注释已承认）。未来可加 HA 服务或设置页按钮触发重新校准。

18. **send_volume_to_pga 并发边缘场景**: BLE 遥控器和编码器同时触发时，`mode: restart` 丢弃首次调用，音量响应可能延迟约 300ms。当前可接受，属已知限制。

19. **BLE reconnect_retries 溢出保护**: 已加 `if (> 1000) = 4` 防溢出帽。正常运行不超过 3，长期断线才累积。

20. **lvgl_compat.h extern "C" 版本脆弱性**: ESPHome 大版本升级（如 2026→2027）时 LVGL API 签名变化会导致链接期而非编译期崩溃。建议大版本升级后逐一核对 `extern "C"` 块中的函数签名与 ESPHome 内置 LVGL 头文件一致。

21. **主页输入卡片显示职责分离**: `current_input` 只控制输入卡片外框和选中背景；未选中卡片也必须保留灰色外框（当前约 `LV_OPA_40`），不能完全透明。MCP23017 音频检测只控制图标、名称、`SIGNAL/IDLE` 明暗；MSGEQ7 真实频谱只控制当前卡片里的小 LED/条和呼吸搜索信号。不要因为当前输入被选中就点亮图标或 `SIGNAL`。

22. **固件版本隐藏入口**: 设置页焦点停在"固件版本"行时，连续单击 3 次进入诊断页。代码使用 `firmware_hidden_click_count` 做普通单击计数兜底，不再只依赖 `on_multi_click` 的三击匹配；后者在实机上可能被单击/双击规则先消耗。

23. **诊断页布局**: 诊断页跟网页预览保持两列网格风格，标题整行，数据项左右两列，底部为"MSGEQ7 重校准 (单击执行)"与"返回设置"。新增中文文本时必须同步补 `font_cn_small` 与 `font_cn` glyphs，例如"双/中/占/闲/全/段/低/水/位/完/成/失/败"。

24. **独立输入音量保存边界**: 四路输入独立音量槽为 `input_vol_0~3`，运行态跟踪当前实际输出通道用 `active_input`。切输入流程中，调用方会先把 `current_input` 改成目标输入，所以 `switch_input` 必须用 `active_input` 保存旧输入音量，再用 `switching_target_input` 加载目标输入音量。`send_volume_to_pga` 内部的安全保存必须包含 `!id(switching_input)` 条件，避免切换过程把旧输入音量写进新输入槽。

25. **独立输入平衡保存边界**: 四路输入独立平衡槽为 `input_bal_0~3`，跟独立音量使用同一切换边界。调平衡入口必须调用 `save_current_input_balance(current_input, balance)`；`switch_input` 必须先用 `active_input` 保存旧输入平衡，再用 `switching_target_input` 加载目标输入平衡。不要改 `send_volume_to_pga` 核心渐变脚本来做存储，它只负责按当前 `balance` 计算左右声道。

26. **BLE `[80 00]` 二义性遥控码（历史逻辑，已废弃）**: 旧版曾用 `pending_ambig80_mute` 延迟判定来区分短按静音和长按音量减，但实机会出现误触发和串键。当前固件只允许自学习 RawFingerprint 触发实体遥控动作；不要恢复 `[80 00]`/`[40 00]`/0x0027 等预设码直连动作逻辑。

27. **暗屏亮度固定 50%**: `display_idle_timer` 的暗屏亮度应保持 `call.set_brightness(0.50f)`。早期 3%/10% 在实机机箱里不可读，后续不要再按旧变更记录改低。

---

## 已知编译器问题

### 1. ruamel.yaml font glyphs 合并 bug (ESPHome 2026.4.x ~ 2026.5.x)

当 `font:` 段中部分字体有 `glyphs:` 而部分没有时，ruamel.yaml 会将所有字体的 glyphs 合并到第一个字体条目中。这会导致 `Failed config` 警告（cosmetic，不阻断编译）。

**规避方法**：要么所有字体都有 `glyphs:`，要么全都没有。本项目采用：仅 MDI 字体有 `glyphs:`（需要子集化），Roboto/NotoSans/Montserrat 无 `glyphs:`（全字体编译，体积略大）。

### v2.1.0+ 音量迁移 (NVS)

v2.1.0 起 `volume_val` 直接映射 PGA2311 寄存器值 (0-255)，取代旧的百分比编码。旧固件升级时，启动脚本自动迁移：

```
新 reg = 旧 vol / 旧 max * 255
```

迁移通过 `v210_migrated` (NVS 持久化 bool) 保证只运行一次。迁移后 `max_volume` 设为 255，`power_on_limit` 设为 192。各输入源独立音量同步缩放。

### 2. switch-case 变量跨越 case (ESP-IDF 5.5 + GCC)

在 `switch` 语句的 `case` 内声明变量（如 `int x = 0`）时，如果后续 `case` 没有用 `{}` 包围，GCC 会报 `crosses initialization of` 错误。

**修复方案**：每个声明变量的 `case` 都用 `{ }` 包围。

### 3. portENTER_CRITICAL 参数类型 (ESP-IDF 5.5)

`portENTER_CRITICAL()` 需要 `portMUX_TYPE*` 参数，不能传 `SemaphoreHandle_t`。`ble_hid_host.h` 中的 `dropped_events` 计数器已改为原子递增，不再使用临界区。

### 4. is_temperature_valid 前向声明

`msgeq7.h` 中 `is_temperature_valid()` 必须在 `read_ntc()` 之前声明，否则 GCC 报 `was not declared in this scope`。

### 5. guard_ok 跨 lambda 不可见

ESPHome YAML 中，`guard_ok` 在一个 `lambda:` 块中声明，不能在另一个独立的 `lambda:` 或 `if:` 条件中访问。应合并为单个 lambda 并使用 `id(script)->execute()` 内联执行。

---

## 远程编译

项目通常在远程 Docker 环境编译（ESPHome 官方 Docker 镜像）。编译命令：

```bash
# 复制 YAML 到 Docker (文件名可简化为英文)
cp 智能前级蓝牙2.0.yaml /config/esphome/20.yaml
esphome compile /config/esphome/20.yaml
```

> 注意：编译机无需本地字体文件，ESPHome 会自动从 Google Fonts 和 jsDelivr CDN 下载。

输出固件位置：
```
/data/build/zhitong-preamp-2/.pioenvs/zhitong-preamp-2/firmware.bin
```

### 关键注意事项

**30. 学习按键时自动重置 key_map 映射**

学习按键和 key_map 动作映射是两层独立机制:
- learned_key_N - NVS 持久化的实体遥控器 raw HID 指纹
- key_map_N - NVS 持久化的动作映射表（源按键 N 映射到哪个动作）

如果学习了一个按键（写入 learned_key_N）但 key_map_N 之前被意外修改（比如在遥控器按键页旋转编码器改变了映射值），就会导致学A得B的 bug。

**修复**: save_learned_remote_key 脚本在 set_learned_key() 之后自动重置对应的 key_map_N 到恒等值 (N)。这样无论之前映射页怎么调过，只要重新学习就强制同步。

**31. 学习按键时自动清除同指纹旧槽位**

_match_learned_key() 从 action=1 遍历到 9，返回第一个匹配的指纹。如果之前把一个实体按键学成 action 4（播放），后来又学成 action 9（切换输入），learned[4] 和 learned[9] 会有相同的 (handle, len, kind, value) 指纹，导致 action 4 永远先匹配，按出来永远是播放。

**修复**: set_learned_key() 在写入新槽位前，先遍历所有其他槽位，如果发现同指纹的旧槽位就清除。确保每个实体按键指纹唯一对应一个 action 槽。

改动文件: PGA/ble_hid_host.h，set_learned_key() 函数内。

**32. 中心键判定加趋势追踪保护**

上键的方向手势从 y=0x059A 运动到 y=0x1333 时，中间会经过 y=0x0614，这个坐标落在中心定区 (0x0666 ± 150) 内。_normalize_report 先检查中心区再检查追踪延续，导致上键中途的包被误判成中间键。

**修复**: 中心键判定条件加 !trend.tracking。已经在趋势追踪中的方向键跨过中心区时不再被误判为中心键。

改动文件: PGA/ble_hid_host.h，_normalize_report() 函数内 line 430。
**33. mdi_settings_icons_24 字体缺少图标码位**

学习页的"学习按键"、"重置学习"、"暂停"图标，以及信息页的"MSGEQ7 重校准"图标使用 `mdi_settings_icons_24` 字体（18px），但该字体 glyphs 列表缺少对应的 4 个 Unicode PUA 码位，导致显示空白。

**修复**: 在 `mdi_settings_icons_24` 字体的 `glyphs:` 列表中新增 4 个码位：
  - U+F03E4 (暂停图标)
  - U+F0652 (学习按键图标)
  - U+F0450 (重置学习图标)
  - U+F0415 (MSGEQ7校准/刷新图标)

改动文件: `智能前级蓝牙2.0.yaml` line 717，glyphs 列表。
