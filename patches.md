# PATCHES — Webradio Mini fork

This document lists every change made on top of upstream Tasmota for the
**Webradio Mini** build (ESP32-S3 Super Mini, 4 MB, audio-only). It exists for
GPLv3 traceability and to make the diff against upstream reviewable.

All code changes are marked in-source with `// RK modif` / `// end RK modif`
(the original author's edits) and `// RK fix` (corrections applied afterwards).
The GPLv3 license and all upstream copyright headers are preserved unchanged.

- **Base:** Tasmota (branch upstream at fork time)
- **Target:** ESP32-S3 Super Mini, 4 MB flash QIO, QSPI PSRAM
- **Feature set:** I2S webradio + local MP3 + DLNA renderer path. No LVGL / no
  display / no touch. No AAC / no OPUS.

---

## 1. Chunked ICY stream decoding

**Files:**
`lib/lib_audio/ESP8266Audio/src/AudioFileSourceICYStream.h`
`lib/lib_audio/ESP8266Audio/src/AudioFileSourceICYStream.cpp`

**Problem.** Some stations serve the audio body with
`Transfer-Encoding: chunked`. The stock ICY reader consumed the raw socket
without stripping the chunk-size lines and CRLF framing, so chunk headers were
fed to the decoder as if they were audio, producing corrupted playback on those
stations.

**Change.**
- Added `Transfer-Encoding` to the collected HTTP headers and detect `chunked`
  in `open()`.
- Added a chunked decoder: `readChunked()`, `readChunkSizeLine()`, `readByte()`,
  `skipCRLF()`, plus state `chunked` / `chunkRemaining` / `chunkEof`.
- Routed every socket read in `readInternal()` through a `bodyRead` lambda that
  transparently switches between raw read (non-chunked) and `readChunked()`.

**Fixes applied on top of the original edit (`// RK fix`):**
- `millis()` is `uint32_t`; the timeout deltas were stored in `int`. Changed to
  `uint32_t start = millis();` in `readByte`, `readChunked` and `readInternal`
  to avoid a broken comparison after ~24 days of uptime.
- EOF clamp in `readInternal` now guards `pos < size` before subtracting, so the
  `uint32_t len` can never wrap to a huge value on finite sources. (Radio
  streams report `size == -1`, so the branch is normally skipped; this keeps it
  safe for finite sources too.)
- `skipCRLF()` is now strict: it returns a framing error instead of silently
  consuming an unexpected byte, which previously could eat one byte of audio
  payload and cause intermittent clicks that are hard to trace.

**Note.** No `AddLog` calls are used in these files: they are part of the
vendored ESP8266Audio library and do not have Tasmota's logging symbols in
scope. `saveURL` is a fixed 128-byte buffer; longer URLs are truncated and
`strncpy` always NUL-terminates so a truncated reconnect URL fails cleanly.

---

## 2. DLNA "loop -> next" fix + stream leak fix

**File:** `tasmota/tasmota_xdrv_driver/xdrv_42_0_i2s_audio_idf51.ino`

**Function:** `I2sMp3WrTask` (the webradio / DLNA playback task).

**Problem 1 — track never advances.** When the decoder stopped running while
`task_running` was still true, the upstream loop had no `else` branch: it spun
forever, `task_has_ended` never became true, and the
`{"Event":{"I2SPlay":"Ended"}}` event was never published. For DLNA this meant
the control point never learned the track had finished, so the next track never
started (the current track effectively looped).

**Change 1.** Added an `else { task_running = false; }` so the task ends when the
decoder is no longer running, letting `I2sEventHandler` publish the `Ended`
event and the playlist advance. The `vTaskDelay(1)` was moved outside the
`if/else` so every iteration yields.

**Problem 2 — memory / socket leak.** On exit the task called `mp3_delete()`,
which frees `audio_i2s_mp3.file/id3/buff/decoder` but **not**
`Audio_webradio.ifile` — the `AudioFileSourceICYStream` that owns the HTTP
socket. Every DLNA track change leaked one ICY stream + socket until RAM ran
out.

**Change 2.** Replaced `mp3_delete()` with `I2sWebRadioStopPlaying()` at the end
of the task, which frees `decoder`, `buff` **and** `ifile`.

**Also:** the `I2sMp3WrTask` definition is wrapped in `#ifdef USE_I2S_MP3` so it
is only compiled when file/stream playback is enabled.

---

## 3. Reconnect mode split (webradio vs DLNA)

**File:** `tasmota/tasmota_xdrv_driver/xdrv_42_7_i2s_webradio_idf51.ino`

**Problem.** Webradio and DLNA both flow through `I2SWebradio()` with a single
hard-coded `SetReconnect()`. They need opposite behaviour:
- Webradio streams drop briefly and should retry (resilient).
- DLNA closes the stream deliberately to advance tracks; retrying delays the
  `Ended` event and stutters the gap.

**Change.**
- Added `enum WR_Mode { WR_MODE_RADIO, WR_MODE_DLNA }` and a `wr_mode` parameter
  to `I2SWebradio()`.
- `SetReconnect(5, 5)` for radio, `SetReconnect(0, 0)` for DLNA.
- `CmndI2SWebRadio` selects the mode from the command index:
  - `I2SWR <url>` -> webradio, reconnect (5,5)
  - `I2SWR2 <url>` -> DLNA, reconnect (0,0)
  Index 2 is reserved for the DLNA selector and is not forwarded as a decoder
  type (both modes decode MP3). Other indices keep the previous decoder-type
  behaviour.

**Also (`// RK fix`):** the diagnostic hexdump `I2S_DumpFirstBytes()` and its
call site are gated behind `#ifdef WR_DEBUG_DUMP`. When enabled it opens a
second HTTP connection to the station on every start (extra latency + log
noise), so it is off by default. The webradio task priority was raised from 3
to 5.

---

## 4. Perceptual volume remap

**File:** `tasmota/tasmota_xdrv_driver/xdrv_42_0_i2s_3_lib_idf51.ino`

**Function:** `TasmotaI2S::consumeSamples`.

**Problem.** The stock `Amplify()` applies a linear gain, so nearly all the
usable range is crammed into the top of the slider (the ear is roughly
logarithmic).

**Change.** Replaced the per-sample `Amplify()` with a remap that gives a much
gentler gain at low/mid slider positions:

```
divisor = 3840 - ((3840 - 128) * gainF2P6 / 64)
out     = sample * gainF2P6 / divisor   (clamped to +/-32767)
```

`gainF2P6` is the Q6 gain control (0..64). Effective gain per position:

| gainF2P6 | divisor | effective gain |
|---------:|--------:|---------------:|
| 64       | 128     | 0.50           |
| 32       | 1984    | ~0.016         |
| 8        | 3376    | ~0.0024        |

**By design:** maximum effective gain is 0.5 (-6 dBFS). This is a deliberate
head-room / anti-clipping margin ahead of the downstream class-D amp, not a bug.
Integer division truncates toward zero, so very small samples at low volume can
round to 0 — inaudible on music and keeps the hot path integer-only.

**Bounds:** in normal use `gainF2P6 <= 64` (the `I2SGain` command clamps its
input to 0..100, mapping to `gainF2P6 <= 64`), so `divisor` stays positive
(128..3840) and never reaches zero. Values of `gainF2P6 > 66` would make
`divisor` negative/zero; the upstream command path never produces them, so no
extra guard is added.

---

## 5. Restored base-class header

**File:** `lib/lib_audio/ESP8266Audio/src/AudioFileSourceHTTPStream.h`

Restored to the correct Tasmota-vendored content (`class
AudioFileSourceHTTPStream`). It had been accidentally overwritten during file
handling with the contents of `AudioFileSourceICYStream.h`, which broke the
class hierarchy (`SetReconnect`, `read`, `close`, etc. went missing). No
functional change versus upstream — this simply undoes the accidental
overwrite.

---

## 6. Build configuration

**Files:**
`platformio.ini`
`platformio_override.ini`
`tasmota/user_config_override.h`

Single build environment `tasmota32s3-webradio-mini`:
- Board `esp32s3-qio_qspi` (4 MB flash QIO, QSPI PSRAM, partition
  `app2880k_fs320k`).
- No LVGL / display / touch / SPI flags (they overflow the 4 MB Super Mini).
- `libesp32_lvgl` dropped from the lib dirs to cut size and compile time.
- `platformio_override.ini` extends the Tasmota-provided `env:tasmota32_base`
  (it does not redefine it).

`user_config_override.h` enables `USE_I2S_AUDIO`, `USE_I2S_WEBRADIO`,
`USE_I2S_MP3` and `USE_I2C`. `USE_I2S_AAC` and `USE_I2S_OPUS` are intentionally
left disabled (flash budget + Super Mini stability). Optional `WR_DEBUG_DUMP` is
documented but off.

> Note: Tasmota's `.gitignore` normally excludes `platformio_override.ini` and
> `user_config_override.h`. They are force-added here so the build is
> reproducible from a clone. If ever upstreaming changes 1–4 via a PR, these two
> files should be excluded (upstream expects each user to create their own from
> the `_sample` files).

---

## Known issues / TODO

- **DLNA** end-to-end (`I2SWR2` + next-track handover) is implemented but not yet
  validated on hardware against a live control point.
