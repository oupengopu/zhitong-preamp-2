#pragma once

#ifndef UNIT_TEST
#include "esphome.h"
#include <driver/gpio.h>
#include <esp_rom_sys.h>
#include <initializer_list>

namespace lcd_probe {

static constexpr gpio_num_t PIN_CS = GPIO_NUM_13;
static constexpr gpio_num_t PIN_DC = GPIO_NUM_14;
static constexpr gpio_num_t PIN_RST = GPIO_NUM_15;
static constexpr gpio_num_t PIN_BLK = GPIO_NUM_7;
static constexpr gpio_num_t PIN_CLK = GPIO_NUM_5;
static constexpr gpio_num_t PIN_MOSI = GPIO_NUM_4;
// Vendor reference code writes the NV3007 GRAM in native portrait order:
// 142 columns x 428 lines, with the active area starting at X+12.
static constexpr uint16_t LCD_W = 142;
static constexpr uint16_t LCD_H = 428;
static constexpr uint16_t LCD_X_OFFSET = 12;
static bool g_three_wire_mode = false;

static void setup_pins() {
  gpio_config_t conf = {};
  conf.pin_bit_mask = (1ULL << PIN_CS) | (1ULL << PIN_DC) | (1ULL << PIN_RST) |
                      (1ULL << PIN_BLK) | (1ULL << PIN_CLK) | (1ULL << PIN_MOSI);
  conf.mode = GPIO_MODE_OUTPUT;
  conf.pull_up_en = GPIO_PULLUP_DISABLE;
  conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  conf.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&conf);

  gpio_set_level(PIN_CS, 1);
  gpio_set_level(PIN_DC, 1);
  gpio_set_level(PIN_RST, 1);
  gpio_set_level(PIN_BLK, 1);
  gpio_set_level(PIN_CLK, 0);
  gpio_set_level(PIN_MOSI, 0);
}

static void run_gpio_square_test() {
  ESP_LOGI("LCD_PROBE", "gpio square test start: GPIO5=1kHz GPIO4=500Hz GPIO14=250Hz GPIO13=62Hz RST=HIGH");
  setup_pins();

  // Slow, scope-friendly direct GPIO toggles. This bypasses LVGL and the SPI
  // peripheral, so a missing waveform here points to pin mapping/wiring/measurement.
  for (uint16_t i = 0; i < 4000; i++) {
    gpio_set_level(PIN_CLK, i & 0x01);          // GPIO5, about 1 kHz square wave
    gpio_set_level(PIN_MOSI, (i >> 1) & 0x01);  // GPIO4, about 500 Hz square wave
    bool dc_level = (i >> 2) & 0x01;
    gpio_set_level(PIN_DC, dc_level);           // GPIO14, about 250 Hz square wave
    gpio_set_level(PIN_CS, (i >> 4) & 0x01);    // GPIO13, slow reference toggle
    gpio_set_level(PIN_RST, 1);
    esp_rom_delay_us(500);

    if ((i & 0x7F) == 0) {
      App.feed_wdt();
      delay(0);
    }
  }

  gpio_set_level(PIN_CLK, 0);
  gpio_set_level(PIN_MOSI, 0);
  gpio_set_level(PIN_DC, 1);
  gpio_set_level(PIN_CS, 1);
  gpio_set_level(PIN_RST, 1);
  ESP_LOGI("LCD_PROBE", "gpio square test done");
}

static void wait_with_wdt(uint32_t ms) {
  uint32_t waited = 0;
  while (waited < ms) {
    uint32_t chunk = (ms - waited) > 50 ? 50 : (ms - waited);
    delay(chunk);
    waited += chunk;
    App.feed_wdt();
  }
}

static void run_reset_probe() {
  ESP_LOGI("LCD_PROBE", "reset probe start: GPIO15/RES LOW 3000ms, HIGH 1000ms, LOW 1000ms, HIGH");
  setup_pins();
  gpio_set_level(PIN_CS, 1);
  gpio_set_level(PIN_DC, 1);
  gpio_set_level(PIN_CLK, 0);
  gpio_set_level(PIN_MOSI, 0);

  gpio_set_level(PIN_RST, 0);
  wait_with_wdt(3000);
  gpio_set_level(PIN_RST, 1);
  wait_with_wdt(1000);
  gpio_set_level(PIN_RST, 0);
  wait_with_wdt(1000);
  gpio_set_level(PIN_RST, 1);
  wait_with_wdt(500);
  ESP_LOGI("LCD_PROBE", "reset probe done");
}

static void write_bits(uint16_t value, uint8_t bit_count) {
  for (int bit = bit_count - 1; bit >= 0; bit--) {
    gpio_set_level(PIN_MOSI, (value >> bit) & 0x01);
    esp_rom_delay_us(2);
    gpio_set_level(PIN_CLK, 1);
    esp_rom_delay_us(2);
    gpio_set_level(PIN_CLK, 0);
    esp_rom_delay_us(2);
  }
}

static void write_byte(uint8_t value) {
  write_bits(value, 8);
}

static void write_payload_byte(bool is_data, uint8_t value) {
  if (g_three_wire_mode) {
    // 3-wire/9-bit SPI: first bit is D/C, then the 8-bit command/data byte.
    write_bits((static_cast<uint16_t>(is_data ? 1 : 0) << 8) | value, 9);
    return;
  }

  for (int bit = 7; bit >= 0; bit--) {
    gpio_set_level(PIN_MOSI, (value >> bit) & 0x01);
    esp_rom_delay_us(2);
    gpio_set_level(PIN_CLK, 1);
    esp_rom_delay_us(2);
    gpio_set_level(PIN_CLK, 0);
    esp_rom_delay_us(2);
  }
}

static void write_cmd(uint8_t cmd) {
  gpio_set_level(PIN_DC, 0);
  gpio_set_level(PIN_CS, 0);
  write_payload_byte(false, cmd);
  gpio_set_level(PIN_CS, 1);
  gpio_set_level(PIN_DC, 1);
}

static void write_data(uint8_t data) {
  gpio_set_level(PIN_DC, 1);
  gpio_set_level(PIN_CS, 0);
  write_payload_byte(true, data);
  gpio_set_level(PIN_CS, 1);
}

static void write_data_bytes(const uint8_t *data, size_t len) {
  gpio_set_level(PIN_DC, 1);
  gpio_set_level(PIN_CS, 0);
  for (size_t i = 0; i < len; i++) write_payload_byte(true, data[i]);
  gpio_set_level(PIN_CS, 1);
}

static void write_cmd_data(uint8_t cmd, std::initializer_list<uint8_t> data) {
  write_cmd(cmd);
  if (data.size() == 0) return;
  gpio_set_level(PIN_DC, 1);
  gpio_set_level(PIN_CS, 0);
  for (uint8_t value : data) write_payload_byte(true, value);
  gpio_set_level(PIN_CS, 1);
}

static void hard_reset_panel() {
  ESP_LOGI("LCD_PROBE", "vendor reset pulse");
  setup_pins();
  gpio_set_level(PIN_RST, 1);
  wait_with_wdt(20);
  gpio_set_level(PIN_RST, 0);
  wait_with_wdt(120);
  gpio_set_level(PIN_RST, 1);
  wait_with_wdt(220);
}

static void vendor_nv3007_init() {
  ESP_LOGI("LCD_PROBE", "vendor NV3007 init start");
  write_cmd_data(0xFF, {0xA5});
  write_cmd_data(0x9A, {0x08});
  write_cmd_data(0x9B, {0x08});
  write_cmd_data(0x9C, {0xB0});
  write_cmd_data(0x9D, {0x16});
  write_cmd_data(0x9E, {0xC4});
  write_cmd_data(0x8F, {0x55, 0x04});
  write_cmd_data(0x84, {0x90});
  write_cmd_data(0x83, {0x7B});
  write_cmd_data(0x85, {0x33});
  write_cmd_data(0x60, {0x00});
  write_cmd_data(0x70, {0x00});
  write_cmd_data(0x61, {0x02});
  write_cmd_data(0x71, {0x02});
  write_cmd_data(0x62, {0x04});
  write_cmd_data(0x72, {0x04});
  write_cmd_data(0x6C, {0x29});
  write_cmd_data(0x7C, {0x29});
  write_cmd_data(0x6D, {0x31});
  write_cmd_data(0x7D, {0x31});
  write_cmd_data(0x6E, {0x0F});
  write_cmd_data(0x7E, {0x0F});
  write_cmd_data(0x66, {0x21});
  write_cmd_data(0x76, {0x21});
  write_cmd_data(0x68, {0x3A});
  write_cmd_data(0x78, {0x3A});
  write_cmd_data(0x63, {0x07});
  write_cmd_data(0x73, {0x07});
  write_cmd_data(0x64, {0x05});
  write_cmd_data(0x74, {0x05});
  write_cmd_data(0x65, {0x02});
  write_cmd_data(0x75, {0x02});
  write_cmd_data(0x67, {0x23});
  write_cmd_data(0x77, {0x23});
  write_cmd_data(0x69, {0x08});
  write_cmd_data(0x79, {0x08});
  write_cmd_data(0x6A, {0x13});
  write_cmd_data(0x7A, {0x13});
  write_cmd_data(0x6B, {0x13});
  write_cmd_data(0x7B, {0x13});
  write_cmd_data(0x6F, {0x00});
  write_cmd_data(0x7F, {0x00});
  write_cmd_data(0x50, {0x00});
  write_cmd_data(0x52, {0xD6});
  write_cmd_data(0x53, {0x08});
  write_cmd_data(0x54, {0x08});
  write_cmd_data(0x55, {0x1E});
  write_cmd_data(0x56, {0x1C});
  write_cmd_data(0xA0, {0x2B, 0x24, 0x00});
  write_cmd_data(0xA1, {0x87});
  write_cmd_data(0xA2, {0x86});
  write_cmd_data(0xA5, {0x00});
  write_cmd_data(0xA6, {0x00});
  write_cmd_data(0xA7, {0x00});
  write_cmd_data(0xA8, {0x36});
  write_cmd_data(0xA9, {0x7E});
  write_cmd_data(0xAA, {0x7E});
  write_cmd_data(0xB9, {0x85});
  write_cmd_data(0xBA, {0x84});
  write_cmd_data(0xBB, {0x83});
  write_cmd_data(0xBC, {0x82});
  write_cmd_data(0xBD, {0x81});
  write_cmd_data(0xBE, {0x80});
  write_cmd_data(0xBF, {0x01});
  write_cmd_data(0xC0, {0x02});
  write_cmd_data(0xC1, {0x00});
  write_cmd_data(0xC2, {0x00});
  write_cmd_data(0xC3, {0x00});
  write_cmd_data(0xC4, {0x33});
  write_cmd_data(0xC5, {0x7E});
  write_cmd_data(0xC6, {0x7E});
  write_cmd_data(0xC8, {0x33, 0x33});
  write_cmd_data(0xC9, {0x68});
  write_cmd_data(0xCA, {0x69});
  write_cmd_data(0xCB, {0x6A});
  write_cmd_data(0xCC, {0x6B});
  write_cmd_data(0xCD, {0x33, 0x33});
  write_cmd_data(0xCE, {0x6C});
  write_cmd_data(0xCF, {0x6D});
  write_cmd_data(0xD0, {0x6E});
  write_cmd_data(0xD1, {0x6F});
  write_cmd_data(0xAB, {0x03, 0x67});
  write_cmd_data(0xAC, {0x03, 0x6B});
  write_cmd_data(0xAD, {0x03, 0x68});
  write_cmd_data(0xAE, {0x03, 0x6C});
  write_cmd_data(0xB3, {0x00});
  write_cmd_data(0xB4, {0x00});
  write_cmd_data(0xB5, {0x00});
  write_cmd_data(0xB6, {0x32});
  write_cmd_data(0xB7, {0x7E});
  write_cmd_data(0xB8, {0x7E});
  write_cmd_data(0xE0, {0x00});
  write_cmd_data(0xE1, {0x03, 0x0F});
  write_cmd_data(0xE2, {0x04});
  write_cmd_data(0xE3, {0x01});
  write_cmd_data(0xE4, {0x0E});
  write_cmd_data(0xE5, {0x01});
  write_cmd_data(0xE6, {0x19});
  write_cmd_data(0xE7, {0x10});
  write_cmd_data(0xE8, {0x10});
  write_cmd_data(0xEA, {0x12});
  write_cmd_data(0xEB, {0xD0});
  write_cmd_data(0xEC, {0x04});
  write_cmd_data(0xED, {0x07});
  write_cmd_data(0xEE, {0x07});
  write_cmd_data(0xEF, {0x09});
  write_cmd_data(0xF0, {0xD0});
  write_cmd_data(0xF1, {0x0E});
  // Vendor Arduino reference has TFT_SEND_CMD(0xF9) commented out, but still
  // sends 0x17 as data after F1. Keep this odd sequence byte-for-byte.
  write_data(0x17);
  write_cmd_data(0xF2, {0x2C, 0x1B, 0x0B, 0x20});
  write_cmd_data(0xE9, {0x29});
  write_cmd_data(0x35, {0x00});
  write_cmd_data(0x44, {0x00, 0x10});
  write_cmd_data(0x46, {0x10});
  write_cmd_data(0xFF, {0x00});
  write_cmd_data(0x3A, {0x05});
  write_cmd(0x11);
  wait_with_wdt(220);
  write_cmd(0x29);
  wait_with_wdt(80);
  ESP_LOGI("LCD_PROBE", "vendor NV3007 init done");
}

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  x0 += LCD_X_OFFSET;
  x1 += LCD_X_OFFSET;
  uint8_t x_data[4] = {
      static_cast<uint8_t>(x0 >> 8), static_cast<uint8_t>(x0 & 0xFF),
      static_cast<uint8_t>(x1 >> 8), static_cast<uint8_t>(x1 & 0xFF)};
  uint8_t y_data[4] = {
      static_cast<uint8_t>(y0 >> 8), static_cast<uint8_t>(y0 & 0xFF),
      static_cast<uint8_t>(y1 >> 8), static_cast<uint8_t>(y1 & 0xFF)};

  write_cmd(0x2A);
  write_data_bytes(x_data, sizeof(x_data));
  write_cmd(0x2B);
  write_data_bytes(y_data, sizeof(y_data));
  write_cmd(0x2C);
}

static void fill565(uint16_t color) {
  set_window(0, 0, LCD_W - 1, LCD_H - 1);
  uint8_t hi = static_cast<uint8_t>(color >> 8);
  uint8_t lo = static_cast<uint8_t>(color & 0xFF);

  for (uint16_t y = 0; y < LCD_H; y++) {
    gpio_set_level(PIN_DC, 1);
    gpio_set_level(PIN_CS, 0);
    for (uint16_t x = 0; x < LCD_W; x++) {
      write_payload_byte(true, hi);
      write_payload_byte(true, lo);
    }
    gpio_set_level(PIN_CS, 1);

    if ((y & 0x07) == 0) {
      App.feed_wdt();
      delay(0);
    }
  }
}

static void run_color_test() {
  g_three_wire_mode = false;
  ESP_LOGI("LCD_PROBE", "raw 4-wire probe start (CS=13 DC=14 RST=15 CLK=5 MOSI=4)");
  hard_reset_panel();
  vendor_nv3007_init();

  ESP_LOGI("LCD_PROBE", "raw fill red");
  fill565(0xF800);
  delay(400);
  ESP_LOGI("LCD_PROBE", "raw fill green");
  fill565(0x07E0);
  delay(400);
  ESP_LOGI("LCD_PROBE", "raw fill blue");
  fill565(0x001F);
  delay(400);
  ESP_LOGI("LCD_PROBE", "raw 4-wire probe done");
}

static void run_color_test_3wire() {
  g_three_wire_mode = true;
  ESP_LOGI("LCD_PROBE", "raw 3-wire/9-bit probe start (CS=13 RST=15 CLK=5 MOSI=4)");
  hard_reset_panel();
  vendor_nv3007_init();

  ESP_LOGI("LCD_PROBE", "3-wire fill white");
  fill565(0xFFFF);
  delay(800);
  ESP_LOGI("LCD_PROBE", "3-wire fill red");
  fill565(0xF800);
  delay(800);
  ESP_LOGI("LCD_PROBE", "3-wire fill blue");
  fill565(0x001F);
  delay(800);
  g_three_wire_mode = false;
  ESP_LOGI("LCD_PROBE", "raw 3-wire/9-bit probe done");
}

}  // namespace lcd_probe
#endif
