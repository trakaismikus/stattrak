"""The background app: Settings > Background, the kill and death events, two engines (docs/APPS.md 4.6).

The test installs apps with the permission "background" through "Apps: SYNC", chooses one in
Settings > Background with the buttons, and reads what the app logged with the commands of the sync
page (docs/DATA.md section 5). The firmware code is the real code.
"""
import struct
import unittest

from stattrak_sim import SETTING_ROWS
from test_apps import KILLS, OK, counter_app
from test_data import DataTestCase, shows, app

DELETE = 0x0117   # APP_CMD_DELETE
OTHER_SOURCE = 'app "OTHER"' + chr(10) + 'data "X"' + chr(10) + 'var ok' + chr(10) + 'on press1 { ok = log(77) }' + chr(10)


def bg_app(name="WATCHER", extra="", permissions="background", columns=('"WHAT"', '"N"')):
    """A background app: it logs (1, kills) for a kill, (2, deaths) for a death, (3, 0) at open"""
    source = """app "%s"
permission %s
data %s
on open { log(3, 0) }
on kill { log(1, counter(game_kills)) }
on death { log(2, counter(game_deaths)) }
%s
""" % (name, permissions, " ".join(columns), extra)
    package = app.compile_source(source)["package"]
    return package, struct.unpack_from("<I", package, 8)[0]


class BackgroundTestCase(DataTestCase):

    def state(self, key):
        return self.dev.state()[key]

    def setting_rows(self):
        """The settings rows (setting ids) from the top: button 2 goes down, round to the first row"""
        self.go_to_setting("GAME_RESET")
        rows = []
        for _ in range(2 * len(SETTING_ROWS)):
            row = self.state("settings_row")
            if rows and row == rows[0]:
                break
            rows.append(row)
            self.dev.click(2, settle_ms=200)
        return [SETTING_ROWS[row] for row in rows]

    def choose_background(self, presses=1):
        """Settings > Background: hold button 2 `presses` times (the next value each time)"""
        self.go_to_setting("BACKGROUND")
        for _ in range(presses):
            self.dev.long_press(2)

    def install_background(self, package, app_id, presses=1):
        """Install, leave Apps: SYNC, choose the app in Settings > Background"""
        self.assert_installed(package)
        self.leave_sync()
        self.choose_background(presses)
        self.dev.run(300)
        self.assertEqual(self.state("app_background"), app_id)

    def rows(self, app_id):
        """The rows of the app, read in Apps: SYNC (the background app stops there and is written)"""
        self.open_sync()
        rows = self.read_rows(app_id)
        self.leave_sync()
        return rows


class SettingRowTest(BackgroundTestCase):

    def test_no_background_row_without_a_background_app(self):
        self.assert_installed(counter_app())
        self.leave_sync()
        self.assertNotIn("BACKGROUND", self.setting_rows())
        self.assertEqual(self.state("app_background"), 0)

    def test_the_row_comes_after_apps_and_starts_off(self):
        package, app_id = bg_app()
        self.assert_installed(package)
        self.leave_sync()
        rows = self.setting_rows()
        self.assertEqual(rows[rows.index("APP_SYNC") + 1], "BACKGROUND")
        self.go_to_setting("BACKGROUND")
        self.dev.run(400)                            # The list scrolls there
        self.assertTrue(shows(self.dev.frame(), "OFF"))
        self.assertEqual(self.state("app_background"), 0)
        self.dev.long_press(2)
        self.assertTrue(shows(self.dev.frame(), "WATCHER"))
        self.assertEqual(self.state("app_background"), app_id)
        self.dev.long_press(2)                       # Round to OFF
        self.assertTrue(shows(self.dev.frame(), "OFF"))
        self.assertEqual(self.state("app_background"), 0)

    def test_two_background_apps_one_runs(self):
        first, first_id = bg_app("FIRST")
        second, second_id = bg_app("SECOND")
        self.assert_installed(first)
        self.dev.long_press(1)                       # SYNC MORE
        self.assert_installed(second, open_sync=False)
        self.leave_sync()
        self.choose_background(1)
        self.assertEqual(self.state("app_background"), first_id)
        self.dev.long_press(2)
        self.assertEqual(self.state("app_background"), second_id)
        self.assertTrue(shows(self.dev.frame(), "SECOND"))

    def test_the_choice_stays_after_a_power_cycle(self):
        package, app_id = bg_app()
        self.install_background(package, app_id)
        self.dev.power_cut()
        self.dev.boot()
        self.assertEqual(self.state("app_background"), app_id)
        # One open for each start of the device
        self.assertEqual(self.rows(app_id), [[3, 0], [3, 0]])


class EventTest(BackgroundTestCase):

    def test_kills_and_deaths_of_the_counter_screen(self):
        package, app_id = bg_app()
        self.install_background(package, app_id)
        self.go_to_view("KILL_DEAD")
        self.dev.click(1)
        self.dev.click(1)
        self.dev.click(2)
        self.dev.long_press(1)                       # -1 kill: no event
        self.assertEqual(self.rows(app_id), [[3, 0], [1, 1], [1, 2], [2, 1]])

    def test_kills_of_the_total_screen(self):
        package, app_id = bg_app()
        self.install_background(package, app_id)
        self.go_to_view("TOTAL")
        self.dev.click(1)
        self.dev.click(2)                            # Both buttons add a kill on TOTAL
        self.assertEqual(self.rows(app_id), [[3, 0], [1, 1], [1, 2]])

    def test_kills_of_an_other_app(self):
        package, app_id = bg_app()
        self.install_background(package, app_id)
        self.open_sync()
        self.assert_installed(counter_app(), open_sync=False)
        self.leave_sync()
        self.open_app(0x1000)
        self.dev.click(1)                            # add_counter(game_kills, 1)
        self.dev.click(2)                            # -1: no event
        # Two opens: Apps: SYNC (the install of COUNTER) stopped it, and it started again after
        self.assertEqual(self.rows(app_id), [[3, 0], [3, 0], [1, 1]])

    def test_no_kill_event_for_its_own_add_counter(self):
        package, app_id = bg_app(permissions="background, counters",
                                 extra="on press1 { log(9, add_counter(game_kills, 1)) }")
        self.install_background(package, app_id)
        self.open_app(app_id)
        self.dev.click(1)
        self.assertEqual(self.rows(app_id), [[3, 0], [9, 1]])

    def test_the_menu_shows_the_running_app(self):
        # The app is open in the background: the app menu shows it, with no new open event
        package, app_id = bg_app(extra='on draw { num(0, 0, log_count(), 0, text, on, left) }')
        self.install_background(package, app_id)
        self.go_to_view("KILL_DEAD")
        self.dev.click(1)
        self.open_app(app_id)
        self.assertEqual(self.state("app_running"), app_id)
        self.assertTrue(shows(self.dev.frame(), "2", "5x7"))   # The open row and the kill row
        self.go_to_view("KILL_DEAD")
        self.dev.click(1)
        self.open_app(app_id)
        self.assertEqual(self.state("app_background"), app_id)
        self.assertEqual(self.rows(app_id), [[3, 0], [1, 1], [1, 2]])

    def test_tick_runs_in_the_background(self):
        package, app_id = bg_app(extra="var t\non tick {\n  t += 1\n  if t % 10 == 0 { log(4, t) }\n}")
        self.install_background(package, app_id)
        self.go_to_view("KILL_DEAD")
        self.dev.run(3050)
        rows = self.rows(app_id)
        self.assertEqual(rows[0], [3, 0])
        self.assertGreaterEqual(len(rows), 1 + 3)
        self.assertEqual(rows[1:4], [[4, 10], [4, 20], [4, 30]])

    def test_the_background_app_and_an_other_data_app_log_together(self):
        package, app_id = bg_app()
        self.install_background(package, app_id)
        other = app.compile_source('app "OTHER"\ndata "X"\non press1 { log(77) }\n')["package"]
        other_id = struct.unpack_from("<I", other, 8)[0]
        self.open_sync()
        self.assert_installed(other, open_sync=False)
        self.leave_sync()
        self.open_app(other_id)
        self.dev.click(1)
        self.go_to_view("KILL_DEAD")
        self.dev.click(1)
        self.open_app(other_id)
        self.dev.click(1)
        self.open_sync()
        self.assertEqual(self.read_rows(other_id), [[77], [77]])
        self.assertEqual(self.read_rows(app_id), [[3, 0], [3, 0], [1, 1]])   # Started again after the install

    def test_off_stops_it(self):
        package, app_id = bg_app(extra="on close { log(5, 0) }")
        self.install_background(package, app_id)
        self.dev.long_press(2)                       # Background: OFF
        self.assertEqual(self.state("app_background"), 0)
        self.go_to_view("KILL_DEAD")
        self.dev.click(1)
        self.assertEqual(self.rows(app_id), [[3, 0], [5, 0]])


class PauseTest(BackgroundTestCase):

    def test_sync_stops_it_and_it_starts_again(self):
        package, app_id = bg_app(extra="on close { log(5, 0) }")
        self.install_background(package, app_id)
        self.open_sync()
        self.assertEqual(self.state("app_background"), 0)
        self.leave_sync()
        self.dev.run(100)
        self.assertEqual(self.state("app_background"), app_id)
        self.assertEqual(self.rows(app_id), [[3, 0], [5, 0], [3, 0], [5, 0]])

    def test_the_update_mode_stops_it(self):
        package, app_id = bg_app()
        self.install_background(package, app_id)
        self.open_update_view_from_menu(transfer=False)
        self.assertEqual(self.dev.view, "UPDATE")
        self.assertEqual(self.state("app_background"), 0)

    def test_a_delete_turns_it_off_and_removes_the_row(self):
        package, app_id = bg_app()
        self.install_background(package, app_id)
        self.open_sync()
        self.assertEqual(self.cmd(DELETE, struct.pack("<I", app_id))[0], OK)
        self.leave_sync()
        self.dev.run(100)
        self.assertEqual(self.state("app_background"), 0)
        self.assertNotIn("BACKGROUND", self.setting_rows())

    def test_open_after_install_shows_the_background_app(self):
        # Install a new version of the background app, then OPEN: one engine runs it (one open event)
        package, app_id = bg_app(extra='on press1 { log(8, 0) }')
        self.install_background(package, app_id)
        self.open_sync()
        source = ('app "WATCHER"\nversion 2\npermission background\ndata "WHAT" "N"\n'
                  'on open { log(3, 0) }\non kill { log(1, counter(game_kills)) }\non press1 { log(8, 0) }\n')
        newer = app.compile_source(source)["package"]
        self.assert_installed(newer, open_sync=False)
        self.dev.long_press(2)                       # OPEN
        self.dev.run(1000)
        self.assertEqual(self.dev.view, "APP_HOST")
        self.assertEqual(self.state("app_background"), app_id)
        self.assertEqual(self.state("app_running"), app_id)
        self.dev.click(1)
        self.go_to_view("KILL_DEAD")
        self.dev.click(1)
        # An update keeps the rows (the open of the first version); then one open and the rows of
        # the same engine
        self.assertEqual(self.rows(app_id), [[3, 0], [3, 0], [8, 0], [1, 1]])

    def test_a_factory_reset_turns_it_off(self):
        package, app_id = bg_app()
        self.install_background(package, app_id)
        self.go_to_setting("FACTORY_RESET")
        self.dev.long_press(2)
        self.assertEqual(self.dev.view, "ASK")
        self.dev.long_press(2)                       # Yes
        self.dev.run(1000)
        self.assertEqual(self.state("app_background"), 0)
        self.assertEqual(self.apps_list()[:1], [KILLS])


class TwoStoresTest(BackgroundTestCase):

    def test_the_reserve_counts_the_rows_of_both_apps(self):
        # The background app has a chunk that waits in RAM. A new chunk of the app on the screen fits
        # alone (174 entries), but not with the one of the background app (188): the settings and
        # the counters keep their 160 entries.
        package, app_id = bg_app()
        other = app.compile_source(OTHER_SOURCE)["package"]
        other_id = struct.unpack_from("<I", other, 8)[0]
        self.install_background(package, app_id)
        self.open_sync()
        self.assert_installed(other, open_sync=False)
        self.fill_flash_to(182)
        self.leave_sync()
        self.dev.run(100)                            # The background app starts: a row in RAM
        self.assertEqual(self.state("app_background"), app_id)
        self.open_app(other_id)
        self.dev.click(1)
        self.open_sync()
        free = self.free_entries()
        self.assertGreaterEqual(free, 160)
        self.assertEqual(self.read_rows(other_id), [], "the row of OTHER took the reserve (%d free)" % free)

    def test_one_store_alone_still_fits(self):
        # The same flash without a background app: the row fits
        other = app.compile_source(OTHER_SOURCE)["package"]
        other_id = struct.unpack_from("<I", other, 8)[0]
        self.assert_installed(other)
        self.fill_flash_to(182)
        self.leave_sync()
        self.open_app(other_id)
        self.dev.click(1)
        self.open_sync()
        self.assertEqual(self.read_rows(other_id), [[77]])


class SafetyTest(BackgroundTestCase):

    def test_an_endless_loop_in_kill_stops_only_the_background_app(self):
        package, app_id = bg_app(extra="")
        source = ('app "LOOPER"\npermission background\non kill {\n  var i = 0\n  while 1 { i += 1 }\n}\n')
        package = app.compile_source(source)["package"]
        app_id = struct.unpack_from("<I", package, 8)[0]
        self.install_background(package, app_id)
        self.go_to_view("KILL_DEAD")
        self.dev.click(1)
        self.dev.run(200)
        self.assertNotEqual(self.state("app_background_error"), 0)
        # The counter and the menu still work, and the way to the update too
        self.dev.click(1)
        self.assertEqual(self.dev.state()["eeprom.game_kills"], 2)
        self.open_update_view_from_menu(transfer=False)
        self.assertEqual(self.dev.view, "UPDATE")

    def test_a_kill_handler_needs_the_permission(self):
        with self.assertRaises(app.AppError) as raised:
            app.compile_source('app "NOPE"\non kill { var a = 1 }\n')
        self.assertIn("permission background", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
