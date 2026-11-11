
#include "bsp_buzzer.h"
#include "board.h"
#include "driver/ledc.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"


#define BUZZER_TIMER              LEDC_TIMER_0
#define BUZZER_MODE               LEDC_LOW_SPEED_MODE
#define BUZZER_OUTPUT_IO          BUZZER_PIN
#define BUZZER_CHANNEL            LEDC_CHANNEL_0
#define BUZZER_DUTY_RES           LEDC_TIMER_8_BIT
#define BUZZER_DUTY               (128)
#define BUZZER_FREQUENCY          (4000) // Frequency in Hertz. Set frequency at 4 kHz


static void _buzzerTimerCallback(TimerHandle_t xTimer);


static TimerHandle_t s_buzzerTimer;


void BspBuzzer_init(void) {

  // Prepare and then apply the LEDC PWM timer configuration
  ledc_timer_config_t buzzerTimer = {
    .speed_mode       = BUZZER_MODE,
    .duty_resolution  = BUZZER_DUTY_RES,
    .timer_num        = BUZZER_TIMER,
    .freq_hz          = BUZZER_FREQUENCY,
    .clk_cfg          = LEDC_AUTO_CLK
  };
  ESP_ERROR_CHECK(ledc_timer_config(&buzzerTimer));

  // Prepare and then apply the LEDC PWM channel configuration
  ledc_channel_config_t buzzerChannel = {
    .speed_mode     = BUZZER_MODE,
    .channel        = BUZZER_CHANNEL,
    .timer_sel      = BUZZER_TIMER,
    .intr_type      = LEDC_INTR_DISABLE,
    .gpio_num       = BUZZER_OUTPUT_IO,
    .duty           = 0,
    .hpoint         = 0
  };
  ESP_ERROR_CHECK(ledc_channel_config(&buzzerChannel));

  // Initialize Buzzer timer
  s_buzzerTimer = xTimerCreate("BuzzerTimer", pdMS_TO_TICKS(10), pdFALSE, NULL, _buzzerTimerCallback);
}


void BspBuzzer_deinit(void) {
  ledc_stop(BUZZER_MODE, BUZZER_CHANNEL, 0);
}


void Buzzer_beep(uint16_t freq, uint8_t duty) {
  ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, duty);
  ledc_set_freq(BUZZER_MODE, BUZZER_TIMER, freq);
  ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
}


void Buzzer_off(void) {
  ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0);
  ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
}


void Buzzer_click(uint16_t freq) {
  Buzzer_tone(freq, 10);
}


void Buzzer_tone(uint16_t freq, uint16_t ms) {
  Buzzer_beep(freq, 128);

  if(s_buzzerTimer != NULL) {
    // Starts the off timer with this length (a click is 10 ms; a new note replaces the last one)
    xTimerChangePeriod(s_buzzerTimer, pdMS_TO_TICKS(ms), 0);
  } else {
    vTaskDelay(pdMS_TO_TICKS(ms));
    Buzzer_off();
  }
}


static void _buzzerTimerCallback(TimerHandle_t xTimer) {
  (void)xTimer;

  // Turn off buzzer
  Buzzer_off();
}