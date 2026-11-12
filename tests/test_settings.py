"""Settings view, game reset and factory reset."""

import unittest

from helpers import SimTestCase, BATTERY_LIPO_1S, MAX_BRIGHTNESS, SETTINGS_NO_BATTERY


class SettingsTest(SimTestCase):

    def test_change_counter_mode(self):
        self.dev.boot()
        self.open_setting("COUNTER")
        self.dev.long_press(2)
        self.assertEqual(self.dev.state()["eeprom.counter"], 1)
        self.dev.long_press(2)  # This setting goes around: GAME -> TOTAL
        self.assertEqual(self.dev.state()["eeprom.counter"], 0)

    def test_setting_is_stored_in_flash(self):
        self.dev.boot()
        self.open_setting("SOUND")
        self.dev.long_press(1)
        self.dev.run(200)
        self.dev.power_cut()
        self.assertEqual(self.dev.read_flash()["sound"], 0)

    def test_brightness_changes_the_display(self):
        self.dev.boot()
        self.open_setting("BRIGHTNESS")
        self.dev.long_press(1)
        state = self.dev.state()
        self.assertEqual(state["eeprom.brightness"], 7)
        self.assertEqual(state["brightness"], (MAX_BRIGHTNESS * 7) // 10)

    def test_setting_stops_at_first_and_last_value(self):
        self.dev.boot()
        self.open_setting("AUTO_OFF")
        self.dev.long_press(1)
        self.assertEqual(self.dev.state()["eeprom.auto_off"], 0)
        for _ in range(8):
            self.dev.long_press(2)
        self.assertEqual(self.dev.state()["eeprom.auto_off"], 5)

    def test_cursor_stops_at_first_and_last_setting(self):
        self.dev.boot()
        self.go_to_view("SETTINGS")
        self.dev.click(1)  # Already at the top
        for _ in range(len(SETTINGS_NO_BATTERY) + 5):
            self.dev.click(2)
        self.dev.run(1000)
        self.assertTrue(self.dev.is_on)
        self.assertEqual(self.dev.view, "SETTINGS")
        # The last setting (serial number) is read-only: a long press changes nothing.
        before = self.dev.state()
        self.dev.long_press(2)
        after = self.dev.state()
        changed = {k for k in before if k.startswith("eeprom.") and before[k] != after[k]}
        self.assertEqual(changed, set())

    def test_cursor_at_the_last_setting_with_all_settings_shown(self):
        # With a real battery type the list has all 14 settings. The draw code looks at the two
        # settings after the cursor. It must not read after the end of the list.
        self.boot_with(voltage=4000, battery_type=BATTERY_LIPO_1S)
        self.open_setting("SERIAL")
        self.dev.click(2)
        self.dev.run(1000)
        self.assertTrue(self.dev.is_on)
        self.assertEqual(self.dev.view, "SETTINGS")

    def test_battery_type_shows_the_battery_settings(self):
        self.boot_with(voltage=4000)
        self.open_setting("BATTERY_TYPE")
        for _ in range(BATTERY_LIPO_1S):
            self.dev.long_press(2)
        self.assertEqual(self.dev.state()["eeprom.battery_type"], BATTERY_LIPO_1S)

        # "Show Bat" is now in the list, after "Auto Off"
        self.open_setting("SHOW_BATTERY", battery=True)
        self.dev.long_press(2)
        self.assertEqual(self.dev.state()["eeprom.show_battery"], 1)

    def test_game_reset_keeps_the_total(self):
        self.boot_with(total_kills=50, total_deaths=40, game_kills=5, game_deaths=4)
        self.open_setting("GAME_RESET")
        self.dev.long_press(2)
        self.assertEqual(self.dev.view, "ASK")
        self.dev.long_press(2)  # YES
        state = self.dev.state()
        self.assertEqual(state["view"], "TOTAL")
        self.assertEqual(state["eeprom.game_kills"], 0)
        self.assertEqual(state["eeprom.game_deaths"], 0)
        self.assertEqual(state["eeprom.total_kills"], 50)
        self.assertEqual(state["eeprom.total_deaths"], 40)

    def test_game_reset_answer_no_changes_nothing(self):
        self.boot_with(game_kills=5)
        self.open_setting("GAME_RESET")
        self.dev.long_press(2)
        self.assertEqual(self.dev.view, "ASK")
        self.dev.long_press(1)  # NO
        state = self.dev.state()
        self.assertEqual(state["view"], "SETTINGS")
        self.assertEqual(state["eeprom.game_kills"], 5)

    def test_factory_reset(self):
        self.boot_with(total_kills=50, game_kills=5, sound=0, brightness=3, counter=1)
        self.open_setting("FACTORY_RESET")
        self.dev.long_press(2)
        self.assertEqual(self.dev.view, "ASK")
        self.dev.long_press(2)  # YES
        state = self.dev.state()
        self.assertEqual(state["view"], "TOTAL")
        self.assertEqual(state["nvs_erases"], 1)
        self.assertEqual(state["eeprom.init"], 1)
        self.assertEqual(state["eeprom.total_kills"], 0)
        self.assertEqual(state["eeprom.game_kills"], 0)
        self.assertEqual(state["eeprom.sound"], 1)
        self.assertEqual(state["eeprom.brightness"], 8)
        self.assertEqual(state["eeprom.counter"], 0)

        # The reset is still there after a power cut, and the device is not a "first start" again
        self.dev.power_cut()
        flash = self.dev.read_flash()
        self.assertEqual(flash["init"], 1)
        self.assertNotIn("total_kills", [k for k, v in flash.items() if v != 0])
        self.dev.power_on(hold="1")
        self.dev.run(100)
        self.assert_powered_off()

    def test_factory_reset_answer_no_changes_nothing(self):
        self.boot_with(total_kills=50)
        self.open_setting("FACTORY_RESET")
        self.dev.long_press(2)
        self.dev.long_press(1)  # NO
        state = self.dev.state()
        self.assertEqual(state["view"], "SETTINGS")
        self.assertEqual(state["eeprom.total_kills"], 50)
        self.assertEqual(state["nvs_erases"], 0)

    def test_factory_reset_needs_a_new_hold_and_no_short_press(self):
        # The hold that opens the question goes on (its repeats come every 0.5 s): it must not answer
        self.boot_with(total_kills=50)
        self.open_setting("FACTORY_RESET")
        self.dev.long_press(2, hold_ms=3000)
        self.assertEqual(self.dev.view, "ASK")
        self.dev.click(2)                  # A short press does not answer either
        self.dev.click(1)
        self.assertEqual(self.dev.view, "ASK")
        self.assertEqual(self.dev.state()["eeprom.total_kills"], 50)
        # The YES button fills while button 2 is held (x 102 .. 123, y 21 .. 29)
        def yes_pixels():
            return sum(row[103:123].count("#") for row in self.dev.frame()[22:29])
        empty = yes_pixels()
        self.dev.press(2)
        self.dev.run(350)
        self.assertGreater(yes_pixels(), empty + 30)
        self.dev.release(2)
        self.dev.run(500)
        self.assertEqual(self.dev.view, "ASK")         # Released before the end: no answer
        self.dev.long_press(1)             # NO
        state = self.dev.state()
        self.assertEqual(state["view"], "SETTINGS")
        self.assertEqual(state["eeprom.total_kills"], 50)

    def test_view_change_is_blocked_while_a_question_is_open(self):
        self.dev.boot()
        self.open_setting("FACTORY_RESET")
        self.dev.long_press(2)
        self.assertEqual(self.dev.view, "ASK")
        self.dev.click_both()
        self.assertEqual(self.dev.view, "ASK")


if __name__ == "__main__":
    unittest.main()
