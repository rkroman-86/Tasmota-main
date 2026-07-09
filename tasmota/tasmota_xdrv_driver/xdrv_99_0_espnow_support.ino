// xdrv_99_0_espnow_support.ino
// Chargé avant xdrv_99_espnow.ino (ordre alphabétique)
// Fournit les includes ESP-NOW sans dépendre de USE_TASMESH

#ifdef USE_ESPNOW
#ifdef ESP32
#include <esp_now.h>
#include <esp_wifi.h>
#endif // ESP32
#endif // USE_ESPNOW
