"""The device screens that the web updater shows must be the screens of this firmware.

The Apps tab of the web updater draws the device display of Settings > "Apps: SYNC" in
JavaScript (updater/js/apps-pictures.js) and plays it on the device picture (updater/js/device.js).
updater/tests/apps.test.js compares that drawing with updater/tests/sync-screens.json. This test
makes those screens again from the firmware (tests/make_sync_screens.py) and compares them with
the file. So a change of the screens on the device fails here until the file is made again
(python tests/make_sync_screens.py), and then in the updater tests until the page draws the same.
"""

import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import make_sync_screens  # noqa: E402


class UpdaterScreensTest(unittest.TestCase):
    def test_sync_screens_file_is_the_firmware_now(self):
        with open(make_sync_screens.OUT, encoding="utf-8") as handle:
            saved = json.load(handle)
        now = make_sync_screens.build()
        self.assertEqual(len(now["screens"]), len(saved["screens"]),
                         "the number of screens changed: run python tests/make_sync_screens.py")
        for index, (a, b) in enumerate(zip(now["screens"], saved["screens"])):
            self.assertEqual(a, b, "screen %d (%s) is not the screen of the firmware now: run "
                             "python tests/make_sync_screens.py, then make updater/js/apps-pictures.js draw it"
                             % (index, a["kind"]))


if __name__ == "__main__":
    unittest.main()
