#include "view.h"
#include "battery.h"
#include "eeprom.h"
#include "app.h"

#include <stdio.h>

static bool _eventHandler(uint16_t event);
static void _draw(void);


view_t g_statsView = {
  .eventHandler = _eventHandler,
  .draw = _draw,
};


static bool _eventHandler(uint16_t event) {
  (void)event;

  return false;
}


static void _draw(void) {
  char str[32];

  const int16_t x = 2;

  GUI_DRAW_TITLE((x + 6*6 + 1), 1, 4, "KILL");
  GUI_DRAW_TITLE((x + 6*11 + 1), 1, 4, "DEAD");
  // GUI_DRAW_TITLE((1 + 6*16), 1, 4, "K/D");
  Gfx_fillRoundRect((x + 6*16 + 1) - 2, 0, 4 * font5x5.x_advance+3, 7, 2, 1);
  Gfx_drawStringAligned((x + 6*16 + 1) + 22/2, 1, "K/D", &font5x5, 0, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);

  // Totals
  uint32_t kills = Eeprom_read(ENTRY_ID__TOTAL_KILLS);
  uint32_t deaths = Eeprom_read(ENTRY_ID__TOTAL_DEATHS);
  float ratio = (deaths != 0) ? ((float)kills / (float)deaths) : ((float)kills);
  
  Gfx_drawString(x + 1, 1 + 8, "TOTAL:", &font5x5, 1, 0, GFX_ROTATION__0);
  int len = snprintf(str, sizeof(str), "%ld", kills);
  Gfx_drawStringAligned(x + 6*8, 1 + 8, str, &font3x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  len = snprintf(str, sizeof(str), "%ld", deaths);
  Gfx_drawStringAligned(x + 6*13, 1 + 8, str, &font3x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  if(ratio < 100.0f)
    snprintf(str, sizeof(str), "%.1f", ratio);
  else
    snprintf(str, sizeof(str), "%.0f", ratio);
  Gfx_drawStringAligned(x + 6*18, 1 + 8, str, &font3x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);

  // Game
  kills = Eeprom_read(ENTRY_ID__GAME_KILLS);
  deaths = Eeprom_read(ENTRY_ID__GAME_DEATHS);
  ratio = (deaths != 0) ? ((float)kills / (float)deaths) : ((float)kills);
  
  Gfx_drawString(x + 1, 1 + 16, " GAME:", &font5x5, 1, 0, GFX_ROTATION__0);
  len = snprintf(str, sizeof(str), "%ld", kills);
  Gfx_drawStringAligned(x + 6*8, 1 + 16, str, &font3x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  len = snprintf(str, sizeof(str), "%ld", deaths);
  Gfx_drawStringAligned(x + 6*13, 1 + 16, str, &font3x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  if(ratio < 100.0f)
    snprintf(str, sizeof(str), "%.1f", ratio);
  else
    snprintf(str, sizeof(str), "%.0f", ratio);
  Gfx_drawStringAligned(x + 6*18, 1 + 16, str, &font3x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  
  // Seperator
  Gfx_drawLineDashed(2, 24, 126, 24, 1, 2);

  // Battery
  int16_t volts = Battery_readVoltage() / 1000;
  int16_t frac = (Battery_readVoltage() - volts * 1000) / 10;

  Gfx_drawStringAligned(x + 30, 27, "Battery", &font3x5, 1, 0, GFX_ALIGN__RIGHT, GFX_ROTATION__0);
  Gfx_drawString(x + 32, 27, ":", &font5x5, 1, 0, GFX_ROTATION__0);

  if(Battery_getType() > BATTERY_TYPE__USB) {
    snprintf(str, sizeof(str), "%d%% (%d.%02dV)", Battery_readLevel(), volts, frac);
    Gfx_drawString(45, 27, str, &font3x5, 1, 0, GFX_ROTATION__0);
  } else {
    snprintf(str, sizeof(str), "%s (%d.%02dV)", (Battery_getType() == BATTERY_TYPE__USB) ? "USB" : "NONE", volts, frac);
    Gfx_drawString(45, 27, str, &font3x5, 1, 0, GFX_ROTATION__0);
  }

}