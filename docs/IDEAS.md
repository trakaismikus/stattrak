# Ideas

The idea inbox. A dated entry for each idea about future functionality, with enough context to
act on it later. When an idea is planned, move it to docs/ROADMAP.md.

## 2026-11-12: Battery drain calibration app

An app logs the battery voltage from full to empty. The web page reads the log and makes a
calibrated %-map for this device, so the battery indicator is more accurate per device. The log
part is done (2026-11, the BATTERY LOG app and the app data of docs/DATA.md). Still open: the
%-map from the log, and a way to send it to the device.

## 2026-11-15: Start picture with live values

The start picture (docs/DATA.md section 4) is a fixed picture. Later: live values on it (total
kills, battery), or a short animation. Not in the first version (decided 2026-11-15).

## 2026-11-15: Marker files for video editors

CLIP MARKS gives a CSV with the time of each kill. Later: export the marks in the format of a
video editor (EDL, Premiere or DaVinci Resolve markers), so they appear on the timeline at once.

## 2026-11-13: Less radio power at the radio start

The soft radio start (the Bluetooth sign, docs/BLUETOOTH.md "Radio start") fixed the brown-out
on the weak unit: no failure in the owner's tests, also with a high-power flashlight on the same
battery. Later, test if a lower Bluetooth transmit power (now +9 dBm) gives more margin. Measure
the range too. Not needed now.
