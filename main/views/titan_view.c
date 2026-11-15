/***************************************************************************************************
 * @file    : titan_view.c
 * @brief   : The TITAN app (see titan_view.h).
 *
 * Pairing (first time, or after FORGET in the TITAN settings):
 *   PAIR (hold 2: search) -> SEARCH -> PICK (1: next, hold 2: pick) -> PIN (the letter wheel)
 *   -> CHECK (connect, send the PIN) -> BATTERY (1: next, hold 2: OK) -> PLAY
 * Paired: CONNECTING -> PLAY. PLAY: 1 = reload, 2 = SESSION. SESSION: hold 2 = a new session.
 * An alert covers PLAY and SESSION; any button closes it.
 * Both buttons = the app menu (main.c). The radio is on only while this view is open.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "titan_view.h"

#include <stdio.h>
#include <string.h>

#include "view.h"
#include "app.h"
#include "app_view.h"
#include "bt_start.h"
#include "hold_button.h"
#include "gate_ble.h"
#include "titan.h"
#include "gfx.h"
#include "font.h"
#include "font_glyph.h"
#include "time.h"

/* > Defines **************************************************************************************/
#define SCREEN_W          128
#define CHECK_LIMIT_MS    15000   // Pairing: the Titan with this serial must answer in this time
#define WRONG_PIN_MS      1500
#define BLINK_MS          500
#define PIN_BOX_W         13
#define PIN_BOX_X0        ((SCREEN_W - GATE_PIN_LEN * PIN_BOX_W - (GATE_PIN_LEN - 1) * 3) / 2)

/* > Type Definitions *****************************************************************************/
typedef enum titan_screen_e {
  SCR__PAIR = 0,
  SCR__SEARCH,
  SCR__NONE_FOUND,
  SCR__PICK,
  SCR__PIN,
  SCR__CHECK,
  SCR__NOT_FOUND,
  SCR__WRONG_PIN,
  SCR__BATTERY,
  SCR__CONNECTING,
  SCR__PLAY,
  SCR__SESSION,
  SCR__RADIO_ERROR,
  SCR_COUNT,
} titan_screen_t;

/* > Local Function Declarations ******************************************************************/
static void _init(void);
static void _deinit(void);
static bool _eventHandler(uint16_t event);
static void _draw(void);
static void _drawScreen(void);
static void _startRadio(void);
static void _setScreen(uint8_t screen);
static void _startPin(const char* serial, const char* letters, bool repair);
static void _drawPlay(uint32_t now);
static void _drawSession(uint32_t now);
static void _drawAlert(uint8_t alert, uint32_t now);
static void _drawCentered(int16_t y, const char* text, const gfx_font_t* font);

/* > Local Variables ******************************************************************************/
view_t g_titanView = {
  .init = _init,
  .deinit = _deinit,
  .eventHandler = _eventHandler,
  .draw = _draw,
};

static const char* const kScreenNames[SCR_COUNT] = {
  "PAIR", "SEARCH", "NONE_FOUND", "PICK", "PIN", "CHECK", "NOT_FOUND", "WRONG_PIN", "BATTERY",
  "CONNECTING", "PLAY", "SESSION", "RADIO_ERROR",
};

static bool           s_active = false;      // Between init and deinit
static bool           s_radioOn = false;
static uint8_t        s_screen = SCR__PAIR;
static uint32_t       s_screenAt = 0;
static hold_buttons_t s_hold;

static uint8_t        s_pick = 0;            // PICK: the selected ETU
static char           s_serial[GATE_SERIAL_LEN + 1];   // The ETU of the PIN wheel / CHECK
static char           s_pin[GATE_PIN_LEN + 1];         // The letters of the wheel
static uint8_t        s_pinPos = 0;
static bool           s_repair = false;      // PIN: a new PIN for the paired ETU (no battery question)
static uint8_t        s_battery = TITAN_BATT__2S_LIPO;
static uint8_t        s_lastAlert = 0;
static bool           s_reloaded = false;    // The "FULL" sign after a reload ...
static uint32_t       s_reloadAt = 0;        // ... since this time

/* > Function Definitions *************************************************************************/

void TitanView_update(uint32_t now) {
  if(!s_active || !s_radioOn)
    return;

  Titan_update(now);
  const uint8_t link = Titan_getLink();

  switch(s_screen) {
    case SCR__SEARCH:
      if(!Titan_isScanning())
        _setScreen(Titan_getFoundCount() > 0 ? SCR__PICK : SCR__NONE_FOUND);
      break;

    case SCR__CHECK:
      if(link == TITAN_LINK__READY) {
        Titan_setPaired(s_serial, s_pin);
        App_click(1500);
        if(s_repair) {
          _setScreen(SCR__PLAY);
        } else {
          s_battery = Titan_getBattery();
          _setScreen(SCR__BATTERY);
        }
      } else if(link == TITAN_LINK__WRONG_PIN) {
        App_tone(400, 300);
        _setScreen(SCR__WRONG_PIN);
      } else if((now - s_screenAt) >= CHECK_LIMIT_MS) {
        Titan_linkStop();
        _setScreen(SCR__NOT_FOUND);
      }
      break;

    case SCR__WRONG_PIN:
      if((now - s_screenAt) >= WRONG_PIN_MS)
        _startPin(s_serial, s_pin, s_repair);
      break;

    case SCR__CONNECTING:
    case SCR__PLAY:
    case SCR__SESSION:
      if(link == TITAN_LINK__WRONG_PIN) {
        // The PIN of the paired ETU is not correct any more (changed in GCS)
        memcpy(s_serial, Titan_getSerial(), sizeof(s_serial));
        memcpy(s_pin, Titan_getPin(), sizeof(s_pin));
        s_repair = true;
        App_tone(400, 300);
        _setScreen(SCR__WRONG_PIN);
      } else if(s_screen == SCR__CONNECTING && link == TITAN_LINK__READY) {
        App_click(1500);
        _setScreen(SCR__PLAY);
      }
      break;

    default: break;
  }

  // A new alert beeps once
  const uint8_t alert = (s_screen == SCR__PLAY || s_screen == SCR__SESSION) ? Titan_getAlert() : 0;
  if(alert != 0 && alert != s_lastAlert)
    App_tone(2400, 150);
  s_lastAlert = alert;
}

void TitanView_onShutdown(void) {
  if(s_active)
    Titan_close(Time_getMillis());   // The count and the session time go to flash
}

uint8_t TitanView_getScreen(void) {
  return s_screen;
}

const char* TitanView_getScreenName(void) {
  return (s_screen < SCR_COUNT) ? kScreenNames[s_screen] : "?";
}

const char* TitanView_getPinEntry(void) {
  return s_pin;
}

uint8_t TitanView_getPinPos(void) {
  return s_pinPos;
}

/* > Local Function Definitions *******************************************************************/

static void _init(void) {
  const uint32_t now = Time_getMillis();
  Titan_load();
  Titan_open(now);
  s_active = true;
  s_radioOn = false;
  s_lastAlert = 0;
  s_reloaded = false;
  s_repair = false;
  _setScreen(Titan_isPaired() ? SCR__CONNECTING : SCR__PAIR);
  BtStart_begin(VIEW_ID__TITAN, _startRadio);   // The radio starts a moment later (soft start)
}

static void _deinit(void) {
  BtStart_cancel(VIEW_ID__TITAN);
  Titan_close(Time_getMillis());
  GateBle_stop();
  s_radioOn = false;
  s_active = false;
}

static void _startRadio(void) {
  s_radioOn = GateBle_start();
  if(!s_radioOn) {
    _setScreen(SCR__RADIO_ERROR);
    return;
  }
  if(Titan_isPaired())
    Titan_linkStart(Titan_getSerial(), Titan_getPin(), Time_getMillis());
}

static void _setScreen(uint8_t screen) {
  s_screen = screen;
  s_screenAt = Time_getMillis();
  HoldButtons_reset(&s_hold);
}

static void _startPin(const char* serial, const char* letters, bool repair) {
  memcpy(s_serial, serial, sizeof(s_serial));
  if(letters != NULL && strlen(letters) == GATE_PIN_LEN)
    memcpy(s_pin, letters, sizeof(s_pin));
  else
    memcpy(s_pin, "AAAAAA", sizeof(s_pin));
  s_pinPos = 0;
  s_repair = repair;
  Titan_linkStop();
  _setScreen(SCR__PIN);
}

// The next or the previous letter of TITAN_PIN_CHARS. A letter that is not in the list (a PIN
// from flash) goes to the first one.
static void _stepLetter(int8_t step) {
  static const char kChars[] = TITAN_PIN_CHARS;
  const int8_t count = (int8_t)(sizeof(kChars) - 1);
  const char* at = strchr(kChars, s_pin[s_pinPos]);
  int8_t index = (at != NULL && *at != '\0') ? (int8_t)(at - kChars) : (int8_t)(step > 0 ? -1 : 0);
  index = (int8_t)((index + step + count) % count);
  s_pin[s_pinPos] = kChars[index];
  App_click(1000);
}

static bool _eventHandler(uint16_t event) {
  const uint32_t now = Time_getMillis();
  HoldButtons_event(&s_hold, event);

  // An alert: any button closes it, and does nothing else
  if((s_screen == SCR__PLAY || s_screen == SCR__SESSION) && Titan_getAlert() != 0) {
    if(event == EVENT__PRESS_1 || event == EVENT__PRESS_2 || event == EVENT__LONG_PRESS_1 ||
       event == EVENT__LONG_PRESS_2) {
      Titan_dismissAlert();
      HoldButtons_reset(&s_hold);
      App_click(1000);
      return true;
    }
    return false;
  }

  const bool hold1 = (event == EVENT__LONG_PRESS_1) && HoldButtons_isHeld(&s_hold, 0);
  const bool hold2 = (event == EVENT__LONG_PRESS_2) && HoldButtons_isHeld(&s_hold, 1);

  switch(s_screen) {
    case SCR__PAIR:
    case SCR__NONE_FOUND:
      if(hold2 && s_radioOn && Titan_scanStart(now)) {
        App_click(1200);
        _setScreen(SCR__SEARCH);
        return true;
      }
      if(hold1 && s_screen == SCR__NONE_FOUND) {
        _setScreen(SCR__PAIR);
        return true;
      }
      break;

    case SCR__SEARCH:
      if(hold1) {
        Titan_scanStop();
        _setScreen(SCR__PAIR);
        return true;
      }
      break;

    case SCR__PICK:
      if(event == EVENT__PRESS_1 && Titan_getFoundCount() > 0) {
        s_pick = (uint8_t)((s_pick + 1) % Titan_getFoundCount());
        App_click(1000);
        return true;
      }
      if(hold2 && s_pick < Titan_getFoundCount()) {
        App_click(1200);
        _startPin(Titan_getFound(s_pick)->serial, NULL, false);
        return true;
      }
      break;

    case SCR__PIN:
      if(event == EVENT__PRESS_1) {
        _stepLetter(+1);
        return true;
      }
      if(event == EVENT__LONG_PRESS_1 || event == EVENT__LONG_HOLD_1) {
        _stepLetter(-1);
        return true;
      }
      if(hold2) {
        App_click(1200);
        if(++s_pinPos >= GATE_PIN_LEN) {
          s_pinPos = GATE_PIN_LEN - 1;
          Titan_linkStart(s_serial, s_pin, now);
          _setScreen(SCR__CHECK);
        } else {
          HoldButtons_reset(&s_hold);
        }
        return true;
      }
      break;

    case SCR__CHECK:
      if(hold1) {
        _startPin(s_serial, s_pin, s_repair);
        return true;
      }
      break;

    case SCR__NOT_FOUND:
      if(hold2) {
        Titan_linkStart(s_serial, s_pin, now);
        _setScreen(SCR__CHECK);
        return true;
      }
      if(hold1) {
        _setScreen(SCR__PAIR);
        return true;
      }
      break;

    case SCR__BATTERY:
      if(event == EVENT__PRESS_1) {
        s_battery = (uint8_t)((s_battery + 1) % TITAN_BATT_COUNT);
        App_click(1000);
        return true;
      }
      if(hold2) {
        Titan_setBattery(s_battery);
        App_click(1500);
        _setScreen(SCR__PLAY);
        return true;
      }
      break;

    case SCR__PLAY:
      if(event == EVENT__PRESS_1) {
        if(Titan_getCapacity() > 0) {
          Titan_reload();
          s_reloaded = true;
          s_reloadAt = now;
          App_click(1500);
        }
        return true;
      }
      if(event == EVENT__PRESS_2) {
        App_click(1000);
        _setScreen(SCR__SESSION);
        return true;
      }
      break;

    case SCR__SESSION:
      if(event == EVENT__PRESS_1 || event == EVENT__PRESS_2) {
        App_click(1000);
        _setScreen(SCR__PLAY);
        return true;
      }
      if(hold2) {
        Titan_newSession(now);
        App_click(1500);
        HoldButtons_reset(&s_hold);
        return true;
      }
      break;

    default: break;
  }

  // Long presses and their repeats stay in this view
  return event == EVENT__LONG_PRESS_1 || event == EVENT__LONG_PRESS_2 ||
         event == EVENT__LONG_HOLD_1 || event == EVENT__LONG_HOLD_2;
}

static void _draw(void) {
  if(!BtStart_draw(VIEW_ID__TITAN, _drawScreen))
    _drawScreen();
}

static void _drawCentered(int16_t y, const char* text, const gfx_font_t* font) {
  Gfx_drawStringAligned(SCREEN_W / 2, y, text, font, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
}

// Dots that move: "SEARCHING..."
static const char* _dots(uint32_t now) {
  static const char* const kDots[4] = { "", ".", "..", "..." };
  return kDots[(now / 400) % 4];
}

static void _drawScreen(void) {
  const uint32_t now = Time_getMillis();
  char str[32];

  switch(s_screen) {
    case SCR__PAIR:
      _drawCentered(2, "TITAN", &kFontNightverbBig);
      _drawCentered(12, "NOT PAIRED", &font3x5);
      HoldButton_draw(HOLD_BUTTON_RIGHT_X(32), 32, "SEARCH", HoldButtons_fill(&s_hold, 1));
      break;

    case SCR__SEARCH:
      snprintf(str, sizeof(str), "SEARCHING%s", _dots(now));
      Gfx_drawString(30, 3, str, &font5x5, 1, 0, GFX_ROTATION__0);
      snprintf(str, sizeof(str), "FOUND: %u", (unsigned)Titan_getFoundCount());
      _drawCentered(12, str, &font3x5);
      HoldButton_draw(HOLD_BUTTON_MARGIN, 26, "BACK", HoldButtons_fill(&s_hold, 0));
      break;

    case SCR__NONE_FOUND:
      _drawCentered(2, "NO TITAN FOUND", &font5x5);
      _drawCentered(11, "SWITCH THE GUN ON", &font3x5);
      HoldButton_draw(HOLD_BUTTON_MARGIN, 26, "BACK", HoldButtons_fill(&s_hold, 0));
      HoldButton_draw(HOLD_BUTTON_RIGHT_X(28), 28, "AGAIN", HoldButtons_fill(&s_hold, 1));
      break;

    case SCR__PICK: {
      const titan_found_t* found = Titan_getFound(s_pick);
      snprintf(str, sizeof(str), "PICK YOUR TITAN %u OF %u", (unsigned)(s_pick + 1), (unsigned)Titan_getFoundCount());
      _drawCentered(1, str, &font3x5);
      if(found != NULL) {
        _drawCentered(9, found->serial, &kFontNightverbBig);
        snprintf(str, sizeof(str), "%d DBM", (int)found->rssi);
        Gfx_drawString(HOLD_BUTTON_MARGIN, 24, Titan_getFoundCount() > 1 ? "1: NEXT" : str, &font3x5, 1, 0, GFX_ROTATION__0);
      }
      HoldButton_draw(HOLD_BUTTON_RIGHT_X(HOLD_BUTTON_W), HOLD_BUTTON_W, "PICK", HoldButtons_fill(&s_hold, 1));
    } break;

    case SCR__PIN: {
      snprintf(str, sizeof(str), "PIN FOR %s", s_serial);
      Gfx_drawString(2, 1, str, &font3x5, 1, 0, GFX_ROTATION__0);
      snprintf(str, sizeof(str), "%u OF %u", (unsigned)(s_pinPos + 1), (unsigned)GATE_PIN_LEN);
      Gfx_drawStringAligned(SCREEN_W - 2, 1, str, &font3x5, 1, 0, GFX_ALIGN__RIGHT, GFX_ROTATION__0);
      for(uint8_t i = 0; i < GATE_PIN_LEN; i++) {
        const int16_t x = PIN_BOX_X0 + i * (PIN_BOX_W + 3);
        const char letter[2] = { s_pin[i], '\0' };
        if(i == s_pinPos) {
          Gfx_fillRoundRect(x, 8, PIN_BOX_W, 12, 2, 1);
          Gfx_drawStringAligned(x + PIN_BOX_W / 2 + 1, 11, letter, &font5x7, 0, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
        } else {
          Gfx_drawStringAligned(x + PIN_BOX_W / 2 + 1, 11, letter, &font5x7, 1, 0,
                                GFX_ALIGN__CENTER, GFX_ROTATION__0);
          Gfx_drawHLine(x + 1, 19, PIN_BOX_W - 2, 1);
        }
      }
      Gfx_drawString(HOLD_BUTTON_MARGIN, 24, "1: NEXT  HOLD 1: BACK", &font3x5, 1, 0, GFX_ROTATION__0);
      HoldButton_draw(HOLD_BUTTON_RIGHT_X(18), 18, "OK", HoldButtons_fill(&s_hold, 1));
    } break;

    case SCR__CHECK:
      snprintf(str, sizeof(str), "CONNECTING%s", _dots(now));
      Gfx_drawString(30, 3, str, &font5x5, 1, 0, GFX_ROTATION__0);
      _drawCentered(12, s_serial, &font3x5);
      HoldButton_draw(HOLD_BUTTON_MARGIN, 26, "BACK", HoldButtons_fill(&s_hold, 0));
      break;

    case SCR__NOT_FOUND:
      snprintf(str, sizeof(str), "%s NOT FOUND", s_serial);
      _drawCentered(2, str, &font5x5);
      _drawCentered(11, "SWITCH THE GUN ON", &font3x5);
      HoldButton_draw(HOLD_BUTTON_MARGIN, 26, "BACK", HoldButtons_fill(&s_hold, 0));
      HoldButton_draw(HOLD_BUTTON_RIGHT_X(28), 28, "AGAIN", HoldButtons_fill(&s_hold, 1));
      break;

    case SCR__WRONG_PIN:
      _drawCentered(7, "WRONG PIN", &kFontNightverbBig);
      _drawCentered(19, "TRY AGAIN", &font3x5);
      break;

    case SCR__BATTERY:
      _drawCentered(1, "GUN BATTERY", &font3x5);
      snprintf(str, sizeof(str), "%s", Titan_batteryName(s_battery));
      _drawCentered(10, str, &kFontNightverbBig);
      Gfx_drawString(HOLD_BUTTON_MARGIN, 24, "1: NEXT", &font3x5, 1, 0, GFX_ROTATION__0);
      HoldButton_draw(HOLD_BUTTON_RIGHT_X(18), 18, "OK", HoldButtons_fill(&s_hold, 1));
      break;

    case SCR__CONNECTING:
      snprintf(str, sizeof(str), "TITAN %s", Titan_getSerial());
      _drawCentered(4, str, &kFontNightverbBig);
      snprintf(str, sizeof(str), "SEARCHING%s", _dots(now));
      Gfx_drawString(44, 17, str, &font3x5, 1, 0, GFX_ROTATION__0);
      break;

    case SCR__PLAY:
    case SCR__SESSION: {
      const uint8_t alert = Titan_getAlert();
      if(alert != 0)
        _drawAlert(alert, now);
      else if(s_screen == SCR__PLAY)
        _drawPlay(now);
      else
        _drawSession(now);
    } break;

    case SCR__RADIO_ERROR:
      _drawCentered(7, "BLUETOOTH ERROR", &font5x5);
      _drawCentered(17, "OPEN TITAN AGAIN", &font3x5);
      break;

    default: break;
  }
}

static const char* _selectorText(uint8_t selector) {
  switch(selector) {
    case GATE_SELECTOR__SAFE: return "SAFE";
    case GATE_SELECTOR__SEMI: return "SEMI";
    case GATE_SELECTOR__AUTO: return "AUTO";
    default:                  return "----";
  }
}

static void _drawPlay(uint32_t now) {
  char str[16];
  const bool blink = ((now / BLINK_MS) % 2) == 0;
  const int32_t rounds = Titan_getRounds();
  const uint16_t capacity = Titan_getCapacity();
  gate_info_t info;
  const bool haveInfo = Titan_getInfo(&info);
  const bool ready = (Titan_getLink() == TITAN_LINK__READY);

  // The big number: rounds left (MAG SIZE set) or shots (OFF). Smaller digits for long numbers.
  snprintf(str, sizeof(str), "%ld", (long)rounds);
  const size_t digits = strlen(str);
  const uint8_t scale = (digits <= 3) ? 4 : (digits <= 4) ? 3 : 2;
  const int16_t y = (scale == 4) ? 2 : (scale == 3) ? 5 : 9;
  Gfx_drawStringScaledAligned(76, y, str, &font5x7, 1, scale, 1, GFX_ALIGN__RIGHT);
  if(Titan_isUncertain())
    Gfx_drawString(0, 1, "~", &font5x7, 1, 0, GFX_ROTATION__0);

  // The column on the right
  const int16_t cx = 82;
  if(capacity > 0 && rounds == 0) {
    if(blink) {
      Gfx_fillRect(cx - 2, 0, SCREEN_W - cx + 2, 7, 1);
      Gfx_drawString(cx, 1, "EMPTY", &font5x5, 0, 0, GFX_ROTATION__0);
    }
  } else if(s_reloaded && (now - s_reloadAt) < 800) {
    Gfx_drawString(cx, 1, "FULL", &font5x5, 1, 0, GFX_ROTATION__0);
  } else if(capacity > 0) {
    snprintf(str, sizeof(str), "/%u", (unsigned)capacity);
    Gfx_drawString(cx, 1, str, &font5x5, 1, 0, GFX_ROTATION__0);
  } else {
    Gfx_drawString(cx, 1, "SHOTS", &font5x5, 1, 0, GFX_ROTATION__0);
  }

  if(ready && haveInfo) {
    Gfx_drawString(cx, 9, _selectorText(info.selector), &font5x5, 1, 0, GFX_ROTATION__0);
    if(info.voltageMv != GATE_VOLTAGE_UNKNOWN)
      snprintf(str, sizeof(str), "%u.%uV", (unsigned)(info.voltageMv / 1000), (unsigned)((info.voltageMv % 1000) / 100));
    else
      snprintf(str, sizeof(str), "--V");
    Gfx_drawString(cx, 17, str, &font5x5, 1, 0, GFX_ROTATION__0);

    // Signs of problems that have no full-screen alert (or a closed one)
    const uint8_t active = Titan_getActiveAlerts();
    const char* sign = NULL;
    if(active & TITAN_ALERT__FIRE_LOCKED)                                    sign = "LOCKED";
    else if(active & TITAN_ALERT__LOW_BATT)                                  sign = "LOW BATT";
    else if((info.flags & GATE_FLAG__MAG_SENSOR) && (info.flags & GATE_FLAG__NO_MAG)) sign = "NO MAG";
    else if(active & TITAN_ALERT__LOW_AMMO)                                  sign = "LOW AMMO";
    if(sign != NULL && blink)
      Gfx_drawString(cx, 25, sign, &font3x5, 1, 0, GFX_ROTATION__0);
  } else {
    Gfx_drawString(cx, 9, "----", &font5x5, 1, 0, GFX_ROTATION__0);
    snprintf(str, sizeof(str), "LINK%s", _dots(now));
    Gfx_drawString(cx, 25, str, &font3x5, 1, 0, GFX_ROTATION__0);
  }
}

static void _drawSession(uint32_t now) {
  char str[24];
  const uint32_t seconds = Titan_getSessionMs(now) / 1000u;

  snprintf(str, sizeof(str), "SHOTS: %lu", (unsigned long)Titan_getSessionShots());
  Gfx_drawString(2, 1, str, &font5x5, 1, 0, GFX_ROTATION__0);
  snprintf(str, sizeof(str), "RELOADS: %u", (unsigned)Titan_getReloads());
  Gfx_drawString(2, 9, str, &font5x5, 1, 0, GFX_ROTATION__0);
  if(seconds >= 3600)
    snprintf(str, sizeof(str), "TIME: %lu:%02lu:%02lu", (unsigned long)(seconds / 3600),
             (unsigned long)((seconds / 60) % 60), (unsigned long)(seconds % 60));
  else
    snprintf(str, sizeof(str), "TIME: %lu:%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
  Gfx_drawString(2, 17, str, &font5x5, 1, 0, GFX_ROTATION__0);
  Gfx_drawString(2, 25, "2: BACK", &font3x5, 1, 0, GFX_ROTATION__0);
  HoldButton_draw(HOLD_BUTTON_RIGHT_X(HOLD_BUTTON_W), HOLD_BUTTON_W, "NEW", HoldButtons_fill(&s_hold, 1));
}

static void _drawAlert(uint8_t alert, uint32_t now) {
  const char* text = "";
  switch(alert) {
    case TITAN_ALERT__LINK_LOST:   text = "LINK LOST"; break;
    case TITAN_ALERT__LOW_BATT:    text = "LOW BATT"; break;
    case TITAN_ALERT__FIRE_LOCKED: text = "FIRE LOCKED"; break;
    case TITAN_ALERT__MAG_EMPTY:   text = "MAG EMPTY"; break;
    case TITAN_ALERT__LOW_AMMO:    text = "LOW AMMO"; break;
    default: break;
  }

  // A frame that blinks around the text; the text stays (easy to read)
  if(((now / BLINK_MS) % 2) == 0)
    Gfx_fillRoundRect(0, 0, SCREEN_W, 32, 3, 1);
  else
    Gfx_drawRoundRect(0, 0, SCREEN_W, 32, 3, 1);
  Gfx_fillRoundRect(6, 4, SCREEN_W - 12, 24, 2, 0);
  _drawCentered(8, text, &kFontNightverbBig);

  char str[24];
  if(alert == TITAN_ALERT__LOW_AMMO) {
    snprintf(str, sizeof(str), "%ld LEFT", (long)Titan_getRounds());
    _drawCentered(19, str, &font3x5);
  } else if(alert == TITAN_ALERT__LOW_BATT) {
    gate_info_t info;
    if(Titan_getInfo(&info) && info.voltageMv != GATE_VOLTAGE_UNKNOWN) {
      snprintf(str, sizeof(str), "%u.%02uV", (unsigned)(info.voltageMv / 1000), (unsigned)((info.voltageMv % 1000) / 10));
      _drawCentered(19, str, &font3x5);
    }
  } else {
    _drawCentered(19, "PRESS A BUTTON", &font3x5);
  }
}
