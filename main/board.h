#ifndef _BOARD_H
#define _BOARD_H


#include "bsp_adc.h"
#include "bsp_buzzer.h"
#include "bsp_button.h"


#define BTN_PIN         (4)
#define BUZZER_PIN      (5)
#define EN_12V_PIN      (6)
#define OLED_DIN_PIN    (7)
#define OLED_CS_PIN     (18)
#define OLED_RST_PIN    (10)
#define OLED_DC_PIN     (9)
#define OLED_SCLK_PIN   (8)
#define EN_BAT_ADC_PIN  (3)
#define ADC_BAT_PIN     (2)
#define PWR_ON_PIN      (1)
#define BTN2_PIN        (0)
#define RXD_PIN         (19)
#define TXD_PIN         (20)


#define OLED_SPI        SPI2_HOST


void Board_init(void);
void Board_deinit(void);

void Board_getUID(uint8_t* uid);
const char* Board_getFwVersion(void);

void Board_powerOn(void);
void Board_powerOff(void);

uint16_t Board_readVoltage(void);

// Why the chip started: a device_log_reason_t value of main/device_log.h (power on, restart,
// crash, watchdog, brown-out, ...). Only reads; for the device log.
uint8_t Board_getResetReason(void);


#endif // _BOARD_H