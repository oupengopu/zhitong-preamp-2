#pragma once

#ifdef UNIT_TEST
#include "esphome_mock.h"
#else
#include "esphome.h"
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_rom_sys.h>
#endif

// PGA2311 SPI 接线 (智能前级2.0 / ESP32-S3)
//   CS   → IO42  (手动控制)
//   CLK  → IO41  (SPI时钟)
//   SDI  → IO2   (MOSI，数据输入)
//   SDO  → 不接
//
// 协议：CS拉低 → 发16bit（高8位=右声道，低8位=左声道）→ CS拉高
// 数值：0=静音, 1=−95.5dB, 192≈0dB, 255=+31.5dB（每步0.5dB）

#define PGA2311_CS_PIN   42
#define PGA2311_CLK_PIN  41
#define PGA2311_MOSI_PIN 2

namespace pga2311 {

inline bool _initialized = false;
inline spi_device_handle_t _spi_dev = nullptr;

inline int _last_r = -1;
inline int _last_l = -1;

static int _clamp(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static float _to_db(int v) {
  if (v == 0) return -999.0f;
  return (v - 192) * 0.5f;
}

static void setup() {
  gpio_config_t cs_conf = {};
  cs_conf.pin_bit_mask = (1ULL << PGA2311_CS_PIN);
  cs_conf.mode = GPIO_MODE_OUTPUT;
  cs_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  cs_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cs_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cs_conf);
  gpio_set_level((gpio_num_t)PGA2311_CS_PIN, 1);

  spi_bus_config_t bus_conf = {};
  bus_conf.mosi_io_num = PGA2311_MOSI_PIN;
  bus_conf.miso_io_num = -1;
  bus_conf.sclk_io_num = PGA2311_CLK_PIN;
  bus_conf.quadwp_io_num = -1;
  bus_conf.quadhd_io_num = -1;
  bus_conf.max_transfer_sz = 32;

  esp_err_t ret = spi_bus_initialize(SPI3_HOST, &bus_conf, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    ESP_LOGE("pga2311", "SPI bus init failed: %s", esp_err_to_name(ret));
    return;
  }

  spi_device_interface_config_t dev_conf = {};
  dev_conf.mode = 0;
  dev_conf.clock_speed_hz = 4000000;   // 4MHz (PGA2311 Datasheet fSCLK 最高 6.25MHz, 安全留余量)
  dev_conf.spics_io_num = -1;
  dev_conf.queue_size = 1;

  ret = spi_bus_add_device(SPI3_HOST, &dev_conf, &_spi_dev);
if (ret != ESP_OK) {
  ESP_LOGE("pga2311", "SPI device add failed: %s", esp_err_to_name(ret));
  spi_bus_free(SPI3_HOST);
  _spi_dev = nullptr;
  return;
}

  _initialized = true;
  ESP_LOGI("pga2311", "PGA2311 初始化完成 (CS=IO42, CLK=IO41, SDI=IO2) [ESP-IDF SPI3_HOST]");
}

static void set_volume(int right_vol, int left_vol) {
  if (!_initialized) {
    ESP_LOGW("pga2311", "set_volume called before setup(), initializing now");
    setup();
    if (!_initialized) return;  // setup() 失败时防止空指针崩溃
  }

  right_vol = _clamp(right_vol, 0, 255);
  left_vol  = _clamp(left_vol,  0, 255);

  if (right_vol == _last_r && left_vol == _last_l) {
    return;
  }

  spi_transaction_t trans = {};
  trans.flags = SPI_TRANS_USE_TXDATA;
  trans.length = 16;
  trans.tx_data[0] = (uint8_t)right_vol;
  trans.tx_data[1] = (uint8_t)left_vol;

  gpio_set_level((gpio_num_t)PGA2311_CS_PIN, 0);
  esp_rom_delay_us(1);

  esp_err_t ret = spi_device_polling_transmit(_spi_dev, &trans);
  if (ret != ESP_OK) {
    ESP_LOGW("pga2311", "SPI transmit failed: %s", esp_err_to_name(ret));
    gpio_set_level((gpio_num_t)PGA2311_CS_PIN, 1);
    return;
  }
  _last_r = right_vol;
  _last_l = left_vol;

  esp_rom_delay_us(1);
  gpio_set_level((gpio_num_t)PGA2311_CS_PIN, 1);
  esp_rom_delay_us(1);

  ESP_LOGD("pga2311", "R=%d(%.1fdB)  L=%d(%.1fdB)",
           right_vol, _to_db(right_vol),
           left_vol,  _to_db(left_vol));
}

static void reset_for_test() {
  _initialized = false;
  _last_r = -1;
  _last_l = -1;
}

static void get_last_values(int& r, int& l) {
  r = _last_r;
  l = _last_l;
}

static bool is_initialized() { return _initialized; }
static void set_initialized(bool val) { _initialized = val; }
static int clamp(int v, int lo, int hi) { return _clamp(v, lo, hi); }
static float to_db(int v) { return _to_db(v); }

}  // namespace pga2311
