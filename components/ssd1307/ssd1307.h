/***************************************************************************************************
 * @file    : ssd1307.h
 * @brief   : SSD1307 OLED driver header
 * @author  : Janis Zaharans
 * @date    : Jun 16, 2022
 **************************************************************************************************/

#ifndef _SSD1307_H
#define _SSD1307_H

/* > Includes *************************************************************************************/
#include <stdint.h>

#define OLED_CH1115

#define SSD1307_VCOMH_MAX_VALUE   (0x35)
#define SSD1307_VCOMH_BASE_VALUE  (0x00)
#define SSD1307_VCOMH_VAR_RANGE   (SSD1307_VCOMH_MAX_VALUE - SSD1307_VCOMH_BASE_VALUE)


#define SSD1307_MAX_BRIGHTNESS  (0xFF + SSD1307_VCOMH_VAR_RANGE) // Contrast + VCOMH Deselect value

/* > Enumerations *********************************************************************************/

enum ssd1307_cmd_e {
  // Fundamental Command Table
  SSD1307_CMD__SET_CONTRAST_CONTROL = 0x81,
  SSD1307_CMD__ENTRIE_DISPLAY_ON    = 0xA4,
  // SSD1307_CMD__ENTRIE_DISPLAY_ON   = 0xA5,
  SSD1307_CMD__SET_NORMAL_DISPLAY   = 0xA6, // Normal display
  SSD1307_CMD__SET_INVERSE_DISPLAY  = 0xA7, // Inverse display
  SSD1307_CMD__SET_DISPLAY_OFF      = 0xAE, // Display OFF (sleepmode)
  SSD1307_CMD__SET_DISPLAY_ON       = 0xAF, // Display ON in normal mode

  // Addressing Setting Command Table
  SSD1307_CMD__SET_LOW_COLUMN       = 0x00, // 0x00 - 0x0F
  SSD1307_CMD__SET_HIGH_COLUMN      = 0x10, // 0x10 - 0x17
  SSD1307_CMD__SET_MEM_ADDR_MODE    = 0x20, // 0 - horizontal, 1 - vertical, 2 - page
  SSD1307_CMD__SET_COL_ADDR         = 0x21, // Set Column Address
  SSD1307_CMD__SET_PAGE_ADDR        = 0x22, // Set Page Address
  SSD1307_CMD__SET_PAGE_START_ADDR  = 0xB0, // Set Page Start Address for Page Addressing Mode

  // Hardware Configuration (Panel resolution & layout related) Command Table
  SSD1307_CMD__SET_DISPLAY_START_LINE = 0x40,
  SSD1307_CMD__SET_SEGMENT_REMAP      = 0xA0, // 0xA0 or 0xA1
  SSD1307_CMD__SET_MULTIPLEX_RATIO    = 0xA8,
  SSD1307_CMD__SET_COM_OUT_SCAN_DIR   = 0xC0, // 0xC0 or 0xC8
  SSD1307_CMD__SET_DISPLAY_OFFSET     = 0xD3,
  SSD1307_CMD__SET_SEG_PINS_HW_CONF   = 0xDA,

  // Timing & Driving scheme Setting Command Table
  SSD1307_CMD__SET_DISPLAY_CLOCK_DIV    = 0xD5,
  SSD1307_CMD__SET_PRECHARGE_PERIOD     = 0xD9,
  SSD1307_CMD__SET_VCOMH_DESELECT_LEVEL = 0xDB,

  // Others
  SSD1307_CMD__SET_NOP  = 0xE3,
};


/* > Type Definitions *****************************************************************************/
typedef struct {
  uint32_t rstPin;
  uint32_t dcPin;
  uint32_t csPin;
  uint32_t pwrPin;
  void* spiHandle;
} ssd1307_drv_t;


/* > Function Declarations *****************************************************************************/
void Ssd1307_init(ssd1307_drv_t* drv);
void Ssd1307_deinit(void);
void Ssd1307_update(uint8_t* buf);
void Ssd1307_setContrast(uint8_t contrast);
void Ssd1307_setBrightness(uint16_t brightness);


#endif // _SSD1307_H