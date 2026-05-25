# 智能前级2.0 — 项目概览

## 项目简介

基于 ESP32-S3 + ESPHome 的 Hi-Fi 音频前级放大器。具备 4 路输入切换 (CD/DAC/PC/AUX)、PGA2311 音量控制、MSGEQ7 七段频谱分析、2.79 寸 TFT 彩屏显示 (LVGL)、MCP23017 I2C GPIO 扩展、温度保护等功能。

**固件版本:** v2.0.0  
**MCU:** ESP32-S3 @ 240MHz  
**框架:** ESPHome 2026.5.0 + LVGL v9.x managed component

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
| D (按键) | IO8 (150ms 消抖) |

### LED 指示灯
| 信号 | GPIO |
|------|------|
| LED | IO16 (PWM) |

### I2C 总线 (MCP23017)
| 信号 | GPIO |
|------|------|
| SDA | IO38 |
| SCL | IO40 |

### MCP23017 扩展 (地址 0x27)
| 引脚 | 功能 | 备注 |
|------|------|------|
| GPA0 | 继电器1 (CD输入) | GPA3~GPA0 映射 继电器4~1 |
| GPA1 | 继电器2 (DAC输入) | |
| GPA2 | 继电器3 (PC输入) | |
| GPA3 | 继电器4 (AUX输入) | |
| GPA6 | 硬件静音 | |
| GPB0 | 音频检测 CD | GPB3~GPB0 映射 AUX~CD |
| GPB1 | 音频检测 DAC | |
| GPB2 | 音频检测 PC | |
| GPB3 | 音频检测 AUX | |

> **注意:** 引脚映射是反转的（GPA3~GPA0 对应继电器4~1, GPB3~GPB0 对应 AUX~CD）

### 未使用的 GPIO
| GPIO | 说明 |
|------|------|
| IO45 | MSGEQ7 RESET旧引脚 (1K下拉) |
| IO46 | GND 启动模式 (不接) |

---

## 项目文件结构

```
智能前级2.0/
├── 智能前级2.0.yaml      # 主配置文件 (~3500行, ESPHome + LVGL UI)
├── pga2311.h              # PGA2311 SPI 驱动 (C++ namespace)
├── msgeq7.h               # MSGEQ7 频谱 + NTC 温度驱动 (C++ namespace)
├── lvgl_compat.h          # LVGL C API 声明补齐 (opaque lv_obj_t 兼容)
├── secrets.yaml           # WiFi/API/OTA 密钥
├── CLAUDE.md              # 本文件
├── CHANGELOG.txt          # 变更历史
├── fonts/                 # 字体文件
│   ├── Montserrat-Regular.ttf
│   ├── NotoSansSC-Regular.ttf
│   └── Roboto-Regular.ttf
├── icons/                 # SVG 图标 (18 个)
├── preview/               # Web 预览模拟器
│   ├── index.html         # 2x 缩放 TFT 模拟器 (HTML/CSS/JS)
│   └── .claude/launch.json
├── .esphome/              # ESPHome 编译缓存
└── 资料/                  # 硬件文档 (PDF 数据手册等)
```

---

## 软件架构

### 核心模块

1. **PGA2311** (`pga2311.h`) — SPI 音量芯片驱动
   - ESP-IDF SPI3_HOST, 4MHz, mode 0
   - 去重写入 (仅值变化时发送)
   - CS 手动控制, 16-bit 传输 (R 高8位, L 低8位)

2. **MSGEQ7** (`msgeq7.h`) — 频谱分析 + NTC 温度
   - ADC1 oneshot API (ESP-IDF 5.x), 12-bit
   - 双速率平滑 (上升 0.35, 下降 0.12)
   - 帧率无关峰值衰减 (powf 解耦)
   - 零漂校准, 噪声门限, 频段增益补偿
   - 线程安全: 临界区保护共享数据, `get_frame()` 快照读取
   - SpectrumFrame: combined[7], left[7], right[7], peak[7], peak_l[7], peak_r[7], temperature

3. **LVGL 兼容层** (`lvgl_compat.h`)
   - ESPHome managed component 使用 opaque lv_obj_t (PIMPL)
   - extern "C" 声明所需 LVGL C API (label/bar/led 函数)
   - 非复合 widget 直接是 lv_obj_t* 全局变量, 无需 `->obj_`

4. **ESPHome YAML** — 主配置 (~3500 行)
   - 4 个 LVGL 页面: main_page, spectrum_page, volume_big, settings_page
   - 3 个页面切换: 主页面/频谱/设置 + 大字音量
   - 音频检测 (MCP23017 GPB0~GPB3, 100ms 上沿延迟, 500ms 下沿延迟)
   - 输入自动切换 (CD > DAC > PC > AUX 优先级, 支持手动覆盖)
   - 编码器: 旋转→音量, 单击→页面循环, 长按→待机
   - 显示超时自动息屏 (可配置 1~60 分钟)

### 关键全局变量
| 变量 | 类型 | 说明 |
|------|------|------|
| `volume_val` | int | 当前音量 (0~255) |
| `current_input` | int | 当前输入 (0=CD, 1=DAC, 2=PC, 3=AUX) |
| `max_volume` | int | 最大音量限制 (10~255) |
| `power_on_limit` | int | 开机音量上限 (10~255) |
| `balance` | int | 平衡 (-20~20) |
| `soft_mute` | bool | 软静音标志 |
| `standby` | bool | 待机状态 |
| `current_db` | float | 当前实际 dB |
| `target_db` | float | 目标 dB (渐变更新的目标) |
| `theme` | int | 颜色主题索引 (0~6) |
| `display_timeout_min` | int | 显示超时分钟数 |
| `display_brightness` | int | 显示亮度 (0~100) |
| `last_manual_input_ms` | uint32_t | 手动选择输入的时间戳 (0=自动模式) |
| `system_ready` | bool | 系统就绪标志 |
| `switching_input` | bool | 正在切换输入 |
| `audio_cd/dac/pc/aux` | binary_sensor | 各输入音频检测状态 |

### 关键脚本
| 脚本 | 说明 |
|------|------|
| `switch_input` | 切换输入: 软静音→切继电器→取消静音 |
| `apply_theme` | 应用主题色到 LVGL 控件 |
| `auto_switch_input` | 自动输入选择 (优先级, 手动覆盖, 500ms 防抖) |
| `display_idle_timer` | 显示超时管理 (可配置分钟数) |
| `exit_standby` | 退出待机: 渐变亮度 (800ms) → 应用主题 → 恢复音量 → 自动切换 |

### 颜色主题
| 索引 | 名称 | 颜色 |
|------|------|------|
| 0 | 翠绿 | 0x10B981 |
| 1 | 天蓝 | 0x3B82F6 |
| 2 | 紫罗兰 | 0x8B5CF6 |
| 3 | 粉红 | 0xEC4899 |
| 4 | 橙红 | 0xF97316 |
| 5 | 青色 | 0x06B6D4 |
| 6 | 经典绿 | 0x34D399 |

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
| main_page | 状态栏(输入/温度/音量/图标) + 大音量 + 输入选择 + 音量条 + 平衡/静音指示 |
| spectrum_page | 左右声道 7 段频谱条 + 峰值保持线 + VU 电平条 + 频率标签 + 输入/温度 |
| volume_big | 大字音量 (编码器旋转时临时显示) |
| settings_page | 7 个设置项 (最大音量/开机限制/平衡/显示超时/亮度/主题/IP/版本) |

---

## 编译和运行

### 编译
```bash
esphome compile 智能前级2.0.yaml
```

### 上传
```bash
esphome run 智能前级2.0.yaml
```

### Web 预览 (TFT 模拟器)
```bash
cd preview && py -3.11 -m http.server 8084
```
然后在浏览器打开 `http://localhost:8084`，显示 2x 缩放 (856×284) 的 TFT 模拟。

### 预览服务器配置
`.claude/launch.json` 中配置了 preview-server，可通过 `preview_start` 工具启动。

---

## 关键注意事项

1. **freetype-py 中文路径问题**: Windows 下 `FT_New_Face` 无法加载含中文的绝对路径。修复方案是回退到 `FT_New_Memory_Face` (读取文件到内存再加载)。此补丁需在 freetype-py 升级后重新应用。

2. **LVGL opaque 类型**: ESPHome 2026 managed component 的 lv_obj_t 是 opaque 类型。非复合 widget (bar/label/led/obj) 的 `id()` 直接返回 `lv_obj_t*`，不需要 `->obj_` 间接访问。复合 widget (dropdown/roller) 返回 `LvDropdownType*` 等类型, 需要先强转 `(lv_obj_t*)`。所有 LVGL C API 函数需在 `lvgl_compat.h` 的 `extern "C"` 块中声明。

3. **ESP32-S3 NTC 校准**: 使用 `ADC_ATTEN_DB_6` + `curve_fitting` 校准方案。6dB 衰减虽量程较小 (0~2.5V) 但线性远优于 12dB, 对室内前级足够。

4. **MSGEQ7 时序**: 3.3V 供电时输出建立需 36-40μs (vs 5V 的 18μs), STROBE 低脉冲和 RESET 脉冲均已放宽到 40-100μs。

5. **音频检测反转**: MCP23017 的音频检测 binary_sensor 配置为 INPUT_PULLUP, 且有 `inverted: true`（检测到信号时引脚被拉低）。

6. **继电器映射反转**: CPA3/GPA2/GPA1/GPA0 对应继电器 4/3/2/1, 不是顺序映射。

7. **MCP23017 地址**: 0x27, I2C bus_a (IO38/IO40)。

8. **PGA2311 SPI 时钟**: 4MHz (数据手册最高 6.25MHz, 留余量)。
