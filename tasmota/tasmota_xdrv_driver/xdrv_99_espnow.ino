// xdrv_99_espnow.ino
#ifdef USE_ESPNOW
#ifdef ESP32

#include <esp_now.h>
#include <esp_wifi.h>

#define XDRV_99 99
#define ESPNOW_CHANNEL_DEFAULT 6
#define ESPNOW_CHANNEL_FILE "/espnow_ch.dat"
#define ESPNOW_INIT_DELAY 15  // Attendre 15s avant init sans routeur

typedef struct {
  char cmd;
} ESPNowPacket;

#ifndef ESP_NOW_RECV_INFO_DEFINED
typedef struct esp_now_recv_info esp_now_recv_info_t;
#endif

bool espnow_initialized = false;
uint8_t espnow_channel = ESPNOW_CHANNEL_DEFAULT;
uint8_t espnow_broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
uint32_t espnow_uptime_init = 0;  // uptime au moment du premier appel

void ESPNow_SaveChannel(uint8_t channel)
{
  File f = LittleFS.open(ESPNOW_CHANNEL_FILE, "w");
  if (f) { f.write(channel); f.close(); }
}

uint8_t ESPNow_LoadChannel(void)
{
  if (!LittleFS.exists(ESPNOW_CHANNEL_FILE)) return 0;
  File f = LittleFS.open(ESPNOW_CHANNEL_FILE, "r");
  if (!f) return 0;
  uint8_t ch = f.read();
  f.close();
  return (ch >= 1 && ch <= 13) ? ch : 0;
}

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
  if (XdrvMailbox.data_len < 1) return;

  ESPNowPacket pkt;
  pkt.cmd = XdrvMailbox.data[0];

  if (!esp_now_is_peer_exist(espnow_broadcast)) {
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, espnow_broadcast, 6);
    peer.channel = espnow_channel;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
  }

  esp_err_t result = esp_now_send(espnow_broadcast, (uint8_t*)&pkt, sizeof(pkt));
  if (result == ESP_OK) {
    AddLog(LOG_LEVEL_INFO, PSTR("ESP-NOW: envoye '%c' canal %d"), pkt.cmd, espnow_channel);
    Response_P(PSTR("{\"ESPNowSend\":\"OK\",\"Cmd\":\"%c\"}"), pkt.cmd);
  } else {
    AddLog(LOG_LEVEL_ERROR, PSTR("ESP-NOW: send error %d"), result);
    Response_P(PSTR("{\"ESPNowSend\":\"ERROR\"}"));
  }
}

void ESPNow_Init(void)
{
  if (espnow_initialized) return;

  if (WifiHasIP()) {
    // Connecté au routeur — lire et sauvegarder son canal
    uint8_t primary;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);
    espnow_channel = primary;
    ESPNow_SaveChannel(espnow_channel);
    AddLog(LOG_LEVEL_INFO, PSTR("ESP-NOW: canal routeur %d sauvegardé"), espnow_channel);

  } else {
    // Sans routeur — attendre ESPNOW_INIT_DELAY secondes
    // pour laisser le WiFi passer en AP mode proprement
    if (espnow_uptime_init == 0) {
      espnow_uptime_init = TasmotaGlobal.uptime;
      return;
    }
    if ((TasmotaGlobal.uptime - espnow_uptime_init) < ESPNOW_INIT_DELAY) return;

    uint8_t saved = ESPNow_LoadChannel();
    if (saved > 0) {
      espnow_channel = saved;
      AddLog(LOG_LEVEL_INFO, PSTR("ESP-NOW: canal sauvegardé %d"), espnow_channel);
    } else {
      espnow_channel = ESPNOW_CHANNEL_DEFAULT;
      AddLog(LOG_LEVEL_INFO, PSTR("ESP-NOW: canal par defaut %d"), espnow_channel);
    }

    esp_err_t ch_err = esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
    if (ch_err != ESP_OK) {
      AddLog(LOG_LEVEL_ERROR, PSTR("ESP-NOW: set channel failed %d, retry..."), ch_err);
      return;  // retry à la prochaine seconde
    }
  }

  esp_now_deinit();
  if (esp_now_init() != ESP_OK) {
    AddLog(LOG_LEVEL_ERROR, PSTR("ESP-NOW: init failed"));
    return;
  }

  esp_now_register_recv_cb(ESPNow_OnReceive);
  espnow_initialized = true;
  AddLog(LOG_LEVEL_INFO, PSTR("ESP-NOW: ready canal %d"), espnow_channel);
}

const char kESPNowCommands[] PROGMEM = "|ESPNowSend";
void (* const ESPNowCommand[])(void) PROGMEM = { &ESPNow_Send };

bool Xdrv99(uint32_t function)
{
  switch (function) {
    case FUNC_NETWORK_UP:
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
