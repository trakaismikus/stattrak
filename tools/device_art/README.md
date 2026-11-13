# Device art

The picture of StatTrak in the web updater and the web simulator, and the animations on its
display.

| File | What it is |
| --- | --- |
| `render_device.py` | Blender script: the front view of the 3D model, transparent background, display off |
| `finish_render.py` | Adds the fine grain of the metal (the render's noise filter removes it) and writes the WebP |
| `fw101/` | The way to the update mode on firmware 1.0.1, from the 1.0.1 simulator (see below) |

The results are in the updater: `updater/assets/device.webp` and
`updater/assets/update-path-1.0.1.js`. `updater/js/device.js` shows them.

## The picture

```sh
blender -b "StatTrak model.blend" --python tools/device_art/render_device.py -- render.png 128
python tools/device_art/finish_render.py render.png updater/assets/device.webp
```

The 3D model (`StatTrak model.blend`, the exact geometry of the device) is not in this repository:
TRIDOS keeps it. Blender 5.x. About 40 seconds on a desktop computer.

The look: matte black anodised aluminium (not metallic, rough, a fine grain), laser etched text in
light grey, a soft light from above and faint rim lights, so the black body shows on a black page
and on a white page. The render removes the glass and the "LED counter" plane of the model (a
plane with square corners in front of the glass) and puts a black glass into the rounded pocket.

### Places in the picture

An orthographic front view at 30 pixels per mm: 1896 x 1024 pixels = 63.2 x 34.133 mm, the device
(59.2 x 30.15 mm) plus 2 mm on each side. Front face coordinates in mm, from the top left of the
device box:

| Part | mm | % of the picture (left, top, width, height) |
| --- | --- | --- |
| Display, 128 x 32 pixels, pitch 0.325 mm | 5.55..47.15 x 5.84..16.24 | 11.946, 22.969, 65.823, 30.469 |
| Glass (the pocket, radius 0.8) | 3.475..49.225 x 3.495..18.595 | 8.663, 16.099, 72.389, 44.239 |
| Inner window of the body (behind the glass, radius 0.75) | 4.97..47.73 x 4.99..17.09 | |
| Button 1 (round, under the body) | 10.85..18.35 x 25.5..29.5 | 20.332, 80.567, 11.867, 11.719 |
| Button 2 | 40.85..48.35 x 25.5..29.5 | 67.801, 80.567, 11.867, 11.719 |

The display: the pixel pitch is measured on a product photo (4 pixels = 1.30 mm), and the pixels
are centred in the inner window. `updater/css/main.css` (`.st-dev__*`), `updater/js/device.js`
and `sim/web/shell.html.in` use these numbers; `updater/tests/device.test.js` checks that they are
the same.

## The animations

- **Apps tab** (`sync-idle`, `sync-question`, `sync-installed`): the screens of Settings >
  "Apps: SYNC", drawn by `updater/js/apps-pictures.js`. They follow the firmware:
  `tests/make_sync_screens.py` takes the screens from the headless simulator into
  `updater/tests/sync-screens.json`; `tests/test_updater_screens.py` (firmware checks) fails when
  the firmware draws them in an other way, and `updater/tests/apps.test.js` fails when the page
  draws them in an other way than the file.
- **Firmware tab, step 1** (`update-1.0.1`): the way to the update mode on **firmware 1.0.1**: the
  counter, both buttons to Settings, button 2 down to `FIRMWARE: UPDATE`, hold button 2,
  `FIRMWARE UPDATE` / `Waiting...`. 1.0.1 on purpose: most devices that come to the updater have
  it, and their owners do not know the update path yet. When most devices have the new firmware,
  make this animation from the simulator of this repository (`tests/stattrak_sim.py`) instead.

### Make the 1.0.1 animation again

```sh
git -C <old firmware repository> archive v1.0.1 | tar -x -C /tmp/fw101
python tools/device_art/fw101/make_update_path.py /tmp/fw101
```

It compiles the 1.0.1 simulator (`sim/simulator.c` of that version) with `fw101/SDL.h` and
`fw101/headless.c` (no window: virtual time, scripted buttons), runs `fw101/flow.txt` and writes
`updater/assets/update-path-1.0.1.js` (a script, so the page also works when opened as a file).
It needs a C compiler: zig from `.venv` (`python tools/setup.py simulator`) or gcc.
