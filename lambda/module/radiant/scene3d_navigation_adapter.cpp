#include "scene3d_navigation_adapter.hpp"
#include "scene3d_physics_adapter.hpp"
#include "../../../radiant/scene3d/scene3d_navigation_internal.hpp"
#include "../../../radiant/scene3d/scene3d_physics.hpp"
#include "../../../radiant/scene3d/scene3d_source.hpp"
#include "../../../radiant/event.hpp"
#include "../../../radiant/radiant.hpp"
#include "../../input/css/dom_element.hpp"
#include "../../input/css/dom_lifecycle.hpp"
#include "../../dom/dom_events.h"
#include "../../dom/realm/dom_realm.h"
#include "../../js/js_runtime.h"
#include "../../runtime/lambda-error.h"
#include "../../runtime/lambda-navigation.hpp"
#include "../../runtime/lambda-number-runtime.hpp"
#include "../../runtime/value_builder.hpp"
#include "../../../lib/mem.h"
#include "../../../lib/physics/physics_math.h"
#include "../../../lib/thread_pool.h"
#include <float.h>
#include <math.h>

static Item nav_job_status(Scene3dNavigation* state, Scene3dBakeJob* job) {
    bool done = __atomic_load_n(&job->worker.done, __ATOMIC_ACQUIRE),
         cancelled = __atomic_load_n(&job->worker.cancelled, __ATOMIC_RELAXED);
    const char* status = job->published_generation ? "published"
                         : job->stale              ? "stale"
                         : cancelled               ? (done ? "cancelled" : "cancelling")
                         : done                    ? (job->worker.success ? "ready" : "failed")
                         : __atomic_load_n(&job->worker.started, __ATOMIC_RELAXED) ? "running"
                                                                                   : "queued";
    bool current = false;
    for (auto* asset = state->assets; asset; asset = asset->next)
        if (!strcmp(asset->id, job->asset))
            current = state->epoch == state->document->mutation_epoch && job->published_generation &&
                      asset->generation == job->published_generation;
    RootedValueBuilder value(RootedValueBuilder::DATA_MAP);
    value.put("token", Item{.item = i2it((int64_t)job->token)});
    value.put("asset", Item{.item = s2it(heap_create_name(job->asset))});
    value.put("status", Item{.item = s2it(heap_create_name(status))});
    value.put("finished", Item{.item = b2it(done)});
    value.put("current", Item{.item = b2it(current)});
    value.put("stage", Item{.item = i2it(__atomic_load_n(&job->worker.stage, __ATOMIC_RELAXED))});
    value.put("work", Item{.item = i2it((int64_t)__atomic_load_n(&job->worker.work, __ATOMIC_RELAXED))});
    value.put("generation",
              job->published_generation ? Item{.item = i2it((int64_t)job->published_generation)} : ItemNull);
    value.put("source_hash", Item{.item = s2it(heap_create_name(job->worker.geometry.source_hash))});
    value.put("settings_hash", Item{.item = s2it(heap_create_name(job->settings_hash))});
    const char* diagnostic = *job->diagnostic                  ? job->diagnostic
                             : done && *job->worker.diagnostic ? job->worker.diagnostic
                                                               : nullptr;
    value.put("diagnostic", diagnostic ? Item{.item = s2it(heap_create_name(diagnostic))} : ItemNull);
    value.put("cells", done && job->worker.success ? Item{.item = i2it((int64_t)job->worker.stats.cells)} : ItemNull);
    value.put("mode", Item{.item = s2it(heap_create_name(job->worker.base ? "dirty" : "full"))});
    NavigationBakeStats completed = {};
    if (done && job->worker.success)
        completed = job->worker.stats;
    auto counter = [&](const char* name, size_t count) {
        value.put(name, done && job->worker.success ? Item{.item = i2it((int64_t)count)} : ItemNull);
    };
    counter("raster_columns", completed.raster_columns);
    counter("reused_columns", completed.reused_columns);
    counter("dirty_tiles", completed.dirty_tiles);
    return value.value.get();
}
static bool nav_agent_avoidance_options(ItemReader options, NavigationAgentAvoidance* result) {
    if (!result || !options.isMap())
        return false;
    NavigationAgentAvoidance next = *result;
    auto fields = options.asMap();
    if (!navigation_boolean(fields.get("enabled"), next.enabled, &next.enabled) ||
        !navigation_number(fields.get("priority"), next.priority, 1, &next.priority) ||
        !navigation_number(fields.get("time_horizon"), next.time_horizon, DBL_MAX, &next.time_horizon) ||
        next.time_horizon <= 0 ||
        !navigation_integer(fields.get("max_neighbors"), (double)next.max_neighbors,
                            NAVIGATION_AVOIDANCE_MAX_AGENTS - 1, &next.max_neighbors) ||
        !navigation_integer(fields.get("max_neighbor_tests"), (double)next.max_neighbor_tests,
                            NAVIGATION_AVOIDANCE_MAX_AGENTS - 1, &next.max_neighbor_tests) ||
        !navigation_integer(fields.get("max_solver_tests"), (double)next.max_solver_tests,
                            NAVIGATION_AVOIDANCE_MAX_SOLVER_TESTS, &next.max_solver_tests))
        return false;
    *result = next;
    return true;
}
static Item nav_agent_value(Scene3dNavigation* state, Scene3dNavigationAgent* agent) {
    RootedValueBuilder result(RootedValueBuilder::DATA_MAP);
    auto integer = [](uint64_t value) { return Item{.item = i2it((int64_t)value)}; };
    result.put("agent", Item{.item = s2it(heap_create_name(agent->id))});
    result.put("asset", Item{.item = s2it(heap_create_name(agent->asset))});
    result.put("status", Item{.item = s2it(heap_create_name(agent->status))});
    result.put("generation", integer(agent->command));
    result.put("path_generation", integer(agent->path));
    result.put("asset_generation", integer(agent->asset_generation));
    result.put("world_identity", integer(agent->world));
    result.put("queries", integer(agent->queries));
    result.put("pending", Item{.item = b2it(agent->pending)});
    result.put("position", navigation_point_value(agent->foot, 3));
    result.put("target", agent->target ? navigation_point_value(agent->goal, 3) : ItemNull);
    result.put("navigation_position", navigation_point_value(agent->position, 3));
    result.put("velocity", navigation_point_value(agent->observed.velocity, 3));
    result.put("preferred_velocity", navigation_point_value(agent->preferred_velocity, 3));
    result.put("avoidance_velocity", navigation_point_value(agent->avoidance_velocity, 3));
    result.put("achieved_velocity", navigation_point_value(agent->achieved_velocity, 3));
    RootedValueBuilder avoidance(RootedValueBuilder::DATA_MAP);
    static const char* avoidance_statuses[] = {"clear", "adjusted", "blocked", "budget_exhausted", "disabled"};
    avoidance.put("enabled", Item{.item = b2it(agent->avoidance.enabled)});
    avoidance.put("priority", push_d(agent->avoidance.priority));
    avoidance.put("time_horizon", push_d(agent->avoidance.time_horizon));
    avoidance.put("status", Item{.item = s2it(heap_create_name(avoidance_statuses[agent->avoidance_status]))});
    avoidance.put("neighbors", integer(agent->avoidance_neighbors));
    avoidance.put("neighbor_tests", integer(agent->avoidance_neighbor_tests));
    avoidance.put("solver_tests", integer(agent->avoidance_solver_tests));
    result.put("avoidance", avoidance.value.get());
    result.put("complete", Item{.item = b2it(agent->observed.complete)});
    result.put("diagnostic", *agent->diagnostic ? Item{.item = s2it(heap_create_name(agent->diagnostic))} : ItemNull);
    NavigationTraversal action = {};
    RootedValueBuilder link(RootedValueBuilder::DATA_MAP);
    if (navigation_follower_action(agent->follower, &action)) {
        link.put("id", integer(action.id));
        link.put("start", navigation_point_value(action.start, 3));
        link.put("end", navigation_point_value(action.end, 3));
        link.put("reverse", Item{.item = b2it(action.reverse)});
        link.put("duration", push_d(action.duration));
        auto* asset = scene3d_nav_agent_asset(state, agent->asset);
        if (asset && asset->generation == agent->asset_generation)
            for (const auto& binding : asset->compiled.link_bindings)
                if (binding.binding.id == action.id) {
                    link.put("name", Item{.item = s2it(heap_create_name(binding.binding.name))});
                    link.put("kind", Item{.item = s2it(heap_create_name(binding.kind))});
                    break;
                }
        result.put("action", link.value.get());
    } else
        result.put("action", ItemNull);
    return result.value.get();
}
struct NavigationAgentEvent {
    Scene3dNavigation* state;
    Scene3dNavigationAgent* agent;
    const char* type;
};
static Item nav_agent_event_value(void* owner) {
    auto* data = (NavigationAgentEvent*)owner;
    RootFrame roots(2);
    Rooted<Item> event(roots, js_create_event(data->type, true, false));
    Rooted<Item> detail(roots, nav_agent_value(data->state, data->agent));
    dom_realm_set_name(event.get(), "detail", detail.get());
    return event.get();
}
bool lambda_scene3d_navigation_event(DomElement* target, Scene3dNavigation* state,
                                    Scene3dNavigationAgent* agent, const char* type) {
    if (!target || !state || !state->document || !agent) return false;
    NavigationAgentEvent event = {state, agent, type};
    return radiant_queue_timing_event((UiContext*)state->document->js.host_ui_context,
                                      target, nav_agent_event_value, &event, type);
}

static Item nav_agent_operate(Scene3dNavigation* state, DomElement* root, MapReader request, const char* operation,
                              bool valid, const char* diagnostic) {
    auto fail = [](const char* message) {
        return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr));
    };
    const char* id = navigation_text(request.get("agent"));
    if (!id || !*id || strlen(id) >= 128)
        return fail("navigation agent: requires an ID shorter than 128 bytes");
    Scene3dNavigationAgent** slot = &state->agents;
    while (*slot && strcmp((*slot)->id, id))
        slot = &(*slot)->next;
    auto* agent = *slot;
    if (!strcmp(operation, "navigation_agent_read"))
        return agent ? nav_agent_value(state, agent) : fail("navigation agent: unknown ID");
    if (!scene3d_nav_can_change(state))
        return fail("navigation agent: unavailable during tick sampling");
    if (!strcmp(operation, "navigation_agent_attach")) {
        if (!valid)
            return fail(diagnostic);
        if (agent || state->agent_count == NAV_AGENT_LIMIT)
            return fail("navigation agent: duplicate ID or 32-agent quota exceeded");
        const char* asset_id = navigation_text(request.get("asset"));
        auto* asset = asset_id ? scene3d_nav_agent_asset(state, asset_id) : nullptr;
        if (!asset)
            return fail("navigation agent: unknown scene asset");
        auto* physics = scene3d_physics(root);
        PhysicsCharacterMove movement = {};
        PhysicsCharacterProfile profile = {};
        PhysicsError error = {};
        DomElement* body = nullptr;
        if (!scene3d_physics_character_configure(physics, request.get("movement").item(), &movement, &profile, &body,
                                                 &error))
            return fail(error.message);
        if (scene3d_navigation_body_owned(state, body)) {
            return fail("navigation agent: a body can have only one native navigation writer");
        }
        if (scene3d_physics_root_owns_body(physics, body)) {
            return fail("navigation agent: body belongs to a native root-motion controller");
        }
        auto options = request.get("options");
        NavigationSurfaceQuery query = {};
        NavigationError nav_error = {};
        if (!navigation_surface_read(asset->compiled, options, &query, &nav_error))
            return fail(nav_error.message);
        if (!scene3d_nav_agent_profile(asset, profile, movement, &query, &diagnostic))
            return fail(diagnostic);
        // agent support-layer admission adds one bounded face pass before the ordinary two endpoint passes.
        if (options.asMap().get("max_projection_tests").isNull()) {
            NavigationSurfaceView surface = {};
            navigation_bake_surface(asset->compiled.bake, &surface);
            query.path.max_projection_tests = 3 * surface.polygon_count;
        }
        PhysicsCharacterState checked = {};
        movement.displacement = {};
        if (!scene3d_physics_character_compute(physics, &movement, &checked, &error))
            return fail(error.message);
        NavigationFollowerDesc desc = {3, 2, 8, .05, .05, 1, 1, .001, 32, true};
        struct Number {
            const char* name;
            double* value;
        };
        Number numbers[] = {{"speed", &desc.max_speed},
                            {"acceleration", &desc.max_acceleration},
                            {"waypoint_distance", &desc.waypoint_distance},
                            {"target_distance", &desc.target_distance},
                            {"path_distance", &desc.path_distance},
                            {"stuck_time", &desc.stuck_time},
                            {"min_progress", &desc.min_progress}};
        for (auto number : numbers)
            if (!navigation_number(options.asMap().get(number.name), *number.value, DBL_MAX, number.value))
                return fail("navigation agent: invalid follower option");
        NavigationAgentAvoidance avoidance = {};
        if (!options.asMap().get("avoidance").isNull() &&
            !nav_agent_avoidance_options(options.asMap().get("avoidance"), &avoidance))
            return fail("navigation agent: invalid avoidance controls or work budget");
        avoidance.max_speed = desc.max_speed;
        if (*state->next_generation > INT64_MAX)
            return fail("navigation agent: command generation quota exceeded");
        struct Candidate {
            Scene3dNavigationAgent* value = nullptr;
            ~Candidate() {
                scene3d_nav_agent_destroy(value);
            }
        } candidate;
        void* memory = mem_alloc(sizeof(Scene3dNavigationAgent), MEM_CAT_RENDER);
        if (!memory)
            return fail("navigation agent: allocation failed");
        candidate.value = new (memory) Scene3dNavigationAgent();
        auto* created = candidate.value;
        if (!navigation_follower_create(&desc, &created->follower, &nav_error))
            return fail(nav_error.message);
        // four bounded searches per tick share a round-robin cursor; large assets cannot expand these ceilings.
        struct Budget {
            size_t* value;
            size_t ceiling;
        };
        Budget budgets[] = {{&query.path.search.max_expansions, 2048},
                            {&query.path.search.max_edge_tests, 8192},
                            {&query.path.max_projection_tests, 4096},
                            {&query.path.max_portal_tests, 65536}};
        for (auto budget : budgets)
            if (*budget.value > budget.ceiling)
                *budget.value = budget.ceiling;
        strcpy(created->id, id);
        strcpy(created->asset, asset_id);
        created->body = dom_node_ref(body);
        created->movement = movement;
        created->query = query;
        created->world = scene3d_physics_world_identity(physics);
        created->asset_generation = asset->generation;
        created->foot = scene3d_nav_agent_foot(profile, movement);
        created->position = created->foot;
        created->precision = profile.precision;
        created->avoidance = avoidance;
        if (!state->agents && !scene3d_runtime_bind(state->runtime, SCENE3D_TICK_NAVIGATION, state, scene3d_nav_agents_tick))
            return fail("navigation agent: navigation tick phase already owned");
        created->command = (*state->next_generation)++;
        *slot = created;
        state->agent_count++;
        candidate.value = nullptr;
        scene3d_nav_agents_schedule(state, root);
        return nav_agent_value(state, created);
    }
    if (!agent)
        return fail("navigation agent: unknown ID");
    auto generation = request.get("generation");
    if (!generation.isNull() &&
        (!generation.isInt() || generation.asInt() <= 0 || (uint64_t)generation.asInt() != agent->command))
        return fail("navigation agent: stale command generation");
    if (!strcmp(operation, "navigation_agent_release")) {
        *slot = agent->next;
        state->agent_count--;
        scene3d_nav_agent_destroy(agent);
        if (!state->agents)
            scene3d_runtime_unbind(state->runtime, SCENE3D_TICK_NAVIGATION, state);
        else
            scene3d_nav_agents_schedule(state, root);
        return Item{.item = b2it(true)};
    }
    if (!strcmp(operation, "navigation_agent_link_complete")) {
        if (!valid)
            return fail(diagnostic);
        // observe first: reset, teleport and asset edits must invalidate an old action acknowledgement.
        if (!scene3d_nav_agent_observe(state, root, agent))
            return fail(agent->diagnostic);
        auto path = request.get("path_generation"), action = request.get("action");
        NavigationError error = {};
        NavigationTraversal active = {};
        uint64_t arrival_polygon = 0;
        auto* asset = scene3d_nav_agent_asset(state, agent->asset);
        if (navigation_follower_action(agent->follower, &active))
            for (size_t i = 0; i < navigation_links_count(asset->compiled.links); i++) {
                NavigationLinkAttachment attachment = {};
                navigation_links_attachment(asset->compiled.links, i, &attachment);
                if (attachment.description.id == active.id) {
                    arrival_polygon = active.reverse ? attachment.start_polygon : attachment.end_polygon;
                    break;
                }
            }
        NavigationVec arrival = {};
        PhysicsCharacterMove settle = agent->movement;
        settle.displacement = {};
        PhysicsCharacterState support = {};
        PhysicsError physics_error = {};
        if (!path.isInt() || path.asInt() <= 0 || !action.isInt() || action.asInt() <= 0 || !arrival_polygon ||
            !scene3d_nav_agent_support(asset, agent, arrival_polygon, agent->foot, &arrival) ||
            !scene3d_physics_character_compute(scene3d_physics(root), &settle, &support, &physics_error) ||
            !support.grounded || plen(support.achieved) > 4 * agent->precision ||
            !navigation_follower_complete_link(agent->follower, (uint64_t)path.asInt(), (uint64_t)action.asInt(),
                                               arrival, &error))
            return fail(error.message ? error.message : "navigation agent: invalid path/action acknowledgement");
        // a link changes connected components; resume from its verified arrival face, never the departure hint.
        agent->status = "moving";
        agent->polygon = arrival_polygon;
        agent->position = arrival;
        navigation_follower_read(agent->follower, &agent->observed);
        scene3d_nav_agents_schedule(state, root);
        return nav_agent_value(state, agent);
    }
    if (!strcmp(operation, "navigation_agent_avoidance")) {
        if (!valid)
            return fail(diagnostic);
        if (!request.get("options").isMap())
            return fail("navigation agent: avoidance update requires an options map");
        NavigationAgentAvoidance next = agent->avoidance;
        if (!nav_agent_avoidance_options(request.get("options"), &next))
            return fail("navigation agent: invalid avoidance controls or work budget");
        if (*state->next_generation > INT64_MAX)
            return fail("navigation agent: command generation quota exceeded");
        agent->avoidance = next;
        agent->command = (*state->next_generation)++;
        scene3d_nav_agents_schedule(state, root);
        return nav_agent_value(state, agent);
    }
    bool target = !strcmp(operation, "navigation_agent_target"), cancel = !strcmp(operation, "navigation_agent_cancel");
    if (!target && !cancel)
        return fail("navigation agent: unsupported operation");
    NavigationVec goal = {};
    if (target && (!valid || !navigation_vector(request.get("target"), 3, {}, &goal) || request.get("target").isNull()))
        return fail(!valid ? diagnostic : "navigation agent: requires a finite 3D target");
    if (*state->next_generation > INT64_MAX)
        return fail("navigation agent: command generation quota exceeded");
    scene3d_nav_agent_stop(agent, target ? "queued" : "idle");
    agent->command = (*state->next_generation)++;
    agent->target = target;
    agent->pending = target;
    agent->goal = goal;
    scene3d_nav_agents_schedule(state, root);
    return nav_agent_value(state, agent);
}
static Item nav_bake_operate(Scene3dNavigation* state, DomElement* root, MapReader request, const char* operation,
                             bool valid, const char* diagnostic) {
    auto fail = [](const char* message) {
        return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr));
    };
    if (!strcmp(operation, "navigation_bake_start")) {
        if (!valid)
            return fail(diagnostic);
        if (!scene3d_nav_can_change(state))
            return fail("navigation bake: unavailable during tick sampling");
        const char* id = navigation_text(request.get("asset"));
        Scene3dNavigationAsset* asset = nullptr;
        for (auto* candidate = state->assets; candidate; candidate = candidate->next)
            if (id && !strcmp(candidate->id, id)) {
                asset = candidate;
                break;
            }
        if (!asset || !asset->source_bake)
            return fail("navigation bake: requires a scene source-bake asset");
        auto options = request.get("options");
        if (!options.isNull() && !options.isMap())
            return fail("navigation bake: options must be a map");
        const char* mode = navigation_text(options.asMap().get("mode"));
        if ((!options.asMap().get("mode").isNull() && !mode) || (mode && strcmp(mode, "full") && strcmp(mode, "dirty")))
            return fail("navigation bake: mode must be full or dirty");
        bool dirty = mode && !strcmp(mode, "dirty");
        size_t tile_size = 0, max_dirty_tiles = 0;
        Scene3dBakeJob* base = nullptr;
        NavigationBakeStats base_stats = {};
        if (dirty) {
            auto token = options.asMap().get("base");
            if (!token.isInt() || token.asInt() <= 0 ||
                !navigation_integer(options.asMap().get("tile_size"), 8, NAVIGATION_MAX_NODES, &tile_size) ||
                !tile_size ||
                !navigation_integer(options.asMap().get("max_dirty_tiles"), 64, NAVIGATION_MAX_NODES,
                                    &max_dirty_tiles) ||
                !max_dirty_tiles)
                return fail("navigation bake: dirty mode requires a base token and positive tile quotas");
            for (auto* candidate = state->jobs; candidate; candidate = candidate->next)
                if (candidate->token == (uint64_t)token.asInt())
                    base = candidate;
            // a published source-backed job is an explicit immutable base, even after source edits.
            if (!base || !base->published_generation || strcmp(base->asset, asset->id) ||
                dom_node_ref_validate(state->document, base->root) != root ||
                dom_node_ref_identity_compare(base->asset_ref, asset->source) ||
                strcmp(base->settings_hash, asset->compiled.settings_hash) ||
                !navigation_bake_stats(base->worker.result, &base_stats))
                return fail("navigation bake: base must be a retained published job for this asset and settings");
        } else if (!options.asMap().get("base").isNull() || !options.asMap().get("tile_size").isNull() ||
                   !options.asMap().get("max_dirty_tiles").isNull()) {
            return fail("navigation bake: tile options require dirty mode");
        }
        size_t count = 0, triangles = 0;
        for (auto* before = state->jobs; before; before = before->next) {
            count++;
            triangles += before->charged_triangles;
        }
        if (count >= 8)
            return fail("navigation bake: release completed jobs before exceeding eight retained jobs");
        if (*state->next_generation > INT64_MAX)
            return fail("navigation bake: token quota exceeded");
        void* memory = mem_alloc(sizeof(Scene3dBakeJob), MEM_CAT_RENDER);
        if (!memory)
            return fail("navigation bake: job allocation failed");
        auto* job = new (memory) Scene3dBakeJob();
        char error[256] = {};
        if (!scene3d_nav_source_snapshot(asset, root, &job->worker.geometry, &job->references, error, sizeof(error))) {
            scene3d_nav_job_destroy(job);
            return fail(error);
        }
        job->charged_triangles = job->worker.geometry.triangles.size() + base_stats.source_triangles;
        if (triangles + job->charged_triangles > SCENE3D_BAKE_TRIANGLE_LIMIT) {
            scene3d_nav_job_destroy(job);
            return fail("navigation bake: combined retained snapshot triangle quota exceeded");
        }
        job->definitions.dimensions = asset->compiled.dimensions;
        job->definitions.max_attachment_tests = asset->compiled.max_attachment_tests;
        for (const auto& link : asset->compiled.link_bindings)
            if (!job->definitions.link_bindings.append(link)) {
                scene3d_nav_job_destroy(job);
                return fail("navigation bake: link snapshot allocation failed");
            }
        job->root = dom_node_ref(root);
        job->asset_ref = asset->source;
        job->target_generation = asset->generation;
        job->worker.settings = asset->settings;
        job->worker.base = base ? navigation_bake_retain(base->worker.result) : nullptr;
        job->worker.tile_size = tile_size;
        job->worker.max_dirty_tiles = max_dirty_tiles;
        strcpy(job->asset, asset->id);
        strcpy(job->settings_hash, asset->compiled.settings_hash);
        job->token = (*state->next_generation)++;
        if (!state->pool)
            state->pool = tp_create(1);
        if (!state->pool || !tp_submit(state->pool, scene3d_nav_bake_worker, &job->worker)) {
            scene3d_nav_job_destroy(job);
            return fail("navigation bake: worker submission failed");
        }
        for (auto* before = state->jobs; before; before = before->next)
            if (!strcmp(before->asset, asset->id))
                scene3d_nav_job_stale(before, "navigation bake: superseded by a newer job");
        asset->latest_job = job->token;
        job->next = state->jobs;
        state->jobs = job;
        return nav_job_status(state, job);
    }
    auto token = request.get("token");
    if (!token.isInt() || token.asInt() <= 0)
        return fail("navigation bake: token must be an opaque positive integer");
    Scene3dBakeJob** link = &state->jobs;
    while (*link && (*link)->token != (uint64_t)token.asInt())
        link = &(*link)->next;
    auto* job = *link;
    if (!job)
        return fail("navigation bake: unknown or released token");
    if (!strcmp(operation, "navigation_bake_poll"))
        return nav_job_status(state, job);
    if (!scene3d_nav_can_change(state))
        return fail("navigation bake: unavailable during tick sampling");
    bool done = __atomic_load_n(&job->worker.done, __ATOMIC_ACQUIRE);
    if (!strcmp(operation, "navigation_bake_cancel")) {
        if (!job->published_generation)
            __atomic_store_n(&job->worker.cancelled, true, __ATOMIC_RELAXED);
        return nav_job_status(state, job);
    }
    if (!strcmp(operation, "navigation_bake_release")) {
        if (!done)
            return fail("navigation bake: wait for worker completion before release");
        *link = job->next;
        scene3d_nav_job_destroy(job);
        return Item{.item = b2it(true)};
    }
    if (!strcmp(operation, "navigation_bake_export")) {
        if (!done || !job->worker.success || job->stale || __atomic_load_n(&job->worker.cancelled, __ATOMIC_RELAXED))
            return fail("navigation bake: no exportable completed result");
        return navigation_bake_value(job->worker.result, job->worker.geometry.source_hash, job->settings_hash,
                                     &job->definitions);
    }
    if (strcmp(operation, "navigation_bake_publish"))
        return fail("navigation bake: unsupported job operation");
    if (job->published_generation || job->stale || !done || !job->worker.success ||
        __atomic_load_n(&job->worker.cancelled, __ATOMIC_RELAXED))
        return nav_job_status(state, job);
    if (!valid)
        return fail(diagnostic);
    Scene3dNavigationAsset* asset = nullptr;
    for (auto* candidate = state->assets; candidate; candidate = candidate->next)
        if (!strcmp(candidate->id, job->asset)) {
            asset = candidate;
            break;
        }
    if (dom_node_ref_validate(state->document, job->root) != root || !asset || !asset->source_bake ||
        asset->generation != job->target_generation || asset->latest_job != job->token ||
        strcmp(asset->compiled.settings_hash, job->settings_hash)) {
        scene3d_nav_job_stale(job, "navigation bake: root, target or settings generation changed");
        return nav_job_status(state, job);
    }
    Scene3dNavigationGeometry fresh;
    lam::ArrayList<DomNodeRef> refs;
    char error[256] = {};
    if (!scene3d_nav_source_snapshot(asset, root, &fresh, &refs, error, sizeof(error))) {
        scene3d_nav_job_stale(job, *error ? error : "navigation bake: source validation failed");
        return nav_job_status(state, job);
    }
    bool same = refs.size() == job->references.size() && !strcmp(fresh.source_hash, job->worker.geometry.source_hash);
    for (size_t i = 0; i < refs.size() && same; i++)
        same = dom_node_ref_identity_compare(refs[i], job->references[i]) == 0;
    if (!same) {
        scene3d_nav_job_stale(job, "navigation bake: source snapshot or identity changed");
        return nav_job_status(state, job);
    }
    struct OverlayOwner {
        NavigationLinks* links = nullptr;
        ~OverlayOwner() {
            navigation_links_destroy(links);
        }
    } overlay;
    NavigationSurfaceView surface = {};
    navigation_bake_surface(job->worker.result, &surface);
    NavigationError link_error = {};
    if (!navigation_build_link_overlay(job->definitions, surface, &overlay.links, &link_error))
        return fail(link_error.message);
    const NavigationGraph* graph =
        overlay.links ? navigation_links_graph(overlay.links) : navigation_bake_graph(job->worker.result);
    size_t old_nodes = navigation_node_count(asset->compiled.graph),
           old_edges = navigation_edge_count(asset->compiled.graph),
           nodes = state->stats.nodes - old_nodes + navigation_node_count(graph),
           edges = state->stats.edges - old_edges + navigation_edge_count(graph);
    size_t storage_nodes = state->stats.storage_nodes - scene3d_nav_storage_nodes(asset->compiled) + job->worker.stats.cells +
                           2 * navigation_links_count(overlay.links);
    auto retained = scene3d_nav_retained_bake(asset->compiled);
    size_t raw_cells = state->stats.raw_cells - retained.raw_cells + job->worker.stats.raw_cells;
    size_t source_triangles =
        state->stats.source_triangles - retained.source_triangles + job->worker.stats.source_triangles;
    if (!scene3d_nav_counts_fit(nodes, edges, state->stats.vertices, storage_nodes, raw_cells, source_triangles))
        return fail("navigation bake: combined scene graph/storage quota exceeded");
    if (*state->next_generation > INT64_MAX)
        return fail("navigation bake: generation quota exceeded");
    // publication changes one native owner after all provenance/quota checks, never authored DOM.
    navigation_links_destroy(asset->compiled.links);
    navigation_bake_destroy(asset->compiled.bake);
    asset->compiled.bake = navigation_bake_retain(job->worker.result);
    asset->compiled.links = overlay.links;
    overlay.links = nullptr;
    asset->compiled.graph = const_cast<NavigationGraph*>(graph);
    strcpy(asset->compiled.source_hash, job->worker.geometry.source_hash);
    asset->generation = (*state->next_generation)++;
    job->published_generation = asset->generation;
    state->stats.nodes = nodes;
    state->stats.edges = edges;
    state->stats.storage_nodes = storage_nodes;
    state->stats.raw_cells = raw_cells;
    state->stats.source_triangles = source_triangles;
    state->stats.compilations++;
    state->stats.invalidations += old_nodes != 0;
    return nav_job_status(state, job);
}
Item scene3d_navigation_operate(Scene3dNavigation* state, DomElement* root, Item request) {
    RootFrame roots(1);
    Rooted<Item> rooted(roots, request);
    auto read = ItemReader(rooted.get().to_const()).asMap();
    auto fail = [](const char* message) {
        return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr));
    };
    const char* operation = navigation_text(read.get("operation"));
    char diagnostic[256] = {};
    bool valid = scene3d_navigation_prepare(state, root, diagnostic, sizeof(diagnostic));
    if (operation && !strncmp(operation, "navigation_agent_", 17))
        return nav_agent_operate(state, root, read, operation, valid, diagnostic);
    if (operation && !strncmp(operation, "navigation_bake_", 16))
        return nav_bake_operate(state, root, read, operation, valid, diagnostic);
    if (operation && !strcmp(operation, "navigation_read")) {
        RootedValueBuilder result, assets(true);
        for (auto* asset = state->assets; asset; asset = asset->next) {
            RootedValueBuilder value;
            value.put("id", Item{.item = s2it(heap_create_name(asset->id))});
            value.put("generation", Item{.item = i2it((int64_t)asset->generation)});
            value.put("revision", Item{.item = i2it(asset->revision)});
            value.put("ready", Item{.item = b2it(scene3d_nav_asset_ready(asset))});
            value.put("link_count", Item{.item = i2it((int64_t)asset->compiled.link_bindings.size())});
            assets.append(value.value.get());
        }
        result.put("assets", assets.value.get());
        result.put("compilations", Item{.item = i2it((int64_t)state->stats.compilations)});
        result.put("invalidations", Item{.item = i2it((int64_t)state->stats.invalidations)});
        result.put("queries", Item{.item = i2it((int64_t)state->stats.queries)});
        result.put("nodes", Item{.item = i2it((int64_t)state->stats.nodes)});
        result.put("edges", Item{.item = i2it((int64_t)state->stats.edges)});
        result.put("vertices", Item{.item = i2it((int64_t)state->stats.vertices)});
        result.put("storage_nodes", Item{.item = i2it((int64_t)state->stats.storage_nodes)});
        result.put("raw_cells", Item{.item = i2it((int64_t)state->stats.raw_cells)});
        result.put("source_triangles", Item{.item = i2it((int64_t)state->stats.source_triangles)});
        result.put("rejected", Item{.item = b2it(!valid)});
        result.put("diagnostic", valid ? ItemNull : Item{.item = s2it(heap_create_name(diagnostic))});
        return result.value.get();
    }
    const char* id = navigation_text(read.get("asset"));
    Scene3dNavigationAsset* selected = nullptr;
    if (id)
        for (auto* asset = state->assets; asset; asset = asset->next)
            if (!strcmp(asset->id, id)) {
                selected = asset;
                break;
            }
    auto generation = read.get("generation");
    // generations are opaque integers: conversion through double could alias adjacent int64 values.
    bool current = selected && scene3d_nav_asset_ready(selected) && generation.isInt() && generation.asInt() > 0 &&
                   (uint64_t)generation.asInt() == selected->generation;
    if (operation && !strcmp(operation, "navigation_current"))
        return Item{.item = b2it(valid && current)};
    if (!valid)
        return fail(diagnostic);
    if (!selected)
        return fail("navigation: unknown scene asset");
    if (!scene3d_nav_asset_ready(selected))
        return fail("navigation: source bake has no published current domain");
    if (!generation.isNull() && !current)
        return fail("navigation: stale asset generation");
    if (!operation || strcmp(operation, "navigation_path"))
        return fail("navigation: unsupported scene operation");
    state->stats.queries++;
    RootedValueBuilder result(navigation_path_value(selected->compiled, read.get("start").item(),
                                                    read.get("goal").item(), read.get("options").item()));
    if (get_type_id(result.value.get()) == LMD_TYPE_ERROR)
        return result.value.get();
    result.put("asset", Item{.item = s2it(heap_create_name(selected->id))});
    result.put("generation", Item{.item = i2it((int64_t)selected->generation)});
    result.put("revision", Item{.item = i2it(selected->revision)});
    return result.value.get();
}
