
/* > Includes *************************************************************************************/
#include "ota.h"

/* > Function Definitions *************************************************************************/

int Ota_init(void) { return 0; }
void Ota_deinit(void) { }
int Ota_start(void) { return 0; }
int Ota_write(const uint8_t* data, uint16_t len) { return len; }
void Ota_end(void) { }

bool Ota_isPendingVerify(void) { return false; }
bool Ota_acceptPending(bool accept) { return false; }

uint8_t Ota_getState(void) { return 0; }
uint8_t Ota_getProgress(void) { return 0; }