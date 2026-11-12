/***************************************************************************************************
 * @file    : main.c
 * @brief   : Headless StatTrak simulator. No window. A script controls it.
 *
 * One run of this program is one "power session" of the device: from power-on until the
 * firmware switches the power off, restarts, or the script stops.
 *
 * It reads commands from standard input, one on each line, and answers on standard output.
 * Each answer ends with a line "ok" or "err <text>". When the session ends, it prints
 * "end <reason> t=<ms> <detail>" and stops.
 *
 *   btn <1|2> <0|1>          press (1) or release (0) a button
 *   run <ms>                 let simulated time pass
 *   voltage <mV>             set the battery voltage
 *   ota transfer <ms>        a phone sends new firmware; the transfer takes <ms>
 *   ota accept               a phone accepts the new firmware
 *   ota state <n> <percent>  force the update state (see components/ble_ota/ota.h)
 *   ble cmd <id> [hex]       a phone sends a user command (main/ble_commands.h), id in hex
 *   ble response             print the oldest answer of the firmware to a user command
 *   nvs fail <0|1>           make all flash writes fail
 *   simapp <file>            install an app package like the window / web simulator (sim_apps.h)
 *   simopen <file>           the same, and the simulator answers yes and opens the app
 *   state                    print the device state as key=value lines
 *   frame                    print the display, 32 lines of 128 characters ('#' = pixel on)
 *   quit                     stop
 *
 * Use tools/sim.py or tests/stattrak_sim.py. You do not have to type these commands by hand.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"

#include "app.h"
#include "app_view.h"
#include "launcher_view.h"
#include "app_host_view.h"
#include "app_settings_view.h"
#include "app_sync_view.h"
#include "apps.h"
#include "eeprom.h"
#include "ota.h"
#include "board.h"
#include "sim_apps.h"
#include "stapp.h"

/* > External Variables (main/main.c) *************************************************************/
extern eeprom_t g_eeprom;

/* > Constants ************************************************************************************/
static const char* const kViewNames[] = {
  #define VIEW(_id, _name) #_id,
    VIEW_LIST
  #undef VIEW
};

/* > Local Variables ******************************************************************************/
static uint64_t s_runUntil = 0;
static bool     s_runPending = false;

/* > Local Functions ******************************************************************************/

static void _reply(const char* text) {
  puts(text);
  fflush(stdout);
}

static void _printState(void) {
  const view_id_t view = AppView_get();

  printf("t=%llu\n", (unsigned long long)Host_nowMs());
  printf("latched=%d\n", HostBoard_isLatched() ? 1 : 0);
  printf("view=%s\n", (view < VIEW_COUNT) ? kViewNames[view] : "INVALID");
  printf("view_id=%d\n", (int)view);
  printf("view_next=%s\n", (AppView_getNext() < VIEW_COUNT) ? kViewNames[AppView_getNext()] : "INVALID");
  printf("launcher_sel=%s\n", kViewNames[Launcher_getSelectedView()]);
  printf("brightness=%u\n", (unsigned)HostOled_getBrightness());
  printf("display_on=%d\n", HostOled_isOn() ? 1 : 0);
  printf("frames=%lu\n", (unsigned long)HostOled_getFrameCount());
  printf("clicks=%lu\n", (unsigned long)HostBuzzer_getClickCount());
  printf("click_freq=%u\n", (unsigned)HostBuzzer_getLastFreq());
  printf("voltage=%u\n", (unsigned)HostBoard_getVoltage());
  printf("btn1=%d\n", HostButton_getLevel(0) ? 1 : 0);
  printf("btn2=%d\n", HostButton_getLevel(1) ? 1 : 0);
  printf("ota_active=%d\n", HostOta_isActive() ? 1 : 0);
  printf("ota_state=%u\n", (unsigned)HostOta_getState());
  printf("ota_progress=%u\n", (unsigned)Ota_getProgress());
  printf("ota_init_calls=%lu\n", (unsigned long)HostOta_getInitCount());
  printf("ota_deinit_calls=%lu\n", (unsigned long)HostOta_getDeinitCount());
  printf("ota_double_init=%lu\n", (unsigned long)HostOta_getDoubleInitCount());
  printf("ota_pending=%u\n", (unsigned)HostOta_getPendingStage());
  printf("ota_update_allowed=%d\n", HostOta_isUpdateAllowed() ? 1 : 0);
  printf("ble_name=%s\n", HostOta_getName());
  printf("rolled_back=%d\n", HostOta_wasRolledBack() ? 1 : 0);
  printf("nvs_writes=%lu\n", (unsigned long)HostNvs_getWriteCount());
  printf("nvs_commits=%lu\n", (unsigned long)HostNvs_getCommitCount());
  printf("nvs_erases=%lu\n", (unsigned long)HostNvs_getEraseCount());
  printf("fw_version=%s\n", Board_getFwVersion());

  /* Values in RAM, as the firmware sees them */
  for(uint32_t i = 0; i < g_eeprom.entryCount; i++)
    printf("eeprom.%s=%lu\n", g_eeprom.entryKeys[i], (unsigned long)g_eeprom.entries[i]);

  /* Values in flash */
  for(uint32_t i = 0; i < HostNvs_getEntryCount(); i++) {
    const host_nvs_entry_t* entry = HostNvs_getEntry(i);
    printf("nvs.%s.%s=%lu\n", entry->ns, entry->key, (unsigned long)entry->value);
  }
  for(uint32_t i = 0; i < HostNvs_getBlobCount(); i++) {
    const host_nvs_blob_t* blob = HostNvs_getBlob(i);
    printf("blob.%s.%s=%u\n", blob->ns, blob->key, (unsigned)blob->length);
  }

  /* Apps (main/apps.c): the list with the hidden apps, the menu, the open app */
  printf("apps_count=%u\n", (unsigned)Apps_getCount());
  printf("apps_installed=%u\n", (unsigned)Apps_getInstalledCount());
  printf("apps_free=%lu\n", (unsigned long)Apps_getFreeBytes());
  printf("apps_list=");
  for(uint8_t i = 0; i < Apps_getCount(); i++) {
    uint32_t id = 0;
    uint8_t flags = 0;
    Apps_getEntry(i, &id, &flags);
    printf("%s%lu", i > 0 ? "," : "", (unsigned long)id);
  }
  printf("\n");
  for(uint8_t i = 0; i < Apps_getCount(); i++) {
    uint32_t id = 0;
    uint8_t flags = 0;
    Apps_getEntry(i, &id, &flags);
    const char* name = Apps_getName(id);
    printf("app.%lu.name=%s\n", (unsigned long)id, name ? name : "");
    printf("app.%lu.flags=%u\n", (unsigned long)id, (unsigned)flags);
    printf("app.%lu.settings=", (unsigned long)id);
    for(uint8_t s = 0; s < Apps_getSettingCount(id); s++)
      printf("%s%u", s > 0 ? "," : "", (unsigned)Apps_getSetting(id, s));
    printf("\n");
  }
  {
    apps_item_t items[APPS_MENU_MAX];
    const uint8_t count = Apps_getMenu(items, APPS_MENU_MAX - 1);
    printf("apps_menu=");
    for(uint8_t i = 0; i < count; i++)
      printf("%lu,", (unsigned long)items[i].id);
    printf("settings\n");
  }
  printf("launcher_app=%lu\n", (unsigned long)Launcher_getSelectedAppId());
  printf("app_running=%lu\n", (unsigned long)(AppHost_isOpen() ? AppHost_getAppId() : 0));
  printf("app_error=%d\n", (int)AppHost_getError());
  printf("app_error_text=%s\n", AppHost_getError() == STAPP_OK ? "" : Stapp_resultText(AppHost_getError()));
  printf("app_install_state=%d\n", (int)AppSync_getState());
  printf("app_settings_id=%lu\n", (unsigned long)AppSettings_getAppId());
  printf("app_settings_row=%u\n", (unsigned)AppSettings_getRow());
  printf("simapp_state=%d\n", (int)SimApps_getState());
  printf("simapp_text=%s\n", SimApps_getText());
}

static void _printFrame(void) {
  const uint8_t* frame = HostOled_getFrame();
  char line[HOST_OLED_WIDTH + 1];

  for(int y = 0; y < HOST_OLED_HEIGHT; y++) {
    for(int x = 0; x < HOST_OLED_WIDTH; x++) {
      const uint8_t byte = frame[x + (y / 8) * HOST_OLED_WIDTH];
      line[x] = (byte & (1 << (y % 8))) ? '#' : '.';
    }
    line[HOST_OLED_WIDTH] = '\0';
    puts(line);
  }
}

/* Returns false at the end of the input. */
static bool _readCommand(void) {
  char line[300];
  char cmd[16] = "";
  char sub[16] = "";
  unsigned long a = 0, b = 0;

  if(fgets(line, sizeof(line), stdin) == NULL)
    return false;

  if(sscanf(line, "%15s", cmd) != 1 || cmd[0] == '#')
    return true; /* Empty line or comment */

  if(strcmp(cmd, "btn") == 0) {
    if(sscanf(line, "%*s %lu %lu", &a, &b) == 2 && (a == 1 || a == 2) && b <= 1) {
      HostButton_setLevel((uint8_t)(a - 1), b == 1);
      _reply("ok");
    } else {
      _reply("err usage: btn <1|2> <0|1>");
    }
  } else if(strcmp(cmd, "run") == 0) {
    if(sscanf(line, "%*s %lu", &a) == 1) {
      s_runUntil = Host_nowMs() + a;
      s_runPending = true;
    } else {
      _reply("err usage: run <ms>");
    }
  } else if(strcmp(cmd, "voltage") == 0) {
    if(sscanf(line, "%*s %lu", &a) == 1 && a <= 0xFFFF) {
      HostBoard_setVoltage((uint16_t)a);
      _reply("ok");
    } else {
      _reply("err usage: voltage <mV>");
    }
  } else if(strcmp(cmd, "ota") == 0) {
    const int count = sscanf(line, "%*s %15s %lu %lu", sub, &a, &b);
    if(count >= 2 && strcmp(sub, "transfer") == 0) {
      _reply(HostOta_startTransfer((uint32_t)a) ? "ok" :
             "err the firmware does not take an update now (open the update view first)");
    } else if(count >= 1 && strcmp(sub, "accept") == 0) {
      if(!HostOta_isActive()) {
        _reply("err update service is off");
      } else {
        printf("accepted=%d\n", HostOta_accept() ? 1 : 0);
        _reply("ok");
      }
    } else if(count >= 2 && strcmp(sub, "state") == 0 && a <= 0xFF && b <= 100) {
      HostOta_setState((uint8_t)a, (uint8_t)b);
      _reply("ok");
    } else {
      _reply("err usage: ota transfer <ms> | ota accept | ota state <n> <percent>");
    }
  } else if(strcmp(cmd, "ble") == 0) {
    char hex[2 * OTA_USER_REQUEST_SIZE + 2] = "";
    const int count = sscanf(line, "%*s %15s %lx %33s", sub, &a, hex);
    if(count >= 2 && strcmp(sub, "cmd") == 0 && a <= 0xFFFF) {
      uint8_t data[OTA_USER_REQUEST_SIZE] = { 0 };
      const size_t hexLen = strlen(hex);
      bool good = (hexLen % 2 == 0) && (hexLen <= 2 * OTA_USER_REQUEST_SIZE);
      for(size_t i = 0; good && i < hexLen / 2; i++) {
        unsigned int byte = 0;
        good = (sscanf(&hex[2 * i], "%2x", &byte) == 1);
        data[i] = (uint8_t)byte;
      }
      if(!good) {
        _reply("err the data must be 0 to 16 bytes as hex text, for example 01ff");
      } else if(!HostOta_isActive()) {
        _reply("err bluetooth is off (the firmware did not call Ota_init)");
      } else if(a < OTA_USER_CMD_FIRST) {
        _reply("err user command numbers start at 0100");
      } else {
        _reply(HostOta_sendUserCommand((uint16_t)a, data, (uint8_t)(hexLen / 2)) ? "ok" : "err too many commands wait");
      }
    } else if(count >= 1 && strcmp(sub, "response") == 0) {
      uint16_t cmdId = 0;
      uint8_t status = 0, len = 0;
      uint8_t data[OTA_USER_RESPONSE_SIZE] = { 0 };
      if(HostOta_takeUserResponse(&cmdId, &status, data, &len)) {
        printf("response cmd=%04x status=%u data=", (unsigned)cmdId, (unsigned)status);
        for(uint8_t i = 0; i < len; i++)
          printf("%02x", (unsigned)data[i]);
        printf("\n");
      } else {
        printf("response none\n");
      }
      _reply("ok");
    } else {
      _reply("err usage: ble cmd <id hex> [data hex] | ble response");
    }
  } else if(strcmp(cmd, "nvs") == 0) {
    if(sscanf(line, "%*s %15s %lu", sub, &a) == 2 && strcmp(sub, "fail") == 0) {
      HostNvs_setFailWrites(a != 0);
      _reply("ok");
    } else {
      _reply("err usage: nvs fail <0|1>");
    }
  } else if(strcmp(cmd, "simapp") == 0 || strcmp(cmd, "simopen") == 0) {
    char path[sizeof(line)] = "";
    static uint8_t s_simApp[STAPP_MAX_SIZE + 1];
    FILE* file = (sscanf(line, "%*s %299[^\r\n]", path) == 1) ? fopen(path, "rb") : NULL;
    if(file == NULL) {
      _reply("err usage: simapp <file> | simopen <file>");
    } else {
      const size_t size = fread(s_simApp, 1, sizeof(s_simApp), file);
      fclose(file);
      _reply(SimApps_start(s_simApp, (uint32_t)size, strcmp(cmd, "simopen") == 0) ? "ok" : "err not started");
    }
  } else if(strcmp(cmd, "state") == 0) {
    _printState();
    _reply("ok");
  } else if(strcmp(cmd, "frame") == 0) {
    _printFrame();
    _reply("ok");
  } else if(strcmp(cmd, "quit") == 0) {
    Host_end(HOST_END__QUIT, "quit command");
  } else {
    _reply("err unknown command");
  }

  return true;
}

static void _idle(void) {
  if(!HostOled_guardsIntact())
    Host_end(HOST_END__ABORT, "the firmware wrote outside the display frame buffer");

  SimApps_update();

  /* simopen: the simulator holds button 2 for the person ("yes"), as the window and the web page do */
  static bool s_simHold = false;
  if(SimApps_wantsYes() != s_simHold) {
    s_simHold = SimApps_wantsYes();
    HostButton_setLevel(1, s_simHold);
  }

  while(Host_nowMs() >= s_runUntil) {
    if(s_runPending) {
      s_runPending = false;
      _reply("ok");
    }

    if(!_readCommand())
      Host_end(HOST_END__QUIT, "end of input");
  }
}

static void _end(host_end_t reason, const char* detail) {
  printf("end %s t=%llu %s\n", Host_endName(reason), (unsigned long long)Host_nowMs(), detail);
  fflush(stdout);
}

static void _usage(void) {
  fprintf(stderr,
    "StatTrak headless simulator\n"
    "  --state FILE     load and save the device state (flash content) in FILE\n"
    "  --hold WHICH     buttons held at power-on: both (default), 1, 2, none\n"
    "  --pending        start as a new firmware that is not accepted yet\n"
    "  --voltage MV     battery voltage in millivolts (default 0)\n"
    "  --usb            the power does not depend on the buttons or the latch\n"
    "  --log            print firmware log lines to stderr\n"
    "Commands come from standard input. See the top of sim/headless/main.c.\n");
}

int main(int argc, char** argv) {
  host_config_t config = { .voltageMv = 4000 };
  const char* hold = "both";

  for(int i = 1; i < argc; i++) {
    if(strcmp(argv[i], "--state") == 0 && (i + 1) < argc) {
      config.stateFile = argv[++i];
    } else if(strcmp(argv[i], "--hold") == 0 && (i + 1) < argc) {
      hold = argv[++i];
    } else if(strcmp(argv[i], "--voltage") == 0 && (i + 1) < argc) {
      config.voltageMv = (uint16_t)strtoul(argv[++i], NULL, 10);
    } else if(strcmp(argv[i], "--pending") == 0) {
      config.pendingVerify = true;
    } else if(strcmp(argv[i], "--usb") == 0) {
      config.usbPower = true;
    } else if(strcmp(argv[i], "--log") == 0) {
      config.log = true;
    } else {
      _usage();
      return 64;
    }
  }

  const host_frontend_t frontend = {
    .idle = _idle,
    .end = _end,
  };

  Host_init(&config, &frontend);

  HostButton_setLevel(0, strcmp(hold, "both") == 0 || strcmp(hold, "1") == 0);
  HostButton_setLevel(1, strcmp(hold, "both") == 0 || strcmp(hold, "2") == 0);

  Host_run();
  return 0;
}
