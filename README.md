# StatTrak firmware

This is the software that runs inside the **StatTrak** kill counter by TRIDOS.DESIGN x JZ.
It is open: you can change it, add your own screens and features, and put your version on
your own device.

**You do not have to be a programmer.** An AI coding tool can do the work on your computer.
You tell it what you want, it shows you pictures of the result, and you decide.

> **Read this first.** You change the firmware at your own risk. A faulty firmware can make
> a device unusable ("bricked"). This project has many protections against that, and the
> simulator lets you try each change on your computer first. But no test can promise that a
> changed firmware is safe. [docs/SAFETY.md](docs/SAFETY.md) explains the risk.

## Start in 5 minutes (no coding)

**What you need**

- A computer with Windows, macOS or Linux.
- An AI coding tool **that can work on your computer**: for example Claude Code, Codex,
  Cursor, GitHub Copilot (agent mode) or Gemini CLI. A chat website alone is not enough: the
  tool must be able to read files and run programs.
- [Python](https://www.python.org/downloads/) and [Git](https://git-scm.com/downloads).
  If you do not have them, the AI tool tells you how to get them.
- Your StatTrak is **not** necessary to start. You can do everything in the simulator.

**What you do**

Open your AI coding tool in an empty folder and paste this text:

```text
I have a StatTrak kill counter and I want to change its firmware. Help me, step by step.

1. Download the firmware project to this computer:
   git clone https://github.com/trakaismikus/stattrak.git
2. Open the folder "stattrak" and read the file AGENTS.md completely. Follow it.
   Start with section 1 "Start here".
3. Ask me before you install anything on my computer. Tell me what it is and how large it is.

I may not be a programmer. Use plain words.
```

<!-- TODO (TRIDOS): replace the address above with the final public address of this repository. -->

The AI tool then:

1. asks you up to three short questions, so it knows how to talk with you,
2. checks your computer and asks before it installs anything,
3. shows you the real StatTrak screens, running on your computer.

After that, say what you want. Some ideas:

- "Show the kills and the deaths on one screen, with a line between them."
- "Add a screen that shows a skull for two seconds after each tenth kill."
- "Make the numbers larger."
- "Add a second counter for headshots."

The device has two buttons, a small display, a buzzer and Bluetooth. It has no clock and no
internet. The AI tool tells you when an idea cannot work, and why.

For a new screen, the AI tool first draws two or three different designs and shows you the
pictures. You pick one. Then it builds the feature and shows it in the simulator. Only when
you are happy, it makes the firmware file for your device.

## Draw your own screens

You know exactly how a screen must look? Draw it. The **Screen Designer** is a page that opens
in your browser: the file `tools/designer/index.html` in the project folder (double click).
Ask your AI tool to open it for you.

- You draw on the real 128 x 32 pixels, with the real fonts of the device.
- You make as many screens as you want, and you write for each one what it does.
- Undo with Ctrl+Z. The page keeps your work in the browser, and "Save" gives you a file.
- The button **Send to AI** makes a text. Paste it into your AI tool. The AI tool then knows
  each pixel of your design.

The AI tool can also put its own proposals into the designer, so you can change them there.

## Put your firmware on your StatTrak

The AI tool guides you through this. The steps:

1. The AI tool runs all tests and makes the firmware file (`out/stattrak-...bin`).
2. Open the **StatTrak web updater** in Chrome, Edge or Opera, on a computer or an Android
   phone: <https://stattrak.tridos.design>. It does not work in Firefox, in Safari or on an iPhone.
3. On the StatTrak: Settings, then `Firmware: UPDATE`, hold the right button.
   The display shows `FIRMWARE UPDATE` and `Waiting...`.
4. In the web updater: pair with `StatTrak`, select your firmware file, start.
   Keep the device near the computer and the page open.
5. The StatTrak restarts and shows `Verifying...`. If the display stays off, switch the
   device on. The web updater connects again and confirms the firmware. The display shows
   `Complete`. Press a button.

**If something goes wrong**

| What you see | What to do |
| --- | --- |
| `Error`, or the transfer stops | Nothing is lost. The old firmware is still there. Start again. |
| `Verifying...` does not go away | Switch the device off (hold both buttons). It starts with the old firmware again. |
| The device does not work as before | Hold both buttons for **10 seconds** while you switch it on. The update mode opens. Install the official firmware. |

The old firmware stays in the device until the web updater confirms the new one. After that
moment the device cannot go back by itself. That is why the tests and the simulator come first.

## Share what you made

Did you make something that other StatTrak owners will like? You can send it to us. If it
is good and safe, it becomes part of the official firmware, with your name on it.
[CONTRIBUTING.md](CONTRIBUTING.md) explains how, in plain words. You do not have to know
GitHub: your AI tool does the technical steps.

Only an idea, no code? Tell us on the "Issues" page of this repository ("Feature idea").

## How to use the device

| Action | Result |
| --- | --- |
| Hold both buttons | Power on / power off |
| Short press on both buttons | Next view: total, kill/death, statistics, settings |
| Total view: short press | +1 kill. Long press: -1 kill |
| Kill/death view: button 1 / button 2 | +1 kill / +1 death. Long press: -1 |
| Settings: short press button 1 / 2 | Up / down |
| Settings: long press button 1 / 2 | Previous / next value, or start the action |
| Hold both buttons for 10 seconds at power-on | Firmware update mode |

## For developers

Everything is one command. The commands work on Windows, macOS and Linux.

```sh
python tools/setup.py              # look: what does this computer have? (changes nothing)
python tools/setup.py simulator    # install cmake, ninja and a C compiler into .venv (550 MB)
python tools/check.py              # safety check, build, unit tests, simulator tests
python tools/sim.py --tour         # a picture of each main view in out/
python tools/sim.py boot click1 click1 frame   # 2 kills, print the display as text
python tools/design.py render designs/example.json   # pictures of a screen design
python tools/setup.py firmware     # install ESP-IDF v5.4 (4 GB). Not necessary with Docker.
python tools/build.py              # tests + firmware build -> build/stattrak.bin
```

- Chip: ESP32-C2, 4 MB flash. Display: 128 x 32 OLED. SDK: ESP-IDF v5.4. Language: C.
- The simulator compiles the real firmware sources for the computer. There is a version
  without a window (for tests and AI tools), one with a window (SDL2) and one for the web
  browser: [sim/README.md](sim/README.md).
- With a serial (UART) connection to the board, `idf.py flash` and `idf.py monitor` work as in
  each ESP-IDF project. Users do not have this connection.

```
main/          application: start sequence, views, settings, storage, battery, Bluetooth commands
components/    graphics, animation, display driver, Bluetooth, firmware update
designs/       screen design files
sim/           simulator: the firmware on a computer (headless, window, web)
tests/         tests: full firmware in the simulator, and unit tests
tools/         setup.py, check.py, check_safety.py, sim.py, design.py, build.py
updater/       the web updater: the page that sends a firmware file to the device
docs/          SAFETY.md, BLUETOOTH.md, DESIGNS.md, AUDIT.md, ROADMAP.md
```

## Who made it

The firmware is by Janis Zaharans. The StatTrak kill counter is a product of TRIDOS.DESIGN x JZ.

## License

The firmware and the tools are free software under the **GNU General Public License,
version 3 or later** (GPL-3.0-or-later): see [LICENSE](LICENSE). In short: you can use,
change and share it. If you give a changed version to other people, you must give them the
source code under the same license.

Not under this license: the names and logos of TRIDOS and StatTrak, and the product
pictures. Other people's parts keep their own licenses: ESP-IDF and its components
(Apache-2.0), the fonts of the Screen Designer and of the web updater in
`tools/designer/fonts/` and `updater/fonts/` (SIL Open Font License, the texts are in these
folders).

## Documents

- [AGENTS.md](AGENTS.md): rules and work sequence for AI coding tools (and for you)
- [CONTRIBUTING.md](CONTRIBUTING.md): how to share a feature or an idea
- [docs/SAFETY.md](docs/SAFETY.md): how a device can get bricked and how to prevent it
- [docs/DESIGNS.md](docs/DESIGNS.md): screen design files
- [docs/BLUETOOTH.md](docs/BLUETOOTH.md): Bluetooth for your own features
- [sim/README.md](sim/README.md): the simulator
- [updater/README.md](updater/README.md): the web updater
- [docs/AUDIT.md](docs/AUDIT.md): code audit, November 2026
- [docs/ROADMAP.md](docs/ROADMAP.md): planned work
