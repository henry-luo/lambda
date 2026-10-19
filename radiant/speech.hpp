#pragma once
#include <stddef.h>
#include <stdint.h>
#include "rdt_speech.h"
#include "../lib/strbuf.h"
#include "../lambda/lambda.h"

// Speech synthesis service (vibe/radiant/Radiant_Design_Speech.md, M1).
struct DomDocument;
struct DomElement;

// ---------------------------------------------------------------- utterance text

// An utterance as sent to an engine: plain UTF-8 text or SSML translated from
// <speak> markup (§4.2). `char_of` maps each UTF-16 unit of `text` (the units
// engines report ranges in) to a code-point index of the utterance's plain text,
// or SPEECH_NO_CHAR for markup.
#define SPEECH_NO_CHAR UINT32_MAX
struct SpeechText {
    StrBuf* text;
    bool ssml;
    uint32_t* char_of;
    uint32_t units, capacity;
    uint32_t chars;
    char error[200];
};
bool speech_text_from_string(const char* utf8, size_t length, SpeechText* out);
// validates the portable markup core; on failure `error` names the problem.
bool speech_text_from_markup(Item element, SpeechText* out);
void speech_text_release(SpeechText* text);
// maps an engine range (UTF-16 units of `text`) onto plain-text code points.
bool speech_text_char_range(const SpeechText* text, uint32_t start, uint32_t length,
    uint32_t* char_index, uint32_t* char_length);

// ---------------------------------------------------------------- service

struct SpeechOptions {
    const char* voice_id;   // null: by `lang`, else the system default
    const char* lang;
    float rate, pitch, volume;
    double at;              // seconds on the document sub-mix clock; negative: as soon as possible
};
enum SpeechState { SPEECH_QUEUED, SPEECH_RENDERING, SPEECH_SPEAKING, SPEECH_PAUSED,
    SPEECH_ENDED, SPEECH_CANCELLED, SPEECH_ERROR };
struct SpeechInfo { SpeechState state; double position; int64_t char_index; };

// takes ownership of `text`. Returns 0 and fills `error` when the document may
// not start audio (D7.5.4) or the request is invalid.
uint64_t radiant_speech_speak(DomElement* owner, SpeechText* text, const SpeechOptions* options,
    const char** error);
bool radiant_speech_command(DomDocument* doc, uint64_t token, const char* command);
bool radiant_speech_info(DomDocument* doc, uint64_t token, SpeechInfo* info);
const char* radiant_speech_state_name(SpeechState state);
// voices of the active engine (the fake engine under LAMBDA_SPEECH_ENGINE=fake).
size_t radiant_speech_voices(RdtSpeechVoice* out, size_t capacity);
// adopts finished renders, arms the next utterance on the document's speech
// voice and queues events for the playback the sub-mix has made; called on the
// UI thread from the frame tick, after radiant_audio_tick. Returns whether any
// utterance is still live.
bool radiant_speech_tick(DomDocument* doc);
// a document that read the voice list hears voiceschanged.
void radiant_speech_watch_voices(DomDocument* doc);
// whether any utterance is live, so the host keeps ticking frames.
bool radiant_speech_active(DomDocument* doc);
// whether a live utterance runs on a real engine, whose render and device
// clock need wall time while EventSim retries an assertion.
bool radiant_speech_wall_clock_pending(DomDocument* doc);
// a refused dom.speak: speecherror on `owner` with a null token (S7.4.2).
void radiant_speech_report_error(DomElement* owner, const char* reason);
// utterance owners follow their view's rebuild, like MIDI sessions.
struct DomNode;
void radiant_speech_follow_rebuild(DomDocument* doc, DomNode* old_root, DomNode* new_root);
