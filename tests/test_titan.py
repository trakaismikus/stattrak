"""The TITAN app: StatTrak and a GATE TITAN II Bluetooth (qa/titan-app/decisions.md).

The fake Titan of the simulator (sim/host/host_gate.c) answers like the GATE document says: the
PIN with "OK" or "ER", INFO_0_API only after a correct PIN. The simulator has no radio: these tests
check the firmware logic and the screens, not the Bluetooth connection itself.
"""

import struct
import unittest

from helpers import SimTestCase, MINUTE

SERIAL = "RD5ZEI"
PIN = "BAAAAB"
CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

# titan_alert_t (main/titan.h)
LINK_LOST, LOW_BATT, FIRE_LOCKED, MAG_EMPTY, LOW_AMMO = 0x01, 0x02, 0x04, 0x08, 0x10
# titan_link_t
LINK_READY, LINK_WRONG_PIN = 3, 4
# INFO_0_API byte 1 (main/gate.h)
FLAG_MAG_SENSOR, FLAG_NO_MAG, FLAG_BB_RESET = 0x01, 0x02, 0x80


def titan_cfg(serial=SERIAL, pin=PIN, mag=0, ammo_alert=2, battery=0, rounds=0, shots=0, reloads=0):
    """The flash blob "titan"/"cfg" (main/titan.c, _save)."""
    data = struct.pack("<B6s6sBBBHIHI", 1, serial.encode(), pin.encode(), mag, ammo_alert, battery,
                       rounds, shots, reloads, 0)
    return data.ljust(32, b"\0")


class TitanTest(SimTestCase):

    # --- Helpers

    def boot_paired(self, real_pin=None, **cfg):
        """A device that is paired with the Titan already; the Titan is near (with `real_pin`, or
        the saved PIN)."""
        self.first_boot()
        self.dev.write_blobs({"cfg": titan_cfg(**cfg)}, namespace="titan")
        self.dev.boot()
        self.dev.gate_add(SERIAL, real_pin or cfg.get("pin", PIN))

    def open_titan(self, settle_ms=1500):
        self.go_to_view("TITAN")
        self.dev.run(settle_ms)   # The soft radio start, then the connection
        return self.dev.state()

    def open_connected(self, **cfg):
        self.boot_paired(**cfg)
        state = self.open_titan()
        self.assertEqual(state["titan_screen"], "PLAY")
        self.assertEqual(state["titan_link"], LINK_READY)
        return state

    def enter_pin(self, letters, start="AAAAAA"):
        """The letter wheel: button 1 = next letter, hold 2 = this letter is OK."""
        for i, letter in enumerate(letters):
            steps = (CHARS.index(letter) - CHARS.index(start[i])) % len(CHARS)
            for _ in range(steps):
                self.dev.click(1, settle_ms=200)
            self.dev.long_press(2)

    def state(self):
        return self.dev.state()

    # --- Menu and radio

    def test_titan_is_in_the_menu_before_settings(self):
        self.dev.boot()
        self.assertEqual(self.state()["apps_menu"], "1,2,3,4,settings")
        self.assertEqual(self.state()["app.4.name"], "TITAN")

    def test_the_radio_is_on_only_while_titan_is_open(self):
        self.dev.boot()
        self.assertEqual(self.state()["gate_on"], 0)
        state = self.open_titan()
        self.assertEqual(state["gate_on"], 1)
        self.assertEqual(state["titan_screen"], "PAIR")
        self.assertEqual(state["ota_active"], 0, "TITAN does not start the update service")
        self.go_to_view("TOTAL")
        self.dev.run(500)
        self.assertEqual(self.state()["gate_on"], 0)

    def test_the_update_mode_works_after_titan(self):
        self.open_connected()
        self.open_setting("FW_UPDATE")
        self.dev.long_press(2)
        self.dev.run(1500)
        state = self.state()
        self.assertEqual(state["view"], "UPDATE")
        self.assertEqual(state["ota_active"], 1)
        self.assertEqual(state["ota_update_allowed"], 1)
        self.assertEqual(state["gate_on"], 0)
        self.assertEqual(state["gate_overlap"], 0, "the TITAN radio and the update service were on together")

    # --- Pairing

    def test_pairing_with_the_letter_wheel_and_a_wrong_pin(self):
        self.dev.boot()
        self.dev.gate_add("KQ81TA", "ZZZZZZ", rssi=-70)
        self.dev.gate_add(SERIAL, PIN, rssi=-52)
        self.open_titan()
        self.dev.long_press(2)                       # SEARCH
        self.assertEqual(self.state()["titan_screen"], "SEARCH")
        self.dev.run(10500)
        state = self.state()
        self.assertEqual(state["titan_screen"], "PICK")
        self.assertEqual(state["titan_found"], 2)
        self.dev.long_press(2)                       # The strongest signal is first: RD5ZEI
        self.assertEqual(self.state()["titan_screen"], "PIN")

        # Hold 1 = the previous letter (A -> 9), button 1 = the next one (9 -> A)
        self.dev.long_press(1)
        self.assertEqual(self.state()["titan_pin_entry"], "9AAAAA")
        self.dev.click(1, settle_ms=200)
        self.assertEqual(self.state()["titan_pin_entry"], "AAAAAA")

        self.enter_pin("BAAAAA")                     # Wrong: the last letter must be B
        self.dev.run(500)
        state = self.state()
        self.assertEqual(state["titan_screen"], "WRONG_PIN")
        self.assertEqual(state["titan_paired"], "", "a wrong PIN is not saved")
        self.dev.run(1600)
        state = self.state()
        self.assertEqual(state["titan_screen"], "PIN")
        self.assertEqual(state["titan_pin_entry"], "BAAAAA", "the letters stay: fix one letter, not all")
        self.assertEqual(state["titan_pin_pos"], 0)

        self.enter_pin("BAAAAB", start="BAAAAA")
        self.dev.run(1000)
        state = self.state()
        self.assertEqual(state["titan_screen"], "BATTERY")
        self.assertEqual(state["titan_paired"], SERIAL)
        self.dev.click(1, settle_ms=200)             # 2S LIPO -> 3S LIPO
        self.dev.long_press(2)
        state = self.state()
        self.assertEqual(state["titan_screen"], "PLAY")
        self.assertEqual(state["titan_batt"], 1)
        self.assertEqual(state["gate_other_commands"], 0, "StatTrak sends only the PIN")
        self.assertIn("blob.titan.cfg", state)

    def test_no_titan_found(self):
        self.dev.boot()
        self.open_titan()
        self.dev.long_press(2)
        self.dev.run(10500)
        self.assertEqual(self.state()["titan_screen"], "NONE_FOUND")
        self.dev.long_press(1)
        self.assertEqual(self.state()["titan_screen"], "PAIR")

    def test_a_paired_titan_connects_by_itself(self):
        state = self.open_connected()
        self.assertEqual(state["gate_authorized"], 1)

    def test_a_changed_pin_asks_for_the_pin_again(self):
        self.boot_paired(pin="AAAAAA", real_pin=PIN)
        self.open_titan(settle_ms=1000)
        self.assertEqual(self.state()["titan_screen"], "WRONG_PIN")
        self.dev.run(1600)
        state = self.state()
        self.assertEqual(state["titan_screen"], "PIN")
        self.assertEqual(state["titan_pin_entry"], "AAAAAA")
        self.enter_pin(PIN, start="AAAAAA")
        self.dev.run(1000)
        state = self.state()
        self.assertEqual(state["titan_screen"], "PLAY", "a new PIN for the paired Titan: no battery question")
        self.assertEqual(state["titan_link"], LINK_READY)

    # --- Count

    def test_count_up_without_mag_size(self):
        self.open_connected()
        self.dev.gate_info(fired=3)
        self.dev.gate_info(fired=2)
        self.dev.run(100)
        self.assertEqual(self.state()["titan_rounds"], 5)
        # The regular read (each 2 s) gives the last value again: no new shots
        self.dev.run(4500)
        self.assertEqual(self.state()["titan_rounds"], 5)
        self.assertEqual(self.state()["titan_shots"], 5)

    def test_count_down_low_ammo_empty_and_reload(self):
        self.open_connected(mag=3, rounds=30)        # 30 rounds, AMMO ALERT 20 % = 6
        self.dev.gate_info(fired=23)
        self.dev.run(100)
        state = self.state()
        self.assertEqual(state["titan_rounds"], 7)
        self.assertEqual(state["titan_alert"], 0)

        self.dev.gate_info(fired=1)
        self.dev.run(100)
        self.assertEqual(self.state()["titan_alert"], LOW_AMMO)
        self.dev.click(2)                            # Closes the alert, does not open SESSION
        state = self.state()
        self.assertEqual(state["titan_alert"], 0)
        self.assertEqual(state["titan_screen"], "PLAY")

        self.dev.gate_info(fired=10)                 # More shots than rounds: 0, not below
        self.dev.run(100)
        state = self.state()
        self.assertEqual(state["titan_rounds"], 0)
        self.assertEqual(state["titan_alert"], MAG_EMPTY)
        self.dev.click(1)                            # Closes the alert only
        self.assertEqual(self.state()["titan_rounds"], 0)
        self.dev.click(1)                            # Reload
        state = self.state()
        self.assertEqual(state["titan_rounds"], 30)
        self.assertEqual(state["titan_reloads"], 1)
        self.assertEqual(state["titan_alerts"], 0)

    def test_etu_reload_and_a_new_magazine_refill(self):
        self.open_connected(mag=3, rounds=30)
        self.dev.gate_info(fired=10)
        self.dev.gate_info(flags=FLAG_BB_RESET)      # The ETU reset its BB counter
        self.dev.run(100)
        self.assertEqual(self.state()["titan_rounds"], 30)

        self.dev.gate_info(fired=5, flags=0)
        self.dev.gate_info(flags=FLAG_MAG_SENSOR | FLAG_NO_MAG)   # Mag out
        self.dev.run(100)
        self.assertEqual(self.state()["titan_rounds"], 25)
        self.dev.gate_info(flags=FLAG_MAG_SENSOR)    # A new mag in
        self.dev.run(100)
        state = self.state()
        self.assertEqual(state["titan_rounds"], 30)
        self.assertEqual(state["titan_reloads"], 2)

    def test_power_off_saves_the_count(self):
        # Holding both buttons switches off without the deinit of the view (Sol review S2)
        self.open_connected(mag=3, rounds=30)
        self.dev.gate_info(fired=4)
        self.dev.run(100)
        self.dev.long_press_both()
        self.assert_powered_off()
        self.dev.boot()
        self.dev.gate_add(SERIAL, PIN)
        state = self.open_titan()
        self.assertEqual(state["titan_rounds"], 26)
        self.assertEqual(state["titan_shots"], 4)

    def test_a_reopened_count_is_uncertain_until_a_reload(self):
        # Also with a full mag: shots while the app was closed are not known (Sol review S3)
        state = self.open_connected(mag=3, rounds=30)
        self.assertEqual(state["titan_uncertain"], 1)
        self.dev.click(1)                            # Reload
        self.assertEqual(self.state()["titan_uncertain"], 0)
        self.go_to_view("TOTAL")
        self.dev.run(500)
        self.assertEqual(self.open_titan()["titan_uncertain"], 1)

    def test_count_and_session_are_saved_and_marked_uncertain(self):
        self.open_connected(mag=3, rounds=30)
        self.dev.gate_info(fired=4)
        self.dev.run(100)
        self.go_to_view("TOTAL")
        self.dev.run(500)
        state = self.open_titan()
        self.assertEqual(state["titan_rounds"], 26)
        self.assertEqual(state["titan_shots"], 4)
        self.assertEqual(state["titan_uncertain"], 1, "shots while the app was closed are not counted")

    # --- Alerts

    def test_low_battery_needs_three_seconds(self):
        self.open_connected(battery=0)               # 2S LIPO: under 7.0 V
        self.dev.gate_info(voltage_mv=6800)          # A drop while firing
        for _ in range(20):                          # Not in the first frames either (Sol review S1)
            self.dev.run(10)
            self.assertEqual(self.state()["titan_alerts"] & LOW_BATT, 0)
        self.dev.run(1000)
        self.dev.gate_info(voltage_mv=7500)
        self.dev.run(3000)
        self.assertEqual(self.state()["titan_alerts"] & LOW_BATT, 0)

        self.dev.gate_info(voltage_mv=6800)
        self.dev.run(3500)
        self.assertEqual(self.state()["titan_alert"], LOW_BATT)

    def test_no_battery_alert_without_a_measured_voltage(self):
        self.open_connected(battery=0)
        self.dev.gate_info(voltage_mv=0xFFFF)        # No battery type set in GCS
        self.dev.run(4000)
        self.assertEqual(self.state()["titan_alerts"], 0)

    def test_fire_locked(self):
        self.open_connected()
        self.dev.gate_info(locked=True)
        self.dev.run(100)
        self.assertEqual(self.state()["titan_alert"], FIRE_LOCKED)
        self.dev.gate_info(locked=False)
        self.dev.run(100)
        self.assertEqual(self.state()["titan_alerts"], 0, "the alert closes when the problem stops")

    def test_link_lost_and_back(self):
        self.open_connected(mag=3, rounds=30)
        self.dev.gate_range(SERIAL, False)
        for _ in range(20):                          # Not in the first frames either (Sol review S1)
            self.dev.run(10)
            self.assertEqual(self.state()["titan_alerts"] & LINK_LOST, 0)
        self.dev.run(3800)
        self.assertEqual(self.state()["titan_alert"], 0, "LINK LOST only after 5 s")
        self.dev.run(1500)
        state = self.state()
        self.assertEqual(state["titan_alert"], LINK_LOST)
        self.assertEqual(state["titan_uncertain"], 1)

        self.dev.gate_range(SERIAL, True)
        self.dev.run(11000)
        state = self.state()
        self.assertEqual(state["titan_link"], LINK_READY)
        self.assertEqual(state["titan_alert"], 0)
        self.assertEqual(state["titan_uncertain"], 1, "still uncertain until the next reload")
        self.dev.click(1)
        self.assertEqual(self.state()["titan_uncertain"], 0)

    def test_gun_activity_keeps_the_device_on(self):
        self.first_boot(auto_off=1)                  # 30 min
        self.dev.write_blobs({"cfg": titan_cfg()}, namespace="titan")
        self.dev.boot()
        self.dev.gate_add(SERIAL, PIN)
        self.open_titan()
        for _ in range(8):                           # 40 min with a shot each 5 min
            self.dev.run(5 * MINUTE)
            self.dev.gate_info(fired=1)
        self.dev.run(100)
        self.assertTrue(self.dev.is_on, "auto-off (30 min) switched the device off in a game")
        self.assertEqual(self.state()["titan_shots"], 8)

    # --- Settings

    def open_setup(self):
        self.dev.click_both()
        while self.state()["launcher_sel"] != "TITAN":
            self.dev.click(2)
        self.dev.long_press(2)
        self.assertEqual(self.dev.view, "TITAN_SETUP")
        self.assertEqual(self.state()["gate_on"], 0, "no radio in the settings")

    def test_mag_size_in_the_settings(self):
        self.boot_paired()
        self.open_setup()
        self.dev.long_press(2)                       # MAG SIZE: OFF -> 10
        self.dev.long_press(2)                       # -> 20
        self.assertEqual(self.state()["titan_mag"], 2)
        self.dev.long_press(1)                       # -> 10
        self.assertEqual(self.state()["titan_mag"], 1)
        self.dev.long_press(1)                       # -> OFF
        self.dev.long_press(1)                       # wraps -> 250
        self.assertEqual(self.state()["titan_mag"], 25)

    def test_forget_the_titan(self):
        self.boot_paired()
        self.open_setup()
        for _ in range(3):                           # MAG SIZE -> AMMO ALERT -> GUN BATT -> TITAN
            self.dev.click(2)
        self.dev.long_press(2)
        self.assertEqual(self.dev.view, "ASK")
        self.dev.long_press(2)                       # YES
        self.dev.run(500)
        state = self.state()
        self.assertEqual(state["view"], "TITAN_SETUP")
        self.assertEqual(state["titan_paired"], "")


if __name__ == "__main__":
    unittest.main()
