/*
  xdrv_espnow.ino - ESP-NOW send/receive support for Tasmota (ESP32)

  Minimal transport layer:
  - Broadcast send (raw bytes)
  - Receive → circular queue
  - Berry interface via EspNowSend command + Berry callback

  Enable with: #define USE_ESPNOW in user_config_override.h

  Usage from Berry:
    espnow.init()
    espnow.send(bytes('48656C6C6F'))
    # callback set via driver mechanism, see espnow.be
*/

#ifdef USE_ESPNOW
#ifdef ESP32

#include "esp_now.h"
#include "esp_wifi.h"

#define XDRV_ESPNOW        99          // Free driver slot, change if conflict
#define ESPNOW_QUEUE_SIZE  16          // Max queued received packets
#define ESPNOW_MAX_PAYLOAD 250         // ESP-NOW max is 250 bytes

/*********************************************************************************************\
 * Data structures
\*********************************************************************************************/

struct espnow_packet_t {
  uint8_t  src[6];
  uint8_t  data[ESPNOW_MAX_PAYLOAD];
  uint8_t  len;
};

struct {
  // Receive queue (simple circular buffer)
  espnow_packet_t queue[ESPNOW_QUEUE_SIZE];
  uint8_t  q_head;       // next slot to write
  uint8_t  q_tail;       // next slot to read
  uint8_t  q_count;      // number of packets waiting

  bool     initialized;
  uint8_t  broadcast[6]; // FF:FF:FF:FF:FF:FF
} EspNowData;

/*********************************************************************************************\
 * Callbacks (called from WiFi task context — keep minimal, no AddLog)
\*********************************************************************************************/

void IRAM_ATTR CB_EspNowReceived(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (!EspNowData.initialized) { return; }
  if (len <= 0 || len > ESPNOW_MAX_PAYLOAD) { return; }
  if (EspNowData.q_count >= ESPNOW_QUEUE_SIZE) { return; } // queue full, drop

  espnow_packet_t *slot = &EspNowData.queue[EspNowData.q_head];
  memcpy(slot->src, info->src_addr, 6);
  memcpy(slot->data, data, len);
  slot->len = (uint8_t)len;

  EspNowData.q_head = (EspNowData.q_head + 1) % ESPNOW_QUEUE_SIZE;
  EspNowData.q_count++;
}

void CB_EspNowSent(const uint8_t *mac, esp_now_send_status_t status) {
  // Optional: log send result
  // AddLog(LOG_LEVEL_DEBUG, PSTR("ENW: Send status %d"), status);
}

/*********************************************************************************************\
 * Core functions
\*********************************************************************************************/

bool EspNowInit(void) {
  if (EspNowData.initialized) { return true; }

  // Broadcast MAC
  memset(EspNowData.broadcast, 0xFF, 6);

  // Queue init
  EspNowData.q_head  = 0;
  EspNowData.q_tail  = 0;
  EspNowData.q_count = 0;

  // ESP-NOW init
  if (esp_now_init() != ESP_OK) {
    AddLog(LOG_LEVEL_ERROR, PSTR("ENW: esp_now_init failed"));
    return false;
  }

  esp_now_register_send_cb(CB_EspNowSent);
  esp_now_register_recv_cb(CB_EspNowReceived);

  // Register broadcast peer
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, EspNowData.broadcast, 6);
  peer.channel  = 0;       // 0 = use current Wi-Fi channel
  peer.encrypt  = false;
  peer.ifidx    = WIFI_IF_STA;

  esp_err_t err = esp_now_add_peer(&peer);
  if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
    AddLog(LOG_LEVEL_ERROR, PSTR("ENW: add_peer broadcast failed %d"), err);
    esp_now_deinit();
    return false;
  }

  EspNowData.initialized = true;
  AddLog(LOG_LEVEL_INFO, PSTR("ENW: Initialized on channel %d"), WiFi.channel());
  return true;
}

/**
 * Send raw bytes as broadcast
 * Returns true on success
 */
bool EspNowSendRaw(const uint8_t *data, uint8_t len) {
  if (!EspNowData.initialized) { return false; }
  if (len == 0 || len > ESPNOW_MAX_PAYLOAD) { return false; }

  esp_err_t result = esp_now_send(EspNowData.broadcast, data, len);
  if (result != ESP_OK) {
    AddLog(LOG_LEVEL_DEBUG, PSTR("ENW: Send error %d"), result);
    return false;
  }
  return true;
}

/*********************************************************************************************\
 * Queue processing — called every 50ms from main loop
 * Fires Berry callback for each received packet
\*********************************************************************************************/

void EspNowProcessQueue(void) {
  while (EspNowData.q_count > 0) {
    espnow_packet_t *pkt = &EspNowData.queue[EspNowData.q_tail];

    // Log for debug
    char src_str[18];
    ToHex_P(pkt->src, 6, src_str, sizeof(src_str), ':');
    AddLog(LOG_LEVEL_DEBUG, PSTR("ENW: Rcvd %d bytes from %s"), pkt->len, src_str);

    // Call Berry callback if registered: espnow_on_receive(src_mac_str, data_bytes)
    // Berry function name: "espnow_on_receive"
    // We call it via the Tasmota Berry dispatch mechanism
#ifdef USE_BERRY
    char src_hex[13];  // 6 bytes = 12 hex chars + null
    ToHex_P(pkt->src, 6, src_hex, sizeof(src_hex), 0);

    char data_hex[ESPNOW_MAX_PAYLOAD * 2 + 1];
    ToHex_P(pkt->data, pkt->len, data_hex, sizeof(data_hex), 0);

    // Build a JSON event for Berry rules: {"EspNow":{"src":"AABBCCDDEEFF","data":"48656C6C6F"}}
    Response_P(PSTR("{\"EspNow\":{\"src\":\"%s\",\"data\":\"%s\"}}"), src_hex, data_hex);
    XdrvRulesProcess(0);
#endif

    // Advance tail
    EspNowData.q_tail  = (EspNowData.q_tail + 1) % ESPNOW_QUEUE_SIZE;
    EspNowData.q_count--;
  }
}

/*********************************************************************************************\
 * Tasmota command: EspNowSend <hex_string>
 * Example: EspNowSend 48656C6C6F  → sends "Hello"
\*********************************************************************************************/

void CmndEspNowSend(void) {
  if (!EspNowData.initialized) {
    ResponseCmndChar("Not initialized - call EspNowInit first");
    return;
  }

  if (XdrvMailbox.data_len < 2) {
    ResponseCmndChar("Usage: EspNowSend <hex>");
    return;
  }

  // Decode hex string to bytes
  uint8_t  buf[ESPNOW_MAX_PAYLOAD];
  uint32_t hex_len = XdrvMailbox.data_len;
  if (hex_len > ESPNOW_MAX_PAYLOAD * 2) { hex_len = ESPNOW_MAX_PAYLOAD * 2; }

  uint32_t byte_len = 0;
  const char *hex = XdrvMailbox.data;
  for (uint32_t i = 0; i + 1 < hex_len; i += 2) {
    uint8_t hi = CharToNum(hex[i]);
    uint8_t lo = CharToNum(hex[i+1]);
    if (hi > 15 || lo > 15) { break; }
    buf[byte_len++] = (hi << 4) | lo;
  }

  if (byte_len == 0) {
    ResponseCmndChar("Invalid hex");
    return;
  }

  bool ok = EspNowSendRaw(buf, byte_len);
  ResponseCmndChar(ok ? "Sent" : "Error");
}

void CmndEspNowInit(void) {
  bool ok = EspNowInit();
  ResponseCmndChar(ok ? "OK" : "Failed");
}

/*********************************************************************************************\
 * Driver interface
\*********************************************************************************************/

const char kEspNowCommands[] PROGMEM = "EspNow|"
  "Init|Send";

void (* const EspNowCommand[])(void) PROGMEM = {
  &CmndEspNowInit,
  &CmndEspNowSend
};

bool Xdrv99(uint32_t function) {
  bool result = false;
  switch (function) {
    case FUNC_PRE_INIT:
      memset(&EspNowData, 0, sizeof(EspNowData));
      AddLog(LOG_LEVEL_DEBUG, PSTR("ENW: Driver loaded"));
      break;
    case FUNC_COMMAND:
      result = DecodeCommand(kEspNowCommands, EspNowCommand);
      break;
    case FUNC_LOOP: {
      // Process queue every 50ms
      static uint32_t last_ms = 0;
      if (TimeReached(last_ms)) {
        SetNextTimeInterval(last_ms, 50);
        if (EspNowData.initialized) {
          EspNowProcessQueue();
        }
      }
      break;
    }
  }
  return result;
}

#endif  // ESP32
#endif  // USE_ESPNOW
