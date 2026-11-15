/***************************************************************************************************
 * @file    : start_pic.c
 * @brief   : The start picture. See start_pic.h and docs/DATA.md section 4.
 *
 * Flash ("data" namespace): "spic" = 512 bytes, "spon" = u8 1 shown / 0 kept but not shown. A
 * missing or short "spic" = no picture; a missing or wrong "spon" = shown.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "start_pic.h"

#include <string.h>

#include "data_store.h"

/* > Local Variables ******************************************************************************/
static uint8_t s_pixels[START_PIC_SIZE];
static bool    s_has = false;
static bool    s_shown = false;

/* > Function Definitions *************************************************************************/

void StartPic_init(void) {
  size_t length = START_PIC_SIZE;
  s_has = DataStore_read("spic", s_pixels, &length);
  uint8_t on = 1;
  length = 1;
  if(!DataStore_read("spon", &on, &length) || on > 1)
    on = 1;
  s_shown = (on == 1);
}

start_pic_state_t StartPic_getState(void) {
  if(!s_has)
    return START_PIC_STATE__NONE;
  return s_shown ? START_PIC_STATE__ON : START_PIC_STATE__OFF;
}

const uint8_t* StartPic_get(void) {
  return s_has ? s_pixels : NULL;
}

bool StartPic_save(const uint8_t* pixels) {
  if(pixels == NULL)
    return false;
  const uint8_t on = 1;
  if(!DataStore_write("spic", pixels, START_PIC_SIZE))
    return false;
  memcpy(s_pixels, pixels, START_PIC_SIZE);
  s_has = true;
  s_shown = true;
  DataStore_write("spon", &on, 1);
  return true;
}

bool StartPic_setShown(bool shown) {
  if(!s_has)
    return false;
  const uint8_t on = shown ? 1 : 0;
  if(!DataStore_write("spon", &on, 1))
    return false;
  s_shown = shown;
  return true;
}

void StartPic_delete(void) {
  DataStore_erase("spic");
  DataStore_erase("spon");
  s_has = false;
  s_shown = false;
}
