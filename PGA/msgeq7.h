#pragma once

#ifndef UNIT_TEST
#include "esphome.h"
#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_rom_sys.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#endif

// MSGEQ7 七段频谱分析器 (智能前级2.0 / ESP32-S3)
//   RESET  → IO1
//   STROBE → IO39
//   R_OUT  → IO3   (ADC1_CH2, 右声道频谱)
//   L_OUT  → IO9   (ADC1_CH8, 左声道频谱)
//   NTC    → IO10  (ADC1_CH9, 温度传感器，共用ADC1)
//
// 频段: 63Hz, 160Hz, 400Hz, 1kHz, 2.5kHz, 6.25kHz, 16kHz
// 协议: RESET高脉冲 → STROBE低脉冲×7 → 每次读ADC
//
// STROBE 空闲高电平说明:
//   MSGEQ7 上电后 STROBE 应为 LOW，但我们的复位序列会先 RESET 再 STROBE，
//   所以空闲时 STROBE=HIGH 不影响读数 — read() 每次都从 RESET 开始，
//   第一个 STROBE 下降沿即触发 63Hz 输出，时序完全合规。
//
// NTC 分压电路 (DOWNSTREAM = NTC 接地端):
//   3.3V → 10kΩ(上拉) → ADC测量点 → NTC(9.4kΩ@25°C) → GND
//   R_ntc = R_series × V_adc / (V_ref - V_adc)
//
// 线程安全:
//   写端 (read/read_ntc) 在临界区内更新共享数据
//   读端通过 get_frame() / get_temperature() 加锁快照, 禁止直接访问内部数组

#define MSGEQ7_RESET_PIN    1
#define MSGEQ7_STROBE_PIN   39
#define MSGEQ7_R_ADC_CH     ADC_CHANNEL_2     // GPIO3  (原 ADC1_CHANNEL_2 → 新 API ADC_CHANNEL_2)
#define MSGEQ7_L_ADC_CH     ADC_CHANNEL_8     // GPIO9
#define MSGEQ7_NTC_ADC_CH   ADC_CHANNEL_9     // GPIO10
#define MSGEQ7_ADC_UNIT     ADC_UNIT_1

// NTC 热敏电阻参数 (与原 yaml 中 ntc/resistance 传感器一致)
#define NTC_B_CONSTANT      3950
#define NTC_REF_TEMP_K      298.15f           // 25°C in Kelvin
#define NTC_REF_RESISTANCE  9400.0f           // 9.4kΩ @ 25°C
#define NTC_SERIES_RESISTOR 10000.0f          // 10kΩ 分压电阻
#define NTC_REF_VOLTAGE     3.3f              // 分压供电电压

#define MSGEQ7_NUM_BANDS    7

// 温度无效标记 (初始/异常回退值, 合理性下界 -100°C, 用 -900 做阈值留足余量)
static constexpr float NTC_INVALID = -999.0f;

namespace msgeq7 {

// ── 频谱帧 (原子快照, 外部唯一读取接口) ──
struct SpectrumFrame {
  int combined[MSGEQ7_NUM_BANDS];   // L+R 平均值 (0-255)
  int left[MSGEQ7_NUM_BANDS];       // 左声道 (0-255)
  int right[MSGEQ7_NUM_BANDS];      // 右声道 (0-255)
  int peak[MSGEQ7_NUM_BANDS];       // Combined 峰值保持 (0-255, 兼容旧代码)
  int peak_l[MSGEQ7_NUM_BANDS];     // 左声道峰值保持 (0-255)
  int peak_r[MSGEQ7_NUM_BANDS];     // 右声道峰值保持 (0-255)
  float temperature;                // NTC 温度 (C), NTC_INVALID = 无效
};

// ═══════════════════════════════════════════════════════════════
//  内部状态 (外部禁止直接访问, 通过 get_frame() / get_temperature() 读取)
//  使用 inline 变量 (C++17) 替代 static, 确保 ODR 安全
// ═══════════════════════════════════════════════════════════════
inline int _r_bands[MSGEQ7_NUM_BANDS] = {0};
inline int _l_bands[MSGEQ7_NUM_BANDS] = {0};
inline int _combined[MSGEQ7_NUM_BANDS] = {0};
inline int _peak[MSGEQ7_NUM_BANDS] = {0};      // combined peak
inline int _peak_l[MSGEQ7_NUM_BANDS] = {0};    // 左声道 peak
inline int _peak_r[MSGEQ7_NUM_BANDS] = {0};    // 右声道 peak
inline float _ntc_temperature = NTC_INVALID;

inline float _smooth_r[MSGEQ7_NUM_BANDS] = {0};
inline float _smooth_l[MSGEQ7_NUM_BANDS] = {0};

static constexpr float SMOOTH_UP   = 0.35f;   // 上升平滑系数 (快响应)
static constexpr float SMOOTH_DOWN = 0.12f;    // 下降平滑系数 (慢衰减, 视觉更自然)

// ── 峰值衰减 (帧率无关: 用 powf(DECAY, dt) 解耦 FPS) ──
//   设计目标: 1 秒内峰值衰减到 ~50% (原 0.965 @20Hz ≈ 0.965^20 ≈ 0.49)
static constexpr float PEAK_DECAY_PER_SEC = 0.49f;  // 每秒保留比例
inline uint32_t _last_read_ms = 0;

// ── 频段增益补偿 (MSGEQ7 高频自然衰减, HiFi 频谱通用做法) ──
// 整数定点: gain = ×N/256, 避免每频段浮点乘法 (7×2=14次 float mul → 14次 int mul+shift)
//   0.9  → 230   (230/256 = 0.898, 误差 0.2%)
//   1.0  → 256
//   1.1  → 282   (282/256 = 1.102)
//   1.25 → 320   (320/256 = 1.250)
//   1.45 → 371   (371/256 = 1.449)
//   1.7  → 435   (435/256 = 1.699)
//   2.0  → 512   (512/256 = 2.000)
static constexpr int BAND_GAIN_Q8[MSGEQ7_NUM_BANDS] = {
  230, 256, 282, 320, 371, 435, 512
};

// ── 噪声门限 (0-255 量纲, 低于此值的频谱归零, 静音时频谱静止) ──
//   MSGEQ7 3.3V 供电时底噪约 100-200mV, 对应 ADC ≈ 124-248 / 4095 → 8-15 / 255
//   旧值 3 太低会导致静音时频谱跳动, 提升到 8 消除底噪
static constexpr int NOISE_GATE = 8;

// ── ADC oneshot 句柄 (ESP-IDF 5.x 新 API) ──
inline adc_oneshot_unit_handle_t _adc_handle = nullptr;

// ── NTC ADC 校准句柄 (6dB 衰减, 线性远优于 12dB) ──
inline adc_cali_handle_t _adc_cali_ntc = nullptr;

// ── 多核线程安全 (ESP32-S3 双核: display / sensor / animation 可能并发读取) ──
inline portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

inline bool _initialized = false;
inline bool _adc_ok = false;  // ADC 初始化成功标志 (防止 _adc_handle==nullptr 时崩溃)

// ── 零漂校准 (setup 时记录静默偏置, read 时减去) ──
//   每片 MSGEQ7 的偏置电压不同, 静音时各频段高度不一
//   在 setup() 的 3 帧丢弃周期中采样平均值作为 _offset, 后续减去
inline int _r_offset[MSGEQ7_NUM_BANDS] = {0};
inline int _l_offset[MSGEQ7_NUM_BANDS] = {0};

// ── NTC 温度 IIR 低通 (防末位闪烁) ──
//   中值滤波后仍有 ±0.3°C 抖动, IIR 平滑后显示更稳定
static constexpr float NTC_IIR_ALPHA = 0.15f;  // 越小越平滑, 0.15 ≈ 7s 时间常数 @10s间隔
inline float _ntc_smooth = NTC_INVALID;

// ═══════════════════════════════════════════════════════════════
//  ADC 内部函数
// ═══════════════════════════════════════════════════════════════
static void _adc_init() {
  if (_adc_handle != nullptr) return;

  adc_oneshot_unit_init_cfg_t unit_cfg = {};
  unit_cfg.unit_id = MSGEQ7_ADC_UNIT;
  unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;
  esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &_adc_handle);
  if (ret != ESP_OK) {
    ESP_LOGE("msgeq7", "ADC unit init failed: %s", esp_err_to_name(ret));
    _adc_ok = false;
    return;
  }

  // 频谱 ADC: 12dB 衰减 (MSGEQ7 输出 0~Vcc, 需要宽量程)
  adc_oneshot_chan_cfg_t spec_cfg = {};
  spec_cfg.atten = ADC_ATTEN_DB_12;   // 0~3.6V
  spec_cfg.bitwidth = ADC_BITWIDTH_12;
  ret = adc_oneshot_config_channel(_adc_handle, MSGEQ7_R_ADC_CH, &spec_cfg);
  if (ret == ESP_OK) ret = adc_oneshot_config_channel(_adc_handle, MSGEQ7_L_ADC_CH, &spec_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE("msgeq7", "ADC spectrum channel config failed: %s", esp_err_to_name(ret));
    _adc_ok = false;
    return;
  }

  // NTC ADC: 6dB 衰减 (分压≤2.5V, 6dB 区间线性远优于 12dB, ±100mV→±20mV)
  //   注: 0°C 时 NTC≈31kΩ → V≈2.5V, 低于0°C 可能削顶, 室内前级可接受
  adc_oneshot_chan_cfg_t ntc_cfg = {};
  ntc_cfg.atten = ADC_ATTEN_DB_6;     // 0~2.5V, 线性好
  ntc_cfg.bitwidth = ADC_BITWIDTH_12;
  ret = adc_oneshot_config_channel(_adc_handle, MSGEQ7_NTC_ADC_CH, &ntc_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE("msgeq7", "ADC NTC channel config failed: %s", esp_err_to_name(ret));
    _adc_ok = false;
    return;
  }

  // NTC ADC 校准 (ESP32-S3 使用 curve_fitting, line_fitting 仅支持老款 ESP32)
  adc_cali_curve_fitting_config_t cali_cfg = {};
  cali_cfg.unit_id = MSGEQ7_ADC_UNIT;
  cali_cfg.chan = MSGEQ7_NTC_ADC_CH;
  cali_cfg.atten = ADC_ATTEN_DB_6;
  cali_cfg.bitwidth = ADC_BITWIDTH_12;
  esp_err_t cali_ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &_adc_cali_ntc);
  if (cali_ret == ESP_OK) {
    ESP_LOGI("msgeq7", "NTC ADC 校准初始化成功 (6dB + curve_fitting)");
  } else {
    ESP_LOGW("msgeq7", "NTC ADC 校准初始化失败 (0x%x), 回退线性映射", cali_ret);
    _adc_cali_ntc = nullptr;
  }

  _adc_ok = true;
}

static int _adc_read(int channel) {
  if (!_adc_ok) return -1;
  int raw = 0;
  esp_err_t ret = adc_oneshot_read(_adc_handle, (adc_channel_t)channel, &raw);
  if (ret != ESP_OK) return -1;
  return raw;
}

// NTC 专用: 读取校准后电压 (mV), 失败返回 -1
// 优先使用 adc_cali 校准, 回退 6dB 线性映射 0~2500mV
static int _adc_read_ntc_voltage_mv() {
  if (!_adc_ok) return -1;
  int raw = 0;
  esp_err_t ret = adc_oneshot_read(_adc_handle, MSGEQ7_NTC_ADC_CH, &raw);
  if (ret != ESP_OK) return -1;
  if (_adc_cali_ntc != nullptr) {
    int voltage_mv = 0;
    esp_err_t cali_ret = adc_cali_raw_to_voltage(_adc_cali_ntc, raw, &voltage_mv);
    if (cali_ret == ESP_OK) return voltage_mv;
  }
  // 校准不可用时回退: 6dB 衰减线性映射 0~2500mV
  return (int)((float)raw * 2500.0f / 4095.0f);
}

// ═══════════════════════════════════════════════════════════════
//  初始化
// ═══════════════════════════════════════════════════════════════
static void setup() {
  // ── GPIO: RESET + STROBE ──
  gpio_config_t io_conf = {};
  io_conf.pin_bit_mask = (1ULL << MSGEQ7_RESET_PIN) | (1ULL << MSGEQ7_STROBE_PIN);
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&io_conf);

  // RESET=LOW, STROBE=HIGH (空闲态; read() 会先 RESET 再 STROBE, 时序合规)
  gpio_set_level((gpio_num_t)MSGEQ7_RESET_PIN, 0);
  gpio_set_level((gpio_num_t)MSGEQ7_STROBE_PIN, 1);

  // ── ADC1 初始化 (ESP-IDF 5.x oneshot API) ──
  _adc_init();

  // ── MSGEQ7 模拟部分上电稳定等待 ──
  //   内部 bias / switched-cap filter / reference 需要稳定时间
  //   ESP32 启动快, MSGEQ7 可能还未就绪, 延迟 10ms 等待模拟电路建立
  delay(10);

  // ── 丢弃前3帧 + 零漂校准 ──
  //   首次读取数据不可靠 (多路复用器/滤波器未完全稳定)
  //   同时记录静默偏置 (假设启动时无音频输入), 用于后续零漂补偿
  if (_adc_ok) {
    int r_sum[MSGEQ7_NUM_BANDS] = {0};
    int l_sum[MSGEQ7_NUM_BANDS] = {0};
    for (int i = 0; i < 3; i++) {
      gpio_set_level((gpio_num_t)MSGEQ7_RESET_PIN, 1);
      esp_rom_delay_us(100);
      gpio_set_level((gpio_num_t)MSGEQ7_RESET_PIN, 0);
      esp_rom_delay_us(100);
      for (int j = 0; j < MSGEQ7_NUM_BANDS; j++) {
        gpio_set_level((gpio_num_t)MSGEQ7_STROBE_PIN, 0);
        esp_rom_delay_us(40);   // 3.3V 供电: 输出建立时间需 36-40μs
        int r_raw = _adc_read(MSGEQ7_R_ADC_CH);
        int l_raw = _adc_read(MSGEQ7_L_ADC_CH);
        if (r_raw >= 0) r_sum[j] += r_raw;
        if (l_raw >= 0) l_sum[j] += l_raw;
        gpio_set_level((gpio_num_t)MSGEQ7_STROBE_PIN, 1);
        esp_rom_delay_us(40);
      }
    }
    // 3帧平均 -> 零漂偏置, 阈值50: 正常静默偏置约 8-40, >50 可能有音频信号, 舍弃校准
    for (int j = 0; j < MSGEQ7_NUM_BANDS; j++) {
      int avg_r = r_sum[j] / 3;
      int avg_l = l_sum[j] / 3;
      _r_offset[j] = (avg_r < 50) ? avg_r : 0;
      _l_offset[j] = (avg_l < 50) ? avg_l : 0;
    }
    ESP_LOGI("msgeq7", "零漂校准: R_offset[%d~%d]=%d/%d/%d/%d/%d/%d/%d",
             0, 6, _r_offset[0], _r_offset[1], _r_offset[2], _r_offset[3],
             _r_offset[4], _r_offset[5], _r_offset[6]);
  }

  _initialized = true;
  ESP_LOGI("msgeq7", "MSGEQ7 初始化完成 (RST=IO1, STB=IO39, R=IO3, L=IO9, NTC=IO10, ADC=%s)",
           _adc_ok ? "OK" : "FAILED");
}

// ═══════════════════════════════════════════════════════════════
//  手动触发零漂重校准
// ═══════════════════════════════════════════════════════════════
// 调用前提: _initialized==true && _adc_ok==true（硬件已初始化）
// 如果 setup() 后又被调用（如用户先开音响再开前机），此函数可修复
// 被污染的 offset 值。音频中断期间调用效果最佳。
// 返回值: 校准成功返回 true，硬件未就绪返回 false
static bool recalibrate() {
  if (!_initialized || !_adc_ok) {
    ESP_LOGW("msgeq7", "recalibrate() 跳过: 硬件未就绪 (init=%d, adc=%d)", _initialized, _adc_ok);
    return false;
  }

  int r_sum[MSGEQ7_NUM_BANDS] = {0};
  int l_sum[MSGEQ7_NUM_BANDS] = {0};
  for (int i = 0; i < 3; i++) {
    gpio_set_level((gpio_num_t)MSGEQ7_RESET_PIN, 1);
    esp_rom_delay_us(100);
    gpio_set_level((gpio_num_t)MSGEQ7_RESET_PIN, 0);
    esp_rom_delay_us(100);
    for (int j = 0; j < MSGEQ7_NUM_BANDS; j++) {
      gpio_set_level((gpio_num_t)MSGEQ7_STROBE_PIN, 0);
      esp_rom_delay_us(40);
      int r_raw = _adc_read(MSGEQ7_R_ADC_CH);
      int l_raw = _adc_read(MSGEQ7_L_ADC_CH);
      if (r_raw >= 0) r_sum[j] += r_raw;
      if (l_raw >= 0) l_sum[j] += l_raw;
      gpio_set_level((gpio_num_t)MSGEQ7_STROBE_PIN, 1);
      esp_rom_delay_us(40);
    }
  }
  for (int j = 0; j < MSGEQ7_NUM_BANDS; j++) {
    int avg_r = r_sum[j] / 3;
    int avg_l = l_sum[j] / 3;
    _r_offset[j] = (avg_r < 50) ? avg_r : 0;
    _l_offset[j] = (avg_l < 50) ? avg_l : 0;
  }
  ESP_LOGI("msgeq7", "重校准: R_offset[0~6]=%d/%d/%d/%d/%d/%d/%d",
           _r_offset[0], _r_offset[1], _r_offset[2], _r_offset[3],
           _r_offset[4], _r_offset[5], _r_offset[6]);
  return true;
}

// ═══════════════════════════════════════════════════════════════
//  读取 MSGEQ7 七段频谱
// ═══════════════════════════════════════════════════════════════
// 调用间隔建议 40~60ms (约 17~25Hz)
// 线程安全: 全部计算在局部变量完成, 仅最终拷贝加锁
static void read() {
  if (!_initialized) setup();
  if (!_adc_ok) return;

  // 帧率无关: 计算本帧实际时间间隔
  uint32_t now = millis();
  float dt = _last_read_ms ? (float)(now - _last_read_ms) / 1000.0f : 0.05f;
  if (dt > 0.5f) dt = 0.05f;   // 首次/长时间暂停后, 默认 50ms
  _last_read_ms = now;
  float decay_factor = powf(PEAK_DECAY_PER_SEC, dt);

  // 快照上一次平滑值 (_smooth_* 仅本函数写入, 无需加锁)
  float local_sr[MSGEQ7_NUM_BANDS], local_sl[MSGEQ7_NUM_BANDS];
  int local_peak[MSGEQ7_NUM_BANDS];
  int local_peak_l[MSGEQ7_NUM_BANDS], local_peak_r[MSGEQ7_NUM_BANDS];
  memcpy(local_sr, _smooth_r, sizeof(_smooth_r));
  memcpy(local_sl, _smooth_l, sizeof(_smooth_l));
  memcpy(local_peak, _peak, sizeof(_peak));
  memcpy(local_peak_l, _peak_l, sizeof(_peak_l));
  memcpy(local_peak_r, _peak_r, sizeof(_peak_r));

  // 局部结果缓冲
  int new_r[MSGEQ7_NUM_BANDS], new_l[MSGEQ7_NUM_BANDS];
  int new_combined[MSGEQ7_NUM_BANDS], new_peak[MSGEQ7_NUM_BANDS];
  int new_peak_l[MSGEQ7_NUM_BANDS], new_peak_r[MSGEQ7_NUM_BANDS];
  float new_sr[MSGEQ7_NUM_BANDS], new_sl[MSGEQ7_NUM_BANDS];

  // 复位多路复用器
  gpio_set_level((gpio_num_t)MSGEQ7_RESET_PIN, 1);
  esp_rom_delay_us(100);   // RESET 高脉冲 ≥100ns, 实给 100μs
  gpio_set_level((gpio_num_t)MSGEQ7_RESET_PIN, 0);
  esp_rom_delay_us(100);   // RESET→首个STROBE: 最小 72μs, 实给 100μs

  for (int i = 0; i < MSGEQ7_NUM_BANDS; i++) {
    gpio_set_level((gpio_num_t)MSGEQ7_STROBE_PIN, 0);
    esp_rom_delay_us(40);   // STROBE 低 + 输出建立: 3.3V 供电建议 ≥36μs, 实给 40μs
                             // (5V 下 18μs 够, 但 3.3V 模拟摆幅小, 建立更慢)

    int r_raw = _adc_read(MSGEQ7_R_ADC_CH);
    int l_raw = _adc_read(MSGEQ7_L_ADC_CH);

    // ADC 读取失败时跳过本频段, 保留上次平滑值 (避免归零闪烁)
    if (r_raw < 0 || l_raw < 0) {
      gpio_set_level((gpio_num_t)MSGEQ7_STROBE_PIN, 1);
      esp_rom_delay_us(40);
      new_r[i] = (int)local_sr[i];
      new_l[i] = (int)local_sl[i];
      new_sr[i] = local_sr[i];
      new_sl[i] = local_sl[i];
      new_combined[i] = (new_r[i] + new_l[i]) / 2;
      new_peak[i] = local_peak[i];
      new_peak_l[i] = local_peak_l[i];
      new_peak_r[i] = local_peak_r[i];
      continue;
    }

    // 限幅
    if (r_raw > 4095) r_raw = 4095;
    if (l_raw > 4095) l_raw = 4095;

    // 零漂补偿: 减去 setup() 时采集的静默偏置
    //   每片 MSGEQ7 偏置不同, 减去后静音时各频段归零更干净
    r_raw -= _r_offset[i];
    l_raw -= _l_offset[i];
    if (r_raw < 0) r_raw = 0;
    if (l_raw < 0) l_raw = 0;

    // 缩放到 0-255
    int r_val = r_raw * 255 / 4095;
    int l_val = l_raw * 255 / 4095;

    // 噪声门限 (先于增益, 防止增益放大噪声)
    if (r_val < NOISE_GATE) r_val = 0;
    if (l_val < NOISE_GATE) l_val = 0;

    // 频段增益补偿 (整数定点: ×N/256, 比 float mul 快 ~5x on ESP32-S3 FPU)
    r_val = (r_val * BAND_GAIN_Q8[i]) >> 8;
    l_val = (l_val * BAND_GAIN_Q8[i]) >> 8;
    if (r_val > 255) r_val = 255;
    if (l_val > 255) l_val = 255;

    // 双速率平滑: 上升快, 下降慢 (视觉更流畅)
    float sr = local_sr[i], sl = local_sl[i];
    new_sr[i] = (r_val > sr) ? sr + (r_val - sr) * SMOOTH_UP
                              : sr + (r_val - sr) * SMOOTH_DOWN;
    new_sl[i] = (l_val > sl) ? sl + (l_val - sl) * SMOOTH_UP
                              : sl + (l_val - sl) * SMOOTH_DOWN;

    // clamp: 防止浮点累积误差溢出 0-255 范围
    new_sr[i] = std::clamp(new_sr[i], 0.0f, 255.0f);
    new_sl[i] = std::clamp(new_sl[i], 0.0f, 255.0f);

    new_r[i] = (int)new_sr[i];
    new_l[i] = (int)new_sl[i];
    new_combined[i] = (new_r[i] + new_l[i]) / 2;

    // 峰值保持 + 帧率无关衰减 (每声道独立)
    // Combined peak 取两声道较大值 (兼容旧代码)
    int new_max = new_combined[i];
    if (new_l[i] > local_peak_l[i]) {
      new_peak_l[i] = new_l[i];
    } else {
      new_peak_l[i] = (int)((float)local_peak_l[i] * decay_factor);
      if (new_peak_l[i] < 1) new_peak_l[i] = 0;
    }
    if (new_r[i] > local_peak_r[i]) {
      new_peak_r[i] = new_r[i];
    } else {
      new_peak_r[i] = (int)((float)local_peak_r[i] * decay_factor);
      if (new_peak_r[i] < 1) new_peak_r[i] = 0;
    }
    if (new_max > local_peak[i]) {
      new_peak[i] = new_max;
    } else {
      new_peak[i] = (int)((float)local_peak[i] * decay_factor);
      if (new_peak[i] < 1) new_peak[i] = 0;
    }

    gpio_set_level((gpio_num_t)MSGEQ7_STROBE_PIN, 1);
    esp_rom_delay_us(40);   // STROBE 高电平 ≥18μs, 3.3V 供电余量给到 40μs
  }

  // ═══ 临界区: 更新共享数组 ═══
  //  ⚠️ 绝对禁止在 CRITICAL 块内加入任何 delay、printf、SPI/I2C 操作或
  //     函数调用。portENTER_CRITICAL 会关闭当前 CPU 核的全部中断，
  //     任何阻塞操作都将导致 ESP32 核心恐慌 (Core Panic) 和系统崩溃。
  //     临界区只做纯内存操作 (memcpy/赋值)，计算在外面提前完成。
  //══════════════════════════════════════════════
  // (防止 display/sensor 任务读到半更新数据)
  portENTER_CRITICAL(&_mux);
  memcpy(_r_bands, new_r, sizeof(_r_bands));
  memcpy(_l_bands, new_l, sizeof(_l_bands));
  memcpy(_combined, new_combined, sizeof(_combined));
  memcpy(_peak, new_peak, sizeof(_peak));
  memcpy(_peak_l, new_peak_l, sizeof(_peak_l));
  memcpy(_peak_r, new_peak_r, sizeof(_peak_r));
  portEXIT_CRITICAL(&_mux);

  // _smooth_* 仅本函数读写, 放在锁外减少临界区时长
  memcpy(_smooth_r, new_sr, sizeof(_smooth_r));
  memcpy(_smooth_l, new_sl, sizeof(_smooth_l));
}

// 温度有效判定 (前向声明)
static bool is_temperature_valid(float t) { return t > -50.0f && t < 150.0f; }

// ═══════════════════════════════════════════════════════════════
//  读取 NTC 温度
// ═══════════════════════════════════════════════════════════════
// 替代 ESPHome 的 adc→resistance→ntc 传感器链
// 使用 B 参数方程: 1/T = 1/T0 + (1/B) × ln(R/R0)
// NTC 独立 6dB 衰减 + adc_cali 校准, 精度约 ±1°C (vs 12dB 线性映射 ±3~5°C)
static void read_ntc() {
  if (!_initialized || !_adc_ok) return;

  // 中值滤波: 连续读3次取中值 (替代 ESPHome 的 median filter)
  int samples[3];
  samples[0] = _adc_read_ntc_voltage_mv();  // 校准后 mV
  samples[1] = _adc_read_ntc_voltage_mv();
  samples[2] = _adc_read_ntc_voltage_mv();

  // 任一采样失败则跳过本次更新
  if (samples[0] < 0 || samples[1] < 0 || samples[2] < 0) return;

  // 简单排序取中值
  if (samples[0] > samples[1]) { int t = samples[0]; samples[0] = samples[1]; samples[1] = t; }
  if (samples[1] > samples[2]) { int t = samples[1]; samples[1] = samples[2]; samples[2] = t; }
  if (samples[0] > samples[1]) { int t = samples[0]; samples[0] = samples[1]; samples[1] = t; }
  int voltage_mv = samples[1];

  // mV → V
  float voltage = (float)voltage_mv / 1000.0f;

  // 电压 → NTC 电阻
  // 电路: 3.3V → 10kΩ(上拉) → ADC → NTC → GND  (DOWNSTREAM)
  // R_ntc = R_series × V_adc / (V_ref - V_adc)
  if (voltage >= (NTC_REF_VOLTAGE - 0.01f) || voltage < 0.01f) {
    portENTER_CRITICAL(&_mux);
    _ntc_temperature = NTC_INVALID;
    portEXIT_CRITICAL(&_mux);
    return;
  }

  float resistance = NTC_SERIES_RESISTOR * voltage / (NTC_REF_VOLTAGE - voltage);

  // B 参数方程
  float t_inv = 1.0f / NTC_REF_TEMP_K + (1.0f / (float)NTC_B_CONSTANT) * logf(resistance / NTC_REF_RESISTANCE);
  float temp = 1.0f / t_inv - 273.15f;

  // IIR 一阶低通 (防温度末位闪烁: 中值滤波后仍有 ±0.3°C 抖动)
  //   首次采样直接采纳, 后续 α=0.15 平滑 (约 7s 时间常数 @10s 间隔)
  //   在临界区内读写 _ntc_smooth 和 _ntc_temperature, 防止与 get_temperature() 竞争
  portENTER_CRITICAL(&_mux);
  if (!is_temperature_valid(_ntc_smooth)) {
    _ntc_smooth = temp;
  } else {
    _ntc_smooth = _ntc_smooth + NTC_IIR_ALPHA * (temp - _ntc_smooth);
  }
  _ntc_temperature = _ntc_smooth;
  portEXIT_CRITICAL(&_mux);
}

// ═══════════════════════════════════════════════════════════════
//  公开读取接口 (线程安全, 外部唯一访问入口)
// ═══════════════════════════════════════════════════════════════

/// ═══ 临界区: 读端快照 ═══
//  ⚠️ 同写端规则：禁止加入 delay/printf/SPI/I2C/函数调用。
//     临界区只做纯内存操作，持锁时间必须极短。
//═════════════════════════════════
// 获取频谱帧快照 (含 combined, peak, temperature), 读端加锁
static bool get_frame(SpectrumFrame* out) {
  if (out == nullptr) return false;
  portENTER_CRITICAL(&_mux);
  memcpy(out->combined, _combined, sizeof(_combined));
  memcpy(out->left, _l_bands, sizeof(_l_bands));
  memcpy(out->right, _r_bands, sizeof(_r_bands));
  memcpy(out->peak, _peak, sizeof(_peak));
  memcpy(out->peak_l, _peak_l, sizeof(_peak_l));
  memcpy(out->peak_r, _peak_r, sizeof(_peak_r));
  out->temperature = _ntc_temperature;
  portEXIT_CRITICAL(&_mux);
  return true;
}

// 获取单个 combined 值 (便捷接口, 用于 LVGL widget update)
static int get_combined(int band) {
  if (band < 0 || band >= MSGEQ7_NUM_BANDS) return 0;
  int val = 0;
  portENTER_CRITICAL(&_mux);
  val = _combined[band];
  portEXIT_CRITICAL(&_mux);
  return val;
}

// 获取单个 peak 值 (combined)
static int get_peak(int band) {
  if (band < 0 || band >= MSGEQ7_NUM_BANDS) return 0;
  int val = 0;
  portENTER_CRITICAL(&_mux);
  val = _peak[band];
  portEXIT_CRITICAL(&_mux);
  return val;
}

// 获取左声道峰值
static int get_peak_left(int band) {
  if (band < 0 || band >= MSGEQ7_NUM_BANDS) return 0;
  int val = 0;
  portENTER_CRITICAL(&_mux);
  val = _peak_l[band];
  portEXIT_CRITICAL(&_mux);
  return val;
}

// 获取右声道峰值
static int get_peak_right(int band) {
  if (band < 0 || band >= MSGEQ7_NUM_BANDS) return 0;
  int val = 0;
  portENTER_CRITICAL(&_mux);
  val = _peak_r[band];
  portEXIT_CRITICAL(&_mux);
  return val;
}

// 获取温度 (线程安全)
static float get_temperature() {
  float val = 0;
  portENTER_CRITICAL(&_mux);
  val = _ntc_temperature;
  portEXIT_CRITICAL(&_mux);
  return val;
}


// ADC 是否可用
static bool is_ready() { return _initialized && _adc_ok; }

}  // namespace msgeq7
