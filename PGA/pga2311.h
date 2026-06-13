#pragma once

#ifdef UNIT_TEST
#include "esphome_mock.h"
#else
#include "esphome.h"
#include <driver/gpio.h>
#include <esp_rom_sys.h>
#endif

// PGA2311 接线 (智能前级2.0 / ESP32-S3)
//   CS   → IO42
//   CLK  → IO41
//   SDI  → IO2
//
// 协议：CS拉低 → 发16bit（高8位=右声道，低8位=左声道）→ CS拉高
// 数值：0=静音, 1=−95.5dB, 192≈0dB, 255=+31.5dB（每步0.5dB）
//
// 使用 GPIO 位操作 (bit-bang) 替代硬件 SPI:
//   ESP32-S3 八线 PSRAM 启用后 (FETCH_INSTRUCTIONS+RODATA) SPI2_HOST 被占用，
//   SPI3_HOST 留给 TFT 显示 (mipi_spi)。
//   PGA2311 每次只传 16bit 且频率只需 2-4MHz，位操作完全足够。

#define PGA2311_CS_PIN   42
#define PGA2311_CLK_PIN  41
#define PGA2311_MOSI_PIN 2

namespace pga2311 {

inline bool _initialized = false;

inline int _last_r = -1;
inline int _last_l = -1;

static int _clamp(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static float _to_db(int v) {
  if (v == 0) return -999.0f;
  return (v - 192) * 0.5f;
}

/// 软件 SPI 发送 16-bit (MSB first, mode 0: CPOL=0 CPHA=0)
static void _send16(uint16_t data) {
  for (int i = 15; i >= 0; i--) {
    gpio_set_level((gpio_num_t)PGA2311_MOSI_PIN, (data >> i) & 1);
    gpio_set_level((gpio_num_t)PGA2311_CLK_PIN, 1);
    esp_rom_delay_us(1);
    gpio_set_level((gpio_num_t)PGA2311_CLK_PIN, 0);
    esp_rom_delay_us(1);
  }
}

static void setup() {
  uint64_t pin_mask = (1ULL << PGA2311_CS_PIN)
                    | (1ULL << PGA2311_CLK_PIN)
                    | (1ULL << PGA2311_MOSI_PIN);

  gpio_config_t io_conf = {};
  io_conf.pin_bit_mask = pin_mask;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&io_conf);

  gpio_set_level((gpio_num_t)PGA2311_CS_PIN, 1);
  gpio_set_level((gpio_num_t)PGA2311_CLK_PIN, 0);
  gpio_set_level((gpio_num_t)PGA2311_MOSI_PIN, 0);

  _initialized = true;
  ESP_LOGI("pga2311", "PGA2311 init done (CS=42, CLK=41, MOSI=2) [bitbang]");
}

static void set_volume(int right_vol, int left_vol) {
  if (!_initialized) {
    ESP_LOGW("pga2311", "set_volume before setup, init now");
    setup();
    if (!_initialized) return;
  }

  right_vol = _clamp(right_vol, 0, 255);
  left_vol  = _clamp(left_vol,  0, 255);

  if (right_vol == _last_r && left_vol == _last_l) return;

  uint16_t frame = ((uint16_t)right_vol << 8) | (uint16_t)left_vol;

  gpio_set_level((gpio_num_t)PGA2311_CS_PIN, 0);
  esp_rom_delay_us(1);

  _send16(frame);

  esp_rom_delay_us(1);
  gpio_set_level((gpio_num_t)PGA2311_CS_PIN, 1);
  esp_rom_delay_us(1);

  _last_r = right_vol;
  _last_l = left_vol;

  ESP_LOGD("pga2311", "R=%d(%.1fdB) L=%d(%.1fdB)",
           right_vol, _to_db(right_vol),
           left_vol,  _to_db(left_vol));
}

static void reset_for_test() { _initialized = false; _last_r = -1; _last_l = -1; }
static void get_last_values(int& r, int& l) { r = _last_r; l = _last_l; }
static bool is_initialized() { return _initialized; }
static void set_initialized(bool val) { _initialized = val; }
static int clamp(int v, int lo, int hi) { return _clamp(v, lo, hi); }
static float to_db(int v) { return _to_db(v); }

}  // namespace pga2311
