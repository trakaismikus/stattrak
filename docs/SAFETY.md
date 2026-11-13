# Safety: how not to brick a StatTrak

"Bricked" means: the device cannot start, or it cannot get new firmware, and the user cannot
repair it. This document tells you how the device protects itself, what can still go wrong,
and what you must do before you put your own firmware on a device.

## 1. How a firmware update works

The flash memory of the device has these parts (`partitions.csv`):

| Part | Address | Size | Content |
| --- | --- | --- | --- |
| bootloader | 0x0 | | Starts the firmware. Never changes after production |
| partition table | 0x8000 | | This table. Never changes after production |
| `nvs` | 0x9000 | 48 KB | Counters and settings |
| `otadata` | 0x15000 | 8 KB | Which firmware slot starts, and its state |
| `phy_init` | 0x17000 | 4 KB | Radio data |
| `factory` | 0x20000 | 1 MB | Firmware from production |
| `ota_0` | 0x120000 | 1472 KB | Firmware slot for updates |
| `ota_1` | 0x290000 | 1472 KB | Firmware slot for updates |

A Bluetooth update writes only a firmware slot (`ota_0` or `ota_1`), `otadata` and `nvs`.
It never writes the bootloader or the partition table.

The sequence:

1. The user opens the update view. Then Bluetooth is on and the device takes new firmware.
   (A user feature can switch Bluetooth on in its own view. It must refuse new firmware
   there: `docs/BLUETOOTH.md`.)
2. The web updater (https://stattrak.tridos.design, called "the phone" below) sends the new firmware. It goes into the slot that is **not** in use.
   The firmware that runs now stays in flash.
3. ESP-IDF checks the new firmware image (checksum, chip type). A damaged image is not used.
4. The device restarts into the new firmware. Its state is **"pending"**.
5. The new firmware shows the accept view ("Verifying...") and starts Bluetooth.
6. The phone connects again and sends the command **"accept"**. The firmware is now **"valid"**.
7. If the new firmware crashes, hangs, or the user switches the device off before the
   accept: at the next start the bootloader **goes back to the old firmware**
   (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`).

The accept over Bluetooth is the important part: a new firmware is kept only if it can
start, keep the power on, and talk over Bluetooth. That is what the next update needs.

## 2. What this does not protect against

After the accept, the device does not go back by itself. Then these can brick a device:

1. **The firmware crashes in normal operation at each start.** The accept view worked, but
   the normal views crash. The rescue way (hold both buttons for 10 seconds) needs the
   normal views to run for these 10 seconds.
2. **The update view cannot be opened.** For example the settings row is gone, the "both
   buttons" gesture is used for something else, or a view takes all button events.
3. **The firmware switches the device off too early at each start.** For example a wrong
   automatic power-off or a wrong battery warning.
4. **The update view opens, but Bluetooth is not started** or the update code is broken
   after the accept (for example by a value in flash that changes later).
5. **eFuses are burned** (secure boot, flash encryption). This is permanent.
6. **The bootloader or the partition table is replaced** with a wrong one over a wired
   connection.

The checks in this repository look for these cases:

| Case | Check |
| --- | --- |
| 1, 3 | `tests/test_robustness.py`: start with bad flash values, random button presses, then the rescue way must work |
| 2, 4 | `tests/test_update.py`, `tests/test_boot.py`, and `tools/check_safety.py` (rules `start-sequence`, `update-views`) |
| 5, 6 | `tools/check_safety.py` (rules `no-efuse-config`, `no-raw-flash`, `partition-table`, `sdk-config`) |

A check can only find what it tests. It is not a guarantee.

## 3. The rules

`python tools/check_safety.py --explain` prints the rules with the reason for each one.
In short:

1. Do not change `partitions.csv`.
2. Keep the chip, crystal, flash size, partition, Bluetooth and rollback settings in
   `sdkconfig.defaults`.
3. Never switch on secure boot, flash encryption or anti-rollback. Never write eFuses.
4. Only the Bluetooth "accept" command marks a firmware as valid. A firmware must not
   accept itself.
5. Firmware slots are written only by `components/ble_ota/ota.c`. No direct flash writes.
6. Do not change the pins in `main/board.h`.
7. Keep the start sequence in `main/main.c`: power latch first, pending firmware opens the
   accept view, 10 seconds hold opens the update view.
8. The update view and the accept view must start Bluetooth (`Ota_init()`).
9. Keep the names of the values in flash. Check each value from flash before use.
10. Keep the Bluetooth name and service numbers.
11. Never block the main loop.
12. Keep "both buttons short" = app menu and "both buttons long" = power off. The app menu
    (`kApps` in `main/views/launcher_view.c`) must always have SETTINGS.
13. Do not switch the device off for a low battery while firmware is received. A device that
    reads a wrong battery level could never get a new firmware.
14. Bluetooth features go into `main/ble_commands.c`, not into the update code. The update
    view and the accept view must always take new firmware (`docs/BLUETOOTH.md`).
15. Add a new value of a setting at the end of its list, never in the middle. The device
    stores the position.

### Risk of each part of the code

| Risk | Files | What to do |
| --- | --- | --- |
| Very high | `partitions.csv`, `sdkconfig.*`, bootloader settings | Do not change |
| High | `components/ble_ota/`, `components/app_ble/`, `main/main.c`, `main/board.c`, `main/board/` | Change only if you must. Test on a device that has a wired connection |
| Medium | `main/app.c`, `main/app_view.c`, `main/eeprom.c`, `main/battery.c`, `main/views/update_view.c`, `main/views/fw_accept_view.c`, `main/views/settings_view.c` | All tests must pass. Add tests for the change |
| Low | Other views, texts, pictures, fonts, sounds | All tests must pass |

## 4. Before you put your firmware on a device

1. `python tools/check.py` shows `OK` for all 4 steps.
2. You looked at your change in the simulator.
3. The firmware is the release build, and it is built from the code that you tested:
   `python tools/build.py`. It runs the checks again and checks the firmware file.
4. In the simulator, these still work with your firmware:
   - Settings, `Firmware: UPDATE` opens the update view:
     `python tools/sim.py --fresh boot both click1 both click2 click2 click2 click2 click2 click2 click2 click2 click2 long2 state`
     must print `view=UPDATE` and `ota_active=1`.
   - Hold both buttons for 10 seconds at power-on: `python tools/sim.py boot:10600 state`
     must print `view=UPDATE`.
5. The battery of the device is full.
6. You have the last official firmware file ready, to go back to it.
7. Your first update? Install the official firmware one time first, with the same steps. Then
   you know that the update works on your device before you try your own firmware.

During the update:

- When the display shows "Verifying...", the old firmware is still safe. If something looks
  wrong, **switch the device off now**. The next start uses the old firmware.
- After "Complete", go through all views, open the settings, and open
  `Firmware: UPDATE` one time. If the update view does not open, install the official
  firmware again immediately, while you still can (try the 10 seconds hold).

## 5. Rescue

| Problem | What to do |
| --- | --- |
| New firmware shows "Verifying..." and nothing happens | Switch off (hold both buttons). The old firmware starts next time |
| New firmware does not start, or the display stays dark | Remove the power (battery out), wait, switch on. The old firmware starts |
| The firmware runs, but something is wrong | Open the update view and install the official firmware |
| The settings menu cannot be reached | Hold both buttons for 10 seconds at power-on. The update view opens |
| Strange behaviour after a change of firmware (wrong settings, power-off) | Settings, `Factory: RESET`. This erases the counters too |
| Nothing above works | The device needs a wired (serial) connection to the chip. <!-- TODO (TRIDOS): say if this is possible for users, and how --> |

## 6. Values in flash and other firmware versions

Counters and settings stay in flash when the firmware changes. An other firmware version
reads the same values. This is why:

- A value in flash can be larger than this firmware expects. This firmware replaces such a
  value with the default at start (`_sanitizeEntries()` in `main/app.c`). Released firmware
  up to version 1.0.1 does not do this: a bad value can crash it at each start.
- A value can be missing in flash, because an older firmware did not have it. This firmware
  writes the default for it at start.
- A setting is stored as the position in its list of values. A new value in the middle of
  the list changes the meaning of the stored positions after it. Add new values at the end.
- Do not change the meaning of a value. If your firmware needs a different meaning, make a
  new entry with a new key.
- Before you go back from your firmware to official firmware 1.0.1 or older,
  do a factory reset in your firmware.

<!-- TODO (TRIDOS): name the first official release that has this change. -->

## 7. Known gaps

See [AUDIT.md](AUDIT.md) and [ROADMAP.md](ROADMAP.md). The most important ones:

- A firmware that was accepted and then crashes at each start has no automatic rescue.
- The battery type list of the release `v1.0.1-added-rcr123a` has a new value in the middle.
  After an update from 1.0.1 or older, check the battery type in the settings (AUDIT, A4).
- A firmware that hangs does not restart by itself (the watchdog only prints a warning).
- Anybody near the device can send firmware while the update view is open.
