#include <stdio.h>

#include "app_ble.h"

#include "esp_log.h"
#include "esp_peripheral.h"

/* BLE */
#include "esp_bt.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"



#define CONFIG_EXAMPLE_IO_TYPE 3
// #define CONFIG_EXAMPLE_RANDOM_ADDR 1

#define GAP_NAME   "StatTrak"


static void _onReset(int reason);
static void _onSync(void);
static void _hostTask(void *param);
static void ble_app_set_addr(void);
static void bleprph_advertise(void);
static int bleprph_gap_event(struct ble_gap_event *event, void *arg);

void ble_store_config_init(void);


static const char *tag = "BLE";

#if CONFIG_EXAMPLE_RANDOM_ADDR
static uint8_t own_addr_type = BLE_OWN_ADDR_RANDOM;
#else
static uint8_t own_addr_type;
#endif

/* > Local Variables *******************************************************************************/
static ble_app_service_t* s_service;



/* > Function Definitions *************************************************************************/

// A name for the next start only (AppBle_setNextName). Empty = GAP_NAME.
static char s_nextName[APP_BLE_NAME_MAX + 1] = "";

void AppBle_setNextName(const char* name) {
  s_nextName[0] = '\0';
  if(name != NULL && strlen(name) <= APP_BLE_NAME_MAX)
    strcpy(s_nextName, name);
}

void AppBle_init(ble_app_service_t* service) {
  esp_err_t ret;
  int rc;

  if(service == NULL) {
    ESP_LOGE(tag, "BLE Service is NULL");
    return;
  }
  s_service = service;

  // Initialize the NimBLE host configuration
#ifdef OLD_NIMBLE_INIT
  ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT); // Release memory for Classic BT if not used
  if (ret != ESP_OK) {
      printf("Failed to release BT memory: %d\n", ret);
  }

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  ret = esp_bt_controller_init(&bt_cfg);
  if (ret != ESP_OK) {
      printf("Bluetooth Controller initialization failed: %d\n", ret);
      return;
  }

  ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (ret != ESP_OK) {
      printf("Failed to enable Bluetooth Controller: %d\n", ret);
      return;
  }

  ret = esp_nimble_init();
  if (ret != ESP_OK) {
      printf("Failed to initialize NimBLE: %d\n", ret);
      return;
  }

#else
  ret = nimble_port_init();
  if (ret != ESP_OK) {
    ESP_LOGE(tag, "Failed to init nimble %d ", ret);
    return;
  }
#endif

  /* Initialize the NimBLE host configuration. */
  ble_hs_cfg.reset_cb = _onReset;
  ble_hs_cfg.sync_cb = _onSync;
  ble_hs_cfg.gatts_register_cb = NULL;//gatt_svr_register_cb;
  ble_hs_cfg.store_status_cb = NULL;//ble_store_util_status_rr;

  ble_hs_cfg.sm_io_cap = CONFIG_EXAMPLE_IO_TYPE;
#ifdef CONFIG_EXAMPLE_BONDING
  ble_hs_cfg.sm_bonding = 1;
  /* Enable the appropriate bit masks to make sure the keys
  * that are needed are exchanged
   */
  ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;
  ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;
#endif
#ifdef CONFIG_EXAMPLE_MITM
  ble_hs_cfg.sm_mitm = 1;
#endif
#ifdef CONFIG_EXAMPLE_USE_SC
  ble_hs_cfg.sm_sc = 1;
#else
  ble_hs_cfg.sm_sc = 0;
#endif
#ifdef CONFIG_EXAMPLE_RESOLVE_PEER_ADDR
  /* Stores the IRK */
  ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ID;
#endif

  // Initialize GATT server

  // Add sevrice
  rc = ble_gatts_count_cfg(service->gattService);
  assert(rc == 0);

  rc = ble_gatts_add_svcs(service->gattService);
  assert(rc == 0);

  // Initialize service
  if(service->init) {
    rc = service->init();
    assert(rc == 0);
  }

  /* Set the device name: GAP_NAME, or the name of AppBle_setNextName() for this start only. */
  rc = ble_svc_gap_device_name_set(s_nextName[0] ? s_nextName : GAP_NAME);
  assert(rc == 0);
  s_nextName[0] = '\0';

  /* XXX Need to have template for store */
  ble_store_config_init();

  nimble_port_freertos_init(_hostTask);
}


void AppBle_deinit(void)
{
  MODLOG_DFLT(INFO, "\n Stoping BLE and notification task \n");

  // vTaskDelete(xHandle);
  int ret = nimble_port_stop();
  if (ret == 0) {
    nimble_port_deinit();

    // ret = esp_nimble_hci_and_controller_deinit();
    // if (ret != ESP_OK)
    // {
    //   ESP_LOGE(tag, "esp_nimble_hci_and_controller_deinit() failed with error: %d", ret);
    // }
  }

  if(s_service && s_service->deinit) {
    s_service->deinit();
    s_service = NULL;
  }
}



static void _onReset(int reason) {
  MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}


static void _onSync(void)
{
  int rc;

#if CONFIG_EXAMPLE_RANDOM_ADDR
  /* Generate a non-resolvable private address. */
  ble_app_set_addr();
#endif

  /* Make sure we have proper identity address set (public preferred) */
#if CONFIG_EXAMPLE_RANDOM_ADDR
  rc = ble_hs_util_ensure_addr(1);
#else
  rc = ble_hs_util_ensure_addr(0);
#endif
  assert(rc == 0);

  /* Figure out address to use while advertising (no privacy for now) */
  rc = ble_hs_id_infer_auto(0, &own_addr_type);
  if (rc != 0) {
    MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
    return;
  }

  /* Printing ADDR */
  uint8_t addr_val[6] = {0};
  rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);

  MODLOG_DFLT(INFO, "Device Address: ");
  print_addr(addr_val);
  MODLOG_DFLT(INFO, "\n");
  /* Begin advertising. */
#if CONFIG_EXAMPLE_EXTENDED_ADV
  ext_bleprph_advertise();
#else
  bleprph_advertise();
#endif
}



#if CONFIG_EXAMPLE_RANDOM_ADDR
static void ble_app_set_addr(void)
{
    ble_addr_t addr;
    int rc;

    /* generate new non-resolvable private address */
    rc = ble_hs_id_gen_rnd(0, &addr);
    assert(rc == 0);

    /* set generated address */
    rc = ble_hs_id_set_rnd(addr.val);

    assert(rc == 0);
}
#endif


static void _hostTask(void *param)
{
    ESP_LOGI(tag, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}


#if CONFIG_EXAMPLE_EXTENDED_ADV
/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
static void
ext_bleprph_advertise(void)
{
    struct ble_gap_ext_adv_params params;
    struct os_mbuf *data;
    uint8_t instance = 0;
    int rc;

    /* First check if any instance is already active */
    if(ble_gap_ext_adv_active(instance)) {
        return;
    }

    /* use defaults for non-set params */
    memset (&params, 0, sizeof(params));

    /* enable connectable advertising */
    params.connectable = 1;

    /* advertise using random addr */
    params.own_addr_type = BLE_OWN_ADDR_PUBLIC;

    params.primary_phy = BLE_HCI_LE_PHY_1M;
    params.secondary_phy = BLE_HCI_LE_PHY_2M;
    //params.tx_power = 127;
    params.sid = 1;

    params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MIN;

    /* configure instance 0 */
    rc = ble_gap_ext_adv_configure(instance, &params, NULL,
                                   bleprph_gap_event, NULL);
    assert (rc == 0);

    /* in this case only scan response is allowed */

    /* get mbuf for scan rsp data */
    data = os_msys_get_pkthdr(sizeof(ext_adv_pattern_1), 0);
    assert(data);

    /* fill mbuf with scan rsp data */
    rc = os_mbuf_append(data, ext_adv_pattern_1, sizeof(ext_adv_pattern_1));
    assert(rc == 0);

    rc = ble_gap_ext_adv_set_data(instance, data);
    assert (rc == 0);

    /* start advertising */
    rc = ble_gap_ext_adv_start(instance, 0, 0);
    assert (rc == 0);
}
#else
/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
static void
bleprph_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;

    /**
     *  Set the advertisement data included in our advertisements:
     *     o Flags (indicates advertisement type and other general info).
     *     o Advertising tx power.
     *     o Device name.
     *     o 16-bit service UUIDs (alert notifications).
     */

    memset(&fields, 0, sizeof fields);

    /* Advertise two flags:
     *     o Discoverability in forthcoming advertisement (general)
     *     o BLE-only (BR/EDR unsupported).
     */
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    /* Indicate that the TX power level field should be included; have the
     * stack fill this value automatically.  This is done by assigning the
     * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
     */
    // fields.tx_pwr_lvl_is_present = 1;
    // fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    // fields.uuids16 = (ble_uuid16_t[]) {
        // BLE_UUID16_INIT(GATT_SVR_SVC_ALERT_UUID)
    // };
    // fields.num_uuids16 = 1;
    // fields.uuids16_is_complete = 1;
    fields.uuids128 = (ble_uuid128_t[]) {
        BLE_UUID128_INIT(0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03)
    };
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d\n", rc);
        return;
    }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, bleprph_gap_event, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
        return;
    }
}
#endif



/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that forms.
 * bleprph uses the same callback for all connections.
 *
 * @param event                 The type of event being signalled.
 * @param ctxt                  Various information pertaining to the event.
 * @param arg                   Application-specified argument; unused by
 *                                  bleprph.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int
bleprph_gap_event(struct ble_gap_event *event, void *arg)
{
  struct ble_gap_conn_desc desc;
  int rc;

  switch (event->type) {

    // --- Connect
    case BLE_GAP_EVENT_CONNECT:
      MODLOG_DFLT(INFO, "connection %s; status=%d ",
                  event->connect.status == 0 ? "established" : "failed",
                  event->connect.status);
      if (event->connect.status != 0) {
        /* Connection failed; resume advertising */
        bleprph_advertise();

        if(s_service && s_service->gap.connectCb) {
          s_service->gap.connectCb();
        }
      }
      return 0;

    // --- Disconnect
    case BLE_GAP_EVENT_DISCONNECT:
      MODLOG_DFLT(INFO, "disconnect; reason=%d ", event->disconnect.reason);
      /* Connection terminated; resume advertising */ 
      bleprph_advertise();

      if(s_service && s_service->gap.disconnectCb) {
        s_service->gap.disconnectCb();
      }
      return 0;

    // --- Connection Update
    case BLE_GAP_EVENT_CONN_UPDATE:
      MODLOG_DFLT(INFO, "connection updated; status=%d ",
                  event->conn_update.status);

      rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
      if(rc != 0) {
        MODLOG_DFLT(ERROR, "Failed to get connection descriptor; rc=%d\n", rc);
        return 0;
      }
      return 0;


    // --- Advertising Complete/Expired
    case BLE_GAP_EVENT_ADV_COMPLETE:
      MODLOG_DFLT(INFO, "advertise complete; reason=%d",
                  event->adv_complete.reason);
    
      // Do not resume advertising if reason is BLE_HS_EDISABLED
      if(event->adv_complete.reason != BLE_HS_EDISABLED && 
         event->adv_complete.reason != BLE_HS_EPREEMPTED)
        bleprph_advertise();
      return 0;


    case BLE_GAP_EVENT_NOTIFY_TX:
        MODLOG_DFLT(INFO, "notify_tx event; conn_handle=%d attr_handle=%d "
                    "status=%d is_indication=%d",
                    event->notify_tx.conn_handle,
                    event->notify_tx.attr_handle,
                    event->notify_tx.status,
                    event->notify_tx.indication);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        MODLOG_DFLT(INFO, "subscribe event; conn_handle=%d attr_handle=%d "
                    "reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
                    event->subscribe.conn_handle,
                    event->subscribe.attr_handle,
                    event->subscribe.reason,
                    event->subscribe.prev_notify,
                    event->subscribe.cur_notify,
                    event->subscribe.prev_indicate,
                    event->subscribe.cur_indicate);

        if(s_service && s_service->gap.subscribeCb) {
          s_service->gap.subscribeCb(event);
        }
        return 0;

    case BLE_GAP_EVENT_MTU:
        MODLOG_DFLT(INFO, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);
        return 0;
    }

    return 0;
}