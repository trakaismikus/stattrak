# StatTrak Web Updater

A web page that sends a firmware file to a StatTrak device over Bluetooth, and that manages the
apps on the device (the tab "Apps").

- The page is `index.html`. It has no build step and no packages.
- The browser must have Web Bluetooth: Chrome, Edge or Opera, on a computer or on Android.
- Web Bluetooth works only on `https://` addresses and on `localhost`.

## How an update works

A transfer that is complete is not an update that is complete. The page follows the device
through all steps:

1. **Pair.** The device shows its update screen. The page reads the firmware version and the device ID.
2. **Select a file.** The page refuses a file that is not a StatTrak firmware (wrong chip, a
   bootloader, an other project, too large). It also tells if the file is one of the official
   releases: it compares the SHA-256 of the file with the release files on GitHub (the repository
   is set at the top of `js/main.js`).
3. **Transfer.** First the page confirms a firmware of an earlier transfer that still waits on
   the device. Thus a "yes" after the transfer can only come from the new firmware. Then the file goes to the device in sectors of 4096 bytes, each with a checksum.
   A sector that arrives damaged is sent again, 3 times at most. If the transfer stops, the page
   tells the device to stop, and the device keeps its old firmware.
4. **Wait for the device.** The device checks the full file and restarts. On some devices the
   display stays off after the restart, and the user must switch the device on.
5. **Confirm.** The new firmware shows "Verifying…" and waits. The page connects again and
   confirms it. Only then the page shows "Flash complete". Without the confirmation the device
   goes back to the old firmware at its next start.

What the page shows when a step fails:

| What occurred | Message |
| --- | --- |
| The device refused the file after the transfer (its display shows "Error") | The device refused the file |
| The new firmware did not start, and the old firmware came back | The new firmware did not start |
| The device did not come back in 5 minutes | No answer from the device |
| The new firmware did not answer the confirmation in 6 minutes | No answer from the device |
| The connection was lost during the transfer | The transfer stopped |
| An answer of the device was lost, and the page cannot know the result | Look at the device display |

If the page was closed or loaded again before the confirmation, it keeps a mark of the open
transfer in the browser (`localStorage`). The next pairing with the same device confirms the
firmware. For a transfer that an other browser made, the button
**Display shows "Verifying…"?** does the same.

The page does not send a confirmation at each connection: firmware 1.0.1 shows "Accepted" on
its display for each confirmation command, also when no new firmware waits.

## The Apps tab

`index.html#apps`. The person opens Settings > "Apps: SYNC" on the device, holds button 2, and
clicks **Connect**. Then the page shows the apps of the device in menu order (SETTINGS is always
last) and the storage of the 24 KB for apps. The person can show or hide an app, move it (drag,
arrows, or the arrow keys on the handle), change its settings, and delete it (the page asks in the
row, not in a browser dialog). Under "Add apps" are the apps of `apps/catalog.json` (made by
`python tools/app.py catalog`; do not change these files by hand) and **From file** for a
`.stapp` file.

An install goes in this order (`docs/APPS.md` section 6):

1. The page sends BEGIN and ASK. The device asks "INSTALL <NAME>?" and the page shows that
   screen: hold button 2 = yes, hold button 1 = no (no answer in 30 seconds = no). No data goes
   before the "yes".
2. After the "yes" the page sends the package and shows the progress. The device checks it and
   writes it to flash. A hold of button 1 on the device, or 10 seconds without data, stops the
   transfer ("The transfer stopped on StatTrak").
3. "Installed": the device shows APP INSTALLED with SYNC MORE (button 1: wait for the next app)
   and OPEN (button 2: the app opens). OPEN turns Bluetooth off, so the page loses the
   connection: it then shows a calm message ("<NAME> is installed. The connection ended."), not
   "Connection lost". The page watches for this 60 seconds after the install (each STATUS keeps
   the device awake); after that a lost connection is "Connection lost" again.

A package of format 2 (an app with `down1` / `down2`, e.g. RUNNER) needs a firmware that reads
format 2 (INFO). For an older firmware the page says "Update the firmware first" and sends
nothing.

**Bluetooth name.** When the firmware has the command DEVICE_NAME, the tab shows the Bluetooth
name of StatTrak in "Apps: SYNC" and can change it (1 to 8 characters 0x20 .. 0x7E, no space at
the start or the end) or set it back to "StatTrak". StatTrak uses a new name the next time the
person opens Apps: SYNC. The update view always uses "StatTrak". The Apps tab finds a StatTrak
with any name: its device chooser also looks for the service UUID that the device advertises
(`03b80e5a-ede8-4b33-a751-6ce34ec4c700`). This is the standard number of the Bluetooth MIDI
service, so the chooser can also list MIDI keyboards near the computer. A browser that refuses
that filter gets the name filter "StatTrak" only. The firmware tab looks for the name "StatTrak"
only.

- The tab has its own Bluetooth connection and sends only the app commands of `docs/APPS.md`
  section 6, never a firmware update command (a test checks this).
- The tabs do not change while a firmware update or an app install runs. Opening the Apps tab
  closes a connection of the firmware tab: the device cannot show the update screen and
  "Apps: SYNC" at the same time.
- An old firmware (no app commands): "Update the firmware first", with a link to the firmware tab.
  A device that is not in "Apps: SYNC" (status 3): the page says what to do on the device.
- The page checks a `.stapp` file (magic, format 1 or 2, size, CRC-32) before it sends it. The
  device does the full check.
- The device has no command for the title picture or the setting names of an installed app. The
  page knows them for catalog apps and for a file installed in this page session; other apps show
  their name as the picture, and their settings are changed on the device.

## Files

| File | What it is |
| --- | --- |
| `index.html`, `css/` | The page |
| `js/ble.js` | The Bluetooth code: connect, commands, firmware transfer |
| `js/firmware.js` | Checks of the firmware file |
| `js/main.js` | The steps of the page |
| `js/apps-protocol.js` | The app commands (`StatTrakApps`: install, list, Bluetooth name, the device chooser of the Apps tab) and the check of a `.stapp` file (`StappFile`) |
| `js/apps-pictures.js` | Title pictures, and the device display in "Apps: SYNC": a port of `_draw()` of `../main/views/app_sync_view.c`, pixel by pixel |
| `js/apps.js`, `css/apps.css` | The tabs and the Apps tab |
| `apps/` | The app catalog: made by `python tools/app.py catalog`, not by hand |
| `tests/fake-device.js` | A stand-in for the device and for Web Bluetooth. Its packet code is a port of the firmware code in `../components/ble_ota/`; its app commands are a port of `../main/ble_commands.c`, `../main/apps.c` and `../main/views/app_sync_view.c` (with the buttons of "Apps: SYNC"); its device chooser uses the filters (name, service UUID) |
| `tests/ble.test.js` | Tests of `js/ble.js` and `js/firmware.js` in Node.js |
| `tests/page.test.js` | Tests of the full page in a headless Chrome. They click the buttons as a user does |
| `tests/apps.test.js` | Tests of `js/apps-protocol.js` in Node.js, each rule of `docs/APPS.md` section 6 against the stand-in device (install order, ASK, DATA only after "yes", a new BEGIN, the transfer stop, SYNC MORE / OPEN, DEVICE_NAME, format 2), and the device screens of `js/apps-pictures.js` against `tests/sync-screens.json` |
| `tests/sync-screens.json` | Screens of "Apps: SYNC" from the firmware (the headless simulator, `../tests/stattrak_sim.py`). Make them again after a change to `_draw()` of `app_sync_view.c` |
| `tests/apps-page.test.js` | Tests of the Apps tab in a headless Chrome: connect, list, hide, move, delete, settings, install (the question first, then the transfer, then "Installed"), no, no answer in 30 s, the transfer stopped on the device, OPEN (a calm message), SYNC MORE, a lost answer, no space, format 2 on an older firmware, the Bluetooth name, a renamed device, old firmware, not in "Apps: SYNC", bad files, lost connection, the tab lock |
| `tests/files.test.js` | Tests of the fonts, of files from other sites, and that the Bluetooth numbers of the page are the numbers of the firmware |

## Run it on your computer

```
npx --yes http-server updater -p 8765 -c-1
```

(Run it in the main folder of the repository.)

Then open `http://localhost:8765`.

## Tests

```
cd updater
node --test "tests/*.test.js"
```

This needs Node.js 22 or later, and Chrome, Chromium or Edge for the page tests (without a
browser these tests are skipped). There is nothing to install.
`SCREENSHOTS=<folder>` keeps a picture of the page at the end of each page test.

The tests use a stand-in device. **They do not show that an update works on a real device.**
After a change to `js/ble.js` or to the update steps in `js/main.js`, do an update of a real
device before the page is published.
After a change to the Apps tab, install (yes, no, OPEN, SYNC MORE), move, hide and delete an
app, and rename the device, on a real device in "Apps: SYNC" before the page is published.

## Publish

The page is a folder of static files. Vercel publishes the folder `updater/` of this repository:

- A push to `main` goes to the public address `https://stattrak.tridos.design`.
- A push to an other branch gets a preview address, which only the team can open.
- `.vercelignore` keeps the tests out of the published files.
