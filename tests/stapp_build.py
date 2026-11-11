"""A small builder of app packages (.stapp) for the tests, with a tiny assembler.

It makes the format of docs/APPS.md section 3 (the same layout as the builder in
tests/unit/test_stapp.c). tools/app.py is the real compiler for apps; this file only makes
test packages, also broken ones.

Example:

    asm = Asm()
    asm.handler("draw")
    asm.text(10, 5, 0, font=1)      # string 0
    asm.halt()
    package = build(name="HELLO", strings=["HI"], asm=asm)
"""

import struct
import zlib

HEADER_SIZE = 48
PICTURE_SIZE = 240
HANDLERS_OFFSET = HEADER_SIZE + PICTURE_SIZE
FIXED_END = HANDLERS_OFFSET + 16
NO_HANDLER = 0xFFFF

PERM_COUNTERS = 0x0001
PERM_SOUND = 0x0002

EVENTS = ["open", "draw", "press1", "press2", "long1", "long2", "tick", "close"]

OP = {
    "HALT": 0x00, "PUSH8": 0x01, "PUSH16": 0x02, "PUSH32": 0x03, "LOAD": 0x04, "STORE": 0x05,
    "DUP": 0x06, "DROP": 0x07, "SWAP": 0x08, "ADD": 0x10, "SUB": 0x11, "MUL": 0x12, "DIV": 0x13,
    "MOD": 0x14, "NEG": 0x15, "EQ": 0x16, "NE": 0x17, "LT": 0x18, "LE": 0x19, "GT": 0x1A, "GE": 0x1B,
    "NOT": 0x1C, "BAND": 0x1D, "BOR": 0x1E, "BXOR": 0x1F, "SHL": 0x20, "SHR": 0x21, "MIN": 0x22,
    "MAX": 0x23, "ABS": 0x24, "JMP": 0x30, "JZ": 0x31, "JNZ": 0x32, "CALL": 0x33, "RET": 0x34,
    "SYS": 0x40,
}

SYS = {
    "pixel": 0, "hline": 1, "vline": 2, "rect": 3, "fill": 4, "rrect": 5, "text": 6, "num": 7,
    "bignum": 8, "bitmap": 9, "invert": 10, "counter": 16, "add_counter": 17, "setting": 18,
    "time": 19, "click": 20, "battery": 21, "random": 22, "save": 23, "redraw": 24, "tone": 25,
}


class Asm:
    """Bytecode with labels. Each method adds one instruction (or a system call with its arguments)."""

    def __init__(self):
        self.code = bytearray()
        self.handlers = {}
        self.labels = {}
        self.fixups = []   # (position of the operand, label, kind)

    # --- Structure

    def handler(self, event):
        self.handlers[event] = len(self.code)
        return self

    def label(self, name):
        self.labels[name] = len(self.code)
        return self

    # --- Instructions

    def op(self, name):
        self.code.append(OP[name])
        return self

    def halt(self):
        return self.op("HALT")

    def push(self, value):
        if -128 <= value <= 127:
            self.code += bytes([OP["PUSH8"]]) + struct.pack("<b", value)
        elif -32768 <= value <= 32767:
            self.code += bytes([OP["PUSH16"]]) + struct.pack("<h", value)
        else:
            self.code += bytes([OP["PUSH32"]]) + struct.pack("<i", value)
        return self

    def load(self, index):
        self.code += bytes([OP["LOAD"], index])
        return self

    def store(self, index):
        self.code += bytes([OP["STORE"], index])
        return self

    def jump(self, name, label):
        self.code.append(OP[name])
        self.fixups.append((len(self.code), label, "rel"))
        self.code += b"\0\0"
        return self

    def call(self, label):
        self.code.append(OP["CALL"])
        self.fixups.append((len(self.code), label, "abs"))
        self.code += b"\0\0"
        return self

    def sys(self, name, *args):
        """Push the arguments, then SYS. Each argument is a number or None (already on the stack)."""
        for arg in args:
            if arg is not None:
                self.push(arg)
        self.code += bytes([OP["SYS"], SYS[name]])
        return self

    def sys_drop(self, name, *args):
        """A system call whose result is not used."""
        return self.sys(name, *args).op("DROP")

    # --- Result

    def assemble(self):
        code = bytearray(self.code)
        for pos, label, kind in self.fixups:
            target = self.labels[label]
            if kind == "rel":
                struct.pack_into("<h", code, pos, target - (pos + 2))
            else:
                struct.pack_into("<H", code, pos, target)
        return bytes(code)


def default_picture(seed=0):
    """A title picture: a frame around the 80 x 24 area, and a pattern in it."""
    pic = bytearray(PICTURE_SIZE)
    for x in range(80):
        pic[x] |= 0x01
        pic[160 + x] |= 0x80
    for page in range(3):
        pic[page * 80] = 0xFF
        pic[page * 80 + 79] = 0xFF
    for i in range(10, 70, 4):
        pic[80 + i] = (0x18 + seed) & 0xFF
    return bytes(pic)


def build(name="TEST APP", app_id=0x12345678, version=1, permissions=0, globals_count=4, saved=0,
          settings=(), strings=(), bitmaps=(), asm=None, code=None, handlers=None, picture=None,
          engine=1, pad_to=None):
    """Return a package as bytes.

    settings: list of (name, [value texts], default index)
    bitmaps:  list of (width, height, data bytes)
    asm:      an Asm (code and handlers from it), or code + handlers {event name: offset}
    pad_to:   make the package this size with unused bytes before the CRC (for size tests)
    """
    if asm is not None:
        code = asm.assemble()
        handlers = asm.handlers
    if code is None:
        code = bytes([OP["HALT"]])
        handlers = {"open": 0}
    handlers = handlers or {}

    head = bytearray(HEADER_SIZE)
    head[0:4] = b"STAP"
    head[4] = 1
    head[5] = engine
    struct.pack_into("<HIH", head, 6, permissions, app_id, version)
    encoded = name.encode("ascii")
    head[16:16 + len(encoded)] = encoded
    head[32] = globals_count
    head[33] = saved
    head[34] = len(settings)
    head[35] = len(strings)
    head[36] = len(bitmaps)

    body = bytearray()
    body += picture if picture is not None else default_picture()
    for event in EVENTS:
        body += struct.pack("<H", handlers.get(event, NO_HANDLER))

    def offset():
        return HEADER_SIZE + len(body)

    if settings:
        struct.pack_into("<H", head, 38, offset())
        for setting_name, values, default in settings:
            body += setting_name.encode("ascii").ljust(12, b"\0")
            body += bytes([len(values), default])
            for value in values:
                body += value.encode("ascii").ljust(8, b"\0")

    if strings:
        struct.pack_into("<H", head, 40, offset())
        table_pos = len(body)
        body += b"\0\0" * len(strings)
        for index, text in enumerate(strings):
            struct.pack_into("<H", body, table_pos + 2 * index, offset())
            body += text.encode("ascii") + b"\0"

    if bitmaps:
        struct.pack_into("<H", head, 42, offset())
        table_pos = len(body)
        body += b"\0\0\0\0" * len(bitmaps)
        for index, (width, height, data) in enumerate(bitmaps):
            struct.pack_into("<BBH", body, table_pos + 4 * index, width, height, offset())
            body += bytes(data)

    struct.pack_into("<HH", head, 44, offset(), len(code))
    body += code

    if pad_to is not None:
        body += bytes(max(0, pad_to - 4 - HEADER_SIZE - len(body)))

    package = bytearray(head + body + b"\0\0\0\0")
    struct.pack_into("<H", package, 14, len(package))
    return finish(package)


def finish(package):
    """Write the size and the CRC again (after a test changed bytes)."""
    package = bytearray(package)
    struct.pack_into("<H", package, 14, len(package))
    struct.pack_into("<I", package, len(package) - 4, zlib.crc32(bytes(package[:-4])) & 0xFFFFFFFF)
    return bytes(package)


def app_id_of(package):
    return struct.unpack_from("<I", package, 8)[0]
