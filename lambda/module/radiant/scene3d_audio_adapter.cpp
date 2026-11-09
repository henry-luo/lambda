#include "scene3d_audio_adapter.hpp"
#include "radiant/scene3d/scene3d_audio_internal.hpp"
#include "radiant/scene3d/scene3d_source.hpp"
#include "radiant/audio.hpp"
#include "radiant/audio_cues.hpp"
#include "radiant/audio_cue_owner.hpp"
#include "radiant/audio_render.hpp"
#include "radiant/speech.hpp"
#include "radiant/event.hpp"
#include "lambda/input/css/dom_element.hpp"
#include "lambda/core/mark_reader.hpp"
#include "lambda/core/physics_scene.hpp"
#include "../../runtime/value_builder.hpp"
#include "../../runtime/lambda-error.h"
#include "../../runtime/lambda-number-runtime.hpp"
#include "../../runtime/lambda-navigation.hpp"
#include "lib/mem.h"
#include "lib/str.h"
#include <float.h>

static AudioClip* audio_clip(Scene3dAudio* state, const char* id) {
    if (id) for (unsigned i = 0; i < state->clip_count; i++) if (!strcmp(state->clips[i].id, id)) return state->clips + i;
    return nullptr;
}
static Item audio_string(const char* text) { return Item{.item = s2it(heap_create_name(text))}; }
static void audio_motion_value(RootedValueBuilder* result, const AudioMotion& motion) {
    result->put("position", navigation_point_value({motion.position.x, motion.position.y, motion.position.z}, 3));
    result->put("velocity", navigation_point_value({motion.velocity.x, motion.velocity.y, motion.velocity.z}, 3));
}
static void audio_cue_rows(RootedValueBuilder* rows, const AudioCueRow* values, unsigned count) {
    for (unsigned i = 0; i < count; i++) {
        const auto& cue = values[i]; RootedValueBuilder row;
        row.put("id", box_uint64_value(cue.id)); row.put("epoch", box_uint64_value(cue.epoch));
        row.put("source", audio_string(cue.command.source)); row.put("identity", box_uint64_value(cue.command.identity));
        row.put("clip_identity", box_uint64_value(cue.command.clip)); row.put("op", audio_string(audio_cue_op_name(cue.command.op)));
        row.put("clock", audio_string(cue.command.scene ? "scene" : "audio")); row.put("seconds", push_d(cue.command.seconds));
        row.put("requested", box_uint64_value(cue.requested)); row.put("actual", box_uint64_value(cue.actual));
        row.put("held", Item{.item = b2it(cue.held)}); row.put("status", audio_string(audio_cue_status_name(cue.status)));
        rows->append(row.value.get());
    }
}
static Item audio_snapshot(Scene3dAudio* state) {
    scene3d_audio_cues_collect(state);
    Scene3dRuntimeStats clock = {}; scene3d_runtime_stats(state->runtime, &clock);
    // scene result VMaps preserve opaque u64 fields through the shared JS membrane (S4.9.1).
    RootedValueBuilder result, sources(true), clips(true);
    result.put("session", box_uint64_value(clock.session));
    result.put("generation", box_uint64_value(state->generation));
    result.put("sample_sequence", box_uint64_value(state->sample_sequence));
    result.put("scene_time", push_d(scene3d_runtime_sampled_time(state->runtime)));
    result.put("audio_clock", push_d(radiant_audio_clock(state->document)));
    result.put("audio_frame", box_uint64_value(radiant_audio_frame(state->document)));
    AudioCueSnapshot cues = {}; cues.epoch = state->cue_epoch; cues.next_id = 1;
    if (state->cues && audio_submix_lock(state->submix)) {
        audio_cues_snapshot_locked(state->cues, &cues); audio_submix_unlock(state->submix);
    }
    result.put("cue_epoch", box_uint64_value(cues.epoch)); result.put("cue_sequence", box_uint64_value(cues.sequence));
    result.put("cue_next_id", box_uint64_value(cues.next_id)); result.put("cue_history_dropped", box_uint64_value(cues.history_dropped));
    RootedValueBuilder pending(true), history(true), receipt(true);
    audio_cue_rows(&pending, cues.pending, cues.pending_count); audio_cue_rows(&history, cues.history, cues.history_count);
    for (unsigned i = 0; i < cues.receipt_count; i++) receipt.append(box_uint64_value(cues.receipt[i]));
    result.put("cue_pending", pending.value.get()); result.put("cue_history", history.value.get()); result.put("cue_receipt", receipt.value.get());
    size_t players = 0, bytes = 0; radiant_audio_usage(state->document, &players, &bytes);
    result.put("document_players", box_uint64_value(players)); result.put("document_bytes", box_uint64_value(bytes));
    result.put("diagnostic", audio_string(state->diagnostic));
    for (unsigned i = 0; i < state->clip_count; i++) {
        const auto& clip = state->clips[i]; RdtAudioInfo info = {};
        bool available = radiant_audio_info(state->document, clip.token, &info);
        RootedValueBuilder row;
        row.put("id", audio_string(clip.id)); row.put("identity", box_uint64_value(clip.identity));
        row.put("frames", box_uint64_value(clip.frames)); row.put("duration", push_d(info.duration));
        row.put("available", Item{.item = b2it(available)}); clips.append(row.value.get());
    }
    for (unsigned i = 0; i < state->count; i++) {
        const auto& source = state->sources[i]; const auto& params = source.parameters;
        RdtAudioInfo info = {}; bool live = source.player && radiant_audio_info(state->document, source.player, &info);
        auto* clip = audio_clip(state, source.description.clip);
        RootedValueBuilder row;
        row.put("id", audio_string(source.description.id)); row.put("identity", box_uint64_value(source.identity));
        row.put("clip", audio_string(source.description.clip)); row.put("playing_clip", box_uint64_value(live && info.state != RDT_AUDIO_READY ? info.identity : 0));
        row.put("armed", Item{.item = b2it(live && info.armed)}); row.put("pitch_follow", Item{.item = b2it(source.description.pitch_follow)});
        row.put("bus", audio_string(*source.description.bus ? source.description.bus : "master"));
        row.put("available", Item{.item = b2it(clip != nullptr)});
        row.put("state", audio_string(live ? rdt_audio_state_name(info.state) : "ready"));
        row.put("spatial", audio_string(source.description.spatial == AUDIO_STEREO ? "none" : source.description.spatial == AUDIO_2D ? "2d" : "3d"));
        audio_motion_value(&row, source.motion);
        row.put("left", push_d(params.left)); row.put("right", push_d(params.right)); row.put("pitch", push_d(params.pitch));
        row.put("gain", push_d(source.description.values[AUDIO_GAIN])); row.put("lowpass", push_d(params.lowpass));
        row.put("smoothing", push_d(params.smoothing)); row.put("loop", Item{.item = b2it(params.loop)});
        row.put("current_left", push_d(info.left)); row.put("current_right", push_d(info.right)); row.put("current_pitch", push_d(info.pitch));
        row.put("cursor", push_d(info.position)); row.put("duration", push_d(info.duration));
        row.put("rendered", box_uint64_value(info.rendered)); row.put("peak", push_d(info.peak)); row.put("rms", push_d(info.rms));
        sources.append(row.value.get());
    }
    result.put("sources", sources.value.get()); result.put("clips", clips.value.get());
    RootedValueBuilder buses(true); AudioGraphSnapshot graph = {};
    if (state->graph) audio_submix_graph_snapshot(state->submix, state->graph, &graph);
    result.put("routing_generation", box_uint64_value(state->routing_generation)); result.put("routing_bytes", box_uint64_value(graph.bytes));
    result.put("routing_active", Item{.item = b2it(graph.active)}); result.put("routing_faulted", Item{.item = b2it(graph.faulted)});
    for (unsigned i = 0; i < graph.config.count; i++) {
        const auto& config = graph.config.buses[i]; const auto& meter = graph.meters[i];
        RootedValueBuilder row, inserts(true), sends(true);
        row.put("id", audio_string(config.id)); row.put("identity", box_uint64_value(graph.routes[i]));
        row.put("parent", audio_string(config.parent)); row.put("gain", push_d(config.gain)); row.put("current_gain", push_d(meter.current_gain));
        row.put("mute", Item{.item = b2it(config.mute)}); row.put("solo", Item{.item = b2it(config.solo)});
        row.put("peak", push_d(meter.peak)); row.put("rms", push_d(meter.rms)); row.put("frames", box_uint64_value(meter.frames));
        for (unsigned j = 0; j < config.insert_count; j++) {
            const auto& effect = config.inserts[j]; RootedValueBuilder insert;
            insert.put("type", audio_string(audio_effect_name(effect.kind))); insert.put("sidechain", audio_string(effect.sidechain));
            size_t field_count = 0;
            const AudioEffectField* fields = scene3d_audio_effect_fields(&field_count);
            for (size_t i = 0; i < field_count; i++)
                insert.put(fields[i].key, push_d(*(const float*)((const char*)&effect + fields[i].offset)));
            inserts.append(insert.value.get());
        }
        for (unsigned j = 0; j < config.send_count; j++) {
            RootedValueBuilder send; send.put("bus", audio_string(config.sends[j].bus)); send.put("gain", push_d(config.sends[j].gain));
            send.put("current_gain", push_d(graph.send_gains[i][j])); sends.append(send.value.get());
        }
        row.put("inserts", inserts.value.get()); row.put("sends", sends.value.get()); buses.append(row.value.get());
    }
    result.put("buses", buses.value.get());
    if (state->listener.node.address) {
        RootedValueBuilder listener;
        listener.put("id", audio_string(state->listener.id)); listener.put("camera", Item{.item = b2it(state->listener.camera)});
        audio_motion_value(&listener, state->listener.motion);
        listener.put("right", navigation_point_value({state->listener.right.x, state->listener.right.y, state->listener.right.z}, 3));
        listener.put("forward", navigation_point_value({state->listener.forward.x, state->listener.forward.y, state->listener.forward.z}, 3));
        result.put("listener", listener.value.get());
    } else result.put("listener", ItemNull);
    return result.value.get();
}
static const char* audio_copy_commands(Scene3dAudio* state, ItemReader input, unsigned limit,
    AudioCueCommand* commands, AudioSource** sources, unsigned* size) {
    if (!input.isArray() || input.asArray().length() < 1 || input.asArray().length() > limit)
        return "audio cues: invalid bounded event array";
    unsigned count = (unsigned)input.asArray().length();
    for (unsigned i = 0; i < count; i++) {
        auto event = input.asArray().get(i); if (!event.isMap()) return "audio cues: event must be a command map";
        auto row = event.asMap(); auto& cue = commands[i];
        const char* source = scene3d_command_name(row.get("source")), *op = scene3d_command_name(row.get("op"));
        const char* clock = row.get("clock").isNull() ? "audio" : scene3d_command_name(row.get("clock"));
        const char* late = row.get("late").isNull() ? "reject" : scene3d_command_name(row.get("late"));
        if (!source || !*source || strlen(source) >= sizeof(cue.source) || !op || !clock || !late ||
            !scene3d_counter(row.get("identity"), &cue.identity) || !cue.identity) return "audio cues: invalid source identity or command";
        str_copy(cue.source, sizeof(cue.source), source, strlen(source));
        if (!strcmp(op, "play")) cue.op = AUDIO_CUE_PLAY;
        else if (!strcmp(op, "pause")) cue.op = AUDIO_CUE_PAUSE;
        else if (!strcmp(op, "resume")) cue.op = AUDIO_CUE_RESUME;
        else if (!strcmp(op, "stop")) cue.op = AUDIO_CUE_STOP;
        else return "audio cues: unknown transport operation";
        if (!strcmp(clock, "scene")) cue.scene = true;
        else if (strcmp(clock, "audio")) return "audio cues: unknown clock";
        if (!strcmp(late, "now")) cue.late = AUDIO_CUE_NOW;
        else if (!strcmp(late, "drop")) cue.late = AUDIO_CUE_DROP;
        else if (strcmp(late, "reject")) return "audio cues: unknown late policy";
        if (cue.scene) {
            if (!row.get("frame").isNull() || !physics_scene_real(row.get("seconds"), NAN, &cue.seconds) ||
                !isfinite(cue.seconds) || cue.seconds < 0) return "audio cues: invalid scene seconds";
        } else if (!row.get("seconds").isNull() || !scene3d_counter(row.get("frame"), &cue.frame) || cue.frame == UINT64_MAX)
            return "audio cues: requires an exact document sample frame";
        if (cue.op == AUDIO_CUE_PLAY) {
            if (!scene3d_counter(row.get("clip_identity"), &cue.clip) || !cue.clip) return "audio cues: missing clip identity";
        } else if (!row.get("clip_identity").isNull()) return "audio cues: clip identity is only valid for play";
        for (unsigned j = 0; j < state->count; j++) if (!strcmp(state->sources[j].description.id, source)) sources[i] = state->sources + j;
    }
    *size = count; return nullptr;
}
static const char* audio_schedule(Scene3dAudio* state, MapReader fields) {
    uint64_t epoch, sequence;
    if (!scene3d_counter(fields.get("epoch"), &epoch) || epoch != state->cue_epoch || !epoch ||
        !scene3d_counter(fields.get("sequence"), &sequence)) return "audio cues: invalid epoch or sequence";
    unsigned count = 0; AudioCueCommand commands[AUDIO_CUE_BATCH] = {};
    AudioSource* sources[AUDIO_CUE_BATCH] = {}; AudioClip* clips[AUDIO_CUE_BATCH] = {};
    if (const char* error = audio_copy_commands(state, fields.get("events"), AUDIO_CUE_BATCH, commands, sources, &count)) return error;
    if (!state->cues) {
        state->submix = radiant_document_submix(state->document);
        state->cue_owner = audio_cue_owner_create(state->document, state->submix, state->cue_epoch);
        state->cues = audio_cue_owner_queue(state->cue_owner);
        if (!state->cues) return "audio cues: native owner capacity exceeded";
    }
    if (const char* error = scene3d_audio_cues_sync(state)) return error;
    bool duplicate = false; const char* error;
    if (!audio_submix_lock(state->submix)) return "audio cues: retired submix";
    error = audio_cues_check_locked(state->cues, epoch, sequence, commands, count, audio_submix_clock(state->submix), &duplicate);
    bool output = audio_cues_output_locked(state->cues, commands, count, audio_submix_clock(state->submix));
    audio_submix_unlock(state->submix);
    if (error || duplicate) return error;
    // all peer identities and activation are checked before preparing or reserving any reader.
    for (unsigned i = 0; i < count; i++) {
        auto* source = sources[i]; const auto& cue = commands[i];
        if (!source || source->identity != cue.identity) return "audio cues: stale source identity";
        if (cue.op == AUDIO_CUE_PLAY) {
            clips[i] = audio_clip(state, source->description.clip);
            if (!clips[i] || clips[i]->identity != cue.clip) return "audio cues: stale clip identity";
        } else if (!source->player) {
            bool earlier_play = false;
            for (unsigned j = 0; j < i; j++) earlier_play |= sources[j] == source && commands[j].op == AUDIO_CUE_PLAY;
            if (!earlier_play) return "audio cues: transport requires a prepared source";
        }
        if ((cue.op == AUDIO_CUE_PLAY || cue.op == AUDIO_CUE_RESUME) && source->description.values[AUDIO_GAIN] > 0 &&
            !radiant_document_may_start_audio(state->document)) return "audio cues: audible playback requires document activation";
    }
    uint64_t candidates[AUDIO_CUE_BATCH] = {}, fresh[AUDIO_CUE_BATCH] = {};
    AudioPlayerLease prepared[AUDIO_CUE_BATCH] = {}, targets[AUDIO_CUE_BATCH] = {};
    bool new_voice[AUDIO_CUE_BATCH] = {}, new_binding[AUDIO_CUE_BATCH] = {};
    // reader decoding/copying stays on the main owner; the callback only swaps retained PCM pointers (D5.5.2).
    for (unsigned i = 0; i < count && !error; i++) if (clips[i]) {
        auto* source = sources[i];
        if (!source->player) {
            fresh[i] = radiant_audio_clone_ready(state->document, clips[i]->token, &source->parameters, source->description.values[AUDIO_GAIN]);
            source->player = fresh[i];
            if (!fresh[i]) { error = "audio cues: source reader admission failed"; break; }
        }
        candidates[i] = radiant_audio_clone_ready(state->document, clips[i]->token, &source->parameters, source->description.values[AUDIO_GAIN]);
        if (!candidates[i]) error = "audio cues: prepared reader quota exceeded";
    }
    if (!error && !audio_submix_prepare_output(state->submix, output, &error)) error = error ? error : "audio cues: output unavailable";
    if (!error && audio_submix_lock(state->submix)) {
        error = audio_cues_check_locked(state->cues, epoch, sequence, commands, count, audio_submix_clock(state->submix), &duplicate);
        for (unsigned i = 0; i < count && !error; i++) {
            auto* source = sources[i];
            if (!audio_cue_owner_bind_locked(state->cue_owner, source->player, false, true, source->route, targets + i, new_voice + i, new_binding + i) ||
                !audio_cues_attach_locked(state->cues, source->identity, targets[i]) ||
                (candidates[i] && !audio_cue_owner_bind_locked(state->cue_owner, candidates[i], true, false, 0, prepared + i)))
                error = "audio cues: retained voice or lease capacity exceeded";
        }
        if (!error) error = audio_cues_commit_locked(state->cues, epoch, sequence, commands, prepared, count, audio_submix_clock(state->submix));
        if (error) for (unsigned i = 0; i < count; i++) if (new_binding[i] || new_voice[i]) {
            audio_cue_owner_unbind_locked(state->cue_owner, targets[i].token, new_voice[i]);
        }
        audio_submix_unlock(state->submix);
    } else if (!error) error = "audio cues: retired submix";
    if (error) {
        for (unsigned i = 0; i < count; i++) {
            radiant_audio_close(state->document, candidates[i]);
            if (fresh[i]) { radiant_audio_close(state->document, fresh[i]); sources[i]->player = 0; }
        }
        return error;
    }
    scene3d_audio_cues_collect(state); return nullptr;
}
static Item audio_render_value(Scene3dAudio* state, uint64_t session, uint64_t id) {
    AudioRenderSnapshot job;
    if (!audio_render_snapshot(state->document, session, id, &job)) return ItemError;
    RootedValueBuilder result, pending(true), history(true);
    result.put("session", box_uint64_value(session)); result.put("job", box_uint64_value(id));
    result.put("frames", box_uint64_value(job.frames)); result.put("rendered", box_uint64_value(job.rendered));
    result.put("bytes", box_uint64_value(job.bytes)); result.put("state", audio_string(audio_render_state_name(job.state)));
    result.put("sample_rate", Item{.item=i2it(AUDIO_BUS_RATE)}); result.put("channels", Item{.item=i2it(2)});
    result.put("diagnostic", audio_string(job.diagnostic));
    audio_cue_rows(&pending, job.cues.pending, job.cues.pending_count); audio_cue_rows(&history, job.cues.history, job.cues.history_count);
    result.put("pending", pending.value.get()); result.put("history", history.value.get()); return result.value.get();
}
static bool audio_render_count(ItemReader value, uint64_t fallback, uint64_t limit, uint64_t* result) {
    if (value.isNull()) { *result = fallback; return fallback <= limit; }
    if (scene3d_counter(value, result)) return *result <= limit;
    int64_t number;
    // bounded work/range counts admit exact JS Number integers; opaque identities retain scene3d_counter.
    if (!lambda_item_to_int64_exact(value.item(), &number) || number < 0 || (uint64_t)number > limit) return false;
    *result = (uint64_t)number; return true;
}
static const char* audio_render_providers(ItemReader input, AudioRenderPlan* plan) {
    if (input.isNull()) return nullptr;
    if (!input.isArray() || input.asArray().length() > AUDIO_CUE_TRACKS) return "audio render: invalid bounded provider array";
    plan->provider_count = (unsigned)input.asArray().length();
    for (unsigned i = 0; i < plan->provider_count; i++) {
        auto entry = input.asArray().get(i); if (!entry.isMap()) return "audio render: provider must be a map";
        auto fields = entry.asMap(); auto& provider = plan->providers[i];
        provider.provider = radiant_audio_provider(scene3d_command_name(fields.get("provider")));
        const char* bus = fields.get("bus").isNull() ? "master" : scene3d_command_name(fields.get("bus"));
        if (!provider.provider || !scene3d_counter(fields.get("token"), &provider.token) || !provider.token ||
            !physics_scene_real(fields.get("position"), 0, &provider.position) || provider.position < 0 ||
            !bus || !*bus || strlen(bus) >= sizeof(provider.bus)) return "audio render: invalid provider, exact token, position or bus";
        str_copy(provider.bus, sizeof(provider.bus), bus, strlen(bus));
    }
    return nullptr;
}
static Item audio_render_operate(Scene3dAudio* state, uint64_t session, const char* operation, MapReader fields) {
    auto fail = [](const char* message) { return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr)); };
    const char* error = nullptr; uint64_t id;
    if (!strcmp(operation, "audio_render_open")) {
        AudioRenderPlan plan = {}; AudioSource* sources[AUDIO_CUE_CAPACITY] = {};
        if (!audio_render_count(fields.get("frames"), 0, AUDIO_RENDER_BYTES / 8, &plan.frames) ||
            !physics_scene_real(fields.get("scene_time"), 0, &plan.scene_time) ||
            !physics_scene_real(fields.get("time_scale"), 1, &plan.time_scale)) return fail("audio render: invalid frame or virtual-time field");
        if (const char* rejected = audio_render_providers(fields.get("providers"), &plan)) return fail(rejected);
        auto events = fields.get("events");
        if (!events.isNull() && (!events.isArray() || events.asArray().length()))
            if (const char* rejected = audio_copy_commands(state, events, AUDIO_CUE_CAPACITY, plan.events, sources, &plan.event_count)) return fail(rejected);
        auto* node = dom_node_ref_validate(state->document, state->root);
        if (!node || !node->is_element() || !dom_element_is_connected(node->as_element())) return fail("audio render: detached scene");
        auto* root = node->as_element();
        AudioGather gather{root, {}, nullptr, nullptr, nullptr, 0, scene3d_text(root, "audio_listener"), scene3d_text(root, "camera")};
        if ((scene3d_present(root, "audio_listener") && !gather.listener_id) || !scene3d_visit(root, scene3d_audio_gather, &gather))
            return fail("audio render: invalid authored audio hierarchy");
        AudioListener listener = {}; float speed;
        if (const char* rejected = scene3d_audio_listener_description(state, gather, &listener)) return fail(rejected);
        if (!scene3d_number(root, "audio_speed_of_sound", 343, &speed) || speed < .001f || speed > AUDIO_COORDINATE_LIMIT ||
            !scene3d_audio_listener_frame(state, root, &listener, scene3d_runtime_sampled_time(state->runtime))) return fail("audio render: invalid listener sample");
        if (const char* rejected = scene3d_audio_bus_config(gather, &plan.graph)) return fail(rejected);
        for (unsigned i = 0; i < plan.event_count; i++) {
            auto* source = sources[i]; const auto& cue = plan.events[i];
            if (!source || source->identity != cue.identity) return fail("audio render: stale source identity");
            AudioSource copy = *source; auto* candidate = dom_node_ref_validate(state->document, source->node); bool present = false;
            for (unsigned j = 0; j < gather.count; j++) present |= candidate == gather.sources[j];
            if (!present || !scene3d_audio_description(candidate->as_element(), &copy.description) || strcmp(copy.description.id, source->description.id))
                return fail("audio render: detached, renamed or invalid source");
            auto* clip = audio_clip(state, copy.description.clip);
            if (!clip || (cue.op == AUDIO_CUE_PLAY && clip->identity != cue.clip)) return fail("audio render: missing or stale clip identity");
            bool copied = false;
            for (unsigned j = 0; j < plan.source_count; j++) copied |= plan.sources[j].identity == source->identity;
            if (!copied) {
                if (plan.source_count == AUDIO_CUE_TRACKS) return fail("audio render: source limit exceeded");
                // export samples authored controls into a private plan; it never publishes them to live players.
                if (!scene3d_audio_source_frame(state, root, &copy, listener, speed, scene3d_runtime_sampled_time(state->runtime)))
                    return fail("audio render: invalid sampled source");
                auto& target = plan.sources[plan.source_count++]; target.identity = source->identity; target.asset = clip->token;
                target.parameters = copy.parameters; target.gain = copy.description.values[AUDIO_GAIN];
                if (copy.description.pitch_follow) target.parameters.pitch = (float)fmax(.125, fmin(8, copy.unscaled_pitch * plan.time_scale));
                const char* bus = *copy.description.bus ? copy.description.bus : "master";
                str_copy(target.bus, sizeof(target.bus), bus, strlen(bus));
            }
        }
        if (!audio_render_open(state->document, session, plan, &id, &error)) return fail(error ? error : "audio render: native admission failed");
        return audio_render_value(state, session, id);
    }
    if (!scene3d_counter(fields.get("job"), &id) || !id) return fail("audio render: missing job identity");
    if (!strcmp(operation, "audio_render_close")) {
        if (!audio_render_close(state->document, session, id)) return fail("audio render: missing job");
        return Item{.item=b2it(true)};
    }
    if (!strcmp(operation, "audio_render_cancel")) {
        if (!audio_render_cancel(state->document, session, id)) return fail("audio render: missing job");
    } else if (!strcmp(operation, "audio_render_step")) {
        uint64_t frames = AUDIO_RENDER_STEP;
        if (!audio_render_count(fields.get("frames"), AUDIO_RENDER_STEP, AUDIO_RENDER_STEP, &frames) ||
            !audio_render_step(state->document, session, id, (unsigned)frames, &error)) return fail(error ? error : "audio render: invalid step budget");
    } else if (!strcmp(operation, "audio_render_read")) {
        uint64_t offset = 0, frames = AUDIO_RENDER_STEP;
        const char* format = fields.get("format").isNull() ? "f32" : scene3d_command_name(fields.get("format"));
        if (!format || (strcmp(format, "f32") && strcmp(format, "wav_header")) ||
            !audio_render_count(fields.get("offset"), 0, AUDIO_RENDER_BYTES / 8, &offset) ||
            !audio_render_count(fields.get("frames"), AUDIO_RENDER_STEP, AUDIO_RENDER_STEP, &frames))
            return fail("audio render: invalid result copy request");
        unsigned char bytes[AUDIO_RENDER_STEP * 8]; size_t length = 0;
        if (!audio_render_read(state->document, session, id, offset, (unsigned)frames, !strcmp(format, "wav_header"), bytes, sizeof(bytes), &length, &error))
            return fail(error ? error : "audio render: result unavailable");
        RootedValueBuilder result;
        result.put("job", box_uint64_value(id)); result.put("offset", box_uint64_value(offset));
        result.put("frames", box_uint64_value(!strcmp(format, "wav_header") ? 0 : frames));
        result.put("data", Item{.item=x2it(heap_binary_from_bytes((const char*)bytes, length))}); return result.value.get();
    } else if (strcmp(operation, "audio_render_snapshot")) return fail("audio render: unknown operation");
    Item result = audio_render_value(state, session, id); return item_is_error(result) ? fail("audio render: missing job") : result;
}
static Item audio_provider_operate(Scene3dAudio* state, uint64_t session, const char* operation, MapReader fields) {
    auto fail = [](const char* message) { return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr)); };
    const auto* provider = radiant_audio_provider(scene3d_command_name(fields.get("provider")));
    uint64_t token;
    if (!provider || !scene3d_counter(fields.get("token"), &token) || !token) return fail("audio provider: missing kind or exact provider token");
    if (!strcmp(operation, "audio_provider_route")) {
        uint64_t expected, identity = 0; AudioRouteId route = 0;
        if (!scene3d_counter(fields.get("expected"), &expected) ||
            (!fields.get("identity").isNull() && !scene3d_counter(fields.get("identity"), &identity))) return fail("audio provider: invalid route ticket");
        if (!fields.get("bus").isNull()) {
            const char* bus = scene3d_command_name(fields.get("bus"));
            route = bus && state->graph ? audio_submix_graph_route(state->submix, state->graph, nullptr, bus) : 0;
            if (!route || identity != route) return fail("audio provider: missing or stale named bus identity");
        } else if (identity) return fail("audio provider: default routing requires a zero bus identity");
        const char* error = nullptr;
        if (!provider->route(state->document, token, expected, route, &error)) return fail(error ? error : "audio provider: route publication rejected");
    } else if (strcmp(operation, "audio_provider_read")) return fail("audio provider: unknown operation");
    AudioProviderInfo snapshot;
    if (!provider->read(state->document, token, &snapshot)) return fail("audio provider: missing provider in the requesting document");
    RootedValueBuilder result;
    result.put("session", box_uint64_value(session)); result.put("provider", audio_string(provider->name)); result.put("token", box_uint64_value(token));
    result.put("route", box_uint64_value(snapshot.route)); result.put("route_valid", Item{.item=b2it(snapshot.route_valid)});
    result.put("state", audio_string(snapshot.state)); result.put("position", push_d(snapshot.position)); result.put("gain", push_d(snapshot.gain));
    result.put("loss", audio_string(snapshot.loss == AUDIO_VOICE_ROUTE_LOST ? "route_lost" : snapshot.loss == AUDIO_VOICE_DEVICE_LOST ? "device_lost" : "none"));
    return result.value.get();
}
Item scene3d_audio_operate(Scene3dAudio* state, Item request) {
    RootFrame roots(1); Rooted<Item> rooted(roots, request);
    auto fail = [](const char* message) { return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr)); };
    if (!state) return fail("audio: missing native scene owner");
    auto read = ItemReader(rooted.get().to_const()).asMap();
    const char* operation = scene3d_command_name(read.get("operation"));
    if (operation && !strcmp(operation, "audio_read")) return audio_snapshot(state);
    auto payload = read.get("payload"); auto fields = payload.asMap(); uint64_t session;
    Scene3dRuntimeStats clock = {}; scene3d_runtime_stats(state->runtime, &clock);
    if (!operation || !payload.isMap() || !scene3d_counter(fields.get("session"), &session) || session != clock.session)
        return fail("audio: missing or stale scene session");
    if (!strncmp(operation, "audio_provider_", 15)) return audio_provider_operate(state, session, operation, fields);
    if (!strncmp(operation, "audio_render_", 13)) return audio_render_operate(state, session, operation, fields);
    if (!strcmp(operation, "audio_schedule")) {
        if (const char* error = audio_schedule(state, fields)) return fail(error);
        return audio_snapshot(state);
    }
    if (!strcmp(operation, "audio_cues_cancel")) {
        uint64_t epoch, event = 0;
        if (!scene3d_counter(fields.get("epoch"), &epoch) || epoch != state->cue_epoch || !epoch ||
            (!fields.get("event").isNull() && !scene3d_counter(fields.get("event"), &event))) return fail("audio cues: stale cancel epoch or event");
        if (state->cues) {
            if (!audio_submix_lock(state->submix)) return fail("audio cues: retired submix");
            bool found = audio_cues_cancel_locked(state->cues, event, audio_submix_clock(state->submix)); audio_submix_unlock(state->submix);
            if (!found) return fail("audio cues: no pending event with that identity");
            scene3d_audio_cues_collect(state);
        } else if (event) return fail("audio cues: no pending event with that identity");
        return audio_snapshot(state);
    }
    if (!strcmp(operation, "audio_clip") || !strcmp(operation, "audio_clip_drop")) {
        const char* id = scene3d_command_name(fields.get("id"));
        if (!id || !*id || strlen(id) >= 128) return fail("audio clip: requires a bounded nonempty ID");
        auto* clip = audio_clip(state, id); uint64_t expected = 0;
        auto counter = fields.get(!strcmp(operation, "audio_clip") ? "expected" : "identity");
        if ((!counter.isNull() && !scene3d_counter(counter, &expected)) || expected != (clip ? clip->identity : 0))
            return fail("audio clip: stale asset identity");
        if (!strcmp(operation, "audio_clip_drop")) {
            if (!clip || state->generation == UINT64_MAX) return fail("audio clip: missing asset or exhausted generation");
            radiant_audio_close(state->document, clip->token);
            unsigned index = (unsigned)(clip - state->clips);
            memmove(clip, clip + 1, (--state->clip_count - index) * sizeof(*clip));
            state->generation++; return audio_snapshot(state);
        }
        Item bytes = fields.get("bytes").item();
        if ((!clip && state->clip_count == AUDIO_SCENE_CLIPS) || state->identity == UINT64_MAX || state->generation == UINT64_MAX ||
            get_type_id(bytes) != LMD_TYPE_BINARY) return fail("audio clip: requires encoded bytes within native clip limits");
        auto* root = dom_node_ref_validate(state->document, state->root);
        if (!root || !root->is_element() || !dom_element_is_connected(root->as_element())) return fail("audio clip: detached scene");
        // decode and quota admission precede replacement; existing cursors retain their old immutable PCM (D5.5.2).
        uint64_t token = radiant_audio_open(state->document, binary_data(bytes.get_binary()), bytes.get_binary()->len);
        if (!token) return fail("audio clip: decode or document admission failed");
        RdtAudioInfo info = {}; radiant_audio_info(state->document, token, &info);
        uint64_t old = clip ? clip->token : 0;
        if (!clip) clip = state->clips + state->clip_count++;
        *clip = {}; str_copy(clip->id, sizeof(clip->id), id, strlen(id));
        clip->identity = ++state->identity; clip->token = token; clip->frames = (size_t)llround(info.duration * AUDIO_BUS_RATE);
        radiant_audio_identity(state->document, token, clip->identity);
        state->generation++; radiant_audio_close(state->document, old); return audio_snapshot(state);
    }
    if (strcmp(operation, "audio_command")) return fail("audio: unknown operation");
    const char* id = scene3d_command_name(fields.get("source")), *action = scene3d_command_name(fields.get("action"));
    uint64_t identity; AudioSource* source = nullptr;
    if (id) for (unsigned i = 0; i < state->count; i++) if (!strcmp(state->sources[i].description.id, id)) source = state->sources + i;
    if (!source || !action || !scene3d_counter(fields.get("identity"), &identity) || identity != source->identity)
        return fail("audio command: missing or stale source identity");
    if (!strcmp(action, "stop")) {
        radiant_audio_close(state->document, source->player); source->player = 0;
    } else if (!strcmp(action, "pause")) {
        if (!source->player || !radiant_audio_pause(state->document, source->player)) return fail("audio command: no live source to pause");
    } else if (!strcmp(action, "teleport")) {
        source->motion.reset = true;
        const char* error = scene3d_audio_sample(state, scene3d_runtime_sampled_time(state->runtime));
        if (error) return fail(error);
    } else if (!strcmp(action, "play") || !strcmp(action, "resume")) {
        auto* root = dom_node_ref_validate(state->document, state->root);
        if (!root || !root->is_element() || !dom_element_is_connected(root->as_element())) return fail("audio command: detached scene");
        float gain = source->description.values[AUDIO_GAIN];
        if (!strcmp(action, "resume")) {
            RdtAudioInfo info = {};
            if (!source->player || !radiant_audio_info(state->document, source->player, &info) || info.state != RDT_AUDIO_PAUSED ||
                !radiant_audio_play(state->document, source->player, gain)) return fail("audio command: no paused source or document activation");
        } else {
            auto* clip = audio_clip(state, source->description.clip);
            uint64_t player = clip ? radiant_audio_source_start(state->document, clip->token, source->player, &source->parameters, gain, source->route) : 0;
            if (!player) return fail("audio command: missing clip, activation or native voice capacity");
            source->player = player;
        }
    } else return fail("audio command: unknown transport action");
    return audio_snapshot(state);
}
