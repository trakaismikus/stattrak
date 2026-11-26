# The StatTrak firmware code: where things are and how to change them

This is the second part of the firmware guide (`SKILL.md` in this folder). Read it before you
change firmware code. The safety rules are in `AGENTS.md` section 3: they come first.

## 1. Where things are

```
main/main.c              start sequence + main loop. HIGH RISK, see AGENTS.md section 3
main/app.c, app.h        settings logic, dim, auto-off, battery warnings, lists of values
main/app_view.c, .h      list of views (VIEW_LIST) and the change between views
main/views/*.c           one file for each view
main/ble_commands.c, .h  Bluetooth commands for user features, and the app manager commands
main/apps.c, .h          the app list (built-in + installed apps) and their storage in flash (docs/APPS.md)
main/app_data.c, .h      the data store of each app: log() rows (docs/DATA.md)
main/backup.c, .h        restore of a backup from the sync page (docs/DATA.md)
main/device_log.c, .h    one entry for each start: why, battery (docs/DATA.md)
main/start_pic.c, .h     the start picture of the person (docs/DATA.md); main/views/intro_view.c shows it
main/views/app_*_view.c  app host (runs an installed app), app settings, app sync ("Apps: SYNC")
main/eeprom.c            values in flash (NVS), with a copy in RAM
main/battery.c           battery level and critical level for each battery type
main/board.c, board/     hardware drivers (pins, buttons, display, ADC, buzzer). HIGH RISK
main/gfx_fx.c            power-on / power-off animation
main/resources/          fonts and pictures
components/gfx/          drawing functions (Gfx_*)
components/gui/          animation engine
components/ssd1307/      display driver
components/app_ble/      Bluetooth start. HIGH RISK
components/ble_ota/      firmware update over Bluetooth. HIGH RISK
components/stapp/        the app engine: checks an app package and runs it with limits (docs/APPS.md)
partitions.csv           flash layout. NEVER CHANGE
sdkconfig.defaults       SDK settings. HIGH RISK
designs/                 screen design files (docs/DESIGNS.md)
sim/                     simulator (host/ = stand-ins for hardware, headless/, sdl/, web/, tools/)
tests/                   tests (Python: full firmware in the simulator, unit/: C unit tests)
tools/                   setup.py, check.py, check_safety.py, sim.py, design.py, build.py, app.py
skills/                  the guides for AI tools (AGENTS.md section 5)
updater/                 the web site (stattrak.tridos.design, updater/README.md): index.html = Firmware
                         and My device, start/ = Get started, designer/ = the Screen Designer
docs/                    SAFETY.md, BLUETOOTH.md, DESIGNS.md, APPS.md, APP_STORE.md (the app store), DATA.md,
                         GUIDE.md (the user guide), AUDIT.md, ROADMAP.md
CONTRIBUTING.md          how to share a feature with all users
```

### How the app works

- The main loop runs each 10 ms: read button events, `App_update()`, clear the frame, call the
  `draw` function of the current view, send the frame to the display.
- Time: `Time_getMillis()` (10 ms steps). Never wait in a loop. Compare times.
- No `malloc`. Use static variables.
- The stack of the main loop is small (3.5 KB). No large local arrays (keep them below
  about 100 bytes) and no recursion. The simulator cannot find a stack overflow.

### Views

A view is a `view_t` (`components/gui/view.h`) with 4 optional functions:
`init`, `draw` (each 10 ms, the frame is empty before), `eventHandler`, `deinit`.

Button events (`event_t` in `main/app.h`):

| Event | When |
| --- | --- |
| `EVENT__PRESS_1`, `EVENT__PRESS_2` | Short press. It comes 180 ms after the release. |
| `EVENT__LONG_PRESS_1`, `_2` | The button is held for 700 ms. |
| `EVENT__LONG_HOLD_1`, `_2` | Each 500 ms after that, while the button is held. |
| `EVENT__RAW_PRESS_x`, `EVENT__RAW_RELEASE_x` | Immediately. Use them only to show a pressed button. |
| `EVENT__PRESS_BOTH`, `EVENT__LONG_PRESS_B` | Handled in `main/main.c`: app menu / power off. Not for views. |

The app menu (`main/views/launcher_view.c`): both buttons in an app open it. Button 1 = previous
app, button 2 = next app (it wraps around). Both buttons, or 3 seconds without a press, open the
selected app. Hold button 2: the settings of the selected installed app. Each title picture is
80 x 24 pixels (icons in `main/resources/app_icons.c`, installed apps have it in the package).

The menu is built each time it opens (`_buildMenu()`): the apps of `main/apps.c` (the built-in
apps KILLS, KILLS + DEAD, STATS and the installed apps, in the saved order, without the hidden
ones), then SETTINGS, always, as the last app. Installed apps come over Bluetooth (Settings >
"Apps: SYNC"); `docs/APPS.md` describes the package, the engine and the commands.

To add a view:

1. Make `main/views/my_view.c` with `view_t g_myView = { ... };`. Copy a small view, for
   example `stats_view.c`. The draw code can come from a design file (the screens guide,
   `skills/stattrak-screens/SKILL.md`).
2. Add `VIEW(MY, my)` to `VIEW_LIST` in `main/app_view.h`, after `LAUNCHER`. To show it in the
   app menu as a built-in app: add it to `kBuiltin` in `main/apps.c` with a new id (4 to 255),
   make `APPS_BUILTIN_COUNT` larger, and draw its title picture (80 x 24) in `_drawCover()` of
   `main/views/launcher_view.c`. `SETTINGS` stays the last app; do not add it to a list.
3. Add the file to `main/CMakeLists.txt` (`SRC_FILES`) **and** to `sim/CMakeLists.txt`
   (`FIRMWARE_SOURCES`).
4. Update `MAIN_VIEWS` in `tests/helpers.py` if the view is in the app menu. Add a test.

An app that users install (and share without a firmware update) is often the better choice
than a new built-in view: `docs/APPS.md`, `apps/`, `python tools/app.py`. Since firmware
1.2.0-b4 (app engine 4) an app has lists, `held()` (a button is down now), 200 variables and
16000 steps for each event, so most built-in screens can also be an app. `docs/APPS.md`
section 7.6 shows how to make an app from a C view.

### Drawing

- `Gfx_*` functions in `components/gfx/gfx.h`. Origin top left. x: 0 to 127, y: 0 to 31.
  Drawing outside the display is safe (it is cut off).
- Fonts: `font5x7` (ASCII), `font5x5` and `font3x5` (digits, upper-case letters, some signs;
  lower-case letters are shown as upper-case) in `main/resources/font.h`; `kFontNightverbBig`
  (title font) and `kFontNightverbLargeNumbers` (digits and `-` only, 14 pixels high) in
  `main/resources/font_glyph.h`. No UTF-8.
- `Gfx_drawRect(x, y, w, h)` draws an outline that is 1 pixel smaller than `w` x `h`. Use
  `Gfx_drawRoundRect(x, y, w, h, 0, 1)` for an exact outline. `tools/design.py code` does this.
- The display has 32 rows. Text with `font5x5` needs 6 rows for each line.

## 2. Settings and values in flash

`ENTRY_LIST` in `main/app.h` is the list of values in flash:
`ENTRY(id, key, default, max)`. `SETTING_LIST` is the list of rows in the settings view.

Rules for values in flash:

- Add new entries **at the end** of `ENTRY_LIST`. The key has 15 characters at most.
  There can be 32 entries at most.
- Never rename or reuse a key. Never change what an old value means. Never add a value in
  the middle of a list of setting values: add it at the end. (A stored setting is the position
  in its list. A new value in the middle moves all values after it.)
- To show the values of a setting in an other sequence, do not move the texts. Change only the
  sequence of the steps on the display. Settings > Battery does this: `kBatteryOrder` in
  `main/views/settings_view.c` (the types with a battery first, `USB/DC` last).
- A device that gets the firmware by an update does not have the new entry in flash. The
  firmware writes the **default** value for it at the first start.
- `max` is the largest valid value. A larger value in flash is replaced with the default at
  start. Do not use a flash value as an array index without this limit.
- Do not write to flash often (flash wears out). Never write in `draw` or each loop.
- The battery type in use is `App_getBatteryTypeInUse()`, not the flash value `battery_type`.
  The flash value `device_model` (not set, DIY, PEQ) decides it: `docs/SAFETY.md`, "The device
  version and the battery type". A new device shows the question "StatTrak version" first, and a
  DIY device without a battery type shows the warning "Battery type not set" at each power-on.

To add a setting `MY_SETTING` with the values OFF / ON:

1. `main/app.h`, end of `ENTRY_LIST`: `ENTRY(MY_SETTING, my_setting, 0, 1)`
2. `main/app.h`, `SETTING_LIST`: add
   `SETTING(MY_SETTING, MySetting, "My Setting", 1, false, SETTING_STR_VALUES("OFF", "ON"))`
   after `SOUND`. The id must be the same as the `ENTRY` id. Do not put a new row between
   `AUTO_OFF` and `BRIGHTNESS` (the code that hides the battery rows needs this order).
3. `main/app.c`, `App_updateSetting()`: add
   `case SETTING_ID__MY_SETTING: Eeprom_write(ENTRY_ID__MY_SETTING, value); break;`
4. The compiler checks that the count of texts is `max + 1`.
5. Tests: add the name to `SETTINGS_WITH_BATTERY` in `tests/helpers.py` and the key to
   `EEPROM_KEYS` in `tests/test_robustness.py`. Add a test for the new behaviour.

## 3. Bluetooth for user features

Bluetooth is open for features: a phone or a computer can send commands to the device and
get answers. Read `docs/BLUETOOTH.md` before you use it. The short version:

- Add commands in `main/ble_commands.c` only. The command numbers start at `0x0100`.
  The code runs in the main loop, like a view. Do not change `components/ble_ota/` for a feature.
- A command has 16 data bytes at most. An answer has a status byte and 13 data bytes at most.
- Bluetooth is on only in the update view and the accept view. A view of your own can switch
  it on with `Ota_init()` in its `init` and `Ota_deinit()` in its `deinit`. Call
  `Ota_setUpdateAllowed(false)` after `Ota_init()` there, so nobody near the user can send
  a firmware while the user plays. Bluetooth uses battery power: keep it off when the feature
  does not need it.
- Test each command in the simulator: `tests/test_bluetooth.py` and `python tools/sim.py ... ble:0100`.
- The app manager of the web updater uses the commands `0x0110` to `0x011C` (`docs/APPS.md`,
  section 6). They work only in the view Settings > "Apps: SYNC". Do not use these numbers.
- The backup, the app data, the device log, the start picture and the live preview of the
  Screen Designer use the commands `0x0120` to `0x012A` (`docs/DATA.md`). Also only in
  "Apps: SYNC". Do not use these numbers.
- The other side (a web page or an app) uses the Bluetooth service `0x8018`, characteristic
  `0x8023`. `docs/BLUETOOTH.md` has the packet format and a JavaScript example.

## 4. Tests

```sh
python tools/check.py          # all checks
python tools/check.py --fast   # faster, while you work (run the full check at the end)
```

- `tests/test_*.py`: the full firmware in the simulator. Write a test like this:

  ```python
  from helpers import SimTestCase

  class MyTest(SimTestCase):
      def test_three_kills(self):
          self.dev.boot()
          for _ in range(3):
              self.dev.click(1)
          self.assertEqual(self.dev.state()["eeprom.total_kills"], 3)
  ```

  `tests/stattrak_sim.py` has all functions of `self.dev` (click, long_press, run, state,
  frame, write_flash, ota_transfer, ble_command, ...).
- `tests/unit/*.c`: unit tests of single modules.
- `self.dev.boot()` answers the two screens of the start for you (DIY to "StatTrak version",
  LATER to "Battery type not set"), so a test starts in the first app. `boot(prompts="keep")`
  leaves them on the display, `boot(prompts="peq")` answers PEQ. In `tools/sim.py`: `boot`,
  `boot:ask` and `boot:peq`. `tests/test_device_model.py` has the tests of these screens.
- `tests/test_robustness.py` starts the firmware with bad flash values and presses buttons at
  random. After each case the update mode must still open. Keep these tests.
- `tests/test_update.py` and `tests/test_boot.py` protect the update mode and the start
  sequence. If one of them fails, your change is dangerous. Do not change these tests to
  make them pass without a very good reason, and tell the user.
- `tests/unit/test_ble_ota.c` sends update data exactly as the web updater does. If it fails,
  the web updater cannot update a device with this firmware.
- `updater/tests/`: the tests of the web updater. `python tools/check.py` does not run them.
  After a change in `updater/`, run `node --test "tests/*.test.js"` in the folder `updater`
  (Node.js 22 or later, and Chrome for the page tests). They use a stand-in device
  (`updater/tests/fake-device.js`), which is a port of `components/ble_ota/`. The two sides
  must agree: do not change one without the other, and see `AGENTS.md` section 3 before you change
  `components/ble_ota/` at all.
- `tests/test_design.py` compares the design tools with the firmware draw functions.
- `tests/test_designer.py` compares the Screen Designer with `tools/design.py` (needs Node.js;
  it is skipped without it).

On Windows with the tools from `.venv`, the build has no memory checker. The memory checker
runs on Linux and macOS with gcc or clang, and in the GitHub checks of a pull request.

## 5. Code style

- 2 spaces, no tabs. Same style as the file that you change.
- Names: `Module_function()`, `s_localVariable`, `kConstant`, `g_globalVariable`,
  `ENUM__VALUE`, private functions start with `_`.
- Lists are X-macros (`VIEW_LIST`, `ENTRY_LIST`, `SETTING_LIST`). Add to the list, do not
  copy the list.
- Check array indexes that come from flash, from Bluetooth or from a calculation.
- `snprintf` with `sizeof(buffer)`. No `sprintf`, no `strcpy`.
- A firmware file that is also in the simulator must compile without ESP-IDF headers.
  Hardware code goes in `main/board/` or `components/`.
