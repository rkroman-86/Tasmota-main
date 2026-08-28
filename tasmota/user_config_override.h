#ifndef _USER_CONFIG_OVERRIDE_H_
#define _USER_CONFIG_OVERRIDE_H_

/*
  Webradio LVGL - Waveshare ESP32-S3-Touch-LCD-2 (8MB flash, PSRAM OPI)
  Webradio streamer WITH display: I2S audio + Universal Display/Touch + LVGL/OpenHASP.
  Screen and I2S pin mapping are provided at runtime by the Tasmota template and
  the UDisplay descriptor - NOT hard-coded here.
*/

// --- Buses ---
#define USE_I2C
#define USE_SPI

// --- Display + touch (mapping comes from the template / UDisplay descriptor) ---
#define USE_DISPLAY
#define USE_UNIVERSAL_DISPLAY
#define USE_UNIVERSAL_TOUCH

// --- LVGL UI (OpenHASP layer on top of LVGL) ---
#define USE_LVGL
#define USE_LVGL_OPENHASP

// --- Audio (webradio streamer) ---
#define USE_I2S_AUDIO
#define USE_I2S_WEBRADIO
#define USE_I2S_MP3

// --- Decoders intentionally NOT enabled (flash budget / not needed) ---
// #define USE_I2S_AAC
// #define USE_I2S_OPUS

// --- Optional: webradio hexdump diagnostic. Leave commented for production. ---
// #define WR_DEBUG_DUMP

#endif  // _USER_CONFIG_OVERRIDE_H_
