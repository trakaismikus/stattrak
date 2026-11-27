---
name: stattrak-firmware
description: Change the firmware of the StatTrak kill counter (ESP32-C2, C, ESP-IDF v5.4). The work sequence, the tests in the simulator, the firmware build, the update of a real device over Bluetooth, and a pull request for the official firmware. Use only when an app cannot do the idea (AGENTS.md section 4), and read the safety rules of AGENTS.md section 3 first.
---

# Change the StatTrak firmware

**Read `AGENTS.md` section 3 (the safety rules) before you change a file.** A fault in the
firmware can make a device unusable. An app cannot. So make a firmware change only when an app
cannot do the idea (`AGENTS.md` section 4).

Before you change code, read the code guide: `code.md` in this folder (where things are, values
in flash, Bluetooth, the tests, the code style).

## 1. The work sequence

First decide with `AGENTS.md` section 4 what the idea is. Most ideas are an **app**: an app has
its own guide (`skills/stattrak-app/SKILL.md`), and it needs no firmware build. This guide is for a
change of the firmware itself.

Do these steps in this order. Do not skip a step. For a Beginner, you do all of them and
you show the result of each one.

1. **Understand the idea.** Say it back in two or three sentences. Ask two questions at most.
   Read the files that you will change, and `docs/SAFETY.md` if the change is near the update
   or start code.
2. **Design the screens first** (the screens guide, `skills/stattrak-screens/SKILL.md`), if the
   change shows something new on the display.
   Make 2 or 3 different proposals, show the pictures, let the person pick.
3. **Make a safe place for the work.** Create a git branch (`my/<short-name>`). For a Beginner:
   "I make a separate copy of the work, so the original stays as it is."
4. **Change the code.** Keep the change small. The code guide (`code.md` in this folder) has the
   recipes: read it before you change code.
5. **Add or change tests** in `tests/` for the new behaviour (the code guide, section 4).
6. **Show it in the simulator.** Take pictures of each step of the feature:
   `python tools/sim.py boot <actions> png:step1 <actions> png:step2`. Show them in order and
   say which button press leads to which picture. Offer the simulator with a window or the web
   simulator (`sim/README.md`) if the person wants to press the buttons.
7. **Change it until the person is happy.** Go back to step 4.
8. **Run all checks:** `python tools/check.py`. All 4 steps must show `OK`.
   If a test fails, find the cause. Do not change a test only to make it pass.
9. **Save the work:** `git commit`. For a Beginner: "I saved a restore point."
10. **Build the firmware file** when the person wants it on a real device: section 2.
11. **Put it on the device:** section 3. Go through it with the person, step by step.
12. **Report:** what changed, what `tools/check.py` printed, the pictures, and the warning
    from "Limits of what you can know" (`AGENTS.md` section 3).
13. **Offer to share it.** If the feature or the fix is good for other owners: section 5.

## 2. Build the firmware file

```sh
python tools/build.py           # release build: use this one for a device
python tools/build.py --debug   # debug build: log output on the serial port, larger
```

`tools/build.py` runs the safety check and the fast tests, builds with ESP-IDF v5.4 (or with
Docker if ESP-IDF is not installed), and checks the result. The firmware file is
`build/stattrak.bin`, with a copy in `out/` that has the version in its name.

If ESP-IDF is missing, the command says so. Then follow `AGENTS.md` section 1.3, with
`python tools/setup.py firmware` (tell the person: about 2 GB download, 4 GB on disk,
10 to 30 minutes).

The direct commands, for developers (in a terminal where ESP-IDF is active):

```sh
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release" build
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.4 \
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release" build
```

- The firmware must fit in 1 MB. The build fails if it does not fit.
- If you change the build type by hand, delete `sdkconfig` and `build/` first. An old
  `sdkconfig` file wins over the defaults. `tools/build.py` does this for you.
- The firmware version is the text in the file `version.txt` (three numbers, for example
  `1.1.0`, or a beta for testers, for example `1.2.0-b1`; at most 9 characters, or the settings
  row "VERSION:" cuts it off). Change it only when the user asks, or for a release.
- The folder of the repository must not have a space in its name (ESP-IDF limit).
- `sdkconfig.hw_new` is for hardware with chip revision 2.0 or newer. Do not add it unless
  the user says that the device needs it.

## 3. Put the firmware on a device

Only when the person asks for it. Before the first time, say this plainly:
**"This is your own firmware. If it has a fault that the tests did not find, the device can
become unusable. The risk is yours."** Wait for a "yes".

Before:

1. `python tools/check.py` passed (the full check, not `--fast`) and `python tools/build.py`
   made the file.
2. The battery of the device is good, or the device is on USB power.
3. First time for this person? Advise them to install the newest **official** firmware first,
   with the same steps. Then they know that the update works on their device before they try
   their own firmware.

Steps (the person does them, you guide):

1. Open the StatTrak web updater in Chrome, Edge or Opera on a computer or an Android phone:
   https://stattrak.tridos.design . It does not work in Firefox, in Safari or on an iPhone.
2. On the device: Settings, then `Firmware: UPDATE`, hold the right button. The display shows
   `FIRMWARE UPDATE` and `Waiting...`.
3. In the web updater: `Pair device`, select `StatTrak`.
4. Select the firmware file from the folder `out/` and start the transfer. The display shows
   `Receiving` and the progress. Keep the page open and the device near the computer.
5. The device restarts and shows `Verifying...`. The web updater connects again and accepts
   the firmware. The display shows `Complete` and the version. Hold a button for OK.
6. **Check the update mode immediately:** open Settings, `Firmware: UPDATE` again and check
   that the web updater can pair with the device. Then cancel. If this does not work, tell
   the person to stop using the feature and get help (`AGENTS.md` section 6).

If something goes wrong:

- `Error` on the display, or the transfer stops: nothing is lost. The old firmware is still
  there. Start again at step 2.
- `Verifying...` does not go away: switch the device off (hold both buttons). At the next
  start the device uses the old firmware again.
- The device does not start normally: hold both buttons for 10 seconds at power-on. The
  update mode opens. Install the official firmware.

## 4. The simulator: test before a device is at risk

The simulator compiles the **real firmware code** (`main/main.c`, the app, all views, the button
logic) for the computer. Only the hardware and ESP-IDF are replaced (`sim/host/`).

```sh
python tools/check.py --build-only            # build it (folder build-host/)
python tools/sim.py boot click1 click1 frame  # 2 kills, print the display as text
python tools/sim.py boot both click1 both png:settings state   # menu, SETTINGS, open
python tools/sim.py --tour                    # a picture of each main view in out/
python tools/sim.py --help                    # all actions
```

- `frame` prints the display as 32 lines of 128 characters (`#` = pixel on). You can read it.
- `png:NAME` saves `out/NAME.png`. Show it to the user.
- `state` prints the view, the counters, the settings and the flash content.
- `ble:0100` sends a Bluetooth user command, as a phone does (the code guide, section 3).
- The flash content stays in `out/device_state.txt`. `--fresh` starts with a new device.
- A window version (SDL2) and a web version exist too: see `sim/README.md`.

What is real and what is simulated:

| Real firmware code | Simulated |
| --- | --- |
| Start sequence, main loop, views, settings, counters, storage logic, button gestures, dim, auto-off, battery warnings, Bluetooth user commands | Time (it moves only in `vTaskDelay`), button levels, display, flash (a key-value table), battery voltage |
| The Bluetooth update packet code runs in a unit test (`tests/unit/test_ble_ota.c`) | Bluetooth radio, the update task and flash writes of `components/ble_ota/ota.c` (replaced by `sim/host/host_ota.c`) |

## 5. Share a firmware feature or a fix with all owners

An app goes to the app store (the app guide, `skills/stattrak-app/SKILL.md`), not here. This section is for a change of the
firmware, the tools or the documents: a new feature, an improvement, or a fix of a fault.

**When you offer it.** The person is happy with the change, and one of these is true:

- Other owners will like the feature, and it does not disturb an owner who does not want it.
- It repairs a fault of the official firmware, of a tool or of a document. A fix is welcome
  also when it is one line.

Do not offer it for a change that is only for this person (their name on a screen, their team
logo). Ask first: "Do you want to send this to TRIDOS, so that all owners can get it?"

`CONTRIBUTING.md` has the rules in plain words. Read it, then do the technical steps for the
person.

### 5.1 Prepare

1. The person needs a free GitHub account. Explain: "GitHub is the website where the firmware
   lives. Your change goes there as a proposal. The TRIDOS team looks at it and tests it on a
   real device before it becomes part of the official firmware."
2. **One change in one proposal.** The work branch of a person often has more than one change.
   Make a new branch from the newest official `main` (`share/<short-name>`) and bring only
   this change to it. For a Beginner: "I make a clean copy with only this one change."
3. Do not change `version.txt`. Do not change the files that `AGENTS.md` section 3
   protects.
4. Run `python tools/check.py` (the full check). All 4 steps must show `OK`. The change has a
   test in `tests/` (the code guide, section 4). For a fix: the test fails without the fix.
5. Get the pictures from the simulator: the screen before and after, or each step of the feature.
6. If the firmware build part is installed: `python tools/build.py`, and write down the size.

### 5.2 Write the text so that each person understands it

`CONTRIBUTING.md`, "Write the text of your proposal", has the rules and examples. Follow it.
In short:

- **The title** says what an owner gets, in plain words: `Kill streak: a sound after 5 kills
  in a row`, not `Update app.c`.
- **The first part** is for a person who never saw code. A feature: what the owner sees and
  which button does what. A fix: **Before** (what went wrong), **How to see the fault** (the
  steps), **Now** (what happens with the fix). Then the pictures from the simulator.
- **The second part** is for the reviewer: the files and why, new values in flash, the
  firmware size, what you ran.
- **The truth about the device.** "Tested in the simulator only", or exactly what the person
  did on a real device. Never write that it works on a device when only the simulator ran.

### 5.3 Send it

1. Make a copy of the repository in the account of the person (a "fork"), put the branch there
   and open the "pull request" (the proposal). Use the `gh` program if it is installed
   (`gh repo fork`, `gh pr create`). If not, guide the person through the GitHub website.
2. The `gh` program cannot add pictures. Give the person the address of the pull request and
   say: "Drag the pictures from the folder `out/` into the text field, below the heading
   Pictures." Give the full paths of the picture files.
3. Tell the person what comes next: the automatic checks take about 10 minutes. A person of
   the team answers, usually in one or two weeks. A request for a change is normal and not a
   "no".
4. When the team asks for a change, the person gives you the comment. Repair the cause on the
   same branch and answer in plain words what you changed.

Only an idea, no code? Help the person to open an "issue" with the form "Feature idea".
A fault that you cannot repair? The form "Fault report".

## 6. When a tool does not do what you expect

| What you see | The cause | What you do |
| --- | --- | --- |
| `python tools/check.py` stops before the end, with no test name | The full check is slow: 15 to 20 minutes on Windows. The time limit of your tool stopped it | Run it with a time limit of 30 minutes, or in the background. Use `--fast` while you work, and the full check at the end |
| `The headless simulator is not built` | The simulator part is not installed, or not built | `python tools/setup.py` shows what is missing (`AGENTS.md` section 1.3). Then `python tools/check.py --build-only` |
| A picture of `tools/sim.py` shows old counters or old settings | The flash content of the simulator stays in `out/device_state.txt` | Add `--fresh` for a new device |
| The first picture shows the question "StatTrak version" | A new device asks this one time. `boot:ask` leaves the question on the display | Use `boot` (it answers DIY and LATER) or `boot:peq` |
| The firmware build says that ESP-IDF v5.4 is not on this computer | The firmware build part is not installed, or the tool does not find it | `python tools/setup.py` shows what it found. Install only after a "yes" of the person (section 2) |
| The firmware build fails in a folder with a space in its name | A limit of ESP-IDF | Move the project to a folder without a space |
| A test of `updater/tests/` fails in the full run, and passes alone | The page tests are sensitive to time on a slow computer | Run that file alone. The checks on GitHub decide |
| A test of the update or of the start fails after your change | Your change is dangerous for the device | Do not change the test. Read `AGENTS.md` section 3, find the cause, tell the person |
