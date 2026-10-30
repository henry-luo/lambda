#include "../../lambda/core/mark_reader.hpp"
#include "../../lambda/core/physics_scene.hpp"
#include "../../lambda/input/css/dom_element.hpp"
#include "../../lambda/input/css/dom_lifecycle.hpp"
#include "../../lambda/runtime/lambda-error.h"
#include "../../lambda/runtime/lambda-number-runtime.hpp"
#include "../../lambda/runtime/value_builder.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/str.h"
#include "../event.hpp"
#include "scene3d.hpp"
#include <math.h>

struct Scene3dRuntimeService {
    void* owner;
    Scene3dTickFn tick;
    bool enabled;
    Scene3dCommandFn command;
    bool command_wake;
};
struct Scene3dRuntime {
    DomDocument* document;
    DomNodeRef root;
    AnimationInstance* driver;
    Scene3dRuntimeService services[SCENE3D_TICK_PHASES];
    void* admission_owner;
    Scene3dAdmitFn admit;
    Scene3dRuntimeStats stats;
    Scene3dCommandBatch commands[SCENE3D_COMMAND_BATCH_LIMIT];
    unsigned command_count;
    double previous_host_time;
    double animation_ahead;
    unsigned max_substeps;
    bool sampling, admitting;
    const char* diagnostic;
};

static bool scene3d_runtime_schedule(Scene3dRuntime* runtime);
static bool scene3d_runtime_fail(Scene3dRuntime* runtime, const char* message);
static bool scene3d_runtime_commands_pending(const Scene3dRuntime* runtime) {
    for (unsigned i = 0; i < runtime->command_count; i++) {
        const auto& service = runtime->services[runtime->commands[i].phase];
        if (service.command_wake) {
            return true;
        }
    }
    return false;
}
static void scene3d_runtime_command_finish(Scene3dRuntime* runtime, const Scene3dCommandBatch& batch,
                                           const char* diagnostic) {
    auto& stats = runtime->stats;
    stats.last_command_sequence = batch.sequence;
    stats.last_command_tick = batch.tick;
    stats.last_command_applied = diagnostic == nullptr;
    str_copy(stats.command_diagnostic, sizeof(stats.command_diagnostic), diagnostic ? diagnostic : "",
             diagnostic ? strlen(diagnostic) : 0);
    uint64_t* count = diagnostic ? &stats.commands_rejected : &stats.commands_applied;
    if (*count == UINT64_MAX) {
        scene3d_runtime_fail(runtime, "command receipt count exceeded");
    } else {
        ++*count;
    }
    batch.release(batch.payload);
}
static void scene3d_runtime_commands_clear(Scene3dRuntime* runtime, const char* diagnostic) {
    while (runtime->command_count) {
        auto batch = runtime->commands[--runtime->command_count];
        scene3d_runtime_command_finish(runtime, batch, diagnostic);
    }
}
static bool scene3d_runtime_command_tick(Scene3dRuntime* runtime) {
    uint64_t next = runtime->stats.ticks + 1;
    while (runtime->command_count && runtime->commands[0].tick <= next) {
        auto batch = runtime->commands[0];
        runtime->command_count--;
        memmove(runtime->commands, runtime->commands + 1, runtime->command_count * sizeof(*runtime->commands));
        auto& service = runtime->services[batch.phase];
        const char* error = nullptr;
        if (batch.session != runtime->stats.session || batch.generation != runtime->stats.generation ||
            batch.service_generation != runtime->stats.service_generations[batch.phase]) {
            error = "command batch has a stale scene/service generation";
        } else if (batch.tick != next) {
            error = "command batch missed its target tick";
        } else if (!service.owner || !service.command) {
            error = "command batch has no live native receiver";
        } else {
            error = service.command(service.owner, batch.payload);
        }
        scene3d_runtime_command_finish(runtime, batch, error);
        if (runtime->stats.faulted) {
            return false;
        }
    }
    return true;
}
static bool scene3d_runtime_generation_available(const Scene3dRuntime* runtime, unsigned phase) {
    return runtime->stats.generation != UINT64_MAX && runtime->stats.service_generations[phase] != UINT64_MAX;
}
static void scene3d_runtime_generation_change(Scene3dRuntime* runtime, unsigned phase) {
    runtime->stats.generation++;
    runtime->stats.service_generations[phase]++;
    scene3d_runtime_commands_clear(runtime, "command batch invalidated by a native owner/source change");
}
bool scene3d_runtime_bind_commands(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner,
                                   Scene3dCommandFn apply) {
    if (!runtime || runtime->sampling || (unsigned)phase >= SCENE3D_TICK_PHASES || !owner || !apply ||
        runtime->services[phase].owner != owner || runtime->services[phase].command) {
        return false;
    }
    runtime->services[phase].command = apply;
    return true;
}
bool scene3d_runtime_command_wake(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner, bool enabled) {
    if (!runtime || (unsigned)phase >= SCENE3D_TICK_PHASES || runtime->services[phase].owner != owner) {
        return false;
    }
    bool previous = runtime->services[phase].command_wake;
    runtime->services[phase].command_wake = enabled;
    if (!scene3d_runtime_schedule(runtime)) {
        runtime->services[phase].command_wake = previous;
        return false;
    }
    return true;
}
bool scene3d_runtime_revise(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner) {
    if (!runtime || runtime->sampling || (unsigned)phase >= SCENE3D_TICK_PHASES ||
        runtime->services[phase].owner != owner || !scene3d_runtime_generation_available(runtime, phase)) {
        return false;
    }
    scene3d_runtime_generation_change(runtime, phase);
    return true;
}
bool scene3d_runtime_submit(Scene3dRuntime* runtime, const Scene3dCommandBatch& batch, const char** error) {
    auto reject = [&](const char* message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    if (!runtime || runtime->sampling || runtime->admitting || runtime->stats.faulted || !batch.payload ||
        !batch.release || (unsigned)batch.phase >= SCENE3D_TICK_PHASES) {
        return reject("command batch requires an idle live owner and copied native payload");
    }
    if (batch.session != runtime->stats.session || batch.generation != runtime->stats.generation ||
        batch.service_generation != runtime->stats.service_generations[batch.phase]) {
        return reject("command batch has a stale scene/service generation");
    }
    if (!runtime->services[batch.phase].command) {
        return reject("command batch has no native receiver");
    }
    if (!batch.sequence || batch.sequence <= runtime->stats.command_sequence) {
        return reject("command sequence must increase without reuse");
    }
    if (batch.tick <= runtime->stats.ticks || batch.tick - runtime->stats.ticks > 100000 ||
        (runtime->animation_ahead > 0 && batch.tick - runtime->stats.ticks == 1)) {
        return reject("command target must be an unsampled future tick within 100000 ticks");
    }
    if (runtime->command_count == SCENE3D_COMMAND_BATCH_LIMIT) {
        return reject("command queue exceeds 32 pending batches");
    }
    unsigned index = runtime->command_count;
    while (index && runtime->commands[index - 1].tick > batch.tick) {
        runtime->commands[index] = runtime->commands[index - 1];
        index--;
    }
    runtime->commands[index] = batch;
    runtime->command_count++;
    if (!scene3d_runtime_schedule(runtime)) {
        runtime->command_count--;
        memmove(runtime->commands + index, runtime->commands + index + 1,
                (runtime->command_count - index) * sizeof(*runtime->commands));
        return reject("command batch could not schedule its native clock");
    }
    runtime->stats.command_sequence = batch.sequence;
    return true;
}

static unsigned scene3d_runtime_active(Scene3dRuntime* runtime) {
    unsigned count = 0;
    for (const auto& service : runtime->services)
        if (service.enabled)
            count++;
    return count;
}
static void scene3d_runtime_released(AnimationInstance* driver) {
    ((Scene3dRuntime*)driver->target)->driver = nullptr;
}
static bool scene3d_runtime_fail(Scene3dRuntime* runtime, const char* message) {
    runtime->diagnostic = message;
    runtime->stats.faulted = true;
    log_error("scene3d runtime fault: %s", message);
    return false;
}
static bool scene3d_runtime_admit(Scene3dRuntime* runtime) {
    if (runtime->admitting)
        return false;
    if (!runtime->admit)
        return true;
    runtime->admitting = true;
    const char* error = runtime->admit(runtime->admission_owner);
    runtime->admitting = false;
    // rejected authored edits leave the old tick/world repairable rather than faulting the solver.
    runtime->diagnostic = error;
    return error == nullptr;
}
static bool scene3d_runtime_run_service(Scene3dRuntime* runtime, unsigned phase, double delta, bool force) {
    auto& service = runtime->services[phase];
    if (!service.owner || (!force && !service.enabled))
        return true;
    auto result = service.tick(service.owner, delta);
    if (result == SCENE3D_TICK_FAILED) {
        const char* failures[] = {"navigation stage failed", "animation stage failed", "physics stage failed"};
        return scene3d_runtime_fail(runtime, failures[phase]);
    }
    if (result == SCENE3D_TICK_IDLE)
        service.enabled = false;
    return true;
}
static bool scene3d_runtime_tick_once(Scene3dRuntime* runtime, bool all_services) {
    if (runtime->stats.ticks == UINT64_MAX || !isfinite((double)(runtime->stats.ticks + 1) * runtime->stats.timestep))
        return scene3d_runtime_fail(runtime, "simulation tick count exceeded");
    if (!scene3d_runtime_command_tick(runtime)) {
        return false;
    }
    // a frame-sampled animation may already own the fractional part of this fixed tick.
    double animation_delta = fmax(0, runtime->stats.timestep - runtime->animation_ahead);
    if (!scene3d_runtime_run_service(runtime, SCENE3D_TICK_NAVIGATION, runtime->stats.timestep, all_services) ||
        !scene3d_runtime_run_service(runtime, SCENE3D_TICK_ANIMATION, animation_delta, all_services) ||
        !scene3d_runtime_run_service(runtime, SCENE3D_TICK_PHYSICS, runtime->stats.timestep, all_services))
        return false;
    runtime->animation_ahead = 0;
    runtime->stats.ticks++;
    runtime->stats.time = (double)runtime->stats.ticks * runtime->stats.timestep;
    return true;
}
static bool scene3d_runtime_animation_only(Scene3dRuntime* runtime) {
    return !runtime->services[SCENE3D_TICK_PHYSICS].enabled && !runtime->services[SCENE3D_TICK_NAVIGATION].enabled &&
           !runtime->command_count;
}
static bool scene3d_runtime_sample_animation(Scene3dRuntime* runtime, double debt, bool presentation = true) {
    // animation-only views retain exact virtual-time samples; no solver work needs catch-up substeps.
    double ticks = floor((debt + runtime->stats.timestep * 1e-10) / runtime->stats.timestep);
    if (ticks >= (double)(UINT64_MAX - runtime->stats.ticks))
        return scene3d_runtime_fail(runtime, "simulation tick count exceeded");
    runtime->sampling = true;
    bool sampled =
        scene3d_runtime_run_service(runtime, SCENE3D_TICK_ANIMATION, fmax(0, debt - runtime->animation_ahead), false);
    runtime->sampling = false;
    if (!sampled)
        return false;
    runtime->stats.ticks += (uint64_t)ticks;
    runtime->stats.time =
        (double)runtime->stats.ticks * runtime->stats.timestep; // INT_CAST_OK: bounded finite whole tick count
    runtime->stats.debt = fmax(0, debt - ticks * runtime->stats.timestep);
    runtime->animation_ahead = runtime->stats.debt;
    runtime->stats.interpolation = runtime->stats.debt / runtime->stats.timestep;
    if (presentation) {
        runtime->stats.presentations++;
    }
    return true;
}
bool scene3d_runtime_advance(Scene3dRuntime* runtime, double elapsed) {
    if (!runtime || runtime->sampling || runtime->stats.faulted || !isfinite(elapsed) || elapsed < 0)
        return false;
    // repeated sampling of one host timestamp must not consume another budget of old debt (D2.11.4).
    if (elapsed == 0 || runtime->stats.paused || runtime->stats.time_scale == 0)
        return true;
    if (!scene3d_runtime_admit(runtime))
        return false;
    if (!scene3d_runtime_active(runtime) && !scene3d_runtime_commands_pending(runtime))
        return true;
    double debt = runtime->stats.debt + elapsed * runtime->stats.time_scale;
    if (!isfinite(debt))
        return scene3d_runtime_fail(runtime, "nonfinite simulation time");
    if (scene3d_runtime_animation_only(runtime)) {
        return scene3d_runtime_sample_animation(runtime, debt);
    }
    if (debt > 2)
        return scene3d_runtime_fail(runtime, "simulation pacing debt exceeded");
    runtime->stats.debt = debt;
    if (elapsed > 0)
        runtime->stats.presentations++;
    runtime->sampling = true;
    unsigned steps = 0;
    // tolerate roundoff at an exact boundary without changing the fixed timestep.
    while (runtime->stats.debt + runtime->stats.timestep * 1e-10 >= runtime->stats.timestep &&
           steps < runtime->max_substeps &&
           (scene3d_runtime_active(runtime) || scene3d_runtime_commands_pending(runtime))) {
        if (!scene3d_runtime_tick_once(runtime, false)) {
            runtime->sampling = false;
            return false;
        }
        runtime->stats.debt = fmax(0, runtime->stats.debt - runtime->stats.timestep);
        steps++;
    }
    runtime->sampling = false;
    if (scene3d_runtime_animation_only(runtime)) {
        if (scene3d_runtime_active(runtime)) {
            return scene3d_runtime_sample_animation(runtime, runtime->stats.debt, false);
        }
        runtime->stats.debt = 0;
    }
    runtime->stats.interpolation = fmin(1, runtime->stats.debt / runtime->stats.timestep);
    return true;
}
static void scene3d_runtime_tick(AnimationInstance* driver, float) {
    auto* runtime = (Scene3dRuntime*)driver->target;
    double now = animation_clock_time(runtime->document->state->animation_scheduler, driver, 0);
    DomNode* root = dom_node_ref_validate(runtime->document, runtime->root);
    bool attached = false;
    for (auto* parent = root; parent; parent = parent->parent)
        if (parent == runtime->document->root) {
            attached = true;
            break;
        }
    double elapsed = now - runtime->previous_host_time;
    runtime->previous_host_time = now;
    if (!attached || !scene3d_runtime_advance(runtime, elapsed) ||
        (!scene3d_runtime_active(runtime) && !scene3d_runtime_commands_pending(runtime)) || runtime->stats.paused ||
        runtime->stats.time_scale == 0)
        driver->play_state = ANIM_PLAY_FINISHED;
}
static bool scene3d_runtime_schedule(Scene3dRuntime* runtime) {
    bool active = (scene3d_runtime_active(runtime) || scene3d_runtime_commands_pending(runtime)) &&
                  !runtime->stats.paused && !runtime->stats.faulted && runtime->stats.time_scale > 0;
    // never cancel the driver from inside its own stage callback.
    if (runtime->sampling || runtime->admitting)
        return true;
    if (!active && runtime->driver)
        animation_scheduler_cancel(runtime->document->state->animation_scheduler, runtime->driver);
    if (active && !runtime->driver) {
        if (!runtime->document->state && !state_store_create(runtime->document))
            return false;
        runtime->previous_host_time = 0;
        runtime->driver = animation_clock_driver_start(runtime->document->state->animation_scheduler, ANIM_TIMELINE,
                                                       runtime, scene3d_runtime_tick, scene3d_runtime_released);
        return runtime->driver != nullptr;
    }
    return true;
}
Scene3dRuntime* scene3d_runtime_create(DomElement* root) {
    if (!root || !root->doc)
        return nullptr;
    auto* runtime = (Scene3dRuntime*)mem_calloc(1, sizeof(Scene3dRuntime), MEM_CAT_RENDER);
    if (!runtime)
        return nullptr;
    // session tokens cannot alias another document or a recreated runtime, even across owner threads.
    static uint64_t next_session = 1;
    uint64_t session = __atomic_load_n(&next_session, __ATOMIC_RELAXED);
    while (
        session && session != UINT64_MAX &&
        !__atomic_compare_exchange_n(&next_session, &session, session + 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
    if (!session || session == UINT64_MAX) {
        mem_free(runtime);
        return nullptr;
    }
    runtime->stats.session = session;
    runtime->document = root->doc;
    runtime->root = dom_node_ref(root);
    runtime->stats.timestep = 1.0 / 120;
    runtime->stats.time_scale = 1;
    runtime->stats.generation = 1;
    runtime->max_substeps = 8;
    return runtime;
}
bool scene3d_runtime_bind_admission(Scene3dRuntime* runtime, void* owner, Scene3dAdmitFn admit) {
    if (!runtime || runtime->sampling || runtime->admitting || runtime->admission_owner || !owner || !admit)
        return false;
    runtime->admission_owner = owner;
    runtime->admit = admit;
    return true;
}
void scene3d_runtime_destroy(Scene3dRuntime* runtime) {
    if (!runtime)
        return;
    if (runtime->driver)
        animation_scheduler_cancel(runtime->document->state->animation_scheduler, runtime->driver);
    scene3d_runtime_commands_clear(runtime, "command batch owner was destroyed");
    mem_free(runtime);
}
void scene3d_runtime_follow_rebuild(Scene3dRuntime* runtime, DomNode* old_root, DomNode* new_root) {
    if (runtime)
        view_state_ref_follow_rebuild(runtime->document, &runtime->root, old_root, new_root);
}
bool scene3d_runtime_bind(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner, Scene3dTickFn tick) {
    if (!runtime || runtime->sampling || (unsigned)phase >= SCENE3D_TICK_PHASES || !owner || !tick ||
        runtime->services[phase].owner || !scene3d_runtime_generation_available(runtime, phase))
        return false;
    scene3d_runtime_generation_change(runtime, phase);
    runtime->services[phase] = {owner, tick, false, nullptr, false};
    return true;
}
bool scene3d_runtime_enable(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner, bool enabled) {
    if (!runtime || (unsigned)phase >= SCENE3D_TICK_PHASES || runtime->services[phase].owner != owner)
        return false;
    bool previous = runtime->services[phase].enabled;
    runtime->services[phase].enabled = enabled;
    if (!scene3d_runtime_schedule(runtime)) {
        runtime->services[phase].enabled = previous;
        return false;
    }
    return true;
}
bool scene3d_runtime_replace(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* previous, void* owner,
                             Scene3dTickFn tick, Scene3dCommandFn commands) {
    if (!runtime || runtime->sampling || !runtime->stats.paused || (unsigned)phase >= SCENE3D_TICK_PHASES ||
        runtime->services[phase].owner != previous || !owner || !tick ||
        !scene3d_runtime_generation_available(runtime, phase))
        return false;
    scene3d_runtime_generation_change(runtime, phase);
    // a prepared replay transfers its owner only after every sampled tick succeeded.
    runtime->services[phase] = {owner, tick, false, commands, false};
    return true;
}
void scene3d_runtime_unbind(Scene3dRuntime* runtime, Scene3dTickPhase phase, void* owner) {
    if (!runtime || (unsigned)phase >= SCENE3D_TICK_PHASES || runtime->services[phase].owner != owner)
        return;
    if (scene3d_runtime_generation_available(runtime, phase)) {
        scene3d_runtime_generation_change(runtime, phase);
    } else {
        scene3d_runtime_commands_clear(runtime, "command batch owner was retired");
        scene3d_runtime_fail(runtime, "native owner generation exceeded");
    }
    runtime->services[phase] = {};
    // removing an owner cannot restart another service while document teardown is unwinding.
    if (!scene3d_runtime_active(runtime))
        scene3d_runtime_schedule(runtime);
}
bool scene3d_runtime_can_configure(const Scene3dRuntime* runtime, double timestep, unsigned max_substeps) {
    return runtime && !runtime->sampling && isfinite(timestep) && timestep > 0 && max_substeps;
}
bool scene3d_runtime_configure(Scene3dRuntime* runtime, double timestep, unsigned max_substeps) {
    if (!scene3d_runtime_can_configure(runtime, timestep, max_substeps))
        return false;
    if (timestep != runtime->stats.timestep) {
        if (runtime->stats.generation == UINT64_MAX) {
            return false;
        }
        scene3d_runtime_commands_clear(runtime, "command batch tick domain changed");
        // structural world replacement starts a new tick domain, not an altered old tick duration.
        runtime->stats.ticks = 0;
        runtime->stats.time = 0;
        runtime->stats.debt = 0;
        runtime->stats.interpolation = 0;
        runtime->animation_ahead = 0;
        runtime->stats.generation++;
    }
    runtime->stats.timestep = timestep;
    runtime->max_substeps = max_substeps;
    return true;
}
bool scene3d_runtime_pause(Scene3dRuntime* runtime, bool paused) {
    if (!runtime || runtime->sampling)
        return false;
    runtime->stats.paused = paused;
    return scene3d_runtime_schedule(runtime);
}
bool scene3d_runtime_time_scale(Scene3dRuntime* runtime, double scale) {
    if (!runtime || runtime->sampling || !isfinite(scale) || scale < 0)
        return false;
    runtime->stats.time_scale = scale;
    return scene3d_runtime_schedule(runtime);
}
bool scene3d_runtime_step(Scene3dRuntime* runtime, size_t ticks) {
    if (!runtime || runtime->sampling || runtime->stats.faulted || ticks > 100000 ||
        !scene3d_runtime_pause(runtime, true) || !scene3d_runtime_admit(runtime))
        return false;
    runtime->sampling = true;
    for (size_t i = 0; i < ticks; i++)
        if (!scene3d_runtime_tick_once(runtime, true)) {
            runtime->sampling = false;
            return false;
        }
    runtime->sampling = false;
    runtime->stats.debt = 0;
    runtime->stats.interpolation = 0;
    return true;
}
bool scene3d_runtime_reset_clock(Scene3dRuntime* runtime, uint64_t tick) {
    if (!runtime || runtime->sampling || !runtime->stats.paused || !isfinite((double)tick * runtime->stats.timestep))
        return false;
    if (runtime->stats.generation == UINT64_MAX) {
        return false;
    }
    scene3d_runtime_commands_clear(runtime, "command batch invalidated by reset/replay");
    runtime->stats.ticks = tick;
    runtime->stats.time = (double)tick * runtime->stats.timestep;
    runtime->stats.debt = 0;
    runtime->stats.interpolation = 0;
    runtime->stats.faulted = false;
    runtime->animation_ahead = 0;
    runtime->stats.generation++;
    runtime->diagnostic = nullptr;
    return true;
}
bool scene3d_runtime_tick_index(const Scene3dRuntime* runtime, double seconds, size_t* tick) {
    if (!runtime || !tick || !isfinite(seconds) || seconds < 0) {
        return false;
    }
    double ticks = seconds / runtime->stats.timestep;
    double selected = round(ticks);
    if (!isfinite(ticks) || ticks > 100000 || fabs(ticks - selected) > 1e-8) {
        return false;
    }
    *tick = (size_t)selected; // INT_CAST_OK: validated bounded whole tick count
    return true;
}
bool scene3d_runtime_stats(Scene3dRuntime* runtime, Scene3dRuntimeStats* stats) {
    if (!runtime || !stats)
        return false;
    *stats = runtime->stats;
    stats->active_services = scene3d_runtime_active(runtime);
    stats->pending_commands = runtime->command_count;
    uint64_t offset = runtime->animation_ahead > 0 ? 2 : 1;
    stats->next_command_tick = stats->ticks <= UINT64_MAX - offset ? stats->ticks + offset : 0;
    return true;
}
const char* scene3d_runtime_diagnostic(Scene3dRuntime* runtime) {
    return runtime ? runtime->diagnostic : nullptr;
}

static Item scene3d_runtime_value(Scene3dRuntime* runtime, bool capabilities) {
    RootedValueBuilder result;
    Scene3dRuntimeStats stats = {};
    scene3d_runtime_stats(runtime, &stats);
    if (capabilities) {
        const char* available[] = {"fixed_step",
                                   "command_batches",
                                   "animation_command_batches",
                                   "animation",
                                   "animation_masks",
                                   "animation_reference_additive",
                                   "animation_reconciliation",
                                   "animation_track_mapping",
                                   "animation_snapshots",
                                   "animation_markers",
                                   "animation_event_provenance",
                                   "animation_event_replay",
                                   "animation_root_translation",
                                   "animation_root_rotation",
                                   "root_motion_capsule_turns",
                                   "root_motion_capsule",
                                   "root_motion_replay",
                                   "rigid_sphere_box",
                                   "rigid_capsule",
                                   "rigid_convex",
                                   "distance_queries",
                                   "mesh_environment",
                                   "heightfield_environment",
                                   "collision_resources",
                                   "shape_overlap",
                                   "shape_cast",
                                   "all_hit_queries",
                                   "ccd",
                                   "ccd_sphere",
                                   "joints",
                                   "joint_distance",
                                   "joint_spherical",
                                   "joint_fixed",
                                   "joint_hinge",
                                   "joint_prismatic",
                                   "joint_cone_twist",
                                   "joint_breaks",
                                   "character_controller",
                                   "character_capsule",
                                   "physical_bones",
                                   "ragdoll_modes",
                                   "ragdoll_replay",
                                   "ragdolls",
                                   "navigation_graph",
                                   "navigation_grid",
                                   "navigation_assets",
                                   "navigation_surface",
                                   "navigation_bake_cells",
                                   "navigation_bake_jobs",
                                   "navigation_dirty_tiles",
                                   "navigation_bake_polygons",
                                   "navigation_links",
                                   "navigation_agents_baked",
                                   "navigation_avoidance_baked"};
        const char* pending[] = {"navigation", "ccd_convex", "animation_graphs"};
        for (const char* key : available)
            result.put(key, Item{.item = b2it(true)});
        for (const char* key : pending)
            result.put(key, Item{.item = b2it(false)});
    } else {
        result.put("session", box_uint64_value(stats.session));
        result.put("generation", box_uint64_value(stats.generation));
        RootedValueBuilder services;
        const char* phase_names[] = {"navigation", "animation", "physics"};
        for (unsigned phase = 0; phase < SCENE3D_TICK_PHASES; phase++) {
            services.put(phase_names[phase], box_uint64_value(stats.service_generations[phase]));
        }
        result.put("service_generations", services.value.get());
        result.put("next_command_tick", box_uint64_value(stats.next_command_tick));
        result.put("pending_commands", box_uint64_value(stats.pending_commands));
        result.put("command_sequence", box_uint64_value(stats.command_sequence));
        result.put("commands_applied", box_uint64_value(stats.commands_applied));
        result.put("commands_rejected", box_uint64_value(stats.commands_rejected));
        result.put("last_command_sequence", box_uint64_value(stats.last_command_sequence));
        result.put("last_command_tick", box_uint64_value(stats.last_command_tick));
        result.put("last_command_applied", Item{.item = b2it(stats.last_command_applied)});
        result.put("command_diagnostic", Item{.item = s2it(heap_create_name(stats.command_diagnostic))});
        result.put("tick", Item{.item = i2it((int64_t)stats.ticks)});
        result.put("time", push_d(stats.time));
        result.put("timestep", push_d(stats.timestep));
        result.put("debt", push_d(stats.debt));
        result.put("time_scale", push_d(stats.time_scale));
        result.put("paused", Item{.item = b2it(stats.paused)});
        result.put("active_services", Item{.item = i2it(stats.active_services)});
        result.put("faulted", Item{.item = b2it(stats.faulted)});
    }
    return result.value.get();
}
Item scene3d_runtime_snapshot(Scene3dRuntime* runtime) {
    return scene3d_runtime_value(runtime, false);
}

Item scene3d_runtime_operate(Scene3dRuntime* runtime, Item request) {
    RootFrame roots(1);
    Rooted<Item> rooted(roots, request);
    ItemReader read(rooted.get().to_const());
    const char* error = nullptr;
    if (!runtime || !read.isMap())
        error = "scene3d: requires a live scene and an operation map";
    auto field = [&](const char* name) { return read.asMap().get(name); };
    auto number = [&](const char* key, double fallback, double* out) {
        auto value = field(key);
        return (value.isNull() || value.isInt() || value.isFloat()) && physics_scene_real(value, fallback, out);
    };
    auto name = error ? ItemReader() : field("operation");
    const char* op = name.isString() ? name.asString()->chars : name.isSymbol() ? name.asSymbol()->chars : nullptr;
    if (!error && !op)
        error = "scene3d: requires an operation";
    if (!error) {
        if (!strcmp(op, "pause") || !strcmp(op, "play")) {
            if (!scene3d_runtime_pause(runtime, !strcmp(op, "pause")))
                error = "scene3d: cannot change playback";
        } else if (!strcmp(op, "step")) {
            double ticks;
            if (!number("ticks", 1, &ticks) || ticks < 0 || ticks > 100000 || floor(ticks) != ticks)
                error = "scene3d: invalid tick count";
            else if (!scene3d_runtime_step(runtime, (size_t)ticks)) // INT_CAST_OK: validated bounded whole tick count
                error = runtime->diagnostic ? runtime->diagnostic : "scene3d: cannot step a faulted scene";
        } else if (!strcmp(op, "time_scale")) {
            double scale;
            if (!number("value", NAN, &scale) || !scene3d_runtime_time_scale(runtime, scale))
                error = "scene3d: invalid time scale";
        } else if (strcmp(op, "read") && strcmp(op, "capabilities"))
            error = "scene3d: unsupported operation";
    }
    if (!error && runtime->stats.faulted && strcmp(op, "read") && strcmp(op, "capabilities") && strcmp(op, "pause"))
        error = runtime->diagnostic;
    if (error)
        return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, error, nullptr));
    return scene3d_runtime_value(runtime, !strcmp(op, "capabilities"));
}
