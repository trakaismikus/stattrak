# Bluetooth for your own features

The StatTrak has Bluetooth Low Energy. The official firmware uses it for the firmware update.
You can also use it for features: a phone or a computer sends a **command** to the device, and
the device sends an **answer**. Examples: read the counters on a phone, set a counter from a
computer, let a game on a PC add a kill.

This page is for AI coding tools and developers. A person without coding knowledge can say to
the AI tool: *"I want my phone to read the kill count over Bluetooth. Follow docs/BLUETOOTH.md."*

## The rule that keeps devices safe

The firmware update uses the same Bluetooth service. If a feature breaks it, the device cannot
get new firmware any more. So:

- **Put your code in `main/ble_commands.c`.** Do not change `components/ble_ota/` or
  `components/app_ble/` for a feature.
- Do not change the Bluetooth name, the service and characteristic numbers, the update commands
  or the packet formats. `python tools/check_safety.py` and `tests/unit/test_ble_ota.c` check this.

## How a user command works

```
phone / computer                                StatTrak
      |                                             |
      |  write to characteristic 0x8023:            |
      |  command number + 16 data bytes + CRC  ---> |  Bluetooth task: checks length and CRC,
      |                                             |  puts the command into a queue
      |                                             |
      |                                             |  main loop (each 10 ms): BleCommands_update()
      |                                             |  takes the command, runs your code
      |  <--- notification on 0x8023:               |
      |       status + 13 data bytes + CRC          |
```

Your code runs in the main loop, the same as the views. It can read and write the counters
and the settings and it can change the view, without special care for tasks.

## Add a command

1. `main/ble_commands.h`: add a number to `enum ble_command_e`. Use `0x0100` or larger.
   The numbers below are for the firmware update.
2. `main/ble_commands.c`, function `_handle()`: add a `case`. Read the data of the phone from
   `cmd->data` (16 bytes; the bytes that the phone did not send are 0). Write the answer into
   `response` (13 bytes at most) and its length into `*responseLen`. Return a status.
3. `tests/test_bluetooth.py`: add a test. The simulator plays the phone:

   ```python
   status, data = self.dev.ble_command(0x0101, b"\x05")
   self.assertEqual(status, 0)
   ```

   On the command line: `python tools/sim.py boot ... ble:0101:05`
4. Add a line to the table "Commands" below.
5. `python tools/check.py`

Example: a command that adds kills.

```c
// ble_commands.h
BLE_COMMAND__ADD_KILLS = 0x0101,   // Data: count (1 byte, 1 to 10). Answer: the new total (4 bytes).

// ble_commands.c, in _handle()
case BLE_COMMAND__ADD_KILLS: {
  const uint8_t count = cmd->data[0];
  if(count < 1 || count > 10)
    return BLE_COMMAND_STATUS__ERROR;       // Never trust data from the radio

  const uint32_t total = Eeprom_read(ENTRY_ID__TOTAL_KILLS) + count;
  Eeprom_write(ENTRY_ID__TOTAL_KILLS, total);
  _put32(&response[0], total);
  *responseLen = 4;
} return BLE_COMMAND_STATUS__OK;
```

Rules for a command:

- **Check each value** that comes from the phone: ranges, list positions, lengths. Anybody near
  the device can send any bytes.
- Keep it short. The code runs between two display frames. No waiting, no loops over long data.
- Do not write to flash for each command if the phone can send many (flash wears out).
- More than 16 bytes in, or more than 13 bytes out: use more than one command (for example a
  command with a part number).

## When is Bluetooth on?

Bluetooth uses battery power and it is a door into the device. The official firmware keeps it
off, except in two views: the **update view** (Settings, `Firmware: UPDATE`) and the **accept
view** (after an update). User commands work there too.

A feature that needs Bluetooth at other times switches it on in a view of its own, with the
soft radio start (see "Radio start" below):

```c
#include "bt_start.h"

static void _startBluetooth(void) {
  Ota_init();                   // Bluetooth on. The device is visible as "StatTrak".
  Ota_setUpdateAllowed(false);  // This view is not for updates: refuse new firmware here.
}

static void _init(void) {
  BtStart_begin(VIEW_ID__MY_VIEW, _startBluetooth);   // The radio starts a moment later
}

static void _deinit(void) {
  BtStart_cancel(VIEW_ID__MY_VIEW);
  Ota_deinit();                 // Bluetooth off
}

static void _draw(void) {
  if(!BtStart_draw(VIEW_ID__MY_VIEW, _drawScreen))   // The Bluetooth sign first
    _drawScreen();
}
```

- `Ota_init()` always allows the firmware update again. So the update view and the accept view
  work as before, also after your view was open. Never call `Ota_setUpdateAllowed(false)` in
  those two views, in `main/main.c` or in `main/app.c`.
- **Never go from your Bluetooth view directly to the update view or to an other view that
  uses Bluetooth.** At a view change, the `init` of the new view runs first, and the `deinit`
  of the old view runs 0.3 seconds later. Your `Ota_deinit()` would switch Bluetooth off for
  the new view. Go to a view without Bluetooth first (the normal "both buttons" way to the
  settings does this).
- Without `Ota_setUpdateAllowed(false)`, a person near the user can send a firmware to the
  device while your view is open. There is no password.
- `Ota_init()` starts the Bluetooth software. That takes time and memory. Start it with
  `BtStart_begin()` in `init`, never in `draw`.
- Keep Bluetooth off when the feature does not need it. It uses battery power all the time
  that it is on.

### Radio start

The radio start pulls a current peak for about 0.1 s. The display uses current too: the more
pixels are lit, the more. On a device with a weak battery path, the two together pulled the
supply below the brown-out level (2.51 V). The chip restarted, the power latch let go, and the
device switched off with a blank screen. On one unit, Settings > `Firmware: UPDATE` (a screen with
about 1,700 lit pixels) switched it off in 6 of 10 starts, also with firmware 1.0.1. With the
display off at the radio start: 0 of 15. With 10 % brightness: 0 of 10. The 10 s hold at power-on
(the kill counter, about 850 lit pixels) always worked.

So the update view, the accept view and the app sync start the radio softly
(`main/views/bt_start.c`):

1. The view slides in and shows only a small Bluetooth sign with waves (under 100 lit pixels).
   The radio is still off.
2. When the slide has ended and the display has shown only the sign for a few frames, the
   radio starts (the start function that the view gave to `BtStart_begin()`).
3. 0.4 s later the screen of the view slides down from the top.

`tests/test_radio_start.py` checks that at most 150 pixels are lit from the radio start to
0.4 s after it, for each of the three views and for the 10 s hold. Use the same start in each
new view that switches Bluetooth on.

## The other side: a web page or an app

The device is a Bluetooth LE peripheral with the name `StatTrak`.

| What | Value |
| --- | --- |
| Service | `0x8018` |
| Characteristic for user commands (and for version, serial number, accept) | `0x8023`: write, and subscribe for the answers |
| Packet | 20 bytes: command number (2 bytes), data (16 bytes), CRC (2 bytes). Low byte first. |
| CRC | CRC-16/XMODEM (polynomial `0x1021`, start value 0) of the first 18 bytes |
| Answer | 20 bytes: `0x0003` (2 bytes), the command number (2 bytes), status (1 byte), data (13 bytes), CRC (2 bytes) |
| Status | 0 = OK, 1 = the firmware does not have this command, 2 = the data is not correct. Your commands can use 3 and larger. |

A page in Chrome, Edge or Opera can talk to the device with Web Bluetooth. This is the same
method as the web updater uses:

```js
const SERVICE = 0x8018, COMMANDS = 0x8023;

function crc16(bytes) {
  let crc = 0;
  for (const byte of bytes) {
    crc ^= byte << 8;
    for (let i = 0; i < 8; i++) crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
    crc &= 0xFFFF;
  }
  return crc;
}

async function connect() {
  const device = await navigator.bluetooth.requestDevice({
    filters: [{ name: "StatTrak" }], optionalServices: [SERVICE] });
  const server = await device.gatt.connect();
  const service = await server.getPrimaryService(SERVICE);
  const commands = await service.getCharacteristic(COMMANDS);
  await commands.startNotifications();
  return commands;
}

async function sendCommand(commands, number, data = []) {
  const packet = new Uint8Array(20);
  const view = new DataView(packet.buffer);
  view.setUint16(0, number, true);
  packet.set(data.slice(0, 16), 2);
  view.setUint16(18, crc16(packet.subarray(0, 18)), true);

  const answer = new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("The device did not answer")), 3000);
    commands.addEventListener("characteristicvaluechanged", function listener(event) {
      const value = event.target.value;
      if (value.byteLength < 20 || value.getUint16(2, true) !== number) return;
      clearTimeout(timer);
      commands.removeEventListener("characteristicvaluechanged", listener);
      resolve(value);
    });
  });

  await commands.writeValueWithResponse(packet);
  const value = await answer;
  const bytes = new Uint8Array(value.buffer, value.byteOffset, 20);
  if (crc16(bytes.subarray(0, 18)) !== value.getUint16(18, true)) throw new Error("Bad CRC");
  return { status: bytes[4], data: bytes.slice(5, 18) };
}

// Example: read the counters (command 0x0100)
const commands = await connect();
const { status, data } = await sendCommand(commands, 0x0100);
const numbers = new DataView(data.buffer);
console.log("total kills", numbers.getUint32(0, true), "total deaths", numbers.getUint32(4, true));
```

A web page must come from `https://` or from `localhost`, and `requestDevice()` must start
from a click. iPhones and Firefox do not have Web Bluetooth.

This example follows the code of the web updater (`updater/js/ble.js`). It was not run against a device for this
document: test it with your device.

## Commands

User commands (`main/ble_commands.h`):

| Number | Name | Data from the phone | Answer data |
| --- | --- | --- | --- |
| `0x0100` | GET_COUNTERS | none | total kills (4 bytes), total deaths (4 bytes), game kills (2 bytes), game deaths (2 bytes). The game numbers stop at 65535. |

Add your commands to this table.

App manager commands (the "Apps" part of the web updater, `docs/APPS.md` section 6). They work
only while the device shows Settings > "Apps: SYNC"; in each other view the answer is status 3.
In that view the device refuses new firmware. Numbers have the low byte first.

| Number | Name | Data from the phone | Answer data |
| --- | --- | --- | --- |
| `0x0110` | INFO | none | engine version (1 byte), newest package format (1), free bytes (2), largest package (2), installed apps (1), largest count of installed apps (1) |
| `0x0111` | LIST | index (1) | id (4), version (2), size (2), flags (1: bit 0 hidden, bit 1 built-in, bit 2 has settings, bit 3 switched off for errors), setting count (1), first 3 letters of the name. Status 4 = no app at this index |
| `0x0112` | NAME | id (4), part (1) | 13 letters of the name from part x 13 |
| `0x0113` | BEGIN | id (4), size (2), CRC-32 of the whole package file (4) | status 5 = not enough space, 6 = too large, 7 = 8 apps are installed |
| `0x011B` | ASK | the name field of the package (16) | after BEGIN: the device asks "INSTALL <NAME>?" now, before any data. Status 2 without BEGIN or for a bad name |
| `0x0114` | DATA | offset (2), 14 package bytes | only after the person said yes (STATUS 1). The next expected offset (2). Status 8 = wrong offset: send again from the answer |
| `0x0115` | END | none | status 0: installed (the device shows "APP INSTALLED", SYNC MORE and OPEN). Status 8 + expected offset (2): data is missing. 9 = bad CRC. 10 = bad package + the reason (1, `Stapp_resultText()`); also a package with an other id or name than BEGIN and ASK |
| `0x0116` | STATUS | none | install state (1): 0 idle, 1 receiving (the person said yes), 2 waiting for the person, 3 installed, 4 refused, 5 no answer in 30 s |
| `0x0117` | DELETE | id (4) | status 12 = a built-in app cannot be deleted |
| `0x0118` | SET_ORDER | count (1), 3 ids (12), part (1) | parts 0, 1, 2, ... with 3 ids each; the order is used when all ids are in. All apps of LIST, each one time |
| `0x0119` | SET_HIDDEN | id (4), hidden (1: 0 or 1) | status 13 = the last shown app cannot be hidden. Shown again = the error count is 0 again |
| `0x011A` | SETTING | id (4), setting (1), value (1, `0xFF` = only read) | the value (1) |
| `0x011C` | DEVICE_NAME | 0 = read / 1 = write (1), the name part after "StatTrak-" (12, 0-padded; all 0 = no part) | 1 = there is a part (1), the part (12, 0-padded). The app sync is "StatTrak-<part>" (or "StatTrak") from its next start; the update view and the accept view are always "StatTrak" |

Status 2 = the data is not correct (an unknown id, a value out of range, a command that does not
fit the install state). Status 11 = the person must answer "INSTALL <NAME>?" first: while the
question is on the display, nothing can change the apps.

Reserved for the firmware update (do not use, do not change):

| Characteristic | Number | Meaning |
| --- | --- | --- |
| `0x8022` commands | `0x0001` | Start an update. Data: the length of the firmware (4 bytes). |
| `0x8022` commands | `0x0002` | Stop the update |
| both | `0x0003` | Answer ("ACK") |
| `0x8023` | `0x0001` | Get the firmware version |
| `0x8023` | `0x0002` | Get the serial number of the chip |
| `0x8023` | `0x0003` | Accept the new firmware |
| `0x8023` | `0x0004` | Reject the new firmware (the device goes back to the old one) |
| `0x8023` | `0x0005` | Restart (not in use) |
| `0x8020` firmware data | | Packets: sector number (2 bytes), packet number (1 byte, `0xFF` = last packet of the sector), data. A sector is 4096 firmware bytes (the last sector is filled with zeros) and then the CRC of the firmware bytes (2 bytes). The device answers each sector. |

## What the simulator can and cannot test

- It can test your command code: the real `main/ble_commands.c` runs, and a test plays the phone.
- It can test the views that switch Bluetooth on and off (`state` shows `ota_active` and
  `ota_update_allowed`).
- It has no radio. It cannot test the connection, the range, the speed, the battery use, or a
  web page. Only a real device can.
- The packet code of the update (`components/ble_ota/ble_ota.c`) runs in the unit test
  `tests/unit/test_ble_ota.c`, with data in the format of the web updater.
