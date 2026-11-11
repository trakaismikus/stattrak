# Code audit, November 2026

Audit of the StatTrak firmware at commit `b51fa0c` (tag `v1.0.1` + 1 commit), before the
source code becomes public. The main question was: **how can a user brick a device with
changed firmware, and how do we prevent it?**

Line numbers are for commit `b51fa0c`.

**Update, 11 November 2026:** the developer's branch `dev` (commit `716b890`: update fixes) is
merged into this branch and reviewed (section F). The web update tool is audited (section E).
The states in the tables below are current.

## Method

1. Read all firmware source files (`main/`, `components/`).
2. Compiled the unchanged firmware code for a computer (`sim/host/`) and ran it with
   AddressSanitizer and UndefinedBehaviorSanitizer, 32-bit like the ESP32-C2.
3. Wrote tests of the normal functions, then tests with bad flash content, random button
   presses and random Bluetooth packets.
4. Built the firmware with the official ESP-IDF v5.4 Docker image on a clean computer.

**Not done:** no test on a real device. No test of the real Bluetooth update task and
Bluetooth start (`components/ble_ota/ota.c`, `components/app_ble/`) on a computer: they need
the chip.

## Summary

| Result | Count |
| --- | --- |
| Findings that can brick a device, break the update, or are a safety problem | 4 (2 fixed, 2 open) |
| Other bugs | 12 (8 fixed, 4 open) |
| Robustness and code quality notes | 9 (2 fixed, 7 open) |
| Things to decide before the code is public | 3 |
| Web update tool (section E) | 12 (3 must be fixed before it is used with this firmware) |
| Review of the `dev` branch (section F) | 1 change not taken, 2 notes |

The update design is good: the old firmware stays in flash, and a new firmware is kept only
after the phone accepts it over Bluetooth. The weak points are (1) values in flash from an
other firmware version, (2) what happens after the accept, and (3) the Bluetooth packet code.

## A. Findings that can brick a device, break the update, or are a safety problem

### A1. A bad value in flash crashes the firmware at each start. FIXED

- **Where:** `main/app.c:126`, `:159`, `:170` and `main/battery.c:89`, `:105`.
- **What:** the settings `low_bat`, `auto_off`, `dim_screen` and `battery_type` are read from
  flash and used as an array index without a check.
- **How it happens:** an other firmware version (for example a user firmware with one more
  option in a setting, or with a different meaning of a value) stores a value that this
  firmware does not know. The value stays in flash when the firmware changes.
- **Result on the unchanged firmware (simulator):** with `auto_off`, `dim_screen` or
  `low_bat` = 65536 or larger, the firmware reads outside its memory in the first loop and
  crashes. On the device this is a crash at each start, 10 ms after the start. The rescue way
  (hold both buttons for 10 seconds) crashes too. With `auto_off` = 17 the firmware
  switched the device off 20 ms after the start. In the first test run 50 of 340 cases with
  bad values failed (46 of them were crashes).
- **Fix:** each entry in `ENTRY_LIST` has a largest valid value. `App_init()` replaces
  a value that is too large with the default. The three table reads use a checked index.
  The battery type is checked in `Battery_setType()`. The compiler checks that the tables
  and the settings texts have the correct count of values.
- **Tests:** `tests/test_robustness.py` (340 cases with bad values), `tests/unit/test_units.c`.
- **Note:** released firmware up to this audit does not have the fix. See `docs/SAFETY.md`, section 6.

### A2. The Bluetooth update packet code writes outside its buffer. FIXED

- **Where:** `components/ble_ota/ble_ota.c:364-387` (`_writeFw`).
- **What:** (a) The size check looks at the first buffer of a packet only. NimBLE can give
  one packet as a chain of buffers. The data of the other buffers is copied without a check:
  a write after the end of `s_fwBuf`. (b) If the first buffer is too large, the code calls
  `assert(0)`, and the device restarts.
- **Who can do it:** any Bluetooth device near the StatTrak, while the update view or the
  accept view is open. There is no pairing.
- **Fix:** the packet code was written again in the `dev` branch: a packet is read with a
  length check over the full buffer chain, a wrong sector number or sector length is refused,
  the CRC of each sector is checked, and only firmware bytes go to flash. A bad sector gets an
  error answer and can be sent again.
- **Also fixed here:** `_notify()` took a buffer from the Bluetooth pool also when the phone
  did not subscribe, and never gave it back (`ble_ota.c:451`).
- **Tests:** `tests/unit/test_ble_ota.c` runs the real `ble_ota.c`: 27 tests, with 60 000
  random packets and with data in the exact format of the web update tool.

### A3. An accepted firmware that crashes in normal operation cannot be rescued. OPEN

- **What:** the phone accepts a new firmware while only the accept view runs. The normal
  views have not run yet. If they crash at each start, the bootloader does not go back
  (the firmware is "valid"), and the 10 seconds rescue hold needs the normal views to run.
- **Proposal:** a start counter in flash. After 3 starts that did not reach 10 seconds of
  normal operation, the firmware opens the update view directly (or goes back to the other
  firmware slot). See `docs/ROADMAP.md`, item 2. This changes the start sequence, so it
  needs a decision and a test on real devices.

### A4. The newest commit changes the meaning of the stored battery type. OPEN

- **Where:** commit `b51fa0c` ("Added 1xRCR123A to battery selection"), `main/battery.h`,
  `main/battery.c`, `main/app.h`. It is the one commit after the tag `v1.0.1`.
- **What:** the new type `1xRCR123A` is at position 2 of the list, not at the end. The device
  stores the position. After an update from 1.0.1, each battery type after USB is one
  position off:

  | Set in 1.0.1 | Shown after the update | Result |
  | --- | --- | --- |
  | 3.7V(1S) | 1xRCR123A | Same limits. No problem |
  | 7.4V(2S) | 3.7V(1S) | Critical limit 3.3 V in place of 6.6 V: no warning, the battery can be discharged too far |
  | 11.1V(3S) | 7.4V(2S) | Critical limit 6.6 V in place of 9.9 V: the battery can be discharged too far |
  | 3xAA/AAA | 11.1V(3S) | Always "critical": the device switches off 60 s after each start, until the user changes the setting |
  | 9.6V(NiMH) | 3xAA/AAA | Critical limit 2.7 V in place of 7.2 V: no warning |

- **Why it is here:** a LiPo battery that is discharged too far is a safety problem, and the
  3xAA case looks like a broken device to the user.
- **Fact, found 11 November 2026:** firmware with this commit is public. The release
  `v1.0.1-added-rcr123a` (15 April 2026) in the repository `janiszah/stattrak-fw` is marked as
  the latest release and was downloaded 8 times. So two groups of devices exist: devices with
  the old list (1.0.1 and older) and devices with the new list. The stored value does not say
  which list it is from.
- **Not changed in this branch.** To move `1xRCR123A` to the end now repairs the first group
  and breaks the second group. The decision needs a fact that the code does not show: which
  firmware do the sold devices have? Options: (1) keep the new order and show a one-time
  message "Check battery type" after the update, (2) find the group from the firmware version
  in the other firmware slot at the first start and convert the value. Both change code that
  must be tested on devices. See `docs/ROADMAP.md`.

## B. Other bugs

| # | Where | What | State |
| --- | --- | --- | --- |
| B1 | `main/app_view.c:48` | If a warning view opens during the 0.3 s change to the update view, `Ota_deinit()` is never called: Bluetooth stays on in normal operation, and the update does not work until the next power cycle. Shown in the simulator. | FIXED |
| B2 | `main/views/settings_view.c:302-305` | With all 14 settings shown and the cursor on the last one, the draw code reads 2 bytes after the end of the list. | FIXED |
| B3 | `main/resources/font.c:16-86` | A character that a font does not have (for example a byte above 127, or a control character) is read from after the end of the font table. | FIXED |
| B4 | `main/battery.c:101` | Battery type USB with a voltage reading of 0 mV is "critical": the device switches off after 60 s. | FIXED |
| B5 | `dependencies.lock`, `main/idf_component.yml` | The lock file had a folder path of the developer's computer. On an other computer the build stopped with an error. The button component had no fixed version. | FIXED |
| B6 | `sim/` | The simulator did not link on Linux. Its main loop was a copy of `main.c` with differences (the check `id == ASK && id == UPDATE` is never true, no storage, power-off stopped the program in a loop). | FIXED (it runs the real `main.c` now) |
| B7 | `components/ble_ota/ota.c:260-268`, `:291`, `:300`, `:399-403` | Errors of `Ota_start()`, `Ota_write()` and `esp_ota_end()` are not used. After a failed transfer the display shows "Completed" and the device restarts. The "accept" command reports "accepted" also when the accept failed. The text "Error" in `update_view.c:93` is at the position of the state "accepted". Not a brick risk: ESP-IDF does not start a bad image. | FIXED in `dev`: a failed transfer shows "Error" and does not restart the device. A transfer without data for 15 s stops. |
| B8 | `components/ble_ota/ota.c:273-328` | After a transfer that stopped in the middle, bytes of the old transfer can stay in the ring buffer and go into the next transfer. The next transfer then fails. (From reading the code. Not tested.) | OPEN, smaller: `dev` stops the old transfer first, but bytes that are in the ring buffer at that moment can still go into the new transfer. The image check at the end finds it. |
| B9 | `main/views/total_view.c:62`, `kill_dead_view.c:88` | The total view has 5 digits and the kill/death view has 3. A larger number shows its first digits: 100000 is shown as `10000`, 1000 as `100`. | OPEN |
| B10 | `main/eeprom.c:157-160`, `main/app.c:97` | Default values are stored on the first start only. A setting that a later firmware adds reads 0 on devices that are already in use, not its default (example: `sound`). | FIXED in `dev`: a value that is not in flash gets its default at the start. |
| B11 | `main/views/fw_accept_view.c`, `main/app.c:142` | In the accept view the battery and auto-off rules are active. With a critical battery reading, the battery warning replaces the accept view after 49 s, Bluetooth goes off, and the firmware is not accepted (the device goes back to the old firmware). | OPEN |
| B12 | `main/main.c:89`, `main/app.c:196` | After `Board_powerOff()` the code stays in `while(1);`. If the chip still has power (for example from USB), the CPU runs at full load with a dark display until the power is removed. To check on hardware. | OPEN |

## C. Robustness and code quality notes

| # | Where | Note |
| --- | --- | --- |
| C1 | `sdkconfig` | `CONFIG_ESP_TASK_WDT_PANIC` is off. If the main loop hangs, the device does not restart: the display freezes with the power on, and the user must remove the battery. |
| C2 | `main/system.c:48` | The power latch is not held during a restart (`TODO` in the code). The device can switch off after a firmware transfer, and the user must switch it on. |
| C3 | `components/ble_ota/` | No pairing and no check of the sender. Anybody near the device can send firmware while the update view is open. The firmware image has no signature (this is on purpose for an open firmware). |
| C4 | `components/ble_ota/ble_ota.c:403-409` | The 2 CRC bytes at the end of each sector are not checked. An error is found only at the end, by the image check of ESP-IDF. **FIXED in `dev`.** |
| C5 | `components/ble_ota/ble_ota.c:272`, `:290`, `:322` | Packet data is read through struct pointers (`ble_ota_cmd_t*`) at addresses that are not aligned. This works on the ESP32-C2. It is not valid C. **FIXED in `dev`** (the data is copied first). |
| C6 | `main/main.c:62-66` | If the storage is full or has a new format, all counters and settings are erased without a message. |
| C7 | `main/time.h` | The file name is the same as the C library header `<time.h>`. This works now because of the include order. A rename is safer. |
| C8 | `main/eeprom.c:24` | `TAG` is a global symbol (no `static`). `settings_view.c:293`, `stats_view.c:42`, `ssd1307.c:155`, `ble_ota.c:207`, `app_ble.c:28`: compiler warnings for unused variables and functions. |
| C9 | `sdkconfig.defaults` | `CONFIG_BTDM_CTRL_MODE_*` are not known by ESP-IDF v5.4 for this chip (warning at each build). `CONFIG_ESP_CONSOLE_UART_BAUDRATE=460800` has no effect: the build uses 74880. |

## D. Before the code is public

1. **License. DECIDED (11 November 2026): GPL-3.0-or-later, file `LICENSE`.** The repository
   had no `LICENSE` file. Without one, nobody has the permission
   to use or change the code. The proposal was: **GPL-3.0-or-later**. Each person can use, change and
   share the firmware, and each changed version that is given to other people must be open
   too. So a seller of copied hardware cannot take the firmware and close it, and each
   improvement can come back to all users. Other open firmware projects use it (Marlin, QMK,
   Flipper Zero). ESP-IDF and NimBLE (Apache-2.0) are compatible with it. The other option is
   **Apache-2.0**: more freedom for other companies, no duty to share changes. The license
   covers the code only. Say in the `README.md` that the names and logos of TRIDOS and
   StatTrak are not part of it. This is not legal advice.
2. **Other people's material.** Check the origin and license of the fonts in
   `main/resources/` (for example the "Nightverb" font) and of the picture
   `sim/web/StatTrakv3.jpg`. The web update tool uses the font "TT Lakes Neue" (TypeType,
   a paid font): its files must not go into a public repository without a license for that.
   The Screen Designer in this repository uses the free font "Barlow Condensed" (SIL Open Font
   License) in its place. It is the closest free match: condensed, square shapes, real italics
   in the heavy weights.
3. **The name.** "StatTrak" is a name that Valve uses in Counter-Strike, and the web
   simulator shows "StatTrak(TM)". Ask a lawyer before the public release.

No password, key or token was found in the source code or in the git history. The commit
history has the e-mail addresses of the author. They become public with the repository.

## E. Web update tool (repository `stattrak-web`, branch `design-refresh`)

The web page that sends the firmware over Bluetooth (`main.html`, `js/ble.js`, `js/main.js`),
at commit `c13cfd2`. Checked by reading the code and against the packet code of this branch.
Not run against a device.

**The data format is correct for this firmware.** The page sends each sector as 4096 bytes
(filled with zeros) plus the CRC of the firmware bytes, with sector numbers from 0. That is
what the new packet code needs. `tests/unit/test_ble_ota.c`
(`the_update_tool_format_is_accepted`) sends data with the same steps as `sendFwData()`.
The page also works with firmware 1.0.1.

| # | Where | What | Importance |
| --- | --- | --- | --- |
| W1 | `js/main.js:460-466` | 4 seconds after the transfer the page shows "flash successful", also when the device did not connect again and the firmware was not accepted. With this firmware a failed transfer shows "Error" on the device, and the page shows success. | **Must fix** |
| W2 | `js/main.js:400-409` | The result of the "accept" command is not used. The page shows "firmware accepted" also when the device answered "not accepted". | **Must fix** |
| W3 | `js/main.js:400` | The accept is sent only when the transfer was made in the same page session. If the page was closed or loaded again, a device that shows "Verifying..." is never accepted, and it goes back to the old firmware at the next start. Fix: send "accept" at each connect. The device answers "nothing to accept" when no firmware waits. | **Must fix** |
| W4 | `js/main.js:256`, `:352-359`, `:553-570` | The firmware file is not checked. The page shows "header OK" for each file. A wrong file (a bootloader, a full flash picture, a file for an other chip, a file that is too large) is sent for minutes and refused by the device at the end. Check before the transfer: first byte `0xE9`, chip id 12 at offset 12, application mark `0xABCD5432` at offset 32, project name `stattrak` at offset 80, size up to 1 MB. `tools/build.py --check-file` does the same checks. | Should fix |
| W5 | `js/ble.js:374-384` | A sector that the device refuses (CRC error) stops the full update. The device can take the same sector again now. Try a sector 3 times. | Should fix |
| W6 | `js/ble.js` (`update`) | The "stop" command is never sent. After a failure the device waits 15 s before it shows "Error". Send "stop" when the transfer fails or the user cancels. | Should fix |
| W7 | `main.html:25-27`, `:279-280` | The button "Offline flasher" opens `tridos.design` (a placeholder). "Privacy" and "Terms" have no target. | Should fix |
| W8 | repository | The page is not published. Web Bluetooth needs `https://`. The start file is `main.html`, not `index.html`. The planned address is `https://stattrak.tridos.design` (the firmware documents use it already). | **Must do** |
| W9 | `js/main.js` | After the update the page does not read the version again. It cannot say "the device now has version X". | Nice to have |
| W10 | `main.html` | The text does not say that the device can switch off after the transfer and must be switched on by hand (see C2), and that the page must stay open until "Complete". | Nice to have |
| W11 | `js/main.js` | No warning when the file is not an official release. A line "this is not an official firmware, the risk is yours" is good for users who got a file from an other person. | Nice to have |
| W12 | `fonts/`, `README.md` | The font files are a paid font (see D2). The `README.md` has one line. No license file. | Before public |

Not found: a problem with the packet sizes. The page writes 252 bytes per packet. If the
connection allows less, the browser splits the write, and the firmware puts the parts together
(tested in `tests/unit/test_ble_ota.c`).

For Bluetooth user features (`docs/BLUETOOTH.md`), the page has `sendCustomCmd()`. It reads
the answer in the format of the update commands. A user feature page needs the status byte:
see the example in `docs/BLUETOOTH.md`.

## F. Review of the `dev` branch (commit `716b890`)

The commit is good work and it is merged: it closes A2 in a better way than the first fix of
this branch, and it closes B7, B10, C4 and C5. All its parts were read. The parts that a
computer can run were tested (packet code, start values, battery, view change, table reads).
The update task in `ota.c` was read only.

| # | What | Result |
| --- | --- | --- |
| F1 | "Critical battery shutdown also active during OTA." The device switched off 60 s after it read a critical battery, also in the middle of a firmware transfer. | **Not taken.** A device that reads "critical" by mistake (a wrong battery type setting, see A4; a fault in a user firmware) could never get a new firmware: a transfer takes longer than 60 s. Release 0.0.12 switched this off on purpose. The power-off stays off while firmware is received. Two simulator tests found the problem (`test_update_mode_is_reachable_with_one_bad_value`, `test_no_automatic_power_off_during_a_transfer`). A transfer that stops because the battery is empty does no harm. |
| F2 | A new "start" while bytes of the transfer before are still in the ring buffer: these bytes are written as the start of the new firmware (B8). | Note. The image check at the end refuses the result. No brick risk. |
| F3 | `Ota_deinit()` did not stop the packet code. After a view change in the middle of a transfer, the packet code stayed "in progress". | Small fix added: `Ota_deinit()` calls `BleOta_abort()`. |

## Test results

Unchanged firmware (`b51fa0c`), with the new tests:

| Test group | Result |
| --- | --- |
| Normal functions (57 tests) | 57 pass |
| Bad flash values and random use | 46 crashes, 4 failures |
| Bluetooth packet code | stops at `assert(0)`; with the memory checker: buffer overflow |
| Firmware build on a clean computer | fails (`dependencies.lock`) |

This branch:

| Check | Result |
| --- | --- |
| `python tools/check.py` (memory checker, 32-bit) | all 4 steps OK |
| `python tools/check.py` on Windows with the tools from `.venv` (no memory checker) | all 4 steps OK |
| Firmware build, ESP-IDF v5.4 Docker image, release and debug | OK, see the pull request for the numbers |
| Web simulator in a browser | starts, counts kills, keeps the state |
