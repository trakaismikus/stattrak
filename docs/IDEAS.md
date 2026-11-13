# Ideas

The idea inbox. A dated entry for each idea about future functionality, with enough context to
act on it later. When an idea is planned, move it to docs/ROADMAP.md.

## 2026-11-12: A device log, read over Bluetooth

Keep a small log on the device (at least for a short time): reset reasons, brown-outs, the step
where a Bluetooth start failed, battery values. A web page connects and reads it. Context: the
"update screen turns the device off" bug needed a special test firmware (branch `diag/bt-memory`)
and photos of the screen to find the cause (a brown-out at the radio start). A log would have
shown it at once. Apps may need a way to send data to the browser too (see the next idea).

## 2026-11-12: Battery drain calibration app

An app logs the battery voltage from full to empty. The web page reads the log and makes a
calibrated %-map for this device, so the battery indicator is more accurate per device. Needs a
way to read app data over Bluetooth (see the device log idea).

## 2026-11-13: Less radio power at the radio start

The soft radio start (the Bluetooth sign, docs/BLUETOOTH.md "Radio start") fixed the brown-out
on the weak unit: no failure in the owner's tests, also with a high-power flashlight on the same
battery. Later, test if a lower Bluetooth transmit power (now +9 dBm) gives more margin. Measure
the range too. Not needed now.
