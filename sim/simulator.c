#include <SDL2/SDL.h>
#include "bsp_button.h"
#include "bsp_oled.h"
#include "app.h"
#include "animation.h"
#include "eeprom.h"
#include "gfx_fx.h"

#ifdef __EMSCRIPTEN__
  #include <emscripten.h>
#endif


#define EEPROM_ENTRY_COUNT  (ENTRY_ID_COUNT)

const char* kEepromEntryKeys[EEPROM_ENTRY_COUNT] = {
  [ENTRY_ID__INIT] = "init",
  [ENTRY_ID__TOTAL_KILLS] = "total_kills",
  [ENTRY_ID__TOTAL_DEATHS] = "total_deaths",
  [ENTRY_ID__GAME_KILLS] = "game_kills",
  [ENTRY_ID__GAME_DEATHS] = "game_deaths",
  [ENTRY_ID__BATTERY_TYPE] = "battery_type",
  [ENTRY_ID__SHOW_BATTERY] = "show_battery",
  [ENTRY_ID__BRIGHTNESS] = "brightness",
  [ENTRY_ID__DIM_SCREEN] = "dim_screen",
  [ENTRY_ID__DIM_LEVEL] = "dim_level",
  [ENTRY_ID__AUTO_OFF] = "auto_off",
  [ENTRY_ID__SOUND] = "sound",
  [ENTRY_ID__COUNTER] = "counter",
  [ENTRY_ID__LOW_BAT] = "low_bat",
};
static uint32_t s_eepromEntries[EEPROM_ENTRY_COUNT];
eeprom_t g_eeprom = {
  .entries = s_eepromEntries,
  .entryCount = EEPROM_ENTRY_COUNT,
  .entryKeys = kEepromEntryKeys,
};

static bool s_running = 1;

void main_loop(void) {
  static uint32_t guiUpdateTime = 0;
  SDL_Event sdlEvent;
  uint32_t tNow = SDL_GetTicks();

  while (SDL_PollEvent(&sdlEvent)) {
    BspButton_process(&sdlEvent);
    if (sdlEvent.type == SDL_QUIT || 
        (sdlEvent.type == SDL_KEYDOWN && sdlEvent.key.keysym.sym == SDLK_ESCAPE)) {
      s_running = 0;
    }
  }

  event_t event = BspButton_getEvent();
  while(event) {
    App_updateActivity(tNow);

    switch(event) {
      // Switch views
      case EVENT__PRESS_BOTH: {
        view_id_t id = AppView_get();

        if(id == VIEW_ID__ASK && id == VIEW_ID__UPDATE)
          break;

        id++;
        if(id > VIEW_ID__SETTINGS)
          id = VIEW_ID__TOTAL;
        AppView_set(id);
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

  // Update OLED every 10ms
  if((tNow - guiUpdateTime) >= 10) {
    guiUpdateTime = tNow;

    App_update();

    Animation_runTickHandler(tNow);

    Gfx_clearAll(0);
    AppView_show();
    GfxFx_renderCollapse();

    Gfx_updateContext();

    SDL_Delay(10); // 100 FPS
  }
}



int main() {
  SDL_Init(SDL_INIT_VIDEO);

  BspOled_init();
  BspButton_init();

  // Initialize Memory
  Eeprom_init(&g_eeprom);

  App_init();


  uint32_t tNow = SDL_GetTicks();
  Animation_setTickCount(tNow);
  
  AppView_set(VIEW_ID__TOTAL);
  GfxFx_initCollapse(true);
  GfxFx_reverseCollapse();


#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(main_loop, 0, 1);
#else
    while (s_running) main_loop();
#endif


  BspButton_deinit();
  BspOled_deinit();

  SDL_Quit();
  return 0;
}
