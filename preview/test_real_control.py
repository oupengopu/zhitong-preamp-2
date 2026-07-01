from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parent


class RealControlPreviewTest(unittest.TestCase):
    def test_preview_no_longer_targets_real_device(self):
        html = (ROOT / "index.html").read_text(encoding="utf-8")
        self.assertNotIn("zhitong-preamp-2.local", html)
        self.assertNotIn("192.168.31.86", html)

    def test_frontend_has_no_real_encoder_controls(self):
        html = (ROOT / "index.html").read_text(encoding="utf-8")
        self.assertNotIn("/encoder/rotate", html)
        self.assertNotIn("realEncoderRotateBatch", html)
        self.assertNotIn("realEncoderPress", html)
        self.assertNotIn("realButtonPress", html)
        self.assertNotIn("REAL_ENCODER_BUTTONS", html)

    def test_preview_server_has_no_real_device_proxy(self):
        server = (ROOT / "server.py").read_text(encoding="utf-8")
        self.assertNotIn("/real/", server)
        self.assertNotIn("/encoder/rotate", server)
        self.assertNotIn("REAL_DEVICE_MAC", server)
        self.assertNotIn("_find_host_by_mac", server)

    def test_input_volume_is_persisted_after_debounce_not_each_step(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("id: schedule_current_input_volume_save", yaml)
        self.assertIn("delay: 2s", yaml)
        self.assertEqual(yaml.count("id(save_current_input_volume)->execute(input, volume);"), 1)
        self.assertGreaterEqual(
            yaml.count("id(schedule_current_input_volume_save)->execute(id(current_input), id(volume_val));"),
            5,
        )

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
        self.assertNotIn("parse_mouse_like", header)
        self.assertIn("HID notify handle", header)

    def test_ble_hid_uses_learned_raw_keys_instead_of_observed_bitmask_presets(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        notify_block = header[header.index("learned-only mode"):header.index("if (evt_type != HID_EVT_NONE)")]
        self.assertIn("evt_type = _match_learned_key(fp);", notify_block)
        self.assertNotIn("pending_ambig80_mute", header)
        self.assertNotIn("pending_ambig40_volume_up", header)
        self.assertNotIn("is_ambig40_report", header)
        self.assertNotIn("suppress_power_until_ms", header)

    def test_ble_hid_mutex_never_blocks_callbacks_indefinitely(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        self.assertNotIn("portMAX_DELAY", header)
        self.assertIn("xSemaphoreTake(S().mux, 0)", header)

    def test_ble_hid_keeps_len10_reports_learnable_without_preset_dispatch(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        notify_block = header[header.index("learned-only mode"):header.index("if (evt_type != HID_EVT_NONE)")]
        self.assertIn("RawFingerprint", header)
        self.assertIn("_make_raw_fingerprint", header)
        self.assertIn("_match_learned_key(fp)", notify_block)
        self.assertNotIn("VOL_UP_1", notify_block)
        self.assertNotIn("NEXT_1", notify_block)
        self.assertNotIn("PREV_1", notify_block)

    def test_ble_hid_learns_normalized_fingerprints_for_dynamic_remote(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        self.assertIn("struct NormalizedFingerprint", header)
        self.assertIn("struct TrendWindow", header)
        self.assertIn("_normalize_report", header)
        self.assertIn("NK_SHORT_KEY", header)
        self.assertIn("NK_AXIS_DIR", header)
        self.assertIn("NK_AXIS_CENTER", header)
        self.assertIn("NK_AXIS_POINT", header)
        self.assertIn("data[0] == 0x40 || data[0] == 0x80", header)
        self.assertIn("data[1] == 0x20", header)
        self.assertIn("int16_t x = (int16_t)(data[1] | (data[2] << 8));", header)
        self.assertIn("int16_t y = (int16_t)(data[3] | (data[4] << 8));", header)
        self.assertIn("_abs32((int32_t)x - 0x0E46) < 100", header)
        self.assertIn("_abs32((int32_t)y - 0x04CA) < 100", header)
        self.assertIn("last_trigger_time", header)
        notify_block = header[header.index("learned-only mode"):header.index("if (evt_type != HID_EVT_NONE)")]
        self.assertIn("evt_type = _match_learned_key(fp);", notify_block)
        self.assertNotIn("_make_raw_fingerprint(n_handle, n_val, n_len)", notify_block)

    def test_main_page_separates_app_and_remote_ble_icons(self):
        yaml = (ROOT.parent / "智能前级蓝牙2.0.yaml").read_text(encoding="utf-8")
        self.assertIn("id(ble_connected) && !id(ble_remote_connected)", yaml)
        self.assertIn("last_runtime_theme", yaml)

    def test_pga2311_releases_cs_before_dedup_return(self):
        header = (ROOT.parent / "PGA" / "pga2311.h").read_text(encoding="utf-8")
        set_volume = header.index("static void set_volume")
        cs_release = header.index("gpio_set_level((gpio_num_t)PGA2311_CS_PIN, 1);", set_volume)
        dedup_return = header.index("if (right_vol == _last_r && left_vol == _last_l) return;", set_volume)
        self.assertLess(cs_release, dedup_return)
        self.assertIn("previous interrupted transfer left CS low", header)

    def test_ble_hid_control_keys_wake_from_standby(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("HID wake from standby", yaml)
        self.assertIn("id(exit_standby)->execute()", yaml)

    def test_power_transition_guard_blocks_audio_restore(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("id: power_transitioning", yaml)
        send_start = yaml.index("id: send_volume_to_pga")
        send_block = yaml[send_start:yaml.index("while:", send_start)]
        self.assertIn("id(power_transitioning)", send_block)
        switch_start = yaml.index("id: switch_input")
        switch_block = yaml[switch_start:yaml.index("id: auto_switch_input", switch_start)]
        self.assertIn("!id(power_transitioning)", switch_block)

    def test_standby_enter_exit_use_power_transition_lock(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        enter_start = yaml.index("id: enter_standby")
        enter_block = yaml[enter_start:yaml.index("id: exit_standby", enter_start)]
        self.assertIn("id(power_transitioning) || id(standby)", enter_block)
        self.assertIn("id(power_transitioning) = true;", enter_block)
        self.assertIn("script.stop: send_volume_to_pga", enter_block)
        self.assertIn("script.stop: switch_input", enter_block)
        self.assertIn("id(standby_wake_block_until_ms) = millis() + 2500;", enter_block)
        self.assertIn("auto off = id(display_backlight)->turn_off();", enter_block)
        self.assertIn("off.perform();", enter_block)
        self.assertIn("id(power_transitioning) = false;", enter_block)

        exit_start = yaml.index("id: exit_standby")
        exit_block = yaml[exit_start:yaml.index("id: reset_idle_timer", exit_start)]
        self.assertIn("!id(standby) || id(power_transitioning) || millis() < id(standby_wake_block_until_ms)", exit_block)
        self.assertIn("id(power_transitioning) = true;", exit_block)
        self.assertIn("id(system_ready) = false;", exit_block)
        self.assertIn("id(power_transitioning) = false;", exit_block)
        self.assertLess(exit_block.index("id(power_transitioning) = false;"), exit_block.index("script.execute: send_volume_to_pga"))
        on_resume = yaml[yaml.index("on_resume:"):yaml.index("pages:", yaml.index("on_resume:"))]
        self.assertIn("lambda: 'return !id(standby);'", on_resume)
        self.assertIn("id: display_backlight", on_resume)

    def test_manual_spectrum_exit_suppresses_auto_reentry(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("id: spectrum_auto_suppress_until_ms", yaml)

        encoder_start = yaml.index("platform: rotary_encoder")
        encoder_block = yaml[encoder_start:yaml.index("platform: template", encoder_start)]
        self.assertIn("id(spectrum_auto_suppress_until_ms) = now + 1000;", encoder_block)
        self.assertLess(
            encoder_block.index("id(spectrum_auto_suppress_until_ms) = now + 1000;"),
            encoder_block.index("lv_scr_load_anim(id(main_page)->obj"),
        )

        click_start = yaml.index("id: encoder_click_action")
        click_block = yaml[click_start:yaml.index("id: encoder_double_action", click_start)]
        self.assertIn("id(spectrum_auto_suppress_until_ms) = millis() + 1000;", click_block)

        auto_start = yaml.index("频谱页自动切换")
        auto_block = yaml[auto_start:yaml.index("页面空闲自动返回主页", auto_start)]
        self.assertIn("auto_suppressed", auto_block)
        self.assertIn("signal_stable_ms = now;", auto_block)
        self.assertIn("!auto_suppressed", auto_block)

    def test_boot_keeps_audio_locked_until_main_page(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        boot_block = yaml[yaml.index("on_boot:"):yaml.index("priority: -200")]
        self.assertIn("id(power_transitioning) = !id(standby);", boot_block)
        self.assertIn("id(power_transitioning) = false;", boot_block)
        self.assertNotIn("system_ready=true before boot input switch", boot_block)
        show_start = yaml.index("id: show_main_boot")
        show_block = yaml[show_start:yaml.index("id: media_reset", show_start)]
        self.assertIn("id(power_transitioning) = false;", show_block)
        self.assertLess(show_block.index("id(power_transitioning) = false;"), show_block.index("id(send_volume_to_pga)->execute();"))

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
        self.assertIn("孔雀青", helper)
        self.assertIn("0x0F766E", helper)
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

    def test_main_balance_marks_follow_theme_and_db_stays_foreground(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("lv_obj_set_style_text_color(id(lbl_bal_L), _accent, LV_PART_MAIN);", yaml)
        self.assertIn("lv_obj_set_style_text_color(id(lbl_bal_R), _accent, LV_PART_MAIN);", yaml)
        self.assertIn("lv_obj_set_style_text_color(id(lbl_balance_val), _accent, LV_PART_MAIN);", yaml)
        self.assertIn("lv_obj_move_foreground(id(lbl_vol_db));", yaml)

    def test_display_uses_rgb_color_order(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("color_order: RGB", yaml)

    def test_encoder_double_click_window_is_relaxed_for_real_button(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        double_start = yaml.index("# 双击")
        double_block = yaml[double_start:yaml.index("# 单击", double_start)]
        self.assertIn("ON for at most 450ms", double_block)
        self.assertIn("OFF for at most 650ms", double_block)
        self.assertIn("OFF for at least 250ms", double_block)

    def test_single_click_waits_longer_than_double_click_gap(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        single_start = yaml.index("# 单击")
        single_block = yaml[single_start:yaml.index("# 长按", single_start)]
        self.assertIn("ON for at most 450ms", single_block)
        self.assertIn("OFF for at least 700ms", single_block)

    def test_ntc_real_divider_has_no_bench_attach_filter(self):
        header = (ROOT.parent / "PGA" / "msgeq7.h").read_text(encoding="utf-8")
        self.assertNotIn("_ntc_plausible_seen", header)
        self.assertNotIn("NTC_ATTACH_CONFIRM_COUNT", header)
        self.assertNotIn("GPIO10 floats when the NTC divider is not fitted", header)
        self.assertIn("NTC_MAX_RUNTIME_STEP_C", header)

    def test_ntc_series_resistor_matches_measured_board_value(self):
        header = (ROOT.parent / "PGA" / "msgeq7.h").read_text(encoding="utf-8")
        self.assertIn("NTC_SERIES_RESISTOR 9740.0f", header)
        self.assertNotIn("NTC_SERIES_RESISTOR 10000.0f", header)

    def test_ntc_temperature_uses_real_machine_offset(self):
        header = (ROOT.parent / "PGA" / "msgeq7.h").read_text(encoding="utf-8")
        self.assertIn("NTC_TEMP_OFFSET_C   (-3.9f)", header)
        self.assertIn("temp += NTC_TEMP_OFFSET_C;", header)

    def test_msgeq7_idle_offset_accepts_real_33v_bias(self):
        header = (ROOT.parent / "PGA" / "msgeq7.h").read_text(encoding="utf-8")
        self.assertIn("OFFSET_CALIBRATION_MAX_RAW", header)
        self.assertIn("static constexpr int OFFSET_CALIBRATION_MAX_RAW = 3600;", header)
        self.assertNotIn("(avg_r < 50) ? avg_r : 0", header)
        self.assertNotIn("(avg_l < 50) ? avg_l : 0", header)
        self.assertIn("(avg_r < OFFSET_CALIBRATION_MAX_RAW) ? avg_r : 0", header)
        self.assertIn("(avg_l < OFFSET_CALIBRATION_MAX_RAW) ? avg_l : 0", header)

    def test_spectrum_display_clamps_idle_noise_frame(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        block = yaml[yaml.index("msgeq7::get_frame(&frame);"):yaml.index("// 频谱条和峰值线", yaml.index("msgeq7::get_frame(&frame);"))]
        self.assertIn("spectrum_frame_peak", block)
        self.assertIn("spectrum_frame_avg", block)
        self.assertIn("spectrum_frame_peak <= 36 && spectrum_frame_avg <= 16", block)
        self.assertIn("frame.left[i] = 0;", block)
        self.assertIn("frame.peak_r[i] = 0;", block)

    def test_encoder_rotation_is_ignored_around_button_press(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("id: encoder_button_guard_until_ms", yaml)
        self.assertIn("id(encoder_button_guard_until_ms) = millis() + 250;", yaml)
        encoder_block = yaml[yaml.index("id: volume_encoder"):yaml.index("lv_obj_t* active = lv_scr_act();")]
        self.assertIn("uint32_t now = millis();", encoder_block)
        self.assertIn("id(encoder_button).state || now < id(encoder_button_guard_until_ms)", encoder_block)
        self.assertIn("last_enc = cur;", encoder_block)

    def test_input_volume_is_saved_at_control_entry_points(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("id: save_current_input_volume", yaml)
        self.assertIn("id: schedule_current_input_volume_save", yaml)
        self.assertIn("parameters:\n      input: int\n      volume: int", yaml)
        self.assertNotIn("id(save_current_input_volume)->execute();", yaml)
        self.assertEqual(yaml.count("id(save_current_input_volume)->execute(input, volume);"), 1)
        self.assertGreaterEqual(
            yaml.count("id(schedule_current_input_volume_save)->execute(id(current_input), id(volume_val));"),
            8,
        )
        self.assertNotIn("diff_large", yaml)
        self.assertNotIn("stop_detect", yaml)

    def test_input_balance_is_independent_per_input(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("id: input_bal_0", yaml)
        self.assertIn("id: input_bal_3", yaml)
        self.assertIn("id: save_current_input_balance", yaml)
        self.assertIn("parameters:\n      input: int\n      balance_value: int", yaml)
        self.assertGreaterEqual(yaml.count("id(save_current_input_balance)->execute(id(current_input), id(balance));"), 3)
        self.assertIn("case 0: id(input_bal_0) = id(balance); break;", yaml)
        self.assertIn("case 0: new_bal = id(input_bal_0); break;", yaml)
        self.assertIn("id(v211_balance_migrated)", yaml)

    def test_idle_dim_and_input_card_borders_are_visible(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("call.set_brightness(0.50f);", yaml)
        self.assertIn("selected ? LV_OPA_90 : LV_OPA_70", yaml)

    def test_secondary_pages_match_readable_three_row_density(self):
        compat = (ROOT.parent / "PGA" / "lvgl_compat.h").read_text(encoding="utf-8")
        settings = (ROOT.parent / "PGA" / "settings_ui.h").read_text(encoding="utf-8")
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("constexpr int ROW_W = 382;", compat)
        self.assertIn("constexpr int ROW_H = 40;", compat)
        self.assertIn("constexpr int TITLE_H = 40;", compat)
        self.assertIn("constexpr int DEBUG_H = 36;", compat)
        self.assertIn("focused ? LV_OPA_40", compat)
        self.assertIn("focused || active ? LV_OPA_70", compat)
        self.assertIn("constexpr int BAR_ACTIVE_H = 9;", settings)
        self.assertIn("5,   // 0 max volume", settings)
        self.assertIn("size: 18\n    glyphs:", yaml)
        self.assertIn("size: 14\n    glyphs:", yaml)
        self.assertIn("active ? settings_ui::BAR_ACTIVE_H", yaml)
        self.assertIn("lv_obj_set_width(setting_bars[i], 176)", yaml)
        self.assertIn("lv_obj_set_width(c3, 48)", yaml)
        self.assertIn("lv_obj_get_child(key_rows[i], 0)", yaml)
        self.assertIn("row_accent = (focused || active) ? accent", yaml)

    def test_power_on_limit_is_bounded_by_max_volume(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertGreaterEqual(yaml.count("if (id(power_on_limit) > id(max_volume)) id(power_on_limit) = id(max_volume);"), 1)
        self.assertIn("if (v > id(max_volume)) v = id(max_volume);", yaml)
        self.assertIn("if (id(power_on_limit) > id(max_volume)) {", yaml)
        self.assertIn("if (id(power_on_limit) > max_vol) {", yaml)
        self.assertIn("if (pol > id(max_volume)) pol = id(max_volume);", yaml)

    def test_theme_encoder_defers_full_apply_until_confirm(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        theme_case = yaml[yaml.index("case 7: { // 主题色彩（循环）"):yaml.index("case 10: { // 输出模式")]
        self.assertNotIn("id(apply_theme)->execute();", theme_case)
        self.assertIn("if (active == 7) id(apply_theme)->execute();", yaml)

    def test_splash_has_500ms_rescue_show_to_avoid_white_screen(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("static bool splash_forced = false;", yaml)
        self.assertIn("now >= 500", yaml)
        self.assertIn("id(show_splash_boot)->execute();", yaml)
        self.assertIn("splash rescue show requested from interval", yaml)

    def test_secondary_page_styling_is_scoped_to_visible_page(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("bool style_all =", yaml)
        self.assertIn("bool style_settings = style_all || active_page == id(settings_page)->obj;", yaml)
        self.assertIn("bool style_ble = style_all || active_page == id(ble_remote_page)->obj", yaml)
        self.assertIn("bool style_keymap = style_all || active_page == id(remote_keys_page)->obj", yaml)
        self.assertIn("bool style_debug = style_all || active_page == id(debug_page)->obj", yaml)
        self.assertIn("if (style_settings) {", yaml)
        self.assertIn("if (style_ble) {", yaml)
        self.assertIn("if (style_keymap) {", yaml)
        self.assertIn("if (style_debug) {", yaml)

    def test_ble_hid_raw_learning_has_fingerprint_and_runtime_table(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        self.assertIn("struct RawFingerprint", header)
        self.assertIn("struct NormalizedFingerprint", header)
        self.assertIn("struct LearnedKey", header)
        self.assertIn("_make_raw_fingerprint", header)
        self.assertIn("_normalize_report", header)
        self.assertIn("_match_learned_key", header)
        self.assertIn("set_learned_key", header)
        self.assertIn("get_last_normalized_fingerprint", header)
        self.assertIn("learned[(int)action]", header)
        self.assertIn("HID learned match action=%d", header)

    def test_ble_hid_has_no_legacy_preset_parser(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        self.assertNotIn("static HidEventType _parse_report", header)
        self.assertNotIn("VOL_UP_1", header)
        self.assertNotIn("case 0x0080: return HID_EVT_MUTE", header)
        self.assertNotIn("case 0x0040: return HID_EVT_NONE", header)
        self.assertNotIn("case 0x2000: return HID_EVT_VOLUME_DOWN", header)

    def test_ble_hid_learning_filters_release_frames(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        self.assertIn("_is_learnable_raw_report", header)
        self.assertIn("keyboard_release", header)
        self.assertIn("consumer_release", header)
        self.assertIn("if (fp.is_valid()) {", header)
        self.assertIn("S().has_raw_fingerprint = true;", header)

    def test_remote_key_learning_state_is_persisted_and_registered(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("id: key_learn_waiting", yaml)
        self.assertIn("id: learned_key_1_hash", yaml)
        self.assertIn("id: learned_key_9_handle", yaml)
        self.assertIn("id: learned_key_9_kind", yaml)
        self.assertIn("id: learned_key_9_value", yaml)
        self.assertIn("id: learned_key_9_aux", yaml)
        self.assertIn("ble_hid_host::set_learned_key((ble_hid_host::HidEventType)action, handle, len, kind, value, aux, hash)", yaml)
        self.assertIn("id(register_learned_remote_keys)->execute();", yaml)
        self.assertIn("id(clear_learned_remote_keys)->execute();", yaml)

    def test_remote_key_learning_uses_latched_normalized_fingerprint(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("learning_capture_active", header)
        self.assertIn("begin_learning_capture()", header)
        self.assertIn("get_last_normalized_fingerprint", header)
        self.assertIn("if (S().learning_capture_active && !S().has_raw_fingerprint)", header)
        self.assertIn("ble_hid_host::begin_learning_capture();", yaml)
        self.assertIn("ble_hid_host::get_last_normalized_fingerprint(&handle, &len, &kind, &value, &aux, &hash)", yaml)

    def test_remote_key_actions_are_core_learning_only(self):
        header = (ROOT.parent / "PGA" / "remote_keys.h").read_text(encoding="utf-8")
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")

        self.assertIn("ACTION_PLAY", header)
        self.assertIn("ACTION_PAUSE", header)
        self.assertIn("ACTION_COUNT = 10", header)
        for removed in ("ACTION_SWIPE", "ACTION_CAMERA", "ACTION_OK"):
            self.assertNotIn(removed, header)
        key_page = yaml[yaml.index("id: remote_keys_page"):yaml.index("id: debug_page")]
        hid_block = yaml[yaml.index("HID 按键事件处理"):yaml.index("推送 HA 事件")]
        for removed in ("拍照", "左划", "右划", "上划", "下划", "切换镜头", "确定"):
            self.assertNotIn(removed, key_page)
            self.assertNotIn(removed, hid_block)
        self.assertIn("id: keymap_row_8", yaml)

    def test_remote_key_learning_uses_dedicated_learning_row_and_polling(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("id: keymap_row_learn", yaml)
        self.assertIn("id: val_keymap_learn", yaml)
        self.assertIn("id: keymap_row_reset", yaml)
        self.assertIn("id: key_learn_select_idx", yaml)
        self.assertIn("id: key_learn_pick_mode", yaml)
        self.assertIn("id(start_remote_key_learning)->execute(action);", yaml)
        self.assertIn("id(poll_remote_key_learning)->execute();", yaml)
        self.assertIn("id(clear_learned_remote_keys)->execute();", yaml)
        self.assertIn("ble_hid_host::get_last_normalized_fingerprint(&handle, &len, &kind, &value, &aux, &hash)", yaml)
        self.assertIn("lv_label_set_text(id(val_keymap_learn), label.c_str());", yaml)
        self.assertIn("lv_label_set_text(id(val_keymap_learn), text.c_str());", yaml)
        self.assertNotIn("双击学习", yaml)
        self.assertIn("id(key_learn_feedback_idx)", yaml)
        self.assertIn("id(refresh_keymap_values)->execute();", yaml)

    def test_remote_key_reset_learning_is_a_separate_focus_row(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("id(keymap_row_learn), id(keymap_row_reset), id(keymap_row_back)", yaml)
        self.assertIn("if (new_idx < 0) new_idx = 0;", yaml)
        self.assertIn("if (new_idx > 11) new_idx = 11;", yaml)
        self.assertIn("} else if (f == 10) {", yaml)
        self.assertIn("id(clear_learned_remote_keys)->execute();", yaml)
        self.assertIn("} else if (f == 11) {", yaml)

    def test_small_chinese_font_has_debug_and_learning_glyphs(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        for ch in "双中行占闲全段低水位完成失败":
            self.assertIn(ch, yaml)

    def test_debug_page_uses_compact_fixed_grid_layout(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("id: dbg_row_cpu", yaml)
        self.assertIn("CPU 占用", yaml)
        self.assertIn("auto style_debug_cell", yaml)
        self.assertIn("style_debug_cell(id(dbg_row_title), 6, 4, 416, 24", yaml)
        self.assertIn("{id(dbg_row_cpu),      6,   32, 204}", yaml)
        self.assertIn("style_debug_cell(id(dbg_row_recal), 6, 106, 204, 30", yaml)
        self.assertNotIn("secondary_ui::row(id(dbg_row_cpu)", yaml)
        self.assertNotIn("lv_obj_scroll_to_view(new_idx == 0 ? id(dbg_row_recal) : id(dbg_row_back)", yaml)

    def test_firmware_version_debug_entry_uses_reliable_click_counter(self):
        yaml_file = next(p for p in ROOT.parent.glob("*.yaml") if "2.0" in p.name)
        yaml = yaml_file.read_text(encoding="utf-8")
        self.assertIn("id: firmware_hidden_click_count", yaml)
        self.assertIn("id: firmware_hidden_click_started_ms", yaml)
        self.assertIn("if (idx == 13) {", yaml)
        self.assertIn("id(firmware_hidden_click_count)++;", yaml)
        self.assertIn("id(firmware_hidden_click_count) >= 3", yaml)
        self.assertIn("id(show_debug_page)->execute();", yaml)

    def test_remote_key_learning_clears_stale_raw_fingerprint(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        clear_fn = header[header.index("static void clear_raw_event"):header.index("static void begin_learning_capture")]
        self.assertIn("S().has_raw_event = false;", clear_fn)
        self.assertIn("S().has_raw_fingerprint = false;", clear_fn)
        self.assertIn("S().last_raw_fingerprint = NormalizedFingerprint{};", clear_fn)
        self.assertNotIn("get_last_raw_fingerprint", header)

    def test_learned_raw_match_has_no_ambiguous_key_overrides(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        notify_block = header[header.index("learned-only mode"):header.index("if (evt_type != HID_EVT_NONE)")]
        self.assertIn("bool learned_match = false;", notify_block)
        self.assertIn("learned_match = evt_type != HID_EVT_NONE;", notify_block)
        self.assertNotIn("is_ambig80_report", notify_block)
        self.assertNotIn("is_ambig40_report", notify_block)
        self.assertNotIn("suppress_power_until_ms", notify_block)

    def test_ble_hid_control_events_are_learned_only(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        notify_block = header[header.index("learned-only mode"):header.index("if (evt_type != HID_EVT_NONE)")]
        self.assertIn("learned-only mode", notify_block)
        self.assertIn("evt_type = _match_learned_key(fp);", notify_block)
        self.assertNotIn("_parse_report(n_val, n_len)", notify_block)
        self.assertNotIn("is_ambig80_report", notify_block)
        self.assertNotIn("is_ambig40_report", notify_block)

    def test_nonblocking_ble_getters_return_safe_defaults_when_lock_busy(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        self.assertIn("BLEState s = BLE_IDLE;", header)
        self.assertIn("int c = 0;", header)
        self.assertIn("int level = -1;", header)
        self.assertIn("bool v = false;", header)
        self.assertIn("uint16_t v = 0;", header)

    def test_peer_bda_restore_reports_lock_failure(self):
        header = (ROOT.parent / "PGA" / "ble_hid_host.h").read_text(encoding="utf-8")
        fn_start = header.rindex("static bool set_peer_bda_from_string(const char* str)")
        fn = header[fn_start:header.index("static bool has_paired_device", fn_start)]
        self.assertIn("bool ok = false;", fn)
        self.assertIn("ok = true;", fn)
        self.assertIn("return ok;", fn)


if __name__ == "__main__":
    unittest.main()
