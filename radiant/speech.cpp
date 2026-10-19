// speech.cpp — the per-document speech service (Radiant_Design_Speech.md §3, M1–M2).
//
// dom.speak queues an utterance; its engine render starts at once (render
// ahead), and utterances then sound in order, each no earlier than its `at`
// time on the document sub-mix clock (§3.4). Samples play through one voice of
// the document's sub-mix on the window bus (audio_bus.hpp), so speech shares a
// clock with the document's MIDI sessions. With offline output the sub-mix
// follows the frame clock, which is virtual under EventSim, so replays with the
// fake engine (LAMBDA_SPEECH_ENGINE=fake) are deterministic. Boundaries become
// DOM events as playback passes them; they are queued as timing events, so
// handlers run at the next task edge, never inside the frame tick.

#include "speech.hpp"
#include "audio.hpp"
#include "event.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/dom/dom_events.h"
#include "../lambda/dom/realm/dom_realm.h"
#include "../lambda/js/js_runtime.h"
#include "../lambda/runtime/lambda-root-frame.hpp"
#include "../lib/mem.h"
#include "../lib/log.h"
#include "../lib/arraylist.hpp"
#include "../lib/utf.h"

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#include "../lib/miniaudio/miniaudio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t SPEECH_QUEUE_LIMIT = 64;            // utterances per document
static const uint32_t SPEECH_FAKE_RATE = 22050;

// ---------------------------------------------------------------- fake engine

static bool speech_fake_engine() {
    const char* engine = getenv("LAMBDA_SPEECH_ENGINE");
    return engine && !strcmp(engine, "fake");
}
// listed best first, as engines list theirs.
static const RdtSpeechVoice SPEECH_FAKE_VOICES[] = {
    {"fake.zh-CN", "Fake Chinese", "zh-CN", true, RDT_SPEECH_QUALITY_ENHANCED, RDT_SPEECH_GENDER_MALE},
    {"fake.en-US", "Fake English", "en-US", true, RDT_SPEECH_QUALITY_DEFAULT, RDT_SPEECH_GENDER_FEMALE},
};

struct SpeechRender {
    float* samples;
    uint32_t frames, rate;
    RdtSpeechMark* marks;
    uint32_t mark_count;
};
static bool render_reserve(SpeechRender* r, uint32_t frames) {
    auto* grown = (float*)mem_realloc(r->samples, ((size_t)r->frames + frames) * sizeof(float), MEM_CAT_RENDER);
    if (!grown) return false;
    r->samples = grown;
    return true;
}
static bool render_tone(SpeechRender* r, double seconds, double hz) {
    uint32_t n = (uint32_t)lround(seconds * r->rate);
    if (!render_reserve(r, n)) return false;
    for (uint32_t i = 0; i < n; i++) r->samples[r->frames + i] = hz > 0 ? 0.2f * (float)sin(2 * M_PI * hz * i / r->rate) : 0.0f;
    r->frames += n;
    return true;
}
// resamples a render to mono AUDIO_BUS_RATE and rescales its marks, so the
// speech voice copies samples as they are.
static bool render_to_bus_rate(SpeechRender* r) {
    if (r->rate == AUDIO_BUS_RATE) return true;
    if (!r->frames || !r->rate) return false;
    ma_uint64 frames = ma_convert_frames(nullptr, 0, ma_format_f32, 1, AUDIO_BUS_RATE, r->samples, r->frames, ma_format_f32, 1, r->rate);
    if (!frames || frames > UINT32_MAX) return false;
    auto* out = (float*)mem_alloc((size_t)frames * sizeof(float), MEM_CAT_RENDER);
    if (!out) return false;
    frames = ma_convert_frames(out, frames, ma_format_f32, 1, AUDIO_BUS_RATE, r->samples, r->frames, ma_format_f32, 1, r->rate);
    if (!frames) { mem_free(out); return false; }
    double scale = AUDIO_BUS_RATE / (double)r->rate;
    for (uint32_t i = 0; i < r->mark_count; i++) r->marks[i].frame = (uint32_t)llround(r->marks[i].frame * scale);
    mem_free(r->samples);
    r->samples = out; r->frames = (uint32_t)frames; r->rate = AUDIO_BUS_RATE;
    return true;
}
static bool render_mark(SpeechRender* r, uint32_t kind, uint32_t start, uint32_t length, const char* name, size_t name_length) {
    auto* grown = (RdtSpeechMark*)mem_realloc(r->marks, (r->mark_count + 1) * sizeof(RdtSpeechMark), MEM_CAT_RENDER);
    if (!grown) return false;
    r->marks = grown;
    RdtSpeechMark* m = &r->marks[r->mark_count++];
    memset(m, 0, sizeof(*m));
    m->kind = kind; m->frame = r->frames; m->text_start = start; m->text_length = length;
    if (name) snprintf(m->name, sizeof(m->name), "%.*s", (int)name_length, name);
    return true;
}
// one step through the engine text: a markup tag, an entity, or a character.
struct FakeStep {
    size_t bytes;
    uint32_t units, cp;
    bool markup, entity;
};
static bool fake_step(const SpeechText* t, size_t byte, uint32_t unit, FakeStep* step) {
    const char* s = t->text->str;
    size_t n = t->text->length;
    *step = {};
    // tags carry no plain characters; text characters always map to one.
    if (t->ssml && s[byte] == '<' && (unit >= t->units || t->char_of[unit] == SPEECH_NO_CHAR)) {
        const char* end = (const char*)memchr(s + byte, '>', n - byte);
        if (!end) return false;
        step->markup = true;
        step->bytes = (size_t)(end - (s + byte)) + 1;
        step->units = (uint32_t)step->bytes;   // markup is ASCII: one UTF-16 unit per byte
        return true;
    }
    int used = utf8_decode(s + byte, n - byte, &step->cp);
    if (used <= 0) return false;
    step->entity = t->ssml && step->cp == '&';
    if (step->entity) {
        const char* semi = (const char*)memchr(s + byte, ';', n - byte);
        if (!semi) return false;
        step->bytes = (size_t)(semi - (s + byte)) + 1;
        step->units = (uint32_t)step->bytes;
    } else {
        step->bytes = (size_t)used;
        step->units = step->cp >= 0x10000 ? 2 : 1;
    }
    return true;
}
// a word is a run of plain characters up to whitespace or punctuation; a CJK
// ideograph is a word by itself. Each word is a 0.25 s tone, then 0.05 s of rest.
// Like AVSpeechSynthesizer.write, it reports no sentence boundaries.
static bool speech_fake_render(const SpeechText* t, SpeechRender* r) {
    memset(r, 0, sizeof(*r));
    r->rate = SPEECH_FAKE_RATE;
    const char* s = t->text->str;
    size_t n = t->text->length, byte = 0;
    uint32_t unit = 0, word_start = 0, word_units = 0, words = 0;
    bool in_word = false;
    auto flush = [&]() -> bool {
        if (!in_word) return true;
        in_word = false;
        double hz = 220 + 40 * (words++ % 6);
        return render_mark(r, RDT_SPEECH_WORD, word_start, word_units, nullptr, 0) &&
            render_tone(r, 0.25, hz) && render_tone(r, 0.05, 0);
    };
    FakeStep step;
    while (byte < n) {
        if (!fake_step(t, byte, unit, &step)) return false;
        if (step.markup) {
            // <break> adds silence and <mark> a bookmark at the current frame.
            if (!flush()) return false;
            const char* tag = s + byte;
            const char* end = tag + step.bytes;
            const char* name = strstr(tag, "name=\"");
            const char* time = strstr(tag, "time=\"");
            if (!strncmp(tag, "<mark", 5) && name && name < end) {
                const char* q = strchr(name + 6, '"');
                if (!q || !render_mark(r, RDT_SPEECH_BOOKMARK, 0, 0, name + 6, (size_t)(q - name - 6))) return false;
            } else if (!strncmp(tag, "<break", 6) && time && time < end) {
                if (!render_tone(r, atoi(time + 6) / 1000.0, 0)) return false;
            }
        } else {
            uint32_t cp = step.cp;
            bool cjk = cp >= 0x3400 && cp <= 0x9FFF;
            bool separator = !step.entity && (cp <= ' ' || strchr(",.;:!?\"'()", (int)cp) || (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFF00 && cp <= 0xFF65));
            if (separator || cjk) { if (!flush()) return false; }
            if (!separator) {
                if (!in_word) { in_word = true; word_start = unit; word_units = 0; }
                word_units += step.units;
                if (cjk && !flush()) return false;
            }
        }
        unit += step.units;
        byte += step.bytes;
    }
    return flush() && (r->frames > 0 || render_tone(r, 0.05, 0));
}

// ---------------------------------------------------------------- service state

struct SpeechUtterance {
    uint64_t token;
    DomNodeRef owner;
    SpeechText text;
    char voice_id[160], lang[32];
    SpeechOptions options;
    SpeechState state;
    bool paused_request;          // paused before it began to sound
    bool scheduled;               // `at` was given
    uint64_t at_frame;            // sub-mix frame of `at`
    RdtSpeechJob* job;
    SpeechRender render;          // mono at AUDIO_BUS_RATE once adopted
    uint32_t next_mark;
    int64_t char_index;
};

// what the document's speech voice plays; shared with the audio thread.
// `samples`/`frames`/`requested` change only under the voice lock; the
// renderer writes `start`, `started` and `cursor`; the UI thread writes `paused`.
struct SpeechPlayback {
    const float* samples;
    uint32_t frames;
    uint64_t requested;           // sub-mix frame to start at; an earlier one starts with the next buffer
    uint64_t start;               // sub-mix frame that played the first sample
    int started, paused;
    uint32_t cursor;              // frames played
};

struct DocumentSpeech : DomDocumentResourceData {
    lam::ArrayList<SpeechUtterance*> queue;
    SpeechPlayback playback;
    SpeechUtterance* current;     // armed on the voice, started or not
    AudioVoiceId voice;
    bool fake;
    uint64_t voices_generation;
    DocumentSpeech() : queue(MEM_CAT_RENDER, 0), playback(), current(nullptr), voice(0), fake(false), voices_generation(0) {}
};

static uint32_t playback_cursor(SpeechPlayback* p) { return __atomic_load_n(&p->cursor, __ATOMIC_ACQUIRE); }
static bool playback_started(SpeechPlayback* p) { return __atomic_load_n(&p->started, __ATOMIC_ACQUIRE); }

// an AudioVoiceRender: adds the armed utterance, starting it sample-accurately at `requested`.
static void speech_voice_render(void* source, float* out, uint32_t frames, uint64_t clock) {
    auto* p = (SpeechPlayback*)source;
    if (!p->samples || __atomic_load_n(&p->paused, __ATOMIC_ACQUIRE)) return;
    uint32_t offset = 0;
    if (!playback_started(p)) {
        if (p->requested >= clock + frames) return;
        offset = p->requested > clock ? (uint32_t)(p->requested - clock) : 0;
        p->start = clock + offset;
        __atomic_store_n(&p->started, 1, __ATOMIC_RELEASE);
    }
    uint32_t cursor = playback_cursor(p);
    uint32_t count = frames - offset < p->frames - cursor ? frames - offset : p->frames - cursor;
    for (uint32_t i = 0; i < count; i++) {
        float v = p->samples[cursor + i];
        out[(offset + i) * 2] += v;
        out[(offset + i) * 2 + 1] += v;
    }
    __atomic_store_n(&p->cursor, cursor + count, __ATOMIC_RELEASE);
}

static void playback_reset(SpeechPlayback* p) {
    p->samples = nullptr; p->frames = 0; p->requested = 0; p->start = 0;
    __atomic_store_n(&p->started, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&p->paused, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&p->cursor, 0, __ATOMIC_RELEASE);
}
static void playback_stop(DocumentSpeech* speech) {
    // a stale or lost voice no longer reaches the audio thread, so no lock is needed then.
    bool locked = audio_voice_lock(speech->voice);
    playback_reset(&speech->playback);
    if (locked) audio_voice_unlock(speech->voice);
    speech->current = nullptr;
}

static void utterance_free(SpeechUtterance* u) {
    if (!u) return;
    if (u->job) rdt_speech_job_destroy(u->job);
    speech_text_release(&u->text);
    mem_free(u->render.samples);
    mem_free(u->render.marks);
    mem_free(u);
}

static void document_speech_destroy(DomDocumentResourceData* data) {
    auto* speech = static_cast<DocumentSpeech*>(data);
    // removing the voice first keeps the audio thread off the samples being freed.
    audio_voice_remove(speech->voice);
    for (SpeechUtterance* u : speech->queue) utterance_free(u);
    speech->~DocumentSpeech();
    mem_free(speech);
}

static DocumentSpeech* document_speech(DomDocument* doc, bool create = false) {
    if (!doc) return nullptr;
    for (DomDocumentResource* resource = doc->resources; resource; resource = resource->next)
        if (resource->destroy == document_speech_destroy) return static_cast<DocumentSpeech*>(resource->data);
    if (!create) return nullptr;
    void* storage = mem_alloc(sizeof(DocumentSpeech), MEM_CAT_RENDER);
    auto* speech = storage ? new (storage) DocumentSpeech() : nullptr;
    if (speech && !dom_document_add_resource(doc, speech, document_speech_destroy)) {
        document_speech_destroy(speech);
        return nullptr;
    }
    if (speech) {
        speech->fake = speech_fake_engine();
        speech->voices_generation = speech->fake ? 1 : rdt_speech_voices_generation();
    }
    return speech;
}

static SpeechUtterance* find_utterance(DocumentSpeech* speech, uint64_t token) {
    if (speech) for (SpeechUtterance* u : speech->queue) if (u->token == token) return u;
    return nullptr;
}

// ---------------------------------------------------------------- events

struct SpeechEventFields {
    const char* kind;
    const char* name;
    int64_t char_index, char_length;
    double time;
    double delay;                 // speechstart: seconds after the `at` time it missed
    bool cancelled;
    const char* reason;
};
struct SpeechEventData {
    const char* type;
    uint64_t token;
    const SpeechEventFields* fields;
};
static void speech_event_set(Item event, const char* key, Item value) {
    RootFrame roots(1);
    Rooted<Item> rooted(roots, value);
    dom_realm_set_name(event, key, rooted.get());
}
// fields use the snake_case names Lambda handlers read (evt.char_index); the
// LambdaJS SpeechSynthesisEvent face is M5.
static Item speech_build_event(void* userdata) {
    auto* data = (SpeechEventData*)userdata;
    const SpeechEventFields& f = *data->fields;
    RootFrame roots(1);
    Rooted<Item> event(roots, js_create_event(data->type, true, false));
    speech_event_set(event.get(), "token", data->token ? js_make_number((double)data->token) : ItemNull);
    if (!strcmp(data->type, "speechboundary")) {
        speech_event_set(event.get(), "kind", make_string_item(f.kind ? f.kind : "word"));
        speech_event_set(event.get(), "char_index", js_make_number((double)f.char_index));
        speech_event_set(event.get(), "char_length", js_make_number((double)f.char_length));
    }
    if (!strcmp(data->type, "speechmark")) speech_event_set(event.get(), "name", make_string_item(f.name ? f.name : ""));
    if (!strcmp(data->type, "speechboundary") || !strcmp(data->type, "speechmark") || !strcmp(data->type, "speechend"))
        speech_event_set(event.get(), "time", js_make_number(f.time));
    if (!strcmp(data->type, "speechend")) speech_event_set(event.get(), "cancelled", (Item){.item = b2it(f.cancelled)});
    if (!strcmp(data->type, "speechstart")) speech_event_set(event.get(), "delay", js_make_number(f.delay));
    if (!strcmp(data->type, "speecherror")) speech_event_set(event.get(), "reason", make_string_item(f.reason ? f.reason : "speech failed"));
    return event.get();
}
static void speech_fire_on(DomElement* target, uint64_t token, const char* type, const SpeechEventFields& fields) {
    if (!target || !target->doc) return;
    SpeechEventData data = {type, token, &fields};
    if (!radiant_queue_timing_event((UiContext*)target->doc->js.host_ui_context, target, speech_build_event, &data, type))
        log_debug("speech: no listener context for %s (token %llu)", type, (unsigned long long)token);
}
// fires `type` on the utterance's owner element; a detached owner hears nothing.
static void speech_fire(DomDocument* doc, SpeechUtterance* u, const char* type, const SpeechEventFields& fields) {
    DomNode* owner = dom_node_ref_validate(doc, u->owner);
    if (owner && owner->is_element() && dom_element_is_connected(owner->as_element()))
        speech_fire_on(owner->as_element(), u->token, type, fields);
}

void radiant_speech_report_error(DomElement* owner, const char* reason) {
    SpeechEventFields fields = {};
    fields.reason = reason;
    speech_fire_on(owner, 0, "speecherror", fields);
}

// ---------------------------------------------------------------- public API

const char* radiant_speech_state_name(SpeechState state) {
    static const char* names[] = {"queued", "rendering", "speaking", "paused", "ended", "cancelled", "error"};
    return state >= SPEECH_QUEUED && state <= SPEECH_ERROR ? names[state] : "error";
}

size_t radiant_speech_voices(RdtSpeechVoice* out, size_t capacity) {
    if (speech_fake_engine()) {
        size_t count = sizeof(SPEECH_FAKE_VOICES) / sizeof(SPEECH_FAKE_VOICES[0]);
        for (size_t i = 0; out && i < count && i < capacity; i++) out[i] = SPEECH_FAKE_VOICES[i];
        return count;
    }
    return rdt_speech_voices(out, capacity);
}

static uint64_t speech_next_token() {
    // tokens never alias another document or a finished utterance (D4.5.1v4).
    static uint64_t next = 1;
    return __atomic_fetch_add(&next, 1, __ATOMIC_RELAXED);
}

uint64_t radiant_speech_speak(DomElement* owner, SpeechText* text, const SpeechOptions* options, const char** error) {
    *error = nullptr;
    DomDocument* doc = owner ? owner->doc : nullptr;
    if (!doc || !dom_element_is_connected(owner)) { *error = "speech owner is not connected"; speech_text_release(text); return 0; }
    // D7.5.4: a network document needs sticky activation before it may sound.
    if (!radiant_document_may_start_audio(doc)) {
        *error = "speech needs a user gesture in a network document (D7.5.4)";
        speech_text_release(text); return 0;
    }
    DocumentSpeech* speech = document_speech(doc, true);
    if (!speech) { *error = "out of memory"; speech_text_release(text); return 0; }
    uint32_t live = 0;
    for (SpeechUtterance* q : speech->queue) if (q->state < SPEECH_ENDED) live++;
    if (live >= SPEECH_QUEUE_LIMIT) { *error = "too many queued utterances"; speech_text_release(text); return 0; }
    auto* u = (SpeechUtterance*)mem_calloc(1, sizeof(SpeechUtterance), MEM_CAT_RENDER);
    if (!u) { *error = "out of memory"; speech_text_release(text); return 0; }
    u->token = speech_next_token();
    u->owner = dom_node_ref(owner);
    u->text = *text;
    memset(text, 0, sizeof(*text));
    u->options = *options;
    if (options->voice_id) { snprintf(u->voice_id, sizeof(u->voice_id), "%s", options->voice_id); u->options.voice_id = u->voice_id; }
    if (options->lang) { snprintf(u->lang, sizeof(u->lang), "%s", options->lang); u->options.lang = u->lang; }
    u->char_index = -1;
    u->scheduled = options->at >= 0;
    u->at_frame = u->scheduled ? (uint64_t)llround(options->at * AUDIO_BUS_RATE) : 0;
    if (speech->fake) {
        // the fake engine has no voices beyond its fixed two.
        if (u->options.voice_id && strncmp(u->options.voice_id, "fake.", 5)) { *error = "no voice for the requested voice or language"; utterance_free(u); return 0; }
        if (!speech_fake_render(&u->text, &u->render) || !render_to_bus_rate(&u->render)) { *error = "fake speech render failed"; utterance_free(u); return 0; }
        u->state = SPEECH_QUEUED;
    } else {
        RdtSpeechRequest request = {u->text.text->str, u->text.text->length, u->text.ssml,
            u->options.voice_id, u->options.lang, u->options.rate, u->options.pitch, u->options.volume};
        u->job = rdt_speech_render_begin(&request);
        if (!u->job) { *error = "speech is not available on this platform"; utterance_free(u); return 0; }
        u->state = SPEECH_RENDERING;
    }
    if (!speech->queue.append(u)) { *error = "out of memory"; utterance_free(u); return 0; }
    return u->token;
}

bool radiant_speech_command(DomDocument* doc, uint64_t token, const char* command) {
    DocumentSpeech* speech = document_speech(doc);
    SpeechUtterance* u = find_utterance(speech, token);
    if (!u || !command || u->state >= SPEECH_ENDED) return false;
    SpeechPlayback* p = &speech->playback;
    bool current = speech->current == u;
    // pausing freezes the voice's cursor; an armed utterance that has not begun waits.
    if (!strcmp(command, "pause")) {
        if (current) __atomic_store_n(&p->paused, 1, __ATOMIC_RELEASE);
        if (u->state == SPEECH_SPEAKING) u->state = SPEECH_PAUSED;
        else u->paused_request = true;
        return true;
    }
    if (!strcmp(command, "resume")) {
        u->paused_request = false;
        if (current) __atomic_store_n(&p->paused, 0, __ATOMIC_RELEASE);
        if (u->state == SPEECH_PAUSED) u->state = SPEECH_SPEAKING;
        return true;
    }
    if (!strcmp(command, "cancel")) {
        if (current) playback_stop(speech);
        u->state = SPEECH_CANCELLED;
        SpeechEventFields fields = {}; fields.cancelled = true; fields.char_index = u->char_index;
        speech_fire(doc, u, "speechend", fields);
        return true;
    }
    return false;
}

bool radiant_speech_info(DomDocument* doc, uint64_t token, SpeechInfo* info) {
    DocumentSpeech* speech = document_speech(doc);
    SpeechUtterance* u = find_utterance(speech, token);
    if (!u || !info) return false;
    double position = 0;
    if (speech->current == u) position = playback_cursor(&speech->playback) / (double)AUDIO_BUS_RATE;
    else if (u->state == SPEECH_ENDED) position = u->render.frames / (double)AUDIO_BUS_RATE;
    *info = {u->state, position, u->char_index};
    return true;
}

// ---------------------------------------------------------------- service loop

// copies a finished engine render into the utterance at the bus rate.
static bool adopt_render(SpeechUtterance* u) {
    uint32_t frames = 0, rate = 0, count = 0;
    const float* samples = rdt_speech_job_samples(u->job, &frames, &rate);
    const RdtSpeechMark* marks = rdt_speech_job_marks(u->job, &count);
    if (!samples || !frames || !rate) return false;
    u->render.samples = (float*)mem_alloc((size_t)frames * sizeof(float), MEM_CAT_RENDER);
    u->render.marks = count ? (RdtSpeechMark*)mem_alloc(count * sizeof(RdtSpeechMark), MEM_CAT_RENDER) : nullptr;
    if (!u->render.samples || (count && !u->render.marks)) return false;
    memcpy(u->render.samples, samples, (size_t)frames * sizeof(float));
    if (count) memcpy(u->render.marks, marks, count * sizeof(RdtSpeechMark));
    u->render.frames = frames; u->render.rate = rate; u->render.mark_count = count;
    rdt_speech_job_destroy(u->job);
    u->job = nullptr;
    return render_to_bus_rate(&u->render);
}

// hands an utterance to the document's speech voice, joining the bus first if needed.
static bool playback_arm(DomDocument* doc, DocumentSpeech* speech, SpeechUtterance* u, const char** error) {
    if (!speech->voice) {
        AudioSubmixId submix = radiant_document_submix(doc);
        speech->voice = submix ? audio_voice_add(submix, speech_voice_render, &speech->playback, error) : 0;
        if (!speech->voice) { if (!*error) *error = "audio device unavailable"; return false; }
    }
    if (!audio_voice_lock(speech->voice)) { speech->voice = 0; *error = "audio device unavailable"; return false; }
    SpeechPlayback* p = &speech->playback;
    playback_reset(p);
    p->samples = u->render.samples; p->frames = u->render.frames; p->requested = u->at_frame;
    audio_voice_unlock(speech->voice);
    speech->current = u;
    return true;
}

static void fire_marks(DomDocument* doc, SpeechUtterance* u, uint32_t cursor) {
    while (u->next_mark < u->render.mark_count && u->render.marks[u->next_mark].frame <= cursor) {
        const RdtSpeechMark& m = u->render.marks[u->next_mark++];
        SpeechEventFields fields = {};
        fields.time = m.frame / (double)AUDIO_BUS_RATE;
        if (m.kind == RDT_SPEECH_BOOKMARK) {
            fields.name = m.name; fields.char_index = u->char_index;
            speech_fire(doc, u, "speechmark", fields);
            continue;
        }
        uint32_t index = 0, length = 0;
        if (!speech_text_char_range(&u->text, m.text_start, m.text_length, &index, &length)) continue;
        // speech_info's char_index tracks the word being spoken.
        bool sentence = m.kind == RDT_SPEECH_SENTENCE;
        if (!sentence) u->char_index = index;
        fields.kind = sentence ? "sentence" : "word"; fields.char_index = index; fields.char_length = length;
        speech_fire(doc, u, "speechboundary", fields);
    }
}

static void utterance_fail(DomDocument* doc, SpeechUtterance* u, const char* reason) {
    u->state = SPEECH_ERROR;
    SpeechEventFields fields = {}; fields.reason = reason;
    speech_fire(doc, u, "speecherror", fields);
}

void radiant_speech_follow_rebuild(DomDocument* doc, DomNode* old_root, DomNode* new_root) {
    DocumentSpeech* speech = document_speech(doc);
    if (!speech || !old_root || !new_root || old_root == new_root) return;
    for (SpeechUtterance* u : speech->queue)
        if (u->state < SPEECH_ENDED && dom_node_ref_validate(doc, u->owner))
            view_state_ref_follow_rebuild(doc, &u->owner, old_root, new_root);
}

// cancels the utterances whose owner left the document; there is no one left to tell.
static void speech_prune(DocumentSpeech* speech, DomDocument* doc) {
    for (SpeechUtterance* u : speech->queue) {
        if (u->state >= SPEECH_ENDED) continue;
        DomNode* owner = dom_node_ref_validate(doc, u->owner);
        if (owner && owner->is_element() && dom_element_is_connected(owner->as_element())) continue;
        if (speech->current == u) playback_stop(speech);
        u->state = SPEECH_CANCELLED;
    }
}

bool radiant_speech_tick(DomDocument* doc) {
    DocumentSpeech* speech = document_speech(doc);
    if (!speech) return false;
    uint64_t generation = speech->fake ? 1 : rdt_speech_voices_generation();
    if (generation != speech->voices_generation) {
        speech->voices_generation = generation;
        radiant_dispatch_window_event((UiContext*)doc->js.host_ui_context, doc, "voiceschanged");
    }
    speech_prune(speech, doc);
    // a lost device ends the utterance it was playing; the next one reopens the bus.
    if (speech->voice && audio_voice_lost(speech->voice)) {
        SpeechUtterance* lost = speech->current;
        playback_stop(speech);
        audio_voice_remove(speech->voice);
        speech->voice = 0;
        if (lost) utterance_fail(doc, lost, "audio device lost");
    }
    // finish engine renders; a failed render is reported and dropped from the queue.
    for (SpeechUtterance* u : speech->queue) {
        if (u->state != SPEECH_RENDERING || !u->job) continue;
        RdtSpeechJobState state = rdt_speech_job_state(u->job);
        if (state == RDT_SPEECH_RENDERING) continue;
        if (state == RDT_SPEECH_RENDERED && adopt_render(u)) { u->state = SPEECH_QUEUED; continue; }
        const char* reason = rdt_speech_job_error(u->job);
        utterance_fail(doc, u, reason ? reason : "speech render failed");
    }
    SpeechPlayback* p = &speech->playback;
    SpeechUtterance* u = speech->current;
    if (u && playback_started(p)) {
        if (u->state == SPEECH_QUEUED) {
            u->state = SPEECH_SPEAKING;
            SpeechEventFields fields = {};
            if (u->scheduled && p->start > u->at_frame) fields.delay = (p->start - u->at_frame) / (double)AUDIO_BUS_RATE;
            speech_fire(doc, u, "speechstart", fields);
        }
        uint32_t cursor = playback_cursor(p);
        fire_marks(doc, u, cursor);
        if (cursor >= p->frames) {
            playback_stop(speech);
            u->state = SPEECH_ENDED;
            SpeechEventFields fields = {}; fields.char_index = u->char_index;
            fields.time = u->render.frames / (double)AUDIO_BUS_RATE;
            speech_fire(doc, u, "speechend", fields);
        }
    }
    // arm the next utterance in order; a rendering or paused head holds the queue.
    if (!speech->current) for (SpeechUtterance* next : speech->queue) {
        if (next->state >= SPEECH_ENDED) continue;
        if (next->state == SPEECH_RENDERING || next->paused_request) break;
        const char* error = nullptr;
        if (!playback_arm(doc, speech, next, &error)) { utterance_fail(doc, next, error); continue; }
        break;
    }
    // finished utterances stay queryable until the queue grows; trim the oldest.
    while (speech->queue.size() > SPEECH_QUEUE_LIMIT * 2 && speech->queue[0]->state >= SPEECH_ENDED) {
        utterance_free(speech->queue[0]);
        speech->queue.remove(0);
    }
    bool active = false;
    for (SpeechUtterance* q : speech->queue) if (q->state < SPEECH_ENDED) active = true;
    // an idle document leaves the bus, so the device can close.
    if (!active && speech->voice) { audio_voice_remove(speech->voice); speech->voice = 0; }
    return active;
}

void radiant_speech_watch_voices(DomDocument* doc) { document_speech(doc, true); }

bool radiant_speech_active(DomDocument* doc) {
    DocumentSpeech* speech = document_speech(doc);
    if (speech) for (SpeechUtterance* u : speech->queue) if (u->state < SPEECH_ENDED) return true;
    return false;
}

bool radiant_speech_wall_clock_pending(DomDocument* doc) {
    DocumentSpeech* speech = document_speech(doc);
    return speech && (!speech->fake || !audio_bus_offline()) && radiant_speech_active(doc);
}
