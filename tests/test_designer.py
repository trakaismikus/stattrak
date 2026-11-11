"""Tests of the Screen Designer (tools/designer/).

The designer is a web page. Its drawing code and its text for AI tools are in
tools/designer/design.js. These tests run that file with Node.js and compare it with
tools/design.py, which tests/test_design.py compares with the real firmware draw code.
So a screen in the designer has the same pixels as on the device.

Without Node.js on the computer these tests are skipped. The checks on GitHub have Node.js.
"""

import json
import os
import random
import re
import shutil
import subprocess
import sys
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))

import design  # noqa: E402
from test_design import make, random_elements  # noqa: E402

DESIGNER_DIR = os.path.join(REPO_ROOT, "tools", "designer")
HELPER = os.path.join(REPO_ROOT, "tests", "designer_node.js")
EXAMPLE = os.path.join(REPO_ROOT, "designs", "example.json")
NODE = shutil.which("node")


def run_node(jobs):
    result = subprocess.run([NODE, HELPER], input=json.dumps(jobs).encode("utf-8"), stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise AssertionError("design.js failed in Node.js:\n%s" % result.stderr.decode("utf-8", "replace"))
    return json.loads(result.stdout.decode("utf-8"))


@unittest.skipIf(NODE is None, "Node.js is not installed")
class DesignerCodeTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.assets = design.load_assets()
        with open(EXAMPLE, "r", encoding="utf-8") as handle:
            cls.example = json.load(handle)

    def clean(self, data):
        clean, problems, _ = design.normalize(data, self.assets)
        self.assertEqual(problems, [])
        return clean

    def random_designs(self, count, seed):
        rng = random.Random(seed)
        return [make(random_elements(rng, self.assets)) for _ in range(count)]

    def test_same_pixels_as_the_python_tool(self):
        designs = self.random_designs(150, 4711) + [self.example]
        results = run_node([{"op": "draw", "design": data} for data in designs])
        for number, (data, result) in enumerate(zip(designs, results)):
            with self.subTest(design=number):
                self.assertEqual(result["problems"], [])
                clean = self.clean(data)
                for screen, frame in zip(clean["screens"], result["frames"]):
                    ours = design.draw_frame(screen, self.assets)
                    if ours != frame:
                        rows = ["python:   %s\ndesigner: %s" % (a, b) for a, b in zip(ours, frame) if a != b]
                        self.fail("The designer draws different pixels for %s\n%s"
                                  % (json.dumps(screen["elements"]), "\n".join(rows[:6])))

    def test_same_check_result_as_the_python_tool(self):
        designs = self.random_designs(40, 99) + [self.example]
        results = run_node([{"op": "draw", "design": data} for data in designs])
        for data, result in zip(designs, results):
            self.assertEqual(result["clean"], self.clean(data))

    def test_values_of_a_wrong_kind_give_the_same_result(self):
        # A file that a person or an AI tool wrote by hand can have a number or a list where a
        # text or a yes/no value must be. The designer and tools/design.py must read it the same way.
        odd = make([{"type": "rect", "x": 0, "y": 0, "w": 9, "h": 9, "fill": [], "note": None},
                    {"type": "rect", "x": 0, "y": 0, "w": 9, "h": 9, "fill": 1, "note": 7},
                    {"type": "rect", "x": 0, "y": 0, "w": 9, "h": 9, "fill": "yes"},
                    {"type": "text", "text": "AB", "scale": 2, "gap": "true", "invert": 0},
                    {"type": "text", "text": "AB", "gap": None, "invert": {}},
                    {"type": "text", "text": "AB", "x": 4.0, "y": 2.0, "scale": 2.0}])
        odd["name"] = None
        odd["screens"][0]["name"] = 12
        odd["screens"][0]["note"] = ["a"]
        names = make([])
        names["screens"] = [{"id": "constructor", "elements": []}, {"id": "toString", "elements": []},
                            {"id": "hasOwnProperty", "name": "__proto__", "elements": []}]
        designs = [odd, names]
        results = run_node([{"op": "draw", "design": data} for data in designs])
        for data, result in zip(designs, results):
            clean, problems, _ = design.normalize(data, self.assets)
            self.assertEqual(problems, [])
            self.assertEqual(result["problems"], [])
            self.assertEqual(result["clean"], clean)
        self.assertEqual([element.get("fill") for element in results[0]["clean"]["screens"][0]["elements"][:3]],
                         [False, False, False])

    def test_wrong_files_are_refused(self):
        cases = [
            [{"type": "circle"}],
            [{"type": "text", "text": "A", "font": "comic"}],
            [{"type": "text", "text": "A", "font": "toString"}],
            [{"type": "text", "text": ""}],
            [{"type": "text", "text": "Zaļš"}],
            [{"type": "text", "text": "A", "x": 1.5}],
            [{"type": "text", "text": "A", "scale": 9}],
            [{"type": "text", "text": "A", "scale": 2, "invert": True}],
            [{"type": "text", "text": "A", "align": "middle"}],
            [{"type": "rect", "x": 0, "y": 0, "w": 0, "h": 5}],
            [{"type": "rect", "x": 0, "y": 0, "w": 10, "h": 5, "radius": 3}],
            [{"type": "rect", "x": 0, "y": 0, "w": 10}],
            [{"type": "line", "x1": 0, "y1": 0, "x2": 5}],
            [{"type": "image", "name": "cat"}],
            [{"type": "image", "name": "constructor"}],
            [{"type": "pixels", "rows": ["##", "#"]}],
            [{"type": "pixels", "rows": ["#x"]}],
            [{"type": "pixels", "rows": []}],
            [{"type": "pixels", "rows": [None, "#"]}],
            [{"type": "text", "text": "A", "font": []}],
            [{"type": "text", "text": "A", "font": ["5x7"]}],
            [{"type": "text", "text": "A", "align": ["left"]}],
            [{"type": "image", "name": {}}],
            [{"type": "pixels", "rows": ["#", 5]}],
            [{"type": "rect", "x": 0, "y": 0, "w": 3, "h": 3, "color": "orange"}],
            [{"type": "text", "text": "A", "x": 5000}],
        ]
        designs = [make(elements) for elements in cases]
        designs += [{"format": "x", "version": 1, "screens": [{}]},
                    {"format": "stattrak-design", "version": 2, "screens": [{}]},
                    {"format": "stattrak-design", "version": True, "screens": [{}]},
                    {"format": "stattrak-design", "version": 1, "screens": []},
                    {"format": "stattrak-design", "version": 1, "screens": [{"id": "9x"}]},
                    {"format": "stattrak-design", "version": 1, "screens": [{"id": "a"}, {"id": "a"}]},
                    [1, 2, 3]]
        results = run_node([{"op": "draw", "design": data} for data in designs])
        for data, result in zip(designs, results):
            with self.subTest(design=data):
                self.assertNotEqual(result["problems"], [], "the designer accepts a wrong file")
                if isinstance(data, dict):
                    self.assertNotEqual(design.normalize(data, self.assets)[1], [],
                                        "tools/design.py accepts a file that the designer refuses")

    def test_same_description_as_the_python_tool(self):
        designs = self.random_designs(20, 5) + [self.example]
        results = run_node([{"op": "describe", "design": data, "source": "designs/x.json"} for data in designs])
        for data, result in zip(designs, results):
            self.assertEqual(result["text"], design.describe(self.clean(data), self.assets, "designs/x.json"))

    def test_area_of_each_element_has_all_its_pixels(self):
        # The designer uses the area to select and to move an element
        designs = [make([element]) for data in self.random_designs(60, 31) for element in data["screens"][0]["elements"]]
        results = run_node([{"op": "draw", "design": data} for data in designs])
        for data, result in zip(designs, results):
            element = self.clean(data)["screens"][0]["elements"][0]
            if element["type"] == "text" and element["invert"]:
                continue  # The white box of an inverted text is 1 pixel larger than the text
            area = result["bounds"][0][0]
            for y, row in enumerate(result["frames"][0]):
                for x, char in enumerate(row):
                    if char == "#":
                        self.assertTrue(area["x"] <= x < area["x"] + area["w"] and area["y"] <= y < area["y"] + area["h"],
                                        "pixel %d,%d is outside of the area %s of %s" % (x, y, area, element))


@unittest.skipIf(NODE is None, "Node.js is not installed")
class SendToAiTest(unittest.TestCase):
    """The text of "Send to AI": an AI tool must get the exact design out of it."""

    @classmethod
    def setUpClass(cls):
        cls.assets = design.load_assets()
        with open(EXAMPLE, "r", encoding="utf-8") as handle:
            cls.example = json.load(handle)

    def prompt(self, data, **options):
        return run_node([{"op": "prompt", "design": data, "options": options}])[0]

    def test_design_data_in_the_text_is_the_exact_design(self):
        rng = random.Random(77)
        designs = [self.example] + [make(random_elements(rng, self.assets)) for _ in range(30)]
        for data in designs:
            result = self.prompt(data)
            blocks = re.findall(r"```json\n(.*?)\n```", result["text"], flags=re.S)
            self.assertEqual(len(blocks), 1)
            from_text, problems, _ = design.normalize(json.loads(blocks[0]), self.assets)
            self.assertEqual(problems, [])
            self.assertEqual(from_text, design.normalize(data, self.assets)[0])

    def test_text_says_what_to_do_and_what_the_screens_are(self):
        text = self.prompt(self.example, request="Put the kill streak screen after the counter.")["text"]
        self.assertTrue(text.startswith("StatTrak screen design \"Example screens\": 3 screens."))
        self.assertIn("What I want: Put the kill streak screen after the counter.", text)
        self.assertIn("designs/example-screens.json", text)
        self.assertIn("python tools/design.py check designs/example-screens.json", text)
        self.assertIn("AGENTS.md", text)
        clean = design.normalize(self.example, self.assets)[0]
        for screen in clean["screens"]:
            self.assertIn("\"%s\" (id: %s)" % (screen["name"], screen["id"]), text)
            self.assertIn("What it does: %s" % screen["note"], text)
        self.assertIn("text \"01337\", font \"numbers\"", text)
        for row in design.draw_frame(clean["screens"][0], self.assets):
            if "#" in row:
                self.assertNotIn(row, text, "no pixel pictures without the option")

    def test_pixel_pictures_are_an_option(self):
        text = self.prompt(self.example, pictures=True)["text"]
        clean = design.normalize(self.example, self.assets)[0]
        for screen in clean["screens"]:
            for row in design.draw_frame(screen, self.assets):
                self.assertIn(row, text)

    def test_default_request_and_file_name(self):
        data = make([{"type": "line", "x1": 0, "y1": 0, "x2": 9, "y2": 0}])
        data["name"] = "  My ★ Kill/Death screens!  "
        result = self.prompt(data)
        self.assertEqual(result["fileName"], "my-kill-death-screens.json")
        self.assertIn("What I want: Build this screen into my StatTrak firmware.", result["text"])
        data["name"] = "★"
        self.assertEqual(self.prompt(data)["fileName"], "my-screens.json")

    def test_file_form_has_no_default_values_and_reads_back(self):
        data = make([{"type": "text", "x": 1, "y": 2, "text": "A"},
                     {"type": "rect", "x": 0, "y": 0, "w": 4, "h": 4, "color": "black", "note": "hole"}])
        result = self.prompt(data)
        elements = result["file"]["screens"][0]["elements"]
        self.assertEqual(elements[0], {"type": "text", "x": 1, "y": 2, "text": "A", "font": "5x7", "align": "left"})
        self.assertEqual(elements[1], {"type": "rect", "x": 0, "y": 0, "w": 4, "h": 4, "fill": False,
                                       "color": "black", "note": "hole"})
        self.assertEqual(design.normalize(result["file"], self.assets)[0], design.normalize(data, self.assets)[0])


class DesignerFilesTest(unittest.TestCase):
    """The designer must work from a folder on a disk, without the internet."""

    def read(self, name):
        with open(os.path.join(DESIGNER_DIR, name), "r", encoding="utf-8") as handle:
            return handle.read()

    def test_page_uses_the_shared_code_and_the_firmware_fonts(self):
        page = self.read("index.html")
        self.assertIn('src="assets.js"', page)
        self.assertIn('src="design.js"', page)

    def test_page_needs_no_internet(self):
        page = self.read("index.html")
        for match in re.finditer(r"""(?:src|href)\s*=\s*["'](https?:)?//[^"']+["']|url\(\s*["']?https?://""", page):
            line = page[:match.start()].count("\n") + 1
            context = page[max(0, match.start() - 60):match.end()]
            if "<a " in context:
                continue  # A link that a person clicks is fine
            self.fail("index.html line %d loads something from the internet: %s" % (line, match.group(0)))

    def test_fonts_of_the_page_are_in_the_folder_with_their_license(self):
        page = self.read("index.html")
        files = set(re.findall(r"url\(\s*[\"']?(fonts/[^\"')]+)", page))
        self.assertTrue(files, "the page has no font files")
        for name in files:
            self.assertTrue(os.path.isfile(os.path.join(DESIGNER_DIR, *name.split("/"))), name)
        for name in ("OFL-BarlowCondensed.txt", "OFL-JetBrainsMono.txt"):
            self.assertTrue(os.path.isfile(os.path.join(DESIGNER_DIR, "fonts", name)), name)
        other = [name for name in os.listdir(os.path.join(DESIGNER_DIR, "fonts"))
                 if not name.endswith((".woff2", ".txt"))]
        self.assertEqual(other, [], "only the free fonts and their license texts belong in this folder")

    def test_no_dialogs_that_stop_the_page(self):
        page = self.read("index.html")
        for call in ("alert(", "confirm(", "prompt("):
            self.assertIsNone(re.search(r"(?<![\w.])%s" % re.escape(call), page), call)


if __name__ == "__main__":
    unittest.main()
