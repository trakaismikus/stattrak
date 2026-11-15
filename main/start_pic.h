/***************************************************************************************************
 * @file    : start_pic.h
 * @brief   : The start picture that the person sets on the sync page (docs/DATA.md section 4).
 *            128 x 32 pixels in the frame buffer format (512 bytes). No picture = the device starts
 *            with the first app.
 **************************************************************************************************/

#ifndef _START_PIC_H
#define _START_PIC_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/* > Defines **************************************************************************************/
#define START_PIC_W       128
#define START_PIC_H       32
#define START_PIC_SIZE    (START_PIC_W * START_PIC_H / 8)   // 512
#define START_PIC_SHOW_MS 2000

// The state of CONFIG_INFO and START_PIC. Values are stable: the web page uses them.
typedef enum start_pic_state_e {
  START_PIC_STATE__NONE = 0,
  START_PIC_STATE__ON,      // Shown at power-on
  START_PIC_STATE__OFF,     // Kept, not shown
} start_pic_state_t;

/* > Function Declarations ************************************************************************/

void              StartPic_init(void);          // App_init: reads the picture from flash (checked)
start_pic_state_t StartPic_getState(void);
const uint8_t*    StartPic_get(void);           // NULL = no picture
bool              StartPic_save(const uint8_t* pixels);   // Also "show" = on
bool              StartPic_setShown(bool shown);          // false = no picture
void              StartPic_delete(void);

#endif // _START_PIC_H
