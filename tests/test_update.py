"""Firmware update mode. If this breaks, a device cannot get new firmware any more.

The simulator has no Bluetooth. A test plays the role of the phone: "ota transfer" sends a
new firmware, "ota accept" accepts it. These tests check the firmware logic around the update:
how to get into the update mode, that nothing switches the device off during a transfer, and
that a new firmware waits for the "accept" before it goes to normal operation.
"""

import unittest

from helpers import SimTestCase, BATTERY_LIPO_1S, MINUTE

OTA_IDLE, OTA_STARTED, OTA_WRITING, OTA_FINISHED, OTA_ACCEPTED, OTA_ERROR = range(6)


class UpdateViewTest(SimTestCase):

    def open_update_view(self):
        self.open_setting("FW_UPDATE")
        self.dev.long_press(2)
        self.assertEqual(self.dev.view, "UPDATE")

    def test_update_mode_from_the_settings(self):
        self.dev.boot()
        self.assertEqual(self.dev.state()["ota_active"], 0, "Bluetooth must be off in normal operation")
        self.open_update_view()
        state = self.dev.state()
        self.assertEqual(state["ota_active"], 1)
        self.assertEqual(state["ota_state"], OTA_IDLE)

    def test_cancel_goes_back_and_switches_bluetooth_off(self):
        self.dev.boot()
        self.open_update_view()
        self.dev.long_press(1)  # CANCEL
        self.dev.run(500)
        state = self.dev.state()
        self.assertEqual(state["view"], "SETTINGS")
        self.assertEqual(state["ota_active"], 0)

    def test_update_mode_can_be_opened_again(self):
        self.dev.boot()
        for _ in range(3):
            self.open_update_view()
            self.dev.long_press(1)
            self.dev.run(500)
            self.assertEqual(self.dev.view, "SETTINGS")
        state = self.dev.state()
        self.assertEqual(state["ota_init_calls"], 3)
        self.assertEqual(state["ota_deinit_calls"], 3)
        self.assertEqual(state["ota_double_init"], 0, "Ota_init() was called while Bluetooth was already on")

    def test_bluetooth_goes_off_when_a_warning_opens_during_the_change_to_update_mode(self):
        # The low battery warning opens 60 s after the start. The change to the update view
        # takes 0.3 s. If the warning comes in this time, the update view is never shown, and
        # Bluetooth must not stay on.
        self.boot_with(voltage=3700, battery_type=BATTERY_LIPO_1S, low_bat=3)
        self.open_setting("FW_UPDATE")
        for offset in (50, 150, 250):
            with self.subTest(offset=offset):
                self.dev.power_cut()
                self.dev.boot(voltage=3700)
                self._setting_idx = 0
                self.open_setting("FW_UPDATE")
                now = self.dev.state()["t"]
                self.dev.run(60000 - 710 - offset - now)  # The long press event comes 710 ms after the press
                self.dev.long_press(2, hold_ms=900, settle_ms=1500)
                state = self.dev.state()
                self.assertEqual(state["view"], "BATTERY_LOW")
                self.assertEqual(state["ota_active"], 0, "Bluetooth stays on in normal operation")

                # The update mode must work after that (DISMISS: a hold)
                self.dev.long_press(1)
                self.dev.run(300)
                self.assertEqual(self.dev.view, "SETTINGS")
                self.dev.long_press(2)
                state = self.dev.state()
                self.assertEqual(state["view"], "UPDATE")
                self.assertEqual(state["ota_active"], 1)
                self.assertEqual(state["ota_double_init"], 0)

    def test_view_change_is_blocked_in_update_mode(self):
        self.dev.boot()
        self.open_update_view()
        self.dev.click_both()
        self.assertEqual(self.dev.view, "UPDATE")
        self.dev.click(1)
        self.dev.click(2)
        state = self.dev.state()
        self.assertEqual(state["view"], "UPDATE")
        self.assertEqual(state["eeprom.total_kills"], 0)

    def test_transfer_restarts_the_device(self):
        self.dev.boot()
        self.open_update_view()
        self.dev.ota_transfer(5000)
        self.dev.run(2500)
        state = self.dev.state()
        self.assertEqual(state["ota_state"], OTA_WRITING)
        self.assertGreater(state["ota_progress"], 20)
        self.assertLess(state["ota_progress"], 80)
        self.dev.run(10000)
        self.assert_powered_off("reset")
        self.assertEqual(self.dev.read_pending(), 1)

    def test_no_automatic_power_off_during_a_transfer(self):
        self.boot_with(voltage=3000, battery_type=BATTERY_LIPO_1S, auto_off=1)
        self.open_update_view()
        self.dev.ota_transfer(40 * MINUTE)
        self.dev.run(35 * MINUTE)  # Auto off is 30 minutes, and the battery is critical
        self.assertTrue(self.dev.is_on, "session ended with: %s" % (self.dev.ended,))
        state = self.dev.state()
        self.assertEqual(state["view"], "UPDATE")
        self.assertEqual(state["ota_state"], OTA_WRITING)


    def test_critical_battery_does_not_stop_a_transfer(self):
        # A device can read "critical" by mistake: a wrong battery type setting, or a bad firmware.
        # It must still be able to get a new firmware. So: no battery power-off during a transfer.
        self.boot_with(voltage=3000, battery_type=BATTERY_LIPO_1S)
        self.open_update_view()
        self.dev.ota_transfer(5 * MINUTE)
        self.dev.run(5 * MINUTE + 10000)
        self.assert_powered_off("reset")  # The transfer finished and the device started again
        self.assertEqual(self.dev.read_pending(), 1)

    def test_critical_battery_switches_off_after_a_failed_transfer(self):
        self.boot_with(voltage=3000, battery_type=BATTERY_LIPO_1S)
        self.open_update_view()
        self.dev.ota_transfer(10 * MINUTE)
        self.dev.run(2 * MINUTE)
        self.assertTrue(self.dev.is_on)
        self.dev.command("ota state %d 0" % OTA_ERROR)  # For example: the phone went away
        self.dev.run(70 * 1000)
        self.assert_powered_off("poweroff")

    def test_failed_transfer_shows_an_error_and_the_user_can_leave(self):
        self.dev.boot()
        self.open_update_view()
        self.dev.ota_transfer(60000)
        self.dev.run(5000)
        self.dev.command("ota state %d 0" % OTA_ERROR)
        self.dev.run(5000)
        self.assertTrue(self.dev.is_on, "an error must not restart the device: %s" % (self.dev.ended,))
        state = self.dev.state()
        self.assertEqual(state["view"], "UPDATE")
        self.assertEqual(state["ota_active"], 1, "Bluetooth must stay on, so the phone can try again")

        # A new transfer works after the error
        self.dev.ota_transfer(2000)
        self.dev.run(1000)
        self.assertEqual(self.dev.state()["ota_state"], OTA_WRITING)
        self.dev.run(10000)
        self.assert_powered_off("reset")

    def test_cancel_works_after_a_failed_transfer(self):
        self.dev.boot()
        self.open_update_view()
        self.dev.command("ota state %d 0" % OTA_ERROR)
        self.dev.run(1000)
        self.dev.long_press(1)  # CANCEL
        self.dev.run(500)
        state = self.dev.state()
        self.assertEqual(state["view"], "SETTINGS")
        self.assertEqual(state["ota_active"], 0)

    def test_accept_without_a_new_firmware_changes_nothing(self):
        self.dev.boot()
        self.open_update_view()
        self.assertFalse(self.dev.ota_accept())
        self.dev.run(500)
        state = self.dev.state()
        self.assertEqual(state["view"], "UPDATE")
        self.assertEqual(state["ota_state"], OTA_IDLE)

    def test_update_is_allowed_in_the_update_views(self):
        self.dev.boot()
        self.open_update_view()
        self.assertEqual(self.dev.state()["ota_update_allowed"], 1)


class NewFirmwareTest(SimTestCase):

    def update_firmware(self):
        """Do a full transfer. The device is off after it, with a new firmware in flash."""
        self.dev.boot()
        self.open_setting("FW_UPDATE")
        self.dev.long_press(2)
        self.dev.ota_transfer(2000)
        self.dev.run(10000)
        self.assert_powered_off("reset")

    def test_update_is_allowed_in_the_accept_view(self):
        # If a new firmware refuses updates before it is accepted, there is no way to replace it
        self.update_firmware()
        self.dev.boot()
        state = self.dev.state()
        self.assertEqual(state["view"], "FW_ACCEPT")
        self.assertEqual(state["ota_update_allowed"], 1)

    def test_new_firmware_waits_for_accept(self):
        self.update_firmware()
        self.dev.boot()
        state = self.dev.state()
        self.assertEqual(state["view"], "FW_ACCEPT")
        self.assertEqual(state["ota_active"], 1, "Bluetooth must be on, or the phone cannot accept")
        self.assertEqual(state["ota_pending"], 2)

        # No way out of this view before the accept
        self.dev.click(1)
        self.dev.click(2)
        self.dev.click_both()
        self.dev.long_press(1)
        self.assertEqual(self.dev.view, "FW_ACCEPT")

        self.assertTrue(self.dev.ota_accept())
        self.dev.run(200)
        self.assertEqual(self.dev.state()["ota_state"], OTA_ACCEPTED)
        self.dev.click(1)                  # A short press does not leave: OK needs a hold
        self.dev.run(500)
        self.assertEqual(self.dev.view, "FW_ACCEPT")
        self.dev.long_press(1)             # OK
        self.dev.run(500)
        state = self.dev.state()
        self.assertEqual(state["view"], "TOTAL")
        self.assertEqual(state["ota_pending"], 0)
        self.assertEqual(state["ota_active"], 0)

        # The next start is a normal start
        self.dev.power_cut()
        self.dev.boot()
        state = self.dev.state()
        self.assertEqual(state["view"], "TOTAL")
        self.assertEqual(state["rolled_back"], 0)

    def test_new_firmware_starts_with_one_button(self):
        # The device restarts into the new firmware without the user. One button must be enough.
        self.update_firmware()
        self.dev.power_on(hold="1")
        self.dev.run(400)
        self.dev.release(1)
        self.dev.run(1000)
        self.assertTrue(self.dev.is_on)
        self.assertEqual(self.dev.view, "FW_ACCEPT")

    def test_firmware_that_is_not_accepted_is_rolled_back(self):
        self.update_firmware()
        self.dev.boot()
        self.assertEqual(self.dev.view, "FW_ACCEPT")
        self.dev.long_press_both()  # The user switches off without accept
        self.assert_powered_off()

        self.dev.boot()
        state = self.dev.state()
        self.assertEqual(state["rolled_back"], 1, "the bootloader must go back to the old firmware")
        self.assertEqual(state["view"], "TOTAL")
        self.assertEqual(state["ota_pending"], 0)

    def test_counters_stay_after_an_update(self):
        self.first_boot(total_kills=42, total_deaths=7)
        self.update_firmware()
        self.dev.boot()
        self.dev.ota_accept()
        self.dev.run(200)
        self.dev.click(1)
        self.dev.run(500)
        state = self.dev.state()
        self.assertEqual(state["eeprom.total_kills"], 42)
        self.assertEqual(state["eeprom.total_deaths"], 7)

    def test_new_firmware_can_be_accepted_with_a_critical_battery(self):
        # The battery warning comes after 49 s. The phone has this time to accept.
        self.first_boot(battery_type=BATTERY_LIPO_1S)
        self.dev.write_flash({}, pending=1)
        self.dev.boot(voltage=3000)
        self.assertEqual(self.dev.view, "FW_ACCEPT")
        self.dev.ota_accept()
        self.dev.run(200)
        self.assertEqual(self.dev.state()["ota_pending"], 0)


if __name__ == "__main__":
    unittest.main()
