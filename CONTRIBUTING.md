# Share your app, your feature or your idea

You made something for your StatTrak, or you have an idea. This page explains how other
owners can get it.

You do not have to know GitHub or programming words. Your AI coding tool does the technical
steps.

| What you have | Where it goes | Tell your AI tool |
| --- | --- | --- |
| **An app** (a game, a timer, a counter) | [The app store](#way-1-you-made-an-app) on the StatTrak web page. Each owner installs it with one click | *"I want to share this app in the app store."* |
| **A firmware feature or a fix** | [A proposal on GitHub](#way-2-you-changed-the-firmware) ("pull request"). If we accept it, it is in the next official firmware | *"I want to share this feature. Follow CONTRIBUTING.md."* |
| **An idea or a fault**, no code | [A message to the team](#way-3-you-have-an-idea-or-you-found-a-fault) ("issue") | *"Help me to open an issue for this idea."* |

Not sure if you made an app or a firmware change? Ask your AI tool: it knows.

## Words that you will see

| Word | What it means |
| --- | --- |
| **App store** | The page where owners share apps: <https://stattrak.tridos.design/#store>. |
| **GitHub** | The website where this firmware lives. A free account is necessary to send a firmware change. |
| **Repository** ("repo") | The folder with all files of the firmware, with its history. |
| **Fork** | Your own copy of the repository on GitHub. You can change it as you like. |
| **Branch** | A separate line of work inside a repository. One feature = one branch. |
| **Commit** | A saved step of your work, with a short note. Like a restore point. |
| **Pull request** ("PR") | A proposal: "please add my change to the official firmware". We look at it, test it and answer. |
| **Issue** | A message to the team: an idea, a question or a fault report. No code is necessary. |
| **Checks** | Automatic tests that run on GitHub for each pull request. Green = passed. |
| **Merge** | We accept the pull request. Your change is now part of the official firmware. |

## Way 1: you made an app

An app goes to the app store, not to GitHub. You need a Google or a GitHub account to sign in.

### What every app needs

- **Two files:** the app (`.stapp`) and its source text (`.sts`). Your AI tool makes both.
  Store apps are open source: each person can download the source.
- **A name** that no other app has. An update is the same name with a higher version number.
- **A true description:** what the app does, and what each button does.
- **Its own title picture** that shows what the app is: a ship for a space game, a dice for a
  dice app. An app with a standard icon or only text waits for a person of our team.
- **It runs without an error.** Your AI tool tests this on your computer first.

### Not allowed

Hate speech. Attacks on real people. Spam, advertising or links. An app that does not do what
its description says.

Airsoft, shooter and game themes are fine. A copy of a known game, or adult or gore content,
is not refused, but a person of our team looks at it. The full rules:
<https://stattrak.tridos.design/legal/rules.html>.

### Send it

1. Tell your AI tool: *"I want to share this app in the app store."* It checks the app
   against the rules and gives you the two files and a description text.
   (For AI tools: [docs/APP_STORE.md](docs/APP_STORE.md).)
2. Open <https://stattrak.tridos.design/#myapps> and sign in.
3. Pick the two files, pick a category, paste the description, and click **Send for review**.

### What happens next

A review checks each new app and each new version within 24 hours. If all is good, the app is
live in the store at once. If not, you see a short text that says what to change. Click
**Copy for my AI tool**, paste the text into your AI tool, and upload the new version.

## Way 2: you changed the firmware

### Before you send it

Your feature or your fix is ready to send when all of these are true:

- [ ] It does **one** thing. Two features = two proposals. Small proposals get an answer faster.
- [ ] `python tools/check.py` shows `OK` for all 4 steps.
- [ ] It has a test in the folder `tests/` that shows that it works.
- [ ] You have pictures of it from the simulator.
- [ ] A user who does not want your feature is not disturbed by it. A new screen, a new
      setting that is off by default, or a clear improvement are good. A change that removes
      or moves something that people use is not.
- [ ] It does not change the protected parts (see below).
- [ ] You wrote all of it yourself or with your AI tool, and you agree that it is published
      under the license of this project: GPL-3.0-or-later (file `LICENSE`). Each person can
      then use, change and share your feature, under the same license.

A fix of a fault is welcome also when it is very small.

### The protected parts

Some files keep the device safe: a fault there can make devices unusable for ever. A
proposal that changes them is not accepted from a first-time contributor without a talk
first (open an issue):

- the firmware update and the Bluetooth start: `components/ble_ota/`, `components/app_ble/`
- the start sequence: `main/main.c`
- the hardware drivers and pins: `main/board.c`, `main/board.h`, `main/board/`
- the flash layout and the chip settings: `partitions.csv`, `sdkconfig.*`
- the safety check itself: `tools/check_safety.py`

### Write the text of your proposal

A person of our team reads the proposal. Other owners read it later in the list of changes.
So the first part is for a person who never saw code. GitHub shows a form with these parts.

**The title** says what an owner gets, in plain words, in 60 characters or less. No file
names, no programming words.

| Good | Bad |
| --- | --- |
| `Kill streak: a sound after 5 kills in a row` | `Update app.c` |
| `Fix: the counter shows 1000 kills and more` | `fix: overflow in draw` |
| `New setting: the display turns 180 degrees` | `feat(settings): add rotate flag` |

**The first part: for each person.**

- *A feature:* what the owner sees and which button does what, in two or three sentences.
  Then one sentence about when it is useful ("in a game with a respawn limit").
- *A fix:* three short lines. **Before:** what went wrong. **How to see the fault:** the steps
  on the device or in the simulator. **Now:** what happens with the fix.
- The pictures from the simulator, in the order of the steps. Say which button press leads
  to which picture.

**The second part: for the reviewer.**

- The files that changed, and why, in one line each.
- New values in flash and new settings: the key, the default, and that they are at the end of
  their list.
- The firmware size from `python tools/build.py`, or "not built".
- What you ran (`python tools/check.py`, the new test) and the result.
- **The truth about the device.** "Tested in the simulator only", or exactly what you did on
  a real device. Never write that it works on a device when only the simulator ran.
- "Made with": the name of the AI tool.

Do not write: a list of each small edit, words like "robust" or "seamless", or a claim that
was not tested.

An example of a good first part, for a fix:

> **Fix: the counter shows 1000 kills and more**
>
> **Before:** in the KILLS + DEAD screen, the kill counter showed `000` after 999 kills.
> **How to see the fault:** set the game kills to 999 in the simulator, then press the left
> button one time. **Now:** the counter shows `1000` in smaller digits.

### Send it

Tell your AI tool: *"Open a pull request for this feature. Follow CONTRIBUTING.md."*
These are the steps that it does (or that you do on the GitHub website):

1. **Make a fork.** On the page of this repository, the button "Fork" makes your own copy.
2. **Put your branch into your fork** ("push"). The branch has only this one change, on top of
   the newest official firmware.
3. **Open a pull request** from your branch to the branch `main` of this repository. GitHub
   shows a text form. Fill it in as above.
4. **Add the pictures.** Drag the picture files into the text field of the form.
5. **Wait for the checks.** They run by themselves and take about 10 minutes. If one is red,
   open it, give the text to your AI tool and let it repair the cause.

### What happens next

1. A person from the team reads your proposal. Usually in one or two weeks.
2. We can ask for changes. That is normal and not a "no". You change your branch, and the
   pull request updates by itself.
3. We test the feature **on a real device**. The simulator cannot test everything.
4. If all is good, we merge it. Your GitHub name is in the history of the firmware, and the
   feature is in the next official release.

We can also say no: when a feature does not fit most users, makes the firmware too large,
uses too much battery, or is a risk for the device. You can still use it on your own
device. That is the point of open firmware.

## Way 3: you have an idea, or you found a fault

**An idea:**

1. Open the "Issues" page of this repository on GitHub.
2. Select "New issue", then "Feature idea".
3. Say what you want and why. A drawing of the screen helps a lot: a photo of a sketch on
   paper is fine, a picture from the simulator or the Screen Designer is better.

We read all ideas. We cannot build all of them.

**A fault:** open an issue with the form "Fault report". Say what you did, what you expected
and what happened. Say the firmware version (Settings, `Version`) and if it is the official
firmware or your own.

A fault that can make devices unusable: please do not write the details in public. Send an
e-mail to the address on the "Security" page of this repository.
<!-- TODO (TRIDOS): add a contact address for security reports (GitHub: Settings, Security, "Private vulnerability reporting"). -->

## Rules for the code (for AI tools and developers)

- Follow [AGENTS.md](AGENTS.md) (the safety rules) and the guides in [skills/](skills/): the
  [firmware guide](skills/stattrak-firmware/SKILL.md) has the work sequence and the pull request
  (its section 5), its [code guide](skills/stattrak-firmware/code.md) has the code style, and the
  [app guide](skills/stattrak-app/SKILL.md) has the app store (its section 8).
- New values in flash go at the **end** of `ENTRY_LIST`. New values of a setting go at the
  **end** of its list. Never rename a stored value.
- A new screen has a design file in `designs/` and a test that takes it through its states.
- A Bluetooth command has a number from `0x0100`, a line in the table in
  `docs/BLUETOOTH.md` and a test in `tests/test_bluetooth.py`.
- The firmware must stay below 1 MB. Say in the pull request how many bytes your feature adds
  (`python tools/build.py` prints the size).
- No new dependencies (libraries) without a talk first.
- Texts on the display are in English and fit the display.
