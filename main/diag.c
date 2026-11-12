/***************************************************************************************************
 * @file    : diag.c
 * @brief   : TEST FIRMWARE ONLY. See diag.h.
 **************************************************************************************************/

#include "diag.h"

#include <stdio.h>
#include <string.h>

#include "nvs_flash.h"
#include "time.h"

#ifdef ESP_PLATFORM
#include "esp_system.h"
#include "esp_heap_caps.h"
#endif

typedef struct {
  uint16_t okCount;
  uint16_t failCount;
  uint32_t okFree, okBig, okAfterFree, okAfterBig;   // The last good start: before and after
  uint32_t failFree, failBig;                         // The last start that did not end
  uint32_t pendFree, pendBig;                         // The start that runs now
  uint8_t  pending;
} diag_t;

char g_diagNow[16] = "-";
char g_diagOk[16] = "-";
char g_diagFail[16] = "-";

static diag_t s_diag;
static uint32_t s_startMs = 0;

static void _heap(uint32_t* freeBytes, uint32_t* biggest, uint32_t* minimum) {
#ifdef ESP_PLATFORM
  *freeBytes = esp_get_free_heap_size();
  *biggest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  *minimum = esp_get_minimum_free_heap_size();
#else
  *freeBytes = *biggest = *minimum = 0;
#endif
}

static void _write(void) {
  nvs_handle_t h;
  if(nvs_open("diag", NVS_READWRITE, &h) != ESP_OK)
    return;
  nvs_set_blob(h, "bt", &s_diag, sizeof(s_diag));
  nvs_commit(h);
  nvs_close(h);
}

void Diag_init(void) {
  memset(&s_diag, 0, sizeof(s_diag));
  nvs_handle_t h;
  if(nvs_open("diag", NVS_READWRITE, &h) == ESP_OK) {
    size_t size = sizeof(s_diag);
    if(nvs_get_blob(h, "bt", &s_diag, &size) != ESP_OK || size != sizeof(s_diag))
      memset(&s_diag, 0, sizeof(s_diag));
    nvs_close(h);
  }
  if(s_diag.pending) {
    // The last Bluetooth start did not reach 3 s: it turned the device off
    s_diag.failCount++;
    s_diag.failFree = s_diag.pendFree;
    s_diag.failBig = s_diag.pendBig;
    s_diag.pending = 0;
    _write();
  }
  Diag_refresh();
}

void Diag_btStart(void) {
  uint32_t minimum;
  _heap(&s_diag.pendFree, &s_diag.pendBig, &minimum);
  s_diag.pending = 1;
  s_startMs = Time_getMillis();
  _write();      // Before the start: a crash in the start keeps this mark
}

void Diag_update(uint32_t now) {
  if(!s_diag.pending || (now - s_startMs) < 3000)
    return;
  uint32_t minimum;
  s_diag.okCount++;
  s_diag.okFree = s_diag.pendFree;
  s_diag.okBig = s_diag.pendBig;
  _heap(&s_diag.okAfterFree, &s_diag.okAfterBig, &minimum);
  s_diag.pending = 0;
  _write();
  Diag_refresh();
}

// Copies at most 15 characters (the settings value field)
static void _set(char* out, const char* text) {
  size_t n = strlen(text);
  if(n > 15)
    n = 15;
  memcpy(out, text, n);
  out[n] = 0;
}

void Diag_refresh(void) {
  char text[64];
  uint32_t freeBytes, biggest, minimum;
  _heap(&freeBytes, &biggest, &minimum);
  // KB: free / largest block / lowest free since power-on
  snprintf(text, sizeof(text), "%lu/%lu/%lu", (unsigned long)(freeBytes / 1024),
           (unsigned long)(biggest / 1024), (unsigned long)(minimum / 1024));
  _set(g_diagNow, text);
  // count x before free/largest > after free
  snprintf(text, sizeof(text), "%ux%lu/%lu>%lu", s_diag.okCount, (unsigned long)(s_diag.okFree / 1024),
           (unsigned long)(s_diag.okBig / 1024), (unsigned long)(s_diag.okAfterFree / 1024));
  _set(g_diagOk, text);
  snprintf(text, sizeof(text), "%ux%lu/%lu", s_diag.failCount, (unsigned long)(s_diag.failFree / 1024),
           (unsigned long)(s_diag.failBig / 1024));
  _set(g_diagFail, text);
}
