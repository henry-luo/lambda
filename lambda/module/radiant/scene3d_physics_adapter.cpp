#include "scene3d_physics_adapter.hpp"
#include "radiant/scene3d/scene3d_physics_internal.hpp"
#include "radiant/scene3d/scene3d.hpp"
#include "radiant/scene3d/scene3d_animation.hpp"
#include "radiant/scene3d/scene3d_source.hpp"
#include "radiant/event.hpp"
#include "../../core/mark_reader.hpp"
#include "../../input/css/dom_element.hpp"
#include "../../input/css/dom_lifecycle.hpp"
#include "../../runtime/lambda-error.h"
#include "../../runtime/lambda-number-runtime.hpp"
#include "../../runtime/lambda-physics.hpp"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/value_builder.hpp"
#include <float.h>

static Item physics_scene_error(const char* message) {
    return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr));
}
bool scene3d_physics_character_configure(Scene3dPhysics* state, Item request, PhysicsCharacterMove* movement,
                                         PhysicsCharacterProfile* profile, DomElement** body, PhysicsError* error) {
    PhysicsCharacterMove configured = {};
    PhysicsCharacterProfile sampled = {};
    if (!state || !movement || !profile || !body ||
        !physics_character_read(state->scene, ItemReader(request.to_const()), &configured)) {
        if (error)
            *error = {"scene3d character: invalid movement description", 0, 0};
        return false;
    }
    if (!scene3d_physics_character_profile(state, configured.body, &sampled, error))
        return false;
    for (const auto& binding : state->scene.bindings)
        if (binding.identity == configured.body && binding.node) {
            *movement = configured;
            *profile = sampled;
            *body = (DomElement*)binding.node;
            return true;
        }
    if (error)
        *error = {"scene3d character: missing body binding", configured.body, 0};
    return false;
}
Item scene3d_physics_root_snapshot(Scene3dPhysics* state) {
    RootedValueBuilder records(true);
    if (state) {
        for (const auto& controller : state->roots) {
            RootedValueBuilder record;
            record.put("body",
                       Item{.item = s2it(heap_create_name(physics_scene_id(state->scene, controller.movement.body)))});
            record.put("source", Item{.item = s2it(heap_create_name(controller.source_id))});
            record.put("tick", box_uint64_value(controller.tick));
            record.put("world_identity", box_uint64_value(controller.world_identity));
            record.put("epoch", box_uint64_value(controller.epoch));
            record.put("sequence", box_uint64_value(controller.sequence));
            record.put("initialized", Item{.item = b2it(controller.initialized)});
            record.put("blocked", Item{.item = b2it(controller.blocked)});
            record.put("grounded", Item{.item = b2it(controller.feedback.grounded)});
            record.put("wall", Item{.item = b2it(controller.feedback.wall)});
            record.put("ceiling", Item{.item = b2it(controller.feedback.ceiling)});
            record.put("stepped", Item{.item = b2it(controller.feedback.stepped)});
            record.put("geometry_tests", box_uint64_value(controller.feedback.geometry_tests));
            const char* names[] = {"requested", "achieved", "position", "recovery", "platform_motion"};
            const PhysicsVec vectors[] = {controller.feedback.requested, controller.feedback.achieved,
                                          controller.feedback.pose.position, controller.feedback.recovery,
                                          controller.feedback.platform_motion};
            for (size_t i = 0; i < sizeof(vectors) / sizeof(*vectors); i++) {
                RootedValueBuilder value(true);
                value.append(push_d(vectors[i].x));
                value.append(push_d(vectors[i].y));
                value.append(push_d(vectors[i].z));
                record.put(names[i], value.value.get());
            }
            const char* rotation_names[] = {"requested_rotation", "achieved_rotation", "quaternion"};
            const PhysicsQuat rotations[] = {controller.requested_rotation, controller.achieved_rotation,
                                             controller.feedback.pose.rotation};
            for (unsigned i = 0; i < 3; i++) {
                RootedValueBuilder value(true);
                value.append(push_d(rotations[i].x));
                value.append(push_d(rotations[i].y));
                value.append(push_d(rotations[i].z));
                value.append(push_d(rotations[i].w));
                record.put(rotation_names[i], value.value.get());
            }
            records.append(record.value.get());
        }
    }
    return records.value.get();
}
static Item physics_scene_snapshot(Scene3dPhysics* state) {
    RootedValueBuilder rigs(true);
    const char* modes[] = {"animated", "physical", "recover"};
    for (size_t i = 0; i < state->ragdolls.size(); i++) {
        const auto& rig = state->scene.ragdolls[i];
        const auto& playback = state->ragdolls[i];
        RootedValueBuilder value;
        value.put("id", Item{.item = s2it(heap_create_name(physics_scene_id(state->scene, rig.identity)))});
        value.put("mode", Item{.item = s2it(heap_create_name(modes[playback.mode]))});
        value.put("requested", Item{.item = s2it(heap_create_name(modes[rig.mode]))});
        value.put("elapsed", push_d(playback.elapsed));
        value.put("weight",
                  push_d(playback.mode == PHYSICS_RAGDOLL_PHYSICAL   ? 0
                         : playback.mode == PHYSICS_RAGDOLL_ANIMATED ? 1
                                                                     : playback.elapsed / rig.recovery_duration));
        rigs.append(value.value.get());
    }
    return physics_state_value(state->scene, state->published, ItemNull,
                               state->ragdolls.empty() ? ItemNull : rigs.value.get());
}
Item scene3d_physics_operate(Scene3dPhysics* state, Item request) {
    RootFrame roots(2);
    Rooted<Item> rooted(roots, request);
    if (!state || !state->world)
        return physics_scene_error("scene3d physics: scene has no physics world");
    ItemReader read(rooted.get().to_const());
    ItemReader operation = read.asMap().get("operation");
    const char* op = operation.isString()   ? operation.asString()->chars
                     : operation.isSymbol() ? operation.asSymbol()->chars
                                            : nullptr;
    PhysicsError error = {};
    if (!op)
        return physics_scene_error("scene3d physics: requires an operation");
    if (!strcmp(op, "play") || !strcmp(op, "pause")) {
        if (!scene3d_physics_play(state, !strcmp(op, "play")))
            return physics_scene_error("scene3d physics: cannot start scheduler");
    } else if (!strcmp(op, "step")) {
        double ticks;
        if (!physics_scene_real(read.asMap().get("ticks"), 1, &ticks) || ticks < 0 || ticks > 100000 ||
            floor(ticks) != ticks)
            return physics_scene_error("scene3d physics: invalid tick count");
        if (!scene3d_physics_advance(state, (size_t)ticks, &state->published, &error))
            return physics_scene_error(error.message);
    } else if (!strcmp(op, "reset")) {
        DomNode *root = dom_node_ref_validate(state->document, state->root);
        bool particles = root && scene3d_particles_needs_world(scene3d_particles(root->as_element()));
        if (!state->ragdolls.empty() || !state->roots.empty() || particles) {
            if (!root || !scene3d_seek(root->as_element(), 0))
                return physics_scene_error("physics: coupled reset failed");
            return physics_scene_snapshot(state);
        }
        if (!scene3d_physics_scene_identity_available(state, &error))
            return physics_scene_error(error.message);
        scene3d_physics_play(state, false);
        PhysicsWorld* replacement = nullptr;
        if (!physics_create(&state->scene.desc, &replacement, &error))
            return physics_scene_error(error.message);
        physics_destroy(state->world);
        state->world = replacement;
        physics_read(state->world, &state->published, &error);
        state->generation++;
        scene3d_physics_scene_identity_commit(state);
    } else if (!strcmp(op, "apply")) {
        lam::ArrayList<PhysicsCommand> commands;
        const char* message = nullptr;
        if (!physics_commands_read(state->scene, read.asMap().get("commands").item(), &commands, &message))
            return physics_scene_error(message);
        if (!scene3d_physics_apply(state, commands.data(), commands.size(), &error))
            return physics_scene_error(error.message);
    } else if (!strcmp(op, "query"))
        return physics_queries_value(state->scene, state->world, read.asMap().get("queries").item());
    else if (!strcmp(op, "move")) {
        PhysicsCharacterMove movement = {};
        PhysicsCharacterState achieved = {};
        if (!physics_character_read(state->scene, ItemReader(rooted.get().to_const()), &movement) ||
            !scene3d_physics_character_move(state, &movement, &achieved, &error)) {
            return physics_scene_error(error.message ? error.message : "physics: invalid character movement request");
        }
        Rooted<Item> moved(roots, physics_character_value(state->scene, achieved));
        if (get_type_id(moved.get()) == LMD_TYPE_ERROR)
            return moved.get();
        physics_read(state->world, &state->published, &error);
        state->generation++;
        if (state->document->state)
            doc_state_request_repaint(state->document->state);
        return moved.get();
    } else if (!strcmp(op, "events"))
        return physics_events_value(state->scene, state->published);
    else if (strcmp(op, "read"))
        return physics_scene_error("scene3d physics: unsupported operation");
    if (state->document->state)
        doc_state_request_repaint(state->document->state);
    return physics_scene_snapshot(state);
}
