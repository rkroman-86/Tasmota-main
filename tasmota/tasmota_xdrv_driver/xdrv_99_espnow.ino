/*
  xdrv_99_espnow.ino - ESP-NOW broadcast send/receive for Tasmota (ESP32)
  Uses QuickEspNow library (same as wizmote driver).
  Enable with: #define USE_ESPNOW in user_config_override.h
*/

#ifdef USE_ESPNOW
#ifdef ESP32

#include "QuickEspNow.h"

#define XDRV_99_ESPNOW  99
#define XDRV_99         99

#define ENMESH_QUEUE_SIZE     8
#define ENMESH_MAX_PAYLOAD    250

struct enmesh_packet_t {
  uint8_t  src[6];
  uint8_t  data[ENMESH_MAX_PAYLOAD];
  uint8_t  len;
  int8_t   rssi;
};

struct {
  enmesh_packet_t queue[ENMESH_QUEUE_SIZE];
  uint8_t  q_head;
  uint8_t  q_tail;
  uint8_t  q_count;
  bool     initialized;
} EspNowMeshData;

/*********************************************************************************************\
 * Callback RX — tâche FreeRTOS QuickEspNow → push queue seulement
\*********************************************************************************************/

void EspNowMeshDataReceived(uint8_t* mac, uint8_t* data, uint8_t len, signed int rssi, bool broadcast) {
  if (!EspNowMeshData.initialized) { return; }
  if (!mac || !data || len == 0 || len > ENMESH_MAX_PAYLOAD) { return; }
  if (EspNowMeshData.q_count >= ENMESH_QUEUE_SIZE) { return; } // drop si plein

  enmesh_packet_t *slot = &EspNowMeshData.queue[EspNowMeshData.q_head];
  memcpy(slot->src,  mac,  6);
  memcpy(slot->data, data, len);
  slot->len  = len;
  slot->rssi = (int8_t)rssi;

  EspNowMeshData.q_head = (EspNowMeshData.q_head + 1) % ENMESH_QUEUE_SIZE;
  EspNowMeshData.q_count++;
}

/*********************************************************************************************\
 * Traitement queue — appelé depuis FUNC_LOOP (contexte Tasmota main loop)
\*********************************************************************************************/

void EspNowMeshProcessQueue(void) {
  while (EspNowMeshData.q_count > 0) {
    enmesh_packet_t *pkt = &EspNowMeshData.queue[EspNowMeshData.q_tail];

    char src_hex[13];
    snprintf(src_hex, sizeof(src_hex), "%02X%02X%02X%02X%02X%02X",
             pkt->src[0], pkt->src[1], pkt->src[2],
             pkt->src[3], pkt->src[4], pkt->src[5]);

    char data_hex[ENMESH_MAX_PAYLOAD * 2 + 1];
    for (uint8_t i = 0; i < pkt->len; i++) {
      snprintf(data_hex + i * 2, 3, "%02X", pkt->data[i]);
    }
    data_hex[pkt->len * 2] = '\0';

    AddLog(LOG_LEVEL_DEBUG, PSTR("ENW: Rcvd %d bytes from %s RSSI %d"),
           pkt->len, src_hex, pkt->rssi);

    // Déclencher les règles Berry depuis le main loop — thread-safe
    Response_P(PSTR("{\"EspNow\":{\"src\":\"%s\",\"data\":\"%s\",\"rssi\":%d}}"),
               src_hex, data_hex, pkt->rssi);
    XdrvRulesProcess(0);

    EspNowMeshData.q_tail  = (EspNowMeshData.q_tail + 1) % ENMESH_QUEUE_SIZE;
    EspNowMeshData.q_count--;
  }
}

/*********************************************************************************************\
 * Init
\*********************************************************************************************/

void EspNowMeshInit(void) {
  if (EspNowMeshData.initialized) {
    AddLog(LOG_LEVEL_INFO, PSTR("ENW: Already initialized"));
    return;
  }
  if (quickEspNow.begin()) {
    quickEspNow.onDataRcvd(EspNowMeshDataReceived);
    EspNowMeshData.initialized = true;
    AddLog(LOG_LEVEL_INFO, PSTR("ENW: Started on channel %d"), WiFi.channel());
  } else {
    AddLog(LOG_LEVEL_ERROR, PSTR("ENW: begin() failed"));
  }
}

/*********************************************************************************************\
 * Commandes
\*********************************************************************************************/

void CmndEspNowMeshInit(void) {
  EspNowMeshInit();
  ResponseCmndChar(EspNowMeshData.initialized ? "OK" : "Failed");
}

void CmndEspNowMeshSend(void) {
  if (!EspNowMeshData.initialized) {
    ResponseCmndChar("Not initialized");
    return;
  }
  if (XdrvMailbox.data_len < 2) {
    ResponseCmndChar("Usage: EspNowMeshSend <hex>");
    return;
  }

  uint8_t buf[ENMESH_MAX_PAYLOAD];
  uint32_t hex_len = XdrvMailbox.data_len;
  if (hex_len > ENMESH_MAX_PAYLOAD * 2) { hex_len = ENMESH_MAX_PAYLOAD * 2; }

  auto hexNibble = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0xFF;
  };

  uint32_t byte_len = 0;
  const char* hex = XdrvMailbox.data;
  for (uint32_t i = 0; i + 1 < hex_len; i += 2) {
    uint8_t hi = hexNibble(hex[i]);
    uint8_t lo = hexNibble(hex[i + 1]);
    if (hi > 15 || lo > 15) { break; }
    buf[byte_len++] = (hi << 4) | lo;
  }

  if (byte_len == 0) {
    ResponseCmndChar("Invalid hex");
    return;
  }

  comms_send_error_t err = quickEspNow.sendBcast(buf, byte_len);
  ResponseCmndChar(err == COMMS_SEND_OK ? "Sent" : "Error");
}

/*********************************************************************************************\
 * Driver interface
\*********************************************************************************************/

const char kEspNowMeshCommands[] PROGMEM = "EspNowMesh|"
  "Init|Send";

void (* const EspNowMeshCommand[])(void) PROGMEM = {
  &CmndEspNowMeshInit,
  &CmndEspNowMeshSend
};

bool Xdrv99(uint32_t function) {
  bool result = false;
  switch (function) {
    case FUNC_PRE_INIT:
      memset(&EspNowMeshData, 0, sizeof(EspNowMeshData));
      break;
    case FUNC_COMMAND:
      result = DecodeCommand(kEspNowMeshCommands, EspNowMeshCommand);
      break;
    case FUNC_LOOP:
      if (EspNowMeshData.initialized && EspNowMeshData.q_count > 0) {
        EspNowMeshProcessQueue();
      }
      break;
  }
  return result;
}

#endif  // ESP32
#endif  // USE_ESPNOW
