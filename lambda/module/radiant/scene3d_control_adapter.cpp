#include "scene3d_control_adapter.hpp"
#include "scene3d_animation_adapter.hpp"
#include "scene3d_audio_adapter.hpp"
#include "scene3d_particles_adapter.hpp"
#include "scene3d_physics_adapter.hpp"
#include "scene3d_navigation_adapter.hpp"
#include "radiant/scene3d/scene3d.hpp"
#include "radiant/scene3d/scene3d_control_access.hpp"
#include "radiant/scene3d/scene3d_source.hpp"
#include "radiant/scene3d/scene3d_physics.hpp"
#include "radiant/scene3d/scene3d_animation.hpp"
#include "radiant/scene3d/scene3d_audio.hpp"
#include "lambda/core/mark_reader.hpp"
#include "lambda/core/physics_scene.hpp"
#include "../../runtime/lambda-error.h"
#include "../../runtime/lambda-root-frame.hpp"
#include <math.h>
#include <string.h>

Item scene3d_physics_control(DomElement* root, Item request) {
    RootFrame roots(1);
    Rooted<Item> command(roots, request);
    Scene3dEntry* entry = scene3d_control_open(root);
    if (!entry)
        return ItemNull;
    if (!scene3d_control_prepare_services(entry, root))
        return ItemError;
    return scene3d_physics_operate(scene3d_control_services(entry).physics, command.get());
}

Item scene3d_control(DomElement* root, Item request) {
    // preparing animation can queue GC-allocating events; retain the command across that handoff (D6.3.3).
    RootFrame roots(1);
    Rooted<Item> command(roots, request);
    auto* entry = scene3d_control_open(root);
    if (!entry)
        return ItemError;
    auto operation = ItemReader(command.get().to_const()).asMap().get("operation");
    const char* op = operation.isString()   ? operation.asString()->chars
                     : operation.isSymbol() ? operation.asSymbol()->chars
                                            : nullptr;
    if (op && !strncmp(op, "audio_", 6)) {
        const char* action = scene3d_command_name(ItemReader(command.get().to_const()).asMap().get("payload").asMap().get("action"));
        auto payload = ItemReader(command.get().to_const()).asMap().get("payload").asMap();
        bool release = !strcmp(op, "audio_clip_drop") || !strcmp(op, "audio_cues_cancel") ||
            !strcmp(op, "audio_provider_read") || (!strcmp(op, "audio_provider_route") && payload.get("bus").isNull()) ||
            !strncmp(op, "audio_render_", 13) || (!strcmp(op, "audio_command") && action &&
            (!strcmp(action, "stop") || !strcmp(action, "pause")));
        // invalid authored edits must still allow the caller to silence and release admitted players.
        if (!scene3d_control_prepare_audio(entry, root) || (!release && !scene3d_control_prepare_services(entry, root))) return ItemError;
        return scene3d_audio_operate(scene3d_control_services(entry).audio, command.get());
    }
    if (op && !strncmp(op, "navigation_", 11)) {
        if (!strncmp(op, "navigation_agent_", 17) && !scene3d_control_prepare_services(entry, root))
            return ItemError;
        auto* navigation = scene3d_control_navigation(entry, root);
        return navigation ? scene3d_navigation_operate(navigation, root, command.get()) : ItemError;
    }
    if (!scene3d_control_prepare_services(entry, root))
        return ItemError;
    if (op && !strcmp(op, "particle_read"))
        return scene3d_particles_snapshot(scene3d_control_services(entry).particles);
    if (op && !strcmp(op, "particle_commands"))
        return scene3d_particles_commands(scene3d_control_services(entry).particles, ItemReader(command.get().to_const()).asMap().get("batch").item());
    if (op && !strcmp(op, "particle_trail_read"))
        return scene3d_particles_trail_read(scene3d_control_services(entry).particles, command.get());
    if (op && (!strcmp(op, "particle_events_read") || !strcmp(op, "particle_events_ack")))
        return scene3d_particles_events(scene3d_control_services(entry).particles, command.get());

    if (op && !strcmp(op, "animation_read"))
        return scene3d_animation_snapshot(scene3d_control_services(entry).animation);
    if (op && !strcmp(op, "animation_commands"))
        return scene3d_animation_commands(scene3d_control_services(entry).animation,
                                          ItemReader(command.get().to_const()).asMap().get("batch").item());
    if (op && !strcmp(op, "animation_replay_events"))
        return scene3d_animation_replay_events(scene3d_control_services(entry).animation, scene3d_control_services(entry).physics, command.get());
    if (op && (!strcmp(op, "seek") || !strcmp(op, "reset"))) {
        double seconds = 0;
        bool valid = strcmp(op, "seek") ||
                     physics_scene_real(ItemReader(command.get().to_const()).asMap().get("seconds"), NAN, &seconds);
        size_t selected;
        if (!valid || !scene3d_runtime_tick_index(scene3d_control_services(entry).runtime, seconds, &selected))
            return err2it_or_error(err_create_heap(
                ERR_INVALID_OPERATION, "scene3d seek: requires a fixed-tick boundary within 100000 ticks", nullptr));
        if (!scene3d_seek(root, selected))
            return ItemError;
        // return the selected state directly; command readers consume authored maps, not result VMaps.
        return scene3d_runtime_snapshot(scene3d_control_services(entry).runtime);
    }
    // an explicit scene play starts its prepared services, including an authored paused physics world.
    if (op && !strcmp(op, "play") &&
        (!scene3d_physics_play(scene3d_control_services(entry).physics, true) || !scene3d_animation_automatic(scene3d_control_services(entry).animation, true)))
        return ItemError;
    RootFrame result_roots(1);
    Rooted<Item> result(result_roots, scene3d_runtime_operate(scene3d_control_services(entry).runtime, command.get()));
    if (!item_is_error(result.get()) && op && (!strcmp(op, "play") || !strcmp(op, "pause") || !strcmp(op, "time_scale") || !strcmp(op, "step")) &&
        scene3d_audio_sample(scene3d_control_services(entry).audio, scene3d_runtime_sampled_time(scene3d_control_services(entry).runtime))) return ItemError;
    return result.get();
}
