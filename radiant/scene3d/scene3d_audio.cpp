#include "scene3d_audio.hpp"
#include "scene3d_source.hpp"
#include "../audio.hpp"
#include "../audio_cues.hpp"
#include "../audio_cue_owner.hpp"
#include "../audio_render.hpp"
#include "../speech.hpp"
#include "../event.hpp"
#include "../../lambda/input/css/dom_element.hpp"
#include "../../lambda/core/mark_reader.hpp"
#include "../../lambda/core/physics_scene.hpp"
#include "../../lambda/runtime/value_builder.hpp"
#include "../../lambda/runtime/lambda-error.h"
#include "../../lambda/runtime/lambda-number-runtime.hpp"
#include "../../lambda/runtime/lambda-navigation.hpp"
#include "../../lib/mem.h"
#include "../../lib/str.h"
#include <float.h>

static constexpr unsigned AUDIO_SCENE_SOURCES = 32, AUDIO_SCENE_CLIPS = 16;
static constexpr float AUDIO_COORDINATE_LIMIT = 1e6f;
enum AudioSpatial { AUDIO_STEREO, AUDIO_2D, AUDIO_3D };
enum AudioDistance { AUDIO_INVERSE, AUDIO_LINEAR, AUDIO_NO_DISTANCE };
enum AudioNumber { AUDIO_GAIN, AUDIO_PITCH, AUDIO_SMOOTH, AUDIO_LOWPASS, AUDIO_REFERENCE, AUDIO_MAXIMUM,
                   AUDIO_ROLLOFF, AUDIO_INNER, AUDIO_OUTER, AUDIO_CONE_GAIN, AUDIO_NUMBERS };
static constexpr struct { const char* name; float fallback, minimum, maximum; } audio_numbers[] = {
    {"gain", 1, 0, 1}, {"pitch", 1, .125f, 8}, {"smoothing", .01f, 0, 10},
    {"lowpass", AUDIO_BUS_RATE / 2.f, 20, AUDIO_BUS_RATE / 2.f},
    {"ref_distance", 1, .001f, 1e6f}, {"max_distance", 1000, .001f, 1e6f},
    {"rolloff", 1, 0, 8}, {"cone_inner", (float)M_PI, 0, (float)M_PI},
    {"cone_outer", (float)M_PI, 0, (float)M_PI}, {"cone_gain", 0, 0, 1}
};
static_assert(sizeof(audio_numbers) / sizeof(*audio_numbers) == AUDIO_NUMBERS, "audio numeric schema");
struct AudioMotion {
    Scene3dVec position, velocity;
    double time;
    bool valid, reset;
};
struct AudioDescription {
    char id[128], clip[128], bus[128];
    float values[AUDIO_NUMBERS];
    Scene3dVec velocity;
    AudioSpatial spatial;
    AudioDistance distance;
    bool loop, doppler, explicit_velocity, pitch_follow;
};
struct AudioSource {
    DomNodeRef node;
    AudioDescription description;
    AudioMotion motion;
    RdtAudioParameters parameters;
    uint64_t identity, player;
    AudioRouteId route;
    float unscaled_pitch;
};
struct AudioListener {
    DomNodeRef node;
    char id[128];
    AudioMotion motion;
    Scene3dVec velocity, right, forward;
    bool explicit_velocity, camera;
};
struct AudioClip {
    char id[128];
    uint64_t identity, token;
    size_t frames;
};
struct Scene3dAudio {
    DomDocument* document;
    DomNodeRef root;
    Scene3dRuntime* runtime;
    Scene3dAnimationState* animation;
    Scene3dPhysics* physics;
    AudioSource sources[AUDIO_SCENE_SOURCES];
    AudioClip clips[AUDIO_SCENE_CLIPS];
    AudioListener listener;
    unsigned count, clip_count;
    uint64_t epoch, generation, sample_sequence, identity;
    float speed;
    AudioGraphId graph;
    AudioSubmixId submix;
    uint64_t routing_generation;
    AudioCueQueue* cues;
    AudioCueOwner* cue_owner;
    uint64_t cue_epoch;
    char diagnostic[512];
};
static bool audio_fail(Scene3dAudio* state, const char* message, const char* id = nullptr) {
    if (id) snprintf(state->diagnostic, sizeof(state->diagnostic), "audio source '%s': %s", id, message);
    else str_copy(state->diagnostic, sizeof(state->diagnostic), message, strlen(message));
    return false;
}
static bool audio_vector_valid(Scene3dVec value) {
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z) && fabsf(value.x) <= AUDIO_COORDINATE_LIMIT &&
           fabsf(value.y) <= AUDIO_COORDINATE_LIMIT && fabsf(value.z) <= AUDIO_COORDINATE_LIMIT;
}
static bool audio_vector_equal(Scene3dVec a, Scene3dVec b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static bool audio_motion_equal(const AudioMotion& a, const AudioMotion& b) {
    return audio_vector_equal(a.position, b.position) && audio_vector_equal(a.velocity, b.velocity) &&
        a.time == b.time && a.valid == b.valid && a.reset == b.reset;
}
static bool audio_description_equal(const AudioDescription& a, const AudioDescription& b) {
    return !strcmp(a.id, b.id) && !strcmp(a.clip, b.clip) && !strcmp(a.bus, b.bus) && !memcmp(a.values, b.values, sizeof(a.values)) &&
        audio_vector_equal(a.velocity, b.velocity) && a.spatial == b.spatial && a.distance == b.distance &&
        a.loop == b.loop && a.doppler == b.doppler && a.explicit_velocity == b.explicit_velocity && a.pitch_follow == b.pitch_follow;
}
static bool audio_parameters_equal(const RdtAudioParameters& a, const RdtAudioParameters& b) {
    return a.left == b.left && a.right == b.right && a.pitch == b.pitch && a.smoothing == b.smoothing &&
        a.lowpass == b.lowpass && a.mono == b.mono && a.loop == b.loop;
}
static bool audio_text(DomElement* node, const char* key, const char* fallback, char* out, bool empty = false) {
    const char* value = scene3d_text(node, key);
    if (!value && scene3d_present(node, key)) return false;
    if (!value) value = fallback;
    if (!value || (!empty && !*value) || strlen(value) >= 128) return false;
    str_copy(out, 128, value, strlen(value)); return true;
}
static bool audio_space(DomNode* child) {
    return !child->is_element() && (!child->is_text() || !child->as_text()->native_string ||
        strspn(child->as_text()->native_string->chars, " \t\r\n") == child->as_text()->native_string->len);
}
static bool audio_leaf(DomElement* node) {
    for (DomNode* child = node->first_child; child; child = child->next_sibling)
        if (!audio_space(child)) return false;
    return true;
}
bool scene3d_audio_tag(DomElement* node) {
    return node && (scene3d_tag(node, "audio-source") || scene3d_tag(node, "audio-listener") || scene3d_tag(node, "audio-bus"));
}
static bool audio_description(DomElement* node, AudioDescription* description) {
    *description = {}; char spatial[128], distance[128];
    if (!audio_text(node, "id", nullptr, description->id) || !audio_text(node, "clip", nullptr, description->clip) ||
        !audio_text(node, "spatial", "3d", spatial) || !audio_text(node, "distance_model", "inverse", distance) ||
        !audio_text(node, "bus", "", description->bus, true) || !audio_leaf(node)) return false;
    if (!strcmp(spatial, "none")) description->spatial = AUDIO_STEREO;
    else if (!strcmp(spatial, "2d")) description->spatial = AUDIO_2D;
    else if (!strcmp(spatial, "3d")) description->spatial = AUDIO_3D;
    else return false;
    if (!strcmp(distance, "inverse")) description->distance = AUDIO_INVERSE;
    else if (!strcmp(distance, "linear")) description->distance = AUDIO_LINEAR;
    else if (!strcmp(distance, "none")) description->distance = AUDIO_NO_DISTANCE;
    else return false;
    for (unsigned i = 0; i < AUDIO_NUMBERS; i++) {
        const auto& spec = audio_numbers[i]; auto& value = description->values[i];
        if (!scene3d_number(node, spec.name, spec.fallback, &value) || value < spec.minimum || value > spec.maximum) return false;
    }
    if (description->values[AUDIO_MAXIMUM] <= description->values[AUDIO_REFERENCE] ||
        description->values[AUDIO_INNER] > description->values[AUDIO_OUTER] ||
        !scene3d_flag(node, "loop", false, &description->loop) || !scene3d_flag(node, "doppler", true, &description->doppler) ||
        !scene3d_flag(node, "pitch_follow", false, &description->pitch_follow) ||
        !scene3d_vector(node, "velocity", {}, &description->velocity) || !audio_vector_valid(description->velocity)) return false;
    description->explicit_velocity = scene3d_present(node, "velocity"); return true;
}
struct AudioGather {
    DomElement* root;
    DomElement* sources[AUDIO_SCENE_SOURCES];
    DomElement* explicit_listener = nullptr;
    DomElement* camera = nullptr;
    DomElement* selected = nullptr;
    unsigned count = 0;
    const char* listener_id;
    const char* camera_id;
    DomElement* buses[AUDIO_GRAPH_BUSES] = {};
    unsigned bus_count = 0;
};
static Scene3dVisit audio_gather(DomElement* node, void* context) {
    auto* gather = (AudioGather*)context;
    if (node != gather->root && scene3d_tag(node, "scene3d")) return SCENE3D_VISIT_SKIP;
    if (scene3d_tag(node, "audio-bus")) {
        if (gather->bus_count == AUDIO_GRAPH_BUSES) return SCENE3D_VISIT_ERROR;
        gather->buses[gather->bus_count++] = node; return SCENE3D_VISIT_SKIP;
    }
    bool listener = scene3d_tag(node, "audio-listener"), camera = scene3d_tag(node, "camera");
    if (listener) {
        if (gather->explicit_listener || !audio_leaf(node)) return SCENE3D_VISIT_ERROR;
        gather->explicit_listener = node;
    }
    if (camera || listener) {
        const char* id = scene3d_text(node, "id");
        if (gather->listener_id && id && !strcmp(gather->listener_id, id)) {
            if (gather->selected) return SCENE3D_VISIT_ERROR;
            gather->selected = node;
        }
        if (camera && ((gather->camera_id && id && !strcmp(gather->camera_id, id)) || (!gather->camera_id && !gather->camera)))
            gather->camera = node;
    }
    if (scene3d_tag(node, "audio-source")) {
        if (gather->count == AUDIO_SCENE_SOURCES) return SCENE3D_VISIT_ERROR;
        gather->sources[gather->count++] = node; return SCENE3D_VISIT_SKIP;
    }
    return listener ? SCENE3D_VISIT_SKIP : SCENE3D_VISIT_CHILDREN;
}
struct AudioIdCheck { AudioGather* gather; AudioSource* sources; AudioListener* listener; };
static Scene3dVisit audio_check_ids(DomElement* node, void* context) {
    auto& check = *(AudioIdCheck*)context;
    if (node != check.gather->root && scene3d_tag(node, "scene3d")) return SCENE3D_VISIT_SKIP;
    const char* id = scene3d_text(node, "id");
    if (id) {
        for (unsigned i = 0; i < check.gather->count; i++)
            if (node != check.gather->sources[i] && !strcmp(id, check.sources[i].description.id)) return SCENE3D_VISIT_ERROR;
        for (unsigned i = 0; i < check.gather->bus_count; i++) {
            auto* bus = check.gather->buses[i]; const char* bus_id = scene3d_text(bus, "id");
            if (bus_id && node != bus && !strcmp(id, bus_id)) return SCENE3D_VISIT_ERROR;
        }
        if (check.listener->node.address && *check.listener->id && node != check.listener->node.address && !strcmp(id, check.listener->id)) return SCENE3D_VISIT_ERROR;
    }
    return scene3d_audio_tag(node) ? SCENE3D_VISIT_SKIP : SCENE3D_VISIT_CHILDREN;
}
static constexpr struct { const char* key; size_t offset; } audio_effect_fields[] = {
    {"frequency", offsetof(AudioEffectConfig, frequency)}, {"q", offsetof(AudioEffectConfig, q)},
    {"gain_db", offsetof(AudioEffectConfig, gain_db)}, {"seconds", offsetof(AudioEffectConfig, seconds)},
    {"feedback", offsetof(AudioEffectConfig, feedback)}, {"wet", offsetof(AudioEffectConfig, wet)},
    {"threshold", offsetof(AudioEffectConfig, threshold)}, {"ratio", offsetof(AudioEffectConfig, ratio)},
    {"attack", offsetof(AudioEffectConfig, attack)}, {"release", offsetof(AudioEffectConfig, release)},
    {"makeup", offsetof(AudioEffectConfig, makeup)}, {"ceiling", offsetof(AudioEffectConfig, ceiling)}
};
static bool audio_bus_description(DomElement* node, AudioBusConfig* bus) {
    char id[128];
    if (!audio_text(node, "id", nullptr, id)) return false;
    *bus = audio_graph_bus_default(id);
    if (!audio_text(node, "parent", !strcmp(id, "master") ? "" : "master", bus->parent, true) ||
        !scene3d_number(node, "gain", 1, &bus->gain) || !scene3d_number(node, "smoothing", .01f, &bus->smoothing) ||
        !scene3d_flag(node, "mute", false, &bus->mute) || !scene3d_flag(node, "solo", false, &bus->solo)) return false;
    for (DomNode* child = node->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) { if (!audio_space(child)) return false; continue; }
        auto* element = child->as_element(); if (!audio_leaf(element)) return false;
        if (scene3d_tag(element, "audio-send")) {
            if (bus->send_count == AUDIO_GRAPH_SENDS) return false;
            auto& send = bus->sends[bus->send_count++];
            if (!audio_text(element, "bus", nullptr, send.bus) || !scene3d_number(element, "gain", 1, &send.gain)) return false;
        } else if (scene3d_tag(element, "audio-effect")) {
            if (bus->insert_count == AUDIO_GRAPH_INSERTS) return false;
            const char* name = scene3d_text(element, "type"); AudioEffectKind kind = AUDIO_EFFECT_KINDS;
            for (unsigned i = 0; name && i < AUDIO_EFFECT_KINDS; i++) if (!strcmp(name, audio_effect_name((AudioEffectKind)i))) kind = (AudioEffectKind)i;
            if (kind == AUDIO_EFFECT_KINDS) return false;
            auto& effect = bus->inserts[bus->insert_count++]; effect = audio_effect_default(kind);
            for (const auto& field : audio_effect_fields) {
                float* value = (float*)((char*)&effect + field.offset);
                if (!scene3d_number(element, field.key, *value, value)) return false;
            }
            if (!audio_text(element, "sidechain", "", effect.sidechain, true)) return false;
        } else return false;
    }
    return true;
}
struct AudioPreparedGraph {
    AudioGraph* value = nullptr;
    ~AudioPreparedGraph() { audio_graph_destroy(value); }
};
static const char* audio_bus_config(const AudioGather& gather, AudioGraphConfig* result) {
    *result = {}; auto& config = *result; config.count = 1; config.buses[0] = audio_graph_bus_default("master");
    bool master = false;
    for (unsigned i = 0; i < gather.bus_count; i++) {
        AudioBusConfig bus;
        if (!audio_bus_description(gather.buses[i], &bus)) return "audio graph: invalid bus/insert description";
        if (!strcmp(bus.id, "master")) {
            if (master) return "audio graph: duplicate master";
            master = true; config.buses[0] = bus;
        } else {
            if (config.count == AUDIO_GRAPH_BUSES) return "audio graph: master plus buses exceeds limit";
            config.buses[config.count++] = bus;
        }
    }
    return nullptr;
}
static bool audio_prepare_routing(Scene3dAudio* state, const AudioGather& gather, AudioSource* sources, AudioPreparedGraph* prepared) {
    if (gather.bus_count || state->graph) {
        AudioGraphConfig config;
        if (const char* error = audio_bus_config(gather, &config)) return audio_fail(state, error);
        if (!state->graph) {
            state->submix = radiant_document_submix(state->document);
            state->graph = audio_submix_graph_open(state->submix);
        }
        const char* error = nullptr;
        if (!state->graph || !audio_submix_graph_prepare(state->submix, state->graph, config, &prepared->value, &error))
            return audio_fail(state, error ? error : "audio graph: owner admission failed");
        if (prepared->value && state->routing_generation == UINT64_MAX) return audio_fail(state, "audio graph: generation exhausted");
        if (prepared->value && !radiant_audio_graph_may_edit(state->document, state->graph))
            return audio_fail(state, "audio graph: active input/tail change requires document activation");
    }
    for (unsigned i = 0; i < gather.count; i++) {
        auto& source = sources[i]; const char* bus = *source.description.bus ? source.description.bus : "master";
        auto before = source.route;
        source.route = state->graph ? audio_submix_graph_route(state->submix, state->graph, prepared->value, bus) : 0;
        if ((state->graph && !source.route) || (!state->graph && strcmp(bus, "master"))) return audio_fail(state, "audio graph: unknown source bus", source.description.id);
        RdtAudioInfo info = {};
        if (source.player && (prepared->value || source.route != before) &&
            radiant_audio_info(state->document, source.player, &info) && (info.state == RDT_AUDIO_PLAYING || info.armed) &&
            info.gain > 0 && !radiant_document_may_start_audio(state->document)) return audio_fail(state, "audio graph: active route change requires document activation");
    }
    return true;
}
static bool audio_motion(AudioMotion* motion, Scene3dVec position, Scene3dVec supplied, bool explicit_velocity, double time) {
    if (!audio_vector_valid(position) || !audio_vector_valid(supplied) || !isfinite(time)) return false;
    Scene3dVec velocity = explicit_velocity ? supplied : Scene3dVec{};
    if (!explicit_velocity && motion->valid && time > motion->time && !motion->reset) {
        double delta = time - motion->time;
        double values[] = {((double)position.x - motion->position.x) / delta,
                           ((double)position.y - motion->position.y) / delta,
                           ((double)position.z - motion->position.z) / delta};
        for (double value : values) if (!isfinite(value) || fabs(value) > AUDIO_COORDINATE_LIMIT) return false;
        velocity = {(float)values[0], (float)values[1], (float)values[2]};
    } else if (!explicit_velocity && motion->valid && time == motion->time &&
               audio_vector_equal(position, motion->position) && !motion->reset) velocity = motion->velocity;
    bool reset = motion->reset && motion->valid && time <= motion->time;
    *motion = {position, velocity, time, true, reset}; return true;
}
static bool audio_world(Scene3dAudio* state, DomElement* root, DomElement* node, Scene3dMatrix* world) {
    bool visible;
    return scene3d_world_pose(root, node, state->animation, scene3d_override_physics_pose, state->physics, world, &visible);
}
static bool audio_listener_frame(Scene3dAudio* state, DomElement* root, AudioListener* listener, double time) {
    if (!listener->node.address) return true;
    auto* node = dom_node_ref_validate(state->document, listener->node);
    if (!node || !node->is_element() || !node->parent || !node->parent->is_element()) return false;
    Scene3dMatrix world, parent, view; Scene3dVec target, up;
    if (!audio_world(state, root, node->as_element(), &world) || !audio_world(state, root, node->parent->as_element(), &parent) ||
        !scene3d_camera_targets(node->as_element(), parent, world, &target, &up)) return false;
    auto position = scene3d_point(world, {});
    if (!audio_motion(&listener->motion, position, listener->velocity, listener->explicit_velocity, time) ||
        !scene3d_camera(position, target, up, &view)) return false;
    listener->right = {view.v[0], view.v[4], view.v[8]};
    listener->forward = {-view.v[2], -view.v[6], -view.v[10]}; return true;
}
static bool audio_source_frame(Scene3dAudio* state, DomElement* root, AudioSource* source, const AudioListener& listener,
    float speed, double time) {
    auto* node = dom_node_ref_validate(state->document, source->node); Scene3dMatrix world;
    if (!node || !node->is_element() || !audio_world(state, root, node->as_element(), &world)) return false;
    const auto& d = source->description;
    if (!audio_motion(&source->motion, scene3d_point(world, {}), d.velocity, d.explicit_velocity, time)) return false;
    auto parameters = rdt_audio_default_parameters(); parameters.loop = d.loop;
    parameters.smoothing = d.values[AUDIO_SMOOTH]; parameters.lowpass = d.values[AUDIO_LOWPASS]; parameters.pitch = d.values[AUDIO_PITCH];
    if (d.spatial != AUDIO_STEREO) {
        if (!listener.node.address) return false;
        Scene3dVec delta = scene3d_sub(source->motion.position, listener.motion.position), right = listener.right;
        Scene3dVec source_velocity = source->motion.velocity, listener_velocity = listener.motion.velocity;
        if (d.spatial == AUDIO_2D) {
            delta.z = right.z = source_velocity.z = listener_velocity.z = 0;
            if (!scene3d_normalize(&right)) return false;
        }
        double distance = sqrt((double)delta.x * delta.x + (double)delta.y * delta.y + (double)delta.z * delta.z);
        Scene3dVec direction = distance > 0 ? scene3d_scale(delta, (float)(1 / distance)) : Scene3dVec{};
        double range = fmin(d.values[AUDIO_MAXIMUM], fmax(d.values[AUDIO_REFERENCE], distance));
        double gain = 1;
        if (d.distance == AUDIO_INVERSE) gain = d.values[AUDIO_REFERENCE] /
            (d.values[AUDIO_REFERENCE] + d.values[AUDIO_ROLLOFF] * (range - d.values[AUDIO_REFERENCE]));
        else if (d.distance == AUDIO_LINEAR) gain = fmax(0, 1 - d.values[AUDIO_ROLLOFF] *
            (range - d.values[AUDIO_REFERENCE]) / (d.values[AUDIO_MAXIMUM] - d.values[AUDIO_REFERENCE]));
        if (distance > 0 && d.values[AUDIO_INNER] < (float)M_PI && d.values[AUDIO_CONE_GAIN] < 1) {
            auto forward = scene3d_point(world, d.spatial == AUDIO_2D ? Scene3dVec{1, 0, 0} : Scene3dVec{0, 0, -1}, 0);
            if (d.spatial == AUDIO_2D) forward.z = 0;
            if (!scene3d_normalize(&forward)) return false;
            double angle = acos(fmax(-1, fmin(1, -scene3d_dot(forward, direction))));
            double blend = angle <= d.values[AUDIO_INNER] ? 0 : angle >= d.values[AUDIO_OUTER] ? 1 :
                (angle - d.values[AUDIO_INNER]) / (d.values[AUDIO_OUTER] - d.values[AUDIO_INNER]);
            gain *= 1 + (d.values[AUDIO_CONE_GAIN] - 1) * blend;
        }
        double pan = fmax(-1, fmin(1, scene3d_dot(direction, right))), angle = (pan + 1) * M_PI / 4;
        parameters.left = (float)(gain * cos(angle)); parameters.right = (float)(gain * sin(angle)); parameters.mono = true;
        if (d.doppler && distance > 0) {
            double source_speed = fmax(-.95 * speed, fmin(.95 * speed, scene3d_dot(source_velocity, direction)));
            double listener_speed = fmax(-.95 * speed, fmin(.95 * speed, scene3d_dot(listener_velocity, direction)));
            parameters.pitch = (float)fmax(.125, fmin(8, parameters.pitch * (speed + listener_speed) / (speed + source_speed)));
        }
    }
    source->unscaled_pitch = parameters.pitch;
    if (d.pitch_follow) {
        Scene3dRuntimeStats clock = {}; scene3d_runtime_stats(state->runtime, &clock);
        parameters.pitch = (float)fmax(.125, fmin(8, parameters.pitch * clock.time_scale));
    }
    source->parameters = parameters; return true;
}
static bool audio_publish(Scene3dAudio* state, AudioSource* sources, unsigned count, AudioListener* listener,
    float speed, double time) {
    auto* root = dom_node_ref_validate(state->document, state->root);
    if (!root || !root->is_element() || !audio_listener_frame(state, root->as_element(), listener, time))
        return audio_fail(state, "audio: invalid sampled listener frame or velocity");
    for (unsigned i = 0; i < count; i++) {
        if (!audio_source_frame(state, root->as_element(), sources + i, *listener, speed, time))
            return audio_fail(state, "invalid sampled frame, cone or velocity", sources[i].description.id);
        RdtAudioInfo info = {};
        if (sources[i].player && radiant_audio_info(state->document, sources[i].player, &info) &&
            (info.state == RDT_AUDIO_PLAYING || info.armed) && sources[i].description.values[AUDIO_GAIN] > 0 &&
            sources[i].description.values[AUDIO_GAIN] != info.gain && !radiant_document_may_start_audio(state->document))
            return audio_fail(state, "audible gain change requires document activation", sources[i].description.id);
    }
    if (state->sample_sequence == UINT64_MAX) return audio_fail(state, "audio: sample sequence exhausted");
    bool changed = !audio_motion_equal(listener->motion, state->listener.motion) ||
        !audio_vector_equal(listener->right, state->listener.right) || !audio_vector_equal(listener->forward, state->listener.forward) || count != state->count;
    for (unsigned i = 0; i < count; i++) {
        auto& source = sources[i];
        changed |= i >= state->count || !audio_motion_equal(source.motion, state->sources[i].motion) ||
                   !audio_parameters_equal(source.parameters, state->sources[i].parameters);
        if (source.player) {
            if (!radiant_audio_parameters(state->document, source.player, &source.parameters, source.description.values[AUDIO_GAIN])) {
                RdtAudioInfo info = {};
                if (radiant_audio_info(state->document, source.player, &info))
                    return audio_fail(state, "native parameter publication failed", source.description.id);
                source.player = 0;
            }
        }
    }
    if (changed) state->sample_sequence++;
    return true;
}
Scene3dAudio* scene3d_audio_create(DomElement* root, Scene3dRuntime* runtime) {
    if (!root || !runtime) return nullptr;
    auto* state = (Scene3dAudio*)mem_calloc(1, sizeof(Scene3dAudio), MEM_CAT_RENDER);
    if (state) { state->document = root->doc; state->root = dom_node_ref(root); state->runtime = runtime;
        state->generation = 1; state->epoch = UINT64_MAX; state->speed = 343; state->cue_epoch = 1; }
    return state;
}
static void audio_cues_collect(Scene3dAudio* state) {
    if (!state->cues || !audio_submix_lock(state->submix)) return;
    bool scene_work = audio_cues_scene_work_locked(state->cues);
    audio_submix_unlock(state->submix);
    audio_cue_owner_collect(state->cue_owner);
    scene3d_runtime_clock_work(state->runtime, scene_work);
}
static const char* audio_cues_sync(Scene3dAudio* state) {
    if (!state->cues) return nullptr;
    audio_cues_collect(state);
    // a held queue can be the first new work after a long idle interval; never consume that idle time.
    if (!audio_submix_has_work(state->submix)) radiant_document_submix(state->document);
    Scene3dRuntimeStats clock = {}; scene3d_runtime_stats(state->runtime, &clock);
    if (!audio_submix_lock(state->submix)) return "audio cues: document submix retired";
    audio_cues_clock_locked(state->cues, audio_submix_clock(state->submix), scene3d_runtime_sampled_time(state->runtime), clock.time_scale, clock.paused || clock.faulted);
    bool work = audio_cues_next_locked(state->cues) != UINT64_MAX;
    audio_submix_unlock(state->submix);
    const char* error = nullptr;
    if (!audio_submix_prepare_output(state->submix, work, &error)) {
        if (audio_submix_lock(state->submix)) {
            audio_cues_cancel_locked(state->cues, 0, audio_submix_clock(state->submix)); audio_submix_unlock(state->submix);
        }
        return error ? error : "audio cues: output admission failed";
    }
    return nullptr;
}
void scene3d_audio_reset(Scene3dAudio* state) {
    if (!state) return;
    if (state->cue_epoch) state->cue_epoch = state->cue_epoch == UINT64_MAX ? 0 : state->cue_epoch + 1;
    if (state->cues && audio_submix_lock(state->submix)) {
        audio_cues_reset_locked(state->cues, state->cue_epoch, audio_submix_clock(state->submix)); audio_submix_unlock(state->submix);
    }
    for (unsigned i = 0; i < state->count; i++) {
        radiant_audio_close(state->document, state->sources[i].player);
        state->sources[i].player = 0; state->sources[i].motion = {};
    }
    state->listener.motion = {};
    audio_cues_collect(state);
    audio_submix_graph_reset(state->submix, state->graph);
}
void scene3d_audio_destroy(Scene3dAudio* state) {
    if (!state) return;
    Scene3dRuntimeStats clock = {}; scene3d_runtime_stats(state->runtime, &clock);
    audio_render_close_session(state->document, clock.session);
    scene3d_audio_reset(state);
    audio_cue_owner_destroy(state->cue_owner); state->cue_owner = nullptr; state->cues = nullptr;
    audio_submix_graph_close(state->submix, state->graph);
    for (unsigned i = 0; i < state->clip_count; i++) radiant_audio_close(state->document, state->clips[i].token);
    mem_free(state);
}
static const char* audio_listener_description(Scene3dAudio* state, const AudioGather& gather, AudioListener* result) {
    auto& listener = *result;
    DomElement* selected = gather.listener_id ? gather.selected : gather.explicit_listener ? gather.explicit_listener : gather.camera;
    if (gather.listener_id && !selected) return "audio: missing selected listener";
    if (selected) {
        auto ref = dom_node_ref(selected);
        if (state->listener.node.address == ref.address && state->listener.node.expected_id == ref.expected_id) listener = state->listener;
        listener.node = ref; listener.camera = scene3d_tag(selected, "camera");
        const char* velocity = listener.camera ? "audio_velocity" : "velocity";
        if (!audio_text(selected, "id", "", listener.id, true) || !scene3d_vector(selected, velocity, {}, &listener.velocity) ||
            !audio_vector_valid(listener.velocity)) return "audio: invalid listener description";
        listener.explicit_velocity = scene3d_present(selected, velocity);
    }
    return nullptr;
}
bool scene3d_audio_prepare(Scene3dAudio* state, DomElement* root, Scene3dAnimationState* animation,
    Scene3dPhysics* physics, char* diagnostic, size_t capacity) {
    auto fail = [&](const char* message) { audio_fail(state, message); str_copy(diagnostic, capacity, state->diagnostic, strlen(state->diagnostic)); return false; };
    if (!state || !root || root->doc != state->document || !dom_element_is_connected(root)) return false;
    state->animation = animation; state->physics = physics;
    if (state->epoch == root->doc->mutation_epoch) {
        const char* error = scene3d_audio_sample(state, scene3d_runtime_sampled_time(state->runtime));
        if (error) { str_copy(diagnostic, capacity, error, strlen(error)); return false; }
        return true;
    }
    AudioGather gather{root, {}, nullptr, nullptr, nullptr, 0, scene3d_text(root, "audio_listener"), scene3d_text(root, "camera")};
    if ((scene3d_present(root, "audio_listener") && !gather.listener_id) ||
        !scene3d_visit(root, audio_gather, &gather)) return fail("audio: invalid source/listener hierarchy or source limit");
    AudioSource sources[AUDIO_SCENE_SOURCES] = {}; AudioListener listener = {};
    if (const char* error = audio_listener_description(state, gather, &listener)) return fail(error);
    uint64_t identity = state->identity;
    for (unsigned i = 0; i < gather.count; i++) {
        auto ref = dom_node_ref(gather.sources[i]);
        for (unsigned j = 0; j < state->count; j++)
            if (state->sources[j].node.address == ref.address && state->sources[j].node.expected_id == ref.expected_id) sources[i] = state->sources[j];
        AudioDescription description;
        if (!audio_description(gather.sources[i], &description)) return fail("audio: invalid source description");
        if (!sources[i].identity || strcmp(sources[i].description.id, description.id)) {
            if (identity == UINT64_MAX) return fail("audio: source identity exhausted");
            sources[i] = {}; sources[i].identity = ++identity;
        }
        sources[i].node = ref; sources[i].description = description;
    }
    AudioIdCheck ids{&gather, sources, &listener};
    if (!scene3d_visit(root, audio_check_ids, &ids)) return fail("audio: source/listener ID conflicts with scene content");
    AudioPreparedGraph prepared;
    if (!audio_prepare_routing(state, gather, sources, &prepared)) {
        str_copy(diagnostic, capacity, state->diagnostic, strlen(state->diagnostic)); return false;
    }
    float speed;
    if (!scene3d_number(root, "audio_speed_of_sound", 343, &speed) || speed < .001f || speed > AUDIO_COORDINATE_LIMIT ||
        state->generation == UINT64_MAX) return fail("audio: invalid speed of sound or exhausted generation");
    if (!audio_publish(state, sources, gather.count, &listener, speed, scene3d_runtime_sampled_time(state->runtime))) {
        str_copy(diagnostic, capacity, state->diagnostic, strlen(state->diagnostic)); return false;
    }
    bool routing_changed = prepared.value != nullptr;
    if (state->graph) {
        AudioPlayerRoute routes[AUDIO_SCENE_SOURCES]; unsigned count = 0;
        for (unsigned i = 0; i < gather.count; i++) if (sources[i].player) routes[count++] = {sources[i].player, sources[i].route};
        const char* error = nullptr;
        if (!radiant_audio_graph_commit(state->document, state->graph, prepared.value, routes, count, &error))
            return fail(error ? error : "audio graph: native publication rejected");
        prepared.value = nullptr;
        if (routing_changed) state->routing_generation++;
    }
    for (unsigned i = 0; i < state->count; i++) {
        bool retained = false;
        for (unsigned j = 0; j < gather.count; j++) if (state->sources[i].identity == sources[j].identity) retained = true;
        if (!retained) radiant_audio_close(state->document, state->sources[i].player);
    }
    bool changed = state->count != gather.count || state->speed != speed || routing_changed;
    for (unsigned i = 0; i < gather.count; i++) changed |= i >= state->count ||
        !audio_description_equal(sources[i].description, state->sources[i].description) || sources[i].identity != state->sources[i].identity;
    changed |= state->listener.node.address != listener.node.address || state->listener.node.expected_id != listener.node.expected_id;
    memcpy(state->sources, sources, gather.count * sizeof(*sources)); state->count = gather.count;
    state->listener = listener; state->identity = identity; state->speed = speed;
    if (changed) state->generation++;
    state->epoch = root->doc->mutation_epoch; state->diagnostic[0] = 0; return true;
}
const char* scene3d_audio_sample(Scene3dAudio* state, double time) {
    if (!state) return nullptr;
    if (const char* error = audio_cues_sync(state)) { audio_fail(state, error); return state->diagnostic; }
    AudioSource sources[AUDIO_SCENE_SOURCES]; memcpy(sources, state->sources, state->count * sizeof(*sources));
    auto listener = state->listener;
    if (!audio_publish(state, sources, state->count, &listener, state->speed, time)) return state->diagnostic;
    memcpy(state->sources, sources, state->count * sizeof(*sources)); state->listener = listener;
    state->diagnostic[0] = 0; return nullptr;
}
void scene3d_audio_follow_rebuild(Scene3dAudio* state, DomNode* before, DomNode* after) {
    if (!state) return;
    view_state_ref_follow_rebuild(state->document, &state->root, before, after);
    view_state_ref_follow_rebuild(state->document, &state->listener.node, before, after);
    for (unsigned i = 0; i < state->count; i++) view_state_ref_follow_rebuild(state->document, &state->sources[i].node, before, after);
    state->epoch = UINT64_MAX;
}
const char* scene3d_audio_diagnostic(Scene3dAudio* state) { return state && state->diagnostic[0] ? state->diagnostic : nullptr; }

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
    audio_cues_collect(state);
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
            for (const auto& field : audio_effect_fields) insert.put(field.key, push_d(*(const float*)((const char*)&effect + field.offset)));
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
    if (const char* error = audio_cues_sync(state)) return error;
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
    // reader decoding/copying stays on the main owner; the callback only swaps retained PCM pointers (D4.5.2).
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
    audio_cues_collect(state); return nullptr;
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
static Item audio_render_operate(Scene3dAudio* state, uint64_t session, const char* operation, MapReader fields) {
    auto fail = [](const char* message) { return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr)); };
    const char* error = nullptr; uint64_t id;
    if (!strcmp(operation, "audio_render_open")) {
        AudioRenderPlan plan = {}; AudioSource* sources[AUDIO_CUE_CAPACITY] = {};
        if (!audio_render_count(fields.get("frames"), 0, AUDIO_RENDER_BYTES / 8, &plan.frames) ||
            !physics_scene_real(fields.get("scene_time"), 0, &plan.scene_time) ||
            !physics_scene_real(fields.get("time_scale"), 1, &plan.time_scale)) return fail("audio render: invalid frame or virtual-time field");
        if (const char* rejected = audio_copy_commands(state, fields.get("events"), AUDIO_CUE_CAPACITY, plan.events, sources, &plan.event_count)) return fail(rejected);
        auto* node = dom_node_ref_validate(state->document, state->root);
        if (!node || !node->is_element() || !dom_element_is_connected(node->as_element())) return fail("audio render: detached scene");
        auto* root = node->as_element();
        AudioGather gather{root, {}, nullptr, nullptr, nullptr, 0, scene3d_text(root, "audio_listener"), scene3d_text(root, "camera")};
        if ((scene3d_present(root, "audio_listener") && !gather.listener_id) || !scene3d_visit(root, audio_gather, &gather))
            return fail("audio render: invalid authored audio hierarchy");
        AudioListener listener = {}; float speed;
        if (const char* rejected = audio_listener_description(state, gather, &listener)) return fail(rejected);
        if (!scene3d_number(root, "audio_speed_of_sound", 343, &speed) || speed < .001f || speed > AUDIO_COORDINATE_LIMIT ||
            !audio_listener_frame(state, root, &listener, scene3d_runtime_sampled_time(state->runtime))) return fail("audio render: invalid listener sample");
        if (const char* rejected = audio_bus_config(gather, &plan.graph)) return fail(rejected);
        for (unsigned i = 0; i < plan.event_count; i++) {
            auto* source = sources[i]; const auto& cue = plan.events[i];
            if (!source || source->identity != cue.identity) return fail("audio render: stale source identity");
            AudioSource copy = *source; auto* candidate = dom_node_ref_validate(state->document, source->node); bool present = false;
            for (unsigned j = 0; j < gather.count; j++) present |= candidate == gather.sources[j];
            if (!present || !audio_description(candidate->as_element(), &copy.description) || strcmp(copy.description.id, source->description.id))
                return fail("audio render: detached, renamed or invalid source");
            auto* clip = audio_clip(state, copy.description.clip);
            if (!clip || (cue.op == AUDIO_CUE_PLAY && clip->identity != cue.clip)) return fail("audio render: missing or stale clip identity");
            bool copied = false;
            for (unsigned j = 0; j < plan.source_count; j++) copied |= plan.sources[j].identity == source->identity;
            if (!copied) {
                if (plan.source_count == AUDIO_CUE_TRACKS) return fail("audio render: source limit exceeded");
                // export samples authored controls into a private plan; it never publishes them to live players.
                if (!audio_source_frame(state, root, &copy, listener, speed, scene3d_runtime_sampled_time(state->runtime)))
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
    static constexpr struct Provider {
        const char* name;
        bool (*route)(DomDocument*, uint64_t, AudioRouteId, AudioRouteId, const char**);
        bool (*read)(DomDocument*, uint64_t, AudioProviderInfo*);
    } providers[] = {{"midi", radiant_midi_route, radiant_midi_routing}, {"speech", radiant_speech_route, radiant_speech_routing},
        {"video", radiant_media_route, radiant_media_routing}};
    const char* name = scene3d_command_name(fields.get("provider")); const Provider* provider = nullptr;
    for (const auto& entry : providers) if (name && !strcmp(name, entry.name)) provider = &entry;
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
            audio_cues_collect(state);
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
        // decode and quota admission precede replacement; existing cursors retain their old immutable PCM (D4.5.2).
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
