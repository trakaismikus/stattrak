"""The way to "Apps: SYNC" that the web updater plays must be the screens of this firmware.

The Apps tab of the web updater plays updater/assets/sync-path.js on the device picture before
"Connect": kill counter, app menu, Settings, "Apps: SYNC", APP SYNC / Waiting. This test records the
same way again with the headless simulator (tools/device_art/make_sync_path.py) and compares it with
the file. A change of those screens fails here until the file is made again:
    python tools/device_art/make_sync_path.py
"""

import json
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools", "device_art"))

import make_sync_path  # noqa: E402


def load_saved():
    with open(make_sync_path.OUT, encoding="utf-8") as handle:
        text = handle.read()
    return json.loads(text[text.index("{"):text.rindex("}") + 1])


class SyncPathTest(unittest.TestCase):
    def test_sync_path_file_is_the_firmware_now(self):
        saved = load_saved()["frames"]
        now = make_sync_path.record()
        hint = ": run python tools/device_art/make_sync_path.py"
        self.assertEqual(len(now), len(saved), "the number of frames changed" + hint)
        for index, (a, b) in enumerate(zip(now, saved)):
            self.assertEqual(a, b, "frame %d is not the screen of the firmware now%s" % (index, hint))

    def test_ends_on_app_sync_with_the_right_button_held_before(self):
        frames = load_saved()["frames"]
        presses = [frame["press"] for frame in frames]
        self.assertIn("H2", presses)
        self.assertEqual(presses[-1], "-")
        # the App Sync screen shows long enough after the hold (its dots move, so it has more frames)
        last_hold = max(i for i, press in enumerate(presses) if press == "H2")
        self.assertGreaterEqual(sum(frame["ms"] for frame in frames[last_hold + 1:]), 300)


if __name__ == "__main__":
    unittest.main()
