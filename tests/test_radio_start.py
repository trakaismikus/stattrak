"""The soft radio start of the Bluetooth screens (main/views/bt_start.c).

The radio start pulls a current peak. With a bright display on a weak battery path, the two together
pulled the supply below the brown-out level and the device switched off (6 of 10 starts on one unit,
also with firmware 1.0.1; 0 of 15 with the display off). So the update view, the accept view and the
app sync first show only a small Bluetooth sign; the radio starts when the display shows only the
sign; the screen comes 0.4 s later. docs/BLUETOOTH.md, "Radio start".
"""

import unittest

from helpers import SimTestCase

# Most lit pixels from the radio start to 0.4 s after it (the current peak is in the first 0.1 s).
# The Settings screen has about 1,700, the kill counter about 850.
MAX_LIT_AT_RADIO_START = 150


def lit(frame):
    return sum(row.count("#") for row in frame)


class RadioStartTest(SimTestCase):

    def watch_radio_start(self, steps=200, key="ota_active"):
        """Run in 10 ms steps. Returns (step of the radio start or None, most lit pixels from the
        radio start to 0.4 s after it, most lit pixels before the radio start in the last 0.1 s)."""
        started = None
        worst = 0
        frames = []
        for step in range(steps):
            self.dev.run(10)
            frames.append(lit(self.dev.frame()))
            if started is None and self.dev.state()[key]:
                started = step
            if started is not None and step - started <= 40:
                worst = max(worst, frames[-1])
        before = max(frames[max(0, started - 3):started]) if started else None
        return started, worst, before

    def open_from_settings(self, row, button=2):
        self.dev.boot()
        self.open_setting(row)
        self.dev.press(button)
        return self.watch_radio_start()

    def check(self, started, worst, before):
        self.assertIsNotNone(started, "Bluetooth did not start")
        self.assertLessEqual(before, MAX_LIT_AT_RADIO_START, "the display was not dark when the radio started")
        self.assertLessEqual(worst, MAX_LIT_AT_RADIO_START, "too many lit pixels at the radio start")

    def test_update_view(self):
        started, worst, before = self.open_from_settings("FW_UPDATE")
        self.dev.release(2)
        self.check(started, worst, before)
        # The radio starts after the view change (0.3 s) and a few frames of the sign
        self.assertGreaterEqual(started * 10, 300)
        self.dev.run(1000)
        state = self.dev.state()
        self.assertEqual(state["view"], "UPDATE")
        self.assertEqual(state["ota_active"], 1)
        self.assertGreater(lit(self.dev.frame()), MAX_LIT_AT_RADIO_START, "the update screen did not come")

    def test_titan(self):
        # The TITAN app (main/views/titan_view.c): its own radio (components/gate_ble), same soft start
        self.dev.boot()
        self.dev.click_both()
        while self.dev.state()["launcher_sel"] != "TITAN":
            self.dev.click(2)
        self.dev.press(1)
        self.dev.press(2)
        self.dev.run(80)
        self.dev.release(1)
        self.dev.release(2)
        started, worst, before = self.watch_radio_start(key="gate_on")
        self.check(started, worst, before)
        self.dev.run(1000)
        self.assertEqual(self.dev.state()["view"], "TITAN")
        self.assertGreater(lit(self.dev.frame()), 0, "the TITAN screen did not come")

    def test_app_sync(self):
        started, worst, before = self.open_from_settings("APP_SYNC")
        self.dev.release(2)
        self.check(started, worst, before)
        self.dev.run(1000)
        state = self.dev.state()
        self.assertEqual(state["view"], "APP_SYNC")
        self.assertEqual(state["ota_update_allowed"], 0)

    def test_accept_view_after_a_firmware_update(self):
        self.dev.power_on(hold="both", pending=True)
        self.dev.release(1)
        self.dev.release(2)
        started, worst, before = self.watch_radio_start(steps=300)
        self.check(started, worst, before)
        self.assertEqual(self.dev.view, "FW_ACCEPT")

    def test_update_mode_with_the_10_second_hold(self):
        self.dev.power_on(hold="both")
        self.dev.run(9500)
        started, worst, before = self.watch_radio_start()
        self.check(started, worst, before)
        self.assertEqual(self.dev.view, "UPDATE")

    def test_no_radio_start_when_the_screen_is_left_before_it(self):
        self.dev.boot()
        self.open_setting("APP_SYNC")  # The update view ignores "both buttons"; the app sync does not
        self.dev.press(2)
        self.dev.run(720)              # The long press: the app sync slides in, the radio is still off
        self.dev.release(2)
        self.assertEqual(self.dev.state()["ota_active"], 0)
        self.dev.click_both()          # The app menu replaces it
        self.assertEqual(self.dev.view, "LAUNCHER")
        self.dev.run(1500)
        state = self.dev.state()
        self.assertEqual(state["ota_active"], 0)
        self.assertEqual(state["ota_init_calls"], 0)


if __name__ == "__main__":
    unittest.main()
