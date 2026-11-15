# StatTrak apps

An app is a screen that you install over Bluetooth, like an app on a watch. It is a small file
(`.stapp`, 0.3 to 4 KB) that the firmware checks and then runs in a safe engine. An app can only
draw, read and (with a permission) change the counters, make a click sound, keep its own values
and read its own settings. It cannot reach the firmware update, Bluetooth, the flash or the power.

This page is the specification. It is for AI coding tools and developers.

- Write an app: `apps/<name>/<name>.sts` (StatTrak Script, section 7; the steps: section 8).
- Build and check it: `python tools/app.py build apps/<name>/<name>.sts` -> `<name>.stapp`.
- Try it in the simulator: `python tools/sim.py boot app:apps/<name>/<name>.stapp ...` (installs it
  directly, like a phone would).
- Put it on a device: the "Apps" part of the web updater.

## 1. The parts

| Part | Where | Job |
| --- | --- | --- |
| Engine | `components/stapp/` (`stapp.h`, `stapp_vm.c`, `stapp_check.c`) | Checks a package, runs its code with limits. No ESP-IDF headers. |
| App list and storage | `main/apps.c` | Built-in apps + installed apps, menu order, shown / hidden, the packages and their values in NVS. |
| App host | `main/views/app_host_view.c` | Runs the selected installed app as a view. |
| App settings | `main/views/app_settings_view.c` | The settings of one app (hold the right button on its title picture in the app menu). |
| App sync | `main/views/app_sync_view.c` + setting "Apps: SYNC" | Bluetooth on for the app manager, firmware update refused, install question. |
| Bluetooth commands | `main/ble_commands.c` | `0x0110` to `0x011C` (section 6); the data commands `0x0120` to `0x012A` are in [DATA.md](DATA.md). |
| Compiler | `tools/app.py` | `.sts` source -> `.stapp` package, with the same check as the engine. |
| Catalog | `apps/` -> `updater/apps/catalog.json` + `updater/apps/*.stapp` | The apps that the web updater offers. |

## 2. Limits

| What | Limit |
| --- | --- |
| Package size | 4096 bytes |
| All installed packages together | 24 KB (the NVS partition is 48 KB and also holds the settings) |
| Installed apps | 8. The app menu has at most 12 apps (built-in + installed + SETTINGS). |
| Name | 1 to 14 characters, `A-Z 0-9 space + - % / . :` (shown in upper case) |
| Global variables | 48 (32-bit signed), with the hidden ones of the compiler. The first `saved` of them are kept in flash (at most 16). |
| Settings | 4, each with 2 to 6 values, value texts at most 8 characters, setting name at most 12 |
| Strings | 32, at most 20 characters each |
| Bitmaps | 8, at most 128 x 32 |
| Code | 2048 bytes |
| Steps for one event | 4000 instructions (draw included). More = app error. |
| Stack | 32 values. Call depth 8. |

## 3. Package format (versions 1, 2 and 3)

Format 3 (firmware 1.2.0-b2, engine version 2) adds a data store, the permission `awake` and
the calls `log()`, `log_count()`, `log_get()`, `log_clear()`, `battery_mv()`: see
[DATA.md](DATA.md) section 6. It also adds the background app: the permission `background`, the
events `kill` and `death` (the handler table has 12 entries, the data area starts at 312) and the
call `background()`: see section 4.6. The compiler writes format 3 only for an app that uses them.

Little-endian. All offsets are from the start of the package.

```
Header (48 bytes)
  0  u8[4]  magic "STAP"
  4  u8     format version = 1 or 2 (2 = the handler table also has down1, down2)
  5  u8     engine version needed (1)
  6  u16    permissions (bit 0: changes counters, bit 1: sound)
  8  u32    app id (not 0, not 0xFFFFFFFF; the compiler makes it from the name unless set)
 12  u16    app version
 14  u16    total size (bytes, with the CRC)
 16  char[16] name, 0-padded
 32  u8     global variable count (0..48)
 33  u8     saved variable count (0..16, <= global count)
 34  u8     setting count (0..4)
 35  u8     string count (0..32)
 36  u8     bitmap count (0..8)
 37  u8     reserved = 0
 38  u16    offset of the settings block
 40  u16    offset of the string table
 42  u16    offset of the bitmap table
 44  u16    offset of the code
 46  u16    code size
Title picture: 240 bytes right after the header (offset 48): 80 x 24, Gfx_drawBitmapRaw format
  (3 pages of 80 bytes, bit 0 = top pixel of the page)
Handler table: right after the title picture (offset 288): code offsets (relative to the code
  start) of: open, draw, press1, press2, long1, long2, tick, close (format 1: 8 x u16), then
  down1, down2 (format 2: 10 x u16), then kill, death (format 3: 12 x u16). 0xFFFF = none. The
  compiler writes format 2 only for an app with a down1 or down2 handler; other apps stay format
  1 (older firmware can install them).
Settings block: for each setting:
  char[12] name (0-padded), u8 value count (2..6), u8 default (< count),
  then value count x char[8] (0-padded)
String table: string count x u16 offsets (to 0-terminated strings, max 20 chars)
Bitmap table: bitmap count x (u8 width, u8 height, u16 offset) (data in Gfx_drawBitmapRaw format,
  width * ((height + 7) / 8) bytes)
Code: code size bytes
Last 4 bytes: u32 CRC-32 (IEEE, as zlib.crc32) of all bytes before it
```

All blocks must be inside the package, must not overlap the header, the picture or the CRC.

### 3.1 The check, rule by rule

`Stapp_check` (C) and `tools/app.py check` (Python) check these rules in this order. The first
broken rule gives the result. "Data area" = from offset 304 (format 1), 308 (format 2) or 312
(format 3), after the handler table, to the
start of the CRC.

| Step | Rule | Result if broken |
| --- | --- | --- |
| 1 | Package size 308 to 4096 bytes | `bad size` |
| 2 | Magic `STAP` | `bad magic` |
| 3 | Format version = 1 or 2, engine version needed = 1 .. `STAPP_ENGINE_VERSION`. Format 2: size at least 312 | `bad version` (`bad size` for the 312) |
| 4 | Total size in the header = package size | `bad size` |
| 5 | CRC | `bad crc` |
| 6 | Permissions only bits 0 and 1. Id not 0 .. 255 (0 and the built-in apps 1, 2, 3; 4 .. 255 are kept for later built-in apps) and not 0xFFFFFFFF. Name: 1 to 14 characters of `A-Z 0-9 space + - % / . :`, then only 0 bytes. Counts: globals <= 48, saved <= 16 and <= globals, settings <= 4, strings <= 32, bitmaps <= 8. Reserved byte = 0 | `bad header` |
| 7 | Code size <= 2048 | `bad layout` |
| 8 | Settings block: walk the settings. Each one starts in the data area and its 14 bytes are in it; its value count is 2 to 6 (a wrong count = `bad settings`, here already) | `bad layout` |
| 9 | The 4 blocks (settings, string table, bitmap table, code) are inside the data area and do not overlap each other. A block with count 0 (or code size 0) is not checked | `bad layout` |
| 10 | Each setting: name 1 to 12 printable characters (0x20 to 0x7E) then 0 bytes; default < value count; each value 1 to 8 printable characters then 0 bytes | `bad settings` |
| 11 | Each string: offset in the data area, a 0 byte before the CRC, at most 20 printable characters | `bad strings` |
| 12 | Each bitmap: width 1 to 128, height 1 to 32, data inside the data area | `bad bitmaps` |
| 13 | The code, instruction by instruction from offset 0: opcode known and operands inside the code (`bad code`); LOAD / STORE index < global count (`bad global`); SYS id known (`bad sys`); its permission in the header (`bad sys`); fixed string or bitmap id (below) < count (`bad strings` / `bad bitmaps`) | see the rule |
| 14 | The last instruction of the code is HALT, JMP or RET (so the code cannot run past its end) | `bad code` |
| 15 | Each JMP / JZ / JNZ target and each CALL target is an instruction start (inside the code) | `bad code` |
| 16 | Each handler is 0xFFFF or an instruction start | `bad handler` |

A fixed string or bitmap id: if the 4 instructions right before `SYS text` are all PUSH8 / PUSH16 /
PUSH32, the first of them is the string id (0 <= id < string count). If the 2 instructions
right before `SYS bitmap` are pushes, the first of them is the bitmap id. Other ids are checked
when the code runs (`bad arg`). Strings and bitmap data may overlap other blocks (they are only
read); the 4 blocks of step 9 must not.

### 3.2 Result texts

`Stapp_resultText()`, in the order of `stapp_result_t`: `ok`, `bad size`, `bad magic`,
`bad version`, `bad crc`, `bad header`, `bad layout`, `bad settings`, `bad strings`,
`bad bitmaps`, `bad code`, `bad global`, `bad sys`, `bad handler`, `stack`, `call depth`,
`too many steps`, `bad arg`.

### 3.3 The app id

Without an `id` line, the compiler makes the id from the name: CRC-32 (zlib.crc32) of the name in
upper case (ASCII). If that is 0 .. 255 or 0xFFFFFFFF, the top bit is switched (XOR 0x80000000).

## 4. The engine (bytecode)

A stack machine with 32-bit signed integers. Arithmetic wraps (two's complement). Division or
modulo by 0 gives 0. Shifts use the low 5 bits of the count.

| Code | Name | Operands | Stack |
| --- | --- | --- | --- |
| 0x00 | HALT | | end of the handler |
| 0x01 | PUSH8 | i8 | -> v |
| 0x02 | PUSH16 | i16 | -> v |
| 0x03 | PUSH32 | i32 | -> v |
| 0x04 | LOAD | u8 global | -> v |
| 0x05 | STORE | u8 global | v -> |
| 0x06 | DUP | | a -> a a |
| 0x07 | DROP | | a -> |
| 0x08 | SWAP | | a b -> b a |
| 0x10..0x14 | ADD SUB MUL DIV MOD | | a b -> a?b |
| 0x15 | NEG | | a -> -a |
| 0x16..0x1B | EQ NE LT LE GT GE | | a b -> 0/1 |
| 0x1C | NOT | | a -> (a == 0) |
| 0x1D..0x21 | BAND BOR BXOR SHL SHR (arithmetic) | | a b -> a?b |
| 0x22 | MIN | | a b -> min |
| 0x23 | MAX | | a b -> max |
| 0x24 | ABS | | a -> abs |
| 0x30 | JMP | i16 rel | (from the next instruction) |
| 0x31 | JZ | i16 rel | a -> (jump if a == 0) |
| 0x32 | JNZ | i16 rel | a -> (jump if a != 0) |
| 0x33 | CALL | u16 abs | (code offset) |
| 0x34 | RET | | |
| 0x40 | SYS | u8 id | arguments -> result (each call pushes one value) |

Every handler ends with HALT or a jump. The check (`Stapp_check`) decodes all code from start to
end: each opcode known, operands inside the code, jumps and calls land on an instruction start,
global index < global count, string / bitmap ids that are constants are < count, the SYS id
exists and its permission is in the header (section 3.1 has the exact rules). The engine checks
the stack, the call depth, the step limit and the ids at run time too. A broken rule at run
time = app error.

Run-time details:

- Each instruction is one step, HALT too. The 4001st step of one event = `too many steps`.
- CALL pushes the return offset on a call stack of 8. A 9th CALL = `call depth`. RET with an
  empty call stack = `call depth` (a handler ends with HALT, not RET).
- DIV and MOD work like C: the result goes toward 0, MOD has the sign of `a`.
  -2147483648 / -1 = -2147483648, -2147483648 % -1 = 0. By 0: the result is 0.
- `text` with a string id that is not in the table, `bitmap` with a bitmap id that is not in
  the table = `bad arg`. Other values out of range (a font 7, a click of 50 Hz, a counter 9) do
  nothing and give 0; they are not app errors.

### System calls (SYS)

The arguments are pushed left to right. Each call pushes one result (0 if nothing).

| Id | Call | Notes |
| --- | --- | --- |
| 0 | `pixel(x, y, c)` | c: 0 off, 1 on, 2 invert |
| 1 | `hline(x, y, w, c)` | |
| 2 | `vline(x, y, h, c)` | |
| 3 | `rect(x, y, w, h, c)` | outline (exact size) |
| 4 | `fill(x, y, w, h, c)` | |
| 5 | `rrect(x, y, w, h, filled)` | the round-corner frame of the built-in screens |
| 6 | `text(x, y, str, font, c, align)` | str = string id. font: 0 5x7, 1 5x5, 2 3x5, 3 title. align: 0 left, 1 center, 2 right |
| 7 | `num(x, y, value, digits, font, c, align)` | digits 0 = as many as needed, else 0-padded |
| 8 | `bignum(x, y, value, digits, scale, c)` | the dotted big digits of KILLS (5x7 font, scale 2..4) |
| 9 | `bitmap(x, y, id, c)` | |
| 10 | `invert(x, y, w, h)` | |
| 16 | `counter(id)` | 0 total kills, 1 total deaths, 2 game kills, 3 game deaths |
| 17 | `add_counter(id, delta)` | permission "counters". delta -10..10. Never below 0. Returns the new value. A change of a game counter also changes the matching total counter, like the built-in screens (so an app changes `game_kills` only) |
| 18 | `setting(n)` | the selected value index of setting n |
| 19 | `time()` | milliseconds since start |
| 20 | `click(freq)` | permission "sound". 200..4000 Hz. Follows the Sound setting |
| 21 | `battery()` | battery level 0..100, -1 for USB / none |
| 22 | `random(n)` | 0 .. n-1 (n 1..65535) |
| 23 | `save()` | save the saved variables now (the firmware also saves at close; at most once in 30 s) |
| 24 | `redraw()` | nothing (the screen is drawn every 10 ms); kept for later |
| 25 | `tone(freq, ms)` | permission "sound". One note: 200..4000 Hz for 10..1000 ms. It does not wait; a new note or click replaces the note that plays. Follows the Sound setting |
| 26 .. 30 | `log`, `log_count`, `log_get`, `log_clear`, `battery_mv` | engine version 2, format 3: [DATA.md](DATA.md) section 6 |
| 31 | `background()` | engine version 2. 1 if this app is the background app now (Settings > Background), else 0. Section 4.6 |

Drawing calls work only in the draw handler. In other handlers they do nothing.

How the drawing calls draw (the Python engine of `tools/app.py run` does the same; the firmware
must match it):

- `c` 2 inverts each pixel that the call draws (for `text`, `num`, `bignum`, `bitmap`: each pixel
  of the glyph or picture that is on).
- `hline` / `vline`: `w` / `h` pixels from x, y. `rect`: the outline of exactly w x h pixels.
- `rrect`: `GUI_DRAW_ROUND_RECT` of `main/app_view.h`: the frame without its 4 corner pixels,
  always color 1; `filled` 1 also fills the inside with color 1.
- `text`, `num`: `Gfx_drawStringAligned` with the font, x is the left edge, the center or the right
  edge. `num` writes the value with `%0*ld` (digits 0 = as many as needed, else at least that many,
  with 0 in front; at most 10).
- `bignum`: `Gfx_drawStringScaled(x, y, digits, &font5x7, c, scale, 1)`: the dotted digits of the
  KILLS screens, 6 x scale pixels for each digit, 7 x scale high. Value as in `num`, always left
  aligned.
- `bitmap`: the pixels that are on in the bitmap get color c; the others do not change.
- `invert(x, y, w, h)`: inverts the w x h pixels.

Where calls work: drawing calls only in `draw`. `add_counter` only in `press1`, `press2`,
`long1`, `long2` (in other handlers it does nothing and gives 0; also not in `down1`, `down2`:
a press of both buttons sends them too), at most 4 times in one event
(more calls only give the value). `click` and `tone` do nothing in `draw`.
Each `draw` may draw at most about 8 full screens of pixels; more = app error `too many steps`.

### Events

| Handler | When |
| --- | --- |
| `open` | the app is opened from the menu (after the saved values are loaded) |
| `draw` | each 10 ms, the screen is empty before |
| `press1`, `press2` | short press: about 0.2 s after the release (the firmware waits to see if a second press comes) |
| `down1`, `down2` | the button goes down: at once, no wait (format 2). Use it in fast games. Also the first button of a "both buttons" press comes here before the app menu opens |
| `long1`, `long2` | long press start and each 500 ms while held |
| `tick` | each 100 ms while the app is open (the background app: all the time, section 4.6) |
| `close` | the app is left (both buttons) |
| `kill`, `death` | format 3, only the background app: the game kills / game deaths went up (section 4.6) |

Both buttons are never sent to an app: short = app menu, long = power off.

### 4.6 The background app

One installed app can run in the background: the person chooses it on the device in
Settings > Background (the row after "Apps"). The row is there only when an installed app has the
permission `background`; its values are OFF and the names of these apps. OFF is the default. The
choice is kept in flash (NVS key `bgapp` of the namespace `apps`); a delete of the app and a factory
reset erase it.

- The background app opens at each start of the device (its `open` event) and runs until the
  power goes off. It never draws. It gets `tick` each 100 ms, and `kill` / `death` when the game
  kills / game deaths go up: by a button of a built-in counter screen (KILLS, KILLS + DEAD), or by
  `add_counter()` of an other app. Not for its own `add_counter()`, and not for a -1. One event
  for each change.
- When the person opens the background app from the app menu, the screen shows the same running
  app: no new `open`, the same variables. Leaving it does not close it (no `close`).
- It stops (`close`) while Apps: SYNC or the update mode is open, and starts again (`open`) after
  them. An install can replace it there. OPEN after an install starts it first, so the screen shows
  the background one.
- Only one app runs in the background: the device has RAM for two engines (the app on the screen
  and the background app, about 9 KB with their data stores). A second app with the permission
  waits in the list of the setting.
- `awake` keeps the device on only while the app is on the screen. In the background the app
  runs while the device is on; auto-off still switches it off.
- `add_counter()` works only in the button events, so not in `kill`, `death` or `tick`.
- An app error (for example too many steps in `kill`) stops only the background app; the
  error counts like an error on the screen (3 errors switch the app off).
- A script with `on kill` or `on death` needs the line `permission background`.

## 5. Storage (NVS namespace `apps`)

| Key | Content |
| --- | --- |
| `list` | the menu: version u8 = 1, count u8, then count x (u32 id, u8 flags). Ids 1, 2, 3 = built-in KILLS, KILLS + DEAD, STATS. Flags bit 0 = hidden. SETTINGS is not in the list; it is always last. |
| `p<id 8 hex>` | the package |
| `v<id 8 hex>` | the saved variables (count x i32) |
| `s<id 8 hex>` | the setting values (count x u8) |
| `e<id 8 hex>` | error count (a 1-byte blob). 3 = the app is switched off: it is hidden, also when it was the last shown app (the menu then shows KILLS), and LIST sets the error flag. SET_HIDDEN 0 sets the count back to 0 |

Saved values are written when the app closes, at power-off and by `save()` (at most once in 30 s). A factory reset deletes all apps.

The data store of an app (`log()` rows) is in the namespace `data` ([DATA.md](DATA.md)). A
delete of the app deletes it; a new version of the app keeps it.

A missing or broken `list` = the default list (the 3 built-in apps, shown). An entry whose
package is missing or fails the check is left out of the menu. At least one app before SETTINGS
is always shown: if all are hidden, KILLS is shown.

## 6. Bluetooth commands

Same channel as `docs/BLUETOOTH.md` (characteristic `0x8023`, 16 data bytes in, status + 13
bytes out). The app commands work only in the "Apps: SYNC" view; elsewhere they answer status 3.
This table is what the firmware does (`main/ble_commands.c`, `main/apps.c`,
`main/views/app_sync_view.c`); the fake device of the web updater (`updater/tests/`) must do the
same.

Status codes: 0 ok, 1 unknown command (an old firmware), 2 bad data (an unknown id, a value out of
range, a bad byte), 3 not in "Apps: SYNC", 4 index too large, 5 no space, 6 package too large,
7 too many apps, 8 wrong offset, 9 bad CRC, 10 bad package, 11 the install question is on the
display (any command that changes apps answers 11 until the person answers), 12 built-in app,
13 the last shown app.

| Number | Name | Data in | Answer |
| --- | --- | --- | --- |
| `0x0110` | INFO | | u8 engine version, u8 format version (the newest that the firmware reads: 2), u16 free bytes, u16 max package, u8 app count, u8 max apps |
| `0x0111` | LIST | u8 index | u32 id, u16 version, u16 size, u8 flags (bit 0 hidden, bit 1 built-in, bit 2 has settings, bit 3 error), u8 setting count, 3 chars of the name (use NAME for the full name). Status 4 = index too large |
| `0x0112` | NAME | u32 id, u8 part | 13 bytes of the name from part * 13. Status 2 = unknown id |
| `0x0113` | BEGIN | u32 id, u16 size, u32 crc | crc = CRC-32 of the whole package file. Status 2 for an id 0 .. 255 or 0xFFFFFFFF or a size under 308; 5 no space, 6 too large, 7 too many apps. A new BEGIN stops an install that runs (also an earlier "yes"). An id that is installed already is replaced: it keeps its place in the menu, and it is shown again if errors had switched it off |
| `0x011B` | ASK | 16 bytes: the name field of the package (offset 16) | after BEGIN: the display shows "INSTALL <NAME>?" (STATUS 2). Status 2 without BEGIN or for a bad name. END later refuses a package with an other name or id (status 10) |
| `0x0114` | DATA | u16 offset, 14 bytes | only after "yes" (STATUS 1). Answer: u16 next expected offset (also when it is ok). Status 8 = wrong offset: send again from the expected offset |
| `0x0115` | END | | checks the CRC and the package, then writes it to flash: status 9 bad CRC, 10 bad package (u8 reason = `stapp_result_t`), 8 missing data (u16 expected offset), 2 the flash write failed. Status 0 = installed: the display shows "APP INSTALLED", SYNC MORE and OPEN |
| `0x0116` | STATUS | | u8 install state (byte 2, firmware 1.2.0-b2: u8 kind, [DATA.md](DATA.md) 5.1): 0 idle, 1 receiving (the person said yes: send DATA), 2 waiting for the person, 3 installed, 4 refused, 5 timeout (30 s; the display shows "REFUSED") |
| `0x0117` | DELETE | u32 id | deletes the package and its values. Status 12 for a built-in app, 2 for an unknown id |
| `0x0118` | SET_ORDER | u8 count, 3 x u32 id, u8 part | every part has this layout (16 bytes); part numbers go from 0 in order; ids after the end are 0. The order must have each app of the list one time. Applied when the last part is in |
| `0x0119` | SET_HIDDEN | u32 id, u8 hidden (0 / 1) | hidden 0 also sets the error count back to 0. Status 13 = the last shown app cannot be hidden |
| `0x011A` | SETTING | u32 id, u8 setting, u8 value (0xFF = read) | u8 value. Status 2 = unknown id, setting or value |
| `0x011C` | DEVICE_NAME | u8 0 = read / 1 = write, the name part after "StatTrak | " (12 bytes at most, 0-padded; all 0 = no part) | u8 1 = there is a part, then the part (12 bytes, 0-padded). A part: 1 to 12 characters 0x20 .. 0x7E, no space at the start or the end; else status 2. "Apps: SYNC" is "StatTrak | <part>" (no part: "StatTrak") from its next start. Only "Apps: SYNC" uses it: the update view and the accept view are always "StatTrak". A factory reset erases it |

Install: BEGIN, ASK, then STATUS until the person answers "INSTALL <NAME>?": 1 = yes, 4 = no,
5 = no answer in 30 s. After yes: DATA from offset 0 in order, then END. END checks the package
and writes it to flash once; its status 0 = installed (STATUS 3). Only a hold that starts while
the question shows answers it (hold the right button = yes, hold the left button = no); the button fills from
the left while it is held. While it receives, 10 s without DATA or a hold of the left button stops the
transfer (STATUS 0 again, the display shows "NOT INSTALLED"); the page then starts with BEGIN.

After the install the display shows "APP INSTALLED", the name, SYNC MORE (hold the left button:
wait for the next app) and OPEN (hold the right button: the app opens, and Bluetooth goes off, so the page
loses the connection). A new BEGIN also works without SYNC MORE.

The Bluetooth name of "Apps: SYNC" always starts with "StatTrak": "StatTrak | " and the part that
the person chose (12 characters at most), so a phone or a computer shows it as a StatTrak. The
advertising packet has 31 bytes: a name of 8 characters or less fits next to the flags and the
128-bit service UUID; a longer name moves the UUID to the scan response
(`components/app_ble/app_ble.c`). The Apps tab finds the device by the name start "StatTrak" or
by the UUID; the firmware tab and older pages look for the exact name "StatTrak", so the update
views keep it.

An answer has no length field: a name (NAME) ends at its first 0 byte.

## 7. StatTrak Script (`.sts`)

```
app "ROUND TIMER"
version 1
description "Times each round of a game."   # one line for the web updater
picture icon stopwatch "ROUND" "TIMER"     # or: picture icon <name> "TEXT", or picture bitmap "file.png"
permission sound

setting length "ROUND TIME" "1:00" "1:30" "2:00" "3:00" default 1
setting beep "SOUND" "ON" "OFF"

saved round = 1          # saved variables first
var running = 0
var start = 0
var acc = 0

on press1 {
  if running {
    acc = acc + time() - start
    running = 0
  } else {
    start = time()
    running = 1
  }
  if setting(beep) == 0 { click(1000) }
}

on draw {
  rrect(0, 2, 30, 28, 1)
  text(15, 5, "ROUND", small, 0, center)
  bignum(4, 13, round, 2, 2, 0)
  var t = acc
  if running { t = t + time() - start }
  t = t / 1000
  bignum(38, 6, t / 60, 2, 3, 1)
  ...
}
```

### 7.1 Lines of the app (top level)

Each of these is one line. `#` starts a comment until the end of the line.

| Line | Meaning |
| --- | --- |
| `app "NAME"` | Needed. 1 to 14 characters, `A-Z 0-9 space + - % / . :` (small letters become capitals). |
| `version N` | 0 to 65535. Default 1. |
| `id 0x1234ABCD` | The app id. Default: made from the name (section 3.3). Not 0 .. 255, not 0xFFFFFFFF. |
| `description "TEXT"` | One line (at most 120 characters) for the web updater. Not in the package. Needed for `catalog`. |
| `picture icon NAME "TEXT"` | Title picture: an icon and one text in the title font (section 7.4). |
| `picture icon NAME "LINE 1" "LINE 2"` | An icon and two lines in the 5x5 font. |
| `picture text "TEXT"` / `picture text "LINE 1" "LINE 2"` | Text only, in the middle. |
| `picture bitmap "file.png"` | A PNG file (next to the script), at most 80 x 24, in the middle. A pixel is on if it is light and not transparent. |
| `permission counters` / `permission sound` / `permission counters, sound` | The permissions: `counters`, `sound`, `awake` ([DATA.md](DATA.md) section 6), `background` (section 4.6). A call that needs a missing permission is a compile error. |
| `setting KEY "NAME" "VALUE" "VALUE" ... [default N or "VALUE"]` | A setting (2 to 6 values). `KEY` is the name in the script: `setting(KEY)`. `default` is the number of the value (0 = the first, the default) or the value text. |
| `const NAME = N` | A fixed number. |
| `saved NAME [= N]` | A variable that is kept in flash. |
| `var NAME [= N]` | A variable of the app. |
| `bitmap NAME "file.png"` or `bitmap NAME { "#..#" "...." }` | A bitmap (at most 128 x 32): a PNG file, or rows of `#` (on) and `.` (off), one or more per line. `NAME` is its id in `bitmap(x, y, NAME, c)`. |
| `on EVENT { ... }` | A handler: `open draw press1 press2 long1 long2 tick close down1 down2 kill death`. |
| `func NAME(A, B) { ... }` | A function. Its parameters and variables are hidden globals. |

When no `picture` line is there, the picture is the name in the title font, in the middle.

Start values: a `var` gets its start value each time the app opens (the compiler puts that code
at the start of `on open`; 0 needs no code, the engine starts with 0). A `saved` variable gets its
start value only the first time: if one saved variable has a start value that is not 0, the
compiler adds a hidden saved variable (the last saved one) that is 0 until the first open.

### 7.2 Statements (in handlers and functions)

- `var x = e` or `var x`: a new variable (0 if there is no `= e`). It is a hidden global. The
  variables of handlers share their places (handlers never run at the same time); each function
  has its own places. A name is known from its `var` line to the end of the handler or function.
- `x = e`, `x += e`, `x -= e`.
- `if e { } else if e { } else { }`. `else` can be on the next line.
- `while e { }`. Each pass counts steps: a loop that does not end gives `too many steps`.
- `return` (in a handler: the end of the handler). In a function: `return e`, or `return` = return 0.
  A function without `return` returns 0.
- A call: `click(1000)`, `my_function(1, 2)`. Its result is dropped.

One statement per line, or more with `;` between them. Inside `( )` a line break is ignored.

### 7.3 Expressions

- Whole numbers: `12`, `-5`, `0x1F`. 32 bits with a sign.
- Names: variables, consts, setting keys (their number), bitmap names (their id), and these:
  fonts `text` (5x7), `caps` (5x5), `small` (3x5), `title`; colors `off on invert`; aligns
  `left center right`; counters `total_kills total_deaths game_kills game_deaths`; `true false`.
- A text in quotes, `"GO"`: its string id (the text goes into the string table, at most 20
  characters, 32 different texts). Usually in `text(x, y, "GO", ...)`.
- Operators, from low to high: `||`, `&&`, `|`, `^`, `&`, `== !=`, `< <= > >=`, `<< >>`, `+ -`,
  `* / %`, then `- ! +` in front of a value. `&&` and `||` stop early and give 0 or 1.
- Calls: the system calls of section 4 with their names (`pixel`, `text`, `counter`, ...),
  `min(a, b)`, `max(a, b)`, `abs(a)` (the opcodes MIN, MAX, ABS), and the `func` functions.

Compile errors (with the line number): an unknown name, a wrong number of values, a fixed value
out of its range (for example `click(50)`, `bignum(..., scale 5, ...)`, `add_counter(0, 20)`),
a missing permission, a function that calls itself (also through other functions), functions
inside each other more than 8 deep, more than 48 variables, more than 2048 bytes of code. A
drawing call in a handler other than `draw` is a note (it does nothing there).

### 7.4 The title picture

80 x 24 pixels, as in the app menu (`_drawIconText()` of `main/views/launcher_view.c`):

- One text: the title font (`kFontNightverbBig`). total = icon width + gap + text width, gap = 3
  if icon width + 5 + text width > 74, else 5. x0 = (80 - total + 1) / 2. The icon at
  y = (24 - icon height + 1) / 2, the text at y = (24 - 7 + 1) / 2 = 9.
- Two lines: the 5x5 font, gap 6, text width = the wider line, the lines at y 6 and 13, left
  aligned after the icon.
- Text only (no icon): each line in the middle.
- Wider than 80 pixels: a compile error.

Icons for `picture icon NAME` (pixel art in `tools/app_icons.py`; the first five are the icons of
the firmware):

| Name | Size | |
| --- | --- | --- |
| `crosshair` | 19 x 19 | KILLS |
| `crosshair_small` | 15 x 15 | KILLS + DEAD |
| `skull` | 15 x 13 | KILLS + DEAD |
| `bars` | 17 x 13 | STATS |
| `gear` | 13 x 13 | SETTINGS |
| `stopwatch` | 15 x 17 | |
| `target` | 15 x 15 | |
| `headshot` | 15 x 15 | a head with a crosshair |
| `list` | 15 x 13 | |
| `lightning` | 11 x 17 | |
| `trophy` | 15 x 15 | |
| `runner` | 14 x 17 | the character of RUNNER |
| `cherries` | 15 x 14 | |

## 8. Write an app

1. Make the folder `apps/<name>/` and the script `apps/<name>/<name>.sts` (section 7). The
   examples: `apps/round-timer/`, `apps/headshot/`, `apps/reaction/`, and the sample apps to try
   on a device: `apps/runner/` (a jump game), `apps/slots/` (a slot machine with confetti and a
   song made with `tone`), `apps/wallpaper/` (looks like KILLS, the digits are set with the
   buttons). `tests/test_sample_apps.py` runs them in the simulator.
   Play an app by hand in the web simulator, with one command (step 3 below):
   `python tools/app.py sim apps/runner/runner.sts`.
   An app gets a short press (`press1`) about 0.2 s after the release (the firmware waits to see
   if it becomes a double press). A fast game uses `down1` / `down2`: they come when the button
   goes down (RUNNER does this).
2. Compile and check it:

   ```
   python tools/app.py build apps/<name>/<name>.sts        # -> apps/<name>/<name>.stapp (not in git)
   python tools/app.py check apps/<name>/<name>.stapp      # the full check, the same as the firmware
   python tools/app.py describe apps/<name>/<name>.sts     # header, settings, texts, code, picture
   python tools/app.py picture apps/<name>/<name>.sts      # out/<name>_picture.png (80 x 24, scale 4)
   ```

3. Try it on the computer, without the simulator build. `run` opens the app, does the actions
   and prints the screen (`#` = pixel on):

   ```
   python tools/app.py run apps/round-timer/round-timer.sts press1 wait:5000 frame
   python tools/app.py run apps/headshot/headshot.sts counter:game_kills=12 press2 png:out/hs.png
   ```

   Actions: `press1 press2 long1 long2 down1 down2 tick kill death`, `wait:MS` (a draw each 10 ms,
   a tick each 100 ms), `frame` (print the screen), `png:FILE`, `counter:NAME=N`, `setting:N=V`
   (value number), `random:SEED`, `background:1` (`background()` gives 1). It prints the clicks too. This engine is a Python copy for previews; the
   simulator runs the real one (`python tools/sim.py boot app:...`).

   Play it by hand on the real firmware, in the browser:

   ```
   python tools/app.py sim apps/runner/runner.sts            # add --fresh for a new device
   ```

   It compiles the app, builds the web simulator when it is missing or old (Emscripten or Docker,
   about 1 minute the first time; `tools/websim.py`), and opens the browser. The app is
   installed like with the web updater (the simulator answers "INSTALL?" with yes) and is open:
   the arrow keys are the buttons, LEFT + RIGHT together = the app menu.
4. Put it into the catalog of the web updater: `python tools/app.py catalog` builds every
   `apps/*/*.sts` into `updater/apps/<name>.stapp` and writes `updater/apps/catalog.json`.
5. Tests: `tests/test_app_tool.py` (`cd tests` then `python -m unittest test_app_tool`).

Python code can use the tool directly: `import app` (with `tools/` in `sys.path`), then
`app.compile_file(path)`, `app.compile_source(text)`, `app.check(data)` -> (result, info),
`app.result_text(result)`, `app.describe(data)`, `app.Machine(data)` (`run(event)`, `wait(ms)`,
`frame()`), `app.build_catalog()`.

### 8.1 catalog.json

```
{ "format": "stattrak-apps", "version": 1, "apps": [
  { "id": "cc78e229",                     // 8 hex digits
    "name": "ROUND TIMER", "version": 1, "size": 1086,
    "permissions": ["sound"],             // words: counters, sound
    "description": "Times each round: ...",
    "file": "round-timer.stapp",          // next to catalog.json
    "picture": "AAAA...",                 // the 240 bytes of the title picture, base64
    "settings": [ { "name": "ROUND TIME", "values": ["0:30", "1:00", ...], "default": 1 } ] } ] }
```
