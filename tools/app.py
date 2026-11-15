#!/usr/bin/env python3
"""StatTrak apps: make, check and show app packages (.stapp). docs/APPS.md is the specification.

An app is written in StatTrak Script (a .sts file, docs/APPS.md section 7). This tool makes the
package that the firmware installs, and it checks a package with the same rules as the engine
of the firmware (components/stapp/stapp_check.c).

    python tools/app.py build    apps/NAME/NAME.sts [-o NAME.stapp]   make the package
    python tools/app.py check    FILE.stapp                           the full check, and the info
    python tools/app.py describe FILE.stapp|FILE.sts                  all parts as text, with the code
    python tools/app.py picture  FILE.sts|FILE.stapp [-o FILE.png]    the title picture as a PNG
    python tools/app.py run      FILE.sts|FILE.stapp [ACTIONS...]     run the app in Python, show the screen
    python tools/app.py catalog                                       build apps/*/ for the web updater
    python tools/app.py sim      FILE.sts|FILE.stapp [--fresh]        the web simulator, with the app open

Other Python code can use the functions: compile_source(), compile_file(), check(), describe(),
disassemble(), picture_rows(), Machine (runs a package) and build_catalog().

Python 3.8 or newer, no extra packages.
"""

import argparse
import base64
import json
import os
import random
import re
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import app_icons  # noqa: E402
import design  # noqa: E402

# --- Limits and numbers (the same as components/stapp/stapp.h) ------------------------------------

ENGINE_VERSION = 2     # 2: log(), log_count(), log_get(), log_clear(), battery_mv(), background()
FORMAT_VERSION = 3     # The newest format. Format 3 only for an app with data columns, "awake",
                       # "background", kill / death or an engine 2 call; format 2 only for down1 / down2;
                       # all other packages stay format 1.
MAGIC = b"STAP"

HEADER_SIZE = 48
PICTURE_OFFSET = 48
PICTURE_W = 80
PICTURE_H = 24
PICTURE_SIZE = PICTURE_W * PICTURE_H // 8          # 240
HANDLERS_OFFSET = PICTURE_OFFSET + PICTURE_SIZE    # 288
DATA_START = HANDLERS_OFFSET + 16                  # 304: the first byte after the handler table (format 1)
CRC_SIZE = 4
MIN_SIZE = DATA_START + CRC_SIZE                   # 308
NO_HANDLER = 0xFFFF

MAX_SIZE = 4096
MAX_NAME = 14
MAX_GLOBALS = 48
MAX_SAVED = 16
MAX_SETTINGS = 4
MIN_VALUES = 2
MAX_VALUES = 6
SETTING_NAME_LEN = 12
VALUE_LEN = 8
MAX_STRINGS = 32
MAX_STRING_LEN = 20
MAX_BITMAPS = 8
MAX_CODE = 2048
MAX_STEPS = 4000
STACK_SIZE = 32
CALL_DEPTH = 8
RESERVED_IDS = 0xFF          # ids 1 .. 255 are for the built-in apps (1 KILLS, 2 KILLS + DEAD, 3 STATS)

PERM_COUNTERS = 0x0001
PERM_SOUND = 0x0002
PERM_DATA = 0x0004       # Format 3: the app has a data store (the `data` line sets it)
PERM_AWAKE = 0x0008      # Format 3: auto-off waits while the app is open
PERM_BACKGROUND = 0x0010 # Format 3: the app can run in the background (Settings > Background)
PERM_V2 = PERM_COUNTERS | PERM_SOUND
PERM_ALL = PERM_V2 | PERM_DATA | PERM_AWAKE | PERM_BACKGROUND
PERMISSIONS = {"counters": PERM_COUNTERS, "sound": PERM_SOUND, "data": PERM_DATA, "awake": PERM_AWAKE,
               "background": PERM_BACKGROUND}
SCRIPT_PERMISSIONS = ("counters", "sound", "awake", "background")   # `data` comes from the `data` line

MAX_COLUMNS = 4          # Format 3: data columns (header byte 37)
COLUMN_NAME_LEN = 12
DATA_APP_VALUES = 1024   # The store of one app: 4096 bytes (the firmware also has 8192 for all apps)
SYS_ENGINE2 = (26, 27, 28, 29, 30, 31)

EVENTS = ("open", "draw", "press1", "press2", "long1", "long2", "tick", "close", "down1", "down2",
          "kill", "death")
HANDLERS_V1 = 8        # Format 1 has the handlers up to close; format 2 also down1 and down2
HANDLERS_V2 = 10       # Format 3 also kill and death (docs/APPS.md section 4.6)
BACKGROUND_EVENTS = ("kill", "death")


def handler_count(format_version):
    return len(EVENTS) if format_version >= 3 else HANDLERS_V2 if format_version >= 2 else HANDLERS_V1


def data_start(format_version):
    """The first byte after the handler table: 304 (format 1), 308 (format 2) or 312 (format 3)"""
    return HANDLERS_OFFSET + 2 * handler_count(format_version)

NAME_CHARS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 +-%/.:")

# The results of the check and of a run, in the order of stapp_result_t. The texts are the texts
# of Stapp_resultText(); the web page shows them.
RESULTS = ("ok", "bad size", "bad magic", "bad version", "bad crc", "bad header", "bad layout",
           "bad settings", "bad strings", "bad bitmaps", "bad code", "bad global", "bad sys",
           "bad handler", "stack", "call depth", "too many steps", "bad arg")
(OK, ERR_SIZE, ERR_MAGIC, ERR_VERSION, ERR_CRC, ERR_HEADER, ERR_LAYOUT, ERR_SETTINGS, ERR_STRINGS,
 ERR_BITMAPS, ERR_CODE, ERR_GLOBAL, ERR_SYS, ERR_HANDLER, ERR_STACK, ERR_CALLS, ERR_STEPS,
 ERR_ARG) = range(len(RESULTS))

# Opcodes: code -> (name, operand). Operands: i8, i16, i32 (numbers), g (u8 global), rel (i16 jump,
# from the next instruction), abs (u16 code offset), sys (u8 system call id).
OPCODES = {
    0x00: ("HALT", None), 0x01: ("PUSH8", "i8"), 0x02: ("PUSH16", "i16"), 0x03: ("PUSH32", "i32"),
    0x04: ("LOAD", "g"), 0x05: ("STORE", "g"), 0x06: ("DUP", None), 0x07: ("DROP", None),
    0x08: ("SWAP", None),
    0x10: ("ADD", None), 0x11: ("SUB", None), 0x12: ("MUL", None), 0x13: ("DIV", None),
    0x14: ("MOD", None), 0x15: ("NEG", None),
    0x16: ("EQ", None), 0x17: ("NE", None), 0x18: ("LT", None), 0x19: ("LE", None),
    0x1A: ("GT", None), 0x1B: ("GE", None), 0x1C: ("NOT", None),
    0x1D: ("BAND", None), 0x1E: ("BOR", None), 0x1F: ("BXOR", None), 0x20: ("SHL", None),
    0x21: ("SHR", None), 0x22: ("MIN", None), 0x23: ("MAX", None), 0x24: ("ABS", None),
    0x30: ("JMP", "rel"), 0x31: ("JZ", "rel"), 0x32: ("JNZ", "rel"), 0x33: ("CALL", "abs"),
    0x34: ("RET", None), 0x40: ("SYS", "sys"),
}
OP = {name: code for code, (name, _) in OPCODES.items()}
OPERAND_SIZE = {None: 0, "i8": 1, "g": 1, "sys": 1, "i16": 2, "rel": 2, "abs": 2, "i32": 4}
PUSH_OPS = (OP["PUSH8"], OP["PUSH16"], OP["PUSH32"])
END_OPS = (OP["HALT"], OP["JMP"], OP["RET"])     # the last instruction of the code must be one of these

# System calls: id -> (name, argument names, permission, draws)
SYSCALLS = {
    0: ("pixel", ("x", "y", "c"), 0, True),
    1: ("hline", ("x", "y", "w", "c"), 0, True),
    2: ("vline", ("x", "y", "h", "c"), 0, True),
    3: ("rect", ("x", "y", "w", "h", "c"), 0, True),
    4: ("fill", ("x", "y", "w", "h", "c"), 0, True),
    5: ("rrect", ("x", "y", "w", "h", "filled"), 0, True),
    6: ("text", ("x", "y", "str", "font", "c", "align"), 0, True),
    7: ("num", ("x", "y", "value", "digits", "font", "c", "align"), 0, True),
    8: ("bignum", ("x", "y", "value", "digits", "scale", "c"), 0, True),
    9: ("bitmap", ("x", "y", "id", "c"), 0, True),
    10: ("invert", ("x", "y", "w", "h"), 0, True),
    16: ("counter", ("id",), 0, False),
    17: ("add_counter", ("id", "delta"), PERM_COUNTERS, False),
    18: ("setting", ("n",), 0, False),
    19: ("time", (), 0, False),
    20: ("click", ("freq",), PERM_SOUND, False),
    21: ("battery", (), 0, False),
    22: ("random", ("n",), 0, False),
    23: ("save", (), 0, False),
    24: ("redraw", (), 0, False),
    25: ("tone", ("freq", "ms"), PERM_SOUND, False),
    26: ("log", ("a", "b", "c", "d"), PERM_DATA, False),
    27: ("log_count", (), PERM_DATA, False),
    28: ("log_get", ("row", "column"), PERM_DATA, False),
    29: ("log_clear", (), PERM_DATA, False),
    30: ("battery_mv", (), 0, False),
    31: ("background", (), 0, False),
}
SYS_BY_NAME = {value[0]: key for key, value in SYSCALLS.items()}
SYS_TEXT = SYS_BY_NAME["text"]
SYS_BITMAP = SYS_BY_NAME["bitmap"]

# Ranges of arguments that the compiler checks when the value is fixed (a number or a name)
ARG_RANGES = {
    ("pixel", "c"): (0, 2), ("hline", "c"): (0, 2), ("vline", "c"): (0, 2), ("rect", "c"): (0, 2),
    ("fill", "c"): (0, 2), ("rrect", "filled"): (0, 1), ("text", "font"): (0, 3), ("text", "c"): (0, 2),
    ("text", "align"): (0, 2), ("num", "font"): (0, 3), ("num", "c"): (0, 2), ("num", "align"): (0, 2),
    ("num", "digits"): (0, 10), ("bignum", "digits"): (0, 10), ("bignum", "scale"): (2, 4),
    ("bignum", "c"): (0, 2), ("bitmap", "c"): (0, 2), ("counter", "id"): (0, 3),
    ("add_counter", "id"): (0, 3), ("add_counter", "delta"): (-10, 10), ("click", "freq"): (200, 4000),
    ("tone", "freq"): (200, 4000), ("tone", "ms"): (10, 1000),
    ("random", "n"): (1, 65535),
}

# Names with a fixed value in scripts
BUILTIN_NAMES = {
    "text": 0, "caps": 1, "small": 2, "title": 3,             # fonts
    "off": 0, "on": 1, "invert": 2,                           # colors
    "left": 0, "center": 1, "right": 2,                       # aligns
    "total_kills": 0, "total_deaths": 1, "game_kills": 2, "game_deaths": 3,   # counters
    "false": 0, "true": 1,
}
OPCODE_FUNCTIONS = {"min": ("MIN", 2), "max": ("MAX", 2), "abs": ("ABS", 1)}
STATEMENT_WORDS = {"var", "if", "else", "while", "return", "on", "func"}

FONT_NAMES = ("5x7", "5x5", "3x5", "big")          # font numbers 0..3 in the fonts of design.py


class AppError(Exception):
    """A script that cannot be compiled, or a file that cannot be read."""


def result_text(result):
    return RESULTS[result] if 0 <= result < len(RESULTS) else "unknown"


def _u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def _u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def _wrap(value):
    """A number as a 32-bit signed integer (two's complement)"""
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value & 0x80000000 else value


def _assets():
    if not hasattr(_assets, "cache"):
        _assets.cache = design.load_assets()
    return _assets.cache


# --- The check (the same rules as Stapp_check) ------------------------------------------------------

def _fixed_text(data, offset, length, low, high_length):
    """A 0-padded text field. Return the text, or None if it is not correct: it must have `low` to
    `high_length` printable characters, and only 0 bytes after the text."""
    field = data[offset:offset + length]
    end = field.find(b"\0")
    if end < 0:
        end = length
    if end < low or end > high_length or any(byte for byte in field[end:]):
        return None
    text = field[:end]
    if any(byte < 0x20 or byte > 0x7E for byte in text):
        return None
    return text.decode("ascii")


def decode(code):
    """All instructions of the code: a list of (offset, opcode, operand, size). Stops at the first
    unknown opcode or at an operand outside the code; then the last item is (offset, opcode, None, 0)."""
    out = []
    pc = 0
    while pc < len(code):
        opcode = code[pc]
        if opcode not in OPCODES:
            out.append((pc, opcode, None, 0))
            return out
        kind = OPCODES[opcode][1]
        size = 1 + OPERAND_SIZE[kind]
        if pc + size > len(code):
            out.append((pc, opcode, None, 0))
            return out
        operand = None
        if kind == "i8":
            operand = struct.unpack_from("<b", code, pc + 1)[0]
        elif kind in ("g", "sys"):
            operand = code[pc + 1]
        elif kind in ("i16", "rel"):
            operand = struct.unpack_from("<h", code, pc + 1)[0]
        elif kind == "abs":
            operand = _u16(code, pc + 1)
        elif kind == "i32":
            operand = struct.unpack_from("<i", code, pc + 1)[0]
        out.append((pc, opcode, operand, size))
        pc += size
    return out


def check(data):
    """Check a full package (docs/APPS.md sections 3 and 4). Return (result, info). `info` is a dict
    with the header values; it is None unless the result is OK. The rules are checked in the order
    of docs/APPS.md section 3.1; the first broken rule gives the result."""
    data = bytes(data)
    size = len(data)

    # 1. Size, magic, version, size in the header, CRC
    if size < MIN_SIZE or size > MAX_SIZE:
        return ERR_SIZE, None
    if data[0:4] != MAGIC:
        return ERR_MAGIC, None
    if data[4] < 1 or data[4] > FORMAT_VERSION or data[5] < 1 or data[5] > ENGINE_VERSION:
        return ERR_VERSION, None
    first = data_start(data[4])
    if size < first + CRC_SIZE:
        return ERR_SIZE, None
    if _u16(data, 14) != size:
        return ERR_SIZE, None
    if zlib.crc32(data[:-CRC_SIZE]) & 0xFFFFFFFF != _u32(data, size - CRC_SIZE):
        return ERR_CRC, None

    # 2. Header values
    permissions = _u16(data, 6)
    app_id = _u32(data, 8)
    name = _fixed_text(data, 16, 16, 1, MAX_NAME)
    global_count, saved_count, setting_count, string_count, bitmap_count, reserved = data[32:38]
    if (permissions & ~PERM_ALL or app_id <= RESERVED_IDS or app_id == 0xFFFFFFFF or name is None
            or any(char not in NAME_CHARS for char in name)
            or global_count > MAX_GLOBALS or saved_count > MAX_SAVED or saved_count > global_count
            or setting_count > MAX_SETTINGS or string_count > MAX_STRINGS or bitmap_count > MAX_BITMAPS):
        return ERR_HEADER, None
    # Format 3: byte 37 = data columns, permissions data / awake. Formats 1, 2: byte 37 = 0, bits 0, 1 only.
    columns = 0
    if data[4] >= 3:
        columns = reserved
        if (columns > MAX_COLUMNS or bool(permissions & PERM_DATA) != (columns > 0)
                or columns > string_count):
            return ERR_HEADER, None
    elif reserved != 0 or permissions & ~PERM_V2:
        return ERR_HEADER, None

    # 3. Layout: the blocks are inside the data area and do not overlap
    settings_offset, strings_offset, bitmaps_offset, code_offset, code_size = struct.unpack_from("<5H", data, 38)
    data_end = size - CRC_SIZE
    if code_size > MAX_CODE:
        return ERR_LAYOUT, None
    blocks = []
    setting_starts = []
    if setting_count:
        pos = settings_offset
        for _ in range(setting_count):
            if pos < first or pos + SETTING_NAME_LEN + 2 > data_end:
                return ERR_LAYOUT, None
            value_count = data[pos + SETTING_NAME_LEN]
            if value_count < MIN_VALUES or value_count > MAX_VALUES:
                return ERR_SETTINGS, None
            setting_starts.append(pos)
            pos += SETTING_NAME_LEN + 2 + VALUE_LEN * value_count
        blocks.append((settings_offset, pos))
    if string_count:
        blocks.append((strings_offset, strings_offset + 2 * string_count))
    if bitmap_count:
        blocks.append((bitmaps_offset, bitmaps_offset + 4 * bitmap_count))
    if code_size:
        blocks.append((code_offset, code_offset + code_size))
    for block_start, block_end in blocks:
        if block_start < first or block_end > data_end:
            return ERR_LAYOUT, None
    for index, (start, end) in enumerate(blocks):
        for other_start, other_end in blocks[index + 1:]:
            if start < other_end and other_start < end:
                return ERR_LAYOUT, None

    # 4. Settings
    for pos in setting_starts:
        value_count = data[pos + SETTING_NAME_LEN]
        if (_fixed_text(data, pos, SETTING_NAME_LEN, 1, SETTING_NAME_LEN) is None
                or data[pos + SETTING_NAME_LEN + 1] >= value_count):
            return ERR_SETTINGS, None
        for value in range(value_count):
            if _fixed_text(data, pos + SETTING_NAME_LEN + 2 + VALUE_LEN * value, VALUE_LEN, 1, VALUE_LEN) is None:
                return ERR_SETTINGS, None

    # 5. Strings
    for index in range(string_count):
        offset = _u16(data, strings_offset + 2 * index)
        if offset < first or offset >= data_end:
            return ERR_STRINGS, None
        end = data.find(b"\0", offset, data_end)
        if end < 0 or end - offset > MAX_STRING_LEN or any(b < 0x20 or b > 0x7E for b in data[offset:end]):
            return ERR_STRINGS, None
        if index < columns and not 1 <= end - offset <= COLUMN_NAME_LEN:
            return ERR_STRINGS, None     # A column name: 1 to 12 characters

    # 6. Bitmaps
    for index in range(bitmap_count):
        width, height, offset = struct.unpack_from("<BBH", data, bitmaps_offset + 4 * index)
        if (width < 1 or width > 128 or height < 1 or height > 32 or offset < first
                or offset + width * ((height + 7) // 8) > data_end):
            return ERR_BITMAPS, None

    # 7. Code: each instruction, in the order of the code
    code = data[code_offset:code_offset + code_size]
    instructions = decode(code)
    for number, (pc, opcode, operand, length) in enumerate(instructions):
        if length == 0:
            return ERR_CODE, None
        name_ = OPCODES[opcode][0]
        if name_ in ("LOAD", "STORE") and operand >= global_count:
            return ERR_GLOBAL, None
        if name_ == "SYS":
            if operand not in SYSCALLS:
                return ERR_SYS, None
            if SYSCALLS[operand][2] & ~permissions:
                return ERR_SYS, None
            # A string or bitmap id that is a fixed number: the instructions right before the SYS
            # push all its arguments from the id on.
            if operand in (SYS_TEXT, SYS_BITMAP):
                back = 4 if operand == SYS_TEXT else 2
                count = string_count if operand == SYS_TEXT else bitmap_count
                if number >= back and all(instructions[number - k][1] in PUSH_OPS for k in range(1, back + 1)):
                    value = instructions[number - back][2]
                    if value < 0 or value >= count:
                        return (ERR_STRINGS if operand == SYS_TEXT else ERR_BITMAPS), None
    if instructions and instructions[-1][1] not in END_OPS:
        return ERR_CODE, None
    starts = set(pc for pc, _, _, _ in instructions)
    for pc, opcode, operand, length in instructions:
        kind = OPCODES[opcode][1]
        if kind == "rel" and pc + length + operand not in starts:
            return ERR_CODE, None
        if kind == "abs" and operand not in starts:
            return ERR_CODE, None

    # 8. Handlers
    for event in range(handler_count(data[4])):
        offset = _u16(data, HANDLERS_OFFSET + 2 * event)
        if offset != NO_HANDLER and offset not in starts:
            return ERR_HANDLER, None

    info = {
        "id": app_id, "version": _u16(data, 12), "size": size, "permissions": permissions, "name": name,
        "global_count": global_count, "saved_count": saved_count, "setting_count": setting_count,
        "string_count": string_count, "bitmap_count": bitmap_count, "columns": columns,
    }
    return OK, info


# --- Read the parts of a package (only for a package that passed the check) -------------------------

def package_parts(data):
    """All parts of a checked package as a dict (for describe, the catalog and the Python engine)"""
    result, info = check(data)
    if result != OK:
        raise AppError("The package is not correct: %s" % result_text(result))
    settings_offset, strings_offset, bitmaps_offset, code_offset, code_size = struct.unpack_from("<5H", data, 38)
    parts = dict(info)
    parts["engine_version"] = data[5]
    parts["picture"] = bytes(data[PICTURE_OFFSET:PICTURE_OFFSET + PICTURE_SIZE])
    parts["format_version"] = data[4]
    # An older format has no handler for the newer events
    parts["handlers"] = [_u16(data, HANDLERS_OFFSET + 2 * i) if i < handler_count(data[4]) else NO_HANDLER
                         for i in range(len(EVENTS))]
    settings = []
    pos = settings_offset
    for _ in range(info["setting_count"]):
        count = data[pos + SETTING_NAME_LEN]
        values = [_fixed_text(data, pos + 14 + VALUE_LEN * v, VALUE_LEN, 1, VALUE_LEN) for v in range(count)]
        settings.append({"name": _fixed_text(data, pos, SETTING_NAME_LEN, 1, SETTING_NAME_LEN),
                         "values": values, "default": data[pos + SETTING_NAME_LEN + 1]})
        pos += 14 + VALUE_LEN * count
    parts["settings"] = settings
    strings = []
    for index in range(info["string_count"]):
        offset = _u16(data, strings_offset + 2 * index)
        strings.append(data[offset:data.index(b"\0", offset)].decode("ascii"))
    parts["strings"] = strings
    parts["column_names"] = strings[:info["columns"]]
    bitmaps = []
    for index in range(info["bitmap_count"]):
        width, height, offset = struct.unpack_from("<BBH", data, bitmaps_offset + 4 * index)
        bitmaps.append((width, height, bytes(data[offset:offset + width * ((height + 7) // 8)])))
    parts["bitmaps"] = bitmaps
    parts["code"] = bytes(data[code_offset:code_offset + code_size])
    return parts


def permission_words(permissions):
    return [word for word, bit in PERMISSIONS.items() if permissions & bit]


# --- Pictures -----------------------------------------------------------------------------------------

def bitmap_to_rows(data, width, height):
    """Gfx_drawBitmapRaw format (pages of 8 rows, bit 0 = top) to rows of '#' and '.'"""
    return ["".join("#" if data[(y // 8) * width + x] & (1 << (y % 8)) else "." for x in range(width))
            for y in range(height)]


def rows_to_bitmap(rows):
    return design.pixels_to_bitmap(rows)


def _read_png(path):
    """A PNG file as rows of '#' and '.' (a pixel is on if it is light and not transparent).
    Non-interlaced PNG files with 8 bits per channel, or 1, 2, 4, 8 bits for grey and palette."""
    try:
        with open(path, "rb") as handle:
            raw = handle.read()
    except OSError as error:
        raise AppError("Cannot read the picture %s: %s" % (path, error))
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise AppError("%s is not a PNG file." % path)
    pos = 8
    idat = b""
    palette = []
    alpha = []
    header = None
    while pos + 8 <= len(raw):
        length, tag = struct.unpack_from(">I4s", raw, pos)
        body = raw[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif tag == b"PLTE":
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif tag == b"tRNS":
            alpha = list(body)
        elif tag == b"IDAT":
            idat += body
        elif tag == b"IEND":
            break
    if header is None:
        raise AppError("%s: the PNG file has no header." % path)
    width, height, depth, color_type, _, _, interlace = header
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color_type)
    if channels is None or interlace or (depth != 8 and not (color_type in (0, 3) and depth in (1, 2, 4))):
        raise AppError("%s: this kind of PNG file is not possible. Save it as 8-bit grey, RGB or RGBA, "
                       "without interlace." % path)
    try:
        pixels = zlib.decompress(idat)
    except zlib.error:
        raise AppError("%s: the PNG data is broken." % path)
    bits = depth * channels
    stride = (width * bits + 7) // 8
    step = max(1, bits // 8)
    rows = []
    previous = bytearray(stride)
    pos = 0
    for _ in range(height):
        kind = pixels[pos]
        line = bytearray(pixels[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            left = line[i - step] if i >= step else 0
            up = previous[i]
            corner = previous[i - step] if i >= step else 0
            if kind == 1:
                line[i] = (line[i] + left) & 0xFF
            elif kind == 2:
                line[i] = (line[i] + up) & 0xFF
            elif kind == 3:
                line[i] = (line[i] + (left + up) // 2) & 0xFF
            elif kind == 4:
                guess = left + up - corner
                pa, pb, pc = abs(guess - left), abs(guess - up), abs(guess - corner)
                line[i] = (line[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 0xFF
        previous = line
        row = []
        for x in range(width):
            if depth < 8:
                byte = line[(x * depth) // 8]
                shift = 8 - depth - (x * depth) % 8
                value = (byte >> shift) & ((1 << depth) - 1)
                if color_type == 3:
                    r, g, b = palette[value] if value < len(palette) else (0, 0, 0)
                    light, opaque = (r * 3 + g * 6 + b) // 10, (alpha[value] if value < len(alpha) else 255)
                else:
                    light, opaque = value * 255 // ((1 << depth) - 1), 255
            else:
                p = line[x * channels:(x + 1) * channels]
                if color_type == 3:
                    r, g, b = palette[p[0]] if p[0] < len(palette) else (0, 0, 0)
                    light, opaque = (r * 3 + g * 6 + b) // 10, (alpha[p[0]] if p[0] < len(alpha) else 255)
                elif color_type in (0, 4):
                    light, opaque = p[0], (p[1] if color_type == 4 else 255)
                else:
                    light, opaque = (p[0] * 3 + p[1] * 6 + p[2]) // 10, (p[3] if color_type == 6 else 255)
            row.append("#" if light >= 128 and opaque >= 128 else ".")
        rows.append("".join(row))
    return rows


def write_png(path, rows, scale=4):
    """Rows of '#' and '.' as a grey PNG, with the colors of the simulator pictures"""
    width = len(rows[0]) * scale
    lines = []
    for row in rows:
        line = b"".join(b"\xff" * scale if char == "#" else b"\x10" * scale for char in row)
        lines += [b"\x00" + line] * scale

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, len(rows) * scale, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(lines), 9))
    png += chunk(b"IEND", b"")
    folder = os.path.dirname(os.path.abspath(path))
    os.makedirs(folder, exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(png)


def picture_rows(kind, icon=None, lines=(), path=None):
    """The 80 x 24 title picture as 24 rows of 80 characters.

    kind "icon": the icon and 1 or 2 lines of text (the rule of the app menu, docs/APPS.md 7.4).
    kind "text": 1 or 2 lines of text only. kind "bitmap": a PNG file, at most 80 x 24, centered."""
    frame = design.Frame(_assets())
    lines = [line.upper() for line in lines]
    if kind == "bitmap":
        rows = _read_png(path)
        if len(rows) > PICTURE_H or len(rows[0]) > PICTURE_W:
            raise AppError("The picture %s has %d x %d pixels. It can have 80 x 24 at most."
                           % (path, len(rows[0]), len(rows)))
        frame.rows((PICTURE_W - len(rows[0])) // 2, (PICTURE_H - len(rows)) // 2, rows, 1)
        return [row[:PICTURE_W] for row in frame.lines()[:PICTURE_H]]

    fonts = _assets()["font_by_name"]
    if kind == "icon":
        if icon not in app_icons.ICONS:
            raise AppError("There is no icon %r. Icons: %s." % (icon, ", ".join(sorted(app_icons.ICONS))))
        icon_rows = app_icons.ICONS[icon]
        icon_w, icon_h = app_icons.size(icon)
    else:
        icon_rows, icon_w, icon_h = None, 0, 0
    if len(lines) not in (1, 2):
        raise AppError("The picture needs 1 or 2 lines of text.")
    for line in lines:
        font = fonts["big" if len(lines) == 1 else "5x5"]
        bad = sorted(set(char for char in line if char not in font["chars"]))
        if bad or not line:
            raise AppError("The picture text %r has characters that the font does not have." % line)

    if len(lines) == 1:
        font = fonts["big"]
        text_w = design.text_width(font, lines[0])
        gap = (3 if icon_w + 5 + text_w > 74 else 5) if icon_rows else 0
        text_y = (PICTURE_H - font["height"] + 1) // 2
        text_ys = [text_y]
    else:
        font = fonts["5x5"]
        text_w = max(design.text_width(font, line) for line in lines)
        gap = 6 if icon_rows else 0
        text_ys = [6, 13]
    total = icon_w + gap + text_w
    if total > PICTURE_W:
        raise AppError("The picture is %d pixels wide; 80 is the most. Use a shorter text, two lines "
                       "(picture icon NAME \"LINE 1\" \"LINE 2\") or a smaller icon." % total)
    x0 = (PICTURE_W - total + 1) // 2
    if icon_rows:
        frame.rows(x0, (PICTURE_H - icon_h + 1) // 2, icon_rows, 1)
    for line, y in zip(lines, text_ys):
        if len(lines) == 2 and not icon_rows:
            x = (PICTURE_W - design.text_width(font, line) + 1) // 2   # text only: each line in the middle
        else:
            x = x0 + icon_w + gap
        frame.text(font["name"], x, y, "left", 1, 0, line)
    return [row[:PICTURE_W] for row in frame.lines()[:PICTURE_H]]


def picture_bytes(rows):
    """80 x 24 rows to the 240 bytes of the package"""
    return rows_to_bitmap(rows)


# --- StatTrak Script: words --------------------------------------------------------------------------

TOKEN_RE = re.compile(r"""
    (?P<space>[ \t\r]+)
  | (?P<comment>\#[^\n]*)
  | (?P<newline>\n)
  | (?P<number>0[xX][0-9A-Fa-f]+|[0-9]+)
  | (?P<string>"(?:[^"\\\n]|\\["\\])*")
  | (?P<name>[A-Za-z_][A-Za-z0-9_]*)
  | (?P<op>\+=|-=|==|!=|<=|>=|&&|\|\||<<|>>|[-+*/%<>=!&|^(){},;])
""", re.X)


def tokenize(source, filename):
    """The words of a script: (kind, value, line). Line breaks inside ( ) are ignored."""
    tokens = []
    line = 1
    depth = 0
    pos = 0
    while pos < len(source):
        match = TOKEN_RE.match(source, pos)
        if not match:
            char = source[pos]
            if char == '"':
                raise AppError("%s, line %d: a text has no closing \"." % (filename, line))
            raise AppError("%s, line %d: the character %r is not possible here." % (filename, line, char))
        kind = match.lastgroup
        value = match.group()
        pos = match.end()
        if kind == "newline":
            if depth == 0:
                tokens.append(("newline", "\n", line))
            line += 1
        elif kind in ("space", "comment"):
            pass
        elif kind == "number":
            tokens.append(("number", int(value, 0), line))
        elif kind == "string":
            tokens.append(("string", re.sub(r"\\(.)", r"\1", value[1:-1]), line))
        elif kind == "name":
            tokens.append(("name", value, line))
        else:
            if value == "(":
                depth += 1
            elif value == ")":
                depth = max(0, depth - 1)
            tokens.append(("op", value, line))
    tokens.append(("newline", "\n", line))
    tokens.append(("end", None, line))
    return tokens


# --- StatTrak Script: sentences --------------------------------------------------------------------

BINARY_LEVELS = [["||"], ["&&"], ["|"], ["^"], ["&"], ["==", "!="], ["<", "<=", ">", ">="], ["<<", ">>"],
                 ["+", "-"], ["*", "/", "%"]]


class Program:
    """What the parser finds in a script"""

    def __init__(self):
        self.name = None
        self.version = 1
        self.app_id = None
        self.description = ""
        self.picture = None        # (kind, icon, lines, path, line)
        self.permissions = 0
        self.columns = []          # the names of the data columns (the `data` line)
        self.settings = []         # (key, name, values, default, line)
        self.saved = []            # (name, value expression, line)
        self.vars = []
        self.consts = {}           # name -> (value expression, line)
        self.bitmaps = []          # (key, rows, line)
        self.handlers = {}         # event -> (body, line)
        self.funcs = {}            # name -> (params, body, line)
        self.lines = {}            # first line of each top-level name, for messages
        self.seen = set()          # the lines that can be there one time only


class Parser:
    def __init__(self, tokens, filename, base_dir):
        self.tokens = tokens
        self.pos = 0
        self.filename = filename
        self.base_dir = base_dir

    # Helpers
    def error(self, line, text):
        raise AppError("%s, line %d: %s" % (self.filename, line, text))

    def peek(self, ahead=0):
        return self.tokens[min(self.pos + ahead, len(self.tokens) - 1)]

    def take(self):
        token = self.tokens[self.pos]
        self.pos += 1
        return token

    def is_op(self, value, ahead=0):
        token = self.peek(ahead)
        return token[0] == "op" and token[1] == value

    def expect_op(self, value):
        token = self.take()
        if token[0] != "op" or token[1] != value:
            self.error(token[2], "\"%s\" is missing here (found %s)." % (value, self.show(token)))
        return token

    def expect(self, kind, what):
        token = self.take()
        if token[0] != kind:
            self.error(token[2], "%s is missing here (found %s)." % (what, self.show(token)))
        return token

    @staticmethod
    def show(token):
        if token[0] == "newline":
            return "the end of the line"
        if token[0] == "end":
            return "the end of the file"
        if token[0] == "string":
            return "the text \"%s\"" % token[1]
        return "\"%s\"" % token[1]

    def skip_newlines(self):
        while self.peek()[0] == "newline" or self.is_op(";"):
            self.take()

    def end_statement(self):
        token = self.peek()
        if token[0] == "newline" or (token[0] == "op" and token[1] == ";"):
            self.take()
        elif not (token[0] == "op" and token[1] == "}") and token[0] != "end":
            self.error(token[2], "the line must end here (found %s)." % self.show(token))

    def number_value(self):
        """A fixed whole number of a top-level line: 5, -5, 0x10 or a name of a const"""
        negative = False
        if self.is_op("-"):
            self.take()
            negative = True
        token = self.take()
        if token[0] == "number":
            return ("num", -token[1] if negative else token[1], token[2])
        if token[0] == "name":
            expression = ("name", token[1], token[2])
            return ("un", "-", expression, token[2]) if negative else expression
        self.error(token[2], "a whole number is missing here (found %s)." % self.show(token))

    # Top level
    def parse(self):
        program = Program()
        while True:
            self.skip_newlines()
            token = self.peek()
            if token[0] == "end":
                break
            if token[0] != "name":
                self.error(token[2], "a line must start with a word like app, setting, var or on "
                                     "(found %s)." % self.show(token))
            self.take()
            word, line = token[1], token[2]
            handler = getattr(self, "top_" + word, None)
            if handler is None:
                self.error(line, "\"%s\" is not known here. A line starts with: app, version, id, "
                                 "description, picture, permission, data, setting, const, saved, var, "
                                 "bitmap, on or func." % word)
            handler(program, line)
            self.end_statement()
        if program.name is None:
            raise AppError("%s: the line app \"NAME\" is missing." % self.filename)
        return program

    def _once(self, program, word, line):
        if word in program.seen:
            self.error(line, "\"%s\" can be there one time only." % word)
        program.seen.add(word)

    def top_app(self, program, line):
        self._once(program, "app", line)
        program.name = self.expect("string", "the name of the app, in quotes,")[1]
        program.lines["app"] = line

    def top_version(self, program, line):
        self._once(program, "version", line)
        token = self.expect("number", "the version number")
        if token[1] > 0xFFFF:
            self.error(line, "the version must be from 0 to 65535.")
        program.version = token[1]

    def top_id(self, program, line):
        self._once(program, "id", line)
        token = self.expect("number", "the app id (for example 0x1234ABCD)")
        if token[1] <= RESERVED_IDS or token[1] >= 0xFFFFFFFF:
            self.error(line, "the id must be from 0x100 to 0xFFFFFFFE (1 to 255 are for the built-in apps).")
        program.app_id = token[1]

    def top_description(self, program, line):
        self._once(program, "description", line)
        program.description = self.expect("string", "the description, in quotes,")[1]
        if len(program.description) > 120:
            self.error(line, "the description has %d characters; 120 is the most." % len(program.description))

    def top_picture(self, program, line):
        if program.picture is not None:
            self.error(line, "\"picture\" can be there one time only.")
        token = self.expect("name", "icon, text or bitmap")
        kind = token[1]
        if kind == "icon":
            icon = self.expect("name", "the name of the icon")[1]
            if icon not in app_icons.ICONS:
                self.error(line, "there is no icon \"%s\". Icons: %s." % (icon, ", ".join(sorted(app_icons.ICONS))))
            lines = self.strings()
            program.picture = ("icon", icon, lines, None, line)
        elif kind == "text":
            program.picture = ("text", None, self.strings(), None, line)
        elif kind == "bitmap":
            path = self.expect("string", "the PNG file, in quotes,")[1]
            program.picture = ("bitmap", None, [], os.path.join(self.base_dir, path), line)
        else:
            self.error(line, "after picture comes icon, text or bitmap.")
        if kind != "bitmap" and len(program.picture[2]) not in (1, 2):
            self.error(line, "the picture needs 1 or 2 texts in quotes.")

    def strings(self):
        out = []
        while self.peek()[0] == "string":
            out.append(self.take()[1])
        return out

    def top_permission(self, program, line):
        while True:
            token = self.expect("name", "counters, sound, awake or background")
            if token[1] == "data":
                self.error(line, "the data store comes from the line: data \"NAME\" ... (1 to 4 column names).")
            if token[1] not in SCRIPT_PERMISSIONS:
                self.error(line, "the permissions are: counters, sound, awake, background.")
            program.permissions |= PERMISSIONS[token[1]]
            if not self.is_op(","):
                break
            self.take()

    def top_data(self, program, line):
        """data "TIME" "MV": a data store with these columns (docs/DATA.md section 6)"""
        self._once(program, "data", line)
        names = self.strings()
        if not 1 <= len(names) <= MAX_COLUMNS:
            self.error(line, "data needs 1 to %d column names in quotes, for example: data \"TIME\" \"MV\"" % MAX_COLUMNS)
        for name in names:
            if not 1 <= len(name) <= COLUMN_NAME_LEN or any(ord(char) < 0x20 or ord(char) > 0x7E for char in name):
                self.error(line, "the column name \"%s\" must have 1 to %d characters (space to ~)." % (name, COLUMN_NAME_LEN))
        if len(set(names)) != len(names):
            self.error(line, "two data columns have the same name.")
        program.columns = names
        program.permissions |= PERM_DATA

    def top_setting(self, program, line):
        key = self.expect("name", "a short name for the setting (used in setting(NAME))")[1]
        name = self.expect("string", "the name of the setting, in quotes,")[1]
        values = self.strings()
        default = 0
        if self.peek()[0] == "name" and self.peek()[1] == "default":
            self.take()
            token = self.take()
            if token[0] == "number":
                default = token[1]
            elif token[0] == "string" and token[1] in values:
                default = values.index(token[1])
            else:
                self.error(line, "after default comes the number of the value (0 = the first) or one of the values in quotes.")
        program.settings.append((key, name, values, default, line))
        self.define(program, key, line)

    def top_const(self, program, line):
        key = self.expect("name", "the name of the const")[1]
        self.expect_op("=")
        program.consts[key] = (self.number_value(), line)
        self.define(program, key, line)

    def _variable(self, program, line, target):
        key = self.expect("name", "the name of the variable")[1]
        value = ("num", 0, line)
        if self.is_op("="):
            self.take()
            value = self.number_value()
        target.append((key, value, line))
        self.define(program, key, line)

    def top_saved(self, program, line):
        self._variable(program, line, program.saved)

    def top_var(self, program, line):
        self._variable(program, line, program.vars)

    def top_bitmap(self, program, line):
        key = self.expect("name", "the name of the bitmap")[1]
        if self.peek()[0] == "string":
            path = os.path.join(self.base_dir, self.take()[1])
            rows = _read_png(path)
        else:
            self.expect_op("{")
            rows = []
            while True:
                self.skip_newlines()
                if self.is_op("}"):
                    self.take()
                    break
                rows.append(self.expect("string", "a row of the bitmap in quotes (# = on, . = off)")[1])
        if (not rows or any(not re.fullmatch(r"[#.]+", row) for row in rows)
                or len(set(len(row) for row in rows)) != 1 or len(rows[0]) > 128 or len(rows) > 32):
            self.error(line, "a bitmap is 1 to 32 rows of the same length (1 to 128), with # (on) and . (off).")
        program.bitmaps.append((key, rows, line))
        self.define(program, key, line)

    def top_on(self, program, line):
        event = self.expect("name", "the event (open, draw, press1, press2, long1, long2, tick, close, down1, down2, "
                                    "kill or death)")[1]
        if event not in EVENTS:
            self.error(line, "the events are: %s." % ", ".join(EVENTS))
        if event in program.handlers:
            self.error(line, "there is already an \"on %s\"." % event)
        program.handlers[event] = (self.block(), line)

    def top_func(self, program, line):
        name = self.expect("name", "the name of the function")[1]
        self.expect_op("(")
        params = []
        while not self.is_op(")"):
            params.append(self.expect("name", "the name of a parameter")[1])
            if not self.is_op(","):
                break
            self.take()
        self.expect_op(")")
        if len(set(params)) != len(params):
            self.error(line, "two parameters have the same name.")
        program.funcs[name] = (params, self.block(), line)
        self.define(program, name, line)

    def define(self, program, key, line):
        if key in program.lines:
            self.error(line, "the name \"%s\" is already used (line %d)." % (key, program.lines[key]))
        if key in BUILTIN_NAMES or key in SYS_BY_NAME or key in OPCODE_FUNCTIONS or key in STATEMENT_WORDS:
            self.error(line, "\"%s\" is a name of the language. Use an other name." % key)
        program.lines[key] = line

    # Statements
    def block(self):
        self.expect_op("{")
        body = []
        while True:
            self.skip_newlines()
            if self.is_op("}"):
                self.take()
                return body
            if self.peek()[0] == "end":
                self.error(self.peek()[2], "a \"}\" is missing.")
            body.append(self.statement())
            self.end_statement()

    def statement(self):
        token = self.peek()
        line = token[2]
        if token[0] == "name":
            word = token[1]
            if word == "var":
                self.take()
                name = self.expect("name", "the name of the variable")[1]
                value = None
                if self.is_op("="):
                    self.take()
                    value = self.expression()
                return ("var", name, value, line)
            if word == "if":
                self.take()
                return self.if_rest(line)
            if word == "while":
                self.take()
                condition = self.expression()
                return ("while", condition, self.block(), line)
            if word == "return":
                self.take()
                value = None
                ahead = self.peek()
                if not (ahead[0] in ("newline", "end") or (ahead[0] == "op" and ahead[1] in ("}", ";"))):
                    value = self.expression()
                return ("return", value, line)
            if word == "else":
                self.error(line, "\"else\" without \"if\".")
            if self.peek(1)[0] == "op" and self.peek(1)[1] in ("=", "+=", "-="):
                self.take()
                op = self.take()[1]
                return ("set", word, op, self.expression(), line)
        expression = self.expression()
        if expression[0] != "call":
            self.error(line, "this line does nothing. A line is: var x = ..., x = ..., if, while, return or a call.")
        return ("expr", expression, line)

    def if_rest(self, line):
        condition = self.expression()
        body = self.block()
        otherwise = []
        save = self.pos
        self.skip_newlines()
        if self.peek()[0] == "name" and self.peek()[1] == "else":
            self.take()
            if self.peek()[0] == "name" and self.peek()[1] == "if":
                inner_line = self.take()[2]
                otherwise = [self.if_rest(inner_line)]
            else:
                otherwise = self.block()
        else:
            self.pos = save
        return ("if", condition, body, otherwise, line)

    # Expressions
    def expression(self, level=0):
        if level == len(BINARY_LEVELS):
            return self.unary()
        left = self.expression(level + 1)
        while self.peek()[0] == "op" and self.peek()[1] in BINARY_LEVELS[level]:
            token = self.take()
            right = self.expression(level + 1)
            left = ("bin", token[1], left, right, token[2])
        return left

    def unary(self):
        token = self.peek()
        if token[0] == "op" and token[1] in ("-", "!", "+"):
            self.take()
            inner = self.unary()
            if token[1] == "+":
                return inner
            if token[1] == "-" and inner[0] == "num":
                return ("num", -inner[1], token[2])
            return ("un", token[1], inner, token[2])
        return self.primary()

    def primary(self):
        token = self.take()
        if token[0] == "number":
            return ("num", token[1], token[2])
        if token[0] == "string":
            return ("str", token[1], token[2])
        if token[0] == "op" and token[1] == "(":
            inner = self.expression()
            self.expect_op(")")
            return inner
        if token[0] == "name":
            if self.is_op("("):
                self.take()
                args = []
                while not self.is_op(")"):
                    args.append(self.expression())
                    if not self.is_op(","):
                        break
                    self.take()
                self.expect_op(")")
                return ("call", token[1], args, token[2])
            return ("name", token[1], token[2])
        self.error(token[2], "a value is missing here (found %s)." % self.show(token))


# --- StatTrak Script: code -----------------------------------------------------------------------

class Compiler:
    def __init__(self, program, filename):
        self.program = program
        self.filename = filename
        self.items = []            # ("op", opcode, operand) or ("label", number)
        self.label_count = 0
        self.strings = []
        self.warnings = []
        self.global_names = {}     # slot -> name (for the disassembly)
        self.used_permissions = {}
        self.engine2 = False       # a call of engine version 2: the package is format 3
        # The data column names are the first strings (docs/DATA.md section 6)
        for column in program.columns:
            self.strings.append(column)

    def error(self, line, text):
        raise AppError("%s, line %d: %s" % (self.filename, line, text))

    def label(self):
        self.label_count += 1
        return self.label_count

    def emit(self, name, operand=None):
        self.items.append(("op", OP[name], operand))

    def place(self, label):
        self.items.append(("label", label))

    def push(self, value, line):
        if value < -0x80000000 or value > 0x7FFFFFFF:
            self.error(line, "the number %d is too large (32 bits with sign: -2147483648 to 2147483647)." % value)
        if -128 <= value <= 127:
            self.emit("PUSH8", value)
        elif -32768 <= value <= 32767:
            self.emit("PUSH16", value)
        else:
            self.emit("PUSH32", value)

    # Names
    def fixed_value(self, expression, depth=0):
        """The value of an expression that is a fixed number at compile time, else None"""
        kind = expression[0]
        if kind == "num":
            return expression[1]
        if kind == "un" and expression[1] == "-":
            inner = self.fixed_value(expression[2], depth)
            return None if inner is None else -inner
        if kind == "name":
            name = expression[1]
            if self.locals is not None and name in self.locals:
                return None
            if name in self.program.consts:
                if depth > 20:
                    self.error(expression[2], "the const \"%s\" uses itself." % name)
                return self.fixed_value(self.program.consts[name][0], depth + 1)
            if name in self.setting_index:
                return self.setting_index[name]
            if name in self.bitmap_index:
                return self.bitmap_index[name]
            if name in BUILTIN_NAMES and name not in self.globals:
                return BUILTIN_NAMES[name]
        return None

    def string_id(self, text, line):
        if len(text) > MAX_STRING_LEN:
            self.error(line, "the text \"%s\" has %d characters; %d is the most." % (text, len(text), MAX_STRING_LEN))
        if any(ord(char) < 0x20 or ord(char) > 0x7E for char in text):
            self.error(line, "the text \"%s\" has characters that the fonts do not have." % text)
        if text not in self.strings:
            self.strings.append(text)
            if len(self.strings) > MAX_STRINGS:
                self.error(line, "the app has more than %d different texts." % MAX_STRINGS)
        return self.strings.index(text)

    # Expressions
    def expression(self, expression):
        kind = expression[0]
        line = expression[-1]
        if kind == "num":
            self.push(expression[1], line)
        elif kind == "str":
            self.push(self.string_id(expression[1], line), line)
        elif kind == "name":
            name = expression[1]
            if self.locals is not None and name in self.locals:
                self.emit("LOAD", self.locals[name])
            elif name in self.globals:
                self.emit("LOAD", self.globals[name])
            else:
                value = self.fixed_value(expression)
                if value is None:
                    if name in self.program.funcs or name in SYS_BY_NAME or name in OPCODE_FUNCTIONS:
                        self.error(line, "\"%s\" is a function. Call it with ( ): %s(...)." % (name, name))
                    self.error(line, "the name \"%s\" is not known. Make a variable first: var %s = 0" % (name, name))
                self.push(value, line)
        elif kind == "un":
            self.expression(expression[2])
            self.emit("NEG" if expression[1] == "-" else "NOT")
        elif kind == "bin":
            op = expression[1]
            if op in ("&&", "||"):
                false_label, end_label = self.label(), self.label()
                if op == "&&":
                    self.expression(expression[2])
                    self.emit("JZ", false_label)
                    self.expression(expression[3])
                    self.emit("JZ", false_label)
                    self.push(1, line)
                    self.emit("JMP", end_label)
                    self.place(false_label)
                    self.push(0, line)
                else:
                    true_label = false_label
                    self.expression(expression[2])
                    self.emit("JNZ", true_label)
                    self.expression(expression[3])
                    self.emit("JNZ", true_label)
                    self.push(0, line)
                    self.emit("JMP", end_label)
                    self.place(true_label)
                    self.push(1, line)
                self.place(end_label)
            else:
                self.expression(expression[2])
                self.expression(expression[3])
                self.emit({"+": "ADD", "-": "SUB", "*": "MUL", "/": "DIV", "%": "MOD", "==": "EQ", "!=": "NE",
                           "<": "LT", "<=": "LE", ">": "GT", ">=": "GE", "&": "BAND", "|": "BOR", "^": "BXOR",
                           "<<": "SHL", ">>": "SHR"}[op])
        elif kind == "call":
            self.call(expression)

    def call(self, expression):
        _, name, args, line = expression
        if name in OPCODE_FUNCTIONS:
            opcode, count = OPCODE_FUNCTIONS[name]
            if len(args) != count:
                self.error(line, "%s() needs %d value%s." % (name, count, "" if count == 1 else "s"))
            for arg in args:
                self.expression(arg)
            self.emit(opcode)
        elif name in SYS_BY_NAME:
            sys_id = SYS_BY_NAME[name]
            _, arg_names, permission, draws = SYSCALLS[sys_id]
            if sys_id in SYS_ENGINE2:
                self.engine2 = True
            if name == "log":
                columns = len(self.program.columns)
                if not columns:
                    self.error(line, "log() needs a data store. Add a line like: data \"TIME\" \"MV\"")
                if len(args) != columns:
                    self.error(line, "log() needs %d value%s: one for each data column (%s)."
                               % (columns, "" if columns == 1 else "s", ", ".join(self.program.columns)))
                args = list(args) + [("num", 0, line)] * (4 - columns)
            elif permission == PERM_DATA and not self.program.columns:
                self.error(line, "%s() needs a data store. Add a line like: data \"TIME\" \"MV\"" % name)
            if len(args) != len(arg_names):
                self.error(line, "%s() needs %d value%s: %s(%s)." % (name, len(arg_names), "" if len(arg_names) == 1
                                                                      else "s", name, ", ".join(arg_names)))
            if permission and not self.program.permissions & permission:
                word = permission_words(permission)[0]
                self.error(line, "%s() needs the permission \"%s\". Add the line: permission %s" % (name, word, word))
            if draws and self.context == "handler" and self.event != "draw":
                self.warnings.append("%s, line %d: %s() draws only in \"on draw\". Here it does nothing."
                                     % (self.filename, line, name))
            for arg_name, arg in zip(arg_names, args):
                value = self.fixed_value(arg)
                if value is not None:
                    low_high = ARG_RANGES.get((name, arg_name))
                    if name == "setting" and arg_name == "n":
                        low_high = (0, len(self.program.settings) - 1)
                    if name == "bitmap" and arg_name == "id":
                        low_high = (0, len(self.program.bitmaps) - 1)
                    if low_high and not low_high[0] <= value <= low_high[1]:
                        self.error(line, "in %s(), %s must be from %d to %d (it is %d)."
                                   % (name, arg_name, low_high[0], low_high[1], value))
                self.expression(arg)
            self.emit("SYS", sys_id)
        elif name in self.program.funcs:
            params = self.program.funcs[name][0]
            if len(args) != len(params):
                self.error(line, "%s() needs %d value%s." % (name, len(params), "" if len(params) == 1 else "s"))
            for arg in args:
                self.expression(arg)
            self.emit("CALL", ("func", name))
        else:
            self.error(line, "there is no function \"%s\"." % name)

    # Statements
    def statements(self, body):
        for statement in body:
            self.statement(statement)

    def statement(self, statement):
        kind = statement[0]
        line = statement[-1]
        if kind == "var":
            _, name, value, _ = statement
            if name in self.locals:
                self.error(line, "the variable \"%s\" is already made in this %s." % (name, self.where()))
            if name in self.globals or name in self.program.lines or name in BUILTIN_NAMES \
                    or name in SYS_BY_NAME or name in OPCODE_FUNCTIONS or name in STATEMENT_WORDS:
                self.error(line, "the name \"%s\" is already used. Use an other name." % name)
            slot = self.new_local(name, line)
            if value is not None:
                self.expression(value)
            else:
                self.push(0, line)
            self.locals[name] = slot
            self.emit("STORE", slot)
        elif kind == "set":
            _, name, op, value, _ = statement
            slot = self.locals.get(name, self.globals.get(name))
            if slot is None:
                if self.fixed_value(("name", name, line)) is not None:
                    self.error(line, "\"%s\" is a fixed value; it cannot change." % name)
                self.error(line, "the name \"%s\" is not known. Make it first: var %s = 0" % (name, name))
            if op != "=":
                self.emit("LOAD", slot)
            self.expression(value)
            if op == "+=":
                self.emit("ADD")
            elif op == "-=":
                self.emit("SUB")
            self.emit("STORE", slot)
        elif kind == "expr":
            self.expression(statement[1])
            self.emit("DROP")
        elif kind == "if":
            _, condition, body, otherwise, _ = statement
            else_label, end_label = self.label(), self.label()
            self.expression(condition)
            self.emit("JZ", else_label)
            self.statements(body)
            if otherwise:
                self.emit("JMP", end_label)
            self.place(else_label)
            if otherwise:
                self.statements(otherwise)
                self.place(end_label)
        elif kind == "while":
            _, condition, body, _ = statement
            top, end = self.label(), self.label()
            self.place(top)
            self.expression(condition)
            self.emit("JZ", end)
            self.statements(body)
            self.emit("JMP", top)
            self.place(end)
        elif kind == "return":
            value = statement[1]
            if self.context == "handler":
                if value is not None:
                    self.error(line, "\"on %s\" cannot return a value. Use: return" % self.event)
                self.emit("HALT")
            else:
                if value is None:
                    self.push(0, line)
                else:
                    self.expression(value)
                self.emit("RET")

    def where(self):
        return ("\"on %s\"" % self.event) if self.context == "handler" else ("function %s()" % self.event)

    def new_local(self, name, line):
        if self.context == "handler":
            slot = self.pool_base + self.handler_locals
            self.handler_locals += 1
            self.pool_size = max(self.pool_size, self.handler_locals)
        else:
            slot = self.next_slot
            self.next_slot += 1
        if slot >= MAX_GLOBALS:
            self.error(line, "the app has too many variables (%d is the most, with the variables in handlers "
                             "and functions)." % MAX_GLOBALS)
        self.global_names.setdefault(slot, []).append("%s.%s" % (self.event, name))
        return slot

    # The whole app
    def check_calls(self):
        """No recursion, and at most CALL_DEPTH function calls inside each other"""
        calls = {}

        def collect(node, out):
            if isinstance(node, tuple):
                if node and node[0] == "call" and node[1] in self.program.funcs:
                    out.add(node[1])
                for part in node:
                    collect(part, out)
            elif isinstance(node, list):
                for part in node:
                    collect(part, out)

        for name, (_, body, _) in self.program.funcs.items():
            calls[name] = set()
            collect(body, calls[name])

        depth_cache = {}

        def depth(name, path):
            if name in path:
                chain = " -> ".join(path[path.index(name):] + [name])
                self.error(self.program.funcs[name][2], "the function %s() calls itself (%s). That is not "
                           "possible in an app (no recursion)." % (name, chain))
            if name not in depth_cache:
                depth_cache[name] = 1 + max([depth(other, path + [name]) for other in calls[name]] or [0])
            return depth_cache[name]

        for name in self.program.funcs:
            if depth(name, []) > CALL_DEPTH:
                self.error(self.program.funcs[name][2], "the function %s() calls other functions more than %d "
                           "deep." % (name, CALL_DEPTH))

    def compile(self):
        program = self.program
        self.locals = None
        self.context = "top"
        self.event = ""
        self.setting_index = {}
        self.bitmap_index = {}
        self.globals = {}

        # Settings
        if len(program.settings) > MAX_SETTINGS:
            self.error(program.settings[MAX_SETTINGS][4], "an app has %d settings at most." % MAX_SETTINGS)
        for index, (key, name, values, default, line) in enumerate(program.settings):
            if not 1 <= len(name) <= SETTING_NAME_LEN:
                self.error(line, "the setting name \"%s\" must have 1 to %d characters." % (name, SETTING_NAME_LEN))
            if not MIN_VALUES <= len(values) <= MAX_VALUES:
                self.error(line, "a setting has %d to %d values." % (MIN_VALUES, MAX_VALUES))
            for value in values + [name]:
                if any(ord(char) < 0x20 or ord(char) > 0x7E for char in value):
                    self.error(line, "the text \"%s\" has characters that the fonts do not have." % value)
            for value in values:
                if not 1 <= len(value) <= VALUE_LEN:
                    self.error(line, "the value \"%s\" must have 1 to %d characters." % (value, VALUE_LEN))
            if not 0 <= default < len(values):
                self.error(line, "default must be from 0 to %d (the number of the value, 0 = the first)."
                           % (len(values) - 1))
            self.setting_index[key] = index

        # Bitmaps
        if len(program.bitmaps) > MAX_BITMAPS:
            self.error(program.bitmaps[MAX_BITMAPS][2], "an app has %d bitmaps at most." % MAX_BITMAPS)
        for index, (key, _, _) in enumerate(program.bitmaps):
            self.bitmap_index[key] = index

        for key, (value, line) in program.consts.items():
            if self.fixed_value(value) is None:
                self.error(line, "a const must be a whole number.")

        # Globals: the saved variables first, then the other variables
        saved_values = []
        for key, value, line in program.saved:
            fixed = self.fixed_value(value)
            if fixed is None:
                self.error(line, "the start value of \"%s\" must be a whole number." % key)
            saved_values.append((key, fixed, line))
        var_values = []
        for key, value, line in program.vars:
            fixed = self.fixed_value(value)
            if fixed is None:
                self.error(line, "the start value of \"%s\" must be a whole number." % key)
            var_values.append((key, fixed, line))
        for slot, (key, _, _) in enumerate(saved_values):
            self.globals[key] = slot
            self.global_names[slot] = key
        # A saved variable with a start value that is not 0 needs a hidden saved variable: it is 0
        # until the app was opened one time, so the open handler knows when to set the start values.
        started_slot = None
        if any(value for _, value, _ in saved_values):
            started_slot = len(saved_values)
            self.global_names[started_slot] = "hidden: started"
        saved_count = len(saved_values) + (1 if started_slot is not None else 0)
        if saved_count > MAX_SAVED:
            self.error(program.saved[-1][2], "an app has %d saved variables at most%s." % (
                MAX_SAVED, " (one is hidden: it remembers that the start values are set)" if started_slot is not None else ""))
        for index, (key, _, _) in enumerate(var_values):
            self.globals[key] = saved_count + index
            self.global_names[saved_count + index] = key
        self.pool_base = saved_count + len(var_values)
        if self.pool_base > MAX_GLOBALS:
            self.error(program.vars[-1][2], "the app has more than %d variables." % MAX_GLOBALS)

        self.check_calls()

        # kill and death come only to the background app (docs/APPS.md section 4.6)
        for event in BACKGROUND_EVENTS:
            if event in program.handlers and not program.permissions & PERM_BACKGROUND:
                self.error(program.handlers[event][1], "\"on %s\" runs only in the background app. "
                           "Add the line: permission background" % event)

        # Handlers. Their variables share one area: handlers never run at the same time.
        self.pool_size = 0
        handler_labels = {}
        has_start_values = started_slot is not None or any(value for _, value, _ in var_values)
        for event in EVENTS:
            if event not in program.handlers and not (event == "open" and has_start_values):
                continue
            body, line = program.handlers.get(event, ([], 0))
            self.context, self.event, self.locals, self.handler_locals = "handler", event, {}, 0
            label = self.label()
            handler_labels[event] = label
            self.place(label)
            if event == "open":
                # The start values: each time for the variables, the first time for the saved ones
                for key, value, var_line in var_values:
                    if value:
                        self.push(value, var_line)
                        self.emit("STORE", self.globals[key])
                if started_slot is not None:
                    started = self.label()
                    self.emit("LOAD", started_slot)
                    self.emit("JNZ", started)
                    for key, value, var_line in saved_values:
                        if value:
                            self.push(value, var_line)
                            self.emit("STORE", self.globals[key])
                    self.push(1, line)
                    self.emit("STORE", started_slot)
                    self.place(started)
            self.statements(body)
            self.emit("HALT")

        # Functions: their parameters and variables have their own places (after the handlers)
        self.next_slot = self.pool_base + self.pool_size
        func_labels = {}
        for name, (params, body, line) in program.funcs.items():
            self.context, self.event, self.locals = "func", name, {}
            label = self.label()
            func_labels[name] = label
            self.place(label)
            slots = []
            for param in params:
                if param in self.globals or param in program.lines or param in BUILTIN_NAMES or param in SYS_BY_NAME:
                    self.error(line, "the parameter name \"%s\" is already used. Use an other name." % param)
                slot = self.new_local(param, line)
                self.locals[param] = slot
                slots.append(slot)
            for slot in reversed(slots):
                self.emit("STORE", slot)
            self.statements(body)
            self.push(0, line)
            self.emit("RET")

        global_count = self.next_slot
        code, offsets = self.assemble(func_labels)
        if len(code) > MAX_CODE:
            raise AppError("%s: the code of the app has %d bytes; %d is the most." % (self.filename, len(code), MAX_CODE))
        handlers = [offsets[handler_labels[event]] if event in handler_labels else NO_HANDLER for event in EVENTS]
        return {
            "code": code, "handlers": handlers, "global_count": global_count, "saved_count": saved_count,
            "strings": list(self.strings), "warnings": self.warnings, "engine2": self.engine2,
            "global_names": {slot: "/".join(names) if isinstance(names, list) else names
                             for slot, names in self.global_names.items()},
        }

    def assemble(self, func_labels):
        offsets = {}
        pc = 0
        for item in self.items:
            if item[0] == "label":
                offsets[item[1]] = pc
            else:
                pc += 1 + OPERAND_SIZE[OPCODES[item[1]][1]]
        code = bytearray()
        for item in self.items:
            if item[0] == "label":
                continue
            _, opcode, operand = item
            kind = OPCODES[opcode][1]
            code.append(opcode)
            if kind == "i8":
                code += struct.pack("<b", operand)
            elif kind == "i16":
                code += struct.pack("<h", operand)
            elif kind == "i32":
                code += struct.pack("<i", operand)
            elif kind in ("g", "sys"):
                code.append(operand)
            elif kind == "rel":
                distance = offsets[operand] - (len(code) + 2)
                if not -32768 <= distance <= 32767:
                    raise AppError("%s: a jump is too long." % self.filename)
                code += struct.pack("<h", distance)
            elif kind == "abs":
                code += struct.pack("<H", offsets[func_labels[operand[1]]])
        return bytes(code), offsets


def make_app_id(name):
    """The app id from the name (when the script has no `id` line): CRC-32 of the name. An id that is
    not possible (0 .. 255 or 0xFFFFFFFF) gets its top bit switched."""
    value = zlib.crc32(name.encode("ascii")) & 0xFFFFFFFF
    if value <= RESERVED_IDS or value == 0xFFFFFFFF:
        value ^= 0x80000000
    return value


def assemble_package(name, app_id, version, permissions, picture, handlers, settings, strings, bitmaps,
                     code, global_count, saved_count, engine_version=None, columns=0, format_version=None):
    """Put the parts together as a package (docs/APPS.md section 3). `settings`: list of
    (name, values, default). `bitmaps`: list of (width, height, data). Blocks in this order after
    the handler table: settings, string table, strings, bitmap table, bitmap data, code, CRC.
    Format 3 for data columns, the permissions data / awake / background, a kill or death handler or
    an engine 2 call (`engine_version` 2); format 2 only if the app has a down1 or down2 handler:
    other packages stay format 1, so that older firmware can install them."""
    if format_version is None:
        if columns or permissions & ~PERM_V2 or engine_version == 2 or                 any(offset != NO_HANDLER for offset in handlers[HANDLERS_V2:]):
            format_version = 3
        elif any(offset != NO_HANDLER for offset in handlers[HANDLERS_V1:HANDLERS_V2]):
            format_version = 2
        else:
            format_version = 1
    if engine_version is None:
        engine_version = 2 if format_version >= 3 else 1
    handlers = list(handlers[:handler_count(format_version)])
    first = data_start(format_version)
    body = bytearray()

    def here():
        return first + len(body)

    settings_offset = here()
    for setting_name, values, default in settings:
        body += setting_name.encode("ascii").ljust(SETTING_NAME_LEN, b"\0")
        body += bytes([len(values), default])
        for value in values:
            body += value.encode("ascii").ljust(VALUE_LEN, b"\0")
    strings_offset = here()
    body += bytes(2 * len(strings))
    for index, text in enumerate(strings):
        struct.pack_into("<H", body, strings_offset - first + 2 * index, here())
        body += text.encode("ascii") + b"\0"
    bitmaps_offset = here()
    body += bytes(4 * len(bitmaps))
    for index, (width, height, data) in enumerate(bitmaps):
        struct.pack_into("<BBH", body, bitmaps_offset - first + 4 * index, width, height, here())
        body += data
    code_offset = here()
    body += code
    size = first + len(body) + CRC_SIZE

    header = bytearray(HEADER_SIZE)
    header[0:4] = MAGIC
    header[4] = format_version
    header[5] = engine_version
    struct.pack_into("<HIHH", header, 6, permissions, app_id, version, size)
    header[16:32] = name.encode("ascii").ljust(16, b"\0")
    header[32:38] = bytes([global_count, saved_count, len(settings), len(strings), len(bitmaps), columns])
    struct.pack_into("<5H", header, 38, settings_offset, strings_offset, bitmaps_offset, code_offset, len(code))
    package = bytes(header) + bytes(picture) + struct.pack("<%dH" % len(handlers), *handlers) + bytes(body)
    return package + struct.pack("<I", zlib.crc32(package) & 0xFFFFFFFF)


def compile_source(source, filename="app.sts", base_dir="."):
    """Compile a script. Return a dict: package (bytes), info (dict of the check), warnings, the
    description, the picture rows, the names of the globals, and the parsed program."""
    program = Parser(tokenize(source, filename), filename, base_dir).parse()
    line = program.lines["app"]
    name = program.name.upper()
    if not 1 <= len(name) <= MAX_NAME:
        raise AppError("%s, line %d: the name must have 1 to %d characters." % (filename, line, MAX_NAME))
    bad = sorted(set(char for char in name if char not in NAME_CHARS))
    if bad:
        raise AppError("%s, line %d: the name cannot have these characters: %s. Possible: A-Z 0-9 space + - %% / . :"
                       % (filename, line, " ".join(repr(char) for char in bad)))

    result = Compiler(program, filename).compile()

    if program.picture is None:
        rows = picture_rows("text", lines=[name])
    else:
        kind, icon, lines, path, picture_line = program.picture
        try:
            rows = picture_rows(kind, icon, lines, path)
        except AppError as error:
            raise AppError("%s, line %d: %s" % (filename, picture_line, error))

    bitmaps = [(len(rows_[0]), len(rows_), rows_to_bitmap(rows_)) for _, rows_, _ in program.bitmaps]
    settings = [(setting_name, values, default) for _, setting_name, values, default, _ in program.settings]
    package = assemble_package(name, program.app_id or make_app_id(name), program.version, program.permissions,
                               picture_bytes(rows), result["handlers"], settings, result["strings"], bitmaps,
                               result["code"], result["global_count"], result["saved_count"],
                               engine_version=2 if result["engine2"] else None, columns=len(program.columns))
    if len(package) > MAX_SIZE:
        raise AppError("%s: the package has %d bytes; %d is the most." % (filename, len(package), MAX_SIZE))
    status, info = check(package)
    if status != OK:
        raise AppError("%s: the compiler made a package that does not pass the check (%s). This is a "
                       "fault of tools/app.py." % (filename, result_text(status)))
    return {"package": package, "info": info, "warnings": result["warnings"], "description": program.description,
            "picture": rows, "global_names": result["global_names"], "program": program}


def compile_file(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            source = handle.read()
    except OSError as error:
        raise AppError("Cannot read %s: %s" % (path, error))
    return compile_source(source, os.path.basename(path), os.path.dirname(os.path.abspath(path)))


def load_package(path):
    """A .stapp file, or a .sts file that is compiled first. Return the package bytes."""
    if path.lower().endswith(".sts"):
        return compile_file(path)["package"]
    try:
        with open(path, "rb") as handle:
            return handle.read()
    except OSError as error:
        raise AppError("Cannot read %s: %s" % (path, error))


# --- Text output ---------------------------------------------------------------------------------

def disassemble(code, handlers=None, global_names=None):
    """The code as lines of text: "0004  JZ      0012". Handler starts and function starts get a
    line with their name before them."""
    names = {}
    for event, offset in zip(EVENTS, handlers or []):
        if offset != NO_HANDLER:
            names.setdefault(offset, []).append("on %s:" % event)
    instructions = decode(code)
    for pc, opcode, operand, size in instructions:
        if size and OPCODES[opcode][1] == "abs":
            names.setdefault(operand, []).append("func %04x:" % operand)
    lines = []
    for pc, opcode, operand, size in instructions:
        for name in names.get(pc, []):
            lines.append(name)
        if size == 0:
            lines.append("%04x  ??      0x%02x (not an instruction)" % (pc, opcode))
            break
        name, kind = OPCODES[opcode]
        if kind is None:
            text = name
        elif kind == "rel":
            text = "%-7s %04x" % (name, pc + size + operand)
        elif kind == "abs":
            text = "%-7s %04x" % (name, operand)
        elif kind == "sys":
            text = "%-7s %s" % (name, SYSCALLS[operand][0] if operand in SYSCALLS else "#%d" % operand)
        elif kind == "g":
            text = "%-7s g%d" % (name, operand)
            if global_names and operand in global_names:
                text += "  (%s)" % global_names[operand]
        else:
            text = "%-7s %d" % (name, operand)
        lines.append("%04x  %s" % (pc, text))
    return lines


def describe(data, global_names=None):
    parts = package_parts(data)
    out = []
    out.append("App \"%s\", version %d, id %08x, %d bytes (engine version %d)"
               % (parts["name"], parts["version"], parts["id"], parts["size"], parts["engine_version"]))
    out.append("Permissions: %s" % (", ".join(permission_words(parts["permissions"])) or "none"))
    if parts["column_names"]:
        out.append("Data columns: %s" % ", ".join(parts["column_names"]))
    out.append("Variables: %d (%d saved)" % (parts["global_count"], parts["saved_count"]))
    out.append("Title picture (80 x 24):")
    out.extend("  " + row for row in bitmap_to_rows(parts["picture"], PICTURE_W, PICTURE_H))
    out.append("Settings: %s" % ("none" if not parts["settings"] else ""))
    for index, setting in enumerate(parts["settings"]):
        values = ", ".join(("[%s]" if i == setting["default"] else "%s") % value
                           for i, value in enumerate(setting["values"]))
        out.append("  %d %s: %s" % (index, setting["name"], values))
    out.append("Texts: %s" % ("none" if not parts["strings"] else ""))
    for index, text in enumerate(parts["strings"]):
        out.append("  %d \"%s\"" % (index, text))
    out.append("Bitmaps: %s" % ("none" if not parts["bitmaps"] else ""))
    for index, (width, height, bitmap) in enumerate(parts["bitmaps"]):
        out.append("  %d: %d x %d" % (index, width, height))
        out.extend("    " + row for row in bitmap_to_rows(bitmap, width, height))
    out.append("Code (%d bytes):" % len(parts["code"]))
    out.extend("  " + line for line in disassemble(parts["code"], parts["handlers"], global_names))
    return "\n".join(out) + "\n"


# --- The engine in Python (to try an app on a computer) -------------------------------------------

class RunError(Exception):
    def __init__(self, result, pc):
        Exception.__init__(self, result_text(result))
        self.result = result
        self.pc = pc


class Screen(design.Frame):
    """The 128 x 32 display. Color 2 inverts a pixel."""

    def pixel(self, x, y, color):
        if 0 <= x < design.WIDTH and 0 <= y < design.HEIGHT:
            self.pixels[y][x] = (self.pixels[y][x] ^ 1) if color == 2 else (1 if color else 0)


class Machine:
    """Runs a checked package with the rules of docs/APPS.md section 4, and draws with the Python copy
    of the firmware draw functions (tools/design.py). The firmware engine is components/stapp/;
    this one is for previews and tests."""

    def __init__(self, package, seed=1):
        self.parts = package_parts(package)
        self.package = package
        self.globals = [0] * MAX_GLOBALS
        self.counters = [0, 0, 0, 0]
        self.settings = [setting["default"] for setting in self.parts["settings"]]
        self.now = 10000
        self.battery = 80
        self.battery_mv = 3900
        self.background = False    # true: the app runs as the background app (Settings > Background)
        self.data = []             # the rows of the data store (docs/DATA.md section 2)
        self.data_capacity = DATA_APP_VALUES
        self.random = random.Random(seed)
        self.clicks = []
        self.saves = 0
        self.error = None
        self.screen = None

    def run(self, event):
        """Run one handler. Return the result text ("ok" or the error)."""
        if self.error is not None:
            return result_text(self.error)
        index = EVENTS.index(event)
        start = self.parts["handlers"][index]
        if event == "draw":
            self.screen = Screen(_assets())
        if start == NO_HANDLER:
            return "ok"
        try:
            self._execute(start, event == "draw")
        except RunError as error:
            self.error = error.result
            return result_text(error.result)
        return "ok"

    def _execute(self, pc, drawing):
        code = self.parts["code"]
        stack = []
        calls = []
        steps = 0
        g = self.globals

        def pop():
            if not stack:
                raise RunError(ERR_STACK, pc)
            return stack.pop()

        def push(value):
            if len(stack) >= STACK_SIZE:
                raise RunError(ERR_STACK, pc)
            stack.append(_wrap(value))

        while True:
            steps += 1
            if steps > MAX_STEPS:
                raise RunError(ERR_STEPS, pc)
            opcode = code[pc]
            name, kind = OPCODES[opcode]
            size = 1 + OPERAND_SIZE[kind]
            operand = decode(code[pc:pc + size])[0][2]
            next_pc = pc + size
            if name == "HALT":
                return
            elif name.startswith("PUSH"):
                push(operand)
            elif name == "LOAD":
                push(g[operand])
            elif name == "STORE":
                g[operand] = pop()
            elif name == "DUP":
                value = pop()
                push(value)
                push(value)
            elif name == "DROP":
                pop()
            elif name == "SWAP":
                b, a = pop(), pop()
                push(b)
                push(a)
            elif name in ("NEG", "NOT", "ABS"):
                a = pop()
                push(-a if name == "NEG" else (1 if a == 0 else 0) if name == "NOT" else abs(a))
            elif name in ("JMP", "JZ", "JNZ"):
                if name == "JMP" or (pop() == 0) == (name == "JZ"):
                    next_pc += operand
            elif name == "CALL":
                if len(calls) >= CALL_DEPTH:
                    raise RunError(ERR_CALLS, pc)
                calls.append(next_pc)
                next_pc = operand
            elif name == "RET":
                if not calls:
                    raise RunError(ERR_CALLS, pc)
                next_pc = calls.pop()
            elif name == "SYS":
                count = len(SYSCALLS[operand][1])
                if len(stack) < count:
                    raise RunError(ERR_STACK, pc)
                args = stack[len(stack) - count:]
                del stack[len(stack) - count:]
                push(self._sys(operand, args, drawing, pc))
            else:
                b, a = pop(), pop()
                push(_binary(name, a, b))
            pc = next_pc

    def _sys(self, sys_id, args, drawing, pc):
        name = SYSCALLS[sys_id][0]
        if SYSCALLS[sys_id][3]:
            if not drawing:
                return 0
            screen = self.screen
            if name == "pixel":
                screen.pixel(*args)
            elif name == "hline":
                x, y, w, c = args
                for i in range(max(0, w)):
                    screen.pixel(x + i, y, c)
            elif name == "vline":
                x, y, h, c = args
                for i in range(max(0, h)):
                    screen.pixel(x, y + i, c)
            elif name == "rect":
                x, y, w, h, c = args
                if w > 0 and h > 0:
                    for i in range(w):
                        screen.pixel(x + i, y, c)
                        if h > 1:
                            screen.pixel(x + i, y + h - 1, c)
                    for i in range(1, h - 1):
                        screen.pixel(x, y + i, c)
                        if w > 1:
                            screen.pixel(x + w - 1, y + i, c)
            elif name == "fill":
                x, y, w, h, c = args
                screen.fill(x, y, max(0, w), max(0, h), c)
            elif name == "rrect":
                # GUI_DRAW_ROUND_RECT of main/app_view.h: a frame without the corner pixels
                x, y, w, h, filled = args
                for i in range(1, w - 1):
                    screen.pixel(x + i, y, 1)
                    screen.pixel(x + i, y + h - 1, 1)
                for i in range(1, h - 1):
                    screen.pixel(x, y + i, 1)
                    screen.pixel(x + w - 1, y + i, 1)
                if filled:
                    screen.fill(x + 1, y + 1, w - 2, h - 2, 1)
            elif name in ("text", "num"):
                if name == "text":
                    x, y, string_id, font, c, align = args
                    if not 0 <= string_id < len(self.parts["strings"]):
                        raise RunError(ERR_ARG, pc)
                    text = self.parts["strings"][string_id]
                else:
                    x, y, value, digits, font, c, align = args
                    text = "%0*d" % (max(0, min(digits, 10)), value)
                if 0 <= font < len(FONT_NAMES) and 0 <= align <= 2:
                    screen.text(FONT_NAMES[font], x, y, ("left", "center", "right")[align], c, 0, text)
            elif name == "bignum":
                x, y, value, digits, scale, c = args
                text = "%0*d" % (max(0, min(digits, 10)), value)
                screen.text("5x7", x, y, "left", c, 0, text, max(2, min(scale, 4)), 1)
            elif name == "bitmap":
                x, y, bitmap_id, c = args
                if not 0 <= bitmap_id < len(self.parts["bitmaps"]):
                    raise RunError(ERR_ARG, pc)
                width, height, data = self.parts["bitmaps"][bitmap_id]
                screen.rows(x, y, bitmap_to_rows(data, width, height), c)
            elif name == "invert":
                x, y, w, h = args
                screen.invert(x, y, x + w, y + h)
            return 0
        if name == "counter":
            return self.counters[args[0]] if 0 <= args[0] < 4 else 0
        if name == "add_counter":
            counter, delta = args
            if 0 <= counter < 4 and -10 <= delta <= 10:
                self.counters[counter] = max(0, self.counters[counter] + delta)
                if counter >= 2:
                    # A game counter also changes the matching total counter (like the built-in screens)
                    self.counters[counter - 2] = max(0, self.counters[counter - 2] + delta)
                return self.counters[counter]
            return 0
        if name == "setting":
            return self.settings[args[0]] if 0 <= args[0] < len(self.settings) else 0
        if name == "time":
            return self.now
        if name == "click":
            if not drawing and 200 <= args[0] <= 4000:   # the firmware plays no sound in draw
                self.clicks.append((self.now, args[0]))
            return 0
        if name == "tone":
            if not drawing and 200 <= args[0] <= 4000 and 10 <= args[1] <= 1000:
                self.clicks.append((self.now, args[0]))   # a note is listed with the clicks
            return 0
        if name == "battery":
            return self.battery
        if name == "battery_mv":
            return self.battery_mv if self.battery >= 0 else -1
        if name == "background":
            return 1 if self.background else 0
        columns = self.parts["columns"]
        if name == "log":
            # Full = stop. Not in draw (the firmware writes the rows later, never while it draws).
            if drawing or not columns or (len(self.data) + 1) * columns > self.data_capacity:
                return 0
            self.data.append(list(args[:columns]))
            return 1
        if name == "log_count":
            return len(self.data)
        if name == "log_get":
            row, column = args
            return self.data[row][column] if 0 <= row < len(self.data) and 0 <= column < columns else 0
        if name == "log_clear":
            if not drawing:
                self.data = []
            return 1 if not drawing else 0
        if name == "random":
            return self.random.randrange(args[0]) if 1 <= args[0] <= 65535 else 0
        if name == "save":
            self.saves += 1
        return 0

    def wait(self, ms):
        """Let time go by, as in the firmware: a draw each 10 ms, a tick each 100 ms"""
        for _ in range(ms // 10):
            self.now += 10
            if self.now % 100 == 0:
                self.run("tick")
            self.run("draw")

    def frame(self):
        """Run the draw handler and return the screen as 32 rows of 128 characters"""
        self.run("draw")
        return self.screen.lines()


def _binary(name, a, b):
    if name == "ADD":
        return a + b
    if name == "SUB":
        return a - b
    if name == "MUL":
        return a * b
    if name in ("DIV", "MOD"):
        if b == 0:
            return 0
        quotient = abs(a) // abs(b)
        if (a < 0) != (b < 0):
            quotient = -quotient
        return quotient if name == "DIV" else a - quotient * b
    if name in ("EQ", "NE", "LT", "LE", "GT", "GE"):
        return int({"EQ": a == b, "NE": a != b, "LT": a < b, "LE": a <= b, "GT": a > b, "GE": a >= b}[name])
    if name == "BAND":
        return a & b
    if name == "BOR":
        return a | b
    if name == "BXOR":
        return a ^ b
    if name == "SHL":
        return a << (b & 31)
    if name == "SHR":
        return a >> (b & 31)
    if name == "MIN":
        return min(a, b)
    if name == "MAX":
        return max(a, b)
    raise ValueError(name)


def run_actions(machine, actions):
    """Actions of the `run` command. Return the text output."""
    out = []
    for action in actions:
        if action in ("press1", "press2", "long1", "long2", "down1", "down2", "tick", "kill", "death"):
            result = machine.run(action)
        elif action.startswith("background:"):
            machine.background = action[11:] not in ("0", "")
            result = "ok"
        elif action.startswith("wait:"):
            machine.wait(int(action[5:]))
            result = "ok"
        elif action.startswith("counter:"):
            key, value = action[8:].split("=")
            index = BUILTIN_NAMES[key] if key in BUILTIN_NAMES else int(key)
            machine.counters[index] = int(value)
            result = "ok"
        elif action.startswith("setting:"):
            key, value = action[8:].split("=")
            machine.settings[int(key)] = int(value)
            result = "ok"
        elif action.startswith("random:"):
            machine.random = random.Random(int(action[7:]))
            result = "ok"
        elif action == "frame":
            out.extend(machine.frame())
            result = "ok"
        elif action.startswith("battery:"):
            machine.battery = int(action[8:])
            result = "ok"
        elif action.startswith("mv:"):
            machine.battery_mv = int(action[3:])
            result = "ok"
        elif action == "data":
            names = machine.parts["column_names"]
            out.append(",".join(names))
            out.extend(",".join(str(value) for value in row) for row in machine.data)
            result = "ok"
        elif action.startswith("png:"):
            write_png(action[4:], machine.frame())
            out.append("Wrote %s" % action[4:])
            result = "ok"
        else:
            raise AppError("Unknown action %r. Actions: press1 press2 long1 long2 down1 down2 tick kill death "
                           "wait:MS frame png:FILE counter:NAME=N setting:N=V random:SEED battery:PERCENT "
                           "mv:MILLIVOLTS background:0|1 data" % action)
        if result != "ok":
            out.append("App error after %s: %s" % (action, result))
    return out


# --- Catalog for the web updater -----------------------------------------------------------------

def build_catalog(apps_dir=None, out_dir=None):
    """Build each app in apps/*/ (apps/NAME/NAME.sts) into updater/apps/NAME.stapp and write
    updater/apps/catalog.json. Return the catalog (a dict)."""
    apps_dir = apps_dir or os.path.join(ROOT, "apps")
    out_dir = out_dir or os.path.join(ROOT, "updater", "apps")
    os.makedirs(out_dir, exist_ok=True)
    entries = []
    for folder in sorted(os.listdir(apps_dir)):
        source = os.path.join(apps_dir, folder, folder + ".sts")
        if not os.path.isfile(source):
            continue
        result = compile_file(source)
        if not result["description"]:
            raise AppError("%s: the line description \"...\" is missing (the web updater shows it)." % source)
        file_name = folder + ".stapp"
        with open(os.path.join(out_dir, file_name), "wb") as handle:
            handle.write(result["package"])
        info = result["info"]
        parts = package_parts(result["package"])
        entries.append({
            "id": "%08x" % info["id"],
            "name": info["name"],
            "version": info["version"],
            "size": info["size"],
            "permissions": permission_words(info["permissions"]),
            "description": result["description"],
            "file": file_name,
            "picture": base64.b64encode(parts["picture"]).decode("ascii"),
            "settings": [{"name": s["name"], "values": s["values"], "default": s["default"]}
                         for s in parts["settings"]],
        })
    catalog = {"format": "stattrak-apps", "version": 1, "apps": entries}
    with open(os.path.join(out_dir, "catalog.json"), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(json.dumps(catalog, indent=2) + "\n")
    return catalog


# --- Command line ----------------------------------------------------------------------------------

def main(argv=None):
    parser = argparse.ArgumentParser(description="StatTrak apps: build, check and show app packages",
                                     epilog="Specification: docs/APPS.md")
    sub = parser.add_subparsers(dest="action", required=True)
    build = sub.add_parser("build", help="compile a .sts script to a .stapp package")
    build.add_argument("file")
    build.add_argument("-o", "--out", help="the package file (default: next to the script)")
    sub.add_parser("check", help="check a package and print its info").add_argument("file")
    sub.add_parser("describe", help="print all parts of a package, with the code").add_argument("file")
    picture = sub.add_parser("picture", help="save the title picture as a PNG")
    picture.add_argument("file")
    picture.add_argument("-o", "--out", help="the PNG file (default: out/NAME_picture.png)")
    run = sub.add_parser("run", help="run the app in Python and print the screen")
    run.add_argument("file")
    run.add_argument("actions", nargs="*", help="press1 press2 long1 long2 down1 down2 tick kill death wait:MS "
                                                "frame png:FILE counter:NAME=N setting:N=V random:SEED "
                                                "background:0|1 (a frame at the end)")
    sub.add_parser("catalog", help="build apps/*/ into updater/apps/ with catalog.json")
    sim = sub.add_parser("sim", help="open the web simulator with the app installed and running")
    sim.add_argument("file")
    sim.add_argument("--fresh", action="store_true", help="a new device (no counters, settings or other apps)")
    sim.add_argument("--rebuild", action="store_true", help="build the simulator page again")
    sim.add_argument("--no-browser", action="store_true", help="only make the page, do not open it")
    args = parser.parse_args(argv)

    try:
        if args.action == "build":
            result = compile_file(args.file)
            for warning in result["warnings"]:
                print("Note: %s" % warning, file=sys.stderr)
            out = args.out or os.path.splitext(args.file)[0] + ".stapp"
            with open(out, "wb") as handle:
                handle.write(result["package"])
            info = result["info"]
            print("%s: %d bytes, id %08x, %d variables (%d saved). Check: ok"
                  % (out, info["size"], info["id"], info["global_count"], info["saved_count"]))
        elif args.action == "check":
            data = load_package(args.file)
            status, info = check(data)
            if status != OK:
                print("%s: %s" % (args.file, result_text(status)))
                return 1
            print("%s: ok" % args.file)
            print("  name %s, version %d, id %08x, %d bytes" % (info["name"], info["version"], info["id"], info["size"]))
            print("  permissions: %s" % (", ".join(permission_words(info["permissions"])) or "none"))
            print("  %d variables (%d saved), %d settings, %d texts, %d bitmaps"
                  % (info["global_count"], info["saved_count"], info["setting_count"], info["string_count"],
                     info["bitmap_count"]))
        elif args.action == "describe":
            names = None
            if args.file.lower().endswith(".sts"):
                result = compile_file(args.file)
                data, names = result["package"], result["global_names"]
            else:
                data = load_package(args.file)
            sys.stdout.write(describe(data, names))
        elif args.action == "picture":
            parts = package_parts(load_package(args.file))
            out = args.out or os.path.join(ROOT, "out", os.path.splitext(os.path.basename(args.file))[0] + "_picture.png")
            write_png(out, bitmap_to_rows(parts["picture"], PICTURE_W, PICTURE_H), 4)
            print(out)
        elif args.action == "run":
            machine = Machine(load_package(args.file))
            result = machine.run("open")
            if result != "ok":
                print("App error in open: %s" % result)
            lines = run_actions(machine, args.actions)
            if not args.actions or not (args.actions[-1] == "frame" or args.actions[-1].startswith("png:")):
                lines.extend(machine.frame())
            if machine.clicks:
                lines.append("Clicks: %s" % ", ".join("%d Hz at %d ms" % (freq, at) for at, freq in machine.clicks))
            print("\n".join(lines))
        elif args.action == "catalog":
            catalog = build_catalog()
            for entry in catalog["apps"]:
                print("updater/apps/%s: %s, %d bytes" % (entry["file"], entry["name"], entry["size"]))
            print("updater/apps/catalog.json: %d apps" % len(catalog["apps"]))
        elif args.action == "sim":
            import websim
            data = load_package(args.file)
            status, info = check(data)
            if status != OK:
                print("%s: %s" % (args.file, result_text(status)))
                return 1
            try:
                websim.open_with_app(data, info["name"], fresh=args.fresh, browser=not args.no_browser,
                                     rebuild=args.rebuild)
            except websim.WebSimError as error:
                print(error, file=sys.stderr)
                return 1
    except AppError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
