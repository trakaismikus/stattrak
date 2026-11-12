"""Settings > "Bluetooth: ENABLE" (main/views/bluetooth_view.c): pick UPDATE or APPS.

The radio start pulls a current peak. With a bright display on a weak battery path, the two together
pulled the supply below the brown-out level and the device switched off (docs/BLUETOOTH.md, "Radio
start"). So Bluetooth starts only after this screen, and this screen and the two Bluetooth screens
light few pixels. The Settings screen (about 1,700 lit pixels) failed 6 of 10 starts on such a
device; the kill counter (about 850) never failed.
"""

import unittest

from helpers import SimTestCase

# Most lit pixels from the radio start to 0.4 s after it (the current peak is in the first 0.1 s)
MAX_LIT_AT_RADIO_START = 500


def lit(frame):
    return sum(row.count("#") for row in frame)


class BluetoothViewTest(SimTestCase):

    def open_bluetooth_view(self):
        self.dev.boot()
        self.open_setting("BLUETOOTH")
        self.dev.long_press(2)
        self.assertEqual(self.dev.view, "BLUETOOTH")

    def test_bluetooth_is_off_on_the_bluetooth_view(self):
        self.open_bluetooth_view()
        self.assertEqual(self.dev.state()["ota_active"], 0)

    def test_hold_button_1_opens_the_update_view(self):
        self.open_bluetooth_view()
        self.dev.long_press(1)
        state = self.dev.state()
        self.assertEqual(state["view"], "UPDATE")
        self.assertEqual(state["ota_active"], 1)
        self.assertEqual(state["ota_update_allowed"], 1)
        self.assertEqual(state["ble_name"], "StatTrak")

    def test_hold_button_2_opens_the_app_sync(self):
        self.open_bluetooth_view()
        self.dev.long_press(2)
        state = self.dev.state()
        self.assertEqual(state["view"], "APP_SYNC")
        self.assertEqual(state["ota_active"], 1)
        self.assertEqual(state["ota_update_allowed"], 0)

    def test_a_press_goes_back_to_the_settings(self):
        for button in (1, 2):
            with self.subTest(button=button):
                self.open_bluetooth_view()
                self.dev.click(button, settle_ms=700)   # A press after a hold counts about 0.5 s after it
                self.assertEqual(self.dev.view, "SETTINGS")
                self.assertEqual(self.dev.state()["ota_active"], 0)
                self.dev.power_cut()
                self._setting_idx = 0

    def test_the_hold_that_opens_the_view_picks_nothing(self):
        # A long hold of the settings row (with repeats) opens this view and nothing more
        self.dev.boot()
        self.open_setting("BLUETOOTH")
        self.dev.long_press(2, hold_ms=2500)
        self.assertEqual(self.dev.view, "BLUETOOTH")
        self.assertEqual(self.dev.state()["ota_active"], 0)

    def test_cancel_and_back_come_back_to_this_view(self):
        self.open_bluetooth_view()
        self.dev.long_press(1)            # UPDATE
        self.dev.long_press(1)            # CANCEL
        self.assertEqual(self.dev.view, "BLUETOOTH")
        self.dev.long_press(2)            # APPS
        self.dev.long_press(1)            # BACK
        self.assertEqual(self.dev.view, "BLUETOOTH")
        self.assertEqual(self.dev.state()["ota_active"], 0)

    def test_few_lit_pixels_when_the_radio_starts(self):
        for button, view in ((1, "UPDATE"), (2, "APP_SYNC")):
            with self.subTest(view=view):
                self.open_bluetooth_view()
                self.assertLessEqual(lit(self.dev.frame()), MAX_LIT_AT_RADIO_START)
                self.dev.press(button)
                started = None
                worst = 0
                for step in range(150):              # 1.5 s in 10 ms steps
                    self.dev.run(10)
                    if started is None and self.dev.state()["ota_active"]:
                        started = step
                    if started is not None and step - started <= 40:
                        worst = max(worst, lit(self.dev.frame()))
                self.dev.release(button)
                self.dev.run(500)
                self.assertIsNotNone(started, "Bluetooth did not start")
                self.assertEqual(self.dev.view, view)
                self.assertLessEqual(worst, MAX_LIT_AT_RADIO_START,
                                     "too many lit pixels at the radio start: the device can switch off")
                self.dev.power_cut()
                self._setting_idx = 0


if __name__ == "__main__":
    unittest.main()
