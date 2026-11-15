"""Backup and restore, app data, device log, start picture and live preview (docs/DATA.md).

The test plays the role of the sync page: it opens Settings > "Apps: SYNC" with the buttons and
sends the commands of docs/DATA.md section 5. The firmware code is the real code.
"""
import os
import struct
import sys
import unittest
import zlib

from helpers import BATTERY_LIPO_1S, MINUTE
from stattrak_sim import APP_CMD_DATA, APP_CMD_END, APP_CMD_STATUS, APP_CMD_LIST, APP_CMD_SETTING
from test_apps import (AppTestCase, INSTALLED, KILLS, KILL_DEAD, STATS, OK, ERROR, NOT_IN_APP_SYNC,
                       WAITING, BAD_CRC, BAD_PACKAGE, IDLE, RECEIVING, WAITING_STATE, REFUSED,
                       counter_app, small_app)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))
import app  # noqa: E402

CONFIG_INFO, CONFIG_ENTRY, APP_VALUES, XFER_BEGIN, START_PIC, PREVIEW_STOP = range(0x0120, 0x0126)
DATA_INFO, DATA_COLUMN, DATA_READ, DATA_CLEAR, LOG_READ = range(0x0126, 0x012B)
KIND_RESTORE, KIND_START_PIC, KIND_PREVIEW, KIND_CLEAR = 1, 2, 3, 4
PREVIEW_STATE = 6
PIC_NONE, PIC_ON, PIC_OFF = 0, 1, 2
POWER_ON, RESTART, CRASH, WATCHDOG, BROWN_OUT = 1, 2, 3, 4, 5


def picture(seed=1):
    """512 bytes of a 128 x 32 picture (frame buffer format) with some pixels on"""
    return bytes(((i * 37 + seed) & 0xFF) if i % 5 == 0 else 0 for i in range(512))


def data_app(name="LOGGER", columns=("TIME", "MV"), extra=""):
    """An app with a data store: button 1 adds a row (time, voltage), button 2 clears the rows"""
    names = " ".join('"%s"' % column for column in columns)
    values = ", ".join(["time()", "battery_mv()", "1", "2"][:len(columns)])
    source = """app "%s"
data %s
%s
var ok
on press1 {
  ok = log(%s)
}
on press2 {
  log_clear()
}
on draw {
  num(0, 0, log_count(), 0, text, on, left)
  num(64, 0, ok, 0, text, on, left)
}
""" % (name, names, extra, values)
    return app.compile_source(source)["package"]


def text_pattern(text, font="5x5"):
    """The pixels of `text` (the firmware font), cut to the pixels that are on"""
    frame = app.Screen(app._assets())
    frame.text(font, 0, 0, "left", 1, 0, text)
    rows = ["".join("#" if p else "." for p in row) for row in frame.pixels]
    used = [y for y, row in enumerate(rows) if "#" in row]
    left = min(row.index("#") for row in rows if "#" in row)
    right = max(row.rindex("#") for row in rows if "#" in row)
    return [rows[y][left:right + 1] for y in range(used[0], used[-1] + 1)]


def shows(frame, text, font="5x5"):
    """True if the display shows `text`, also as dark text on light (the titles)"""
    pattern = text_pattern(text, font)
    inverted = ["".join("." if c == "#" else "#" for c in row) for row in pattern]
    height, width = len(pattern), len(pattern[0])
    for y in range(32 - height + 1):
        for x in range(128 - width + 1):
            for wanted in (pattern, inverted):
                if all(frame[y + j][x:x + width] == wanted[j] for j in range(height)):
                    return True
    return False


def section(kind, body):
    return struct.pack("<BH", kind, len(body)) + body


def value(key, number):
    return section(1, bytes([len(key)]) + key.encode("ascii") + struct.pack("<I", number))


def stbk(*sections):
    return b"STBK\x01" + b"".join(sections)


class DataTestCase(AppTestCase):

    def open_sync(self):
        """Settings > "Apps: SYNC", then wait until the radio start (only the Bluetooth sign) is over"""
        self.dev.open_app_sync()
        self.dev.run(1500)

    def entries(self):
        """CONFIG_ENTRY for all flash values: {key: (value, max)}"""
        status, info = self.cmd(CONFIG_INFO)
        self.assertEqual(status, OK)
        result = {}
        for index in range(info[1]):
            status, part0 = self.cmd(CONFIG_ENTRY, bytes([index, 0]))
            self.assertEqual(status, OK)
            number, largest, length = struct.unpack("<IIB", part0)
            key = self.cmd(CONFIG_ENTRY, bytes([index, 1]))[1]
            if length > 13:
                key += self.cmd(CONFIG_ENTRY, bytes([index, 2]))[1]
            self.assertEqual(len(key), length)
            result[key.decode("ascii")] = (number, largest)
        self.assertEqual(self.cmd(CONFIG_ENTRY, bytes([info[1], 0]))[0], 4)
        return result

    def transfer(self, kind, data, answer="yes", parts=None):
        """XFER_BEGIN, the answer on the device for a restore, DATA, END. Return (status, answer)."""
        crc = zlib.crc32(data) & 0xFFFFFFFF
        status, _ = self.cmd(XFER_BEGIN, struct.pack("<BHI", kind, len(data), crc))
        if status != OK:
            return status, b""
        if kind == KIND_RESTORE:
            state = self.cmd(APP_CMD_STATUS)[1]
            self.assertEqual(tuple(state), (WAITING_STATE, KIND_RESTORE))
            self.assertTrue(shows(self.dev.frame(), "RESTORE BACKUP?"), "RESTORE BACKUP?")
            if answer == "yes":
                self.dev.long_press(2)
            elif answer == "no":
                self.dev.long_press(1)
                return self.cmd(APP_CMD_STATUS)
        commands = []
        for offset in range(0, len(data), 14):
            chunk = data[offset:offset + 14]
            if parts is not None and offset not in parts:
                continue
            commands.append((APP_CMD_DATA, struct.pack("<H", offset) + chunk.ljust(14, b"\0")))
        for result in self.dev.ble_commands(commands):
            self.assertEqual(result[0], OK, result)
        return self.cmd(APP_CMD_END)

    def read_rows(self, app_id):
        status, info = self.cmd(DATA_INFO, struct.pack("<I", app_id))
        self.assertEqual(status, OK)
        columns, rows = info[0], struct.unpack_from("<H", info, 1)[0]
        values = []
        while len(values) < rows * columns:
            status, answer = self.cmd(DATA_READ, struct.pack("<IH", app_id, len(values)))
            self.assertEqual(status, OK)
            self.assertGreater(answer[0], 0)
            values += struct.unpack_from("<%di" % answer[0], answer, 1)
        return [values[i:i + columns] for i in range(0, len(values), columns)]


class SyncOnlyTest(DataTestCase):

    def test_commands_work_only_in_app_sync(self):
        self.open_update_view_from_menu(transfer=False)
        self.assertEqual(self.dev.view, "UPDATE")
        for cmd_id in range(0x0120, 0x012B):
            with self.subTest(cmd=hex(cmd_id)):
                self.assertEqual(self.cmd(cmd_id, bytes(16))[0], NOT_IN_APP_SYNC)

    def test_config_info(self):
        self.open_sync()
        status, info = self.cmd(CONFIG_INFO)
        self.assertEqual(status, OK)
        self.assertEqual(len(info), 13)
        self.assertEqual(info[0], 1)
        self.assertEqual(info[2], 0x1F)
        self.assertEqual(info[3], PIC_NONE)
        used, total, each, free = struct.unpack_from("<HHHH", info, 4)
        self.assertEqual((used, total, each), (0, 8192, 4096))
        self.assertGreater(free, 10000)


class StartPictureTest(DataTestCase):

    def test_no_picture_starts_with_the_first_app(self):
        self.dev.power_cut()
        self.dev.boot(settle_ms=200)
        self.assertEqual(self.dev.view, "TOTAL")

    def test_first_shown_app_when_kills_is_hidden(self):
        self.open_sync()
        self.assertEqual(self.cmd(0x0119, struct.pack("<IB", KILLS, 1))[0], OK)   # SET_HIDDEN
        self.dev.power_cut()
        self.dev.boot(settle_ms=200)
        self.assertEqual(self.dev.view, "KILL_DEAD")

    def test_send_show_then_first_app(self):
        self.open_sync()
        pixels = picture()
        status, _ = self.transfer(KIND_START_PIC, pixels)
        self.assertEqual(status, OK)
        self.assertEqual(tuple(self.cmd(APP_CMD_STATUS)[1]), (INSTALLED, KIND_START_PIC))
        # The device shows the new picture for 2 s
        self.assertEqual(self.dev.frame(), self.expected_frame(pixels))
        self.dev.run(2100)
        self.assertEqual(self.cmd(APP_CMD_STATUS)[1][0], IDLE)
        self.assertEqual(self.cmd(START_PIC, b"\x00")[1][0], PIC_ON)
        # Read it back
        back = b""
        for offset in range(0, 512, 12):
            status, part = self.cmd(START_PIC, struct.pack("<BH", 4, offset))
            self.assertEqual(status, OK)
            back += part
        self.assertEqual(back, pixels)
        self.assertEqual(self.dev.read_blobs("data")["spic"], pixels)

        # At the next start: the picture, 2 s after the buttons are up, then the first app
        self.dev.power_cut()
        self.dev.boot(settle_ms=500)
        self.assertEqual(self.dev.view, "INTRO")
        self.assertEqual(self.dev.frame(), self.expected_frame(pixels))
        self.dev.run(1400)
        self.assertEqual(self.dev.view, "INTRO")
        self.dev.run(800)
        self.assertEqual(self.dev.view, "TOTAL")

    def test_press_skips_and_counts_nothing(self):
        self.open_sync()
        self.transfer(KIND_START_PIC, picture())
        self.dev.power_cut()
        self.dev.boot(settle_ms=800)
        self.assertEqual(self.dev.view, "INTRO")
        kills = self.dev.state()["eeprom.total_kills"]
        self.dev.click(1)
        self.assertEqual(self.dev.state()["view_next"], "TOTAL")
        self.dev.run(300)                       # the slide
        self.assertEqual(self.dev.view, "TOTAL")
        self.assertEqual(self.dev.state()["eeprom.total_kills"], kills)

    def test_off_and_delete(self):
        self.open_sync()
        self.transfer(KIND_START_PIC, picture())
        self.assertEqual(self.cmd(START_PIC, b"\x02")[1][0], PIC_OFF)
        self.dev.power_cut()
        self.dev.boot(settle_ms=200)
        self.assertEqual(self.dev.view, "TOTAL")
        self.open_sync()
        self.assertEqual(self.cmd(START_PIC, b"\x01")[1][0], PIC_ON)
        self.assertEqual(self.cmd(START_PIC, b"\x03")[1][0], PIC_NONE)
        self.assertEqual(self.cmd(START_PIC, b"\x01")[0], ERROR)
        self.assertEqual(self.cmd(START_PIC, struct.pack("<BH", 4, 0))[0], ERROR)
        self.assertNotIn("spic", self.dev.read_blobs("data"))
        self.dev.power_cut()
        self.dev.boot(settle_ms=200)
        self.assertEqual(self.dev.view, "TOTAL")

    def test_broken_picture_in_flash_is_no_picture(self):
        self.dev.power_cut()
        self.dev.write_blobs({"spic": picture()[:300], "spon": b"\x01"}, namespace="data")
        self.dev.boot(settle_ms=200)
        self.assertEqual(self.dev.view, "TOTAL")

    def test_wrong_size_and_crc(self):
        self.open_sync()
        self.assertEqual(self.cmd(XFER_BEGIN, struct.pack("<BHI", KIND_START_PIC, 511, 0))[0], ERROR)
        self.assertEqual(self.cmd(XFER_BEGIN, struct.pack("<BHI", 9, 512, 0))[0], ERROR)
        pixels = picture()
        self.assertEqual(self.cmd(XFER_BEGIN, struct.pack("<BHI", KIND_START_PIC, 512, 1234))[0], OK)
        commands = [(APP_CMD_DATA, struct.pack("<H", o) + pixels[o:o + 14].ljust(14, b"\0")) for o in range(0, 512, 14)]
        self.dev.ble_commands(commands)
        self.assertEqual(self.cmd(APP_CMD_END)[0], BAD_CRC)
        self.assertEqual(self.cmd(START_PIC, b"\x00")[1][0], PIC_NONE)
        self.assertTrue(shows(self.dev.frame(), "NOT SAVED"), "NOT SAVED")

    def test_factory_reset_deletes_the_picture(self):
        self.open_sync()
        self.transfer(KIND_START_PIC, picture())
        self.dev.power_cut()
        self.dev.boot(settle_ms=3000)
        self.open_setting("FACTORY_RESET")
        self.dev.long_press(2)
        self.dev.long_press(2)     # "Do a Factory Reset?" yes
        self.dev.run(500)
        self.assertNotIn("spic", self.dev.read_blobs("data"))
        self.dev.power_cut()
        self.dev.boot(settle_ms=200)
        self.assertEqual(self.dev.view, "TOTAL")

    def test_update_mode_still_opens_with_a_picture(self):
        # The 10 s hold at power-on opens the update view, also with a start picture
        self.open_sync()
        self.transfer(KIND_START_PIC, picture())
        self.dev.power_cut()
        self.dev.power_on(hold="both")
        self.dev.run(10500)
        self.assertEqual(self.dev.view, "UPDATE")

    @staticmethod
    def expected_frame(pixels):
        rows = []
        for y in range(32):
            rows.append("".join("#" if pixels[(y // 8) * 128 + x] & (1 << (y % 8)) else "." for x in range(128)))
        return rows


class PreviewTest(DataTestCase):

    def test_preview_frame_shows_and_writes_nothing(self):
        self.open_sync()
        pixels = bytearray(512)
        pixels[0] = 0xFF                 # 8 pixels in the top left corner
        pixels[300] = 0x0F
        writes = self.dev.state().get("nvs_writes")
        # Only the parts that are not 0
        parts = [o for o in range(0, 512, 14) if any(pixels[o:o + 14])]
        status, _ = self.transfer(KIND_PREVIEW, bytes(pixels), parts=parts)
        self.assertEqual(status, OK)
        self.assertEqual(tuple(self.cmd(APP_CMD_STATUS)[1]), (PREVIEW_STATE, KIND_PREVIEW))
        self.assertEqual(self.dev.frame(), StartPictureTest.expected_frame(bytes(pixels)))
        self.assertEqual(self.dev.state().get("nvs_writes"), writes)
        self.assertEqual(self.cmd(START_PIC, b"\x00")[1][0], PIC_NONE)

        # The next frame: the old one stays on the display while it comes
        second = bytearray(512)
        second[127] = 0x01
        self.cmd(XFER_BEGIN, struct.pack("<BHI", KIND_PREVIEW, 512, zlib.crc32(bytes(second)) & 0xFFFFFFFF))
        self.assertEqual(self.dev.frame(), StartPictureTest.expected_frame(bytes(pixels)))
        self.dev.ble_commands([(APP_CMD_DATA, struct.pack("<H", 126) + bytes(second[126:140]))])
        self.assertEqual(self.cmd(APP_CMD_END)[0], OK)
        self.assertEqual(self.dev.frame(), StartPictureTest.expected_frame(bytes(second)))

        # PREVIEW_STOP: back to "Waiting..."
        self.assertEqual(self.cmd(PREVIEW_STOP)[0], OK)
        self.assertTrue(shows(self.dev.frame(), "APP SYNC"), "APP SYNC")
        self.assertEqual(self.cmd(APP_CMD_STATUS)[1][0], IDLE)

    def test_button_ends_preview(self):
        self.open_sync()
        pixels = bytes([0xFF] * 512)
        self.transfer(KIND_PREVIEW, pixels)
        self.dev.click(1)
        self.assertEqual(self.dev.view, "APP_SYNC")
        self.assertEqual(self.cmd(APP_CMD_STATUS)[1][0], IDLE)
        self.assertTrue(shows(self.dev.frame(), "APP SYNC"), "APP SYNC")


class DeviceLogTest(DataTestCase):

    def log(self):
        self.open_sync()
        entries = []
        while True:
            status, answer = self.cmd(LOG_READ, bytes([len(entries)]))
            self.assertEqual(status, OK)
            count = answer[0]
            got = [tuple(answer[1 + 4 * i:5 + 4 * i]) for i in range((len(answer) - 1) // 4)]
            entries += got
            if len(entries) >= count or not got:
                return entries

    def test_entry_after_5_seconds(self):
        self.assertEqual(self.dev.read_blobs("data").get("log"), None)
        self.dev.run(5000)
        entries = self.log()
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0][0], POWER_ON)
        self.assertEqual(entries[0][1], 255)            # no battery type set
        self.assertEqual(entries[0][2], 0)

    def test_reasons_newest_first_and_64_at_most(self):
        self.dev.run(5000)
        self.dev.power_cut()
        self.dev.write_flash({"battery_type": BATTERY_LIPO_1S})
        self.dev.boot(reset=BROWN_OUT, voltage=3900)
        self.dev.run(5000)
        entries = self.log()
        self.assertEqual([e[0] for e in entries], [BROWN_OUT, POWER_ON])
        self.assertLessEqual(entries[0][1], 100)
        self.assertEqual(entries[0][3], (entries[1][3] + 1) & 0xFF)
        # A full log keeps the newest 64
        self.dev.power_cut()
        blob = bytes([1, 64, 200]) + bytes(4) + bytes([CRASH, 50, 0, 7]) * 64
        self.dev.write_blobs({"log": blob}, namespace="data")
        self.dev.boot(reset=WATCHDOG)
        self.dev.run(5000)
        entries = self.log()
        self.assertEqual(len(entries), 64)
        self.assertEqual(entries[0][0], WATCHDOG)
        self.assertEqual(entries[0][3], 200)
        self.assertEqual(entries[1][0], CRASH)

    def test_broken_log_is_empty(self):
        self.dev.power_cut()
        self.dev.write_blobs({"log": b"\x01\x05\x00"}, namespace="data")
        self.dev.boot()
        self.dev.run(5000)
        self.assertEqual(len(self.log()), 1)

    def test_factory_reset_keeps_the_log(self):
        self.dev.run(5000)
        self.open_setting("FACTORY_RESET")
        self.dev.long_press(2)
        self.dev.long_press(2)
        self.dev.run(500)
        self.assertIn("log", self.dev.read_blobs("data"))
        self.assertEqual(len(self.log()), 1)


class RestoreTest(DataTestCase):

    def test_entries_list(self):
        self.open_sync()
        entries = self.entries()
        self.assertIn("total_kills", entries)
        self.assertIn("battery_type", entries)
        self.assertEqual(entries["brightness"], (8, 10))
        self.assertEqual(entries["init"][0], 1)

    def test_no_does_nothing(self):
        self.open_sync()
        status, state = self.transfer(KIND_RESTORE, stbk(value("total_kills", 500)), answer="no")
        self.assertEqual(state[0], REFUSED)
        self.assertEqual(self.dev.state()["eeprom.total_kills"], 0)

    def test_restore_values(self):
        self.open_sync()
        data = stbk(value("total_kills", 1234), value("game_kills", 12), value("brightness", 3),
                    value("battery_type", BATTERY_LIPO_1S),
                    value("init", 0),                 # never from a backup
                    value("sound", 7),                # above the largest value
                    value("no_such_key", 1),          # an other firmware
                    section(99, b"later"),            # a part of a newer firmware
                    section(5, b"\x05MIKUS"))
        status, answer = self.transfer(KIND_RESTORE, data)
        self.assertEqual(status, OK, answer)
        self.assertEqual(tuple(answer), (5, 4))       # written, skipped
        self.assertTrue(shows(self.dev.frame(), "BACKUP RESTORED"), "BACKUP RESTORED")
        self.assertTrue(shows(self.dev.frame(), "4 SKIPPED"), "4 SKIPPED")
        state = self.dev.state()
        self.assertEqual(state["eeprom.total_kills"], 1234)
        self.assertEqual(state["eeprom.game_kills"], 12)
        self.assertEqual(state["eeprom.brightness"], 3)
        self.assertEqual(state["eeprom.init"], 1)
        self.assertEqual(state["eeprom.sound"], 1)
        # The new brightness is in use at once
        self.assertEqual(state["brightness"], ((0xFF + 0x35) * 3) // 10)
        self.assertEqual(self.dev.read_blobs("apps")["bname"], b"MIKUS")
        # OK: back to "Waiting..."
        self.dev.long_press(2)
        self.assertEqual(self.cmd(APP_CMD_STATUS)[1][0], IDLE)

    def test_broken_backup_writes_nothing(self):
        self.open_sync()
        status, answer = self.transfer(KIND_RESTORE, b"STBK\x01" + struct.pack("<BH", 1, 40) + b"short")
        self.assertEqual(status, BAD_PACKAGE)
        self.assertEqual(answer[0], 3)                # a section outside the data
        status, answer = self.transfer(KIND_RESTORE, b"XXXX\x01" + value("total_kills", 9))
        self.assertEqual((status, answer[0]), (BAD_PACKAGE, 1))
        self.assertEqual(self.dev.state()["eeprom.total_kills"], 0)
        self.assertTrue(shows(self.dev.frame(), "NOT RESTORED"), "NOT RESTORED")

    def test_restore_apps_setup_and_start_picture(self):
        package = counter_app(app_id=0x5000, name="COUNTER", saved=1)
        self.assert_installed(package)
        self.dev.long_press(1)        # SYNC MORE
        count = self.cmd(0x0110)[1][6] + 3
        order = [(STATS, 0), (0x5000, 0), (KILLS, 1), (KILL_DEAD, 0)]
        self.assertEqual(count, len(order))
        body = bytes([len(order)]) + b"".join(struct.pack("<IB", i, h) for i, h in order)
        pixels = picture(3)
        data = stbk(section(2, body),
                    section(3, struct.pack("<IB", 0x5000, 2) + bytes([1, 0])),     # the 2 settings
                    section(4, struct.pack("<IB", 0x5000, 1) + struct.pack("<i", 77)),
                    section(4, struct.pack("<IB", 0x7777, 1) + struct.pack("<i", 1)),   # not installed
                    section(6, b"\x02" + pixels))
        status, answer = self.transfer(KIND_RESTORE, data)
        self.assertEqual(status, OK, answer)
        self.assertEqual(answer[1], 1, answer)
        self.assertEqual(self.apps_list(), [STATS, 0x5000, KILLS, KILL_DEAD])
        self.assertEqual(self.cmd(APP_CMD_SETTING, struct.pack("<IBB", 0x5000, 0, 0xFF))[1][0], 1)
        status, values = self.cmd(APP_VALUES, struct.pack("<IB", 0x5000, 0))
        self.assertEqual((status, values[0]), (OK, 1))
        self.assertEqual(struct.unpack_from("<i", values, 1)[0], 77)
        self.assertEqual(self.cmd(START_PIC, b"\x00")[1][0], PIC_OFF)
        flags = self.cmd(APP_CMD_LIST, bytes([2]))[1][8]
        self.assertEqual(flags & 1, 1)                # KILLS hidden
        self.dev.power_cut()
        self.dev.boot(settle_ms=200)
        self.assertEqual(self.dev.view, "STATS")      # the first shown app; the picture is off

    def test_question_blocks_other_changes(self):
        self.open_sync()
        self.cmd(XFER_BEGIN, struct.pack("<BHI", KIND_RESTORE, 20, 0))
        self.assertEqual(self.cmd(START_PIC, b"\x03")[0], WAITING)
        self.assertEqual(self.cmd(XFER_BEGIN, struct.pack("<BHI", KIND_START_PIC, 512, 0))[0], WAITING)
        self.assertEqual(self.cmd(CONFIG_INFO)[0], OK)        # reading works
        self.dev.run(31000)
        self.assertEqual(self.cmd(APP_CMD_STATUS)[1][0], 5)   # no answer in 30 s


class AppDataTest(DataTestCase):

    def open_logger(self, package):
        self.assert_installed(package)
        self.leave_sync()
        self.open_app(struct.unpack_from("<I", package, 8)[0])

    def test_rows_columns_and_read(self):
        package = data_app()
        app_id = struct.unpack_from("<I", package, 8)[0]
        self.dev.power_cut()
        self.dev.write_flash({"battery_type": BATTERY_LIPO_1S})
        self.dev.boot(voltage=3800)
        self.open_logger(package)
        for _ in range(3):
            self.dev.click(1)
        self.assertEqual(self.dev.state()["app_error"], 0, self.dev.state()["app_error_text"])
        self.open_sync()          # closes the app: the rows go to flash in the next loop
        rows = self.read_rows(app_id)
        self.assertEqual(len(rows), 3)
        self.assertTrue(all(3700 <= row[1] <= 3900 for row in rows), rows)
        self.assertLess(rows[0][0], rows[2][0])
        self.assertEqual(self.cmd(DATA_COLUMN, struct.pack("<IB", app_id, 0)), (OK, b"TIME"))
        self.assertEqual(self.cmd(DATA_COLUMN, struct.pack("<IB", app_id, 1)), (OK, b"MV"))
        self.assertEqual(self.cmd(DATA_COLUMN, struct.pack("<IB", app_id, 2))[0], ERROR)
        info = self.cmd(DATA_INFO, struct.pack("<I", app_id))[1]
        self.assertEqual(info[0], 2)
        self.assertEqual(struct.unpack_from("<HH", info, 1), (3, 512))
        self.assertEqual(info[5], 0)
        self.assertEqual(self.cmd(DATA_INFO, struct.pack("<I", KILLS))[1][0], 0)
        self.assertEqual(self.cmd(DATA_INFO, struct.pack("<I", 0x12345678))[0], ERROR)
        status, info = self.cmd(CONFIG_INFO)
        self.assertEqual(struct.unpack_from("<H", info, 4)[0], 3 * 2 * 4)

    def test_rows_stay_after_power_off(self):
        package = data_app()
        app_id = struct.unpack_from("<I", package, 8)[0]
        self.open_logger(package)
        self.dev.click(1)
        self.dev.click(1)
        self.dev.long_press_both(settle_ms=2000)        # power off: the rows go to flash
        self.dev.boot()
        self.open_sync()
        self.assertEqual(len(self.read_rows(app_id)), 2)

    def test_full_stops(self):
        package = data_app(columns=("A", "B", "C"))
        app_id = struct.unpack_from("<I", package, 8)[0]
        self.dev.power_cut()
        # 1020 values in flash: 340 rows of 3. One more row fits (1023), then full.
        chunks = {}
        values = list(range(1020))
        for n in range(0, 1020, 64):
            chunks["c%08x%02x" % (app_id, n // 64)] = struct.pack("<%di" % len(values[n:n + 64]), *values[n:n + 64])
        chunks["m%08x" % app_id] = bytes([1, 3]) + struct.pack("<H", 1020)
        self.dev.boot()
        self.open_logger(package)
        self.dev.power_cut()
        self.dev.write_blobs(chunks, namespace="data")
        self.dev.boot()
        self.open_app(app_id)
        self.dev.click(1)                         # 1023 values: fits
        self.dev.click(1)                         # full: log() gives 0
        self.open_sync()
        info = self.cmd(DATA_INFO, struct.pack("<I", app_id))[1]
        self.assertEqual(struct.unpack_from("<H", info, 1)[0], 341)
        self.assertEqual(info[5] & 1, 1)          # full
        rows = self.read_rows(app_id)
        self.assertEqual(rows[0], [0, 1, 2])
        self.assertEqual(rows[339], [1017, 1018, 1019])
        self.assertEqual(rows[340][2], 1)

    def test_clear_asks_first(self):
        package = data_app()
        app_id = struct.unpack_from("<I", package, 8)[0]
        self.open_logger(package)
        self.dev.click(1)
        self.open_sync()
        self.assertEqual(self.cmd(DATA_CLEAR, struct.pack("<I", app_id))[0], OK)
        self.assertEqual(tuple(self.cmd(APP_CMD_STATUS)[1]), (WAITING_STATE, KIND_CLEAR))
        self.assertTrue(shows(self.dev.frame(), "DELETE LOGGER DATA?"), "DELETE LOGGER DATA?")
        self.dev.long_press(1)                    # NO
        self.assertEqual(len(self.read_rows(app_id)), 1)
        self.dev.long_press(1)                    # BACK from REFUSED is not here: start again
        self.open_sync()
        self.cmd(DATA_CLEAR, struct.pack("<I", app_id))
        self.dev.long_press(2)                    # YES
        self.assertEqual(tuple(self.cmd(APP_CMD_STATUS)[1]), (INSTALLED, KIND_CLEAR))
        self.assertTrue(shows(self.dev.frame(), "DATA DELETED"), "DATA DELETED")
        self.assertEqual(self.read_rows(app_id), [])
        self.assertFalse(any(key.startswith("m%08x" % app_id) for key in self.dev.read_blobs("data")))
        self.assertEqual(self.cmd(DATA_CLEAR, struct.pack("<I", app_id))[0], ERROR)   # no data

    def test_app_clears_and_delete_removes_the_store(self):
        package = data_app()
        app_id = struct.unpack_from("<I", package, 8)[0]
        self.open_logger(package)
        self.dev.click(1)
        self.dev.click(2)                          # log_clear()
        self.dev.click(1)
        self.open_sync()
        self.assertEqual(len(self.read_rows(app_id)), 1)
        self.assertEqual(self.cmd(0x0117, struct.pack("<I", app_id))[0], OK)    # DELETE
        self.assertFalse([key for key in self.dev.read_blobs("data") if ("%08x" % app_id) in key])

    def test_total_limit_of_all_apps(self):
        first, second = data_app("LOGA"), data_app("LOGB")
        id_a = struct.unpack_from("<I", first, 8)[0]
        id_b = struct.unpack_from("<I", second, 8)[0]
        self.assert_installed(first)
        self.dev.long_press(1)
        self.assert_installed(second)
        self.dev.power_cut()
        blobs = {}
        # App A: 1024 values (full), app B: 1022 values: 2046 of 2048 together
        for app_id, count in ((id_a, 1024), (id_b, 1022)):
            for n in range(0, count, 64):
                part = min(64, count - n)
                blobs["c%08x%02x" % (app_id, n // 64)] = bytes(4 * part)
            blobs["m%08x" % app_id] = bytes([1, 2]) + struct.pack("<H", count)
        self.dev.write_blobs(blobs, namespace="data")
        self.dev.boot()
        self.open_app(id_b)
        self.dev.click(1)          # 2048: fits
        self.dev.click(1)          # full for all apps
        self.open_sync()
        self.assertEqual(struct.unpack_from("<H", self.cmd(DATA_INFO, struct.pack("<I", id_b))[1], 1)[0], 512)
        self.assertEqual(struct.unpack_from("<H", self.cmd(CONFIG_INFO)[1], 4)[0], 8192)

    def test_flash_reserve_keeps_settings_working(self):
        # 8 apps with 24 KB of packages, then app rows until the store says full: the settings, the
        # counters and the device log must still save, and an app can still be replaced.
        packages = []
        for index in range(7):
            packages.append(small_app(0x6000 + index, name="BIG%d" % index, size=3300))
        logger = data_app("FILLER", columns=("A", "B", "C", "D"))
        logger_id = struct.unpack_from("<I", logger, 8)[0]
        for package in packages:
            self.assert_installed(package)
            self.dev.long_press(1)
        self.assert_installed(logger)
        self.dev.power_cut()
        blobs = {}
        for n in range(16):
            blobs["c%08x%02x" % (logger_id, n)] = bytes(256)
        blobs["m%08x" % logger_id] = bytes([1, 4]) + struct.pack("<H", 1020)
        self.dev.write_blobs(blobs, namespace="data")
        self.dev.boot()
        self.dev.run(5000)
        self.open_sync()
        free = struct.unpack_from("<H", self.cmd(CONFIG_INFO)[1], 10)[0]
        self.assertGreaterEqual(free, 160 * 32 - 2048, "the reserve for the settings is gone")
        self.leave_sync()
        self.go_to_view("TOTAL")
        for _ in range(20):
            self.dev.click(1)                       # counters
        self.dev.run(1000)
        self.assertEqual(self.dev.read_flash()["total_kills"], 20)
        self.open_sync()
        self.assertEqual(self.install(packages[0])["state"], INSTALLED)   # a replace still fits



class AwakeTest(DataTestCase):
    """Permission "awake": auto-off waits while the app is open (BATTERY LOG of many hours)"""

    def run_open_app(self, extra):
        package = data_app("WAKE%d" % len(extra), extra=extra)
        self.dev.power_cut()
        self.dev.write_flash({"auto_off": 1})          # 30 min
        self.dev.boot()
        self.assert_installed(package)
        self.leave_sync()
        self.open_app(struct.unpack_from("<I", package, 8)[0])
        self.dev.run(40 * MINUTE)

    def test_awake_app_keeps_the_device_on(self):
        self.run_open_app("permission awake")
        self.assertTrue(self.dev.is_on, self.dev.ended)
        self.assertEqual(self.dev.view, "APP_HOST")

    def test_other_app_switches_off(self):
        self.run_open_app("")
        self.assertFalse(self.dev.is_on)

    def test_battery_log_example_app(self):
        package = app.compile_file(os.path.join(REPO_ROOT, "apps", "battery-log", "battery-log.sts"))["package"]
        app_id = struct.unpack_from("<I", package, 8)[0]
        self.dev.power_cut()
        self.dev.write_flash({"battery_type": BATTERY_LIPO_1S, "auto_off": 1})
        self.dev.boot(voltage=4000)
        self.assert_installed(package)
        self.leave_sync()
        self.open_app(app_id)
        self.dev.click(1)                              # start: the first row at once
        self.dev.set_voltage(3700)
        self.dev.run(3 * MINUTE + 500)
        self.assertTrue(self.dev.is_on)
        self.open_sync()
        rows = self.read_rows(app_id)
        self.assertEqual([row[0] for row in rows], [0, 1, 2, 3])
        self.assertGreater(rows[0][1], rows[-1][1])
        self.assertEqual(self.cmd(DATA_COLUMN, struct.pack("<IB", app_id, 0)), (OK, b"MIN"))


if __name__ == "__main__":
    unittest.main()
