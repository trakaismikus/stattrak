/*
 * A small stand-in for the Apache NimBLE host headers. It has only the types and functions
 * that components/ble_ota/ble_ota.c uses, so the packet code can run in a test on a computer.
 */
#ifndef _TEST_NIMBLE_BLE_HS_H
#define _TEST_NIMBLE_BLE_HS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <inttypes.h>

/* --- Error codes */
#define ESP_OK                        (0)
#define ESP_FAIL                      (-1)

#define BLE_ATT_ERR_UNLIKELY          (0x0e)
#define BLE_ATT_ERR_INSUFFICIENT_RES  (0x11)

#define BLE_HS_CONN_HANDLE_NONE       (0xffff)

/* --- Log: the level is one of INFO, ERROR, DEBUG. The text is checked by the compiler only. */
#define MODLOG_DFLT(_level, ...)      do { if(0) { printf(__VA_ARGS__); } } while(0)

/* --- Buffers. NimBLE gives the data of one write as a chain of buffers. */
struct os_mbuf {
  uint8_t*        om_data;
  uint16_t        om_len;
  struct os_mbuf* om_next;
};

#define SLIST_NEXT(_elem, _field)     ((_elem)->_field)

/* Total length of a buffer chain, and a copy of a part of it into one flat buffer */
uint16_t        test_mbuf_pktlen(const struct os_mbuf* om);
#define OS_MBUF_PKTLEN(_om)           (test_mbuf_pktlen(_om))
int             os_mbuf_copydata(const struct os_mbuf* om, int off, int len, void* dst);

int             os_mbuf_append(struct os_mbuf* om, const void* data, uint16_t len);
struct os_mbuf* ble_hs_mbuf_from_flat(const void* buf, uint16_t len);

/* --- UUID */
typedef struct {
  uint8_t type;
} ble_uuid_t;

typedef struct {
  ble_uuid_t u;
  uint16_t   value;
} ble_uuid16_t;

#define BLE_UUID_TYPE_16              (16)
#define BLE_UUID16_INIT(_uuid16)      { .u = { .type = BLE_UUID_TYPE_16 }, .value = (_uuid16) }
#define BLE_UUID16_DECLARE(_uuid16)   ((ble_uuid_t*)(&(ble_uuid16_t)BLE_UUID16_INIT(_uuid16)))

/* --- GATT */
#define BLE_GATT_SVC_TYPE_PRIMARY     (1)

#define BLE_GATT_CHR_F_READ           (0x0002)
#define BLE_GATT_CHR_F_WRITE          (0x0008)
#define BLE_GATT_CHR_F_INDICATE       (0x0020)

#define BLE_GATT_ACCESS_OP_READ_CHR   (0)
#define BLE_GATT_ACCESS_OP_WRITE_CHR  (1)

struct ble_gatt_access_ctxt;

typedef int ble_gatt_access_fn(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt* ctxt, void* arg);

struct ble_gatt_chr_def {
  const ble_uuid_t*   uuid;
  ble_gatt_access_fn* access_cb;
  void*               arg;
  uint16_t            flags;
  uint16_t*           val_handle;
};

struct ble_gatt_svc_def {
  uint8_t                         type;
  const ble_uuid_t*               uuid;
  const struct ble_gatt_chr_def*  characteristics;
};

struct ble_gatt_access_ctxt {
  uint8_t                         op;
  struct os_mbuf*                 om;
  const struct ble_gatt_chr_def*  chr;
};

int ble_gattc_notify_custom(uint16_t conn_handle, uint16_t attr_handle, struct os_mbuf* om);

/* --- GAP */
struct ble_gap_event {
  uint8_t type;
  struct {
    uint16_t conn_handle;
    uint16_t attr_handle;
    uint8_t  cur_notify;
    uint8_t  cur_indicate;
  } subscribe;
};

#endif // _TEST_NIMBLE_BLE_HS_H
