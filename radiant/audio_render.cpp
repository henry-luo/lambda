#include "audio_render.hpp"
#include "audio_cue_owner.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/endian.h"
#include "../lib/mem.h"
#include "../lib/str.h"
#include <math.h>
#include <string.h>

struct AudioRenderJob {
    DomDocument* document;
    uint64_t session, id, frames, rendered;
    AudioSubmixId submix;
    AudioGraphId graph;
    AudioCueOwner* owner;
    float* output;
    size_t bytes;
    AudioRenderState state;
    AudioCueSnapshot receipts;
    char diagnostic[256];
};
struct AudioRenderPool : DomDocumentResourceData { AudioRenderJob* jobs[AUDIO_RENDER_JOBS]; };
const char* audio_render_state_name(AudioRenderState state) {
    static const char* names[] = {"ready", "running", "complete", "cancelled", "error"};
    return names[state];
}
static void render_retire(AudioRenderJob* job) {
    if (job->owner) {
        auto* queue = audio_cue_owner_queue(job->owner);
        if (audio_submix_lock(job->submix)) {
            audio_cues_cancel_locked(queue, 0, job->rendered); audio_cues_snapshot_locked(queue, &job->receipts);
            audio_submix_unlock(job->submix);
        }
        audio_cue_owner_destroy(job->owner); job->owner = nullptr;
    }
    audio_submix_destroy(job->submix); job->submix = 0; job->graph = 0;
}
static void render_destroy(AudioRenderJob* job) {
    if (!job) return;
    render_retire(job); mem_free(job->output); mem_free(job);
}
static void render_pool_destroy(DomDocumentResourceData* data) {
    auto* pool = (AudioRenderPool*)data;
    for (auto* job : pool->jobs) render_destroy(job);
    mem_free(pool);
}
static AudioRenderPool* render_pool(DomDocument* document, bool create = false) {
    if (!document) return nullptr;
    for (auto* resource = document->resources.get(); resource; resource = resource->next.get())
        if (resource->destroy == render_pool_destroy) return (AudioRenderPool*)resource->data.get();
    if (!create) return nullptr;
    auto* pool = (AudioRenderPool*)mem_calloc(1, sizeof(AudioRenderPool), MEM_CAT_RENDER);
    if (pool && !dom_document_add_resource(document, pool, render_pool_destroy)) { mem_free(pool); return nullptr; }
    return pool;
}
static AudioRenderJob* render_job(DomDocument* document, uint64_t session, uint64_t id) {
    auto* pool = render_pool(document);
    if (pool) for (auto* job : pool->jobs) if (job && job->session == session && job->id == id) return job;
    return nullptr;
}
static void render_notice(void* data, uint64_t, AudioPlayerNotice) {
    auto* job = (AudioRenderJob*)data;
    job->state = AUDIO_RENDER_ERROR;
    const char* message = "render reader was externally retired";
    str_copy(job->diagnostic, sizeof(job->diagnostic), message, strlen(message));
}
static unsigned render_source(const AudioRenderPlan& plan, uint64_t identity) {
    for (unsigned i = 0; i < plan.source_count; i++) if (plan.sources[i].identity == identity) return i;
    return plan.source_count;
}
bool audio_render_open(DomDocument* document, uint64_t session, const AudioRenderPlan& input, uint64_t* id, const char** error) {
    const char* ignored = nullptr; if (!error) error = &ignored; *error = nullptr;
    auto fail = [&](const char* text) { *error = text; return false; };
    AudioRenderPlan plan = input;
    if (!document || !session || !id || !plan.frames || plan.frames > AUDIO_RENDER_BYTES / (2 * sizeof(float)) ||
        plan.source_count > AUDIO_CUE_TRACKS || plan.provider_count > AUDIO_CUE_TRACKS - plan.source_count ||
        plan.event_count > AUDIO_CUE_CAPACITY || plan.provider_count > AUDIO_CUE_CAPACITY - plan.event_count ||
        (!plan.provider_count && (!plan.source_count || !plan.event_count)) ||
        !isfinite(plan.scene_time) || plan.scene_time < 0 || !isfinite(plan.time_scale) || plan.time_scale <= 0)
        return fail("audio render: invalid plan or finite output limit");
    // register the native quota owner before the pool that will retire its snapshot readers (D4.5.2).
    if (plan.provider_count && rdt_audio_pcm_storage_bytes(plan.frames) > radiant_audio_available_pcm(document))
        return fail("audio render: provider snapshot exceeds document PCM/reader quota");
    auto* pool = render_pool(document, true); if (!pool) return fail("audio render: document owner admission failed");
    unsigned slot = AUDIO_RENDER_JOBS; size_t bytes = (size_t)plan.frames * 2 * sizeof(float), retained = 0;
    for (unsigned i = 0; i < AUDIO_RENDER_JOBS; i++) { if (pool->jobs[i]) retained += pool->jobs[i]->bytes; else if (slot == AUDIO_RENDER_JOBS) slot = i; }
    if (slot == AUDIO_RENDER_JOBS || bytes > AUDIO_RENDER_DOCUMENT_BYTES - retained) return fail("audio render: document job/output quota exceeded");
    struct Prepared {
        AudioRenderJob* job;
        ~Prepared() { render_destroy(job); }
    } prepared{(AudioRenderJob*)mem_calloc(1, sizeof(AudioRenderJob), MEM_CAT_RENDER)};
    auto* job = prepared.job; if (!job) return fail("audio render: native job allocation failed");
    job->document = document; job->session = session; job->frames = plan.frames; job->bytes = bytes;
    job->output = (float*)mem_calloc(1, bytes, MEM_CAT_RENDER); job->submix = audio_submix_create(true);
    job->graph = audio_submix_graph_open(job->submix); job->id = job->graph;
    if (!job->output || !job->graph) return fail("audio render: private output/submix quota exceeded");
    AudioGraphConfig config = plan.graph;
    if (!config.count) { config.count = 1; config.buses[0] = audio_graph_bus_default("master"); }
    AudioGraph* graph = nullptr;
    if (!audio_submix_graph_prepare(job->submix, job->graph, config, &graph, error)) return false;
    if (!audio_submix_graph_commit(job->submix, job->graph, graph, nullptr, 0)) {
        audio_graph_destroy(graph); return fail("audio render: private graph publication failed");
    }
    job->owner = audio_cue_owner_create(document, job->submix, 1, job, render_notice);
    if (!job->owner) return fail("audio render: cue owner admission failed");
    struct ProviderAssets {
        DomDocument* document; uint64_t tokens[AUDIO_CUE_TRACKS] = {}; unsigned count = 0;
        ~ProviderAssets() { for (unsigned i = 0; i < count; i++) radiant_audio_close(document, tokens[i]); }
    } assets{document};
    for (unsigned i = 0; i < plan.provider_count; i++) {
        const auto& provider = plan.providers[i]; const char* bus = *provider.bus ? provider.bus : "master";
        if (!audio_submix_graph_route(job->submix, job->graph, nullptr, bus)) return fail("audio render: invalid copied provider route");
        auto asset = radiant_audio_provider_snapshot(document, provider.provider, provider.token, provider.position, (uint32_t)plan.frames, error);
        if (!asset) return false;
        assets.tokens[assets.count++] = asset;
        auto& source = plan.sources[plan.source_count++]; source = {}; source.asset = asset;
        source.identity = audio_bus_identity(); if (!source.identity) return fail("audio render: source identity exhausted");
        source.parameters = rdt_audio_default_parameters(); source.gain = 1; str_copy(source.bus, sizeof(source.bus), bus, strlen(bus));
        // prepared provider PCM joins the same sample-zero cue path as ordinary copied clips.
        auto& event = plan.events[plan.event_count++]; event = {}; event.identity = source.identity; event.clip = asset; event.op = AUDIO_CUE_PLAY;
        str_fmt(event.source, sizeof(event.source), "provider:%s:%llu", provider.provider->name, (unsigned long long)provider.token);
    }
    uint64_t sources[AUDIO_CUE_TRACKS] = {}, candidates[AUDIO_CUE_CAPACITY] = {};
    AudioPlayerLease targets[AUDIO_CUE_TRACKS] = {}, readers[AUDIO_CUE_CAPACITY] = {};
    // each private reader shares the accounted immutable asset, never a live cursor or DSP history.
    for (unsigned i = 0; i < plan.source_count; i++) {
        const auto& source = plan.sources[i];
        if (!source.identity || render_source(plan, source.identity) != i) return fail("audio render: duplicate source identity");
        sources[i] = radiant_audio_clone_ready(document, source.asset, &source.parameters, source.gain);
        if (!sources[i]) return fail("audio render: document reader quota exceeded");
        auto route = audio_submix_graph_route(job->submix, job->graph, nullptr, source.bus);
        if (!route || !audio_submix_lock(job->submix)) { radiant_audio_close(document, sources[i]); return fail("audio render: invalid copied source route"); }
        bool bound = audio_cue_owner_bind_locked(job->owner, sources[i], true, true, route, targets + i);
        audio_submix_unlock(job->submix);
        if (!bound) { radiant_audio_close(document, sources[i]); return fail("audio render: shared voice capacity exceeded"); }
    }
    for (unsigned i = 0; i < plan.event_count; i++) {
        const auto& event = plan.events[i]; unsigned source = render_source(plan, event.identity);
        if (source == plan.source_count) return fail("audio render: event has no copied source");
        if (event.op != AUDIO_CUE_PLAY) continue;
        const auto& spec = plan.sources[source];
        candidates[i] = radiant_audio_clone_ready(document, spec.asset, &spec.parameters, spec.gain);
        if (!candidates[i]) return fail("audio render: prepared reader quota exceeded");
        if (!audio_submix_lock(job->submix)) { radiant_audio_close(document, candidates[i]); return fail("audio render: retired private submix"); }
        bool bound = audio_cue_owner_bind_locked(job->owner, candidates[i], true, false, 0, readers + i);
        audio_submix_unlock(job->submix);
        if (!bound) { radiant_audio_close(document, candidates[i]); return fail("audio render: cue reader admission failed"); }
    }
    if (!audio_submix_lock(job->submix)) return fail("audio render: retired private submix");
    auto* queue = audio_cue_owner_queue(job->owner);
    bool attached = true;
    for (unsigned i = 0; i < plan.source_count; i++) attached &= audio_cues_attach_locked(queue, plan.sources[i].identity, targets[i]);
    audio_cues_clock_locked(queue, 0, plan.scene_time, plan.time_scale, false);
    const char* rejected = attached ? nullptr : "audio render: source lease admission failed";
    for (unsigned begin = 0; begin < plan.event_count && !rejected; begin += AUDIO_CUE_BATCH) {
        unsigned count = plan.event_count - begin; if (count > AUDIO_CUE_BATCH) count = AUDIO_CUE_BATCH;
        rejected = audio_cues_commit_locked(queue, 1, begin / AUDIO_CUE_BATCH + 1, plan.events + begin, readers + begin, count, 0);
    }
    AudioCueSnapshot snapshot; audio_cues_snapshot_locked(queue, &snapshot);
    for (unsigned i = 0; i < snapshot.pending_count && !rejected; i++) if (snapshot.pending[i].held || snapshot.pending[i].requested > plan.frames)
        rejected = "audio render: cue exceeds the selected virtual interval";
    audio_submix_unlock(job->submix);
    if (rejected) return fail(rejected);
    audio_cue_owner_collect(job->owner); pool->jobs[slot] = job; prepared.job = nullptr; *id = job->id; return true;
}
bool audio_render_step(DomDocument* document, uint64_t session, uint64_t id, unsigned frames, const char** error) {
    const char* ignored = nullptr; if (!error) error = &ignored; *error = nullptr;
    auto* job = render_job(document, session, id);
    if (!job || !frames || frames > AUDIO_RENDER_STEP) { *error = "audio render: missing job or invalid step budget"; return false; }
    if (job->state == AUDIO_RENDER_COMPLETE) return true;
    if (job->state != AUDIO_RENDER_READY && job->state != AUDIO_RENDER_RUNNING) {
        render_retire(job); *error = "audio render: terminal job cannot advance"; return false;
    }
    unsigned count = job->frames - job->rendered < frames ? (unsigned)(job->frames - job->rendered) : frames;
    if (!audio_submix_render(job->submix, job->output + job->rendered * 2, count)) { *error = "audio render: private clock unavailable"; return false; }
    job->rendered += count; job->state = job->rendered == job->frames ? AUDIO_RENDER_COMPLETE : AUDIO_RENDER_RUNNING;
    AudioGraphSnapshot graph; audio_submix_graph_snapshot(job->submix, job->graph, &graph);
    if (graph.faulted) {
        job->state = AUDIO_RENDER_ERROR; const char* message = "native DSP graph fault";
        str_copy(job->diagnostic, sizeof(job->diagnostic), message, strlen(message));
    }
    audio_cue_owner_collect(job->owner);
    if (job->state == AUDIO_RENDER_COMPLETE || job->state == AUDIO_RENDER_ERROR) {
        if (job->state == AUDIO_RENDER_COMPLETE && audio_submix_lock(job->submix)) {
            audio_cues_apply_locked(audio_cue_owner_queue(job->owner), job->rendered); audio_submix_unlock(job->submix);
        }
        render_retire(job);
    }
    return true;
}
bool audio_render_snapshot(DomDocument* document, uint64_t session, uint64_t id, AudioRenderSnapshot* snapshot) {
    auto* job = render_job(document, session, id); if (!job || !snapshot) return false;
    if (job->state == AUDIO_RENDER_ERROR) render_retire(job);
    *snapshot = {}; snapshot->id = id; snapshot->frames = job->frames; snapshot->rendered = job->rendered;
    snapshot->bytes = job->bytes; snapshot->state = job->state; memcpy(snapshot->diagnostic, job->diagnostic, sizeof(snapshot->diagnostic));
    if (job->owner && audio_submix_lock(job->submix)) {
        audio_cues_snapshot_locked(audio_cue_owner_queue(job->owner), &snapshot->cues); audio_submix_unlock(job->submix);
    } else snapshot->cues = job->receipts;
    return true;
}
bool audio_render_cancel(DomDocument* document, uint64_t session, uint64_t id) {
    auto* job = render_job(document, session, id); if (!job) return false;
    render_retire(job); mem_free(job->output); job->output = nullptr; job->bytes = 0; job->state = AUDIO_RENDER_CANCELLED; return true;
}
bool audio_render_close(DomDocument* document, uint64_t session, uint64_t id) {
    auto* pool = render_pool(document); if (!pool) return false;
    for (auto*& job : pool->jobs) if (job && job->session == session && job->id == id) {
        render_destroy(job); job = nullptr; return true;
    }
    return false;
}
void audio_render_close_session(DomDocument* document, uint64_t session) {
    auto* pool = render_pool(document); if (pool) for (auto*& job : pool->jobs) if (job && job->session == session) { render_destroy(job); job = nullptr; }
}
bool audio_render_read(DomDocument* document, uint64_t session, uint64_t id, uint64_t offset, unsigned frames,
    bool header, void* bytes, size_t capacity, size_t* length, const char** error) {
    const char* ignored = nullptr; if (!error) error = &ignored; *error = nullptr;
    auto* job = render_job(document, session, id);
    if (!job || !bytes || !length || !job->output || (!header && (!frames || frames > AUDIO_RENDER_STEP ||
        offset > job->rendered || frames > job->rendered - offset))) { *error = "audio render: unavailable result range"; return false; }
    size_t count = header ? AUDIO_RENDER_WAV_HEADER : (size_t)frames * 2 * sizeof(float);
    if (capacity < count) { *error = "audio render: result copy capacity exceeded"; return false; }
    auto* out = (uint8_t*)bytes;
    if (header) {
        // IEEE-float WAV keeps the native stereo precision and streams through ordinary procedural IO.
        memcpy(out, "RIFF", 4); write_le32(out + 4, (uint32_t)(50 + job->frames * 8)); memcpy(out + 8, "WAVEfmt ", 8);
        write_le32(out + 16, 18); write_le16(out + 20, 3); write_le16(out + 22, 2); write_le32(out + 24, AUDIO_BUS_RATE);
        write_le32(out + 28, AUDIO_BUS_RATE * 8); write_le16(out + 32, 8); write_le16(out + 34, 32);
        write_le16(out + 36, 0); memcpy(out + 38, "fact", 4); write_le32(out + 42, 4); write_le32(out + 46, (uint32_t)job->frames);
        memcpy(out + 50, "data", 4); write_le32(out + 54, (uint32_t)(job->frames * 8));
    } else for (size_t i = 0; i < (size_t)frames * 2; i++) {
        uint32_t bits; memcpy(&bits, job->output + (size_t)offset * 2 + i, sizeof(bits)); write_le32(out + i * 4, bits);
    }
    *length = count; return true;
}
