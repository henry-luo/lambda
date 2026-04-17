# Radiant Video Playback: Integration Proposal

## Overview

This document proposes adding `<video>` element playback support to Radiant using **native OS APIs** on macOS and Windows, and a **minimal static FFmpeg build** on Linux. The design uses a **three-tier threading model** (decode → playback → render) so video frame rate is independent of page rendering, and a **post-composite blit** strategy that bypasses the display list for efficient frame delivery. Audio playback is integrated from the start using platform-native audio APIs, with A/V sync driven by presentation timestamps.

---

## Scope and Goals

**In scope (local playback):**
- Decode and render `<video>` element content from **local files** (H.264, H.265, VP9, AV1)
- Audio playback for video tracks (AAC, Opus, PCM) — integrated from the start, not deferred
- A/V synchronisation using presentation timestamps (PTS)
- Basic playback controls: play, pause, seek, volume, mute
- `autoplay`, `loop`, `muted`, `controls`, `poster` attribute support
- Frame delivery via post-composite blit into Radiant's CPU surface
- Independent video playback thread for frame-rate decoupling from page rendering

**Out of scope (future work):**
- `<audio>` standalone element
- **Web video playback** (HTTP/HTTPS URL sources)
- **Adaptive streaming** (HLS/DASH)
- DRM / Encrypted Media Extensions
- WebRTC / live capture
- Picture-in-picture
- GL overlay texture for zero-copy 4K video

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
    double      pts;        // presentation timestamp (seconds)
} RdtVideoFrame;

typedef struct {
    void (*on_state_changed)(RdtVideo* video, RdtVideoState state, void* userdata);
    void (*on_frame_ready)(RdtVideo* video, void* userdata);
    void (*on_duration_known)(RdtVideo* video, double seconds, void* userdata);
    void (*on_video_size_known)(RdtVideo* video, int width, int height, void* userdata);
} RdtVideoCallbacks;

// Lifecycle
RdtVideo*       rdt_video_create(const RdtVideoCallbacks* cb, void* userdata);
void            rdt_video_destroy(RdtVideo* video);

// Source — local file path only (web URLs deferred to future)
int             rdt_video_open_file(RdtVideo* video, const char* file_path);

// Layout rect — decode resolution is capped to this size to limit memory.
// Call on layout change. Width/height in CSS pixels (scaled by device_scale internally).
void            rdt_video_set_layout_rect(RdtVideo* video, int width, int height);

// Playback control
void            rdt_video_play(RdtVideo* video);
void            rdt_video_pause(RdtVideo* video);
void            rdt_video_seek(RdtVideo* video, double seconds);
void            rdt_video_set_loop(RdtVideo* video, bool loop);

// Audio control
void            rdt_video_set_volume(RdtVideo* video, float volume);  // 0.0–1.0
void            rdt_video_set_muted(RdtVideo* video, bool muted);

// Query — all thread-safe, lock-free reads
RdtVideoState   rdt_video_get_state(RdtVideo* video);
double          rdt_video_get_current_time(RdtVideo* video);
double          rdt_video_get_duration(RdtVideo* video);
int             rdt_video_get_width(RdtVideo* video);   // intrinsic video width
int             rdt_video_get_height(RdtVideo* video);  // intrinsic video height
bool            rdt_video_has_audio(RdtVideo* video);

// Frame retrieval — returns the latest decoded frame (may be same frame as last call).
// Copies into caller-owned buffer. Returns 0 on success, -1 if no frame available.
// The playback thread manages PTS scheduling; this always returns the current frame.
int             rdt_video_get_frame(RdtVideo* video, RdtVideoFrame* frame);

#ifdef __cplusplus
}
#endif
```

### 1.3 Internal Impl Struct Pattern

Each backend allocates a platform-specific struct. The struct captures the three-tier threading model: decode thread (demux + decode), playback thread (PTS scheduling + audio output), and frame buffer for the render thread consumer.

```cpp
// rdt_video_avf.mm (macOS)
// AVFoundation manages decode + playback scheduling internally via dispatch queues.
// We only need to manage the frame buffer handoff.
#ifdef __APPLE__
#import <AVFoundation/AVFoundation.h>

struct RdtVideo {
    // AVFoundation handles decode + playback threads internally
    AVPlayer*               player;
    AVPlayerItem*           player_item;
    AVPlayerItemVideoOutput* video_output;
    id                      end_observer;       // NSNotification observer for AVPlayerItemDidPlayToEndTime

    // Frame double-buffer (atomic swap between AVF callback and render thread)
    RdtVideoFrame           frame_buffers[2];
    _Atomic int             front_index;        // index of latest complete frame
    _Atomic bool            has_new_frame;

    // Decode resolution cap (set by rdt_video_set_layout_rect)
    int                     target_width;
    int                     target_height;

    // State
    RdtVideoCallbacks       callbacks;
    void*                   userdata;
    _Atomic RdtVideoState   state;
    _Atomic double          current_time;
    double                  duration;
    int                     intrinsic_width;
    int                     intrinsic_height;
    bool                    has_audio_track;
    bool                    loop;
    bool                    muted;
    float                   volume;
};
#endif
```

```cpp
// rdt_video_mf.cpp (Windows)
#ifdef _WIN32
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <audioclient.h>     // WASAPI

struct RdtVideo {
    // Decode thread: IMFSourceReader pull loop
    IMFSourceReader*        source_reader;
    HANDLE                  decode_thread;
    _Atomic bool            decode_running;

    // Playback thread: PTS scheduling + audio output
    HANDLE                  playback_thread;
    _Atomic bool            playback_running;

    // Audio output (WASAPI)
    IAudioClient*           audio_client;
    IAudioRenderClient*     audio_render_client;
    WAVEFORMATEX*           audio_format;
    uint8_t*                audio_ring_buffer;
    _Atomic int             audio_ring_write;
    _Atomic int             audio_ring_read;
    int                     audio_ring_capacity;

    // Frame double-buffer (decode thread writes, render thread reads)
    RdtVideoFrame           frame_buffers[2];
    _Atomic int             front_index;
    _Atomic bool            has_new_frame;

    // Decode resolution cap
    int                     target_width;
    int                     target_height;

    // PTS tracking
    _Atomic double          current_time;       // audio clock (authoritative)
    double                  video_pts;           // PTS of current video frame
    LARGE_INTEGER           playback_start_qpc;  // QueryPerformanceCounter at play()
    double                  playback_start_pts;   // PTS at play()

    // State
    RdtVideoCallbacks       callbacks;
    void*                   userdata;
    _Atomic RdtVideoState   state;
    double                  duration;
    int                     intrinsic_width;
    int                     intrinsic_height;
    bool                    has_audio_track;
    bool                    loop;
    bool                    muted;
    float                   volume;
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
#include <pthread.h>

struct RdtVideo {
    // Demux + decode state
    AVFormatContext*        fmt_ctx;
    AVCodecContext*         video_codec_ctx;
    AVCodecContext*         audio_codec_ctx;
    SwsContext*             sws_ctx;
    SwrContext*             swr_ctx;
    int                     video_stream_idx;
    int                     audio_stream_idx;

    // Decode thread: av_read_frame → decode → sws_scale → frame buffer
    pthread_t               decode_thread;
    _Atomic bool            decode_running;

    // Playback thread: PTS scheduling + audio output
    pthread_t               playback_thread;
    _Atomic bool            playback_running;

    // Audio output (PulseAudio via dlopen, ALSA fallback)
    void*                   pulse_handle;       // dlopen("libpulse-simple.so")
    void*                   audio_stream;       // pa_simple* or snd_pcm_t*
    uint8_t*                audio_ring_buffer;
    _Atomic int             audio_ring_write;
    _Atomic int             audio_ring_read;
    int                     audio_ring_capacity;
    int                     audio_sample_rate;
    int                     audio_channels;

    // Frame double-buffer (atomic swap between decode and render threads)
    RdtVideoFrame           frame_buffers[2];
    _Atomic int             front_index;
    _Atomic bool            has_new_frame;

    // Decode resolution cap (sws_scale target dimensions)
    int                     target_width;
    int                     target_height;

    // PTS tracking
    _Atomic double          current_time;       // audio clock (authoritative)
    double                  video_pts;           // PTS of current decoded frame
    struct timespec         playback_start_time; // clock_gettime at play()
    double                  playback_start_pts;  // PTS at play()

    // State
    RdtVideoCallbacks       callbacks;
    void*                   userdata;
    _Atomic RdtVideoState   state;
    double                  duration;
    int                     intrinsic_width;
    int                     intrinsic_height;
    bool                    has_audio_track;
    bool                    loop;
    bool                    muted;
    float                   volume;
};
#endif
```

---

## 2. Platform Backends

### 2.1 macOS — AVFoundation (`rdt_video_avf.mm`)

| Item | Detail |
|------|--------|
| **Framework** | `AVFoundation.framework`, `CoreMedia.framework`, `CoreVideo.framework` |
| **Source** | `[AVPlayer playerWithURL:[NSURL fileURLWithPath:path]]` — local file only |
| **Decode** | `AVPlayer` + `AVPlayerItem` — OS handles HW decode (VideoToolbox) transparently |
| **Frame extraction** | `AVPlayerItemVideoOutput` with `CVPixelBufferRef` in `kCVPixelFormatType_32BGRA`; convert to RGBA and copy into `RdtVideoFrame.pixels` |
| **Audio** | `AVPlayer` handles audio mixing and output natively — no extra work needed |
| **A/V sync** | `AVPlayer` manages sync internally; `currentTime` is authoritative |
| **Threading** | AVFoundation manages decode + playback on internal dispatch queues. We poll `hasNewPixelBufferForItemTime:` from the render thread. No explicit threads needed. |
| **HW codecs** | H.264, H.265/HEVC, VP9 (macOS 11+), AV1 (Apple Silicon, macOS 13+) |
| **Binary size** | ~0 KB (system framework, dynamic link) |
| **Build** | Add `-framework AVFoundation -framework CoreMedia -framework CoreVideo` to macOS link flags |

**Key implementation notes:**
- Use `[AVPlayerItemVideoOutput hasNewPixelBufferForItemTime:]` to poll for new frames during the render loop; return cached frame buffer when no new pixel buffer is available to prevent flicker
- Convert `CVPixelBufferRef` → RGBA via `CVPixelBufferLockBaseAddress` + byte swizzle (BGRA→RGBA)
- **KVO does not fire under GLFW's event loop** — `rdt_video_get_state()` polls `AVPlayerItem.status` and `AVPlayer.timeControlStatus` directly instead
- **No ARC** — use `alloc/init` patterns and explicit `retain`/`release` for all ObjC objects stored in the struct
- End-of-playback notification for loop restart via `AVPlayerItemDidPlayToEndTimeNotification`
- Volume/mute via `AVPlayer.volume` — affects audio output directly
- Relative `src` paths resolved via `parse_url(document->url, src)` + `url_to_local_path()`

### 2.2 Windows — Media Foundation (`rdt_video_mf.cpp`)

| Item | Detail |
|------|--------|
| **API** | Media Foundation `IMFSourceReader` (pull model) |
| **Source** | `MFCreateSourceReaderFromURL(L"file:///path")` — local file path converted to file:// URL |
| **Decode** | `MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING` for automatic colour conversion |
| **Frame extraction** | `ReadSample()` → `IMFMediaBuffer` → `Lock()` → copy RGB32 into `RdtVideoFrame.pixels` |
| **Audio** | WASAPI shared mode via `IAudioClient` + `IAudioRenderClient`. Decode thread demuxes audio samples, playback thread feeds them to WASAPI buffer in its callback/event loop. |
| **A/V sync** | Audio clock is authoritative. Playback thread reads WASAPI stream position as master clock, drops/holds video frames to match. |
| **Threading** | Explicit decode thread (`ReadSample` loop) + playback thread (PTS scheduling + audio feed). |
| **HW codecs** | H.264, H.265 (Win10+), VP9 (Win10 1709+), AV1 (Win10 1903+ with AV1 extension) |
| **Binary size** | ~0 KB (system DLLs: `mfplat.dll`, `mfreadwrite.dll`, `mf.dll`) |
| **Build** | Link `mfplat.lib mfreadwrite.lib mf.lib mfuuid.lib ole32.lib` — already have `ole32` in current Windows link set |

**Key implementation notes:**
- Call `MFStartup()` once at init, `MFShutdown()` at teardown
- Set output media type to `MFVideoFormat_RGB32` for direct pixel copy
- Decode thread calls `ReadSample()` in a loop for both video and audio streams
- Audio samples → ring buffer → playback thread → WASAPI `IAudioRenderClient::GetBuffer/ReleaseBuffer`
- Seek via `IMFSourceReader::SetCurrentPosition()` — flush both audio and video pipelines on seek
- Volume control: `ISimpleAudioVolume` on the WASAPI session

### 2.3 Linux — FFmpeg (`rdt_video_ffmpeg.cpp`)

| Item | Detail |
|------|--------|
| **Libraries** | `libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample` |
| **License** | LGPL-2.1 (with `--enable-gpl` disabled) |
| **Source** | `avformat_open_input(path)` — local file path |
| **Decode** | `avcodec_send_packet()` / `avcodec_receive_frame()` loop on decode thread |
| **Frame extraction** | `sws_scale()` to convert decoded `AVFrame` (YUV420P etc.) → RGBA at `target_width × target_height` |
| **Audio** | `swr_convert()` to resample to S16LE stereo → ring buffer → playback thread → PulseAudio/ALSA output |
| **A/V sync** | Audio clock is authoritative. Playback thread tracks audio PTS via samples written; video frames dropped/held to match. |
| **Threading** | Explicit decode thread (demux + decode both streams) + playback thread (audio output + PTS scheduling). |
| **HW accel** | Optional: `h264_vaapi` / `hevc_vaapi` via `av_hwdevice_ctx_create()`. SW fallback is default. |
| **Binary size** | ~3–4 MB static (see §4) |

**Minimal FFmpeg configure (local file playback only):**

```bash
./configure \
    --prefix=/opt/ffmpeg-minimal \
    --enable-static --disable-shared \
    --disable-programs --disable-doc \
    --disable-everything \
    --enable-decoder=h264,hevc,vp9,av1,aac,opus,pcm_s16le,pcm_f32le \
    --enable-demuxer=mov,matroska,webm,mp4,avi,ogg,wav \
    --enable-parser=h264,hevc,vp9,av1,aac,opus \
    --enable-protocol=file \
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
- Playback thread calls `pa_simple_write()` or `snd_pcm_writei()` with decoded PCM data
- Audio buffer size: 4096 samples (~23ms at 44.1kHz) — balances latency vs. underrun risk

---

## 3. Integration with Radiant

### 3.1 Element Handling

`HTM_TAG_VIDEO` is already recognised in `resolve_css_style.cpp` and `intrinsic_sizing.cpp` as a replaced element with default dimensions 300×150. The integration points:

| File | Change |
|------|--------|
| `resolve_htm_style.cpp` | Parse `<video>` attributes: `src`, `width`, `height`, `autoplay`, `loop`, `muted`, `controls`, `poster`, `preload` |
| `intrinsic_sizing.cpp` | Once metadata is loaded (`on_video_size_known`), replace 300×150 default with actual video dimensions and trigger relayout |
| `render.cpp` | During display list recording, emit `DL_VIDEO_PLACEHOLDER` at the video element's layout rect |
| new `render_video.cpp` | Post-composite blit: copy latest video frame into final surface, clipped to layout rect |
| `window.cpp` | Tick active video players; after composite, call `render_video_blit()` for each active video |
| `view.hpp` / `view_pool.cpp` | Add `RdtVideo*` field to view node for replaced `<video>` elements |
| `display_list.h` | Add `DL_VIDEO_PLACEHOLDER` opcode (records layout rect + clip region, no pixel data) |

### 3.2 Three-Tier Threading Model

Video playback uses three tiers of execution, each with distinct responsibilities:

```
┌─────────────────┐     ┌──────────────────────┐     ┌────────────────────┐
│  Decode Thread   │     │   Playback Thread     │     │   Render Thread    │
│  (per video)     │     │   (per video)         │     │   (main thread)    │
├─────────────────┤     ├──────────────────────┤     ├────────────────────┤
│ demux container  │     │ PTS scheduling       │     │ display list record│
│ decode video pkt │────→│ frame drop/hold      │     │ dl_replay (tiled)  │
│ sws_scale→RGBA   │     │ audio sample output  │     │ tile_grid_composite│
│ decode audio pkt │────→│ A/V sync (audio=master)│    │ video_blit (post)  │
│ resolution cap   │     │ double-buffer swap   │←───│ rdt_video_get_frame│
└─────────────────┘     └──────────────────────┘     └────────────────────┘
```

**Why three tiers, not two?**

A two-tier model (decode thread + render thread consumer) couples video frame rate to page rendering rate. Problems:
- Page rendering is not constant-rate — it's event-driven, may stall on reflow/relayout
- Audio has hard real-time constraints (~5ms buffer underrun = audible glitch); video decode can tolerate jitter
- Multiple videos on one page need independent frame rate control
- A/V sync requires a dedicated timing loop that is not gated by page paint

The playback thread owns the timing contract: "deliver frame N at PTS T, drop frame N-1 if late." The decode thread is a pure producer. The render thread is a pure consumer that grabs whatever frame is current.

**Per-platform threading:**

| Platform | Decode Thread | Playback Thread | Render Thread |
|----------|---------------|-----------------|---------------|
| **macOS** | AVFoundation internal (dispatch queue) | AVFoundation internal (AVPlayer schedules frames + audio) | Main thread polls `hasNewPixelBufferForItemTime:` |
| **Windows** | Explicit pthread: `ReadSample()` loop | Explicit pthread: WASAPI feed + PTS scheduling | Main thread calls `rdt_video_get_frame()` |
| **Linux** | Explicit pthread: `av_read_frame()` + decode | Explicit pthread: PulseAudio/ALSA write + PTS scheduling | Main thread calls `rdt_video_get_frame()` |

On macOS, AVFoundation provides the three-tier model implicitly. On Windows/Linux, we create two explicit threads per video.

### 3.3 A/V Synchronisation

**Audio clock is the master.** Video frames are dropped or held to match audio playback position. This is the standard approach used by VLC, mpv, and browser engines.

**Sync algorithm (playback thread, Win/Linux):**

```
while playback_running:
    audio_pts = samples_written / sample_rate + playback_start_pts

    // Feed audio: drain audio ring buffer → platform audio API
    while audio_ring has data:
        write samples to WASAPI / PulseAudio / ALSA
        update samples_written

    // Schedule video: pick frame closest to audio_pts
    video_frame = peek decoded frame queue
    if video_frame.pts <= audio_pts:
        // Frame is due or late — present it, drop if >1 frame behind
        swap into front_index (atomic)
        has_new_frame = true
    else:
        // Frame is early — hold current frame, sleep until due
        sleep_ms = (video_frame.pts - audio_pts) * 1000
        sleep(min(sleep_ms, 5))  // cap sleep to 5ms for responsiveness
```

**No-audio case** (video-only files, or `muted` with no audio track): Use wall-clock time as master. `playback_start_time` = `clock_gettime(CLOCK_MONOTONIC)` at play start. Video PTS compared against `(now - playback_start_time) + playback_start_pts`.

**Seek:** Flush both audio and video pipelines. Reset `playback_start_pts` to seek target. On FFmpeg, call `avformat_seek_file()` + `avcodec_flush_buffers()` for both codecs. Snap to nearest keyframe.

### 3.4 Post-Composite Blit Strategy

The current Radiant rendering pipeline uses a display list + tile-based parallel rasterization:

```
1. RECORD: Walk view tree → dl_* calls → fill DisplayList
2. REPLAY: TileGrid init → pool dispatch → workers replay per-tile → composite
3. Upload composite surface → GL texture → draw quad → swap buffers
```

Video frames bypass the display list and are blitted directly into the final composite surface **after step 2, before step 3**. This avoids copying video pixels into every overlapping tile's local buffer during parallel replay.

**Recording (step 1):**
- When the paint walk reaches a `<video>` view node, emit `DL_VIDEO_PLACEHOLDER` into the display list
- The placeholder records: layout rect (x, y, w, h), current clip rect (from `DL_PUSH_CLIP` stack), object-fit mode, and a pointer to the view's `RdtVideo*`
- During replay, `DL_VIDEO_PLACEHOLDER` fills the rect with black (or poster image pixels)

**Post-composite blit (new step 2.5):**
- After `tile_grid_composite()` assembles the final surface, iterate all `DL_VIDEO_PLACEHOLDER` items
- For each: call `rdt_video_get_frame()` to get the latest RGBA frame
- Blit frame pixels into `surface->pixels` at the layout rect, applying:
  - `object-fit` transform (contain/cover/fill/none/scale-down) — pre-computed once on layout, not per-frame
  - Clip rect intersection (handles `overflow: hidden`, scrolled containers)
  - `blit_surface_scaled()` for bilinear interpolation when frame size ≠ layout rect size
- This is the same pattern already used for raster images via `DL_BLIT_SURFACE_SCALED`

**Why not render video through the display list?**
- A 1080p RGBA frame = 8.3 MB. With tile-based replay, this frame would be partially copied into every overlapping 256×256 tile, then composited again into the final surface. Wasteful — the tiles are overwritten next frame anyway.
- Post-composite blit: single memcpy (or scaled blit) into the final surface. ~1ms for 1080p.

**Why not direct GL rendering?**
- Would require switching from the single-texture fullscreen quad to a multi-texture compositing pipeline with clip masking and z-ordering. Significant GL architecture change for marginal benefit at this stage.
- Deferred to future as a Phase 4+ optimisation for zero-copy 4K video.

### 3.5 Video-Only Dirty Optimisation

A playing video makes its rect "always dirty" — every frame. Without optimisation, this forces full display list replay (re-rasterize the entire page) 30-60 times per second, even though only the video rect changed.

**Optimisation:** Distinguish "video-dirty" from "layout-dirty":

| Dirty Type | Trigger | Action |
|------------|---------|--------|
| **Layout-dirty** | Reflow, CSS animation, scroll, hover | Full DL record + replay + video blit |
| **Video-dirty** | New video frame available, no other changes | Skip DL replay, only blit new video frames into existing surface, re-upload GL texture |

The `DirtyTracker` already supports region-level dirty marking. Extend it with a `video_only_dirty` flag:
- Set `video_only_dirty = true` when `rdt_video_get_frame()` returns a new frame and no layout/CSS/scroll change occurred
- In the render path: if `video_only_dirty && !layout_dirty`, skip `render_html_doc()` entirely, just blit video frames and call `repaint_window()`
- This avoids the ~21ms DL replay cost, reducing video-frame overhead to ~1ms (blit + GL upload)

### 3.6 Scroll and Visibility

When a video scrolls partially or fully off-screen:
- **Partially visible**: Blit is clipped to the viewport intersection. Audio continues.
- **Fully off-screen**: Skip the blit entirely (no pixel copy). Audio continues. Decode thread continues (video may scroll back into view).
- **`preload="none"` + off-screen**: Don't start decode until the element scrolls into view or play is explicitly triggered.

The existing `DirtyTracker` viewport culling can be extended to check video element rects against the viewport.

### 3.7 Resolution Capping

A 1080p RGBA frame = 8.3 MB. Double-buffered = 16.6 MB per video. Three 1080p videos = ~50 MB.

To limit memory, decode resolution is capped to the layout rect size:
- `rdt_video_set_layout_rect(video, css_width, css_height)` is called after layout resolves the `<video>` element dimensions
- On Win/Linux: `sws_scale()` / MF output type targets `target_width × target_height` instead of intrinsic dimensions
- On macOS: `AVPlayerItemVideoOutput` pixel buffer attributes set to target dimensions
- If layout rect changes (e.g., responsive resize), update target dimensions and let the next decoded frame pick up the new size
- Intrinsic dimensions (`rdt_video_get_width/height`) still return the original video dimensions for aspect ratio calculation

**Memory per video at layout rect size:**

| Layout Size | Frame Buffer (double) | vs. Full 1080p |
|-------------|----------------------|----------------|
| 300×150 (default) | 360 KB | 4.3% |
| 640×360 | 1.8 MB | 10.8% |
| 1280×720 | 7.4 MB | 44.6% |
| 1920×1080 | 16.6 MB | 100% |

### 3.8 Audio Architecture

Audio is not deferred — it ships with Phase 1 alongside video decode.

**Design principles:**
- **One audio session per platform.** macOS: AVFoundation manages this. Windows: one WASAPI `IAudioClient` per video (shared mode allows multiple). Linux: one PulseAudio/ALSA stream per video.
- **Audio thread is the playback thread.** Audio output has hard real-time constraints (~5ms underrun = glitch). The playback thread's primary job is feeding audio samples on time; video frame scheduling is secondary.
- **Ring buffer between decode and playback.** Decode thread writes PCM samples into a lock-free SPSC ring buffer. Playback thread reads and writes to the platform audio API.

**Audio pipeline per platform:**

| Platform | Decode → PCM | Ring Buffer | Playback → Output |
|----------|--------------|-------------|-------------------|
| **macOS** | AVFoundation handles internally | N/A | AVPlayer audio output (automatic) |
| **Windows** | MF `ReadSample(audio_stream)` → `IMFMediaBuffer` → S16LE/F32 PCM | SPSC ring, 64KB | Playback thread → `IAudioRenderClient::GetBuffer/ReleaseBuffer` |
| **Linux** | `avcodec_receive_frame(audio)` → `swr_convert()` → S16LE stereo | SPSC ring, 64KB | Playback thread → `pa_simple_write()` or `snd_pcm_writei()` |

**Volume and mute:**
- macOS: `AVPlayer.volume` / `AVPlayer.muted`
- Windows: `ISimpleAudioVolume::SetMasterVolume/SetMute` on the WASAPI session
- Linux: Scale PCM samples in the ring buffer read path (multiply by volume float). Mute = skip write to audio device but keep PTS advancing.

**Multiple videos:** Each video has its own audio output stream. OS-level audio mixing handles the final output. No application-level mixer needed.

### 3.9 Resource Lifecycle

| Event | Action |
|-------|--------|
| `<video>` element created in DOM | `rdt_video_create()` — allocate player, store in view node |
| `src` attribute set / changed | `rdt_video_open_file()` — open local file, begin metadata probe |
| `on_video_size_known` callback | Store intrinsic dimensions, trigger relayout, call `rdt_video_set_layout_rect()` |
| `autoplay` attribute present | `rdt_video_play()` after metadata is ready |
| Element removed from DOM | `rdt_video_destroy()` — stop playback, join threads, free decoder, release buffers |
| Page navigation | Destroy all active `RdtVideo` instances |
| Window close | Destroy all active `RdtVideo` instances (before GLFW teardown) |

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
        "windows": ["mfplat", "mfreadwrite", "mf", "mfuuid", "ole32"],
        "linux": ["avcodec", "avformat", "avutil", "swscale", "swresample", "pthread"]
    }
}
```

### 5.2 Linux FFmpeg Build

Add FFmpeg as a dependency in `setup-linux-deps.sh`:

```bash
# Build minimal FFmpeg for local video playback
FFMPEG_VERSION="7.1"
if [ ! -f "$PREFIX/lib/libavcodec.a" ]; then
    curl -sL "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz" | tar xJ
    cd "ffmpeg-${FFMPEG_VERSION}"
    ./configure \
        --prefix="$PREFIX" \
        --enable-static --disable-shared \
        --disable-programs --disable-doc \
        --disable-everything \
        --enable-decoder=h264,hevc,vp9,av1,aac,opus,pcm_s16le,pcm_f32le \
        --enable-demuxer=mov,matroska,webm,mp4,avi,wav \
        --enable-parser=h264,hevc,vp9,av1,aac,opus \
        --enable-protocol=file \
        --enable-swscale --enable-swresample \
        --disable-avdevice --disable-network \
        --enable-small --enable-lto \
        --cc=clang
    make -j$(nproc)
    make install
    cd ..
fi
```

---

## 6. Implementation Plan

### Phase 1 — Core Decode + Audio + Frame Delivery

- [x] Define `rdt_video.h` public API (as specified in §1.2) ✅
- [x] Implement `rdt_video_avf.mm` — open local file, decode, frame extraction, audio playback, play/pause/seek ✅
- [ ] Implement `rdt_video_mf.cpp` — same feature set with WASAPI audio, explicit decode + playback threads
- [ ] Implement `rdt_video_ffmpeg.cpp` — same feature set with PulseAudio/ALSA audio, explicit decode + playback threads
- [x] A/V sync: audio-clock-as-master on macOS (AVPlayer manages internally) ✅
- [x] Resolution capping via `rdt_video_set_layout_rect()` ✅
- [ ] Unit tests: decode a known MP4 (H.264+AAC), verify frame dimensions, pixel output, and audio sample delivery

### Phase 2 — Radiant Integration

- [x] Add `RdtVideo*` to view node (`view.hpp` `EmbedProp`) for `<video>` elements ✅
- [x] Parse `<video>` attributes in `resolve_htm_style.cpp` (src, width, height, autoplay, loop, muted) ✅
- [x] Add `DL_VIDEO_PLACEHOLDER` opcode to display list (records layout rect, clip region, object-fit, RdtVideo pointer) ✅
- [x] Implement `render_video.cpp` — post-composite blit: `rdt_video_get_frame()` → `blit_video_frame()` into final surface ✅
- [x] Integrate into `window.cpp` render loop: `has_active_video` flag drives continuous redraw ✅
- [ ] Video-only dirty optimisation: skip DL replay when only video frames changed
- [ ] Update intrinsic sizing with actual video dimensions on `on_video_size_known` callback
- [ ] Scroll/visibility: skip blit for off-viewport videos, continue audio

### Phase 3 — Controls + Polish

- [ ] Default `controls` UI overlay: play/pause button, seek bar, time display, volume slider
- [ ] `poster` attribute — display poster image before playback starts
- [x] `autoplay`, `loop`, `muted` attribute handling ✅
- [ ] `preload` attribute (none / metadata / auto)
- [ ] Seek with keyframe snapping + audio/video pipeline flush
- [ ] Error handling: unsupported codec → display error poster; missing file → error state
- [ ] Memory / resource cleanup on navigation and window close
- [ ] Performance profiling: verify <2ms post-composite blit overhead at 1080p

### Future — Web Video + Streaming (not in current scope)

- [ ] `rdt_video_open_url()` for HTTP/HTTPS sources via `resource_manager`
- [ ] Adaptive streaming (HLS/DASH)
- [ ] GL overlay texture for zero-copy 4K video
- [ ] `<audio>` standalone element
- [ ] Picture-in-picture

---

## 7. Implementation Notes (macOS)

The macOS AVFoundation backend (`rdt_video_avf.mm`) is fully implemented and working. Key lessons from implementation:

### 7.1 No-ARC Memory Management

The project does not use ARC (`-fobjc-arc`). All AVFoundation objects must be explicitly retained and released:
- `AVPlayerItem` and `AVPlayer` must use `[[... alloc] init...]` instead of convenience factory methods (`playerWithURL:`, `playerItemWithAsset:`) which return autoreleased objects that get freed when the `@autoreleasepool` exits.
- Observer tokens from `addPeriodicTimeObserverForInterval:` and `addObserverForName:` must be `retain`'d.
- `rdt_video_destroy()` must `release` all retained ObjC objects before freeing the struct.

### 7.2 KVO Does Not Fire Under GLFW

GLFW's event loop (`glfwWaitEvents`/`glfwPollEvents`) does not pump the NSRunLoop in the default mode needed for KVO delivery. As a result, KVO observers on `AVPlayerItem.status` and `AVPlayer.timeControlStatus` never fire.

**Solution:** `rdt_video_get_state()` polls `player_item.status` and `player.timeControlStatus` directly each time it's called from the render thread. On the first poll that sees `AVPlayerItemStatusReadyToPlay`, it performs the state transition inline (extract intrinsic dimensions, duration, audio track info). KVO observers remain registered as a fallback but are not relied upon.

### 7.3 Frame Caching to Prevent Flicker

AVFoundation's `hasNewPixelBufferForItemTime:` returns `false` most render frames (video decodes at ~30fps, render runs at ~60fps). If `rdt_video_get_frame()` returns -1 on "no new frame", the video rect shows the black CSS background, causing flicker.

**Solution:** When no new pixel buffer is available, return the previously decoded frame from the internal `frame_buffer` cache. The frame buffer persists between calls and is only updated when a genuinely new pixel buffer is available.

### 7.4 Relative URL Resolution

HTML `src` attributes use paths relative to the document (e.g., `"../media/test_video.mp4"`). These must be resolved to absolute file paths before passing to AVFoundation.

**Solution:** Use the same `parse_url(document->url, src)` + `url_to_local_path()` pattern used by `load_image()` in `surface.cpp`.

### 7.5 Files Modified

| File | Change |
|------|--------|
| `radiant/rdt_video.h` | New — public C API (lifecycle, playback control, audio control, state queries, frame retrieval) |
| `radiant/rdt_video_avf.mm` | New — macOS AVFoundation backend (~490 lines) |
| `radiant/render_video.cpp` | New — post-composite blit: scan DL for `DL_VIDEO_PLACEHOLDER`, blit frames with nearest-neighbor scaling + clipping |
| `radiant/display_list.h` | Added `DL_VIDEO_PLACEHOLDER` opcode, `DlVideoPlaceholder` struct |
| `radiant/display_list.cpp` | Added `dl_video_placeholder()` recording function |
| `radiant/tile_pool.cpp` | Added `DL_VIDEO_PLACEHOLDER` no-op cases in both tile replay switches |
| `radiant/view.hpp` | Added `RdtVideo* video` to `EmbedProp` |
| `radiant/state_store.hpp` | Added `bool has_active_video` to `RadiantState` |
| `radiant/render.cpp` | Added `render_video_content()` for DL placeholder recording; dispatch for video elements; post-composite `render_video_frames()` call |
| `radiant/resolve_htm_style.cpp` | `HTM_TAG_VIDEO` case: resolve src path, create `RdtVideo`, parse autoplay/loop/muted, set `has_active_video` |
| `radiant/window.cpp` | `has_active_video` forces continuous redraw |
| `build_lambda_config.json` | Added AVFoundation + CoreMedia frameworks; excluded `rdt_vector_cg.mm` from .mm glob |
| `utils/generate_premake.py` | Added `.mm` file globbing for macOS |
| `test/html/video_test.html` | New — test page with 3 `<video>` elements (autoplay+muted+loop, static, default size) |
| `test/media/test_video.mp4` | New — Big Buck Bunny 360p 10s H.264 test clip (~1MB) |

---

## 8. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| FFmpeg LGPL compliance | Legal | Static link is LGPL-compliant if object files are provided for relinking, or use shared linking on Linux only |
| A/V sync drift | User-visible | Audio clock as master; video frames dropped/held to match. PTS-based resync on seek. Proven pattern (VLC, mpv). |
| Audio underrun (glitch) | User-visible | Playback thread has real-time priority; 64KB ring buffer (~23ms at 44.1kHz stereo S16LE) provides margin; PulseAudio/WASAPI have their own internal buffers |
| High CPU on SW decode (Linux, no VA-API) | Performance | Cap decode resolution to layout rect size (§3.7); limit to 1080p SW decode; document VA-API as optional HW path |
| Audio API fragmentation on Linux | Compatibility | `dlopen()` PulseAudio with ALSA fallback; no hard link dependency |
| Video forcing full-page repaint every frame | Performance | Video-only dirty optimisation (§3.5): skip DL replay when only video frames changed, blit directly into existing surface |
| Memory pressure from multiple videos | Resources | Resolution capping (§3.7) limits frame buffers to layout rect size. Double-buffer (not ring) limits to 2 frames × layout pixels per video. |
| Thread lifecycle on element removal | Crash | `rdt_video_destroy()` sets `decode_running = false` + `playback_running = false`, then `pthread_join()` both threads before freeing any state. Atomic flags ensure clean shutdown. |
| Seek latency on large files | User-visible | Seek to nearest keyframe (not arbitrary frame). Flush both decode and audio pipelines. Accept ±0.5s accuracy for non-keyframe seeks. |
