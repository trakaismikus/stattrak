#ifndef _OTA_H
#define _OTA_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/* > Enumerations **********************************************************************************/
enum ota_stete_e {
  OTA_STATE__IDLE = 0,
  OTA_STATE__STARTED,
  OTA_STATE__WRITING,
  OTA_STATE__FINISHED,
  OTA_STATE__ACCEPTED,
};


#ifdef __cplusplus
extern "C" {
#endif

/* > Function Declarations *************************************************************************/

int Ota_init(void);
void Ota_deinit(void);
int Ota_start(void);
int Ota_write(const uint8_t* data, uint16_t len);
void Ota_end(void);

bool Ota_isPendingVerify(void);
bool Ota_acceptPending(bool accept);

uint8_t Ota_getState(void);
uint8_t Ota_getProgress(void);

#ifdef __cplusplus
}
#endif

#endif // _OTA_H