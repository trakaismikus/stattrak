/***************************************************************************************************
 * @file    : main.c
 * @brief   : StatTrak simulator with a window (SDL2) or in a web page (Emscripten).
 *
 * It runs the real firmware code (see sim/README.md). Keys:
 *
 *   LEFT        button 1 (kill)
 *   RIGHT       button 2 (death)
 *   UP / DOWN   battery voltage + / - 100 mV
 *   U           a phone sends new firmware (only while the update view is open)
 *   A           a phone accepts the new firmware (only while the device waits for it)
 *   ESC         quit
 *
 * The device powers on by itself: the simulator holds the two buttons for a short time, as a
 * user does. After a power-off, hold LEFT and RIGHT together to power on again.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
  #include <emscripten.h>
#elif defined(_WIN32)
  #include <process.h>
#else
  #include <unistd.h>
#endif

#include "host.h"
#include "ssd1307.h"

/* > Defines **************************************************************************************/
#define PIXEL_SIZE          (4)
#define WINDOW_WIDTH        (HOST_OLED_WIDTH * PIXEL_SIZE)
#define WINDOW_HEIGHT       (HOST_OLED_HEIGHT * PIXEL_SIZE)

#define AUTO_HOLD_MS        (800)     /* The simulator holds both buttons this long at power-on */
#define FAKE_TRANSFER_MS    (8000)
#define VOLTAGE_STEP_MV     (100)

#ifdef __EMSCRIPTEN__
  #define DEFAULT_STATE_FILE  "/stattrak_sim_state.txt"
#else
  #define DEFAULT_STATE_FILE  "stattrak_sim_state.txt"
#endif

/* > Local Variables ******************************************************************************/
static SDL_Window*   s_window = NULL;
static SDL_Renderer* s_renderer = NULL;

static bool     s_keys[2] = { false, false };
static bool     s_autoHold = true;
static uint32_t s_realStart = 0;

static char**   s_argv = NULL;

/* > Local Functions ******************************************************************************/

static void _sleepReal(uint32_t ms) {
#ifdef __EMSCRIPTEN__
  emscripten_sleep(ms);   /* Gives time to the browser */
#else
  SDL_Delay(ms);
#endif
}

static void _setTitle(const char* text) {
  char title[160];
  snprintf(title, sizeof(title), "StatTrak simulator - %s", text);
  SDL_SetWindowTitle(s_window, title);
}

static void _showHelp(void) {
  char text[120];
  snprintf(text, sizeof(text), "LEFT/RIGHT = buttons | battery %u mV (UP/DOWN) | U = update, A = accept",
           (unsigned)HostBoard_getVoltage());
  _setTitle(text);
}

static void _render(const uint8_t* frame) {
  uint8_t level = 0;

  if(frame != NULL && HostOled_isOn()) {
    /* Keep a small brightness for the lowest setting, so the picture stays readable */
    const float coef = (float)HostOled_getBrightness() / (float)SSD1307_MAX_BRIGHTNESS;
    level = (uint8_t)(coef * 230.0f) + 25;
  }

  SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
  SDL_RenderClear(s_renderer);
  SDL_SetRenderDrawColor(s_renderer, level, level, level, 255);

  if(frame != NULL && level > 0) {
    for(int x = 0; x < HOST_OLED_WIDTH; x++) {
      for(int y = 0; y < HOST_OLED_HEIGHT; y++) {
        if(frame[x + (y / 8) * HOST_OLED_WIDTH] & (1 << (y % 8))) {
          const SDL_Rect pixel = { x * PIXEL_SIZE, y * PIXEL_SIZE, PIXEL_SIZE, PIXEL_SIZE };
          SDL_RenderFillRect(s_renderer, &pixel);
        }
      }
    }
  }

  SDL_RenderPresent(s_renderer);
}

static void _saveWebState(void) {
#ifdef __EMSCRIPTEN__
  EM_ASM({
    try {
      localStorage.setItem('stattrak_sim_state', FS.readFile(UTF8ToString($0), { encoding: 'utf8' }));
    } catch(e) { }
  }, DEFAULT_STATE_FILE);
#endif
}

static void _loadWebState(void) {
#ifdef __EMSCRIPTEN__
  EM_ASM({
    try {
      var text = localStorage.getItem('stattrak_sim_state');
      if(text) FS.writeFile(UTF8ToString($0), text);
    } catch(e) { }
  }, DEFAULT_STATE_FILE);
#endif
}

/* Starts the program again: the same as a new power-on of the device. Does not return. */
static void _restart(void) {
#ifdef __EMSCRIPTEN__
  EM_ASM({ location.reload(); });
  emscripten_sleep(100000);
#else
  SDL_Quit();
  #if defined(_WIN32)
    _execv(s_argv[0], (const char* const*)s_argv);
  #else
    execv(s_argv[0], s_argv);
  #endif
  perror("restart failed");
#endif
  exit(1);
}

/* Reads the keyboard. Returns false when the user wants to quit. */
static bool _pollEvents(bool running) {
  SDL_Event event;

  while(SDL_PollEvent(&event)) {
    if(event.type == SDL_QUIT)
      return false;

    if(event.type != SDL_KEYDOWN && event.type != SDL_KEYUP)
      continue;

    const bool down = (event.type == SDL_KEYDOWN);

    switch(event.key.keysym.sym) {
      case SDLK_ESCAPE:
        if(down)
          return false;
        break;

      case SDLK_LEFT:   s_keys[0] = down; break;
      case SDLK_RIGHT:  s_keys[1] = down; break;

      case SDLK_UP:
      case SDLK_DOWN:
        if(down && running) {
          int voltage = (int)HostBoard_getVoltage();
          voltage += (event.key.keysym.sym == SDLK_UP) ? VOLTAGE_STEP_MV : -VOLTAGE_STEP_MV;
          if(voltage < 0) voltage = 0;
          if(voltage > 20000) voltage = 20000;
          HostBoard_setVoltage((uint16_t)voltage);
          _showHelp();
        }
        break;

      case SDLK_u:
        if(down && running && !event.key.repeat) {
          if(HostOta_startTransfer(FAKE_TRANSFER_MS))
            _setTitle("a phone sends new firmware...");
          else
            _setTitle("U works only while the update view is open");
        }
        break;

      case SDLK_a:
        if(down && running && !event.key.repeat) {
          if(!HostOta_isActive())
            _setTitle("A works only while the device waits for a phone");
          else if(HostOta_accept())
            _setTitle("the phone accepted the new firmware");
          else
            _setTitle("there is no new firmware to accept");
        }
        break;

      default: break;
    }
  }

  return true;
}

/* > Front end hooks ******************************************************************************/

static void _idle(void) {
  if(!_pollEvents(true))
    Host_end(HOST_END__QUIT, "window closed");

  if(s_autoHold && Host_nowMs() >= AUTO_HOLD_MS)
    s_autoHold = false;

  HostButton_setLevel(0, s_keys[0] || s_autoHold);
  HostButton_setLevel(1, s_keys[1] || s_autoHold);

  /* Keep simulated time and real time together */
  const int32_t ahead = (int32_t)((uint32_t)Host_nowMs() - (SDL_GetTicks() - s_realStart));
  if(ahead > 0) {
    _sleepReal((uint32_t)ahead);
  } else {
    if(ahead < -250)
      s_realStart = SDL_GetTicks() - (uint32_t)Host_nowMs();   /* Too slow: start again from here */

#ifdef __EMSCRIPTEN__
    /* The browser must get time also when the simulator is late (for example in a hidden tab).
     * If not, the page does not answer any more. */
    static uint64_t s_lastYield = 0;
    if((Host_nowMs() - s_lastYield) >= 20) {
      s_lastYield = Host_nowMs();
      emscripten_sleep(0);
    }
#endif
  }
}

static void _frame(const uint8_t* frame) {
  _render(frame);
}

static void _stateChanged(void) {
  _saveWebState();
}

static void _end(host_end_t reason, const char* detail) {
  _saveWebState();
  printf("StatTrak simulator: session ended: %s (%s)\n", Host_endName(reason), detail);
  fflush(stdout);

  if(reason == HOST_END__QUIT || reason == HOST_END__ABORT) {
    if(reason == HOST_END__ABORT)
      fprintf(stderr, "The firmware stopped with an error. On a device this is a crash and a restart.\n");
    SDL_Quit();
    return; /* The host exits */
  }

  if(reason == HOST_END__RESET)
    _restart();

  /* Power off: black display. Wait until the user holds the two buttons. */
  _render(NULL);
  _setTitle("POWER OFF - hold LEFT + RIGHT to power on");
  s_keys[0] = s_keys[1] = false;

  while(!(s_keys[0] && s_keys[1])) {
    if(!_pollEvents(false)) {
      SDL_Quit();
      return;
    }
    _sleepReal(10);
  }

  _restart();
}

/* > Main *****************************************************************************************/

int main(int argc, char** argv) {
  host_config_t config = {
    .stateFile = DEFAULT_STATE_FILE,
    .voltageMv = 4000,
  };

  s_argv = argv;

  for(int i = 1; i < argc; i++) {
    if(strcmp(argv[i], "--state") == 0 && (i + 1) < argc) {
      config.stateFile = argv[++i];
    } else if(strcmp(argv[i], "--voltage") == 0 && (i + 1) < argc) {
      config.voltageMv = (uint16_t)strtoul(argv[++i], NULL, 10);
    } else if(strcmp(argv[i], "--log") == 0) {
      config.log = true;
    } else {
      fprintf(stderr,
        "StatTrak simulator\n"
        "  --state FILE   file that keeps the device flash content (default: %s)\n"
        "  --voltage MV   battery voltage in millivolts (default: 4000)\n"
        "  --log          print firmware log lines\n"
        "Keys: LEFT/RIGHT = buttons, UP/DOWN = battery voltage, U = send update, A = accept update, ESC = quit\n",
        DEFAULT_STATE_FILE);
      return 64;
    }
  }

  if(SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  s_window = SDL_CreateWindow("StatTrak simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
  s_renderer = (s_window != NULL) ? SDL_CreateRenderer(s_window, -1, 0) : NULL;
  if(s_renderer == NULL) {
    fprintf(stderr, "Cannot open the window: %s\n", SDL_GetError());
    return 1;
  }

  _loadWebState();

  const host_frontend_t frontend = {
    .idle = _idle,
    .frame = _frame,
    .stateChanged = _stateChanged,
    .end = _end,
  };

  Host_init(&config, &frontend);
  HostButton_setLevel(0, s_autoHold);
  HostButton_setLevel(1, s_autoHold);
  _showHelp();

  s_realStart = SDL_GetTicks();
  Host_run();
  return 0;
}
