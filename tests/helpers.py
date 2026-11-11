"""Shared parts of the simulator tests."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from stattrak_sim import Device  # noqa: E402

# Settings in the order of the settings view (SETTING_LIST in main/app.h).
# SHOW_BATTERY and LOW_BAT are hidden when the battery type is NONE or USB.
SETTINGS_WITH_BATTERY = [
    "GAME_RESET", "COUNTER", "BATTERY_TYPE", "AUTO_OFF", "SHOW_BATTERY", "LOW_BAT",
    "BRIGHTNESS", "DIM_SCREEN", "DIM_LEVEL", "SOUND", "FACTORY_RESET", "FW_UPDATE",
    "VERSION", "SERIAL",
]
SETTINGS_NO_BATTERY = [name for name in SETTINGS_WITH_BATTERY if name not in ("SHOW_BATTERY", "LOW_BAT")]

# Views that the "both buttons" short press goes through, in order.
MAIN_VIEWS = ["TOTAL", "KILL_DEAD", "STATS", "SETTINGS"]

# Battery types (main/battery.h)
BATTERY_NONE = 0
BATTERY_USB = 1
BATTERY_RCR123A = 2
BATTERY_LIPO_1S = 3

# Brightness that the display driver gets for the default setting (80 %).
MAX_BRIGHTNESS = 0xFF + 0x35
DEFAULT_BRIGHTNESS = (MAX_BRIGHTNESS * 8) // 10
DEFAULT_DIM_BRIGHTNESS = (DEFAULT_BRIGHTNESS * 2) // 10

MINUTE = 60 * 1000


class SimTestCase(unittest.TestCase):
    """A test with one simulated device in self.dev."""

    def setUp(self):
        self.dev = Device()
        self.addCleanup(self.dev.close)
        self._setting_idx = 0

    def first_boot(self, **flash):
        """Make a device that was started one time before (not a first start) and switch it off."""
        self.dev.boot()
        self.assertTrue(self.dev.is_on)
        self.dev.power_cut()
        if flash:
            self.dev.write_flash(flash)

    def boot_with(self, voltage=None, **flash):
        """Start a device that already has `flash` values stored."""
        self.first_boot(**flash)
        self.dev.boot(voltage=voltage)
        self.assertTrue(self.dev.is_on, "device did not stay on: %s" % (self.dev.ended,))
        self._setting_idx = 0
        return self.dev

    def go_to_view(self, name):
        """Use the "both buttons" short press until the view is `name`."""
        for _ in range(len(MAIN_VIEWS) + 1):
            if self.dev.view == name:
                return
            self.dev.click_both()
        self.assertEqual(self.dev.view, name)

    def open_setting(self, name, battery=None):
        """Go to the settings view and move the cursor to the setting `name`."""
        if battery is None:
            # The two battery settings are in the list only for a real battery type
            battery = self.dev.state()["eeprom.battery_type"] > BATTERY_USB
        order = SETTINGS_WITH_BATTERY if battery else SETTINGS_NO_BATTERY
        target = order.index(name)
        self.go_to_view("SETTINGS")
        while self._setting_idx < target:
            self.dev.click(2)
            self._setting_idx += 1
        while self._setting_idx > target:
            self.dev.click(1)
            self._setting_idx -= 1

    def assert_powered_off(self, reason="poweroff"):
        self.assertFalse(self.dev.is_on, "the device is still on")
        self.assertEqual(self.dev.ended[0], reason, "session ended with: %s" % (self.dev.ended,))
