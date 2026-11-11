
/* > Includes *************************************************************************************/
#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system.h"

#include "esp_log.h"
#include "nvs_flash.h"

#include "ota.h"
#include "board.h"

#include "animation.h"
#include "gfx.h"
#include "gfx_fx.h"
#include "app_view.h"
#include "launcher_view.h"
#include "app.h"
#include "debug.h"
#include "eeprom.h"
#include "time.h"

/* > Local Function Declarations ******************************************************************/
static void _guiTask(void *pvParameters);

/* > Local Variables *******************************************************************************/
static const char *TAG = "App";


#define EEPROM_ENTRY_COUNT  (ENTRY_ID_COUNT)

const char* kEepromEntryKeys[EEPROM_ENTRY_COUNT] = {
  #define ENTRY(_id, _key, _default, _max) [ENTRY_ID__## _id] = #_key,
    ENTRY_LIST
  #undef ENTRY
};

static uint32_t s_eepromEntries[EEPROM_ENTRY_COUNT];
eeprom_t g_eeprom = {
  .entries = s_eepromEntries,
  .entryCount = EEPROM_ENTRY_COUNT,
  .entryKeys = kEepromEntryKeys,
};

static bool s_pendingFwAccept = false;
static const char* s_popupText = NULL;
static uint32_t s_popupTime = 0;

/* > Function Definitions *************************************************************************/

void app_main(void)
{
  ESP_LOGI(TAG, "StatTrak\n");

  // Print the task name
  ESP_LOGI(TAG, "Task name: %s\n", pcTaskGetName(NULL));

  /* Initialize NVS */
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  Board_powerOn();

  // System_printChipInfo();

  if(Ota_isPendingVerify()) {
    ESP_LOGI(TAG, "New firmware is pending verification!");
    s_pendingFwAccept = true;
  } else {
    ESP_LOGI(TAG, "Application valid");
  }

  Board_init();

  // Initialize Memory
  Eeprom_init(&g_eeprom);

  // Do not turn on if only one button is pressed and it is not firmware accept or initial startup
  if(!(s_pendingFwAccept || (Eeprom_read(ENTRY_ID__INIT) == 0))) {
    if(BspButton_getState(0) != 1 || BspButton_getState(1) != 1) {
      Board_powerOff();
      while(1);
    }
  }

  App_init();
  App_click(1000);

  uint32_t tNow = Time_getMillis();
  Animation_setTickCount(tNow);

  if(!s_pendingFwAccept) {
    GfxFx_initCollapse(true);
    GfxFx_reverseCollapse();
    AppView_set(VIEW_ID__TOTAL);
  } else {
    GfxFx_initCollapse(false);
    AppView_set(VIEW_ID__FW_ACCEPT);
  }


  while(1) {
    tNow = Time_getMillis();
    
    _guiTask(NULL);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t otaState = Ota_getState();
    if(otaState == OTA_STATE__FINISHED) {
      ESP_LOGI(TAG, "OTA finished");
      System_resetAfter(3);
    }

    // taskYIELD();
    // else if(Ota_getState() == OTA_STATE__IDLE) {
    //   x++;
    //   if(x == 100) {
    //     Ota_deinit();
    //   }
    //   else if(x == 200) {
    //     Ota_init();
    //     x = 0;
    //   }
    // }
  }

  // _reset();

}


static void _guiTask(void *pvParameters)
{
  static uint8_t init = 0;
  static uint32_t guiUpdateTime = 0;
  uint32_t tNow = Time_getMillis();

  if(init) {
    event_t event = BspButton_getEvent();
    while(event) {
      App_updateActivity(tNow);

      switch(event) {
        // Both buttons: an app opens the app menu, the app menu opens the selected app.
        // The menu always has SETTINGS, the way to the firmware update.
        case EVENT__PRESS_BOTH: {
          // The view that is shown or comes in now (a view change can still be animated)
          view_id_t id = AppView_getNext();

          if(id == VIEW_ID__ASK || id == VIEW_ID__UPDATE ||
             id == VIEW_ID__AUTO_OFF || id == VIEW_ID__FW_ACCEPT ||
             id == VIEW_ID__BATTERY_LOW || id == VIEW_ID__BATTERY_CRITICAL)
            break;

          Launcher_onBothButtons();
        } break;
        
        // Shutdown
        case EVENT__LONG_PRESS_B: {
            App_requestShutdown();
        } break;

        // All the other events are handled by the current view
        default: {
          AppView_eventHandler(event);
        } break;
      }

      event = BspButton_getEvent();
    }
  } else {
    // Wait until buttons are released
    if(BspButton_getState(0) == 0 && BspButton_getState(1) == 0 && GfxFx_isNormal()) {
      BspButton_clearEvents();
      init = 1;
    } else {
      // If buttons are held for 10s afeter power on, go to FW Update
      if(Time_getMillis() >= (10 * 1000)) {
        // Timeout waiting for button release
        BspButton_clearEvents();
        init = 1;
        AppView_set(VIEW_ID__UPDATE);
      }
    }
  }


  // Update OLED every 10ms
  if((tNow - guiUpdateTime) >= 10) {
    App_update();

    guiUpdateTime = tNow;
    Animation_runTickHandler(tNow);

    Gfx_clearAll(0);
    AppView_show();

    if(s_popupText != NULL) {
      if((tNow - s_popupTime) < 2000) {
        Gfx_fillRoundRect(8, 4, Gfx_getWidth() - 16, Gfx_getHeight() - 8, 3, 0);
        Gfx_drawRoundRect(9, 5, Gfx_getWidth() - 18, Gfx_getHeight() - 10, 3, 1);
        Gfx_drawStringAligned(Gfx_getWidth()/2, Gfx_getHeight()/2 - font5x5.height/2, s_popupText, &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
      } else {
        s_popupText = NULL;
      }
    }

    GfxFx_renderCollapse();

    Gfx_updateContext();

    Eeprom_update();
  }
}
