"""App engine 4 on the simulated device (the firmware engine, the app host and the buttons):
lists, held(), more variables and more steps (docs/APPS.md section 4.1)."""

import os
import sys
import unittest

from test_apps import AppTestCase, INSTALLED

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))
import app  # noqa: E402

SOURCE = """app "LISTS"
var xs[60]
var shown
on open {
  var i = 0
  while i < len(xs) {
    xs[i] = i
    i += 1
  }
}
on draw {
  # About 9000 steps: more than engine 1 to 3 allow (4000)
  var i = 0
  var sum = 0
  while i < 500 {
    sum += xs[i % 60]
    i += 1
  }
  if held(2) { fill(0, 0, 64, 32, on) }
  num(127, 0, sum, 0, text, on, right)
}
on press1 { shown = xs[shown + 100] }
"""


class EngineFourTest(AppTestCase):

    def setUp(self):
        super().setUp()
        self.package = app.compile_source(SOURCE, "lists.sts", ".")["package"]
        self.app_id = app.check(self.package)[1]["id"]
        self.assertEqual(self.package[5], 4)
        result = self.install(self.package)
        self.assertEqual(result["state"], INSTALLED, result)
        self.leave_sync()
        self.open_app(self.app_id)

    def left_half_on(self):
        frame = self.dev.frame()
        return sum(row[:64].count("#") for row in frame)

    def test_lists_and_many_steps_run(self):
        state = self.dev.state()
        self.assertEqual(state["view"], "APP_HOST")
        self.assertEqual(state["app_error"], 0)
        # sum of xs[i % 60] for i < 500 = 8 * (0 + .. + 59) + (0 + .. + 19) = 14160 + 190
        machine = app.Machine(self.package)
        machine.run("open")
        self.assertEqual(self.dev.frame()[-32:], machine.frame()[-32:])

    def test_held_is_1_while_the_button_is_down(self):
        self.assertLess(self.left_half_on(), 50)
        self.dev.press(2)
        self.dev.run(300)
        self.assertEqual(self.left_half_on(), 64 * 32)
        self.dev.release(2)
        self.dev.run(500)
        self.assertLess(self.left_half_on(), 50)
        self.assertEqual(self.dev.state()["app_error"], 0)

    def test_a_bad_index_is_an_app_error(self):
        self.dev.click(1)
        self.assertEqual(self.dev.state()["app_error_text"], "bad index")
        # Both buttons still open the menu
        self.dev.click_both()
        self.assertEqual(self.dev.view, "LAUNCHER")


if __name__ == "__main__":
    unittest.main()
