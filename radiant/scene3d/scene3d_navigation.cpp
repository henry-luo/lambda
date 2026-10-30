#include "../../lambda/dom/dom_events.h"
#include "../../lambda/dom/realm/dom_realm.h"
#include "../../lambda/input/css/dom_element.hpp"
#include "../../lambda/input/css/dom_lifecycle.hpp"
// establish parser entry points' C linkage before auxiliary declarations.
#include "../../lambda/input/input.hpp"
#include "../../lambda/input/input-parsers.h"
#include "../../lambda/js/js_runtime.h"
#include "../../lambda/runtime/lambda-error.h"
#include "../../lambda/runtime/lambda-navigation.hpp"
#include "../../lambda/runtime/lambda-number-runtime.hpp"
#include "../../lambda/runtime/value_builder.hpp"
#include "../../lib/mem.h"
#include "../../lib/physics/physics_math.h"
#include "../../lib/thread_pool.h"
#include "../event.hpp"
#include "scene3d.hpp"
#include "scene3d_physics.hpp"
#include "scene3d_source.hpp"
#include <float.h>
#include <math.h>

struct Scene3dNavigationSource {
    char id[128] = {};
    bool walkable = true;
    uint32_t groups = UINT32_MAX;
    double cost = 1;
};
// this is the entire worker boundary: no DOM refs, Item values or script callbacks.
struct Scene3dBakeWorker {
    Scene3dNavigationGeometry geometry;
    NavigationBakeSettings settings = {};
    NavigationBake* result = nullptr;
    NavigationBake* base = nullptr;
    size_t tile_size = 0, max_dirty_tiles = 0;
    NavigationBakeStats stats = {};
    bool cancelled = false, started = false, done = false, success = false;
    unsigned stage = 0;
    size_t work = 0;
    char diagnostic[256] = {};
    ~Scene3dBakeWorker() {
        navigation_bake_destroy(result);
        navigation_bake_destroy(base);
    }
};
struct Scene3dBakeJob {
    Scene3dBakeJob* next = nullptr;
    Scene3dBakeWorker worker;
    DomNodeRef root = {}, asset_ref = {};
    lam::ArrayList<DomNodeRef> references;
    NavigationCompiled definitions;
    char asset[128] = {}, settings_hash[65] = {}, diagnostic[256] = {};
    uint64_t token = 0, target_generation = 0, published_generation = 0;
    size_t charged_triangles = 0;
    bool stale = false;
};

struct Scene3dNavigationAsset {
    Scene3dNavigationAsset* next = nullptr;
    DomNodeRef source = {};
    char id[128] = {};
    uint64_t key = 0, generation = 0;
    uint32_t revision = 0;
    NavigationCompiled compiled;
    bool source_bake = false;
    uint64_t geometry_key = 0, latest_job = 0;
    NavigationBakeSettings settings = {};
    lam::ArrayList<Scene3dNavigationSource> sources;
};
static constexpr size_t NAV_AGENT_LIMIT = NAVIGATION_AVOIDANCE_MAX_AGENTS, NAV_AGENT_SEARCHES = 4,
                        NAV_AGENT_ROUTE_LIMIT = 4096;
struct NavigationAgentAvoidance {
    bool enabled = true;
    double priority = .5, time_horizon = 2, max_speed = 2;
    size_t max_neighbors = NAVIGATION_AVOIDANCE_MAX_AGENTS - 1;
    size_t max_neighbor_tests = NAVIGATION_AVOIDANCE_MAX_AGENTS - 1, max_solver_tests = 1024;
};
struct Scene3dNavigationAgent {
    Scene3dNavigationAgent* next = nullptr;
    DomNodeRef body = {};
    char id[128] = {}, asset[128] = {}, diagnostic[256] = {};
    uint64_t command = 0, path = 0, asset_generation = 0, world = 0, polygon = 0, queries = 0;
    PhysicsCharacterMove movement = {};
    NavigationSurfaceQuery query = {};
    NavigationVec goal = {}, foot = {}, position = {};
    NavigationFollower* follower = nullptr;
    NavigationVec preferred_velocity = {}, achieved_velocity = {};
    NavigationAgentAvoidance avoidance = {};
    NavigationVec avoidance_velocity = {};
    NavigationAvoidanceStatus avoidance_status = NAVIGATION_AVOIDANCE_DISABLED;
    size_t avoidance_neighbors = 0, avoidance_neighbor_tests = 0, avoidance_solver_tests = 0;
    NavigationFollowState observed = {};
    const char* status = "idle";
    bool target = false, pending = false;
    double precision = 0;
    ~Scene3dNavigationAgent() {
        navigation_follower_destroy(follower);
    }
};
struct Scene3dNavigation {
    DomDocument* document = nullptr;
    DomNodeRef root = {};
    Scene3dRuntime* runtime = nullptr;
    Scene3dNavigationAsset* assets = nullptr;
    Scene3dNavigationStats stats = {};
    uint64_t epoch = UINT64_MAX, rejected_epoch = UINT64_MAX;
    uint64_t* next_generation = nullptr;
    char diagnostic[256] = {};
    ThreadPool* pool = nullptr;
    Scene3dBakeJob* jobs = nullptr;
    Scene3dNavigationAgent* agents = nullptr;
    size_t agent_count = 0, next_agent = 0;
};
static Scene3dTickResult nav_agents_tick(void* owner, double delta);
bool scene3d_navigation_body_owned(Scene3dNavigation* state, DomElement* body) {
    if (state && body) {
        for (auto* agent = state->agents; agent; agent = agent->next) {
            if (!dom_node_ref_identity_compare(agent->body, dom_node_ref(body))) {
                return true;
            }
        }
    }
    return false;
}
static bool nav_agents_schedule(Scene3dNavigation* state, DomElement* root);
static void nav_agent_destroy(Scene3dNavigationAgent* agent) {
    if (agent) {
        agent->~Scene3dNavigationAgent();
        mem_free(agent);
    }
}
static void nav_job_destroy(Scene3dBakeJob* job) {
    if (job) {
        job->~Scene3dBakeJob();
        mem_free(job);
    }
}
static void nav_job_stale(Scene3dBakeJob* job, const char* message) {
    if (job->published_generation)
        return;
    job->stale = true;
    str_copy(job->diagnostic, sizeof(job->diagnostic), message, strlen(message));
    __atomic_store_n(&job->worker.cancelled, true, __ATOMIC_RELAXED);
}
static size_t nav_source_views(const Scene3dNavigationAsset* asset, Scene3dNavigationSelection* sources) {
    size_t i = 0;
    for (const auto& source : asset->sources)
        sources[i++] = {source.id, source.walkable, source.groups, source.cost};
    return i;
}
static bool nav_source_key(const Scene3dNavigationAsset* asset, DomElement* root, uint64_t* key, char* diagnostic,
                           size_t capacity) {
    Scene3dNavigationSelection sources[64];
    size_t count = nav_source_views(asset, sources);
    return scene3d_navigation_geometry_key(root, sources, count, key, diagnostic, capacity);
}
static bool nav_source_snapshot(const Scene3dNavigationAsset* asset, DomElement* root,
                                Scene3dNavigationGeometry* geometry, lam::ArrayList<DomNodeRef>* refs, char* diagnostic,
                                size_t capacity) {
    Scene3dNavigationSelection sources[64];
    size_t count = nav_source_views(asset, sources);
    return scene3d_collect_navigation_geometry(root, sources, count, geometry, refs, diagnostic, capacity);
}
static bool nav_asset_ready(const Scene3dNavigationAsset* asset) {
    return !asset->source_bake || asset->compiled.bake;
}
static size_t nav_storage_nodes(const NavigationCompiled& compiled) {
    if (!compiled.bake)
        return navigation_node_count(compiled.graph);
    // merged polygons still retain their backing cells; graph compression cannot expand the cache budget.
    NavigationBakeStats stats = {};
    navigation_bake_stats(compiled.bake, &stats);
    return stats.cells + 2 * navigation_links_count(compiled.links);
}
static NavigationBakeStats nav_retained_bake(const NavigationCompiled& compiled) {
    NavigationBakeStats stats = {};
    navigation_bake_stats(compiled.bake, &stats);
    return stats;
}
static bool nav_counts_fit(size_t nodes, size_t edges, size_t vertices, size_t storage_nodes, size_t raw_cells,
                           size_t source_triangles) {
    return nodes <= NAVIGATION_MAX_NODES && edges <= NAVIGATION_MAX_EDGES && vertices <= NAVIGATION_MAX_VERTICES &&
           storage_nodes <= NAVIGATION_MAX_NODES && raw_cells <= NAVIGATION_MAX_NODES &&
           source_triangles <= SCENE3D_BAKE_TRIANGLE_LIMIT;
}
bool scene3d_navigation_tag(DomElement* node) {
    const char* tag = node->local_name();
    return !strncmp(tag, "navigation-", 11) && navigation_kind(tag) != NAVIGATION_INVALID;
}
static void nav_asset_destroy(Scene3dNavigationAsset* asset) {
    if (asset) {
        asset->~Scene3dNavigationAsset();
        mem_free(asset);
    }
}
Scene3dNavigation* scene3d_navigation_create(DomElement* root, Scene3dRuntime* runtime) {
    uint64_t* sequence = scene3d_navigation_sequence(root->doc);
    if (!sequence)
        return nullptr;
    void* memory = mem_alloc(sizeof(Scene3dNavigation), MEM_CAT_RENDER);
    if (!memory)
        return nullptr;
    auto* state = new (memory) Scene3dNavigation();
    state->document = root->doc;
    state->root = dom_node_ref(root);
    state->runtime = runtime;
    state->next_generation = sequence;
    return state;
}
void scene3d_navigation_destroy(Scene3dNavigation* state) {
    if (!state)
        return;
    scene3d_runtime_unbind(state->runtime, SCENE3D_TICK_NAVIGATION, state);
    while (state->agents) {
        auto* agent = state->agents;
        state->agents = agent->next;
        nav_agent_destroy(agent);
    }
    for (auto* job = state->jobs; job; job = job->next)
        __atomic_store_n(&job->worker.cancelled, true, __ATOMIC_RELAXED);
    // joining precedes any native input/result teardown; workers never borrow the document.
    tp_destroy(state->pool);
    while (state->jobs) {
        auto* job = state->jobs;
        state->jobs = job->next;
        nav_job_destroy(job);
    }
    while (state->assets) {
        auto* asset = state->assets;
        state->assets = asset->next;
        nav_asset_destroy(asset);
    }
    state->~Scene3dNavigation();
    mem_free(state);
}
static bool nav_asset_placement(DomElement* root, DomElement* node, unsigned depth, unsigned* work) {
    if (depth > 64 || ++*work > 8192)
        return false;
    if (scene3d_navigation_tag(node) &&
        (!node->parent || !node->parent->is_element() ||
         strcmp(node->parent->as_element()->local_name(), "resources") || node->parent->parent != root))
        return false;
    for (DomNode* child = node->first_child; child; child = child->next_sibling)
        if (child->is_element() && !nav_asset_placement(root, child->as_element(), depth + 1, work))
            return false;
    return true;
}
static bool nav_source_compile(Scene3dNavigationAsset* asset, DomElement* root, ItemReader source, char* diagnostic,
                               size_t capacity) {
    auto fail = [&](const char* message) {
        str_copy(diagnostic, capacity, message, strlen(message));
        return false;
    };
    auto list = navigation_field(source, "sources");
    NavigationError error = {};
    double version;
    const char* kind = source.isElement() ? source.asElement().tagName() : navigation_text(source.asMap().get("type"));
    if (navigation_kind(kind) != NAVIGATION_BAKE ||
        !navigation_number(navigation_field(source, "version"), 1, 1, &version) || version != 1 ||
        !navigation_field(source, "triangles").isNull() || !navigation_field(source, "format").isNull() ||
        !list.isArray() || !list.asArray().length() || list.asArray().length() > 64 ||
        !navigation_bake_settings_read(navigation_field(source, "settings"), &asset->settings))
        return fail("navigation: source bake requires schema 1, settings and 1..64 selections");
    if (!navigation_bake_settings_valid(&asset->settings, nullptr, nullptr, &error))
        return fail(error.message);
    for (int64_t i = 0; i < list.asArray().length(); i++) {
        auto value = list.asArray().get(i);
        const char* id = navigation_text(value.isMap() ? value.asMap().get("id") : value);
        Scene3dNavigationSource entry;
        size_t groups;
        if (!id || !*id || strlen(id) >= sizeof(entry.id) ||
            !navigation_boolean(value.asMap().get("walkable"), true, &entry.walkable) ||
            !navigation_integer(value.asMap().get("groups"), UINT32_MAX, UINT32_MAX, &groups) ||
            !navigation_number(value.asMap().get("cost"), 1, DBL_MAX, &entry.cost))
            return fail("navigation: invalid source selection");
        for (const auto& before : asset->sources)
            if (!strcmp(before.id, id))
                return fail("navigation: duplicate source selection");
        strcpy(entry.id, id);
        entry.groups = (uint32_t)groups;
        if (!asset->sources.append(entry))
            return fail("navigation: selection allocation failed");
    }
    if (!nav_source_key(asset, root, &asset->geometry_key, diagnostic, capacity))
        return false;
    asset->source_bake = true;
    asset->compiled.kind = NAVIGATION_BAKE;
    asset->compiled.dimensions = 3;
    if (!navigation_links_read(&asset->compiled, source))
        return fail(asset->compiled.diagnostic);
    return navigation_bake_settings_hash(&asset->settings, asset->compiled.settings_hash) ||
           fail("navigation: settings fingerprint failed");
}
static bool nav_source_definition_equal(const Scene3dNavigationAsset* a, const Scene3dNavigationAsset* b) {
    if (!a->source_bake || !b->source_bake)
        return false;
    if (strcmp(a->compiled.settings_hash, b->compiled.settings_hash) || a->sources.size() != b->sources.size())
        return false;
    for (size_t i = 0; i < a->sources.size(); i++) {
        const auto &x = a->sources[i], &y = b->sources[i];
        if (strcmp(x.id, y.id) || x.walkable != y.walkable || x.groups != y.groups || x.cost != y.cost)
            return false;
    }
    return true;
}
static bool nav_asset_equal(const Scene3dNavigationAsset* a, const Scene3dNavigationAsset* b) {
    if (a->source_bake != b->source_bake)
        return false;
    if (!a->source_bake)
        return navigation_compiled_equal(a->compiled, b->compiled);
    return nav_source_definition_equal(a, b) && navigation_link_declarations_equal(a->compiled, b->compiled);
}
static bool nav_asset_compile(Scene3dNavigationAsset* asset, DomElement* root, DomElement* node, char* diagnostic,
                              size_t capacity) {
    auto fail = [&](const char* message) {
        str_copy(diagnostic, capacity, message, strlen(message));
        return false;
    };
    const char* transforms[] = {"position", "rotation", "quaternion", "scale", "matrix"};
    for (const char* key : transforms)
        if (node->has_attribute(key) || !scene3d_value(node, key).isNull())
            return fail("navigation: assets use world coordinates and cannot have object transforms");
    for (DomNode* child = node->first_child; child; child = child->next_sibling)
        if (child->is_element() ||
            (child->is_text() && child->as_text()->native_string &&
             strspn(child->as_text()->native_string->chars, " \r\n\t") != child->as_text()->native_string->len))
            return fail("navigation: assets have no child content");
    double version;
    if (!scene3d_real(node, "version", 1, &version) || version != 1)
        return fail("navigation: asset requires schema version 1");
    struct Scratch {
        Pool* pool = nullptr;
        ~Scratch() {
            if (pool)
                pool_destroy(pool);
        }
    } scratch;
    auto data = scene3d_value(node, "data");
    ItemReader source;
    const char* json = data.isString() ? data.asString()->chars : data.isNull() ? scene3d_text(node, "data") : nullptr;
    if (!data.isNull() || json) {
        if (json) {
            if (strlen(json) > 16 * 1024 * 1024 || (data.isString() && strlen(json) != data.asString()->len))
                return fail("navigation: asset JSON byte quota exceeded or embedded NUL");
            scratch.pool = pool_create();
            if (!scratch.pool)
                return fail("navigation: JSON scratch allocation failed");
            Input* input = Input::create(scratch.pool);
            if (!input)
                return fail("navigation: JSON input allocation failed");
            bool valid = false;
            Item parsed = parse_json_to_item_strict(input, json, &valid);
            if (!valid || get_type_id(parsed) != LMD_TYPE_MAP)
                return fail("navigation: data requires a JSON object");
            source = ItemReader(parsed.to_const());
        } else if (data.isMap())
            source = data;
        else
            return fail("navigation: data requires a map or JSON object string");
    } else {
        Element* backing = dom_element_backing(node);
        if (!backing)
            return fail("navigation: asset has no authored data");
        source = ItemReader(Item{.element = backing}.to_const());
    }
    if (!navigation_field(source, "sources").isNull()) {
        if (strcmp(node->local_name(), "navigation-bake"))
            return fail("navigation: only bake assets accept source selections");
        return nav_source_compile(asset, root, source, diagnostic, capacity);
    }
    if (!navigation_compile(&asset->compiled, source))
        return fail(asset->compiled.diagnostic);
    if (strcmp(node->local_name(), navigation_kind_tag(asset->compiled.kind)))
        return fail("navigation: asset tag and data kind disagree");
    return true;
}
bool scene3d_navigation_prepare(Scene3dNavigation* state, DomElement* root, char* diagnostic, size_t capacity) {
    if (!state || state->document != root->doc)
        return false;
    auto report = [&]() {
        str_copy(diagnostic, capacity, state->diagnostic, strlen(state->diagnostic));
        return false;
    };
    if (state->epoch == root->doc->mutation_epoch)
        return nav_agents_schedule(state, root);
    if (state->rejected_epoch == root->doc->mutation_epoch)
        return report();
    auto reject = [&](const char* message) {
        str_copy(state->diagnostic, sizeof(state->diagnostic), message, strlen(message));
        state->rejected_epoch = root->doc->mutation_epoch;
        return report();
    };
    Scene3dRuntimeStats clock = {};
    scene3d_runtime_stats(state->runtime, &clock);
    if (!scene3d_runtime_can_configure(state->runtime, clock.timestep, 1)) {
        // sampling is transient; retry this same authored epoch after the tick finishes.
        const char* message = "navigation: authored edits are unavailable during tick sampling";
        str_copy(diagnostic, capacity, message, strlen(message));
        return false;
    }
    unsigned work = 0;
    if (!nav_asset_placement(root, root, 0, &work))
        return reject("navigation: assets belong directly to scene resources; scene depth/node quota exceeded");
    struct Plan {
        Scene3dNavigationAsset* chosen;
        Scene3dNavigationAsset* created;
        DomNodeRef source;
        uint64_t key;
        uint32_t revision;
        uint64_t geometry_key;
    };
    struct Plans {
        lam::ArrayList<Plan> values;
        ~Plans() {
            for (const auto& plan : values)
                nav_asset_destroy(plan.created);
        }
    } plans;
    size_t nodes = 0, edges = 0, vertices = 0, storage_nodes = 0, raw_cells = 0, source_triangles = 0;
    uint64_t next_generation = *state->next_generation;
    for (DomNode* resource = root->first_child; resource; resource = resource->next_sibling) {
        if (!resource->is_element() || strcmp(resource->as_element()->local_name(), "resources"))
            continue;
        for (DomNode* child = resource->as_element()->first_child; child; child = child->next_sibling) {
            if (!child->is_element() || !scene3d_navigation_tag(child->as_element()))
                continue;
            auto* node = child->as_element();
            const char* id = scene3d_text(node, "id");
            if (!id || !*id || strlen(id) >= 128 || plans.values.size() >= 64)
                return reject("navigation: assets require IDs shorter than 128 bytes and at most 64 definitions");
            for (const auto& plan : plans.values)
                if (!strcmp(plan.chosen->id, id))
                    return reject("navigation: duplicate asset ID");
            uint64_t key = 5381;
            unsigned work = 0;
            if (!scene3d_source_key(node, &key, &work, 0, false))
                return reject("navigation: source quota exceeded");
            double revision;
            if (!scene3d_real(node, "revision", 0, &revision) || revision < 0 || revision > UINT32_MAX ||
                floor(revision) != revision)
                return reject("navigation: invalid asset revision");
            auto ref = dom_node_ref(node);
            Scene3dNavigationAsset* before = nullptr;
            for (auto* asset = state->assets; asset; asset = asset->next)
                if (!strcmp(asset->id, id)) {
                    before = asset;
                    break;
                }
            bool same =
                before && before->source.address == ref.address && before->source.expected_id == ref.expected_id;
            Scene3dNavigationAsset *chosen = before, *created = nullptr;
            uint64_t geometry_key = before ? before->geometry_key : 0;
            bool source_changed = false;
            if (same && before->source_bake && before->key == key && before->revision == (uint32_t)revision) {
                uint64_t current;
                if (!nav_source_key(before, root, &current, state->diagnostic, sizeof(state->diagnostic))) {
                    state->rejected_epoch = root->doc->mutation_epoch;
                    return report();
                }
                source_changed = current != before->geometry_key;
            }
            if (!same || before->key != key || before->revision != (uint32_t)revision || source_changed) {
                void* memory = mem_alloc(sizeof(Scene3dNavigationAsset), MEM_CAT_RENDER);
                if (!memory)
                    return reject("navigation: asset allocation failed");
                created = new (memory) Scene3dNavigationAsset();
                strcpy(created->id, id);
                state->stats.compilations++;
                if (!nav_asset_compile(created, root, node, state->diagnostic, sizeof(state->diagnostic))) {
                    nav_asset_destroy(created);
                    state->rejected_epoch = root->doc->mutation_epoch;
                    return report();
                }
                geometry_key = created->geometry_key;
                bool equivalent = same && before->revision == (uint32_t)revision && nav_asset_equal(before, created);
                bool same_source =
                    same && before->revision == (uint32_t)revision && nav_source_definition_equal(before, created);
                if (same_source) {
                    bool geometry_same = before->geometry_key == geometry_key;
                    if (!geometry_same && before->compiled.bake) {
                        Scene3dNavigationGeometry fresh;
                        if (!nav_source_snapshot(created, root, &fresh, nullptr, state->diagnostic,
                                                 sizeof(state->diagnostic))) {
                            nav_asset_destroy(created);
                            state->rejected_epoch = root->doc->mutation_epoch;
                            return report();
                        }
                        geometry_same = !strcmp(fresh.source_hash, before->compiled.source_hash);
                    }
                    equivalent = equivalent && geometry_same;
                    if (!equivalent && geometry_same && before->compiled.bake) {
                        // link-only edits retain the published geometry and replace only connectivity.
                        created->compiled.bake = navigation_bake_retain(before->compiled.bake);
                        created->compiled.graph =
                            const_cast<NavigationGraph*>(navigation_bake_graph(created->compiled.bake));
                        strcpy(created->compiled.source_hash, before->compiled.source_hash);
                        if (!navigation_attach_links(&created->compiled)) {
                            str_copy(state->diagnostic, sizeof(state->diagnostic), created->compiled.diagnostic,
                                     strlen(created->compiled.diagnostic));
                            nav_asset_destroy(created);
                            state->rejected_epoch = root->doc->mutation_epoch;
                            return report();
                        }
                    }
                }
                if (equivalent) {
                    nav_asset_destroy(created);
                    created = nullptr;
                } else {
                    if (next_generation > INT64_MAX) {
                        nav_asset_destroy(created);
                        return reject("navigation: generation quota exceeded");
                    }
                    created->generation = next_generation++;
                    chosen = created;
                }
            }
            nodes += navigation_node_count(chosen->compiled.graph);
            edges += navigation_edge_count(chosen->compiled.graph);
            vertices += navigation_mesh_vertex_count(chosen->compiled.mesh);
            storage_nodes += nav_storage_nodes(chosen->compiled);
            auto retained = nav_retained_bake(chosen->compiled);
            raw_cells += retained.raw_cells;
            source_triangles += retained.source_triangles;
            if (!nav_counts_fit(nodes, edges, vertices, storage_nodes, raw_cells, source_triangles)) {
                nav_asset_destroy(created);
                return reject("navigation: combined scene graph/vertex/storage quota exceeded");
            }
            if (!plans.values.append({chosen, created, ref, key, (uint32_t)revision, geometry_key})) {
                nav_asset_destroy(created);
                return reject("navigation: reconciliation allocation failed");
            }
        }
    }
    // all candidates validate before any old owner is released; future jobs must invalidate at this boundary.
    for (auto* asset = state->assets; asset;) {
        auto* next = asset->next;
        bool kept = false;
        for (const auto& plan : plans.values)
            kept |= plan.chosen == asset;
        if (!kept) {
            for (auto* job = state->jobs; job; job = job->next)
                if (!strcmp(job->asset, asset->id) && job->target_generation == asset->generation)
                    nav_job_stale(job, "navigation bake: target or source generation changed");
            state->stats.invalidations++;
            nav_asset_destroy(asset);
        }
        asset = next;
    }
    state->assets = nullptr;
    Scene3dNavigationAsset** tail = &state->assets;
    for (auto& plan : plans.values) {
        auto* asset = plan.chosen;
        asset->source = plan.source;
        asset->key = plan.key;
        asset->revision = plan.revision;
        asset->geometry_key = plan.geometry_key;
        asset->next = nullptr;
        *tail = asset;
        tail = &asset->next;
        plan.created = nullptr;
    }
    state->stats.assets = plans.values.size();
    state->stats.nodes = nodes;
    state->stats.edges = edges;
    state->stats.vertices = vertices;
    state->stats.storage_nodes = storage_nodes;
    state->stats.raw_cells = raw_cells;
    state->stats.source_triangles = source_triangles;
    *state->next_generation = next_generation;
    state->epoch = root->doc->mutation_epoch;
    state->rejected_epoch = UINT64_MAX;
    state->diagnostic[0] = 0;
    return nav_agents_schedule(state, root);
}
void scene3d_navigation_follow_rebuild(Scene3dNavigation* state, DomNode* old_root, DomNode* new_root) {
    if (!state)
        return;
    view_state_ref_follow_rebuild(state->document, &state->root, old_root, new_root);
    for (auto* asset = state->assets; asset; asset = asset->next)
        view_state_ref_follow_rebuild(state->document, &asset->source, old_root, new_root);
    for (auto* agent = state->agents; agent; agent = agent->next)
        view_state_ref_follow_rebuild(state->document, &agent->body, old_root, new_root);
    state->epoch = state->rejected_epoch = UINT64_MAX;
}
bool scene3d_navigation_stats(Scene3dNavigation* state, Scene3dNavigationStats* stats) {
    if (!state || !stats)
        return false;
    *stats = state->stats;
    return true;
}
bool scene3d_navigation_generation(Scene3dNavigation* state, const char* id, uint64_t* generation) {
    if (!state || !id || !generation)
        return false;
    for (auto* asset = state->assets; asset; asset = asset->next)
        if (!strcmp(asset->id, id)) {
            *generation = asset->generation;
            return true;
        }
    return false;
}
static void nav_bake_worker(void* opaque) {
    auto* worker = (Scene3dBakeWorker*)opaque;
    __atomic_store_n(&worker->started, true, __ATOMIC_RELAXED);
    auto proceed = [](void* owner, NavigationBakeStage stage, size_t work) {
        auto* state = (Scene3dBakeWorker*)owner;
        __atomic_store_n(&state->stage, (unsigned)stage, __ATOMIC_RELAXED);
        __atomic_store_n(&state->work, work, __ATOMIC_RELAXED);
        return !__atomic_load_n(&state->cancelled, __ATOMIC_RELAXED);
    };
    NavigationBakeDesc desc = {worker->geometry.triangles.data(), worker->geometry.triangles.size(), worker->settings,
                               proceed, worker};
    NavigationError error = {};
    if (worker->base) {
        NavigationBakeUpdate update = {worker->base, worker->tile_size, worker->max_dirty_tiles};
        worker->success = navigation_bake_update(&desc, &update, &worker->result, &error);
    } else {
        worker->success = navigation_bake_create(&desc, &worker->result, &error);
    }
    if (worker->success)
        navigation_bake_stats(worker->result, &worker->stats);
    else if (error.message)
        str_copy(worker->diagnostic, sizeof(worker->diagnostic), error.message, strlen(error.message));
    // release/acquire publishes native result/error storage; the worker touches nothing afterward.
    __atomic_store_n(&worker->done, true, __ATOMIC_RELEASE);
}
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
static bool nav_can_change(Scene3dNavigation* state) {
    Scene3dRuntimeStats clock = {};
    scene3d_runtime_stats(state->runtime, &clock);
    return scene3d_runtime_can_configure(state->runtime, clock.timestep, 1);
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
bool scene3d_navigation_has_agents(Scene3dNavigation* state) {
    return state && state->agents;
}
static bool nav_agents_schedule(Scene3dNavigation* state, DomElement* root) {
    if (!state->agents)
        return true;
    bool active = false;
    for (auto* agent = state->agents; agent; agent = agent->next)
        active |= agent->target;
    return scene3d_runtime_enable(state->runtime, SCENE3D_TICK_NAVIGATION, state,
                                  active && scene3d_physics_playing(scene3d_physics(root)));
}
static Scene3dNavigationAsset* nav_agent_asset(Scene3dNavigation* state, const char* id) {
    for (auto* asset = state->assets; asset; asset = asset->next)
        if (!strcmp(asset->id, id))
            return asset;
    return nullptr;
}
static void nav_agent_stop(Scene3dNavigationAgent* agent, const char* status, const char* diagnostic = nullptr) {
    agent->status = status;
    agent->pending = false;
    agent->polygon = 0;
    str_copy(agent->diagnostic, sizeof(agent->diagnostic), diagnostic ? diagnostic : "",
             diagnostic ? strlen(diagnostic) : 0);
    navigation_follower_cancel(agent->follower);
    agent->observed = {};
    agent->preferred_velocity = {};
    agent->avoidance_velocity = {};
}
static NavigationVec nav_agent_foot(const PhysicsCharacterProfile& profile, const PhysicsCharacterMove& move) {
    return {profile.shape_pose.position.x,
            profile.shape_pose.position.y - profile.height * .5 - profile.radius - move.skin,
            profile.shape_pose.position.z};
}
static bool nav_agent_profile(Scene3dNavigationAsset* asset, const PhysicsCharacterProfile& profile,
                              const PhysicsCharacterMove& move, NavigationSurfaceQuery* query,
                              const char** diagnostic) {
    NavigationBakeSettings settings = {};
    NavigationSurfaceView view = {};
    NavigationError error = {};
    PhysicsVec axis = prot(profile.shape_pose.rotation, {0, 1, 0});
    double radius = profile.radius + move.skin, height = profile.height + 2 * radius;
    if (!asset || !nav_asset_ready(asset) || !asset->compiled.bake || asset->compiled.dimensions != 3 ||
        !navigation_bake_settings(asset->compiled.bake, &settings) ||
        !navigation_bake_surface(asset->compiled.bake, &view)) {
        *diagnostic = "navigation agent: requires a published 3D clearance bake";
        return false;
    }
    if (move.up.x != 0 || move.up.y <= 0 || move.up.z != 0 || fabs(axis.y) < 1 - 1e-8 ||
        settings.max_slope > move.slope_angle) {
        *diagnostic = "navigation agent: requires an upright Y-up capsule and compatible slope limit";
        return false;
    }
    // script options cannot understate the actual controller's footprint or step requirement.
    query->radius = radius;
    query->height = height;
    query->max_step = move.step_height;
    if (!navigation_surface_profile_valid(&view, query, &error)) {
        *diagnostic = error.message;
        return false;
    }
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
        auto* asset = nav_agent_asset(state, agent->asset);
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
static bool nav_agent_event(Scene3dNavigation* state, DomElement* root, Scene3dNavigationAgent* agent,
                            const char* type) {
    NavigationAgentEvent event = {state, agent, type};
    return radiant_queue_timing_event((UiContext*)state->document->js.host_ui_context, root, nav_agent_event_value,
                                      &event, type);
}
static bool nav_agent_observe(Scene3dNavigation* state, DomElement* root, Scene3dNavigationAgent* agent) {
    auto* physics = scene3d_physics(root);
    auto* body = dom_node_ref_validate(state->document, agent->body);
    uint64_t identity = body && body->is_element() ? scene3d_physics_character_body(physics, body->as_element()) : 0;
    PhysicsCharacterProfile profile = {};
    PhysicsError error = {};
    if (!identity || !scene3d_physics_character_profile(physics, identity, &profile, &error)) {
        nav_agent_stop(agent, "invalid",
                       error.message ? error.message : "navigation agent: body detached from this world");
        return false;
    }
    agent->movement.body = identity;
    agent->foot = nav_agent_foot(profile, agent->movement);
    agent->precision = profile.precision;
    auto* asset = nav_agent_asset(state, agent->asset);
    const char* diagnostic = nullptr;
    if (!nav_agent_profile(asset, profile, agent->movement, &agent->query, &diagnostic)) {
        nav_agent_stop(agent, "invalid", diagnostic);
        return false;
    }
    uint64_t world = scene3d_physics_world_identity(physics);
    if (world != agent->world || asset->generation != agent->asset_generation) {
        nav_agent_stop(agent, agent->target ? "queued" : "idle");
        agent->pending = agent->target;
        agent->world = world;
        agent->asset_generation = asset->generation;
        agent->movement.platform = 0;
    }
    return true;
}
static bool nav_agent_support(Scene3dNavigationAsset* asset, Scene3dNavigationAgent* agent, uint64_t polygon,
                              NavigationVec foot, NavigationVec* position) {
    NavigationBakeSupport support = {};
    NavigationBakeSettings settings = {};
    if (!navigation_bake_support(asset->compiled.bake, polygon, foot, &support) ||
        !navigation_bake_settings(asset->compiled.bake, &settings))
        return false;
    double tolerance = 4 * agent->precision,
           above = settings.max_step + agent->query.radius * (1 / cos(settings.max_slope) - 1);
    // raster span bounds support below the flattened floor; steps and normal capsule clearance bound it above.
    if (hypot(foot.x - support.position.x, foot.z - support.position.z) > tolerance ||
        foot.y < support.low - tolerance || foot.y > support.position.y + above + tolerance ||
        foot.y + agent->query.height > support.ceiling + tolerance)
        return false;
    *position = support.position;
    return true;
}
static bool nav_agent_search(Scene3dNavigation* state, Scene3dNavigationAgent* agent) {
    auto* asset = nav_agent_asset(state, agent->asset);
    NavigationError error = {};
    struct Route {
        NavigationMeshPath value = {};
        ~Route() {
            navigation_mesh_path_clear(&value);
        }
    } route;
    agent->queries++;
    state->stats.queries++;
    agent->pending = false;
    NavigationSurfaceView surface = {};
    navigation_bake_surface(asset->compiled.bake, &surface);
    size_t tests = 0;
    uint64_t start = 0;
    double best = INFINITY;
    for (uint64_t polygon = 1; polygon <= surface.polygon_count; polygon++) {
        if (tests == agent->query.path.max_projection_tests) {
            nav_agent_stop(agent, "budget_exhausted", "navigation agent: support projection budget exhausted");
            return false;
        }
        tests++;
        NavigationVec position;
        if (!navigation_node_admitted(surface.graph, polygon, &agent->query.path.search) ||
            !nav_agent_support(asset, agent, polygon, agent->foot, &position))
            continue;
        double distance = fabs(position.y - agent->foot.y);
        if (distance < best) {
            best = distance;
            start = polygon;
            agent->position = position;
        }
    }
    if (!start) {
        nav_agent_stop(agent, "off_surface", "navigation agent: achieved foot has no compatible baked support layer");
        return false;
    }
    auto query = agent->query;
    query.path.start = agent->position;
    query.path.goal = agent->goal;
    query.path.max_projection_tests -= tests;
    if (!navigation_surface_find_compiled(asset->compiled, &query, &route.value, &error)) {
        nav_agent_stop(agent, "blocked", error.message);
        return false;
    }
    if (!route.value.point_count) {
        nav_agent_stop(agent, route.value.corridor.status == NAVIGATION_EXHAUSTED ? "budget_exhausted" : "unreachable");
        return false;
    }
    if (route.value.point_count > NAV_AGENT_ROUTE_LIMIT || route.value.traversal_count > 128) {
        nav_agent_stop(agent, "budget_exhausted", "navigation agent: copied route quota exceeded");
        return false;
    }
    if (*state->next_generation > INT64_MAX) {
        nav_agent_stop(agent, "blocked", "navigation agent: path generation quota exceeded");
        return false;
    }
    if (!navigation_follower_assign(agent->follower, &route.value, *state->next_generation, &error)) {
        nav_agent_stop(agent, "blocked", error.message);
        return false;
    }
    agent->path = (*state->next_generation)++;
    agent->polygon = route.value.start_polygon;
    agent->status = "moving";
    agent->diagnostic[0] = 0;
    navigation_follower_read(agent->follower, &agent->observed);
    return true;
}
static bool nav_agent_walk(Scene3dNavigationAsset* asset, Scene3dNavigationAgent* agent, NavigationVec end,
                           NavigationSurfaceMotion* motion) {
    NavigationSurfaceView surface = {};
    NavigationError error = {};
    navigation_bake_surface(asset->compiled.bake, &surface);
    NavigationSurfaceMove movement = {agent->query, agent->polygon, 32, 256};
    movement.profile.path.start = agent->position;
    movement.profile.path.goal = end;
    if (!navigation_surface_walk(&surface, &movement, motion, &error)) {
        nav_agent_stop(agent, "blocked", error.message);
        return false;
    }
    if (!motion->polygon) {
        nav_agent_stop(agent, motion->status == NAVIGATION_EXHAUSTED ? "budget_exhausted" : "off_surface",
                       "navigation agent: no verified starting polygon");
        return false;
    }
    // projection may select a route endpoint, but it must never pull the body onto a disconnected floor.
    if (motion->start_distance > 4 * agent->precision) {
        nav_agent_stop(agent, "off_surface", "navigation agent: retained navigation position left its baked domain");
        return false;
    }
    return true;
}
static bool nav_agent_ground(Scene3dNavigationAsset* asset, Scene3dNavigationAgent* agent,
                             const PhysicsCharacterState& achieved) {
    if (!achieved.grounded) {
        nav_agent_stop(agent, "blocked",
                       "navigation agent: controller target has no ground support within snap distance");
        return false;
    }
    NavigationBakeSettings settings = {};
    navigation_bake_settings(asset->compiled.bake, &settings);
    if (achieved.ground_normal.y + 8 * DBL_EPSILON < cos(settings.max_slope)) {
        nav_agent_stop(agent, "blocked", "navigation agent: physical support exceeds the baked slope limit");
        return false;
    }
    return true;
}
struct NavigationAgentFrame {
    PhysicsPose pose = {}, platform_pose = {};
    NavigationVec foot = {}, velocity = {};
    uint64_t polygon = 0, platform = 0;
    bool target = false;
    bool steer = false;
};
static void nav_agent_propose(Scene3dNavigationAsset* asset, Scene3dNavigationAgent* agent,
                              const PhysicsCharacterProfile& before, const PhysicsCharacterState& achieved,
                              double delta, NavigationAgentFrame* frame) {
    if (!nav_agent_ground(asset, agent, achieved))
        return;
    // collision recovery and sliding can change direction; validate the computed target before committing it.
    PhysicsPose shape = pcompose(achieved.pose, pcompose(ppose_inverse(before.pose), before.shape_pose));
    PhysicsCharacterProfile proposed = before;
    proposed.shape_pose = shape;
    NavigationVec final = nav_agent_foot(proposed, agent->movement), position;
    NavigationSurfaceMotion verified = {};
    if (!nav_agent_walk(asset, agent, final, &verified))
        return;
    if (!nav_agent_support(asset, agent, verified.polygon, final, &position) ||
        hypot(verified.end.x - final.x, verified.end.z - final.z) > 4 * agent->precision) {
        nav_agent_stop(agent, "blocked", "navigation agent: controller target leaves the baked domain");
        return;
    }
    frame->pose = achieved.pose;
    frame->foot = final;
    frame->velocity = {(final.x - agent->foot.x) / delta, (final.y - agent->foot.y) / delta,
                       (final.z - agent->foot.z) / delta};
    frame->polygon = verified.polygon;
    frame->platform = achieved.platform;
    frame->platform_pose = achieved.platform_pose;
    frame->target = true;
}
static bool nav_agents_publish(Scene3dNavigationAgent* const* agents, const NavigationAgentFrame* frames, size_t count,
                               Scene3dPhysics* physics) {
    PhysicsCommand commands[NAV_AGENT_LIMIT] = {};
    size_t command_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (!frames[i].target)
            continue;
        auto& command = commands[command_count++];
        command.kind = PHYSICS_TARGET;
        command.body = agents[i]->movement.body;
        command.pose = frames[i].pose;
    }
    if (!command_count)
        return true;
    PhysicsError error = {};
    // validate and publish all verified targets together before updating retained controller feedback.
    if (!scene3d_physics_apply(physics, commands, command_count, &error)) {
        for (size_t i = 0; i < count; i++) {
            if (frames[i].target)
                nav_agent_stop(agents[i], "blocked", error.message);
        }
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (!frames[i].target)
            continue;
        agents[i]->polygon = frames[i].polygon;
        agents[i]->movement.platform = frames[i].platform;
        agents[i]->movement.platform_pose = frames[i].platform_pose;
        agents[i]->achieved_velocity = frames[i].velocity;
    }
    return true;
}
static void nav_agent_prepare(Scene3dNavigation* state, DomElement* root, Scene3dNavigationAgent* agent, double delta,
                              NavigationAvoidanceAgent* crowd, NavigationAgentFrame* frame) {
    if (agent->observed.status != NAVIGATION_FOLLOW_MOVING)
        return;
    auto* asset = nav_agent_asset(state, agent->asset);
    if (!nav_agent_support(asset, agent, agent->polygon, agent->foot, &agent->position)) {
        nav_agent_stop(agent, "off_surface", "navigation agent: achieved foot left its retained support layer");
        return;
    }
    PhysicsCharacterMove movement = agent->movement;
    movement.displacement = {};
    PhysicsCharacterProfile before = {};
    PhysicsCharacterState achieved = {};
    PhysicsError physics_error = {};
    auto* physics = scene3d_physics(root);
    if (!scene3d_physics_character_profile(physics, movement.body, &before, &physics_error) ||
        !scene3d_physics_character_compute(physics, &movement, &achieved, &physics_error)) {
        nav_agent_stop(agent, "blocked", physics_error.message);
        return;
    }
    // settle through the controller before claiming progress/arrival; a projected or airborne foot is not achieved
    // support.
    if (!nav_agent_ground(asset, agent, achieved))
        return;
    if (plen(achieved.achieved) > 4 * agent->precision) {
        nav_agent_propose(asset, agent, before, achieved, delta, frame);
        crowd->preferred = {frame->velocity.x, 0, frame->velocity.z};
        return;
    }
    NavigationError error = {};
    if (!navigation_follower_feedback(agent->follower, agent->achieved_velocity, &error) ||
        !navigation_follower_step(agent->follower, agent->position, agent->path, delta, &agent->observed, &error)) {
        nav_agent_stop(agent, "blocked", error.message);
        return;
    }
    static const char* statuses[] = {"idle", "moving", "action", "arrived", "path_end", "stale", "off_path", "stuck"};
    agent->status = statuses[agent->observed.status];
    if (agent->observed.status != NAVIGATION_FOLLOW_MOVING)
        return;
    crowd->preferred = agent->observed.velocity;
    crowd->responsive = agent->avoidance.enabled;
    // a steering plan precedes the controller target; settling may already have produced a target instead.
    frame->steer = true;
}
static void nav_agent_execute(Scene3dNavigation* state, DomElement* root, Scene3dNavigationAgent* agent,
                              NavigationVec velocity, double delta, NavigationAgentFrame* frame) {
    if (agent->observed.status != NAVIGATION_FOLLOW_MOVING)
        return;
    auto* asset = nav_agent_asset(state, agent->asset);
    NavigationVec intended = {agent->position.x + velocity.x * delta, agent->position.y,
                              agent->position.z + velocity.z * delta};
    NavigationSurfaceMotion safe = {};
    if (!nav_agent_walk(asset, agent, intended, &safe))
        return;
    PhysicsCharacterMove movement = agent->movement;
    movement.displacement = {safe.end.x - agent->foot.x, 0, safe.end.z - agent->foot.z};
    PhysicsCharacterProfile before = {};
    PhysicsCharacterState achieved = {};
    PhysicsError error = {};
    auto* physics = scene3d_physics(root);
    if (!scene3d_physics_character_profile(physics, movement.body, &before, &error) ||
        !scene3d_physics_character_compute(physics, &movement, &achieved, &error)) {
        nav_agent_stop(agent, "blocked", error.message);
        return;
    }
    nav_agent_propose(nav_agent_asset(state, agent->asset), agent, before, achieved, delta, frame);
}
static Scene3dTickResult nav_agents_tick(void* owner, double delta) {
    auto* state = (Scene3dNavigation*)owner;
    auto* node = dom_node_ref_validate(state->document, state->root);
    if (!node || !node->is_element())
        return SCENE3D_TICK_IDLE;
    auto* root = node->as_element();
    Scene3dNavigationAgent* agents[NAV_AGENT_LIMIT];
    size_t count = 0;
    for (auto* agent = state->agents; agent; agent = agent->next)
        agents[count++] = agent;
    if (!count)
        return SCENE3D_TICK_IDLE;
    bool readable[NAV_AGENT_LIMIT] = {};
    const char* previous[NAV_AGENT_LIMIT] = {};
    for (size_t i = 0; i < count; i++) {
        auto* agent = agents[i];
        previous[i] = agent->status;
        NavigationVec old_foot = agent->foot;
        uint64_t old_world = agent->world;
        readable[i] = nav_agent_observe(state, root, agent);
        agent->achieved_velocity = {};
        agent->preferred_velocity = {};
        agent->avoidance_velocity = {};
        if (readable[i] && old_world == agent->world) {
            agent->achieved_velocity = {(agent->foot.x - old_foot.x) / delta, 0, (agent->foot.z - old_foot.z) / delta};
            if (!navigation_vec_finite(agent->achieved_velocity))
                agent->achieved_velocity = {};
        }
    }
    size_t searched = 0, start = state->next_agent % count;
    for (size_t turn = 0; turn < count; turn++) {
        size_t i = (start + turn) % count;
        auto* agent = agents[i];
        if (!readable[i] || !agent->pending)
            continue;
        if (searched == NAV_AGENT_SEARCHES)
            break;
        searched++;
        state->next_agent = (i + 1) % count;
        if (nav_agent_search(state, agent) && !nav_agent_event(state, root, agent, "navigationpathchanged"))
            return SCENE3D_TICK_FAILED;
    }
    NavigationAvoidanceAgent crowd[NAV_AGENT_LIMIT] = {};
    NavigationAgentFrame frames[NAV_AGENT_LIMIT] = {};
    size_t crowd_slots[NAV_AGENT_LIMIT];
    for (size_t i = 0; i < count; i++)
        crowd_slots[i] = SIZE_MAX;
    size_t crowd_count = 0;
    for (size_t i = 0; i < count; i++)
        if (readable[i]) {
            auto* agent = agents[i];
            auto& member = crowd[crowd_count];
            crowd_slots[i] = crowd_count++;
            member.id = agent->movement.body;
            member.position = agent->foot;
            member.preferred = {};
            member.radius = agent->query.radius;
            member.height = agent->query.height;
            member.priority = agent->avoidance.priority;
            member.time_horizon = agent->avoidance.time_horizon;
            member.max_neighbors = agent->avoidance.max_neighbors;
            member.max_neighbor_tests = agent->avoidance.max_neighbor_tests;
            member.max_solver_tests = agent->avoidance.max_solver_tests;
            member.responsive = false;
            if (agent->target && !agent->pending && agent->observed.status != NAVIGATION_FOLLOW_IDLE)
                nav_agent_prepare(state, root, agent, delta, &member, &frames[i]);
            member.max_speed = fmax(agent->avoidance.max_speed, hypot(member.preferred.x, member.preferred.z));
            agent->preferred_velocity = member.preferred;
        }
    NavigationAvoidanceResult choices[NAV_AGENT_LIMIT] = {};
    NavigationError avoidance_error = {};
    if (!navigation_avoidance_step(crowd, crowd_count, delta, choices, &avoidance_error)) {
        for (size_t i = 0; i < count; i++) {
            if (frames[i].steer || frames[i].target) {
                nav_agent_stop(agents[i], "blocked", avoidance_error.message);
                agents[i]->avoidance_status = NAVIGATION_AVOIDANCE_BLOCKED;
                frames[i] = {};
            }
        }
    } else {
        for (size_t i = 0; i < count; i++)
            if (frames[i].steer || frames[i].target) {
                auto* agent = agents[i];
                auto& choice = choices[crowd_slots[i]];
                // stationary participants constrain neighbors but retain their last completed solve diagnostics.
                agent->avoidance_neighbors = choice.neighbors;
                agent->avoidance_neighbor_tests = choice.neighbor_tests;
                agent->avoidance_solver_tests = choice.solver_tests;
                agent->avoidance_status = choice.status;
                if (choice.status == NAVIGATION_AVOIDANCE_BLOCKED || choice.status == NAVIGATION_AVOIDANCE_EXHAUSTED) {
                    const char* status =
                        choice.status == NAVIGATION_AVOIDANCE_EXHAUSTED ? "budget_exhausted" : "blocked";
                    const char* diagnostic = choice.status == NAVIGATION_AVOIDANCE_EXHAUSTED
                                                 ? "navigation agent: avoidance work budget exhausted"
                                                 : "navigation agent: local avoidance constraints are infeasible";
                    nav_agent_stop(agent, status, diagnostic);
                    frames[i] = {};
                }
            }
    }
    NavigationVec actual[NAV_AGENT_LIMIT] = {};
    for (size_t i = 0; i < count; i++)
        if (crowd_slots[i] != SIZE_MAX) {
            auto* agent = agents[i];
            size_t slot = crowd_slots[i];
            if (frames[i].steer) {
                NavigationVec velocity = crowd[slot].responsive ? choices[slot].velocity : crowd[slot].preferred;
                agent->avoidance_velocity = velocity;
                agent->preferred_velocity = crowd[slot].preferred;
                agent->observed.velocity = velocity;
                nav_agent_execute(state, root, agent, velocity, delta, &frames[i]);
            }
            if (frames[i].target)
                actual[slot] = frames[i].velocity;
        }
    bool clear = false;
    if (!navigation_avoidance_verify(crowd, actual, crowd_count, delta, &clear, &avoidance_error) || !clear) {
        for (size_t i = 0; i < count; i++)
            if (crowd_slots[i] != SIZE_MAX && frames[i].target) {
                auto* agent = agents[i];
                nav_agent_stop(agent, "blocked",
                               "navigation agent: simultaneous controller targets enter a shared capsule");
                agent->avoidance_status = NAVIGATION_AVOIDANCE_BLOCKED;
                frames[i].target = false;
            }
    }
    bool published = nav_agents_publish(agents, frames, count, scene3d_physics(root));
    for (size_t i = 0; i < count; i++) {
        auto* agent = agents[i];
        if (strcmp(previous[i], agent->status)) {
            const char* type = !strcmp(agent->status, "action")    ? "navigationlinkrequired"
                               : !strcmp(agent->status, "arrived") ? "navigationarrived"
                                                                   : "navigationstatechanged";
            if (!nav_agent_event(state, root, agent, type))
                return SCENE3D_TICK_FAILED;
        }
    }
    return published ? SCENE3D_TICK_ACTIVE : SCENE3D_TICK_FAILED;
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
    if (!nav_can_change(state))
        return fail("navigation agent: unavailable during tick sampling");
    if (!strcmp(operation, "navigation_agent_attach")) {
        if (!valid)
            return fail(diagnostic);
        if (agent || state->agent_count == NAV_AGENT_LIMIT)
            return fail("navigation agent: duplicate ID or 32-agent quota exceeded");
        const char* asset_id = navigation_text(request.get("asset"));
        auto* asset = asset_id ? nav_agent_asset(state, asset_id) : nullptr;
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
        if (!nav_agent_profile(asset, profile, movement, &query, &diagnostic))
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
                nav_agent_destroy(value);
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
        created->foot = nav_agent_foot(profile, movement);
        created->position = created->foot;
        created->precision = profile.precision;
        created->avoidance = avoidance;
        if (!state->agents && !scene3d_runtime_bind(state->runtime, SCENE3D_TICK_NAVIGATION, state, nav_agents_tick))
            return fail("navigation agent: navigation tick phase already owned");
        created->command = (*state->next_generation)++;
        *slot = created;
        state->agent_count++;
        candidate.value = nullptr;
        nav_agents_schedule(state, root);
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
        nav_agent_destroy(agent);
        if (!state->agents)
            scene3d_runtime_unbind(state->runtime, SCENE3D_TICK_NAVIGATION, state);
        else
            nav_agents_schedule(state, root);
        return Item{.item = b2it(true)};
    }
    if (!strcmp(operation, "navigation_agent_link_complete")) {
        if (!valid)
            return fail(diagnostic);
        // observe first: reset, teleport and asset edits must invalidate an old action acknowledgement.
        if (!nav_agent_observe(state, root, agent))
            return fail(agent->diagnostic);
        auto path = request.get("path_generation"), action = request.get("action");
        NavigationError error = {};
        NavigationTraversal active = {};
        uint64_t arrival_polygon = 0;
        auto* asset = nav_agent_asset(state, agent->asset);
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
            !nav_agent_support(asset, agent, arrival_polygon, agent->foot, &arrival) ||
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
        nav_agents_schedule(state, root);
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
        nav_agents_schedule(state, root);
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
    nav_agent_stop(agent, target ? "queued" : "idle");
    agent->command = (*state->next_generation)++;
    agent->target = target;
    agent->pending = target;
    agent->goal = goal;
    nav_agents_schedule(state, root);
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
        if (!nav_can_change(state))
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
        if (!nav_source_snapshot(asset, root, &job->worker.geometry, &job->references, error, sizeof(error))) {
            nav_job_destroy(job);
            return fail(error);
        }
        job->charged_triangles = job->worker.geometry.triangles.size() + base_stats.source_triangles;
        if (triangles + job->charged_triangles > SCENE3D_BAKE_TRIANGLE_LIMIT) {
            nav_job_destroy(job);
            return fail("navigation bake: combined retained snapshot triangle quota exceeded");
        }
        job->definitions.dimensions = asset->compiled.dimensions;
        job->definitions.max_attachment_tests = asset->compiled.max_attachment_tests;
        for (const auto& link : asset->compiled.link_bindings)
            if (!job->definitions.link_bindings.append(link)) {
                nav_job_destroy(job);
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
        if (!state->pool || !tp_submit(state->pool, nav_bake_worker, &job->worker)) {
            nav_job_destroy(job);
            return fail("navigation bake: worker submission failed");
        }
        for (auto* before = state->jobs; before; before = before->next)
            if (!strcmp(before->asset, asset->id))
                nav_job_stale(before, "navigation bake: superseded by a newer job");
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
    if (!nav_can_change(state))
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
        nav_job_destroy(job);
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
        nav_job_stale(job, "navigation bake: root, target or settings generation changed");
        return nav_job_status(state, job);
    }
    Scene3dNavigationGeometry fresh;
    lam::ArrayList<DomNodeRef> refs;
    char error[256] = {};
    if (!nav_source_snapshot(asset, root, &fresh, &refs, error, sizeof(error))) {
        nav_job_stale(job, *error ? error : "navigation bake: source validation failed");
        return nav_job_status(state, job);
    }
    bool same = refs.size() == job->references.size() && !strcmp(fresh.source_hash, job->worker.geometry.source_hash);
    for (size_t i = 0; i < refs.size() && same; i++)
        same = dom_node_ref_identity_compare(refs[i], job->references[i]) == 0;
    if (!same) {
        nav_job_stale(job, "navigation bake: source snapshot or identity changed");
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
    size_t storage_nodes = state->stats.storage_nodes - nav_storage_nodes(asset->compiled) + job->worker.stats.cells +
                           2 * navigation_links_count(overlay.links);
    auto retained = nav_retained_bake(asset->compiled);
    size_t raw_cells = state->stats.raw_cells - retained.raw_cells + job->worker.stats.raw_cells;
    size_t source_triangles =
        state->stats.source_triangles - retained.source_triangles + job->worker.stats.source_triangles;
    if (!nav_counts_fit(nodes, edges, state->stats.vertices, storage_nodes, raw_cells, source_triangles))
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
            value.put("ready", Item{.item = b2it(nav_asset_ready(asset))});
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
    bool current = selected && nav_asset_ready(selected) && generation.isInt() && generation.asInt() > 0 &&
                   (uint64_t)generation.asInt() == selected->generation;
    if (operation && !strcmp(operation, "navigation_current"))
        return Item{.item = b2it(valid && current)};
    if (!valid)
        return fail(diagnostic);
    if (!selected)
        return fail("navigation: unknown scene asset");
    if (!nav_asset_ready(selected))
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
