---
name: stattrak-app
description: Make an app for the StatTrak kill counter (a game, a timer, a counter or a tool on its display of 128 x 32 pixels). Show it as pictures, try it in the simulator, put it on the StatTrak and share it in the StatTrak app store. Use when a person wants something new on their StatTrak and an app can do it (AGENTS.md section 4).
---

# Make a StatTrak app

An app is a small program in StatTrak Script (a `.sts` file). The person installs it from the
web page in some seconds. An app needs no firmware build, and it cannot damage the StatTrak.

- **Where:** in the folder of the StatTrak project (the folder with `tools/app.py`). You do not
  have it? `git clone https://github.com/trakaismikus/stattrak.git`. Run all commands there.
- **What you need:** Python 3.8 or newer. No other install. On some computers the command is
  `python3`, on Windows also `py`.
- **The rules for each task:** `AGENTS.md` of the project. Section 1: the person and how you
  talk to them. Section 3: the safety rules. Section 4: an app or a firmware change.
- **The language:** `docs/APPS.md`. Read these parts, not all of it: in section 4 the tables
  "System calls (SYS)" (all calls of an app) and "Events", and section 7 (7.1 to 7.4: the
  language). Section 2 has the limits.
- **The examples:** the folder `apps/`. Read one that is small and near to the idea:

  | Example | What it is | Lines |
  | --- | --- | --- |
  | `apps/headshot/headshot.sts` | A counter that reads the kills | 92 |
  | `apps/round-timer/round-timer.sts` | A timer with settings and a saved value | 127 |
  | `apps/reaction/reaction.sts` | A small game with states, a picture, a sound and a best time | 141 |
  | `apps/runner/runner.sts` | A fast game: `down1`, moving pictures | 338 |

  `apps/` has the official apps, and your app while you work on it.

## The loop

Run this loop after each change.

```sh
python tools/app.py new "DICE"                  # one time: writes apps/dice/dice.sts, a small counter that works
python tools/app.py run apps/dice/dice.sts wait:500 png:out/dice-1.png press2 wait:500 png:out/dice-2.png frame
python tools/app.py picture apps/dice/dice.sts  # the title picture of the app menu: out/dice_picture.png
python tools/app.py sim apps/dice/dice.sts      # the person plays it in the browser, on the real firmware
python tools/app.py store-check apps/dice/dice.sts   # the checks of the app store, and the package for the upload
```

`run` opens the app and does the actions in their sequence. `png:FILE` saves the screen as a
picture. `frame` prints the screen as text: 32 lines of 128 characters, `#` = pixel on. You can
read it, also when your tool cannot look at a picture.

| Action of `run` | What it does |
| --- | --- |
| `press1` `press2` | A short press. Button 1 is the left one, button 2 the right one |
| `long1` `long2` | A hold (700 ms or more) |
| `down1` `down2` `up1` `up2` | The button goes down, the button goes up (fast games, `held()`) |
| `wait:MS` | Time goes by: a draw each 10 ms, a tick each 100 ms |
| `tick` | One tick now |
| `png:FILE` `frame` | Save the screen as a picture, print the screen as text |
| `counter:game_kills=12` | Set a counter: `total_kills`, `total_deaths`, `game_kills`, `game_deaths` |
| `setting:0=2` | Set the first setting of the app to its third value |
| `battery:15` `mv:3700` | Set the battery level in percent, the battery voltage in millivolts |
| `random:7` | Start the random numbers again with this seed. An other seed gives an other sequence: try some seeds to see each result of a dice |
| `kill` `death` `background:1` | For the background app (`docs/APPS.md` section 4.6) |
| `data` | Print the rows that the app wrote with `log()` |

Read each line of the output that starts with one of these words:

- **`App error`**: the app stopped (`App error after press2: bad index`). Find the cause.
  On a StatTrak the person sees an error screen, and after 3 errors the app is switched off.
- **`Look:`**: the tool saw a thing that a person would see as a fault: a text that the edge of
  the screen cuts, a text that is not on the screen, two texts on the same pixels, an empty
  screen. Repair it, or know why it is right (an animation that moves a text in).
- **`Clicks:`**: the sounds that the app made, with the time. The clock of an app starts at
  10000 ms, so the first sound is at 10000 ms or later. A picture has no sound.

## 1. The first version: a picture in some minutes

A person wants to see their idea on the display. Do not start with questions.

1. **Say what you make**, in two sentences: what the screen shows and what each button does.
   You do not know a detail? Take the simplest answer that makes sense and say what you took.
   The person corrects it when they see the picture.
2. **Make a safe place for the work:** a git branch (`my/<short-name>`). For a Beginner: "I make
   a separate copy of the work, so the original stays as it is."
3. **Start the app:** `python tools/app.py new "NAME"`. The name has 1 to 14 characters: `A-Z`,
   `0-9`, space and `+ - % / . :`. The folder and the file get the name in small letters, with
   `-` for a space (the name `MY TIMER` gives the folder `my-timer`). If the command says that
   the app store has an app with this name already: go on. It matters only for the store
   (section 8).
4. **Read before you write:** the parts of `docs/APPS.md` and one example from the list above.
   Do not write a call from memory.
5. **Write the first version** into the new file: the main screen and the main button. No
   settings, no second screen and no saved values yet: they come when the person likes the main
   thing. A short animation or a sound is right when the app is not clear without it (a dice
   that turns). The new file has three lines for the app store: `description`, `picture` and
   `# demo:`. Keep them, and make them true when the app does what the person wants.
6. **Run it and save a picture of each step:** the start screen, then the screen after each
   important button press.
7. **Look at each picture yourself** (section 2), and repair what is wrong.
8. **Show the pictures in their sequence.** Give the full path of each file, and open them for
   the person if your tool can. Say which button press leads to which picture. Then ask: "Is
   this what you had in mind? What do you want different?" For a new person, add questions 1
   and 2 of `AGENTS.md` section 1.2 to this message. It is the only message with more than one
   question.

## 2. Look at your pictures before the person sees them

Open each picture (most AI tools can look at a PNG file), or read the `frame` text. A person
must never be the first one who sees a fault. Stop and repair when one of these is not true:

| # | Check | How |
| --- | --- | --- |
| 1 | Nothing is cut off at an edge. The display is 128 x 32: x from 0 to 127, y from 0 to 31 | The picture, and no line `Look: ... is cut off` |
| 2 | No text is on an other text or on a picture | The picture, and no line `Look: ... are on the same pixels` |
| 3 | The largest value still fits | Run with the largest value: `counter:game_kills=999`, a long time, a high score, each result of a dice (`random:SEED`). Digits get wider, texts do not get shorter |
| 4 | The main thing is the largest thing on the screen | A person reads it in a game, with the arm out. The font `small` (5 pixels high) is only for labels |
| 5 | Each state of the app has a good screen | The start, the middle, the end or the result, and the empty state (zero, no best time yet) |
| 6 | A person knows what the buttons do | The screen says it (`PRESS TO START`), or it is clear without words |
| 7 | The picture is what the person asked for | Read their words again. Not what is easy to build |

The fonts: `text` (5 x 7 pixels, all characters from space to `~`), `caps` (5 x 5) and `small`
(3 x 5): digits, upper-case letters and some signs only, `title` (the large title font).
Not sure if a font has a sign? Use the font `text`, or look at the picture. `bignum` draws
numbers only: each digit is 6 x scale pixels wide and 7 x scale high.

## 3. Change it until the person is happy

Go back to the loop after each answer of the person. The person says that it is good: go on
with section 4. The person is not happy: these offers help.

- **Other layouts, or the Screen Designer.** The person does not like the look, but cannot say
  what they want? Do not guess a third and a fourth time. Offer both ways in one message: "I
  can draw two or three other layouts, or you can draw the screen yourself with the mouse, on
  the real pixels. Which do you want?" The screens guide has both:
  `skills/stattrak-screens/SKILL.md`.
- **For the Screen Designer:** make a design file of the screens as they are now
  (`designs/<name>.json`, `docs/DESIGNS.md`) and open the designer with it (the screens guide).
  The person changes it and clicks **Send to AI**. Then you build exactly what they drew. With
  a StatTrak that is connected, **Show on StatTrak** in the designer shows the drawing on the
  real display.

**The simulator. Offer it each time, before the app goes on a StatTrak**, also when the person
is happy with the pictures. Say: "Do you want to play it on your computer first? You do not
need your StatTrak for that."

```sh
python tools/app.py sim apps/<name>/<name>.sts
```

It opens the browser with the real firmware and the app open in it. The arrow keys LEFT and
RIGHT are the two buttons. LEFT + RIGHT together open the app menu. The command installs
nothing: it uses the simulator page of the web site (the newest official firmware; it needs
the internet). `--no-browser` only makes the page and prints its path.

## 4. Test it

1. Press each button in each state of the app at least one time, with `run`. Short presses and
   holds. No line `App error`.
2. Run the checks of the app store. They find more:
   `python tools/app.py store-check apps/<name>/<name>.sts`. The lines with `FIX` are faults.
   A fault of the name, the description or the title picture can wait until section 8.
3. Is the simulator part installed? The folder `build-host` is there, or `python tools/setup.py`
   says it (the command only looks). If yes, run the app on the real firmware too:
   `python tools/sim.py --fresh boot app:apps/<name>/<name>.stapp long2 wait:500 png:<name>-fw`.
   `app:` installs the app, `long2` holds the right button for OPEN. Add the button presses of
   the app after it (`click1`, `click2`, `long1`, `long2`, `wait:MS`), then `png:`. If no: do
   not ask a Beginner to install it for an app.

Only for a pull request that adds an app to the official apps of TRIDOS: the app also needs a
test in `tests/test_sample_apps.py`. And `python tools/app.py catalog` is for the official apps
of the web page: do not run it for the app of a person.

## 5. Save the work

`git commit`. For a Beginner: "I saved a restore point."

## 6. Put it on the StatTrak

Offer it when the person is happy with the app: "Do you want it on your StatTrak now?" The
person does the steps, you guide:

1. Build the file: `python tools/app.py build apps/<name>/<name>.sts`. Give the full path of
   `apps/<name>/<name>.stapp`.
2. The web page https://stattrak.tridos.design/#device in Chrome, Edge or Opera, on a computer
   or an Android phone. Not Firefox, not Safari, not an iPhone.
3. On the StatTrak: Settings, then `Apps: SYNC`, hold the right button.
4. On the page: **Connect**, pick the StatTrak, then **From file** and the `.stapp` file.
5. The StatTrak asks `INSTALL <NAME>?`. Hold the right button for yes.

Tell the person before the install which firmware the app needs. `store-check` says it: `The
app installs on a StatTrak with firmware ... or newer` (1.2.0-b1 is the first firmware with
apps; an app with a list or `held()` needs 1.2.0-b4, `docs/APPS.md` section 7.5). If the page
says **Update the firmware first**: the StatTrak needs a newer official firmware (the Firmware
tab of the page).

## 7. Report

Say what the app does, show the pictures, and say what you tested. Say plainly: "I tested it on
the computer. I cannot test it on your StatTrak."

## 8. Share it in the app store

The app store is on the web page: https://stattrak.tridos.design/#store . Each owner can
install an app from it with one click. Each new app and each new version gets a review within
24 hours.

**When you offer it:** the person is happy with the app, and it ran without an error. Say this
in one message, and wait for one "yes":

"Do you want other StatTrak owners to get your app? I can make it ready for the app store. You
must know four things first:

1. Your app becomes **open source**: each person can download the app and its source text.
2. You sign in on the page with a Google or a GitHub account. Your maker name is public.
3. A review looks at the app. It can take 24 hours. It can say no, with a reason.
4. You upload the files yourself. I cannot sign in for you."

Never ask for a password of the person, and never type one.

**Then:**

1. Run `python tools/app.py store-check apps/<name>/<name>.sts`. Repair each line with `FIX`
   and run it again. Read each line with `NOTE`.
2. **The name is taken** (`The store has an app ... already`)? The name of an app cannot change
   after the upload, so pick a new one with the person: their own name for it, not `DICE 2`.
   Change the line `app "NAME"` in the source. The folder and the file can keep their names.
3. Do the checks that a computer cannot do. The command lists them, and `docs/APP_STORE.md`
   section 2 has the details: a true description (what the app does, and what the buttons do;
   a button that does nothing needs no words), a title picture that shows what the app is,
   content that the rules allow.
4. The command ends with `Result: ready for the upload` and the steps for the person: the two
   files with their full paths, the description to paste, the page. Give them to the person,
   and say which category fits (Games, Timers, Counters or Tools). `--open` opens the upload
   page in the browser.
5. After the upload: `docs/APP_STORE.md` section 4 (the states, and what to do when the review
   says no).

A maker can upload only 5 versions in one day. So do not use the store as a test.

## 9. When a tool does not do what you expect

| What you see | The cause | What you do |
| --- | --- | --- |
| `python` is not found | The name of the command is different on this computer | Try `python3`, and on Windows `py`. No Python at all: `AGENTS.md` section 1.3 |
| `run` prints only `Wrote out/...png` | The last action is `png:`. Then `run` prints no screen | Add `frame` as the last action |
| `App error after ...: bad index` | The app used a place of a list that is not there | Find the index. A list `var a[8]` has the places 0 to 7 |
| `App error after ...: too many steps` | One event did more than 16000 steps (4000 for an app of an older engine) | Make the loop shorter, or do a part of the work in each `tick` |
| The picture does not show your change | You gave the `.stapp` file to `run`, and it is an old build | Give the `.sts` file: `run` compiles it each time |
| The screen is empty | The `draw` part draws nothing in this state, or it draws with the color `off` on an empty screen | Read the `draw` part for this state |
| A text shows in upper-case, or a character is not there | The fonts `caps` and `small` have digits, upper-case letters and some signs only | Use the font `text`: it has all characters from space to `~` |
| `the text "..." has 21 characters; 20 is the most` | A text of an app has 20 characters at most | Use a shorter text, or two texts |
| `the package has ... bytes; 4096 is the most` | The app is too large | Smaller pictures, fewer texts, a function for code that is there two times |
| A sound does not play in the simulator or on the StatTrak | `click` or `tone` is in the `draw` part. The firmware plays no sound there | Put it into a button event or into `tick` |
| A short press comes late in a game | `press1` comes about 0.2 seconds after the release | Use `on down1` / `on down2` for a fast game (`apps/runner/`) |
| The simulator page stays empty, or the app is not in it | No internet, or the page of the web site is older than the app engine that the app needs | Try again with the internet. A developer can build the page from this folder: `--build` (it needs Docker or Emscripten: ask before an install) |
| The store says `The .stapp file is not the package of this .sts source` | The tools of this folder are older or newer than the tools of the store | Get the newest official `main` of this project, run `store-check` again, upload both files again |
| `python tools/sim.py` says `The headless simulator is not built` | The simulator part is not installed or not built | An app does not need it. Use `python tools/app.py run` and `sim` |
