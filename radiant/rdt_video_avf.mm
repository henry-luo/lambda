// rdt_video_avf.mm — macOS AVFoundation backend for RdtVideo
//
// AVFoundation manages decode + playback scheduling on internal dispatch queues.
// We poll for new video frames from the render thread and let AVPlayer handle audio.

#ifdef __APPLE__

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <objc/runtime.h>

#include "rdt_video.h"
#include "../lib/log.h"
#include "../lib/mem.h"

#include <stdatomic.h>

// ---------------------------------------------------------------------------
// Internal struct
// ---------------------------------------------------------------------------

struct RdtVideo {
    // AVFoundation objects
    AVPlayer*                    player;
    AVPlayerItem*                player_item;
    AVPlayerItemVideoOutput*     video_output;
    id                           end_observer;

    // Frame double-buffer (atomic swap between AVF output and render thread)
    uint8_t*                     frame_buffer;       // single RGBA buffer for latest frame
    int                          frame_width;
    int                          frame_height;
    int                          frame_stride;
    atomic_bool                  has_new_frame;

    // Decode resolution cap
    int                          target_width;
    int                          target_height;

    // State
    RdtVideoCallbacks            callbacks;
    void*                        userdata;
    atomic_int                   state;
    atomic_int                   current_time_ms;    // milliseconds, for lock-free read
    double                       duration;
    int                          intrinsic_width;
    int                          intrinsic_height;
    bool                         has_audio_track;
    bool                         loop;
    bool                         muted;
    float                        volume;

    // KVO observation tracking
    bool                         observing_status;
    bool                         observing_time_control;

    // Periodic time observer token
    id                           time_observer;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void set_state(RdtVideo* v, RdtVideoState new_state) {
    atomic_store(&v->state, (int)new_state);
    if (v->callbacks.on_state_changed) {
        v->callbacks.on_state_changed(v, new_state, v->userdata);
    }
}

// ---------------------------------------------------------------------------
// KVO observer for AVPlayerItem.status and AVPlayer.timeControlStatus
// ---------------------------------------------------------------------------

@interface RdtVideoObserver : NSObject
@property (assign) RdtVideo* video;
@end

@implementation RdtVideoObserver

- (void)observeValueForKeyPath:(NSString*)keyPath
                      ofObject:(id)object
                        change:(NSDictionary*)change
                       context:(void*)context {
    RdtVideo* v = self.video;
    if (!v) return;

    if ([keyPath isEqualToString:@"status"]) {
        AVPlayerItem* item = (AVPlayerItem*)object;
        if (item.status == AVPlayerItemStatusReadyToPlay) {
            // extract video dimensions
            NSArray* tracks = [item.asset tracksWithMediaType:AVMediaTypeVideo];
            if (tracks.count > 0) {
                AVAssetTrack* vt = tracks[0];
                CGSize size = vt.naturalSize;
                v->intrinsic_width = (int)size.width;
                v->intrinsic_height = (int)size.height;
                log_info("video: intrinsic size %dx%d", v->intrinsic_width, v->intrinsic_height);
                if (v->callbacks.on_video_size_known) {
                    v->callbacks.on_video_size_known(v, v->intrinsic_width, v->intrinsic_height, v->userdata);
                }
            }
            // check for audio
            NSArray* audio_tracks = [item.asset tracksWithMediaType:AVMediaTypeAudio];
            v->has_audio_track = (audio_tracks.count > 0);
            log_info("video: has_audio=%d", v->has_audio_track);

            // duration
            CMTime dur = item.duration;
            if (CMTIME_IS_VALID(dur) && !CMTIME_IS_INDEFINITE(dur)) {
                v->duration = CMTimeGetSeconds(dur);
                if (v->callbacks.on_duration_known) {
                    v->callbacks.on_duration_known(v, v->duration, v->userdata);
                }
            }

            set_state(v, RDT_VIDEO_STATE_READY);
        } else if (item.status == AVPlayerItemStatusFailed) {
            log_error("video: AVPlayerItem failed: %s",
                      item.error ? [[item.error localizedDescription] UTF8String] : "unknown");
            set_state(v, RDT_VIDEO_STATE_ERROR);
        }
    } else if ([keyPath isEqualToString:@"timeControlStatus"]) {
        AVPlayer* player = (AVPlayer*)object;
        if (player.timeControlStatus == AVPlayerTimeControlStatusPlaying) {
            set_state(v, RDT_VIDEO_STATE_PLAYING);
        } else if (player.timeControlStatus == AVPlayerTimeControlStatusPaused) {
            RdtVideoState cur = (RdtVideoState)atomic_load(&v->state);
            // don't override ENDED or READY with PAUSED
            if (cur == RDT_VIDEO_STATE_PLAYING) {
                set_state(v, RDT_VIDEO_STATE_PAUSED);
            }
        }
    }
}

@end

// We store the observer as an associated object on the player
static RdtVideoObserver* get_or_create_observer(RdtVideo* v) {
    static char kObserverKey;
    RdtVideoObserver* obs = objc_getAssociatedObject(v->player, &kObserverKey);
    if (!obs) {
        obs = [[RdtVideoObserver alloc] init];
        obs.video = v;
        objc_setAssociatedObject(v->player, &kObserverKey, obs, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    return obs;
}

// ---------------------------------------------------------------------------
// Public API implementation
// ---------------------------------------------------------------------------

RdtVideo* rdt_video_create(const RdtVideoCallbacks* cb, void* userdata) {
    RdtVideo* v = (RdtVideo*)mem_calloc(1, sizeof(RdtVideo), MEM_CAT_RENDER);
    if (cb) v->callbacks = *cb;
    v->userdata = userdata;
    atomic_store(&v->state, (int)RDT_VIDEO_STATE_IDLE);
    v->volume = 1.0f;
    v->target_width = 300;
    v->target_height = 150;
    return v;
}

void rdt_video_destroy(RdtVideo* video) {
    if (!video) return;

    // remove observers
    if (video->player) {
        RdtVideoObserver* obs = get_or_create_observer(video);
        if (video->observing_time_control) {
            [video->player removeObserver:obs forKeyPath:@"timeControlStatus"];
            video->observing_time_control = false;
        }
        if (video->time_observer) {
            [video->player removeTimeObserver:video->time_observer];
            video->time_observer = nil;
        }
    }
    if (video->player_item && video->observing_status) {
        RdtVideoObserver* obs = get_or_create_observer(video);
        [video->player_item removeObserver:obs forKeyPath:@"status"];
        video->observing_status = false;
    }
    if (video->end_observer) {
        [[NSNotificationCenter defaultCenter] removeObserver:video->end_observer];
        video->end_observer = nil;
    }

    // stop playback
    if (video->player) {
        [video->player pause];
    }

    // No ARC — explicitly release retained ObjC objects
    if (video->time_observer) {
        [video->time_observer release];
        video->time_observer = nil;
    }
    if (video->end_observer) {
        [video->end_observer release];
        video->end_observer = nil;
    }
    [video->video_output release];
    video->video_output = nil;
    [video->player release];
    video->player = nil;
    [video->player_item release];
    video->player_item = nil;

    // free frame buffer
    if (video->frame_buffer) {
        mem_free(video->frame_buffer);
        video->frame_buffer = NULL;
    }

    mem_free(video);
}

int rdt_video_open_file(RdtVideo* video, const char* file_path) {
    if (!video || !file_path) return -1;

    @autoreleasepool {
        NSString* path = [NSString stringWithUTF8String:file_path];
        NSURL* url = [NSURL fileURLWithPath:path];
        if (!url) {
            log_error("video: invalid file path: %s", file_path);
            set_state(video, RDT_VIDEO_STATE_ERROR);
            return -1;
        }

        log_info("video: opening file %s", file_path);
        set_state(video, RDT_VIDEO_STATE_LOADING);

        // create player item and player
        // NOTE: No ARC — use alloc/init to get +1 retained references
        AVAsset* asset = [AVAsset assetWithURL:url];
        video->player_item = [[AVPlayerItem alloc] initWithAsset:asset];
        video->player = [[AVPlayer alloc] initWithPlayerItem:video->player_item];
        video->player.volume = video->muted ? 0.0f : video->volume;

        // set up video output for frame extraction
        NSDictionary* attrs = @{
            (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
        };
        video->video_output = [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:attrs];
        [video->player_item addOutput:video->video_output];

        // observe status and time control
        RdtVideoObserver* obs = get_or_create_observer(video);
        [video->player_item addObserver:obs forKeyPath:@"status" options:0 context:NULL];
        video->observing_status = true;
        [video->player addObserver:obs forKeyPath:@"timeControlStatus" options:0 context:NULL];
        video->observing_time_control = true;

        // periodic time observer (10Hz)
        __block RdtVideo* weakVideo = video;
        // NOTE: No ARC — retain the returned observer token
        video->time_observer = [[video->player addPeriodicTimeObserverForInterval:CMTimeMake(1, 10)
                                                                          queue:dispatch_get_main_queue()
                                                                     usingBlock:^(CMTime time) {
            if (weakVideo) {
                double t = CMTimeGetSeconds(time);
                atomic_store(&weakVideo->current_time_ms, (int)(t * 1000.0));
            }
        }] retain];

        // end-of-playback notification
        // NOTE: No ARC — retain the returned observer token
        video->end_observer = [[[NSNotificationCenter defaultCenter]
            addObserverForName:AVPlayerItemDidPlayToEndTimeNotification
                       object:video->player_item
                        queue:[NSOperationQueue mainQueue]
                   usingBlock:^(NSNotification* note) {
            if (weakVideo) {
                if (weakVideo->loop) {
                    [weakVideo->player seekToTime:kCMTimeZero];
                    [weakVideo->player play];
                } else {
                    set_state(weakVideo, RDT_VIDEO_STATE_ENDED);
                }
            }
        }] retain];
    }

    return 0;
}

void rdt_video_set_layout_rect(RdtVideo* video, int width, int height) {
    if (!video) return;
    video->target_width = width > 0 ? width : 300;
    video->target_height = height > 0 ? height : 150;
}

void rdt_video_play(RdtVideo* video) {
    if (!video || !video->player) return;
    [video->player play];
}

void rdt_video_pause(RdtVideo* video) {
    if (!video || !video->player) return;
    [video->player pause];
}

void rdt_video_seek(RdtVideo* video, double seconds) {
    if (!video || !video->player) return;
    CMTime target = CMTimeMakeWithSeconds(seconds, NSEC_PER_SEC);
    [video->player seekToTime:target toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
}

void rdt_video_set_loop(RdtVideo* video, bool loop) {
    if (!video) return;
    video->loop = loop;
}

void rdt_video_set_volume(RdtVideo* video, float volume) {
    if (!video) return;
    video->volume = volume;
    if (video->player && !video->muted) {
        video->player.volume = volume;
    }
}

void rdt_video_set_muted(RdtVideo* video, bool muted) {
    if (!video) return;
    video->muted = muted;
    if (video->player) {
        video->player.volume = muted ? 0.0f : video->volume;
    }
}

float rdt_video_get_volume(RdtVideo* video) {
    if (!video) return 1.0f;
    return video->volume;
}

RdtVideoState rdt_video_get_state(RdtVideo* video) {
    if (!video) return RDT_VIDEO_STATE_IDLE;

    // Poll AVFoundation status directly (KVO may not fire without a run loop)
    RdtVideoState cur = (RdtVideoState)atomic_load(&video->state);
    if (cur == RDT_VIDEO_STATE_LOADING && video->player_item) {
        if (video->player_item.status == AVPlayerItemStatusReadyToPlay) {
            // perform the transition that KVO would have done
            NSArray* tracks = [video->player_item.asset tracksWithMediaType:AVMediaTypeVideo];
            if (tracks.count > 0) {
                AVAssetTrack* vt = tracks[0];
                CGSize size = vt.naturalSize;
                video->intrinsic_width = (int)size.width;
                video->intrinsic_height = (int)size.height;
                log_info("video: intrinsic size %dx%d (polled)", video->intrinsic_width, video->intrinsic_height);
            }
            NSArray* audio_tracks = [video->player_item.asset tracksWithMediaType:AVMediaTypeAudio];
            video->has_audio_track = (audio_tracks.count > 0);
            CMTime dur = video->player_item.duration;
            if (CMTIME_IS_VALID(dur) && !CMTIME_IS_INDEFINITE(dur)) {
                video->duration = CMTimeGetSeconds(dur);
            }
            set_state(video, RDT_VIDEO_STATE_READY);
            cur = RDT_VIDEO_STATE_READY;
        } else if (video->player_item.status == AVPlayerItemStatusFailed) {
            log_error("video: AVPlayerItem failed (polled): %s",
                      video->player_item.error ? [[video->player_item.error localizedDescription] UTF8String] : "unknown");
            set_state(video, RDT_VIDEO_STATE_ERROR);
            cur = RDT_VIDEO_STATE_ERROR;
        }
    }

    // also poll timeControlStatus for playing/paused transitions
    if (video->player && (cur == RDT_VIDEO_STATE_READY || cur == RDT_VIDEO_STATE_PLAYING || cur == RDT_VIDEO_STATE_PAUSED)) {
        if (video->player.timeControlStatus == AVPlayerTimeControlStatusPlaying) {
            if (cur != RDT_VIDEO_STATE_PLAYING) {
                set_state(video, RDT_VIDEO_STATE_PLAYING);
                cur = RDT_VIDEO_STATE_PLAYING;
            }
        } else if (video->player.timeControlStatus == AVPlayerTimeControlStatusPaused) {
            if (cur == RDT_VIDEO_STATE_PLAYING) {
                set_state(video, RDT_VIDEO_STATE_PAUSED);
                cur = RDT_VIDEO_STATE_PAUSED;
            }
        }
    }

    return cur;
}

double rdt_video_get_current_time(RdtVideo* video) {
    if (!video) return 0.0;
    return (double)atomic_load(&video->current_time_ms) / 1000.0;
}

double rdt_video_get_duration(RdtVideo* video) {
    if (!video) return 0.0;
    return video->duration;
}

int rdt_video_get_width(RdtVideo* video) {
    if (!video) return 0;
    return video->intrinsic_width;
}

int rdt_video_get_height(RdtVideo* video) {
    if (!video) return 0;
    return video->intrinsic_height;
}

bool rdt_video_has_audio(RdtVideo* video) {
    if (!video) return false;
    return video->has_audio_track;
}

int rdt_video_get_frame(RdtVideo* video, RdtVideoFrame* frame) {
    if (!video || !frame || !video->video_output || !video->player_item) return -1;

    @autoreleasepool {
        CMTime current = [video->player_item currentTime];
        if (![video->video_output hasNewPixelBufferForItemTime:current]) {
            // no new frame — return the cached frame to avoid flicker
            if (video->frame_buffer && video->frame_width > 0 && video->frame_height > 0) {
                frame->pixels = video->frame_buffer;
                frame->width  = video->frame_width;
                frame->height = video->frame_height;
                frame->stride = video->frame_stride;
                frame->pts    = CMTimeGetSeconds(current);
                return 0;
            }
            return -1;
        }

        CVPixelBufferRef pb = [video->video_output copyPixelBufferForItemTime:current
                                                           itemTimeForDisplay:NULL];
        if (!pb) return -1;

        CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);

        int src_w = (int)CVPixelBufferGetWidth(pb);
        int src_h = (int)CVPixelBufferGetHeight(pb);
        int src_stride = (int)CVPixelBufferGetBytesPerRow(pb);
        uint8_t* src_pixels = (uint8_t*)CVPixelBufferGetBaseAddress(pb);

        // determine output dimensions (cap to target layout rect)
        int out_w = src_w;
        int out_h = src_h;
        if (video->target_width > 0 && video->target_height > 0) {
            if (out_w > video->target_width || out_h > video->target_height) {
                float scale_x = (float)video->target_width / (float)out_w;
                float scale_y = (float)video->target_height / (float)out_h;
                float scale = scale_x < scale_y ? scale_x : scale_y;
                out_w = (int)(out_w * scale);
                out_h = (int)(out_h * scale);
            }
        }

        // for now, use source dimensions directly (no resize in get_frame; 
        // resize happens during blit_surface_scaled in the render path)
        out_w = src_w;
        out_h = src_h;
        int out_stride = out_w * 4;

        // allocate/reallocate internal frame buffer if needed
        int needed = out_stride * out_h;
        if (!video->frame_buffer || video->frame_width != out_w || video->frame_height != out_h) {
            if (video->frame_buffer) mem_free(video->frame_buffer);
            video->frame_buffer = (uint8_t*)mem_alloc(needed, MEM_CAT_RENDER);
            video->frame_width = out_w;
            video->frame_height = out_h;
            video->frame_stride = out_stride;
        }

        // convert BGRA → RGBA
        for (int y = 0; y < out_h; y++) {
            uint8_t* src_row = src_pixels + y * src_stride;
            uint8_t* dst_row = video->frame_buffer + y * out_stride;
            for (int x = 0; x < out_w; x++) {
                dst_row[x * 4 + 0] = src_row[x * 4 + 2]; // R = B
                dst_row[x * 4 + 1] = src_row[x * 4 + 1]; // G = G
                dst_row[x * 4 + 2] = src_row[x * 4 + 0]; // B = R
                dst_row[x * 4 + 3] = src_row[x * 4 + 3]; // A = A
            }
        }

        CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
        CVBufferRelease(pb);

        // fill output frame
        frame->pixels = video->frame_buffer;
        frame->width = out_w;
        frame->height = out_h;
        frame->stride = out_stride;
        frame->pts = CMTimeGetSeconds(current);

        atomic_store(&video->has_new_frame, false);
    }

    return 0;
}

#endif // __APPLE__
