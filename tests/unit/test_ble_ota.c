/***************************************************************************************************
 * @file    : test_ble_ota.c
 * @brief   : Tests of the Bluetooth firmware-update packet code (components/ble_ota/ble_ota.c).
 *
 * This code reads data that any Bluetooth device near the StatTrak can send while Bluetooth is
 * on. It must never write outside its buffer and it must never stop the firmware, whatever the
 * data is. It must also stay compatible with the update tool (the web page that sends the
 * firmware): test "the_update_tool_format_is_accepted" sends data exactly as that tool does.
 *
 * The test calls the code the same way as the NimBLE Bluetooth stack: through the access
 * function in the GATT table. NimBLE is replaced by tests/unit/nimble/host/ble_hs.h.
 **************************************************************************************************/

#include <stdlib.h>
#include <string.h>

#include "utest.h"
#include "ble_ota.h"

/* > Defines **************************************************************************************/
enum { CHR_RECV_FW = 0, CHR_OTA_BAR, CHR_COMMAND, CHR_CUSTOM, CHR_COUNT };

#define CMD_START         (0x0001)
#define CMD_STOP          (0x0002)
#define CMD_ACK           (0x0003)

#define CMD_ACCEPTED      (0x0000)
#define CMD_REJECTED      (0x0001)

#define ACK_SUCCESS       (0x0000)
#define ACK_CRC_ERR       (0x0001)
#define ACK_SECTOR_ERR    (0x0002)
#define ACK_LENGTH_ERR    (0x0003)

#define CONN_HANDLE       (1)
#define HEADER_SIZE       (3)   /* sector (2 bytes) + packet number (1 byte) */
#define LAST_PACKET       (0xFF)

/* > NimBLE stand-in ******************************************************************************/
static int      s_mbufsAlive = 0;

static uint16_t s_notifyAttr = 0;
static uint8_t  s_notifyData[64];
static uint16_t s_notifyLen = 0;
static uint32_t s_notifyCount = 0;

static struct os_mbuf* _mbufNew(const uint8_t* data, uint16_t len) {
  struct os_mbuf* om = malloc(sizeof(*om));
  /* Exact size: a read after the end of the data is an error that the sanitizer reports */
  om->om_data = malloc(len > 0 ? len : 1);
  if(len > 0)
    memcpy(om->om_data, data, len);
  om->om_len = len;
  om->om_next = NULL;
  s_mbufsAlive++;
  return om;
}

static void _mbufFree(struct os_mbuf* om) {
  while(om != NULL) {
    struct os_mbuf* next = om->om_next;
    free(om->om_data);
    free(om);
    s_mbufsAlive--;
    om = next;
  }
}

struct os_mbuf* ble_hs_mbuf_from_flat(const void* buf, uint16_t len) {
  return _mbufNew(buf, len);
}

int os_mbuf_append(struct os_mbuf* om, const void* data, uint16_t len) {
  om->om_data = realloc(om->om_data, om->om_len + len);
  memcpy(om->om_data + om->om_len, data, len);
  om->om_len += len;
  return 0;
}

uint16_t test_mbuf_pktlen(const struct os_mbuf* om) {
  uint32_t total = 0;
  for(; om != NULL; om = om->om_next)
    total += om->om_len;
  return (uint16_t)total;
}

/* The same rule as NimBLE: -1 when the chain has less data than the caller wants */
int os_mbuf_copydata(const struct os_mbuf* om, int off, int len, void* dst) {
  uint8_t* out = dst;

  while(om != NULL && off >= om->om_len) {
    off -= om->om_len;
    om = om->om_next;
  }

  while(len > 0 && om != NULL) {
    int part = om->om_len - off;
    if(part > len)
      part = len;
    memcpy(out, om->om_data + off, (size_t)part);
    out += part;
    len -= part;
    off = 0;
    om = om->om_next;
  }

  return (len > 0) ? -1 : 0;
}

int ble_gattc_notify_custom(uint16_t conn_handle, uint16_t attr_handle, struct os_mbuf* om) {
  (void)conn_handle;
  s_notifyAttr = attr_handle;
  s_notifyLen = (om->om_len <= sizeof(s_notifyData)) ? om->om_len : sizeof(s_notifyData);
  memcpy(s_notifyData, om->om_data, s_notifyLen);
  s_notifyCount++;
  _mbufFree(om); /* The stack takes the buffer */
  return 0;
}

/* > Test helpers *********************************************************************************/
static const struct ble_gatt_chr_def* s_chrs;
static uint16_t s_handles[CHR_COUNT];

static uint8_t  s_received[64 * 1024];
static uint32_t s_receivedLen = 0;
static uint32_t s_receiveCalls = 0;
static uint16_t s_receiveMaxLen = 0;
static uint32_t s_startEvents = 0;
static uint32_t s_stopEvents = 0;
static bool     s_acceptStart = true;

static uint8_t  s_customData[64];
static uint16_t s_customLen = 0;
static uint32_t s_customCalls = 0;

static void _onFirmware(uint8_t* data, uint16_t len) {
  s_receiveCalls++;
  if(len > s_receiveMaxLen)
    s_receiveMaxLen = len;
  if(s_receivedLen + len <= sizeof(s_received)) {
    memcpy(s_received + s_receivedLen, data, len);
    s_receivedLen += len;
  }
}

static bool _onEvent(ble_ota_event_t event) {
  if(event == BLE_OTA_EVENT__START) {
    s_startEvents++;
    return s_acceptStart;
  }
  if(event == BLE_OTA_EVENT__STOP)
    s_stopEvents++;
  return true;
}

/* Answers each custom command with its own data, with the bits changed */
static uint16_t _onCustom(uint8_t* data, uint16_t len, uint8_t* resp, uint16_t respLen) {
  s_customCalls++;
  s_customLen = (len <= sizeof(s_customData)) ? len : sizeof(s_customData);
  memcpy(s_customData, data, s_customLen);

  if(len > respLen)
    return 0;
  for(uint16_t i = 0; i < len; i++)
    resp[i] = (uint8_t)~data[i];
  return len;
}

static void _subscribe(int chr, bool enable) {
  struct ble_gap_event event = { 0 };
  event.subscribe.attr_handle = s_handles[chr];
  event.subscribe.cur_indicate = enable ? 1 : 0;
  g_otaService.gap.subscribeCb(&event);
}

static void _setup(void) {
  s_chrs = g_otaService.gattService[0].characteristics;
  for(int i = 0; i < CHR_COUNT; i++) {
    s_handles[i] = (uint16_t)(100 + i);
    *s_chrs[i].val_handle = s_handles[i]; /* NimBLE sets these when it registers the service */
    _subscribe(i, true);
  }

  BleOta_abort(); /* Stop a transfer that an other test left open */
  BleOta_setReceiveFwCallback(_onFirmware);
  BleOta_setEventCallback(_onEvent);
  BleOta_setCustomCallback(_onCustom);

  s_receivedLen = 0;
  s_receiveCalls = 0;
  s_receiveMaxLen = 0;
  s_startEvents = 0;
  s_stopEvents = 0;
  s_acceptStart = true;
  s_customCalls = 0;
  s_customLen = 0;
  s_notifyCount = 0;
  s_notifyLen = 0;
  s_notifyAttr = 0;
}

/* Writes `len` bytes to a characteristic. The data is split: `firstLen` bytes in the first
 * buffer, the remaining bytes in buffers of `chunk` bytes. firstLen = len means one buffer. */
static int _write(int chr, const uint8_t* data, uint16_t len, uint16_t firstLen, uint16_t chunk) {
  if(firstLen > len)
    firstLen = len;

  struct os_mbuf* head = _mbufNew(data, firstLen);
  struct os_mbuf* tail = head;
  uint16_t offset = firstLen;

  while(offset < len) {
    uint16_t part = (uint16_t)(len - offset);
    if(chunk > 0 && part > chunk)
      part = chunk;
    tail->om_next = _mbufNew(data + offset, part);
    tail = tail->om_next;
    offset = (uint16_t)(offset + part);
  }

  struct ble_gatt_access_ctxt ctxt = {
    .op = BLE_GATT_ACCESS_OP_WRITE_CHR,
    .om = head,
    .chr = &s_chrs[chr],
  };
  const int rc = s_chrs[chr].access_cb(CONN_HANDLE, s_handles[chr], &ctxt, NULL);

  _mbufFree(head);
  return rc;
}

static void _makeCommand(uint8_t* out, uint16_t cmdId, uint32_t value, bool goodCrc) {
  ble_ota_cmd_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.cmdId = cmdId;
  memcpy(&cmd.payload[0], &value, sizeof(value));
  cmd.crc = BleOta_calculateCrc((const uint8_t*)&cmd, sizeof(cmd) - sizeof(uint16_t));
  if(!goodCrc)
    cmd.crc ^= 0x5555;
  memcpy(out, &cmd, sizeof(cmd));
}

static void _sendCommand(uint16_t cmdId, uint32_t value) {
  uint8_t cmd[sizeof(ble_ota_cmd_t)];
  _makeCommand(cmd, cmdId, value, true);
  _write(CHR_COMMAND, cmd, sizeof(cmd), sizeof(cmd), 0);
}

static void _sendStart(uint32_t fwLen) {
  _sendCommand(CMD_START, fwLen);
}

/* The answer to the last command: 0 = accepted, 1 = rejected */
static uint16_t _commandAnswer(void) {
  ble_ota_cmd_t ack;
  memcpy(&ack, s_notifyData, sizeof(ack));
  return (uint16_t)(ack.payload[2] | (ack.payload[3] << 8));
}

/* The answer to the last sector */
static ble_ota_ack_t _sectorAnswer(void) {
  ble_ota_ack_t ack;
  memcpy(&ack, s_notifyData, sizeof(ack));
  return ack;
}

/* Sends one firmware packet: sector number, packet number, data. */
static void _sendPacket(uint16_t sector, uint8_t seq, const uint8_t* data, uint16_t len, uint16_t firstLen) {
  static uint8_t packet[8192];
  packet[0] = (uint8_t)(sector & 0xFF);
  packet[1] = (uint8_t)(sector >> 8);
  packet[2] = seq;
  memcpy(&packet[HEADER_SIZE], data, len);
  const uint16_t total = (uint16_t)(len + HEADER_SIZE);
  _write(CHR_RECV_FW, packet, total, (firstLen == 0) ? total : firstLen, 97);
}

/* Makes the 4098 bytes of one sector: the data, zeros up to 4096, then the CRC of the data. */
static void _makeSector(uint8_t* sectorBuf, const uint8_t* data, uint16_t len) {
  memset(sectorBuf, 0, OTA_BUFFER_SIZE);
  memcpy(sectorBuf, data, len);
  const uint16_t crc = BleOta_calculateCrc(data, len);
  sectorBuf[OTA_BLOCK_SIZE] = (uint8_t)(crc & 0xFF);
  sectorBuf[OTA_BLOCK_SIZE + 1] = (uint8_t)(crc >> 8);
}

/* Sends the 4098 bytes of a sector in packets of `packetData` bytes. */
static void _sendSectorBytes(uint16_t sector, const uint8_t* sectorBuf, uint16_t total, uint16_t packetData) {
  uint16_t offset = 0;
  uint8_t seq = 0;
  while(offset < total) {
    uint16_t part = (uint16_t)(total - offset);
    if(part > packetData)
      part = packetData;
    const bool last = (offset + part) >= total;
    _sendPacket(sector, last ? LAST_PACKET : seq, &sectorBuf[offset], part, 0);
    offset = (uint16_t)(offset + part);
    seq++;
  }
}

/* Sends one sector with `len` firmware bytes. */
static void _sendSector(uint16_t sector, const uint8_t* data, uint16_t len, uint16_t packetData) {
  static uint8_t sectorBuf[OTA_BUFFER_SIZE];
  _makeSector(sectorBuf, data, len);
  _sendSectorBytes(sector, sectorBuf, OTA_BUFFER_SIZE, packetData);
}

static void _fill(uint8_t* data, uint32_t len, uint32_t seed) {
  for(uint32_t i = 0; i < len; i++)
    data[i] = (uint8_t)((i * 31u + seed * 7u + (i >> 8)) & 0xFF);
}

/* > Tests: commands ******************************************************************************/

TEST(crc_is_crc16_ccitt) {
  /* The standard check value of CRC-16/XMODEM for the text "123456789" */
  CHECK_EQ(BleOta_calculateCrc((const uint8_t*)"123456789", 9), 0x31C3);
  CHECK_EQ(BleOta_calculateCrc((const uint8_t*)"", 0), 0);
}

TEST(start_command_sets_the_length_and_is_acknowledged) {
  _setup();
  _sendStart(123456);
  CHECK_EQ(BleOta_getFwLength(), 123456);
  CHECK_EQ(s_startEvents, 1);

  CHECK_EQ(s_notifyCount, 1);
  CHECK_EQ(s_notifyAttr, s_handles[CHR_COMMAND]);
  CHECK_EQ(s_notifyLen, sizeof(ble_ota_cmd_t));

  ble_ota_cmd_t ack;
  memcpy(&ack, s_notifyData, sizeof(ack));
  CHECK_EQ(ack.cmdId, CMD_ACK);
  CHECK_EQ(ack.payload[0] | (ack.payload[1] << 8), CMD_START);
  CHECK_EQ(_commandAnswer(), CMD_ACCEPTED);
  CHECK_EQ(ack.crc, BleOta_calculateCrc((const uint8_t*)&ack, sizeof(ack) - sizeof(uint16_t)));
}

TEST(start_command_with_length_zero_is_rejected) {
  static uint8_t data[100];
  _setup();
  _sendStart(0);
  CHECK_EQ(s_startEvents, 0);
  CHECK_EQ(_commandAnswer(), CMD_REJECTED);

  _sendSector(0, data, sizeof(data), 244);
  CHECK_EQ(s_receiveCalls, 0);
}

TEST(start_command_that_the_firmware_refuses_is_rejected) {
  static uint8_t data[100];
  _setup();
  s_acceptStart = false; /* For example: the firmware is too large, or updates are not allowed */
  _sendStart(sizeof(data));
  CHECK_EQ(s_startEvents, 1);
  CHECK_EQ(_commandAnswer(), CMD_REJECTED);

  _sendSector(0, data, sizeof(data), 244);
  CHECK_EQ(s_receiveCalls, 0);
}

TEST(command_with_bad_crc_or_bad_length_is_ignored) {
  uint8_t cmd[sizeof(ble_ota_cmd_t) + 4] = { 0 };
  _setup();
  _sendStart(1000);

  _makeCommand(cmd, CMD_START, 999999, false);
  _write(CHR_COMMAND, cmd, sizeof(ble_ota_cmd_t), sizeof(ble_ota_cmd_t), 0);
  CHECK_EQ(BleOta_getFwLength(), 1000);

  _makeCommand(cmd, CMD_START, 999999, true);
  _write(CHR_COMMAND, cmd, sizeof(ble_ota_cmd_t) - 1, sizeof(ble_ota_cmd_t) - 1, 0);
  _write(CHR_COMMAND, cmd, sizeof(ble_ota_cmd_t) + 1, sizeof(ble_ota_cmd_t) + 1, 0);
  _write(CHR_COMMAND, cmd, 1, 1, 0);
  _write(CHR_COMMAND, cmd, 0, 0, 0);
  CHECK_EQ(BleOta_getFwLength(), 1000);
  CHECK_EQ(s_startEvents, 1);
}

TEST(command_in_many_buffers_is_put_together) {
  uint8_t cmd[sizeof(ble_ota_cmd_t)];
  _setup();
  _makeCommand(cmd, CMD_START, 4242, true);
  _write(CHR_COMMAND, cmd, sizeof(cmd), 3, 5);
  CHECK_EQ(BleOta_getFwLength(), 4242);
  CHECK_EQ(s_startEvents, 1);
}

TEST(stop_command_stops_the_transfer) {
  static uint8_t data[OTA_BLOCK_SIZE];
  _setup();
  _fill(data, sizeof(data), 11);

  _sendStart(2 * OTA_BLOCK_SIZE);
  _sendSector(0, data, OTA_BLOCK_SIZE, 244);
  CHECK_EQ(s_receiveCalls, 1);

  _sendCommand(CMD_STOP, 0);
  CHECK_EQ(s_stopEvents, 1);
  CHECK_EQ(_commandAnswer(), CMD_ACCEPTED);

  _sendSector(1, data, OTA_BLOCK_SIZE, 244);
  CHECK_EQ(s_receiveCalls, 1);
  CHECK_EQ(_sectorAnswer().ackStatus, ACK_SECTOR_ERR);
}

TEST(lost_connection_stops_the_transfer) {
  static uint8_t data[OTA_BLOCK_SIZE];
  _setup();
  _fill(data, sizeof(data), 12);

  _sendStart(2 * OTA_BLOCK_SIZE);
  _sendSector(0, data, OTA_BLOCK_SIZE, 244);
  CHECK_EQ(s_receiveCalls, 1);

  g_otaService.gap.disconnectCb();
  CHECK_EQ(s_stopEvents, 1);

  /* The next phone must subscribe again before it gets answers */
  const uint32_t notifyBefore = s_notifyCount;
  _sendSector(1, data, OTA_BLOCK_SIZE, 244);
  CHECK_EQ(s_receiveCalls, 1);
  CHECK_EQ(s_notifyCount, notifyBefore);

  /* A lost connection without a transfer is not a "stop" */
  g_otaService.gap.disconnectCb();
  CHECK_EQ(s_stopEvents, 1);
}

TEST(firmware_can_abort_the_transfer) {
  static uint8_t data[OTA_BLOCK_SIZE];
  _setup();
  _fill(data, sizeof(data), 13);

  _sendStart(2 * OTA_BLOCK_SIZE);
  BleOta_abort(); /* For example: a flash write failed */
  _sendSector(0, data, OTA_BLOCK_SIZE, 244);
  CHECK_EQ(s_receiveCalls, 0);
  CHECK_EQ(_sectorAnswer().ackStatus, ACK_SECTOR_ERR);
}

/* > Tests: firmware data *************************************************************************/

TEST(firmware_data_is_ignored_before_the_start_command) {
  uint8_t data[200];
  _setup();
  _fill(data, sizeof(data), 1);

  _sendSector(0, data, sizeof(data), 100);
  CHECK_EQ(s_receiveCalls, 0);
  /* The phone gets an error for the last packet of the sector, so it does not wait for ever */
  CHECK_EQ(s_notifyAttr, s_handles[CHR_RECV_FW]);
  CHECK_EQ(_sectorAnswer().ackStatus, ACK_SECTOR_ERR);
}

TEST(full_sector_is_delivered_without_its_crc) {
  static uint8_t data[OTA_BLOCK_SIZE];
  _setup();
  _fill(data, sizeof(data), 2);

  _sendStart(2 * OTA_BLOCK_SIZE);
  _sendSector(0, data, OTA_BLOCK_SIZE, 244);

  CHECK_EQ(s_receiveCalls, 1);
  CHECK_EQ(s_receivedLen, OTA_BLOCK_SIZE);
  CHECK(memcmp(s_received, data, OTA_BLOCK_SIZE) == 0);

  /* The sector is acknowledged on the firmware characteristic */
  const ble_ota_ack_t ack = _sectorAnswer();
  CHECK_EQ(s_notifyAttr, s_handles[CHR_RECV_FW]);
  CHECK_EQ(s_notifyLen, 20);
  CHECK_EQ(ack.sector, 0);
  CHECK_EQ(ack.ackStatus, ACK_SUCCESS);
  CHECK_EQ(ack.currentSector, 1);
  CHECK_EQ(ack.crc, BleOta_calculateCrc((const uint8_t*)&ack, sizeof(ack) - sizeof(uint16_t)));
}

TEST(short_last_sector_is_delivered_with_the_exact_length) {
  static uint8_t data[2 * OTA_BLOCK_SIZE + 300];
  _setup();
  _fill(data, sizeof(data), 3);

  _sendStart(sizeof(data));
  _sendSector(0, &data[0], OTA_BLOCK_SIZE, 500);
  _sendSector(1, &data[OTA_BLOCK_SIZE], OTA_BLOCK_SIZE, 20);
  _sendSector(2, &data[2 * OTA_BLOCK_SIZE], 300, 244);

  /* Only firmware bytes go to flash: no padding and no CRC bytes */
  CHECK_EQ(s_receiveCalls, 3);
  CHECK_EQ(s_receivedLen, sizeof(data));
  CHECK(memcmp(s_received, data, sizeof(data)) == 0);
  CHECK_EQ(_sectorAnswer().ackStatus, ACK_SUCCESS);
}

/* The update tool (the web page, file js/ble.js, function sendFwData) sends the firmware like
 * this. If this test fails, released update tools cannot update a device with this firmware. */
TEST(the_update_tool_format_is_accepted) {
  enum { DATA_PER_PACKET = 256 - 4 - 3 };
  static uint8_t firmware[3 * OTA_BLOCK_SIZE + 1234];
  static uint8_t sectorData[OTA_BUFFER_SIZE];
  const uint32_t total = sizeof(firmware);
  uint32_t sent = 0;
  uint16_t sector = 0;

  _setup();
  _fill(firmware, total, 21);
  _sendStart(total);
  CHECK_EQ(_commandAnswer(), CMD_ACCEPTED);

  while(sent < total) {
    const uint16_t part = (uint16_t)(((total - sent) < OTA_BLOCK_SIZE) ? (total - sent) : OTA_BLOCK_SIZE);
    _makeSector(sectorData, &firmware[sent], part);

    uint16_t remaining = OTA_BUFFER_SIZE;
    uint16_t offset = 0;
    uint8_t packetSeq = 0;

    for(;;) {
      const uint16_t payload = (remaining < DATA_PER_PACKET) ? remaining : DATA_PER_PACKET;
      _sendPacket(sector, packetSeq, &sectorData[offset], payload, 0);
      offset = (uint16_t)(offset + payload);
      remaining = (uint16_t)(remaining - payload);

      if(packetSeq == LAST_PACKET)
        break;
      packetSeq++;
      if(remaining <= DATA_PER_PACKET)
        packetSeq = LAST_PACKET;
    }

    const ble_ota_ack_t ack = _sectorAnswer();
    CHECK_EQ(ack.sector, sector);
    CHECK_EQ(ack.ackStatus, ACK_SUCCESS);
    CHECK_EQ(ack.crc, BleOta_calculateCrc((const uint8_t*)&ack, 18));

    sent += part;
    sector++;
  }

  CHECK_EQ(s_receivedLen, total);
  CHECK(memcmp(s_received, firmware, total) == 0);
}

TEST(packet_in_many_buffers_is_put_together) {
  static uint8_t data[1000];
  static uint8_t sector[OTA_BUFFER_SIZE];
  _setup();
  _fill(data, sizeof(data), 4);

  _sendStart(sizeof(data));

  /* One sector in one packet. NimBLE gives it as a chain: 4 bytes, then parts of 97 bytes. */
  _makeSector(sector, data, sizeof(data));
  _sendPacket(0, LAST_PACKET, sector, sizeof(sector), 4);

  CHECK_EQ(s_receiveCalls, 1);
  CHECK_EQ(s_receivedLen, sizeof(data));
  CHECK(memcmp(s_received, data, sizeof(data)) == 0);
}

TEST(sector_with_a_wrong_crc_is_refused_and_can_be_sent_again) {
  static uint8_t data[OTA_BLOCK_SIZE];
  static uint8_t sector[OTA_BUFFER_SIZE];
  _setup();
  _fill(data, sizeof(data), 14);

  _sendStart(OTA_BLOCK_SIZE);
  _makeSector(sector, data, OTA_BLOCK_SIZE);
  sector[1000] ^= 0x01; /* One bit changed on the way */
  _sendSectorBytes(0, sector, OTA_BUFFER_SIZE, 244);

  CHECK_EQ(s_receiveCalls, 0);
  CHECK_EQ(_sectorAnswer().ackStatus, ACK_CRC_ERR);
  CHECK_EQ(_sectorAnswer().currentSector, 0);

  _sendSector(0, data, OTA_BLOCK_SIZE, 244);
  CHECK_EQ(s_receiveCalls, 1);
  CHECK_EQ(_sectorAnswer().ackStatus, ACK_SUCCESS);
  CHECK(memcmp(s_received, data, OTA_BLOCK_SIZE) == 0);
}

TEST(sector_that_is_not_4098_bytes_is_refused) {
  static uint8_t data[OTA_BUFFER_SIZE];
  _setup();
  _fill(data, sizeof(data), 15);

  _sendStart(300);

  /* 300 data bytes + CRC, without the padding up to 4096 */
  const uint16_t crc = BleOta_calculateCrc(data, 300);
  data[300] = (uint8_t)(crc & 0xFF);
  data[301] = (uint8_t)(crc >> 8);
  _sendSectorBytes(0, data, 302, 244);

  CHECK_EQ(s_receiveCalls, 0);
  CHECK_EQ(_sectorAnswer().ackStatus, ACK_LENGTH_ERR);
}

TEST(sector_with_a_wrong_number_is_refused) {
  static uint8_t data[OTA_BLOCK_SIZE];
  _setup();
  _fill(data, sizeof(data), 16);

  _sendStart(3 * OTA_BLOCK_SIZE);
  _sendSector(1, data, OTA_BLOCK_SIZE, 244);       /* Sector 0 is expected */
  CHECK_EQ(s_receiveCalls, 0);
  CHECK_EQ(_sectorAnswer().ackStatus, ACK_SECTOR_ERR);
  CHECK_EQ(_sectorAnswer().sector, 0);              /* The answer has the sector that the device waits for */

  _sendSector(0xFFFF, data, OTA_BLOCK_SIZE, 244);
  CHECK_EQ(s_receiveCalls, 0);

  _sendSector(0, data, OTA_BLOCK_SIZE, 244);
  CHECK_EQ(s_receiveCalls, 1);
  _sendSector(0, data, OTA_BLOCK_SIZE, 244);       /* The same sector a second time */
  CHECK_EQ(s_receiveCalls, 1);
  CHECK_EQ(_sectorAnswer().ackStatus, ACK_SECTOR_ERR);
}

TEST(sector_with_a_lost_packet_is_refused) {
  static uint8_t data[OTA_BLOCK_SIZE];
  static uint8_t sector[OTA_BUFFER_SIZE];
  _setup();
  _fill(data, sizeof(data), 17);
  _makeSector(sector, data, OTA_BLOCK_SIZE);

  _sendStart(OTA_BLOCK_SIZE);
  _sendPacket(0, 0, &sector[0], 1000, 0);
  _sendPacket(0, 2, &sector[1000], 1000, 0);        /* Packet 1 is lost */
  _sendPacket(0, 3, &sector[2000], 1000, 0);
  _sendPacket(0, LAST_PACKET, &sector[3000], OTA_BUFFER_SIZE - 3000, 0);

  CHECK_EQ(s_receiveCalls, 0);
  CHECK_EQ(_sectorAnswer().ackStatus, ACK_LENGTH_ERR);

  /* The sector can be sent again */
  _sendSector(0, data, OTA_BLOCK_SIZE, 244);
  CHECK_EQ(s_receiveCalls, 1);
  CHECK(memcmp(s_received, data, OTA_BLOCK_SIZE) == 0);
}

TEST(data_after_the_end_of_the_firmware_is_refused) {
  static uint8_t data[OTA_BLOCK_SIZE];
  _setup();
  _fill(data, sizeof(data), 18);

  _sendStart(500);
  _sendSector(0, data, 500, 244);
  CHECK_EQ(s_receiveCalls, 1);
  CHECK_EQ(s_receivedLen, 500);

  _sendSector(1, data, OTA_BLOCK_SIZE, 244);
  CHECK_EQ(s_receiveCalls, 1);
  CHECK_EQ(_sectorAnswer().ackStatus, ACK_LENGTH_ERR);
}

TEST(too_much_data_in_one_buffer_is_refused) {
  static uint8_t big[OTA_BLOCK_SIZE + 500];
  _setup();
  _fill(big, sizeof(big), 5);

  _sendStart(100000);
  _sendPacket(0, 0, big, 3000, 0);
  _sendPacket(0, 1, big, 3000, 0);   /* 6000 bytes: more than one sector buffer */
  _sendPacket(0, LAST_PACKET, big, 3000, 0);

  CHECK_EQ(s_receiveCalls, 0);
  CHECK_EQ(_sectorAnswer().ackStatus, ACK_LENGTH_ERR);

  /* A new transfer must work after the bad one */
  _sendStart(OTA_BLOCK_SIZE);
  _sendSector(0, big, OTA_BLOCK_SIZE, 244);
  CHECK_EQ(s_receiveCalls, 1);
  CHECK_EQ(s_receivedLen, OTA_BLOCK_SIZE);
  CHECK(memcmp(s_received, big, OTA_BLOCK_SIZE) == 0);
}

TEST(too_much_data_in_a_buffer_chain_is_refused) {
  static uint8_t big[6000];
  _setup();
  _fill(big, sizeof(big), 6);

  _sendStart(100000);
  /* The first buffer is small and passes a size check of the first buffer only. The chain
   * behind it is too long. */
  _sendPacket(0, 0, big, 5000, 10);
  _sendPacket(0, 1, big, 5000, 10);
  _sendPacket(0, LAST_PACKET, big, 5000, 10);

  CHECK_EQ(s_receiveCalls, 0);

  _sendStart(OTA_BLOCK_SIZE);
  _sendSector(0, big, OTA_BLOCK_SIZE, 244);
  CHECK_EQ(s_receiveCalls, 1);
  CHECK(memcmp(s_received, big, OTA_BLOCK_SIZE) == 0);
}

TEST(very_short_packets_are_ignored) {
  static const uint8_t kPacket[4] = { 0, 0, LAST_PACKET, 0x42 };
  _setup();
  _sendStart(1000);
  for(uint16_t len = 0; len <= 3; len++)
    _write(CHR_RECV_FW, kPacket, len, len, 0);
  CHECK_EQ(s_receiveCalls, 0);
}

/* > Tests: custom commands ***********************************************************************/

TEST(custom_command_goes_to_the_firmware_and_the_answer_goes_back) {
  uint8_t cmd[sizeof(ble_ota_cmd_t)];
  _setup();
  _makeCommand(cmd, 0x0100, 0xCAFE, true);

  _write(CHR_CUSTOM, cmd, sizeof(cmd), 7, 4);        /* In a chain of buffers */
  CHECK_EQ(s_customCalls, 1);
  CHECK_EQ(s_customLen, sizeof(cmd));
  CHECK(memcmp(s_customData, cmd, sizeof(cmd)) == 0);

  CHECK_EQ(s_notifyAttr, s_handles[CHR_CUSTOM]);
  CHECK_EQ(s_notifyLen, sizeof(cmd));
  CHECK_EQ(s_notifyData[0], (uint8_t)~cmd[0]);
}

TEST(custom_command_with_a_wrong_length_is_ignored) {
  uint8_t cmd[sizeof(ble_ota_cmd_t) + 8] = { 0 };
  _setup();
  _write(CHR_CUSTOM, cmd, sizeof(ble_ota_cmd_t) - 1, sizeof(ble_ota_cmd_t) - 1, 0);
  _write(CHR_CUSTOM, cmd, sizeof(ble_ota_cmd_t) + 1, sizeof(ble_ota_cmd_t) + 1, 0);
  _write(CHR_CUSTOM, cmd, 0, 0, 0);
  CHECK_EQ(s_customCalls, 0);
  CHECK_EQ(s_notifyCount, 0);
}

TEST(firmware_can_send_a_custom_answer_later) {
  static const uint8_t kAnswer[20] = { 3, 0, 0x00, 0x01, 0, 42 };
  _setup();

  CHECK_EQ(BleOta_sendCustom(kAnswer, sizeof(kAnswer)), 0);
  CHECK_EQ(s_notifyCount, 1);
  CHECK_EQ(s_notifyAttr, s_handles[CHR_CUSTOM]);
  CHECK_EQ(s_notifyLen, sizeof(kAnswer));
  CHECK(memcmp(s_notifyData, kAnswer, sizeof(kAnswer)) == 0);

  /* Nothing is sent without data, or when the phone does not listen */
  CHECK(BleOta_sendCustom(NULL, 20) != 0);
  CHECK(BleOta_sendCustom(kAnswer, 0) != 0);
  _subscribe(CHR_CUSTOM, false);
  CHECK(BleOta_sendCustom(kAnswer, sizeof(kAnswer)) != 0);
  CHECK_EQ(s_notifyCount, 1);
}

/* > Tests: robustness ****************************************************************************/

TEST(no_buffer_is_lost_when_the_phone_did_not_subscribe) {
  static uint8_t data[300];
  _setup();
  _fill(data, sizeof(data), 7);
  for(int i = 0; i < CHR_COUNT; i++)
    _subscribe(i, false);

  const int before = s_mbufsAlive;
  for(int i = 0; i < 50; i++) {
    _sendStart(sizeof(data));
    _sendSector(0, data, sizeof(data), 100);
  }
  CHECK_EQ(s_notifyCount, 0);
  CHECK_EQ(s_mbufsAlive, before);
}

TEST(random_data_does_not_break_anything) {
  static uint8_t data[8000];
  _setup();
  srand(20261110);

  for(uint32_t i = 0; i < 60000; i++) {
    const int what = rand() % 12;

    if(what == 0) {
      _sendStart((uint32_t)rand());
    } else if(what == 1) {
      uint8_t cmd[sizeof(ble_ota_cmd_t)];
      _makeCommand(cmd, (uint16_t)(rand() % 5), (uint32_t)rand(), (rand() % 4) != 0);
      _write(CHR_COMMAND, cmd, sizeof(cmd), sizeof(cmd), 0);
    } else if(what == 2) {
      /* Any bytes, any length, to any writable characteristic */
      static const int kWritable[] = { CHR_RECV_FW, CHR_COMMAND, CHR_CUSTOM };
      const uint16_t len = (uint16_t)(rand() % 700);
      _fill(data, len, i);
      _write(kWritable[rand() % 3], data, len, (uint16_t)(rand() % (len + 1)), (uint16_t)(1 + rand() % 300));
    } else if(what == 3) {
      g_otaService.gap.disconnectCb();
      for(int chr = 0; chr < CHR_COUNT; chr++)
        _subscribe(chr, (rand() % 4) != 0);
    } else if(what == 4) {
      /* A correct sector for the sector number that is probably next */
      const uint16_t len = (uint16_t)((rand() % 2) ? OTA_BLOCK_SIZE : (1 + rand() % OTA_BLOCK_SIZE));
      _fill(data, len, i);
      _sendSector((uint16_t)(rand() % 3), data, len, (uint16_t)(20 + rand() % 500));
    } else {
      /* A firmware packet with a random header, length and split */
      const uint16_t len = (uint16_t)((rand() % 8 == 0) ? (rand() % 6000) : (rand() % 520));
      const uint16_t sector = (uint16_t)((rand() % 6 == 0) ? rand() : (rand() % 3));
      const uint8_t seq = (uint8_t)((rand() % 3 == 0) ? LAST_PACKET : (rand() % 20));
      _fill(data, len, i);
      _sendPacket(sector, seq, data, len, (uint16_t)(rand() % (len + HEADER_SIZE + 1)));
    }
  }

  /* Never more than one sector of firmware bytes goes to the flash writer at one time */
  CHECK(s_receiveMaxLen <= OTA_BLOCK_SIZE);
  CHECK(s_receiveCalls > 0);
  CHECK_EQ(s_mbufsAlive, 0);
}

/* > Main *****************************************************************************************/

int main(void) {
  RUN(crc_is_crc16_ccitt);
  RUN(start_command_sets_the_length_and_is_acknowledged);
  RUN(start_command_with_length_zero_is_rejected);
  RUN(start_command_that_the_firmware_refuses_is_rejected);
  RUN(command_with_bad_crc_or_bad_length_is_ignored);
  RUN(command_in_many_buffers_is_put_together);
  RUN(stop_command_stops_the_transfer);
  RUN(lost_connection_stops_the_transfer);
  RUN(firmware_can_abort_the_transfer);

  RUN(firmware_data_is_ignored_before_the_start_command);
  RUN(full_sector_is_delivered_without_its_crc);
  RUN(short_last_sector_is_delivered_with_the_exact_length);
  RUN(the_update_tool_format_is_accepted);
  RUN(packet_in_many_buffers_is_put_together);
  RUN(sector_with_a_wrong_crc_is_refused_and_can_be_sent_again);
  RUN(sector_that_is_not_4098_bytes_is_refused);
  RUN(sector_with_a_wrong_number_is_refused);
  RUN(sector_with_a_lost_packet_is_refused);
  RUN(data_after_the_end_of_the_firmware_is_refused);
  RUN(too_much_data_in_one_buffer_is_refused);
  RUN(too_much_data_in_a_buffer_chain_is_refused);
  RUN(very_short_packets_are_ignored);

  RUN(custom_command_goes_to_the_firmware_and_the_answer_goes_back);
  RUN(custom_command_with_a_wrong_length_is_ignored);
  RUN(firmware_can_send_a_custom_answer_later);

  RUN(no_buffer_is_lost_when_the_phone_did_not_subscribe);
  RUN(random_data_does_not_break_anything);

  return utest_finish();
}
