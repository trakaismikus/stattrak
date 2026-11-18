"""The package check of tools/app.py (Python) and of the firmware (components/stapp, C) must give the
same result for the same package. The page and the tools say "this app is good" only if the
device says so too.

Needs the built stapp_tool (python tools/check.py --build-only). Skipped without it.
"""

import os
import random
import shutil
import subprocess
import sys
import tempfile
import unittest
import zlib

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))

import app  # noqa: E402


def find_stapp_tool():
    for folder in (os.path.join(REPO_ROOT, "build-host"), os.path.join(REPO_ROOT, "build-host", "Debug")):
        for name in ("stapp_tool", "stapp_tool.exe"):
            path = os.path.join(folder, name)
            if os.path.isfile(path):
                return path
    return None


def c_check(tool, packages, folder):
    paths = []
    for index, data in enumerate(packages):
        path = os.path.join(folder, "p%05d.stapp" % index)
        with open(path, "wb") as handle:
            handle.write(data)
        paths.append(path)
    result = subprocess.run([tool, "check"], input="\n".join(paths) + "\n", stdout=subprocess.PIPE,
                            universal_newlines=True, check=True)
    return result.stdout.splitlines()


def fix_crc(data):
    data = bytearray(data)
    if len(data) >= 4:
        data[-4:] = (zlib.crc32(bytes(data[:-4])) & 0xFFFFFFFF).to_bytes(4, "little")
    return bytes(data)


@unittest.skipIf(find_stapp_tool() is None, "stapp_tool is not built (python tools/check.py --build-only)")
class CheckMatchTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.tool = find_stapp_tool()
        cls.folder = tempfile.mkdtemp(prefix="stattrak_apps_")
        cls.examples = []
        apps_dir = os.path.join(REPO_ROOT, "apps")
        for name in sorted(os.listdir(apps_dir)):
            source = os.path.join(apps_dir, name, name + ".sts")
            if os.path.isfile(source):
                cls.examples.append(app.load_package(source))
        # An app for engine 4 (lists, held(), more steps): the check of its changed packages too
        cls.examples.append(app.load_package(os.path.join(REPO_ROOT, "tools", "bench", "bench.sts")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def compare(self, packages):
        c_results = c_check(self.tool, packages, self.folder)
        self.assertEqual(len(c_results), len(packages))
        mismatches = []
        for index, data in enumerate(packages):
            py_result = app.result_text(app.check(data)[0])
            if py_result != c_results[index]:
                mismatches.append((index, py_result, c_results[index]))
        self.assertEqual(mismatches[:5], [], "%d packages with a different result (index, Python, C)" % len(mismatches))

    def test_the_example_apps_pass_both_checks(self):
        self.assertGreaterEqual(len(self.examples), 3)
        self.compare(self.examples)
        for result in c_check(self.tool, self.examples, self.folder):
            self.assertEqual(result, "ok")

    def test_changed_packages_give_the_same_result(self):
        rng = random.Random(2026)
        packages = []
        for _ in range(1500):
            data = bytearray(rng.choice(self.examples))
            for _ in range(rng.randint(1, 4)):
                at = rng.randrange(len(data))
                kind = rng.randrange(4)
                if kind == 0:
                    data[at] = rng.randrange(256)
                elif kind == 1:
                    data[at] ^= 1 << rng.randrange(8)
                elif kind == 2:
                    data[at] = rng.choice((0, 0xFF))
                else:
                    # A header field: counts, offsets, sizes
                    field = rng.randrange(32, 48)
                    data[field] = rng.randrange(256)
            if rng.randrange(8) == 0:
                data = data[:rng.randrange(len(data) + 1)]
            else:
                data = fix_crc(data)
            packages.append(bytes(data))
        self.compare(packages)


def find_headless():
    for folder in (os.path.join(REPO_ROOT, "build-host"), os.path.join(REPO_ROOT, "build-host", "Debug")):
        for name in ("stattrak_headless", "stattrak_headless.exe"):
            if os.path.isfile(os.path.join(folder, name)):
                return True
    return False


@unittest.skipIf(not find_headless(), "the simulator is not built (python tools/check.py --build-only)")
class ScreenMatchTest(unittest.TestCase):
    """The first screen of each example app is the same in `tools/app.py run` (the preview) and on
    the simulated device (the firmware engine and the firmware draw functions)."""

    def frame_lines(self, text):
        return [line for line in text.splitlines() if len(line) == 128 and set(line) <= set(".#")]

    def test_example_apps_look_the_same_on_the_device(self):
        apps_dir = os.path.join(REPO_ROOT, "apps")
        names = [n for n in sorted(os.listdir(apps_dir)) if os.path.isfile(os.path.join(apps_dir, n, n + ".sts"))]
        self.assertGreaterEqual(len(names), 3)
        for name in names:
            with self.subTest(app=name):
                source = os.path.join(apps_dir, name, name + ".sts")
                preview = subprocess.run([sys.executable, os.path.join(REPO_ROOT, "tools", "app.py"), "run", source, "frame"],
                                         stdout=subprocess.PIPE, universal_newlines=True, check=True).stdout
                folder = tempfile.mkdtemp(prefix="stattrak_screen_")
                try:
                    package = os.path.join(folder, name + ".stapp")
                    with open(package, "wb") as handle:
                        handle.write(app.load_package(source))
                    # Install it. The menu opens on the first app (APP_SYNC is not in the menu); button 1
                    # goes back to SETTINGS, again to the installed app (the last one before SETTINGS)
                    device = subprocess.run([sys.executable, os.path.join(REPO_ROOT, "tools", "sim.py"), "--fresh",
                                             "--out", folder, "boot", "app:" + package, "both", "click1", "click1",
                                             "both", "wait:500", "state", "frame"],
                                            stdout=subprocess.PIPE, universal_newlines=True, check=True).stdout
                finally:
                    shutil.rmtree(folder, ignore_errors=True)
                self.assertIn("view=APP_HOST", device)
                self.assertEqual(self.frame_lines(device)[-32:], self.frame_lines(preview)[-32:])


if __name__ == "__main__":
    unittest.main()
