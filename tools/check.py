#!/usr/bin/env python3
"""Run all checks of the StatTrak firmware on a computer. No device is necessary.

    python tools/check.py              all checks (2 to 5 minutes)
    python tools/check.py --fast       no memory checker, fewer random sessions (1 minute)
    python tools/check.py --build-only build the simulator programs only

Steps:
  1. Safety check     - tools/check_safety.py (dangerous source changes)
  2. Build            - the firmware code as computer programs, in build-host/
  3. Unit tests       - storage, battery, graphics, Bluetooth update packets
  4. Simulator tests  - the full firmware with simulated buttons, battery and flash

Run this before each firmware build for a device. All steps must pass.

Needs: Python 3.8+, CMake 3.16+ and a C compiler. If the computer does not have them:
python tools/setup.py simulator   (it installs them into the folder .venv, also on Windows)
"""

import argparse
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DIR = os.path.join(ROOT, "build-host")

sys.path.insert(0, os.path.join(ROOT, "tools"))
import toolchain  # noqa: E402


def log(text):
    print(text, flush=True)


def run(command, cwd=None, env=None, quiet=False):
    """Run a command. Return (exit code, output)."""
    process = subprocess.run(command, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             universal_newlines=True, errors="replace")
    if not quiet and process.returncode != 0:
        log(process.stdout[-6000:])
    return process.returncode, process.stdout


def step_safety():
    log("\n[1/4] Safety check")
    code, output = run([sys.executable, os.path.join(ROOT, "tools", "check_safety.py")], quiet=True)
    log(output.strip())
    return code == 0


def configure(sanitize, bits32):
    if os.path.isdir(BUILD_DIR):
        shutil.rmtree(BUILD_DIR)

    cmake = toolchain.find_program("cmake")
    command = [cmake, "-S", os.path.join(ROOT, "sim"), "-B", BUILD_DIR,
               "-DSTATTRAK_SANITIZE=%s" % ("ON" if sanitize else "OFF"),
               "-DSTATTRAK_32BIT=%s" % ("ON" if bits32 else "OFF"),
               "-DSTATTRAK_SDL=OFF"] + toolchain.cmake_compiler_arguments()

    code, _ = run(command, quiet=True)
    if code != 0:
        return False
    code, _ = run([cmake, "--build", BUILD_DIR, "--parallel"], quiet=True)
    return code == 0


def step_build(fast):
    log("\n[2/4] Build the firmware code for this computer")

    compiler, _ = toolchain.find_compiler()
    if not toolchain.find_program("cmake") or compiler is None:
        log("CMake or a C compiler is not installed on this computer.")
        log("Run: python tools/setup.py simulator   (it tells you what it installs, then installs it)")
        return None

    # The best build first: memory checker + 32-bit like the device. Then simpler builds.
    # The compiler from .venv ("zig cc") has no memory checker.
    variants = [(False, False)] if (fast or compiler == "zig") else [(True, True), (True, False), (False, False)]
    for sanitize, bits32 in variants:
        if configure(sanitize, bits32):
            name = "%s, %s" % ("with memory checker" if sanitize else "no memory checker",
                               "32-bit like the device" if bits32 else "64-bit")
            log("Build: OK (%s)" % name)
            if not fast and not sanitize:
                log("NOTE: this computer cannot build with the memory checker (AddressSanitizer). "
                    "Some memory errors are not found without it.")
            elif not fast and not bits32:
                log("NOTE: no 32-bit build on this computer (Linux: install gcc-multilib). The 64-bit build is used.")
            return {"sanitize": sanitize, "bits32": bits32}

    # Show the error of the plain build
    cmake = toolchain.find_program("cmake")
    command = [cmake, "-S", os.path.join(ROOT, "sim"), "-B", BUILD_DIR, "-DSTATTRAK_SDL=OFF"]
    code, _ = run(command + toolchain.cmake_compiler_arguments())
    if code == 0:
        run([cmake, "--build", BUILD_DIR])
    log("Build: FAILED")
    return None


def program(name):
    for candidate in (os.path.join(BUILD_DIR, name), os.path.join(BUILD_DIR, name + ".exe"),
                      os.path.join(BUILD_DIR, "Debug", name + ".exe")):
        if os.path.isfile(candidate):
            return candidate
    return None


def step_unit_tests():
    log("\n[3/4] Unit tests")
    ok = True
    for name in ("test_units", "test_ble_ota"):
        path = program(name)
        if path is None:
            log("%s: program not found" % name)
            ok = False
            continue
        code, output = run([path], quiet=True)
        last = output.strip().split("\n")[-1] if output.strip() else ""
        if code == 0:
            log("%s: OK (%s)" % (name, last))
        else:
            log("%s: FAILED" % name)
            log(output[-6000:])
            ok = False
    return ok


def step_simulator_tests(fast, seeds):
    log("\n[4/4] Simulator tests")
    headless = program("stattrak_headless")
    if headless is None:
        log("stattrak_headless: program not found")
        return False

    env = dict(os.environ)
    env["STATTRAK_HEADLESS"] = headless
    if program("gfx_tool"):
        env["STATTRAK_GFX_TOOL"] = program("gfx_tool")
    if seeds is not None:
        env["STATTRAK_FUZZ_SEEDS"] = str(seeds)
    elif fast:
        env["STATTRAK_FUZZ_SEEDS"] = "1"

    code, output = run([sys.executable, "-m", "unittest", "discover", "-s", os.path.join(ROOT, "tests"),
                        "-p", "test_*.py"], cwd=ROOT, env=env, quiet=True)
    lines = output.strip().split("\n")
    if code == 0:
        log("Simulator tests: OK (%s)" % " ".join(line for line in lines[-3:] if line.startswith("Ran")))
    else:
        log(output[-12000:])
        log("Simulator tests: FAILED")
    return code == 0


def main():
    parser = argparse.ArgumentParser(description="Run all checks of the StatTrak firmware")
    parser.add_argument("--fast", action="store_true", help="no memory checker and fewer random sessions")
    parser.add_argument("--build-only", action="store_true", help="build the simulator programs only")
    parser.add_argument("--seeds", type=int, default=None, help="count of random button sessions (default 4)")
    args = parser.parse_args()

    start = time.time()

    if args.build_only:
        return 0 if step_build(args.fast) else 1

    results = []
    results.append(("Safety check", step_safety()))

    build = step_build(args.fast)
    results.append(("Build", build is not None))
    if build is not None:
        results.append(("Unit tests", step_unit_tests()))
        results.append(("Simulator tests", step_simulator_tests(args.fast, args.seeds)))

    log("\n--- Result (%d s)" % int(time.time() - start))
    for name, ok in results:
        log("  %-16s %s" % (name, "OK" if ok else "FAILED"))

    if all(ok for _, ok in results) and len(results) == 4:
        log("\nAll checks passed. This does NOT show that Bluetooth update works on a real device.")
        log("Read docs/SAFETY.md before you put this firmware on a device.")
        return 0

    log("\nDo NOT put this firmware on a device.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
