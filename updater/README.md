# StatTrak web site

This folder is the StatTrak web site, `https://stattrak.tridos.design`. It has three pages with
one menu (Get started, Firmware, Apps, Designer):

| Page | What it is |
| --- | --- |
| `index.html` | The web updater: sends a firmware file to a StatTrak over Bluetooth (tab "Firmware"), and manages the apps on the device (tab "My device"). Most of this file is about it. |
| `start/index.html` | Get started: what StatTrak is, how to update, install apps, design screens and make your own app. Pictures in `start/img/` (copies of `docs/images/`). |
| `designer/index.html` | The Screen Designer (docs/DESIGNS.md). It also works from a disk, without the internet. |

- The pages have no build step and no packages. They share `css/`, `fonts/` and `assets/`.
- The header (the logo and the menu) is the same on every page: `css/site.css`. The Screen
  Designer has its own tool bar under it.
- A link between the pages names the file (`designer/index.html`, not `designer/`), so the
  links also work when a page is opened from a disk.
- `tests/site.test.js` checks the menu and that each local link and file exists.
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

## My device

`index.html#device` (the old address `#apps` works too). The person opens Settings > "Apps: SYNC" on the device, holds the right button, and
clicks **Connect**. Then the page shows the apps of the device in menu order (SETTINGS is always
last) and the storage of the 24 KB for apps. The person can show or hide an app, move it (drag,
arrows, or the arrow keys on the handle), change its settings, and delete it (the page asks in the
row, not in a browser dialog). Under "Add apps" are the apps of `apps/catalog.json` (made by
`python tools/app.py catalog`; do not change these files by hand) and **From file** for a
`.stapp` file.

An install goes in this order (`docs/APPS.md` section 6):

1. The page sends BEGIN and ASK. The device asks "INSTALL <NAME>?" and the page shows that
   screen: hold the right button = yes, hold the left button = no (no answer in 30 seconds = no). No data goes
   before the "yes".
2. After the "yes" the page sends the package and shows the progress. The device checks it and
   writes it to flash. A hold of the left button on the device, or 10 seconds without data, stops the
   transfer ("The transfer stopped on StatTrak").
3. "Installed": the device shows APP INSTALLED with SYNC MORE (hold the left button: wait for the
   next app) and OPEN (hold the right button: the app opens). All buttons on the display answer a hold
   (0.7 s; the button fills while it is held), never a short press. OPEN turns Bluetooth off, so the page loses the
   connection: it then shows a calm message ("<NAME> is installed. The connection ended."), not
   "Connection lost". The page watches for this 60 seconds after the install (each STATUS keeps
   the device awake); after that a lost connection is "Connection lost" again.

A package of format 2 (an app with `down1` / `down2`, e.g. RUNNER) needs a firmware that reads
format 2 (INFO). For an older firmware the page says "Update the firmware first" and sends
nothing.

**Bluetooth name.** When the firmware has the command DEVICE_NAME, the tab shows the Bluetooth
name of StatTrak in "Apps: SYNC" and can change the part after "StatTrak | " (1 to 12 characters
0x20 .. 0x7E, no space at the start or the end) or remove it (back to "StatTrak"). StatTrak uses a new name the next time the
person opens Apps: SYNC. The update view always uses "StatTrak". My device finds a StatTrak
with any name: its device chooser looks for a name that starts with "StatTrak", and for the
service UUID that the device advertises (`03b80e5a-ede8-4b33-a751-6ce34ec4c700`). This is the standard number of the Bluetooth MIDI
service, so the chooser can also list MIDI keyboards near the computer. A browser that refuses
that filter gets the name start filter "StatTrak" only. The firmware tab looks for the exact name
"StatTrak" only.

- The tab has its own Bluetooth connection and sends only the app commands of `docs/APPS.md`
  section 6, never a firmware update command (a test checks this).
- The tabs do not change while a firmware update or an app install runs. Opening My device
  closes a connection of the firmware tab: the device cannot show the update screen and
  "Apps: SYNC" at the same time.
- An old firmware (no app commands): "Update the firmware first", with a link to the firmware tab.
  A device that is not in "Apps: SYNC" (status 3): the page says what to do on the device.
- The page checks a `.stapp` file (magic, format 1 or 2, size, CRC-32) before it sends it. The
  device does the full check.
- The device has no command for the title picture or the setting names of an installed app. The
  page knows them for catalog apps and for a file installed in this page session; other apps show
  their name as the picture, and their settings are changed on the device.
- **Update available.** An installed app that is older than the same app in the catalog (or in
  the apps of an added script, below) shows "Update available (v1 → v2)" and an **Update**
  button. The update is a normal install of the new package (the device asks first).

## The StatTrak button (header)

`js/devbar.js`. The header of `index.html` shows which StatTrak the page talks to: **Connect**
when none, else the name of the device (the part after "StatTrak | ", or "StatTrak") and the
number of app updates. A click opens a panel: serial, firmware, app storage, the updates (each
with Update), My device, Disconnect. Connect in the panel uses the connection of My device (the
device in "Apps: SYNC"); from the firmware tab it opens My device first. During a firmware
update the button shows the device of the firmware tab. The other pages have a **Connect**
link to `index.html#device`.

## Shared parts (every page)

Use these; do not write the same thing again in a page or in an added script.

- **The header** is the same on every page: the logo, the menu, the StatTrak button
  (`<div class="devbar" id="devBar"></div>`, `js/devbar.js`) and `<!-- site:meta -->`.
  `tests/site.test.js` checks that the headers are the same.
- **Buttons:** only the classes of `css/site.css` (`.btn` + one size `--sm` / `--lg` / `--icon`
  + one kind `--primary` / `--ghost` / `--danger` / `--danger-ghost`). No other button styles in a
  page. The header buttons are `.devbtn` (34 px, the height of `.btn--sm`).
- **The way on the device** ("Settings → Apps: SYNC → Hold right button"):
  `SiteUI.crumbs([...])` (`js/utils.js`), with the boxes `.path-crumb` of `css/site.css`.
- **The device picture:** `<st-device>` (`js/device.js`, `assets/device.webp`). Its own frames:
  `StDevice.anims[name] = (arg) => [{ b: { w: 128, h: 32, d }, ms, press: "" }]` (or a Promise),
  then `<st-device anim="name" arg="...">`; one picture: `el.showBuffer({ w, h, d })`.
- Classes of an added site must not use the names of these files: they start with their own
  prefix (the private site uses `as-`).

## Places for added parts

A site that is built from this folder can add its own parts (an extra tab, a sign-in button,
its styles and scripts) without changes to these files. Each page has HTML comments at the
places: `<!-- site:head -->` (end of `<head>`), `<!-- site:nav -->` (in the menu, after My
device), `<!-- site:meta -->` (right side of the header), `<!-- site:body -->` (after the
scripts); `index.html` also has `<!-- site:views -->` (after the My device view). The site works
without them: they are comments. The added scripts use:

- `AppsTab.api.addTab(name, { tab, view, onShow })`: a tab at `#<name>` (also `#<name>/...`)
  that keeps the "Apps: SYNC" connection when the person goes between it and My device. The
  firmware tab ends that connection, as before.
- `AppsTab.api.addCatalog(source, items)`: apps that the added script can install
  (`AppsTab.api.install(item)`); they also give "Update available".
- `AppsTab.api.subscribe(fn)`: called after each change of the state of My device.
- The browser event `stattrak:device` (`js/utils.js`, `SiteEvents`): `connected`, `flashed`
  (after a confirmed firmware update, with the version before) and `disconnected`, with the
  serial (the device id, as Settings > Serial on StatTrak), the firmware version and the name.
  The page itself sends nothing over the network for it.

### Start picture, backup, app data, device log

Under the app list (`js/apps-data.js`, commands in `js/data-protocol.js`, `../docs/DATA.md`).
The page shows these parts only when the device answers CONFIG_INFO. An older firmware gets one
line: "Update the firmware to use this". While a transfer or an install runs, all buttons are
locked.

- **Start picture.** "Choose a PNG or JPG": the page makes it 128 x 32 black and white (scaled to
  fit, the same aspect, centered; it says "scaled to fit" for an other size). The slider "How dark
  is on" (50 % at the start) sets which pixels are on. The picture shows on the device picture
  before it goes. "Send to device" sends it (transfer kind 2); StatTrak shows it for 2 s. When
  StatTrak has a picture, the page shows it, a switch "Show at power-on" and "Delete start
  picture" (the page asks first, in the page). A link goes to the Screen Designer: Download PNG
  there makes an exact 128 x 32 picture.
- **Backup.** "Download backup" reads all flash values, the apps (order, hidden, settings, saved
  values), the Bluetooth name, the start picture and the device log, and saves
  `stattrak-backup-YYYY-MM-DD.json` (`../docs/DATA.md` 5.3). The apps themselves are not in it.
  "Restore from file" shows a table "On the device now / In the file" with a tick for each part
  (stats, settings, apps setup, Bluetooth name, start picture; all on at the start) and lists the
  apps of the file that StatTrak does not have ("Install these apps first, or restore without
  them"). "Restore" sends the restore data (5.4, transfer kind 1). StatTrak asks "RESTORE
  BACKUP?" (hold button 2 = yes). After it, the page reads StatTrak again and lists each value
  that is not the same as in the file (StatTrak skipped it). The menu order: the apps of the file
  first, in the order of the file, then the other apps of StatTrak.
- **App data.** Each app with a data store: the rows used and the rows that fit, "Download CSV"
  (`<app-name>-data-YYYY-MM-DD.csv`, the column names first; a first column TIME also gets a
  column "TIME mm:ss.mmm") and "Delete data" (StatTrak asks "DELETE <NAME> DATA?").
- **Device log.** One line for each start, newest first: the reason (how the run before it
  ended), the battery, "New firmware". "Download CSV" saves `stattrak-log-YYYY-MM-DD.csv`.

## Screen Designer: Download PNG and Live preview

- **Download PNG** saves the selected screen as an exact 128 x 32 PNG (white = on, black = off),
  for the start picture of the Apps tab.
- **Live preview** connects to StatTrak over Bluetooth (Chrome or Edge; the device in Settings >
  Apps: SYNC) with the code of the Apps tab (`../js/ble.js`, `../js/apps-protocol.js`,
  `../js/data-protocol.js`: plain scripts, so the designer still opens from a disk). It sends the
  selected screen as a preview frame (transfer kind 3: only the parts that are not all 0, then END
  with the CRC-32 of all 512 bytes), and again about 400 ms after the last change, one frame at a
  time. A button on StatTrak ends the preview; the next change shows it again. "Stop" sends
  PREVIEW_STOP and ends the connection. Nothing is written to the flash of StatTrak.

## Files

| File | What it is |
| --- | --- |
| `index.html`, `css/` | The page |
| `js/ble.js` | The Bluetooth code: connect, commands, firmware transfer |
| `js/firmware.js` | Checks of the firmware file |
| `js/main.js` | The steps of the page |
| `js/apps-protocol.js` | The app commands (`StatTrakApps`: install, list, Bluetooth name, the device chooser of the Apps tab) and the check of a `.stapp` file (`StappFile`) |
| `js/data-protocol.js` | The commands of `../docs/DATA.md` (`StatTrakData`: CONFIG_INFO and CONFIG_ENTRY, saved values, the transfers restore / start picture / preview frame, app data, device log), the backup file and the restore data (`BackupFile`), the CSV files (`DataCsv`), the frame buffer format (`FrameBuffer`). Also used by the Screen Designer |
| `js/apps-data.js` | The parts under the app list of the Apps tab: start picture, backup, app data, device log |
| `js/apps-pictures.js` | Title pictures, and the device display in "Apps: SYNC": a port of `_draw()` of `../main/views/app_sync_view.c`, pixel by pixel |
| `js/apps.js`, `css/apps.css` | The tabs and My device |
| `js/devbar.js`, `css/site.css` | The StatTrak button in the header |
| `js/device.js` | The device picture with a live display (`<st-device anim=...>`): step 1 of the firmware update (the way to the update mode on firmware 1.0.1) and the Apps tab (the screens of "Apps: SYNC" from `js/apps-pictures.js`, and `anim="still"` for the new start picture). Its numbers (where the display and the round buttons are) are in `../tools/device_art/README.md` |
| `assets/device.webp` | The device: a render of the 3D model, made by `../tools/device_art/` (also the picture of the web simulator) |
| `assets/sync-path.js` | The display of the firmware of this repository on the way to "Apps: SYNC", from the headless simulator (`../tools/device_art/make_sync_path.py`). The Apps tab plays it before Connect |
| `assets/update-path-1.0.1.js` | The display of firmware 1.0.1 on the way to the update mode, from the 1.0.1 simulator (`../tools/device_art/fw101/make_update_path.py`). Most devices that come to the page have 1.0.1 |
| `apps/` | The app catalog: made by `python tools/app.py catalog`, not by hand |
| `tests/fake-device.js` | A stand-in for the device and for Web Bluetooth. Its packet code is a port of the firmware code in `../components/ble_ota/`; its app commands are a port of `../main/ble_commands.c`, `../main/apps.c` and `../main/views/app_sync_view.c` (with the buttons of "Apps: SYNC"); its device chooser uses the filters (name, service UUID); with `data: true` it has the commands and the transfers of `../docs/DATA.md` (test hooks for app data, the device log, the start picture, the flash values) |
| `tests/ble.test.js` | Tests of `js/ble.js` and `js/firmware.js` in Node.js |
| `tests/page.test.js` | Tests of the full page in a headless Chrome. They click the buttons as a user does |
| `tests/apps.test.js` | Tests of `js/apps-protocol.js` in Node.js, each rule of `docs/APPS.md` section 6 against the stand-in device (install order, ASK, DATA only after "yes", a new BEGIN, the transfer stop, SYNC MORE / OPEN, DEVICE_NAME, format 2), and the device screens of `js/apps-pictures.js` against `tests/sync-screens.json` |
| `tests/sync-screens.json` | Screens of "Apps: SYNC" from the firmware (the headless simulator, `../tests/stattrak_sim.py`). Make them again after a change to `_draw()` of `app_sync_view.c` (`python tests/make_sync_screens.py`); the firmware check `../tests/test_updater_screens.py` fails until then |
| `tests/apps-page.test.js` | Tests of the Apps tab in a headless Chrome: connect, list, hide, move, delete, settings, install (the question first, then the transfer, then "Installed"), no, no answer in 30 s, the transfer stopped on the device, OPEN (a calm message), SYNC MORE, a lost answer, no space, format 2 on an older firmware, the Bluetooth name, a renamed device, old firmware, not in "Apps: SYNC", bad files, lost connection, the tab lock |
| `tests/data.test.js` | Tests of `js/data-protocol.js` in Node.js against the stand-in device: an older firmware, the flash values, the backup file, the restore round trip (order, hidden flags, settings, saved values, name, start picture), the skipped values, only the ticked parts, no / no answer / a bad backup, the table, the start picture, the preview frame (empty parts left out), app data and its CSV, the device log, status 11 while the device asks |
| `tests/data-page.test.js` | Tests in a headless Chrome of the parts under the app list (older firmware, start picture, backup, restore, app data, device log) and of the designer buttons Download PNG and Live preview |
| `tests/device.test.js` | Tests of `js/device.js` in Node.js: the 1.0.1 frames, the Apps screens are the screens of `js/apps-pictures.js`, the display is at the same place in the page and in the web simulator |
| `tests/devbar-page.test.js` | Tests in a headless Chrome of My device (`#device`, `#apps`), the StatTrak button (Connect, name, serial, firmware, storage, Disconnect), Update available and the update from the button, an added tab that keeps the connection, the event `stattrak:device` |
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
After a change to the backup and data parts or to the live preview, send a start picture,
download a backup and restore it, download and delete app data, and run the live preview of the
Screen Designer, on a real device.

## Publish

The site is a folder of static files. Vercel publishes the folder `updater/` of this repository:

- A push to `main` goes to the public address `https://stattrak.tridos.design`.
- A push to an other branch gets a preview address, which only the team can open.
- `.vercelignore` keeps the tests out of the published files.
