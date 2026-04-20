// rdt_video_stub.cpp — Stub video playback for platforms without a backend
// On macOS: rdt_video_avf.mm (AVFoundation)
// On Linux:  rdt_video_ffmpeg.cpp (FFmpeg) — not yet implemented, using stubs
// On Windows: rdt_video_mf.cpp (Media Foundation) — not yet implemented, using stubs

#include "rdt_video.h"
#include <stdlib.h>

struct RdtVideo {
    RdtVideoState state;
};

extern "C" {

RdtVideo* rdt_video_create(const RdtVideoCallbacks* cb, void* userdata) {
    (void)cb; (void)userdata;
    RdtVideo* v = (RdtVideo*)calloc(1, sizeof(RdtVideo));
    if (v) v->state = RDT_VIDEO_STATE_IDLE;
    return v;
}

void rdt_video_destroy(RdtVideo* video) {
    free(video);
}

int rdt_video_open_file(RdtVideo* video, const char* file_path) {
    (void)video; (void)file_path;
    return -1;
}

void rdt_video_set_layout_rect(RdtVideo* video, int width, int height) {
    (void)video; (void)width; (void)height;
}

void rdt_video_play(RdtVideo* video) { (void)video; }
void rdt_video_pause(RdtVideo* video) { (void)video; }

void rdt_video_seek(RdtVideo* video, double seconds) {
    (void)video; (void)seconds;
}

void rdt_video_set_loop(RdtVideo* video, bool loop) {
    (void)video; (void)loop;
}

void rdt_video_set_volume(RdtVideo* video, float volume) {
    (void)video; (void)volume;
}

void rdt_video_set_muted(RdtVideo* video, bool muted) {
    (void)video; (void)muted;
}

RdtVideoState rdt_video_get_state(RdtVideo* video) {
    if (!video) return RDT_VIDEO_STATE_IDLE;
    return video->state;
}

double rdt_video_get_current_time(RdtVideo* video) {
    (void)video;
    return 0.0;
}

double rdt_video_get_duration(RdtVideo* video) {
    (void)video;
    return 0.0;
}

int rdt_video_get_width(RdtVideo* video) {
    (void)video;
    return 0;
}

int rdt_video_get_height(RdtVideo* video) {
    (void)video;
    return 0;
}

bool rdt_video_has_audio(RdtVideo* video) {
    (void)video;
    return false;
}

float rdt_video_get_volume(RdtVideo* video) {
    (void)video;
    return 0.0f;
}

int rdt_video_get_frame(RdtVideo* video, RdtVideoFrame* frame) {
    (void)video; (void)frame;
    return -1;
}

} // extern "C"
