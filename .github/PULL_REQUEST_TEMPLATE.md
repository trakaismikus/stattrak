<!-- Thank you for your proposal. CONTRIBUTING.md has the rules in plain words. -->

## What does this change do?

<!-- Two or three sentences. What does the user see? -->

## Pictures from the simulator

<!-- python tools/sim.py boot ... png:name   Drag the pictures from the folder out/ into this field. -->

## Checks

- [ ] `python tools/check.py` shows `OK` for all 4 steps
- [ ] The feature has a test in `tests/`
- [ ] It does one thing only
- [ ] It does not change the protected parts (`components/ble_ota/`, `components/app_ble/`, `main/main.c`, `main/board*`, `partitions.csv`, `sdkconfig.*`, `tools/check_safety.py`)
- [ ] New values in flash are at the end of `ENTRY_LIST`, new setting values are at the end of their list

Firmware size, from `python tools/build.py`: <!-- bytes, or "not built" -->

## Was it on a real device?

- [ ] No, only in the simulator
- [ ] Yes. The Bluetooth update to this firmware worked, and the update mode opened again after it.

## Made with

<!-- Optional: the AI tool that you used, and anything a reviewer should know. -->
