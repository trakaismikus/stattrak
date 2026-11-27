# What is new

The changes that you can see and feel on your StatTrak. The newest version is first.

## 2.0.0 (27 November 2026)

The first big update since 1.0.1. Your kills, deaths and settings stay after the update.

### Apps

- **Your StatTrak can have apps now.** Games, timers, counters and tools. Install them in a few
  seconds from the web page: [stattrak.tridos.design](https://stattrak.tridos.design), tab
  **My device**. On the StatTrak: Settings, then `Apps: SYNC`. The StatTrak asks `INSTALL?`
  before each app.
- **An app store.** Apps from TRIDOS and from other owners. One click to install. Sort,
  hide and delete your apps. You can also hide the built-in screens that you do not use.
- **Apps to start with:** RESPAWN TIMER (the wait at the respawn point in big digits, with a
  chime when you can go), HEADSHOT %, ROUND TIMER, REACTION, RUNNER, SLOTS, CLIP MARKS (time
  marks of your kills for your video), BATTERY LOG and WALLPAPER.
- **One app can run in the background.** It counts while you look at an other screen, and it
  comes on the screen by itself when it must tell you something, for example when the respawn
  time is up. Settings > `Background`.
- **Make your own app.** The Screen Designer on the web page draws screens pixel for pixel. An
  AI coding tool can make the app for you, also if you never wrote code. Start on the page
  **Get started**.

### Easier to use

- **The app menu.** Press both buttons: a menu of all your apps and screens opens. The left and
  right buttons go to the previous and next app. SETTINGS is always the last one.
- **The Settings list goes round.** Up from the first row goes to the last row, and down from
  the last row goes to the first row.
- **Your own start picture** when the StatTrak switches on (none by default).
- **Your own Bluetooth name**, for example `StatTrak | RED 7`.

### Your data

- **Backup and restore.** Save your stats, settings, app menu, app settings and saved values,
  and start picture in one file on your computer. Put them back on the same StatTrak or on a
  new one. The apps themselves are not in the file: install them first, the page tells you
  which. The StatTrak asks `RESTORE BACKUP?` first.
- **Download the data of an app**, for example the battery log or the clip marks, as a file for
  a spreadsheet.

### Battery

- **A calmer, truer battery level.** The % follows the real battery curve. It no longer drops
  fast at the start and at the end, and it does not jump when the buzzer or Bluetooth is on.
- **The StatTrak asks one time which version it is: DIY or PEQ.** This keeps the battery type
  right (see "After the update" below).
  - **PEQ** (the StatTrak in the PEQ box): the battery is always `1xRCR123A`. Settings has no
    row "Battery".
  - **DIY** (the StatTrak on your own battery): set your battery type one time in
    Settings > Battery. Until you do, the StatTrak shows "Battery type not set" at each start.
- **`USB` is now `USB/DC`.** Use it for USB power or a fixed DC supply: no battery warnings.
- **A new order in Settings > Battery:** NONE, 1xRCR123A, 3.7V(1S), 7.4V(2S), 11.1V(3S),
  9.6V(NiMH), 3xAA/AAA, USB/DC.

### Fixed

- **Some StatTrak units switched off** when you opened the firmware update or `Apps: SYNC`. Now
  the screen shows only a small Bluetooth sign while Bluetooth starts, and the unit stays on.
- **A safer firmware update.** The web page says "Flash complete" only after the StatTrak
  started the new firmware and confirmed it. It checks the file before it sends it, and it
  tells you clearly when an update fails.
- **Bad saved values no longer stop the StatTrak.** A saved value that the firmware does not
  know gets its default value.

### After the update

1. The display shows `Verifying...`, then `Complete`. Hold a button for OK.
2. The question `STATTRAK VERSION` shows. Hold the left button for **DIY** or the right button
   for **PEQ**.
3. DIY only: set your battery type in Settings > Battery.

Wrong answer? Hold a button on Settings > `Version`. The question shows again. Your data stays.

### Go back to 1.0.1

You can install 1.0.1 again from the web page. Do a factory reset first (Settings >
`Factory: RESET`), or the old firmware can read the new settings wrong.

## 1.0.1 with RCR123A (15 April 2026)

- The battery type `1xRCR123A` in Settings > Battery.
