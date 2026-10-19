// rdt_speech_avf.mm — macOS AVSpeechSynthesizer backend for rdt_speech.h
//
// AVSpeechSynthesizer.write delivers PCM buffers and word/bookmark markers on
// the main queue, whatever thread starts it; Radiant's UI loop pumps that queue
// (rdt_video_pump_events), so jobs are started and polled on the UI thread and
// never block it. Output is mono Float32 (22.05 kHz for the system voices).

#ifdef __APPLE__

#import <AVFoundation/AVFoundation.h>

#include "rdt_speech.h"
#include "../lib/log.h"
#include "../lib/mem.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>

struct RdtSpeechJob {
    AVSpeechSynthesizer* synth;
    id sink;
    atomic_int state;
    float* samples;
    uint32_t frames, capacity, sample_rate, bytes_per_frame;
    RdtSpeechMark* marks;
    uint32_t mark_count, mark_capacity;
    char error[160];
};

static void speech_fail(RdtSpeechJob* job, const char* message) {
    snprintf(job->error, sizeof(job->error), "%s", message);
    atomic_store(&job->state, RDT_SPEECH_FAILED);
}
static bool speech_append_samples(RdtSpeechJob* job, AVAudioPCMBuffer* pcm) {
    uint32_t count = pcm.frameLength;
    if (job->frames + count > job->capacity) {
        uint32_t capacity = job->capacity ? job->capacity : 65536;
        while (capacity < job->frames + count) capacity *= 2;
        float* grown = (float*)mem_realloc(job->samples, (size_t)capacity * sizeof(float), MEM_CAT_RENDER);
        if (!grown) return false;
        job->samples = grown; job->capacity = capacity;
    }
    // keep the first channel; convert 16-bit voices to float.
    if (pcm.floatChannelData) memcpy(job->samples + job->frames, pcm.floatChannelData[0], count * sizeof(float));
    else if (pcm.int16ChannelData) for (uint32_t i = 0; i < count; i++) job->samples[job->frames + i] = pcm.int16ChannelData[0][i] / 32768.0f;
    else return false;
    job->frames += count;
    return true;
}

@interface RdtSpeechSink : NSObject <AVSpeechSynthesizerDelegate> {
@public RdtSpeechJob* job;
}
@end
@implementation RdtSpeechSink
- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer willSpeakMarker:(AVSpeechSynthesisMarker*)marker
        utterance:(AVSpeechUtterance*)utterance {
    (void)synthesizer; (void)utterance;
    if (!job || atomic_load(&job->state) != RDT_SPEECH_RENDERING) return;
    uint32_t kind = marker.mark == AVSpeechSynthesisMarkerMarkWord ? RDT_SPEECH_WORD
        : marker.mark == AVSpeechSynthesisMarkerMarkSentence ? RDT_SPEECH_SENTENCE
        : marker.mark == AVSpeechSynthesisMarkerMarkBookmark ? RDT_SPEECH_BOOKMARK : 0;
    if (!kind) return;
    if (job->mark_count == job->mark_capacity) {
        uint32_t capacity = job->mark_capacity ? job->mark_capacity * 2 : 32;
        auto* grown = (RdtSpeechMark*)mem_realloc(job->marks, capacity * sizeof(RdtSpeechMark), MEM_CAT_RENDER);
        if (!grown) { speech_fail(job, "speech marker allocation failed"); return; }
        job->marks = grown; job->mark_capacity = capacity;
    }
    RdtSpeechMark* mark = &job->marks[job->mark_count++];
    memset(mark, 0, sizeof(*mark));
    mark->kind = kind;
    // markers report byte offsets into the rendered stream, not frames.
    mark->frame = (uint32_t)(marker.byteSampleOffset / (job->bytes_per_frame ? job->bytes_per_frame : 4));
    mark->text_start = (uint32_t)marker.textRange.location;
    mark->text_length = (uint32_t)marker.textRange.length;
    if (kind == RDT_SPEECH_BOOKMARK && marker.bookmarkName)
        snprintf(mark->name, sizeof(mark->name), "%s", marker.bookmarkName.UTF8String);
}
@end

// Web Speech rate (0.1..10, 1 = normal) onto AVSpeechUtterance's 0..1 scale (0.5 = normal).
static float speech_av_rate(float rate) {
    float lo = AVSpeechUtteranceMinimumSpeechRate, mid = AVSpeechUtteranceDefaultSpeechRate, hi = AVSpeechUtteranceMaximumSpeechRate;
    if (!isfinite(rate) || rate <= 0) return mid;
    if (rate <= 1) return lo + (mid - lo) * rate;
    return fminf(hi, mid + (hi - mid) * log10f(rate));
}

static atomic_ullong speech_voice_generation = 1;
static void speech_watch_voices(void) {
    static dispatch_once_t once;
    // the deployment target (macOS 15.7) has every API used here, so no availability branches.
    dispatch_once(&once, ^{
        [[NSNotificationCenter defaultCenter] addObserverForName:AVSpeechSynthesisAvailableVoicesDidChangeNotification
            object:nil queue:nil usingBlock:^(NSNotification*) { atomic_fetch_add(&speech_voice_generation, 1); }];
    });
}

extern "C" uint64_t rdt_speech_voices_generation(void) {
    speech_watch_voices();
    return atomic_load(&speech_voice_generation);
}

static NSString* speech_string(const char* text, size_t length) {
    return [[[NSString alloc] initWithBytes:text length:length encoding:NSUTF8StringEncoding] autorelease];
}
// Apple's current voices (compact, enhanced, premium) sound best at equal quality;
// Eloquence formant voices come next and the classic MacinTalk set, mostly novelty
// voices, last.
static int speech_voice_family(AVSpeechSynthesisVoice* voice) {
    NSString* identifier = voice.identifier;
    if ([identifier hasPrefix:@"com.apple.speech.synthesis.voice."]) return 0;
    if ([identifier hasPrefix:@"com.apple.eloquence."]) return 1;
    return 2;
}
static NSArray<AVSpeechSynthesisVoice*>* speech_ranked_voices(void) {
    return [[AVSpeechSynthesisVoice speechVoices] sortedArrayWithOptions:NSSortStable
        usingComparator:^NSComparisonResult(AVSpeechSynthesisVoice* a, AVSpeechSynthesisVoice* b) {
            NSInteger ra = a.quality * 4 + speech_voice_family(a), rb = b.quality * 4 + speech_voice_family(b);
            return ra > rb ? NSOrderedAscending : ra < rb ? NSOrderedDescending : NSOrderedSame;
        }];
}
// BCP 47 match ignoring case and `_`; a bare language ("en") matches any of its regions.
static bool speech_lang_matches(NSString* voice_lang, const char* lang) {
    NSString* want = [[NSString stringWithUTF8String:lang] stringByReplacingOccurrencesOfString:@"_" withString:@"-"];
    NSString* have = [voice_lang stringByReplacingOccurrencesOfString:@"_" withString:@"-"];
    if ([have caseInsensitiveCompare:want] == NSOrderedSame) return true;
    return [want rangeOfString:@"-"].location == NSNotFound &&
        [[have lowercaseString] hasPrefix:[[want lowercaseString] stringByAppendingString:@"-"]];
}
// the most natural installed voice for `lang`, rather than the language default,
// which is usually the compact voice even when a better one is installed.
static AVSpeechSynthesisVoice* speech_best_voice(const char* lang) {
    for (AVSpeechSynthesisVoice* voice in speech_ranked_voices())
        if (speech_lang_matches(voice.language, lang)) return voice;
    return [AVSpeechSynthesisVoice voiceWithLanguage:speech_string(lang, strlen(lang))];
}

extern "C" size_t rdt_speech_voices(RdtSpeechVoice* out, size_t capacity) {
    speech_watch_voices();
    @autoreleasepool {
        size_t count = 0;
        for (AVSpeechSynthesisVoice* voice in speech_ranked_voices()) {
            if (out && count < capacity) {
                RdtSpeechVoice* v = &out[count];
                memset(v, 0, sizeof(*v));
                snprintf(v->id, sizeof(v->id), "%s", voice.identifier.UTF8String);
                snprintf(v->name, sizeof(v->name), "%s", voice.name.UTF8String);
                snprintf(v->lang, sizeof(v->lang), "%s", voice.language.UTF8String);
                v->local = true;  // system voices synthesize on the device
                v->quality = (uint8_t)voice.quality;
                v->gender = voice.gender == AVSpeechSynthesisVoiceGenderMale ? RDT_SPEECH_GENDER_MALE
                    : voice.gender == AVSpeechSynthesisVoiceGenderFemale ? RDT_SPEECH_GENDER_FEMALE : RDT_SPEECH_GENDER_UNSPECIFIED;
            }
            count++;
        }
        return count;
    }
}

extern "C" RdtSpeechJob* rdt_speech_render_begin(const RdtSpeechRequest* request) {
    if (!request || !request->text) return nullptr;
    auto* job = (RdtSpeechJob*)mem_calloc(1, sizeof(RdtSpeechJob), MEM_CAT_RENDER);
    if (!job) return nullptr;
    atomic_store(&job->state, RDT_SPEECH_RENDERING);
    @autoreleasepool {
        NSString* text = speech_string(request->text, request->length);
        AVSpeechUtterance* utterance = !text ? nil : request->ssml
            ? [AVSpeechUtterance speechUtteranceWithSSMLRepresentation:text]
            : [AVSpeechUtterance speechUtteranceWithString:text];
        if (!utterance) { speech_fail(job, request->ssml ? "the speech markup was rejected" : "the text is not valid UTF-8"); return job; }
        AVSpeechSynthesisVoice* voice = nil;
        if (request->voice_id) voice = [AVSpeechSynthesisVoice voiceWithIdentifier:speech_string(request->voice_id, strlen(request->voice_id))];
        else if (request->lang) voice = speech_best_voice(request->lang);
        if ((request->voice_id || request->lang) && !voice) { speech_fail(job, "no voice for the requested voice or language"); return job; }
        if (voice) utterance.voice = voice;
        utterance.rate = speech_av_rate(request->rate);
        utterance.pitchMultiplier = fmaxf(0.5f, fminf(2.0f, isfinite(request->pitch) ? request->pitch : 1.0f));
        utterance.volume = fmaxf(0.0f, fminf(1.0f, isfinite(request->volume) ? request->volume : 1.0f));

        RdtSpeechSink* sink = [[RdtSpeechSink alloc] init];
        sink->job = job;
        job->sink = sink;
        job->synth = [[AVSpeechSynthesizer alloc] init];
        job->synth.delegate = sink;
        // the block retains the sink; destroy() detaches it so late callbacks find no job.
        [job->synth writeUtterance:utterance toBufferCallback:^(AVAudioBuffer* buffer) {
            RdtSpeechJob* current = sink->job;
            if (!current || atomic_load(&current->state) != RDT_SPEECH_RENDERING) return;
            AVAudioPCMBuffer* pcm = (AVAudioPCMBuffer*)buffer;
            if (![pcm isKindOfClass:[AVAudioPCMBuffer class]]) { speech_fail(current, "unexpected speech buffer type"); return; }
            if (!current->sample_rate) {
                current->sample_rate = (uint32_t)pcm.format.sampleRate;
                current->bytes_per_frame = pcm.format.streamDescription->mBytesPerFrame;
            }
            // a zero-length buffer ends the utterance.
            if (pcm.frameLength == 0) { atomic_store(&current->state, RDT_SPEECH_RENDERED); return; }
            if (!speech_append_samples(current, pcm)) speech_fail(current, "speech buffer allocation failed");
        }];
    }
    return job;
}

extern "C" RdtSpeechJobState rdt_speech_job_state(RdtSpeechJob* job) {
    return job ? (RdtSpeechJobState)atomic_load(&job->state) : RDT_SPEECH_FAILED;
}
extern "C" const float* rdt_speech_job_samples(RdtSpeechJob* job, uint32_t* frames, uint32_t* sample_rate) {
    if (frames) *frames = job ? job->frames : 0;
    if (sample_rate) *sample_rate = job ? job->sample_rate : 0;
    return job ? job->samples : nullptr;
}
extern "C" const RdtSpeechMark* rdt_speech_job_marks(RdtSpeechJob* job, uint32_t* count) {
    if (count) *count = job ? job->mark_count : 0;
    return job ? job->marks : nullptr;
}
extern "C" const char* rdt_speech_job_error(RdtSpeechJob* job) {
    return job && job->error[0] ? job->error : nullptr;
}
extern "C" void rdt_speech_job_destroy(RdtSpeechJob* job) {
    if (!job) return;
    if (job->sink) ((RdtSpeechSink*)job->sink)->job = nullptr;
    if (job->synth) {
        job->synth.delegate = nil;
        [job->synth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
        [job->synth release];
    }
    if (job->sink) [(RdtSpeechSink*)job->sink release];
    mem_free(job->samples);
    mem_free(job->marks);
    mem_free(job);
}

#endif // __APPLE__
