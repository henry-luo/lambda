# Radiant Video Playback: Integration Proposal

## Overview

This document proposes adding `<video>` element playback support to Radiant using **native OS APIs** on macOS and Windows, and a **minimal static FFmpeg build** on Linux. The design prioritises zero unnecessary binary bloat, hardware-accelerated decode where available, and clean integration with Radiant's existing software-rendered-to-GL-texture display pipeline.

---

## Scope and Goals

**In scope:**
- Decode and render `<video>` element content (H.264, H.265, VP9, AV1)
- Audio playback for video tracks (AAC, Opus)
- Basic playback controls: play, pause, seek, volume, mute
- `autoplay`, `loop`, `muted`, `controls`, `poster` attribute support
- Frame delivery to Radiant's CPU surface for GL texture upload

**Out of scope (future work):**
- `<audio>` standalone element
- Adaptive streaming (HLS/DASH)
- DRM / Encrypted Media Extensions
- WebRTC / live capture
- Picture-in-picture

---

## 1. Architecture

### 1.1 Abstraction Layer

A platform-agnostic C API (`rdt_video.h`) abstracts all platform backends behind a single interface, following the same pattern as `rdt_vector.hpp` / `rdt_vector_cg.mm` / `rdt_vector_tvg.cpp`.

```
radiant/
├── rdt_video.h              // Public C API
├── rdt_video_avf.mm         // macOS: AVFoundation backend
├── rdt_video_mf.cpp         // Windows: Media Foundation backend
├── rdt_video_ffmpeg.cpp     // Linux: FFmpeg backend
```

### 1.2 Public API

```cpp
// rdt_video.h

#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RdtVideo RdtVideo;

typedef enum {
    RDT_VIDEO_STATE_IDLE,
    RDT_VIDEO_STATE_LOADING,
    RDT_VIDEO_STATE_READY,
    RDT_VIDEO_STATE_PLAYING,
    RDT_VIDEO_STATE_PAUSED,
    RDT_VIDEO_STATE_ENDED,
    RDT_VIDEO_STATE_ERROR,
} RdtVideoState;

typedef struct {
    uint8_t*    pixels;     // RGBA 32bpp, caller-owned buffer
    int         width;
    int         height;
    int         stride;     // bytes per row
} RdtVideoFrame;

typedef struct {
    void (*on_state_changed)(RdtVideo* video, RdtVideoState state, void* userdata);
    void (*on_frame_ready)(RdtVideo* video, void* userdata);
    void (*on_duration_known)(RdtVideo* video, double seconds, void* userdata);
} RdtVideoCallbacks;

// Lifecycle
RdtVideo*       rdt_video_create(const RdtVideoCallbacks* cb, void* userdata);
void            rdt_video_destroy(RdtVideo* video);

// Source
int             rdt_video_open_url(RdtVideo* video, const char* url);
int             rdt_video_open_buffer(RdtVideo* video, const uint8_t* data,
                                      size_t size, const char* mime_type);

// Playback
void            rdt_video_play(RdtVideo* video);
void            rdt_video_pause(RdtVideo* video);
void            rdt_video_seek(RdtVideo* video, double seconds);
void            rdt_video_set_volume(RdtVideo* video, float volume);  // 0.0–1.0
void            rdt_video_set_muted(RdtVideo* video, bool muted);
void            rdt_video_set_loop(RdtVideo* video, bool loop);

// Query
RdtVideoState   rdt_video_get_state(RdtVideo* video);
double          rdt_video_get_current_time(RdtVideo* video);
double          rdt_video_get_duration(RdtVideo* video);
int             rdt_video_get_width(RdtVideo* video);
int             rdt_video_get_height(RdtVideo* video);

// Frame retrieval — copies the latest decoded frame into caller buffer.
// Returns 0 on success, -1 if no new frame available.
int             rdt_video_get_frame(RdtVideo* video, RdtVideoFrame* frame);

#ifdef __cplusplus
}
#endif
```

### 1.3 Internal Impl Struct Pattern

Each backend allocates a platform-specific struct, following the `rdt_vector_cg.mm` convention:

```cpp
// rdt_video_avf.mm (macOS)
#ifdef __APPLE__
#import <AVFoundation/AVFoundation.h>

struct RdtVideo {
    AVPlayer*               player;
    AVPlayerItemVideoOutput* video_output;
    RdtVideoCallbacks       callbacks;
    void*                   userdata;
    RdtVideoState           state;
    double                  duration;
    int                     width;
    int                     height;
    bool                    loop;
    bool                    muted;
};
#endif
```

```cpp
// rdt_video_mf.cpp (Windows)
#ifdef _WIN32
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

struct RdtVideo {
    IMFSourceReader*        source_reader;
    IMFMediaType*           output_type;
    RdtVideoCallbacks       callbacks;
    void*                   userdata;
    RdtVideoState           state;
    double                  duration;
    int                     width;
    int                     height;
    bool                    loop;
    bool                    muted;
    // Audio via IMFSourceReader second stream
};
#endif
```

```cpp
// rdt_video_ffmpeg.cpp (Linux)
#ifdef __linux__
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

struct RdtVideo {
    AVFormatContext*        fmt_ctx;
    AVCodecContext*         video_codec_ctx;
    AVCodecContext*         audio_codec_ctx;
    SwsContext*             sws_ctx;
    SwrContext*             swr_ctx;
    int                     video_stream_idx;
    int                     audio_stream_idx;
    RdtVideoCallbacks       callbacks;
    void*                   userdata;
    RdtVideoState           state;
    double                  duration;
    int                     width;
    int                     height;
    bool                    loop;
    bool                    muted;
};
#endif
```

---

## 2. Platform Backends

### 2.1 macOS — AVFoundation (`rdt_video_avf.mm`)

| Item | Detail |
|------|--------|
| **Framework** | `AVFoundation.framework`, `CoreMedia.framework` |
| **Decode** | `AVPlayer` + `AVPlayerItem` — OS handles HW decode (VideoToolbox) transparently |
| **Frame extraction** | `AVPlayerItemVideoOutput` with `CVPixelBufferRef` in `kCVPixelFormatType_32BGRA`; convert to RGBA and copy into `RdtVideoFrame.pixels` |
| **Audio** | `AVPlayer` handles audio output natively |
| **HW codecs** | H.264, H.265/HEVC, VP9 (macOS 11+), AV1 (Apple Silicon, macOS 13+) |
| **Binary size** | ~0 KB (system framework, dynamic link) |
| **Build** | Add `-framework AVFoundation -framework CoreMedia -framework CoreVideo` to macOS link flags |

**Key implementation notes:**
- Use `[AVPlayerItemVideoOutput hasNewPixelBufferForItemTime:]` to poll for new frames during the render loop
- Convert `CVPixelBufferRef` → RGBA via `CVPixelBufferLockBaseAddress` + byte swizzle (BGRA→RGBA) or `vImage` conversion
- Register KVO on `AVPlayerItem.status` and `AVPlayer.timeControlStatus` to drive `on_state_changed` callbacks
- Boundary time observer for end-of-playback → `RDT_VIDEO_STATE_ENDED`

### 2.2 Windows — Media Foundation (`rdt_video_mf.cpp`)

| Item | Detail |
|------|--------|
| **API** | Media Foundation `IMFSourceReader` (pull model) |
| **Decode** | `MFCreateSourceReaderFromURL` / `MFCreateSourceReaderFromByteStream` with `MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING` for automatic colour conversion |
| **Frame extraction** | `ReadSample()` → `IMFMediaBuffer` → `Lock()` → copy RGB32 into `RdtVideoFrame.pixels` |
| **Audio** | Separate `ReadSample()` on audio stream → `IMFMediaBuffer` → feed to WASAPI via `IAudioRenderClient` |
| **HW codecs** | H.264, H.265 (Win10+), VP9 (Win10 1709+), AV1 (Win10 1903+ with AV1 extension) |
| **Binary size** | ~0 KB (system DLLs: `mfplat.dll`, `mfreadwrite.dll`, `mf.dll`) |
| **Build** | Link `mfplat.lib mfreadwrite.lib mf.lib mfuuid.lib ole32.lib` — already have `ole32` in current Windows link set |

**Key implementation notes:**
- Call `MFStartup()` once at init, `MFShutdown()` at teardown
- Set output media type to `MFVideoFormat_RGB32` for direct pixel copy
- Use a decode thread that calls `ReadSample()` in a loop, parking decoded frames in a ring buffer
- Main render thread picks latest frame via `rdt_video_get_frame()`
- Seek via `IMFSourceReader::SetCurrentPosition()`

### 2.3 Linux — FFmpeg (`rdt_video_ffmpeg.cpp`)

| Item | Detail |
|------|--------|
| **Libraries** | `libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample` |
| **License** | LGPL-2.1 (with `--enable-gpl` disabled) |
| **Decode** | `avcodec_send_packet()` / `avcodec_receive_frame()` loop |
| **Frame extraction** | `sws_scale()` to convert decoded `AVFrame` (YUV420P etc.) → RGBA into `RdtVideoFrame.pixels` |
| **Audio** | `swr_convert()` to resample to S16/F32 planar → output via ALSA (`snd_pcm_writei`) or PulseAudio |
| **HW accel** | Optional: `h264_vaapi` / `hevc_vaapi` via `av_hwdevice_ctx_create()`. SW fallback is default. |
| **Binary size** | ~3–4 MB static (see §4) |

**Minimal FFmpeg configure:**

```bash
./configure \
    --prefix=/opt/ffmpeg-minimal \
    --enable-static --disable-shared \
    --disable-programs --disable-doc \
    --disable-everything \
    --enable-decoder=h264,hevc,vp9,av1,aac,opus,pcm_s16le \
    --enable-demuxer=mov,matroska,webm,mp4,avi,ogg \
    --enable-parser=h264,hevc,vp9,av1,aac,opus \
    --enable-protocol=file,pipe,http,https,data \
    --enable-filter=aresample \
    --enable-swscale --enable-swresample \
    --disable-avdevice --disable-avfilter \
    --disable-network \
    --enable-small \
    --enable-lto \
    --cc=clang \
    --extra-cflags="-Os -ffunction-sections -fdata-sections" \
    --extra-ldflags="-Wl,--gc-sections"
```

**Audio output on Linux:**
- Prefer PulseAudio (`libpulse-simple`) — near-universal on desktop Linux
- Fallback: ALSA direct (`libasound2`) — always available
- Link dynamically via `dlopen()` to avoid hard runtime dependency

---

## 3. Integration with Radiant

### 3.1 Element Handling

`HTM_TAG_VIDEO` is already recognised in `resolve_css_style.cpp` and `intrinsic_sizing.cpp` as a replaced element with default dimensions 300×150. The integration points:

| File | Change |
|------|--------|
| `resolve_htm_style.cpp` | Parse `<video>` attributes: `src`, `width`, `height`, `autoplay`, `loop`, `muted`, `controls`, `poster`, `preload` |
| `intrinsic_sizing.cpp` | Once metadata is loaded, replace 300×150 default with actual video dimensions from `rdt_video_get_width/height()` |
| `render_img.cpp` → new `render_video.cpp` | Render current video frame into the surface at the element's layout rect |
| `window.cpp` | Tick active video players each frame; poll `rdt_video_get_frame()` and mark surface dirty if new frame available |
| `view.hpp` / `view_pool.cpp` | Add `RdtVideo*` field to view node for replaced `<video>` elements |

### 3.2 Render Loop Integration

The current render loop in `window.cpp` follows this path:

```
glfwPollEvents()
  → check dirty/resize
  → render_html_doc() — paints view tree into CPU surface
  → repaint_window() — uploads surface to GL texture → draws quad
  → glfwSwapBuffers()
```

Video frames are composited **during `render_html_doc()`** at paint time:

1. `render_video()` is called when the paint walk reaches a `<video>` view node
2. It calls `rdt_video_get_frame()` to obtain the latest decoded RGBA frame
3. The frame pixels are blitted into `ui_context.surface->pixels` at the element's layout rect, respecting `object-fit` scaling
4. No additional GL texture upload is needed — the video pixels are already in the CPU surface that gets uploaded each frame

This avoids any changes to the GL pipeline and keeps video rendering consistent with how images (`render_img.cpp`) are already handled.

### 3.3 Decode Threading

Decoding must run on a background thread to avoid blocking the main render loop:

```
Main thread                     Decode thread
─────────────                   ─────────────
rdt_video_play()           →    start decode loop
                                avcodec_send_packet / ReadSample / AVPlayerItemVideoOutput
                                  → write decoded RGBA frame to ring buffer (double-buffer)
rdt_video_get_frame()      ←    atomic swap latest frame pointer
  → copy into surface
```

- **macOS**: AVFoundation manages its own threads; `AVPlayerItemVideoOutput` is thread-safe to poll from the main thread.
- **Windows**: Dedicate a thread for `IMFSourceReader::ReadSample()` loop; post frames to a lock-free single-producer/single-consumer ring buffer.
- **Linux/FFmpeg**: Dedicate a thread for the `av_read_frame()` → `avcodec_send_packet()` → `avcodec_receive_frame()` → `sws_scale()` pipeline; same ring buffer pattern.

Audio playback runs on the decode thread (or a third thread) and is kept in sync using PTS timestamps from the decoded frames.

### 3.4 Resource Lifecycle

| Event | Action |
|-------|--------|
| `<video>` element created in DOM | `rdt_video_create()` — allocate player, store in view node |
| `src` attribute set / changed | `rdt_video_open_url()` — begin async load |
| Element removed from DOM | `rdt_video_destroy()` — stop playback, free decoder, release buffers |
| Navigation away from page | Destroy all active `RdtVideo` instances |

---

## 4. Binary Size Impact

| Platform | Decoder | Link Type | Additional Size |
|----------|---------|-----------|-----------------|
| **macOS** | AVFoundation | Dynamic (system framework) | **~15–30 KB** (glue code only) |
| **Windows** | Media Foundation | Dynamic (system DLL) | **~20–50 KB** (glue code + COM boilerplate) |
| **Linux** | FFmpeg (minimal) | Static | **~3–4 MB** |

**Linux FFmpeg breakdown (estimated, post-LTO + strip):**

| Component | Size |
|-----------|------|
| libavcodec (H.264 + H.265 + VP9 + AV1 + AAC + Opus) | ~2.5–3.0 MB |
| libavformat (demuxers: MOV, MKV, WebM, AVI, OGG) | ~300–400 KB |
| libavutil | ~150–200 KB |
| libswscale | ~100–150 KB |
| libswresample | ~50–80 KB |
| **Total** | **~3.1–3.8 MB** |

Current release binary is ~9 MB. After video support:

| Platform | Expected Binary |
|----------|----------------|
| macOS | ~9 MB (negligible change) |
| Windows | ~9 MB (negligible change) |
| Linux | ~12–13 MB |

---

## 5. Build System Changes

### 5.1 Premake / Makefile

Add to `build_lambda_config.json`:

```json
{
    "video_sources": {
        "macos": ["radiant/rdt_video_avf.mm"],
        "windows": ["radiant/rdt_video_mf.cpp"],
        "linux": ["radiant/rdt_video_ffmpeg.cpp"]
    },
    "video_links": {
        "macos": ["AVFoundation.framework", "CoreMedia.framework", "CoreVideo.framework"],
        "windows": ["mfplat", "mfreadwrite", "mf", "mfuuid"],
        "linux": ["avcodec", "avformat", "avutil", "swscale", "swresample", "pthread"]
    }
}
```

### 5.2 Linux FFmpeg Build

Add FFmpeg as a dependency in `setup-linux-deps.sh`:

```bash
# Build minimal FFmpeg for video support
FFMPEG_VERSION="7.1"
if [ ! -f "$PREFIX/lib/libavcodec.a" ]; then
    curl -sL "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz" | tar xJ
    cd "ffmpeg-${FFMPEG_VERSION}"
    ./configure \
        --prefix="$PREFIX" \
        --enable-static --disable-shared \
        --disable-programs --disable-doc \
        --disable-everything \
        --enable-decoder=h264,hevc,vp9,av1,aac,opus \
        --enable-demuxer=mov,matroska,webm,mp4,avi \
        --enable-parser=h264,hevc,vp9,av1,aac,opus \
        --enable-protocol=file \
        --enable-swscale --enable-swresample \
        --disable-avdevice \
        --enable-small --enable-lto \
        --cc=clang
    make -j$(nproc)
    make install
    cd ..
fi
```

---

## 6. Implementation Plan

### Phase 1 — Core Decode + Frame Delivery

- [ ] Define `rdt_video.h` public API
- [ ] Implement `rdt_video_avf.mm` — open, decode, frame extraction, play/pause
- [ ] Implement `rdt_video_mf.cpp` — same feature set
- [ ] Implement `rdt_video_ffmpeg.cpp` — same feature set
- [ ] Unit tests: decode a known MP4 (H.264+AAC), verify frame dimensions and pixel output

### Phase 2 — Radiant Integration

- [ ] Add `RdtVideo*` to view node for `<video>` elements
- [ ] Parse `<video>` attributes in `resolve_htm_style.cpp`
- [ ] Implement `render_video.cpp` — blit decoded frame to CPU surface
- [ ] Integrate frame polling into `window.cpp` render loop
- [ ] Update intrinsic sizing with actual video dimensions on metadata load

### Phase 3 — Audio + Controls

- [ ] Audio output: AVFoundation (macOS), WASAPI (Windows), PulseAudio/ALSA (Linux)
- [ ] A/V sync using PTS timestamps
- [ ] Default `controls` UI overlay: play/pause button, seek bar, time display, volume
- [ ] `poster` attribute — display poster image before playback starts

### Phase 4 — Polish

- [ ] `autoplay`, `loop`, `muted` attribute handling
- [ ] `preload` attribute (none / metadata / auto)
- [ ] Seek support with keyframe snapping
- [ ] Error handling and fallback (unsupported codec → display error poster)
- [ ] Memory / resource cleanup on navigation
- [ ] Performance profiling: verify <2ms frame copy overhead at 1080p

---

## 7. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| FFmpeg LGPL compliance | Legal | Static link is LGPL-compliant if object files are provided for relinking, or use shared linking on Linux only |
| A/V sync drift | User-visible | Use presentation timestamps (PTS) from container; resync on seek |
| High CPU on SW decode (Linux, no VA-API) | Performance | Limit to 1080p SW decode; document VA-API as optional HW path |
| Audio API fragmentation on Linux | Compatibility | `dlopen()` PulseAudio with ALSA fallback; no hard link dependency |
| Large video stalling render loop | UI jank | All decode on background thread; frame delivery via lock-free buffer; main thread only copies pixels |
