
/* > Includes *************************************************************************************/
#include "system.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"

#include "esp_log.h"

static const char *TAG = "System";


/* > Function Definitions *************************************************************************/

void System_printChipInfo(void) {
  /* Print chip information */
  esp_chip_info_t chip_info;
  uint32_t flash_size;
  esp_chip_info(&chip_info);
  ESP_LOGI(TAG, "This is %s chip with %d CPU core(s), %s%s%s%s, ",
          CONFIG_IDF_TARGET,
          chip_info.cores,
          (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
          (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
          (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
          (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

  unsigned major_rev = chip_info.revision / 100;
  unsigned minor_rev = chip_info.revision % 100;
  ESP_LOGI(TAG, "silicon revision v%d.%d, ", major_rev, minor_rev);
  if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
    ESP_LOGE(TAG, "Get flash size failed");
    return;
  }

  ESP_LOGI(TAG, "%" PRIu32 "MB %s flash", flash_size / (uint32_t)(1024 * 1024),
          (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

  ESP_LOGI(TAG, "Minimum free heap size: %" PRIu32 " bytes", esp_get_minimum_free_heap_size());
}


void System_resetAfter(int seconds) {

  // TODO: For StatTrak, use gpio_hold_en() to hold power enable pin high

  for (int i = seconds; i >= 0; i--) {
    ESP_LOGI(TAG, "Restarting in %d seconds...", i);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
  ESP_LOGI(TAG, "Restarting now.");
  // fflush(stdout);
  esp_restart();
}