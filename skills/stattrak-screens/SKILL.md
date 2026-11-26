---
name: stattrak-screens
description: Design how a screen of the StatTrak kill counter looks (128 x 32 pixels, one color). Draw 2 or 3 proposals as exact pictures, open the Screen Designer page for the person, and build the screen that a person drew (the "Send to AI" text). Use for a new screen or a change of a look, in an app or in the firmware.
---

# Screen designs: show options before you build

A drawing of a screen is cheaper than code, and people decide faster with pictures.

**When:** the person describes a new screen, or a change to how a screen looks. Always for a
Beginner. For other levels: when they ask for options, or the look is not clear.

**Not for the first version of an app.** The app guide (`skills/stattrak-app/SKILL.md`) shows a
first version that works, with no question first. Use this guide for an app when the person
does not like the look of that version.

**How:**

1. Write 2 or 3 design files: `designs/<feature>-a.json`, `-b.json`, `-c.json`. Make them
   really different (layout, font size, what is in the center), not three copies.
   `docs/DESIGNS.md` has the file format. `designs/example.json` is an example.
2. Check and draw them:

   ```sh
   python tools/design.py check  designs/<feature>-a.json
   python tools/design.py render designs/<feature>-a.json     # pictures in out/
   ```

3. Show the pictures side by side. Say in one line each what is different. Ask which one the
   person likes, and what to change.
4. Change the picked design until it is right. Then get the draw code:
   `python tools/design.py code designs/<feature>-a.json`. It prints one C function for each
   screen with the exact `Gfx_*` calls. Put it into the view (the code guide of the firmware,
   `skills/stattrak-firmware/code.md`, section 1) and replace the
   fixed texts with the real values.
5. Keep the final design file in `designs/`. Delete the proposals that were not picked.

**For an app** the steps are the same, but the draw code is StatTrak Script and not C. Use the
positions and sizes that `python tools/design.py describe designs/<feature>-a.json` prints, and
write them as the drawing calls of `docs/APPS.md` section 4 (`text`, `num`, `bignum`, `rrect`,
`fill`, `bitmap`). Then compare the picture of the app (`python tools/app.py run ... png:...`)
with the picture of the design.

**The person is not happy with how a screen looks?** Do not guess a third and a fourth time.
Offer both ways: "I can draw two or three new proposals, or you can draw the screen yourself in
the Screen Designer. Which do you want?"

The pictures are exact: `tools/design.py` uses the fonts of the firmware, and a test compares
its drawing with the real firmware draw functions.

**The Screen Designer.** `updater/designer/index.html` is a page where a person draws screens
with the mouse: the real pixels, the real fonts, more than one screen, undo, save. It opens
with a double click, without a server and without the internet. It is also on the web site:
`https://stattrak.tridos.design/designer/index.html`. Use it in both directions:

- **From you to the person.** After you made design files, say: "You can change the designs
  yourself. Open the file `updater/designer/index.html` in your browser, press Open and select
  the file `designs/<feature>-a.json`." Open the page for the person if you can.
- **From the person to you.** The button "Send to AI" in the designer makes a text that
  starts with `StatTrak screen design`. When the person pastes it:
  1. Save the JSON block at its end as the file that the text names (`designs/<name>.json`).
     Copy it exactly.
  2. `python tools/design.py check designs/<name>.json`
  3. `python tools/design.py describe designs/<name>.json` prints the notes of the person, all
     elements with their positions, and the pixels of each screen.
  4. Build exactly what it shows. Do not move elements "to make it nicer". Ask first.

  A person can also give you the saved `.json` file. Put it into `designs/` and do steps 2 to 4.

Limits of the display that you must respect in each design: 128 x 32 pixels, on or off only
(no grey, no color), 5 fonts, text in one line, the characters from space to `~` only.

The rules for each task are in `AGENTS.md` of the project (section 1: the person and their
level, section 3: the safety rules). An app: `skills/stattrak-app/SKILL.md`. A firmware change: `skills/stattrak-firmware/SKILL.md`.
