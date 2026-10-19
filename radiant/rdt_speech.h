#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Speech engines render an utterance to mono float PCM plus boundary records
// (vibe/radiant/Radiant_Design_Speech.md §3.3). Engines never touch the DOM or
// the Lambda heap: requests and results are plain copied data.
enum { RDT_SPEECH_QUALITY_DEFAULT = 1, RDT_SPEECH_QUALITY_ENHANCED = 2, RDT_SPEECH_QUALITY_PREMIUM = 3 };
enum { RDT_SPEECH_GENDER_UNSPECIFIED = 0, RDT_SPEECH_GENDER_MALE = 1, RDT_SPEECH_GENDER_FEMALE = 2 };
typedef struct RdtSpeechVoice {
    char id[160];
    char name[80];
    char lang[32];      // BCP 47
    bool local;
    uint8_t quality;    // RDT_SPEECH_QUALITY_*: downloaded enhanced and premium voices sound most natural
    uint8_t gender;     // RDT_SPEECH_GENDER_*
} RdtSpeechVoice;

enum { RDT_SPEECH_WORD = 1, RDT_SPEECH_SENTENCE = 2, RDT_SPEECH_BOOKMARK = 4 };
typedef struct RdtSpeechMark {
    uint32_t kind;          // RDT_SPEECH_WORD, _SENTENCE or _BOOKMARK
    uint32_t frame;         // sample offset from the utterance start
    uint32_t text_start;    // UTF-16 offset into the request text (words only)
    uint32_t text_length;   // UTF-16 units
    char name[64];          // bookmark name
} RdtSpeechMark;

typedef struct RdtSpeechRequest {
    const char* text;       // UTF-8 plain text, or SSML when `ssml` is set
    size_t length;
    bool ssml;
    const char* voice_id;   // null: the best installed voice for `lang`
    const char* lang;       // null: the system default
    float rate;             // Web Speech scale, 0.1..10, 1 = normal
    float pitch;            // 0..2, 1 = normal
    float volume;           // 0..1
} RdtSpeechRequest;

typedef enum { RDT_SPEECH_RENDERING, RDT_SPEECH_RENDERED, RDT_SPEECH_FAILED } RdtSpeechJobState;
typedef struct RdtSpeechJob RdtSpeechJob;

#ifdef __cplusplus
extern "C" {
#endif
// platform engine (AVFoundation on macOS; null elsewhere). Rendering is
// asynchronous: the macOS engine delivers buffers on the main run loop, which
// Radiant's UI thread pumps, so callers poll the job from that thread.
// the installed voices, most natural first: by quality, then the current voice
// family ahead of older formant and novelty voices.
size_t rdt_speech_voices(RdtSpeechVoice* out, size_t capacity);
// increments whenever the installed voice set changes.
uint64_t rdt_speech_voices_generation(void);
RdtSpeechJob* rdt_speech_render_begin(const RdtSpeechRequest* request);
RdtSpeechJobState rdt_speech_job_state(RdtSpeechJob* job);
// valid once RENDERED; the job keeps ownership until destroyed.
const float* rdt_speech_job_samples(RdtSpeechJob* job, uint32_t* frames, uint32_t* sample_rate);
const RdtSpeechMark* rdt_speech_job_marks(RdtSpeechJob* job, uint32_t* count);
const char* rdt_speech_job_error(RdtSpeechJob* job);
// cancels an unfinished render and releases everything.
void rdt_speech_job_destroy(RdtSpeechJob* job);
#ifdef __cplusplus
}
#endif
