#!/usr/bin/env python3
"""Build the StatTrak firmware file for the device.

    python tools/build.py              release build (use this one for a device)
    python tools/build.py --debug      debug build (log output on the serial port, larger)
    python tools/build.py --no-tests   do not run the tests first (the safety check always runs)

Steps:
  1. Safety check and the fast tests (python tools/check.py --fast). The build stops if they fail.
  2. The build, with ESP-IDF v5.4 of this computer or with Docker (image espressif/idf:v5.4).
  3. A check of the result: is it a firmware file for this chip, and does it fit in the device?

Result: build/stattrak.bin, and a copy with the version in its name in out/.
The file goes to the device over Bluetooth with the StatTrak web updater. See docs/SAFETY.md, section 4.

If ESP-IDF is missing: python tools/setup.py firmware
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import toolchain  # noqa: E402

BUILD_DIR = os.path.join(ROOT, "build")
FIRMWARE_FILE = os.path.join(BUILD_DIR, "stattrak.bin")
BUILD_TYPE_FILE = os.path.join(BUILD_DIR, ".stattrak_build_type")

WEB_UPDATER = "https://stattrak.tridos.design"

IMAGE_MAGIC = 0xE9
APP_MAGIC = 0xABCD5432
CHIP_ID_ESP32C2 = 0x000C


def log(text=""):
    print(text, flush=True)


def run(command, cwd=ROOT, shell=False):
    shown = command if isinstance(command, str) else " ".join(str(part) for part in command)
    log("  > " + shown)
    return subprocess.run(command, cwd=cwd, shell=shell).returncode


# --- The firmware file ---------------------------------------------------------------------------------

def update_slot_size():
    """Size in bytes of the smallest firmware slot in partitions.csv. The firmware must fit in it."""
    sizes = []
    with open(os.path.join(ROOT, "partitions.csv"), "r", encoding="utf-8") as handle:
        for line in handle:
            parts = [part.strip() for part in line.split(",")]
            if len(parts) >= 5 and parts[1] == "app":
                size = parts[4].lower()
                if size.endswith("k"):
                    sizes.append(int(size[:-1], 0) * 1024)
                elif size.endswith("m"):
                    sizes.append(int(size[:-1], 0) * 1024 * 1024)
                else:
                    sizes.append(int(size, 0))
    if not sizes:
        raise ValueError("partitions.csv has no firmware slot")
    return min(sizes)


def read_firmware_info(path):
    """Read the header of an ESP-IDF application file. Return a dict, or raise ValueError."""
    with open(path, "rb") as handle:
        data = handle.read(0x120)
    if len(data) < 0x120:
        raise ValueError("the file is too short to be a firmware file")
    if data[0] != IMAGE_MAGIC:
        raise ValueError("the file does not start as an ESP firmware file")
    chip_id = struct.unpack_from("<H", data, 12)[0]
    if chip_id != CHIP_ID_ESP32C2:
        raise ValueError("the file is for an other chip (chip id 0x%04X, the StatTrak has 0x%04X)"
                         % (chip_id, CHIP_ID_ESP32C2))
    if struct.unpack_from("<I", data, 0x20)[0] != APP_MAGIC:
        raise ValueError("the file is not an application (it can be a bootloader or a full flash picture)")

    def text(offset, length):
        return data[offset:offset + length].split(b"\0", 1)[0].decode("ascii", "replace")

    return {"version": text(0x30, 32), "project": text(0x50, 32), "size": os.path.getsize(path)}


def check_firmware(path):
    info = read_firmware_info(path)
    limit = update_slot_size()
    if info["project"] != "stattrak":
        raise ValueError("the project name in the file is %r, not \"stattrak\"" % info["project"])
    if info["size"] > limit:
        raise ValueError("the file has %d bytes. A firmware slot of the device has %d bytes."
                         % (info["size"], limit))
    info["limit"] = limit
    return info


# --- Steps ---------------------------------------------------------------------------------------------

def step_checks(no_tests):
    if no_tests:
        log("[1/3] Safety check (tests are skipped: --no-tests)")
        return run([sys.executable, os.path.join(ROOT, "tools", "check_safety.py")]) == 0

    log("[1/3] Safety check and fast tests")
    compiler, _ = toolchain.find_compiler()
    if not toolchain.find_program("cmake") or compiler is None:
        log("The simulator tools are not installed, so the tests cannot run.")
        log("Run: python tools/setup.py simulator   (or build without tests: --no-tests)")
        return False
    return run([sys.executable, os.path.join(ROOT, "tools", "check.py"), "--fast"]) == 0


def prepare_build_folder(build_type):
    """An old sdkconfig file wins over the defaults. Start clean when the build type changes."""
    previous = None
    try:
        with open(BUILD_TYPE_FILE, "r", encoding="utf-8") as handle:
            previous = handle.read().strip()
    except OSError:
        pass

    if previous != build_type:
        if os.path.isdir(BUILD_DIR) or os.path.isfile(os.path.join(ROOT, "sdkconfig")):
            log("The build type changed (%s -> %s): start with a clean build folder." % (previous or "unknown", build_type))
        shutil.rmtree(BUILD_DIR, ignore_errors=True)
        for name in ("sdkconfig", "sdkconfig.old"):
            try:
                os.remove(os.path.join(ROOT, name))
            except OSError:
                pass


def step_build(build_type):
    log("\n[2/3] Build the firmware (%s)" % build_type)
    arguments = ["-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.%s" % build_type, "build"]

    idf_dir, version = toolchain.find_idf()
    if idf_dir:
        log("ESP-IDF %s in %s" % (version, idf_dir))
        if " " in ROOT:
            log("This folder has a space in its name: %s" % ROOT)
            log("ESP-IDF cannot build there. Move the repository to a folder without spaces.")
            return False
        prepare_build_folder(build_type)
        code = toolchain.run_idf_script(idf_dir, "idf.py", arguments, cwd=ROOT)
    elif toolchain.docker_works():
        log("Docker image %s" % toolchain.IDF_DOCKER_IMAGE)
        prepare_build_folder(build_type)
        command = ["docker", "run", "--rm", "-v", "%s:/project" % ROOT, "-w", "/project"]
        if not toolchain.IS_WINDOWS:
            # The build files belong to you, not to the administrator of the computer
            command += ["--user", "%d:%d" % (os.getuid(), os.getgid()), "-e", "HOME=/tmp"]
        inner = ("git config --global --add safe.directory /project; "
                 "idf.py '%s' build" % arguments[0])
        code = run(command + [toolchain.IDF_DOCKER_IMAGE, "bash", "-c", inner])
    else:
        log("ESP-IDF %s is not on this computer, and Docker is not running." % toolchain.IDF_VERSION)
        log("Run: python tools/setup.py firmware")
        return False

    if code != 0:
        log("The build failed. The first line with \"error:\" above says where.")
        return False

    os.makedirs(BUILD_DIR, exist_ok=True)
    with open(BUILD_TYPE_FILE, "w", encoding="utf-8") as handle:
        handle.write(build_type + "\n")
    return True


def step_result(build_type):
    log("\n[3/3] Check the firmware file")
    if not os.path.isfile(FIRMWARE_FILE):
        log("The build made no file build/stattrak.bin.")
        return False
    try:
        info = check_firmware(FIRMWARE_FILE)
    except ValueError as error:
        log("build/stattrak.bin is not usable: %s." % error)
        return False

    with open(os.path.join(ROOT, "version.txt"), "r", encoding="utf-8") as handle:
        wanted = handle.read().strip()
    if info["version"] != wanted:
        log("build/stattrak.bin has the version %r, but version.txt says %r." % (info["version"], wanted))
        log("Delete the folder build/ and build again.")
        return False

    out_dir = os.path.join(ROOT, "out")
    os.makedirs(out_dir, exist_ok=True)
    safe_version = "".join(char if (char.isalnum() or char in ".-_") else "_" for char in info["version"]) or "unknown"
    copy = os.path.join(out_dir, "stattrak-%s%s.bin" % (safe_version, "" if build_type == "release" else "-debug"))
    shutil.copyfile(FIRMWARE_FILE, copy)

    log("Firmware file: %s" % copy)
    log("  version %s, %d bytes (%d %% of a firmware slot)"
        % (info["version"], info["size"], (100 * info["size"]) // info["limit"]))
    log("")
    log("Before you send it to a device, read docs/SAFETY.md, section 4.")
    log("Short version: send the file with the StatTrak web updater (%s) over Bluetooth. Then, BEFORE you" % WEB_UPDATER)
    log("use the device, open the update mode again and check that the web updater can still connect.")
    return True


def main():
    parser = argparse.ArgumentParser(description="Build the StatTrak firmware file for the device")
    parser.add_argument("--debug", action="store_true", help="debug build (default: release build)")
    parser.add_argument("--no-tests", action="store_true", help="do not run the tests first")
    parser.add_argument("--check-file", metavar="FILE", help="only check a firmware file, build nothing")
    args = parser.parse_args()

    if args.check_file:
        try:
            info = check_firmware(args.check_file)
        except (OSError, ValueError) as error:
            log("%s is not usable: %s." % (args.check_file, error))
            return 1
        log("%s: OK. Version %s, %d bytes (%d %% of a firmware slot)."
            % (args.check_file, info["version"], info["size"], (100 * info["size"]) // info["limit"]))
        return 0

    build_type = "debug" if args.debug else "release"
    if not step_checks(args.no_tests):
        log("\nThe checks failed. No firmware was built.")
        return 1
    if not step_build(build_type):
        return 1
    return 0 if step_result(build_type) else 1


if __name__ == "__main__":
    sys.exit(main())
