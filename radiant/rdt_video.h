// rdt_video.h — Platform-agnostic video playback API for Radiant
//
// Three-tier threading model:
//   Decode thread  → demux + decode + colour convert
//   Playback thread → PTS scheduling + audio output + A/V sync
//   Render thread   → polls latest frame via rdt_video_get_frame()
//
// macOS:  AVFoundation manages decode + playback internally.
// Windows: Media Foundation decode thread + WASAPI playback thread.
// Linux:  FFmpeg decode thread + PulseAudio/ALSA playback thread.

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

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

RdtVideo*       rdt_video_create(const RdtVideoCallbacks* cb, void* userdata);
void            rdt_video_destroy(RdtVideo* video);

// ---------------------------------------------------------------------------
// Source — local file path only (web URLs deferred to future)
// ---------------------------------------------------------------------------

int             rdt_video_open_file(RdtVideo* video, const char* file_path);

// ---------------------------------------------------------------------------
// Layout rect — decode resolution capped to this size to limit memory.
// Call on layout change. Width/height in physical pixels.
// ---------------------------------------------------------------------------

void            rdt_video_set_layout_rect(RdtVideo* video, int width, int height);

// ---------------------------------------------------------------------------
// Playback control
// ---------------------------------------------------------------------------

void            rdt_video_play(RdtVideo* video);
void            rdt_video_pause(RdtVideo* video);
void            rdt_video_seek(RdtVideo* video, double seconds);
void            rdt_video_set_loop(RdtVideo* video, bool loop);

// ---------------------------------------------------------------------------
// Audio control
// ---------------------------------------------------------------------------

void            rdt_video_set_volume(RdtVideo* video, float volume);  // 0.0–1.0
void            rdt_video_set_muted(RdtVideo* video, bool muted);

// ---------------------------------------------------------------------------
// Query — all thread-safe, lock-free reads
// ---------------------------------------------------------------------------

RdtVideoState   rdt_video_get_state(RdtVideo* video);
double          rdt_video_get_current_time(RdtVideo* video);
double          rdt_video_get_duration(RdtVideo* video);
int             rdt_video_get_width(RdtVideo* video);   // intrinsic video width
int             rdt_video_get_height(RdtVideo* video);  // intrinsic video height
bool            rdt_video_has_audio(RdtVideo* video);

// ---------------------------------------------------------------------------
// Frame retrieval — returns the latest decoded frame.
// Copies into caller-owned buffer. Returns 0 on success, -1 if no frame.
// The playback thread manages PTS scheduling; this always returns the current frame.
// ---------------------------------------------------------------------------

int             rdt_video_get_frame(RdtVideo* video, RdtVideoFrame* frame);

#ifdef __cplusplus
}
#endif
