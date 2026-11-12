# Roadmap

Product status of the StatTrak firmware. Finding numbers (A1, B7, ...) are from
[AUDIT.md](AUDIT.md).

## In progress

- **Apps** (branch `feat/apps`, not shipped). Apps that users install over Bluetooth, like apps
  on a watch: the app engine (`components/stapp/`), the app list and storage (`main/apps.c`),
  the app host, app settings and "Apps: SYNC" views, the app manager commands `0x0110` to
  `0x011C`, the compiler `tools/app.py`, the example apps in `apps/` and 3 sample apps to
  try on a device (RUNNER, SLOTS, WALLPAPER; `tone()` for notes). Feedback round 1: the device
  asks "INSTALL <NAME>?" before the app is sent, small YES / NO buttons that fill while held,
  SYNC MORE and OPEN after the install, a Bluetooth name for "Apps: SYNC", `down1` / `down2`
  events (package format 2) so RUNNER jumps at once, new WALLPAPER buttons, and
  `Factory: RESET` after `Firmware: UPDATE` in the settings. All buttons on the display use the
  small style and answer a hold that starts on their screen (`main/views/hold_button.c`).The web and window simulators
  install apps too (an "Apps" panel, `--app FILE`) and play the buzzer in the browser. Specification:
  [APPS.md](APPS.md). Tested in the simulator only; the install over Bluetooth and the flash
  use of 24 KB of packages in NVS were not run on a real device.

## Next up

0. **The update screen turned the device off (reported 2026-11-12, not reproduced since).** On the
   first app firmware, Settings > `Firmware: UPDATE` gave a blank screen several times; the 10 s
   hold at power-on worked. Tests when it happens again: `Apps: SYNC` (does it turn off too?),
   `Sound: OFF` then `Firmware: UPDATE`, a fresh battery. Ideas after the results: start
   Bluetooth a moment after the click, less radio power at the start, more free RAM (the app
   firmware has 72 KB, 1.1.0 has 84 KB).

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
   `GH_OWNER` and `GH_REPO` in `updater/js/main.js` and the link at the top of
   `updater/index.html` from `janiszah/stattrak-fw` to this repository, and put a note into
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
  with the firmware fonts (`tools/designer/index.html`): more than one screen, notes, undo and
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
