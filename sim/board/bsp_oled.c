#include "bsp_oled.h"
#include "gfx.h"
#include <SDL2/SDL.h>


/* > Defines **************************************************************************************/
#define WINDOW_WIDTH    (OLED_WIDTH * OLED_PIXEL_SIZE)
#define WINDOW_HEIGHT   (OLED_HEIGHT * OLED_PIXEL_SIZE)


#define SSD1307_VCOMH_MAX_VALUE   (0x35)
#define SSD1307_VCOMH_BASE_VALUE  (0x00)
#define SSD1307_VCOMH_VAR_RANGE   (SSD1307_VCOMH_MAX_VALUE - SSD1307_VCOMH_BASE_VALUE)
#define SSD1307_MAX_BRIGHTNESS    (0xFF + SSD1307_VCOMH_VAR_RANGE) // Contrast + VCOMH Deselect value


/* > Local Function Declarations ******************************************************************/
static void _update(uint8_t* buf);


/* > Local Variables ******************************************************************************/
static uint8_t s_oledBuf[2][OLED_WIDTH * OLED_HEIGHT / 8];

static gfx_ctx_t s_ctx = {
    .width = OLED_WIDTH,
    .height = OLED_HEIGHT,
    .frame_buf = s_oledBuf[0],
    .update = &_update,
};

static uint8_t s_brightness = 0;

/* > Function Definitions *************************************************************************/
void BspOled_init(void) {
  SDL_Window *window = SDL_CreateWindow("StatTrak",
    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);

  Gfx_setContext(&s_ctx);
  Gfx_updateContext();
}

void BspOled_deinit(void) {
  SDL_Window* window = SDL_GetWindowFromID(1);
  SDL_DestroyRenderer(SDL_GetRenderer(window));
  SDL_DestroyWindow(window);
}


static void _update(uint8_t* buf) {
  SDL_Window* window = SDL_GetWindowFromID(1);
  SDL_Renderer* renderer = SDL_GetRenderer(window);

  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Black background
  SDL_RenderClear(renderer);

  SDL_SetRenderDrawColor(renderer, s_brightness, s_brightness, s_brightness, 255); // White for pixels

  for (int x = 0; x < OLED_WIDTH; x++) {
    for (int y = 0; y < OLED_HEIGHT; y++) {
      int byte_index = x + (y / 8) * OLED_WIDTH;
      int bit_index = y % 8;

      if (buf[byte_index] & (1 << bit_index)) {
        SDL_Rect pixel = { x * OLED_PIXEL_SIZE, y * OLED_PIXEL_SIZE, OLED_PIXEL_SIZE, OLED_PIXEL_SIZE }; // Scale up for visibility
        SDL_RenderFillRect(renderer, &pixel);
      }
    }
  }

  SDL_RenderPresent(renderer);
}


void Ssd1307_setBrightness(uint16_t brightness) {
  (void)brightness;
  
  float coef = (float)brightness / (float)SSD1307_MAX_BRIGHTNESS;
  s_brightness = (uint8_t)(coef * 230) + 25; // Keep some minimum brightness
}

void Ssd1307_deinit(void) {
  // Not implemented in simulator
}