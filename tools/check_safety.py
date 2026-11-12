#!/usr/bin/env python3
"""Safety check: finds changes that can make a StatTrak device unusable ("bricked").

A StatTrak gets new firmware over Bluetooth only. If a firmware cannot start, cannot keep
the power on, or cannot open the firmware update mode, the user cannot repair the device.
This script checks the parts of the source code that protect against that. It does not
build anything and it needs only Python 3.

Run:   python tools/check_safety.py
Rules: python tools/check_safety.py --explain

If a rule fails and the change was on purpose, read docs/SAFETY.md first. A rule must
change in this file in the same commit, with the reason in the commit message.
"""

import argparse
import hashlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SOURCE_DIRS = ["main", "components"]

# sha256 of partitions.csv (with LF line ends) that is in the flash of all released devices.
PARTITIONS_SHA256 = "2059ebb2b59049ff7bac2431e946af2e85d623ab812b4c1509bda31413f30582"

# Settings that the released devices depend on.
REQUIRED_CONFIG = {
    "CONFIG_IDF_TARGET": '"esp32c2"',
    "CONFIG_XTAL_FREQ_26": "y",
    "CONFIG_XTAL_FREQ": "26",
    "CONFIG_ESPTOOLPY_FLASHSIZE_4MB": "y",
    "CONFIG_PARTITION_TABLE_CUSTOM": "y",
    "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME": '"partitions.csv"',
    "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE": "y",
    "CONFIG_BT_ENABLED": "y",
    "CONFIG_BT_NIMBLE_ENABLED": "y",
}

# Settings that burn eFuses. An eFuse cannot be changed back. Never switch these on.
FORBIDDEN_CONFIG = [
    "CONFIG_SECURE_BOOT",
    "CONFIG_SECURE_FLASH_ENC_ENABLED",
    "CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT",
    "CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK",
]

# Pins of the released hardware (main/board.h).
REQUIRED_PINS = {
    "BTN_PIN": 4,
    "BUZZER_PIN": 5,
    "EN_12V_PIN": 6,
    "OLED_DIN_PIN": 7,
    "OLED_CS_PIN": 18,
    "OLED_RST_PIN": 10,
    "OLED_DC_PIN": 9,
    "OLED_SCLK_PIN": 8,
    "EN_BAT_ADC_PIN": 3,
    "ADC_BAT_PIN": 2,
    "PWR_ON_PIN": 1,
    "BTN2_PIN": 0,
}

# Names of values in flash (NVS) that released firmware uses. They must stay, with the same name.
RELEASED_NVS_KEYS = [
    "init", "total_kills", "total_deaths", "game_kills", "game_deaths", "battery_type",
    "show_battery", "brightness", "dim_screen", "dim_level", "auto_off", "counter", "sound",
    "low_bat",
]

# Functions that write the firmware slots or select the firmware to start. Only the update code
# (components/ble_ota/ota.c) can use them.
OTA_ONLY_FUNCTIONS = [
    "esp_ota_begin", "esp_ota_write", "esp_ota_end", "esp_ota_abort", "esp_ota_set_boot_partition",
    "esp_ota_mark_app_valid_cancel_rollback", "esp_ota_mark_app_invalid_rollback_and_reboot",
]
OTA_FILE = "components/ble_ota/ota.c"

# Functions that can destroy the bootloader, the partition table or the eFuses. Never use them.
FORBIDDEN_FUNCTIONS = [
    "esp_flash_write", "esp_flash_erase_region", "esp_flash_erase_chip", "esp_flash_write_encrypted",
    "esp_partition_write", "esp_partition_write_raw", "esp_partition_erase_range",
    "spi_flash_write", "spi_flash_erase_range", "spi_flash_erase_sector",
    "esp_efuse_write_field_blob", "esp_efuse_write_field_bit", "esp_efuse_write_field_cnt",
    "esp_efuse_write_reg", "esp_efuse_write_block", "esp_efuse_write_key", "esp_efuse_write_keys",
    "esp_efuse_batch_write_begin", "esp_efuse_batch_write_commit", "esp_efuse_set_write_protect",
    "esp_efuse_set_read_protect",
    "esp_flash_encryption_init_checks", "esp_secure_boot_permanently_enable",
]

# nvs_flash_erase() removes all stored values. Only these files can call it.
NVS_ERASE_FILES = ["main/main.c", "main/eeprom.c"]

RULES = [
    ("partition-table",
     "partitions.csv must not change.",
     "The partition table is in the flash of each device. A Bluetooth update cannot replace it. "
     "Firmware that is built for a different table writes to the wrong place or does not start."),
    ("sdk-config",
     "sdkconfig.defaults must keep the chip, crystal, flash size, partition table, Bluetooth and rollback settings.",
     "Rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) lets the bootloader go back to the old firmware when the "
     "new firmware is not accepted. A wrong crystal frequency stops Bluetooth. A wrong target does not start."),
    ("no-efuse-config",
     "Secure boot, flash encryption and anti-rollback must stay off.",
     "They burn eFuses in the chip. That cannot be changed back, and other firmware does not start after it."),
    ("pins",
     "The pin numbers in main/board.h must not change.",
     "PWR_ON_PIN keeps the power on. A wrong pin switches the device off when the buttons are released. "
     "A wrong output pin can also damage the hardware."),
    ("accept-only-from-phone",
     "Only the Bluetooth 'accept' command can mark a new firmware as good.",
     "A new firmware starts in the state 'pending'. The phone accepts it over Bluetooth, and that shows that "
     "Bluetooth update works in the new firmware. If the firmware accepts itself, a firmware with a broken "
     "update function stays on the device for ever."),
    ("update-functions",
     "Functions that write firmware or select the start firmware are used only in components/ble_ota/ota.c.",
     "A wrong call can erase the firmware that runs now, or select an empty slot."),
    ("no-raw-flash",
     "No direct flash write, flash erase or eFuse write functions.",
     "They can destroy the bootloader or the partition table. A Bluetooth update cannot repair that."),
    ("nvs-erase",
     "nvs_flash_erase() is called only in main/main.c and main/eeprom.c.",
     "It removes all counters and settings of the user."),
    ("start-sequence",
     "main/main.c must set the power latch first, must show the accept view for a pending firmware, "
     "and must keep the '10 seconds hold' way into the update mode.",
     "These are the ways to repair a device with a bad firmware."),
    ("update-views",
     "The update view and the accept view must start the Bluetooth update service, and nothing there or "
     "in the start code can refuse new firmware. 'Both buttons' must open the app menu, and the app menu "
     "must always end with SETTINGS (the way to 'Firmware: UPDATE'): _buildMenu() in launcher_view.c adds it "
     "in code, never from flash.",
     "Without Bluetooth in these views the phone cannot send or accept firmware. Ota_setUpdateAllowed(false) "
     "is for views of user features only: in the update path it makes the device refuse each new firmware."),
    ("nvs-keys",
     "The released flash value names must stay, and each name has 15 characters at most.",
     "A renamed value is lost for users (counters go to zero). A name that is too long is never stored."),
    ("bluetooth-identity",
     "The Bluetooth name, the update service numbers, the update command numbers and the sector size "
     "must not change. Only main/views/app_sync_view.c may give Bluetooth an other name for one start "
     "(AppBle_setNextName), and AppBle_init() must forget that name after it used it.",
     "The web updater finds the device and talks to it with them. If they change, it cannot update or accept "
     "firmware. Command numbers for user features start at 0x0100. A name of the person in the update view "
     "or the accept view would hide the device from the web updater."),
]


class Checker:
    def __init__(self, root):
        self.root = root
        self.failures = []

    # --- Helpers ---------------------------------------------------------------------------------

    def fail(self, rule, message):
        self.failures.append((rule, message))

    def path(self, relative):
        return os.path.join(self.root, *relative.split("/"))

    def read(self, relative, rule=None):
        try:
            with open(self.path(relative), "r", encoding="utf-8", errors="replace", newline="") as handle:
                return handle.read().replace("\r\n", "\n")
        except OSError:
            if rule:
                self.fail(rule, "File is missing: %s" % relative)
            return None

    def source_files(self):
        for source_dir in SOURCE_DIRS:
            base = self.path(source_dir)
            for directory, _, names in os.walk(base):
                for name in sorted(names):
                    if name.endswith((".c", ".h", ".cpp", ".hpp")):
                        full = os.path.join(directory, name)
                        yield os.path.relpath(full, self.root).replace(os.sep, "/")

    @staticmethod
    def strip_comments(text):
        text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
        return re.sub(r"//[^\n]*", "", text)

    def config_files(self):
        names = [name for name in os.listdir(self.root) if name.startswith("sdkconfig") and name != "sdkconfig.old"]
        return sorted(names)

    # --- Rules -----------------------------------------------------------------------------------

    def check_partition_table(self):
        text = self.read("partitions.csv", "partition-table")
        if text is None:
            return
        digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
        if digest != PARTITIONS_SHA256:
            self.fail("partition-table", "partitions.csv is not the released partition table (sha256 %s)." % digest)

    def check_sdk_config(self):
        text = self.read("sdkconfig.defaults", "sdk-config")
        if text is None:
            return
        values = dict(re.findall(r"^(CONFIG_[A-Z0-9_]+)=(.*)$", text, flags=re.M))
        for key, expected in REQUIRED_CONFIG.items():
            if values.get(key, "").strip() != expected:
                self.fail("sdk-config", "sdkconfig.defaults must have %s=%s (it has: %s)."
                          % (key, expected, values.get(key, "nothing")))

        # No other sdkconfig file can switch a required setting off
        for name in self.config_files():
            other = self.read(name)
            if other is None or name == "sdkconfig.defaults":
                continue
            for key, expected in REQUIRED_CONFIG.items():
                for match in re.finditer(r"^%s=(.*)$" % re.escape(key), other, flags=re.M):
                    if match.group(1).strip() != expected:
                        self.fail("sdk-config", "%s changes %s to %s." % (name, key, match.group(1).strip()))
                if re.search(r"^# %s is not set" % re.escape(key), other, flags=re.M):
                    self.fail("sdk-config", "%s switches %s off." % (name, key))

    def check_no_efuse_config(self):
        for name in self.config_files():
            text = self.read(name)
            if text is None:
                continue
            for line in text.split("\n"):
                line = line.strip()
                if line.startswith("#") or "=" not in line:
                    continue
                key, _, value = line.partition("=")
                if key.strip() in FORBIDDEN_CONFIG and value.strip() == "y":
                    self.fail("no-efuse-config", "%s has the forbidden setting %s." % (name, line))

    def check_pins(self):
        text = self.read("main/board.h", "pins")
        if text is None:
            return
        code = self.strip_comments(text)
        for pin, expected in REQUIRED_PINS.items():
            definitions = re.findall(r"^[ \t]*#[ \t]*define[ \t]+%s\b[ \t]*(.*)$" % pin, code, flags=re.M)
            if len(definitions) != 1:
                self.fail("pins", "main/board.h must have one definition of %s (found %d)." % (pin, len(definitions)))
                continue
            # The value must be a plain number: "(1)" or "1". An expression is not accepted.
            value = re.fullmatch(r"\(?\s*(\d+)\s*\)?", definitions[0].strip())
            if not value or int(value.group(1)) != expected:
                self.fail("pins", "%s must be %d (it is: %s)." % (pin, expected, definitions[0].strip()))

    def check_function_use(self):
        for relative in self.source_files():
            text = self.read(relative)
            if text is None:
                continue
            code = self.strip_comments(text)

            for name in FORBIDDEN_FUNCTIONS:
                if re.search(r"\b%s\s*\(" % name, code):
                    self.fail("no-raw-flash", "%s uses %s()." % (relative, name))

            if relative != OTA_FILE:
                for name in OTA_ONLY_FUNCTIONS:
                    if re.search(r"\b%s\s*\(" % name, code):
                        rule = "accept-only-from-phone" if "mark_app" in name else "update-functions"
                        self.fail(rule, "%s uses %s(). Only %s can use it." % (relative, name, OTA_FILE))

                # Ota_acceptPending(true) accepts a new firmware. Only the Bluetooth command can do that.
                if re.search(r"\bOta_acceptPending\s*\(", code) and not relative.endswith("ota.h"):
                    self.fail("accept-only-from-phone", "%s calls Ota_acceptPending(). Only the Bluetooth "
                              "'accept' command in %s can call it." % (relative, OTA_FILE))

            if relative not in NVS_ERASE_FILES and re.search(r"\bnvs_flash_erase\w*\s*\(", code):
                self.fail("nvs-erase", "%s calls nvs_flash_erase()." % relative)

        ota = self.read(OTA_FILE, "accept-only-from-phone")
        if ota is not None:
            self.check_accept_location(self.strip_comments(ota))

    @staticmethod
    def function_body(code, name):
        """Return (start, end) of the body of the C function `name`, or None."""
        match = re.search(r"\b%s\s*\([^;{}]*\)\s*\{" % re.escape(name), code)
        if not match:
            return None
        depth = 0
        for position in range(match.end() - 1, len(code)):
            if code[position] == "{":
                depth += 1
            elif code[position] == "}":
                depth -= 1
                if depth == 0:
                    return match.end(), position
        return None

    def check_accept_location(self, code):
        """The accept must be in one place only: the Bluetooth command CUSTOM_ID__ACCEPT_FW."""
        rule = "accept-only-from-phone"

        # 1. esp_ota_mark_app_valid_cancel_rollback(): one call, inside Ota_acceptPending()
        accept_body = self.function_body(code, "Ota_acceptPending")
        marks = [m.start() for m in re.finditer(r"\besp_ota_mark_app_valid_cancel_rollback\s*\(", code)]
        if accept_body is None:
            self.fail(rule, "%s has no function Ota_acceptPending()." % OTA_FILE)
        elif len(marks) != 1 or not (accept_body[0] <= marks[0] < accept_body[1]):
            self.fail(rule, "%s must call esp_ota_mark_app_valid_cancel_rollback() one time only, "
                      "inside Ota_acceptPending() (found %d calls)." % (OTA_FILE, len(marks)))

        # 2. Ota_acceptPending(true): one call, inside the "case CUSTOM_ID__ACCEPT_FW" of _customCallback()
        callback_body = self.function_body(code, "_customCallback")
        case_range = None
        if callback_body is not None:
            case = re.compile(r"case\s+CUSTOM_ID__ACCEPT_FW\s*:").search(code, callback_body[0], callback_body[1])
            if case:
                end = re.compile(r"\bbreak\s*;|\bcase\b|\bdefault\s*:").search(code, case.end(), callback_body[1])
                case_range = (case.end(), end.start() if end else callback_body[1])

        accepts = 0
        for call in re.finditer(r"\bOta_acceptPending\s*\(([^)]*)\)", code):
            argument = call.group(1).strip()
            if argument == "bool accept" or argument == "false":
                continue  # The function definition, and the "reject" command
            accepts += 1
            if case_range is None or not (case_range[0] <= call.start() < case_range[1]):
                self.fail(rule, "%s calls Ota_acceptPending(%s) outside the Bluetooth command "
                          "CUSTOM_ID__ACCEPT_FW." % (OTA_FILE, argument))
        if accepts != 1:
            self.fail(rule, "%s must call Ota_acceptPending(true) one time only, in the Bluetooth command "
                      "CUSTOM_ID__ACCEPT_FW (found %d calls)." % (OTA_FILE, accepts))

    def check_start_sequence(self):
        text = self.read("main/main.c", "start-sequence")
        if text is None:
            return
        code = self.strip_comments(text)

        power_on = code.find("Board_powerOn()")
        board_init = code.find("Board_init()")
        if power_on < 0:
            self.fail("start-sequence", "main/main.c does not call Board_powerOn().")
        elif board_init >= 0 and power_on > board_init:
            self.fail("start-sequence", "main/main.c must call Board_powerOn() before Board_init().")

        required = [
            (r"\bOta_isPendingVerify\s*\(\s*\)", "it does not check for a pending firmware (Ota_isPendingVerify)"),
            (r"AppView_set\s*\(\s*VIEW_ID__FW_ACCEPT\s*\)", "it does not open the accept view for a pending firmware"),
            (r"AppView_set\s*\(\s*VIEW_ID__UPDATE\s*\)", "it has no '10 seconds hold' way into the update view"),
            (r"OTA_STATE__FINISHED", "it does not restart after a finished firmware transfer"),
        ]
        for pattern, problem in required:
            if not re.search(pattern, code):
                self.fail("start-sequence", "main/main.c: %s." % problem)

        match = re.search(r"Time_getMillis\s*\(\s*\)\s*>=\s*\(\s*(\d+)\s*\*\s*1000\s*\)", code)
        if not match or int(match.group(1)) != 10:
            self.fail("start-sequence", "main/main.c: the hold time for the update mode must stay 10 seconds "
                      "(the user manual says 10 seconds).")

    def check_update_views(self):
        for relative in ("main/views/update_view.c", "main/views/fw_accept_view.c"):
            text = self.read(relative, "update-views")
            if text is not None and not re.search(r"\bOta_init\s*\(\s*\)", self.strip_comments(text)):
                self.fail("update-views", "%s does not call Ota_init()." % relative)

        # Ota_setUpdateAllowed(false) is for views of user features. Not for the update path.
        for relative in ("main/views/update_view.c", "main/views/fw_accept_view.c", "main/main.c", "main/app.c",
                         "main/app_view.c", "main/ble_commands.c"):
            text = self.read(relative)
            if text is not None and re.search(r"\bOta_setUpdateAllowed\s*\(", self.strip_comments(text)):
                self.fail("update-views", "%s calls Ota_setUpdateAllowed(). Only a view of a user feature can "
                          "call it (docs/BLUETOOTH.md)." % relative)

        app = self.read("main/app.c", "update-views")
        if app is not None:
            code = self.strip_comments(app)
            if not re.search(r"case\s+SETTING_ID__FW_UPDATE\s*:\s*AppView_set\s*\(\s*VIEW_ID__UPDATE\s*\)", code):
                self.fail("update-views", "main/app.c: the setting 'Firmware: UPDATE' must open the update view.")

        header = self.read("main/app.h", "update-views")
        if header is not None and not re.search(r"SETTING\(\s*FW_UPDATE\s*,", header):
            self.fail("update-views", "main/app.h: the setting FW_UPDATE is not in SETTING_LIST.")

        view_list = self.read("main/app_view.h", "update-views")
        if view_list is not None:
            for view in ("UPDATE", "FW_ACCEPT"):
                if not re.search(r"VIEW\(\s*%s\s*," % view, view_list):
                    self.fail("update-views", "main/app_view.h: the view %s is not in VIEW_LIST." % view)

        # The way to Settings: "both buttons" opens the app menu, and the app menu has SETTINGS
        main_c = self.read("main/main.c", "update-views")
        if main_c is not None:
            both = re.search(r"case\s+EVENT__PRESS_BOTH\s*:(.*?)\bbreak\s*;\s*\}", self.strip_comments(main_c), flags=re.S)
            if not both or not re.search(r"\bLauncher_onBothButtons\s*\(\s*\)", both.group(1)):
                self.fail("update-views", "main/main.c: 'both buttons' (EVENT__PRESS_BOTH) must call "
                          "Launcher_onBothButtons(), the way to the app menu and to Settings.")
        launcher = self.read("main/views/launcher_view.c", "update-views")
        if launcher is not None:
            self.check_menu_has_settings(self.strip_comments(launcher))

        # The app manager commands work only in the view "Apps: SYNC" (there the firmware is refused
        # and the person answers the install question)
        commands = self.read("main/ble_commands.c", "update-views")
        if commands is not None:
            code = self.strip_comments(commands)
            body = self.body_text(code, "_handleApp")
            if body is None or not re.match(r"\s*(const\s+uint8_t\s*\*\s*d\s*=\s*cmd->data\s*;\s*)?"
                                            r"if\s*\(\s*!\s*AppSync_isActive\s*\(\s*\)\s*\)\s*"
                                            r"return\s+BLE_COMMAND_STATUS__NOT_IN_APP_SYNC\s*;", body):
                self.fail("update-views", "main/ble_commands.c: _handleApp() must start with "
                          "'if(!AppSync_isActive()) return BLE_COMMAND_STATUS__NOT_IN_APP_SYNC;'.")
            handle = self.body_text(code, "_handle")
            if handle is None or re.search(r"case\s+BLE_COMMAND__APP_", handle):
                self.fail("update-views", "main/ble_commands.c: the app commands are handled only in _handleApp().")

    def body_text(self, code, name):
        """The text between the braces of the C function `name`, or None."""
        span = self.function_body(code, name)
        return code[span[0]:span[1]] if span else None

    def check_menu_has_settings(self, code):
        """The app menu is built from flash (main/apps.c), but SETTINGS is added in code: as the last,
        unconditional statement of _buildMenu(), after at most APPS_MENU_MAX - 1 other apps."""
        where = "main/views/launcher_view.c"
        body = self.body_text(code, "_buildMenu")
        if body is None:
            self.fail("update-views", "%s: the function _buildMenu() (it builds the app menu) is missing." % where)
            return

        # The last statement at the top level of the function body
        depth = 0
        last_start = 0
        for pos, char in enumerate(body):
            if char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                # The end of a block ("if(...) { }"), not of a value ("(apps_item_t){ ... };")
                if depth == 0 and body[pos + 1:].lstrip()[:1] not in (";", ",", ")"):
                    last_start = pos + 1
            elif char == ";" and depth == 0 and pos < len(body.rstrip()) - 1:
                last_start = pos + 1
        last = body[last_start:].strip()

        if not re.match(r"s_items\s*\[\s*s_count\s*\+\+\s*\]\s*=\s*\(\s*apps_item_t\s*\)\s*\{\s*VIEW_ID__SETTINGS\b", last) or \
                re.search(r"\b(return|goto)\b", body):
            self.fail("update-views", "%s: _buildMenu() must end with the line that adds VIEW_ID__SETTINGS, with no "
                      "condition and no return before it. SETTINGS is the way to 'Firmware: UPDATE'." % where)
        if not re.search(r"\bApps_getMenu\s*\(\s*s_items\s*,\s*APPS_MENU_MAX\s*-\s*1\s*\)", body):
            self.fail("update-views", "%s: _buildMenu() must leave one place for SETTINGS: "
                      "Apps_getMenu(s_items, APPS_MENU_MAX - 1)." % where)

        # Nothing else changes the menu
        outside = re.sub(r"static\s+int8_t\s+s_count\s*=\s*0\s*;", "", code.replace(body, ""))
        if re.search(r"\bs_count\s*(=[^=]|\+\+|--|\+=|-=)|\bs_items\s*\[[^\]]*\]\s*(\.\w+\s*)?=[^=]", outside):
            self.fail("update-views", "%s: only _buildMenu() can change the app menu (s_items, s_count)." % where)
        both = self.body_text(code, "Launcher_onBothButtons")
        if both is None or not re.search(r"\b_buildMenu\s*\(\s*\)", both):
            self.fail("update-views", "%s: Launcher_onBothButtons() must build the menu (_buildMenu())." % where)

    def check_nvs_keys(self):
        text = self.read("main/app.h", "nvs-keys")
        if text is None:
            return
        block = re.search(r"#define\s+ENTRY_LIST(.*?)\n\s*\n", text, flags=re.S)
        if not block:
            self.fail("nvs-keys", "main/app.h has no ENTRY_LIST.")
            return
        keys = re.findall(r"ENTRY\(\s*\w+\s*,\s*(\w+)\s*,", block.group(1))
        for key in RELEASED_NVS_KEYS:
            if key not in keys:
                self.fail("nvs-keys", "The released flash value '%s' is not in ENTRY_LIST any more." % key)
        for key in keys:
            if len(key) > 15:
                self.fail("nvs-keys", "The flash value name '%s' has more than 15 characters." % key)
        for key in set(keys):
            if keys.count(key) > 1:
                self.fail("nvs-keys", "The flash value name '%s' is in ENTRY_LIST more than one time." % key)

    def check_bluetooth_identity(self):
        app_ble = self.read("components/app_ble/app_ble.c", "bluetooth-identity")
        if app_ble is not None:
            if not re.search(r'#define\s+GAP_NAME\s+"StatTrak"', app_ble):
                self.fail("bluetooth-identity", 'components/app_ble/app_ble.c: GAP_NAME must be "StatTrak".')
            # The name of AppBle_setNextName() is for one start only: AppBle_init() clears it
            init = re.search(r"void\s+AppBle_init\s*\(.*?\n}", self.strip_comments(app_ble), flags=re.S)
            if init is None or not re.search(r"s_nextName\[0\]\s*=\s*'\\0'\s*;", init.group(0)):
                self.fail("bluetooth-identity", "components/app_ble/app_ble.c: AppBle_init() must clear the "
                                                "name of AppBle_setNextName() after it used it.")
            uuid = "0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03"
            if uuid not in app_ble:
                self.fail("bluetooth-identity", "components/app_ble/app_ble.c: the advertised service UUID changed.")

        # Only "Apps: SYNC" gives Bluetooth the name of the person
        allowed = ("main/views/app_sync_view.c", "components/app_ble/app_ble.c", "components/app_ble/app_ble_name.h",
                   "components/app_ble/app_ble.h")
        for relative in self.source_files():
            if relative in allowed:
                continue
            text = self.read(relative)
            if text is not None and re.search(r"\bAppBle_setNextName\s*\(", self.strip_comments(text)):
                self.fail("bluetooth-identity", "%s: only main/views/app_sync_view.c may call AppBle_setNextName()."
                          % relative)

        ble_ota = self.read("components/ble_ota/ble_ota.c", "bluetooth-identity")
        if ble_ota is not None:
            expected = {
                "BLE_OTA_SERVICE_UUID": "0x8018",
                "RECV_FW_UUID": "0x8020",
                "OTA_BAR_UUID": "0x8021",
                "COMMAND_UUID": "0x8022",
                "CUSTOMER_UUID": "0x8023",
            }
            for name, value in expected.items():
                if not re.search(r"#define\s+%s\s+%s\b" % (name, value), ble_ota):
                    self.fail("bluetooth-identity", "components/ble_ota/ble_ota.c: %s must be %s." % (name, value))
            self.check_numbers("components/ble_ota/ble_ota.c", self.strip_comments(ble_ota),
                               {"CMD_ID__START": 1, "CMD_ID__STOP": 2, "CMD_ID__ACK": 3})

        ota = self.read(OTA_FILE, "bluetooth-identity")
        if ota is not None:
            self.check_numbers(OTA_FILE, self.strip_comments(ota),
                               {"CUSTOM_ID__GET_VERSION": 1, "CUSTOM_ID__GET_UID": 2, "CUSTOM_ID__ACCEPT_FW": 3,
                                "CUSTOM_ID__REJECT_FW": 4, "CUSTOM_ID__REBOOT": 5})

        for relative, name, expected_value in (("components/ble_ota/ble_ota.h", "OTA_BLOCK_SIZE", 4096),
                                               ("components/ble_ota/ota.h", "OTA_USER_CMD_FIRST", 0x0100)):
            header = self.read(relative, "bluetooth-identity")
            if header is None:
                continue
            match = re.search(r"^[ \t]*#[ \t]*define[ \t]+%s[ \t]+\(?\s*(0[xX][0-9a-fA-F]+|\d+)\s*\)?[ \t]*$" % name,
                              self.strip_comments(header), flags=re.M)
            if not match or int(match.group(1), 0) != expected_value:
                self.fail("bluetooth-identity", "%s: %s must be %d." % (relative, name, expected_value))

    def check_numbers(self, relative, code, expected):
        """Enum values of the update protocol: `NAME = number,` with the released number."""
        for name, value in expected.items():
            matches = re.findall(r"\b%s\s*=\s*(0[xX][0-9a-fA-F]+|\d+)\s*[,}]" % name, code)
            if len(matches) != 1 or int(matches[0], 0) != value:
                self.fail("bluetooth-identity", "%s: %s must be %d." % (relative, name, value))

    def run(self):
        self.check_partition_table()
        self.check_sdk_config()
        self.check_no_efuse_config()
        self.check_pins()
        self.check_function_use()
        self.check_start_sequence()
        self.check_update_views()
        self.check_nvs_keys()
        self.check_bluetooth_identity()
        return self.failures


def main():
    parser = argparse.ArgumentParser(description="StatTrak firmware safety check")
    parser.add_argument("--root", default=ROOT, help="repository root (default: the folder above tools/)")
    parser.add_argument("--explain", action="store_true", help="print all rules and the reason for each rule")
    args = parser.parse_args()

    if args.explain:
        for name, rule, reason in RULES:
            print("[%s]\n  Rule:   %s\n  Reason: %s\n" % (name, rule, reason))
        return 0

    failures = Checker(args.root).run()
    reasons = {name: (rule, reason) for name, rule, reason in RULES}

    if not failures:
        print("Safety check: OK (%d rules)" % len(RULES))
        return 0

    print("Safety check: FAILED\n")
    seen = set()
    for rule, message in failures:
        print("  [%s] %s" % (rule, message))
        seen.add(rule)
    print("")
    for rule in sorted(seen):
        print("Why [%s] is a rule: %s" % (rule, reasons[rule][1]))
    print("\nDo not flash a device with this firmware. Read docs/SAFETY.md.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
