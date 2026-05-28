/*
  xdrv_99_espnow.ino - ESP-NOW broadcast send/receive for Tasmota (ESP32)

  Minimal transport layer using QuickEspNow (same lib as wizmote).
  - broadcast send (raw bytes via EspNowMeshSend <hex>)
  - receive → JSON event → Berry rules

  Enable with: #define USE_ESPNOW in user_config_override.h

  Berry usage:
    tasmota.add_rule("EspNow#data", def(val, trig, msg)
      print("src:", msg["src"], "data:", msg["data"])
    end)
    tasmota.cmd("EspNowMeshInit")
    tasmota.cmd("EspNowMeshSend 48656C6C6F")
*/

#ifdef USE_ESPNOW
#ifdef ESP32

#include "QuickEspNow.h"

#define XDRV_99_ESPNOW  99
#define XDRV_99  99

struct {
  bool initialized;
} EspNowMeshData;

/*********************************************************************************************\
 * Receive callback — appelé par QuickEspNow depuis sa tâche RX
\*********************************************************************************************/

void EspNowMeshDataReceived(uint8_t* mac, uint8_t* data, uint8_t len, signed int rssi, bool broadcast) {
  if (!EspNowMeshData.initialized) { return; }
  if (!mac || !data || len == 0) { return; }

  // Convertir MAC en hex string
  char src_hex[13];
  snprintf(src_hex, sizeof(src_hex), "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // Convertir data en hex string
  char data_hex[ESPNOW_MAX_MESSAGE_LENGTH * 2 + 1];
  for (uint8_t i = 0; i < len && i < ESPNOW_MAX_MESSAGE_LENGTH; i++) {
    snprintf(data_hex + i * 2, 3, "%02X", data[i]);
  }
  data_hex[len * 2] = '\0';

  AddLog(LOG_LEVEL_DEBUG, PSTR("ENW: Rcvd %d bytes from %s RSSI %d"), len, src_hex, rssi);

  // Générer l'event JSON pour Berry rules
  // {"EspNow":{"src":"AABBCCDDEEFF","data":"48656C6C6F","rssi":-65}}
  Response_P(PSTR("{\"EspNow\":{\"src\":\"%s\",\"data\":\"%s\",\"rssi\":%d}}"),
             src_hex, data_hex, rssi);
  XdrvRulesProcess(0);
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
 * Commandes Tasmota
\*********************************************************************************************/

// EspNowMeshInit
void CmndEspNowMeshInit(void) {
  EspNowMeshInit();
  ResponseCmndChar(EspNowMeshData.initialized ? "OK" : "Failed");
}

// EspNowMeshSend <hex>
// Exemple: EspNowMeshSend 48656C6C6F  → envoie "Hello" en broadcast
void CmndEspNowMeshSend(void) {
  if (!EspNowMeshData.initialized) {
    ResponseCmndChar("Not initialized");
    return;
  }
  if (XdrvMailbox.data_len < 2) {
    ResponseCmndChar("Usage: EspNowMeshSend <hex>");
    return;
  }

  // Décoder hex → bytes
  uint8_t buf[ESPNOW_MAX_MESSAGE_LENGTH];
  uint32_t hex_len = XdrvMailbox.data_len;
  if (hex_len > ESPNOW_MAX_MESSAGE_LENGTH * 2) { hex_len = ESPNOW_MAX_MESSAGE_LENGTH * 2; }

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
  }
  return result;
}

#endif  // ESP32
#endif  // USE_ESPNOW
