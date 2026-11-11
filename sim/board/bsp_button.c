#include "bsp_button.h"
#include "app.h"
#include "cyclic_buf.h"


#define BUTTON_LONG_PRESS_TIME  (700)

typedef struct
{
	uint32_t		key;

	uint16_t		state;

	uint8_t			press;
	uint8_t			release;
  uint8_t     hold;

  uint8_t     longPressArm;
  uint8_t     longPress;

	uint32_t		pressTime;
	uint32_t		releaseTime;
	uint32_t		holdTime;
} button_t;


static inline void _addEvent(uint8_t event);



/* > Local Variables ******************************************************************************/
static uint8_t s_btnEventBuf[32];
static cyclic_buf_t s_btnEvents;
static button_t s_btns[2] = {
  { .key = SDLK_LEFT },
  { .key = SDLK_RIGHT },
};

void BspButton_init(void) {
  CyclicBuf_init(&s_btnEvents, s_btnEventBuf, sizeof(s_btnEventBuf));
}

void BspButton_deinit(void) {
  // Nothing to do
}


uint8_t BspButton_getEvent(void) {
  if(CyclicBuf_isEmpty(&s_btnEvents)) {
    return EVENT__NONE;
  }

  // Read the next event from the cyclic buffer
  return CyclicBuf_read(&s_btnEvents);
}


void BspButton_process(SDL_Event* event) {
  uint8_t i;
  static bool bothPressed;
  static bool ignoreRelease;
  static uint8_t buttonStates = 0;

  const bool isPressed = (event->type == SDL_KEYDOWN);
  const uint32_t key = event->key.keysym.sym;
  const uint32_t now = SDL_GetTicks();

  // if(key == 256 && isPressed)
    // quitRequest = true;

  for(i=0; i<2; i++) {
    button_t* btn = &s_btns[i];

    if(key == btn->key) {

      if(isPressed) {
        if(btn->state == 0) {
          btn->pressTime = now;

          _addEvent(EVENT__RAW_PRESS_1 + i);

          // Both press
          if(s_btns[i ^ 1].state == 1) {
            bothPressed = true;
          }

          btn->longPressArm = true;
        } else if(btn->longPressArm) {
          btn->longPressArm = false;
          _addEvent(EVENT__LONG_PRESS_1 + i);
          btn->pressTime = now;
          ignoreRelease = true;
        } else if ((now - btn->pressTime) >= BUTTON_LONG_PRESS_TIME) {
          btn->pressTime = now;
          _addEvent(EVENT__LONG_HOLD_1 + i);
          ignoreRelease = true;
        }

        btn->state = 1;
      } 
      // Release
      else {

        _addEvent(EVENT__RAW_RELEASE_1 + i);
        
        if(ignoreRelease) {
          ignoreRelease = false;
        }
        else if((now - btn->pressTime) < 200) {

          if(bothPressed) {
            bothPressed = 0;
            ignoreRelease = 1;  // Ignore other buttons release

            _addEvent(EVENT__PRESS_BOTH);
          } else {
            _addEvent(EVENT__PRESS_1 + i);
          }
        } else {
          bothPressed = 0;
        }

        btn->longPressArm = false;
        btn->state = 0;
      }

      if(btn->state) 
        buttonStates |= (1<<i);
      else
        buttonStates &= ~(1<<i);
    }
  }
}


static inline void _addEvent(uint8_t event) {
  if(!CyclicBuf_isFull(&s_btnEvents)) {
    CyclicBuf_write(&s_btnEvents, event);
  } else {
    printf("Button event buffer is full, dropping event %d", event);
  }
}