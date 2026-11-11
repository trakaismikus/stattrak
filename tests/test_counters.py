"""Kill and death counters: the main function of the product."""

import unittest

from helpers import SimTestCase
from stattrak_sim import Device


class TotalViewTest(SimTestCase):

    def test_each_button_counts_one_kill(self):
        self.dev.boot()
        self.dev.click(1)
        state = self.dev.state()
        self.assertEqual(state["eeprom.total_kills"], 1)
        self.assertEqual(state["eeprom.game_kills"], 1)
        self.dev.click(2)
        state = self.dev.state()
        self.assertEqual(state["eeprom.total_kills"], 2)
        self.assertEqual(state["eeprom.game_kills"], 2)
        self.assertEqual(state["eeprom.total_deaths"], 0)

    def test_three_fast_presses_count_three_kills(self):
        self.dev.boot()
        for _ in range(3):
            self.dev.press(1)
            self.dev.run(60)
            self.dev.release(1)
            self.dev.run(60)
        self.dev.run(400)
        self.assertEqual(self.dev.state()["eeprom.total_kills"], 3)

    def test_long_press_removes_one_kill(self):
        self.boot_with(total_kills=5, game_kills=2)
        self.dev.long_press(1)
        state = self.dev.state()
        self.assertEqual(state["eeprom.total_kills"], 4)
        self.assertEqual(state["eeprom.game_kills"], 1)

    def test_counter_does_not_go_below_zero(self):
        self.boot_with(total_kills=1, game_kills=0)
        self.dev.long_press(2)
        self.dev.long_press(2)
        state = self.dev.state()
        self.assertEqual(state["eeprom.total_kills"], 0)
        self.assertEqual(state["eeprom.game_kills"], 0)

    def test_hold_removes_one_kill_each_half_second(self):
        self.boot_with(total_kills=10, game_kills=10)
        # Long press at 0.7 s, then a repeat at 1.2 s, 1.7 s and 2.2 s
        self.dev.long_press(1, hold_ms=2300)
        self.assertEqual(self.dev.state()["eeprom.total_kills"], 6)

    def test_counter_stays_after_power_cut(self):
        self.dev.boot()
        self.dev.click(1)
        self.dev.click(1)
        self.dev.power_cut()  # No clean power-off
        self.assertEqual(self.dev.read_flash()["total_kills"], 2)
        self.dev.boot()
        self.assertEqual(self.dev.state()["eeprom.total_kills"], 2)

    def test_one_kill_is_two_flash_writes(self):
        # Flash memory wears out. One kill must not write more than the two counters.
        self.dev.boot()
        before = self.dev.state()["nvs_writes"]
        self.dev.click(1)
        self.assertEqual(self.dev.state()["nvs_writes"] - before, 2)

    def test_button_press_makes_a_click_sound(self):
        self.dev.boot()
        before = self.dev.state()["clicks"]
        self.dev.click(1)
        state = self.dev.state()
        self.assertEqual(state["clicks"], before + 1)
        self.assertEqual(state["click_freq"], 1000)

    def test_no_sound_when_sound_is_off(self):
        self.boot_with(sound=0)
        before = self.dev.state()["clicks"]
        self.dev.click(1)
        self.assertEqual(self.dev.state()["clicks"], before)

    def test_game_counter_mode_shows_game_kills(self):
        # A device in GAME mode with 7 game kills must show the same picture as a
        # device in TOTAL mode with 7 total kills.
        self.boot_with(counter=1, game_kills=7, total_kills=123)
        game_frame = self.dev.frame()

        with Device() as other:
            other.boot()
            other.power_cut()
            other.write_flash({"counter": 0, "total_kills": 7})
            other.boot()
            self.assertEqual(game_frame, other.frame())

    def test_display_changes_when_a_kill_is_counted(self):
        self.dev.boot()
        before = self.dev.frame()
        self.dev.click(1)
        self.dev.run(500)
        self.assertNotEqual(before, self.dev.frame())


class KillDeadViewTest(SimTestCase):

    def test_button_1_is_kill_and_button_2_is_death(self):
        self.dev.boot()
        self.go_to_view("KILL_DEAD")
        self.dev.click(1)
        self.dev.click(1)
        self.dev.click(2)
        state = self.dev.state()
        self.assertEqual(state["eeprom.game_kills"], 2)
        self.assertEqual(state["eeprom.total_kills"], 2)
        self.assertEqual(state["eeprom.game_deaths"], 1)
        self.assertEqual(state["eeprom.total_deaths"], 1)

    def test_long_press_removes_kill_or_death(self):
        self.boot_with(game_kills=3, total_kills=30, game_deaths=2, total_deaths=20)
        self.go_to_view("KILL_DEAD")
        self.dev.long_press(1)
        self.dev.long_press(2)
        state = self.dev.state()
        self.assertEqual(state["eeprom.game_kills"], 2)
        self.assertEqual(state["eeprom.total_kills"], 29)
        self.assertEqual(state["eeprom.game_deaths"], 1)
        self.assertEqual(state["eeprom.total_deaths"], 19)


class ViewCycleTest(SimTestCase):

    def test_both_buttons_go_through_the_main_views(self):
        self.dev.boot()
        seen = [self.dev.view]
        for _ in range(4):
            self.dev.click_both()
            seen.append(self.dev.view)
        self.assertEqual(seen, ["TOTAL", "KILL_DEAD", "STATS", "SETTINGS", "TOTAL"])

    def test_both_buttons_do_not_count_a_kill(self):
        self.dev.boot()
        for _ in range(4):
            self.dev.click_both()
        state = self.dev.state()
        self.assertEqual(state["eeprom.total_kills"], 0)
        self.assertEqual(state["eeprom.total_deaths"], 0)


if __name__ == "__main__":
    unittest.main()
