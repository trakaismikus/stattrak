"""Tests of tools/check_safety.py: it must pass on this repository and it must catch dangerous changes."""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(REPO_ROOT, "tools", "check_safety.py")


class SafetyCheckTest(unittest.TestCase):

    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="stattrak_safety_")
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        for name in ("main", "components"):
            shutil.copytree(os.path.join(REPO_ROOT, name), os.path.join(self.root, name))
        for name in os.listdir(REPO_ROOT):
            if name == "partitions.csv" or (name.startswith("sdkconfig.") and name != "sdkconfig.old"):
                shutil.copy(os.path.join(REPO_ROOT, name), os.path.join(self.root, name))

    def check(self):
        result = subprocess.run([sys.executable, SCRIPT, "--root", self.root],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
        return result.returncode, result.stdout

    def edit(self, relative, old, new):
        path = os.path.join(self.root, *relative.split("/"))
        with open(path, "r", encoding="utf-8", newline="") as handle:
            text = handle.read()
        self.assertIn(old, text, "test is out of date: %r is not in %s" % (old, relative))
        with open(path, "w", encoding="utf-8", newline="") as handle:
            handle.write(text.replace(old, new))

    def append(self, relative, text):
        with open(os.path.join(self.root, *relative.split("/")), "a", encoding="utf-8", newline="") as handle:
            handle.write(text)

    def assert_fails(self, rule):
        code, output = self.check()
        self.assertEqual(code, 1, output)
        self.assertIn("[%s]" % rule, output)

    # --- The repository itself -------------------------------------------------------------------

    def test_repository_passes(self):
        code, output = self.check()
        self.assertEqual(code, 0, output)

    def test_explain_lists_the_rules(self):
        result = subprocess.run([sys.executable, SCRIPT, "--explain"], stdout=subprocess.PIPE,
                                universal_newlines=True)
        self.assertEqual(result.returncode, 0)
        self.assertIn("[partition-table]", result.stdout)

    def test_comment_is_not_a_violation(self):
        self.append("main/views/stats_view.c", "\n// esp_ota_mark_app_valid_cancel_rollback();\n"
                                               "/* esp_flash_erase_chip(NULL); */\n")
        code, output = self.check()
        self.assertEqual(code, 0, output)

    # --- Dangerous changes -----------------------------------------------------------------------

    def test_partition_table_change(self):
        self.edit("partitions.csv", "1472k", "1400k")
        self.assert_fails("partition-table")

    def test_rollback_switched_off(self):
        self.edit("sdkconfig.defaults", "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y", "")
        self.assert_fails("sdk-config")

    def test_rollback_switched_off_in_an_other_config_file(self):
        self.append("sdkconfig.release", "\nCONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=n\n")
        self.assert_fails("sdk-config")

    def test_wrong_crystal(self):
        self.edit("sdkconfig.defaults", "CONFIG_XTAL_FREQ=26", "CONFIG_XTAL_FREQ=40")
        self.assert_fails("sdk-config")

    def test_flash_encryption(self):
        self.append("sdkconfig.release", "\nCONFIG_SECURE_FLASH_ENC_ENABLED=y\n")
        self.assert_fails("no-efuse-config")

    def test_secure_boot(self):
        self.append("sdkconfig.defaults", "\nCONFIG_SECURE_BOOT=y\n")
        self.assert_fails("no-efuse-config")

    def test_power_pin_change(self):
        self.edit("main/board.h", "#define PWR_ON_PIN      (1)", "#define PWR_ON_PIN      (2)")
        self.assert_fails("pins")

    def test_power_pin_as_an_expression(self):
        self.edit("main/board.h", "#define PWR_ON_PIN      (1)", "#define PWR_ON_PIN      (1 << 1)")
        self.assert_fails("pins")

    def test_accept_moved_to_the_start_of_the_update_service(self):
        # The only call is still in ota.c, but not in the Bluetooth "accept" command
        self.edit("components/ble_ota/ota.c", "bool accepted = Ota_acceptPending(true);",
                  "bool accepted = true;")
        self.edit("components/ble_ota/ota.c", "  // Initialize OTA state", "  Ota_acceptPending(true);")
        self.assert_fails("accept-only-from-phone")

    def test_mark_valid_at_the_start_of_the_update_service(self):
        self.edit("components/ble_ota/ota.c", "  // Initialize OTA state",
                  "  esp_ota_mark_app_valid_cancel_rollback();")
        self.assert_fails("accept-only-from-phone")

    def test_accept_with_a_variable(self):
        self.edit("components/ble_ota/ota.c", "  // Initialize OTA state", "  Ota_acceptPending(s_queue != NULL);")
        self.assert_fails("accept-only-from-phone")

    def test_firmware_accepts_itself(self):
        self.append("main/views/total_view.c", "\nvoid Hack(void) { esp_ota_mark_app_valid_cancel_rollback(); }\n")
        self.assert_fails("accept-only-from-phone")

    def test_view_accepts_the_firmware(self):
        self.append("main/views/fw_accept_view.c", "\nvoid Hack(void) { Ota_acceptPending(true); }\n")
        self.assert_fails("accept-only-from-phone")

    def test_update_function_in_an_other_file(self):
        self.append("main/app.c", "\nvoid Hack(void) { esp_ota_set_boot_partition(NULL); }\n")
        self.assert_fails("update-functions")

    def test_raw_flash_write(self):
        self.append("main/app.c", "\nvoid Hack(void) { esp_flash_erase_region(NULL, 0, 4096); }\n")
        self.assert_fails("no-raw-flash")

    def test_efuse_write(self):
        self.append("main/system.c", "\nvoid Hack(void) { esp_efuse_write_field_bit(NULL); }\n")
        self.assert_fails("no-raw-flash")

    def test_storage_erase_in_a_view(self):
        self.append("main/views/stats_view.c", "\nvoid Hack(void) { nvs_flash_erase(); }\n")
        self.assert_fails("nvs-erase")

    def test_power_latch_removed(self):
        self.edit("main/main.c", "  Board_powerOn();", "")
        self.assert_fails("start-sequence")

    def test_power_latch_after_board_init(self):
        self.edit("main/main.c", "  Board_powerOn();", "")
        self.edit("main/main.c", "  Board_init();", "  Board_init();\n  Board_powerOn();")
        self.assert_fails("start-sequence")

    def test_rescue_hold_removed(self):
        self.edit("main/main.c", "AppView_set(VIEW_ID__UPDATE);", "")
        self.assert_fails("start-sequence")

    def test_rescue_hold_time_changed(self):
        self.edit("main/main.c", ">= (10 * 1000)", ">= (30 * 1000)")
        self.assert_fails("start-sequence")

    def test_accept_view_removed_from_start(self):
        self.edit("main/main.c", "AppView_set(VIEW_ID__FW_ACCEPT);", "AppView_set(VIEW_ID__TOTAL);")
        self.assert_fails("start-sequence")

    def test_update_view_without_bluetooth(self):
        self.edit("main/views/update_view.c", "  Ota_init();", "")
        self.assert_fails("update-views")

    def test_update_setting_removed(self):
        self.edit("main/app.c", "      AppView_set(VIEW_ID__UPDATE);", "      AppView_set(VIEW_ID__TOTAL);")
        self.assert_fails("update-views")

    def test_settings_removed_from_the_app_menu(self):
        self.edit("main/views/launcher_view.c",
                  "  s_items[s_count++] = (apps_item_t){ VIEW_ID__SETTINGS, APPS_ID__NONE };\n", "")
        self.assert_fails("update-views")

    def test_settings_in_the_app_menu_only_under_a_condition(self):
        # SETTINGS must not depend on the flash content (the apps list)
        self.edit("main/views/launcher_view.c",
                  "  s_items[s_count++] = (apps_item_t){ VIEW_ID__SETTINGS, APPS_ID__NONE };\n",
                  "  if(s_count < 3)\n    s_items[s_count++] = (apps_item_t){ VIEW_ID__SETTINGS, APPS_ID__NONE };\n")
        self.assert_fails("update-views")

    def test_app_menu_without_a_place_for_settings(self):
        self.edit("main/views/launcher_view.c", "Apps_getMenu(s_items, APPS_MENU_MAX - 1)",
                  "Apps_getMenu(s_items, APPS_MENU_MAX)")
        self.assert_fails("update-views")

    def test_both_buttons_do_not_open_the_app_menu(self):
        self.edit("main/main.c", "          Launcher_onBothButtons();", "          AppView_set(VIEW_ID__TOTAL);")
        self.assert_fails("update-views")

    def test_update_view_refuses_new_firmware(self):
        self.edit("main/views/update_view.c", "  Ota_init();", "  Ota_init();\n  Ota_setUpdateAllowed(false);")
        self.assert_fails("update-views")

    def test_start_code_refuses_new_firmware(self):
        self.edit("main/app.c", "  // Update Battery Level", "  Ota_setUpdateAllowed(false);")
        self.assert_fails("update-views")

    def test_user_view_can_refuse_new_firmware(self):
        self.append("main/views/stats_view.c", "\nvoid MyView_init(void) { Ota_init(); Ota_setUpdateAllowed(false); }\n")
        code, output = self.check()
        self.assertEqual(code, 0, output)

    def test_flash_value_renamed(self):
        self.edit("main/app.h", "ENTRY(TOTAL_KILLS,   total_kills,", "ENTRY(TOTAL_KILLS,   kills,")
        self.assert_fails("nvs-keys")

    def test_flash_value_name_too_long(self):
        self.edit("main/app.h", "ENTRY(LOW_BAT,       low_bat,",
                  "ENTRY(NEW_THING,     a_name_with_16_ch, 0, 1) \\\n  ENTRY(LOW_BAT,       low_bat,")
        self.assert_fails("nvs-keys")

    def test_bluetooth_name_change(self):
        self.edit("components/app_ble/app_ble.c", '#define GAP_NAME   "StatTrak"', '#define GAP_NAME   "MyTrak"')
        self.assert_fails("bluetooth-identity")

    def test_bluetooth_service_number_change(self):
        self.edit("components/ble_ota/ble_ota.c", "0x8020", "0x9020")
        self.assert_fails("bluetooth-identity")

    def test_update_command_number_change(self):
        self.edit("components/ble_ota/ble_ota.c", "CMD_ID__START = 0x0001,", "CMD_ID__START = 0x0011,")
        self.assert_fails("bluetooth-identity")

    def test_accept_command_number_change(self):
        self.edit("components/ble_ota/ota.c", "CUSTOM_ID__ACCEPT_FW    = 0x0003,", "CUSTOM_ID__ACCEPT_FW    = 0x0103,")
        self.assert_fails("bluetooth-identity")

    def test_user_command_range_change(self):
        self.edit("components/ble_ota/ota.h", "#define OTA_USER_CMD_FIRST        (0x0100)",
                  "#define OTA_USER_CMD_FIRST        (0x0001)")
        self.assert_fails("bluetooth-identity")

    def test_sector_size_change(self):
        self.edit("components/ble_ota/ble_ota.h", "(4096)", "(2048)")
        self.assert_fails("bluetooth-identity")


if __name__ == "__main__":
    unittest.main()
