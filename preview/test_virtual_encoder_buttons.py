from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "智能前级蓝牙2.0.yaml"


class VirtualEncoderButtonTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.yaml = YAML.read_text(encoding="utf-8")

    def test_virtual_encoder_entities_are_removed_from_firmware(self):
        for text in (
            "Virtual Encoder CW",
            "Virtual Encoder CCW",
            "Virtual Encoder Click",
            "Virtual Encoder Double",
            "Virtual Encoder Long",
            "virtual_encoder_pos",
            "virtual_encoder_cw",
            "virtual_encoder_ccw",
            "virtual_encoder_click",
            "virtual_encoder_double",
            "virtual_encoder_long",
        ):
            self.assertNotIn(text, self.yaml)

    def test_encoder_action_scripts_are_named_for_real_hardware(self):
        self.assertIn("id: encoder_click_action", self.yaml)
        self.assertIn("id: encoder_double_action", self.yaml)
        self.assertIn("id: encoder_long_action", self.yaml)
        self.assertNotIn("id: virtual_encoder_click_action", self.yaml)
        self.assertNotIn("id: virtual_encoder_double_action", self.yaml)
        self.assertNotIn("id: virtual_encoder_long_action", self.yaml)


if __name__ == "__main__":
    unittest.main()
