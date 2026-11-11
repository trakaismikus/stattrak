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
        self.assertTrue(re.fullmatch(r"\d+\.\d+\.\d+\n", text), "version.txt must be three numbers, like 1.1.0")
        version = text.strip()
        # The Bluetooth answer "get version" has space for 13 characters, the settings view for 15
        self.assertLessEqual(len(version), 13)

        self.dev.boot()
        self.assertEqual(self.dev.state()["fw_version"], version)


if __name__ == "__main__":
    unittest.main()
