"""Shared parts of the simulator tests."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from stattrak_sim import Device  # noqa: E402

# Settings in the order of the settings view (SETTING_LIST in main/app.h).
# SHOW_BATTERY and LOW_BAT are hidden when the battery type is NONE or USB.
# BATTERY_TYPE is in the list only for the device version DIY (the default of Device.boot()).
SETTINGS_WITH_BATTERY = [
    "GAME_RESET", "COUNTER", "APP_SYNC", "BATTERY_TYPE", "AUTO_OFF", "SHOW_BATTERY", "LOW_BAT",
    "BRIGHTNESS", "DIM_SCREEN", "DIM_LEVEL", "SOUND", "FW_UPDATE", "FACTORY_RESET",
    "VERSION", "SERIAL",
]
SETTINGS_NO_BATTERY = [name for name in SETTINGS_WITH_BATTERY if name not in ("SHOW_BATTERY", "LOW_BAT")]

# The apps of the app menu, in menu order (kApps in main/views/launcher_view.c).
MAIN_VIEWS = ["TOTAL", "KILL_DEAD", "STATS", "SETTINGS"]

# Battery types (main/battery.h)
BATTERY_NONE = 0
BATTERY_USB = 1
BATTERY_RCR123A = 2
BATTERY_LIPO_1S = 3

# The device version (device_model_enum in main/app.h)
MODEL_UNSET = 0
MODEL_DIY = 1
MODEL_PEQ = 2

# Flash values of a device that is set up: DIY (the answer of Device.boot()) with the battery type
# USB/DC. It starts without the question "StatTrak version" and without the warning "Battery type
# not set". For a test that starts the device by hand (power_on) and looks at the first app.
NO_PROMPTS = {"battery_type": BATTERY_USB}

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
        """Open the app `name` with the app menu: both buttons, button 2 until it is selected, both."""
        if self.dev.view == name:
            return
        self.dev.click_both()
        self.assertEqual(self.dev.view, "LAUNCHER")
        for _ in range(len(MAIN_VIEWS)):
            if self.dev.state()["launcher_sel"] == name:
                break
            self.dev.click(2)
        self.assertEqual(self.dev.state()["launcher_sel"], name)
        self.dev.click_both()
        self.assertEqual(self.dev.view, name)

    def open_setting(self, name, battery=None):
        """Go to the settings view and move the cursor to the setting `name`."""
        state = self.dev.state()
        if battery is None:
            # The two battery settings are in the list only for a real battery type (the type in use)
            battery = state["battery_type"] > BATTERY_USB
        order = SETTINGS_WITH_BATTERY if battery else SETTINGS_NO_BATTERY
        if state["eeprom.device_model"] != MODEL_DIY:
            order = [row for row in order if row != "BATTERY_TYPE"]
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
