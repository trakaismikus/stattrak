"""The firmware version. It comes from the file version.txt."""

import os
import re
import unittest

from helpers import SimTestCase
from stattrak_sim import REPO_ROOT


class VersionTest(SimTestCase):

    def test_the_firmware_has_the_version_of_version_txt(self):
        with open(os.path.join(REPO_ROOT, "version.txt"), "r", encoding="utf-8") as handle:
            text = handle.read()
        self.assertTrue(re.fullmatch(r"\d+\.\d+\.\d+(-b\d+)?\n", text),
                        "version.txt must be three numbers, like 1.1.0, or a beta like 1.2.0-b1")
        # The selected settings row "VERSION: <version>" shows about 18 characters in all
        self.assertLessEqual(len(text.strip()), 9, "a longer version is cut off in the settings")
        version = text.strip()
        # The Bluetooth answer "get version" has space for 13 characters, the settings view for 15
        self.assertLessEqual(len(version), 13)

        self.dev.boot()
        self.assertEqual(self.dev.state()["fw_version"], version)


if __name__ == "__main__":
    unittest.main()
