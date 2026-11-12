/***************************************************************************************************
 * @file    : host_board.c
 * @brief   : Stand-in for main/board.c and the display, battery and buzzer drivers.
 *
 * The button logic is NOT replaced: main/board/bsp_button.c is compiled as it is.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "host.h"

#include <stdio.h>
#ifdef __EMSCRIPTEN__
  #include <emscripten.h>
#endif
#include <string.h>

#include "board.h"
#include "bsp_oled.h"
#include "ssd1307.h"
#include "gfx.h"

/* > Defines **************************************************************************************/
#ifndef PROJECT_VER
  #define PROJECT_VER "0.0.0"
#endif

#define GUARD_SIZE    (64)
#define GUARD_VALUE   (0xA5)

/* > Local Function Declarations ******************************************************************/
static void _update(uint8_t* buf);

/* > Local Variables ******************************************************************************/
static bool     s_latched = false;
static uint16_t s_voltageMv = 0;

/* The device has two buffers (g_oled_buf[2][...]). Guard bytes on each side show a write outside. */
static struct {
  uint8_t guardBefore[GUARD_SIZE];
  uint8_t buf[2][HOST_FRAME_SIZE];
  uint8_t guardAfter[GUARD_SIZE];
} s_fb;

static gfx_ctx_t s_ctx = {
  .width = HOST_OLED_WIDTH,
  .height = HOST_OLED_HEIGHT,
  .frame_buf = s_fb.buf[0],
  .update = &_update,
};

static uint8_t  s_shownFrame[HOST_FRAME_SIZE];
static uint32_t s_frameCount = 0;
static uint16_t s_brightness = 0;
static bool     s_displayOn = false;

static uint32_t s_clickCount = 0;
static uint16_t s_lastFreq = 0;

/* > Front end API ********************************************************************************/

bool HostBoard_isLatched(void)                    { return s_latched; }
void HostBoard_setVoltage(uint16_t millivolts)    { s_voltageMv = millivolts; }
uint16_t HostBoard_getVoltage(void)               { return s_voltageMv; }

const uint8_t* HostOled_getFrame(void)            { return s_shownFrame; }
uint32_t HostOled_getFrameCount(void)             { return s_frameCount; }
uint16_t HostOled_getBrightness(void)             { return s_brightness; }
bool HostOled_isOn(void)                          { return s_displayOn; }

bool HostOled_guardsIntact(void) {
  for(uint32_t i = 0; i < GUARD_SIZE; i++) {
    if(s_fb.guardBefore[i] != GUARD_VALUE || s_fb.guardAfter[i] != GUARD_VALUE)
      return false;
  }
  return true;
}

uint32_t HostBuzzer_getClickCount(void)           { return s_clickCount; }
uint16_t HostBuzzer_getLastFreq(void)             { return s_lastFreq; }

/* > main/board.h *********************************************************************************/

void Board_init(void) {
  BspButton_init();   /* Real code: main/board/bsp_button.c */
  BspOled_init();
}

void Board_deinit(void) {
  BspOled_deinit();
}

void Board_powerOn(void) {
  s_latched = true;
}

void Board_powerOff(void) {
  Board_deinit();
  s_latched = false;

  /* On the device the power goes away here. */
  Host_end(HOST_END__POWER_OFF, "Board_powerOff");
}

uint16_t Board_readVoltage(void) {
  return s_voltageMv;
}

void Board_getUID(uint8_t* uid) {
  static const uint8_t kUid[6] = { 0x51, 0xA7, 0x7A, 0x4B, 0x00, 0x01 };
  memcpy(uid, kUid, sizeof(kUid));
}

const char* Board_getFwVersion(void) {
  return PROJECT_VER;
}

/* > Display **************************************************************************************/

void BspOled_init(void) {
  memset(s_fb.guardBefore, GUARD_VALUE, sizeof(s_fb.guardBefore));
  memset(s_fb.guardAfter, GUARD_VALUE, sizeof(s_fb.guardAfter));
  memset(s_fb.buf, 0, sizeof(s_fb.buf));

  s_displayOn = true;
  Gfx_setContext(&s_ctx);
  Gfx_updateContext();
}

void BspOled_deinit(void) {
  Ssd1307_deinit();
}

void Ssd1307_setBrightness(uint16_t brightness) {
  if(brightness > SSD1307_MAX_BRIGHTNESS)
    brightness = SSD1307_MAX_BRIGHTNESS;
  s_brightness = brightness;
}

void Ssd1307_deinit(void) {
  s_displayOn = false;
}

static void _update(uint8_t* buf) {
  memcpy(s_shownFrame, buf, HOST_FRAME_SIZE);
  s_frameCount++;

  Host_notifyFrame(s_shownFrame);
}

/* > Buzzer ***************************************************************************************/

// The web page plays the buzzer with the browser sound (Module.simBeep in sim/web/shell.html.in).
// The window and the headless simulators have no sound.
static void _sound(uint16_t freq, uint16_t ms) {
#ifdef __EMSCRIPTEN__
  EM_ASM({ if(Module.simBeep) Module.simBeep($0, $1); }, freq, ms);
#else
  (void)freq;
  (void)ms;
#endif
}

void Buzzer_click(uint16_t freq) {
  s_clickCount++;
  s_lastFreq = freq;
  _sound(freq, 10);
}

void Buzzer_tone(uint16_t freq, uint16_t ms) {
  s_clickCount++;   // The tests count a note like a click
  s_lastFreq = freq;
  _sound(freq, ms);
}

void Buzzer_beep(uint16_t freq, uint8_t duty) {
  (void)duty;
  s_lastFreq = freq;
}

void Buzzer_off(void) {
}
