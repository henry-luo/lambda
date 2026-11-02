#include "scene3d.hpp"
#include "scene3d_source.hpp"
#include "scene3d_physics.hpp"
#include "scene3d_animation.hpp"
#include "../../lib/physics/physics_math.h"
#include "../animation_value.hpp"
#include "../event.hpp"
#include "../../lambda/core/mark_reader.hpp"
#include "../../lambda/input/css/dom_element.hpp"
#include "../../lambda/input/css/dom_lifecycle.hpp"
#include "../../lambda/runtime/lambda-number-runtime.hpp"
#include "../../lambda/runtime/lambda-error.h"
#include "../../lambda/runtime/lambda-navigation.hpp"
#include "../../lambda/runtime/value_builder.hpp"
#include "../../lib/mem.h"
#include "../../lib/mem_factory.h"
#include "../../lib/procgen/rng.h"
#include "../../lib/str.h"
#include <float.h>

static constexpr unsigned PARTICLE_EMITTER_LIMIT = 32, PARTICLE_STORAGE_LIMIT = 65536, PARTICLE_SPAWN_LIMIT = 4096,
                          PARTICLE_CURVE_LIMIT = SCENE3D_PARTICLE_CURVE_LIMIT, PARTICLE_EVENT_STORAGE_LIMIT = 16384,
                          PARTICLE_SURFACE_STORAGE_LIMIT = 65536;
static constexpr unsigned PARTICLE_COMMAND_LIMIT = 128, PARTICLE_DEPTH_LIMIT = 8;
enum ParticleParameter {
    PARTICLE_RATE,
    PARTICLE_DURATION,
    PARTICLE_LIFE_MIN,
    PARTICLE_LIFE_MAX,
    PARTICLE_SIZE,
    PARTICLE_DRAG,
    PARTICLE_RADIUS,
    PARTICLE_ANGLE,
    PARTICLE_SPEED,
    PARTICLE_SPREAD,
    PARTICLE_PARAMETERS
};
enum ParticleShape { PARTICLE_POINT, PARTICLE_BOX, PARTICLE_SPHERE, PARTICLE_CONE, PARTICLE_MESH };
enum ParticleOverflow { PARTICLE_DROP, PARTICLE_RECYCLE, PARTICLE_ERROR };
struct ParticleCurve {
    unsigned count;
    double times[PARTICLE_CURVE_LIMIT], values[PARTICLE_CURVE_LIMIT * 3];
};
struct ParticleDescription {
    double parameters[PARTICLE_PARAMETERS];
    Scene3dVec extent, velocity, gravity;
    Scene3dParticleCollisionDesc collision;
    Scene3dParticleAnalytic analytic;
    Scene3dParticleNoise noise;
    Scene3dParticleSheet sheet;
    Scene3dParticleTrailDesc trail;
    Scene3dParticleSurface *surface;
    unsigned surface_slot;
    float normal_speed;
    uint64_t seed;
    unsigned capacity, burst, shape, overflow;
    bool looping, emitting, gpu, world, billboard;
    ParticleCurve curves[SCENE3D_PARTICLE_CURVES];
};
struct ParticlePlayback {
    unsigned head, count;
    double time, credit;
    uint64_t sequence, spawned, dropped, recycled, collisions, killed, queries, geometry_work, triangle_work,
        event_sequence;
    unsigned event_head, event_count;
    bool started;
};
struct ParticleRecord {
    Scene3dParticleSample value;
    Scene3dVec dynamic_velocity; // authored transport never accumulates into force-driven motion
    Scene3dVec spawn_velocity; // local recipe offset for device-owned births
    Scene3dMatrix frame; // frozen birth basis/translation; motion is in the selected simulation space
    float initial_size;
    unsigned slot, surface_slot;
    bool newborn;
};
struct ParticleEvent {
    uint64_t sequence, particle, tick, world_identity;
    Scene3dParticleContact contact;
    unsigned depth;
};
struct ParticleSpawnRequest {
    uint64_t emitter;
    Scene3dVec position, velocity;
    unsigned count, depth;
    bool positioned;
};
struct ParticleSpawnBatch { unsigned count; ParticleSpawnRequest requests[PARTICLE_COMMAND_LIMIT]; };
struct ParticleEmitter {
    DomNodeRef source;
    char id[128];
    uint64_t identity;
    ParticleDescription description;
    ParticlePlayback playback;
    ParticleRecord *particles, *scratch;
    ParticleEvent *events, *event_scratch;
    Scene3dMatrix *matrices;
    Scene3dParticleGpu *gpu;
    Scene3dParticleTrailState *trail;
    Scene3dParticleSurfaces surfaces;
    float *slots, *colors, *billboards;
    unsigned *frames;
    Scene3dParticleSheet sheet; // committed presentation, independent of pending source edits
    float max_size;
    unsigned matrix_count;
    bool rotating, fading;
    Scene3dVec lo, hi;
};
struct Scene3dParticles {
    DomDocument *document;
    DomNodeRef root;
    Scene3dRuntime *runtime;
    Scene3dParticleGpuOps gpu_ops;
    Scene3dPhysics *replay_physics;
    Scene3dAnimationState *replay_animation;
    Scene3dRuntime *replay_runtime;
    ParticleEmitter *emitters[PARTICLE_EMITTER_LIMIT];
    unsigned count;
    ParticleSpawnBatch pending;
    unsigned pending_spawns;
    uint64_t emitter_sequence;
    uint64_t epoch, generation, replay_source_generation;
    uint64_t replay_source_epoch, replay_clock_generation, replay_physics_generation, replay_world_identity,
        replay_animation_generation;
    size_t replay_ticks;
    double replay_timestep;
    bool bound, automatic, replay_geometry;
    const char *diagnostic;
};
static bool particle_fail(char *diagnostic, size_t capacity, const char *message) {
    str_copy(diagnostic, capacity, message, strlen(message));
    return false;
}
static void particle_emitter_destroy(ParticleEmitter *emitter) {
    if (!emitter)
        return;
    mem_free(emitter->particles);
    mem_free(emitter->scratch);
    mem_free(emitter->matrices);
    mem_free(emitter->events);
    mem_free(emitter->event_scratch);
    mem_free(emitter->slots);
    mem_free(emitter->colors);
    mem_free(emitter->billboards);
    mem_free(emitter->frames);
    scene3d_particle_gpu_destroy(emitter->gpu);
    scene3d_particle_trail_destroy(emitter->trail);
    for (auto *surface : emitter->surfaces.values)
        scene3d_particle_surface_release(surface);
    mem_free(emitter);
}
static ParticleEmitter *particle_emitter_create(const ParticleDescription &description, Scene3dParticles *state) {
    auto *emitter = (ParticleEmitter *)mem_calloc(1, sizeof(ParticleEmitter), MEM_CAT_RENDER);
    if (!emitter)
        return nullptr;
    emitter->description = description;
    if (description.surface) {
        if (!scene3d_particle_surface_retain(description.surface)) {
            particle_emitter_destroy(emitter);
            return nullptr;
        }
        emitter->surfaces.values[0] = description.surface;
        emitter->description.surface_slot = 0;
    }
    emitter->particles = (ParticleRecord *)mem_calloc(description.capacity, sizeof(ParticleRecord), MEM_CAT_RENDER);
    emitter->scratch = (ParticleRecord *)mem_calloc(description.capacity, sizeof(ParticleRecord), MEM_CAT_RENDER);
    if (description.trail.samples) {
        emitter->trail = scene3d_particle_trail_create(description.capacity, description.trail.samples, description.gpu);
        if (!emitter->trail) { particle_emitter_destroy(emitter); return nullptr; }
    }
    if (description.gpu) {
        emitter->gpu = scene3d_particle_gpu_create(description.capacity, state->gpu_ops, &emitter->surfaces);
        emitter->slots = (float *)mem_calloc(description.capacity, sizeof(float), MEM_CAT_RENDER);
        scene3d_particle_gpu_trail_attach(emitter->gpu, emitter->trail);
    } else {
        emitter->matrices = (Scene3dMatrix *)mem_calloc(description.capacity, sizeof(Scene3dMatrix), MEM_CAT_RENDER);
        emitter->colors = (float *)mem_calloc(description.capacity * 4, sizeof(float), MEM_CAT_RENDER);
        emitter->billboards = (float *)mem_calloc(description.capacity * 3, sizeof(float), MEM_CAT_RENDER);
        emitter->frames = (unsigned *)mem_calloc(description.capacity, sizeof(unsigned), MEM_CAT_RENDER);
        emitter->events =
            (ParticleEvent *)mem_calloc(description.collision.event_capacity, sizeof(ParticleEvent), MEM_CAT_RENDER);
        emitter->event_scratch =
            (ParticleEvent *)mem_calloc(description.collision.event_capacity, sizeof(ParticleEvent), MEM_CAT_RENDER);
    }
    if ((!description.gpu && (!emitter->events || !emitter->event_scratch || !emitter->colors || !emitter->frames ||
                             !emitter->billboards)) || !emitter->particles ||
        !emitter->scratch || (description.gpu ? !emitter->gpu || !emitter->slots : !emitter->matrices)) {
        particle_emitter_destroy(emitter);
        return nullptr;
    }
    return emitter;
}
static ParticleEmitter *particle_emitter_find(Scene3dParticles *state, DomElement *node) {
    if (state && node) {
        for (unsigned i = 0; i < state->count; i++) {
            auto *emitter = state->emitters[i];
            if (emitter->source.address == node && emitter->source.expected_id == node->DomNode::id)
                return emitter;
        }
    }
    return nullptr;
}
static ParticleEmitter *particle_emitter_identity(Scene3dParticles *state, uint64_t identity) {
    if (state && identity) for (unsigned i = 0; i < state->count; i++)
        if (state->emitters[i]->identity == identity) return state->emitters[i];
    return nullptr;
}
static bool particle_vector_finite(Scene3dVec value) {
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}
struct ParticleCurveSpec {
    const char *tag;
    unsigned components;
    double minimum, maximum, initial[3];
};
static const ParticleCurveSpec particle_curve_specs[SCENE3D_PARTICLE_CURVES] = {
    {"particle-size-curve", 1, 0, 1e6, {1}},
    {"particle-rotation-curve", 3, -1e6, 1e6, {}},
    {"particle-color-curve", 3, 0, 1, {1, 1, 1}},
    {"particle-opacity-curve", 1, 0, 1, {1}},
    {"particle-velocity-curve", 3, -1e6, 1e6, {}}
};
static bool particle_curve_read(DomElement *node, Pool *pool, const ParticleCurveSpec &spec, ParticleCurve *curve) {
    unsigned components = spec.components;
    float *times = nullptr, *values = nullptr;
    unsigned count = 0, value_count = 0;
    if (node->first_child || !scene3d_numbers(node, "times", pool, &times, &count, PARTICLE_CURVE_LIMIT) ||
        !scene3d_numbers(node, "values", pool, &values, &value_count, PARTICLE_CURVE_LIMIT * components) || count < 2 ||
        value_count != count * components || times[0] != 0 || times[count - 1] != 1)
        return false;
    curve->count = count;
    for (unsigned i = 0; i < count; i++) {
        if (times[i] < 0 || times[i] > 1 || (i && times[i] <= times[i - 1]))
            return false;
        curve->times[i] = times[i];
        for (unsigned c = 0; c < components; c++) {
            double value = values[i * components + c];
            if (value < spec.minimum || value > spec.maximum)
                return false;
            curve->values[i * components + c] = value;
        }
    }
    return true;
}
static bool particle_curve_same(const ParticleCurve &a, const ParticleCurve &b, unsigned components) {
    return a.count == b.count && !memcmp(a.times, b.times, a.count * sizeof(double)) &&
           !memcmp(a.values, b.values, a.count * components * sizeof(double));
}
static bool particle_curves_same(const ParticleDescription &a, const ParticleDescription &b) {
    for (unsigned i = 0; i < SCENE3D_PARTICLE_CURVES; i++)
        if (!particle_curve_same(a.curves[i], b.curves[i], particle_curve_specs[i].components))
            return false;
    return true;
}
static bool particle_curve_sample(const ParticleCurve &curve, unsigned components, double phase, double *values) {
    AnimationTrackView track = {
        curve.times,     curve.values, nullptr,    nullptr,
        nullptr,         curve.count,  components, components == 1 ? ANIMATION_NUMBER : ANIMATION_VECTOR,
        ANIMATION_LINEAR};
    return animation_track_sample(track, phase, values);
}
static bool particle_velocity_sample(const ParticleDescription &d, double age, double lifetime, Scene3dVec *value) {
    double sampled[3];
    if (!particle_curve_sample(d.curves[SCENE3D_PARTICLE_VELOCITY_CURVE], 3, age / lifetime, sampled)) return false;
    *value = {(float)sampled[0], (float)sampled[1], (float)sampled[2]};
    return true;
}
static bool particle_sheet_domain(const Scene3dParticleSheet &sheet, double lifetime) {
    // floor/uint conversion must stay within float's consecutive-integer range.
    if (sheet.flags & SCENE3D_PARTICLE_SHEET_LIFETIME)
        return (!(sheet.count > 1 && sheet.rate > 0) || (float)lifetime > 0) &&
               sheet.rate * (float)sheet.count <= 16777215.f;
    return sheet.rate * (float)lifetime <= 16777215.f;
}
static bool particle_sheet_read(DomElement *node, Scene3dParticleSheet *sheet, double minimum, double maximum) {
    const char *mode = scene3d_text(node, "mode");
    bool lifetime = !mode || !strcmp(mode, "lifetime"), loop, random;
    if (node->first_child || (mode && !lifetime && strcmp(mode, "fps")) ||
        scene3d_present(node, lifetime ? "fps" : "cycles") ||
        !scene3d_unsigned(node, "columns", 1, 256, &sheet->columns) || !sheet->columns ||
        !scene3d_unsigned(node, "rows", 1, 256, &sheet->rows) || !sheet->rows) return false;
    unsigned cells = sheet->columns * sheet->rows;
    if (!scene3d_unsigned(node, "first", 0, cells - 1, &sheet->first) ||
        !scene3d_unsigned(node, "count", cells - sheet->first, cells - sheet->first, &sheet->count) || !sheet->count ||
        !scene3d_number(node, lifetime ? "cycles" : "fps", lifetime ? 1 : 10, &sheet->rate) ||
        sheet->rate < 0 || sheet->rate > 1024 ||
        !scene3d_number(node, "inset", 0, &sheet->inset) || sheet->inset < 0 || sheet->inset >= .5f ||
        !scene3d_flag(node, "loop", true, &loop) || !scene3d_flag(node, "random-start", false, &random)) return false;
    sheet->flags = (loop ? SCENE3D_PARTICLE_SHEET_LOOP : 0) | (random ? SCENE3D_PARTICLE_SHEET_RANDOM : 0) |
                   (lifetime ? SCENE3D_PARTICLE_SHEET_LIFETIME : 0);
    return particle_sheet_domain(*sheet, minimum) && particle_sheet_domain(*sheet, maximum);
}
static bool particle_description(DomElement *root, DomElement *node, Pool *pool, ParticleDescription *result,
                                 char *diagnostic, size_t capacity) {
    auto &d = *result;
    const char *backend = scene3d_text(node, "backend"), *space = scene3d_text(node, "space");
    const char *presentation = scene3d_text(node, "presentation");
    d.billboard = presentation && !strcmp(presentation, "billboard");
    if ((scene3d_present(node, "presentation") && !presentation) ||
        (presentation && strcmp(presentation, "mesh") && !d.billboard)) return false;
    const char *shape = scene3d_text(node, "shape"), *overflow = scene3d_text(node, "overflow");
    d.gpu = backend && !strcmp(backend, "gpu");
    if ((backend && strcmp(backend, "cpu") && !d.gpu) || (space && strcmp(space, "local") && strcmp(space, "world")))
        return false;
    d.world = space && !strcmp(space, "world");
    const char *shapes[] = {"point", "box", "sphere", "cone", "mesh"};
    const char *policies[] = {"drop-new", "recycle-oldest", "error"};
    bool known = !shape;
    for (unsigned i = 0; i < 5; i++)
        if (shape && !strcmp(shape, shapes[i])) {
            d.shape = i;
            known = true;
        }
    if (!known)
        return false;
    known = !overflow;
    for (unsigned i = 0; i < 3; i++)
        if (overflow && !strcmp(overflow, policies[i])) {
            d.overflow = i;
            known = true;
        }
    if (!known || !scene3d_unsigned(node, "capacity", 256, SCENE3D_INSTANCE_LIMIT, &d.capacity) || !d.capacity ||
        !scene3d_unsigned(node, "burst", 0, PARTICLE_SPAWN_LIMIT, &d.burst))
        return false;
    auto seed = scene3d_value(node, "seed");
    const char *seed_text = scene3d_text(node, "seed");
    d.seed = 0;
    if (seed_text) {
        if (!scene3d_counter_text(seed_text, strlen(seed_text), &d.seed))
            return false;
    } else if (!seed.isNull() && !scene3d_counter(seed, &d.seed))
        return false;
    const char *names[] = {"rate", "duration", "lifetime-min", "lifetime", "size",
                           "drag", "radius",   "angle",        "speed",    "spread"};
    const double defaults[] = {10, 0, 1, 1, 1, 0, 1, 30, 1, 0};
    for (unsigned i = 0; i < PARTICLE_PARAMETERS; i++) {
        if (!scene3d_real(node, names[i], defaults[i], d.parameters + i) || d.parameters[i] < 0 ||
            d.parameters[i] > 1e6)
            return false;
    }
    // omitted lifetime-min follows lifetime, making a single lifetime a fixed duration.
    if (!node->has_attribute("lifetime-min") && scene3d_value(node, "lifetime-min").isNull())
        d.parameters[PARTICLE_LIFE_MIN] = d.parameters[PARTICLE_LIFE_MAX];
    if ((d.gpu && !((float)d.parameters[PARTICLE_LIFE_MIN] > 0)) || !(d.parameters[PARTICLE_LIFE_MIN] > 0) ||
        d.parameters[PARTICLE_LIFE_MIN] > d.parameters[PARTICLE_LIFE_MAX] || !(d.parameters[PARTICLE_SIZE] > 0) ||
        d.parameters[PARTICLE_ANGLE] > 180 || !scene3d_vector(node, "extent", {1, 1, 1}, &d.extent) ||
        !scene3d_vector(node, "velocity", {}, &d.velocity) ||
        !scene3d_vector(node, "gravity", {0, -9.81f, 0}, &d.gravity) || d.extent.x < 0 || d.extent.y < 0 ||
        d.extent.z < 0 || !scene3d_flag(node, "loop", true, &d.looping) ||
        !scene3d_flag(node, "emitting", true, &d.emitting))
        return false;
    d.surface_slot = SCENE3D_PARTICLE_SURFACE_VERSIONS;
    if (!scene3d_number(node, "normal-speed", 0, &d.normal_speed) || fabsf(d.normal_speed) > 1e6 ||
        (d.shape != PARTICLE_MESH && (scene3d_text(node, "surface") || d.normal_speed)))
        return false;
    for (unsigned kind = 0; kind < SCENE3D_PARTICLE_CURVES; kind++) {
        auto &curve = d.curves[kind];
        const auto &spec = particle_curve_specs[kind];
        curve.count = 2;
        curve.times[1] = 1;
        for (unsigned key = 0; key < 2; key++)
            for (unsigned c = 0; c < spec.components; c++)
                curve.values[key * spec.components + c] = spec.initial[c];
    }
    d.sheet = {1, 1, 0, 1, SCENE3D_PARTICLE_SHEET_LOOP | SCENE3D_PARTICLE_SHEET_LIFETIME, 1, 0};
    bool curves[SCENE3D_PARTICLE_CURVES] = {}, noise = false, sheet = false;
    d.collision.event_capacity = d.gpu ? 0 : 256;
    for (DomNode *child = node->first_child; child; child = child->next_sibling) {
        if (!child->is_element())
            continue;
        auto *element = child->as_element();
        const char *tag = element->local_name();
        if (!strcmp(tag, "geometry") || !strcmp(tag, "material"))
            continue;
        if (!strcmp(tag, "particle-collision")) {
            if (d.collision.enabled || d.analytic.count ||
                (d.gpu ? !scene3d_particle_analytic_read(element, &d.analytic)
                       : element->first_child || !scene3d_particle_collision_read(element, &d.collision)))
                return false;
        } else if (!strcmp(tag, "particle-trail")) {
            if (d.trail.samples || !scene3d_particle_trail_read(element, &d.trail)) return false;
        } else if (!strcmp(tag, "particle-sheet")) {
            if (sheet || !particle_sheet_read(element, &d.sheet, d.parameters[PARTICLE_LIFE_MIN],
                                               d.parameters[PARTICLE_LIFE_MAX])) return false;
            sheet = true;
        } else if (!strcmp(tag, "particle-noise")) {
            Scene3dVec strength;
            if (noise || element->first_child ||
                !scene3d_vector(element, "strength", {1, 1, 1}, &strength) ||
                !scene3d_number(element, "frequency", 1, &d.noise.frequency) ||
                d.noise.frequency < 0 || d.noise.frequency > 1024 ||
                d.noise.frequency * d.parameters[PARTICLE_LIFE_MAX] > 65535)
                return false;
            d.noise.strength[0] = strength.x;
            d.noise.strength[1] = strength.y;
            d.noise.strength[2] = strength.z;
            for (float value : d.noise.strength)
                if (value < 0 || value > 1e6)
                    return false;
            noise = true;
        } else {
            unsigned kind = 0;
            while (kind < SCENE3D_PARTICLE_CURVES && strcmp(tag, particle_curve_specs[kind].tag)) kind++;
            if (kind == SCENE3D_PARTICLE_CURVES || curves[kind] ||
                !particle_curve_read(element, pool, particle_curve_specs[kind], d.curves + kind))
                return false;
            curves[kind] = true;
        }
    }
    return d.shape != PARTICLE_MESH ||
           scene3d_particle_surface_read(root, node, pool, &d.surface, diagnostic, capacity);
}
static bool particle_collision_same(const Scene3dParticleCollisionDesc &a, const Scene3dParticleCollisionDesc &b) {
    return a.radius == b.radius && a.skin == b.skin && a.restitution == b.restitution && a.friction == b.friction &&
           a.bounce_threshold == b.bounce_threshold && a.membership == b.membership && a.filter == b.filter &&
           a.response == b.response && a.impacts == b.impacts && a.iterations == b.iterations &&
           a.event_capacity == b.event_capacity && a.enabled == b.enabled && a.sensors == b.sensors &&
           a.ignore_owner == b.ignore_owner && a.overlap_error == b.overlap_error && a.events == b.events;
}
static bool particle_description_same(const ParticleDescription &a, const ParticleDescription &b) {
    return !memcmp(a.parameters, b.parameters, sizeof(a.parameters)) && a.seed == b.seed && a.capacity == b.capacity &&
           a.burst == b.burst && a.shape == b.shape && a.normal_speed == b.normal_speed &&
           scene3d_particle_surface_same(a.surface, b.surface) && a.overflow == b.overflow && a.looping == b.looping &&
           a.emitting == b.emitting && a.gpu == b.gpu && a.world == b.world && a.billboard == b.billboard &&
           particle_collision_same(a.collision, b.collision) && !memcmp(&a.extent, &b.extent, sizeof(a.extent)) &&
           !memcmp(&a.analytic, &b.analytic, sizeof(a.analytic)) &&
           !memcmp(&a.noise, &b.noise, sizeof(a.noise)) && !memcmp(&a.sheet, &b.sheet, sizeof(a.sheet)) &&
           scene3d_particle_trail_same(a.trail, b.trail) &&
           !memcmp(&a.velocity, &b.velocity, sizeof(a.velocity)) &&
           !memcmp(&a.gravity, &b.gravity, sizeof(a.gravity)) && particle_curves_same(a, b);
}
static bool particle_active(const ParticleEmitter *emitter) {
    const auto &d = emitter->description;
    return emitter->playback.count ||
           (d.emitting &&
            (d.parameters[PARTICLE_RATE] > 0 ||
             (d.burst && (!emitter->playback.started || (d.looping && d.parameters[PARTICLE_DURATION] > 0)))) &&
            (!d.parameters[PARTICLE_DURATION] || d.looping ||
             emitter->playback.time < d.parameters[PARTICLE_DURATION]));
}
static bool particles_active(const Scene3dParticles *state) {
    if (state->pending.count) return true;
    for (unsigned i = 0; i < state->count; i++)
        if (particle_active(state->emitters[i]))
            return true;
    return false;
}
static double particle_random(const ParticleDescription &d, uint64_t sequence, unsigned channel) {
    return math_splitmix64_unit(procgen_key_step(procgen_key_at(d.seed, sequence), channel, 1));
}
static float particle_key_unit24(uint64_t key) {
    return (float)(math_splitmix64_mix(key) >> 40) * 0x1p-24f;
}
static unsigned particle_sheet_frame(const ParticleDescription &d, uint64_t sequence, double age, double lifetime) {
    const auto &sheet = d.sheet;
    unsigned offset = sheet.flags & SCENE3D_PARTICLE_SHEET_RANDOM
                          ? (unsigned)(particle_key_unit24(procgen_key_step(procgen_key_at(d.seed, sequence), 9, 1)) * sheet.count)
                          : 0;
    // a static/single-cell sheet does not need a float lifetime phase.
    if (sheet.count == 1 || !sheet.rate) return sheet.first + offset;
    float phase = sheet.flags & SCENE3D_PARTICLE_SHEET_LIFETIME
                      ? ((float)age / (float)lifetime) * (sheet.rate * (float)sheet.count)
                      : (float)age * sheet.rate;
    unsigned advance = (unsigned)floorf(phase) + offset;
    return sheet.first + (sheet.flags & SCENE3D_PARTICLE_SHEET_LOOP ? advance % sheet.count
                                                                 : (advance < sheet.count ? advance : sheet.count - 1));
}
static Scene3dVec particle_acceleration(const ParticleDescription &d, uint64_t sequence, double age) {
    if (!scene3d_particle_noise_enabled(d.noise))
        return d.gravity;
    float phase = (float)age * d.noise.frequency;
    unsigned interval = (unsigned)floorf(phase);
    float weight = phase - interval;
    weight = weight * weight * (3 - 2 * weight);
    uint64_t key = procgen_key_at(d.seed, sequence);
    uint64_t a = procgen_key_step(key, interval, 1), b = procgen_key_step(key, interval + 1, 1);
    float values[] = {d.gravity.x, d.gravity.y, d.gravity.z};
    for (unsigned c = 0; c < 3; c++) {
        // the same top-24-bit draw is exactly representable by GLSL 330 floats.
        auto draw = [&](uint64_t lattice) {
            return particle_key_unit24(procgen_key_step(lattice, c + 9, 2)) * 2 - 1;
        };
        float low = draw(a), high = draw(b);
        values[c] += d.noise.strength[c] * (low + (high - low) * weight);
    }
    return {values[0], values[1], values[2]};
}
static ParticleRecord particle_spawn(const ParticleDescription &d, uint64_t sequence) {
    ParticleRecord record = {};
    auto &p = record.value;
    record.surface_slot = d.surface_slot;
    p.sequence = sequence;
    p.lifetime = d.parameters[PARTICLE_LIFE_MIN] +
                 (d.parameters[PARTICLE_LIFE_MAX] - d.parameters[PARTICLE_LIFE_MIN]) * particle_random(d, sequence, 0);
    if (!d.gpu) {
        double z = 2 * particle_random(d, sequence, 1) - 1;
        double angle = 2 * acos(-1.0) * particle_random(d, sequence, 2);
        double radial = sqrt(fmax(0, 1 - z * z));
        Scene3dVec direction = {(float)(radial * cos(angle)), (float)z, (float)(radial * sin(angle))};
        p.velocity = scene3d_add(d.velocity, scene3d_scale(direction, (float)d.parameters[PARTICLE_SPREAD]));
        if (d.shape == PARTICLE_BOX) {
            p.position = {(float)((2 * particle_random(d, sequence, 3) - 1) * d.extent.x * .5),
                          (float)((2 * particle_random(d, sequence, 4) - 1) * d.extent.y * .5),
                          (float)((2 * particle_random(d, sequence, 5) - 1) * d.extent.z * .5)};
        } else if (d.shape == PARTICLE_SPHERE) {
            p.position = scene3d_scale(direction,
                                       (float)(d.parameters[PARTICLE_RADIUS] * cbrt(particle_random(d, sequence, 3))));
        } else if (d.shape == PARTICLE_CONE) {
            double cosine =
                1 - particle_random(d, sequence, 3) * (1 - cos(d.parameters[PARTICLE_ANGLE] * acos(-1.0) / 180));
            double sine = sqrt(fmax(0, 1 - cosine * cosine));
            Scene3dVec cone = {(float)(sine * cos(angle)), (float)cosine, (float)(sine * sin(angle))};
            p.velocity = scene3d_add(p.velocity, scene3d_scale(cone, (float)d.parameters[PARTICLE_SPEED]));
        } else if (d.shape == PARTICLE_MESH) {
            Scene3dVec normal;
            scene3d_particle_surface_sample(d.surface, particle_random(d, sequence, 6), particle_random(d, sequence, 7),
                                            particle_random(d, sequence, 8), &p.position, &normal);
            p.velocity = scene3d_add(p.velocity, scene3d_scale(normal, d.normal_speed));
        }
    }
    p.size = record.initial_size = (float)d.parameters[PARTICLE_SIZE];
    record.newborn = true;
    return record;
}
static bool particle_billboard_scale(const ParticleRecord &record, double size, float *values) {
    bool finite = true;
    for (unsigned axis = 0; axis < 2; axis++) {
        const float *column = record.frame.v + axis * 4;
        double value = hypot(hypot((double)column[0], column[1]), column[2]) * size;
        values[axis] = (float)value;
        finite &= isfinite(value) && value <= FLT_MAX;
    }
    values[2] = record.value.rotation.z;
    return finite;
}
static bool particle_pose(const ParticleDescription &d, ParticleRecord *record) {
    double sampled, rotation[3], color[3], opacity;
    double phase = record->value.age / record->value.lifetime;
    if (!particle_curve_sample(d.curves[SCENE3D_PARTICLE_SIZE_CURVE], 1, phase, &sampled) ||
        (!d.gpu && (!particle_curve_sample(d.curves[SCENE3D_PARTICLE_ROTATION_CURVE], 3, phase, rotation) ||
                    !particle_curve_sample(d.curves[SCENE3D_PARTICLE_COLOR_CURVE], 3, phase, color) ||
                    !particle_curve_sample(d.curves[SCENE3D_PARTICLE_OPACITY_CURVE], 1, phase, &opacity))))
        return false;
    if (!d.gpu) {
        record->value.rotation = {(float)rotation[0], (float)rotation[1], (float)rotation[2]};
        Scene3dVec transport;
        if (!particle_velocity_sample(d, record->value.age, record->value.lifetime, &transport)) return false;
        record->value.velocity = scene3d_add(record->dynamic_velocity, transport);
        if (!particle_vector_finite(record->value.velocity)) return false;
        for (unsigned c = 0; c < 3; c++) record->value.color[c] = (float)color[c];
        record->value.color[3] = (float)opacity;
        record->value.frame = particle_sheet_frame(d, record->value.sequence, record->value.age, record->value.lifetime);
    }
    double size = sampled * record->initial_size;
    if (!isfinite(size) || size > FLT_MAX)
        return false;
    float billboard[3];
    if (d.billboard && !particle_billboard_scale(*record, size, billboard)) return false;
    if (d.world) {
        // the retained affine basis also scales geometry; finite motion alone does not bound its vertices.
        for (unsigned row = 0; row < 3; row++) {
            double reach = 0;
            for (unsigned column = 0; column < 3; column++)
                reach += fabs(record->frame.v[column * 4 + row]);
            if (reach * size > FLT_MAX)
                return false;
        }
    }
    record->value.size = (float)size;
    return true;
}
struct ParticleContactCollector {
    ParticleEmitter *emitter;
    ParticlePlayback *playback;
    uint64_t particle, tick, world_identity;
    bool quiet;
    unsigned depth;
};
static bool particle_contact_collect(void *owner, const Scene3dParticleContact &contact, PhysicsError *error) {
    auto &collector = *(ParticleContactCollector *)owner;
    auto &p = *collector.playback;
    auto *emitter = collector.emitter;
    const char *failure = nullptr;
    if (p.collisions == UINT64_MAX || (contact.killed && p.killed == UINT64_MAX))
        failure = "particle collision: contact counter exhausted";
    bool enqueue = !collector.quiet && emitter->description.collision.events;
    if (!failure && enqueue &&
        (p.event_count == emitter->description.collision.event_capacity || p.event_sequence == UINT64_MAX))
        failure = "particle collision: retained contact queue capacity exhausted";
    if (failure) {
        if (error)
            error->message = failure;
        return false;
    }
    p.collisions++;
    if (contact.killed)
        p.killed++;
    if (enqueue) {
        auto &event = emitter->event_scratch[p.event_count++];
        event = {++p.event_sequence, collector.particle, collector.tick, collector.world_identity, contact, collector.depth};
    }
    return true;
}
static bool particle_sampled_frame(Scene3dParticles *state, ParticleEmitter *emitter,
                                   Scene3dParticleCollisionFrame *frame, uint64_t *tick, const char **diagnostic) {
    auto *root = dom_node_ref_validate(state->document, state->root);
    auto *source = dom_node_ref_validate(state->document, emitter->source);
    Scene3dRuntimeStats clock = {};
    if (!root || !source || !root->is_element() || !source->is_element() ||
        !scene3d_runtime_stats(state->replay_runtime ? state->replay_runtime : state->runtime, &clock) ||
        clock.ticks == UINT64_MAX) {
        *diagnostic = "particles: detached source or exhausted tick";
        return false;
    }
    frame->physics = state->replay_physics ? state->replay_physics : scene3d_physics(root->as_element());
    auto *animation = state->replay_animation ? state->replay_animation : scene3d_animations(root->as_element());
    bool visible;
    if (!frame->physics ||
        !scene3d_world_pose(root->as_element(), source->as_element(), animation, scene3d_override_physics_pose,
                            frame->physics, &frame->world, &visible) ||
        !scene3d_inverse_affine(frame->world, &frame->inverse)) {
        *diagnostic = "particles: requires an invertible sampled emitter frame";
        return false;
    }
    for (DomNode *node = source->parent; node; node = node->parent) {
        if (node->is_element() && (frame->excluded_body = scene3d_physics_body(frame->physics, node->as_element())))
            break;
    }
    *tick = clock.ticks + 1;
    return true;
}
static bool particle_emission_plan(const ParticleDescription &d, ParticlePlayback *playback, double timestep,
                                    unsigned extra, unsigned remaining, unsigned *authored, unsigned *spawns) {
    double next_time = playback->time + timestep;
    if (!isfinite(next_time) || next_time <= playback->time)
        return false;
    double active_time = d.parameters[PARTICLE_DURATION] && !d.looping
                             ? fmin(timestep, fmax(0, d.parameters[PARTICLE_DURATION] - playback->time))
                             : timestep;
    double credit = playback->credit + (d.emitting ? d.parameters[PARTICLE_RATE] * active_time : 0);
    double rate_count = floor(credit + 1e-10), cycles = 0;
    if (d.emitting && d.burst && d.looping && d.parameters[PARTICLE_DURATION] > 0) {
        double duration = d.parameters[PARTICLE_DURATION];
        cycles = floor(next_time / duration + 1e-10) - floor(playback->time / duration + 1e-10);
    }
    bool initial_burst = !playback->started && (!d.parameters[PARTICLE_DURATION] || d.looping ||
                                               playback->time < d.parameters[PARTICLE_DURATION]);
    double requested = rate_count + (d.emitting ? d.burst * (cycles + (initial_burst ? 1 : 0)) : 0);
    if (!isfinite(requested) || requested < 0 || requested > remaining) return false;
    *authored = (unsigned)requested;
    if (extra > remaining - *authored) return false;
    *spawns = *authored + extra;
    if (*spawns > UINT64_MAX - playback->sequence) return false;
    playback->credit = fmax(0, credit - rate_count);
    playback->started |= d.emitting; playback->time = next_time;
    return true;
}
static unsigned particle_pending_count(const Scene3dParticles *state, uint64_t identity) {
    unsigned count = 0;
    for (unsigned i = 0; i < state->pending.count; i++)
        if (state->pending.requests[i].emitter == identity) count += state->pending.requests[i].count;
    return count;
}
static bool particle_step(Scene3dParticles *state, ParticleEmitter *emitter, double timestep, unsigned *work,
                          Scene3dParticleCollisionWork *query_work, ParticlePlayback *result, const char **diagnostic) {
    auto fail = [&](const char *message) {
        *diagnostic = message;
        return false;
    };
    const auto &d = emitter->description;
    auto playback = emitter->playback;
    for (unsigned i = 0; i < playback.event_count; i++)
        emitter->event_scratch[i] = emitter->events[(playback.event_head + i) % d.collision.event_capacity];
    playback.event_head = 0;
    Scene3dParticleCollisionFrame collision_frame = {};
    uint64_t tick = 0;
    if ((d.world || d.collision.enabled) &&
        !particle_sampled_frame(state, emitter, &collision_frame, &tick, diagnostic))
        return false;
    Scene3dMatrix birth_frame = d.world ? collision_frame.world : scene3d_identity();
    if (d.world) {
        // stored world poses never inherit the emitter's later movement, including during collision response.
        collision_frame.world = collision_frame.inverse = scene3d_identity();
    }
    ParticleContactCollector contacts = {
        emitter, &playback, 0, tick, scene3d_physics_world_identity(collision_frame.physics), !state->bound};
    auto collide = [&](ParticleRecord *record, double delta, bool *alive) {
        if (!d.collision.enabled) {
            *alive = true;
            return true;
        }
        contacts.particle = record->value.sequence;
        contacts.depth = record->value.depth;
        PhysicsError error = {};
        Scene3dVec transport;
        if (!particle_velocity_sample(d, fmax(0, record->value.age - delta), record->value.lifetime, &transport))
            return fail("particle velocity: invalid collision transport");
        record->value.velocity = scene3d_add(record->dynamic_velocity, transport);
        auto gravity = particle_acceleration(d, record->value.sequence, fmax(0, record->value.age - delta));
        if (scene3d_particle_collide(d.collision, collision_frame, &record->value, gravity,
                                     d.parameters[PARTICLE_DRAG], delta, query_work, particle_contact_collect,
                                     &contacts, alive, &error, transport)) {
            record->dynamic_velocity = scene3d_sub(record->value.velocity, transport);
            return true;
        }
        return fail(error.message ? error.message : "particle collision: native query failed");
    };
    auto query_before = *query_work;
    unsigned authored, spawns;
    if (!particle_emission_plan(d, &playback, timestep, particle_pending_count(state, emitter->identity),
                                PARTICLE_SPAWN_LIMIT - *work, &authored, &spawns))
        return fail("particles: nonfinite emission, exhausted sequence or scene spawn work exceeds 4096 attempts per tick");
    *work += spawns;
    // fixed-tick births occur after existing particles age/expire; compaction preserves spawn order.
    unsigned kept = 0;
    uint64_t occupied[SCENE3D_INSTANCE_LIMIT / 64] = {};
    auto motion = scene3d_particle_motion(d.parameters[PARTICLE_DRAG], timestep);
    for (unsigned i = 0; i < playback.count; i++) {
        ParticleRecord record = emitter->particles[(playback.head + i) % d.capacity];
        auto &p = record.value;
        p.age += timestep;
        if (p.age >= p.lifetime || p.lifetime - p.age <= timestep * 1e-10)
            continue;
        record.newborn = false;
        if (d.collision.enabled) {
            bool alive;
            if (!collide(&record, timestep, &alive))
                return false;
            if (!alive)
                continue;
        } else if (!d.gpu) {
            PhysicsVec position, velocity;
            Scene3dVec transport;
            if (!particle_velocity_sample(d, p.age - timestep, p.lifetime, &transport))
                return fail("particle velocity: invalid fixed-tick transport");
            auto gravity = particle_acceleration(d, p.sequence, p.age - timestep);
            if (!scene3d_particle_integrate({p.position.x, p.position.y, p.position.z},
                                            {record.dynamic_velocity.x, record.dynamic_velocity.y, record.dynamic_velocity.z},
                                            {gravity.x, gravity.y, gravity.z}, motion, &position, &velocity))
                return fail("particles: motion exceeds finite simulation coordinates");
            position = padd(position, pmul({transport.x, transport.y, transport.z}, timestep));
            for (unsigned c = 0; c < 3; c++)
                if (fabs(paxis(position, c)) > FLT_MAX || fabs(paxis(velocity, c)) > FLT_MAX)
                    return fail("particles: motion exceeds finite simulation coordinates");
            p.position = {(float)position.x, (float)position.y, (float)position.z};
            record.dynamic_velocity = {(float)velocity.x, (float)velocity.y, (float)velocity.z};
        }
        if (!particle_pose(d, &record))
            return fail("particles: pose curves exceed finite native values");
        if (record.slot >= d.capacity || (occupied[record.slot / 64] & (UINT64_C(1) << (record.slot % 64))))
            return fail("particles: invalid native slot ownership");
        occupied[record.slot / 64] |= UINT64_C(1) << (record.slot % 64);
        emitter->scratch[kept++] = record;
    }
    playback.head = 0;
    playback.count = kept;
    const ParticleSpawnRequest *requests[PARTICLE_COMMAND_LIMIT];
    unsigned request_count = 0, request_index = 0, request_end = authored;
    for (unsigned i = 0; i < state->pending.count; i++)
        if (state->pending.requests[i].emitter == emitter->identity) requests[request_count++] = state->pending.requests + i;
    if (request_count) request_end += requests[0]->count;
    Scene3dMatrix basis = birth_frame, inverse_basis = scene3d_identity();
    basis.v[12] = basis.v[13] = basis.v[14] = 0;
    if (d.gpu && request_count && !scene3d_inverse_affine(basis, &inverse_basis))
        return fail("particle commands: birth basis is not invertible");
    unsigned next_slot = 0;
    for (unsigned i = 0; i < spawns; i++) {
        const ParticleSpawnRequest *request = nullptr;
        if (i >= authored) {
            while (i >= request_end && request_index + 1 < request_count) request_end += requests[++request_index]->count;
            request = requests[request_index];
        }
        uint64_t sequence = playback.sequence++;
        if (playback.count == d.capacity && d.overflow == PARTICLE_ERROR)
            return fail("particles: authored capacity exhausted");
        if (playback.count == d.capacity && d.overflow == PARTICLE_DROP) {
            if (playback.dropped == UINT64_MAX)
                return fail("particles: dropped counter exhausted");
            playback.dropped++;
            continue;
        }
        unsigned recycled_slot = d.capacity;
        if (playback.count == d.capacity) {
            recycled_slot = emitter->scratch[playback.head].slot;
            if (playback.recycled == UINT64_MAX)
                return fail("particles: recycled counter exhausted");
            playback.recycled++;
            playback.head = (playback.head + 1) % d.capacity;
            playback.count--;
        }
        if (playback.spawned == UINT64_MAX)
            return fail("particles: spawned counter exhausted");
        auto record = particle_spawn(d, sequence);
        record.frame = birth_frame;
        if (request) {
            record.value.depth = request->depth;
            if (request->positioned) {
                record.frame.v[12] = request->position.x; record.frame.v[13] = request->position.y;
                record.frame.v[14] = request->position.z;
            }
            // keep GPU birth recipes immutable while adding velocity in simulation-space axes.
            if (d.gpu) record.spawn_velocity = scene3d_point(inverse_basis, request->velocity, 0);
        }
        if (d.world && playback.count &&
            (scene3d_determinant(birth_frame.v) < 0) !=
                (scene3d_determinant(emitter->scratch[playback.head].frame.v) < 0))
            return fail("particles: one emitter batch cannot mix birth-frame winding");
        if (!d.gpu) {
            if (d.world || (request && request->positioned)) record.value.position = scene3d_point(record.frame, record.value.position);
            if (d.world) record.value.velocity = scene3d_point(record.frame, record.value.velocity, 0);
            if (request) record.value.velocity = scene3d_add(record.value.velocity, request->velocity);
        }
        record.dynamic_velocity = record.value.velocity;
        {
            if (recycled_slot < d.capacity)
                record.slot = recycled_slot;
            else {
                while (next_slot < d.capacity && (occupied[next_slot / 64] & (UINT64_C(1) << (next_slot % 64))))
                    next_slot++;
                if (next_slot == d.capacity)
                    return fail("particles: no admitted free slot");
                record.slot = next_slot;
            }
            occupied[record.slot / 64] |= UINT64_C(1) << (record.slot % 64);
        }
        if (!particle_vector_finite(record.value.position) || !particle_vector_finite(record.value.velocity) ||
            !particle_pose(d, &record))
            return fail("particles: spawn exceeds finite native values");
        bool alive;
        if (!collide(&record, 0, &alive))
            return false;
        if (alive)
            emitter->scratch[(playback.head + playback.count++) % d.capacity] = record;
        else
            occupied[record.slot / 64] &= ~(UINT64_C(1) << (record.slot % 64));
        if (!alive && record.slot < next_slot) next_slot = record.slot;
        playback.spawned++;
    }
    const size_t added[] = {query_work->queries - query_before.queries, query_work->geometry - query_before.geometry,
                            query_work->triangles - query_before.triangles};
    uint64_t *totals[] = {&playback.queries, &playback.geometry_work, &playback.triangle_work};
    for (unsigned i = 0; i < 3; i++) {
        if (added[i] > UINT64_MAX - *totals[i])
            return fail("particle collision: query counter exhausted");
        *totals[i] += added[i];
    }
    *result = playback;
    return true;
}
static Scene3dParticleGpuControls particle_gpu_controls(const ParticleDescription &d) {
    Scene3dParticleGpuControls controls = {};
    controls.gravity[0] = d.gravity.x;
    controls.gravity[1] = d.gravity.y;
    controls.gravity[2] = d.gravity.z;
    controls.drag = (float)d.parameters[PARTICLE_DRAG];
    for (unsigned i = 0; i < 4; i++)
        controls.seed[i] = (float)((d.seed >> (i * 16)) & UINT64_C(65535));
    controls.collision = d.analytic;
    controls.noise = d.noise;
    controls.sheet = d.sheet;
    for (unsigned kind = 0; kind < SCENE3D_PARTICLE_CURVES; kind++) {
        const auto &curve = d.curves[kind];
        controls.curve_counts[kind] = curve.count;
        for (unsigned i = 0; i < curve.count; i++) {
            controls.curves[kind][i][0] = (float)curve.times[i];
            for (unsigned c = 0; c < particle_curve_specs[kind].components; c++)
                controls.curves[kind][i][c + 1] = (float)curve.values[i * particle_curve_specs[kind].components + c];
        }
    }
    return controls;
}
struct ParticleGpuBounds {
    Scene3dVec low, high;
    float size;
    bool empty;
};
static bool particle_gpu_stage(ParticleEmitter *emitter, const ParticlePlayback &playback, double timestep,
                               ParticleGpuBounds *bounds) {
    const auto &d = emitter->description;
    auto controls = particle_gpu_controls(d);
    bool births_changed = false;
    for (unsigned i = 0; i < playback.count; i++)
        births_changed |= emitter->scratch[(playback.head + i) % d.capacity].newborn;
    Scene3dParticleGpuUpload upload = {};
    if (!scene3d_particle_gpu_upload(emitter->gpu, births_changed, &upload))
        return false;
    double rounding = 16 * FLT_EPSILON;
    if (scene3d_particle_gpu_stateful(controls)) {
        Scene3dParticleGpuStats stats;
        if (!scene3d_particle_gpu_stats(emitter->gpu, &stats)) return false;
        // stateful kernels accumulate float sums; gamma_n encloses the retained update chain.
        double error = 16 * (stats.history_ticks + 1) * FLT_EPSILON;
        rounding = error / (1 - error);
    }
    *bounds = {{}, {}, 0, true};
    double max_curve = 0;
    for (unsigned i = 0; i < d.curves[SCENE3D_PARTICLE_SIZE_CURVE].count; i++)
        max_curve = fmax(max_curve, d.curves[SCENE3D_PARTICLE_SIZE_CURVE].values[i]);
    const double acceleration[] = {fabs(d.gravity.x) + (double)d.noise.strength[0],
                                   fabs(d.gravity.y) + (double)d.noise.strength[1],
                                   fabs(d.gravity.z) + (double)d.noise.strength[2]};
    double transport[3] = {}, variation[3] = {};
    const auto &velocity_curve = d.curves[SCENE3D_PARTICLE_VELOCITY_CURVE];
    for (unsigned i = 0; i < velocity_curve.count; i++)
        for (unsigned c = 0; c < 3; c++) {
            transport[c] = fmax(transport[c], fabs(velocity_curve.values[i * 3 + c]));
            if (i) variation[c] += fabs(velocity_curve.values[i * 3 + c] - velocity_curve.values[(i - 1) * 3 + c]);
        }
    for (unsigned i = 0; i < playback.count; i++) {
        const auto &record = emitter->scratch[(playback.head + i) % d.capacity];
        const auto &sample = record.value;
        auto &birth = upload.births[record.slot];
        if (record.newborn) {
            birth = {};
            for (unsigned word = 0; word < 4; word++)
                birth.sequence[word] = (float)((sample.sequence >> (word * 16)) & UINT64_C(65535));
            birth.shape_extent[0] = (float)d.shape;
            birth.shape_extent[1] = d.extent.x;
            birth.shape_extent[2] = d.extent.y;
            birth.shape_extent[3] = d.extent.z;
            if (d.surface) {
                birth.shape_extent[1] = (float)record.surface_slot;
                birth.shape_extent[2] = (float)d.surface->count;
                birth.shape_extent[3] = d.normal_speed;
            }
            auto velocity = scene3d_add(d.velocity, record.spawn_velocity);
            birth.velocity_radius[0] = velocity.x;
            birth.velocity_radius[1] = velocity.y;
            birth.velocity_radius[2] = velocity.z;
            birth.velocity_radius[3] = (float)d.parameters[PARTICLE_RADIUS];
            birth.cone_spread_anchor[0] = (float)(d.parameters[PARTICLE_ANGLE] * acos(-1.0) / 180);
            birth.cone_spread_anchor[1] = (float)d.parameters[PARTICLE_SPEED];
            birth.cone_spread_anchor[2] = (float)d.parameters[PARTICLE_SPREAD];
            for (unsigned row = 0; row < 3; row++)
                for (unsigned column = 0; column < 4; column++)
                    birth.frame[row][column] = record.frame.v[column * 4 + row];
        }
        upload.metadata[record.slot] = {(float)sample.age, (float)sample.lifetime, record.initial_size,
                                       record.newborn ? 2.0f : 1.0f};
        double elapsed = fmax(0, (double)(float)sample.age - birth.anchor_position_age[3]);
        double scale = record.initial_size * max_curve * (1 + 16 * FLT_EPSILON);
        if (!isfinite(scale) || scale > FLT_MAX)
            return false;
        double basis_scale = 1;
        if (d.world) {
            basis_scale = 0;
            for (unsigned row = 0; row < 3; row++) {
                double extent = 0;
                for (unsigned column = 0; column < 3; column++)
                    extent += fabs(birth.frame[row][column]);
                basis_scale = fmax(basis_scale, extent);
            }
        }
        if (!isfinite(scale * basis_scale) || scale * basis_scale > FLT_MAX)
            return false;
        bounds->size = fmaxf(bounds->size, (float)(scale * basis_scale));
        double shape_center[3] = {}, shape_radius[3] = {}, shape_speed[3];
        const Scene3dParticleSurface *surface = nullptr;
        if (birth.shape_extent[0] == PARTICLE_MESH) {
            if (record.surface_slot >= SCENE3D_PARTICLE_SURFACE_VERSIONS ||
                !(surface = emitter->surfaces.values[record.surface_slot]))
                return false;
            const float lo[] = {surface->lo.x, surface->lo.y, surface->lo.z};
            const float hi[] = {surface->hi.x, surface->hi.y, surface->hi.z};
            for (unsigned c = 0; c < 3; c++) {
                shape_center[c] = (lo[c] + (double)hi[c]) * .5;
                shape_radius[c] = (hi[c] - (double)lo[c]) * .5;
            }
        }
        for (unsigned c = 0; c < 3; c++) {
            shape_speed[c] = fabs(birth.velocity_radius[c]) + birth.cone_spread_anchor[2] +
                             (surface ? fabs(birth.shape_extent[3]) : 0);
            if (birth.shape_extent[0] == PARTICLE_BOX)
                shape_radius[c] = birth.shape_extent[c + 1] * .5;
            if (birth.shape_extent[0] == PARTICLE_SPHERE)
                shape_radius[c] = birth.velocity_radius[3];
            if (birth.shape_extent[0] == PARTICLE_CONE)
                shape_speed[c] += birth.cone_spread_anchor[1];
        }
        float low[3], high[3];
        for (unsigned c = 0; c < 3; c++) {
            double center = birth.frame[c][3], radius = 0, speed = 0;
            for (unsigned axis = 0; axis < 3; axis++) {
                center += birth.frame[c][axis] * shape_center[axis];
                radius += fabs(birth.frame[c][axis]) * shape_radius[axis];
                speed += fabs(birth.frame[c][axis]) * shape_speed[axis];
            }
            if (birth.cone_spread_anchor[3] > .5f) {
                center = birth.anchor_position_age[c];
                radius = 0;
                speed = fabs(birth.anchor_velocity[c]);
            }
            if (d.analytic.count) {
                // static impacts redirect velocity between axes; its norm bounds every reflected component.
                double speed_bound = 0, gravity_bound = 0, transport_bound = 0;
                for (unsigned axis = 0; axis < 3; axis++) {
                    double component = 0;
                    for (unsigned local = 0; local < 3; local++)
                        component += fabs(birth.frame[axis][local]) * shape_speed[local];
                    if (birth.cone_spread_anchor[3] > .5f)
                        component = fabs(birth.anchor_velocity[axis]);
                    speed_bound += component * component;
                    gravity_bound += acceleration[axis] * acceleration[axis];
                    double envelope = transport[axis] + variation[axis];
                    transport_bound += envelope * envelope;
                }
                // reflected impulses retain authored offsets; their full curve variation bounds redirected speed.
                speed = sqrt(speed_bound) + sqrt(transport_bound);
                radius += speed * elapsed + sqrt(gravity_bound) * elapsed * elapsed * .5 +
                          d.analytic.skin * d.analytic.impacts * SCENE3D_PARTICLE_HISTORY_LIMIT;
            } else
                radius += (speed + transport[c]) * elapsed + acceleration[c] * elapsed * elapsed * .5;
            // outward rounding keeps float device arithmetic inside the native visibility bound.
            radius += (fabs(center) + radius + 1) * rounding;
            if (!isfinite(radius) || fabs(center) + radius > FLT_MAX ||
                speed + transport[c] + acceleration[c] * elapsed > FLT_MAX)
                return false;
            low[c] = (float)(center - radius);
            high[c] = (float)(center + radius);
        }
        // a zero-size head still contributes device positions to its visible trail.
        if (sample.size || emitter->trail) {
            Scene3dVec lo = {low[0], low[1], low[2]}, hi = {high[0], high[1], high[2]};
            if (bounds->empty) {
                bounds->low = lo;
                bounds->high = hi;
                bounds->empty = false;
            } else {
                scene3d_include(lo, &bounds->low, &bounds->high);
                scene3d_include(hi, &bounds->low, &bounds->high);
            }
        }
    }
    return scene3d_particle_gpu_stage(emitter->gpu, controls, timestep);
}
static void particle_publish_trail(ParticleEmitter *emitter) {
    auto *trail = emitter->trail;
    if (!trail) return;
    if (trail->styles) memset(trail->styles, 0, trail->records * 12 * sizeof(float));
    for (unsigned i = 0; i < emitter->playback.count; i++) {
        const auto &record = emitter->particles[(emitter->playback.head + i) % emitter->description.capacity];
        const auto &p = record.value;
        trail->slots[i] = (float)record.slot;
        if (trail->gpu) continue;
        float *style = trail->styles + record.slot * 12;
        style[0] = p.position.x; style[1] = p.position.y; style[2] = p.position.z; style[3] = 1;
        style[7] = (float)p.frame;
        memcpy(style + 8, p.color, sizeof(p.color));
    }
}
static void particle_publish_matrices(ParticleEmitter *emitter) {
    emitter->matrix_count = 0;
    emitter->sheet = emitter->description.sheet;
    emitter->lo = emitter->hi = {};
    // bounds describe the committed device pose, even while an edited curve awaits its first tick.
    emitter->rotating = emitter->fading = false;
    particle_publish_trail(emitter);
    const auto &opacity = emitter->description.curves[SCENE3D_PARTICLE_OPACITY_CURVE];
    for (unsigned i = 0; i < opacity.count; i++) emitter->fading |= opacity.values[i] < 1;
    const auto &rotation = emitter->description.curves[SCENE3D_PARTICLE_ROTATION_CURVE];
    for (unsigned i = 0; i < rotation.count * 3; i++)
        emitter->rotating |= rotation.values[i] != 0;
    for (unsigned i = 0; i < emitter->playback.count; i++) {
        const auto &record = emitter->particles[(emitter->playback.head + i) % emitter->description.capacity];
        const auto &p = record.value;
        if (!p.size)
            continue;
        if (emitter->gpu) {
            emitter->slots[emitter->matrix_count++] =
                (float)emitter->particles[(emitter->playback.head + i) % emitter->description.capacity].slot;
            continue;
        }
        emitter->frames[emitter->matrix_count] = p.frame;
        particle_billboard_scale(record, p.size, emitter->billboards + emitter->matrix_count * 3);
        memcpy(emitter->colors + emitter->matrix_count * 4, p.color, sizeof(p.color));
        auto &matrix = emitter->matrices[emitter->matrix_count++];
        // rotation is inside the frozen affine birth basis, including nonuniform scale/shear.
        matrix = scene3d_multiply(record.frame, scene3d_transform({}, p.rotation, {p.size, p.size, p.size}));
        matrix.v[12] = p.position.x;
        matrix.v[13] = p.position.y;
        matrix.v[14] = p.position.z;
        if (emitter->matrix_count == 1)
            emitter->lo = emitter->hi = p.position;
        else
            scene3d_include(p.position, &emitter->lo, &emitter->hi);
    }
}
static unsigned particle_surface_live_mask(const ParticleEmitter *emitter) {
    unsigned mask = 0;
    for (unsigned i = 0; i < emitter->playback.count; i++) {
        unsigned slot = emitter->particles[(emitter->playback.head + i) % emitter->description.capacity].surface_slot;
        if (slot < SCENE3D_PARTICLE_SURFACE_VERSIONS)
            mask |= 1u << slot;
    }
    return mask;
}
static void particle_surfaces_prune(ParticleEmitter *emitter) {
    unsigned mask = particle_surface_live_mask(emitter);
    if (emitter->description.surface)
        mask |= 1u << emitter->description.surface_slot;
    for (unsigned slot = 0; slot < SCENE3D_PARTICLE_SURFACE_VERSIONS; slot++) {
        if (!emitter->surfaces.values[slot] || (mask & (1u << slot)))
            continue;
        // invalidate the upload cache before retiring a table; old active births keep their slot alive.
        scene3d_particle_gpu_surface_changed(emitter->gpu, slot);
        scene3d_particle_surface_release(emitter->surfaces.values[slot]);
        emitter->surfaces.values[slot] = nullptr;
    }
}
static Scene3dTickResult particles_tick(void *owner, double timestep) {
    auto *state = (Scene3dParticles *)owner;
    if (!particles_active(state))
        return SCENE3D_TICK_IDLE;
    if (state->generation == UINT64_MAX)
        return SCENE3D_TICK_FAILED;
    ParticlePlayback staged[PARTICLE_EMITTER_LIMIT];
    ParticleGpuBounds bounds[PARTICLE_EMITTER_LIMIT];
    bool updated[PARTICLE_EMITTER_LIMIT] = {};
    unsigned work = 0;
    Scene3dParticleCollisionWork query_work = {};
    for (unsigned i = 0; i < state->count; i++) {
        // a peer must not consume an idle emitter's finite emission window.
        staged[i] = state->emitters[i]->playback;
        if (!particle_active(state->emitters[i]) && !particle_pending_count(state, state->emitters[i]->identity))
            continue;
        if (!particle_step(state, state->emitters[i], timestep, &work, &query_work, staged + i, &state->diagnostic))
            return SCENE3D_TICK_FAILED;
        updated[i] = true;
        auto *emitter = state->emitters[i];
        scene3d_particle_trail_begin(emitter->trail);
        if (emitter->trail && !emitter->gpu) for (unsigned j = 0; j < staged[i].count; j++) {
            const auto &record = emitter->scratch[(staged[i].head + j) % emitter->description.capacity];
            scene3d_particle_trail_append(emitter->trail, record.slot, record.value.position, record.newborn, timestep);
        }
    }
    for (unsigned i = 0; i < state->count; i++) {
        if (updated[i] && state->emitters[i]->gpu &&
            !particle_gpu_stage(state->emitters[i], staged[i], timestep, bounds + i)) {
            state->diagnostic = scene3d_particle_gpu_diagnostic(state->emitters[i]->gpu);
            if (!state->diagnostic) state->diagnostic = "GPU particles: bounded device stage failed";
            for (unsigned j = 0; j < state->count; j++)
                scene3d_particle_gpu_discard(state->emitters[j]->gpu);
            return SCENE3D_TICK_FAILED;
        }
        auto *emitter = state->emitters[i];
        if (updated[i] && emitter->gpu && emitter->trail) {
            auto *trail = emitter->trail;
            trail->bounds[1][(trail->head + 1) % trail->samples] = {bounds[i].low, bounds[i].high, !bounds[i].empty};
        }
    }
    // no live particle state or instance buffer changes until every emitter's bounded step succeeds.
    for (unsigned i = 0; i < state->count; i++) {
        auto *emitter = state->emitters[i];
        if (!updated[i])
            continue;
        if (emitter->gpu)
            scene3d_particle_gpu_commit(emitter->gpu);
        scene3d_particle_trail_commit(emitter->trail);
        auto *retired = emitter->particles;
        emitter->particles = emitter->scratch;
        emitter->scratch = retired;
        auto *retired_events = emitter->events;
        emitter->events = emitter->event_scratch;
        emitter->event_scratch = retired_events;
        emitter->playback = staged[i];
        particle_surfaces_prune(emitter);
        particle_publish_matrices(emitter);
        if (emitter->gpu) {
            emitter->lo = bounds[i].low;
            emitter->hi = bounds[i].high;
            emitter->max_size = bounds[i].size;
        }
    }
    state->pending.count = state->pending_spawns = 0;
    state->generation++;
    state->diagnostic = nullptr;
    if (state->bound && state->document->state)
        doc_state_request_repaint(state->document->state);
    return particles_active(state) ? SCENE3D_TICK_ACTIVE : SCENE3D_TICK_IDLE;
}
static const char *particle_commands_apply(void *owner, const void *payload) {
    auto *state = (Scene3dParticles *)owner;
    const auto &batch = *(const ParticleSpawnBatch *)payload;
    if (!state->bound || !dom_node_ref_validate(state->document, state->root) || state->generation == UINT64_MAX)
        return "particle commands: unavailable owner or exhausted snapshot generation";
    if (batch.count > PARTICLE_COMMAND_LIMIT - state->pending.count)
        return "particle commands: due request storage exceeds 128 records";
    unsigned extra[PARTICLE_EMITTER_LIMIT] = {};
    for (unsigned i = 0; i < batch.count; i++) {
        auto *emitter = particle_emitter_identity(state, batch.requests[i].emitter);
        if (!emitter) return "particle commands: stale emitter identity";
        for (unsigned j = 0; j < state->count; j++)
            if (state->emitters[j] == emitter) extra[j] += batch.requests[i].count;
    }
    Scene3dRuntimeStats clock = {};
    if (!scene3d_runtime_stats(state->runtime, &clock)) return "particle commands: unavailable clock";
    unsigned work = 0, requested = 0;
    for (unsigned i = 0; i < state->count; i++) {
        auto *emitter = state->emitters[i];
        unsigned count = extra[i] + particle_pending_count(state, emitter->identity);
        requested += extra[i];
        if (!particle_active(emitter) && !count) continue;
        auto playback = emitter->playback;
        unsigned authored, spawns;
        if (!particle_emission_plan(emitter->description, &playback, clock.timestep, count,
                                    PARTICLE_SPAWN_LIMIT - work, &authored, &spawns))
            return "particle commands: next fixed tick exceeds finite emission or 4096 spawn attempts";
        work += spawns;
    }
    // receipts admit copied requests; only the later particle phase publishes and consumes births.
    if (!scene3d_runtime_enable(state->runtime, SCENE3D_TICK_PARTICLES, state, state->automatic))
        return "particle commands: native scheduling failed";
    memcpy(state->pending.requests + state->pending.count, batch.requests, batch.count * sizeof(*batch.requests));
    state->pending.count += batch.count; state->pending_spawns += requested; state->generation++;
    return nullptr;
}
Scene3dParticles *scene3d_particles_create(DomElement *root, Scene3dRuntime *runtime,
                                           const Scene3dParticleGpuOps &gpu) {
    if (!root || !runtime)
        return nullptr;
    auto *state = (Scene3dParticles *)mem_calloc(1, sizeof(Scene3dParticles), MEM_CAT_RENDER);
    if (!state)
        return nullptr;
    state->document = root->doc;
    state->root = dom_node_ref(root);
    state->runtime = runtime;
    state->gpu_ops = gpu;
    state->epoch = UINT64_MAX;
    state->generation = 1;
    if (!scene3d_runtime_bind(runtime, SCENE3D_TICK_PARTICLES, state, particles_tick)) {
        mem_free(state);
        return nullptr;
    }
    state->bound = true;
    if (!scene3d_runtime_bind_commands(runtime, SCENE3D_TICK_PARTICLES, state, particle_commands_apply)) {
        scene3d_particles_destroy(state); return nullptr;
    }
    return state;
}
void scene3d_particles_destroy(Scene3dParticles *state) {
    if (!state)
        return;
    scene3d_particles_replay_detach(state);
    if (state->bound)
        scene3d_runtime_unbind(state->runtime, SCENE3D_TICK_PARTICLES, state);
    for (unsigned i = 0; i < state->count; i++)
        particle_emitter_destroy(state->emitters[i]);
    mem_free(state);
}
struct ParticleSources {
    DomElement *nodes[PARTICLE_EMITTER_LIMIT];
    unsigned count = 0;
};
static Scene3dVisit particle_source_visit(DomElement *node, void* context) {
    auto* sources = (ParticleSources*)context;
    if (!strcmp(node->local_name(), "particle-emitter")) {
        if (sources->count == PARTICLE_EMITTER_LIMIT)
            return SCENE3D_VISIT_ERROR;
        auto *parent = node->parent && node->parent->is_element() ? node->parent->as_element() : nullptr;
        if (!parent || (strcmp(parent->local_name(), "scene3d") && strcmp(parent->local_name(), "group") &&
                        strcmp(parent->local_name(), "rigid-body")))
            return SCENE3D_VISIT_ERROR;
        sources->nodes[sources->count++] = node;
        return SCENE3D_VISIT_SKIP;
    }
    return SCENE3D_VISIT_CHILDREN;
}
static bool particle_sources(DomElement* node, ParticleSources* sources) {
    return scene3d_visit(node, particle_source_visit, sources);
}
bool scene3d_particles_prepare(Scene3dParticles *state, DomElement *root, bool automatic, char *diagnostic,
                               size_t capacity) {
    Scene3dRuntimeStats current = {};
    if (!state || !root || state->document != root->doc || state->root.address != root ||
        state->root.expected_id != root->DomNode::id || !scene3d_runtime_stats(state->runtime, &current) ||
        !scene3d_runtime_can_configure(state->runtime, current.timestep, 1))
        return false;
    if (state->epoch == root->doc->mutation_epoch && state->automatic == automatic)
        return true;
    ParticleSources sources;
    if (!particle_sources(root, &sources))
        return particle_fail(diagnostic, capacity, "particles: invalid placement or source quota");
    ParticleDescription descriptions[PARTICLE_EMITTER_LIMIT] = {};
    struct Staged {
        Pool *pool = mem_pool_create(nullptr, MEM_ROLE_TEMP, "scene3d particle descriptions");
        ParticleEmitter *created[PARTICLE_EMITTER_LIMIT] = {};
        Scene3dParticleTrailState *trails[PARTICLE_EMITTER_LIMIT] = {};
        Scene3dParticleSurface *decoded[PARTICLE_EMITTER_LIMIT] = {}, *acquired[PARTICLE_EMITTER_LIMIT] = {};
        ~Staged() {
            for (auto *surface : decoded)
                scene3d_particle_surface_release(surface);
            for (auto *surface : acquired)
                scene3d_particle_surface_release(surface);
            for (auto *emitter : created)
                particle_emitter_destroy(emitter);
            for (auto *trail : trails) scene3d_particle_trail_destroy(trail);
            mem_pool_destroy(pool);
        }
    } staged;
    if (!staged.pool)
        return false;
    uint64_t emitter_sequence = state->emitter_sequence;
    ParticleEmitter *selected[PARTICLE_EMITTER_LIMIT] = {};
    unsigned total = 0, total_events = 0, total_trail_points = 0;
    bool changed = state->count != sources.count, active = false;
    for (unsigned i = 0; i < sources.count; i++) {
        auto *node = sources.nodes[i];
        const char *id = scene3d_text(node, "id");
        char description_error[256] = {};
        if (!id || !*id || strlen(id) >= sizeof(ParticleEmitter::id) ||
            !particle_description(root, node, staged.pool, descriptions + i, description_error,
                                  sizeof(description_error)))
            return particle_fail(diagnostic, capacity,
                                 *description_error ? description_error : "particles: invalid emitter description");
        staged.decoded[i] = descriptions[i].surface;
        for (unsigned j = 0; j < i; j++)
            if (!strcmp(id, scene3d_text(sources.nodes[j], "id")))
                return particle_fail(diagnostic, capacity, "particles: duplicate emitter id");
        total += descriptions[i].capacity;
        unsigned trail_points = scene3d_particle_trail_records(descriptions[i].capacity) * descriptions[i].trail.samples;
        if (trail_points > SCENE3D_PARTICLE_TRAIL_POINT_LIMIT - total_trail_points)
            return particle_fail(diagnostic, capacity, "particle trail: scene point storage exceeds 1048576 slots");
        total_trail_points += trail_points;
        if (total > PARTICLE_STORAGE_LIMIT)
            return particle_fail(diagnostic, capacity, "particles: scene storage quota exceeded");
        total_events += descriptions[i].collision.event_capacity;
        if (total_events > PARTICLE_EVENT_STORAGE_LIMIT)
            return particle_fail(diagnostic, capacity, "particles: scene contact storage quota exceeded");
        auto *before = particle_emitter_find(state, node);
        bool retain = before && before->description.capacity == descriptions[i].capacity &&
                      before->description.seed == descriptions[i].seed &&
                      before->description.gpu == descriptions[i].gpu &&
                      before->description.world == descriptions[i].world &&
                      before->description.collision.event_capacity == descriptions[i].collision.event_capacity;
        selected[i] = retain ? before : (staged.created[i] = particle_emitter_create(descriptions[i], state));
        if (!selected[i])
            return particle_fail(diagnostic, capacity, "particles: native allocation failed");
        if (!retain) {
            // equivalent templates keep the native identity; newly owned storage consumes the next token.
            if (emitter_sequence == UINT64_MAX) return particle_fail(diagnostic, capacity, "particles: emitter identity exhausted");
            selected[i]->identity = ++emitter_sequence;
        }
        if (retain && before->description.trail.samples != descriptions[i].trail.samples && descriptions[i].trail.samples) {
            staged.trails[i] = scene3d_particle_trail_create(descriptions[i].capacity, descriptions[i].trail.samples, descriptions[i].gpu);
            if (!staged.trails[i]) return particle_fail(diagnostic, capacity, "particle trail: native allocation failed");
        }
        // a compatible lifetime edit only affects new births; old records still
        // need exact neighboring noise-lattice indices in the admitted float domain.
        for (unsigned j = 0; j < selected[i]->playback.count; j++) {
            const auto &record = selected[i]->particles[(selected[i]->playback.head + j) % descriptions[i].capacity];
            float billboard[3];
            if (descriptions[i].billboard && !particle_billboard_scale(record, record.value.size, billboard))
                return particle_fail(diagnostic, capacity, "particle billboard: retained scale exceeds float coordinates");
            if (!particle_sheet_domain(descriptions[i].sheet, record.value.lifetime))
                return particle_fail(diagnostic, capacity, "particle sheet: retained lifetime exceeds frame-domain limits");
            if (record.value.lifetime * descriptions[i].noise.frequency > 65535)
                return particle_fail(diagnostic, capacity, "particle noise: retained lifetime exceeds 65535 intervals");
        }
        if (descriptions[i].surface) {
            auto *emitter = selected[i];
            unsigned live = particle_surface_live_mask(emitter), chosen = SCENE3D_PARTICLE_SURFACE_VERSIONS;
            for (unsigned slot = 0; slot < SCENE3D_PARTICLE_SURFACE_VERSIONS; slot++)
                if (scene3d_particle_surface_same(emitter->surfaces.values[slot], descriptions[i].surface)) {
                    chosen = slot;
                    break;
                }
            if (chosen == SCENE3D_PARTICLE_SURFACE_VERSIONS) {
                for (unsigned slot = 0; slot < SCENE3D_PARTICLE_SURFACE_VERSIONS; slot++)
                    if (!(live & (1u << slot))) {
                        chosen = slot;
                        break;
                    }
                if (chosen == SCENE3D_PARTICLE_SURFACE_VERSIONS)
                    return particle_fail(diagnostic, capacity,
                                         "particle surface: eight retained versions are still live");
                if (!scene3d_particle_surface_retain(descriptions[i].surface))
                    return particle_fail(diagnostic, capacity, "particle surface: reference count exhausted");
                staged.acquired[i] = descriptions[i].surface;
            } else
                descriptions[i].surface = emitter->surfaces.values[chosen];
            descriptions[i].surface_slot = chosen;
        }
        changed |= !retain || selected[i] != state->emitters[i] ||
                   !particle_description_same(selected[i]->description, descriptions[i]) || strcmp(selected[i]->id, id);
        ParticleEmitter probe = *selected[i];
        probe.description = descriptions[i];
        active |= particle_active(&probe);
    }
    size_t surface_faces = 0;
    for (unsigned i = 0; i < sources.count; i++) {
        unsigned mask = particle_surface_live_mask(selected[i]);
        if (descriptions[i].surface)
            mask |= 1u << descriptions[i].surface_slot;
        for (unsigned slot = 0; slot < SCENE3D_PARTICLE_SURFACE_VERSIONS; slot++) {
            if (!(mask & (1u << slot)))
                continue;
            auto *surface =
                slot == descriptions[i].surface_slot ? descriptions[i].surface : selected[i]->surfaces.values[slot];
            if (!surface || surface->count > PARTICLE_SURFACE_STORAGE_LIMIT - surface_faces)
                return particle_fail(diagnostic, capacity,
                                     "particle surface: scene retained triangle budget exceeds 65536");
            surface_faces += surface->count;
        }
    }
    bool checkpoint[PARTICLE_EMITTER_LIMIT] = {};
    for (unsigned i = 0; i < sources.count; i++) {
        auto *emitter = selected[i];
        const auto &before = emitter->description;
        const auto &after = descriptions[i];
        checkpoint[i] = emitter->gpu && !staged.created[i] &&
                        (before.parameters[PARTICLE_DRAG] != after.parameters[PARTICLE_DRAG] ||
                         memcmp(&before.gravity, &after.gravity, sizeof(before.gravity)) ||
                         memcmp(&before.noise, &after.noise, sizeof(before.noise)) ||
                         !particle_curve_same(before.curves[SCENE3D_PARTICLE_VELOCITY_CURVE],
                                              after.curves[SCENE3D_PARTICLE_VELOCITY_CURVE], 3) ||
                         memcmp(&before.analytic, &after.analytic, sizeof(before.analytic)));
        if (checkpoint[i] && !scene3d_particle_gpu_checkpoint(emitter->gpu))
            return particle_fail(diagnostic, capacity, "GPU particles: motion checkpoint failed");
    }
    Scene3dRuntimeStats clock = {};
    active |= !changed && state->pending.count;
    if (!scene3d_runtime_stats(state->runtime, &clock) ||
        (changed && (state->generation == UINT64_MAX || clock.generation == UINT64_MAX ||
                     clock.service_generations[SCENE3D_TICK_PARTICLES] == UINT64_MAX)) ||
        !scene3d_runtime_enable(state->runtime, SCENE3D_TICK_PARTICLES, state, automatic && active))
        return false;
    if (changed && !scene3d_runtime_revise(state->runtime, SCENE3D_TICK_PARTICLES, state))
        return false;
    if (!scene3d_runtime_command_wake(state->runtime, SCENE3D_TICK_PARTICLES, state, automatic)) return false;
    if (changed) state->pending.count = state->pending_spawns = 0;
    state->emitter_sequence = emitter_sequence;
    for (unsigned i = 0; i < state->count; i++) {
        bool kept = false;
        for (unsigned j = 0; j < sources.count; j++)
            kept |= selected[j] == state->emitters[i];
        if (!kept)
            particle_emitter_destroy(state->emitters[i]);
    }
    memset(state->emitters, 0, sizeof(state->emitters));
    state->count = sources.count;
    for (unsigned i = 0; i < state->count; i++) {
        auto *emitter = state->emitters[i] = selected[i];
        emitter->source = dom_node_ref(sources.nodes[i]);
        if (checkpoint[i])
            scene3d_particle_gpu_checkpoint_commit(emitter->gpu);
        if (staged.acquired[i]) {
            unsigned slot = descriptions[i].surface_slot;
            scene3d_particle_gpu_surface_changed(emitter->gpu, slot);
            scene3d_particle_surface_release(emitter->surfaces.values[slot]);
            emitter->surfaces.values[slot] = staged.acquired[i];
            staged.acquired[i] = nullptr;
        }
        if (emitter->description.trail.samples != descriptions[i].trail.samples) {
            scene3d_particle_gpu_trail_attach(emitter->gpu, staged.trails[i]);
            scene3d_particle_trail_destroy(emitter->trail);
            emitter->trail = staged.trails[i]; staged.trails[i] = nullptr;
        }
        emitter->description = descriptions[i];
        particle_publish_trail(emitter);
        particle_surfaces_prune(emitter);
        const char *id = scene3d_text(sources.nodes[i], "id");
        str_copy(emitter->id, sizeof(emitter->id), id, strlen(id));
        staged.created[i] = nullptr;
    }
    if (changed)
        state->generation++;
    state->automatic = automatic;
    state->epoch = root->doc->mutation_epoch;
    return true;
}
void scene3d_particles_follow_rebuild(Scene3dParticles *state, DomNode *old_root, DomNode *new_root) {
    if (!state)
        return;
    state->epoch = UINT64_MAX;
    view_state_ref_follow_rebuild(state->document, &state->root, old_root, new_root);
    for (unsigned i = 0; i < state->count; i++)
        view_state_ref_follow_rebuild(state->document, &state->emitters[i]->source, old_root, new_root);
}
uint64_t scene3d_particles_generation(Scene3dParticles *state) { return state ? state->generation : 0; }
bool scene3d_particles_view(Scene3dParticles *state, DomElement *node, Scene3dEmitterView *result) {
    auto *emitter = particle_emitter_find(state, node);
    if (!emitter || !result)
        return false;
    const auto &playback = emitter->playback;
    *result = {emitter->matrices,
               emitter->description.capacity,
               emitter->matrix_count,
               playback.count,
               playback.sequence,
               playback.spawned,
               playback.dropped,
               playback.recycled,
               sizeof(ParticleEmitter) +
                   emitter->description.capacity * (2 * sizeof(ParticleRecord) + sizeof(Scene3dMatrix)),
               playback.time,
               emitter->lo,
               emitter->hi};
    result->storage_bytes += 2 * emitter->description.collision.event_capacity * sizeof(ParticleEvent);
    result->collisions = playback.collisions;
    result->killed = playback.killed;
    result->collision_queries = playback.queries;
    result->collision_work = playback.geometry_work;
    result->collision_triangles = playback.triangle_work;
    result->pending_events = playback.event_count;
    result->identity = emitter->identity;
    result->world = emitter->description.world;
    result->rotating = emitter->rotating;
    result->fading = emitter->fading;
    result->colors = emitter->colors;
    result->frames = emitter->frames;
    result->billboards = emitter->billboards;
    result->billboard = emitter->description.billboard;
    result->sheet_columns = emitter->sheet.columns ? emitter->sheet.columns : 1;
    result->sheet_rows = emitter->sheet.rows ? emitter->sheet.rows : 1;
    result->sheet_inset = emitter->sheet.inset;
    if (!emitter->gpu) result->storage_bytes += emitter->description.capacity * (7 * sizeof(float) + sizeof(unsigned));
    result->mirrored =
        result->world && playback.count && scene3d_determinant(emitter->particles[playback.head].frame.v) < 0;
    if (emitter->gpu) {
        Scene3dParticleGpuStats stats = {};
        if (!scene3d_particle_gpu_stats(emitter->gpu, &stats))
            return false;
        result->matrices = nullptr;
        result->slots = emitter->slots;
        result->gpu = true;
        result->gpu_positions = stats.positions;
        result->gpu_births = stats.births;
        result->trail.target = stats.trails;
        result->max_size = emitter->max_size;
        result->gpu_readbacks = stats.readbacks;
        result->gpu_passes = stats.passes;
        result->gpu_recoveries = stats.recoveries;
        result->gpu_bytes = stats.gpu_bytes;
        result->gpu_history_ticks = stats.history_ticks;
        result->storage_bytes = sizeof(ParticleEmitter) +
                                emitter->description.capacity * (2 * sizeof(ParticleRecord) + sizeof(float)) +
                                stats.cpu_bytes;
    }
    for (auto *surface : emitter->surfaces.values) {
        if (!surface)
            continue;
        result->surface_versions++;
        result->surface_triangles += surface->count;
        result->surface_bytes += sizeof(*surface) + surface->records * sizeof(*surface->triangles);
    }
    result->storage_bytes += result->surface_bytes;
    if (const auto *trail = emitter->trail) {
        const auto &d = emitter->description.trail;
        result->storage_bytes += trail->bytes;
        auto target = result->trail.target;
        result->trail = {trail->gpu ? nullptr : trail->points[0], trail->slots, trail->styles, trail->samples, trail->head,
                         trail->columns, trail->rows, trail->records, d.lifetime, d.width, d.fade};
        result->trail.target = target;
        bool empty = true;
        if (trail->gpu) for (unsigned i = 0; i < trail->samples; i++) {
            const auto &bounds = trail->bounds[0][i];
            if (!bounds.valid) continue;
            if (empty) { result->trail.lo = bounds.lo; result->trail.hi = bounds.hi; empty = false; }
            else { scene3d_include(bounds.lo, &result->trail.lo, &result->trail.hi); scene3d_include(bounds.hi, &result->trail.lo, &result->trail.hi); }
        }
        else for (unsigned i = 0; i < playback.count; i++) {
            unsigned slot = emitter->particles[(playback.head + i) % emitter->description.capacity].slot;
            for (unsigned j = 0; j < trail->samples; j++) {
                const auto &point = trail->points[0][slot * trail->samples + j];
                if (point.age < 0) continue;
                if (empty) { result->trail.lo = result->trail.hi = point.position; empty = false; }
                else scene3d_include(point.position, &result->trail.lo, &result->trail.hi);
            }
        }
    }
    return true;
}
bool scene3d_particles_trail_samples(Scene3dParticles *state, DomElement *node, unsigned index,
                                     Scene3dParticleTrailPoint *points, unsigned *count) {
    auto *emitter = particle_emitter_find(state, node);
    if (!emitter || !emitter->trail || !count || !points || index >= emitter->playback.count) return false;
    const auto &record = emitter->particles[(emitter->playback.head + index) % emitter->description.capacity];
    if (emitter->gpu) return scene3d_particle_gpu_trail_sample(emitter->gpu, record.slot, points, count);
    *count = scene3d_particle_trail_samples(emitter->trail, record.slot, points);
    return true;
}
bool scene3d_particles_samples(Scene3dParticles *state, DomElement *node, unsigned first, unsigned count,
                               Scene3dParticleSample *result) {
    auto *emitter = particle_emitter_find(state, node);
    if (!emitter || count > 64 || (count && !result) || first > emitter->playback.count ||
        count > emitter->playback.count - first)
        return false;
    unsigned slots[64];
    for (unsigned i = 0; i < count; i++) {
        const auto &record = emitter->particles[(emitter->playback.head + first + i) % emitter->description.capacity];
        result[i] = record.value;
        slots[i] = record.slot;
    }
    return !emitter->gpu || scene3d_particle_gpu_sample(emitter->gpu, slots, count, result);
}
bool scene3d_particles_sample(Scene3dParticles *state, DomElement *node, unsigned index,
                              Scene3dParticleSample *result) {
    return scene3d_particles_samples(state, node, index, 1, result);
}
Scene3dParticles *scene3d_particles_replay(Scene3dParticles *state, size_t ticks, double timestep, char *diagnostic,
                                           size_t capacity) {
    if (!state || state->generation == UINT64_MAX || ticks > 100000 || !isfinite(timestep) || timestep <= 0 ||
        !scene3d_runtime_can_configure(state->runtime, timestep, 1))
        return nullptr;
    auto *replay = (Scene3dParticles *)mem_calloc(1, sizeof(Scene3dParticles), MEM_CAT_RENDER);
    if (!replay)
        return nullptr;
    replay->gpu_ops = state->gpu_ops;
    replay->document = state->document;
    replay->root = state->root;
    replay->runtime = state->runtime;
    replay->automatic = state->automatic;
    replay->generation = 1;
    replay->epoch = state->epoch;
    replay->emitter_sequence = state->emitter_sequence;
    replay->replay_source_generation = state->generation;
    replay->replay_ticks = ticks;
    replay->replay_timestep = timestep;
    for (unsigned i = 0; i < state->count; i++)
        replay->replay_geometry |= state->emitters[i]->description.surface != nullptr;
    if (scene3d_particles_needs_world(state) || replay->replay_geometry) {
        auto *source = dom_node_ref_validate(state->document, state->root);
        Scene3dRuntimeStats clock = {};
        if (!source || !source->is_element() || state->epoch != state->document->mutation_epoch ||
            !scene3d_runtime_stats(state->runtime, &clock)) {
            particle_fail(diagnostic, capacity, "particle world replay: unadmitted or detached source");
            scene3d_particles_destroy(replay);
            return nullptr;
        }
        auto *physics = scene3d_physics(source->as_element());
        replay->replay_source_epoch = state->epoch;
        replay->replay_clock_generation = clock.generation;
        replay->replay_physics_generation = scene3d_physics_generation(physics);
        replay->replay_world_identity = scene3d_physics_world_identity(physics);
        replay->replay_animation_generation = scene3d_animation_generation(scene3d_animations(source->as_element()));
    }
    for (unsigned i = 0; i < state->count; i++) {
        auto *before = state->emitters[i];
        auto *emitter = particle_emitter_create(before->description, replay);
        if (!emitter) {
            scene3d_particles_destroy(replay);
            return nullptr;
        }
        emitter->source = before->source;
        emitter->identity = before->identity;
        memcpy(emitter->id, before->id, sizeof(emitter->id));
        replay->emitters[replay->count++] = emitter;
    }
    auto *root = dom_node_ref_validate(state->document, state->root);
    if (scene3d_particles_needs_world(replay)) {
        if (!root || !root->is_element() ||
            !scene3d_physics_replay_particles(root->as_element(), replay, ticks, timestep, diagnostic, capacity)) {
            scene3d_particles_destroy(replay);
            return nullptr;
        }
        return replay;
    }
    for (size_t tick = 0; tick < ticks; tick++) {
        if (particles_tick(replay, timestep) == SCENE3D_TICK_FAILED) {
            particle_fail(diagnostic, capacity,
                          replay->diagnostic ? replay->diagnostic : "particles: authored replay failed");
            scene3d_particles_destroy(replay);
            return nullptr;
        }
    }
    return replay;
}
static bool particle_replay_owner(Scene3dParticles *state, Scene3dParticles *replay) {
    return state && replay && state->bound && !replay->bound && state->generation != UINT64_MAX &&
           state->generation == replay->replay_source_generation && state->document == replay->document &&
           state->runtime == replay->runtime && state->root.address == replay->root.address &&
           state->root.expected_id == replay->root.expected_id;
}
bool scene3d_particles_replay_current(Scene3dParticles *state, Scene3dParticles *replay) {
    if (!particle_replay_owner(state, replay))
        return false;
    if (replay->replay_geometry && state->document->mutation_epoch != replay->replay_source_epoch)
        return false;
    if (!scene3d_particles_needs_world(replay))
        return true;
    auto *root = dom_node_ref_validate(state->document, state->root);
    Scene3dRuntimeStats clock = {};
    if (!root || !root->is_element() || state->document->mutation_epoch != replay->replay_source_epoch ||
        !scene3d_runtime_stats(state->runtime, &clock))
        return false;
    auto *physics = scene3d_physics(root->as_element());
    return clock.generation == replay->replay_clock_generation &&
           scene3d_physics_generation(physics) == replay->replay_physics_generation &&
           scene3d_physics_world_identity(physics) == replay->replay_world_identity &&
           scene3d_animation_generation(scene3d_animations(root->as_element())) == replay->replay_animation_generation;
}
static bool particle_adopt(Scene3dParticles *state, Scene3dParticles *replay, bool coordinated) {
    Scene3dRuntimeStats clock = {};
    if (!particle_replay_owner(state, replay) || !scene3d_runtime_stats(state->runtime, &clock) || !clock.paused ||
        !scene3d_runtime_can_configure(state->runtime, clock.timestep, 1))
        return false;
    // geometry-dependent candidates retain admitted copies, not authority to replace a later source edit.
    if (replay->replay_geometry && state->document->mutation_epoch != replay->replay_source_epoch)
        return false;
    // sampled-world candidates depend on a reconstructed world, not only the particle buffers they own.
    if (scene3d_particles_needs_world(replay) &&
        (!coordinated || state->document->mutation_epoch != replay->replay_source_epoch ||
         clock.ticks != replay->replay_ticks || clock.timestep != replay->replay_timestep))
        return false;
    if (!scene3d_runtime_enable(state->runtime, SCENE3D_TICK_PARTICLES, state,
                                state->automatic && particles_active(replay)))
        return false;
    for (unsigned i = 0; i < state->count; i++)
        particle_emitter_destroy(state->emitters[i]);
    state->count = replay->count;
    memcpy(state->emitters, replay->emitters, sizeof(state->emitters));
    replay->count = 0;
    state->pending.count = state->pending_spawns = 0;
    state->generation++;
    state->diagnostic = nullptr;
    return true;
}
bool scene3d_particles_adopt(Scene3dParticles *state, Scene3dParticles *replay) {
    return particle_adopt(state, replay, false);
}
bool scene3d_particles_adopt_after_replay(Scene3dParticles *state, Scene3dParticles *replay) {
    return particle_adopt(state, replay, true);
}
static void particle_snapshot_header(Scene3dParticles *state, const Scene3dRuntimeStats &clock,
                                     RootedValueBuilder *result) {
    result->put("session", box_uint64_value(clock.session));
    result->put("generation", box_uint64_value(clock.generation));
    result->put("service_generation", box_uint64_value(clock.service_generations[SCENE3D_TICK_PARTICLES]));
    result->put("snapshot_generation", box_uint64_value(state->generation));
    result->put("tick", box_uint64_value(clock.ticks));
}
Item scene3d_particles_trail_read(Scene3dParticles *state, Item request) {
    RootFrame roots(1); Rooted<Item> rooted(roots, request);
    auto fail = [](const char *message) { return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr)); };
    Scene3dRuntimeStats clock = {};
    if (!state || !scene3d_runtime_stats(state->runtime, &clock)) return ItemError;
    auto read = ItemReader(rooted.get().to_const()).asMap();
    const char *id = navigation_text(read.get("emitter"));
    size_t index = 0;
    // selectors accept exact whole JS numbers; opaque session/sequence counters remain integers only.
    if (!id || !navigation_integer(read.get("index"), 0, SCENE3D_INSTANCE_LIMIT - 1, &index))
        return fail("particle trail: requires an emitter id and a bounded whole particle index");
    ParticleEmitter *emitter = nullptr;
    for (unsigned i = 0; i < state->count; i++) if (!strcmp(state->emitters[i]->id, id)) emitter = state->emitters[i];
    if (!emitter || !emitter->trail || index >= emitter->playback.count)
        return fail("particle trail: no live particle history at the requested emitter/index");
    auto *source = dom_node_ref_validate(state->document, emitter->source);
    Scene3dParticleTrailPoint points[SCENE3D_PARTICLE_TRAIL_SAMPLE_LIMIT]; unsigned count;
    if (!source || !source->is_element() ||
        !scene3d_particles_trail_samples(state, source->as_element(), (unsigned)index, points, &count)) return ItemError;
    const auto &record = emitter->particles[(emitter->playback.head + (unsigned)index) % emitter->description.capacity];
    // copy bounded native history before allocating any caller-owned values (D4.5.2/D5.3.3).
    RootedValueBuilder result, samples(true);
    particle_snapshot_header(state, clock, &result);
    result.put("emitter", Item{.item = s2it(heap_create_name(emitter->id))});
    result.put("sequence", box_uint64_value(record.value.sequence));
    result.put("depth", box_uint64_value(record.value.depth));
    result.put("space", Item{.item = s2it(heap_create_name(emitter->description.world ? "world" : "local"))});
    result.put("segments", box_uint64_value(emitter->trail->samples - 1));
    result.put("lifetime", push_d(emitter->description.trail.lifetime));
    result.put("width", push_d(emitter->description.trail.width));
    result.put("fade", Item{.item = b2it(emitter->description.trail.fade)});
    for (unsigned i = 0; i < count; i++) {
        RootedValueBuilder point;
        point.put("position", navigation_point_value({points[i].position.x, points[i].position.y, points[i].position.z}, 3));
        point.put("age", push_d(points[i].age)); samples.append(point.value.get());
    }
    result.put("samples", samples.value.get()); return result.value.get();
}
Item scene3d_particles_commands(Scene3dParticles *state, Item batch) {
    RootFrame roots(1); Rooted<Item> rooted(roots, batch);
    auto fail = [](const char *message) { return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr)); };
    if (!state || !state->bound || !ItemReader(rooted.get().to_const()).isMap())
        return fail("particle commands: requires a live scene and a batch map");
    struct Owned {
        ParticleSpawnBatch *value = (ParticleSpawnBatch *)mem_calloc(1, sizeof(ParticleSpawnBatch), MEM_CAT_RENDER);
        ~Owned() { mem_free(value); }
    } owned;
    if (!owned.value) return fail("particle commands: native payload allocation failed");
    auto input = ItemReader(rooted.get().to_const()).asMap();
    Scene3dCommandBatch ticket = {};
    if (!scene3d_counter(input.get("session"), &ticket.session) ||
        !scene3d_counter(input.get("generation"), &ticket.generation) ||
        !scene3d_counter(input.get("service_generation"), &ticket.service_generation) ||
        !scene3d_counter(input.get("sequence"), &ticket.sequence) || !scene3d_counter(input.get("tick"), &ticket.tick))
        return fail("particle commands: missing or lossy batch identities");
    auto commands = input.get("commands");
    if (!commands.isArray() || commands.asArray().length() < 1 || commands.asArray().length() > PARTICLE_COMMAND_LIMIT)
        return fail("particle commands: requires 1..128 spawn commands");
    owned.value->count = (unsigned)commands.asArray().length();
    unsigned total = 0;
    for (unsigned i = 0; i < owned.value->count; i++) {
        auto value = commands.asArray().get(i);
        auto fields = value.asMap();
        const char *op = scene3d_command_name(fields.get("op")), *id = scene3d_command_name(fields.get("emitter"));
        auto &request = owned.value->requests[i];
        size_t count, depth;
        if (!value.isMap() || !op || strcmp(op, "spawn") || !id ||
            !scene3d_counter(fields.get("identity"), &request.emitter) ||
            !navigation_integer(fields.get("count"), NAN, PARTICLE_SPAWN_LIMIT, &count) || !count ||
            !navigation_integer(fields.get("depth"), 0, PARTICLE_DEPTH_LIMIT, &depth))
            return fail("particle commands: invalid spawn operation, emitter identity, count or depth");
        auto *emitter = particle_emitter_identity(state, request.emitter);
        if (!emitter || strcmp(id, emitter->id)) return fail("particle commands: stale emitter identity");
        auto vector = [&](const char *key, Scene3dVec *result) {
            NavigationVec components;
            if (!navigation_vector(fields.get(key), 3, {}, &components) || fabs(components.x) > 1e6 ||
                fabs(components.y) > 1e6 || fabs(components.z) > 1e6) return false;
            *result = {(float)components.x, (float)components.y, (float)components.z}; return true;
        };
        if (!vector("position", &request.position) || !vector("velocity", &request.velocity))
            return fail("particle commands: origins/velocities require three finite coordinates in [-1000000,1000000]");
        request.positioned = !fields.get("position").isNull(); request.count = (unsigned)count; request.depth = (unsigned)depth;
        if (request.count > PARTICLE_SPAWN_LIMIT - total) return fail("particle commands: batch spawn count exceeds 4096");
        total += request.count;
    }
    ticket.phase = SCENE3D_TICK_PARTICLES; ticket.payload = owned.value;
    ticket.release = [](void *payload) { mem_free(payload); };
    const char *error = nullptr;
    if (!scene3d_runtime_submit(state->runtime, ticket, &error)) return fail(error);
    owned.value = nullptr;
    return scene3d_runtime_snapshot(state->runtime);
}
Item scene3d_particles_snapshot(Scene3dParticles *state) {
    if (!state)
        return ItemError;
    RootedValueBuilder result, emitters(true);
    Scene3dRuntimeStats clock = {};
    if (!scene3d_runtime_stats(state->runtime, &clock))
        return ItemError;
    particle_snapshot_header(state, clock, &result);
    result.put("automatic", Item{.item = b2it(state->automatic)});
    result.put("pending_spawn_requests", box_uint64_value(state->pending.count));
    result.put("pending_spawn_count", box_uint64_value(state->pending_spawns));
    result.put("diagnostic", Item{.item = s2it(heap_create_name(state->diagnostic ? state->diagnostic : ""))});
    for (unsigned i = 0; i < state->count; i++) {
        auto *emitter = state->emitters[i];
        Scene3dEmitterView view;
        auto *source = dom_node_ref_validate(state->document, emitter->source);
        if (!source || !source->is_element() || !scene3d_particles_view(state, source->as_element(), &view))
            return ItemError;
        Scene3dParticleSample copied[64];
        unsigned copied_count = view.live < 64 ? view.live : 64;
        if (!scene3d_particles_samples(state, source->as_element(), 0, copied_count, copied) ||
            !scene3d_particles_view(state, source->as_element(), &view))
            return ItemError;
        RootedValueBuilder value, samples(true);
        value.put("backend", Item{.item = s2it(heap_create_name(view.gpu ? "gpu" : "cpu"))});
        value.put("space", Item{.item = s2it(heap_create_name(view.world ? "world" : "local"))});
        value.put("presentation", Item{.item = s2it(heap_create_name(view.billboard ? "billboard" : "mesh"))});
        value.put("trail_segments", box_uint64_value(view.trail.samples ? view.trail.samples - 1 : 0));
        value.put("trail_point_slots", box_uint64_value(view.trail.records * view.trail.samples));
        const char *gpu_names[] = {"gpu_readbacks", "gpu_passes", "gpu_recoveries", "gpu_bytes", "gpu_history_ticks"};
        uint64_t gpu_values[] = {view.gpu_readbacks, view.gpu_passes, view.gpu_recoveries, view.gpu_bytes, view.gpu_history_ticks};
        for (unsigned k = 0; k < 5; k++)
            value.put(gpu_names[k], box_uint64_value(gpu_values[k]));
        value.put("id", Item{.item = s2it(heap_create_name(emitter->id))});
        value.put("identity", box_uint64_value(emitter->identity));
        const char *names[] = {"capacity", "live",    "rendered", "sequence",
                               "spawned",  "dropped", "recycled", "storage_bytes"};
        uint64_t counts[] = {view.capacity, view.live,    view.count,    view.sequence,
                             view.spawned,  view.dropped, view.recycled, view.storage_bytes};
        for (unsigned n = 0; n < 8; n++)
            value.put(names[n], box_uint64_value(counts[n]));
        const char *collision_names[] = {"collisions",          "killed",        "collision_queries", "collision_work",
                                         "collision_triangles", "pending_events"};
        uint64_t collision_counts[] = {view.collisions,          view.killed,
                                       view.collision_queries,   view.collision_work,
                                       view.collision_triangles, view.pending_events};
        for (unsigned k = 0; k < 6; k++)
            value.put(collision_names[k], box_uint64_value(collision_counts[k]));
        const char *surface_names[] = {"surface_versions", "surface_triangles", "surface_bytes"};
        uint64_t surface_counts[] = {view.surface_versions, view.surface_triangles, view.surface_bytes};
        for (unsigned k = 0; k < 3; k++)
            value.put(surface_names[k], box_uint64_value(surface_counts[k]));
        value.put("time", push_d(view.time));
        value.put("sheet_grid", navigation_point_value({(float)view.sheet_columns, (float)view.sheet_rows, view.sheet_inset}, 3));
        value.put("center_low", navigation_point_value({view.lo.x, view.lo.y, view.lo.z}, 3));
        value.put("center_high", navigation_point_value({view.hi.x, view.hi.y, view.hi.z}, 3));
        value.put("samples_truncated", Item{.item = b2it(view.live > 64)});
        for (unsigned n = 0; n < view.live && n < 64; n++) {
            const auto &sample = copied[n];
            RootedValueBuilder record;
            record.put("sequence", box_uint64_value(sample.sequence));
            record.put("depth", box_uint64_value(sample.depth));
            record.put("age", push_d(sample.age));
            record.put("lifetime", push_d(sample.lifetime));
            record.put("size", push_d(sample.size));
            record.put("frame", Item{.item = i2it((int64_t)sample.frame)});
            RootedValueBuilder color(true);
            for (float component : sample.color) color.append(push_d(component));
            record.put("color", color.value.get());
            const char *collision_states[] = {"running", "impact-limit", "initial-overlap"};
            record.put("collision_status", Item{.item = s2it(heap_create_name(collision_states[sample.collision_status]))});
            record.put("rotation",
                       navigation_point_value({sample.rotation.x, sample.rotation.y, sample.rotation.z}, 3));
            record.put("position",
                       navigation_point_value({sample.position.x, sample.position.y, sample.position.z}, 3));
            record.put("velocity",
                       navigation_point_value({sample.velocity.x, sample.velocity.y, sample.velocity.z}, 3));
            samples.append(record.value.get());
        }
        value.put("samples", samples.value.get());
        emitters.append(value.value.get());
    }
    result.put("emitters", emitters.value.get());
    return result.value.get();
}

bool scene3d_particles_needs_world(Scene3dParticles *state) {
    if (state)
        for (unsigned i = 0; i < state->count; i++)
            if (state->emitters[i]->description.collision.enabled || state->emitters[i]->description.world)
                return true;
    return false;
}
const char *scene3d_particles_diagnostic(Scene3dParticles *state) { return state ? state->diagnostic : nullptr; }
bool scene3d_particles_replay_attach(Scene3dParticles *state, Scene3dRuntime *runtime, Scene3dPhysics *physics,
                                     Scene3dAnimationState *animation) {
    if (!state || state->bound || state->replay_runtime || !runtime || !physics || !animation ||
        !scene3d_runtime_bind(runtime, SCENE3D_TICK_PARTICLES, state, particles_tick))
        return false;
    state->replay_runtime = runtime;
    state->replay_physics = physics;
    state->replay_animation = animation;
    return true;
}
void scene3d_particles_replay_detach(Scene3dParticles *state) {
    if (!state || !state->replay_runtime)
        return;
    scene3d_runtime_unbind(state->replay_runtime, SCENE3D_TICK_PARTICLES, state);
    state->replay_runtime = nullptr;
    state->replay_physics = nullptr;
    state->replay_animation = nullptr;
}

Item scene3d_particles_events(Scene3dParticles *state, Item request) {
    RootFrame roots(1);
    Rooted<Item> rooted(roots, request);
    auto fail = [](const char *message) {
        return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr));
    };
    Scene3dRuntimeStats clock = {};
    if (!state || !scene3d_runtime_stats(state->runtime, &clock))
        return ItemError;
    auto read = ItemReader(rooted.get().to_const()).asMap();
    const char *operation = navigation_text(read.get("operation"));
    const char *selected = navigation_text(read.get("emitter"));
    if (!operation || (!selected && !read.get("emitter").isNull()))
        return fail("particle events: invalid request");
    ParticleEmitter *emitter = nullptr;
    for (unsigned i = 0; i < state->count; i++)
        if (selected && !strcmp(state->emitters[i]->id, selected))
            emitter = state->emitters[i];
    if (selected && !emitter)
        return fail("particle events: unknown emitter");
    if (!strcmp(operation, "particle_events_ack")) {
        uint64_t session, generation, through;
        if (!emitter || !scene3d_counter(read.get("session"), &session) ||
            !scene3d_counter(read.get("generation"), &generation) || !scene3d_counter(read.get("through"), &through) ||
            session != clock.session || generation != clock.generation || through > emitter->playback.event_sequence)
            return fail("particle events: stale or invalid acknowledgement");
        if (state->generation == UINT64_MAX)
            return fail("particle events: snapshot generation exhausted");
        auto &p = emitter->playback;
        unsigned removed = 0;
        while (p.event_count && emitter->events[p.event_head].sequence <= through) {
            p.event_head = (p.event_head + 1) % emitter->description.collision.event_capacity;
            p.event_count--;
            removed++;
        }
        if (removed)
            state->generation++;
        RootedValueBuilder result;
        particle_snapshot_header(state, clock, &result);
        result.put("acknowledged", box_uint64_value(removed));
        result.put("pending", box_uint64_value(p.event_count));
        return result.value.get();
    }
    if (strcmp(operation, "particle_events_read"))
        return fail("particle events: unknown operation");
    uint64_t limit = 256;
    if ((!read.get("limit").isNull() && !scene3d_counter(read.get("limit"), &limit)) || !limit || limit > 1024)
        return fail("particle events: read limit must be an integer from 1 to 1024");
    RootedValueBuilder result, emitters(true);
    particle_snapshot_header(state, clock, &result);
    for (unsigned i = 0; i < state->count; i++) {
        auto *current = state->emitters[i];
        if (selected && current != emitter)
            continue;
        const auto &p = current->playback;
        unsigned count = p.event_count < limit ? p.event_count : (unsigned)limit;
        RootedValueBuilder value, events(true);
        value.put("id", Item{.item = s2it(heap_create_name(current->id))});
        value.put("pending", box_uint64_value(p.event_count));
        value.put("capacity", box_uint64_value(current->description.collision.event_capacity));
        value.put("truncated", Item{.item = b2it(count != p.event_count)});
        uint64_t through = 0;
        for (unsigned j = 0; j < count; j++) {
            const auto &event = current->events[(p.event_head + j) % current->description.collision.event_capacity];
            const auto &contact = event.contact;
            RootedValueBuilder row;
            const char *names[] = {"sequence", "particle", "tick", "world_identity", "body", "collider"};
            const uint64_t values[] = {event.sequence,       event.particle,   event.tick,
                                       event.world_identity, contact.hit.body, contact.hit.collider};
            for (unsigned k = 0; k < 6; k++)
                row.put(names[k], box_uint64_value(values[k]));
            row.put("kind", Item{.item = s2it(heap_create_name(contact.killed ? "kill" : "impact"))});
            row.put("depth", box_uint64_value(event.depth));
            row.put("overlapping", Item{.item = b2it(contact.hit.overlapping)});
            row.put("fraction", push_d(contact.tick_fraction));
            const char *vectors[] = {"position", "point", "normal", "velocity_before", "velocity_after"};
            const PhysicsVec points[] = {contact.center, contact.hit.point_b, contact.hit.normal,
                                         contact.velocity_before, contact.velocity_after};
            for (unsigned k = 0; k < 5; k++)
                row.put(vectors[k], navigation_point_value({points[k].x, points[k].y, points[k].z}, 3));
            events.append(row.value.get());
            through = event.sequence;
        }
        value.put("through", box_uint64_value(through));
        value.put("events", events.value.get());
        emitters.append(value.value.get());
    }
    result.put("emitters", emitters.value.get());
    return result.value.get();
}
