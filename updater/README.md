# StatTrak Web Updater

A web page that sends a firmware file to a StatTrak device over Bluetooth.

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

## Files

| File | What it is |
| --- | --- |
| `index.html`, `css/` | The page |
| `js/ble.js` | The Bluetooth code: connect, commands, firmware transfer |
| `js/firmware.js` | Checks of the firmware file |
| `js/main.js` | The steps of the page |
| `tests/fake-device.js` | A stand-in for the device and for Web Bluetooth. Its packet code is a port of the firmware code in `../components/ble_ota/` |
| `tests/ble.test.js` | Tests of `js/ble.js` and `js/firmware.js` in Node.js |
| `tests/page.test.js` | Tests of the full page in a headless Chrome. They click the buttons as a user does |
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

## Publish

The page is a folder of static files. Vercel publishes the folder `updater/` of this repository:

- A push to `main` goes to the public address `https://stattrak.tridos.design`.
- A push to an other branch gets a preview address, which only the team can open.
- `.vercelignore` keeps the tests out of the published files.
