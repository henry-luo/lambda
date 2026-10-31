#include "scene3d_physics.hpp"
#include "../../lambda/dom/dom_events.h"
#include "../../lambda/dom/realm/dom_realm.h"
#include "../../lambda/input/css/dom_element.hpp"
#include "../../lambda/input/css/dom_lifecycle.hpp"
#include "../../lambda/js/js_runtime.h"
#include "../../lambda/runtime/lambda-error.h"
#include "../../lambda/runtime/lambda-number-runtime.hpp"
#include "../../lambda/runtime/lambda-physics.hpp"
#include "../../lambda/runtime/lambda-root-frame.hpp"
#include "../../lambda/runtime/value_builder.hpp"
#include "../../lib/hashmap_typed.hpp"
#include "../../lib/math_utils.h"
#include "../../lib/mem.h"
#include "../../lib/physics/physics_math.h"
#include "../animation_value.hpp"
#include "../event.hpp"
#include "scene3d.hpp"
#include "scene3d_animation.hpp"
#include "scene3d_source.hpp"
#include <float.h>

static void physics_scene_diagnostic(char* target, size_t capacity, const char* message) {
    if (capacity) {
        strncpy(target, message, capacity - 1);
        target[capacity - 1] = 0;
    }
}

struct PhysicsRagdollPlayback {
    PhysicsRagdollMode mode;
    double elapsed;
    bool initialized;
    PhysicsPose starts[64];
};
static constexpr size_t PHYSICS_ROOT_LIMIT = 32;
struct PhysicsRootMotion {
    DomNodeRef body, source;
    char source_id[128];
    PhysicsCharacterMove movement;
    PhysicsCharacterState feedback;
    double consumed[3], precision;
    PhysicsQuat consumed_rotation, requested_rotation, achieved_rotation;
    bool has_translation, has_rotation;
    uint64_t epoch, sequence, world_identity, tick;
    bool initialized, blocked;
};
struct PhysicsRootStep {
    PhysicsCharacterState movement;
    Scene3dRootMotion source;
    PhysicsQuat requested_rotation, achieved_rotation;
    bool blocked;
};
struct Scene3dPhysics {
    DomDocument* document = nullptr;
    DomNodeRef root = {};
    PhysicsScene scene;
    PhysicsWorld* world = nullptr;
    PhysicsState published = {};
    Scene3dRuntime* runtime = nullptr;
    uint64_t epoch = 0, generation = 1;
    uint64_t world_identity = 0;
    uint64_t* next_identity = nullptr;
    bool automatic = false, playing = false;
    lam::ArrayList<PhysicsRagdollPlayback> ragdolls;
    lam::ArrayList<PhysicsRootMotion> roots{MEM_CAT_RENDER, 0};
    lam::ArrayList<PhysicsRootMotion> pending_roots{MEM_CAT_RENDER, 0};
    bool root_definitions_pending = false;
    lam::ArrayList<PhysicsRootStep> root_steps{MEM_CAT_RENDER, 0};
    bool root_committing = false;
    lam::ArrayList<PhysicsEvent> event_buffer;
    bool collecting_events = false;
    bool advancing = false;
    bool quiet = false;
    Scene3dAnimationState* replay_animation = nullptr;
};
static bool physics_scene_identity_available(Scene3dPhysics* state, PhysicsError* error) {
    if (state->next_identity && *state->next_identity && *state->next_identity <= INT64_MAX)
        return true;
    if (error)
        *error = {"scene3d physics: world identity quota exceeded", 0, state->published.tick};
    return false;
}
static void physics_scene_identity_commit(Scene3dPhysics* state) {
    state->world_identity = (*state->next_identity)++;
}
static bool physics_roots_prepare(Scene3dPhysics* state, PhysicsError* error);
static void physics_roots_complete(Scene3dPhysics* state);
static bool physics_pose_matrix(PhysicsPose pose, Scene3dMatrix* world);
static bool physics_rigid_pose(const Scene3dMatrix& matrix, PhysicsPose* pose, bool uniform_scale = false);
static bool physics_ragdoll_prepare(Scene3dPhysics* state, double delta, lam::ArrayList<PhysicsRagdollPlayback>* next,
                                    PhysicsError* error);
// an authored controller changes solver roles/filters, without changing the immutable source description.
struct PhysicsRagdollWorld {
    lam::ArrayList<PhysicsBodyDesc> bodies;
    lam::ArrayList<PhysicsColliderDesc> colliders;
    lam::ArrayList<PhysicsJointDesc> joints;
    PhysicsDesc desc = {};
};
static bool physics_ragdoll_world(const PhysicsScene& scene, const lam::ArrayList<PhysicsRagdollPlayback>& rigs,
                                  PhysicsRagdollWorld* out) {
    if (rigs.empty()) {
        out->desc = scene.desc;
        return true;
    }
    struct Entry {
        uint64_t id;
    };
    struct Inactive {
        TypedHashMap<Entry, HashMapIntegralMemberKeyOps<Entry, &Entry::id>> ids = {};
        ~Inactive() {
            ids.destroy();
        }
    } inactive;
    if (!inactive.ids.init(scene.pose_bindings.size()))
        return false;
    for (size_t i = 0; i < scene.ragdolls.size(); i++)
        if (rigs[i].mode != PHYSICS_RAGDOLL_PHYSICAL)
            for (size_t k = 0; k < scene.ragdolls[i].count; k++)
                inactive.ids.set({scene.pose_bindings[scene.ragdolls[i].bones[k].binding].body});
    if (inactive.ids.oom())
        return false;
    for (size_t i = 0; i < scene.bodies.size(); i++) {
        auto body = scene.bodies[i];
        if (inactive.ids.get({body.id})) {
            body.motion = PHYSICS_KINEMATIC_POSITION;
            body.ccd = false;
        }
        if (!out->bodies.append(body))
            return false;
    }
    for (auto collider : scene.colliders) {
        if (inactive.ids.get({collider.body}))
            collider.membership = collider.filter = 0;
        if (!out->colliders.append(collider))
            return false;
    }
    for (auto joint : scene.joints) {
        if (inactive.ids.get({joint.body_a}) || inactive.ids.get({joint.body_b}))
            joint.disabled = true;
        if (!out->joints.append(joint))
            return false;
    }
    out->desc = scene.desc;
    out->desc.bodies = out->bodies.data();
    out->desc.colliders = out->colliders.data();
    out->desc.joints = out->joints.data();
    return true;
}
static bool physics_ragdoll_playback(Scene3dPhysics* state, const PhysicsScene& scene,
                                     lam::ArrayList<PhysicsRagdollPlayback>* out, bool retain) {
    for (const auto& rig : scene.ragdolls) {
        PhysicsRagdollPlayback playback = {};
        playback.mode = rig.mode;
        playback.initialized = rig.mode != PHYSICS_RAGDOLL_ANIMATED;
        bool kept = false;
        if (retain)
            for (size_t i = 0; i < state->scene.ragdolls.size(); i++) {
                const auto& before = state->scene.ragdolls[i];
                bool same = before.identity == rig.identity && before.count == rig.count && before.mode == rig.mode;
                for (size_t k = 0; k < rig.count && same; k++) {
                    const auto &a = state->scene.pose_bindings[before.bones[k].binding],
                               &b = scene.pose_bindings[rig.bones[k].binding];
                    same = before.bones[k].identity == rig.bones[k].identity && a.body == b.body &&
                           !memcmp(&a.body_in_bone, &b.body_in_bone, sizeof(PhysicsPose));
                }
                if (same) {
                    playback = state->ragdolls[i];
                    kept = true;
                    break;
                }
            }
        if (!kept)
            for (size_t k = 0; k < rig.count; k++) {
                size_t body = scene.pose_bindings[rig.bones[k].binding].body_index;
                playback.starts[k] = retain && body < state->published.body_count ? state->published.bodies[body].pose
                                                                                  : scene.bodies[body].pose;
            }
        if (!out->append(playback))
            return false;
    }
    return true;
}
static const char* physics_dom_tag(void* node) {
    return ((DomElement*)node)->local_name();
}
static ItemReader physics_dom_field(void* node, const char* key) {
    return scene3d_value((DomElement*)node, key);
}
static size_t physics_dom_count(void* node) {
    size_t count = 0;
    for (DomNode* c = ((DomElement*)node)->first_child; c; c = c->next_sibling)
        if (c->is_element())
            count++;
    return count;
}
static void* physics_dom_child(void* node, size_t index) {
    for (DomNode* c = ((DomElement*)node)->first_child; c; c = c->next_sibling)
        if (c->is_element()) {
            if (!index--)
                return c->as_element();
        }
    return nullptr;
}
static const char* physics_dom_text(void* node, const char* key) {
    return ((DomElement*)node)->get_attribute(key);
}
static PhysicsSceneSource physics_dom_source() {
    return {physics_dom_tag, physics_dom_field, physics_dom_count, physics_dom_child, physics_dom_text};
}
static bool physics_root_parameter_key(const char* key, char name[64]) {
    const char prefix[] = "root-motion-";
    size_t length = sizeof(prefix) - 1;
    memcpy(name, prefix, length);
    for (; *key; key++) {
        if (length == 63) {
            return false;
        }
        name[length++] = *key == '_' ? '-' : *key;
    }
    name[length] = 0;
    return true;
}
static bool physics_root_real_parameter(void* owner, const char* key, double fallback, double* value) {
    char name[64];
    return physics_root_parameter_key(key, name) && scene3d_real((DomElement*)owner, name, fallback, value);
}
static bool physics_root_vector_parameter(void* owner, const char* key, PhysicsVec fallback, PhysicsVec* value) {
    char name[64];
    Scene3dVec result;
    if (!physics_root_parameter_key(key, name) ||
        !scene3d_vector((DomElement*)owner, name, {(float)fallback.x, (float)fallback.y, (float)fallback.z}, &result)) {
        return false;
    }
    *value = {result.x, result.y, result.z};
    return true;
}
static bool physics_roots_read(const PhysicsScene& scene, DomElement* root, lam::ArrayList<PhysicsRootMotion>* result,
                               char* diagnostic, size_t capacity) {
    auto reject = [&](const char* message) {
        physics_scene_diagnostic(diagnostic, capacity, message);
        return false;
    };
    for (const auto& binding : scene.bindings) {
        auto* body = (DomElement*)binding.node;
        const char* source = scene3d_text(body, "root-motion-source");
        if (!source && scene3d_value(body, "root-motion-source").isNull()) {
            continue;
        }
        if (!source || !*source || strlen(source) >= sizeof(PhysicsRootMotion::source_id) ||
            result->size() == PHYSICS_ROOT_LIMIT) {
            return reject("root motion: requires a named source and at most 32 capsule controllers");
        }
        PhysicsError error = {};
        if (!physics_character_validate_source(&scene.desc, binding.identity, &error)) {
            return reject(error.message);
        }
        for (const auto& rig : scene.ragdolls) {
            for (size_t i = 0; i < rig.count; i++) {
                if (scene.pose_bindings[rig.bones[i].binding].body == binding.identity) {
                    return reject("root motion: body already belongs to a ragdoll controller");
                }
            }
        }
        if (scene3d_navigation_body_owned(scene3d_navigation(root), body)) {
            return reject("root motion: body already belongs to a navigation controller");
        }
        PhysicsRootMotion entry = {};
        entry.body = dom_node_ref(body);
        entry.movement = physics_character_defaults(scene.desc.length_scale);
        entry.movement.body = binding.identity;
        PhysicsCharacterParameterSource parameters = {body, physics_root_real_parameter, physics_root_vector_parameter};
        if (!physics_character_read_parameters(&parameters, &entry.movement) ||
            !physics_character_parameters_valid(&entry.movement)) {
            return reject("root motion: invalid character parameters or work budgets");
        }
        memcpy(entry.source_id, source, strlen(source) + 1);
        if (!result->append(entry)) {
            return reject("root motion: controller allocation failed");
        }
    }
    return true;
}
static void physics_roots_retain(Scene3dPhysics* state, lam::ArrayList<PhysicsRootMotion>* incoming) {
    for (auto& entry : *incoming) {
        for (const auto& previous : state->roots) {
            if (!dom_node_ref_identity_compare(entry.body, previous.body) &&
                !strcmp(entry.source_id, previous.source_id)) {
                auto movement = entry.movement;
                entry = previous;
                entry.movement = movement;
                break;
            }
        }
    }
}
bool scene3d_physics_has_root_motion(Scene3dPhysics* state) {
    return state && !(state->root_definitions_pending ? state->pending_roots : state->roots).empty();
}
static bool physics_root_owned(Scene3dPhysics* state, uint64_t body) {
    if (state) {
        for (const auto& root : state->roots) {
            if (root.movement.body == body) {
                return true;
            }
        }
    }
    return false;
}
static bool physics_root_target_allowed(Scene3dPhysics* state, uint64_t body, PhysicsError* error) {
    if (state && !state->root_committing && physics_root_owned(state, body)) {
        if (error) {
            *error = {"root motion: controller owns this body's native targets", body, state->published.tick};
        }
        return false;
    }
    return true;
}
bool scene3d_physics_root_owns_body(Scene3dPhysics* state, DomElement* node) {
    return physics_root_owned(state, scene3d_physics_character_body(state, node));
}
static const AnimationRootMotion& physics_root_clock(const Scene3dRootMotion& source) {
    return source.has_translation ? source.translation : source.rotation;
}
static PhysicsQuat physics_root_quaternion(const double value[4]) {
    return {value[0], value[1], value[2], value[3]};
}
static void physics_root_consume(PhysicsRootMotion* entry, const Scene3dRootMotion& source, uint64_t world_identity,
                                 uint64_t tick) {
    const auto& clock = physics_root_clock(source);
    entry->has_translation = source.has_translation;
    entry->has_rotation = source.has_rotation;
    memcpy(entry->consumed, source.translation.total, sizeof(entry->consumed));
    entry->consumed_rotation =
        source.has_rotation ? physics_root_quaternion(source.rotation.total) : PhysicsQuat{0, 0, 0, 1};
    entry->epoch = clock.epoch;
    entry->sequence = clock.sequence;
    entry->world_identity = world_identity;
    entry->tick = tick;
}
static bool physics_root_frame(Scene3dPhysics* state, Scene3dAnimationState* animation, DomElement* source,
                               Scene3dMatrix* frame) {
    DomNode* root = dom_node_ref_validate(state->document, state->root);
    auto* parent = source->parent && source->parent->is_element() ? source->parent->as_element() : nullptr;
    bool visible;
    return root && root->is_element() && parent &&
           scene3d_world_pose(root->as_element(), parent, animation, scene3d_override_physics_pose, state, frame,
                              &visible);
}
static bool physics_root_turn(const PhysicsRootMotion& entry, const Scene3dRootMotion& source,
                              const Scene3dMatrix& frame, uint64_t world_identity, PhysicsQuat* requested,
                              PhysicsQuat* achieved) {
    *requested = *achieved = {0, 0, 0, 1};
    if (!source.has_rotation || entry.world_identity != world_identity ||
        entry.epoch != physics_root_clock(source).epoch) {
        return true;
    }
    PhysicsPose parent = {};
    if (!physics_rigid_pose(frame, &parent, true)) {
        return false;
    }
    PhysicsQuat delta = pqmul(physics_root_quaternion(source.rotation.total), pqconj(entry.consumed_rotation));
    *requested = pqmul(pqmul(parent.rotation, delta), pqconj(parent.rotation));
    if (!pqnorm(requested)) {
        return false;
    }
    PhysicsVec up = punit(entry.movement.up), vector = {requested->x, requested->y, requested->z};
    PhysicsVec twist = pmul(up, pdot(vector, up));
    // only remove float-frame roundoff after admitting a turn about the capsule's unchanged up axis.
    if (plen(psub(vector, twist)) > 32 * FLT_EPSILON) {
        return false;
    }
    *achieved = {twist.x, twist.y, twist.z, requested->w};
    return pqnorm(achieved);
}
bool scene3d_physics_roots_admit(Scene3dPhysics* state, Scene3dAnimationState* animation, bool publish,
                                 char* diagnostic, size_t capacity) {
    if (!state) {
        return true;
    }
    auto reject = [&](const char* message) {
        physics_scene_diagnostic(diagnostic, capacity, message);
        return false;
    };
    lam::ArrayList<PhysicsRootMotion> admitted(MEM_CAT_RENDER, 0);
    const auto& definitions = state->root_definitions_pending ? state->pending_roots : state->roots;
    for (const auto& previous : definitions) {
        auto entry = previous;
        DomElement* source = nullptr;
        Scene3dRootMotion motion = {};
        auto* body = dom_node_ref_validate(state->document, entry.body);
        if (!body || !body->is_element() || !animation ||
            !scene3d_animation_root_motion(animation, entry.source_id, &source, &motion)) {
            return reject("root motion: missing or unextracted position/quaternion source");
        }
        PhysicsCharacterProfile profile = {};
        PhysicsCharacterState probe = {};
        PhysicsError error = {};
        // zero displacement validates capsule orientation/precision without mutating a target or tick.
        if (!scene3d_physics_character_profile(state, entry.movement.body, &profile, &error) ||
            !scene3d_physics_character_compute(state, &entry.movement, &probe, &error)) {
            return reject(error.message ? error.message : "root motion: invalid capsule profile");
        }
        entry.precision = profile.precision;
        if (motion.has_rotation) {
            Scene3dMatrix frame = {};
            PhysicsPose parent = {};
            PhysicsVec lever = psub(profile.shape_pose.position, profile.pose.position);
            if (plen(pcross(lever, punit(entry.movement.up))) > profile.precision ||
                !physics_root_frame(state, animation, source, &frame) || !physics_rigid_pose(frame, &parent, true)) {
                return reject("root motion: capsule turn requires an axial pivot and a uniform proper parent frame");
            }
        }
        DomNodeRef current = dom_node_ref(source);
        const auto& clock = physics_root_clock(motion);
        bool rebase = !entry.initialized || entry.world_identity != state->world_identity ||
                      entry.epoch != clock.epoch || entry.has_translation != motion.has_translation ||
                      entry.has_rotation != motion.has_rotation || dom_node_ref_identity_compare(entry.source, current);
        if (rebase) {
            entry.source = current;
            physics_root_consume(&entry, motion, state->world_identity, state->published.tick);
            entry.requested_rotation = entry.achieved_rotation = {0, 0, 0, 1};
            entry.feedback = {};
            entry.feedback.pose = profile.pose;
            entry.blocked = false;
            entry.initialized = true;
        }
        if (!admitted.append(entry)) {
            return reject("root motion: admission allocation failed");
        }
    }
    if (publish) {
        if (!state->root_steps.reserve(admitted.size())) {
            return reject("root motion: tick scratch allocation failed");
        }
        state->roots = static_cast<lam::ArrayList<PhysicsRootMotion>&&>(admitted);
        // a removed last controller has no prepare phase to retire its previous tick's scratch.
        state->root_steps.clear();
        state->pending_roots.clear();
        state->root_definitions_pending = false;
    }
    return true;
}
static bool physics_roots_prepare(Scene3dPhysics* state, PhysicsError* error) {
    state->root_steps.clear();
    if (state->roots.empty()) {
        return true;
    }
    DomNode* root_node = dom_node_ref_validate(state->document, state->root);
    auto* root = root_node && root_node->is_element() ? root_node->as_element() : nullptr;
    auto* animation = state->replay_animation ? state->replay_animation : root ? scene3d_animations(root) : nullptr;
    auto reject = [&](const char* message) {
        if (error) {
            *error = {message, 0, state->published.tick};
        }
        return false;
    };
    PhysicsCommand commands[PHYSICS_ROOT_LIMIT] = {};
    unsigned remaining_work = 65536;
    size_t count = 0;
    for (const auto& entry : state->roots) {
        PhysicsRootStep step = {};
        DomNode* source_node = dom_node_ref_validate(state->document, entry.source);
        auto* source = source_node && source_node->is_element() ? source_node->as_element() : nullptr;
        if (!root || !entry.initialized || !source || !scene3d_animation_root_value(animation, source, &step.source)) {
            return reject("root motion: detached or unadmitted source during a tick");
        }
        auto movement = entry.movement;
        double delta[3] = {};
        if (step.source.has_translation && entry.world_identity == state->world_identity &&
            entry.epoch == physics_root_clock(step.source).epoch) {
            for (unsigned i = 0; i < 3; i++) {
                delta[i] = step.source.translation.total[i] - entry.consumed[i];
            }
        }
        Scene3dMatrix frame = {};
        if (!physics_root_frame(state, animation, source, &frame)) {
            return reject("root motion: source requires a connected parent frame");
        }
        // position tracks describe their parent's coordinate axes; translation never enters this vector conversion.
        movement.displacement = {frame.v[0] * delta[0] + frame.v[4] * delta[1] + frame.v[8] * delta[2],
                                 frame.v[1] * delta[0] + frame.v[5] * delta[1] + frame.v[9] * delta[2],
                                 frame.v[2] * delta[0] + frame.v[6] * delta[1] + frame.v[10] * delta[2]};
        if (!physics_root_turn(entry, step.source, frame, state->world_identity, &step.requested_rotation,
                               &step.achieved_rotation)) {
            return reject("root motion: capsule turns must preserve up in a uniform proper parent frame");
        }
        movement.platform = entry.feedback.platform;
        movement.platform_pose = entry.feedback.platform_pose;
        if (!remaining_work) {
            return reject("root motion: shared character geometry budget exhausted");
        }
        movement.geometry_budget = (unsigned)fmin(movement.geometry_budget, remaining_work);
        if (!scene3d_physics_character_compute(state, &movement, &step.movement, error)) {
            return false;
        }
        remaining_work -= (unsigned)step.movement.geometry_tests;
        step.movement.pose.rotation = pqmul(step.achieved_rotation, step.movement.pose.rotation);
        if (!pqnorm(&step.movement.pose.rotation)) {
            return reject("root motion: capsule turn is outside the numerical range");
        }
        double length = plen(movement.displacement);
        PhysicsVec controlled =
            psub(psub(step.movement.achieved, step.movement.recovery), step.movement.platform_motion);
        double precision = 4 * entry.precision;
        step.blocked = length > precision && pdot(controlled, movement.displacement) / length < length - precision;
        if (!state->root_steps.append(step)) {
            return reject("root motion: tick scratch allocation failed");
        }
        commands[count].kind = PHYSICS_TARGET;
        commands[count].body = movement.body;
        commands[count++].pose = step.movement.pose;
    }
    // all geometry queries succeed before one target transaction; feedback publishes after the solver tick.
    state->root_committing = true;
    bool valid = scene3d_physics_apply(state, commands, count, error);
    state->root_committing = false;
    return valid;
}
static void physics_roots_complete(Scene3dPhysics* state) {
    for (size_t i = 0; i < state->root_steps.size(); i++) {
        auto& controller = state->roots[i];
        const auto& step = state->root_steps[i];
        physics_root_consume(&controller, step.source, state->world_identity, state->published.tick);
        controller.requested_rotation = step.requested_rotation;
        controller.achieved_rotation = step.achieved_rotation;
        controller.feedback = step.movement;
        controller.blocked = step.blocked;
    }
}
static Item physics_scene_error(const char* message) {
    return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr));
}
struct PhysicsDomEvent {
    const PhysicsScene* scene;
    const PhysicsEvent* event;
    const char* type;
};
static Item physics_scene_event(void* data) {
    auto* source = (PhysicsDomEvent*)data;
    RootFrame roots(2);
    Rooted<Item> event(roots, js_create_event(source->type, true, false));
    Rooted<Item> detail(roots, physics_event_value(*source->scene, *source->event));
    dom_realm_set_name(event.get(), "detail", detail.get());
    return event.get();
}
static bool physics_scene_step(Scene3dPhysics* state, size_t ticks, PhysicsError* error) {
    uint64_t previous_tick = state->published.tick;
    lam::ArrayList<PhysicsRagdollPlayback> next;
    if (!state->ragdolls.empty() && ticks &&
        (ticks != 1 || !physics_ragdoll_prepare(state, state->scene.desc.timestep, &next, error)))
        return false;
    if (ticks && !state->roots.empty() && (ticks != 1 || !physics_roots_prepare(state, error))) {
        return false;
    }
    bool complete = physics_step(state->world, ticks, &state->published, error);
    if (complete && ticks) {
        physics_roots_complete(state);
    }
    if (complete && ticks && !state->ragdolls.empty())
        state->ragdolls = static_cast<lam::ArrayList<PhysicsRagdollPlayback>&&>(next);
    // a failed multi-tick call can still commit a prefix; refresh metadata with
    // the borrowed body snapshot before rendering or delivering its events.
    if (!complete)
        physics_read(state->world, &state->published, nullptr);
    if (state->collecting_events)
        for (size_t i = 0; i < state->published.event_count; i++)
            if (state->event_buffer.size() >= state->scene.desc.event_limit ||
                !state->event_buffer.append(state->published.events[i])) {
                if (error)
                    error->message = "physics: ragdoll batch event quota exceeded";
                complete = false;
                break;
            }
    // the shared queue copies event values now and delivers handlers after the solver has unwound.
    DomNode* root = dom_node_ref_validate(state->document, state->root);
    // another step on an already faulted world must not deliver its old events again.
    if (!state->quiet && root && (complete || state->published.tick != previous_tick))
        for (size_t i = 0; i < state->published.event_count; i++) {
            const auto& event = state->published.events[i];
            const char* type = event.joint    ? "physicsjointbreak"
                               : event.sensor ? (event.started ? "physicssensorenter" : "physicssensorexit")
                                              : (event.started ? "physicscontactstart" : "physicscontactend");
            PhysicsDomEvent data = {&state->scene, &event, type};
            if (!radiant_queue_timing_event((UiContext*)state->document->js.host_ui_context, root->as_element(),
                                            physics_scene_event, &data, type)) {
                if (complete && error)
                    error->message = "scene3d physics: cannot queue contact event";
                complete = false;
            }
        }
    state->generation++;
    if (!state->quiet && state->document->state)
        doc_state_request_repaint(state->document->state);
    return complete;
}
static Scene3dTickResult physics_scene_tick(void* owner, double) {
    auto* state = (Scene3dPhysics*)owner;
    DomNode* root = dom_node_ref_validate(state->document, state->root);
    if (!root || !state->world)
        return SCENE3D_TICK_IDLE;
    PhysicsError error = {};
    if (!physics_scene_step(state, 1, &error)) {
        log_error("scene3d physics tick: %s", error.message ? error.message : "simulation failed");
        state->playing = false;
        return SCENE3D_TICK_FAILED;
    }
    return SCENE3D_TICK_ACTIVE;
}
bool scene3d_physics_play(Scene3dPhysics* state, bool playing) {
    state->playing = playing;
    if (!scene3d_runtime_enable(state->runtime, SCENE3D_TICK_PHYSICS, state,
                                playing && state->automatic && state->world)) {
        return false;
    }
    DomNode* root = dom_node_ref_validate(state->document, state->root);
    if (root && root->is_element() && scene3d_physics(root->as_element()) == state) {
        return scene3d_animation_sync_schedule(scene3d_animations(root->as_element()));
    }
    return true;
}
bool scene3d_physics_playing(Scene3dPhysics* state) {
    return state && state->playing && state->automatic && state->world;
}
bool scene3d_physics_character_profile(Scene3dPhysics* state, uint64_t body, PhysicsCharacterProfile* profile,
                                       PhysicsError* error) {
    return physics_character_profile(state ? state->world : nullptr, body, profile, error);
}
uint64_t scene3d_physics_body(Scene3dPhysics *state, DomElement *node) {
    if (state && node)
        for (const auto& binding : state->scene.bindings)
            if (binding.node == node)
                return binding.identity;
    return 0;
}
uint64_t scene3d_physics_character_body(Scene3dPhysics *state, DomElement *node) {
    return scene3d_physics_body(state, node);
}
bool scene3d_physics_query(Scene3dPhysics *state, const PhysicsQuery *queries, size_t count, PhysicsHit *hits,
                           PhysicsError *error) {
    return physics_query(state ? state->world : nullptr, queries, count, hits, error);
}
size_t scene3d_physics_query_colliders(Scene3dPhysics *state) { return state ? state->scene.colliders.size() : 0; }
bool scene3d_physics_velocity_at(Scene3dPhysics *state, uint64_t body, PhysicsVec point, PhysicsVec *velocity,
                                 PhysicsError *error) {
    if (state && velocity && pfinite(point)) {
        for (size_t i = 0; i < state->published.body_count; i++) {
            const auto &value = state->published.bodies[i];
            if (value.id != body)
                continue;
            *velocity = padd(value.velocity, pcross(value.angular_velocity, psub(point, value.centre_of_mass)));
            if (pfinite(*velocity))
                return true;
            break;
        }
    }
    if (error)
        *error = {"scene3d physics: invalid sampled surface velocity", body, state ? state->published.tick : 0};
    return false;
}
bool scene3d_physics_character_compute(Scene3dPhysics* state, const PhysicsCharacterMove* movement,
                                       PhysicsCharacterState* result, PhysicsError* error) {
    return physics_character_move(state ? state->world : nullptr, movement, result, error);
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
bool scene3d_physics_character_move(Scene3dPhysics* state, const PhysicsCharacterMove* movement,
                                    PhysicsCharacterState* result, PhysicsError* error) {
    if (!state || !movement || !result) {
        if (error)
            *error = {"scene3d character: invalid movement output", 0, 0};
        return false;
    }
    if (!physics_root_target_allowed(state, movement->body, error)) {
        return false;
    }
    PhysicsCharacterState moved = {};
    if (!scene3d_physics_character_compute(state, movement, &moved, error))
        return false;
    PhysicsCommand command = {};
    command.kind = PHYSICS_TARGET;
    command.body = movement->body;
    command.pose = moved.pose;
    // use the same native target commit as script-driven movement, without allocating script values in a tick.
    if (!scene3d_physics_apply(state, &command, 1, error))
        return false;
    *result = moved;
    return true;
}
static bool physics_scene_animation_conflict(const PhysicsScene& scene, DomElement* node, unsigned depth = 0) {
    if (depth > 64)
        return true;
    if (!strcmp(node->local_name(), "keyframe-track")) {
        const char* path = scene3d_text(node, "path");
        if (path)
            for (size_t i = 0; i < scene.bindings.size(); i++) {
                const auto& binding = scene.bindings[i];
                const char* visual = scene3d_text((DomElement*)binding.node, "id");
                size_t length = visual ? strlen(visual) : 0;
                if (length && !strncmp(path, visual, length) && path[length] == '.') {
                    const char* property = path + length + 1;
                    if (!strcmp(property, "position") || !strcmp(property, "quaternion")) {
                        bool extracted;
                        // extraction leaves this pose solver-owned; it does not write an animated body target.
                        return !scene3d_flag(node, "root-motion", false, &extracted) || !extracted;
                    }
                    if (!strcmp(property, "rotation")) {
                        return true;
                    }
                }
            }
    }
    for (DomNode* c = node->first_child; c; c = c->next_sibling)
        if (c->is_element() && physics_scene_animation_conflict(scene, c->as_element(), depth + 1))
            return true;
    return false;
}
Scene3dPhysics* scene3d_physics_create(DomElement* root, char* diagnostic, size_t capacity, bool automatic,
                                       Scene3dRuntime* runtime) {
    void* memory = mem_alloc(sizeof(Scene3dPhysics), MEM_CAT_RENDER);
    if (!memory)
        return nullptr;
    auto* state = new (memory) Scene3dPhysics();
    state->document = root->doc;
    state->root = dom_node_ref(root);
    state->epoch = root->doc->mutation_epoch;
    state->automatic = automatic;
    state->runtime = runtime;
    state->next_identity = scene3d_physics_sequence(root->doc);
    if (!scene3d_runtime_bind(runtime, SCENE3D_TICK_PHYSICS, state, physics_scene_tick)) {
        state->~Scene3dPhysics();
        mem_free(state);
        return nullptr;
    }
    PhysicsError error = {};
    bool valid = physics_scene_identity_available(state, &error) &&
                 physics_scene_compile(&state->scene, physics_dom_source(), root);
    if (valid && physics_scene_animation_conflict(state->scene, root)) {
        valid = false;
        error.message = "physics body animation tracks require tick sampling support";
    }
    if (valid) {
        valid = physics_roots_read(state->scene, root, &state->roots, diagnostic, capacity);
        if (!valid) {
            error.message = diagnostic;
        }
    }
    if (valid)
        valid = physics_ragdoll_playback(state, state->scene, &state->ragdolls, false);
    PhysicsRagdollWorld effective;
    if (valid && !state->scene.bodies.empty())
        valid = physics_ragdoll_world(state->scene, state->ragdolls, &effective) &&
                physics_create(&effective.desc, &state->world, &error) &&
                physics_read(state->world, &state->published, &error);
    if (valid)
        valid = scene3d_runtime_configure(runtime, state->scene.desc.timestep, state->scene.max_substeps) &&
                scene3d_physics_play(state, state->scene.playing);
    if (!valid) {
        if (error.message != diagnostic) {
            physics_scene_diagnostic(diagnostic, capacity, error.message ? error.message : state->scene.diagnostic);
        }
        scene3d_physics_destroy(state);
        return nullptr;
    }
    physics_scene_identity_commit(state);
    return state;
}
void scene3d_physics_destroy(Scene3dPhysics* state) {
    if (!state)
        return;
    scene3d_runtime_unbind(state->runtime, SCENE3D_TICK_PHYSICS, state);
    physics_destroy(state->world);
    state->~Scene3dPhysics();
    mem_free(state);
}
// bodies and colliders keep their identities/owners, and the world its stepping.
static bool physics_scene_same_structure(const PhysicsScene& before, const PhysicsScene& after) {
    if (after.bodies.size() != before.bodies.size() || after.colliders.size() != before.colliders.size() ||
        after.bindings.size() != before.bindings.size() || after.desc.timestep != before.desc.timestep ||
        after.desc.iterations != before.desc.iterations || after.desc.length_scale != before.desc.length_scale)
        return false;
    for (size_t i = 0; i < after.bodies.size(); i++)
        if (after.bodies[i].id != before.bodies[i].id)
            return false;
    for (size_t i = 0; i < after.colliders.size(); i++)
        if (after.colliders[i].id != before.colliders[i].id || after.colliders[i].body != before.colliders[i].body)
            return false;
    for (size_t i = 0; i < after.bindings.size(); i++)
        if (after.bindings[i].node != before.bindings[i].node)
            return false;
    return true;
}
// an authored edit reaches the running world instead of restarting it (Lambda_Pkg_Physics.md §6.2):
// gravity, body poses and velocities become commands that override the simulated values, and
// damping, gravity scale and collider changes are configured in place. The template is the higher
// tier and overrides only what it changed (S12.1.5v2); a structural change restarts the world.
static bool physics_scene_reconcile(Scene3dPhysics* state, PhysicsScene& current, char* diagnostic, size_t capacity) {
    const PhysicsScene& before = state->scene;
    lam::ArrayList<PhysicsCommand> commands;
    bool configure = false;
    auto command = [&](PhysicsCommandKind kind, uint64_t body, PhysicsVec value, PhysicsPose pose) {
        PhysicsCommand c = {};
        c.kind = kind;
        c.body = body;
        c.value = value;
        c.pose = pose;
        return commands.append(c);
    };
    PhysicsPose still = {{0, 0, 0}, {0, 0, 0, 1}};
    if (memcmp(&current.desc.gravity, &before.desc.gravity, sizeof(PhysicsVec)) &&
        !command(PHYSICS_GRAVITY, 0, current.desc.gravity, still))
        return false;
    for (size_t i = 0; i < current.bodies.size(); i++) {
        const PhysicsBodyDesc &was = before.bodies[i], &now = current.bodies[i];
        if ((memcmp(&now.pose, &was.pose, sizeof(PhysicsPose)) && !command(PHYSICS_TELEPORT, now.id, {}, now.pose)) ||
            (memcmp(&now.velocity, &was.velocity, sizeof(PhysicsVec)) &&
             !command(PHYSICS_VELOCITY, now.id, now.velocity, still)) ||
            (memcmp(&now.angular_velocity, &was.angular_velocity, sizeof(PhysicsVec)) &&
             !command(PHYSICS_ANGULAR_VELOCITY, now.id, now.angular_velocity, still)))
            return false;
        configure |= now.motion != was.motion || now.linear_damping != was.linear_damping ||
                     now.angular_damping != was.angular_damping || now.gravity_scale != was.gravity_scale ||
                     now.ccd != was.ccd;
    }
    // source buffers have distinct addresses after a compile; only changed geometry is an edit.
    configure |= !physics_scene_colliders_equal(current, before);
    bool joint_changes = current.joints.size() != before.joints.size();
    if (!joint_changes)
        for (size_t i = 0; i < current.joints.size(); i++)
            joint_changes |= !physics_joint_equal(&current.joints[i], &before.joints[i]);
    configure |= joint_changes;
    bool pose_changes = current.pose_bindings.size() != before.pose_bindings.size();
    if (!pose_changes)
        for (size_t i = 0; i < current.pose_bindings.size(); i++) {
            const auto &was = before.pose_bindings[i], &now = current.pose_bindings[i];
            pose_changes |= was.node != now.node || was.identity != now.identity || was.body != now.body ||
                            was.enabled != now.enabled ||
                            memcmp(&was.body_in_bone, &now.body_in_bone, sizeof(PhysicsPose));
        }
    bool world_changes = configure || commands.size(), changed = world_changes || pose_changes;
    PhysicsError error = {};
    lam::ArrayList<PhysicsRagdollPlayback> rigs;
    if (!physics_ragdoll_playback(state, current, &rigs, true))
        return false;
    bool rig_changes = current.ragdolls.size() != before.ragdolls.size();
    if (!rig_changes)
        for (size_t i = 0; i < current.ragdolls.size(); i++) {
            const auto &a = before.ragdolls[i], &b = current.ragdolls[i];
            rig_changes |= a.identity != b.identity || a.mode != b.mode || a.count != b.count ||
                           a.recovery_duration != b.recovery_duration;
            if (a.count == b.count)
                for (size_t k = 0; k < a.count; k++)
                    rig_changes |= a.bones[k].identity != b.bones[k].identity;
        }
    world_changes |= rig_changes || (!current.ragdolls.empty() && pose_changes);
    changed |= world_changes;
    PhysicsRagdollWorld effective;
    if (world_changes && state->world && !physics_scene_identity_available(state, &error)) {
        physics_scene_diagnostic(diagnostic, capacity, error.message);
        return false;
    }
    if (world_changes && state->world &&
        (!physics_ragdoll_world(current, rigs, &effective) ||
         !physics_reconfigure_commands(state->world, &effective.desc, commands.data(), commands.size(), &error))) {
        physics_scene_diagnostic(diagnostic, capacity,
                                 error.message ? error.message : "authored physics edit rejected");
        log_debug("scene3d physics: authored edit rejected: %s", error.message ? error.message : "");
        return false;
    }
    if (world_changes && state->world)
        physics_scene_identity_commit(state);
    bool playing = current.playing != before.playing;
    // pacing is coordinator-owned, so an authored budget edit must reach it without replacing the world.
    if (!scene3d_runtime_configure(state->runtime, current.desc.timestep, current.max_substeps))
        return false;
    // the moved arrays keep their buffers, so the descriptor's views stay valid
    state->scene = static_cast<PhysicsScene&&>(current);
    state->ragdolls = static_cast<lam::ArrayList<PhysicsRagdollPlayback>&&>(rigs);
    if (playing)
        scene3d_physics_play(state, state->scene.playing);
    if (changed && state->world) {
        physics_read(state->world, &state->published, nullptr);
        state->generation++;
        if (state->document->state)
            doc_state_request_repaint(state->document->state);
    }
    return true;
}
Scene3dPhysicsRefresh scene3d_physics_refresh(Scene3dPhysics* state, DomElement* root, char* diagnostic,
                                              size_t capacity) {
    if (!state || state->document != root->doc)
        return SCENE3D_PHYSICS_REBUILD;
    if (state->epoch == root->doc->mutation_epoch)
        return SCENE3D_PHYSICS_KEEP;
    PhysicsScene current;
    auto reject = [&](const char* message) {
        physics_scene_diagnostic(diagnostic, capacity, message);
        return SCENE3D_PHYSICS_REJECT;
    };
    if (!physics_scene_compile(&current, physics_dom_source(), root))
        return reject(current.diagnostic);
    // reject before a world transaction or structural replacement can invalidate published views mid-tick.
    if (!scene3d_runtime_can_configure(state->runtime, current.desc.timestep, current.max_substeps))
        return reject("physics: authored edits are unavailable during tick sampling");
    // tracks added after initial compilation must obey the same transform ownership rule.
    if (physics_scene_animation_conflict(current, root))
        return reject("physics body animation tracks require tick sampling support");
    lam::ArrayList<PhysicsRootMotion> roots(MEM_CAT_RENDER, 0);
    if (!physics_roots_read(current, root, &roots, diagnostic, capacity)) {
        return SCENE3D_PHYSICS_REJECT;
    }
    if (!physics_scene_same_structure(state->scene, current)) {
        // coordinated admission must not delete the owner whose public batch is still on the stack.
        if (state->advancing)
            return reject("physics: cannot replace an owner during its owned batch");
        return SCENE3D_PHYSICS_REBUILD;
    }
    physics_scene_diagnostic(diagnostic, capacity, "authored physics edit rejected");
    if (!physics_scene_reconcile(state, current, diagnostic, capacity))
        return SCENE3D_PHYSICS_REJECT;
    physics_roots_retain(state, &roots);
    state->pending_roots = static_cast<lam::ArrayList<PhysicsRootMotion>&&>(roots);
    state->root_definitions_pending = true;
    state->epoch = root->doc->mutation_epoch;
    if (capacity)
        diagnostic[0] = 0;
    return SCENE3D_PHYSICS_KEEP;
}
// bindings hold raw node pointers, so only nodes still inside the old scene are dereferenced
template <class Binding>
static void physics_follow_bindings(lam::ArrayList<Binding>& list, DomNode* node, DomNode* old_root,
                                    DomNode* new_root) {
    if (!node->is_element())
        return;
    for (size_t i = 0; i < list.size(); i++)
        if (list[i].node == (void*)node->as_element()) {
            DomNode* replacement = view_state_corresponding_node(node, old_root, new_root);
            list[i].node = replacement && replacement->is_element() ? replacement->as_element() : nullptr;
        }
    for (DomNode* c = node->as_element()->first_child; c; c = c->next_sibling)
        physics_follow_bindings(list, c, old_root, new_root);
}
// a template re-render rebuilds the scene's DOM: the world follows the corresponding
// nodes, as form controls keep native edits, and the next match reconciles any
// authored change against it
void scene3d_physics_follow_rebuild(Scene3dPhysics* state, DomNode* old_root, DomNode* new_root) {
    if (!state)
        return;
    DomNode* scene = dom_node_ref_validate(state->document, state->root);
    if (!scene)
        return;
    // detached children are absent from the retained scene; walk their still-live old subtree instead.
    DomNode* scope = view_state_corresponding_node(scene, old_root, new_root) ? scene : old_root;
    physics_follow_bindings(state->scene.bindings, scope, old_root, new_root);
    physics_follow_bindings(state->scene.ids, scope, old_root, new_root);
    physics_follow_bindings(state->scene.pose_bindings, scope, old_root, new_root);
    physics_follow_bindings(state->scene.ragdolls, scope, old_root, new_root);
    for (auto& controller : state->roots) {
        view_state_ref_follow_rebuild(state->document, &controller.body, old_root, new_root);
        view_state_ref_follow_rebuild(state->document, &controller.source, old_root, new_root);
    }
    view_state_ref_follow_rebuild(state->document, &state->root, old_root, new_root);
    // a rebuild leaves the document's mutation epoch alone; force the next match to recompile
    state->epoch = UINT64_MAX;
}
uint64_t scene3d_physics_generation(Scene3dPhysics* state) {
    return state ? state->generation : 0;
}
uint64_t scene3d_physics_world_identity(Scene3dPhysics* state) {
    return state ? state->world_identity : 0;
}
bool scene3d_physics_has_ragdolls(Scene3dPhysics* state) {
    return state && !state->ragdolls.empty();
}
bool scene3d_physics_advance(Scene3dPhysics* state, size_t ticks, PhysicsState* result, PhysicsError* error) {
    if (!state || !state->world || !result)
        return false;
    scene3d_physics_play(state, false);
    // ragdoll targets consume the same animation-before-physics ticks as automatic playback.
    auto* root = dom_node_ref_validate(state->document, state->root);
    bool agents = root && scene3d_navigation_has_agents(scene3d_navigation(root->as_element()));
    bool particles = root && scene3d_particles_needs_world(scene3d_particles(root->as_element()));
    // scene-query particles must observe the same completed worlds when the legacy physics surface steps.
    bool coordinated = (!state->ragdolls.empty() || !state->roots.empty() || agents || particles) && ticks;
    if (coordinated) {
        state->event_buffer.clear();
        state->collecting_events = true;
        state->advancing = true;
    }
    bool complete = coordinated ? scene3d_runtime_step(state->runtime, ticks) : physics_scene_step(state, ticks, error);
    if (coordinated) {
        state->advancing = false;
        state->collecting_events = false;
        state->published.events = state->event_buffer.data();
        state->published.event_count = state->event_buffer.size();
        if (!complete && error && !error->message)
            error->message = scene3d_runtime_diagnostic(state->runtime);
    }
    *result = state->published;
    return complete;
}
bool scene3d_physics_seek(Scene3dPhysics* state, size_t ticks) {
    if (!state || ticks > 100000)
        return false;
    if (!state->world)
        return true;
    if ((!state->ragdolls.empty() || !state->roots.empty()) && ticks) {
        log_error("scene3d physics seek: coupled controllers require the shared scene replay");
        return false;
    }
    PhysicsWorld* replacement = nullptr;
    PhysicsState selected = {};
    PhysicsError error = {};
    if (!physics_scene_identity_available(state, &error))
        return false;
    lam::ArrayList<PhysicsRagdollPlayback> rigs;
    PhysicsRagdollWorld effective;
    // replay a replacement from authored input; failure preserves the current world, and seek emits no gameplay cues.
    if (!physics_ragdoll_playback(state, state->scene, &rigs, false) ||
        !physics_ragdoll_world(state->scene, rigs, &effective) ||
        !physics_create(&effective.desc, &replacement, &error) ||
        !physics_step(replacement, ticks, &selected, &error) || !physics_step(replacement, 0, &selected, &error)) {
        physics_destroy(replacement);
        log_error("scene3d physics seek: %s", error.message ? error.message : "replay failed");
        return false;
    }
    physics_destroy(state->world);
    state->world = replacement;
    state->published = selected;
    state->generation++;
    physics_scene_identity_commit(state);
    state->ragdolls = static_cast<lam::ArrayList<PhysicsRagdollPlayback>&&>(rigs);
    if (state->document->state)
        doc_state_request_repaint(state->document->state);
    return true;
}
static bool physics_pose_matrix(PhysicsPose pose, Scene3dMatrix* world) {
    double q[4] = {pose.rotation.x, pose.rotation.y, pose.rotation.z, pose.rotation.w}, matrix[16];
    if (!animation_quaternion_matrix(q, matrix))
        return false;
    *world = scene3d_rotation_matrix(matrix, {(float)pose.position.x, (float)pose.position.y, (float)pose.position.z});
    return true;
}
struct PhysicsRagdollSample {
    Scene3dPhysics* state;
    const PhysicsRagdollDesc* rig;
    double delta;
    bool invalid = false;
};
static bool physics_ragdoll_parent(void* owner, DomElement* node, Scene3dMatrix* matrix) {
    auto& sample = *(PhysicsRagdollSample*)owner;
    auto* state = sample.state;
    for (size_t k = 0; k < sample.rig->count; k++)
        if (state->scene.pose_bindings[sample.rig->bones[k].binding].node == node)
            return false;
    for (size_t i = 0; i < state->scene.bindings.size(); i++)
        if (state->scene.bindings[i].node == node) {
            const auto& body = state->published.bodies[i];
            PhysicsPose pose = body.pose;
            if (body.motion == PHYSICS_DYNAMIC)
                sample.invalid = true;
            else if (body.motion == PHYSICS_KINEMATIC_POSITION && body.target_set)
                pose = body.target;
            else if (body.motion == PHYSICS_KINEMATIC_VELOCITY) {
                PhysicsVec local_centre = prot(pqconj(pose.rotation), psub(body.centre_of_mass, pose.position));
                pose = ppose_integrate(pose, local_centre, body.velocity, body.angular_velocity, sample.delta);
            }
            return physics_pose_matrix(pose, matrix);
        }
    for (const auto& binding : state->scene.pose_bindings)
        if (binding.node == node && binding.enabled) {
            // cross-controller dynamic/kinematic ancestors need a coupled target schedule, not last-tick poses.
            if (state->published.bodies[binding.body_index].motion != PHYSICS_FIXED)
                sample.invalid = true;
            break;
        }
    return scene3d_physics_pose(state, node, matrix);
}
static bool physics_rigid_pose(const Scene3dMatrix& matrix, PhysicsPose* pose, bool uniform_scale) {
    PhysicsVec axes[3];
    double rotation[9], quaternion[4];
    for (unsigned i = 0; i < 3; i++)
        axes[i] = pv(matrix.v[i * 4], matrix.v[i * 4 + 1], matrix.v[i * 4 + 2]);
    if (uniform_scale) {
        double scale = plen(axes[0]);
        if (!(scale > 0) || !isfinite(scale)) {
            return false;
        }
        for (unsigned i = 0; i < 3; i++) {
            axes[i] = pmul(axes[i], 1 / scale);
        }
    }
    for (unsigned i = 0; i < 3; i++)
        for (unsigned k = 0; k < 3; k++)
            if (fabs(pdot(axes[i], axes[k]) - (i == k)) > 1e-4)
                return false;
    if (pdot(pcross(axes[0], axes[1]), axes[2]) < 0)
        return false;
    for (unsigned row = 0; row < 3; row++)
        for (unsigned column = 0; column < 3; column++)
            rotation[row * 3 + column] = paxis(axes[column], row);
    if (!math_rotation_quaternion(rotation, quaternion))
        return false;
    pose->position = pv(matrix.v[12], matrix.v[13], matrix.v[14]);
    pose->rotation = {quaternion[0], quaternion[1], quaternion[2], quaternion[3]};
    return pfinite(pose->position);
}
static bool physics_ragdoll_prepare(Scene3dPhysics* state, double delta, lam::ArrayList<PhysicsRagdollPlayback>* next,
                                    PhysicsError* error) {
    auto fail = [&](const char* message) {
        if (error)
            error->message = message;
        return false;
    };
    DomNode* root = dom_node_ref_validate(state->document, state->root);
    if (!root || !root->is_element())
        return fail("physics: detached ragdoll scene");
    lam::ArrayList<PhysicsCommand> commands;
    for (size_t i = 0; i < state->ragdolls.size(); i++) {
        auto playback = state->ragdolls[i];
        const auto& rig = state->scene.ragdolls[i];
        if (playback.mode != PHYSICS_RAGDOLL_PHYSICAL && (delta > 0 || !playback.initialized)) {
            double elapsed = fmin(rig.recovery_duration, playback.elapsed + delta);
            double weight = playback.mode == PHYSICS_RAGDOLL_RECOVER ? elapsed / rig.recovery_duration : 1;
            PhysicsRagdollSample sample = {state, &rig, delta};
            for (size_t k = 0; k < rig.count; k++) {
                const auto& binding = state->scene.pose_bindings[rig.bones[k].binding];
                Scene3dMatrix matrix = {};
                bool visible;
                PhysicsPose destination;
                auto* animation =
                    state->replay_animation ? state->replay_animation : scene3d_animations(root->as_element());
                if (!scene3d_world_pose(root->as_element(), (DomElement*)binding.node, animation,
                                        physics_ragdoll_parent, &sample, &matrix, &visible) ||
                    sample.invalid || !physics_rigid_pose(matrix, &destination))
                    return fail("physics: ragdoll animation requires rigid poses and fixed/kinematic body ancestors");
                destination = pcompose(destination, binding.body_in_bone);
                if (playback.mode == PHYSICS_RAGDOLL_RECOVER) {
                    const auto& from = playback.starts[k];
                    double a[] = {from.rotation.x, from.rotation.y, from.rotation.z, from.rotation.w};
                    double b[] = {destination.rotation.x, destination.rotation.y, destination.rotation.z,
                                  destination.rotation.w},
                           q[4];
                    if (!animation_quaternion_slerp(a, b, weight, q))
                        return fail("physics: invalid ragdoll recovery rotation");
                    destination.position = padd(pmul(from.position, 1 - weight), pmul(destination.position, weight));
                    destination.rotation = {q[0], q[1], q[2], q[3]};
                }
                PhysicsCommand command = {};
                command.kind = delta > 0 ? PHYSICS_TARGET : PHYSICS_TELEPORT;
                command.body = binding.body;
                command.pose = destination;
                if (!commands.append(command))
                    return fail("physics: ragdoll target allocation failed");
                const PhysicsCommandKind velocity_kinds[] = {PHYSICS_VELOCITY, PHYSICS_ANGULAR_VELOCITY};
                if (!delta)
                    for (auto kind : velocity_kinds) {
                        command = {};
                        command.kind = kind;
                        command.body = binding.body;
                        if (!commands.append(command))
                            return fail("physics: ragdoll velocity allocation failed");
                    }
            }
            playback.initialized = true;
            playback.elapsed = elapsed;
            if (weight >= 1 && playback.mode == PHYSICS_RAGDOLL_RECOVER)
                playback.mode = PHYSICS_RAGDOLL_ANIMATED;
        }
        if (!next->append(playback))
            return fail("physics: ragdoll playback allocation failed");
    }
    return commands.empty() || scene3d_physics_apply(state, commands.data(), commands.size(), error);
}
bool scene3d_physics_sample(Scene3dPhysics* state, char* diagnostic, size_t capacity) {
    if (!state) {
        return false;
    }
    DomNode* root = dom_node_ref_validate(state->document, state->root);
    auto* animation = state->replay_animation      ? state->replay_animation
                      : root && root->is_element() ? scene3d_animations(root->as_element())
                                                   : nullptr;
    if (!scene3d_physics_roots_admit(state, animation, true, diagnostic, capacity)) {
        return false;
    }
    if (state->ragdolls.empty()) {
        return true;
    }
    bool needed = false;
    for (const auto& rig : state->ragdolls)
        needed |= !rig.initialized;
    if (!needed)
        return true;
    lam::ArrayList<PhysicsRagdollPlayback> next;
    PhysicsError error = {};
    if (!physics_ragdoll_prepare(state, 0, &next, &error)) {
        physics_scene_diagnostic(diagnostic, capacity, error.message ? error.message : "ragdoll initialization failed");
        return false;
    }
    state->ragdolls = static_cast<lam::ArrayList<PhysicsRagdollPlayback>&&>(next);
    physics_read(state->world, &state->published, nullptr);
    state->generation++;
    return true;
}
struct Scene3dReplay {
    Scene3dRuntime* runtime = nullptr;
    Scene3dPhysics* physics = nullptr;
    Scene3dAnimationState* animation = nullptr;
    char diagnostic[256] = {};
    ~Scene3dReplay() {
        scene3d_physics_destroy(physics);
        scene3d_animation_destroy(animation);
        scene3d_runtime_destroy(runtime);
    }
};
static bool scene3d_replay_prepare(Scene3dReplay* replay, DomElement* root) {
    replay->runtime = scene3d_runtime_create(root);
    if (!replay->runtime) {
        return false;
    }
    replay->physics =
        scene3d_physics_create(root, replay->diagnostic, sizeof(replay->diagnostic), false, replay->runtime);
    if (!replay->physics) {
        return false;
    }
    replay->physics->quiet = true;
    replay->animation = scene3d_animation_create(root, replay->diagnostic, sizeof(replay->diagnostic), replay->runtime,
                                                 true, replay->physics);
    if (!replay->animation) {
        return false;
    }
    replay->physics->replay_animation = replay->animation;
    return scene3d_physics_sample(replay->physics, replay->diagnostic, sizeof(replay->diagnostic));
}
bool scene3d_physics_replay_particles(DomElement *root, Scene3dParticles *particles, size_t ticks, double timestep,
                                      char *diagnostic, size_t capacity) {
    Scene3dReplay replay;
    Scene3dRuntimeStats clock = {};
    if (!root || !particles || ticks > 100000 || !scene3d_replay_prepare(&replay, root) ||
        !scene3d_runtime_stats(replay.runtime, &clock) || clock.timestep != timestep ||
        !scene3d_particles_replay_attach(particles, replay.runtime, replay.physics, replay.animation)) {
        physics_scene_diagnostic(diagnostic, capacity,
                                 *replay.diagnostic
                                     ? replay.diagnostic
                                     : "particle world replay: cannot prepare the admitted private timeline");
        return false;
    }
    struct Binding {
        Scene3dParticles *state;
        ~Binding() { scene3d_particles_replay_detach(state); }
    } binding{particles};
    // query each completed private world before its next tick; displayed physics/animation stay untouched.
    if (!scene3d_runtime_step(replay.runtime, ticks)) {
        const char *message = scene3d_particles_diagnostic(particles);
        if (!message)
            message = scene3d_runtime_diagnostic(replay.runtime);
        physics_scene_diagnostic(diagnostic, capacity,
                                 message ? message : "particle world replay: private step failed");
        return false;
    }
    return true;
}
bool scene3d_physics_replay(Scene3dPhysics* state, size_t ticks, Scene3dAnimationState** animation) {
    if (!state || !animation || !*animation || ticks > 100000) {
        return false;
    }
    DomNode* root = dom_node_ref_validate(state->document, state->root);
    if (!root || !root->is_element()) {
        return false;
    }
    Scene3dReplay replay;
    PhysicsError error = {};
    if (!scene3d_replay_prepare(&replay, root->as_element())) {
        return false;
    }
    // empty scenes own no solver; their private animation/particle timeline still replays normally.
    // replay owns a separate mixer/world: a later invalid sample cannot disturb displayed poses or emit cues.
    if (!scene3d_runtime_step(replay.runtime, ticks) ||
        (replay.physics->world && !physics_step(replay.physics->world, 0, &replay.physics->published, &error)) ||
        !scene3d_animation_adopt(replay.animation, *animation, state->runtime)) {
        log_error("scene3d physics replay: %s", *replay.diagnostic ? replay.diagnostic
                                                : error.message    ? error.message
                                                                   : "staged sampling or ownership transfer failed");
        return false;
    }
    *animation = replay.animation;
    replay.animation = nullptr;
    physics_destroy(state->world);
    state->world = replay.physics->world;
    replay.physics->world = nullptr;
    state->world_identity = replay.physics->world_identity;
    state->scene = static_cast<PhysicsScene&&>(replay.physics->scene);
    state->published = replay.physics->published;
    state->ragdolls = static_cast<lam::ArrayList<PhysicsRagdollPlayback>&&>(replay.physics->ragdolls);
    state->roots = static_cast<lam::ArrayList<PhysicsRootMotion>&&>(replay.physics->roots);
    state->root_steps = static_cast<lam::ArrayList<PhysicsRootStep>&&>(replay.physics->root_steps);
    state->event_buffer.clear();
    state->generation++;
    if (state->document->state)
        doc_state_request_repaint(state->document->state);
    return true;
}
bool scene3d_physics_replay_events(Scene3dPhysics* state, size_t from, size_t to, Scene3dAnimationEvents* events) {
    if (!state || !events || !events->values.empty() || from > to || to > 100000) {
        return false;
    }
    DomNode* root = dom_node_ref_validate(state->document, state->root);
    if (!root || !root->is_element()) {
        return false;
    }
    Scene3dReplay replay;
    // prefix reconstruction is silent; collection has a separate bounded budget and never adopts its world.
    if (!scene3d_replay_prepare(&replay, root->as_element()) || !scene3d_runtime_step(replay.runtime, from) ||
        !scene3d_animation_collect(replay.animation, events) || !scene3d_runtime_step(replay.runtime, to - from)) {
        const char* error = *replay.diagnostic ? replay.diagnostic : scene3d_runtime_diagnostic(replay.runtime);
        if (!*events->diagnostic) {
            const char* message = error ? error : "animation cue replay: authored reconstruction failed";
            str_copy(events->diagnostic, sizeof(events->diagnostic), message, strlen(message));
        }
        return false;
    }
    return true;
}
bool scene3d_physics_pose(Scene3dPhysics* state, DomElement* node, Scene3dMatrix* world) {
    if (!state || !state->world)
        return false;
    for (size_t i = 0; i < state->scene.bindings.size(); i++)
        if (state->scene.bindings[i].node == node)
            return physics_pose_matrix(state->published.bodies[i].pose, world);
    for (const auto& binding : state->scene.pose_bindings)
        if (binding.node == node && binding.enabled) {
            // bodies report world poses; the stored bone-to-body frame removes collider/bind offsets once.
            PhysicsPose bone;
            return physics_scene_bound_pose(binding, state->published, &bone) && physics_pose_matrix(bone, world);
        }
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
bool scene3d_physics_apply(Scene3dPhysics* state, const PhysicsCommand* commands, size_t count, PhysicsError* error) {
    if (!state || !state->world)
        return false;
    if (!state->root_committing && commands && count <= 65536) {
        for (size_t i = 0; i < count; i++) {
            if (commands[i].kind == PHYSICS_TARGET && !physics_root_target_allowed(state, commands[i].body, error)) {
                return false;
            }
        }
    }
    bool discontinuity = false;
    if (commands && count <= 65536)
        for (size_t i = 0; i < count; i++)
            discontinuity |= commands[i].kind == PHYSICS_TELEPORT;
    // ordinary controller targets/ticks preserve identity; discontinuous edits invalidate retained routes.
    if (discontinuity && !physics_scene_identity_available(state, error))
        return false;
    if (!physics_apply(state->world, commands, count, error))
        return false;
    if (discontinuity)
        physics_scene_identity_commit(state);
    physics_read(state->world, &state->published, nullptr);
    state->generation++;
    if (!state->quiet && state->document->state)
        doc_state_request_repaint(state->document->state);
    return true;
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
        if (!physics_scene_identity_available(state, &error))
            return physics_scene_error(error.message);
        scene3d_physics_play(state, false);
        PhysicsWorld* replacement = nullptr;
        if (!physics_create(&state->scene.desc, &replacement, &error))
            return physics_scene_error(error.message);
        physics_destroy(state->world);
        state->world = replacement;
        physics_read(state->world, &state->published, &error);
        state->generation++;
        physics_scene_identity_commit(state);
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
