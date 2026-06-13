from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "智能前级蓝牙2.0.yaml"


class VirtualEncoderButtonTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.yaml = YAML.read_text(encoding="utf-8")

    def _button_block(self, name):
        pattern = rf'name: "{re.escape(name)}".*?(?=\n  - platform: template|\nsensor:)'
        match = re.search(pattern, self.yaml, re.S)
        self.assertIsNotNone(match, f"missing button block: {name}")
        return match.group(0)

    def test_virtual_buttons_use_action_scripts(self):
        self.assertIn("id: virtual_encoder_click_action", self.yaml)
        self.assertIn("id: virtual_encoder_double_action", self.yaml)
        self.assertIn("id: virtual_encoder_long_action", self.yaml)

        self.assertIn("script.execute: virtual_encoder_click_action", self._button_block("Virtual Encoder Click"))
        self.assertIn("script.execute: virtual_encoder_double_action", self._button_block("Virtual Encoder Double"))
        self.assertIn("script.execute: virtual_encoder_long_action", self._button_block("Virtual Encoder Long"))

    def test_virtual_buttons_do_not_publish_gpio_button_state(self):
        for name in ("Virtual Encoder Click", "Virtual Encoder Double", "Virtual Encoder Long"):
            block = self._button_block(name)
            self.assertNotIn("encoder_button).publish_state", block)


if __name__ == "__main__":
    unittest.main()
