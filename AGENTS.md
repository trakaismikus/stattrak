# AGENTS.md: instructions for AI coding tools

This file is for AI coding tools (Claude Code, Codex, Cursor, Copilot, Gemini and others).
The person who works with you has probably not read it, and may not be a programmer. You are
the guide.

This file is short on purpose. It has the rules that are for each task, and it tells you which
guide to read for your task. **Read all of this file. Then read the guide of your task
completely, before you start that task.**

| Step | What you do | Where |
| --- | --- | --- |
| 1 | Start: show a first picture fast, and learn who the person is | Section 1 |
| 2 | Get the idea. No idea yet: help to find one | Section 4.1 |
| 3 | Decide: no code, an app, or a firmware change. Say what it means | Section 4 |
| 4 | An app: make it, try it in the simulator, put it on the StatTrak, share it in the app store | The app guide: `skills/stattrak-app/SKILL.md` |
| 4 | The person does not like the look of a screen, or a firmware change has a new screen | The screens guide: `skills/stattrak-screens/SKILL.md` |
| 4 | A firmware change: make it, build it, put it on a device, share it as a pull request | The firmware guide: `skills/stattrak-firmware/SKILL.md` |

The safety rules of section 3 are for each step.

## 1. Start here (each new conversation)

Look for the file `.stattrak/profile.md` (it is not in git). If it is there, read it: it says
who the person is and how you talk to them. Then go on with their idea (section 4).

If it is not there, the person is new. There are two ways to start.

### 1.1 The fast start: the first message has an idea

Most people come with the start text of the README, and it has an idea in it ("My idea: a
dice ..."). A new person wants to see their idea on the display. Questions and installs can
wait. So:

1. Decide with section 4 what the idea is. Most ideas are an app. The StatTrak can do it now,
   without code (section 4.2)? Say so and show how.
2. **An app:** read the app guide (`skills/stattrak-app/SKILL.md`) and make a first version now
   (section 1 of the guide). Ask no question first. Install nothing: an app needs only Python
   (`python --version`; on some computers the command is `python3`). Talk to the person as to a
   Beginner (the table in section 1.2) until you know more.
3. Show the picture of the screen. The aim: the person sees their idea some minutes after their
   first message.
4. In the same message, ask what they want different, and ask questions 1 and 2 of section 1.2
   (not question 3). This is the only message with more than one question, also for a Beginner.
   When you have the answers, write `.stattrak/profile.md`.
5. Go on with the app guide. The check of the computer (section 1.3) and the tour (section 1.4)
   come later: when the person wants a firmware change, or wants to see the screens of the
   device.

**The idea needs a firmware change**, or you do not understand the idea: do sections 1.2 to 1.4
first. A firmware change needs the simulator part, and the person must know the risk before
you start.

### 1.2 Learn who the person is

Ask these questions in **one** short message, with the answer options. Do not ask more than
these three.

1. **Have you written code before?** (a) Never. (b) A little. (c) Yes, I am a developer.
2. **Have you built or changed electronics before, for example with Arduino or ESP32?**
   (a) No. (b) A little. (c) Yes.
3. **What do you want to do today?** (a) Look around and try the simulator.
   (b) I have an idea for something new. (c) I want to change something that my StatTrak
   does now. (d) I have no idea yet. Show me what is possible.

If the first message of the person has an idea already, do not ask question 3.

**Never ask the person "do you want an app or a firmware change?".** Most people cannot know
the difference. You decide with section 4, and you tell the person what your decision means
for them.

Then write `.stattrak/profile.md` with the answers, the idea (if there is one) and the level
that you selected. A short text is enough, for example:

```
Level: Beginner
Code: never. Electronics: never.
Idea: a dice app (apps/dice/dice.sts, branch my/dice).
```

The levels:

| Level | Who | How you talk and work |
| --- | --- | --- |
| **Beginner** | Answer 1 is (a), or the person says they are not technical | Plain, short sentences. No jargon: say "the firmware file", not "the binary"; "a saved copy", not "a commit". You do all the work: never ask the person to type a command or to edit a file. One question at a time, with options. After each change, show a picture of the display. Before each step, say in one sentence what it is for. |
| **Maker** | Some code or some electronics | Short explanations. Name the files that you change. The person can run commands if they want to. |
| **Developer** | Answer 1 is (c) | Technical and brief. No tutorials. Show diffs and test output. |

If you are not sure, use Beginner. The person can ask you to change the level at any time
("talk to me like a developer"). The safety rules in section 3 are the same for all levels.

### 1.3 Check the computer

Run `python tools/setup.py` (on some computers the command is `python3`). It only looks. It
changes nothing. It prints what the computer has and what is missing.

If something is missing:

1. Tell the person **what** will be installed, **where** and **how large** it is. The output of
   `tools/setup.py` has these facts. For a Beginner, add one sentence about what the thing is for.
2. Wait for a "yes".
3. Run the install command: `python tools/setup.py simulator`.

Install only the simulator part now. It is small (about 550 MB) and stays in the folder
`.venv` of this repository. The firmware build part (`python tools/setup.py firmware`, about
4 GB) is necessary only when the person wants to put a firmware on a real device. Ask at that
time.

An app needs less: the app tool (`python tools/app.py`) needs only Python. If the person does
not want to install the simulator part, you can still make an app, show pictures of it and
open it in the simulator of the web site (the app guide). Then skip section 1.4 and show the
pictures of an example app:
`python tools/app.py run apps/slots/slots.sts wait:1000 png:out/slots.png`.

If Python is missing, the person must install it first: https://www.python.org/downloads/
(on Windows: tick "Add python.exe to PATH" in the installer). If Git is missing:
https://git-scm.com/downloads

### 1.4 Show the device

Run `python tools/sim.py --tour` and show the pictures from `out/` to the person. This is the
real firmware, running on the computer. Tell the person in two sentences what they see.

Then go on with the answer to question 3:

| Answer | What you do next |
| --- | --- |
| (a) Look around | Offer the simulator in the browser, so the person can press the buttons: the page https://stattrak.tridos.design/sim/simulator.html . Show the app store (https://stattrak.tridos.design/#store): these are things that other owners made. Then ask for an idea. |
| (b) An idea, or (c) a change | Section 4: decide what the idea is (no code, an app, a firmware change) and tell the person. Then the app guide or the firmware guide. |
| (d) No idea yet | Section 4.1: help the person to find an idea. |

## 2. What this repository is

Firmware for the **StatTrak** kill counter by TRIDOS.DESIGN x JZ.

- Chip: ESP32-C2 (RISC-V, one core, 272 KB RAM, 4 MB flash, 26 MHz crystal)
- Display: 128 x 32 pixels, each pixel is on (white) or off
- Input: 2 buttons. Output: 1 buzzer. Battery voltage measurement.
- Bluetooth LE: for the firmware update, and for user features (the code guide of the
  firmware). It is off in normal operation.
- Language: C (GNU C11). SDK: ESP-IDF **v5.4**.

## 3. The most important rule: do not brick the device

"Bricked" means: the device cannot start, or it cannot get new firmware, and the user cannot
repair it. Users install firmware **over Bluetooth**. Think of Bluetooth as the only way to
repair a device.

How the device protects itself (do not weaken any part of this):

1. A new firmware is written to a spare slot. The old firmware stays in flash.
2. The device restarts into the new firmware in the state **"pending"** and shows the
   **accept view** ("Verifying...").
3. The web updater connects again over Bluetooth and sends **"accept"**. This shows that
   Bluetooth works in the new firmware. Only then the firmware is marked as good.
4. If the new firmware crashes, hangs or is switched off before the accept, the bootloader
   **goes back to the old firmware** at the next start.
5. Rescue: hold **both buttons for 10 seconds at power-on** to open the update mode.

A firmware that was accepted cannot go back. So a firmware that (a) was accepted and
(b) cannot open the update mode after that, is a bricked device. All rules below protect
the update mode and the start sequence.

### Never do these things

| Never | Why |
| --- | --- |
| Change `partitions.csv` | The table is in the flash of each device. An update over Bluetooth cannot change it. |
| Change the chip, crystal, flash size, partition or rollback settings in `sdkconfig.defaults` | Wrong crystal = no Bluetooth. No rollback = no way back from a bad firmware. |
| Switch on secure boot, flash encryption or anti-rollback. Call any `esp_efuse_*` write function | They burn eFuses. That is permanent. |
| Call `esp_ota_mark_app_valid_cancel_rollback()` or `Ota_acceptPending()` in new code | Only the Bluetooth "accept" command can accept a firmware. A firmware must not accept itself. |
| Use `esp_ota_*`, `esp_flash_*`, `esp_partition_*` write or erase functions outside `components/ble_ota/ota.c` | They can erase the running firmware, the bootloader or the partition table. |
| Change the pin numbers in `main/board.h` | `PWR_ON_PIN` keeps the power on. A wrong pin can also damage hardware. |
| Change the start sequence in `main/main.c`: `Board_powerOn()` first, pending firmware opens the accept view, 10 seconds hold opens the update view | These are the repair paths. |
| Remove `Ota_init()` from `update_view.c` or `fw_accept_view.c`, or remove the setting "Firmware: UPDATE" | Without Bluetooth in these views the web updater cannot send or accept firmware. |
| Call `Ota_setUpdateAllowed(false)` in the update view, the accept view, `main.c` or `app.c` | The device then refuses each new firmware. |
| Change the Bluetooth name `StatTrak`, the service numbers `0x8018`, `0x8020` to `0x8023`, or the update commands and packets | The web updater finds the device and talks to it with them. |
| Start Bluetooth (`Ota_init()`) outside the soft radio start: the start function that a view gives to `BtStart_begin()` (`docs/BLUETOOTH.md`, "Radio start") | The radio start and a bright display together pull too much current. On a weak unit the device switched off in 6 of 10 starts. The soft start shows only a small Bluetooth sign while the radio starts. |
| Switch the device off for a low battery while firmware is received | A device that reads a wrong battery level could never get a new firmware. |
| Rename, reuse or change the meaning of a flash value in `ENTRY_LIST` (`main/app.h`) | Users lose their counters, or an other firmware version reads a wrong value. |
| Block the main loop: `while(1)` or a long delay without `vTaskDelay()`, slow code in a `draw` function | Buttons, the display and the update mode stop. The user cannot switch off. |
| Handle `EVENT__PRESS_BOTH` or `EVENT__LONG_PRESS_B` in a different way in normal views, or remove `SETTINGS` from the app menu (the last line of `_buildMenu()` in `main/views/launcher_view.c`) | "Both short" = app menu, and the app menu always ends with SETTINGS (the way to the update). "Both long" = power off. SETTINGS is added in code, never from the app list in flash: no app, no flash content and no Bluetooth command can remove it. |
| Let the app commands (`0x0110` to `0x011C` and `0x0120` to `0x012A` in `main/ble_commands.c`) work outside the view "Apps: SYNC" (`AppSync_isActive()`) | Only there the person sees the install question, and only there the device refuses new firmware while the phone talks to it. |
| Write to flash in a `draw` function, also in the app host (`main/views/app_host_view.c`): app errors and saved values are written later, in `AppHost_update()` | The draw function runs each 10 ms. Flash wears out, and a slow write stops the buttons and the display. |
| Tell the user to run `idf.py flash`, `idf.py erase-flash` or `esptool` | These need a wired connection and replace the bootloader. Only do it if the user asks for it and says that the device has a serial (UART) connection. |

`python tools/check_safety.py` finds most of these changes automatically.
`python tools/check_safety.py --explain` prints each rule with its reason.

### Limits of what you can know

- The simulator has **no Bluetooth radio and no real flash**. A pass in the simulator does not
  show that the Bluetooth update works on a device. Say this to the user each time.
- You cannot test on hardware. Do not say "it works on the device". Say what you tested.
- If a change touches `components/ble_ota/`, `components/app_ble/`, `main/main.c`,
  `main/board.c`, `main/board/`, `partitions.csv` or `sdkconfig.*`: stop and tell the user
  that this is a high-risk change, before you make it. Read `docs/SAFETY.md`.

## 4. No code, an app, or a firmware change: you decide

The person has an idea. They do not know how it must be built, and they must not have to
know. You decide, in this order:

1. **No code.** The StatTrak or the web page can do it now (section 4.2). Say so and show how.
2. **An app.** The app engine can do it (section 4.3). This is the normal way. Read the app
   guide: `skills/stattrak-app/SKILL.md`.
3. **A firmware change.** Only when an app cannot do it (section 4.4). Read the firmware
   guide: `skills/stattrak-firmware/SKILL.md`.

Then tell the person what your decision means for them (section 4.5), before you build.

### 4.1 The person has no idea yet

Do not show a list of 20 ideas. Do this:

1. Ask one question: "What do you want to see or do in a game that your StatTrak does not do
   now?" If the answer is "I do not know", ask: "Do you want something for the game, or
   something for fun in the break?"
2. Give four examples from different groups, each in one line. Use the table below. Add the
   apps that other owners made: the app store, https://stattrak.tridos.design/#store .
3. Show one or two example apps as pictures, so the person sees what is possible:
   `python tools/app.py run apps/slots/slots.sts wait:1000 png:out/slots.png` (the apps are in
   the folder `apps/`).
4. When the person picks one, change it with them: "Which numbers do you want to see? What
   must the left button do?"

| Group | Ideas |
| --- | --- |
| Counters | Headshots, knife kills or hits on me in each game. A kill streak with a sound after 5 in a row. Shots that are left in the magazine. |
| Timers | The wait at the respawn point. A bomb timer that beeps faster at the end. The time of the game. |
| Games for the break | A reaction game. A dice for "who goes first". A coin flip. A small jump game. |
| Tools | A battery log. Time marks for a video. A score for two teams. |
| The look | My own start picture. My own counter screen with larger digits or my team name. |

The device has two buttons, a display of 128 x 32 pixels (on or off, no colors), a buzzer and
Bluetooth. It has no clock, no internet, no sensor for shots. Tell the person when an idea
cannot work, and why, and offer the nearest thing that can work.

### 4.2 Things that need no code

Check this first. Many wishes are there already:

| The wish | How, without code |
| --- | --- |
| My own picture when the device starts | Screen Designer, **Download PNG**, then the web page, My device, **Boot image** |
| A different first screen at the start | My device: move that app to the first place |
| Remove a screen that I do not use | My device: hide the app (the built-in ones too) |
| A name for my StatTrak | My device: the Bluetooth name |
| A copy of my stats and settings | My device: **Backup** |
| An app that another owner made | The app store on the web page: **Install** |

### 4.3 What an app can do

`docs/APPS.md` is the full specification (sections 2 and 4). The short list:

| An app can | Details |
| --- | --- |
| Draw each pixel of the display | Text in 4 fonts, numbers, the big dotted digits, lines, boxes, up to 16 pictures of its own, animation (a new picture each 10 ms) |
| Use the two buttons | Short press, long press, "the button goes down now" (fast games), "the button is held" |
| Read the counters | Total kills, total deaths, kills and deaths of the game |
| Change the counters | With the permission `counters`, in a button event |
| Make sound | A click or a note on the buzzer, also a tune (permission `sound`) |
| Measure time | Milliseconds since the start: a stopwatch, a countdown, a game speed |
| Use random numbers | A dice, a slot machine, enemies in a game |
| Read the battery | The level in percent and the voltage |
| Keep values | 16 numbers stay after power-off: a high score, its own counters |
| Have settings | 4 settings with 2 to 6 values each. The firmware draws the settings screen |
| Log data | Rows of numbers (4 KB) that the person downloads as a file from My device |
| Keep the device on | With the permission `awake`, while the app is on the screen |
| Run in the background | One app: it gets each kill and death, counts time, and comes on the screen for an alarm (permission `background`) |
| Be the main screen | The person puts it first in the app menu and hides the built-in counter. So "my own kill counter screen" is an app |

| An app cannot | Then it is |
| --- | --- |
| Change a built-in screen (KILLS, KILLS + DEAD, STATS), the Settings list, the app menu, the power-on animation | A firmware change. Or an app that is a new screen next to them |
| Change what both buttons do (short = app menu, long = power off) | Not possible. Section 3 protects it |
| Use Bluetooth: talk to a phone, to a computer or to another StatTrak | A firmware change (the code guide of the firmware, "Bluetooth") |
| Change how the device works: dim, auto-off, brightness, battery warnings, a new device setting | A firmware change |
| Show a text that the person types, or more than 32 fixed texts of 20 characters | A firmware change |
| Know the date or the time of the day | Not possible: the device has no clock |
| Be larger than 4 KB, or do more than 16000 steps for one event | Make it smaller, or a firmware change |
| Run in the background together with a second background app | Not possible: one at a time |
| Use new hardware: a sensor, a light, a different display | Not possible with this device |

### 4.4 The rule

- **If an app can do it, make an app.** Also when a firmware change looks more elegant to you.
  An app is safe for the device, fast to install and easy to share.
- **A part of the idea needs the firmware?** Say it, and offer the app part first: "I can make
  the timer as an app today. The part 'the display turns red' is not possible: the display has
  one color. The part 'show the timer in a corner of the KILLS screen' needs a firmware change,
  because an app cannot change a built-in screen. Do you want the app first?"
- **Only a firmware change can do it?** Say in one sentence which thing an app cannot do. Then
  the firmware guide, and its warning (section 3 there) before the firmware goes on a device.
- **The idea is very large** (a game with many levels): start with a small version as an app.
  Let the person play it. Then make it larger.
- Not sure after you read `docs/APPS.md`? Write a small test app and run it
  (`python tools/app.py run`). That is faster than a guess.

### 4.5 Tell the person what it means

Say it before you build, in words for their level. Do not ask the person to choose. Say what
you will do and why, and let them stop you.

| | An app | A firmware change |
| --- | --- | --- |
| How it goes on the StatTrak | In some seconds, from the web page (My device). The StatTrak asks `INSTALL?` | The whole firmware goes on over Bluetooth. Some minutes, then a restart |
| The risk | An app runs in a closed box. A faulty app shows an error, and after 3 errors the StatTrak switches it off. It cannot damage the device | A fault that the tests did not find can make the device unusable. The risk is the person's (the firmware guide, section 3) |
| What the computer needs | Python only | The simulator part (550 MB). For a real device also the firmware build part (4 GB) |
| An official firmware update later | The app stays | The official firmware replaces the change. You must build the own firmware again |
| Take it off again | Delete the app | Install the official firmware again |
| Share it | The app store: each owner installs it with one click. A review within 24 hours (the app guide, section 8) | A proposal on GitHub. The team tests it on a real device. It comes with the next official firmware (the firmware guide, section 5) |
| The limits | The list of section 4.3 | All that the hardware can do |

A text for a Beginner (an app): "Your idea works as an **app**. That is the safe way: an app
cannot damage your StatTrak, it goes on in a few seconds, and you can delete it again. I make it
now and show you pictures."

A text for a Beginner (a firmware change): "Your idea needs a change of the **firmware**, the
main software of the StatTrak. An app cannot do it, because (the reason). I can make it, and
you see it on the computer first. It takes longer, and when you put it on your StatTrak there
is a small risk for the device. Do you want that, or do you want a simpler version as an app?"

## 5. The guides

Each guide is one task. Read the guide completely before you start the task. Do not read a
guide that you do not need: it only fills your memory.

| Guide | Read it when | What is in it |
| --- | --- | --- |
| `skills/stattrak-app/SKILL.md` | The idea is an app | The first version, the check of your own pictures, the simulator, the Screen Designer, the tests, the install on the StatTrak, the app store, the known faults of the tools |
| `skills/stattrak-screens/SKILL.md` | The person does not like the look of a screen (an app or the firmware), or a firmware change has a new screen | Design files, 2 or 3 proposals as pictures, the Screen Designer page, the "Send to AI" text |
| `skills/stattrak-firmware/SKILL.md` | The idea needs a firmware change | The work sequence, the firmware build, the update of a real device, the simulator, the pull request, the known faults of the tools |
| `skills/stattrak-firmware/code.md` | Before you change firmware code | Where things are, views, drawing, values in flash, Bluetooth commands, the tests, the code style |

More documents: `docs/APPS.md` (the app language and the app engine), `docs/APP_STORE.md` (the
check list of the app store), `docs/DESIGNS.md` (the design files), `docs/SAFETY.md` (the
high-risk parts), `docs/BLUETOOTH.md`, `docs/DATA.md`, `CONTRIBUTING.md` (how to share).

A text of the Screen Designer (it starts with `StatTrak screen design` and says "Follow
AGENTS.md, section 5"): read the screens guide and build exactly what the text shows.

The guides have the Agent Skills format: a folder with a file `SKILL.md`. A tool that loads
skills finds them in `.claude/skills/` and `.agents/skills/` (small files that name the guide).
An other tool opens the guide as a normal file.

## 6. When you are not sure

Ask the user. A question costs nothing. A bricked device cannot be repaired.
If the person needs help from people: the "Issues" page of the repository on GitHub.
