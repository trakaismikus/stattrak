<!-- Thank you for your proposal. CONTRIBUTING.md has the rules in plain words.
     The title above: what an owner gets, in plain words. Good: "Kill streak: a sound after
     5 kills in a row". Not good: "Update app.c".
     An app does not go here: it goes to the app store (CONTRIBUTING.md, way 1). -->

## What does this change do?

<!-- For a person who never saw code. Two or three sentences.
     A feature: what does the owner see, and which button does what? When is it useful?
     A fix: use the three lines below and delete this comment. -->

<!-- Only for a fix:
**Before:** what went wrong.
**How to see the fault:** the steps on the device or in the simulator.
**Now:** what happens with the fix.
-->

## Pictures from the simulator

<!-- python tools/sim.py boot ... png:name   Drag the pictures from the folder out/ into this field.
     Say which button press leads to which picture. For a fix: before and after. -->

## For the reviewer

<!-- The files that changed, and why, in one line each. New values in flash and new settings
     (the key and the default). -->

## Checks

- [ ] `python tools/check.py` shows `OK` for all 4 steps
- [ ] The change has a test in `tests/` (for a fix: the test fails without the fix)
- [ ] It does one thing only
- [ ] It does not change the protected parts (`components/ble_ota/`, `components/app_ble/`, `main/main.c`, `main/board*`, `partitions.csv`, `sdkconfig.*`, `tools/check_safety.py`)
- [ ] New values in flash are at the end of `ENTRY_LIST`, new setting values are at the end of their list

Firmware size, from `python tools/build.py`: <!-- bytes, or "not built" -->

## Was it on a real device?

- [ ] No, only in the simulator
- [ ] Yes. The Bluetooth update to this firmware worked, and the update mode opened again after it.

## Made with

<!-- Optional: the AI tool that you used, and anything a reviewer should know. -->
