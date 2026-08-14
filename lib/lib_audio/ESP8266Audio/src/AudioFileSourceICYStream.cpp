/*
  AudioFileSourceICYStream
  Streaming Shoutcast ICY source
  
  Copyright (C) 2017  Earle F. Philhower, III
 
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
 
#if defined(ESP32) || defined(ESP8266)
 
#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif
 
#include "AudioFileSourceICYStream.h"
#include <string.h>
 
//RK modif
#include <ctype.h>
//end RK modif
 
AudioFileSourceICYStream::AudioFileSourceICYStream()
{
  pos = 0;
  reconnectTries = 0;
  saveURL[0] = 0;
}
 
AudioFileSourceICYStream::AudioFileSourceICYStream(const char *url)
{
  saveURL[0] = 0;
  reconnectTries = 0;
  open(url);
}
 
bool AudioFileSourceICYStream::open(const char *url)
{  
  //RK modif
  //static const char *hdr[] = { "icy-metaint", "icy-name", "icy-genre", "icy-br" };
  static const char *hdr[] = { "icy-metaint", "icy-name", "icy-genre", "icy-br", "Transfer-Encoding" };
  //end RK modif
 
  pos = 0;
  if (!http.begin(client, url)) {
    cb.st(STATUS_HTTPFAIL, PSTR("Can't connect to url"));
    return false;
  }
  http.addHeader("Icy-MetaData", "1");
 
  //RK modif
  //http.collectHeaders( hdr, 4 );
  http.collectHeaders(hdr, 5);  
  //end RK modif
 
  http.setReuse(true);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  int code = http.GET();
 
  //RK modif
  // ---- CHUNKED transfer decoding init (RK) ----
   chunked = false;
   chunkRemaining = 0;
   chunkEof = false;
 
   if (http.hasHeader("Transfer-Encoding")) {
     String te = http.header("Transfer-Encoding");
     te.toLowerCase();
     if (te.indexOf("chunked") >= 0) {
       chunked = true;
     }
   }
   // --------------------------------------------
  // end RK modif
 
  if (http.hasHeader(hdr[0])) {
    String ret = http.header(hdr[0]);
    icyMetaInt = ret.toInt();
  } else {
    icyMetaInt = 0;
  }
  if (http.hasHeader(hdr[1])) {
    String ret = http.header(hdr[1]);
//    cb.md("SiteName", false, ret.c_str());
  }
  if (http.hasHeader(hdr[2])) {
    String ret = http.header(hdr[2]);
//    cb.md("Genre", false, ret.c_str());
  }
  if (http.hasHeader(hdr[3])) {
    String ret = http.header(hdr[3]);
//    cb.md("Bitrate", false, ret.c_str());
  }
 
  icyByteCount = 0;
  size = http.getSize();
 
  // Note: saveURL is a fixed 128-byte buffer (see AudioFileSourceHTTPStream.h).
  // A longer URL is silently truncated; strncpy below always NUL-terminates so a
  // truncated reconnect URL fails cleanly rather than reading out of bounds.
  // (No AddLog here: this file is the vendored ESP8266Audio lib and does not have
  // Tasmota's logging symbols in scope.)
  strncpy(saveURL, url, sizeof(saveURL));
  saveURL[sizeof(saveURL)-1] = 0;
  return true;
}
 
AudioFileSourceICYStream::~AudioFileSourceICYStream()
{
  http.end();
}
 
// RK modif
bool AudioFileSourceICYStream::readByte(uint8_t &b, bool nonBlock) {
  WiFiClient *stream = http.getStreamPtr();
  if (!stream) return false;
 
  if (!nonBlock) {
    //RK fix: millis() is uint32_t - storing in int breaks the delta after ~24 days uptime
    uint32_t start = millis();
    while (stream->available() <= 0 && (millis() - start < 500)) yield();
  }
  if (stream->available() <= 0) return false;
 
  int r = stream->read(&b, 1);
  return (r == 1);
}
 
bool AudioFileSourceICYStream::skipCRLF(bool nonBlock) {
  uint8_t b;
  if (!readByte(b, nonBlock)) return false;
  //RK fix: be strict about CRLF framing. A desync of one byte here silently
  //eats audio payload and produces intermittent clicks that are very hard to
  //trace. Consume exactly \r\n (or a bare \n); anything else is a framing error.
  if (b == '\r') {
    if (!readByte(b, nonBlock)) return false;
    return (b == '\n');
  }
  if (b == '\n') return true;
  //RK fix: unexpected byte where CRLF was expected -> report framing error
  return false;
}
 
bool AudioFileSourceICYStream::readChunkSizeLine(uint32_t &outSize, bool nonBlock) {
  char line[32];
  uint32_t idx = 0;
  uint8_t b;
 
  while (idx < sizeof(line) - 1) {
    if (!readByte(b, nonBlock)) return false;
    if (b == '\n') break;
    if (b == '\r') continue;
    line[idx++] = (char)b;
  }
  line[idx] = 0;
 
  // strip extensions after ';'
  char *semi = strchr(line, ';');
  if (semi) *semi = 0;
 
  uint32_t val = 0;
  for (uint32_t i = 0; line[i]; i++) {
    char c = line[i];
    if (!isxdigit((unsigned char)c)) break;
    val <<= 4;
    if (c >= '0' && c <= '9') val |= (c - '0');
    else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
  }
  outSize = val;
  return true;
}
 
int AudioFileSourceICYStream::readChunked(uint8_t *dst, uint32_t len, bool nonBlock) {
  if (chunkEof) return 0;
 
  // get chunk size if needed
  while (chunkRemaining == 0) {
    uint32_t cs = 0;
    if (!readChunkSizeLine(cs, nonBlock)) return 0;
 
    if (cs == 0) {
      // final chunk: consume CRLF and stop
      skipCRLF(nonBlock);
      chunkEof = true;
      return 0;
    }
    chunkRemaining = (int32_t)cs;
  }
 
  uint32_t toRead = len;
  if (toRead > (uint32_t)chunkRemaining) toRead = (uint32_t)chunkRemaining;
 
  WiFiClient *stream = http.getStreamPtr();
  if (!stream) return 0;
 
  if (!nonBlock) {
    //RK fix: millis() overflow (see readByte)
    uint32_t start = millis();
    while ((uint32_t)stream->available() < toRead && (millis() - start < 500)) yield();
  }
 
  size_t avail = stream->available();
  if (avail == 0) return 0;
  if (avail < toRead) toRead = avail;
 
  int r = stream->read(dst, toRead);
  if (r < 0) r = 0;
 
  chunkRemaining -= r;
 
  if (chunkRemaining == 0) {
    // consume CRLF after chunk data
    skipCRLF(nonBlock);
  }
 
  return r;
}
// end RK modif
 
 
uint32_t AudioFileSourceICYStream::readInternal(void *data, uint32_t len, bool nonBlock)
{
  // Ensure we can't possibly read 2 ICY headers in a single go #355
  if (icyMetaInt > 1) {
    len = std::min((int)(icyMetaInt >> 1), (int)len);
  }
retry:
  if (!http.connected()) {
    cb.st(STATUS_DISCONNECTED, PSTR("Stream disconnected"));
    http.end();
    for (int i = 0; i < reconnectTries; i++) {
      char buff[64];
      sprintf_P(buff, PSTR("Attempting to reconnect, try %d"), i);
      cb.st(STATUS_RECONNECTING, buff);
      delay(reconnectDelayMs);
      if (open(saveURL)) {
        cb.st(STATUS_RECONNECTED, PSTR("Stream reconnected"));
        break;
      }
    }
    if (!http.connected()) {
      cb.st(STATUS_DISCONNECTED, PSTR("Unable to reconnect"));
      return 0;
    }
  }
  if ((size > 0) && (pos >= size)) return 0;
 
  WiFiClient *stream = http.getStreamPtr();
 
  // RK modif
     auto bodyRead = [&](uint8_t *dst, uint32_t l, bool nb) -> int {
     if (!chunked) {
       int r = stream->read(dst, l);
       if (r < 0) r = 0;
       return r;
     }
     return readChunked(dst, l, nb);
   };
  // end RK modif
 
  // Can't read past EOF...
 
  // RK modif
  //if ( (size > 0) && (len > (uint32_t)(pos - size)) ) len = pos - size;
  //RK fix: guard pos < size before subtracting. len is uint32_t, so if
  //(size - pos) were ever negative it would wrap to a huge value. size is -1
  //for infinite radio streams (branch skipped), but this keeps the clamp safe
  //for finite sources too.
  if ((size > 0) && (pos < size) && (pos + (int)len > size)) len = size - pos;
  // end RK modif
 
  if (!nonBlock) {
    //RK fix: millis() overflow
    uint32_t start = millis();
    while ((stream->available() < (int)len) && (millis() - start < 500)) yield();
  }
 
  size_t avail = stream->available();
  if (!nonBlock && !avail) {
    cb.st(STATUS_NODATA, PSTR("No stream data available"));
    http.end();
    goto retry;
  }
  if (avail == 0) return 0;
  if (avail < len) len = avail;
 
  int read = 0;
  int ret = 0;
  // If the read would hit an ICY block, split it up...
  if (((int)(icyByteCount + len) > (int)icyMetaInt) && (icyMetaInt > 0)) {
    int beforeIcy = icyMetaInt - icyByteCount;
    if (beforeIcy > 0) {
      //RK modif
         ret = bodyRead(reinterpret_cast<uint8_t*>(data), beforeIcy, nonBlock);
         read += ret;
         pos += ret;
         len -= ret;
         data = (void *)(reinterpret_cast<char*>(data) + ret);
         icyByteCount += ret;
         if (ret != beforeIcy) return read; // Partial read
      //ret = stream->read(reinterpret_cast<uint8_t*>(data), beforeIcy);
      //if (ret < 0) ret = 0;
      //read += ret;
      //pos += ret;
      //len -= ret;
      //data = (void *)(reinterpret_cast<char*>(data) + ret);
      //icyByteCount += ret;
      //if (ret != beforeIcy) return read; // Partial read
      // end RK modif
    }
 
    // ICY MD handling
    int mdSize;
    uint8_t c;
 
    // RK modif
    //int mdret = stream->read(&c, 1);
    int mdret = bodyRead(&c, 1, nonBlock);
    // end RK modif
 
    if (mdret==0) return read;
    mdSize = c * 16;
    if ((mdret == 1) && (mdSize > 0)) {
      // This is going to get ugly fast.
      char icyBuff[256 + 16 + 1];
      char *readInto = icyBuff + 16;
      memset(icyBuff, 0, 16); // Ensure no residual matches occur
      while (mdSize) {
        int toRead = mdSize > 256 ? 256 : mdSize;
 
        // RK modif
        //int ret = stream->read((uint8_t*)readInto, toRead);
        //if (ret < 0) return read;
        //if (ret == 0) { delay(1); continue; }
        int ret = bodyRead((uint8_t*)readInto, toRead, nonBlock);
        if (ret == 0) { delay(1); continue; }
        // end RK modif
 
        mdSize -= ret;
        // At this point we have 0...15 = last 15 chars read from prior read plus new data
        int end = 16 + ret; // The last byte of valid data
        char *header = (char *)memmem((void*)icyBuff, end, (void*)"StreamTitle=", 12);
        if (!header) {
          // No match, so move the last 16 bytes back to the start and continue
          memmove(icyBuff, icyBuff+end-16, 16);
          delay(1);
	  continue;
        }
        // Found header, now move it to the front
        int lastValidByte = end - (header -icyBuff) + 1;
        memmove(icyBuff, header, lastValidByte);
        // Now fill the buffer to the end with read data
        while (mdSize && lastValidByte < 255) {
          int toRead = mdSize > (256 - lastValidByte) ? (256 - lastValidByte) : mdSize;
 
          // RK modif
          //ret = stream->read((uint8_t*)icyBuff + lastValidByte, toRead);
          //if (ret==-1) return read; // error
          //if (ret == 0) { delay(1); continue; }
          ret = bodyRead((uint8_t*)icyBuff + lastValidByte, toRead, nonBlock);
          if (ret == 0) { delay(1); continue; }
          // end RK modif
 
          mdSize -= ret;
          lastValidByte += ret;
        }
        // Buffer now contains StreamTitle=....., parse it
        char *p = icyBuff+12;
        if (*p=='\'' || *p== '"' ) {
          char closing[] = { *p, ';', '\0' };
          char *psz = strstr( p+1, closing );
          if( !psz ) psz = strchr( &icyBuff[13], ';' );
          if( psz ) *psz = '\0';
          p++;
        } else {
          char *psz = strchr( p, ';' );
          if( psz ) *psz = '\0';
        }
        cb.md("StreamTitle", false, p);
 
        // Now skip rest of MD block
        while (mdSize) {
          int toRead = mdSize > 256 ? 256 : mdSize;
 
          // RK modif
          //ret = stream->read((uint8_t*)icyBuff, toRead);
          //if (ret < 0) return read;
          //if (ret == 0) { delay(1); continue; }
           ret = bodyRead((uint8_t*)icyBuff, toRead, nonBlock);
           if (ret == 0) { delay(1); continue; }
          // end RK modif
 
          mdSize -= ret;
        }
      }
    }
    icyByteCount = 0;
  }
 
 
  // RK modif
  //ret = stream->read(reinterpret_cast<uint8_t*>(data), len);
  //if (ret < 0) ret = 0;
  //read += ret;
  //pos += ret;
  //icyByteCount += ret;
  //return read;
  ret = bodyRead(reinterpret_cast<uint8_t*>(data), len, nonBlock);
  read += ret;
  pos += ret;
  icyByteCount += ret;
  return read;
  // end RK modif
}
 
#endif