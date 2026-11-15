#ifndef _APP_ICONS_H
#define _APP_ICONS_H

/* > Includes *************************************************************************************/
#include <stdint.h>

/* > Types ****************************************************************************************/
typedef struct app_icon_s {
  uint8_t width;
  uint8_t height;
  const uint8_t* data;  // Gfx_drawBitmapRaw() format
} app_icon_t;

/* > External variables ***************************************************************************/
extern const app_icon_t kIconCrosshair;  // 19 x 19
extern const app_icon_t kIconCrosshairSmall;  // 15 x 15
extern const app_icon_t kIconSkull;  // 15 x 13
extern const app_icon_t kIconBars;  // 17 x 13
extern const app_icon_t kIconGear;  // 13 x 13
extern const app_icon_t kIconLightning;  // 11 x 17 (TITAN)

#endif // _APP_ICONS_H
