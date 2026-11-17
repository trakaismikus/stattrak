# Roadmap

Product status of the StatTrak firmware. Finding numbers (A1, B7, ...) are from
[AUDIT.md](AUDIT.md).

## In progress

Nothing at the moment.

## Next up

These need a decision by TRIDOS and a test on real devices. They change code that the
simulator cannot test.

1. **Battery type list (A4).** The release `v1.0.1-added-rcr123a` has the new battery type in
   the middle of the list, so devices with two different lists exist. Decide: keep the new
   order and show "Check battery type" one time after an update, or convert the stored value
   at the first start. Must be decided before the next official release.
2. **Rescue from a firmware that crashes at each start (A3).** Count the starts in flash.
   After 3 starts that did not reach 10 seconds of normal operation, open the update view
   directly, before the normal views start. Option: go back to the other firmware slot.
3. **Restart when the firmware hangs (C1).** Switch on `CONFIG_ESP_TASK_WDT_PANIC` and feed
   the watchdog from the main loop. Check the two `while(1);` loops after power-off first (B12).
4. **Test this branch on real devices.** The app menu (speed of the slides on the real
   display, the way to Settings and to the update mode), the update fixes of the `dev` branch, the Bluetooth
   user commands, the web updater (`updater/`) and the firmware build with the setup tool on
   Windows and macOS were not run on hardware. The test lists are in the pull request.

### Before this repository becomes public

This repository (`trakaismikus/stattrak`) is private until these are done:

1. The test on real devices (item 4 above), and the pull request with this work is merged.
2. The public address of the web updater works: the DNS record for
   `stattrak.tridos.design`.
3. The official firmware files are in the "Releases" of this repository. Then change
   `GH_OWNER` and `GH_REPO` in `updater/js/main.js` from `janiszah/stattrak-fw` to this
   repository, and put a note into
   the old release repository.
4. Check the firmware fonts and the picture (AUDIT, section D), and the use of the name
   "StatTrak".
5. Say in `docs/SAFETY.md` if a wired rescue is possible for users.

## Backlog

- First official release with the fixes of this audit, so that devices in use get the
  check of flash values (A1).
- Ask for a confirmation on the device (button press) before a firmware transfer starts (C3).
- Clear the ring buffer when a firmware transfer starts (B8).
- A Bluetooth command "get state" (is a new firmware waiting for the accept?), so the web
  updater can show the real state.
- A small example web page for Bluetooth user commands.
- Counter display for 100000 kills and more, and for 1000 and more in the kill/death view (B9).
- No battery warning and no auto-off in the accept view (B11).
- Keep the power latch during a restart (C2).
- Rename `main/time.h`, remove the compiler warnings, clean `sdkconfig.defaults` (C7, C8, C9).
- New production units: a small rescue firmware in the `factory` slot and a bootloader that
  starts it when both buttons are held for a long time.
- Simulator: sound, and the real update task (`ota.c`) with a FreeRTOS stand-in.

## Shipped

- **2026-11-17: RESPAWN TIMER and `popup()` (firmware 1.2.0-b3).** A new app for the wait at the
  respawn point: big digits (MM:SS, or H:MM:SS from 1 hour), right button start / pause, left
  button back to the full time, hold left to edit the time (arrows, a check mark), a bar of the
  time left, hints for the left button as words or icons (app setting). When the time is up the
  screen flashes GO! with a running man and a chime plays until a button is pressed. It warns
  when it is not the background app. Engine version 3: the call `popup()` lets the background app
  come on the screen over a counter screen or an other app (never over the menu, the settings,
  the update mode or a warning), and makes the screen bright (`main/views/app_host_view.c`;
  docs/APPS.md section 4.6). Not tried on a device yet.

- **2026-11-17: your own title picture for an app.** A script can draw the whole 80 x 24 title
  picture as rows (`picture { "#..#" ... }`), or put its own `bitmap` next to the name
  (`picture icon NAME "TEXT"`). Both are in the `.sts` source, so an app store that takes only the
  source can show them. No firmware change: the package always held the picture as pixels. Old
  scripts build to the same bytes (`tools/app.py`, docs/APPS.md section 7.4).

- **2026-11-16: a favicon for the web site.** A black leaning S on the StatTrak orange, in the
  browser tab of every page (`updater/favicon.svg`; `favicon.ico` and `apple-touch-icon.png` are
  made from it by `node tests/make-favicon.js`).
- **2026-11: backup, app data, device log, start picture, live preview, background app
  (firmware 1.2.0-b2).**
  On the Apps tab: a backup file of the stats, the settings, the app menu, the app settings and
  saved values, the Bluetooth name and the start picture, and a restore that asks "RESTORE
  BACKUP?" on the device first and checks each value (`main/backup.c`). A data store for each
  app (4 KB each, 8 KB for all; full = stop; the settings and the counters always keep 5 KB of
  flash), with `log()` in the app scripts (package format 3, engine version 2), CSV download and
  a delete that the device asks first (`main/app_data.c`). Two example apps: BATTERY LOG and
  CLIP MARKS. A device log of the last 64 starts (why, battery, new firmware;
  `main/device_log.c`). A start picture that the person chooses (none by default: the device
  starts with the first app of the menu), with on / off and delete on the page
  (`main/start_pic.c`, `main/views/intro_view.c`). "Live preview" and "Download PNG" in the
  Screen Designer. A background app: one app with the permission "background" runs all the time
  (Settings > Background, only shown when such an app is installed), gets the kill and death
  events of the counter screens and keeps logging while you use other screens; CLIP MARKS and
  BATTERY LOG use it (two app engines, `main/views/app_host_view.c`; docs/APPS.md section 4.6).
  A calmer, truer battery level: a discharge curve for lithium and NiMH cells in place of a
  straight line, a voltage average of about 26 s and a dead band, so the % and the battery
  symbol do not jump (`main/battery.c`). A safety rule keeps the order of the setting values (a backup stores
  positions). Specification: [DATA.md](DATA.md). Not tried on a device yet: the flash use of
  full app stores next to 24 KB of apps was only tested with a model of the NVS in the
  simulator.

- **2026-11: My device and the StatTrak button.** The Apps tab is now **My device**
  (`#device`; `#apps` still works). The header shows the connected StatTrak (its name, the
  number of app updates; a panel with serial, firmware, storage, the updates, Disconnect), and
  Connect from any tab. Installed apps with a newer catalog version show "Update available".
  The site has places for added parts (an extra tab, a sign-in button) and tells added scripts
  about the device with the event `stattrak:device`; the site itself sends nothing.
  On a web server, Get started and the Screen Designer open inside the same page, so the
  StatTrak connection stays when you change pages. My device: the name of StatTrak at the top
  (a pencil opens the Bluetooth name), the boot image (the start picture), backup, app data and
  device log above the app list, each one folds. The Screen Designer: "Show on StatTrak" sends
  the selected screen once, over the connection of the header (in place of the live preview).
  No console on the page (the log is in the developer tools of the browser).
  Firmware tab: "Use this" next to an official release loads its file (through `api/firmware.js`),
  no download and no drop. The "Show on StatTrak" state is a popup on the button (a dot shows it).
- **2026-11: App Sync how-to and Screen Designer tools.** The Apps tab shows the whole way to
  App Sync on the current firmware (counter, app menu, Settings, "Apps: SYNC", hold the right
  button, each click shown) with the lit buttons, at the size of the firmware how-to; "Apps: SYNC"
  is now row 3 of Settings (after Game and Counter); the same path boxes as the
  firmware step, easier to see. No button numbers on the web site: left and right button. The
  buttons on the display (BACK, YES, NO, OK, OPEN, CANCEL, DISMISS, SYNC MORE) have the same gap
  on both sides of the text. Screen Designer: the device picture with the live screen at the
  top, the 4 built-in screens as the start project, Select tool (more parts, or part of a
  drawing; Combine, Intersect and Subtract make one drawing of them), Circle, round corners as a number up to round ends, a cut-out border for each part,
  black parts red while selected, a Trace picture to draw over (100 % = as high as the screen, corner handles), and
  "New screen" (a short menu; the AI help is a window of its own). Design format: `circle` and `border` (docs/DESIGNS.md).
- **2026-11: new device picture for the web updater and the web simulator.** A render of the 3D model
  (matte black anodised aluminium, etched text, a glass that fills the rounded pocket) that looks
  right on black and on white pages, with a live display on top (`updater/js/device.js`, plain pixels with no drawn gaps). Step 1
  of the firmware update plays the way to the update mode of firmware 1.0.1 (made by the 1.0.1
  simulator) in place of the old video; the Apps tab plays the "Apps: SYNC" screens (waiting, the
  hold of YES, installed). A firmware check (`tests/test_updater_screens.py`) fails when the device
  screens change and the page was not made again. Tools: `tools/device_art/`.
- **2026-11: one web site (PR #8).** Firmware, Apps and the Screen Designer with one menu, and a
  new "Get started" page: what StatTrak is, how to update, install apps, design screens and make
  your own app. The designer moved from `tools/designer/` to `updater/designer/` (it still opens
  from a disk). Not online yet: the Vercel project `stattrak` must be connected to this
  repository, the domain `stattrak.tridos.design` moved to it, and the DNS record made.
- **2026-11: apps (firmware 1.2.0-b1, a beta for testers).** Apps that users install over Bluetooth, like apps
  on a watch: the app engine (`components/stapp/`), the app list and storage (`main/apps.c`),
  the app host, app settings and "Apps: SYNC" views, the app manager commands `0x0110` to
  `0x011C`, the compiler `tools/app.py`, the example apps in `apps/` and 3 sample apps to
  try on a device (RUNNER, SLOTS, WALLPAPER; `tone()` for notes). Feedback round 1: the device
  asks "INSTALL <NAME>?" before the app is sent, small YES / NO buttons that fill while held,
  SYNC MORE and OPEN after the install, a Bluetooth name for "Apps: SYNC", `down1` / `down2`
  events (package format 2) so RUNNER jumps at once, new WALLPAPER buttons, and
  `Factory: RESET` after `Firmware: UPDATE` in the settings. All buttons on the display use the
  small style and answer a hold that starts on their screen (`main/views/hold_button.c`). The web and window simulators
  install apps too (an "Apps" panel, `--app FILE`) and play the buzzer in the browser. Specification:
  [APPS.md](APPS.md). Installs over Bluetooth tested by the owner on a device; the flash use
  of 24 KB of packages in NVS was not measured on a device.
- **2026-11: soft radio start (firmware 1.2.0-b1).** Fix for "the update screen
  turned the device off" (2026-11-12): a brown-out at the radio start, because the bright
  Settings screen and the radio start together pulled the supply too low on a weak unit (also
  with firmware 1.0.1; test firmware: 6 of 10 starts failed, 0 of 15 with the display off). The
  update view, the accept view and the app sync now show a small Bluetooth sign first and start
  the radio when only the sign is lit (docs/BLUETOOTH.md, "Radio start"). Same branch: the
  app sync Bluetooth name is "StatTrak | " + a part of 12 characters at most, and the settings list
  wraps (up from the first row goes to the last row, down from the last row to the first).
  Device test on the weak unit (2026-11-13): no failure, also with a high-power flashlight on
  the same battery.
- **2026-11: a README for people who do not code.** A title picture (the slot machine app on
  the device), product photos and links to the StatTrak page of the TRIDOS shop, and sections
  with screenshots for "Start in 5 minutes", the Screen Designer, the Web Updater, Apps and
  App Sync, and the device views (pictures in `docs/images/`). The developer commands are in a
  section that opens on a click.
- **2026-11: app menu.** Both buttons open a menu of the apps (KILLS, KILLS + DEAD, STATS,
  SETTINGS) in place of "next view". Each app has a title picture; button 1 / 2 go to the
  previous / next app, both buttons or 3 seconds without a press open it. The app slides up and
  the menu comes up from below; a bar at the bottom shows the position. The safety check makes
  sure that the menu keeps SETTINGS (the way to the firmware update). Not yet tried on a device.
- **2026-11: one repository, and the web updater.** The web updater is in `updater/`, with
  the fixes for this firmware: "Flash complete" only after the device restarted and confirmed
  the new firmware, clear messages when an update fails, a check of the firmware file before
  the transfer, a damaged sector is sent again, free fonts, no files from other sites. Tests
  with a stand-in device, also of the full page in headless Chrome. The page and the firmware
  are checked to use the same Bluetooth numbers. The repository address is
  `github.com/trakaismikus/stattrak`. The firmware version is 1.1.0, and the number is in
  the file `version.txt` now (before, it came from git tags).
- **2026-11: Screen Designer and license.** A page to draw StatTrak screens pixel for pixel
  with the firmware fonts (`updater/designer/index.html`): more than one screen, notes, undo and
  redo, save and open, and "Send to AI" (a text with the exact design data). Its drawing code
  is tested against `tools/design.py` and so against the firmware. Free fonts in place of the
  paid brand font. License: GPL-3.0-or-later. The firmware build with `tools/setup.py firmware`
  and `tools/build.py` was run on Windows.
- **2026-11: firmware for everybody.** Guided setup for AI coding tools: three questions to
  learn who the user is, a setup tool that says what it installs (`tools/setup.py`; the
  simulator needs Python only, also on Windows), one command for the firmware file
  (`tools/build.py`). Screen design files with exact pictures, text for AI tools and C draw
  code (`tools/design.py`, `docs/DESIGNS.md`). Bluetooth commands for user features
  (`main/ble_commands.c`, `docs/BLUETOOTH.md`). How to share a feature, in plain words
  (`CONTRIBUTING.md`, issue and pull request forms). Update fixes of the `dev` branch merged
  (B7, B10, C4, C5). Audit of the web update tool.
- **2026-11: open-source readiness.** Code audit (`docs/AUDIT.md`). Simulator that runs the
  real firmware code, with headless, window and web front ends. Tests: full firmware in the
  simulator, unit tests, bad flash values, random use, Bluetooth packet code. Safety check
  (`tools/check_safety.py`) and one command for all checks (`tools/check.py`).
  Instructions for AI coding tools (`AGENTS.md`). Fixes: A1, A2, B1 to B6.
- 2026-04: battery type `1xRCR123A`. Light sleep at power-off.
- 2026-01: firmware update over Bluetooth with accept and rollback. Auto-off, low and
  critical battery warnings, statistics view, web simulator. Version 1.0.0.
