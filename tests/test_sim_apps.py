"""The app installer of the window and web simulators (sim/host/sim_apps.c).

The web page and "simulator --app FILE" install an app with the Bluetooth app commands, like the
web updater. The headless command "simapp FILE" runs the same code, so these tests cover it.
"""

import os
import shutil
import struct
import sys
import tempfile
import unittest

from helpers import BATTERY_LIPO_1S
from test_apps import AppTestCase, counter_app, INSTALLED, KILLS, KILL_DEAD, STATS

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))
import app  # noqa: E402

# sim_apps_state_t (sim/host/sim_apps.h)
OPENING, SENDING, QUESTION, DONE, REFUSED, FAILED = 1, 2, 3, 4, 5, 6


class SimAppsTest(AppTestCase):

    def setUp(self):
        super().setUp()
        self.folder = tempfile.mkdtemp(prefix="stattrak_simapp_")
        self.addCleanup(shutil.rmtree, self.folder, True)

    def simapp(self, package, run_ms=3000):
        path = os.path.join(self.folder, "app.stapp")
        with open(path, "wb") as handle:
            handle.write(package)
        self.dev.command("simapp " + path)
        self.dev.run(run_ms)
        return self.dev.state()

    def test_install_asks_the_person_then_installs(self):
        package = app.compile_file(os.path.join(REPO_ROOT, "apps", "runner", "runner.sts"))["package"]
        app_id = struct.unpack_from("<I", package, 8)[0]
        state = self.simapp(package)
        self.assertEqual(state["view"], "APP_SYNC")
        self.assertEqual(state["simapp_state"], QUESTION, state["simapp_text"])
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS])      # nothing installed yet
        self.dev.long_press(2)                                               # yes
        self.dev.run(500)
        state = self.dev.state()
        self.assertEqual(state["simapp_state"], DONE, state["simapp_text"])
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS, app_id])
        self.dev.long_press(1)                       # leave Apps: SYNC: back to the view before it
        self.assertEqual(self.dev.view, "TOTAL")
        self.open_app(app_id)
        self.assertEqual(self.dev.state()["app_running"], app_id)

    def test_no_installs_nothing(self):
        state = self.simapp(counter_app())
        self.assertEqual(state["simapp_state"], QUESTION, state["simapp_text"])
        self.dev.long_press(1)                                               # no
        self.dev.run(500)
        state = self.dev.state()
        self.assertEqual(state["simapp_state"], REFUSED, state["simapp_text"])
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS])

    def test_a_bad_file_is_refused_by_the_firmware(self):
        package = bytearray(counter_app())
        package[100] ^= 0xFF                                                 # the CRC of the file no longer fits
        state = self.simapp(bytes(package))
        # The device asks first (the data comes only after yes), then it checks the app
        self.assertEqual(state["simapp_state"], QUESTION, state["simapp_text"])
        self.dev.long_press(2)
        self.dev.run(1000)
        state = self.dev.state()
        self.assertEqual(state["simapp_state"], FAILED)
        self.assertIn("not a valid app file", state["simapp_text"])
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS])

    def test_simopen_answers_yes_and_opens_the_app(self):
        # "python tools/app.py sim", the page address #app=... and "simulator --app FILE" do this
        package = app.compile_file(os.path.join(REPO_ROOT, "apps", "slots", "slots.sts"))["package"]
        app_id = struct.unpack_from("<I", package, 8)[0]
        path = os.path.join(self.folder, "slots.stapp")
        with open(path, "wb") as handle:
            handle.write(package)
        self.dev.command("simopen " + path)
        self.dev.run(4000)
        state = self.dev.state()
        self.assertEqual(state["simapp_state"], DONE, state["simapp_text"])
        self.assertEqual(state["view"], "APP_HOST")
        self.assertEqual(state["app_running"], app_id)
        self.assertEqual(state["app_error"], 0)

    def test_simopen_during_the_power_on_hold(self):
        # The web page gives the app at once, while the simulator still holds both buttons for the
        # power-on. A hold that started before the question does not answer it: the simulator must
        # wait until button 2 is up, then hold it again (before this fix: the update mode after 10 s).
        package = app.compile_file(os.path.join(REPO_ROOT, "apps", "runner", "runner.sts"))["package"]
        path = os.path.join(self.folder, "runner.stapp")
        with open(path, "wb") as handle:
            handle.write(package)
        self.dev.power_cut()
        self.dev.power_on(hold="both")
        self.dev.command("simopen " + path)
        self.dev.run(800)
        self.dev.release(1)
        self.dev.run(3000)                       # Button 2 is still held from the power-on
        self.assertEqual(self.dev.state()["simapp_state"], QUESTION)
        self.dev.release(2)
        self.dev.run(4000)
        state = self.dev.state()
        self.assertEqual(state["simapp_state"], DONE, state["simapp_text"])
        self.assertEqual(state["view"], "APP_HOST")

    def test_simopen_waits_for_the_start_screen(self):
        # The page starts the install at once; the device shows its start screen first
        self.dev.power_cut()
        self.dev.power_on(hold="both")
        path = os.path.join(self.folder, "app.stapp")
        with open(path, "wb") as handle:
            handle.write(counter_app())
        self.dev.command("simopen " + path)
        self.dev.run(400)
        self.dev.release(1)
        self.dev.release(2)
        self.dev.run(8000)
        state = self.dev.state()
        self.assertEqual(state["simapp_state"], DONE, state["simapp_text"])
        self.assertEqual(state["app_running"], 0x1000)

    def test_the_install_stops_when_the_device_leaves_app_sync(self):
        # A battery warning replaces "Apps: SYNC" during the question: no install, no stuck panel
        self.boot_with(voltage=3700, battery_type=BATTERY_LIPO_1S, low_bat=3)
        self.dev.run(45 * 1000)
        state = self.simapp(counter_app())
        self.assertEqual(state["simapp_state"], QUESTION, state["simapp_text"])
        self.dev.run(20 * 1000)                                              # the warning comes
        state = self.dev.state()
        self.assertEqual(state["view"], "BATTERY_LOW")
        self.assertEqual(state["simapp_state"], FAILED)
        self.assertIn("left Apps: SYNC", state["simapp_text"])
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS])

    def test_install_while_an_app_runs_and_install_again(self):
        package = counter_app()
        self.assertEqual(self.install(package)["state"], INSTALLED)
        self.leave_sync()
        self.open_app(0x1000)
        state = self.simapp(package)                                         # the same id: an update
        self.assertEqual(state["simapp_state"], QUESTION, state["simapp_text"])
        self.dev.long_press(2)
        self.dev.run(500)
        self.assertEqual(self.dev.state()["simapp_state"], DONE)
        self.assertEqual(self.apps_list(), [KILLS, KILL_DEAD, STATS, 0x1000])


if __name__ == "__main__":
    unittest.main()
