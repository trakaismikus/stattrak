"""RESPAWN TIMER (apps/respawn/) on the real firmware (the simulator).

The app is installed like the web updater does it. The test sets the time with the buttons, runs it
down, and checks the alarm: on the screen of the app, and as the background app over the KILLS +
DEAD screen (popup(), docs/APPS.md section 4.6).
"""

import os
import struct
import unittest

from test_background import BackgroundTestCase
from test_data import app

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PACKAGE = app.compile_file(os.path.join(REPO_ROOT, "apps", "respawn", "respawn.sts"))["package"]
APP_ID = struct.unpack_from("<I", PACKAGE, 8)[0]


def lit(frame):
    return sum(row.count("#") for row in frame)


class RespawnTest(BackgroundTestCase):

    def no_error(self):
        state = self.dev.state()
        self.assertEqual(state["app_error"], 0, state["app_error_text"])

    def set_five_seconds(self):
        """Edit: 0:10:00 -> 0:00:05 with the buttons, then the check mark"""
        self.dev.long_press(1)               # Edit, the first minutes digit is selected
        self.dev.click(1)                    # 1 -> 0
        for _ in range(3):
            self.dev.long_press(2)           # To the last seconds digit
        for _ in range(5):
            self.dev.click(2)
        self.dev.long_press(2)               # The check mark
        self.dev.click(2)                    # Saved

    def test_on_the_screen_warning_edit_run_and_alarm(self):
        self.assert_installed(PACKAGE)
        self.leave_sync()
        self.open_app(APP_ID)
        warning = self.dev.frame()
        self.dev.click(2)                    # Closes the warning only: the time does not start
        self.assertNotEqual(self.dev.frame(), warning)
        self.set_five_seconds()
        kills = self.dev.state()["eeprom.game_kills"]
        self.dev.click(2)                    # Start
        self.dev.run(3000)
        self.no_error()
        clicks = self.dev.state()["clicks"]
        self.dev.run(3000)                   # The time is up: the chime plays, the screen flashes
        self.assertGreater(self.dev.state()["clicks"], clicks)
        frames = set()
        for _ in range(4):
            self.dev.run(130)
            frames.add(lit(self.dev.frame()))
        self.assertGreater(max(frames) - min(frames), 1500, frames)   # White, then black
        self.dev.click(1)                    # Stops the alarm
        clicks = self.dev.state()["clicks"]
        self.dev.run(3000)
        self.assertEqual(self.dev.state()["clicks"], clicks)
        self.assertEqual(self.dev.state()["eeprom.game_kills"], kills)
        self.assertEqual(self.dev.view, "APP_HOST")
        self.no_error()

    def test_in_the_background_it_comes_back_when_the_time_is_up(self):
        self.install_background(PACKAGE, APP_ID)
        self.open_app(APP_ID)
        before = self.dev.frame()
        self.dev.click(2)                    # The background app shows no warning: this starts it
        self.dev.click(2)                    # Pause
        self.assertNotEqual(self.dev.frame(), before)
        paused = self.dev.frame()
        self.dev.click(1)                    # Paused: reset was selected; now settings is
        left = lambda frame: [row[:13] for row in frame[0:11]]      # The top action (not the digits)
        self.assertNotEqual(left(self.dev.frame()), left(paused))   # It is white now
        self.dev.click(1)                    # Reset is selected again: a short press does no reset
        self.dev.long_press(1)               # Hold: reset = back to the full time, settings selected
        self.set_five_seconds()
        self.dev.click(2)                    # Start
        self.go_to_view("KILL_DEAD")
        kills = self.dev.state()["eeprom.game_kills"]
        self.dev.run(6000)
        self.assertEqual(self.dev.view, "APP_HOST")
        self.assertEqual(self.state("app_running"), APP_ID)
        self.dev.click(1)                    # Stops the alarm, and is no kill
        self.assertEqual(self.dev.state()["eeprom.game_kills"], kills)
        clicks = self.dev.state()["clicks"]
        self.dev.run(3000)
        self.assertEqual(self.dev.state()["clicks"], clicks)
        self.go_to_view("KILL_DEAD")         # Nothing rings: no popup
        self.dev.run(3000)
        self.assertEqual(self.dev.view, "KILL_DEAD")
        self.no_error()


if __name__ == "__main__":
    unittest.main()
