/***************************************************************************************************
 * @file    : ssd1307.c
 * @brief   : SSD1307 OLED driver source code
 * @author  : Janis Zaharans
 * @date    : Jun 16, 2022
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "ssd1307.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"


/* > Local Function Declarations ******************************************************************/
static void _writeCommand(uint8_t cmd);
static void _writeCommand2(uint8_t cmd, uint8_t data);
static void _writeData(uint8_t* buf, uint32_t size);


/* > Macros ***************************************************************************************/
#define _resetLow()   gpio_set_level(s_drv->rstPin, 0)
#define _resetHigh()  gpio_set_level(s_drv->rstPin, 1)

#define _cmdMode()    gpio_set_level(s_drv->dcPin, 0)
#define _dataMode()   gpio_set_level(s_drv->dcPin, 1)

// #define _select()     HAL_GPIO_WritePin(s_drv->csGpio, s_drv->csPin, 0)
// #define _deselect()   HAL_GPIO_WritePin(s_drv->csGpio, s_drv->csPin, 1)

#define _vccOn()      gpio_set_level(s_drv->pwrPin, 1)
#define _vccOff()     gpio_set_level(s_drv->pwrPin, 0)


/* > Local Variables ******************************************************************************/
static ssd1307_drv_t* s_drv;


/* > Function Definitions *************************************************************************/
void Ssd1307_init(ssd1307_drv_t* drv) {

  s_drv = drv;

  // _deselect();

  // Reset
  _resetLow();
  vTaskDelay(1 / portTICK_PERIOD_MS);
  _resetHigh();
  vTaskDelay(1 / portTICK_PERIOD_MS);

  // Power on VCC
  _vccOn();
  vTaskDelay(10 / portTICK_PERIOD_MS);

#if defined(OLED_CH1115)
  _writeCommand(SSD1307_CMD__SET_DISPLAY_OFF);  // Display Off

  _writeCommand(SSD1307_CMD__SET_PAGE_START_ADDR);  // Set Page Address
  _writeCommand(SSD1307_CMD__SET_HIGH_COLUMN);      // Set Higher Column address
  _writeCommand(SSD1307_CMD__SET_LOW_COLUMN);       // Set Lower Column address

  _writeCommand(0xA4); // Set Entire Display OFF

  // Timing
  _writeCommand2(SSD1307_CMD__SET_DISPLAY_CLOCK_DIV, 0x51); // SET DISPLAY CLOCK
  
  // Hardware Configuration
  _writeCommand2(SSD1307_CMD__SET_MULTIPLEX_RATIO, 0x1F);   // Select Multiplex Ratio (1/32 Duty)
  _writeCommand2(SSD1307_CMD__SET_DISPLAY_OFFSET, 0x30);    // Set Display Offset
  _writeCommand(SSD1307_CMD__SET_DISPLAY_START_LINE);       // Set Display Start Line
  _writeCommand2(0xAD, 0x8A); // Set DC/DC - 0x8B: ON, 0x8A: OFF

  _writeCommand(SSD1307_CMD__SET_SEGMENT_REMAP);     // Set Segment Re-Map 0x00 -> 0-128, 0x01 -> 127-0
  _writeCommand(SSD1307_CMD__SET_COM_OUT_SCAN_DIR);        // Set COM Output Scan Direction

  _writeCommand(0xA2);  // Common pad configuration

  _writeCommand2(SSD1307_CMD__SET_CONTRAST_CONTROL, 0x35);  // Set Contrast

  // Timing
  _writeCommand2(SSD1307_CMD__SET_PRECHARGE_PERIOD, 0x22);      // Set Pre-Charge period
  _writeCommand2(SSD1307_CMD__SET_VCOMH_DESELECT_LEVEL, 0x35);  // Set Deselect Vcomh level

  _writeCommand(SSD1307_CMD__SET_NORMAL_DISPLAY); // Set Normal Display
  _writeCommand(SSD1307_CMD__SET_DISPLAY_ON);     // Display ON
#else
  // Send initialization sequence
  _writeCommand(SSD1307_CMD__SET_DISPLAY_OFF);  // Display Off

  // Timing
  _writeCommand2(SSD1307_CMD__SET_DISPLAY_CLOCK_DIV, 0x80); // SET DISPLAY CLOCK

  // Hardware Configuration
  _writeCommand2(SSD1307_CMD__SET_MULTIPLEX_RATIO, 0x1F);   // Select Multiplex Ratio (1/32 Duty)
  _writeCommand2(SSD1307_CMD__SET_DISPLAY_OFFSET, 0x00);    // Set Display Offset
  _writeCommand(SSD1307_CMD__SET_DISPLAY_START_LINE);       // Set Display Start Line
  _writeCommand(SSD1307_CMD__SET_SEGMENT_REMAP | 0x01);     // Set Segment Re-Map 0x00 -> 0-128, 0x01 -> 127-0
  _writeCommand(SSD1307_CMD__SET_COM_OUT_SCAN_DIR);         // Set COM Output Scan Direction
  _writeCommand2(SSD1307_CMD__SET_SEG_PINS_HW_CONF, 0x12);  // Set COM Hardware Configuration - Alternative (odd/even)

  _writeCommand2(SSD1307_CMD__SET_CONTRAST_CONTROL, 0x48);  // Set Contrast
  // _writeCommand2(SSD1307_CMD__SET_CONTRAST_CONTROL, 0xFF);  // Set Contrast

  // Timing
  _writeCommand2(SSD1307_CMD__SET_PRECHARGE_PERIOD, 0xA1);      // Set Pre-Charge period
  _writeCommand2(SSD1307_CMD__SET_VCOMH_DESELECT_LEVEL, 0x00);  // Set Deselect Vcomh level

  // _writeCommand(SSD1307_CMD__SET_MEM_ADDR_MODE);  // Set Horizontal addressing

  _writeCommand(SSD1307_CMD__ENTRIE_DISPLAY_ON);  // Entire Display ON
  _writeCommand(SSD1307_CMD__SET_NORMAL_DISPLAY); // Set Normal Display
  _writeCommand(SSD1307_CMD__SET_DISPLAY_ON);     // Display ON
#endif
}

void Ssd1307_deinit(void) {
  _writeCommand(SSD1307_CMD__SET_DISPLAY_OFF);  // Display OFF
  vTaskDelay(1 / portTICK_PERIOD_MS);
  _vccOff();
}


void Ssd1307_setContrast(uint8_t contrast) {
  _writeCommand2(SSD1307_CMD__SET_CONTRAST_CONTROL, contrast);
}

void Ssd1307_setVcomhDeselectLevel(uint8_t vcomh) {
  if(vcomh > 0x35) vcomh = 0x35;
  _writeCommand2(SSD1307_CMD__SET_VCOMH_DESELECT_LEVEL, vcomh);  // Set Deselect Vcomh level
}

void Ssd1307_setBrightness(uint16_t brightness) {
  if(brightness > SSD1307_MAX_BRIGHTNESS)
    brightness = SSD1307_MAX_BRIGHTNESS;

  if(brightness >= SSD1307_VCOMH_VAR_RANGE) {
    _writeCommand2(SSD1307_CMD__SET_CONTRAST_CONTROL, brightness - SSD1307_VCOMH_VAR_RANGE);
    _writeCommand2(SSD1307_CMD__SET_VCOMH_DESELECT_LEVEL, SSD1307_VCOMH_MAX_VALUE);
  } else {
    _writeCommand2(SSD1307_CMD__SET_CONTRAST_CONTROL, 0x00);
    _writeCommand2(SSD1307_CMD__SET_VCOMH_DESELECT_LEVEL, SSD1307_VCOMH_BASE_VALUE + brightness);
  }
}


void Ssd1307_setCursor(uint16_t column, uint16_t page) {
  _writeCommand(SSD1307_CMD__SET_PAGE_START_ADDR | page);
  _writeCommand(SSD1307_CMD__SET_LOW_COLUMN + (column % 16));
  _writeCommand(SSD1307_CMD__SET_HIGH_COLUMN + (column / 16)); 
}


void Ssd1307_update(uint8_t* buf) {
  uint16_t x, y;

  for(y=0; y<(32/8); y++) {

    Ssd1307_setCursor(0, y);
    vTaskDelay(1 / portTICK_PERIOD_MS);
    _writeData(buf, 32);
    buf += 32;
    _writeData(buf, 32);
    buf += 32;
    _writeData(buf, 32);
    buf += 32;
    _writeData(buf, 32);
    buf += 32;
  }
}


/* > Local Function Definitions *******************************************************************/

static void _writeCommand(uint8_t cmd) {
  _cmdMode();
  // _select();
  spi_transaction_t t = {
    .length = sizeof(cmd) * 8,
    .tx_buffer = &cmd,
  };
  spi_device_polling_transmit((spi_device_handle_t)s_drv->spiHandle, &t);
  // _deselect();
}

static void _writeCommand2(uint8_t cmd, uint8_t data) {
  _cmdMode();
  // _select();
  uint8_t buf[2] = { cmd, data };
  spi_transaction_t t = {
    .length = sizeof(buf) * 8,
    .tx_buffer = buf,
  };
  spi_device_polling_transmit((spi_device_handle_t)s_drv->spiHandle, &t);
  // _deselect();
}


static void _writeData(uint8_t* buf, uint32_t size) {
  _dataMode();
  // _select();
  spi_transaction_t t = {
    .length = size * 8,
    .tx_buffer = buf,
  };
  spi_device_polling_transmit((spi_device_handle_t)s_drv->spiHandle, &t);
  // _deselect();
}