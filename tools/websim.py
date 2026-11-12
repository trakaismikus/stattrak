#!/usr/bin/env python3
"""The web simulator (sim/README.md, "Web simulator"): build it, and open it with an app.

    python tools/app.py sim apps/NAME/NAME.sts      the normal way (app.py calls open_with_app())

The page is one HTML file: build-web/simulator.html. It runs the real firmware. The address
simulator.html#app=<package as base64url> installs the app like the web updater, answers yes and
opens it; "&fresh" starts a new device.

The build needs Emscripten: `emcmake` (the Emscripten SDK), or Docker with the image
emscripten/emsdk (on Windows also the Docker of WSL). A build takes about 1 minute; the next
builds are faster.
"""

import base64
import os
import pathlib
import shutil
import subprocess
import sys
import webbrowser

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DIR = os.path.join(ROOT, "build-web")
PAGE = os.path.join(BUILD_DIR, "simulator.html")
EMSDK_IMAGE = "emscripten/emsdk:4.0.10"   # the same as the CI job "Web simulator build"
SOURCES = ("sim", "main", "components", "version.txt", os.path.join("updater", "apps"))   # the apps of the panel


class WebSimError(Exception):
    pass


def _newest_source():
    newest = 0.0
    for name in SOURCES:
        path = os.path.join(ROOT, name)
        if os.path.isfile(path):
            newest = max(newest, os.path.getmtime(path))
            continue
        for folder, _, files in os.walk(path):
            for file in files:
                newest = max(newest, os.path.getmtime(os.path.join(folder, file)))
    return newest


def is_up_to_date():
    return os.path.isfile(PAGE) and os.path.getmtime(PAGE) >= _newest_source()


def _wsl_path(path):
    """C:\\Users\\x -> /mnt/c/Users/x"""
    drive, rest = os.path.splitdrive(os.path.abspath(path))
    return "/mnt/%s%s" % (drive[0].lower(), rest.replace("\\", "/"))


def _builder():
    """(name, function that gives the command for a shell line, source folder the build sees)"""
    if shutil.which("emcmake"):
        return "Emscripten", lambda line: ["bash", "-c", line] if os.name != "nt" else ["cmd", "/c", line], ROOT
    if shutil.which("docker"):
        return "Docker", lambda line: ["docker", "run", "--rm", "-v", ROOT + ":/src", "-w", "/src", EMSDK_IMAGE,
                                       "bash", "-c", line], "/src"
    if os.name == "nt" and shutil.which("wsl"):
        mount = _wsl_path(ROOT)
        return "Docker in WSL", lambda line: ["wsl", "--", "docker", "run", "--rm", "-v", mount + ":/src", "-w", "/src",
                                              EMSDK_IMAGE, "bash", "-c", line], "/src"
    return None


def _cache_source():
    """The source folder of an earlier configure of build-web (None: no earlier configure)"""
    cache = os.path.join(BUILD_DIR, "CMakeCache.txt")
    if not os.path.isfile(cache):
        return None
    with open(cache, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if line.startswith("CMAKE_HOME_DIRECTORY:"):
                return line.split("=", 1)[1].strip()
    return None


def build(log=print):
    found = _builder()
    if found is None:
        raise WebSimError(
            "The web simulator is not built (build-web/simulator.html), and this computer has no Emscripten\n"
            "and no Docker to build it. Install Docker (or the Emscripten SDK), or get the file\n"
            "\"stattrak-web-simulator\" of the newest CI run (GitHub, Actions, job \"Web simulator build\")\n"
            "and put it at build-web/simulator.html.")
    name, command, source = found
    # A build folder of an other builder (other paths) cannot be used again
    def norm(path):
        return path.replace("\\", "/").rstrip("/").lower()

    earlier = _cache_source()
    if earlier is not None and norm(earlier) != norm(source + "/sim"):
        shutil.rmtree(BUILD_DIR, ignore_errors=True)
    os.makedirs(BUILD_DIR, exist_ok=True)
    line = ("emcmake cmake -S sim -B build-web -DSINGLE_FILE=1 -DSTATTRAK_TESTS=OFF > build-web/build.log 2>&1"
            " && cmake --build build-web >> build-web/build.log 2>&1")
    log("Building the web simulator with %s (about 1 minute; the log is build-web/build.log)..." % name)
    result = subprocess.run(command(line), cwd=ROOT)
    if result.returncode != 0 or not os.path.isfile(PAGE):
        raise WebSimError("The build of the web simulator failed. See the lines above and build-web/build.log.")
    os.utime(PAGE, None)   # newer than the sources now, also when the build changed nothing
    log("Built %s" % PAGE)


def open_with_app(package, name, fresh=False, browser=True, rebuild=False, log=print):
    """Open the simulator page with the app installed and running. Return the launcher file path."""
    if rebuild or not is_up_to_date():
        if os.path.isfile(PAGE) and _builder() is None and not rebuild:
            # Still good for the app: the app goes into the address, not into the page
            log("Note: build-web/simulator.html is older than the firmware code, and this computer cannot build it.")
        else:
            build(log)
    data = base64.urlsafe_b64encode(bytes(package)).decode("ascii").rstrip("=")
    address = "simulator.html#app=" + data + ("&fresh" if fresh else "")
    # A small page that goes to the address: a file:// address with a long "#..." part does not
    # go through all programs that open a browser (Windows drops it)
    safe = "".join(c if c.isalnum() else "-" for c in name.lower()).strip("-") or "app"
    launcher = os.path.join(BUILD_DIR, "play-%s.html" % safe)
    with open(launcher, "w", encoding="utf-8") as handle:
        handle.write('<!doctype html><meta charset="utf-8"><title>%s</title>\n'
                     '<script>location.replace(%r);</script>\n'
                     '<a href="%s">Open the simulator</a>\n' % (name, address, address))
    log("Simulator with %s: %s" % (name, launcher))
    if browser:
        webbrowser.open(pathlib.Path(launcher).resolve().as_uri())
    return launcher


if __name__ == "__main__":
    try:
        build()
    except WebSimError as error:
        print(error, file=sys.stderr)
        sys.exit(1)
