# Backup, app data, device log, start picture and live preview

Firmware 1.2.0-b2 and newer. The decisions behind this work are in
`qa/backup-and-data/decisions.md` of the owner's workspace; this file is the technical
description. The web page is the "Apps" tab of `updater/` (the sync page) and the Screen
Designer (`updater/designer/`).

All commands of this file work only in the view Settings > "Apps: SYNC" (status 3 in other
views), like the app manager commands of [APPS.md](APPS.md) section 6. Numbers are little-endian
(low byte first). A command has 16 data bytes, an answer has 13 data bytes at most
([BLUETOOTH.md](BLUETOOTH.md)).

## 1. What is stored where

All data is in the NVS partition (48 KB, `partitions.csv`). The flash has no other free space.
Changing the partition table over Bluetooth can brick a device, so it stays as it is.

| Namespace / key | Content | Written |
| --- | --- | --- |
| `data` / `m<id 8 hex>` | app data meta: u8 version = 1, u8 columns (1..4), u16 value count | flush of the app data |
| `data` / `c<id 8 hex><n 2 hex>` | app data chunk n: 64 values (i32), 256 bytes (the last one can be shorter) | flush of the app data |
| `data` / `log` | device log: u8 version = 1, u8 count, u8 next boot number, u32 CRC-32 of the last firmware version text, then count x 4 bytes (newest first) | once, 5 s after each start |
| `data` / `spic` | start picture: 512 bytes, the frame buffer format (4 pages of 128 bytes, bit 0 = top pixel of the page) | the sync page |
| `data` / `spon` | u8 1 = show the start picture at power-on, 0 = keep it but do not show it | the sync page |

A factory reset erases the NVS partition. The firmware writes the device log back after it (it
has no personal data). The app data and the start picture are gone after a factory reset.
Deleting an app deletes its app data.

## 2. App data (one store for each app)

Each app has its own store. An app reads and writes only its own store.

- Size: **4096 bytes for each app** (1024 values), **8192 bytes for all apps together**. The
  sync page shows the rows used and the rows that fit.
- An app declares 1 to 4 columns (package format 3, section 6). A row = one value for each
  column.
- **Full = stop.** `log()` gives 0 and adds nothing when the store of the app is full, when all
  stores together are full, or when the flash is too full (below).
- **Stats and settings always win.** Before the firmware starts a new chunk, it asks the NVS
  for its free entries (`nvs_get_stats`). Fewer than `APP_DATA_RESERVE_ENTRIES` (160 entries
  of 32 bytes = 5 KB) + the chunk = full. This space stays for the settings, the counters, app
  installs and the NVS itself.
- The rows of the open app are in RAM. The firmware writes the changed chunks at most once in
  30 s, when the app closes and at power-off. A power loss can lose the last 30 s of rows.
- A store keeps the column count of the app that made it. A new app version with an other
  column count gets 0 from `log()` until the store is cleared (nothing is lost without a
  question).
- `log_clear()` (app) and DATA_CLEAR (sync page, the device asks first) delete the rows.

## 3. Device log

5 seconds after each start (with a battery reading) the firmware adds one entry and keeps the
last 64. An entry is 4 bytes:

| Byte | Content |
| --- | --- |
| 0 | why the device started: 0 unknown, 1 power on, 2 restart (software, also after a firmware update), 3 crash, 4 watchdog, 5 brown-out (the supply was too low), 6 wake from sleep, 7 other |
| 1 | battery % (0..100), 255 = USB / no battery / unknown |
| 2 | flags: bit 0 = the firmware version is new since the last entry |
| 3 | boot number (0..255, counts up, wraps) |

The reason of an entry says how the run **before** it ended: "brown-out" after a run means the
battery or the supply dropped in that run.

## 4. Start picture

- No picture (the default): the device starts with the first shown app of the app menu.
- A picture that is on: it shows for 2 s at power-on, then it slides down and the first shown
  app comes in from above (the slide of the app menu, 220 ms). A press of a button skips the
  rest of the 2 s (the press does nothing else).
- A picture that is off: kept in flash, not shown.
- A missing, short or damaged `spic` = no picture. The start picture cannot stop the start
  sequence: the accept view of a pending firmware and the 10 s hold to the update view come
  before it and do not change (`main/main.c`).
- When a picture arrives over Bluetooth, the device shows it for 2 s.

## 5. Bluetooth commands

### 5.1 Transfers: restore, start picture, preview frame

A transfer uses the install steps of APPS.md section 6: XFER_BEGIN (here) in place of BEGIN, then
DATA (`0x0114`), END (`0x0115`) and STATUS (`0x0116`) as for a package. The data goes into the 4 KB
work buffer.

| Kind | Size | Question on the device | DATA order | END does |
| --- | --- | --- | --- | --- |
| 1 restore | 5 .. 4096 | "RESTORE BACKUP?" (hold YES / NO, 30 s) | in order from 0 | checks the whole backup, then writes it (5.4). Answer: u8 values written, u8 values skipped. The display shows "BACKUP RESTORED" and OK |
| 2 start picture | 512 | none | in order from 0 | saves the picture (and "show" = 1). The display shows it for 2 s |
| 3 preview frame | 512 | none | any order; missing parts are 0 | the display shows the frame until a button is pressed or PREVIEW_STOP. Nothing is written to flash |

STATUS answers 2 bytes: u8 state, u8 kind (0 package, 1 restore, 2 start picture, 3 preview,
4 data clear). States as in APPS.md (0 idle, 1 receiving, 2 waiting for the person, 3 done,
4 refused, 5 no answer in 30 s) and 6 = a preview frame shows. Old pages read only the first byte.

END status: 0 ok, 9 bad CRC (CRC-32 of the whole transfer, zlib.crc32; the preview frame: of all
512 bytes, also the parts that were not sent), 8 missing data (restore and picture), 10 bad
backup (u8 reason: 1 magic, 2 version, 3 a section is outside the data), 2 flash write failed.

### 5.2 Commands

| Number | Name | Data | Answer |
| --- | --- | --- | --- |
| `0x0120` | CONFIG_INFO | none | u8 config version (1), u8 flash value count, u8 features (bit 0 backup, 1 app data, 2 device log, 3 start picture, 4 preview), u8 start picture (0 none, 1 shown at power-on, 2 kept but off), u16 app data bytes used (all apps), u16 app data size of all apps (8192), u16 app data size of one app (4096), u16 free flash bytes (free NVS entries x 32, at most 65535), u8 device log count |
| `0x0121` | CONFIG_ENTRY | u8 index, u8 part | part 0: u32 value, u32 largest valid value, u8 key length. Part 1: key bytes 0 .. 12. Part 2: key bytes 13 .. 14. Status 4 = no value at this index |
| `0x0122` | APP_VALUES | u32 id, u8 first | u8 saved value count of the app, then up to 3 i32 from `first`. Status 2 = not an installed app |
| `0x0123` | XFER_BEGIN | u8 kind, u16 size, u32 crc | 5.1. Status 2 = bad kind or size, 11 = the device asks the person something now |
| `0x0124` | START_PIC | u8 op (0 read, 1 show at power-on, 2 do not show, 3 delete, 4 read pixels), u16 offset (op 4) | ops 0..3: u8 state (as CONFIG_INFO). Op 4: 12 bytes from `offset` (less at the end). Status 2 = no picture (ops 1, 2, 4) or a bad op |
| `0x0125` | PREVIEW_STOP | none | the preview ends; the display shows "Waiting..." |
| `0x0126` | DATA_INFO | u32 id | u8 columns (of the store when it has rows, else of the app; 0 = the app has no data store), u16 rows, u16 rows that fit now (this app's size or what is left of all, the smaller one), u8 flags (bit 0 full, bit 1 the store was made by an app version with an other column count: the column names of DATA_COLUMN do not fit it). A built-in app: all 0. Status 2 = unknown id |
| `0x0127` | DATA_COLUMN | u32 id, u8 column | the column name (1 to 12 bytes). Status 2 = unknown id or column |
| `0x0128` | DATA_READ | u32 id, u16 first value | u8 count (0..3), then count x i32: values `first` .. (value = row x columns + column) |
| `0x0129` | DATA_CLEAR | u32 id | the device asks "DELETE <NAME> DATA?" (state 2, kind 4). YES = the rows are deleted, state 3. Status 2 = unknown id or no data |
| `0x012A` | LOG_READ | u8 first (0 = newest) | u8 entry count, then up to 3 entries (4 bytes each, section 3) from `first` |

Commands that change something (XFER_BEGIN, START_PIC ops 1..3, PREVIEW_STOP, DATA_CLEAR) get
status 11 while the device asks the person a question.

### 5.3 The backup file (made by the page)

A JSON file, `stattrak-backup-<date>.json`:

```json
{
  "type": "stattrak-backup", "version": 1,
  "created": "2026-11-15T12:00:00.000Z", "firmware": "1.2.0-b2",
  "stats": { "total_kills": 120, "total_deaths": 30, "game_kills": 4, "game_deaths": 1 },
  "settings": { "battery_type": 2, "brightness": 8, "...": 0 },
  "apps": [ { "id": 1, "name": "KILLS", "builtin": true, "hidden": false },
            { "id": 2864434397, "name": "RUNNER", "version": 3, "hidden": false,
              "settings": [1, 0], "values": [12, 0, 7] } ],
  "deviceName": "MIKUS",
  "startPicture": { "show": true, "pixels": "<base64 of the 512 bytes>" },
  "deviceLog": [ { "reason": 5, "battery": 41, "firmwareChanged": false, "boot": 17 } ]
}
```

`settings` has all flash values of CONFIG_ENTRY except `init` and the 4 stats, by key name.
`startPicture` is `null` when the device has none. `deviceLog` is never written back.

### 5.4 The restore data (kind 1)

```
"STBK" (4 bytes), u8 version = 1, then sections until the end:
  u8 type, u16 length, length bytes
type 1 VALUE        u8 key length (1..15), key, u32 value       (one section for each value)
type 2 APP_ORDER    u8 count, count x (u32 id, u8 hidden)        (all apps of the device, each one time)
type 3 APP_SETTINGS u32 id, u8 count, count x u8
type 4 APP_VALUES   u32 id, u8 count, count x i32
type 5 DEVICE_NAME  u8 length (0..12), the name part
type 6 START_PIC    u8 show (0 delete the picture, 1 on, 2 off), then 512 bytes (not for 0)
```

The firmware first walks all sections. A section outside the data = status 10, nothing is
written. Then it writes, and **skips** (counts in "skipped") each value that it cannot use:

- VALUE: an unknown key, `init`, or a value above the largest valid value.
- APP_ORDER: not exactly the apps of the device (then the order and the hidden flags stay).
  Hidden flags: shown apps first, then hidden ones; the last shown app is never hidden.
- APP_SETTINGS: an app that is not installed, an other setting count; each value above its range.
- APP_VALUES: an app that is not installed, or an other saved value count.
- DEVICE_NAME: not a valid name part (APPS.md, DEVICE_NAME).
- An unknown section type.

Each written value counts in "written". The stats are replaced, not added. After the writes the
firmware uses the new settings at once (brightness, battery type, ...).

The page leaves out the parts that the person unticked. After the restore the page reads the
device again and lists each value that is not the same as in the file.

## 6. Package format 3 (apps that use these features)

Format 3 is format 2 (10 handlers) with these changes. The compiler writes format 3 only for an
app that uses a feature of this section; such an app needs engine version 2 (firmware 1.2.0-b2).
Older firmware refuses it (`bad version`).

- Header byte 37 (reserved in formats 1 and 2) = data column count, 0 .. 4.
- Permissions: bit 2 `data` (the app has a data store), bit 3 `awake` (auto-off waits while the
  app is the open view; dim, the battery warnings and the critical power-off still work).
  Columns > 0 needs `data`, `data` needs columns > 0.
- The first `columns` strings of the string table are the column names: 1 to 12 characters.

New system calls (engine version 2):

| Id | Call | Permission | Notes |
| --- | --- | --- | --- |
| 26 | `log(a, b, c, d)` | data | adds one row: the first `columns` arguments (the compiler fills missing ones with 0). Gives 1 = added, 0 = full or not allowed. Not in `draw` (gives 0) |
| 27 | `log_count()` | data | the rows in the store |
| 28 | `log_get(row, column)` | data | a value, 0 outside the rows / columns |
| 29 | `log_clear()` | data | deletes all rows of this app. Not in `draw` |
| 30 | `battery_mv()` | none | the battery voltage in millivolts, -1 for USB / no battery |

Script lines (`tools/app.py`): `data "NAME" "NAME" ...` (1 to 4 columns, sets the `data`
permission) and `permission awake`. `log()` takes one value for each column.

The example apps `apps/battery-log/` (BATTERY LOG: minutes and millivolts, each 1, 2 or 5
minutes; about 8 hours at 1 minute) and `apps/clip-marks/` (CLIP MARKS: the time since START,
the clip number and the kill count for each kill) use these features.
