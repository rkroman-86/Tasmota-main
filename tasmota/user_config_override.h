#ifndef _USER_CONFIG_OVERRIDE_H_
#define _USER_CONFIG_OVERRIDE_H_

#define USE_ESPNOW

// Espacer les tentatives WiFi à 300s (5 min)
// Laisse ESP-NOW fonctionner entre les tentatives
#define WIFI_RETRY_SECONDS 300

#endif  // _USER_CONFIG_OVERRIDE_H_
