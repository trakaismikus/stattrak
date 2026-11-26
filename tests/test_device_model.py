"""The device version (DIY / PEQ) and the battery type (docs/SAFETY.md, docs/AUDIT.md A4).

Two battery lists exist on devices: firmware 1.0.1 and older, and all firmware from "1.0.1 with
RCR123A". The stored number does not say which list it is from. So the firmware asks one time
"StatTrak version": PEQ is always 1xRCR123A, DIY sets the battery type again. Before the answer
the device does no battery check.
"""

import json
import os
import struct
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))

import design  # noqa: E402
from helpers import (SimTestCase, BATTERY_NONE, BATTERY_USB, BATTERY_RCR123A, BATTERY_LIPO_1S,  # noqa: E402
                     MODEL_UNSET, MODEL_DIY, MODEL_PEQ, SETTINGS_WITH_BATTERY)
from stattrak_sim import SETTING_ROWS  # noqa: E402
from test_apps import KILLS, KILL_DEAD, STATS  # noqa: E402
from test_data import DataTestCase, KIND_RESTORE, KIND_START_PIC, OK, app, picture, stbk, value, shows  # noqa: E402

BATTERY_LIPO_2S = 4
BATTERY_LIPO_3S = 5     # In the list of firmware 1.0.1 and older, 5 was "3xAA/AAA"
OTA_ACCEPTED = 4        # components/ble_ota/ota.h


def design_frame(name, screen_id):
    with open(os.path.join(REPO_ROOT, "designs", name), "r", encoding="utf-8") as handle:
        data = json.load(handle)
    assets = design.load_assets()
    clean, problems, _ = design.normalize(data, assets)
    assert problems == [], problems
    screen = [s for s in clean["screens"] if s["id"] == screen_id][0]
    return design.draw_frame(screen, assets)


def opens_app():
    """An app that adds a row at each "on open": the time, battery() and battery_mv()."""
    source = """app "OPENS"
data "T" "BAT" "MV"
var ok
on open {
  ok = log(time(), battery(), battery_mv())
}
on draw {
  num(0, 0, log_count(), 0, text, on, left)
}
"""
    return app.compile_source(source)["package"]


class DeviceModelTestCase(SimTestCase):

    def old_firmware_device(self, **flash):
        """A device with a firmware before this one: it has no device version in flash."""
        self.first_boot(**flash)
        self.dev.write_flash({}, remove=("device_model",))

    def model(self):
        return self.dev.state()["eeprom.device_model"]

    def battery(self):
        return self.dev.state()["eeprom.battery_type"]

    def open_row(self, name):
        """Settings with the cursor on the row `name`. It reads the row from the device: the rows
        change with the device version, so the count of open_setting() can be wrong after an answer."""
        self.go_to_view("SETTINGS")
        for _ in range(2 * len(SETTING_ROWS)):
            if SETTING_ROWS[self.dev.state()["settings_row"]] == name:
                return
            self.dev.click(2)
        self.fail("the row %s is not in Settings" % name)

    def setting_rows(self):
        """The rows of Settings, read from the device: down from the first row until it comes again."""
        self.go_to_view("SETTINGS")
        rows = []
        for _ in range(2 * len(SETTING_ROWS)):
            row = SETTING_ROWS[self.dev.state()["settings_row"]]
            if row in rows:
                break
            rows.append(row)
            self.dev.click(2)
        return rows


class QuestionTest(DeviceModelTestCase):

    def test_a_new_device_asks_for_its_version(self):
        self.dev.boot(prompts="keep")
        self.assertEqual(self.dev.view, "DEVICE_MODEL")
        self.assertEqual(self.model(), MODEL_UNSET)
        self.assertEqual(self.dev.frame(), design_frame("device-version.json", "versionQuestion"))

    def test_a_short_press_does_not_answer(self):
        self.dev.boot(prompts="keep")
        self.dev.click(1)
        self.dev.click(2)
        self.assertEqual(self.dev.view, "DEVICE_MODEL")
        self.assertEqual(self.model(), MODEL_UNSET)
        self.assertEqual(self.dev.state()["eeprom.total_kills"], 0)

    def test_the_hold_of_the_power_on_does_not_answer(self):
        # Button 1 stays down after the power-on: that hold started before the question
        self.dev.power_on(hold="both")
        self.dev.run(400)
        self.dev.release(2)
        self.dev.run(3000)
        self.assertEqual(self.dev.view, "DEVICE_MODEL")
        self.assertEqual(self.model(), MODEL_UNSET)
        self.dev.release(1)
        self.dev.run(1000)
        self.dev.long_press(1)      # A new hold answers
        self.assertEqual(self.model(), MODEL_DIY)

    def test_diy_sets_no_battery_type_and_warns(self):
        self.dev.boot(prompts="keep")
        self.dev.long_press(1)
        self.assertEqual(self.model(), MODEL_DIY)
        self.assertEqual(self.battery(), BATTERY_NONE)
        self.assertEqual(self.dev.view, "BATTERY_UNSET")
        self.assertEqual(self.dev.frame(), design_frame("battery-not-set.json", "batteryNotSet"))
        self.dev.long_press(1)      # LATER
        self.assertEqual(self.dev.view, "TOTAL")
        # The answer is in flash
        self.dev.power_cut()
        self.assertEqual(self.dev.read_flash()["device_model"], MODEL_DIY)

    def test_peq_is_rcr123a_without_a_warning(self):
        self.dev.boot(prompts="keep")
        self.dev.long_press(2)
        state = self.dev.state()
        self.assertEqual(state["eeprom.device_model"], MODEL_PEQ)
        self.assertEqual(state["eeprom.battery_type"], BATTERY_RCR123A)
        self.assertEqual(state["battery_type"], BATTERY_RCR123A)
        self.assertEqual(state["view"], "TOTAL")
        self.dev.power_cut()
        self.dev.boot(prompts="keep")
        self.assertEqual(self.dev.view, "TOTAL")

    def test_a_long_answer_does_not_act_on_the_next_screen(self):
        # The repeats of the hold (from 1.2 s) must not answer LATER / SET or count a kill
        self.dev.boot(prompts="keep")
        self.dev.long_press(1, hold_ms=3000)
        self.assertEqual(self.dev.view, "BATTERY_UNSET")
        self.dev.power_cut()
        self.dev.boot(prompts="keep")
        self.dev.long_press(1, hold_ms=3000)    # LATER, held long
        state = self.dev.state()
        self.assertEqual(state["view"], "TOTAL")
        self.assertEqual(state["eeprom.total_kills"], 0)

    def test_the_question_comes_one_time_for_each_power_on(self):
        self.dev.boot(prompts="keep")
        self.dev.click_both()               # The app menu: the way out without an answer
        self.assertEqual(self.dev.view, "LAUNCHER")
        self.dev.click_both()
        self.assertEqual(self.dev.view, "TOTAL")
        self.dev.run(5000)
        self.dev.click(1)
        state = self.dev.state()
        self.assertEqual(state["view"], "TOTAL")
        self.assertEqual(state["eeprom.total_kills"], 1)
        self.assertEqual(state["eeprom.device_model"], MODEL_UNSET)
        self.dev.power_cut()
        self.dev.boot(prompts="keep")
        self.assertEqual(self.dev.view, "DEVICE_MODEL")

    def test_power_off_works_on_the_question(self):
        self.dev.boot(prompts="keep")
        self.dev.long_press_both()
        self.assert_powered_off()

    def test_a_bad_value_in_flash_asks_again(self):
        self.first_boot(device_model=200)
        self.dev.boot(prompts="keep")
        self.assertEqual(self.dev.view, "DEVICE_MODEL")
        self.assertEqual(self.model(), MODEL_UNSET)


class UpdateFromOldFirmwareTest(DeviceModelTestCase):

    def test_no_battery_check_before_the_answer(self):
        # Firmware 1.0.1, battery "3xAA/AAA" = 5. In the list of today 5 is "11.1V(3S)": 4.0 V is
        # "critical" for it, and the device switched off 60 s after each start (AUDIT A4).
        self.old_firmware_device(battery_type=BATTERY_LIPO_3S, low_bat=3)
        self.dev.boot(voltage=4000, prompts="keep")
        self.assertEqual(self.dev.view, "DEVICE_MODEL")
        self.dev.run(3 * 60 * 1000)
        self.assertTrue(self.dev.is_on, "switched off: %s" % (self.dev.ended,))
        state = self.dev.state()
        self.assertEqual(state["view"], "DEVICE_MODEL")
        self.assertEqual(state["battery_type"], BATTERY_NONE)
        self.assertEqual(state["eeprom.battery_type"], BATTERY_LIPO_3S, "the stored type stays until the answer")

    def test_the_same_values_with_an_answer_do_switch_off(self):
        # The control of the test above: with the device version DIY the stored type is in use
        self.first_boot(battery_type=BATTERY_LIPO_3S)
        self.dev.boot(voltage=4000, prompts="keep")
        self.dev.run(70 * 1000)
        self.assert_powered_off()

    def test_diy_forgets_the_battery_type_of_the_old_firmware(self):
        self.old_firmware_device(battery_type=BATTERY_LIPO_2S, total_kills=321)
        self.dev.boot(voltage=7400, prompts="keep")
        self.dev.long_press(1)
        state = self.dev.state()
        self.assertEqual(state["eeprom.device_model"], MODEL_DIY)
        self.assertEqual(state["eeprom.battery_type"], BATTERY_NONE)
        self.assertEqual(state["eeprom.total_kills"], 321)
        self.assertEqual(state["view"], "BATTERY_UNSET")

    def test_peq_gets_rcr123a_from_each_old_value(self):
        for old in (BATTERY_NONE, BATTERY_USB, BATTERY_RCR123A, BATTERY_LIPO_1S, 7):
            with self.subTest(old=old):
                self.dev.power_cut()
                self.dev.write_flash({"init": 1, "battery_type": old}, remove=("device_model",))
                self.dev.boot(voltage=3900, prompts="keep")
                self.assertEqual(self.dev.view, "DEVICE_MODEL")
                self.dev.long_press(2)
                state = self.dev.state()
                self.assertEqual(state["eeprom.battery_type"], BATTERY_RCR123A)
                self.assertEqual(state["battery_type"], BATTERY_RCR123A)

    def test_the_accept_of_the_new_firmware_comes_first(self):
        # The first start after the transfer: "Verifying...", not the question. No battery check.
        self.old_firmware_device(battery_type=BATTERY_LIPO_3S)
        self.dev.boot(pending=True, voltage=4000, prompts="keep")
        self.assertEqual(self.dev.view, "FW_ACCEPT")
        self.dev.run(2 * 60 * 1000)
        self.assertTrue(self.dev.is_on, "switched off before the accept: %s" % (self.dev.ended,))
        state = self.dev.state()
        self.assertEqual(state["view"], "FW_ACCEPT")
        self.assertEqual(state["ota_active"], 1)
        self.assertEqual(state["eeprom.device_model"], MODEL_UNSET)
        self.assertEqual(state["eeprom.battery_type"], BATTERY_LIPO_3S, "no change before the accept")
        self.assertTrue(self.dev.ota_accept())
        self.dev.run(200)
        self.assertEqual(self.dev.state()["ota_state"], OTA_ACCEPTED)
        self.dev.long_press(1)      # OK
        self.dev.run(500)
        self.assertEqual(self.dev.view, "DEVICE_MODEL")

    def test_a_firmware_that_is_not_accepted_keeps_the_old_values(self):
        # Power off before the accept: the bootloader goes back to the old firmware. It must find
        # its battery type as it was.
        self.old_firmware_device(battery_type=BATTERY_LIPO_2S)
        self.dev.boot(pending=True, voltage=7400, prompts="keep")
        self.dev.click(1)
        self.dev.long_press(1)
        self.dev.long_press(2)
        self.assertEqual(self.dev.view, "FW_ACCEPT")
        self.dev.power_cut()
        flash = self.dev.read_flash()
        self.assertEqual(flash["battery_type"], BATTERY_LIPO_2S)
        self.assertEqual(flash.get("device_model", 0), MODEL_UNSET)


class RescueTest(DeviceModelTestCase):
    """The ways to the update mode must work for a device without a version, on the two new screens."""

    def test_hold_10_seconds_opens_the_update_mode(self):
        for model in (None, MODEL_DIY):
            with self.subTest(model=model):
                self.dev.power_cut()
                if model is None:
                    self.old_firmware_device()
                else:
                    self.first_boot()       # DIY, no battery type: the warning comes at the start
                self.dev.power_on(hold="both")
                self.dev.run(10600)
                state = self.dev.state()
                self.assertEqual(state["view"], "UPDATE")
                self.assertEqual(state["ota_active"], 1)
                self.assertEqual(state["ota_update_allowed"], 1)

    def test_settings_update_from_the_question(self):
        self.dev.boot(prompts="keep")
        self.assertEqual(self.dev.view, "DEVICE_MODEL")
        self.open_setting("FW_UPDATE")
        self.dev.long_press(2)
        state = self.dev.state()
        self.assertEqual(state["view"], "UPDATE")
        self.assertEqual(state["ota_active"], 1)

    def test_settings_update_from_the_warning(self):
        self.first_boot()
        self.dev.boot(prompts="keep")
        self.assertEqual(self.dev.view, "BATTERY_UNSET")
        self.open_setting("FW_UPDATE")
        self.dev.long_press(2)
        self.assertEqual(self.dev.view, "UPDATE")

    def test_a_full_update_works_without_an_answer(self):
        self.dev.boot(prompts="keep")
        self.open_setting("FW_UPDATE")
        self.dev.long_press(2)
        self.dev.ota_transfer(2000)
        self.dev.run(10000)
        self.assert_powered_off("reset")


class WarningTest(DeviceModelTestCase):

    def test_the_warning_comes_at_each_power_on_while_the_type_is_none(self):
        self.first_boot()
        for _ in range(2):
            self.dev.boot(prompts="keep")
            self.assertEqual(self.dev.view, "BATTERY_UNSET")
            self.dev.long_press(1)
            self.assertEqual(self.dev.view, "TOTAL")
            self.dev.run(5000)
            self.assertEqual(self.dev.view, "TOTAL", "one time for each power-on")
            self.dev.power_cut()

    def test_no_warning_with_a_battery_type(self):
        for battery in (BATTERY_USB, BATTERY_RCR123A, BATTERY_LIPO_1S):
            with self.subTest(battery=battery):
                self.dev.power_cut()
                self.first_boot(battery_type=battery)
                self.dev.boot(voltage=3900, prompts="keep")
                self.assertEqual(self.dev.view, "TOTAL")

    def test_set_opens_settings_on_the_row_battery(self):
        self.first_boot()
        self.dev.boot(prompts="keep")
        self.dev.long_press(2)      # SET
        state = self.dev.state()
        self.assertEqual(state["view"], "SETTINGS")
        self.assertEqual(SETTING_ROWS[state["settings_row"]], "BATTERY_TYPE")
        self.assertTrue(shows(self.dev.frame(), "BATTERY:"), "BATTERY:")
        self.dev.long_press(2, hold_ms=750)      # NONE -> USB/DC
        self.assertEqual(self.battery(), BATTERY_USB)
        self.assertTrue(shows(self.dev.frame(), "USB/DC"), "USB/DC")
        self.dev.power_cut()
        self.dev.boot(prompts="keep")
        self.assertEqual(self.dev.view, "TOTAL")

    def test_usb_dc_is_a_choice_without_a_warning(self):
        # The person switched the battery check off on purpose (USB power, a constant DC supply)
        self.first_boot(battery_type=BATTERY_USB, total_kills=12)
        self.dev.boot(prompts="keep")
        state = self.dev.state()
        self.assertEqual(state["view"], "TOTAL")
        self.assertEqual(state["eeprom.total_kills"], 12)


class BatteryInUseTest(DeviceModelTestCase):
    """Each screen uses the battery type that is in use, not the stored one."""

    def counter_frame(self, model, **flash):
        """The kill counter of a device with these flash values, opened through the app menu."""
        self.dev.power_cut()
        self.first_boot(**flash)
        if model is None:
            self.dev.write_flash({}, remove=("device_model",))
        self.dev.boot(voltage=3900, prompts="keep")
        if self.dev.view != "TOTAL":
            self.dev.click_both()
            self.assertEqual(self.dev.view, "LAUNCHER")
            self.dev.click_both()
        self.assertEqual(self.dev.view, "TOTAL")
        self.dev.run(1000)
        return self.dev.frame()

    def test_no_battery_bar_without_a_device_version(self):
        bar = self.counter_frame(MODEL_DIY, battery_type=BATTERY_LIPO_1S, show_battery=1)
        no_bar = self.counter_frame(MODEL_DIY, battery_type=BATTERY_LIPO_1S, show_battery=0)
        self.assertNotEqual(bar, no_bar, "the test must see the battery bar")
        unset = self.counter_frame(None, battery_type=BATTERY_LIPO_1S, show_battery=1)
        self.assertEqual(unset, no_bar)

    def test_peq_shows_the_battery_bar_with_an_other_stored_type(self):
        bar = self.counter_frame(MODEL_DIY, battery_type=BATTERY_RCR123A, show_battery=1)
        self.dev.power_cut()
        self.dev.write_flash({"device_model": MODEL_PEQ, "battery_type": BATTERY_NONE})
        self.dev.boot(voltage=3900, prompts="keep")
        self.assertEqual(self.dev.view, "TOTAL")
        self.dev.run(1000)
        self.assertEqual(self.dev.frame(), bar)


class SettingsTest(DeviceModelTestCase):

    def test_diy_has_the_row_battery(self):
        self.first_boot(battery_type=BATTERY_LIPO_1S)
        self.dev.boot(voltage=3900)
        self.assertEqual(self.setting_rows(), SETTINGS_WITH_BATTERY)

    def test_peq_has_no_row_battery_but_the_battery_rows(self):
        self.dev.boot(voltage=3900, prompts="peq")
        rows = self.setting_rows()
        self.assertNotIn("BATTERY_TYPE", rows)
        self.assertEqual(rows, [row for row in SETTINGS_WITH_BATTERY if row != "BATTERY_TYPE"])

    def test_no_version_has_no_battery_rows(self):
        self.old_firmware_device(battery_type=BATTERY_LIPO_1S)
        self.dev.boot(voltage=3900, prompts="keep")
        rows = self.setting_rows()
        for row in ("BATTERY_TYPE", "SHOW_BATTERY", "LOW_BAT"):
            self.assertNotIn(row, rows)

    def test_peq_does_the_battery_check(self):
        self.dev.boot(voltage=3900, prompts="peq")
        self.dev.power_cut()
        self.dev.boot(voltage=3000)
        self.dev.run(70 * 1000)
        self.assert_powered_off()

    def test_peq_does_not_use_an_other_stored_type(self):
        self.dev.boot(voltage=3900, prompts="peq")
        self.dev.power_cut()
        self.dev.write_flash({"battery_type": BATTERY_LIPO_3S})
        self.dev.boot(voltage=3900)
        self.dev.run(70 * 1000)
        self.assertTrue(self.dev.is_on, "used the stored type: %s" % (self.dev.ended,))
        self.assertEqual(self.dev.state()["battery_type"], BATTERY_RCR123A)

    def test_hold_on_version_asks_again_and_keeps_the_data(self):
        self.first_boot(battery_type=BATTERY_LIPO_2S, total_kills=77, brightness=4)
        self.dev.boot(voltage=7400)
        self.open_setting("VERSION")
        self.dev.long_press(2)
        self.assertEqual(self.dev.view, "DEVICE_MODEL")
        self.dev.long_press(2)      # PEQ
        state = self.dev.state()
        self.assertEqual(state["eeprom.device_model"], MODEL_PEQ)
        self.assertEqual(state["eeprom.battery_type"], BATTERY_RCR123A)
        self.assertEqual(state["eeprom.total_kills"], 77)
        self.assertEqual(state["eeprom.brightness"], 4)
        self.assertEqual(state["view"], "TOTAL")

        # And back to DIY: the battery type is not set, the warning says so
        self.open_row("VERSION")
        self.dev.long_press(1)
        self.assertEqual(self.dev.view, "DEVICE_MODEL")
        self.dev.long_press(1)      # DIY
        state = self.dev.state()
        self.assertEqual(state["eeprom.device_model"], MODEL_DIY)
        self.assertEqual(state["eeprom.battery_type"], BATTERY_NONE)
        self.assertEqual(state["eeprom.total_kills"], 77)
        self.assertEqual(state["view"], "BATTERY_UNSET")

    def test_the_same_answer_again_changes_nothing(self):
        self.first_boot(battery_type=BATTERY_LIPO_2S)
        self.dev.boot(voltage=7400)
        self.open_setting("VERSION")
        self.dev.long_press(2)
        self.assertEqual(self.dev.view, "DEVICE_MODEL")
        self.dev.long_press(1)      # DIY again
        state = self.dev.state()
        self.assertEqual(state["eeprom.battery_type"], BATTERY_LIPO_2S)
        self.assertEqual(state["battery_type"], BATTERY_LIPO_2S)
        self.assertEqual(state["view"], "TOTAL")

    def test_the_app_menu_leaves_the_question_of_settings(self):
        self.first_boot(battery_type=BATTERY_LIPO_2S)
        self.dev.boot(voltage=7400)
        self.open_setting("VERSION")
        self.dev.long_press(2)
        self.dev.click_both()
        self.assertEqual(self.dev.view, "LAUNCHER")
        state = self.dev.state()
        self.assertEqual(state["eeprom.device_model"], MODEL_DIY)
        self.assertEqual(state["eeprom.battery_type"], BATTERY_LIPO_2S)

    def test_factory_reset_asks_at_the_next_power_on(self):
        self.first_boot(battery_type=BATTERY_LIPO_2S, total_kills=9)
        self.dev.boot(voltage=7400)
        self.open_setting("FACTORY_RESET")
        self.dev.long_press(2)
        self.dev.long_press(2)      # "Do a Factory Reset?" yes
        self.dev.run(3000)
        state = self.dev.state()
        self.assertEqual(state["view"], "TOTAL")
        self.assertEqual(state["eeprom.device_model"], MODEL_UNSET)
        self.assertEqual(state["eeprom.battery_type"], BATTERY_NONE)
        self.assertEqual(state["battery_type"], BATTERY_NONE)
        self.dev.power_cut()
        self.dev.boot(voltage=7400, prompts="keep")
        self.assertEqual(self.dev.view, "DEVICE_MODEL")


class InstalledAppTest(DataTestCase):
    """An installed app as the first app of the menu: it opens after the start screens."""

    def first_app(self):
        package = opens_app()
        self.assert_installed(package)
        self.open_sync()
        for builtin in (KILLS, KILL_DEAD, STATS):
            self.assertEqual(self.cmd(0x0119, struct.pack("<IB", builtin, 1))[0], OK)   # SET_HIDDEN
        self.dev.power_cut()
        return struct.unpack_from("<I", package, 8)[0]

    def rows(self, app_id):
        self.assertEqual(self.dev.view, "APP_HOST")
        self.assertEqual(self.dev.state()["app_error"], 0, self.dev.state()["app_error_text"])
        self.open_sync()          # closes the app: the rows go to flash in the next loop
        return self.read_rows(app_id)

    def test_the_app_opens_one_time_after_the_warning(self):
        # Before: "on open", "on close" for the warning, and "on open" again after LATER
        app_id = self.first_app()           # DIY without a battery type
        self.dev.boot(prompts="keep")
        self.assertEqual(self.dev.view, "BATTERY_UNSET")
        self.dev.long_press(1)              # LATER
        self.assertEqual(len(self.rows(app_id)), 1)

    def test_the_app_opens_one_time_after_the_question(self):
        app_id = self.first_app()
        self.dev.write_flash({}, remove=("device_model",))
        self.dev.boot(voltage=3900, prompts="keep")
        self.assertEqual(self.dev.view, "DEVICE_MODEL")
        self.dev.long_press(2)              # PEQ
        rows = self.rows(app_id)
        self.assertEqual(len(rows), 1)
        self.assertTrue(0 <= rows[0][1] <= 100, rows)       # battery(): PEQ has a battery

    def test_an_app_gets_no_battery_without_a_device_version(self):
        app_id = self.first_app()
        self.dev.write_flash({"battery_type": BATTERY_LIPO_1S}, remove=("device_model",))
        self.dev.boot(voltage=3900, prompts="keep")
        self.dev.click_both()               # The app menu, then the app, without an answer
        self.dev.click_both()
        rows = self.rows(app_id)
        self.assertEqual([row[1:] for row in rows], [[-1, -1]])

    def test_an_app_gets_the_battery_of_peq_with_an_other_stored_type(self):
        app_id = self.first_app()
        self.dev.write_flash({"device_model": MODEL_PEQ, "battery_type": BATTERY_NONE})
        self.dev.boot(voltage=3900, prompts="keep")
        # The app opens at the power-on, before the first battery measurement: open it again
        self.dev.click_both()
        self.dev.click_both()
        rows = self.rows(app_id)
        self.assertEqual(len(rows), 2)
        self.assertNotEqual(rows[0][1], -1, rows)
        self.assertTrue(0 < rows[1][1] <= 100, rows)
        self.assertTrue(3800 <= rows[1][2] <= 4000, rows)


class StartPictureTest(DataTestCase):

    def test_the_warning_comes_before_the_start_picture(self):
        self.open_sync()
        self.transfer(KIND_START_PIC, picture())
        self.dev.power_cut()
        self.dev.boot(prompts="keep")
        self.assertEqual(self.dev.view, "BATTERY_UNSET")
        self.dev.long_press(1)      # LATER
        self.assertEqual(self.dev.view, "INTRO")
        self.dev.run(3000)
        self.assertEqual(self.dev.view, "TOTAL")
        self.dev.run(3000)
        self.assertEqual(self.dev.view, "TOTAL", "no second warning after the picture")


class BackupTest(DataTestCase):

    def restore(self, *values):
        self.open_sync()
        status, answer = self.transfer(KIND_RESTORE, stbk(*values))
        self.assertEqual(status, OK, answer)
        return tuple(answer)        # written, skipped

    def test_a_backup_never_changes_the_device_version(self):
        # The device of the test is DIY (the answer of boot())
        self.assertEqual(self.restore(value("device_model", MODEL_PEQ), value("brightness", 3)), (1, 1))
        state = self.dev.state()
        self.assertEqual(state["eeprom.device_model"], MODEL_DIY)
        self.assertEqual(state["eeprom.brightness"], 3)

    def test_diy_takes_the_battery_type_of_a_backup(self):
        self.assertEqual(self.restore(value("battery_type", BATTERY_LIPO_1S)), (1, 0))
        state = self.dev.state()
        self.assertEqual(state["eeprom.battery_type"], BATTERY_LIPO_1S)
        self.assertEqual(state["battery_type"], BATTERY_LIPO_1S)

    def test_peq_keeps_rcr123a(self):
        self.dev.power_cut()
        self.dev.write_flash({"device_model": MODEL_PEQ, "battery_type": BATTERY_RCR123A})
        self.dev.boot(voltage=3900)
        self.assertEqual(self.restore(value("battery_type", BATTERY_LIPO_2S), value("device_model", MODEL_DIY),
                                      value("brightness", 3)), (1, 2))
        state = self.dev.state()
        self.assertEqual(state["eeprom.device_model"], MODEL_PEQ)
        self.assertEqual(state["eeprom.battery_type"], BATTERY_RCR123A)
        self.assertEqual(state["battery_type"], BATTERY_RCR123A)
        self.assertEqual(state["eeprom.brightness"], 3)

    def test_no_version_does_not_take_a_battery_type(self):
        self.dev.power_cut()
        self.dev.write_flash({}, remove=("device_model",))
        self.dev.boot(voltage=7400, prompts="keep")
        self.assertEqual(self.restore(value("battery_type", BATTERY_LIPO_2S)), (0, 1))
        state = self.dev.state()
        self.assertEqual(state["eeprom.device_model"], MODEL_UNSET)
        self.assertEqual(state["battery_type"], BATTERY_NONE)


if __name__ == "__main__":
    import unittest
    unittest.main()
