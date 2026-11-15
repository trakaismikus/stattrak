/***************************************************************************************************
 * @file    : host_core.c
 * @brief   : Simulated time, session control, log output and the saved device state file.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "host.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "system.h"

/* > External Functions ***************************************************************************/
void app_main(void); /* main/main.c */

/* > Local Variables ******************************************************************************/
static host_config_t   s_config;
static host_frontend_t s_frontend;

static uint64_t s_nowMs = 0;
static bool     s_stateChanged = false;
static bool     s_ending = false;

/* > Session **************************************************************************************/

void Host_init(const host_config_t* config, const host_frontend_t* frontend) {
  memset(&s_config, 0, sizeof(s_config));
  memset(&s_frontend, 0, sizeof(s_frontend));

  if(config != NULL)
    s_config = *config;
  if(frontend != NULL)
    s_frontend = *frontend;

  s_nowMs = 0;
  s_stateChanged = false;
  s_ending = false;

  HostBoard_setVoltage(s_config.voltageMv);
  HostBoard_setResetReason(s_config.resetReason);

  if(s_config.stateFile != NULL)
    HostState_load(s_config.stateFile);

  if(s_config.pendingVerify)
    HostOta_setPendingStage(1);

  HostOta_boot();
}

void Host_run(void) {
  app_main();

  /* app_main() of this firmware never returns. */
  Host_end(HOST_END__ABORT, "app_main returned");
}

void Host_end(host_end_t reason, const char* detail) {
  if(!s_ending) {
    s_ending = true;

    if(s_config.stateFile != NULL)
      HostState_save(s_config.stateFile);

    if(s_frontend.end != NULL)
      s_frontend.end(reason, detail != NULL ? detail : "");
  }

  exit(reason == HOST_END__ABORT ? 2 : 0);
}

const char* Host_endName(host_end_t reason) {
  switch(reason) {
    case HOST_END__QUIT:        return "quit";
    case HOST_END__POWER_OFF:   return "poweroff";
    case HOST_END__POWER_LOST:  return "powerlost";
    case HOST_END__RESET:       return "reset";
    case HOST_END__ABORT:       return "abort";
    default:                    return "unknown";
  }
}

void Host_notifyFrame(const uint8_t* frame) {
  if(s_frontend.frame != NULL)
    s_frontend.frame(frame);
}

/* > Time *****************************************************************************************/

uint64_t Host_nowMs(void) {
  return s_nowMs;
}

void Host_sleepMs(uint32_t ms) {
  const uint64_t end = s_nowMs + ms;

  while(s_nowMs < end) {
    /* Go to the next step border, or to the end of the sleep */
    uint64_t next = ((s_nowMs / HOST_STEP_MS) + 1) * HOST_STEP_MS;
    if(next > end)
      next = end;
    s_nowMs = next;

    if((s_nowMs % HOST_STEP_MS) == 0)
      HostButton_tick();

    /* The power comes from a held button until the firmware sets the power latch. */
    if(!s_config.usbPower && !HostBoard_isLatched() && !HostButton_getLevel(0) && !HostButton_getLevel(1))
      Host_end(HOST_END__POWER_LOST, "no button held and power latch not set");

    if(s_stateChanged) {
      s_stateChanged = false;
      if(s_config.stateFile != NULL)
        HostState_save(s_config.stateFile);
      if(s_frontend.stateChanged != NULL)
        s_frontend.stateChanged();
    }

    if(s_frontend.idle != NULL)
      s_frontend.idle();
  }
}

/* > FreeRTOS stand-ins ***************************************************************************/

void vTaskDelay(TickType_t ticks) {
  Host_sleepMs((uint32_t)pdTICKS_TO_MS(ticks));
}

TickType_t xTaskGetTickCount(void) {
  return (TickType_t)pdMS_TO_TICKS(s_nowMs);
}

const char* pcTaskGetName(TaskHandle_t task) {
  (void)task;
  return "main";
}

/* > ESP-IDF stand-ins ****************************************************************************/

const char* esp_err_to_name(esp_err_t code) {
  switch(code) {
    case ESP_OK:                        return "ESP_OK";
    case ESP_FAIL:                      return "ESP_FAIL";
    case ESP_ERR_NO_MEM:                return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG:           return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE:         return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_NVS_NOT_INITIALIZED:   return "ESP_ERR_NVS_NOT_INITIALIZED";
    case ESP_ERR_NVS_NOT_FOUND:         return "ESP_ERR_NVS_NOT_FOUND";
    case ESP_ERR_NVS_NOT_ENOUGH_SPACE:  return "ESP_ERR_NVS_NOT_ENOUGH_SPACE";
    case ESP_ERR_NVS_INVALID_HANDLE:    return "ESP_ERR_NVS_INVALID_HANDLE";
    case ESP_ERR_NVS_KEY_TOO_LONG:      return "ESP_ERR_NVS_KEY_TOO_LONG";
    case ESP_ERR_NVS_NO_FREE_PAGES:     return "ESP_ERR_NVS_NO_FREE_PAGES";
    default:                            return "ESP_ERR_UNKNOWN";
  }
}

void Host_errorCheckFailed(esp_err_t code, const char* expr, const char* file, int line) {
  char detail[256];
  snprintf(detail, sizeof(detail), "ESP_ERROR_CHECK failed: %s = %s (0x%x) at %s:%d",
           expr, esp_err_to_name(code), (unsigned)code, file, line);
  Host_end(HOST_END__ABORT, detail);
}

void Host_log(char level, const char* tag, const char* fmt, ...) {
  if(!s_config.log)
    return;

  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "%c (%lu) %s: ", level, (unsigned long)s_nowMs, tag);
  vfprintf(stderr, fmt, args);
  fputc('\n', stderr);
  va_end(args);
}

/* > main/system.h stand-in ***********************************************************************/

void System_resetAfter(int seconds) {
  /* The device code counts from "seconds" down to 0, with a 1 s delay each time. */
  for(int i = seconds; i >= 0; i--)
    vTaskDelay(1000 / portTICK_PERIOD_MS);

  Host_end(HOST_END__RESET, "esp_restart");
}

void System_printChipInfo(void) {
}

/* > Saved device state ***************************************************************************/

void HostState_markChanged(void) {
  s_stateChanged = true;
}

/*
 * File format, one item on each line:
 *   nvs <namespace> <key> <value>
 *   blob <namespace> <key> <hex>   (a byte array, 2 hex digits for each byte; "-" = empty)
 *   ota pending <0|1|2>      (see HostOta_setPendingStage)
 * Lines that start with '#' are comments.
 */
bool HostState_load(const char* path) {
  FILE* file = fopen(path, "r");
  if(file == NULL)
    return false;

  HostNvs_clear();
  HostOta_setPendingStage(0);

  /* A blob line has 2 hex digits for each byte: large, so not on the stack */
  static char line[2 * HOST_NVS_MAX_BLOB + 64];
  static uint8_t data[HOST_NVS_MAX_BLOB];
  while(fgets(line, sizeof(line), file) != NULL) {
    char ns[16], key[16];
    unsigned long value;
    int pos = 0;

    if(line[0] == '#')
      continue;

    if(sscanf(line, "nvs %15s %15s %lu", ns, key, &value) == 3) {
      HostNvs_put(ns, key, (uint32_t)value);
    } else if(sscanf(line, "blob %15s %15s %n", ns, key, &pos) == 2 && pos > 0) {
      uint32_t length = 0;
      bool good = true;
      const char* hex = &line[pos];
      if(hex[0] != '-') {
        while(good && hex[0] != '\0' && hex[0] != '\n' && hex[0] != '\r' && hex[0] != ' ') {
          unsigned int byte = 0;
          good = (length < HOST_NVS_MAX_BLOB) && hex[1] != '\0' && (sscanf(hex, "%2x", &byte) == 1);
          data[length++] = (uint8_t)byte;
          hex += 2;
        }
      }
      if(good)
        HostNvs_putBlob(ns, key, data, length);
    } else if(sscanf(line, "ota pending %lu", &value) == 1) {
      HostOta_setPendingStage((uint8_t)value);
    }
  }

  fclose(file);
  s_stateChanged = false;
  return true;
}

bool HostState_save(const char* path) {
  FILE* file = fopen(path, "w");
  if(file == NULL)
    return false;

  fprintf(file, "# StatTrak simulator: saved device state (flash content)\n");
  for(uint32_t i = 0; i < HostNvs_getEntryCount(); i++) {
    const host_nvs_entry_t* entry = HostNvs_getEntry(i);
    fprintf(file, "nvs %s %s %lu\n", entry->ns, entry->key, (unsigned long)entry->value);
  }
  for(uint32_t i = 0; i < HostNvs_getBlobCount(); i++) {
    const host_nvs_blob_t* blob = HostNvs_getBlob(i);
    fprintf(file, "blob %s %s ", blob->ns, blob->key);
    if(blob->length == 0)
      fputc('-', file);
    for(uint32_t j = 0; j < blob->length; j++)
      fprintf(file, "%02x", (unsigned)blob->data[j]);
    fputc('\n', file);
  }
  fprintf(file, "ota pending %d\n", (int)HostOta_getPendingStage());

  fclose(file);
  return true;
}
