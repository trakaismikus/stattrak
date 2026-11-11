"""Find the programs that the StatTrak tools need. Used by setup.py, check.py and build.py.

Two groups of programs:

  Simulator and tests:  CMake, a C compiler, Ninja (optional).
      They can come from the computer (for example gcc on Linux) or from the folder .venv in
      this repository. "python tools/setup.py simulator" makes .venv: it installs the Python
      packages cmake, ninja and ziglang there. ziglang contains a full C compiler ("zig cc"),
      so a Windows computer needs nothing but Python.

  Firmware for the device:  ESP-IDF v5.4, the SDK of the chip maker (Espressif).
      "python tools/setup.py firmware" installs it. Docker with the image espressif/idf:v5.4
      also works.
"""

import json
import os
import shlex
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VENV_DIR = os.path.join(ROOT, ".venv")
LOCAL_DIR = os.path.join(ROOT, ".stattrak")   # Notes of this computer. Not in git.
SETTINGS_FILE = os.path.join(LOCAL_DIR, "tools.json")

IDF_VERSION = "v5.4"
IDF_TARGET = "esp32c2"
IDF_DOCKER_IMAGE = "espressif/idf:v5.4"

IS_WINDOWS = os.name == "nt"


def venv_bin(name):
    """Path of a program in .venv, or None."""
    folder = os.path.join(VENV_DIR, "Scripts" if IS_WINDOWS else "bin")
    for candidate in (name, name + ".exe"):
        path = os.path.join(folder, candidate)
        if os.path.isfile(path):
            return path
    return None


def venv_python():
    return venv_bin("python") or venv_bin("python3")


def find_program(name):
    """A program on the PATH of the computer, or in .venv."""
    return shutil.which(name) or venv_bin(name)


def find_zig():
    """The C compiler from the Python package ziglang in .venv."""
    python = venv_python()
    if python is None:
        return None
    try:
        output = subprocess.run([python, "-c", "import os, ziglang; print(os.path.dirname(ziglang.__file__))"],
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, universal_newlines=True,
                                timeout=60).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return None
    for name in ("zig.exe", "zig"):
        path = os.path.join(output, name) if output else ""
        if path and os.path.isfile(path):
            return path
    return None


def macos_developer_tools():
    """macOS always has /usr/bin/gcc, but it is only a pointer to the Command Line Tools of Apple.
    Without these tools it does not compile: it opens an install window. True when they are there."""
    try:
        result = subprocess.run(["xcode-select", "-p"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                universal_newlines=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return False
    return result.returncode == 0 and os.path.isdir(result.stdout.strip())


def find_compiler():
    """Return ("system", path) for gcc or clang of the computer, ("zig", path), or (None, None)."""
    system_ok = (sys.platform != "darwin") or macos_developer_tools()
    if system_ok:
        if os.environ.get("CC") and shutil.which(os.environ["CC"]):
            return "system", shutil.which(os.environ["CC"])
        for name in ("gcc", "clang", "cc"):
            path = shutil.which(name)
            if path:
                return "system", path
    zig = find_zig()
    if zig:
        return "zig", zig
    return None, None


def cmake_compiler_arguments():
    """CMake arguments that select the compiler and the build program."""
    arguments = []
    kind, path = find_compiler()
    if kind == "zig":
        arguments += ["-DCMAKE_C_COMPILER=%s" % path.replace("\\", "/"), "-DCMAKE_C_COMPILER_ARG1=cc"]
    ninja = find_program("ninja")
    if ninja:
        arguments += ["-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=%s" % ninja.replace("\\", "/")]
    return arguments


# --- Settings that setup.py stores (where ESP-IDF is) --------------------------------------------------

def load_settings():
    try:
        with open(SETTINGS_FILE, "r", encoding="utf-8") as handle:
            data = json.load(handle)
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def save_settings(settings):
    os.makedirs(LOCAL_DIR, exist_ok=True)
    with open(SETTINGS_FILE, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(settings, handle, indent=2, sort_keys=True)
        handle.write("\n")


# --- ESP-IDF ------------------------------------------------------------------------------------------

def default_idf_dir():
    """Where setup.py puts ESP-IDF. The path must not have spaces: ESP-IDF does not work with them."""
    if IS_WINDOWS:
        return r"C:\Espressif\frameworks\esp-idf-" + IDF_VERSION
    return os.path.join(os.path.expanduser("~"), "esp", "esp-idf-" + IDF_VERSION)


def idf_version_of(folder):
    """The version text of an ESP-IDF folder ("v5.4", "v5.4.1"...), or None."""
    version_file = os.path.join(folder, "version.txt")
    try:
        with open(version_file, "r", encoding="utf-8") as handle:
            return handle.read().strip()
    except OSError:
        pass
    cmake_file = os.path.join(folder, "tools", "cmake", "version.cmake")
    try:
        with open(cmake_file, "r", encoding="utf-8") as handle:
            text = handle.read()
    except OSError:
        return None
    parts = []
    for key in ("IDF_VERSION_MAJOR", "IDF_VERSION_MINOR", "IDF_VERSION_PATCH"):
        marker = "set(%s " % key
        if marker not in text:
            return None
        parts.append(text.split(marker, 1)[1].split(")", 1)[0].strip())
    version = "v%s.%s" % (parts[0], parts[1])
    return version if parts[2] == "0" else "%s.%s" % (version, parts[2])


def is_right_idf(version):
    return bool(version) and (version == IDF_VERSION or version.startswith(IDF_VERSION + "."))


def find_idf():
    """Return (folder, version) of an ESP-IDF v5.4 that is ready to use, or (None, None).

    The folder that setup.py makes (default_idf_dir) is not in this list: setup.py stores it in
    the settings only after the install script of ESP-IDF passed. A download without its tools
    is not a usable ESP-IDF.
    """
    candidates = [load_settings().get("idf_path", ""), os.environ.get("IDF_PATH", ""),
                  os.path.join(os.path.expanduser("~"), "esp", "esp-idf")]
    for folder in candidates:
        if folder and os.path.isfile(os.path.join(folder, "tools", "idf.py")):
            version = idf_version_of(folder)
            if is_right_idf(version):
                return folder, version
    return None, None


def docker_works():
    docker = shutil.which("docker")
    if not docker:
        return False
    try:
        return subprocess.run([docker, "info"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                              timeout=30).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def cmd_environment():
    """The environment for a Windows .bat file of ESP-IDF.

    The .bat files of ESP-IDF stop at once, without an error code, when the variable MSYSTEM is
    set. Git Bash sets it, and many AI coding tools run their commands in Git Bash.
    """
    return {key: value for key, value in os.environ.items() if not key.upper().startswith("MSYS")}


def run_idf_script(idf_dir, name, arguments, cwd=None, quiet=False):
    """Run install or export + idf.py of the ESP-IDF in `idf_dir`. Return the exit code.

    name "install": the install script with `arguments`.
    name "idf.py":  load the ESP-IDF environment, then run idf.py with `arguments`.
    """
    output = subprocess.DEVNULL if quiet else None
    if IS_WINDOWS:
        quoted = " ".join('"%s"' % argument for argument in arguments)
        if name == "install":
            line = 'cmd /d /s /c "call "%s" %s"' % (os.path.join(idf_dir, "install.bat"), quoted)
        else:
            line = 'cmd /d /s /c "call "%s" >nul && python "%s" %s"' % (
                os.path.join(idf_dir, "export.bat"), os.path.join(idf_dir, "tools", "idf.py"), quoted)
        if not quiet:
            print("  > " + line, flush=True)
        return subprocess.run(line, cwd=cwd, env=cmd_environment(), stdout=output, stderr=output).returncode

    quoted = " ".join(shlex.quote(argument) for argument in arguments)
    if name == "install":
        command = ["bash", os.path.join(idf_dir, "install.sh")] + list(arguments)
    else:
        command = ["bash", "-c", ". %s >/dev/null && idf.py %s" % (shlex.quote(os.path.join(idf_dir, "export.sh")), quoted)]
    if not quiet:
        print("  > " + " ".join(command), flush=True)
    return subprocess.run(command, cwd=cwd, stdout=output, stderr=output).returncode


def idf_works(idf_dir):
    """True when idf.py of this ESP-IDF starts: its compiler and its Python packages are installed."""
    try:
        return run_idf_script(idf_dir, "idf.py", ["--version"], cwd=idf_dir, quiet=True) == 0
    except (OSError, subprocess.SubprocessError):
        return False


def python_ok():
    return sys.version_info >= (3, 8)
