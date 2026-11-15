# TITAN: a shot counter for a GATE TITAN II Bluetooth

**Status: proof of concept, branch `feat/titan-app`.** It is not in the official firmware. It
was tested in the simulator only. The live test on a gun is at the end of this page.

TITAN is a built-in app of the StatTrak. It connects to a GATE TITAN II Bluetooth (an electronic
trigger unit, "ETU", in an airsoft replica) and shows:

- the rounds left in the magazine (an estimate), or the shots of the session,
- the selector position (SAFE, SEMI, AUTO) and the gun battery voltage,
- alerts: LOW BATT, FIRE LOCKED, LOW AMMO, MAG EMPTY, LINK LOST.

StatTrak sends only the PIN to the gun, and reads. It does not change settings of the gun and it
cannot fire. Use GATE Control Station (GCS) for the gun settings.

The source of the protocol: the GATE Public API,
https://help.gatee.eu/space/GHC/4006150145 . The Titan needs firmware `v0.2-02` or newer.

## Use it

### Pair (one time)

You need the serial number and the PIN of the Titan. Both are on the label of the Titan cable
and on the PIN card in the box. GCS also shows a saved PIN (menu, your user name, the device).

1. Switch the gun on. Open the app menu (both buttons), select **TITAN** (lightning picture),
   open it (both buttons).
2. **PAIR / NOT PAIRED**: hold button 2 (SEARCH). StatTrak searches for 10 seconds.
3. **PICK YOUR TITAN**: the Titans near you, the strongest signal first. Button 1 = the next
   one. Hold button 2 (PICK) when you see your serial number.
4. **PIN FOR <serial>**: the letter wheel, one place at a time (6 places).
   - Button 1 = the next letter (A to Z, then 0 to 9).
   - Hold button 1 = the previous letter (it repeats while you hold).
   - Hold button 2 (OK) = this letter is correct, go to the next place.
   After the 6th place, StatTrak connects and sends the PIN.
5. **WRONG PIN**: the wheel comes back with your letters. Hold button 2 on the correct letters
   and change only the wrong one.
6. **GUN BATTERY**: button 1 = the next type, hold button 2 = OK. This sets the LOW BATT
   voltage.

After this, TITAN connects by itself each time it opens (**TITAN <serial> / SEARCHING...**).
If the PIN changes in GCS, TITAN shows WRONG PIN and the wheel again.

### Play

```
 87          /120     <- rounds left (big) / magazine size, or SHOTS
             SEMI     <- selector
             7.6V     <- gun battery
             NO MAG   <- a sign: LOCKED, LOW BATT, NO MAG, LOW AMMO (blinks)
```

- **Button 1 = reload**: the magazine is full again (only with a MAG SIZE).
- TITAN also refills by itself when the ETU resets its BB counter, or when the mag sensor sees a
  new magazine (only if the gun has a mag sensor).
- **Button 2 = SESSION**: shots, reloads and the time of the session. Hold button 2 there (NEW)
  = a new session. Button 1 or 2 = back to PLAY.
- **Both buttons** = the app menu. The radio goes off.
- `~` in front of the number: shots can be missing (the connection was lost, or the app was
  closed). It goes away at the next reload.
- At 0 the number stays 0 and EMPTY blinks.

### Alerts

An alert fills the display, blinks and beeps one time (the Sound setting applies). Any button
closes it. It comes again only when the problem stops and starts again. It closes by itself
when the problem stops.

| Alert | When |
| --- | --- |
| LINK LOST | No connection for 5 seconds. TITAN tries to connect again all the time. |
| LOW BATT | The gun voltage is under the limit of GUN BATT for 3 seconds without a break. Short drops while you fire do not count. The ETU sends the voltage only when a battery type is set in GCS. |
| FIRE LOCKED | The ETU says that it does not fire. |
| MAG EMPTY | Rounds left = 0 (with a MAG SIZE). |
| LOW AMMO | Rounds left at or under AMMO ALERT (with a MAG SIZE). |

LOW BATT limits: 2S LIPO 7.0 V, 3S LIPO 10.5 V, 2S LIFE 6.2 V, 3S LIFE 9.3 V, 8.4 NIMH 7.7 V,
9.6 NIMH 8.8 V, OFF = no alert.

### Settings

In the app menu, select TITAN and **hold button 2**. No radio in this view.

| Setting | Values | Default |
| --- | --- | --- |
| MAG SIZE | OFF (count up), 10 to 250 in steps of 10 (count down). A long hold goes 50 at a time. | OFF |
| AMMO ALERT | OFF, 10 %, 20 %, 30 % of MAG SIZE | 20 % |
| GUN BATT | the 7 types above | from the pairing |
| TITAN | the paired serial number. Hold button 2 = forget it (with a question). | |

Button 1 / 2 = previous / next row. Hold button 1 / 2 = previous / next value.

## What it cannot do

- It cannot read the magazine size or the battery type that is set in GCS. The GATE API has no
  command for it. So MAG SIZE and GUN BATT are StatTrak settings.
- It cannot show error codes or clear errors. The GATE API has only "fire locked".
- The count is an estimate. It counts what the ETU reports as fired. Shots during a lost
  connection or while the app is closed are not counted (the `~` sign).
- It does not know about hits. The kill counter apps stay separate.

## How it works (for developers)

| Part | File |
| --- | --- |
| Screens: pairing, PIN wheel, PLAY, SESSION, alerts | `main/views/titan_view.c` |
| Settings view | `main/views/titan_setup_view.c` |
| Logic: pairing data, connection steps, count, alerts, flash | `main/titan.c` |
| GATE messages: frames, CRC-8, INFO_0_API, names | `main/gate.c` |
| Radio: NimBLE in the central role | `components/gate_ble/` |
| Simulator stand-in: fake Titans | `sim/host/host_gate.c` |
| Tests | `tests/test_titan.py`, `tests/unit/test_units.c` (`gate_*`), `tests/test_radio_start.py` |

- **StatTrak is the "phone" side here** (the BLE central). It does not advertise, so nobody can
  connect to it or send firmware while TITAN is open. The firmware update code
  (`components/ble_ota`, `components/app_ble`) is not changed.
- The radio is on only while the TITAN view is open. It starts with the soft radio start
  (`main/views/bt_start.c`), and `GateBle_stop()` runs in the deinit of the view. The way from
  TITAN to the update view goes through the app menu and Settings, so the two never run at the
  same time. The simulator counts it if they do (`gate_overlap`, must stay 0).
- The connection: search (active scan; the name is `TTN2B BT <serial>`), connect, find the GATE
  service `a64379fd-...`, the characteristics Commands `31a399e4-...` and INFO_0_API
  `3adc82fc-...`, switch their notifications on, send the PIN command `0xB0` (PIN + serial).
  The ETU answers `OK` or `ER`. INFO_0_API comes as a notification at each change, and TITAN
  reads it each 2 seconds for the voltage.
- CRC-8 of a frame: CRC-8/SMBUS (polynomial 0x07, start 0). The GATE document does not name it;
  it matches all 13 example frames of the document (unit test).
- Shots: INFO_0_API bytes 4-5 are "BBs fired since the last notification". Only notifications
  count; a read gives the last value again.
- Flash: NVS namespace `titan`, blob `cfg` (32 bytes, version 1): serial, PIN, MAG SIZE,
  AMMO ALERT, GUN BATT, rounds left, session shots, reloads, session time. Written at pairing,
  at a settings change, at a new session and when the app closes. Never for each shot.
- Built-in app id 4 (`APPS_ID__TITAN`). Devices that get this firmware get TITAN in the app
  menu, last before SETTINGS. The web updater shows it as "Built-in", with its name as the
  picture.

## Live test on a gun

The simulator has no radio. These must be tested on a real StatTrak and a real Titan:

1. Install this firmware with the web updater. Then check at once that the update mode still
   works (Settings, `Firmware: UPDATE`, the web updater can pair). (AGENTS.md section 7.)
2. Pairing: the search finds the Titan, the serial number is correct, the PIN is accepted.
   A wrong PIN shows WRONG PIN.
3. The shot count in SEMI: 10 single shots = 10.
4. The shot count in AUTO: one long burst. Compare with a known count (a magazine with a known
   number of BBs, or the GCS statistics).
5. Reload with button 1, and the refill by the mag sensor (if the gun has one).
6. Selector: SAFE / SEMI / AUTO on the display.
7. Voltage: compare with a meter. LOW BATT with a nearly empty battery (or GUN BATT set to a
   type with a higher limit, for example 3S LIPO with a 2S battery).
8. LINK LOST: walk away with the StatTrak (or switch the gun off). Come back: TITAN connects
   again by itself.
9. GCS and StatTrak connected at the same time (GATE documents "Bluetooth multi-connection").
10. Battery use of the StatTrak: one hour with TITAN open.
11. The update mode works after TITAN was open (Settings, `Firmware: UPDATE`).
