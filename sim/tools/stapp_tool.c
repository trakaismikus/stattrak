/***************************************************************************************************
 * @file    : stapp_tool.c
 * @brief   : The package check of the app engine (components/stapp) as a command line program.
 *
 *   stapp_tool check     Reads file paths from standard input, one per line. Prints the result
 *                        of Stapp_check() for each one ("ok", "bad crc", ...), one per line.
 *
 * tests/test_app_match.py uses it to show that tools/app.py and the firmware give the same
 * result for the same package.
 **************************************************************************************************/

#include <stdio.h>
#include <string.h>

#include "stapp.h"

int main(int argc, char** argv) {
  static uint8_t data[STAPP_MAX_SIZE + 64];
  char path[1024];

  if(argc != 2 || strcmp(argv[1], "check") != 0) {
    fprintf(stderr, "usage: stapp_tool check   (file paths on standard input)\n");
    return 2;
  }

  while(fgets(path, sizeof(path), stdin)) {
    path[strcspn(path, "\r\n")] = 0;
    if(path[0] == 0)
      continue;
    FILE* file = fopen(path, "rb");
    if(file == NULL) {
      printf("missing\n");
      continue;
    }
    const size_t size = fread(data, 1, sizeof(data), file);
    fclose(file);
    printf("%s\n", Stapp_resultText(Stapp_check(data, (uint32_t)size, NULL)));
  }
  return 0;
}
