
/* > Includes *************************************************************************************/
#include "bsp_oled.h"
#include "board.h"

#include "esp_log.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "ssd1307.h"
#include "gfx.h"


/* > Local Function Declarations ******************************************************************/
static void _initSpi(void);
static void _initOled(void);


/* > Local Variables ******************************************************************************/
static const char TAG[] = "OLED";

static spi_device_handle_t s_spiHandle;


// --- OLED ---
static ssd1307_drv_t s_oled = {
  .rstPin = OLED_RST_PIN,
  .dcPin = OLED_DC_PIN,
  .csPin = OLED_CS_PIN,
  .pwrPin = EN_12V_PIN,
};


#define OLED_WIDTH  128
#define OLED_HEIGHT 32

uint8_t   g_oled_buf[2][OLED_WIDTH * OLED_HEIGHT / 8];

gfx_ctx_t g_ctx = {
    .width = OLED_WIDTH,
    .height = OLED_HEIGHT,
    .frame_buf = g_oled_buf[0],
    .update = &Ssd1307_update,
};


/* > Function Definitions *************************************************************************/
void BspOled_init(void) {
  _initOled();

  Gfx_setContext(&g_ctx);
  Gfx_updateContext();
}


void BspOled_deinit(void) {
  Ssd1307_deinit();
}


/* > Local Function Definitions *******************************************************************/

static void _initOled(void) {
  gpio_set_direction(EN_12V_PIN, GPIO_MODE_OUTPUT);
  gpio_set_direction(OLED_RST_PIN, GPIO_MODE_OUTPUT);
  gpio_set_direction(OLED_DC_PIN, GPIO_MODE_OUTPUT);

  _initSpi();

  // Set SPI Handle
  s_oled.spiHandle = s_spiHandle;

  Ssd1307_init(&s_oled);
  ESP_LOGI(TAG, "OLED initialized");
}


static void _initSpi(void) {
  spi_bus_config_t buscfg = {
    .miso_io_num = -1,
    .mosi_io_num = OLED_DIN_PIN,
    .sclk_io_num = OLED_SCLK_PIN,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 128*4,
  };

  spi_device_interface_config_t devcfg = {
    .clock_speed_hz = 1 * 1000 * 1000,
    .mode = 3,
    .spics_io_num = OLED_CS_PIN,
    .queue_size = 3,
    // .pre_cb = _spiPreTransferCallback,
  };

  // Initialize SPI bus
  ESP_LOGI(TAG, "Initializing bus SPI%d...", OLED_SPI + 1);
  esp_err_t ret = spi_bus_initialize(OLED_SPI, &buscfg, 0);//SPI_DMA_CH_AUTO);
  if(ret != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_initialize() failed: %s", esp_err_to_name(ret));
    return;
  }

  // Initialize SPI device
  ESP_LOGI(TAG, "Adding SPI Device...");
  ret = spi_bus_add_device(OLED_SPI, &devcfg, &s_spiHandle);
  if(ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
  }
}


