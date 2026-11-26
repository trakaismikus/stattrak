"""The firmware must stay usable with bad data and with random button presses.

"Bad data" is the important case for an open-source firmware: a different firmware version
can store a value in flash that this version does not know. The device must not crash, and
the firmware update mode must stay reachable. If it is not reachable, the device is "bricked".

Set STATTRAK_FUZZ_SEEDS to a number to run more (or fewer) random sessions. Default: 4.
"""

import os
import random
import unittest

from helpers import SimTestCase, MAIN_VIEWS, MINUTE
from stattrak_sim import Device, SimCrash

EEPROM_KEYS = [
    "init", "total_kills", "total_deaths", "game_kills", "game_deaths", "battery_type",
    "show_battery", "brightness", "dim_screen", "dim_level", "auto_off", "counter", "sound",
    "low_bat", "device_model",
]

# Values that an other firmware version, or a damaged flash, can leave behind.
BAD_VALUES = [0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 65536, 256, 255, 100, 17, 9, 6, 5]

FUZZ_SEEDS = int(os.environ.get("STATTRAK_FUZZ_SEEDS", "4"))
FUZZ_ACTIONS = int(os.environ.get("STATTRAK_FUZZ_ACTIONS", "250"))


class BadFlashDataTest(SimTestCase):

    def test_normal_use_with_one_bad_value(self):
        for key in EEPROM_KEYS:
            for value in BAD_VALUES:
                with self.subTest(key=key, value=value):
                    with Device() as dev:
                        dev.boot(voltage=4000)
                        dev.power_cut()
                        dev.write_flash({key: value})

                        dev.boot(voltage=4000)
                        self.assertTrue(dev.is_on, "did not start: %s" % (dev.ended,))
                        for _ in range(len(MAIN_VIEWS)):
                            dev.click_both()
                            dev.click(1)
                            dev.click(2)
                            dev.long_press(2)
                        dev.run(3000)
                        self.assertTrue(dev.is_on, "switched off: %s" % (dev.ended,))

    def test_bad_value_is_replaced_with_the_default(self):
        defaults = {"auto_off": 0, "dim_screen": 3, "low_bat": 0, "battery_type": 0, "brightness": 8,
                    "dim_level": 2, "sound": 1, "counter": 0, "show_battery": 0}
        self.first_boot(**{key: 200 for key in defaults})
        self.dev.boot(voltage=4000)
        state = self.dev.state()
        for key, default in defaults.items():
            with self.subTest(key=key):
                self.assertEqual(state["eeprom." + key], default)
        self.dev.power_cut()
        flash = self.dev.read_flash()
        for key, default in defaults.items():
            with self.subTest(key=key, where="flash"):
                self.assertEqual(flash[key], default)

    def test_largest_valid_value_is_kept(self):
        largest = {"auto_off": 5, "dim_screen": 7, "low_bat": 4, "battery_type": 7, "brightness": 10,
                   "dim_level": 9, "sound": 1, "counter": 1, "show_battery": 1,
                   "total_kills": 0xFFFFFFFF, "game_deaths": 0xFFFFFFFF}
        self.first_boot(**largest)
        self.dev.boot(voltage=12000)  # 12 V: good for the 9.6 V NiMH battery type
        state = self.dev.state()
        for key, value in largest.items():
            with self.subTest(key=key):
                self.assertEqual(state["eeprom." + key], value)

    def test_value_that_an_older_firmware_did_not_store_gets_its_default(self):
        # An update can add a new stored value. The flash of the device does not have it yet.
        # It must get its default value, not 0.
        with Device() as dev:
            dev.boot(voltage=4000)
            dev.power_cut()
            before = dev.read_flash()
            self.assertEqual(before["init"], 1)
            dev.write_flash({"total_kills": 77}, remove=["brightness", "dim_level", "sound"])

            dev.boot(voltage=4000)
            self.assertTrue(dev.is_on, "did not start: %s" % (dev.ended,))
            state = dev.state()
            self.assertEqual(state["eeprom.brightness"], before["brightness"])
            self.assertEqual(state["eeprom.dim_level"], before["dim_level"])
            self.assertEqual(state["eeprom.sound"], before["sound"])
            self.assertEqual(state["eeprom.total_kills"], 77, "the other values must stay")

            # The defaults are also written to flash
            dev.run(5000)
            dev.power_cut()
            after = dev.read_flash()
            self.assertEqual(after["brightness"], before["brightness"])
            self.assertEqual(after["total_kills"], 77)

    def test_update_mode_is_reachable_with_one_bad_value(self):
        for key in EEPROM_KEYS:
            for value in BAD_VALUES:
                with self.subTest(key=key, value=value):
                    with Device() as dev:
                        dev.boot(voltage=4000)
                        dev.power_cut()
                        dev.write_flash({key: value})

                        # The rescue procedure: hold both buttons for 10 seconds at power-on
                        dev.power_on(hold="both", voltage=4000)
                        dev.run(10600)
                        self.assertTrue(dev.is_on, "switched off before update mode: %s" % (dev.ended,))
                        dev.release(1)
                        dev.release(2)
                        state = dev.state()
                        self.assertEqual(state["view"], "UPDATE")
                        self.assertEqual(state["ota_active"], 1)

                        # The device must stay on for a full transfer (3 minutes here)
                        dev.ota_transfer(3 * MINUTE)
                        dev.run(3 * MINUTE + 10000)
                        self.assertFalse(dev.is_on)
                        self.assertEqual(dev.ended[0], "reset", "transfer did not finish: %s" % (dev.ended,))

    def test_new_firmware_can_be_accepted_with_bad_values(self):
        rng = random.Random(1234)
        for round_no in range(12):
            flash = {key: rng.choice(BAD_VALUES) for key in rng.sample(EEPROM_KEYS, 5)}
            with self.subTest(flash=flash):
                with Device() as dev:
                    dev.boot(voltage=4000)
                    dev.power_cut()
                    dev.write_flash(flash, pending=1)

                    dev.boot(voltage=4000)
                    self.assertTrue(dev.is_on, "did not start: %s" % (dev.ended,))
                    state = dev.state()
                    self.assertEqual(state["view"], "FW_ACCEPT")
                    self.assertTrue(dev.ota_accept())

    def test_all_values_bad(self):
        rng = random.Random(99)
        for round_no in range(20):
            flash = {key: rng.choice(BAD_VALUES + [rng.getrandbits(32)]) for key in EEPROM_KEYS}
            with self.subTest(flash=flash):
                with Device() as dev:
                    dev.boot(voltage=4000)
                    dev.power_cut()
                    dev.write_flash(flash)
                    dev.power_on(hold="both", voltage=4000)
                    dev.run(10600)
                    self.assertTrue(dev.is_on, "switched off before update mode: %s" % (dev.ended,))
                    self.assertEqual(dev.view, "UPDATE")


class FlashFailureTest(SimTestCase):

    def test_device_works_when_flash_writes_fail(self):
        self.dev.boot()
        self.dev.nvs_fail(True)
        self.dev.click(1)
        self.dev.click(1)
        self.go_to_view("SETTINGS")
        self.dev.click(2)
        self.dev.long_press(2)
        self.dev.run(1000)
        self.assertTrue(self.dev.is_on)
        self.assertEqual(self.dev.state()["eeprom.total_kills"], 2)

    def test_factory_reset_when_flash_writes_fail(self):
        self.dev.boot()
        self.open_setting("FACTORY_RESET")
        self.dev.nvs_fail(True)
        self.dev.long_press(2)
        self.dev.long_press(2)
        self.dev.run(1000)
        self.assertTrue(self.dev.is_on)


class RandomUseTest(unittest.TestCase):
    """A "monkey" presses buttons at random. The firmware must not crash or hang."""

    def _random_session(self, seed):
        rng = random.Random(seed)
        log = []

        def note(text):
            log.append(text)

        dev = Device()
        try:
            dev.boot(voltage=4000)
            for _ in range(FUZZ_ACTIONS):
                if not dev.is_on:
                    note("-> session ended %s, start again" % (dev.ended,))
                    self.assertIn(dev.ended[0], ("poweroff", "reset"), "unexpected end")
                    dev.power_cut()
                    if rng.random() < 0.3:
                        flash = {rng.choice(EEPROM_KEYS): rng.choice(BAD_VALUES)}
                        note("bad flash %s" % flash)
                        dev.write_flash(flash)
                    dev.boot(voltage=4000)
                    continue

                action = rng.choice([
                    "click1", "click1", "click2", "click2", "both", "both", "long1", "long2",
                    "wait", "wait", "hold", "voltage", "ota", "accept", "raw", "longwait",
                    "nvsfail", "cut",
                ])
                note(action)

                if action == "click1":
                    dev.click(1, hold_ms=rng.randint(20, 150), settle_ms=rng.randint(0, 400))
                elif action == "click2":
                    dev.click(2, hold_ms=rng.randint(20, 150), settle_ms=rng.randint(0, 400))
                elif action == "both":
                    dev.click_both(hold_ms=rng.randint(30, 200), settle_ms=rng.randint(0, 600))
                elif action == "long1":
                    dev.long_press(1, hold_ms=rng.randint(700, 3000), settle_ms=rng.randint(0, 600))
                elif action == "long2":
                    dev.long_press(2, hold_ms=rng.randint(700, 3000), settle_ms=rng.randint(0, 600))
                elif action == "hold":
                    button = rng.choice([1, 2])
                    dev.press(button)
                    dev.run(rng.randint(0, 1500))
                    if dev.is_on:
                        dev.click(3 - button, hold_ms=rng.randint(20, 900), settle_ms=0)
                    if dev.is_on:
                        dev.release(button)
                elif action == "raw":
                    dev.command("btn %d %d" % (rng.choice([1, 2]), rng.choice([0, 1])))
                    dev.run(rng.randint(0, 300))
                elif action == "wait":
                    dev.run(rng.randint(0, 3000))
                elif action == "longwait":
                    if rng.random() < 0.2:
                        dev.run(rng.randint(1, 40) * MINUTE)
                elif action == "voltage":
                    dev.set_voltage(rng.choice([0, 1, 2500, 3000, 3300, 3500, 3700, 4200, 7400, 12600, 65535]))
                elif action == "ota":
                    if dev.state()["ota_active"]:
                        dev.ota_transfer(rng.randint(100, 8000))
                elif action == "accept":
                    if dev.state()["ota_active"] and rng.random() < 0.5:
                        dev.ota_accept()
                elif action == "nvsfail":
                    dev.nvs_fail(rng.random() < 0.3)
                elif action == "cut":
                    if rng.random() < 0.2:
                        dev.power_cut()
                        dev.boot(voltage=4000)

                if dev.is_on:
                    state = dev.state()
                    self.assertNotEqual(state["view"], "INVALID")
                    self.assertEqual(state["ota_double_init"], 0, "Bluetooth was started two times")

            # After all that, the rescue procedure must still work.
            dev.power_cut()
            dev.power_on(hold="both", voltage=4000)
            dev.run(10600)
            self.assertTrue(dev.is_on, "rescue start failed: %s" % (dev.ended,))
            self.assertEqual(dev.view, "UPDATE")
        except (SimCrash, AssertionError) as error:
            tail = "\n".join(log[-25:])
            raise AssertionError(
                "Random session with seed %d failed.\n%s\nLast actions:\n%s\n"
                "Run again: STATTRAK_FUZZ_SEEDS=%d python -m unittest tests.test_robustness.RandomUseTest"
                % (seed, error, tail, seed)
            ) from error
        finally:
            dev.close()

    def test_random_use(self):
        for seed in range(1, FUZZ_SEEDS + 1):
            with self.subTest(seed=seed):
                self._random_session(seed)


if __name__ == "__main__":
    unittest.main()
