"""Bluetooth commands for user features (main/ble_commands.c).

The simulator has no Bluetooth radio. A test plays the role of the phone: dev.ble_command()
sends a command to the firmware and returns the answer. The firmware code that handles the
command is the real code.

Add a test here for each command that you add.
"""

import struct
import unittest

from helpers import SimTestCase
from stattrak_sim import SimError

CMD_GET_COUNTERS = 0x0100

STATUS_OK = 0
STATUS_UNKNOWN = 1


class UserCommandTest(SimTestCase):

    def open_update_view(self):
        """Bluetooth is on in the update view."""
        self.open_setting("BLUETOOTH")
        self.dev.long_press(2)   # The Bluetooth view
        self.dev.long_press(1)   # UPDATE
        self.assertEqual(self.dev.view, "UPDATE")

    def test_get_counters(self):
        self.boot_with(total_kills=1337, total_deaths=42, game_kills=7, game_deaths=3)
        self.open_update_view()

        status, data = self.dev.ble_command(CMD_GET_COUNTERS)
        self.assertEqual(status, STATUS_OK)
        self.assertEqual(struct.unpack("<IIHH", data), (1337, 42, 7, 3))

    def test_get_counters_with_large_numbers(self):
        self.boot_with(total_kills=0xFFFFFFFF, total_deaths=70000, game_kills=70000, game_deaths=65535)
        self.open_update_view()

        status, data = self.dev.ble_command(CMD_GET_COUNTERS)
        self.assertEqual(status, STATUS_OK)
        # The game numbers have 2 bytes: they stop at 65535
        self.assertEqual(struct.unpack("<IIHH", data), (0xFFFFFFFF, 70000, 65535, 65535))

    def test_unknown_command_gets_an_answer(self):
        self.dev.boot()
        self.open_update_view()
        status, data = self.dev.ble_command(0x7777, b"\x01\x02\x03")
        self.assertEqual(status, STATUS_UNKNOWN)
        self.assertEqual(data, b"")

    def test_commands_do_not_work_while_bluetooth_is_off(self):
        self.dev.boot()
        self.assertEqual(self.dev.state()["ota_active"], 0)
        with self.assertRaises(SimError):
            self.dev.ble_command(CMD_GET_COUNTERS)

    def test_update_command_numbers_are_not_user_commands(self):
        self.dev.boot()
        self.open_update_view()
        with self.assertRaises(SimError):
            self.dev.ble_command(0x0003)  # "Accept firmware" is not a user command

    def test_many_commands_do_not_stop_the_device(self):
        self.boot_with(total_kills=5)
        self.open_update_view()
        answers = 0
        for _ in range(40):
            try:
                self.dev.command("ble cmd %04x" % CMD_GET_COUNTERS)
            except SimError:
                pass  # The queue is full: the phone must wait
        self.dev.run(500)
        while any(line.startswith("response cmd=") for line in self.dev.command("ble response")):
            answers += 1
        self.assertGreaterEqual(answers, 1)
        self.assertTrue(self.dev.is_on)
        self.assertEqual(self.dev.view, "UPDATE")

        # The commands work after that
        status, data = self.dev.ble_command(CMD_GET_COUNTERS)
        self.assertEqual(status, STATUS_OK)
        self.assertEqual(struct.unpack("<IIHH", data)[0], 5)

    def test_commands_work_while_a_new_firmware_waits_for_the_accept(self):
        self.first_boot(total_kills=9)
        self.dev.write_flash({}, pending=1)
        self.dev.boot()
        self.assertEqual(self.dev.view, "FW_ACCEPT")
        status, data = self.dev.ble_command(CMD_GET_COUNTERS)
        self.assertEqual(status, STATUS_OK)
        self.assertEqual(struct.unpack("<IIHH", data)[0], 9)


if __name__ == "__main__":
    unittest.main()
