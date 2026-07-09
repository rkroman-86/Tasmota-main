// xdrv_99_espnow.ino

#ifdef USE_ESPNOW
#ifdef ESP32

#include <esp_now.h>
#include <esp_wifi.h>

#define XDRV_99 99

typedef struct {
  char cmd;
} ESPNowPacket;

// Forward declaration explicite du type si manquant
#ifndef ESP_NOW_RECV_INFO_DEFINED
typedef struct esp_now_recv_info esp_now_recv_info_t;
#endif

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

void ESPNow_Init(void)
{
  uint8_t primary;
  wifi_second_chan_t second;
  esp_wifi_get_channel(&primary, &second);
  esp_now_deinit();
  if (esp_now_init() != ESP_OK) {
    AddLog(LOG_LEVEL_ERROR, PSTR("ESP-NOW: init failed"));
    return;
  }
  esp_now_register_recv_cb(ESPNow_OnReceive);
  AddLog(LOG_LEVEL_INFO, PSTR("ESP-NOW: ready canal %d"), primary);
}

bool Xdrv99(uint32_t function)
{
  switch (function) {
    case FUNC_INIT:
      ESPNow_Init();
      break;
  }
  return false;
}

#endif // ESP32
#endif // USE_ESPNOW
