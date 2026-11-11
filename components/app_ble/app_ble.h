#ifndef _APP_BLE_H
#define _APP_BLE_H

#include "host/ble_hs.h"


typedef struct {
  struct {
    void (*connectCb)(void);
    void (*disconnectCb)(void);
    void (*subscribeCb)(struct ble_gap_event *event);
  } gap;

  int (*init)(void);
  void (*deinit)(void);

  const struct ble_gatt_svc_def *gattService;
} ble_app_service_t;


void AppBle_init(ble_app_service_t *service);
void AppBle_deinit(void);


#endif // _APP_BLE_H