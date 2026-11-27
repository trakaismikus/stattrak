"""tools/websim.py: is the page up to date, with or without the test apps in its Apps panel."""

import os
import shutil
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))
import websim  # noqa: E402


class PanelAppsTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.mkdtemp()
        self.old = (websim.BUILD_DIR, websim.PAGE, websim._newest_source)
        websim.BUILD_DIR = self.folder
        websim.PAGE = os.path.join(self.folder, "simulator.html")
        websim._newest_source = lambda: time.time() - 60   # the page is newer than the sources

    def tearDown(self):
        websim.BUILD_DIR, websim.PAGE, websim._newest_source = self.old
        shutil.rmtree(self.folder, ignore_errors=True)

    def page(self, panel=None):
        with open(websim.PAGE, "w", encoding="utf-8") as handle:
            handle.write("<html></html>")
        if panel is not None:
            with open(os.path.join(self.folder, "panel-apps.txt"), "w", encoding="utf-8") as handle:
                handle.write(panel + "\n")

    def test_no_page(self):
        self.assertFalse(websim.is_up_to_date())
        self.assertFalse(websim.is_up_to_date(panel_apps=False))

    def test_page_with_test_apps(self):
        self.page("on")
        self.assertTrue(websim.is_up_to_date())
        self.assertTrue(websim.is_up_to_date(panel_apps=True))
        # The web site needs a page without the test apps: build again
        self.assertFalse(websim.is_up_to_date(panel_apps=False))

    def test_page_without_test_apps(self):
        self.page("off")
        self.assertTrue(websim.is_up_to_date())
        self.assertTrue(websim.is_up_to_date(panel_apps=False))
        self.assertFalse(websim.is_up_to_date(panel_apps=True))

    def test_page_of_an_older_build_has_the_test_apps(self):
        self.page()
        self.assertTrue(websim.is_up_to_date(panel_apps=True))
        self.assertFalse(websim.is_up_to_date(panel_apps=False))

    def test_old_page(self):
        self.page("off")
        websim._newest_source = lambda: time.time() + 60
        self.assertFalse(websim.is_up_to_date(panel_apps=False))


if __name__ == "__main__":
    unittest.main()
