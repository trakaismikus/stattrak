# AGENTS.md: instructions for AI coding tools

This file is for AI coding tools (Claude Code, Codex, Cursor, Copilot, Gemini and others).
Read all of it before you change this repository. The person who works with you has probably
not read it, and may not be a programmer. You are the guide.

**The way of a person through this file:**

| Step | What you do | Section |
| --- | --- | --- |
| 1 | Learn who the person is, check the computer, show the device | 1 |
| 2 | Get the idea. No idea yet: help to find one | 16.1 |
| 3 | Decide: no code, an app, or a firmware change. Explain what it means | 16 |
| 4 | Build it: an app | 17 |
| 4 | Build it: a firmware change | 4, 6, 7 |
| 5 | Share it: an app in the app store | 18 |
| 5 | Share it: a firmware feature or a fix as a pull request | 13 |

The safety rules of section 3 are for each step.

## 1. Start here (each new conversation)

Do these three steps before any other work.

### Step 1: learn who the person is

Look for the file `.stattrak/profile.md` (it is not in git). If it is there, read it and go
to step 2.

If it is not there, ask these questions in **one** short message, with the answer options.
Do not ask more than these three.

1. **Have you written code before?** (a) Never. (b) A little. (c) Yes, I am a developer.
2. **Have you built or changed electronics before, for example with Arduino or ESP32?**
   (a) No. (b) A little. (c) Yes.
3. **What do you want to do today?** (a) Look around and try the simulator.
   (b) I have an idea for something new. (c) I want to change something that my StatTrak
   does now. (d) I have no idea yet. Show me what is possible.

If the first message of the person has an idea already, do not ask question 3.

**Never ask the person "do you want an app or a firmware change?".** Most people cannot know
the difference. You decide with section 16, and you tell the person what your decision means
for them.

Then write `.stattrak/profile.md` with the answers, the idea (if there is one) and the level
that you selected:

| Level | Who | How you talk and work |
| --- | --- | --- |
| **Beginner** | Answer 1 is (a), or the person says they are not technical | Plain, short sentences. No jargon: say "the firmware file", not "the binary"; "a saved copy", not "a commit". You do all the work: never ask the person to type a command or to edit a file. One question at a time, with options. After each change, show a picture of the display. Before each step, say in one sentence what it is for. |
| **Maker** | Some code or some electronics | Short explanations. Name the files that you change. The person can run commands if they want to. |
| **Developer** | Answer 1 is (c) | Technical and brief. No tutorials. Show diffs and test output. |

If you are not sure, use Beginner. The person can ask you to change the level at any time
("talk to me like a developer"). The safety rules in section 3 are the same for all levels.

### Step 2: check the computer

Run `python tools/setup.py` (on some computers the command is `python3`). It only looks. It
changes nothing. It prints what the computer has and what is missing.

If something is missing:

1. Tell the person **what** will be installed, **where** and **how large** it is. The output of
   `tools/setup.py` has these facts. For a Beginner, add one sentence about what the thing is for.
2. Wait for a "yes".
3. Run the install command: `python tools/setup.py simulator`.

Install only the simulator part now. It is small (about 550 MB) and stays in the folder
`.venv` of this repository. The firmware build part (`python tools/setup.py firmware`, about
4 GB) is necessary only when the person wants to put a firmware on a real device. Ask at that
time.

An app needs less: the app tool (`python tools/app.py`) needs only Python. If the person does
not want to install the simulator part, you can still make an app and show pictures of it
(section 17). Then skip step 3 and show the pictures of an example app:
`python tools/app.py run apps/slots/slots.sts wait:1000 png:out/slots.png`.

If Python is missing, the person must install it first: https://www.python.org/downloads/
(on Windows: tick "Add python.exe to PATH" in the installer). If Git is missing:
https://git-scm.com/downloads

### Step 3: show the device

Run `python tools/sim.py --tour` and show the pictures from `out/` to the person. This is the
real firmware, running on the computer. Tell the person in two sentences what they see.

Then go on with the answer to question 3:

| Answer | What you do next |
| --- | --- |
| (a) Look around | Offer the simulator in the browser, so the person can press the buttons: `sim/README.md`, or the page https://stattrak.tridos.design/sim/simulator.html . Show the app store (https://stattrak.tridos.design/#store): these are things that other owners made. Then ask for an idea. |
| (b) An idea, or (c) a change | Section 16: decide what the idea is (no code, an app, a firmware change) and tell the person. Then section 17 (an app) or section 4 (a firmware change). |
| (d) No idea yet | Section 16.1: help the person to find an idea. |

## 2. What this repository is

Firmware for the **StatTrak** kill counter by TRIDOS.DESIGN x JZ.

- Chip: ESP32-C2 (RISC-V, one core, 272 KB RAM, 4 MB flash, 26 MHz crystal)
- Display: 128 x 32 pixels, each pixel is on (white) or off
- Input: 2 buttons. Output: 1 buzzer. Battery voltage measurement.
- Bluetooth LE: for the firmware update, and for user features (section 9). It is off in
  normal operation.
- Language: C (GNU C11). SDK: ESP-IDF **v5.4**.

## 3. The most important rule: do not brick the device

"Bricked" means: the device cannot start, or it cannot get new firmware, and the user cannot
repair it. Users install firmware **over Bluetooth**. Think of Bluetooth as the only way to
repair a device.

How the device protects itself (do not weaken any part of this):

1. A new firmware is written to a spare slot. The old firmware stays in flash.
2. The device restarts into the new firmware in the state **"pending"** and shows the
   **accept view** ("Verifying...").
3. The web updater connects again over Bluetooth and sends **"accept"**. This shows that
   Bluetooth works in the new firmware. Only then the firmware is marked as good.
4. If the new firmware crashes, hangs or is switched off before the accept, the bootloader
   **goes back to the old firmware** at the next start.
5. Rescue: hold **both buttons for 10 seconds at power-on** to open the update mode.

A firmware that was accepted cannot go back. So a firmware that (a) was accepted and
(b) cannot open the update mode after that, is a bricked device. All rules below protect
the update mode and the start sequence.

### Never do these things

| Never | Why |
| --- | --- |
| Change `partitions.csv` | The table is in the flash of each device. An update over Bluetooth cannot change it. |
| Change the chip, crystal, flash size, partition or rollback settings in `sdkconfig.defaults` | Wrong crystal = no Bluetooth. No rollback = no way back from a bad firmware. |
| Switch on secure boot, flash encryption or anti-rollback. Call any `esp_efuse_*` write function | They burn eFuses. That is permanent. |
| Call `esp_ota_mark_app_valid_cancel_rollback()` or `Ota_acceptPending()` in new code | Only the Bluetooth "accept" command can accept a firmware. A firmware must not accept itself. |
| Use `esp_ota_*`, `esp_flash_*`, `esp_partition_*` write or erase functions outside `components/ble_ota/ota.c` | They can erase the running firmware, the bootloader or the partition table. |
| Change the pin numbers in `main/board.h` | `PWR_ON_PIN` keeps the power on. A wrong pin can also damage hardware. |
| Change the start sequence in `main/main.c`: `Board_powerOn()` first, pending firmware opens the accept view, 10 seconds hold opens the update view | These are the repair paths. |
| Remove `Ota_init()` from `update_view.c` or `fw_accept_view.c`, or remove the setting "Firmware: UPDATE" | Without Bluetooth in these views the web updater cannot send or accept firmware. |
| Call `Ota_setUpdateAllowed(false)` in the update view, the accept view, `main.c` or `app.c` | The device then refuses each new firmware. |
| Change the Bluetooth name `StatTrak`, the service numbers `0x8018`, `0x8020` to `0x8023`, or the update commands and packets | The web updater finds the device and talks to it with them. |
| Switch the device off for a low battery while firmware is received | A device that reads a wrong battery level could never get a new firmware. |
| Rename, reuse or change the meaning of a flash value in `ENTRY_LIST` (`main/app.h`) | Users lose their counters, or an other firmware version reads a wrong value. |
| Block the main loop: `while(1)` or a long delay without `vTaskDelay()`, slow code in a `draw` function | Buttons, the display and the update mode stop. The user cannot switch off. |
| Handle `EVENT__PRESS_BOTH` or `EVENT__LONG_PRESS_B` in a different way in normal views, or remove `SETTINGS` from the app menu (the last line of `_buildMenu()` in `main/views/launcher_view.c`) | "Both short" = app menu, and the app menu always ends with SETTINGS (the way to the update). "Both long" = power off. SETTINGS is added in code, never from the app list in flash: no app, no flash content and no Bluetooth command can remove it. |
| Let the app commands (`0x0110` to `0x011C` and `0x0120` to `0x012A` in `main/ble_commands.c`) work outside the view "Apps: SYNC" (`AppSync_isActive()`) | Only there the person sees the install question, and only there the device refuses new firmware while the phone talks to it. |
| Write to flash in a `draw` function, also in the app host (`main/views/app_host_view.c`): app errors and saved values are written later, in `AppHost_update()` | The draw function runs each 10 ms. Flash wears out, and a slow write stops the buttons and the display. |
| Tell the user to run `idf.py flash`, `idf.py erase-flash` or `esptool` | These need a wired connection and replace the bootloader. Only do it if the user asks for it and says that the device has a serial (UART) connection. |

`python tools/check_safety.py` finds most of these changes automatically.
`python tools/check_safety.py --explain` prints each rule with its reason.

### Limits of what you can know

- The simulator has **no Bluetooth radio and no real flash**. A pass in the simulator does not
  show that the Bluetooth update works on a device. Say this to the user each time.
- You cannot test on hardware. Do not say "it works on the device". Say what you tested.
- If a change touches `components/ble_ota/`, `components/app_ble/`, `main/main.c`,
  `main/board.c`, `main/board/`, `partitions.csv` or `sdkconfig.*`: stop and tell the user
  that this is a high-risk change, before you make it. Read `docs/SAFETY.md`.

## 4. The work sequence for a firmware change

First decide with section 16 what the idea is. Most ideas are an **app**: an app has its own
work sequence in section 17, and it needs no firmware build. This section is for a change of
the firmware itself.

Do these steps in this order. Do not skip a step. For a Beginner, you do all of them and
you show the result of each one.

1. **Understand the idea.** Say it back in two or three sentences. Ask two questions at most.
   Read the files that you will change, and `docs/SAFETY.md` if the change is near the update
   or start code.
2. **Design the screens first** (section 5), if the change shows something new on the display.
   Make 2 or 3 different proposals, show the pictures, let the person pick.
3. **Make a safe place for the work.** Create a git branch (`my/<short-name>`). For a Beginner:
   "I make a separate copy of the work, so the original stays as it is."
4. **Change the code.** Keep the change small. Sections 10 and 11 have the recipes.
5. **Add or change tests** in `tests/` for the new behaviour (section 12).
6. **Show it in the simulator.** Take pictures of each step of the feature:
   `python tools/sim.py boot <actions> png:step1 <actions> png:step2`. Show them in order and
   say which button press leads to which picture. Offer the simulator with a window or the web
   simulator (`sim/README.md`) if the person wants to press the buttons.
7. **Change it until the person is happy.** Go back to step 4.
8. **Run all checks:** `python tools/check.py`. All 4 steps must show `OK`.
   If a test fails, find the cause. Do not change a test only to make it pass.
9. **Save the work:** `git commit`. For a Beginner: "I saved a restore point."
10. **Build the firmware file** when the person wants it on a real device: section 6.
11. **Put it on the device:** section 7. Go through it with the person, step by step.
12. **Report:** what changed, what `tools/check.py` printed, the pictures, and the warning
    from "Limits of what you can know".
13. **Offer to share it.** If the feature or the fix is good for other owners: section 13.

## 5. Screen designs: show options before you build

A drawing of a screen is cheaper than code, and people decide faster with pictures.

**When:** the person describes a new screen, or a change to how a screen looks. Always for a
Beginner. For other levels: when they ask for options, or the look is not clear.

**How:**

1. Write 2 or 3 design files: `designs/<feature>-a.json`, `-b.json`, `-c.json`. Make them
   really different (layout, font size, what is in the center), not three copies.
   `docs/DESIGNS.md` has the file format. `designs/example.json` is an example.
2. Check and draw them:

   ```sh
   python tools/design.py check  designs/<feature>-a.json
   python tools/design.py render designs/<feature>-a.json     # pictures in out/
   ```

3. Show the pictures side by side. Say in one line each what is different. Ask which one the
   person likes, and what to change.
4. Change the picked design until it is right. Then get the draw code:
   `python tools/design.py code designs/<feature>-a.json`. It prints one C function for each
   screen with the exact `Gfx_*` calls. Put it into the view (section 10) and replace the
   fixed texts with the real values.
5. Keep the final design file in `designs/`. Delete the proposals that were not picked.

**For an app** the steps are the same, but the draw code is StatTrak Script and not C. Use the
positions and sizes that `python tools/design.py describe designs/<feature>-a.json` prints, and
write them as the drawing calls of `docs/APPS.md` section 4 (`text`, `num`, `bignum`, `rrect`,
`fill`, `bitmap`). Then compare the picture of the app (`python tools/app.py run ... png:...`)
with the picture of the design.

**The person is not happy with how a screen looks?** Do not guess a third and a fourth time.
Offer both ways: "I can draw two or three new proposals, or you can draw the screen yourself in
the Screen Designer. Which do you want?"

The pictures are exact: `tools/design.py` uses the fonts of the firmware, and a test compares
its drawing with the real firmware draw functions.

**The Screen Designer.** `updater/designer/index.html` is a page where a person draws screens
with the mouse: the real pixels, the real fonts, more than one screen, undo, save. It opens
with a double click, without a server and without the internet. It is also on the web site:
`https://stattrak.tridos.design/designer/index.html`. Use it in both directions:

- **From you to the person.** After you made design files, say: "You can change the designs
  yourself. Open the file `updater/designer/index.html` in your browser, press Open and select
  the file `designs/<feature>-a.json`." Open the page for the person if you can.
- **From the person to you.** The button "Send to AI" in the designer makes a text that
  starts with `StatTrak screen design`. When the person pastes it:
  1. Save the JSON block at its end as the file that the text names (`designs/<name>.json`).
     Copy it exactly.
  2. `python tools/design.py check designs/<name>.json`
  3. `python tools/design.py describe designs/<name>.json` prints the notes of the person, all
     elements with their positions, and the pixels of each screen.
  4. Build exactly what it shows. Do not move elements "to make it nicer". Ask first.

  A person can also give you the saved `.json` file. Put it into `designs/` and do steps 2 to 4.

Limits of the display that you must respect in each design: 128 x 32 pixels, on or off only
(no grey, no color), 5 fonts, text in one line, the characters from space to `~` only.

## 6. Build the firmware file

```sh
python tools/build.py           # release build: use this one for a device
python tools/build.py --debug   # debug build: log output on the serial port, larger
```

`tools/build.py` runs the safety check and the fast tests, builds with ESP-IDF v5.4 (or with
Docker if ESP-IDF is not installed), and checks the result. The firmware file is
`build/stattrak.bin`, with a copy in `out/` that has the version in its name.

If ESP-IDF is missing, the command says so. Then follow section 1, step 2, with
`python tools/setup.py firmware` (tell the person: about 2 GB download, 4 GB on disk,
10 to 30 minutes).

The direct commands, for developers (in a terminal where ESP-IDF is active):

```sh
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release" build
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.4 \
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release" build
```

- The firmware must fit in 1 MB. The build fails if it does not fit.
- If you change the build type by hand, delete `sdkconfig` and `build/` first. An old
  `sdkconfig` file wins over the defaults. `tools/build.py` does this for you.
- The firmware version is the text in the file `version.txt` (three numbers, for example
  `1.1.0`, or a beta for testers, for example `1.2.0-b1`; at most 9 characters, or the settings
  row "VERSION:" cuts it off). Change it only when the user asks, or for a release.
- The folder of the repository must not have a space in its name (ESP-IDF limit).
- `sdkconfig.hw_new` is for hardware with chip revision 2.0 or newer. Do not add it unless
  the user says that the device needs it.

## 7. Put the firmware on a device

Only when the person asks for it. Before the first time, say this plainly:
**"This is your own firmware. If it has a fault that the tests did not find, the device can
become unusable. The risk is yours."** Wait for a "yes".

Before:

1. `python tools/check.py` passed (the full check, not `--fast`) and `python tools/build.py`
   made the file.
2. The battery of the device is good, or the device is on USB power.
3. First time for this person? Advise them to install the newest **official** firmware first,
   with the same steps. Then they know that the update works on their device before they try
   their own firmware.

Steps (the person does them, you guide):

1. Open the StatTrak web updater in Chrome, Edge or Opera on a computer or an Android phone:
   https://stattrak.tridos.design . It does not work in Firefox, in Safari or on an iPhone.
2. On the device: Settings, then `Firmware: UPDATE`, hold the right button. The display shows
   `FIRMWARE UPDATE` and `Waiting...`.
3. In the web updater: `Pair device`, select `StatTrak`.
4. Select the firmware file from the folder `out/` and start the transfer. The display shows
   `Receiving` and the progress. Keep the page open and the device near the computer.
5. The device restarts and shows `Verifying...`. The web updater connects again and accepts
   the firmware. The display shows `Complete` and the version. Hold a button for OK.
6. **Check the update mode immediately:** open Settings, `Firmware: UPDATE` again and check
   that the web updater can pair with the device. Then cancel. If this does not work, tell
   the person to stop using the feature and get help (section 14).

If something goes wrong:

- `Error` on the display, or the transfer stops: nothing is lost. The old firmware is still
  there. Start again at step 2.
- `Verifying...` does not go away: switch the device off (hold both buttons). At the next
  start the device uses the old firmware again.
- The device does not start normally: hold both buttons for 10 seconds at power-on. The
  update mode opens. Install the official firmware.

## 8. The simulator: test before a device is at risk

The simulator compiles the **real firmware code** (`main/main.c`, the app, all views, the button
logic) for the computer. Only the hardware and ESP-IDF are replaced (`sim/host/`).

```sh
python tools/check.py --build-only            # build it (folder build-host/)
python tools/sim.py boot click1 click1 frame  # 2 kills, print the display as text
python tools/sim.py boot both click1 both png:settings state   # menu, SETTINGS, open
python tools/sim.py --tour                    # a picture of each main view in out/
python tools/sim.py --help                    # all actions
```

- `frame` prints the display as 32 lines of 128 characters (`#` = pixel on). You can read it.
- `png:NAME` saves `out/NAME.png`. Show it to the user.
- `state` prints the view, the counters, the settings and the flash content.
- `ble:0100` sends a Bluetooth user command, as a phone does (section 9).
- The flash content stays in `out/device_state.txt`. `--fresh` starts with a new device.
- A window version (SDL2) and a web version exist too: see `sim/README.md`.

What is real and what is simulated:

| Real firmware code | Simulated |
| --- | --- |
| Start sequence, main loop, views, settings, counters, storage logic, button gestures, dim, auto-off, battery warnings, Bluetooth user commands | Time (it moves only in `vTaskDelay`), button levels, display, flash (a key-value table), battery voltage |
| The Bluetooth update packet code runs in a unit test (`tests/unit/test_ble_ota.c`) | Bluetooth radio, the update task and flash writes of `components/ble_ota/ota.c` (replaced by `sim/host/host_ota.c`) |

## 9. Bluetooth for user features

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

## 10. Where things are

```
main/main.c              start sequence + main loop. HIGH RISK, see section 3
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
tools/                   setup.py, check.py, check_safety.py, sim.py, design.py, build.py
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
   example `stats_view.c`. The draw code can come from a design file (section 5).
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

## 11. Settings and values in flash

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

## 12. Tests

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
  must agree: do not change one without the other, and see section 3 before you change
  `components/ble_ota/` at all.
- `tests/test_design.py` compares the design tools with the firmware draw functions.
- `tests/test_designer.py` compares the Screen Designer with `tools/design.py` (needs Node.js;
  it is skipped without it).

On Windows with the tools from `.venv`, the build has no memory checker. The memory checker
runs on Linux and macOS with gcc or clang, and in the GitHub checks of a pull request.

## 13. Share a firmware feature or a fix with all owners

An app goes to the app store (section 18), not here. This section is for a change of the
firmware, the tools or the documents: a new feature, an improvement, or a fix of a fault.

**When you offer it.** The person is happy with the change, and one of these is true:

- Other owners will like the feature, and it does not disturb an owner who does not want it.
- It repairs a fault of the official firmware, of a tool or of a document. A fix is welcome
  also when it is one line.

Do not offer it for a change that is only for this person (their name on a screen, their team
logo). Ask first: "Do you want to send this to TRIDOS, so that all owners can get it?"

`CONTRIBUTING.md` has the rules in plain words. Read it, then do the technical steps for the
person.

### 13.1 Prepare

1. The person needs a free GitHub account. Explain: "GitHub is the website where the firmware
   lives. Your change goes there as a proposal. The TRIDOS team looks at it and tests it on a
   real device before it becomes part of the official firmware."
2. **One change in one proposal.** The work branch of a person often has more than one change.
   Make a new branch from the newest official `main` (`share/<short-name>`) and bring only
   this change to it. For a Beginner: "I make a clean copy with only this one change."
3. Do not change `version.txt`. Do not change the files that section 3 protects.
4. Run `python tools/check.py` (the full check). All 4 steps must show `OK`. The change has a
   test in `tests/` (section 12). For a fix: the test fails without the fix.
5. Get the pictures from the simulator: the screen before and after, or each step of the feature.
6. If the firmware build part is installed: `python tools/build.py`, and write down the size.

### 13.2 Write the text so that each person understands it

`CONTRIBUTING.md`, "Write the text of your proposal", has the rules and examples. Follow it.
In short:

- **The title** says what an owner gets, in plain words: `Kill streak: a sound after 5 kills
  in a row`, not `Update app.c`.
- **The first part** is for a person who never saw code. A feature: what the owner sees and
  which button does what. A fix: **Before** (what went wrong), **How to see the fault** (the
  steps), **Now** (what happens with the fix). Then the pictures from the simulator.
- **The second part** is for the reviewer: the files and why, new values in flash, the
  firmware size, what you ran.
- **The truth about the device.** "Tested in the simulator only", or exactly what the person
  did on a real device. Never write that it works on a device when only the simulator ran.

### 13.3 Send it

1. Make a copy of the repository in the account of the person (a "fork"), put the branch there
   and open the "pull request" (the proposal). Use the `gh` program if it is installed
   (`gh repo fork`, `gh pr create`). If not, guide the person through the GitHub website.
2. The `gh` program cannot add pictures. Give the person the address of the pull request and
   say: "Drag the pictures from the folder `out/` into the text field, below the heading
   Pictures." Give the full paths of the picture files.
3. Tell the person what comes next: the automatic checks take about 10 minutes. A person of
   the team answers, usually in one or two weeks. A request for a change is normal and not a
   "no".
4. When the team asks for a change, the person gives you the comment. Repair the cause on the
   same branch and answer in plain words what you changed.

Only an idea, no code? Help the person to open an "issue" with the form "Feature idea".
A fault that you cannot repair? The form "Fault report".

## 14. When you are not sure

Ask the user. A question costs nothing. A bricked device cannot be repaired.
If the person needs help from people: the "Issues" page of the repository on GitHub.

## 15. Code style

- 2 spaces, no tabs. Same style as the file that you change.
- Names: `Module_function()`, `s_localVariable`, `kConstant`, `g_globalVariable`,
  `ENUM__VALUE`, private functions start with `_`.
- Lists are X-macros (`VIEW_LIST`, `ENTRY_LIST`, `SETTING_LIST`). Add to the list, do not
  copy the list.
- Check array indexes that come from flash, from Bluetooth or from a calculation.
- `snprintf` with `sizeof(buffer)`. No `sprintf`, no `strcpy`.
- A firmware file that is also in the simulator must compile without ESP-IDF headers.
  Hardware code goes in `main/board/` or `components/`.

## 16. No code, an app, or a firmware change: you decide

The person has an idea. They do not know how it must be built, and they must not have to
know. You decide, in this order:

1. **No code.** The StatTrak or the web page can do it now (section 16.2). Say so and show how.
2. **An app.** The app engine can do it (section 16.3). This is the normal way. Section 17.
3. **A firmware change.** Only when an app cannot do it (section 16.4). Section 4.

Then tell the person what your decision means for them (section 16.5), before you build.

### 16.1 The person has no idea yet

Do not show a list of 20 ideas. Do this:

1. Ask one question: "What do you want to see or do in a game that your StatTrak does not do
   now?" If the answer is "I do not know", ask: "Do you want something for the game, or
   something for fun in the break?"
2. Give four examples from different groups, each in one line. Use the table below. Add the
   apps that other owners made: the app store, https://stattrak.tridos.design/#store .
3. Show one or two example apps as pictures, so the person sees what is possible:
   `python tools/app.py run apps/slots/slots.sts wait:1000 png:out/slots.png` (the apps are in
   the folder `apps/`).
4. When the person picks one, change it with them: "Which numbers do you want to see? What
   must the left button do?"

| Group | Ideas |
| --- | --- |
| Counters | Headshots, knife kills or hits on me in each game. A kill streak with a sound after 5 in a row. Shots that are left in the magazine. |
| Timers | The wait at the respawn point. A bomb timer that beeps faster at the end. The time of the game. |
| Games for the break | A reaction game. A dice for "who goes first". A coin flip. A small jump game. |
| Tools | A battery log. Time marks for a video. A score for two teams. |
| The look | My own start picture. My own counter screen with larger digits or my team name. |

The device has two buttons, a display of 128 x 32 pixels (on or off, no colors), a buzzer and
Bluetooth. It has no clock, no internet, no sensor for shots. Tell the person when an idea
cannot work, and why, and offer the nearest thing that can work.

### 16.2 Things that need no code

Check this first. Many wishes are there already:

| The wish | How, without code |
| --- | --- |
| My own picture when the device starts | Screen Designer, **Download PNG**, then the web page, My device, **Boot image** |
| A different first screen at the start | My device: move that app to the first place |
| Remove a screen that I do not use | My device: hide the app (the built-in ones too) |
| A name for my StatTrak | My device: the Bluetooth name |
| A copy of my stats and settings | My device: **Backup** |
| An app that another owner made | The app store on the web page: **Install** |

### 16.3 What an app can do

`docs/APPS.md` is the full specification (sections 2 and 4). The short list:

| An app can | Details |
| --- | --- |
| Draw each pixel of the display | Text in 4 fonts, numbers, the big dotted digits, lines, boxes, up to 16 pictures of its own, animation (a new picture each 10 ms) |
| Use the two buttons | Short press, long press, "the button goes down now" (fast games), "the button is held" |
| Read the counters | Total kills, total deaths, kills and deaths of the game |
| Change the counters | With the permission `counters`, in a button event |
| Make sound | A click or a note on the buzzer, also a tune (permission `sound`) |
| Measure time | Milliseconds since the start: a stopwatch, a countdown, a game speed |
| Use random numbers | A dice, a slot machine, enemies in a game |
| Read the battery | The level in percent and the voltage |
| Keep values | 16 numbers stay after power-off: a high score, its own counters |
| Have settings | 4 settings with 2 to 6 values each. The firmware draws the settings screen |
| Log data | Rows of numbers (4 KB) that the person downloads as a file from My device |
| Keep the device on | With the permission `awake`, while the app is on the screen |
| Run in the background | One app: it gets each kill and death, counts time, and comes on the screen for an alarm (permission `background`) |
| Be the main screen | The person puts it first in the app menu and hides the built-in counter. So "my own kill counter screen" is an app |

| An app cannot | Then it is |
| --- | --- |
| Change a built-in screen (KILLS, KILLS + DEAD, STATS), the Settings list, the app menu, the power-on animation | A firmware change. Or an app that is a new screen next to them |
| Change what both buttons do (short = app menu, long = power off) | Not possible. Section 3 protects it |
| Use Bluetooth: talk to a phone, to a computer or to another StatTrak | A firmware change (section 9) |
| Change how the device works: dim, auto-off, brightness, battery warnings, a new device setting | A firmware change |
| Show a text that the person types, or more than 32 fixed texts of 20 characters | A firmware change |
| Know the date or the time of the day | Not possible: the device has no clock |
| Be larger than 4 KB, or do more than 16000 steps for one event | Make it smaller, or a firmware change |
| Run in the background together with a second background app | Not possible: one at a time |
| Use new hardware: a sensor, a light, a different display | Not possible with this device |

### 16.4 The rule

- **If an app can do it, make an app.** Also when a firmware change looks more elegant to you.
  An app is safe for the device, fast to install and easy to share.
- **A part of the idea needs the firmware?** Say it, and offer the app part first: "I can make
  the timer as an app today. The part 'the display turns red' is not possible: the display has
  one color. The part 'show the timer in a corner of the KILLS screen' needs a firmware change,
  because an app cannot change a built-in screen. Do you want the app first?"
- **Only a firmware change can do it?** Say in one sentence which thing an app cannot do. Then
  section 4, and the warning of section 7 before the firmware goes on a device.
- **The idea is very large** (a game with many levels): start with a small version as an app.
  Let the person play it. Then make it larger.
- Not sure after you read `docs/APPS.md`? Write a small test app and run it
  (`python tools/app.py run`). That is faster than a guess.

### 16.5 Tell the person what it means

Say it before you build, in words for their level. Do not ask the person to choose. Say what
you will do and why, and let them stop you.

| | An app | A firmware change |
| --- | --- | --- |
| How it goes on the StatTrak | In some seconds, from the web page (My device). The StatTrak asks `INSTALL?` | The whole firmware goes on over Bluetooth. Some minutes, then a restart |
| The risk | An app runs in a closed box. A faulty app shows an error, and after 3 errors the StatTrak switches it off. It cannot damage the device | A fault that the tests did not find can make the device unusable. The risk is the person's (section 7) |
| What the computer needs | Python only | The simulator part (550 MB). For a real device also the firmware build part (4 GB) |
| An official firmware update later | The app stays | The official firmware replaces the change. You must build the own firmware again |
| Take it off again | Delete the app | Install the official firmware again |
| Share it | The app store: each owner installs it with one click. A review within 24 hours (section 18) | A proposal on GitHub. The team tests it on a real device. It comes with the next official firmware (section 13) |
| The limits | The list of section 16.3 | All that the hardware can do |

A text for a Beginner (an app): "Your idea works as an **app**. That is the safe way: an app
cannot damage your StatTrak, it goes on in a few seconds, and you can delete it again. I make it
now and show you pictures."

A text for a Beginner (a firmware change): "Your idea needs a change of the **firmware**, the
main software of the StatTrak. An app cannot do it, because (the reason). I can make it, and
you see it on the computer first. It takes longer, and when you put it on your StatTrak there
is a small risk for the device. Do you want that, or do you want a simpler version as an app?"

## 17. Make an app: the work sequence

An app needs no firmware build and no firmware update. Do these steps in this order. For a
Beginner, you do all of them and you show the result of each one.

1. **Understand the idea.** Say it back in two or three sentences: what each screen shows and
   what each button does. Ask two questions at most. Read `docs/APPS.md` (sections 2, 4, 7 and
   8) and the example app in `apps/` that is nearest to the idea.
2. **Design the screens first** (section 5). Always for a Beginner. Make 2 or 3 different
   proposals, show the pictures, let the person pick.
3. **Make a safe place for the work.** Create a git branch (`my/<short-name>`).
4. **Write the app:** `apps/<name>/<name>.sts`. Put these lines in from the start, so the app
   is ready for the app store later (section 18): a true `description`, the app's own title
   picture (`picture { ... }` or its own icon), and a `# demo:` line.
5. **Build and check it:**

   ```sh
   python tools/app.py build apps/<name>/<name>.sts     # makes apps/<name>/<name>.stapp
   python tools/app.py check apps/<name>/<name>.stapp   # the same check as the firmware
   ```

6. **Show it.** Take a picture of each step and show them in order. Say which button press
   leads to which picture.

   ```sh
   python tools/app.py run apps/<name>/<name>.sts wait:500 png:out/<name>-1.png press1 wait:2000 png:out/<name>-2.png
   python tools/app.py picture apps/<name>/<name>.sts   # the title picture of the app menu
   ```

   The output must have no line that starts with `App error`. Press each button in each state
   of the app at least one time.
7. **Offer the simulator. Do this each time, before the app goes on a StatTrak.** Say: "Do you
   want to play it on your computer first? You do not need your StatTrak for that."

   ```sh
   python tools/app.py sim apps/<name>/<name>.sts
   ```

   It opens the browser with the real firmware and the app in it. The arrow keys LEFT and RIGHT
   are the two buttons. LEFT + RIGHT together open the app menu.

   This command builds the simulator page the first time. That needs Emscripten or Docker. If
   the computer has neither, do **not** ask a Beginner to install them. Use the page on the
   web: open https://stattrak.tridos.design/sim/simulator.html , and tell the person: "In the
   box Apps on the right, click **Other app file (.stapp)** and pick this file:" with the full
   path of `apps/<name>/<name>.stapp`.
8. **Not happy with the look? Offer the Screen Designer.** Say: "You can draw the screen
   yourself, with the mouse, on the real pixels." Make a design file of the screens as they are
   now (`designs/<name>.json`, `docs/DESIGNS.md`) and open the designer with it (section 5).
   The person changes it and clicks **Send to AI**. Then you build exactly what they drew. With
   a StatTrak that is connected, **Show on StatTrak** in the designer shows the drawing on the
   real display.
9. **Change it until the person is happy.** Go back to step 4.
10. **Test it.** Run each button path with `python tools/app.py run` again: no `App error`.
    If the simulator part is installed, run it on the real firmware too:
    `python tools/sim.py --fresh boot app:apps/<name>/<name>.stapp long2 wait:500 png:<name>-fw`.
    `app:` installs the app, `long2` holds the right button for OPEN. Add the button presses of
    the app after it (`click1`, `click2`, `long1`, `long2`, `wait:MS`), then `png:`.
    An app that you add to the official apps of this repository also needs a test in
    `tests/test_sample_apps.py`.
11. **Save the work:** `git commit`. For a Beginner: "I saved a restore point."
12. **Put it on the StatTrak**, when the person wants that. The person does it, you guide:
    1. The web page https://stattrak.tridos.design/#device in Chrome, Edge or Opera, on a
       computer or an Android phone. Not Firefox, not Safari, not an iPhone.
    2. On the StatTrak: Settings, then `Apps: SYNC`, hold the right button.
    3. On the page: **Connect**, pick the StatTrak, then **From file** and the file
       `apps/<name>/<name>.stapp`. Give the full path.
    4. The StatTrak asks `INSTALL <NAME>?`. Hold the right button for yes.

    If the page says **Update the firmware first**: the StatTrak needs a newer official
    firmware (the Firmware tab of the page). An app with a list or `held()` needs firmware
    1.2.0-b4 or newer (`docs/APPS.md` section 7.5). Tell the person this before the install.
13. **Report:** what the app does, the pictures, what you tested. Say plainly: "I tested it on
    the computer. I cannot test it on your StatTrak."
14. **Offer to share it:** "Do you want other StatTrak owners to get your app? I can make it
    ready for the app store." Section 18.

## 18. Share an app in the app store

The app store is on the web page: https://stattrak.tridos.design/#store . Each owner can
install an app from it with one click. Each new app and each new version gets a review within
24 hours.

**When you offer it:** the person is happy with the app, and it ran without an error.

**Tell the person first**, and wait for a "yes":

1. "Your app becomes **open source**: each person can download the app and its source text."
2. "You sign in on the page with a Google or a GitHub account. Your maker name is public."
3. "A review looks at the app. It can take 24 hours. It can say no, with a reason."
4. "You upload the files yourself. I cannot sign in for you."

Never ask for a password of the person, and never type one.

**Then read `docs/APP_STORE.md` and follow it.** It has the check list of the review (the
name, the version, a true description, the app's own title picture, no picture files, the
`# demo:` line, the content rules, the two files that must fit together), the upload steps for
the person, and what to do when the review says no. Do the whole check list before the person
uploads: a maker can upload only 5 versions in one day.
