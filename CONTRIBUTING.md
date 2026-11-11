# Share your feature or your idea

You made something for your StatTrak, or you have an idea. This page explains how it can
become part of the official firmware that all owners get.

You do not have to know GitHub or programming words. Your AI coding tool does the technical
steps (tell it: *"I want to share this feature. Follow CONTRIBUTING.md."*).

## Words that you will see

| Word | What it means |
| --- | --- |
| **GitHub** | The website where this firmware lives. A free account is necessary to send something. |
| **Repository** ("repo") | The folder with all files of the firmware, with its history. |
| **Fork** | Your own copy of the repository on GitHub. You can change it as you like. |
| **Branch** | A separate line of work inside a repository. One feature = one branch. |
| **Commit** | A saved step of your work, with a short note. Like a restore point. |
| **Pull request** ("PR") | A proposal: "please add my change to the official firmware". We look at it, test it and answer. |
| **Issue** | A message to the team: an idea, a question or a fault report. No code is necessary. |
| **Checks** | Automatic tests that run on GitHub for each pull request. Green = passed. |
| **Merge** | We accept the pull request. Your change is now part of the official firmware. |

## Way 1: you have an idea, no code

1. Open the "Issues" page of this repository on GitHub.
2. Select "New issue", then "Feature idea".
3. Say what you want and why. A drawing of the screen helps a lot: a photo of a sketch on
   paper is fine, a picture from the simulator is better.

We read all ideas. We cannot build all of them.

## Way 2: you have a working feature

### Before you send it

Your feature is ready to send when all of these are true:

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

### The protected parts

Some files keep the device safe: a fault there can make devices unusable for ever. A
proposal that changes them is not accepted from a first-time contributor without a talk
first (open an issue):

- the firmware update and the Bluetooth start: `components/ble_ota/`, `components/app_ble/`
- the start sequence: `main/main.c`
- the hardware drivers and pins: `main/board.c`, `main/board.h`, `main/board/`
- the flash layout and the chip settings: `partitions.csv`, `sdkconfig.*`
- the safety check itself: `tools/check_safety.py`

### Send it

Tell your AI tool: *"Open a pull request for this feature. Follow CONTRIBUTING.md."*
These are the steps that it does (or that you do on the GitHub website):

1. **Make a fork.** On the page of this repository, the button "Fork" makes your own copy.
2. **Put your branch into your fork.** ("push")
3. **Open a pull request** from your branch to the branch `main` of this repository. GitHub
   shows a text form. Fill it in: what the feature does, the pictures, what you tested.
4. **Wait for the checks.** They run by themselves and take about 10 minutes. If one is red,
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

## Rules for the code (for AI tools and developers)

- Follow [AGENTS.md](AGENTS.md): the safety rules, the work sequence, the code style.
- New values in flash go at the **end** of `ENTRY_LIST`. New values of a setting go at the
  **end** of its list. Never rename a stored value.
- A new screen has a design file in `designs/` and a test that takes it through its states.
- A Bluetooth command has a number from `0x0100`, a line in the table in
  `docs/BLUETOOTH.md` and a test in `tests/test_bluetooth.py`.
- The firmware must stay below 1 MB. Say in the pull request how many bytes your feature adds
  (`python tools/build.py` prints the size).
- No new dependencies (libraries) without a talk first.
- Texts on the display are in English and fit the display.

## Report a fault

Open an issue with the form "Fault report". Say what you did, what you expected and what
happened. Say the firmware version (Settings, `Version`) and if it is the official firmware
or your own.

A fault that can make devices unusable: please do not write the details in public. Send an
e-mail to the address on the "Security" page of this repository.
<!-- TODO (TRIDOS): add a contact address for security reports (GitHub: Settings, Security, "Private vulnerability reporting"). -->
