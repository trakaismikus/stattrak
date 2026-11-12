"""Installable apps (docs/APPS.md): the app list, install over Bluetooth, the app host, settings.

The test plays the role of the web updater: it opens Settings > "Apps: SYNC" with the buttons
and sends the app commands (dev.install_app). The firmware code is the real code.
"""

import random
import struct
import unittest
import zlib

from helpers import SimTestCase, BATTERY_LIPO_1S
from stattrak_sim import (APP_CMD_BEGIN, APP_CMD_DATA, APP_CMD_DELETE, APP_CMD_END, APP_CMD_INFO,
                          APP_CMD_LIST, APP_CMD_NAME, APP_CMD_SET_HIDDEN, APP_CMD_SET_ORDER,
                          APP_CMD_SETTING, APP_CMD_STATUS, APP_CMD_ASK, APP_CMD_DEVICE_NAME, SimError)
from stapp_build import Asm, build, finish, PERM_COUNTERS, PERM_SOUND

# Status codes (main/ble_commands.h)
OK = 0
ERROR = 2
NOT_IN_APP_SYNC = 3
INDEX = 4
NO_SPACE = 5
TOO_LARGE = 6
TOO_MANY = 7
OFFSET = 8
BAD_CRC = 9
BAD_PACKAGE = 10
WAITING = 11
BUILTIN = 12
LAST_SHOWN = 13

# Install states (STATUS)
IDLE, RECEIVING, WAITING_STATE, INSTALLED, REFUSED, TIMEOUT = range(6)

# App flags (LIST)
HIDDEN, BUILTIN_FLAG, HAS_SETTINGS, ERROR_FLAG = 1, 2, 4, 8

# Engine results (components/stapp/stapp.h)
STAPP_ERR_MAGIC = 2
STAPP_ERR_SYS = 12
STAPP_ERR_STEPS = 16

KILLS, KILL_DEAD, STATS = 1, 2, 3


def counter_app(app_id=0x1000, name="COUNTER", permissions=PERM_COUNTERS, settings=True, saved=0):
    """An app that shows game kills in big digits. Button 1 = +1 kill, button 2 = -1 kill.
    Global 0 counts the presses of button 1 (a saved variable when saved=1)."""
    asm = Asm()
    asm.handler("draw")
    asm.sys_drop("text", 2, 2, 0, 1, 1, 0)                    # "COUNT" at (2, 2), 5x5 font
    asm.push(40).push(6)
    asm.sys("counter", 2)                                     # game kills
    asm.sys_drop("bignum", 3, 3, 1)                           # bignum(40, 6, kills, 3, 3, 1)
    asm.halt()
    asm.handler("press1")
    asm.load(0).push(1).op("ADD").store(0)
    if permissions & PERM_COUNTERS:
        asm.sys_drop("add_counter", 2, 1)
    asm.halt()
    asm.handler("press2")
    if permissions & PERM_COUNTERS:
        asm.sys_drop("add_counter", 2, -1)
    asm.halt()
    return build(name=name, app_id=app_id, permissions=permissions, strings=["COUNT"], asm=asm, saved=saved,
                 settings=[("MODE", ["A", "B", "C"], 1), ("SOUND", ["ON", "OFF"], 0)] if settings else ())


def loop_app(app_id=0x2000, name="LOOP"):
    """An app with an endless loop in its draw handler."""
    asm = Asm()
    asm.handler("draw").label("top").jump("JMP", "top")
    return build(name=name, app_id=app_id, asm=asm)


def small_app(app_id, name=None, size=None):
    asm = Asm()
    asm.handler("draw")
    asm.sys_drop("fill", 0, 0, 10, 10, 1)
    asm.halt()
    return build(name=name or ("APP %d" % (app_id & 0xFF)), app_id=app_id, asm=asm, pad_to=size)


class AppTestCase(SimTestCase):

    def setUp(self):
        super().setUp()
        self.dev.boot()

    # --- Helpers

    def install(self, package, **kwargs):
        result = self.dev.install_app(package, **kwargs)
        return result

    def assert_installed(self, package, **kwargs):
        result = self.install(package, **kwargs)
        self.assertEqual(result["state"], INSTALLED, result)

    def cmd(self, cmd_id, data=b""):
        return self.dev.ble_command(cmd_id, data)

    def apps_list(self):
        text = self.dev.state()["apps_list"]
        return [int(x) for x in str(text).split(",") if x != ""]

    def menu(self):
        return str(self.dev.state()["apps_menu"]).split(",")

    def leave_sync(self):
        """Hold 1 in the app sync view: back to the settings. After an install, the first hold of
        button 1 is SYNC MORE (the screen "Waiting..."), the second one BACK."""
        self.dev.long_press(1)
        if self.dev.view == "APP_SYNC":
            self.dev.long_press(1)
        self.assertEqual(self.dev.view, "SETTINGS")

    def open_app(self, app_id):
        """Open an app with the app menu (both, button 2 until it is selected, both)."""
        self.dev.click_both()
        self.assertEqual(self.dev.view, "LAUNCHER")
        for _ in range(12):
            if self.dev.state()["launcher_app"] == app_id:
                break
            self.dev.click(2)
        self.assertEqual(self.dev.state()["launcher_app"], app_id)
        self.dev.click_both()
        self.dev.run(300)

    def open_update_view_from_menu(self, transfer=True):
        """The way to the firmware update: menu, SETTINGS, the row "Firmware", hold button 2.
        Then a phone starts to send a firmware (the update view must take it)."""
        self.dev.click_both()
        self.assertEqual(self.dev.view, "LAUNCHER")
        for _ in range(12):
            if self.dev.state()["launcher_sel"] == "SETTINGS":
                break
            self.dev.click(1)
        self.assertEqual(self.dev.state()["launcher_sel"], "SETTINGS")
        self.dev.click_both()
        self.assertEqual(self.dev.view, "SETTINGS")
        for _ in range(16):
            self.dev.click(2, settle_ms=200)   # The last row (it does not wrap)
        self.dev.click(1, settle_ms=200)       # Serial -> Version
        self.dev.click(1, settle_ms=200)       # Version -> Factory
        self.dev.click(1, settle_ms=200)       # Factory -> Firmware
        self.dev.long_press(2)
        self.assertEqual(self.dev.view, "UPDATE")
        self.assertEqual(self.dev.state()["ota_update_allowed"], 1)
        if transfer:
            self.dev.ota_transfer(60000)       # The phone can send a firmware
            self.dev.run(500)
            self.assertEqual(self.dev.state()["view"], "UPDATE")


class StoppedTransferTest(AppTestCase):
    """A page that stops in the middle of a transfer (closed, out of range) must not block the device."""

    def begin(self):
        self.dev.open_app_sync()
        package = build()
        status, _ = self.cmd(0x0113, struct.pack("<IHI", struct.unpack_from("<I", package, 8)[0], len(package),
                                                 zlib.crc32(package) & 0xFFFFFFFF))
        self.assertEqual(status, 0)
        self.assertEqual(self.cmd(APP_CMD_ASK, package[16:32])[0], 0)
        self.dev.long_press(2)                         # Yes: now the data can come
        self.assertEqual(self.cmd(0x0116)[1][0], 1)   # Receiving

    def test_the_transfer_ends_after_10_seconds_without_data(self):
        self.begin()
        self.dev.run(9000)
        self.assertEqual(self.cmd(0x0116)[1][0], 1)
        self.dev.run(1500)
        self.assertEqual(self.cmd(0x0116)[1][0], 0)   # Idle: a new install can start
        self.leave_sync()                              # And the view can be left again

    def test_hold_button_1_stops_the_transfer(self):
        self.begin()
        self.dev.long_press(1)
        state = self.dev.state()
        self.assertEqual(state["view"], "APP_SYNC")
        self.assertEqual(self.cmd(0x0116)[1][0], 0)
        self.leave_sync()


class BuiltinAppsTest(AppTestCase):

    def test_a_device_without_apps_has_the_three_builtin_apps(self):
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS])
        self.assertEqual(self.menu(), ["1", "2", "3", "settings"])
        # Nothing was written for the apps at start
        self.dev.power_cut()
        self.assertEqual(self.dev.read_blobs("apps"), {})

    def test_a_broken_list_is_the_default_list(self):
        self.dev.power_cut()
        self.dev.write_blobs({"list": b"\x07\x02garbage"})
        self.dev.boot()
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS])

    def test_a_list_entry_without_a_package_is_left_out(self):
        self.dev.power_cut()
        entries = [(KILLS, 0), (0x5555, 0), (KILL_DEAD, 0), (STATS, 0)]
        self.dev.write_blobs({"list": bytes([1, len(entries)]) + b"".join(struct.pack("<IB", i, f) for i, f in entries)})
        self.dev.boot()
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS])

    def test_a_list_without_the_builtin_apps_gets_them_back(self):
        self.dev.power_cut()
        self.dev.write_blobs({"list": bytes([1, 1]) + struct.pack("<IB", STATS, 0)})
        self.dev.boot()
        self.assertEqual(self.apps_list(), [STATS, KILLS, KILL_DEAD])

    def test_a_broken_package_in_flash_is_left_out(self):
        self.assert_installed(counter_app())
        self.dev.power_cut()
        package = bytearray(self.dev.read_blobs()["p00001000"])
        package[60] ^= 0xFF   # The CRC does not fit any more
        self.dev.write_blobs({"p00001000": bytes(package)})
        self.dev.boot()
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS])
        self.assertEqual(self.menu(), ["1", "2", "3", "settings"])


class InstallTest(AppTestCase):

    def test_install_and_run_an_app(self):
        result = self.install(counter_app())
        self.assertEqual(result["state"], INSTALLED, result)
        state = self.dev.state()
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS, 0x1000])
        self.assertEqual(state["app.4096.name"], "COUNTER")
        self.assertEqual(state["blob.apps.p00001000"], len(counter_app()))
        self.assertEqual(self.menu(), ["1", "2", "3", "4096", "settings"])

        self.leave_sync()
        self.open_app(0x1000)
        state = self.dev.state()
        self.assertEqual(state["view"], "APP_HOST")
        self.assertEqual(state["app_running"], 0x1000)
        self.assertEqual(state["app_error"], 0)
        # It draws: the text at the top left and the big digits
        frame = self.dev.frame()
        self.assertGreater(sum(row[:30].count("#") for row in frame[2:7]), 10)
        self.assertGreater(sum(row[40:].count("#") for row in frame[6:28]), 40)

    def test_the_app_changes_the_counters_with_the_permission(self):
        self.assert_installed(counter_app())
        self.leave_sync()
        self.open_app(0x1000)
        self.dev.click(1)
        self.dev.click(1)
        self.dev.click(1)
        self.dev.click(2)
        state = self.dev.state()
        self.assertEqual(state["eeprom.game_kills"], 2)
        self.assertEqual(state["eeprom.total_kills"], 2)   # A GAME counter changes its TOTAL too
        # Never below 0
        for _ in range(4):
            self.dev.click(2)
        state = self.dev.state()
        self.assertEqual(state["eeprom.game_kills"], 0)
        self.assertEqual(state["eeprom.total_kills"], 0)

    def test_one_press_changes_the_counters_at_most_4_times(self):
        # Fable review F3: each change is a flash write
        asm = Asm()
        asm.handler("press1")
        for _ in range(10):
            asm.sys_drop("add_counter", 2, 1)
        asm.halt()
        self.assert_installed(build(name="MANY", app_id=0x1100, permissions=PERM_COUNTERS, asm=asm))
        self.leave_sync()
        self.open_app(0x1100)
        self.dev.click(1)
        state = self.dev.state()
        self.assertEqual(state["eeprom.game_kills"], 4)
        self.assertEqual(state["eeprom.total_kills"], 4)

    def test_an_app_without_the_permission_is_refused(self):
        package = counter_app()
        # The same code with the permission bits taken out of the header
        package = bytearray(package)
        package[6] = 0
        result = self.install(finish(package))
        self.assertEqual((result["step"], result["status"]), ("end", BAD_PACKAGE), result)
        self.assertEqual(result["data"], bytes([STAPP_ERR_SYS]))
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS])

    def test_the_menu_shows_the_title_picture(self):
        self.assert_installed(counter_app())
        self.leave_sync()
        self.dev.click_both()
        for _ in range(12):
            if self.dev.state()["launcher_app"] == 0x1000:
                break
            self.dev.click(2)
        self.dev.run(400)
        frame = self.dev.frame()
        # The frame of the test picture: 80 x 24 at (24, 2)
        self.assertEqual(frame[2][24:104], "#" * 80)
        self.assertEqual(frame[25][24:104], "#" * 80)

    def test_the_person_says_no(self):
        result = self.install(counter_app(), answer="no")
        self.assertEqual(result["state"], REFUSED)
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS])
        self.dev.power_cut()
        self.assertNotIn("p00001000", self.dev.read_blobs())

    def test_no_answer_in_30_seconds_is_no(self):
        result = self.install(counter_app(), answer="wait")
        self.assertEqual(result["state"], TIMEOUT)
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS])

    def test_the_question_is_on_the_display(self):
        result = self.install(counter_app(), answer=None)
        self.assertEqual(result["status"], OK)
        state = self.dev.state()
        self.assertEqual(state["app_install_state"], WAITING_STATE)
        status, data = self.cmd(APP_CMD_STATUS)
        self.assertEqual(data, bytes([WAITING_STATE]))
        # Nothing can change the apps while the person answers, and no data comes before the answer
        self.assertEqual(self.cmd(APP_CMD_BEGIN, struct.pack("<IHI", 0x77, 400, 0))[0], WAITING)
        self.assertEqual(self.cmd(APP_CMD_ASK, b"OTHER".ljust(16, b"\0"))[0], WAITING)
        self.assertEqual(self.cmd(APP_CMD_DATA, bytes(16))[0], WAITING)
        self.assertEqual(self.cmd(APP_CMD_SET_HIDDEN, struct.pack("<IB", KILLS, 1))[0], WAITING)
        self.assertEqual(self.cmd(APP_CMD_DEVICE_NAME, b"\x01MIKUS".ljust(16, b"\0"))[0], WAITING)
        self.assertGreater(sum(row.count("#") for row in self.dev.frame()[10:30]), 100)
        self.dev.long_press(2)
        # Yes: the device takes the data now; nothing is installed before END
        self.assertEqual(self.cmd(APP_CMD_STATUS)[1], bytes([RECEIVING]))
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS])

    def test_no_data_before_the_question(self):
        self.dev.open_app_sync()
        package = counter_app()
        # ASK without BEGIN, DATA without yes, a bad name
        self.assertEqual(self.cmd(APP_CMD_ASK, b"COUNTER".ljust(16, b"\0"))[0], ERROR)
        self.assertEqual(self.cmd(APP_CMD_BEGIN, struct.pack("<IHI", 0x1000, len(package), zlib.crc32(package)))[0], OK)
        self.assertEqual(self.cmd(APP_CMD_DATA, struct.pack("<H", 0) + package[0:14])[0], ERROR)
        self.assertEqual(self.cmd(APP_CMD_END)[0], ERROR)
        self.assertEqual(self.cmd(APP_CMD_ASK, b"counter".ljust(16, b"\0"))[0], ERROR)     # small letters
        self.assertEqual(self.cmd(APP_CMD_ASK, b"COUNTER\0X".ljust(16, b"\0"))[0], ERROR)  # bytes after the end
        self.assertEqual(self.cmd(APP_CMD_STATUS)[1], bytes([IDLE]))
        self.assertEqual(self.cmd(APP_CMD_ASK, b"COUNTER".ljust(16, b"\0"))[0], OK)
        self.assertEqual(self.cmd(APP_CMD_STATUS)[1], bytes([WAITING_STATE]))

    def test_only_a_hold_that_starts_during_the_question_answers_it(self):
        self.dev.open_app_sync()
        package = counter_app()
        self.dev.press(2)                                   # Held before the question comes
        self.assertEqual(self.cmd(APP_CMD_BEGIN, struct.pack("<IHI", 0x1000, len(package), zlib.crc32(package)))[0], OK)
        self.assertEqual(self.cmd(APP_CMD_ASK, package[16:32])[0], OK)
        self.dev.run(1500)                                  # Long press and its repeats
        self.dev.release(2)
        self.dev.run(400)
        self.assertEqual(self.cmd(APP_CMD_STATUS)[1], bytes([WAITING_STATE]))
        # The hold fills the YES button (x 102 .. 123, y 21 .. 29) from the left
        def yes_pixels():
            return sum(row[103:123].count("#") for row in self.dev.frame()[22:29])
        empty = yes_pixels()
        self.dev.press(2)
        self.dev.run(350)
        half = yes_pixels()
        self.assertGreater(half, empty + 30)
        self.dev.run(500)
        self.dev.release(2)
        self.dev.run(400)
        self.assertEqual(self.cmd(APP_CMD_STATUS)[1], bytes([RECEIVING]))

    def test_after_the_install_sync_more_or_open(self):
        self.assert_installed(counter_app())
        frame = "\n".join(self.dev.frame())
        self.assertEqual(self.dev.view, "APP_SYNC")
        # A short press does nothing: the buttons on the display answer a hold
        self.dev.click(1, settle_ms=600)
        self.dev.click(2, settle_ms=600)
        self.assertEqual(self.cmd(APP_CMD_STATUS)[1], bytes([INSTALLED]))
        self.assertEqual(self.dev.view, "APP_SYNC")
        # SYNC MORE (hold 1): back to "Waiting", Bluetooth stays on
        self.dev.long_press(1)
        self.assertEqual(self.cmd(APP_CMD_STATUS)[1], bytes([IDLE]))
        self.assertEqual(self.dev.view, "APP_SYNC")
        self.assertNotEqual("\n".join(self.dev.frame()), frame)
        # The next install, then OPEN (button 2): the app runs and Bluetooth is off
        self.assert_installed(counter_app(app_id=0x2000, name="SECOND"), open_sync=False)
        self.dev.long_press(2, settle_ms=800)
        state = self.dev.state()
        self.assertEqual(state["view"], "APP_HOST")
        self.assertEqual(state["app_running"], 0x2000)
        self.assertEqual(state["ota_active"], 0)

    def test_the_name_must_be_the_name_of_the_question(self):
        result = self.install(counter_app(), name=b"OTHER APP")
        self.assertEqual((result["step"], result["status"]), ("end", BAD_PACKAGE), result)
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS])
        self.assertEqual(self.cmd(APP_CMD_STATUS)[1], bytes([IDLE]))

    def test_bad_crc(self):
        result = self.install(counter_app(), crc=0x12345678)
        self.assertEqual((result["step"], result["status"]), ("end", BAD_CRC), result)
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS])
        # The next install works
        self.assert_installed(counter_app())

    def test_bad_package(self):
        package = bytearray(counter_app())
        package[0:4] = b"XTAP"
        result = self.install(bytes(package))
        self.assertEqual((result["step"], result["status"]), ("end", BAD_PACKAGE), result)
        self.assertEqual(result["data"], bytes([STAPP_ERR_MAGIC]))
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS])

    def test_the_id_must_be_the_id_of_the_package(self):
        self.dev.open_app_sync()
        package = counter_app()
        status, _ = self.cmd(APP_CMD_BEGIN, struct.pack("<IHI", 0x9999, len(package), zlib.crc32(package)))
        self.assertEqual(status, OK)
        self.assertEqual(self.cmd(APP_CMD_ASK, package[16:32])[0], OK)
        self.dev.long_press(2)
        chunks = [(APP_CMD_DATA, struct.pack("<H", o) + package[o:o + 14].ljust(14, b"\0"))
                  for o in range(0, len(package), 14)]
        self.assertTrue(all(answer[0] == OK for answer in self.dev.ble_commands(chunks)))
        status, data = self.cmd(APP_CMD_END)
        self.assertEqual(status, BAD_PACKAGE)

    def test_builtin_ids_cannot_be_installed(self):
        self.dev.open_app_sync()
        self.assertEqual(self.cmd(APP_CMD_BEGIN, struct.pack("<IHI", KILLS, 400, 0))[0], ERROR)
        self.assertEqual(self.cmd(APP_CMD_BEGIN, struct.pack("<IHI", 0, 400, 0))[0], ERROR)

    def test_wrong_offset(self):
        self.dev.open_app_sync()
        package = counter_app()
        self.assertEqual(self.cmd(APP_CMD_BEGIN, struct.pack("<IHI", 0x1000, len(package), zlib.crc32(package)))[0], OK)
        self.assertEqual(self.cmd(APP_CMD_ASK, package[16:32])[0], OK)
        self.dev.long_press(2)
        self.assertEqual(self.cmd(APP_CMD_DATA, struct.pack("<H", 0) + package[0:14])[0], OK)
        status, data = self.cmd(APP_CMD_DATA, struct.pack("<H", 28) + package[28:42])
        self.assertEqual(status, OFFSET)
        self.assertEqual(struct.unpack("<H", data)[0], 14)
        # END before all data: the expected offset again
        status, data = self.cmd(APP_CMD_END)
        self.assertEqual(status, OFFSET)
        self.assertEqual(struct.unpack("<H", data)[0], 14)

    def test_too_large(self):
        self.dev.open_app_sync()
        self.assertEqual(self.cmd(APP_CMD_BEGIN, struct.pack("<IHI", 0x1000, 4097, 0))[0], TOO_LARGE)

    def test_no_space(self):
        for i in range(6):
            self.assert_installed(small_app(0x100 + i, size=4096))
        status, data = self.cmd(APP_CMD_INFO)
        self.assertEqual(status, OK)
        self.assertEqual(struct.unpack_from("<H", data, 2)[0], 0)   # free bytes
        result = self.install(small_app(0x200), open_sync=False)
        self.assertEqual((result["step"], result["status"]), ("begin", NO_SPACE))
        # A new version of an installed app has its own space
        self.assert_installed(small_app(0x100, name="NEW", size=4000))

    def test_too_many_apps(self):
        for i in range(8):
            self.assert_installed(small_app(0x100 + i))
        result = self.install(small_app(0x200), open_sync=False)
        self.assertEqual((result["step"], result["status"]), ("begin", TOO_MANY))
        self.assertEqual(len(self.apps_list()), 11)
        self.assertEqual(len(self.menu()), 12)

    def test_info_list_and_name(self):
        self.assert_installed(counter_app())
        status, data = self.cmd(APP_CMD_INFO)
        self.assertEqual(status, OK)
        engine, fmt, free, max_size, count, max_apps = struct.unpack("<BBHHBB", data)
        self.assertEqual((engine, fmt, max_size, count, max_apps), (1, 2, 4096, 1, 8))
        self.assertEqual(free, 24 * 1024 - len(counter_app()))

        status, data = self.cmd(APP_CMD_LIST, bytes([0]))
        self.assertEqual(status, OK)
        app_id, version, size, flags, settings = struct.unpack_from("<IHHBB", data)
        self.assertEqual((app_id, flags, data[10:13]), (KILLS, BUILTIN_FLAG, b"KIL"))

        status, data = self.cmd(APP_CMD_LIST, bytes([3]))
        app_id, version, size, flags, settings = struct.unpack_from("<IHHBB", data)
        self.assertEqual((app_id, version, size, flags, settings), (0x1000, 1, len(counter_app()), HAS_SETTINGS, 2))
        self.assertEqual(self.cmd(APP_CMD_LIST, bytes([4]))[0], INDEX)

        self.assertEqual(self.cmd(APP_CMD_NAME, struct.pack("<IB", 0x1000, 0)), (OK, b"COUNTER"))
        self.assertEqual(self.cmd(APP_CMD_NAME, struct.pack("<IB", KILL_DEAD, 0)), (OK, b"KILLS + DEAD"))
        self.assertEqual(self.cmd(APP_CMD_NAME, struct.pack("<IB", 0x1000, 1)), (OK, b""))
        self.assertEqual(self.cmd(APP_CMD_NAME, struct.pack("<IB", 0x4444, 0))[0], ERROR)


class DeviceNameTest(AppTestCase):
    """DEVICE_NAME: the Bluetooth name of "Apps: SYNC". The update views keep "StatTrak"."""

    def name_cmd(self, name=None):
        data = b"\x00" if name is None else b"\x01" + name
        return self.cmd(APP_CMD_DEVICE_NAME, data.ljust(16, b"\0")[:16])

    def test_read_write_and_back_to_stattrak(self):
        self.dev.open_app_sync()
        self.assertEqual(self.dev.state()["ble_name"], "StatTrak")
        self.assertEqual(self.name_cmd(), (OK, b"\x00StatTrak"))
        self.assertEqual(self.name_cmd(b"Mikus 1"), (OK, b"\x01Mikus 1\x00"))
        self.assertEqual(self.name_cmd(), (OK, b"\x01Mikus 1\x00"))
        # Used from the next start of Apps: SYNC, not now
        self.assertEqual(self.dev.state()["ble_name"], "StatTrak")
        self.leave_sync()
        self.dev.open_app_sync()
        self.assertEqual(self.dev.state()["ble_name"], "Mikus 1")
        # Kept after a power cut
        self.dev.power_cut()
        self.dev.boot()
        self.dev.open_app_sync()
        self.assertEqual(self.dev.state()["ble_name"], "Mikus 1")
        # All 0 = back to "StatTrak"
        self.assertEqual(self.name_cmd(b""), (OK, b"\x00StatTrak"))
        self.leave_sync()
        self.dev.open_app_sync()
        self.assertEqual(self.dev.state()["ble_name"], "StatTrak")

    def test_bad_names_are_refused(self):
        self.dev.open_app_sync()
        for bad in (b" MIKUS", b"MIKUS ", b"MI\x07KUS", b"MIKUS\x00X", b"ABCDEFGH" + b"I", b"\xffABC"):
            self.assertEqual(self.name_cmd(bad)[0], ERROR, bad)
        self.assertEqual(self.cmd(APP_CMD_DEVICE_NAME, b"\x02".ljust(16, b"\0"))[0], ERROR)
        self.assertEqual(self.name_cmd(b"ABCDEFGH"), (OK, b"\x01ABCDEFGH"))   # 8 characters is the most
        self.assertEqual(self.name_cmd(b"~!{}"), (OK, b"\x01~!{}\x00\x00\x00\x00"))

    def test_the_update_views_are_always_stattrak(self):
        self.dev.open_app_sync()
        self.assertEqual(self.name_cmd(b"MIKUS")[0], OK)
        self.leave_sync()
        self.dev.open_app_sync()
        self.assertEqual(self.dev.state()["ble_name"], "MIKUS")
        self.dev.long_press(1)
        self.open_update_view_from_menu(transfer=False)
        self.assertEqual(self.dev.state()["ble_name"], "StatTrak")

    def test_a_factory_reset_erases_the_name(self):
        self.dev.open_app_sync()
        self.assertEqual(self.name_cmd(b"MIKUS")[0], OK)
        self.leave_sync()
        self.dev.click(2, settle_ms=200)   # "Firmware"
        self.dev.click(2, settle_ms=200)   # "Factory"
        self.dev.long_press(2)
        self.assertEqual(self.dev.view, "ASK")
        self.dev.long_press(2)             # Yes
        self.dev.run(1000)
        self.dev.open_app_sync()
        self.assertEqual(self.dev.state()["ble_name"], "StatTrak")
        self.assertEqual(self.name_cmd(), (OK, b"\x00StatTrak"))


class ManageTest(AppTestCase):

    def test_delete(self):
        self.assert_installed(counter_app(saved=1))
        self.assertEqual(self.cmd(APP_CMD_DELETE, struct.pack("<I", 0x1000))[0], OK)
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS])
        self.assertEqual(self.cmd(APP_CMD_DELETE, struct.pack("<I", 0x1000))[0], ERROR)
        self.assertEqual(self.cmd(APP_CMD_DELETE, struct.pack("<I", KILLS))[0], BUILTIN)
        self.dev.power_cut()
        self.assertEqual(sorted(self.dev.read_blobs()), ["list"])

    def test_hide_and_show(self):
        self.assert_installed(counter_app())
        self.assertEqual(self.cmd(APP_CMD_SET_HIDDEN, struct.pack("<IB", KILL_DEAD, 1))[0], OK)
        self.assertEqual(self.menu(), ["1", "3", "4096", "settings"])
        self.assertEqual(self.cmd(APP_CMD_LIST, bytes([1]))[1][8], BUILTIN_FLAG | HIDDEN)
        self.assertEqual(self.cmd(APP_CMD_SET_HIDDEN, struct.pack("<IB", KILL_DEAD, 0))[0], OK)
        self.assertEqual(self.menu(), ["1", "2", "3", "4096", "settings"])
        self.assertEqual(self.cmd(APP_CMD_SET_HIDDEN, struct.pack("<IB", KILL_DEAD, 2))[0], ERROR)
        self.assertEqual(self.cmd(APP_CMD_SET_HIDDEN, struct.pack("<IB", 0x7777, 1))[0], ERROR)

    def test_the_last_shown_app_cannot_be_hidden(self):
        self.assertEqual(self.cmd_in_sync(APP_CMD_SET_HIDDEN, struct.pack("<IB", KILLS, 1))[0], OK)
        self.assertEqual(self.cmd(APP_CMD_SET_HIDDEN, struct.pack("<IB", KILL_DEAD, 1))[0], OK)
        self.assertEqual(self.cmd(APP_CMD_SET_HIDDEN, struct.pack("<IB", STATS, 1))[0], LAST_SHOWN)
        self.assertEqual(self.menu(), ["3", "settings"])

    def cmd_in_sync(self, cmd_id, data):
        self.dev.open_app_sync()
        return self.cmd(cmd_id, data)

    def set_order(self, ids):
        answers = []
        for part in range(0, max(1, (len(ids) + 2) // 3)):
            chunk = ids[part * 3:part * 3 + 3]
            data = bytes([len(ids)]) + b"".join(struct.pack("<I", i) for i in chunk).ljust(12, b"\0") + bytes([part])
            answers.append(self.cmd(APP_CMD_SET_ORDER, data)[0])
        return answers

    def test_order(self):
        self.assert_installed(counter_app())
        self.assert_installed(small_app(0x300), open_sync=False)
        self.assertEqual(self.set_order([0x300, STATS, 0x1000, KILLS, KILL_DEAD]), [OK, OK])
        self.assertEqual(self.apps_list(), [0x300, STATS, 0x1000, KILLS, KILL_DEAD])
        self.assertEqual(self.menu(), ["768", "3", "4096", "1", "2", "settings"])
        # Wrong orders change nothing
        self.assertEqual(self.set_order([KILLS, KILL_DEAD, STATS]), [ERROR])          # Not all apps
        self.assertEqual(self.set_order([KILLS, KILLS, STATS, 0x300, 0x1000]), [OK, ERROR])  # Twice
        self.assertEqual(self.set_order([KILLS, KILL_DEAD, STATS, 0x300, 0x9999]), [OK, ERROR])  # Unknown
        self.assertEqual(self.apps_list(), [0x300, STATS, 0x1000, KILLS, KILL_DEAD])
        # A part out of order
        data = bytes([5]) + bytes(12) + bytes([1])
        self.assertEqual(self.cmd(APP_CMD_SET_ORDER, data)[0], ERROR)
        # Fable review F4: an unfinished order (connection lost after part 0), then a new one
        self.assertEqual(self.set_order([KILLS, KILL_DEAD, STATS, 0x300, 0x1000])[:1], [OK])
        data = bytes([5]) + struct.pack("<III", STATS, KILLS, KILL_DEAD) + bytes([0])
        self.assertEqual(self.cmd(APP_CMD_SET_ORDER, data)[0], OK)   # Part 0 again: a new order
        data = bytes([5]) + struct.pack("<II", 0x300, 0x1000).ljust(12, b"\0") + bytes([1])
        self.assertEqual(self.cmd(APP_CMD_SET_ORDER, data)[0], OK)
        self.assertEqual(self.apps_list(), [STATS, KILLS, KILL_DEAD, 0x300, 0x1000])

    def test_settings_over_bluetooth(self):
        self.assert_installed(counter_app())
        self.assertEqual(self.cmd(APP_CMD_SETTING, struct.pack("<IBB", 0x1000, 0, 0xFF)), (OK, bytes([1])))
        self.assertEqual(self.cmd(APP_CMD_SETTING, struct.pack("<IBB", 0x1000, 0, 2)), (OK, bytes([2])))
        self.assertEqual(self.cmd(APP_CMD_SETTING, struct.pack("<IBB", 0x1000, 0, 3))[0], ERROR)
        self.assertEqual(self.cmd(APP_CMD_SETTING, struct.pack("<IBB", 0x1000, 2, 0))[0], ERROR)
        self.assertEqual(self.cmd(APP_CMD_SETTING, struct.pack("<IBB", KILLS, 0, 0xFF))[0], ERROR)
        self.assertEqual(self.dev.state()["app.4096.settings"], "2,0")
        self.dev.power_cut()
        self.assertEqual(self.dev.read_blobs()["s00001000"], bytes([2, 0]))

    def test_app_commands_work_only_in_app_sync(self):
        self.open_update_view_from_menu(transfer=False)
        self.assertEqual(self.dev.view, "UPDATE")
        for cmd_id in range(APP_CMD_INFO, APP_CMD_SETTING + 1):
            status, _ = self.cmd(cmd_id, bytes(16))
            self.assertEqual(status, NOT_IN_APP_SYNC, hex(cmd_id))

    def test_app_sync_refuses_firmware_and_the_update_view_takes_it_after(self):
        self.dev.open_app_sync()
        state = self.dev.state()
        self.assertEqual(state["ota_active"], 1)
        self.assertEqual(state["ota_update_allowed"], 0)
        with self.assertRaises(SimError):
            self.dev.ota_transfer(500)
        self.leave_sync()
        self.assertEqual(self.dev.state()["ota_active"], 0)
        self.open_update_view_from_menu()

    def test_long_press_1_leaves_app_sync(self):
        self.dev.open_app_sync()
        self.dev.long_press(1)
        self.assertEqual(self.dev.view, "SETTINGS")


class DeviceSettingsTest(AppTestCase):

    def open_app_settings(self, app_id):
        self.dev.click_both()
        for _ in range(12):
            if self.dev.state()["launcher_app"] == app_id:
                break
            self.dev.click(2)
        self.dev.long_press(2)

    def test_hold_button_2_in_the_menu_opens_the_app_settings(self):
        self.assert_installed(counter_app())
        self.leave_sync()
        self.open_app_settings(0x1000)
        state = self.dev.state()
        self.assertEqual(state["view"], "APP_SETTINGS")
        self.assertEqual(state["app_settings_id"], 0x1000)
        # The selected row is in the light band, with the name and the value
        frame = self.dev.frame()
        self.assertGreater(frame[10].count("#"), 120)

        self.dev.long_press(2)    # MODE: B -> C
        self.assertEqual(self.dev.state()["app.4096.settings"], "2,0")
        self.dev.long_press(2)    # C -> A (wraps)
        self.dev.click(2)         # Next row
        self.assertEqual(self.dev.state()["app_settings_row"], 1)
        self.dev.long_press(1)    # SOUND: ON -> OFF (wraps back)
        self.assertEqual(self.dev.state()["app.4096.settings"], "0,1")

        # Both buttons: the menu, with this app selected
        self.dev.click_both()
        state = self.dev.state()
        self.assertEqual(state["view"], "LAUNCHER")
        self.assertEqual(state["launcher_app"], 0x1000)

        # Saved in flash
        self.dev.power_cut()
        self.assertEqual(self.dev.read_blobs()["s00001000"], bytes([0, 1]))

    def test_no_settings_view_for_an_app_without_settings(self):
        self.assert_installed(counter_app(settings=False))
        self.leave_sync()
        self.open_app_settings(0x1000)
        self.assertEqual(self.dev.view, "LAUNCHER")

    def test_no_settings_view_for_a_builtin_app(self):
        self.dev.click_both()
        self.dev.long_press(2)
        self.assertEqual(self.dev.view, "LAUNCHER")

    def test_the_menu_opens_the_app_from_its_settings(self):
        self.assert_installed(counter_app())
        self.leave_sync()
        self.open_app_settings(0x1000)
        self.dev.click_both()
        self.dev.click_both()
        state = self.dev.state()
        self.assertEqual(state["view"], "APP_HOST")
        self.assertEqual(state["app_running"], 0x1000)


class AppErrorTest(AppTestCase):

    def test_an_endless_loop_gives_app_error_and_the_menu_still_works(self):
        self.assert_installed(loop_app())
        self.leave_sync()
        self.open_app(0x2000)
        state = self.dev.state()
        self.assertEqual(state["view"], "APP_HOST")
        self.assertEqual(state["app_error"], STAPP_ERR_STEPS)
        self.assertEqual(state["app_error_text"], "too many steps")
        self.assertGreater(sum(row.count("#") for row in self.dev.frame()), 100)   # "APP ERROR"
        self.dev.power_cut()
        self.assertEqual(self.dev.read_blobs()["e00002000"], bytes([1]))
        self.dev.boot()
        self.open_app(0x2000)
        # The buttons do nothing in the app; both buttons open the menu, and SETTINGS is there
        self.dev.click(1)
        self.assertEqual(self.dev.state()["eeprom.total_kills"], 0)
        self.open_update_view_from_menu()

    def test_three_errors_switch_the_app_off(self):
        self.assert_installed(loop_app())
        self.leave_sync()
        for _ in range(3):
            self.open_app(0x2000)
            self.assertEqual(self.dev.state()["app_error"], STAPP_ERR_STEPS)
        self.dev.click_both()
        self.assertEqual(self.menu(), ["1", "2", "3", "settings"])
        self.assertEqual(self.dev.state()["app.8192.flags"], HIDDEN | ERROR_FLAG)
        # It stays off after a power cycle
        self.dev.power_cut()
        self.dev.boot()
        self.assertEqual(self.menu(), ["1", "2", "3", "settings"])
        # Shown again by the app manager: a new chance
        self.dev.open_app_sync()
        self.assertEqual(self.cmd(APP_CMD_SET_HIDDEN, struct.pack("<IB", 0x2000, 0))[0], OK)
        self.assertEqual(self.dev.state()["app.8192.flags"], 0)

    def test_too_much_drawing_is_an_app_error(self):
        asm = Asm()
        asm.handler("draw").label("top")
        asm.sys_drop("invert", 0, 0, 128, 32)
        asm.jump("JMP", "top")
        self.assert_installed(build(name="DRAW", app_id=0x2100, asm=asm))
        self.leave_sync()
        self.open_app(0x2100)
        self.assertEqual(self.dev.state()["app_error"], STAPP_ERR_STEPS)

    def test_a_bad_string_id_at_run_time_is_an_app_error(self):
        asm = Asm()
        asm.handler("draw")
        asm.push(2).push(2)
        asm.sys("time").push(0).op("MUL").push(9).op("ADD")   # string id 9, not a constant
        asm.push(0).push(1).push(0)
        asm.sys_drop("text", None, None, None, None, None, None)
        asm.halt()
        self.assert_installed(build(name="BAD", app_id=0x2200, strings=["A"], asm=asm))
        self.leave_sync()
        self.open_app(0x2200)
        self.assertEqual(self.dev.state()["app_error_text"], "bad arg")


class SafetyTest(AppTestCase):

    def test_settings_and_the_update_view_with_8_apps(self):
        for i in range(8):
            self.assert_installed(small_app(0x100 + i))
        self.leave_sync()
        self.open_update_view_from_menu()

    def test_settings_and_the_update_view_with_all_apps_hidden(self):
        self.assert_installed(small_app(0x100))
        for app_id in (KILLS, KILL_DEAD, STATS):
            self.assertEqual(self.cmd(APP_CMD_SET_HIDDEN, struct.pack("<IB", app_id, 1))[0], OK)
        self.assertEqual(self.cmd(APP_CMD_SET_HIDDEN, struct.pack("<IB", 0x100, 1))[0], LAST_SHOWN)
        # The flash says all are hidden (an other firmware, damaged flash): KILLS is shown
        self.dev.power_cut()
        entries = [(KILLS, 1), (KILL_DEAD, 1), (STATS, 1), (0x100, 1)]
        self.dev.write_blobs({"list": bytes([1, 4]) + b"".join(struct.pack("<IB", i, f) for i, f in entries)})
        self.dev.boot()
        self.assertEqual(self.menu(), ["1", "settings"])
        self.open_update_view_from_menu()

    def test_data_survives_a_power_cycle(self):
        self.dev.power_cut()
        self.dev.write_flash({"battery_type": BATTERY_LIPO_1S})
        self.dev.boot()
        self.assert_installed(counter_app(saved=1))
        self.assertEqual(self.cmd(APP_CMD_SETTING, struct.pack("<IBB", 0x1000, 1, 1))[0], OK)
        self.assertEqual(self.cmd(APP_CMD_SET_HIDDEN, struct.pack("<IB", STATS, 1))[0], OK)
        self.leave_sync()
        self.open_app(0x1000)
        for _ in range(3):
            self.dev.click(1)
        self.dev.click_both()           # Close: the saved variable goes to flash
        self.dev.run(500)
        self.dev.power_cut()
        self.assertEqual(self.dev.read_blobs()["v00001000"], struct.pack("<i", 3))

        self.dev.boot()
        state = self.dev.state()
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS, 0x1000])
        self.assertEqual(self.menu(), ["1", "2", "4096", "settings"])
        self.assertEqual(state["app.4096.settings"], "1,1")
        self.assertEqual(state["eeprom.game_kills"], 3)

    def test_saved_values_are_written_on_power_off(self):
        self.assert_installed(counter_app(saved=1))
        self.leave_sync()
        self.open_app(0x1000)
        self.dev.click(1)
        self.dev.long_press_both()
        self.assert_powered_off()
        self.assertEqual(self.dev.read_blobs()["v00001000"], struct.pack("<i", 1))

    def test_a_factory_reset_removes_the_apps(self):
        self.assert_installed(counter_app())
        self.leave_sync()
        self.dev.click(2, settle_ms=200)   # "Firmware" is the row after "Apps"
        self.dev.click(2, settle_ms=200)   # then "Factory"
        self.dev.long_press(2)
        self.assertEqual(self.dev.view, "ASK")
        self.dev.long_press(2)             # Yes
        self.dev.run(1000)
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS])


class RobustnessTest(AppTestCase):

    def test_random_app_commands_never_break_the_way_to_the_update(self):
        rng = random.Random(1234)
        self.dev.open_app_sync()
        commands = []
        for _ in range(300):
            cmd_id = rng.randint(APP_CMD_INFO, APP_CMD_SETTING)
            if rng.random() < 0.3:
                # Known ids in the data, so the commands reach deeper code
                data = struct.pack("<I", rng.choice([0, 1, 2, 3, 0x1000, 0xFFFFFFFF])) + bytes(rng.randrange(256) for _ in range(12))
            else:
                data = bytes(rng.randrange(256) for _ in range(16))
            commands.append((cmd_id, data))
        self.dev.ble_commands(commands)
        state = self.dev.state()
        self.assertTrue(self.dev.is_on)
        self.assertGreaterEqual(int(state["apps_count"]), 1)
        if state["app_install_state"] == WAITING_STATE:
            self.dev.long_press(1)
        self.dev.long_press(1)
        self.open_update_view_from_menu()

    def test_random_packages_are_refused_or_safe(self):
        rng = random.Random(99)
        base = counter_app()
        self.dev.open_app_sync()
        for case in range(12):
            package = bytearray(base)
            for _ in range(rng.randint(1, 8)):
                package[rng.randrange(12, len(package) - 4)] = rng.randrange(256)
            package = finish(package)
            result = self.install(package, open_sync=False)
            self.assertIn(result["step"], ("begin", "ask", "end", "answer"), result)
            if result["state"] == INSTALLED:
                # It passed the check: it must run without breaking the device
                self.leave_sync()
                self.open_app(0x1000)
                self.dev.click(1)
                self.dev.click(2)
                self.dev.long_press(1)
                self.dev.run(500)
                self.dev.open_app_sync()
        self.dev.long_press(1)
        self.open_update_view_from_menu()

    def test_random_bytes_as_packages(self):
        rng = random.Random(7)
        self.dev.open_app_sync()
        for case in range(10):
            size = rng.randint(305, 1200)
            data = bytearray(rng.randrange(256) for _ in range(size))
            data[0:4] = b"STAP"
            data[4], data[5] = 1, 1
            struct.pack_into("<I", data, 8, 0x5000 + case)
            # A random name field: the device does not even ask
            result = self.install(bytes(data), open_sync=False)
            self.assertEqual((result["step"], result["status"]), ("ask", ERROR), result)
            # With a good name it asks; after yes the check of END refuses the package
            result = self.install(bytes(data), open_sync=False, name=b"RANDOM %d" % case)
            self.assertEqual(result["step"], "end", result)
            self.assertEqual(result["status"], BAD_PACKAGE, result)
        self.dev.long_press(1)
        self.open_update_view_from_menu()


if __name__ == "__main__":
    unittest.main()
