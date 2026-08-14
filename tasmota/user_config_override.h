#ifndef _USER_CONFIG_OVERRIDE_H_
#define _USER_CONFIG_OVERRIDE_H_

/*
  Webradio Mini - ESP32-S3 Super Mini (4MB flash)
  Audio-only build: I2S webradio + local MP3, no display / no LVGL / no touch.
  AAC and OPUS are intentionally disabled (flash budget + Super Mini stability).
*/

// I2C (kept for optional sensors; harmless if unused)
#define USE_I2C

// --- Audio (webradio streamer) ---
#define USE_I2S_AUDIO       // core I2S audio driver
#define USE_I2S_WEBRADIO    // ICY / webradio streaming (also pulls in the WR/DLNA task)
#define USE_I2S_MP3         // local MP3 playback + DLNA renderer path

// --- Decoders intentionally NOT enabled on the Super Mini ---
// AAC  (~75 kB flash) - unstable / no room on the 4MB Super Mini
// OPUS (~25 kB flash) - not needed for the webradio use case
// #define USE_I2S_AAC
// #define USE_I2S_OPUS

// --- Optional: enable the webradio hexdump diagnostic (opens a 2nd HTTP
//     connection on each start). Leave commented for production. ---
// #define WR_DEBUG_DUMP

#endif  // _USER_CONFIG_OVERRIDE_H_