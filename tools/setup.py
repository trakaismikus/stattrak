#!/usr/bin/env python3
"""Get this computer ready for the StatTrak firmware. Made for AI coding tools and for people.

    python tools/setup.py              look only: what is here, what is missing. Changes nothing.
    python tools/setup.py simulator    install what the simulator and the tests need, then try them
    python tools/setup.py firmware     install what a firmware build for the device needs

Rule for AI tools: run the first command, tell the person in plain words what will be installed
and how large it is, and wait for a "yes". Then run the install command.

What the install commands do:

  simulator   Makes the folder .venv in this repository and installs three Python packages there:
              cmake, ninja and ziglang (a C compiler). About 150 MB download, 550 MB on disk.
              Nothing is installed outside of this folder. Skipped parts: the ones that the
              computer already has. Then it builds the simulator and saves a first picture.

  firmware    Downloads ESP-IDF v5.4, the software kit of the chip maker, and its compiler.
              About 2 GB download, 4 GB on disk, 10 to 30 minutes. It goes to
              C:\\Espressif\\frameworks\\esp-idf-v5.4 (Windows) or ~/esp/esp-idf-v5.4, and its
              tools go to the folder .espressif in your home folder. Needs Git.
              Not necessary when Docker is installed and running.

To remove everything again: delete .venv, the ESP-IDF folder and the .espressif folder.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import toolchain  # noqa: E402

SIMULATOR_PACKAGES = {"cmake": "cmake", "ninja": "ninja", "compiler": "ziglang"}


def log(text=""):
    print(text, flush=True)


def run(command, cwd=None, env=None):
    log("  > " + " ".join(str(part) for part in command))
    return subprocess.run(command, cwd=cwd, env=env).returncode


# --- Look ---------------------------------------------------------------------------------------------

def status():
    """What this computer has. Each item: (key, name, ok, detail)."""
    compiler_kind, compiler_path = toolchain.find_compiler()
    idf_dir, idf_version = toolchain.find_idf()
    docker = toolchain.docker_works()
    cmake = toolchain.find_program("cmake")
    ninja = toolchain.find_program("ninja")
    git = shutil.which("git")
    space = " " in ROOT

    simulator = [
        ("python", "Python 3.8 or newer", toolchain.python_ok(), sys.version.split()[0]),
        ("cmake", "CMake (build tool)", bool(cmake), cmake or "missing"),
        ("compiler", "C compiler", compiler_kind is not None,
         ("%s (%s)" % (compiler_path, "from .venv" if compiler_kind == "zig" else "of this computer"))
         if compiler_kind else "missing"),
        ("ninja", "Ninja (makes builds faster, optional)", bool(ninja), ninja or "missing"),
    ]
    firmware = [
        ("git", "Git", bool(git), git or "missing"),
        ("idf", "ESP-IDF %s, or Docker" % toolchain.IDF_VERSION, bool(idf_dir) or docker,
         ("ESP-IDF %s in %s" % (idf_version, idf_dir)) if idf_dir else
         ("Docker is running (image %s)" % toolchain.IDF_DOCKER_IMAGE) if docker else "missing"),
        ("path", "Folder name without spaces", not space,
         ROOT if not space else "%s  <- ESP-IDF cannot build in a folder with a space in its name" % ROOT),
    ]
    return {"simulator": simulator, "firmware": firmware}


def print_status(report):
    titles = (("simulator", "Simulator and tests (try your changes on this computer)"),
              ("firmware", "Firmware build (make the file for the device)"))
    for key, title in titles:
        ready = all(ok for name, _, ok, _ in report[key] if name != "ninja")
        log("%s: %s" % (title, "READY" if ready else "NOT READY"))
        for _, name, ok, detail in report[key]:
            log("  [%s] %-38s %s" % ("ok" if ok else "  ", name, detail))
        log()

    missing_simulator = [name for name, _, ok, _ in report["simulator"] if not ok and name in SIMULATOR_PACKAGES]
    idf_missing = not dict((name, ok) for name, _, ok, _ in report["firmware"])["idf"]

    if missing_simulator or idf_missing:
        log("To install what is missing:")
    if missing_simulator:
        log("  python tools/setup.py simulator")
        log("      Installs into the folder .venv of this repository: %s."
            % ", ".join(SIMULATOR_PACKAGES[name] for name in missing_simulator))
        log("      About 150 MB download, 550 MB on disk. Nothing outside of this folder changes.")
    if idf_missing:
        log("  python tools/setup.py firmware")
        log("      Downloads ESP-IDF %s (the software kit of the chip maker) to %s"
            % (toolchain.IDF_VERSION, toolchain.default_idf_dir()))
        log("      and its tools to the folder .espressif in your home folder.")
        log("      About 2 GB download, 4 GB on disk, 10 to 30 minutes.")
    if not missing_simulator and not idf_missing:
        log("Nothing to install. Next: python tools/check.py")


# --- Install: simulator -------------------------------------------------------------------------------

def install_simulator():
    report = status()
    missing = [name for name, _, ok, _ in report["simulator"] if not ok and name in SIMULATOR_PACKAGES]

    if not toolchain.python_ok():
        log("Python %s is too old. Install Python 3.8 or newer from https://www.python.org/downloads/"
            % sys.version.split()[0])
        return 1

    if missing:
        packages = [SIMULATOR_PACKAGES[name] for name in missing]
        log("Install into .venv: %s" % ", ".join(packages))
        if toolchain.venv_python() is None:
            if run([sys.executable, "-m", "venv", toolchain.VENV_DIR]) != 0:
                log("Cannot make the folder .venv. On Debian or Ubuntu, install the package python3-venv first.")
                return 1
        python = toolchain.venv_python()
        if run([python, "-m", "pip", "install", "--disable-pip-version-check", "--quiet"] + packages) != 0:
            log("The install did not work. Check the internet connection and try again.")
            return 1
    else:
        log("The computer has all programs for the simulator.")

    log("\nBuild the simulator")
    if run([sys.executable, os.path.join(ROOT, "tools", "check.py"), "--build-only"]) != 0:
        return 1

    log("\nTry the simulator: start the firmware and save a picture of the display")
    out_dir = os.path.join(ROOT, "out")
    code = run([sys.executable, os.path.join(ROOT, "tools", "sim.py"), "--fresh", "--out", out_dir,
                "boot", "click1", "click1", "click1", "png:setup_check"])
    picture = os.path.join(out_dir, "setup_check.png")
    if code != 0 or not os.path.isfile(picture):
        log("The simulator did not run.")
        return 1

    log("\nThe simulator works. Picture of the display: %s" % picture)
    log("Next: python tools/check.py   (all tests, 2 to 5 minutes)")
    return 0


# --- Install: firmware --------------------------------------------------------------------------------

def install_firmware(idf_dir):
    if toolchain.docker_works() and not toolchain.find_idf()[0]:
        log("Docker is running. tools/build.py uses the image %s. ESP-IDF is not installed."
            % toolchain.IDF_DOCKER_IMAGE)
        return 0

    existing, version = toolchain.find_idf()
    if existing:
        idf_dir = existing
        log("ESP-IDF %s is here: %s" % (version, idf_dir))
    else:
        git = shutil.which("git")
        if not git:
            log("Git is missing. Install it from https://git-scm.com/downloads and run this command again.")
            return 1
        if " " in idf_dir:
            log("The folder %r has a space in its name. ESP-IDF does not work there. Use --idf-dir." % idf_dir)
            return 1

        if not os.path.isfile(os.path.join(idf_dir, "tools", "idf.py")):
            log("Download ESP-IDF %s to %s (about 1.5 GB)" % (toolchain.IDF_VERSION, idf_dir))
            os.makedirs(os.path.dirname(idf_dir), exist_ok=True)
            code = run([git, "clone", "--branch", toolchain.IDF_VERSION, "--depth", "1", "--recursive",
                        "--shallow-submodules", "https://github.com/espressif/esp-idf.git", idf_dir])
            if code != 0:
                log("The download did not work. Delete the folder %s and try again." % idf_dir)
                return 1

    # A folder with ESP-IDF is not enough: its compiler and its Python packages must be there too
    if not toolchain.idf_works(idf_dir):
        log("\nInstall the compiler for the chip (%s)" % toolchain.IDF_TARGET)
        code = toolchain.run_idf_script(idf_dir, "install", [toolchain.IDF_TARGET], cwd=idf_dir)
        if code != 0 or not toolchain.idf_works(idf_dir):
            log("\nThe ESP-IDF install did not work. Its messages above say what is missing.")
            if not toolchain.IS_WINDOWS:
                log("On Debian or Ubuntu it needs: sudo apt install git wget flex bison gperf python3 python3-pip "
                    "python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0")
            return 1

    settings = toolchain.load_settings()
    settings["idf_path"] = idf_dir
    toolchain.save_settings(settings)

    log("\nESP-IDF is ready. Next: python tools/build.py")
    return 0


# --- Main ---------------------------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Get this computer ready for the StatTrak firmware",
                                     epilog="Without an action: look only, change nothing.")
    parser.add_argument("action", nargs="?", choices=["simulator", "firmware"],
                        help="what to install (leave out to look only)")
    parser.add_argument("--json", action="store_true", help="print what the computer has as JSON")
    parser.add_argument("--idf-dir", default=toolchain.default_idf_dir(),
                        help="folder for ESP-IDF (default: %(default)s)")
    args = parser.parse_args()

    if args.action == "simulator":
        return install_simulator()
    if args.action == "firmware":
        return install_firmware(args.idf_dir)

    report = status()
    if args.json:
        data = {key: [{"item": name, "name": text, "ok": ok, "detail": detail} for name, text, ok, detail in items]
                for key, items in report.items()}
        print(json.dumps(data, indent=2))
    else:
        print_status(report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
