// xdrv_99_espnow.ino
#ifdef USE_ESPNOW
#ifdef ESP32

#include <esp_now.h>
#include <esp_wifi.h>

#define XDRV_99 99
#define ESPNOW_CHANNEL 6

typedef struct {
  char cmd;
} ESPNowPacket;

#ifndef ESP_NOW_RECV_INFO_DEFINED
typedef struct esp_now_recv_info esp_now_recv_info_t;
#endif

bool espnow_initialized = false;
uint8_t espnow_broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

void ESPNow_OnReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
  if (len != sizeof(ESPNowPacket)) return;
  ESPNowPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));
  uint8_t *mac = info->src_addr;
  AddLog(LOG_LEVEL_INFO, PSTR("ESP-NOW: '%c' de %02X:%02X:%02X:%02X:%02X:%02X"),
    pkt.cmd, mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  Response_P(PSTR("{\"ESPNow\":{\"Cmd\":\"%c\"}}"), pkt.cmd);
  MqttPublishPrefixTopicRulesProcess_P(RESULT_OR_TELE, PSTR("ESPNow"));
}

void ESPNow_Send(void)
{
  if (!espnow_initialized) {
    AddLog(LOG_LEVEL_ERROR, PSTR("ESP-NOW: pas encore initialisé"));
    return;
  }
  if (XdrvMailbox.data_len < 1) {
    AddLog(LOG_LEVEL_ERROR, PSTR("ESP-NOW: usage: ESPNowSend N|F"));
    return;
  }

  ESPNowPacket pkt;
  pkt.cmd = XdrvMailbox.data[0];

  if (!esp_now_is_peer_exist(espnow_broadcast)) {
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, espnow_broadcast, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
  }

  esp_err_t result = esp_now_send(espnow_broadcast, (uint8_t*)&pkt, sizeof(pkt));
  if (result == ESP_OK) {
    AddLog(LOG_LEVEL_INFO, PSTR("ESP-NOW: envoye '%c' canal %d"), pkt.cmd, ESPNOW_CHANNEL);
    Response_P(PSTR("{\"ESPNowSend\":\"OK\",\"Cmd\":\"%c\"}"), pkt.cmd);
  } else {
    AddLog(LOG_LEVEL_ERROR, PSTR("ESP-NOW: send error %d"), result);
    Response_P(PSTR("{\"ESPNowSend\":\"ERROR\"}"));
  }
}

void ESPNow_Init(void)
{
  if (espnow_initialized) return;

  // Attendre que le WiFi soit démarré (connecté OU en AP mode)
  if (WifiHasIP() == false && WifiIsInManagerMode() == false) return;

  // Forcer le canal après que le WiFi soit actif
  esp_err_t ch_err = esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (ch_err != ESP_OK) {
    AddLog(LOG_LEVEL_ERROR, PSTR("ESP-NOW: set channel failed %d"), ch_err);
    return;
  }

  esp_now_deinit();
  if (esp_now_init() != ESP_OK) {
    AddLog(LOG_LEVEL_ERROR, PSTR("ESP-NOW: init failed"));
    return;
  }

  esp_now_register_recv_cb(ESPNow_OnReceive);
  espnow_initialized = true;
  AddLog(LOG_LEVEL_INFO, PSTR("ESP-NOW: ready canal %d"), ESPNOW_CHANNEL);
}

const char kESPNowCommands[] PROGMEM = "|ESPNowSend";
void (* const ESPNowCommand[])(void) PROGMEM = { &ESPNow_Send };

bool Xdrv99(uint32_t function)
{
  switch (function) {
    case FUNC_EVERY_SECOND:
      ESPNow_Init();
      break;
    case FUNC_COMMAND:
      return DecodeCommand(kESPNowCommands, ESPNowCommand);
  }
  return false;
}

#endif // ESP32
#endif // USE_ESPNOW
