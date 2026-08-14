/*
  xdrv_42_i2s_audio.ino - Audio dac support for Tasmota

  Copyright (C) 2021  Gerhard Mutz and Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#if defined(ESP32) && ESP_IDF_VERSION_MAJOR >= 5
#if defined(USE_I2S_AUDIO) && defined(USE_I2S_WEBRADIO)

void I2sMDCallback(void *cbData, const char *type, bool isUnicode, const char *str) {
  const char *ptr = reinterpret_cast<const char *>(cbData);
  (void) isUnicode; // Punt this ball for now
  (void) ptr;
  if (strstr_P(type, PSTR("Title"))) {
    strncpy(audio_i2s_mp3.audio_title, str, sizeof(audio_i2s_mp3.audio_title));
    audio_i2s_mp3.audio_title[sizeof(audio_i2s_mp3.audio_title)-1] = 0;
  } else {
    // Who knows what to do?  Not me!
  }
}

void I2SWrStatusCB(void *cbData, int code, const char *str){
  AddLog(LOG_LEVEL_INFO, "I2S: status: %s",str);
}

// RK modif
// --- Webradio buffering tuning --- 
static const uint32_t WR_PREFILL_MS       = 4000;   // temps max pour prébuffer
static const uint32_t WR_PREFILL_STEP_MS  = 50;
static const uint32_t WR_START_THRESHOLD  = 64 * 1024;  // démarrer decode à partir de 64k
static const uint32_t WR_LOW_WATERMARK    = 16 * 1024;  // (future) si on veut gérer “pause decode”
//end RK modif

//RK modif
static void I2S_DumpFirstBytes(const char *url, uint32_t n = 64) {
  AudioFileSourceICYStream *t = new AudioFileSourceICYStream();
  if (!t) return;

  t->SetReconnect(1, 1);
  if (!t->open(url)) {
    AddLog(LOG_LEVEL_INFO, "I2S: dump open failed");
    delete t;
    return;
  }

  uint8_t b[96];
  if (n > sizeof(b)) n = sizeof(b);

  int r = t->read(b, n);
  t->close();
  delete t;

  if (r <= 0) {
    AddLog(LOG_LEVEL_INFO, "I2S: dump read=0");
    return;
  }

  // Hex + ASCII lisible
  char line[3*96 + 1];
  char asc[96 + 1];
  uint32_t i;
  for (i = 0; i < (uint32_t)r; i++) {
    sprintf(&line[i*3], "%02X ", b[i]);
    asc[i] = (b[i] >= 32 && b[i] <= 126) ? (char)b[i] : '.';
  }
  line[i*3] = 0;
  asc[i] = 0;

  AddLog(LOG_LEVEL_INFO, "I2S: first bytes HEX: %s", line);
  AddLog(LOG_LEVEL_INFO, "I2S: first bytes TXT: %s", asc);
}
//end RK modif


// RK modif
// Reconnect behaviour differs by use case:
//  - Webradio: streams can drop briefly; retry a few times to ride through glitches.
//  - DLNA:     the control point deliberately closes the stream to advance tracks,
//              so retrying would delay the "Ended" event and stutter the gap. Use 0,0
//              for a clean, immediate handover to the next track.
// The mode is selected by the command index (see CmndI2SWebRadio):
//   I2SWR / I2SWR<n> -> WR_MODE_RADIO (5,5)
//   I2SWR2           -> WR_MODE_DLNA  (0,0)
enum WR_Mode { WR_MODE_RADIO = 0, WR_MODE_DLNA = 1 };
// end RK modif

// RK modif: added wr_mode parameter (was: I2SWebradio(const char*, uint32_t))
bool I2SWebradio(const char *url, uint32_t decoder_type, uint8_t wr_mode) {
// end RK modif

  size_t wr_tasksize = 8000; // suitable for ACC and MP3
  if(decoder_type == OPUS_DECODER){ // opus needs a ton of stack
    wr_tasksize = 26000;
  }

  size_t finalBufferSize = preallocateBufferSize;
  if(CanUsePSRAM()){
    size_t targetsize = (ESP_getMaxAllocPsram()/4) * 3; // use up to 3/4 of available PSRAM
    finalBufferSize = targetsize;
  }
  // allocate buffers if not already done
  if (audio_i2s_mp3.preallocateBuffer == NULL) {
    audio_i2s_mp3.preallocateBuffer = special_malloc(finalBufferSize);
  }
  if (audio_i2s_mp3.preallocateCodec == NULL) {
    audio_i2s_mp3.preallocateCodec = special_malloc(preallocateCodecSize);
  }
  // check if we have buffers
  if (audio_i2s_mp3.preallocateBuffer == NULL || audio_i2s_mp3.preallocateCodec == NULL) {
    AddLog(LOG_LEVEL_INFO, "I2S: cannot allocate buffers");
    if (audio_i2s_mp3.preallocateBuffer != NULL) {
      free(audio_i2s_mp3.preallocateBuffer);
      audio_i2s_mp3.preallocateBuffer = NULL;
    }
    if (audio_i2s_mp3.preallocateCodec != NULL) {
      free(audio_i2s_mp3.preallocateCodec);
      audio_i2s_mp3.preallocateCodec = NULL;
    }
    return false;
  }

  Audio_webradio.ifile = new AudioFileSourceICYStream();
  // RK modif: reconnect tries depend on mode (radio = resilient, DLNA = clean handover)
  if (wr_mode == WR_MODE_DLNA) {
    Audio_webradio.ifile->SetReconnect(0, 0);   // DLNA: no retry, let playlist advance
  } else {
    Audio_webradio.ifile->SetReconnect(5, 5);   // Radio: ride through brief drops
  }
  // end RK modif
  Audio_webradio.ifile->RegisterMetadataCB(I2sMDCallback, NULL);
  Audio_webradio.ifile->RegisterStatusCB(I2SWrStatusCB, NULL);
  if(!Audio_webradio.ifile->open(url)){
    goto i2swr_fail;
  }
  

  // RK modif
    //AddLog(LOG_LEVEL_INFO, "I2S: did connect to %s",url);
    AddLog(LOG_LEVEL_INFO, "I2S: did connect to %s",url);
    I2S_DumpFirstBytes(url, 64);
  //end RK modif

  I2SAudioPower(true);
  audio_i2s_mp3.buff = new AudioFileSourceBuffer(Audio_webradio.ifile, audio_i2s_mp3.preallocateBuffer, finalBufferSize);
  if(audio_i2s_mp3.buff == nullptr){
    goto i2swr_fail;
  }
  audio_i2s_mp3.buff->RegisterStatusCB(I2sStatusCallback, NULL);


  if(I2SinitDecoder(decoder_type) == false){
    AddLog(LOG_LEVEL_DEBUG, "I2S: decoder init failed");
    goto i2swr_fail;
  }

  audio_i2s_mp3.decoder->RegisterStatusCB(I2sStatusCallback, NULL);
  if(audio_i2s_mp3.decoder->begin(audio_i2s_mp3.buff, audio_i2s.out)){
    AddLog(LOG_LEVEL_DEBUG, "I2S: decoder started");
  } else {
    goto i2swr_fail;
  }

  AddLog(LOG_LEVEL_DEBUG,PSTR("I2S: will launch webradio task with decoder type %u"), decoder_type);
  //RK modif
  xTaskCreatePinnedToCore(I2sMp3WrTask, "MP3-WR", wr_tasksize, NULL, 5, &audio_i2s_mp3.mp3_task_handle, 1);
  //xTaskCreatePinnedToCore(I2sMp3WrTask, "MP3-WR", wr_tasksize, NULL, 3, &audio_i2s_mp3.mp3_task_handle, 1);
  //end RK modif
  return true;

i2swr_fail:
    I2sStopPlaying();
    I2sWebRadioStopPlaying();
    return false;
}

void CmndI2SWebRadio(void) {
  if (I2SPrepareTx() != I2S_OK) return;

  if (XdrvMailbox.data_len > 0) {
    // RK modif: the command index selects the reconnect mode.
    //   I2SWR  <url>   (index 0/absent) -> webradio, reconnect (5,5)
    //   I2SWR2 <url>   (index 2)        -> DLNA,     reconnect (0,0)
    // Index 2 is reserved for the DLNA mode selector, so it is NOT forwarded as a
    // decoder type; both modes decode MP3 (the only format used here). Other indices
    // keep the previous behaviour of selecting the decoder type.
    uint8_t  wr_mode      = (XdrvMailbox.index == 2) ? WR_MODE_DLNA : WR_MODE_RADIO;
    uint32_t decoder_type = (XdrvMailbox.index == 2) ? MP3_DECODER  : (uint32_t)XdrvMailbox.index;

    if(I2SWebradio(XdrvMailbox.data, decoder_type, wr_mode)){
      ResponseCmndChar(XdrvMailbox.data);
    } else {
      ResponseCmndFailed();
    }
    // end RK modif
  } else {
    ResponseCmndChar_P(PSTR("Stopped"));
  }
}


void I2sWebRadioStopPlaying() {
  if(audio_i2s_mp3.decoder) {
    audio_i2s_mp3.decoder->stop();
    delete audio_i2s_mp3.decoder;
    audio_i2s_mp3.decoder = nullptr;
  }
  if (audio_i2s_mp3.buff) {
    audio_i2s_mp3.buff->close();
    delete audio_i2s_mp3.buff;
    audio_i2s_mp3.buff = NULL;
  }
  if (Audio_webradio.ifile) {
    Audio_webradio.ifile->close();
    delete Audio_webradio.ifile;
    Audio_webradio.ifile = NULL;
  }
}


#endif // defined(USE_I2S_AUDIO) && defined(USE_I2S_WEBRADIO)
#endif // defined(ESP32) && ESP_IDF_VERSION_MAJOR >= 5
