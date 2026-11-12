# TRIDOS x JZ - StatTrak Simulator

The simulator runs the **real firmware code** on a computer. The files `main/main.c`,
`main/app.c`, all views, the storage code and the button logic are compiled without changes.
Only the hardware and the ESP-IDF SDK are replaced, by the files in `sim/host/`.

Use it to test a change before a device is at risk.

| Program | For | Start |
| --- | --- | --- |
| Headless (no window) | Tests, scripts, AI coding tools | `python tools/sim.py ...` |
| Window (SDL2) | To try the firmware by hand | `build-sim/simulator` |
| Web page (Emscripten) | To show the firmware in a browser | `simulator.html` |

## What the simulator can and cannot show

| It runs the real code of | It replaces (simulates) |
| --- | --- |
| Start sequence and main loop (`main/main.c`) | Time: it moves only when the firmware calls `vTaskDelay()` |
| Views, settings, counters, dim, auto-off, battery warnings | Button levels, display, buzzer, battery voltage |
| Storage logic (`main/eeprom.c`) | Flash: a key-value table, saved in a text file |
| Button gestures (`main/board/bsp_button.c`) | The button driver component (same timing rules) |
| | Bluetooth and the firmware update (`sim/host/host_ota.c`) |

**The simulator cannot show that the Bluetooth firmware update works in your firmware.**
It also cannot find: a stack overflow, timing problems of the real hardware, and errors in
the drivers (`main/board/`, `components/ssd1307`, `components/app_ble`, `components/ble_ota/ota.c`).

## Headless simulator

Build it with `python tools/check.py --build-only` (folder `build-host/`). No C compiler or
CMake on the computer? `python tools/setup.py simulator` installs them into the folder `.venv`
of this repository (also on Windows). Then:

```sh
python tools/sim.py boot click1 click1 frame          # 2 kills, print the display
python tools/sim.py boot both click1 both png:settings  # menu, SETTINGS, open: out/settings.png
python tools/sim.py boot:10600 state                  # hold both buttons 10 s: update mode
python tools/sim.py --fresh boot set:auto_off=1       # a new device
python tools/sim.py --help                            # all actions
```

`frame` prints the display as text. Example (`00002`):

```
..........###.###.###.............###.###.###.....   (cut)
......###.............###.....###.............###.
```

The flash content stays in `out/device_state.txt` between runs:

```
nvs eeprom total_kills 2
ota pending 0
```

You can edit this file to test a value from an other firmware version.

### In a test

```python
from stattrak_sim import Device

with Device() as dev:
    dev.boot()                      # hold both buttons, release, wait for the animation
    dev.click(1)                    # short press on button 1
    dev.long_press(2)               # long press on button 2
    dev.click_both()                # app menu (or: open the selected app)
    dev.run(30000)                  # 30 simulated seconds (this takes milliseconds)
    print(dev.state()["view"])      # TOTAL, KILL_DEAD, STATS, SETTINGS, UPDATE, ...
    print(dev.state()["eeprom.total_kills"])
    dev.save_png("screen.png")
    dev.power_cut()                 # pull the battery
    dev.write_flash({"auto_off": 1})
    dev.boot()
```

See `tests/stattrak_sim.py` for all functions and `tests/test_*.py` for examples.

### The program itself

`build-host/stattrak_headless` reads commands from standard input: `btn`, `run`, `voltage`,
`ota`, `nvs`, `state`, `frame`, `quit`. One run is one power session. The top of
`sim/headless/main.c` has the full list.

## Window simulator (SDL2)

Needs SDL2: `sudo apt install libsdl2-dev` (Ubuntu) or `brew install sdl2` (macOS).

```sh
cmake -S sim -B build-sim -GNinja
cmake --build build-sim
./build-sim/simulator
```

| Key | Function |
| --- | --- |
| LEFT / RIGHT | Button 1 / button 2 |
| LEFT + RIGHT | Short: app menu, or open the selected app. Long: power off. After a power-off: power on |
| UP / DOWN | Battery voltage + / - 100 mV |
| U | A phone sends new firmware (only in the update view) |
| A | A phone accepts the new firmware (only in the "Verifying..." view) |
| ESC | Quit |

The device powers on by itself when the program starts. Counters and settings stay in
`stattrak_sim_state.txt`. Delete this file to get a new device.

Apps: `./build-sim/simulator --app apps/runner/runner.stapp` installs an app after the start, like
the web updater: the simulator opens "Apps: SYNC" and sends the app with the Bluetooth app
commands. The device asks "INSTALL RUNNER?": hold RIGHT (button 2) = yes, hold LEFT = no. Then
hold LEFT to leave "Apps: SYNC" and press LEFT + RIGHT for the app menu.

## Web simulator (Emscripten)

```sh
# Source Emscripten SDK (replace $EMSCRIPTEN_PATH)
source "$EMSCRIPTEN_PATH/emsdk/emsdk_env.sh"

emcmake cmake -S sim -B build-web                   # simulator.html + .js + .wasm
# or
emcmake cmake -S sim -B build-web -DSINGLE_FILE=1   # one html file, opens from disk

cmake --build build-web

python3 -m http.server -d build-web                 # then open localhost:8000/simulator.html
```

With Docker, without an Emscripten installation:

```sh
docker run --rm -v "$PWD":/src -w /src emscripten/emsdk:4.0.10 \
  bash -c "emcmake cmake -S sim -B build-web -DSINGLE_FILE=1 && cmake --build build-web"
```

The keys are the same as in the window simulator. The page keeps counters and settings in
the browser storage, and plays the buzzer with the browser sound (after the first key press).

**Apps:** the panel "Apps" on the right installs an app, like the web updater: click one of the
apps of the catalog (`updater/apps/`, put into the page by the build), or pick a `.stapp` file.
The device asks "INSTALL <NAME>?": hold RIGHT (button 2) = yes. Then hold LEFT to leave "Apps:
SYNC" and press LEFT + RIGHT for the app menu. Run `python tools/app.py catalog` before the build
to have the newest apps in the page.

## Build options

| CMake option | Default | Meaning |
| --- | --- | --- |
| `STATTRAK_SANITIZE` | OFF | Build with AddressSanitizer and UndefinedBehaviorSanitizer (finds memory errors) |
| `STATTRAK_32BIT` | OFF | 32-bit code like the ESP32-C2. Linux: needs `gcc-multilib` |
| `STATTRAK_SDL` | ON | Build the window simulator if SDL2 is installed |
| `STATTRAK_TESTS` | ON | Build the unit test programs |

`python tools/check.py` uses the memory checker and the 32-bit build when the computer has them.

## How it works

```
sim/host/include/      stand-ins for the ESP-IDF headers (FreeRTOS, NVS, log, button driver)
sim/host/host_core.c   simulated time, session end (power off, restart, abort), state file
sim/host/host_board.c  power latch, display buffer with guard bytes, buzzer, battery voltage
sim/host/host_button.c the state machine of the espressif/button component
sim/host/host_nvs.c    flash key-value table, with "write fails" for tests
sim/host/host_ota.c    stand-in for the Bluetooth update: fake transfer, accept, rollback
sim/host/sim_apps.c    installs an app with the Bluetooth app commands (window, web page, "simapp")
sim/headless/main.c    front end: commands on standard input
sim/sdl/main.c         front end: window or web page
```

The button timing comes from `sdkconfig.defaults` (`CONFIG_BUTTON_*`). If you add a firmware
source file, add it to `FIRMWARE_SOURCES` in `sim/CMakeLists.txt` too.
