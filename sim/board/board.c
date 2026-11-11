#include "board.h"

#ifndef PROJECT_VER
  #define PROJECT_VER "v1.0.0"
#endif

void Board_powerOn(void) {
}

void Board_powerOff(void) {
  printf("Board powered off\n");
}

uint16_t Board_readVoltage(void) {
  return 0;
}

void Board_getUID(uint8_t* uid) {
  for(int i=0; i<6; i++) {
    uid[i] = i;
  }
}

const char* Board_getFwVersion(void) {
  return PROJECT_VER;
}