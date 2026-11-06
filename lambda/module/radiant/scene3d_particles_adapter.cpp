#include "scene3d_particles_adapter.hpp"
#include "../../../radiant/scene3d/scene3d_particles_internal.hpp"
#include "../../../radiant/scene3d/scene3d_physics.hpp"
#include "../../../radiant/event.hpp"
#include "../../core/mark_reader.hpp"
#include "../../input/css/dom_element.hpp"
#include "../../input/css/dom_lifecycle.hpp"
#include "../../runtime/lambda-number-runtime.hpp"
#include "../../runtime/lambda-error.h"
#include "../../runtime/lambda-navigation.hpp"
#include "../../runtime/value_builder.hpp"
#include "../../../lib/mem.h"
#include <float.h>

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
    // copy bounded native history before allocating any caller-owned values (D5.5.2/D6.3.3).
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
        auto *emitter = scene3d_particle_emitter_identity(state, request.emitter);
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
