#include "scene3d_animation_adapter.hpp"
#include "scene3d_physics_adapter.hpp"
#include "../../../radiant/scene3d/scene3d_animation_internal.hpp"
#include "../../../radiant/scene3d/scene3d.hpp"
#include "../../../radiant/scene3d/scene3d_physics.hpp"
#include "../../../radiant/scene3d/scene3d_source.hpp"
#include "../../../radiant/event.hpp"
#include "../../../radiant/radiant.hpp"
#include "../../core/mark_reader.hpp"
#include "../../runtime/lambda-number-runtime.hpp"
#include "../../runtime/lambda-error.h"
#include "../../runtime/value_builder.hpp"
#include "../../../lib/mem.h"
#include <math.h>
#include <string.h>

static bool scene3d_animation_real(ItemReader value, double fallback, double* result) {
    if (value.isNull()) {
        *result = fallback;
        return isfinite(*result);
    }
    return item_try_to_double(value.item(), result) && isfinite(*result);
}
static bool scene3d_command_clip(Scene3dAnimationState* state, ItemReader value, ItemReader identity,
                                 uint64_t* result) {
    auto* clip = scene3d_animation_clip(state, scene3d_command_name(value));
    uint64_t selected;
    if (!clip || clip->controlled || !scene3d_counter(identity, &selected) || selected != clip->action->identity) {
        return false;
    }
    *result = selected;
    return true;
}
Item scene3d_animation_commands(Scene3dAnimationState* state, Item batch) {
    RootFrame roots(1);
    Rooted<Item> source(roots, batch);
    auto fail = [](const char* message) {
        return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr));
    };
    if (!state || !ItemReader(source.get().to_const()).isMap()) {
        return fail("animation commands: requires a live scene and batch map");
    }
    struct Owned {
        Scene3dAnimationCommands* value = nullptr;
        ~Owned() {
            mem_free(value);
        }
    } owned;
    owned.value = (Scene3dAnimationCommands*)mem_calloc(1, sizeof(Scene3dAnimationCommands), MEM_CAT_RENDER);
    if (!owned.value) {
        return fail("animation commands: payload allocation failed");
    }
    auto input = ItemReader(source.get().to_const()).asMap();
    Scene3dCommandBatch ticket = {};
    if (!scene3d_counter(input.get("session"), &ticket.session) ||
        !scene3d_counter(input.get("generation"), &ticket.generation) ||
        !scene3d_counter(input.get("service_generation"), &ticket.service_generation) ||
        !scene3d_counter(input.get("sequence"), &ticket.sequence) ||
        !scene3d_counter(input.get("tick"), &ticket.tick) ||
        !scene3d_counter(input.get("revision"), &owned.value->revision) ||
        !scene3d_counter(input.get("world_identity"), &owned.value->world_identity) ||
        owned.value->revision != state->revision || owned.value->world_identity != scene3d_animation_world(state)) {
        return fail("animation commands: missing, lossy or stale batch identities");
    }
    auto values = input.get("commands");
    if (!values.isArray() || values.asArray().length() < 1 || values.asArray().length() > ANIMATION_COMMAND_LIMIT) {
        return fail("animation commands: requires 1..256 commands");
    }
    owned.value->count = (unsigned)values.asArray().length();
    const char* names[] = {"play",  "stop", "reset", "weight", "rate",     "pause",
                           "phase", "mask", "fade",  "warp",   "crossfade"};
    for (unsigned i = 0; i < owned.value->count; i++) {
        auto item = values.asArray().get(i);
        auto fields = item.asMap();
        auto& command = owned.value->values[i];
        const char* op = scene3d_command_name(fields.get("op"));
        unsigned kind = 0;
        while (kind < sizeof(names) / sizeof(*names) && (!op || strcmp(op, names[kind]))) {
            kind++;
        }
        if (!item.isMap() || kind == sizeof(names) / sizeof(*names) ||
            !scene3d_command_clip(state, fields.get("clip"), fields.get("identity"), &command.action)) {
            return fail("animation commands: unknown operation, stale clip or compatibility-controller writer");
        }
        command.kind = (AnimationCommandKind)kind;
        auto real = [&](const char* key, double fallback, double* result) {
            return scene3d_animation_real(fields.get(key), fallback, result);
        };
        auto flag = [&](const char* key, bool fallback, bool* result) {
            auto value = fields.get(key);
            if (!value.isNull() && !value.isBool()) {
                return false;
            }
            *result = value.isNull() ? fallback : value.asBool();
            return true;
        };
        if (!flag("reset", false, &command.reset) || !flag("paused", true, &command.paused) ||
            !flag("fade_in", true, &command.fade_in) || !flag("warp", false, &command.warp)) {
            return fail("animation commands: flags must be bool values");
        }
        if ((command.kind == ANIMATION_COMMAND_WEIGHT || command.kind == ANIMATION_COMMAND_RATE ||
             command.kind == ANIMATION_COMMAND_PHASE || command.kind == ANIMATION_COMMAND_MASK) &&
            !real("value", NAN, &command.value)) {
            return fail("animation commands: requires a finite value");
        }
        if (command.kind == ANIMATION_COMMAND_WEIGHT && command.value < 0) {
            return fail("animation commands: weight must be nonnegative");
        }
        if (command.kind == ANIMATION_COMMAND_PHASE) {
            auto* action = animation_mixer_find_action(state->mixer, command.action);
            if (command.value < 0 || command.value > action->clip.duration) {
                return fail("animation commands: phase is outside the clip duration");
            }
        }
        if (command.kind == ANIMATION_COMMAND_MASK) {
            uint64_t channel = 0;
            int64_t index;
            auto* action = animation_mixer_find_action(state->mixer, command.action);
            auto value = fields.get("channel");
            bool exact = scene3d_counter(value, &channel);
            // bounded indices use the canonical integer reader for exact JS numeric literals.
            if (!exact && lambda_item_to_int64_exact(value.item(), &index) && index >= 0) {
                channel = (uint64_t)index;
                exact = true;
            }
            if (!exact || channel >= action->clip.count || command.value < 0 || command.value > 1) {
                return fail("animation commands: invalid channel index or mask weight");
            }
            command.channel = (unsigned)channel;
        }
        if ((command.kind == ANIMATION_COMMAND_FADE || command.kind == ANIMATION_COMMAND_WARP ||
             command.kind == ANIMATION_COMMAND_CROSSFADE) &&
            (!real("duration", NAN, &command.duration) || command.duration < 0)) {
            return fail("animation commands: duration must be finite and nonnegative");
        }
        if (command.kind == ANIMATION_COMMAND_WARP &&
            (!real("from", NAN, &command.value) || !real("to", NAN, &command.second))) {
            return fail("animation commands: warp requires finite endpoints");
        }
        if (command.kind == ANIMATION_COMMAND_CROSSFADE &&
            (!scene3d_command_clip(state, fields.get("other"), fields.get("other_identity"), &command.other) ||
             command.other == command.action)) {
            return fail("animation commands: crossfade requires another live independent clip");
        }
    }
    ticket.phase = SCENE3D_TICK_ANIMATION;
    ticket.payload = owned.value;
    ticket.release = [](void* payload) { mem_free(payload); };
    const char* error = nullptr;
    if (!scene3d_runtime_submit(state->runtime, ticket, &error)) {
        return fail(error);
    }
    owned.value = nullptr;
    return scene3d_runtime_snapshot(state->runtime);
}

Item scene3d_animation_replay_events(Scene3dAnimationState* state, Scene3dPhysics* physics, Item request) {
    RootFrame roots(1);
    Rooted<Item> source(roots, request);
    auto fail = [](const char* message) {
        return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr));
    };
    if (!state || !physics || !ItemReader(source.get().to_const()).isMap() || state->event_replays == UINT64_MAX) {
        return fail("animation cue replay: requires a live scene and available replay identity");
    }
    auto fields = ItemReader(source.get().to_const()).asMap();
    double from, to;
    size_t first, last;
    auto delivery = fields.get("deliver");
    if (!scene3d_animation_real(fields.get("from"), NAN, &from) ||
        !scene3d_animation_real(fields.get("to"), NAN, &to) ||
        !scene3d_runtime_tick_index(state->runtime, from, &first) ||
        !scene3d_runtime_tick_index(state->runtime, to, &last) || first > last ||
        (!delivery.isNull() && !delivery.isBool())) {
        return fail(
            "animation cue replay: requires ordered fixed-tick boundaries within 100000 ticks and bool delivery");
    }
    bool deliver = delivery.isNull() || delivery.asBool();
    Scene3dAnimationEvents events;
    if (!scene3d_physics_replay_events(physics, first, last, &events)) {
        return fail(*events.diagnostic ? events.diagnostic : "animation cue replay: authored reconstruction failed");
    }
    for (auto& event : events.values) {
        auto* action = scene3d_animation_action(state, event.clip);
        if (!action) {
            return fail("animation cue replay: reconstructed clip has no admitted source identity");
        }
        event.timing.action = action->identity;
    }
    uint64_t sequence = state->event_replays + 1;
    Scene3dRuntimeStats clock = {};
    scene3d_runtime_stats(state->runtime, &clock);
    RootedValueBuilder result, records(true);
    result.put("session", box_uint64_value(clock.session));
    result.put("generation", box_uint64_value(clock.generation));
    result.put("revision", box_uint64_value(state->revision));
    result.put("command_revision", box_uint64_value(state->command_revision));
    result.put("world_identity", box_uint64_value(scene3d_animation_world(state)));
    result.put("replay_sequence", box_uint64_value(sequence));
    result.put("from_tick", box_uint64_value(first));
    result.put("to_tick", box_uint64_value(last));
    result.put("from", push_d(first * clock.timestep));
    result.put("to", push_d(last * clock.timestep));
    result.put("authored", Item{.item = b2it(true)});
    result.put("delivery_requested", Item{.item = b2it(deliver)});
    result.put("delivery_queued", Item{.item = b2it(deliver && !events.values.empty())});
    result.put("count", box_uint64_value(events.values.size()));
    for (const auto& event : events.values) {
        RootedValueBuilder record;
        record.put("type", Item{.item = s2it(heap_create_name(event.timing.type))});
        record.put("clip", Item{.item = s2it(heap_create_name(event.clip))});
        record.put("marker", Item{.item = s2it(heap_create_name(event.timing.marker))});
        record.put("action", box_uint64_value(event.timing.action));
        record.put("sequence", box_uint64_value(event.timing.sequence));
        const char* names[] = {"time", "clip_time", "iteration", "direction", "weight", "loop_delta"};
        double values[] = {event.timing.time,      event.timing.clip_time,
                           event.timing.iteration, event.timing.direction,
                           event.timing.weight,    !strcmp(event.timing.type, "loop") ? event.timing.detail : 0};
        for (unsigned i = 0; i < sizeof(names) / sizeof(*names); i++) {
            record.put(names[i], push_d(values[i]));
        }
        records.append(record.value.get());
    }
    result.put("events", records.value.get());
    if (item_is_error(result.value.get())) {
        return result.value.get();
    }
    if (deliver && !events.values.empty()) {
        struct Delivery {
            Scene3dAnimationEvents* events;
            unsigned index;
            uint64_t revision, sequence, session, generation;
        } data = {&events, 0, state->revision, sequence, clock.session, clock.generation};
        auto build = [](void* owner) {
            auto* data = (Delivery*)owner;
            const auto& event = data->events->values[data->index++];
            return radiant_scene_animation_event_value(event.clip, data->revision, event.timing, data->sequence,
                                                       data->session, data->generation);
        };
        DomNode* root = dom_node_ref_validate(state->document, state->root);
        if (!root || !root->is_element() ||
            !radiant_queue_timing_events((UiContext*)state->document->js.host_ui_context, root->as_element(), build,
                                         &data, (unsigned)events.values.size())) {
            return fail("animation cue replay: native delivery batch could not be admitted");
        }
    }
    state->event_replays = sequence;
    return result.value.get();
}

Item scene3d_animation_snapshot(Scene3dAnimationState* state) {
    if (!state) {
        return ItemError;
    }
    RootedValueBuilder result, clips(true);
    Scene3dRuntimeStats clock = {};
    scene3d_runtime_stats(state->runtime, &clock);
    result.put("session", box_uint64_value(clock.session));
    result.put("generation", box_uint64_value(clock.generation));
    result.put("service_generation", box_uint64_value(clock.service_generations[SCENE3D_TICK_ANIMATION]));
    result.put("world_identity", box_uint64_value(scene3d_animation_world(state)));
    result.put("tick", box_uint64_value(clock.ticks));
    result.put("next_command_tick", box_uint64_value(clock.next_command_tick));
    result.put("revision", box_uint64_value(state->revision));
    result.put("command_revision", box_uint64_value(state->command_revision));
    result.put("event_replays", box_uint64_value(state->event_replays));
    result.put("command_sequence", box_uint64_value(clock.command_sequence));
    result.put("time", push_d(animation_mixer_time(state->mixer)));
    result.put("automatic", Item{.item = b2it(state->automatic)});
    const char* loops[] = {"once", "repeat", "pingpong"};
    for (auto* clip = state->clips; clip; clip = clip->next) {
        AnimationActionSnapshot snapshot = {};
        if (!animation_action_snapshot(clip->action, &snapshot)) {
            return ItemError;
        }
        RootedValueBuilder value;
        value.put("clip", Item{.item = s2it(heap_create_name(clip->name))});
        value.put("identity", box_uint64_value(snapshot.identity));
        value.put("duration", push_d(snapshot.duration));
        value.put("time", push_d(snapshot.time));
        value.put("weight", push_d(snapshot.weight));
        value.put("effective_weight", push_d(snapshot.effective_weight));
        value.put("time_scale", push_d(snapshot.time_scale));
        value.put("effective_time_scale", push_d(snapshot.effective_time_scale));
        value.put("loop", Item{.item = s2it(heap_create_name(loops[snapshot.loop]))});
        value.put("loop_count", push_d(snapshot.loop_count));
        value.put("active", Item{.item = b2it(snapshot.active)});
        value.put("enabled", Item{.item = b2it(snapshot.enabled)});
        value.put("paused", Item{.item = b2it(snapshot.paused)});
        value.put("scheduled", Item{.item = b2it(snapshot.scheduled)});
        value.put("scheduled_start", push_d(snapshot.scheduled_start));
        value.put("fade_active", Item{.item = b2it(snapshot.fade.active)});
        value.put("warp_active", Item{.item = b2it(snapshot.warp.active)});
        clips.append(value.value.get());
    }
    result.put("clips", clips.value.get());
    RootedValueBuilder motion(true);
    for (auto* binding = state->bindings; binding; binding = binding->next) {
        if (!binding->root_motion) {
            continue;
        }
        AnimationRootMotion sampled = {};
        if (!animation_mixer_root_motion(state->mixer, binding->identity, &sampled)) {
            return ItemError;
        }
        RootedValueBuilder value, delta(true), total(true);
        DomNode* node = dom_node_ref_validate(state->document, binding->node);
        const char* name = node && node->is_element() ? scene3d_text(node->as_element(), "id") : nullptr;
        value.put("node", Item{.item = s2it(heap_create_name(name ? name : ""))});
        value.put("property", Item{.item = s2it(heap_create_name(binding->property))});
        value.put("binding", box_uint64_value(binding->identity));
        value.put("epoch", box_uint64_value(sampled.epoch));
        value.put("sequence", box_uint64_value(sampled.sequence));
        for (unsigned c = 0; c < sampled.count; c++) {
            delta.append(push_d(sampled.delta[c]));
            total.append(push_d(sampled.total[c]));
        }
        value.put("delta", delta.value.get());
        value.put("total", total.value.get());
        motion.append(value.value.get());
    }
    result.put("root_motion", motion.value.get());
    DomNode* root = dom_node_ref_validate(state->document, state->root);
    RootFrame roots(1);
    Rooted<Item> feedback(roots, scene3d_physics_root_snapshot(
                                     root && root->is_element() ? scene3d_physics(root->as_element()) : nullptr));
    result.put("root_controllers", feedback.get());
    return result.value.get();
}
