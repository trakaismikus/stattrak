#!/usr/bin/env python3
"""Use the headless StatTrak simulator from a command line. Made for AI coding tools and for scripts.

The simulator runs the real firmware code on this computer. You give it a list of actions.
It prints the display as text and can save it as a PNG picture.

    python tools/sim.py boot click1 click1 frame
    python tools/sim.py boot both both both png:settings state
    python tools/sim.py --tour                     (a picture of each main view in out/)

Build the simulator first: python tools/check.py --build-only

Actions (done from left to right):
    boot              power on: hold both buttons, release them, wait for the start animation
    boot:MS           power on and keep both buttons held for MS milliseconds (10600 = update mode)
    click1 click2     short press on button 1 (kill) or button 2 (death)
    both              short press on the two buttons (next view)
    long1 long2       long press on button 1 or 2 (in settings: change the value)
    long1:MS          long press with a hold time in milliseconds
    longboth          long press on the two buttons (power off)
    press1 release1   hold / release button 1 (also press2, release2)
    wait:MS           let MS milliseconds pass
    voltage:MV        set the battery voltage in millivolts
    ota:MS            a phone sends new firmware, the transfer takes MS milliseconds
    accept            a phone accepts the new firmware
    ble:ID            a phone sends a Bluetooth user command (ID in hex, example: ble:0100)
    ble:ID:HEX        the same, with data bytes as hex text (example: ble:0101:05ff)
    cut               remove the power immediately
    frame             print the display as text ('#' = pixel on)
    png:NAME          save the display as out/NAME.png
    state             print the device state (view, counters, settings, flash values)
    set:KEY=VALUE     change a value in flash while the device is off (example: set:auto_off=1)

The flash content stays in out/device_state.txt between runs. Use --fresh for a new device.
"""

import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tests"))

from stattrak_sim import Device, SimCrash, SimError  # noqa: E402


def print_state(dev):
    for key, value in dev.state().items():
        print("%s=%s" % (key, value))


def do_action(dev, action, out_dir, voltage):
    name, _, arg = action.partition(":")

    if name == "boot":
        if arg:
            dev.power_on(hold="both", voltage=voltage)
            dev.run(int(arg))
            if dev.is_on:
                dev.release(1)
                dev.release(2)
                dev.run(300)
        else:
            dev.boot(voltage=voltage)
    elif name in ("click1", "click2"):
        dev.click(int(name[-1]))
    elif name == "both":
        dev.click_both()
    elif name in ("long1", "long2"):
        dev.long_press(int(name[-1]), hold_ms=int(arg) if arg else 800)
    elif name == "longboth":
        dev.long_press_both()
    elif name in ("press1", "press2"):
        dev.press(int(name[-1]))
    elif name in ("release1", "release2"):
        dev.release(int(name[-1]))
    elif name == "wait":
        dev.run(int(arg))
    elif name == "voltage":
        dev.set_voltage(int(arg))
    elif name == "ota":
        dev.ota_transfer(int(arg))
    elif name == "accept":
        print("accepted=%d" % (1 if dev.ota_accept() else 0))
    elif name == "ble":
        cmd_id, _, data = arg.partition(":")
        answer = dev.ble_command(int(cmd_id, 16), bytes.fromhex(data))
        if answer is None:
            print("ble %s: no answer" % cmd_id)
        else:
            print("ble %s: status=%d data=%s" % (cmd_id, answer[0], answer[1].hex() or "(none)"))
    elif name == "cut":
        dev.power_cut()
    elif name == "frame":
        print(dev.frame_text())
    elif name == "png":
        path = os.path.join(out_dir, (arg or "screen") + ".png")
        dev.save_png(path)
        print("saved %s" % path)
    elif name == "state":
        print_state(dev)
    elif name == "set":
        key, _, value = arg.partition("=")
        dev.write_flash({key: int(value, 0)})
    else:
        raise SimError("unknown action: %s (see python tools/sim.py --help)" % action)

    if dev.ended is not None and name not in ("cut", "set"):
        reason, time_ms, detail = dev.ended
        print("session ended: %s at %d ms (%s)" % (reason, time_ms, detail))
        dev.power_cut()


def tour(dev, out_dir):
    """Save a picture of each main view."""
    dev.boot()
    for _ in range(3):
        dev.click(1)
    for view in ("total", "kill_dead", "stats", "settings"):
        dev.run(400)
        print("--- %s (view %s)" % (view, dev.view))
        print(dev.frame_text())
        print("saved %s" % dev.save_png(os.path.join(out_dir, "tour_%s.png" % view)))
        dev.click_both()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("actions", nargs="*", help="actions, done from left to right")
    parser.add_argument("--out", default=os.path.join(ROOT, "out"), help="folder for pictures and the device state")
    parser.add_argument("--fresh", action="store_true", help="start with a new device (empty flash)")
    parser.add_argument("--voltage", type=int, default=4000, help="battery voltage in millivolts (default 4000)")
    parser.add_argument("--tour", action="store_true", help="save a picture of each main view")
    args = parser.parse_args()

    if not args.actions and not args.tour:
        parser.print_help()
        return 2

    os.makedirs(args.out, exist_ok=True)
    state_file = os.path.join(args.out, "device_state.txt")
    if (args.fresh or args.tour) and os.path.isfile(state_file):
        os.remove(state_file)

    try:
        dev = Device(state_file=state_file)
    except FileNotFoundError as error:
        print(error)
        return 2

    try:
        if args.tour:
            tour(dev, args.out)
        for action in args.actions:
            do_action(dev, action, args.out, args.voltage)
        return 0
    except SimCrash as error:
        print("FIRMWARE ERROR: %s" % error)
        return 1
    except SimError as error:
        print("ERROR: %s" % error)
        return 2
    finally:
        dev.close()


if __name__ == "__main__":
    sys.exit(main())
