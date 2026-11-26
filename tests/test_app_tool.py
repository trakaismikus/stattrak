"""Tests of the app tool (tools/app.py): the compiler of StatTrak Script, the check of app
packages, the title pictures and the example apps in apps/.

The check here must give the same result as Stapp_check() of the firmware engine
(components/stapp/). The broken packages of these tests are the cases that both must refuse.
"""

import os
import re
import struct
import sys
import contextlib
import io
import tempfile
import unittest
import zlib

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))

import app  # noqa: E402
import app_icons  # noqa: E402

EXAMPLES = ("round-timer", "headshot", "reaction")


def example_path(name):
    return os.path.join(REPO_ROOT, "apps", name, name + ".sts")


def compile_text(source):
    return app.compile_source(source, "test.sts", REPO_ROOT)


def with_crc(data):
    """Write the right CRC at the end, after a test changed a byte"""
    data = bytearray(data)
    struct.pack_into("<I", data, len(data) - 4, zlib.crc32(bytes(data[:-4])) & 0xFFFFFFFF)
    return bytes(data)


def small_package(code, handlers=None, permissions=0, global_count=2, saved_count=0, settings=(),
                  strings=(), bitmaps=(), name="TEST", app_id=0x12345678):
    """A package with exactly this code (bytes) and these handlers (default: open at 0)"""
    if handlers is None:
        handlers = [0] + [app.NO_HANDLER] * 7
    picture = bytes(app.PICTURE_SIZE)
    return app.assemble_package(name, app_id, 1, permissions, picture, handlers, list(settings), list(strings),
                                list(bitmaps), bytes(code), global_count, saved_count)


def code_of(*parts):
    """Bytes from opcode names and numbers: code_of("PUSH8", 1, "HALT")"""
    out = bytearray()
    for part in parts:
        out.append(app.OP[part] if isinstance(part, str) else part & 0xFF)
    return bytes(out)


class ExampleApps(unittest.TestCase):
    def test_each_example_compiles_and_passes_the_check(self):
        for name in EXAMPLES:
            with self.subTest(app=name):
                result = app.compile_file(example_path(name))
                status, info = app.check(result["package"])
                self.assertEqual(app.result_text(status), "ok")
                self.assertLessEqual(info["size"], app.MAX_SIZE)
                self.assertEqual(result["warnings"], [])
                self.assertTrue(result["description"])

    def test_permissions_of_the_examples(self):
        words = {name: app.permission_words(app.compile_file(example_path(name))["info"]["permissions"])
                 for name in EXAMPLES}
        self.assertEqual(words, {"round-timer": ["sound"], "headshot": ["counters"], "reaction": ["sound"]})

    def test_round_timer_beeps_three_times_when_the_round_is_over(self):
        machine = app.Machine(app.compile_file(example_path("round-timer"))["package"])
        self.assertEqual(machine.run("open"), "ok")
        self.assertEqual(machine.globals[0], 1)              # saved round = 1 at the first start
        self.assertEqual(machine.run("press1"), "ok")         # start
        machine.wait(59000)
        self.assertEqual(machine.clicks, [(10000, 1000)])     # only the click of the start
        machine.wait(3000)
        beeps = [freq for _, freq in machine.clicks[1:]]
        self.assertEqual(beeps, [2000, 2000, 2000])
        self.assertEqual(machine.run("press2"), "ok")         # next round
        self.assertEqual(machine.globals[0], 2)
        self.assertEqual(machine.run("long2"), "ok")          # back to round 1
        self.assertEqual(machine.globals[0], 1)
        self.assertIsNone(machine.error)

    def test_round_timer_sound_setting_off(self):
        machine = app.Machine(app.compile_file(example_path("round-timer"))["package"])
        machine.settings[1] = 1    # SOUND: OFF
        machine.run("open")
        machine.run("press1")
        machine.wait(65000)
        self.assertEqual(machine.clicks, [])

    def test_headshot_counts_and_undoes(self):
        machine = app.Machine(app.compile_file(example_path("headshot"))["package"])
        machine.run("open")
        for event in ("press1", "press2", "press2", "press1"):
            self.assertEqual(machine.run(event), "ok")
        self.assertEqual(machine.counters, [4, 0, 4, 0])
        self.assertEqual(machine.globals[0], 2)               # headshots
        machine.wait(1000)
        machine.run("long1")                                  # undo the last kill (no headshot)
        machine.wait(400)
        machine.run("long1")                                  # the same long press: nothing
        self.assertEqual(machine.counters, [3, 0, 3, 0])
        machine.wait(1000)
        machine.run("long2")                                  # a new long press: undo a headshot kill
        self.assertEqual(machine.counters, [2, 0, 2, 0])
        self.assertEqual(machine.globals[0], 1)
        screen = "\n".join(machine.frame())
        self.assertIn("#", screen)
        self.assertIsNone(machine.error)

    def test_headshot_starts_again_after_a_game_reset(self):
        machine = app.Machine(app.compile_file(example_path("headshot"))["package"])
        machine.run("open")
        machine.run("press2")
        machine.run("close")
        machine.counters[2] = 0       # the game counter was reset in the firmware
        machine.run("open")
        self.assertEqual(machine.globals[0], 0)

    def test_reaction_keeps_the_best_time(self):
        machine = app.Machine(app.compile_file(example_path("reaction"))["package"])
        machine.run("open")
        machine.run("press1")
        waited = 0
        while machine.globals[1] != 2 and waited < 5000:     # GO comes after 1 to 4 seconds
            machine.wait(10)
            waited += 10
        self.assertTrue(1000 <= waited <= 4010, waited)
        machine.wait(250)
        machine.run("press2")
        self.assertEqual(machine.globals[1], 3)               # state RESULT
        self.assertEqual(machine.globals[0], 250)             # best
        self.assertEqual(machine.clicks[-1][1], 2000)
        machine.run("press1")
        machine.wait(100)
        machine.run("press1")                                 # too soon
        self.assertEqual(machine.globals[1], 4)
        self.assertEqual(machine.globals[0], 250)
        self.assertIsNone(machine.error)

    def test_catalog(self):
        with tempfile.TemporaryDirectory() as folder:
            catalog = app.build_catalog(os.path.join(REPO_ROOT, "apps"), folder)
            names = [entry["name"] for entry in catalog["apps"]]
            self.assertEqual(names, ["BATTERY LOG", "CLIP MARKS", "HEADSHOT %", "REACTION", "RESPAWN TIMER", "ROUND TIMER", "RUNNER", "SLOTS",
                                 "WALLPAPER"])
            for entry in catalog["apps"]:
                with open(os.path.join(folder, entry["file"]), "rb") as handle:
                    data = handle.read()
                status, info = app.check(data)
                self.assertEqual(status, app.OK)
                self.assertEqual(entry["id"], "%08x" % info["id"])
                self.assertEqual(entry["size"], len(data))
                self.assertEqual(len(app.base64.b64decode(entry["picture"])), 240)
            timer = catalog["apps"][names.index("ROUND TIMER")]
            self.assertEqual(timer["settings"][0]["name"], "ROUND TIME")
            self.assertEqual(timer["settings"][0]["default"], 1)
            self.assertTrue(os.path.isfile(os.path.join(folder, "catalog.json")))


class Check(unittest.TestCase):
    """Each kind of broken package gets the right result (the same as Stapp_check)"""

    def setUp(self):
        self.good = app.compile_file(example_path("round-timer"))["package"]

    def assertResult(self, data, text):
        self.assertEqual(app.result_text(app.check(data)[0]), text)

    def code_offset(self, data):
        return struct.unpack_from("<H", data, 44)[0]

    def test_good(self):
        self.assertResult(self.good, "ok")

    def test_result_texts(self):
        self.assertEqual(app.RESULTS[:5], ("ok", "bad size", "bad magic", "bad version", "bad crc"))
        self.assertEqual(len(app.RESULTS), 19)
        self.assertEqual(app.RESULTS[-1], "bad index")

    def test_truncated(self):
        self.assertResult(self.good[:-1], "bad size")
        self.assertResult(self.good[:100], "bad size")
        self.assertResult(with_crc(self.good[:-10] + self.good[-4:]), "bad size")   # header size is wrong

    def test_too_large(self):
        data = bytearray(self.good)
        data[-4:-4] = bytes(app.MAX_SIZE)
        struct.pack_into("<H", data, 14, len(data) & 0xFFFF)
        self.assertResult(with_crc(data), "bad size")

    def test_bad_magic(self):
        self.assertResult(with_crc(b"STAX" + self.good[4:]), "bad magic")

    def test_bad_version(self):
        data = bytearray(self.good)
        data[5] = app.ENGINE_VERSION + 1   # needs a newer engine
        self.assertResult(with_crc(data), "bad version")

    def test_bad_crc(self):
        data = bytearray(self.good)
        data[200] ^= 1
        self.assertResult(data, "bad crc")

    def test_bad_header(self):
        for offset, value in ((8, 0), (8, 3), (16, ord("a")), (32, 49), (33, 15), (37, 1), (6, 4)):
            data = bytearray(self.good)
            if offset == 8:
                struct.pack_into("<I", data, 8, value)
            else:
                data[offset] = value
            with self.subTest(offset=offset, value=value):
                self.assertResult(with_crc(data), "bad header")

    def test_overlapping_blocks(self):
        data = bytearray(self.good)
        strings_offset = struct.unpack_from("<H", data, 40)[0]
        struct.pack_into("<H", data, 44, strings_offset)      # the code starts in the string table
        self.assertResult(with_crc(data), "bad layout")

    def test_block_outside(self):
        data = bytearray(self.good)
        struct.pack_into("<H", data, 44, 100)                 # the code in the picture
        self.assertResult(with_crc(data), "bad layout")
        data = bytearray(self.good)
        struct.pack_into("<H", data, 46, 3000)                # code larger than 2048
        self.assertResult(with_crc(data), "bad layout")

    def test_bad_settings(self):
        package = small_package(code_of("HALT"), settings=[("A", ["1", "2"], 2)])     # default >= count
        self.assertResult(package, "bad settings")
        package = small_package(code_of("HALT"), settings=[("A", ["1", "2"], 0)])
        self.assertResult(package, "ok")
        data = bytearray(package)
        data[app.DATA_START + 12] = 7                         # 7 values
        self.assertResult(with_crc(data), "bad settings")

    def test_bad_strings(self):
        long_text = "X" * 21
        self.assertResult(small_package(code_of("HALT"), strings=[long_text]), "bad strings")
        # A text() call with a fixed string id that is not in the table
        code = code_of("PUSH8", 0, "PUSH8", 0, "PUSH8", 1, "PUSH8", 0, "PUSH8", 1, "PUSH8", 0, "SYS", 6, "DROP", "HALT")
        self.assertResult(small_package(code, strings=["A"]), "bad strings")
        code = code_of("PUSH8", 0, "PUSH8", 0, "PUSH8", 0, "PUSH8", 0, "PUSH8", 1, "PUSH8", 0, "SYS", 6, "DROP", "HALT")
        self.assertResult(small_package(code, strings=["A"]), "ok")

    def test_bad_bitmaps(self):
        self.assertResult(small_package(code_of("HALT"), bitmaps=[(0, 8, b"")]), "bad bitmaps")
        code = code_of("PUSH8", 0, "PUSH8", 0, "PUSH8", 1, "PUSH8", 1, "SYS", 9, "DROP", "HALT")
        self.assertResult(small_package(code, bitmaps=[(1, 1, b"\x01")]), "bad bitmaps")

    def test_unknown_opcode(self):
        self.assertResult(small_package(code_of(0x50, "HALT")), "bad code")

    def test_operand_outside_the_code(self):
        self.assertResult(small_package(code_of("HALT", "PUSH16", 1)), "bad code")

    def test_bad_jump(self):
        # JMP +1 lands in the middle of the PUSH8
        self.assertResult(small_package(code_of("JMP", 1, 0, "PUSH8", 5, "HALT")), "bad code")
        self.assertResult(small_package(code_of("JMP", 2, 0, "PUSH8", 5, "HALT")), "ok")
        # A jump outside of the code
        self.assertResult(small_package(code_of("JMP", 0x10, 0, "HALT")), "bad code")
        # A call that does not land on an instruction start
        self.assertResult(small_package(code_of("CALL", 2, 0, "HALT")), "bad code")

    def test_bad_jump_in_a_compiled_app(self):
        data = bytearray(self.good)
        start = self.code_offset(data)
        code = bytes(data[start:start + struct.unpack_from("<H", data, 46)[0]])
        jump = next(pc for pc, opcode, _, _ in app.decode(code) if app.OPCODES[opcode][0] == "JZ")
        struct.pack_into("<h", data, start + jump + 1, struct.unpack_from("<h", data, start + jump + 1)[0] + 1)
        self.assertResult(with_crc(data), "bad code")

    def test_code_must_end_with_halt_jump_or_ret(self):
        self.assertResult(small_package(code_of("PUSH8", 1)), "bad code")

    def test_unknown_sys(self):
        self.assertResult(small_package(code_of("SYS", 11, "HALT")), "bad sys")
        self.assertResult(small_package(code_of("SYS", 99, "HALT")), "bad sys")

    def test_sys_without_permission(self):
        code = code_of("PUSH16", 0xE8, 0x03, "SYS", 20, "DROP", "HALT")     # click(1000)
        self.assertResult(small_package(code), "bad sys")
        self.assertResult(small_package(code, permissions=app.PERM_SOUND), "ok")
        code = code_of("PUSH8", 0, "PUSH8", 1, "SYS", 17, "DROP", "HALT")   # add_counter(0, 1)
        self.assertResult(small_package(code, permissions=app.PERM_SOUND), "bad sys")
        self.assertResult(small_package(code, permissions=app.PERM_COUNTERS), "ok")

    def test_global_out_of_range(self):
        self.assertResult(small_package(code_of("LOAD", 2, "DROP", "HALT"), global_count=2), "bad global")
        self.assertResult(small_package(code_of("STORE", 1, "HALT"), global_count=2), "ok")

    def test_handler_not_on_an_instruction_start(self):
        handlers = [0, 1] + [app.NO_HANDLER] * 6
        self.assertResult(small_package(code_of("PUSH8", 1, "HALT"), handlers=handlers), "bad handler")
        handlers = [0, 2] + [app.NO_HANDLER] * 6
        self.assertResult(small_package(code_of("PUSH8", 1, "HALT"), handlers=handlers), "ok")

    def test_handler_outside_the_code(self):
        handlers = [5] + [app.NO_HANDLER] * 7
        self.assertResult(small_package(code_of("HALT"), handlers=handlers), "bad handler")


class Compiler(unittest.TestCase):
    def disassembly(self, source):
        result = compile_text(source)
        parts = app.package_parts(result["package"])
        return app.disassemble(parts["code"], parts["handlers"])

    def test_if_and_assignment(self):
        lines = self.disassembly('app "T"\nvar x = 0\non press1 {\n  x = x + 1\n  if x > 3 { x = 0 }\n}\n')
        self.assertEqual(lines, [
            "on press1:",
            "0000  LOAD    g0",
            "0002  PUSH8   1",
            "0004  ADD",
            "0005  STORE   g0",
            "0007  LOAD    g0",
            "0009  PUSH8   3",
            "000b  GT",
            "000c  JZ      0013",
            "000f  PUSH8   0",
            "0011  STORE   g0",
            "0013  HALT",
        ])

    def test_function_and_system_call(self):
        lines = self.disassembly('app "F"\nfunc twice(a) { return a * 2 }\n'
                                 'on draw { num(0, 0, twice(21), 0, text, on, left) }\n')
        self.assertEqual(lines, [
            "on draw:",
            "0000  PUSH8   0",
            "0002  PUSH8   0",
            "0004  PUSH8   21",
            "0006  CALL    0015",
            "0009  PUSH8   0",
            "000b  PUSH8   0",
            "000d  PUSH8   1",
            "000f  PUSH8   0",
            "0011  SYS     num",
            "0013  DROP",
            "0014  HALT",
            "func 0015:",
            "0015  STORE   g0",
            "0017  LOAD    g0",
            "0019  PUSH8   2",
            "001b  MUL",
            "001c  RET",
            "001d  PUSH8   0",
            "001f  RET",
        ])

    def test_while_and_logic(self):
        lines = self.disassembly('app "W"\non tick {\n  var i = 0\n  while i < 3 && !0 { i += 1 }\n}\n')
        self.assertEqual(lines, [
            "on tick:",
            "0000  PUSH8   0",
            "0002  STORE   g0",
            "0004  LOAD    g0",
            "0006  PUSH8   3",
            "0008  LT",
            "0009  JZ      0017",
            "000c  PUSH8   0",
            "000e  NOT",
            "000f  JZ      0017",
            "0012  PUSH8   1",
            "0014  JMP     0019",
            "0017  PUSH8   0",
            "0019  JZ      0026",
            "001c  LOAD    g0",
            "001e  PUSH8   1",
            "0020  ADD",
            "0021  STORE   g0",
            "0023  JMP     0004",
            "0026  HALT",
        ])

    def test_start_values(self):
        source = 'app "S"\nsaved best = 5\nvar speed = 300\n'
        self.assertEqual(self.disassembly(source), [
            "on open:",
            "0000  PUSH16  300",
            "0003  STORE   g2",
            "0005  LOAD    g1",
            "0007  JNZ     0012",
            "000a  PUSH8   5",
            "000c  STORE   g0",
            "000e  PUSH8   1",
            "0010  STORE   g1",
            "0012  HALT",
        ])
        result = compile_text(source)
        self.assertEqual(result["info"]["saved_count"], 2)    # best + the hidden "started"
        machine = app.Machine(result["package"])
        machine.run("open")
        self.assertEqual(machine.globals[:3], [5, 1, 300])
        machine.globals[0] = 7                                # a saved value from flash
        machine.globals[2] = 0
        machine.run("open")
        self.assertEqual(machine.globals[:3], [7, 1, 300])

    def test_large_numbers_and_strings(self):
        result = compile_text('app "N"\non draw {\n  text(1, 2, "HI", small, on, left)\n'
                              '  num(0, 8, 100000, 0, text, on, left)\n  text(1, 2, "HI", small, on, left)\n}\n')
        parts = app.package_parts(result["package"])
        self.assertEqual(parts["strings"], ["HI"])
        self.assertIn("PUSH32  100000", "\n".join(app.disassemble(parts["code"])))

    def test_handler_variables_share_places(self):
        result = compile_text('app "V"\non press1 { var a = 1 }\non press2 { var b = 2\n var c = 3 }\n')
        self.assertEqual(result["info"]["global_count"], 2)

    def test_hidden_globals_for_parameters(self):
        result = compile_text('app "P"\nvar x = 0\nfunc f(a, b) { var c = a + b\n return c }\n'
                              'on press1 { x = f(1, 2) }\n')
        self.assertEqual(result["info"]["global_count"], 4)
        machine = app.Machine(result["package"])
        machine.run("press1")
        self.assertEqual(machine.globals[0], 3)

    def test_engine_arithmetic(self):
        result = compile_text('app "A"\nvar a = 0\nvar b = 0\nvar c = 0\nvar d = 0\non press1 {\n'
                              '  a = -7 / 2\n  b = -7 % 2\n  c = 5 / 0\n  d = (1 << 31) - 1\n}\n')
        machine = app.Machine(result["package"])
        machine.run("press1")
        self.assertEqual(machine.globals[:4], [-3, -1, 0, 0x7FFFFFFF])

    def test_no_sound_in_draw(self):
        # As in the firmware: click and tone do nothing in draw (they work in the other handlers)
        machine = app.Machine(compile_text('app "S"\npermission sound\non draw { click(1000); tone(500, 100) }\n'
                                           'on tick { tone(700, 100) }\n')["package"])
        machine.wait(100)
        self.assertEqual(machine.clicks, [(10100, 700)])

    def test_down_events_make_a_format_2_package(self):
        # Without down1 / down2: format 1 (8 handlers, data from 304), as before
        old = compile_text('app "OLD"\non press1 { click(900) }\npermission sound\n')["package"]
        self.assertEqual(old[4], 1)
        self.assertEqual(struct.unpack_from("<H", old, 44)[0], 304)
        # With down2: format 2 (10 handlers, data from 308); down2 runs at once, press1 later
        new = compile_text('app "NEW"\npermission sound\non down2 { click(1400) }\non press1 { click(900) }\n')["package"]
        self.assertEqual(new[4], 2)
        self.assertEqual(struct.unpack_from("<H", new, 44)[0], 308)
        self.assertEqual(app.check(new)[0], app.OK)
        machine = app.Machine(new)
        self.assertEqual(machine.run("down1"), "ok")     # No handler: nothing
        self.assertEqual(machine.run("down2"), "ok")
        self.assertEqual(machine.run("press1"), "ok")
        self.assertEqual([freq for _, freq in machine.clicks], [1400, 900])
        # A format 2 header on a package of 308 bytes: too small (format 2 needs 312)
        data = bytearray(old[:300]) + bytearray(4) + bytearray(4)
        data[4] = 2
        struct.pack_into("<H", data, 14, len(data))
        self.assertEqual(app.result_text(app.check(with_crc(data))[0]), "bad size")
        # Format 4 does not exist
        data = bytearray(new)
        data[4] = 4
        self.assertEqual(app.result_text(app.check(with_crc(data))[0]), "bad version")

    def test_data_and_awake_make_a_format_3_package(self):
        # docs/DATA.md section 6: data columns, awake, engine 2 calls
        source = ('app "LOG"\ndata "TIME" "MV"\npermission awake\n'
                  'on press1 { log(time(), battery_mv()) }\non draw { num(0, 0, log_count(), 0, text, on, left) }\n')
        package = compile_text(source)["package"]
        self.assertEqual((package[4], package[5], package[37]), (3, 2, 2))
        parts = app.package_parts(package)
        self.assertEqual(parts["column_names"], ["TIME", "MV"])
        self.assertEqual(parts["permissions"], app.PERM_DATA | app.PERM_AWAKE)
        machine = app.Machine(package)
        machine.battery_mv = 3711
        machine.run("press1")
        machine.now += 1000
        machine.run("press1")
        self.assertEqual(machine.data, [[10000, 3711], [11000, 3711]])
        # battery_mv() alone: format 3, engine 2, no data
        package = compile_text('app "MV"\non draw { num(0, 0, battery_mv(), 0, text, on, left) }\n')["package"]
        self.assertEqual((package[4], package[5], package[37]), (3, 2, 0))
        # The old apps stay format 1 / engine 1
        old = compile_text('app "OLD"\non press1 { click(900) }\npermission sound\n')["package"]
        self.assertEqual((old[4], old[5]), (1, 1))

    def test_popup_needs_background_and_engine_3(self):
        # docs/APPS.md section 4.6: popup() = engine 3, format 3, only with the permission background
        package = compile_text('app "POP"\npermission background\non tick { popup() }\n')["package"]
        self.assertEqual((package[4], package[5]), (3, 3))
        self.assertEqual(app.check(package)[0], app.OK)
        machine = app.Machine(package)
        machine.run("tick")
        self.assertEqual(machine.popups, 1)
        with self.assertRaises(app.AppError) as error:
            compile_text('app "POP"\non tick { popup() }\n')
        self.assertIn("background", str(error.exception))

    # Engine 4 (docs/APPS.md section 7): lists, len(), held(), the `engine 4` line, the larger limits
    def test_lists(self):
        source = """app "LISTS"
saved best[3] = { 5, 0, 7 }
var xs[8]
var tune[4] = {
  440, 494,
  523, 587
}
var total
on open {
  var i = 0
  while i < len(xs) {
    xs[i] = i * 3
    i += 1
  }
  xs[2] += 100
  xs[3] -= 1
  total = tune[1] + best[2] + xs[2] + xs[3]
}
on press1 { total = xs[total] }
"""
        result = compile_text(source)
        package = result["package"]
        self.assertEqual((package[4], package[5]), (3, 4))      # a list: engine 4 (format 3)
        self.assertEqual(app.check(package)[0], app.OK)
        self.assertEqual(result["info"]["saved_count"], 4)      # 3 values + the hidden "started"
        machine = app.Machine(package)
        self.assertEqual(machine.run("open"), "ok")
        self.assertEqual(machine.globals[:3], [5, 0, 7])
        self.assertEqual(machine.globals[4:12], [0, 3, 106, 8, 12, 15, 18, 21])
        self.assertEqual(machine.globals[12:16], [440, 494, 523, 587])
        self.assertEqual(machine.globals[16], 494 + 7 + 106 + 8)
        self.assertEqual(machine.run("press1"), "bad index")    # xs[615]
        self.assertIn("LOADX   g4[8]", "\n".join(app.disassemble(app.package_parts(package)["code"])))

    def test_list_errors(self):
        for source, text in (
                ('app "A"\nvar xs[4]\non press1 { xs = 1 }\n', "is a list"),
                ('app "A"\nvar xs[4]\nvar y\non press1 { y = xs }\n', "is a list"),
                ('app "A"\nvar xs[4]\nvar y\non press1 { y = xs[4] }\n', "has 4 values (0 to 3)"),
                ('app "A"\nvar xs[4]\non press1 { xs[-1] = 2 }\n', "has 4 values"),
                ('app "A"\nvar y\non press1 { y[0] = 2 }\n', "is not a list"),
                ('app "A"\nvar y\non press1 { y = len(y) }\n', "the name of a list"),
                ('app "A"\nvar xs[0]\n', "from 1 to 255"),
                ('app "A"\nvar xs[2] = { 1, 2, 3 }\n', "too many"),
                ('app "A"\nvar xs[201]\n', "more than 200 variables"),
                ('app "A"\nsaved xs[17]\n', "16 saved values"),
                ('app "A"\nengine 3\n', "engine 4"),
                ('app "A"\non press1 { var y = held(3) }\n', "from 1 to 2")):
            with self.subTest(source=source):
                with self.assertRaises(app.AppError) as raised:
                    compile_text(source)
                self.assertIn(text, str(raised.exception))

    def test_a_list_makes_engine_4_and_old_parameter_names_stay(self):
        self.assertEqual(compile_text('app "A"\nvar xs[3]\nvar n\non open { n = len(xs) }\n')["package"][5], 4)
        package = compile_text('app "A"\nvar y\nfunc f(min, abs) { return min + abs }\non open { y = f(1, 2) }\n')["package"]
        self.assertEqual(package[5], 1)

    def test_list_size_from_a_const(self):
        package = compile_text('app "A"\nconst N = 12\nvar xs[N]\nvar n\non open { n = len(xs) }\n')["package"]
        machine = app.Machine(package)
        machine.run("open")
        self.assertEqual(machine.globals[12], 12)

    def test_held(self):
        package = compile_text('app "H"\nvar a\nvar b\non draw { a = held(1)\n b = held(2) }\n')["package"]
        self.assertEqual(package[5], 4)
        machine = app.Machine(package)
        machine.run("draw")
        self.assertEqual(machine.globals[:2], [0, 0])
        for event, expected in (("down1", [1, 0]), ("long1", [1, 0]), ("down2", [1, 1]), ("up1", [0, 1]),
                                ("press2", [0, 0]), ("down2", [0, 1]), ("press2", [0, 0])):
            self.assertEqual(machine.run(event), "ok")
            machine.run("draw")
            self.assertEqual(machine.globals[:2], expected, event)

    def test_old_scripts_keep_held_and_len_as_their_own_names(self):
        # Scripts from before engine 4 can have a variable or a function with these names
        package = compile_text('app "A"\nvar held = 2\nvar len\non press1 { len = held + 1 }\n')["package"]
        self.assertEqual(package[5], 1)
        machine = app.Machine(package)
        machine.run("open")
        machine.run("press1")
        self.assertEqual(machine.globals[:2], [2, 3])
        package = compile_text('app "A"\nvar y\nfunc held(n) { return n * 2 }\non press1 { y = held(4) }\n')["package"]
        self.assertEqual(package[5], 1)
        machine = app.Machine(package)
        machine.run("press1")
        self.assertEqual(machine.globals[0], 8)
        self.assertIn("Rename the variable",
                      self.compile_error('app "A"\nvar held\non draw { var y = held(1) }\n'))

    def test_engine_4_for_more_than_the_old_limits(self):
        # Without a list or held(): the old engine, so that older firmware installs the app
        few = compile_text('app "A"\n' + "".join("var v%d\n" % i for i in range(48)))["package"]
        self.assertEqual(few[5], 1)
        many = compile_text('app "A"\n' + "".join("var v%d\n" % i for i in range(49)))["package"]
        self.assertEqual((many[4], many[5]), (3, 4))
        self.assertEqual(app.check(many)[0], app.OK)
        bitmaps = "".join('bitmap b%d { "#" }\n' % i for i in range(9))
        self.assertEqual(compile_text('app "A"\n' + bitmaps)["package"][5], 4)
        line = compile_text('app "A"\nengine 4\n')["package"]
        self.assertEqual((line[4], line[5]), (3, 4))
        # The step limit follows the engine: 4000 for engine 1 to 3, 16000 for engine 4
        loop = 'on press1 { var i = 0\n while i < 1000 { i += 1 } }\n'
        old = app.Machine(compile_text('app "A"\n' + loop)["package"])
        self.assertEqual(old.run("press1"), "too many steps")
        new = app.Machine(compile_text('app "A"\nengine 4\n' + loop)["package"])
        self.assertEqual(new.run("press1"), "ok")

    def test_check_of_engine_4_packages(self):
        # The same rules as Stapp_check(): lists and held() only in a package for engine 4
        loadx = bytes([app.OP["PUSH8"], 0, app.OP["LOADX"], 0, 2, app.OP["DROP"], app.OP["HALT"]])
        held = bytes([app.OP["PUSH8"], 1, app.OP["SYS"], 33, app.OP["DROP"], app.OP["HALT"]])
        for code, old_result in ((loadx, "bad code"), (held, "bad sys")):
            package = small_package(code)
            self.assertEqual(app.result_text(app.check(package)[0]), old_result)
            data = bytearray(package)
            data[5] = 4
            self.assertEqual(app.result_text(app.check(with_crc(data))[0]), "ok")
        for base, length in ((1, 2), (0, 0), (2, 1)):
            code = bytes([app.OP["PUSH8"], 0, app.OP["LOADX"], base, length, app.OP["DROP"], app.OP["HALT"]])
            data = bytearray(small_package(code))
            data[5] = 4
            self.assertEqual(app.result_text(app.check(with_crc(data))[0]), "bad global", (base, length))

    def test_bench_saves_its_numbers(self):
        # BENCH (tools/bench/): it tests the 5 loads by itself and saves 3 rows for each load in its
        # data store (RUN, STEPS, FPS, MS), so the sync page can download them as a CSV
        package = app.load_package(os.path.join(REPO_ROOT, "tools", "bench", "bench.sts"))
        self.assertEqual(app.check(package)[0], app.OK)
        self.assertEqual(app.package_parts(package)["columns"], 4)
        machine = app.Machine(package)
        self.assertEqual(machine.run("open"), "ok")
        machine.wait(21000)
        self.assertIsNone(machine.error)
        self.assertEqual([row[1] for row in machine.data], [s for s in (0, 4000, 8000, 12000, 15500) for _ in range(3)])
        self.assertEqual({(row[0], row[2], row[3]) for row in machine.data}, {(1, 100, 10)})
        machine.wait(5000)
        self.assertEqual(len(machine.data), 15)                 # done: no more rows
        self.assertEqual(machine.run("long2"), "ok")            # test again: run 2
        machine.wait(5000)
        self.assertEqual(machine.data[-1][:2], [2, 0])
        # The STEPS on the screen are the real steps of the draw handler (at most 16000)
        for limit, result in ((15400, "too many steps"), (15600, "ok")):
            machine = app.Machine(package)
            machine.run("open")
            for _ in range(4):
                machine.run("press2")                           # the load 15500
            machine.wait(1200)
            machine.max_steps = limit
            self.assertEqual(machine.run("draw"), result, limit)

    def test_data_errors(self):
        for source, text in (
                ('app "A"\non press1 { log(1) }\n', "needs a data store"),
                ('app "A"\ndata "X" "Y"\non press1 { log(1) }\n', "needs 2 values"),
                ('app "A"\ndata\n', "1 to 4 column names"),
                ('app "A"\ndata "A" "B" "C" "D" "E"\n', "1 to 4 column names"),
                ('app "A"\ndata "THIS IS TOO LONG"\n', "1 to 12 characters"),
                ('app "A"\npermission data\n', "comes from the line"),
                ('app "A"\non press1 { log_clear() }\n', "needs a data store")):
            with self.subTest(source=source):
                with self.assertRaises(app.AppError) as raised:
                    compile_text(source)
                self.assertIn(text, str(raised.exception))

    def test_log_is_full_and_not_in_draw(self):
        package = compile_text('app "A"\ndata "X" "Y" "Z" "W"\nvar r\non press1 { r = log(1, 2, 3, 4) }\n'
                               'on draw { r = log(5, 6, 7, 8) }\n')["package"]
        machine = app.Machine(package)
        machine.run("draw")
        self.assertEqual(machine.data, [])
        for _ in range(300):
            machine.run("press1")
        self.assertEqual(len(machine.data), 256)          # 1024 values: full = stop

    def test_runtime_errors(self):
        machine = app.Machine(compile_text('app "L"\non press1 { while 1 { } }\n')["package"])
        self.assertEqual(machine.run("press1"), "too many steps")
        self.assertEqual(machine.run("press2"), "too many steps")   # stays

    def compile_error(self, source):
        with self.assertRaises(app.AppError) as context:
            compile_text(source)
        return str(context.exception)

    def test_errors_in_plain_words_with_the_line(self):
        self.assertIn("line 3", self.compile_error('app "E"\non press1 {\n  x = 1\n}\n'))
        self.assertIn("not known", self.compile_error('app "E"\non press1 {\n  x = 1\n}\n'))
        message = self.compile_error('app "E"\nfunc a() { return b() }\nfunc b() { return a() }\n')
        self.assertIn("calls itself", message)
        message = self.compile_error('app "E"\non press1 { click(1000) }\n')
        self.assertIn("permission sound", message)
        message = self.compile_error('app "E"\npermission sound\non press1 { click(50) }\n')
        self.assertIn("from 200 to 4000", message)
        self.assertIn("6 values", self.compile_error('app "E"\non draw { text(1, 2, "A") }\n'))
        self.assertIn("one time only", self.compile_error('app "E"\napp "F"\n'))
        self.assertIn("missing", self.compile_error('version 2\n'))
        self.assertIn("characters", self.compile_error('app "E"\non draw { text(0, 0, "%s", text, on, left) }\n' % ("X" * 21)))
        self.assertIn("name of the language", self.compile_error('app "E"\nvar time = 0\n'))
        self.assertIn("80 is the most", self.compile_error('app "E"\npicture icon crosshair "VERY LONG NAME"\n'))
        self.assertIn("cannot have these characters", self.compile_error('app "E_1"\n'))

    def test_drawing_outside_draw_is_a_note(self):
        result = compile_text('app "D"\non press1 { pixel(1, 1, on) }\n')
        self.assertEqual(len(result["warnings"]), 1)
        self.assertIn("draws only in", result["warnings"][0])

    def test_app_id(self):
        self.assertEqual(compile_text('app "Id"\n')["info"]["id"], app.make_app_id("ID"))
        self.assertEqual(compile_text('app "Id"\nid 0x1234ABCD\n')["info"]["id"], 0x1234ABCD)
        self.assertIn("built-in", self.compile_error('app "Id"\nid 2\n'))


class Pictures(unittest.TestCase):
    def test_round_timer_picture(self):
        rows = app.compile_file(example_path("round-timer"))["picture"]
        self.assertEqual(len(rows), 24)
        self.assertEqual(rows[4:21], [
            ".....................###........................................................",
            "......................#.........................................................",
            "....................#####..#........#####.#####.#...#.#...#.####................",
            "..................##.....###........#...#.#...#.#...#.##..#.#...#...............",
            ".................#....#....#........#####.#...#.#...#.#.#.#.#...#...............",
            "................#.....#.....#.......#.#...#...#.#...#.#..##.#...#...............",
            "................#.....#.....#.......#..##.#####.#####.#...#.####................",
            "...............#......#......#..................................................",
            "...............#......#......#..................................................",
            "...............#......####...#......#####.#####.#...#.#####.#####...............",
            "...............#.............#........#.....#...##.##.#.....#...#...............",
            "...............#.............#........#.....#...#.#.#.####..#####...............",
            "................#...........#.........#.....#...#...#.#.....#.#.................",
            "................#...........#.........#...#####.#...#.#####.#..##...............",
            ".................#.........#....................................................",
            "..................##.....##.....................................................",
            "....................#####.......................................................",
        ])
        self.assertTrue(all(set(row) == {"."} for row in rows[:4] + rows[21:]))

    def test_one_line_follows_the_rule_of_the_app_menu(self):
        # crosshair (19 wide) + "KILLS" in the title font: like the built-in KILLS picture
        rows = app.picture_rows("icon", "crosshair", ["KILLS"])
        icon_w, icon_h = app_icons.size("crosshair")
        text_w = app.design.text_width(app._assets()["font_by_name"]["big"], "KILLS")
        gap = 3 if icon_w + 5 + text_w > 74 else 5
        x0 = (80 - (icon_w + gap + text_w) + 1) // 2
        y0 = (24 - icon_h + 1) // 2
        for y, icon_row in enumerate(app_icons.ICONS["crosshair"]):
            self.assertEqual(rows[y0 + y][x0:x0 + icon_w], icon_row)
        self.assertIn("#", rows[9][x0 + icon_w + gap:x0 + icon_w + gap + text_w])

    def test_picture_bytes_round_trip(self):
        rows = app.picture_rows("text", lines=["ABC"])
        data = app.picture_bytes(rows)
        self.assertEqual(len(data), 240)
        self.assertEqual(app.bitmap_to_rows(data, 80, 24), rows)

    def test_picture_from_a_png_file(self):
        rows = ["#" * 80 if y % 2 else "." * 80 for y in range(24)]
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "p.png")
            app.write_png(path, rows, 1)
            self.assertEqual(app.picture_rows("bitmap", path=path), rows)
            with open(os.path.join(folder, "a.sts"), "w") as handle:
                handle.write('app "PNG"\npicture bitmap "p.png"\n')
            self.assertEqual(app.compile_file(os.path.join(folder, "a.sts"))["picture"], rows)

    def test_a_whole_picture_drawn_in_the_source(self):
        # picture { ... }: 80 x 24 rows of # and . (a frame with a cross), exactly as written
        rows = ["#" * 80] + ["#" + ("#" if y == 11 else ".") * 78 + "#" for y in range(1, 23)] + ["#" * 80]
        rows = [row[:39] + "##" + row[41:] if 0 < y < 23 else row for y, row in enumerate(rows)]
        source = 'app "OWN"\npicture {\n%s}\n' % "".join('  "%s"\n' % row for row in rows)
        result = compile_text(source)
        self.assertEqual(result["picture"], rows)
        self.assertEqual(app.bitmap_to_rows(app.package_parts(result["package"])["picture"], 80, 24), rows)
        self.assertEqual(result["info"]["bitmap_count"], 0)     # not a bitmap of the code

    def test_a_smaller_picture_is_in_the_middle(self):
        rows = compile_text('app "OWN"\npicture { "###" "#.#" "###" }\n')["picture"]
        self.assertEqual(rows[10][38:41], "###")
        self.assertEqual(rows[11][38:41], "#.#")
        self.assertEqual(rows[12][38:41], "###")
        self.assertEqual(sum(row.count("#") for row in rows), 8)

    def test_an_icon_drawn_in_the_source(self):
        # picture icon NAME "TEXT" with a bitmap of the source: the same layout as a built-in icon.
        # The bitmap can come after the picture line.
        ship = ["#........", "###......", "#######..", "#########", "#######..", "###......", "#........"]
        bitmap = "bitmap ship {\n%s}\n" % "".join('  "%s"\n' % row for row in ship)
        rows = compile_text('app "SHIP"\npicture icon ship "SHIP"\n' + bitmap)["picture"]
        self.assertEqual(rows, app.picture_rows("icon", "ship", ["SHIP"], own_icons={"ship": ship}))
        text_w = app.design.text_width(app._assets()["font_by_name"]["big"], "SHIP")
        x0 = (80 - (9 + 5 + text_w) + 1) // 2
        y0 = (24 - 7 + 1) // 2
        for y, row in enumerate(ship):
            self.assertEqual(rows[y0 + y][x0:x0 + 9], row)
        self.assertIn("#", rows[9][x0 + 14:x0 + 14 + text_w])      # the text after the icon
        two = compile_text('app "SHIP"\npicture icon ship "SPACE" "SHIP"\n' + bitmap)["picture"]
        self.assertEqual(two, app.picture_rows("icon", "ship", ["SPACE", "SHIP"], own_icons={"ship": ship}))

    def test_a_built_in_icon_wins_over_a_bitmap_with_its_name(self):
        # Old sources keep their picture: a bitmap named like a built-in icon does not replace it
        source = 'app "R"\npicture icon target "R"\nbitmap target { "#" }\n'
        self.assertEqual(compile_text(source)["picture"], app.picture_rows("icon", "target", ["R"]))

    def test_errors_of_an_own_picture(self):
        def error(source):
            with self.assertRaises(app.AppError) as context:
                compile_text(source)
            return str(context.exception)
        self.assertIn("80 x 24 at most", error('app "E"\npicture { "%s" }\n' % ("#" * 81)))
        self.assertIn("80 x 24 at most", error('app "E"\npicture { %s }\n' % ('"#" ' * 25)))
        self.assertIn("same length", error('app "E"\npicture { "##" "#" }\n'))
        self.assertIn("same length", error('app "E"\npicture { "#x" }\n'))
        self.assertIn("same length", error('app "E"\npicture { }\n'))
        self.assertIn("one time only", error('app "E"\npicture { "#" }\npicture text "E"\n'))
        message = error('app "E"\npicture icon nothing "E"\n')
        self.assertIn("line 2", message)
        self.assertIn("no icon", message)
        self.assertIn("bitmap of this source", message)
        tall = '"#" ' * 25
        self.assertIn("24 is the most", error('app "E"\npicture icon big "E"\nbitmap big { %s }\n' % tall))
        self.assertIn("80 is the most", error('app "E"\npicture icon w "E"\nbitmap w { "%s" }\n' % ("#" * 76)))

    def test_every_icon_fits(self):
        for name in app_icons.ICONS:
            with self.subTest(icon=name):
                rows = app_icons.ICONS[name]
                self.assertTrue(all(len(row) == len(rows[0]) and set(row) <= {"#", "."} for row in rows))
                self.assertLessEqual(len(rows), 24)
                app.picture_rows("icon", name, ["A"])

    def test_firmware_icons_are_the_same_as_in_the_firmware(self):
        with open(os.path.join(REPO_ROOT, "main", "resources", "app_icons.c"), "r", encoding="utf-8") as handle:
            source = handle.read()
        for name, symbol in app_icons.FIRMWARE_ICONS.items():
            with self.subTest(icon=name):
                data_text = re.search(r"%sData\[\] = \{(.*?)\};" % symbol, source, re.S).group(1)
                data = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", data_text))
                size = re.search(r"%s = \{ \.width = (\d+), \.height = (\d+)" % symbol, source)
                width, height = int(size.group(1)), int(size.group(2))
                self.assertEqual((width, height), app_icons.size(name))
                self.assertEqual(app.rows_to_bitmap(app_icons.ICONS[name]), data[:width * ((height + 7) // 8)])


class CommandLine(unittest.TestCase):
    def test_sim_makes_a_page_that_opens_the_app(self):
        # "python tools/app.py sim": the launcher page goes to simulator.html#app=<the package>
        import base64
        import websim
        package = app.compile_file(example_path("reaction"))["package"]
        with tempfile.TemporaryDirectory() as folder, contextlib.redirect_stdout(io.StringIO()):
            old = (websim.BUILD_DIR, websim.is_up_to_date)
            websim.BUILD_DIR, websim.is_up_to_date = folder, (lambda: True)
            try:
                launcher = websim.open_with_app(package, "REACTION", fresh=True, browser=False)
            finally:
                websim.BUILD_DIR, websim.is_up_to_date = old
            with open(launcher, "r", encoding="utf-8") as handle:
                text = handle.read()
        found = re.search(r"simulator\.html#app=([A-Za-z0-9_-]+)&fresh", text)
        self.assertIsNotNone(found)
        data = found.group(1)
        self.assertEqual(base64.urlsafe_b64decode(data + "=" * (-len(data) % 4)), package)

    def test_build_check_describe(self):
        with tempfile.TemporaryDirectory() as folder, contextlib.redirect_stdout(io.StringIO()):
            out = os.path.join(folder, "t.stapp")
            self.assertEqual(app.main(["build", example_path("reaction"), "-o", out]), 0)
            self.assertEqual(app.main(["check", out]), 0)
            self.assertEqual(app.main(["picture", out, "-o", os.path.join(folder, "t.png")]), 0)
            with open(out, "rb") as handle:
                text = app.describe(handle.read())
            self.assertIn('App "REACTION"', text)
            self.assertIn("on draw:", text)
            with open(out, "r+b") as handle:
                handle.seek(100)
                handle.write(b"\xAA")
            self.assertEqual(app.main(["check", out]), 1)

    def test_run_says_an_app_error_in_draw_one_time(self):
        # An error in draw or tick (during wait:, frame, png:) must not be silent: a person who
        # tries an app before the app store sees it here, as the review of the store does
        package = compile_text('app "BAD"\nvar a[3]\nvar i = 5\non draw {\n  a[i] = 1\n}\n')["package"]
        machine = app.Machine(package)
        self.assertEqual(machine.run("open"), "ok")
        lines = app.run_actions(machine, ["wait:100", "wait:100", "frame"])
        errors = [line for line in lines if line.startswith("App error")]
        self.assertEqual(errors, ["App error after wait:100: bad index"])
        # The same app with no action: the last screen of the command says it
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "bad.stapp")
            with open(path, "wb") as handle:
                handle.write(package)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                app.main(["run", path])
            self.assertEqual(out.getvalue().count("App error in draw:"), 1)
        # A good app has no such line
        good = app.compile_file(example_path("reaction"))["package"]
        lines = app.run_actions(app.Machine(good), ["wait:800", "press1", "wait:1500", "frame"])
        self.assertEqual([line for line in lines if line.startswith("App error")], [])


if __name__ == "__main__":
    unittest.main()
