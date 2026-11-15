"""Tests of the screen design tools (tools/design.py and updater/designer/).

The most important test: a design must look the same in the tools and on the device. The test
draws each kind of element two times, with the Python code of tools/design.py and with the real
firmware draw functions (the program gfx_tool, made from components/gfx/gfx.c), and compares
all pixels.
"""

import json
import os
import random
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))

import design  # noqa: E402

SCRIPT = os.path.join(REPO_ROOT, "tools", "design.py")
EXAMPLE = os.path.join(REPO_ROOT, "designs", "example.json")


def make(elements, note=""):
    return {"format": "stattrak-design", "version": 1, "name": "Test",
            "screens": [{"id": "s1", "name": "Screen 1", "note": note, "elements": elements}]}


def random_elements(rng, assets):
    """One screen with 1 to 8 random elements of all kinds, also partly outside of the display."""
    fonts = [font["name"] for font in assets["fonts"]]
    images = [image["name"] for image in assets["images"]]
    elements = []
    for _ in range(rng.randint(1, 8)):
        kind = rng.choice(design.ELEMENT_TYPES)
        color = rng.choice(["white", "white", "black"])
        x, y = rng.randint(-8, 130), rng.randint(-6, 34)
        if kind == "text":
            scale = rng.choice([1, 1, 1, 2, 3])
            elements.append({"type": "text", "x": x, "y": y, "color": color, "font": rng.choice(fonts),
                             "text": "".join(rng.choice("0123456789-AKZ %:/") for _ in range(rng.randint(1, 9))),
                             "align": rng.choice(["left", "center", "right"]), "scale": scale,
                             "gap": scale > 1 and rng.random() < 0.5,
                             "invert": scale == 1 and rng.random() < 0.3})
        elif kind == "rect":
            w, h = rng.randint(1, 60), rng.randint(1, 30)
            elements.append({"type": "rect", "x": x, "y": y, "w": w, "h": h, "color": color,
                             "fill": rng.random() < 0.5, "radius": rng.randint(0, (min(w, h) - 1) // 2)})
        elif kind == "line":
            elements.append({"type": "line", "x1": x, "y1": y, "x2": rng.randint(-8, 130),
                             "y2": rng.randint(-6, 34), "color": color})
        elif kind == "circle":
            elements.append({"type": "circle", "x": x, "y": y, "r": rng.randint(0, 20), "color": color,
                             "fill": rng.random() < 0.5})
        elif kind == "image":
            elements.append({"type": "image", "name": rng.choice(images), "x": x, "y": y, "color": color})
        else:
            w, h = rng.randint(1, 20), rng.randint(1, 12)
            elements.append({"type": "pixels", "x": x, "y": y, "color": color,
                             "rows": ["".join(rng.choice("#.") for _ in range(w)) for _ in range(h)]})
        if rng.random() < 0.3:
            elements[-1]["border"] = rng.randint(1, design.MAX_BORDER)
    return elements


class DesignTestCase(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.assets = design.load_assets()
        cls.tool = design.find_gfx_tool()

    def clean(self, data):
        clean, problems, _ = design.normalize(data, self.assets)
        self.assertEqual(problems, [])
        return clean

    def problems(self, elements):
        return design.normalize(make(elements), self.assets)[1]


class FirmwareParityTest(DesignTestCase):
    """The pictures of the design tools are the pictures of the firmware."""

    def setUp(self):
        if self.tool is None:
            self.skipTest("gfx_tool is not built (python tools/check.py --build-only)")

    def assert_same(self, elements):
        screen = self.clean(make(elements))["screens"][0]
        ours = design.draw_frame(screen, self.assets)
        firmware = design.firmware_frame(screen, self.tool)
        if ours != firmware:
            rows = ["tools: %s\nfirmw: %s" % (a, b) for a, b in zip(ours, firmware) if a != b]
            self.fail("The design tool and the firmware draw different pixels for %s\n%s"
                      % (json.dumps(elements), "\n".join(rows[:8])))

    def test_assets_file_is_made_from_this_firmware(self):
        with open(design.ASSETS_FILE, "r", encoding="utf-8") as handle:
            stored = handle.read()
        fresh = subprocess.run([self.tool, "assets"], stdout=subprocess.PIPE, check=True).stdout.decode("utf-8")
        expected = design.ASSETS_HEADER + design.ASSETS_PREFIX + json.dumps(
            json.loads(fresh), separators=(",", ":"), sort_keys=True) + ";\n"
        self.assertEqual(stored, expected, "The firmware fonts or pictures changed. "
                         "Run: python tools/design.py assets")

    def test_each_font_with_all_its_characters(self):
        text = "".join(chr(code) for code in range(32, 127))
        for font in self.assets["fonts"]:
            for start in range(0, len(text), 12):
                part = text[start:start + 12]
                if part.strip() == "":
                    continue
                with self.subTest(font=font["name"], text=part):
                    self.assert_same([{"type": "text", "x": 1, "y": 2, "text": part, "font": font["name"]}])

    def test_text_alignment(self):
        for font in ("5x7", "5x5", "3x5", "big", "numbers"):
            for align in ("left", "center", "right"):
                for text in ("7", "42", "1337", "-908"):
                    with self.subTest(font=font, align=align, text=text):
                        self.assert_same([{"type": "text", "x": 64, "y": 9, "text": text, "font": font,
                                           "align": align}])

    def test_text_scaled(self):
        for scale in (2, 3, 4):
            for gap in (False, True):
                for align in ("left", "center", "right"):
                    with self.subTest(scale=scale, gap=gap, align=align):
                        self.assert_same([{"type": "text", "x": 60, "y": 1, "text": "K17", "font": "5x7",
                                           "scale": scale, "gap": gap, "align": align}])

    def test_text_inverted_and_black(self):
        self.assert_same([{"type": "text", "x": 20, "y": 10, "text": "SOUND", "font": "5x5", "invert": True}])
        self.assert_same([{"type": "text", "x": 64, "y": 10, "text": "On", "font": "5x7", "invert": True,
                           "align": "center"}])
        self.assert_same([{"type": "rect", "x": 0, "y": 0, "w": 128, "h": 32, "fill": True},
                          {"type": "text", "x": 5, "y": 5, "text": "BLACK", "font": "big", "color": "black"}])

    def test_rectangles(self):
        for w in (1, 2, 3, 4, 7, 20, 128):
            for h in (1, 2, 3, 5, 12, 32):
                for fill in (False, True):
                    for radius in range(0, (min(w, h) - 1) // 2 + 1):
                        with self.subTest(w=w, h=h, fill=fill, radius=radius):
                            self.assert_same([{"type": "rect", "x": 0, "y": 0, "w": w, "h": h, "fill": fill,
                                               "radius": radius}])

    def test_lines(self):
        rng = random.Random(7)
        for _ in range(150):
            line = {"type": "line", "x1": rng.randint(-10, 137), "y1": rng.randint(-6, 37),
                    "x2": rng.randint(-10, 137), "y2": rng.randint(-6, 37)}
            with self.subTest(line=line):
                self.assert_same([line])

    def test_pictures_and_pixels(self):
        for image in self.assets["images"]:
            with self.subTest(image=image["name"]):
                self.assert_same([{"type": "image", "name": image["name"], "x": 3, "y": -2}])
        rng = random.Random(11)
        for _ in range(40):
            w, h = rng.randint(1, 40), rng.randint(1, 32)
            rows = ["".join(rng.choice("#.") for _ in range(w)) for _ in range(h)]
            with self.subTest(w=w, h=h):
                self.assert_same([{"type": "pixels", "x": rng.randint(-5, 120), "y": rng.randint(-5, 28),
                                   "rows": rows}])

    def test_circles(self):
        for r in range(0, 21):
            for fill in (False, True):
                for x, y in ((64, 15), (3, 30), (125, -2)):
                    with self.subTest(r=r, fill=fill, x=x, y=y):
                        self.assert_same([{"type": "circle", "x": x, "y": y, "r": r, "fill": fill}])

    def test_round_corners_larger_than_16(self):
        for w, h, r in ((60, 40, 19), (128, 35, 17), (100, 33, 16)):
            for fill in (False, True):
                with self.subTest(w=w, h=h, r=r, fill=fill):
                    self.assert_same([{"type": "rect", "x": 2, "y": -3, "w": w, "h": h, "fill": fill, "radius": r}])

    def test_cut_out_borders(self):
        background = {"type": "rect", "x": 0, "y": 0, "w": 128, "h": 32, "fill": True}
        parts = [
            {"type": "text", "x": 64, "y": 4, "text": "AK-47", "font": "5x7", "align": "center"},
            {"type": "text", "x": 64, "y": 12, "text": "OK", "font": "3x5", "align": "right", "invert": True},
            {"type": "text", "x": 10, "y": 6, "text": "12", "font": "5x5", "scale": 2, "gap": True},
            {"type": "rect", "x": 30, "y": 8, "w": 20, "h": 11, "radius": 5},
            {"type": "line", "x1": 0, "y1": 31, "x2": 127, "y2": 0},
            {"type": "circle", "x": 100, "y": 16, "r": 7, "fill": True},
            {"type": "image", "name": "warning", "x": 90, "y": 0},
            {"type": "pixels", "x": 40, "y": 20, "rows": ["#..#", ".##.", "#..#"]},
        ]
        for part in parts:
            for border in range(1, design.MAX_BORDER + 1):
                for color in ("white", "black"):
                    element = dict(part, border=border, color=color)
                    with self.subTest(element=element):
                        self.assert_same([background, element])

    def test_elements_outside_of_the_display(self):
        self.assert_same([{"type": "text", "x": 120, "y": 28, "text": "OUTSIDE", "font": "5x7"},
                          {"type": "rect", "x": -5, "y": -5, "w": 20, "h": 20, "radius": 3},
                          {"type": "rect", "x": 118, "y": 25, "w": 30, "h": 30, "fill": True, "radius": 4}])

    def test_random_screens(self):
        rng = random.Random(20261111)
        for round_number in range(60):
            elements = random_elements(rng, self.assets)
            with self.subTest(round=round_number):
                self.assert_same(elements)

    def test_example_design(self):
        clean, _ = design.load_design(EXAMPLE, self.assets)
        for screen in clean["screens"]:
            with self.subTest(screen=screen["id"]):
                self.assertEqual(design.draw_frame(screen, self.assets), design.firmware_frame(screen, self.tool))


class FormatTest(DesignTestCase):

    def test_example_design_is_correct(self):
        clean, warnings = design.load_design(EXAMPLE, self.assets)
        self.assertGreaterEqual(len(clean["screens"]), 2)
        self.assertEqual(warnings, [])

    def test_wrong_files_are_refused_with_a_plain_message(self):
        cases = [
            ([{"type": "triangle"}], "\"type\" must be one of"),
            ([{"type": "text", "text": "A", "font": "comic"}], "does not exist"),
            ([{"type": "text", "text": ""}], "must not be empty"),
            ([{"type": "text", "text": "Zaļš"}], "do not have these characters"),
            ([{"type": "text", "text": "A", "x": 1.5}], "whole number"),
            ([{"type": "text", "text": "A", "scale": 9}], "must be from 1 to 8"),
            ([{"type": "text", "text": "A", "scale": 2, "invert": True}], "scale 1 only"),
            ([{"type": "rect", "x": 0, "y": 0, "w": 0, "h": 5}], "must be from 1"),
            ([{"type": "rect", "x": 0, "y": 0, "w": 10, "h": 5, "radius": 3}], "too large"),
            ([{"type": "line", "x1": 0, "y1": 0, "x2": 5}], "whole number"),
            ([{"type": "image", "name": "cat"}], "does not exist"),
            ([{"type": "pixels", "rows": ["##", "#"]}], "same length"),
            ([{"type": "pixels", "rows": ["#x"]}], "same length"),
            ([{"type": "rect", "x": 0, "y": 0, "w": 3, "h": 3, "color": "orange"}], "\"color\" must be"),
        ]
        for elements, expected in cases:
            with self.subTest(elements=elements):
                problems = self.problems(elements)
                self.assertTrue(any(expected in problem for problem in problems), problems)

    def test_wrong_top_level(self):
        for data in ({"format": "x", "version": 1, "screens": [{}]}, {"format": "stattrak-design", "version": 2,
                     "screens": [{}]}, {"format": "stattrak-design", "version": 1, "screens": []}):
            with self.subTest(data=data):
                self.assertNotEqual(design.normalize(data, self.assets)[1], [])
        same_id = make([])
        same_id["screens"].append(dict(same_id["screens"][0]))
        self.assertTrue(any("two times" in problem for problem in design.normalize(same_id, self.assets)[1]))

    def test_warnings(self):
        warnings = design.normalize(make([{"type": "text", "text": "12a", "font": "numbers"},
                                          {"type": "text", "text": "abc", "font": "5x5"}]), self.assets)[2]
        self.assertEqual(len(warnings), 2)

    def test_text_width(self):
        self.assertEqual(design.text_width(self.assets["font_by_name"]["5x7"], "ABC"), 17)
        self.assertEqual(design.text_width(self.assets["font_by_name"]["3x5"], "A"), 3)

    def test_draw_without_the_firmware_tool(self):
        screen = self.clean(make([{"type": "rect", "x": 0, "y": 0, "w": 3, "h": 2, "fill": True},
                                  {"type": "pixels", "x": 126, "y": 30, "rows": ["##", ".#"]}]))["screens"][0]
        frame = design.draw_frame(screen, self.assets)
        self.assertEqual(len(frame), 32)
        self.assertEqual(frame[0], "###" + "." * 125)
        self.assertEqual(frame[1], "###" + "." * 125)
        self.assertEqual(frame[30][-2:], "##")
        self.assertEqual(frame[31][-2:], ".#")
        self.assertEqual(sum(row.count("#") for row in frame), 9)


class OutputTest(DesignTestCase):

    def run_tool(self, *args):
        result = subprocess.run([sys.executable, SCRIPT] + list(args), stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, universal_newlines=True)
        return result.returncode, result.stdout, result.stderr

    def test_describe_has_the_notes_the_elements_and_the_pixels(self):
        code, out, _ = self.run_tool("describe", EXAMPLE)
        self.assertEqual(code, 0)
        clean, _ = design.load_design(EXAMPLE, self.assets)
        for screen in clean["screens"]:
            self.assertIn("\"%s\" (id: %s)" % (screen["name"], screen["id"]), out)
            self.assertIn(screen["note"], out)
            for row in design.draw_frame(screen, self.assets):
                self.assertIn(row, out)
        self.assertIn("128 x 32", out)

    def test_code_has_one_function_for_each_screen(self):
        code, out, _ = self.run_tool("code", EXAMPLE)
        self.assertEqual(code, 0)
        clean, _ = design.load_design(EXAMPLE, self.assets)
        for screen in clean["screens"]:
            self.assertIn("static void _draw%s%s(void) {" % (screen["id"][0].upper(), screen["id"][1:]), out)
        self.assertEqual(out.count("{"), out.count("}"))

    def test_code_escapes_the_text(self):
        clean = self.clean(make([{"type": "text", "text": "A\"B\\C", "font": "5x7"}]))
        self.assertIn('"A\\"B\\\\C"', design.c_code(clean, self.assets))

    def test_code_with_notes_of_more_than_one_line(self):
        data = make([{"type": "line", "x1": 0, "y1": 0, "x2": 5, "y2": 0, "note": "first\nsecond\\\n"}],
                    note="Line one\nLine two")
        data["name"] = "My\ndesign"
        data["screens"][0]["name"] = "Screen\none"
        data["screens"][0]["elements"].append({"type": "pixels", "x": 0, "y": 2, "rows": ["#"]})
        out = design.c_code(self.clean(data), self.assets)
        for line in out.split("\n"):
            stripped = line.strip()
            self.assertTrue(stripped == "" or stripped.startswith(("//", "static ", "Gfx_", "0x", "}")), line)
        self.assertIn("// Screen \"Screen one\": Line one Line two", out)
        self.assertIn("// first second\n", out)

    def test_code_of_free_pixels_is_a_firmware_picture(self):
        clean = self.clean(make([{"type": "pixels", "x": 4, "y": 5, "rows": ["#.", ".#", "##"]}]))
        out = design.c_code(clean, self.assets)
        self.assertIn("static const uint8_t kS1Pixels1[] = {\n  0x05, 0x06,\n};", out)
        self.assertIn("Gfx_drawBitmapRaw(4, 5, kS1Pixels1, 2, 3, 1, GFX_ROTATION__0);", out)

    def test_render_makes_a_picture_of_each_screen(self):
        with tempfile.TemporaryDirectory() as out_dir:
            code, out, err = self.run_tool("render", EXAMPLE, "--out", out_dir)
            self.assertEqual(code, 0, err)
            clean, _ = design.load_design(EXAMPLE, self.assets)
            files = sorted(os.listdir(out_dir))
            self.assertEqual(len(files), len(clean["screens"]))
            for name in files:
                with open(os.path.join(out_dir, name), "rb") as handle:
                    self.assertEqual(handle.read(8), b"\x89PNG\r\n\x1a\n")

    def test_wrong_file_gives_exit_code_1(self):
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "bad.json")
            with open(path, "w", encoding="utf-8") as handle:
                json.dump(make([{"type": "text", "text": "A", "font": "nothing"}]), handle)
            code, _, err = self.run_tool("check", path)
            self.assertEqual(code, 1)
            self.assertIn("does not exist", err)

            code, _, err = self.run_tool("check", os.path.join(folder, "missing.json"))
            self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
