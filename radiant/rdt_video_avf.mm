// rdt_video_avf.mm — macOS AVFoundation backend for RdtVideo
//
// AVFoundation manages decode + playback scheduling on internal dispatch queues.
// The shared document bus consumes bounded PCM; video frames follow its consumed sample time.

#ifdef __APPLE__

#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <objc/runtime.h>

#include "rdt_video.h"
#include "audio_bus.hpp"
#include "audio_stream.hpp"
#include "audio.hpp"
#include "../lib/log.h"
#include "../lib/mem.h"
#include "../lib/ownership.hpp"

#include <math.h>
#include <stdatomic.h>
#include <string.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

extern "C" void rdt_video_pump_events(void) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.0, true);
}

static bool media_mime_token(unichar ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') ||
        (ch && ch < 128 && strchr("!#$%&'*+-.^_`|~", ch));
}

extern "C" const char* rdt_video_can_play_type(const char* mime_type, size_t length) {
    if (!mime_type || !length) return "";
    @autoreleasepool {
        NSString* input = [[[NSString alloc] initWithBytes:mime_type length:length
            encoding:NSUTF8StringEncoding] autorelease];
        if (!input) return "";
        NSCharacterSet* whitespace = [NSCharacterSet characterSetWithCharactersInString:@" \t\r\n"];
        input = [input stringByTrimmingCharactersInSet:whitespace];
        NSUInteger position = 0, count = input.length;
        while (position < count && [input characterAtIndex:position] != '/') {
            if (!media_mime_token([input characterAtIndex:position])) return "";
            position++;
        }
        if (!position || position == count) return "";
        NSUInteger subtype_start = ++position;
        while (position < count && [input characterAtIndex:position] != ';') position++;
        NSUInteger essence_end = position;
        while (essence_end > subtype_start &&
                [whitespace characterIsMember:[input characterAtIndex:essence_end - 1]]) essence_end--;
        if (essence_end == subtype_start) return "";
        for (NSUInteger index = subtype_start; index < essence_end; index++)
            if (!media_mime_token([input characterAtIndex:index])) return "";
        NSString* essence = [[input substringToIndex:essence_end] lowercaseString];
        if (![[AVURLAsset audiovisualMIMETypes] containsObject:essence]) return "";

        // parse MIME parameters with §4.4's quoted escapes, invalid-parameter skipping, and first-codecs rule.
        NSString* codecs = nil;
        while (position < count) {
            position++;
            while (position < count && [whitespace characterIsMember:[input characterAtIndex:position]]) position++;
            NSUInteger name_start = position;
            bool name_valid = true;
            while (position < count) {
                unichar ch = [input characterAtIndex:position];
                if (ch == ';' || ch == '=') break;
                name_valid &= media_mime_token(ch);
                position++;
            }
            NSString* name = [[input substringWithRange:NSMakeRange(name_start, position - name_start)] lowercaseString];
            if (position == count) break;
            if ([input characterAtIndex:position] == ';') continue;
            if (++position == count) break;
            NSMutableString* value = [NSMutableString string];
            if ([input characterAtIndex:position] == '"') {
                position++;
                while (position < count) {
                    unichar ch = [input characterAtIndex:position++];
                    if (ch == '"') break;
                    if (ch == '\\' && position < count) ch = [input characterAtIndex:position++];
                    [value appendString:[NSString stringWithCharacters:&ch length:1]];
                }
                while (position < count && [input characterAtIndex:position] != ';') position++;
            } else {
                NSUInteger value_start = position;
                while (position < count && [input characterAtIndex:position] != ';') position++;
                NSUInteger value_end = position;
                while (value_end > value_start &&
                        [whitespace characterIsMember:[input characterAtIndex:value_end - 1]]) value_end--;
                if (value_end == value_start) continue;
                [value appendString:[input substringWithRange:NSMakeRange(value_start, value_end - value_start)]];
            }
            bool value_valid = true;
            for (NSUInteger index = 0; index < value.length; index++) {
                unichar ch = [value characterAtIndex:index];
                value_valid &= ch == '\t' || (ch >= 32 && ch <= 126) || (ch >= 128 && ch <= 255);
            }
            if (!codecs && name_valid && value_valid && [name isEqualToString:@"codecs"]) codecs = value;
        }
        if (!codecs || !codecs.length) return "maybe";
        NSString* escaped = [[codecs stringByReplacingOccurrencesOfString:@"\\" withString:@"\\\\"]
            stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""];
        NSString* extended = [NSString stringWithFormat:@"%@; codecs=\"%@\"", essence, escaped];
        // query the same decoder used for playback; unsupported codecs must not select that source.
        return [AVURLAsset isPlayableExtendedMIMEType:extended] ? "probably" : "";
    }
}

// ---------------------------------------------------------------------------
// Internal struct
// ---------------------------------------------------------------------------

struct RdtVideo {
    RdtVideoAudioBinding          audio_binding;
    AudioStream*                 pcm;
    AudioVoiceId                 audio_voice;
    AudioRouteId                 audio_route;
    AudioRouteRef                audio_routing;
    AudioVoiceLoss               audio_loss;
    AVAssetReader*               audio_reader;
    AVAssetReaderTrackOutput*    audio_output;
    CMSampleBufferRef            audio_packet;
    unsigned                     packet_begin, packet_offset, packet_frames;
    uint64_t                     packet_frame, loop_base, media_frames;
    bool                         audio_eof, audio_reserved, play_requested;
    // AVFoundation objects
    AVAsset*                     asset;
    AVPlayer*                    player;
    AVPlayerItem*                player_item;
    AVAssetImageGenerator*       image_generator;
    id                           end_observer;

    // Frame double-buffer (atomic swap between AVF output and render thread)
    uint8_t*                     frame_buffer;       // single RGBA buffer for latest frame
    int                          frame_width;
    int                          frame_height;
    int                          frame_stride;
    atomic_bool                  has_new_frame;
    atomic_int                   last_generated_frame_ms;
    atomic_int                   last_notified_frame_ms;

    // Decode resolution cap
    int                          target_width;
    int                          target_height;

    // State
    RdtVideoCallbacks            callbacks;
    void*                        userdata;
    atomic_int                   state;
    atomic_int                   current_time_ms;    // milliseconds, for lock-free read
    double                       duration;
    double                       requested_time;
    int                          intrinsic_width;
    int                          intrinsic_height;
    bool                         has_audio_track;
    bool                         loop;
    bool                         muted;
    float                        volume;
    bool                         metadata_sent;

    // KVO observation tracking
    bool                         observing_status;
    bool                         observing_time_control;

    // Periodic time observer token
    id                           time_observer;
};

static void set_state(RdtVideo* video, RdtVideoState state);
static void video_pcm_reader_close(RdtVideo* video) {
    if (video->audio_packet) CFRelease(video->audio_packet);
    video->audio_packet = nullptr;
    [video->audio_reader cancelReading]; [video->audio_reader release]; [video->audio_output release];
    video->audio_reader = nil; video->audio_output = nil;
}
static void video_pcm_close(RdtVideo* video) {
    audio_voice_remove(video->audio_voice); video->audio_voice = 0;
    video_pcm_reader_close(video); audio_stream_destroy(video->pcm); video->pcm = nullptr;
    if (video->audio_reserved && video->audio_binding.release) video->audio_binding.release(video->audio_binding.owner);
    video->audio_reserved = false;
}
static bool video_pcm_error(RdtVideo* video, const char* reason) {
    audio_voice_remove(video->audio_voice); video->audio_voice = 0;
    audio_stream_fail(video->pcm); log_error("video_pcm: %s", reason);
    set_state(video, RDT_VIDEO_STATE_ERROR); return false;
}
static bool video_pcm_reader_open(RdtVideo* video, uint64_t frame) {
    video_pcm_reader_close(video); video->audio_eof = false;
    NSArray* tracks = [video->asset tracksWithMediaType:AVMediaTypeAudio];
    if (!tracks.count) return video_pcm_error(video, "audio track is unavailable");
    NSError* error = nil;
    video->audio_reader = [[AVAssetReader alloc] initWithAsset:video->asset error:&error];
    NSDictionary* settings = @{AVFormatIDKey:@(kAudioFormatLinearPCM), AVSampleRateKey:@(AUDIO_BUS_RATE),
        AVNumberOfChannelsKey:@2, AVLinearPCMBitDepthKey:@32, AVLinearPCMIsFloatKey:@YES,
        AVLinearPCMIsBigEndianKey:@NO, AVLinearPCMIsNonInterleaved:@NO};
    video->audio_output = [[AVAssetReaderTrackOutput alloc] initWithTrack:tracks[0] outputSettings:settings];
    if (!video->audio_reader || ![video->audio_reader canAddOutput:video->audio_output])
        return video_pcm_error(video, "audio decoder admission failed");
    [video->audio_reader addOutput:video->audio_output];
    video->audio_reader.timeRange = CMTimeRangeMake(CMTimeMake((int64_t)frame, AUDIO_BUS_RATE), kCMTimePositiveInfinity);
    if (![video->audio_reader startReading]) return video_pcm_error(video, "audio decoder could not start");
    return true;
}
static bool video_pcm_seek(RdtVideo* video, uint64_t frame) {
    audio_voice_remove(video->audio_voice); video->audio_voice = 0;
    audio_stream_reset(video->pcm, frame); video->loop_base = 0;
    return video_pcm_reader_open(video, frame);
}
static bool video_pcm_prepare(RdtVideo* video) {
    if (video->pcm) return true;
    if (!video->audio_binding.submix || !isfinite(video->duration) || video->duration <= 0 || video->duration > 86400)
        return video_pcm_error(video, "missing document submix or unsupported finite media duration");
    if (video->audio_binding.reserve) {
        if (!video->audio_binding.reserve(video->audio_binding.owner, audio_stream_storage_bytes()))
            return video_pcm_error(video, "document PCM quota exceeded");
        video->audio_reserved = true;
    }
    video->pcm = audio_stream_create();
    if (!video->pcm) { video_pcm_close(video); return video_pcm_error(video, "PCM allocation failed"); }
    video->media_frames = (uint64_t)ceil(video->duration * AUDIO_BUS_RATE);
    audio_stream_gain(video->pcm, video->muted ? 0 : video->volume);
    // the initial seek belongs to the PCM clock; a silent AVPlayer cannot transfer that cursor.
    double start = video->requested_time < video->duration ? video->requested_time : video->duration;
    return video_pcm_seek(video, (uint64_t)llround(start * AUDIO_BUS_RATE));
}
static bool video_pcm_fill(RdtVideo* video) {
    if (!video->pcm) return true;
    // decoding is control-thread work; the output callback reads only the bounded PCM ring.
    for (unsigned work = 0; work < 64; work++) {
        auto info = audio_stream_snapshot(video->pcm);
        if (info.state == AUDIO_STREAM_ERROR) return video_pcm_error(video, "stream PCM validation failed");
        if (info.finished || info.queued == info.capacity) return true;
        if (video->audio_packet) {
            unsigned count = video->packet_frames - video->packet_offset;
            if (count > 4096) count = 4096;
            float samples[4096 * 2];
            CMBlockBufferRef data = CMSampleBufferGetDataBuffer(video->audio_packet);
            if (!data || CMBlockBufferCopyDataBytes(data, (size_t)video->packet_offset * 8, (size_t)count * 8, samples) != noErr)
                return video_pcm_error(video, "invalid decoded PCM buffer");
            unsigned consumed = audio_stream_write(video->pcm, video->packet_frame + video->packet_offset - video->packet_begin, samples, count);
            video->packet_offset += consumed;
            if (video->packet_offset == video->packet_frames) { CFRelease(video->audio_packet); video->audio_packet = nullptr; }
            if (!consumed) return true;
            continue;
        }
        if (video->audio_eof) {
            uint64_t end = video->loop_base + video->media_frames;
            audio_stream_write(video->pcm, end, nullptr, 0);
            if (audio_stream_snapshot(video->pcm).written != end) return true;
            if (!video->loop) { audio_stream_finish(video->pcm); return true; }
            if (end > INT64_MAX - video->media_frames) return video_pcm_error(video, "loop sample clock exhausted");
            video->loop_base = end;
            if (!video_pcm_reader_open(video, 0)) return false;
            continue;
        }
        video->audio_packet = [video->audio_output copyNextSampleBuffer];
        if (!video->audio_packet) {
            if (video->audio_reader.status != AVAssetReaderStatusCompleted) return video_pcm_error(video, "audio decoder failed before EOF");
            video->audio_eof = true; continue;
        }
        CMItemCount frames = CMSampleBufferGetNumSamples(video->audio_packet);
        CMTime pts = CMSampleBufferGetPresentationTimeStamp(video->audio_packet);
        if (!CMTIME_IS_NUMERIC(pts) || frames <= 0 || frames > AUDIO_STREAM_PACKET)
            return video_pcm_error(video, "decoded PCM packet exceeds the admitted profile");
        int64_t first = CMTimeConvertScale(pts, AUDIO_BUS_RATE, kCMTimeRoundingMethod_RoundHalfAwayFromZero).value;
        if (first <= -frames || first >= (int64_t)video->media_frames) {
            CFRelease(video->audio_packet); video->audio_packet = nullptr; continue;
        }
        unsigned begin = first < 0 ? (unsigned)-first : 0;
        uint64_t start = first < 0 ? 0 : (uint64_t)first;
        uint64_t count = (uint64_t)frames - begin;
        if (count > video->media_frames - start) count = video->media_frames - start;
        video->packet_frame = video->loop_base + start;
        video->packet_begin = video->packet_offset = begin; video->packet_frames = begin + (unsigned)count;
    }
    return true;
}
static void video_notify_time(RdtVideo* video, double seconds) {
    int current_ms = (int)(seconds * 1000); // INT_CAST_OK: bounded native media timestamp in milliseconds.
    atomic_store(&video->current_time_ms, current_ms);
    int previous = atomic_load(&video->last_notified_frame_ms);
    if (current_ms >= 0 && (previous < 0 || current_ms < previous || current_ms - previous >= 33)) {
        atomic_store(&video->last_notified_frame_ms, current_ms); atomic_store(&video->has_new_frame, true);
        if (video->callbacks.on_frame_ready) video->callbacks.on_frame_ready(video, video->userdata);
    }
}
static void video_pcm_poll(RdtVideo* video) {
    if (atomic_load(&video->state) == RDT_VIDEO_STATE_ERROR) return;
    // retained routes fence pending and paused transports as well as active callback readers.
    if (video->audio_routing && !audio_submix_route_ref_valid(video->audio_binding.submix, video->audio_routing)) {
        video->audio_loss = AUDIO_VOICE_ROUTE_LOST; video_pcm_error(video, "shared audio route was lost"); return;
    }
    if (!video->pcm) return;
    if (video->audio_voice && audio_voice_loss(video->audio_voice) != AUDIO_VOICE_OK) {
        video->audio_loss = audio_voice_loss(video->audio_voice);
        video_pcm_error(video, "shared audio output was lost"); return;
    }
    if (!video_pcm_fill(video)) return;
    video_notify_time(video, rdt_video_get_current_time(video));
    if (audio_stream_snapshot(video->pcm).state == AUDIO_STREAM_ENDED) {
        audio_voice_remove(video->audio_voice); video->audio_voice = 0; video->play_requested = false;
        if (atomic_load(&video->state) != RDT_VIDEO_STATE_ENDED) set_state(video, RDT_VIDEO_STATE_ENDED);
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void set_state(RdtVideo* v, RdtVideoState new_state) {
    atomic_store(&v->state, (int)new_state);
    if (v->callbacks.on_state_changed) {
        v->callbacks.on_state_changed(v, new_state, v->userdata);
    }
}

static void populate_metadata(RdtVideo* v, AVPlayerItem* item, bool emit_callbacks) {
    if (!v || !item) return;
    if (v->metadata_sent && emit_callbacks) return;

    NSArray* tracks = [item.asset tracksWithMediaType:AVMediaTypeVideo];
    if (tracks.count > 0) {
        AVAssetTrack* vt = tracks[0];
        CGSize size = vt.naturalSize;
        v->intrinsic_width = (int)fabs(size.width);
        v->intrinsic_height = (int)fabs(size.height);
        log_info("video: intrinsic size %dx%d", v->intrinsic_width, v->intrinsic_height);
        if (emit_callbacks && v->callbacks.on_video_size_known) {
            v->callbacks.on_video_size_known(v, v->intrinsic_width, v->intrinsic_height, v->userdata);
        }
    }

    NSArray* audio_tracks = [item.asset tracksWithMediaType:AVMediaTypeAudio];
    v->has_audio_track = (audio_tracks.count > 0);
    log_info("video: has_audio=%d", v->has_audio_track);

    CMTime dur = item.duration;
    if (CMTIME_IS_VALID(dur) && !CMTIME_IS_INDEFINITE(dur)) {
        v->duration = CMTimeGetSeconds(dur);
        if (emit_callbacks && v->callbacks.on_duration_known) {
            v->callbacks.on_duration_known(v, v->duration, v->userdata);
        }
    }

    if (emit_callbacks) {
        v->metadata_sent = true;
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
            populate_metadata(v, item, true);
            set_state(v, RDT_VIDEO_STATE_READY);
        } else if (item.status == AVPlayerItemStatusFailed) {
            log_error("video: AVPlayerItem failed: %s",
                      item.error ? [[item.error localizedDescription] UTF8String] : "unknown");
            set_state(v, RDT_VIDEO_STATE_ERROR);
        }
    } else if ([keyPath isEqualToString:@"timeControlStatus"]) {
        if (v->has_audio_track) return;
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
        [obs release];
    }
    return obs;
}

// ---------------------------------------------------------------------------
// Public API implementation
// ---------------------------------------------------------------------------

RdtVideo* rdt_video_create(const RdtVideoCallbacks* cb, void* userdata) {
    RdtVideo* v = (RdtVideo*)mem_calloc(1, sizeof(RdtVideo), MEM_CAT_RENDER); // OBJ_HEAP_OK: video backend handle owns platform state until rdt_video_destroy.
    if (cb) v->callbacks = *cb;
    v->userdata = userdata;
    atomic_store(&v->state, (int)RDT_VIDEO_STATE_IDLE);
    atomic_store(&v->last_generated_frame_ms, -1);
    atomic_store(&v->last_notified_frame_ms, -1);
    v->volume = 1.0f;
    v->target_width = 300;
    v->target_height = 150;
    return v;
}

bool rdt_video_bind_audio(RdtVideo* video, const RdtVideoAudioBinding* binding) {
    if (!video || (binding && video->player_item)) return false;
    if (binding) {
        bool callbacks = binding->reserve || binding->release || binding->retire;
        if ((callbacks && (!binding->reserve || !binding->release || !binding->retire)) || !audio_submix_lock(binding->submix)) return false;
        audio_submix_unlock(binding->submix);
    }
    // retire callbacks while their native owner is still valid, including early source rejection.
    if (video->audio_binding.submix) rdt_video_pause(video);
    video_pcm_close(video);
    audio_submix_release_route(video->audio_binding.submix, video->audio_routing);
    video->audio_route = video->audio_routing = 0; video->audio_loss = AUDIO_VOICE_OK;
    if (video->audio_binding.retire) video->audio_binding.retire(video->audio_binding.owner);
    video->audio_binding = binding ? *binding : RdtVideoAudioBinding{};
    if (!binding) { video->callbacks = {}; video->userdata = nullptr; }
    return true;
}

void rdt_video_destroy(RdtVideo* video) {
    if (!video) return;
    rdt_video_bind_audio(video, nullptr);

    // remove observers
    if (video->player) {
        RdtVideoObserver* obs = get_or_create_observer(video);
        // queued platform blocks retain the observer, whose owner pointer retires before native storage.
        obs.video = nullptr;
        if (video->observing_time_control) {
            [video->player removeObserver:obs forKeyPath:@"timeControlStatus"];
            video->observing_time_control = false;
        }
        if (video->time_observer) {
            [video->player removeTimeObserver:video->time_observer];
        }
    }
    if (video->player_item && video->observing_status) {
        RdtVideoObserver* obs = get_or_create_observer(video);
        [video->player_item removeObserver:obs forKeyPath:@"status"];
        video->observing_status = false;
    }
    if (video->end_observer) {
        [[NSNotificationCenter defaultCenter] removeObserver:video->end_observer];
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
    [video->image_generator release];
    video->image_generator = nil;
    [video->player release];
    video->player = nil;
    [video->player_item release];
    video->player_item = nil;
    [video->asset release];
    video->asset = nil;

    // free frame buffer
    lam::Temp<uint8_t> frame_buffer(video->frame_buffer);
    video->frame_buffer = NULL;
    lam::Temp<RdtVideo> owned(video);  // destroy releases the backend handle
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
        video->asset = [[AVURLAsset alloc] initWithURL:url options:nil];
        if (![video->asset isPlayable]) {
            log_info("video: source is not playable: %s", file_path);
            [video->asset release];
            video->asset = nil;
            set_state(video, RDT_VIDEO_STATE_ERROR);
            return -1;
        }
        video->player_item = [[AVPlayerItem alloc] initWithAsset:video->asset];
        video->player = [[AVPlayer alloc] initWithPlayerItem:video->player_item];
        // all audible PCM belongs to the shared bus; AVPlayer remains a silent metadata/video owner.
        video->player.volume = 0.0f;

        // Local media exposes track dimensions before playback becomes ready;
        // publish them now so the first layout can use the intrinsic ratio.
        populate_metadata(video, video->player_item, false);

        // AVPlayerItemVideoOutput can make otherwise playable items fail with
        // AVFoundationErrorDomain -11821 in headless/test environments. Keep
        // AVPlayer for timing/audio and use AVAssetImageGenerator for frames.
        video->image_generator = [[AVAssetImageGenerator alloc] initWithAsset:video->asset];
        video->image_generator.appliesPreferredTrackTransform = YES;
        video->image_generator.requestedTimeToleranceBefore = CMTimeMake(1, 60);
        video->image_generator.requestedTimeToleranceAfter = CMTimeMake(1, 60);

        // observe status and time control
        RdtVideoObserver* obs = get_or_create_observer(video);
        [video->player_item addObserver:obs forKeyPath:@"status" options:0 context:NULL];
        video->observing_status = true;
        [video->player addObserver:obs forKeyPath:@"timeControlStatus" options:0 context:NULL];
        video->observing_time_control = true;

        // periodic media-time observer. It only wakes Radiant when AVFoundation
        // reports a fresh pixel buffer, so low-FPS and paused videos do not
        // force display-rate redraws.
        RdtVideoObserver* frameObserver = obs;
        // NOTE: No ARC — retain the returned observer token
        video->time_observer = [[video->player addPeriodicTimeObserverForInterval:CMTimeMake(1, 60)
                                                                          queue:dispatch_get_main_queue()
                                                                     usingBlock:^(CMTime time) {
            RdtVideo* weakVideo = frameObserver.video;
            if (weakVideo && !weakVideo->has_audio_track) {
                video_notify_time(weakVideo, CMTimeGetSeconds(time));
            }
        }] retain];

        // end-of-playback notification
        // NOTE: No ARC — retain the returned observer token
        video->end_observer = [[[NSNotificationCenter defaultCenter]
            addObserverForName:AVPlayerItemDidPlayToEndTimeNotification
                   object:video->player_item
                        queue:[NSOperationQueue mainQueue]
                   usingBlock:^(NSNotification* note) {
            (void)note;
            RdtVideo* weakVideo = frameObserver.video;
            if (weakVideo && !weakVideo->has_audio_track) {
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
    if (!video || !video->player || atomic_load(&video->state) == RDT_VIDEO_STATE_ERROR) return;
    video->play_requested = true;
    if (atomic_load(&video->state) == RDT_VIDEO_STATE_LOADING) return;
    if (!video->has_audio_track) { [video->player play]; return; }
    video_pcm_poll(video);
    if (atomic_load(&video->state) == RDT_VIDEO_STATE_ERROR) return;
    video->play_requested = true;
    if (!video_pcm_prepare(video)) return;
    if (audio_stream_snapshot(video->pcm).state == AUDIO_STREAM_ENDED && !video_pcm_seek(video, 0)) return;
    if (!video_pcm_fill(video) || !audio_stream_play(video->pcm)) return;
    if (!video->audio_voice) video->audio_voice = audio_voice_add(video->audio_binding.submix, audio_stream_mix, video->pcm, nullptr, video->audio_route);
    if (!video->audio_voice) { video_pcm_error(video, "shared voice admission failed"); return; }
    set_state(video, RDT_VIDEO_STATE_PLAYING);
}

void rdt_video_pause(RdtVideo* video) {
    if (!video || !video->player) return;
    video->play_requested = false;
    if (!video->pcm) {
        [video->player pause];
        if (video->has_audio_track && atomic_load(&video->state) == RDT_VIDEO_STATE_PLAYING) set_state(video, RDT_VIDEO_STATE_PAUSED);
        return;
    }
    audio_voice_remove(video->audio_voice); video->audio_voice = 0; audio_stream_pause(video->pcm);
    if (atomic_load(&video->state) == RDT_VIDEO_STATE_PLAYING) set_state(video, RDT_VIDEO_STATE_PAUSED);
}

void rdt_video_seek(RdtVideo* video, double seconds) {
    if (!video || !video->player || !isfinite(seconds) || seconds < 0) return;
    video->requested_time = seconds;
    if (!video->pcm) {
        if (!video->has_audio_track)
            [video->player seekToTime:CMTimeMakeWithSeconds(seconds, NSEC_PER_SEC) toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
        return;
    }
    bool playing = atomic_load(&video->state) == RDT_VIDEO_STATE_PLAYING;
    if (seconds > video->duration) seconds = video->duration;
    if (!video_pcm_seek(video, (uint64_t)llround(seconds * AUDIO_BUS_RATE))) return;
    atomic_store(&video->last_generated_frame_ms, -1);
    if (playing) rdt_video_play(video);
    if (video->callbacks.on_frame_ready) video->callbacks.on_frame_ready(video, video->userdata);
}

void rdt_video_set_loop(RdtVideo* video, bool loop) {
    if (!video || video->loop == loop) return;
    double position = rdt_video_get_current_time(video); video->loop = loop;
    // discard queued next-loop samples when loop policy changes, preserving the selected media time.
    if (video->pcm) rdt_video_seek(video, position);
}

void rdt_video_set_volume(RdtVideo* video, float volume) {
    if (!video || !isfinite(volume) || volume < 0 || volume > 1) return;
    video->volume = volume; if (video->pcm) audio_stream_gain(video->pcm, video->muted ? 0 : volume);
}

void rdt_video_set_muted(RdtVideo* video, bool muted) {
    if (!video) return;
    video->muted = muted; if (video->pcm) audio_stream_gain(video->pcm, muted ? 0 : video->volume);
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
            populate_metadata(video, video->player_item, true);
            set_state(video, RDT_VIDEO_STATE_READY);
            cur = RDT_VIDEO_STATE_READY;
        } else if (video->player_item.status == AVPlayerItemStatusFailed) {
            log_error("video: AVPlayerItem failed (polled): %s",
                      video->player_item.error ? [[video->player_item.error localizedDescription] UTF8String] : "unknown");
            set_state(video, RDT_VIDEO_STATE_ERROR);
            cur = RDT_VIDEO_STATE_ERROR;
        }
    }

    if (cur == RDT_VIDEO_STATE_READY && video->play_requested) { rdt_video_play(video); cur = (RdtVideoState)atomic_load(&video->state); }
    video_pcm_poll(video); cur = (RdtVideoState)atomic_load(&video->state);
    if (video->pcm || cur == RDT_VIDEO_STATE_ERROR) return cur;

    // also poll timeControlStatus for playing/paused transitions
    if (!video->has_audio_track && video->player && (cur == RDT_VIDEO_STATE_READY || cur == RDT_VIDEO_STATE_PLAYING || cur == RDT_VIDEO_STATE_PAUSED)) {
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

bool rdt_video_audio_routing(RdtVideo* video, AudioProviderInfo* info) {
    if (!video || !info || !video->audio_binding.submix || !video->has_audio_track) return false;
    auto state = rdt_video_get_state(video);
    *info = {video->audio_route, video->audio_loss, rdt_video_state_name(state), rdt_video_get_current_time(video),
        video->muted ? 0 : video->volume,
        !video->audio_route || audio_submix_route_valid(video->audio_binding.submix, video->audio_route)};
    return true;
}

bool rdt_video_audio_route(RdtVideo* video, uint64_t expected, uint64_t route, const char** error) {
    const char* ignored = nullptr; if (!error) error = &ignored; *error = nullptr;
    auto fail = [&](const char* message) { *error = message; return false; };
    AudioProviderInfo info;
    if (!rdt_video_audio_routing(video, &info) || info.route != expected) return fail("video route: missing provider or stale expected route");
    if (route && atomic_load(&video->state) == RDT_VIDEO_STATE_ERROR) return fail("video route: failed source must be reloaded");
    if (route == video->audio_route && !video->audio_routing) return true;
    if (!audio_submix_bind_route(video->audio_binding.submix, &video->audio_routing, video->audio_voice, route))
        return fail("video route: stale/foreign route or reference quota exceeded");
    video->audio_route = route; return true;
}

bool rdt_video_events_pending(RdtVideo* video) {
    if (!video || !video->player) return false;
    // Virtual assertion retries must allow native playback to reach its completion event too.
    RdtVideoState state = rdt_video_get_state(video);
    if (video->pcm) return state == RDT_VIDEO_STATE_PLAYING && !audio_bus_offline();
    return state == RDT_VIDEO_STATE_LOADING || state == RDT_VIDEO_STATE_PLAYING ||
        video->player.timeControlStatus == AVPlayerTimeControlStatusWaitingToPlayAtSpecifiedRate;
}

double rdt_video_get_current_time(RdtVideo* video) {
    if (!video) return 0.0;
    if (video->pcm) {
        uint64_t frame = audio_stream_snapshot(video->pcm).frame;
        if (video->loop && video->media_frames) frame %= video->media_frames;
        if (frame > video->media_frames) frame = video->media_frames;
        return frame / (double)AUDIO_BUS_RATE;
    }
    if (video->has_audio_track) return video->duration > 0 && video->requested_time > video->duration ?
        video->duration : video->requested_time;
    if (video->player_item) {
        CMTime current = [video->player_item currentTime];
        if (CMTIME_IS_VALID(current) && !CMTIME_IS_INDEFINITE(current)) {
            double seconds = CMTimeGetSeconds(current);
            if (isfinite(seconds) && seconds >= 0.0) return seconds;
        }
    }
    // the main-queue time observer is asynchronous; keep its sample as a fallback
    // only while AVFoundation has no usable media time.
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
    if (!video || !frame || !video->player_item) return -1;

    @autoreleasepool {
        if (!video->image_generator) return -1;

        CMTime current = CMTimeMakeWithSeconds(rdt_video_get_current_time(video), AUDIO_BUS_RATE);
        int current_ms = (int)(CMTimeGetSeconds(current) * 1000.0);
        if (current_ms == atomic_load(&video->last_generated_frame_ms) &&
            video->frame_buffer && video->frame_width > 0 && video->frame_height > 0) {
            frame->pixels = video->frame_buffer;
            frame->width = video->frame_width;
            frame->height = video->frame_height;
            frame->stride = video->frame_stride;
            frame->pts = CMTimeGetSeconds(current);
            return 0;
        }

        NSError* error = nil;
        CMTime actual = kCMTimeZero;
        CGImageRef image = [video->image_generator copyCGImageAtTime:current
                                                           actualTime:&actual
                                                                error:&error];
        if (!image) return -1;

        int out_w = (int)CGImageGetWidth(image);
        int out_h = (int)CGImageGetHeight(image);
        int out_stride = out_w * 4;
        int needed = out_stride * out_h;
        if (!video->frame_buffer || video->frame_width != out_w || video->frame_height != out_h) {
            lam::Temp<uint8_t> previous(video->frame_buffer);  // the resized buffer replaces it
            video->frame_buffer = (uint8_t*)mem_alloc(needed, MEM_CAT_RENDER);
            video->frame_width = out_w;
            video->frame_height = out_h;
            video->frame_stride = out_stride;
        }

        CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate(video->frame_buffer, out_w, out_h, 8,
                                                     out_stride, color_space,
                                                     kCGImageAlphaPremultipliedLast |
                                                     kCGBitmapByteOrder32Big);
        CGColorSpaceRelease(color_space);
        if (!context) {
            CGImageRelease(image);
            return -1;
        }

        CGContextDrawImage(context, CGRectMake(0, 0, out_w, out_h), image);
        CGContextRelease(context);
        CGImageRelease(image);

        frame->pixels = video->frame_buffer;
        frame->width = out_w;
        frame->height = out_h;
        frame->stride = out_stride;
        frame->pts = CMTimeGetSeconds(actual);

        atomic_store(&video->last_generated_frame_ms, current_ms);
        atomic_store(&video->has_new_frame, false);
        return 0;
    }
}

#endif // __APPLE__
