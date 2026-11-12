"""Screen dim, automatic power-off and battery warnings."""

import unittest

from helpers import (SimTestCase, BATTERY_LIPO_1S, BATTERY_NONE, DEFAULT_BRIGHTNESS,
                     DEFAULT_DIM_BRIGHTNESS, MINUTE)


class DimTest(SimTestCase):

    def test_screen_dims_after_30_seconds_and_wakes_on_a_press(self):
        self.dev.boot()  # The 30 seconds start at power-on
        self.dev.run(27000)
        self.assertEqual(self.dev.state()["brightness"], DEFAULT_BRIGHTNESS)
        self.dev.run(8000)
        self.assertEqual(self.dev.state()["brightness"], DEFAULT_DIM_BRIGHTNESS)
        self.dev.click(1)
        self.assertEqual(self.dev.state()["brightness"], DEFAULT_BRIGHTNESS)

    def test_dim_can_be_switched_off(self):
        self.boot_with(dim_screen=0)
        self.dev.run(2 * MINUTE)
        self.assertEqual(self.dev.state()["brightness"], DEFAULT_BRIGHTNESS)


class AutoOffTest(SimTestCase):

    def test_auto_off_is_off_by_default(self):
        self.dev.boot()
        self.dev.run(35 * MINUTE)
        self.assertTrue(self.dev.is_on)
        self.assertEqual(self.dev.view, "TOTAL")

    def test_warning_5_minutes_before_power_off(self):
        self.boot_with(auto_off=1)  # 30 minutes
        self.dev.run(24 * MINUTE)
        self.assertEqual(self.dev.view, "TOTAL")
        self.dev.run(1 * MINUTE)
        self.assertEqual(self.dev.view, "AUTO_OFF")

        # A press closes the warning. It must not count a kill.
        self.dev.click(1)
        self.dev.run(500)
        state = self.dev.state()
        self.assertEqual(state["view"], "TOTAL")
        self.assertEqual(state["eeprom.total_kills"], 0)

        # The 30 minutes start again
        self.dev.run(24 * MINUTE)
        self.assertEqual(self.dev.view, "TOTAL")

    def test_power_off_after_the_set_time(self):
        self.boot_with(auto_off=1)  # 30 minutes
        self.dev.run(29 * MINUTE)
        self.assertTrue(self.dev.is_on)
        self.dev.run(2 * MINUTE)
        self.assert_powered_off()


class BatteryTest(SimTestCase):

    def test_no_battery_type_means_no_battery_warning(self):
        self.boot_with(voltage=0, battery_type=BATTERY_NONE)
        self.dev.run(2 * MINUTE)
        self.assertTrue(self.dev.is_on)
        self.assertEqual(self.dev.view, "TOTAL")

    def test_critical_battery_warns_and_then_switches_off(self):
        self.boot_with(voltage=3000, battery_type=BATTERY_LIPO_1S)
        self.dev.run(45000)
        self.assertEqual(self.dev.view, "TOTAL")
        self.dev.run(5000)
        self.assertEqual(self.dev.view, "BATTERY_CRITICAL")
        # The warning cannot be closed
        self.dev.click(1)
        self.dev.click_both()
        self.assertEqual(self.dev.view, "BATTERY_CRITICAL")
        self.dev.run(15000)
        self.assert_powered_off()

    def test_critical_timer_starts_again_when_the_battery_is_good_again(self):
        self.boot_with(voltage=3000, battery_type=BATTERY_LIPO_1S)
        self.dev.run(30000)
        self.dev.set_voltage(3900)
        self.dev.run(1000)
        self.dev.set_voltage(3000)
        self.dev.run(40000)  # More than 60 s after start, less than 49 s after the good reading
        self.assertTrue(self.dev.is_on)
        self.assertEqual(self.dev.view, "TOTAL")

    def test_low_battery_warning_is_shown_one_time(self):
        # 3.7 V on a 1S LiPo is 16 %. The warning level is 20 % (setting value 3).
        self.boot_with(voltage=3700, battery_type=BATTERY_LIPO_1S, low_bat=3)
        self.dev.run(58 * 1000)
        self.assertEqual(self.dev.view, "TOTAL")
        self.dev.run(3000)
        self.assertEqual(self.dev.view, "BATTERY_LOW")
        self.dev.click(1)                  # A short press does not dismiss it: DISMISS needs a hold
        self.dev.run(500)
        self.assertEqual(self.dev.view, "BATTERY_LOW")
        self.dev.long_press(1)
        state = self.dev.state()
        self.assertEqual(state["view"], "TOTAL")
        self.assertEqual(state["eeprom.total_kills"], 0)
        self.dev.run(3 * MINUTE)
        self.assertEqual(self.dev.view, "TOTAL")

    def test_low_battery_warning_is_off_by_default(self):
        self.boot_with(voltage=3650, battery_type=BATTERY_LIPO_1S)
        self.dev.run(2 * MINUTE)
        self.assertEqual(self.dev.view, "TOTAL")

    def test_battery_symbol_is_drawn_when_show_battery_is_on(self):
        self.boot_with(voltage=4000, battery_type=BATTERY_LIPO_1S, show_battery=1)
        with_battery = self.dev.frame()
        self.dev.power_cut()
        self.dev.write_flash({"show_battery": 0})
        self.dev.boot(voltage=4000)
        self.assertNotEqual(with_battery, self.dev.frame())


if __name__ == "__main__":
    unittest.main()
