#include "bsp_adc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "board.h"



#define APPLY_VOLTAGE_DIV_COEF(_voltage)  (((_voltage) * (36 + 10)) / 10) // 36kOhm + 10kOhm voltage divider

#define FILTER_SHIFT  (4) // Low-pass filter coefficient 1/16


static adc_oneshot_unit_handle_t s_handle;
static adc_cali_handle_t s_caliHandle;

// Filtered ADC voltage in mV, scaled by (1 << FILTER_SHIFT) to avoid truncation error
static int32_t s_adcVoltageAcc = 0;
static bool s_filterInit = false;


void BspAdc_init(void) {
  // Initialize ADC
  adc_oneshot_unit_init_cfg_t initConfig = {
    .unit_id = ADC_UNIT_1,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&initConfig, &s_handle));

  // Initialize ADC channels
  adc_oneshot_chan_cfg_t chanConfig = {
    .bitwidth = ADC_BITWIDTH_DEFAULT,
    .atten = ADC_ATTEN_DB_12,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(s_handle, ADC_CHANNEL_2, &chanConfig));


  // Set calibration scheme
  adc_cali_line_fitting_config_t caliConfig = {
    .unit_id = initConfig.unit_id,
    .atten = chanConfig.atten,
    .bitwidth = chanConfig.bitwidth,
  };
  ESP_ERROR_CHECK(adc_cali_create_scheme_line_fitting(&caliConfig, &s_caliHandle));

  gpio_set_direction(EN_BAT_ADC_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(EN_BAT_ADC_PIN, 1); // Enable ADC power
}


void BspAdc_deinit(void) {
  ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(s_caliHandle));
  ESP_ERROR_CHECK(adc_oneshot_del_unit(s_handle));
}


int BspAdc_readVoltage(void) {
  int raw = 0;
  int voltage = 0;

  // On a read error keep the last filtered value
  if(adc_oneshot_read(s_handle, ADC_CHANNEL_2, &raw) == ESP_OK &&
     adc_cali_raw_to_voltage(s_caliHandle, raw, &voltage) == ESP_OK) {
    if(!s_filterInit) {
      // Start from the first sample instead of ramping up from 0
      s_adcVoltageAcc = voltage << FILTER_SHIFT;
      s_filterInit = true;
    } else {
      // Simple low-pass filter for voltage
      s_adcVoltageAcc += voltage - (s_adcVoltageAcc >> FILTER_SHIFT);
    }
  }

  int32_t filtered = (s_adcVoltageAcc + (1 << (FILTER_SHIFT - 1))) >> FILTER_SHIFT;
  return APPLY_VOLTAGE_DIV_COEF(filtered);
}
