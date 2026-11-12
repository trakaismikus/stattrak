"""Python control of the headless StatTrak simulator.

The simulator runs the real firmware code on a computer (see sim/README.md).
One process is one "power session" of the device. The flash content stays in a
text file between sessions, so a test can switch the device off and on again.

Example:

    from stattrak_sim import Device

    with Device() as dev:
        dev.boot()                  # hold both buttons, then release them
        dev.click(1)                # short press on button 1
        print(dev.state()["eeprom.total_kills"])
        print(dev.frame_text())     # the display as text
        dev.save_png("screen.png")  # the display as a picture
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import zlib

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

OLED_WIDTH = 128
OLED_HEIGHT = 32

# Time for the firmware to see a short press: the button driver waits
# CONFIG_BUTTON_SHORT_PRESS_TIME_MS (180 ms) after the release.
CLICK_SETTLE_MS = 300

# The rows of the settings view (SETTING_LIST in main/app.h). SHOW_BATTERY and LOW_BAT are not
# there for the battery types NONE and USB.
SETTING_ROWS = [
    "GAME_RESET", "COUNTER", "BATTERY_TYPE", "AUTO_OFF", "SHOW_BATTERY", "LOW_BAT",
    "BRIGHTNESS", "DIM_SCREEN", "DIM_LEVEL", "SOUND", "BLUETOOTH", "FACTORY_RESET",
    "VERSION", "SERIAL",
]

# The app menu has 12 apps at most (main/apps.h)
APP_MENU_MAX = 12

# App manager commands (main/ble_commands.h, docs/APPS.md section 6)
APP_CMD_INFO = 0x0110
APP_CMD_LIST = 0x0111
APP_CMD_NAME = 0x0112
APP_CMD_BEGIN = 0x0113
APP_CMD_DATA = 0x0114
APP_CMD_END = 0x0115
APP_CMD_STATUS = 0x0116
APP_CMD_DELETE = 0x0117
APP_CMD_SET_ORDER = 0x0118
APP_CMD_SET_HIDDEN = 0x0119
APP_CMD_SETTING = 0x011A
APP_CMD_ASK = 0x011B
APP_CMD_DEVICE_NAME = 0x011C


class SimError(Exception):
    """The simulator refused a command."""


class SimCrash(Exception):
    """The simulator process stopped in a way that is a firmware bug (crash, sanitizer report, hang)."""


def find_headless():
    """Return the path of the headless simulator program."""
    env = os.environ.get("STATTRAK_HEADLESS")
    if env:
        return env

    names = ["stattrak_headless", "stattrak_headless.exe"]
    dirs = [
        os.path.join(REPO_ROOT, "build-host"),
        os.path.join(REPO_ROOT, "build-host", "Debug"),
    ]
    for directory in dirs:
        for name in names:
            path = os.path.join(directory, name)
            if os.path.isfile(path):
                return path

    raise FileNotFoundError(
        "The headless simulator is not built. Run: python tools/check.py --build-only "
        "(or set STATTRAK_HEADLESS to the program path)."
    )


class Device:
    """One simulated StatTrak device."""

    def __init__(self, state_file=None, binary=None, command_timeout=120.0):
        self._binary = binary or find_headless()
        self._timeout = command_timeout
        self._proc = None
        self._stderr_file = None
        self._tmp_dir = None

        if state_file is None:
            self._tmp_dir = tempfile.mkdtemp(prefix="stattrak_sim_")
            state_file = os.path.join(self._tmp_dir, "device_state.txt")
        self.state_file = state_file

        # Set when the session ended: (reason, time_ms, detail)
        self.ended = None

    # --- Context manager -------------------------------------------------------------------------

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False

    def close(self):
        """Stop the simulator and remove temporary files."""
        self.power_cut()
        if self._tmp_dir:
            shutil.rmtree(self._tmp_dir, ignore_errors=True)
            self._tmp_dir = None

    # --- Power -----------------------------------------------------------------------------------

    @property
    def is_on(self):
        return self._proc is not None and self.ended is None

    def power_on(self, hold="both", pending=False, voltage=None, usb=False, log=False):
        """Start a power session. `hold` = buttons held at power-on: "both", "1", "2" or "none".

        The buttons stay held until you call release(). Use boot() for a normal start.
        """
        if self._proc is not None:
            self.power_cut()

        args = [self._binary, "--state", self.state_file, "--hold", str(hold)]
        if pending:
            args.append("--pending")
        if voltage is not None:
            args += ["--voltage", str(int(voltage))]
        if usb:
            args.append("--usb")
        if log:
            args.append("--log")

        self.ended = None
        self._stderr_file = tempfile.TemporaryFile(mode="w+b")
        self._proc = subprocess.Popen(
            args,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=self._stderr_file,
            universal_newlines=True,
            bufsize=1,
        )
        return self

    def boot(self, pending=False, voltage=None, settle_ms=1000):
        """Normal start: hold both buttons, release them, wait for the start animation."""
        self.power_on(hold="both", pending=pending, voltage=voltage)
        self.run(400)
        if self.is_on:
            self.release(1)
            self.release(2)
            self.run(settle_ms)
        return self

    def power_cut(self):
        """Remove the power immediately (as if the battery is pulled out)."""
        if self._proc is None:
            return
        try:
            if self._proc.poll() is None:
                try:
                    self._proc.stdin.write("quit\n")
                    self._proc.stdin.flush()
                    self._proc.wait(timeout=5)
                except (OSError, ValueError, subprocess.TimeoutExpired):
                    self._proc.kill()
                    self._proc.wait()
        finally:
            for stream in (self._proc.stdin, self._proc.stdout):
                try:
                    stream.close()
                except (OSError, ValueError):
                    pass
            self._proc = None
            if self._stderr_file is not None:
                self._stderr_file.close()
                self._stderr_file = None

    # --- Commands --------------------------------------------------------------------------------

    def _stderr_text(self):
        if self._stderr_file is None:
            return ""
        self._stderr_file.flush()
        self._stderr_file.seek(0)
        return self._stderr_file.read().decode("utf-8", errors="replace")

    def command(self, line):
        """Send one command. Return the answer lines (without the final "ok")."""
        if self._proc is None:
            raise SimError("The device is off. Call power_on() or boot() first.")
        if self.ended is not None:
            raise SimError("The session ended: %s" % (self.ended,))

        timed_out = []

        def _kill():
            timed_out.append(True)
            self._proc.kill()

        timer = threading.Timer(self._timeout, _kill)
        timer.start()
        try:
            try:
                self._proc.stdin.write(line + "\n")
                self._proc.stdin.flush()
            except (OSError, ValueError):
                pass  # The process is gone. The read below reports why.

            lines = []
            while True:
                out = self._proc.stdout.readline()
                if out == "":
                    self._proc.wait()
                    code = self._proc.returncode
                    stderr = self._stderr_text()
                    if timed_out:
                        raise SimCrash(
                            "The firmware did not answer in %.0f s (command: %r). "
                            "It is in a loop that never calls vTaskDelay().\n%s" % (self._timeout, line, stderr)
                        )
                    raise SimCrash(
                        "The simulator stopped with exit code %s (command: %r).\n%s" % (code, line, stderr)
                    )
                out = out.rstrip("\r\n")
                if out == "ok":
                    return lines
                if out.startswith("err"):
                    raise SimError("%s (command: %r)" % (out, line))
                if out.startswith("end "):
                    parts = out.split(" ", 3)
                    reason = parts[1]
                    time_ms = int(parts[2].split("=", 1)[1]) if len(parts) > 2 else 0
                    detail = parts[3] if len(parts) > 3 else ""
                    self.ended = (reason, time_ms, detail)
                    self._proc.wait()
                    if reason == "abort":
                        raise SimCrash("The firmware aborted: %s\n%s" % (detail, self._stderr_text()))
                    return lines
                lines.append(out)
        finally:
            timer.cancel()

    def run(self, ms):
        """Let simulated time pass."""
        if self.is_on:
            self.command("run %d" % int(ms))
        return self

    def press(self, button):
        self.command("btn %d 1" % int(button))
        return self

    def release(self, button):
        self.command("btn %d 0" % int(button))
        return self

    def click(self, button, hold_ms=60, settle_ms=CLICK_SETTLE_MS):
        """Short press on one button (1 or 2)."""
        self.press(button)
        self.run(hold_ms)
        if self.is_on:
            self.release(button)
            self.run(settle_ms)
        return self

    def click_both(self, hold_ms=80, settle_ms=500):
        """Short press on the two buttons together (app menu, or open the selected app)."""
        self.press(1)
        self.press(2)
        self.run(hold_ms)
        if self.is_on:
            self.release(1)
            self.release(2)
            self.run(settle_ms)
        return self

    def long_press(self, button, hold_ms=800, settle_ms=500):
        """Long press on one button (1 or 2)."""
        self.press(button)
        self.run(hold_ms)
        if self.is_on:
            self.release(button)
            self.run(settle_ms)
        return self

    def long_press_both(self, hold_ms=800, settle_ms=1500):
        """Long press on the two buttons together (power off)."""
        self.press(1)
        self.press(2)
        self.run(hold_ms)
        if self.is_on:
            self.release(1)
            self.release(2)
            self.run(settle_ms)
        return self

    def set_voltage(self, millivolts):
        self.command("voltage %d" % int(millivolts))
        return self

    def ota_transfer(self, duration_ms):
        """A phone sends new firmware. The update view must be open."""
        self.command("ota transfer %d" % int(duration_ms))
        return self

    def ota_accept(self):
        """A phone accepts the new firmware. Return True if a firmware was pending."""
        lines = self.command("ota accept")
        return "accepted=1" in lines

    def ble_command(self, cmd_id, data=b"", wait_ms=100):
        """A phone sends a user command over Bluetooth (see main/ble_commands.h).

        Return (status, answer bytes), or None when the firmware did not answer.
        Raises SimError when Bluetooth is off.
        """
        self.command("ble cmd %04x %s" % (cmd_id, bytes(data).hex()))
        self.run(wait_ms)
        for line in self.command("ble response"):
            if line.startswith("response cmd="):
                fields = dict(part.split("=", 1) for part in line.split()[1:])
                if int(fields["cmd"], 16) != cmd_id:
                    raise SimError("answer to command %s, expected %04x" % (fields["cmd"], cmd_id))
                return int(fields["status"]), bytes.fromhex(fields.get("data", ""))
        return None

    # --- Apps (docs/APPS.md): the app manager of the web updater ---------------------------------

    def open_app_sync(self):
        """Open Settings > "Bluetooth: ENABLE" > APPS with the buttons, as a person does. The device must
        be on."""
        if self.view == "APP_SYNC":
            return self
        if self.view == "BLUETOOTH":
            self.long_press(2)
            if self.view != "APP_SYNC":
                raise SimError("could not open the app sync (view %s)" % self.view)
            return self
        if self.view != "SETTINGS":
            self.click_both()
            for _ in range(APP_MENU_MAX):
                if self.state()["launcher_sel"] == "SETTINGS":
                    break
                self.click(1)
            self.click_both()
        if self.view != "SETTINGS":
            raise SimError("could not open the settings (view %s)" % self.view)
        # Move the cursor to "Bluetooth" (up from the first row goes to the last row, so no "to the top")
        target = SETTING_ROWS.index("BLUETOOTH")
        for _ in range(2 * len(SETTING_ROWS)):
            row = self.state()["settings_row"]
            if row == target:
                break
            self.click(2 if row < target else 1, settle_ms=200)
        self.long_press(2)                 # "Bluetooth: ENABLE": the Bluetooth view
        if self.view != "BLUETOOTH":
            raise SimError("could not open Bluetooth: ENABLE (view %s)" % self.view)
        self.long_press(2)                 # APPS
        if self.view != "APP_SYNC":
            raise SimError("could not open the app sync (view %s)" % self.view)
        return self

    def ble_commands(self, commands, wait_ms=20):
        """Send many user commands, 4 at a time (the size of the command queue).

        `commands` = list of (cmd_id, data). Return the list of (status, answer bytes).
        """
        answers = []
        for start in range(0, len(commands), 4):
            batch = commands[start:start + 4]
            for cmd_id, data in batch:
                self.command("ble cmd %04x %s" % (cmd_id, bytes(data).hex()))
            self.run(wait_ms)
            for cmd_id, _ in batch:
                found = None
                for line in self.command("ble response"):
                    if line.startswith("response cmd="):
                        fields = dict(part.split("=", 1) for part in line.split()[1:])
                        found = (int(fields["status"]), bytes.fromhex(fields.get("data", "")))
                answers.append(found)
        return answers

    def install_app(self, package, answer="yes", open_sync=True, crc=None, name=None):
        """Install a package like the web updater: BEGIN, ASK (the device asks "INSTALL <NAME>?"),
        the person answers, STATUS, then (after yes) DATA, END and STATUS.

        answer: "yes" (hold button 2), "no" (hold button 1), "wait" (no answer: 30 s timeout) or
        None (do not answer; the device waits). name: the name field of ASK (16 bytes; default:
        the name field of the package). Return a dict: step (where it stopped: begin, ask,
        answer, data, end), status, data, state (the install state of STATUS, or None).
        """
        package = bytes(package)
        if open_sync:
            self.open_app_sync()
        app_id = struct.unpack_from("<I", package, 8)[0] if len(package) >= 12 else 0
        if crc is None:
            crc = zlib.crc32(package) & 0xFFFFFFFF
        if name is None:
            name = package[16:32] if len(package) >= 32 else b""
        result = {"step": "begin", "status": None, "data": b"", "state": None}

        status, data = self.ble_command(APP_CMD_BEGIN, struct.pack("<IHI", app_id, len(package) & 0xFFFF, crc))
        result.update(status=status, data=data)
        if status != 0:
            return result

        result["step"] = "ask"
        status, data = self.ble_command(APP_CMD_ASK, bytes(name).ljust(16, b"\0")[:16])
        result.update(status=status, data=data)
        if status != 0:
            return result

        result["step"] = "answer"
        if answer == "yes":
            self.long_press(2)
        elif answer == "no":
            self.long_press(1)
        elif answer == "wait":
            self.run(31000)
        if answer is None:
            return result
        status, data = self.ble_command(APP_CMD_STATUS)
        result.update(status=status, data=data, state=data[0] if data else None)
        if status != 0 or result["state"] != 1:
            return result

        result["step"] = "data"
        chunks = []
        for offset in range(0, len(package), 14):
            chunk = package[offset:offset + 14].ljust(14, b"\0")
            chunks.append((APP_CMD_DATA, struct.pack("<H", offset) + chunk))
        for answer_ in self.ble_commands(chunks):
            if answer_ is None or answer_[0] != 0:
                result.update(status=None if answer_ is None else answer_[0], data=b"" if answer_ is None else answer_[1])
                return result

        result["step"] = "end"
        status, data = self.ble_command(APP_CMD_END)
        result.update(status=status, data=data)
        if status != 0:
            return result

        status, data = self.ble_command(APP_CMD_STATUS)
        result.update(status=status, data=data, state=data[0] if data else None)
        return result

    def nvs_fail(self, fail=True):
        """Make all flash writes fail (or work again)."""
        self.command("nvs fail %d" % (1 if fail else 0))
        return self

    # --- Read the device -------------------------------------------------------------------------

    def state(self):
        """Return the device state as a dict. Numbers are int."""
        result = {}
        for line in self.command("state"):
            key, _, value = line.partition("=")
            result[key] = int(value) if value.lstrip("-").isdigit() else value
        return result

    @property
    def view(self):
        return self.state()["view"]

    def frame(self):
        """Return the display as 32 strings of 128 characters. '#' is a pixel that is on."""
        return self.command("frame")

    def frame_text(self):
        return "\n".join(self.frame())

    def save_png(self, path, scale=4):
        """Save the display as a PNG picture."""
        write_png(path, self.frame(), scale)
        return path

    # --- Flash content while the device is off ---------------------------------------------------

    def _state_lines(self):
        if not os.path.isfile(self.state_file):
            return []
        with open(self.state_file, "r", encoding="utf-8") as handle:
            return [line.rstrip("\r\n") for line in handle]

    def read_flash(self, namespace="eeprom"):
        """Return the saved number values of one flash namespace as a dict: {"total_kills": 3, ...}."""
        values = {}
        for line in self._state_lines():
            parts = line.split()
            if len(parts) == 4 and parts[0] == "nvs" and parts[1] == namespace:
                values[parts[2]] = int(parts[3])
        return values

    def read_blobs(self, namespace="apps"):
        """Return the saved byte arrays (blobs) of one flash namespace: {"list": b"...", ...}."""
        blobs = {}
        for line in self._state_lines():
            parts = line.split()
            if len(parts) == 4 and parts[0] == "blob" and parts[1] == namespace:
                blobs[parts[2]] = b"" if parts[3] == "-" else bytes.fromhex(parts[3])
        return blobs

    def write_blobs(self, blobs, namespace="apps", remove=()):
        """Change saved byte arrays while the device is off. `blobs` = {key: bytes}. Other lines stay."""
        if self._proc is not None and self.ended is None:
            raise SimError("Switch the device off before you change the flash content.")
        pending = self.read_pending()
        lines = []
        for line in self._state_lines():
            parts = line.split()
            if parts and parts[0] == "ota":
                continue
            if len(parts) >= 3 and parts[0] == "blob" and parts[1] == namespace and \
                    (parts[2] in blobs or parts[2] in remove):
                continue
            lines.append(line)
        for key, data in blobs.items():
            lines.append("blob %s %s %s" % (namespace, key, bytes(data).hex() or "-"))
        lines.append("ota pending %d" % pending)
        with open(self.state_file, "w", encoding="utf-8", newline="\n") as handle:
            for line in lines:
                handle.write(line + "\n")
        return self

    def read_pending(self):
        """Return the firmware-update stage in the state file (0, 1 or 2)."""
        if not os.path.isfile(self.state_file):
            return 0
        with open(self.state_file, "r", encoding="utf-8") as handle:
            for line in handle:
                parts = line.split()
                if len(parts) == 3 and parts[0] == "ota" and parts[1] == "pending":
                    return int(parts[2])
        return 0

    def write_flash(self, values, pending=None, namespace="eeprom", remove=()):
        """Change saved flash values while the device is off. Other values stay.

        `remove` is a list of value names to delete, as if an older firmware never stored them.
        """
        if self._proc is not None and self.ended is None:
            raise SimError("Switch the device off before you change the flash content.")

        current = self.read_flash(namespace)
        current.update(values)
        for key in remove:
            current.pop(key, None)
        if pending is None:
            pending = self.read_pending()

        # The lines of the other namespaces and the blobs stay as they are
        others = []
        for line in self._state_lines():
            parts = line.split()
            if not parts or line.startswith("#") or (parts[0] == "nvs" and len(parts) > 1 and parts[1] == namespace):
                continue
            if parts[0] == "ota":
                continue
            others.append(line)

        with open(self.state_file, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("# StatTrak simulator: saved device state (flash content)\n")
            for key, value in current.items():
                handle.write("nvs %s %s %d\n" % (namespace, key, value & 0xFFFFFFFF))
            for line in others:
                handle.write(line + "\n")
            handle.write("ota pending %d\n" % pending)
        return self


def write_png(path, frame_lines, scale=4):
    """Write display lines ('#' = on) as a grey PNG. No extra Python packages are necessary."""
    width = OLED_WIDTH * scale
    rows = []
    for line in frame_lines:
        row = bytearray()
        for char in line.ljust(OLED_WIDTH, ".")[:OLED_WIDTH]:
            row += (b"\xff" if char == "#" else b"\x10") * scale
        rows += [b"\x00" + bytes(row)] * scale
    raw = b"".join(rows)
    height = len(frame_lines) * scale

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(png)


if __name__ == "__main__":
    sys.stderr.write("This is a library. Use tools/sim.py to run the simulator from a command line.\n")
    sys.exit(2)
