"""Power-on rules. These protect the way into the firmware update mode."""

import unittest

from helpers import SimTestCase, DEFAULT_BRIGHTNESS, NO_PROMPTS


class BootTest(SimTestCase):

    def test_first_start_stores_defaults(self):
        self.dev.boot()
        state = self.dev.state()
        self.assertEqual(state["view"], "TOTAL")
        self.assertEqual(state["latched"], 1)
        self.assertEqual(state["eeprom.init"], 1)
        self.assertEqual(state["eeprom.brightness"], 8)
        self.assertEqual(state["eeprom.dim_screen"], 3)
        self.assertEqual(state["eeprom.dim_level"], 2)
        self.assertEqual(state["eeprom.sound"], 1)
        self.assertEqual(state["eeprom.total_kills"], 0)
        self.assertEqual(state["brightness"], DEFAULT_BRIGHTNESS)
        self.assertEqual(state["nvs.eeprom.init"], 1)

    def test_first_start_works_with_one_button(self):
        self.dev.power_on(hold="1")
        self.dev.run(400)
        self.dev.release(1)
        self.dev.run(1000)
        self.assertTrue(self.dev.is_on)
        # A new device asks for its version first (tests/test_device_model.py)
        self.assertEqual(self.dev.view, "DEVICE_MODEL")

    def test_later_start_needs_both_buttons(self):
        self.first_boot()
        for hold in ("1", "2", "none"):
            with self.subTest(hold=hold):
                self.dev.power_on(hold=hold)
                self.dev.run(100)
                self.assert_powered_off()
                self.assertEqual(self.dev.ended[1], 0, "the firmware must switch off immediately")

    def test_power_latch_is_set_before_the_buttons_are_released(self):
        # The power comes from the held buttons until the firmware sets the power latch.
        # If the firmware does not set the latch early, the simulator ends with "powerlost".
        self.first_boot()
        self.dev.power_on(hold="both")
        self.dev.run(20)
        self.assertEqual(self.dev.state()["latched"], 1)
        self.dev.release(1)
        self.dev.release(2)
        self.dev.run(2000)
        self.assertTrue(self.dev.is_on, "session ended with: %s" % (self.dev.ended,))

    def test_buttons_are_ignored_until_released(self):
        self.first_boot(**NO_PROMPTS)
        self.dev.power_on(hold="both")
        self.dev.run(3000)  # A long press on both buttons is "power off" in normal operation
        self.assertTrue(self.dev.is_on)
        self.dev.release(1)
        self.dev.release(2)
        self.dev.run(1000)
        state = self.dev.state()
        self.assertEqual(state["view"], "TOTAL")
        self.assertEqual(state["eeprom.total_kills"], 0)

    def test_hold_both_buttons_10_seconds_opens_update_mode(self):
        self.first_boot()
        self.dev.power_on(hold="both")
        self.dev.run(10600)
        state = self.dev.state()
        self.assertEqual(state["view"], "UPDATE")
        self.assertEqual(state["ota_active"], 1, "the update service must be on in the update view")
        self.dev.release(1)
        self.dev.release(2)
        self.dev.run(2000)
        state = self.dev.state()
        self.assertEqual(state["view"], "UPDATE")
        self.assertEqual(state["ota_active"], 1)

    def test_hold_both_buttons_9_seconds_is_a_normal_start(self):
        self.first_boot(**NO_PROMPTS)
        self.dev.power_on(hold="both")
        self.dev.run(9000)
        self.dev.release(1)
        self.dev.release(2)
        self.dev.run(1000)
        state = self.dev.state()
        self.assertEqual(state["view"], "TOTAL")
        self.assertEqual(state["ota_active"], 0)

    def test_long_press_both_buttons_switches_off(self):
        self.dev.boot()
        self.dev.long_press_both()
        self.assert_powered_off()

    def test_display_shows_something_after_start(self):
        self.dev.boot()
        frame = self.dev.frame()
        self.assertEqual(len(frame), 32)
        self.assertTrue(all(len(line) == 128 for line in frame))
        self.assertGreater(sum(line.count("#") for line in frame), 100)


if __name__ == "__main__":
    unittest.main()
