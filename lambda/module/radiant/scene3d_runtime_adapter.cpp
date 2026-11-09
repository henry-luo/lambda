#include "radiant/scene3d/scene3d.hpp"
#include "lambda/core/mark_reader.hpp"
#include "lambda/core/physics_scene.hpp"
#include "../../runtime/lambda-error.h"
#include "../../runtime/lambda-number-runtime.hpp"
#include "../../runtime/value_builder.hpp"
#include <math.h>
#include <string.h>

static Item scene3d_runtime_value(Scene3dRuntime* runtime, bool capabilities) {
    RootedValueBuilder result;
    Scene3dRuntimeStats stats = {};
    scene3d_runtime_stats(runtime, &stats);
    if (capabilities) {
        const char *available[] = {"fixed_step",
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
                                   "navigation_avoidance_baked",
                                   "particles_cpu_mesh",
                                   "particles_cpu_scene_collision",
                                   "particles_contact_batches",
                                   "particles_gpu",
                                   "particles_gpu_mesh",
                                   "particles_gpu_analytic_collision",
                                   "particles_gpu_collision_replay",
                                   "particles_seeded",
                                   "particles_world_space",
                                   "particles_mesh_surface",
                                   "particles_size_curves",
                                   "particles_rotation_curves",
                                   "particles_noise",
                                   "particles_color_curves",
                                   "particles_opacity_curves",
                                   "particles_depth_ordering",
                                   "particles_texture_sheet", "particles_velocity_curves", "particles_billboards",
                                   "particles_cpu_trails", "particles_gpu_trails", "particles_trails",
                                   "particles_spawn_commands", "particles_child_queues",
                                   "audio_clips", "audio_sources", "audio_stereo", "audio_spatial_2d", "audio_spatial_3d",
                                   "audio_doppler", "audio_source_lowpass", "audio_source_meters",
                                   "audio_bus_routing", "audio_bus_sends", "audio_bus_solo", "audio_dsp_inserts", "audio_sidechain", "audio_dsp_tails",
                                   "audio_sample_cues", "audio_scene_cues", "audio_cue_receipts", "audio_pitch_follow",
                                   "audio_render_jobs", "audio_render_pcm", "audio_render_wav",
                                   "audio_midi_routes", "audio_speech_routes", "audio_provider_tickets", "audio_route_lifetime"};
        const char *pending[] = {"navigation", "ccd_convex", "animation_graphs", "particles_collision"};
        for (const char* key : available)
            result.put(key, Item{.item = b2it(true)});
        for (const char* key : pending)
            result.put(key, Item{.item = b2it(false)});
    } else {
        result.put("session", box_uint64_value(stats.session));
        result.put("generation", box_uint64_value(stats.generation));
        RootedValueBuilder services;
        for (unsigned phase = 0; phase < SCENE3D_TICK_PHASES; phase++) {
            services.put(scene3d_runtime_phase_name(phase), box_uint64_value(stats.service_generations[phase]));
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
                error = scene3d_runtime_diagnostic(runtime) ? scene3d_runtime_diagnostic(runtime) : "scene3d: cannot step a faulted scene";
        } else if (!strcmp(op, "time_scale")) {
            double scale;
            if (!number("value", NAN, &scale) || !scene3d_runtime_time_scale(runtime, scale))
                error = "scene3d: invalid time scale";
        } else if (strcmp(op, "read") && strcmp(op, "capabilities"))
            error = "scene3d: unsupported operation";
    }
    Scene3dRuntimeStats stats = {};
    if (!error && scene3d_runtime_stats(runtime, &stats) && stats.faulted &&
        strcmp(op, "read") && strcmp(op, "capabilities") && strcmp(op, "pause"))
        error = scene3d_runtime_diagnostic(runtime);
    if (error)
        return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, error, nullptr));
    return scene3d_runtime_value(runtime, !strcmp(op, "capabilities"));
}
