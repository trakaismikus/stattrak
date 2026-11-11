"""The app menu: both buttons open it, buttons 1 and 2 select an app, both buttons or 3 s open it."""

import unittest

from helpers import SimTestCase, MAIN_VIEWS


class AppMenuTest(SimTestCase):

    def selected(self):
        return self.dev.state()["launcher_sel"]

    def test_both_buttons_open_the_menu_with_the_current_app_selected(self):
        self.dev.boot()
        self.assertEqual(self.dev.view, "TOTAL")
        self.dev.click_both()
        self.assertEqual(self.dev.view, "LAUNCHER")
        self.assertEqual(self.selected(), "TOTAL")

    def test_both_buttons_in_the_menu_open_the_selected_app(self):
        self.dev.boot()
        self.dev.click_both()
        self.dev.click(2)
        self.assertEqual(self.selected(), "KILL_DEAD")
        self.dev.click_both()
        self.assertEqual(self.dev.view, "KILL_DEAD")
        # The menu opens again on the app that is open now
        self.dev.click_both()
        self.assertEqual(self.selected(), "KILL_DEAD")

    def test_button_2_goes_through_the_apps_and_wraps_around(self):
        self.dev.boot()
        self.dev.click_both()
        seen = [self.selected()]
        for _ in range(len(MAIN_VIEWS)):
            self.dev.click(2)
            seen.append(self.selected())
        self.assertEqual(seen, MAIN_VIEWS + [MAIN_VIEWS[0]])
        self.assertEqual(self.dev.view, "LAUNCHER")

    def test_button_1_goes_back_and_wraps_to_settings(self):
        self.dev.boot()
        self.dev.click_both()
        self.dev.click(1)
        self.assertEqual(self.selected(), "SETTINGS")
        self.dev.click(1)
        self.assertEqual(self.selected(), "STATS")

    def test_settings_is_three_presses_away(self):
        # The way to "Firmware: UPDATE": both, button 1, both
        self.dev.boot()
        self.dev.click_both()
        self.dev.click(1)
        self.dev.click_both()
        self.assertEqual(self.dev.view, "SETTINGS")

    def test_the_selected_app_opens_after_3_seconds_without_a_press(self):
        self.dev.boot()
        self.dev.click_both()
        self.dev.click(2)
        self.dev.click(2)
        self.dev.run(2000)  # 2.3 s after the last press (click settles 300 ms)
        self.assertEqual(self.dev.view, "LAUNCHER")
        self.dev.run(1500)
        self.assertEqual(self.dev.view, "STATS")

    def test_each_press_starts_the_3_seconds_again(self):
        self.dev.boot()
        self.dev.click_both()
        for _ in range(4):
            self.dev.run(2000)
            self.dev.click(2)
        self.assertEqual(self.dev.view, "LAUNCHER")
        self.assertEqual(self.selected(), "TOTAL")

    def test_the_menu_does_not_count_kills_or_deaths(self):
        self.dev.boot()
        for _ in range(3):
            self.dev.click_both()
            self.dev.click(1)
            self.dev.click(2)
            self.dev.click(2)
            self.dev.click_both()
        state = self.dev.state()
        self.assertEqual(state["eeprom.total_kills"], 0)
        self.assertEqual(state["eeprom.total_deaths"], 0)
        self.assertEqual(state["eeprom.game_kills"], 0)

    def test_a_fast_press_after_both_buttons_is_not_a_kill(self):
        self.dev.boot()
        self.dev.click_both(settle_ms=100)
        self.dev.click(1)
        self.dev.run(500)
        state = self.dev.state()
        self.assertEqual(state["view"], "LAUNCHER")
        self.assertEqual(state["eeprom.total_kills"], 0)

    def test_a_very_fast_press_after_both_buttons_goes_to_the_menu(self):
        # Sol review S3: the press comes while the menu still slides in
        self.dev.boot()
        self.dev.click_both(settle_ms=30)
        self.dev.click(1, hold_ms=40)
        self.dev.run(500)
        state = self.dev.state()
        self.assertEqual(state["view"], "LAUNCHER")
        self.assertEqual(state["eeprom.total_kills"], 0)

    def test_a_held_button_stops_the_3_seconds(self):
        # Sol review S1: the menu must not close while a button is down
        self.dev.boot()
        self.dev.click_both()
        self.dev.run(2400)          # 2.9 s after the menu was open
        self.dev.press(2)
        self.dev.run(400)           # Held past the 3 s
        self.assertEqual(self.dev.view, "LAUNCHER")
        self.dev.release(2)
        self.dev.run(400)
        state = self.dev.state()
        self.assertEqual(state["view"], "LAUNCHER")
        self.assertEqual(state["launcher_sel"], "KILL_DEAD")
        self.assertEqual(state["eeprom.total_kills"], 0)
        self.dev.run(3300)          # 3 s after the move (the press comes 180 ms after the release)
        self.assertEqual(self.dev.view, "KILL_DEAD")

    def test_both_buttons_while_the_menu_closes_keep_it_open(self):
        # Sol review S2: the menu closes by itself, both buttons during the close reopen it
        self.dev.boot()
        self.dev.click_both()
        self.dev.run(3000 - 500 + 250 + 20)   # Just after the auto-open started
        self.dev.click_both(hold_ms=40, settle_ms=0)
        self.dev.run(1000)
        self.assertEqual(self.dev.view, "LAUNCHER")
        self.dev.run(2500)
        self.assertEqual(self.dev.view, "TOTAL")

    def test_a_press_after_opening_an_app_counts_in_the_app(self):
        self.dev.boot()
        self.dev.click_both()
        self.dev.click(2)
        self.dev.click_both(settle_ms=300)
        self.dev.click(2)  # Death in KILLS + DEAD
        state = self.dev.state()
        self.assertEqual(state["view"], "KILL_DEAD")
        self.assertEqual(state["eeprom.game_deaths"], 1)
        self.assertEqual(state["eeprom.game_kills"], 0)

    def test_fast_both_both_goes_back_to_the_same_app(self):
        self.dev.boot()
        self.dev.click_both(settle_ms=50)
        self.dev.click_both()
        self.assertEqual(self.dev.view, "TOTAL")
        self.dev.click(1)
        self.assertEqual(self.dev.state()["eeprom.total_kills"], 1)

    def test_long_press_on_both_buttons_in_the_menu_switches_off(self):
        self.dev.boot()
        self.dev.click_both()
        self.dev.long_press_both()
        self.assert_powered_off()

    def test_the_menu_shows_a_picture_and_the_page_bar(self):
        self.dev.boot()
        self.dev.click_both()
        frame = self.dev.frame()
        # The bar in segments: row 29 has a line under each app, row 28 only under the selected one
        self.assertGreater(frame[29].count("#"), 4 * 20)
        self.assertGreater(frame[28].count("#"), 20)
        self.assertLess(frame[28].count("#"), 40)
        # The title picture
        self.assertGreater(sum(row.count("#") for row in frame[2:26]), 100)

    def test_the_bar_follows_the_selected_app(self):
        self.dev.boot()
        self.dev.click_both()
        first = self.dev.frame()[28].index("#")
        self.dev.click(2)
        self.dev.run(300)
        second = self.dev.frame()[28].index("#")
        self.assertGreater(second, first + 20)


if __name__ == "__main__":
    unittest.main()
