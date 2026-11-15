# Screen design files

A design file is a drawing of one or more StatTrak screens. It says exactly which pixel is on.
People and AI tools use it to agree on how a screen looks **before** code is written.

- A person can make and change one with the Screen Designer: open `updater/designer/index.html`
  in a browser (double click). It has undo, more than one screen, "Save", "Open" and "Send to AI".
  It opens with the 4 built-in screens of the firmware (`designs/stattrak-apps.json`: Big counter,
  Split counter, Stats, Settings; `tests/test_default_design.py` checks them against the
  simulator, pixel for pixel). Tools: Move, Select (a box around parts, or around some pixels of
  a drawing, to move them as a group), Text, Box, Circle, Line, Draw, Erase and Trace (a picture
  under the screen to draw over; it stays in the browser and is not in the file). A black part
  is red while it is selected. "New screen" also takes a screen that an AI tool made: the
  designer gives the request to copy, and the answer is pasted back.
- An AI tool can write one by hand: it is a small JSON text file.
- `tools/design.py` turns it into pictures, into a text that an AI tool can read, and into the
  C draw code for the firmware.

```sh
python tools/design.py check    designs/example.json   # is the file correct?
python tools/design.py render   designs/example.json   # a PNG picture of each screen in out/
python tools/design.py describe designs/example.json   # notes, elements and pixels as text
python tools/design.py code     designs/example.json   # C draw code for a firmware view
```

The pictures are exact. The fonts and pictures come from the firmware source
(`updater/designer/assets.js`, made by `python tools/design.py assets`). The test
`tests/test_design.py` draws each kind of element with `tools/design.py` and with the real
firmware draw functions, and compares all pixels. `tests/test_designer.py` does the same
for the drawing code of the Screen Designer (`updater/designer/design.js`).

## Send a design to an AI tool

The button "Send to AI" in the Screen Designer makes one text to paste into an AI coding tool.
It has three parts:

1. What the person wants, and what the AI tool must do: save the design data as
   `designs/<name>.json`, run `python tools/design.py check` and `describe`, follow `AGENTS.md`.
2. Each screen in words: its note, and its elements with exact positions and sizes.
3. The design data: the file itself, as a JSON block. This is the exact design.

Why this form: the words are for people and for a first understanding, and the data is what
the tools use. An AI tool that works in the firmware folder gets pixel pictures and draw
code from `tools/design.py`, so the text does not need them. The option "Add each screen as
a text picture" is for an AI tool that cannot run the tools. "Download pictures" gives PNG
files for an AI tool that can look at images.

## The display

- 128 pixels wide, 32 pixels high.
- A pixel is **on (white)** or **off (black)**. No grey, no colors.
- `x` goes to the right from 0 to 127. `y` goes down from 0 to 31. The top left pixel is `x=0, y=0`.
- An element can be partly outside of the display. That part is not drawn.

## The file

```json
{
  "format": "stattrak-design",
  "version": 1,
  "name": "My screens",
  "screens": [
    {
      "id": "home",
      "name": "Home",
      "note": "Shows the total kills. Button 1 adds a kill.",
      "elements": [
        { "type": "text", "x": 64, "y": 12, "text": "01337", "font": "numbers", "align": "center" }
      ]
    }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `id` | A short name for the code: letters, digits and `_`, starts with a letter. The draw function gets this name (`_drawHome`). |
| `name` | The name for people. |
| `note` | **What the screen does:** when it shows, what each button does, which texts are live values. A drawing cannot show behaviour. Write it here. |
| `elements` | What is drawn, in this order. A later element is on top. |

Each element can have `"note"` (for example `"the live kill count"`) and `"color"`:
`"white"` (pixels on, the default) or `"black"` (pixels off, to cut something out of a white area).

Each element can also have `"border"`: a **cut-out border** of 0 to 4 pixels. Before the element
is drawn, all pixels up to that many pixels around it are switched off, so the element stays
clear on top of other parts (for example a text on a picture). It follows the shape of the
element: the firmware draws the element black at each place from `-border` to `+border`
pixels around it (`python tools/design.py code` writes the loop). For an inverted text the white
box grows instead.

### Text

```json
{ "type": "text", "x": 4, "y": 2, "text": "KILLS", "font": "5x5", "align": "left", "scale": 1, "gap": false, "invert": false }
```

| Field | Values |
| --- | --- |
| `font` | See the table below. Default `5x7`. |
| `x`, `y` | `y` is the top of the text. `x` depends on `align`. |
| `align` | `left`: `x` is the left edge. `center`: `x` is the center. `right`: the text ends 1 pixel before `x`. |
| `scale` | 1 to 8. Each font pixel becomes a square of this size. Default 1. |
| `gap` | With `scale` 2 or more: `true` leaves 1 pixel between the squares (a dot-matrix look). |
| `invert` | `true`: black text in a white box that is 1 pixel larger on each side. Only with `scale` 1. |

Only the characters from space to `~` (ASCII). One line. No letters with marks.

| Font | Height | Width of a character | Has |
| --- | --- | --- | --- |
| `5x7` | 7 | 5, then 1 pixel space | all characters, capital and small letters |
| `5x5` | 5 | 5, then 1 pixel space | capital letters (small letters are drawn as capitals), digits, signs |
| `3x5` | 5 | 3, then 1 pixel space | capital letters, digits, signs. Very small. |
| `big` | 7 | different for each letter (bold), then 1 pixel space | capital letters, digits, signs. For titles. |
| `numbers` | 14 | 12 for most digits, then 3 pixels space | the digits 0 to 9 and `-` only. For the main counter. |

Five digits in `numbers` are about 72 pixels wide. Text in `5x7` at scale 2 is 14 pixels high.
`python tools/design.py describe` prints the exact size of each text.

### Rectangle

```json
{ "type": "rect", "x": 0, "y": 10, "w": 128, "h": 12, "fill": false, "radius": 0 }
```

`w` and `h` are the size in pixels, 1 or more. `fill`: `true` for a filled rectangle,
`false` for an outline of 1 pixel. `radius`: round corners, 0 to `(smaller side - 1) / 2`.

### Line

```json
{ "type": "line", "x1": 0, "y1": 9, "x2": 127, "y2": 9 }
```

A line of 1 pixel from the first point to the second point, the two points included.

### Circle

```json
{ "type": "circle", "x": 64, "y": 16, "r": 10, "fill": false }
```

`x`, `y` is the center, `r` the radius (0 to 127). The circle is `2 * r + 1` pixels wide and high.
`fill`: `true` for a filled circle, `false` for an outline of 1 pixel. The firmware draws it with
`Gfx_drawCircle` / `Gfx_fillCircle`.

### Picture from the firmware

```json
{ "type": "image", "name": "warning", "x": 0, "y": 0 }
```

| `name` | Size | Picture |
| --- | --- | --- |
| `stattrak` | 128 x 32 | the StatTrak logo |
| `tridos` | 128 x 32 | the TRIDOS logo |
| `warning` | 32 x 32 | a warning triangle |

### Free pixels

```json
{ "type": "pixels", "x": 10, "y": 3, "rows": ["#..#", ".##.", "#..#"] }
```

A small picture that you draw yourself. Each text in `rows` is one line of pixels, from the top.
`#` is a pixel that is drawn, `.` is a pixel that stays as it is. All lines have the same length.
128 x 32 at most.

## From a design to the firmware

`python tools/design.py code designs/my.json` prints one C function for each screen:

```c
// Screen "Big counter": The main screen. It shows the total kills as 5 digits.
static void _drawCounter(void) {
  Gfx_drawStringAligned(64, 1, "TOTAL KILLS", &font3x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  Gfx_drawLine(20, 7, 107, 7, 1);
  Gfx_drawStringAligned(64, 12, "01337", &kFontNightverbLargeNumbers, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);  // the live total kills value
}
```

Put the function into the view that shows the screen (see "Views" in `AGENTS.md`) and call it
from the `draw` function of the view. Replace the fixed texts with the real values
(`snprintf` into a small buffer). Free pixels become a `static const uint8_t` table in the
picture format of the firmware.

The code is a start, not the full view: the view also needs its button handling, and often an
animation or a state. The `note` of the screen says what it must do.

## Rules for AI tools

- Make the design files first, show the pictures, and build after the person picked one
  (`AGENTS.md`, section 5).
- Use the notes. Each screen says when it shows and what the buttons do. Each text that is a
  live value says so in its `note`.
- Keep texts away from the edges by 1 pixel or more, if the design allows it. Lines of text
  need 1 pixel or more between them.
- Check each file with `python tools/design.py check` before you show it.
- Keep the design of each screen that is in the firmware in `designs/`, so the next person can
  change it.
