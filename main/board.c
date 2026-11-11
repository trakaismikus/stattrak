
/* > Includes *************************************************************************************/
#include "board.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_mac.h"
#include "esp_app_desc.h"

#include "bsp_adc.h"
#include "bsp_buzzer.h"
#include "bsp_button.h"
#include "bsp_oled.h"


/* > Local Function Declarations ******************************************************************/

/* > Local Variables ******************************************************************************/
static const char TAG[] = "Board";

/* > Function Definitions *************************************************************************/

void Board_init(void) {
  BspAdc_init();
  BspBuzzer_init();
  BspButton_init();
  BspOled_init();

  ESP_LOGI(TAG, "Board initialized");
}


void Board_deinit(void) {
  BspOled_deinit();
  ESP_LOGI(TAG, "Board deinitialized");
}


void Board_powerOn(void) {
  gpio_set_direction(PWR_ON_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(PWR_ON_PIN, 1);
}

void Board_powerOff(void) {
  Board_deinit();
  gpio_set_level(PWR_ON_PIN, 0);
  esp_light_sleep_start();
  // esp_deep_sleep_start();
}


uint16_t Board_readVoltage(void) {
  return BspAdc_readVoltage();
}

void Board_getUID(uint8_t* uid) {
  esp_efuse_mac_get_default(uid);
}

const char* Board_getFwVersion(void) {
  const esp_app_desc_t* app_desc = esp_app_get_description();
  return app_desc->version;
}