from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parent


class RealControlPreviewTest(unittest.TestCase):
    def test_default_device_uses_mdns_name(self):
        html = (ROOT / "index.html").read_text(encoding="utf-8")
        self.assertIn("zhitong-preamp-2.local", html)
        self.assertNotIn("192.168.31.86", html)

    def test_frontend_batches_encoder_rotation(self):
        html = (ROOT / "index.html").read_text(encoding="utf-8")
        self.assertIn("/encoder/rotate", html)
        self.assertIn("realEncoderRotateBatch", html)

    def test_proxy_supports_encoder_rotate_endpoint(self):
        server = (ROOT / "server.py").read_text(encoding="utf-8")
        self.assertIn("/real/encoder/rotate", server)
        self.assertIn("steps", server)

    def test_proxy_falls_back_to_arp_for_mdns(self):
        server = (ROOT / "server.py").read_text(encoding="utf-8")
        self.assertIn("REAL_DEVICE_MAC", server)
        self.assertIn("_find_host_by_mac", server)

    def test_ble_hid_uses_esphome_ble_dispatcher(self):
        yaml = (ROOT.parent / "智能前级蓝牙2.0.yaml").read_text(encoding="utf-8")
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        self.assertIn("ESPHOME_ESP32_BLE_GAP_SCAN_EVENT_HANDLER_COUNT=1", yaml)
        self.assertIn("ESPHOME_ESP32_BLE_GATTC_EVENT_HANDLER_COUNT=1", yaml)
        self.assertIn("setup(id(ble_hub))", yaml)
        self.assertIn("setup(esphome::esp32_ble::ESP32BLE *ble", header)
        self.assertIn("add_gap_scan_event_callback", header)

    def test_ble_hid_requests_security_before_hid_reports(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        self.assertIn("esp_ble_set_encryption", header)
        self.assertIn("ESP_BLE_SEC_ENCRYPT_NO_MITM", header)
        self.assertIn("注册 HID notify 失败", header)
        self.assertIn("CCCD 写入失败", header)

    def test_ble_hid_defers_open_until_scan_stops(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        self.assertIn("connect_pending", header)
        self.assertIn("_open_pending_connection", header)
        self.assertIn("ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT", header)
        self.assertIn("connect request ignored while busy", header)
        self.assertIn("S().auto_reconnect = false", header)

    def test_ble_hid_only_subscribes_input_reports(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        self.assertIn("ESP_GATT_CHAR_PROP_BIT_NOTIFY", header)
        self.assertIn("ESP_GATT_CHAR_PROP_BIT_INDICATE", header)
        self.assertIn("HID_BOOT_MOUSE_INPUT_UUID", header)
        self.assertIn("parse_mouse_like", header)
        self.assertIn("HID notify handle", header)

    def test_ble_hid_supports_observed_bitmask_remote(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        self.assertIn("case 0x0080: return HID_EVT_MUTE", header)
        self.assertIn("case 0x0040: return HID_EVT_VOLUME_UP", header)
        self.assertIn("case 0x2000: return HID_EVT_VOLUME_DOWN", header)
        self.assertIn("pending_ambig80_mute", header)
        self.assertIn("<= 900", header)
        self.assertIn("now + 800", header)
        self.assertIn("suppress spurious power packet", header)

    def test_ble_hid_supports_observed_len10_remote_packets(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        self.assertIn("VOL_UP_1", header)
        self.assertIn("NEXT_1", header)
        self.assertIn("NEXT_2", header)
        self.assertIn("PLAY_PAUSE_1", header)
        self.assertIn("CYCLE_INPUT_1", header)
        self.assertIn("POWER_1", header)
        self.assertIn("near16", header)
        self.assertIn("emit_len10", header)
        self.assertIn("HID_EVT_PAUSE", header)
        self.assertIn("0x09EC", header)

    def test_main_page_separates_app_and_remote_ble_icons(self):
        yaml = (ROOT.parent / "智能前级蓝牙2.0.yaml").read_text(encoding="utf-8")
        self.assertIn("id(ble_connected) && !id(ble_remote_connected)", yaml)
        self.assertIn("last_runtime_theme", yaml)

    def test_ble_hid_control_keys_wake_from_standby(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("HID wake from standby", yaml)
        self.assertIn("id(exit_standby)->execute()", yaml)

    def test_ble_hid_splits_play_and_pause(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn('ha_event_name = "PLAY"', yaml)
        self.assertIn('ha_event_name = "PAUSE"', yaml)
        self.assertIn('publish_state("暂停")', yaml)

    def test_theme_names_use_single_helper(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        helper = (ROOT.parent / "PGA" / "lvgl_compat.h").read_text(encoding="utf-8")
        self.assertIn("get_theme_name", helper)
        self.assertIn("麦景图蓝", helper)
        self.assertIn("lv_label_set_text(id(val_theme), get_theme_name(th))", yaml)
        self.assertIn("lv_label_set_text(id(val_theme), get_theme_name(v))", yaml)
        self.assertNotIn('const char* names[] = {"翠绿"', yaml)

    def test_main_input_card_selection_does_not_light_signal_text(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("MCP23017: 只控制图标/名称/SIGNAL/IDLE 明暗", yaml)
        self.assertIn("current_input: 只控制外框/选中背景", yaml)
        self.assertIn("MSGEQ7: 只控制当前输入卡片里的小 LED 点/条", yaml)
        self.assertIn("lv_color_t icon_color = mcp_signal ? _accent : lv_color_hex(0x334155);", yaml)
        self.assertIn("lv_color_t name_color = lv_color_hex(0xF8FAFC);", yaml)
        self.assertNotIn("icon_color = mcp_signal ? (selected ?", yaml)

    def test_mute_icon_uses_red_when_muted_and_neutral_gray_when_open(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("mute_now ? lv_color_hex(0xEF4444) : lv_color_hex(0x6B7280)", yaml)
        self.assertIn("mute_ok ? lv_color_hex(0xEF4444) : lv_color_hex(0x6B7280)", yaml)

    def test_display_uses_rgb_color_order(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("color_order: RGB", yaml)

    def test_input_volume_is_saved_at_control_entry_points(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("id: save_current_input_volume", yaml)
        self.assertIn("parameters:\n      input: int\n      volume: int", yaml)
        self.assertNotIn("id(save_current_input_volume)->execute();", yaml)
        self.assertGreaterEqual(yaml.count("id(save_current_input_volume)->execute(id(current_input), id(volume_val));"), 8)
        self.assertIn("if (!id(switching_input) && !id(soft_mute) && (diff_large || stop_detect))", yaml)

    def test_idle_dim_and_input_card_borders_are_visible(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("call.set_brightness(0.50f);", yaml)
        self.assertIn("selected ? LV_OPA_90 : LV_OPA_70", yaml)

    def test_secondary_pages_match_compact_preview_density(self):
        compat = (ROOT.parent / "PGA" / "lvgl_compat.h").read_text(encoding="utf-8")
        settings = (ROOT.parent / "PGA" / "settings_ui.h").read_text(encoding="utf-8")
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("constexpr int ROW_H = 36;", compat)
        self.assertIn("constexpr int TITLE_H = 38;", compat)
        self.assertIn("constexpr int DEBUG_H = 32;", compat)
        self.assertIn("focused ? LV_OPA_40", compat)
        self.assertIn("focused || active ? LV_OPA_60", compat)
        self.assertIn("constexpr int BAR_ACTIVE_H = 9;", settings)
        self.assertIn("5,   // 0 max volume", settings)
        self.assertIn("size: 18\n    glyphs:", yaml)
        self.assertIn("active ? settings_ui::BAR_ACTIVE_H", yaml)


if __name__ == "__main__":
    unittest.main()
