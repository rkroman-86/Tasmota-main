/*
  AudioFileSourceHTTPStream
  Connect to a HTTP based streaming service
  
  Copyright (C) 2017  Earle F. Philhower, III
*/

#if defined(ESP32) || defined(ESP8266)
#pragma once

#include <Arduino.h>
#ifdef ESP32
  #include <HTTPClient.h>
  #include <WiFi.h>
#else
  #include <ESP8266HTTPClient.h>
#endif

#include "AudioFileSourceHTTPStream.h"

class AudioFileSourceICYStream : public AudioFileSourceHTTPStream
{
  public:
    AudioFileSourceICYStream();
    AudioFileSourceICYStream(const char *url);
    virtual ~AudioFileSourceICYStream() override;
    
    virtual bool open(const char *url) override;

  private:
    virtual uint32_t readInternal(void *data, uint32_t len, bool nonBlock) override;
    int icyMetaInt;
    int icyByteCount;

    // ---- CHUNKED transfer decoding (RK) ----
    bool chunked = false;
    int32_t chunkRemaining = 0;
    bool chunkEof = false;

    int  readChunked(uint8_t *dst, uint32_t len, bool nonBlock);
    bool readChunkSizeLine(uint32_t &outSize, bool nonBlock);
    bool readByte(uint8_t &b, bool nonBlock);
    bool skipCRLF(bool nonBlock);
    // ---------------------------------------
};

#endif