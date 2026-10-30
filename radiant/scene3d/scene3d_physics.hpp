#pragma once
#include "../../lambda/lambda-data.hpp"
#include "../../lib/physics/physics.h"
#include "scene3d_math.hpp"
struct DomElement;
struct DomNode;
struct Scene3dPhysics;
struct Scene3dRuntime;
struct Scene3dAnimationState;
struct Scene3dAnimationEvents;
Scene3dPhysics* scene3d_physics_create(DomElement* root, char* diagnostic, size_t capacity, bool automatic,
                                       Scene3dRuntime* runtime);
void scene3d_physics_destroy(Scene3dPhysics* state);
bool scene3d_physics_play(Scene3dPhysics* state, bool playing);
bool scene3d_physics_playing(Scene3dPhysics* state);
bool scene3d_physics_character_configure(Scene3dPhysics* state, Item request, PhysicsCharacterMove* movement,
                                         PhysicsCharacterProfile* profile, DomElement** body, PhysicsError* error);
bool scene3d_physics_character_profile(Scene3dPhysics* state, uint64_t body, PhysicsCharacterProfile* profile,
                                       PhysicsError* error);
uint64_t scene3d_physics_character_body(Scene3dPhysics* state, DomElement* node);
bool scene3d_physics_character_compute(Scene3dPhysics* state, const PhysicsCharacterMove* movement,
                                       PhysicsCharacterState* result, PhysicsError* error);
bool scene3d_physics_character_move(Scene3dPhysics* state, const PhysicsCharacterMove* movement,
                                    PhysicsCharacterState* result, PhysicsError* error);
bool scene3d_physics_sample(Scene3dPhysics* state, char* diagnostic, size_t capacity);
bool scene3d_physics_apply(Scene3dPhysics* state, const PhysicsCommand* commands, size_t count, PhysicsError* error);
bool scene3d_physics_seek(Scene3dPhysics* state, size_t ticks);
bool scene3d_physics_has_ragdolls(Scene3dPhysics* state);
bool scene3d_physics_has_root_motion(Scene3dPhysics* state);
bool scene3d_physics_root_owns_body(Scene3dPhysics* state, DomElement* node);
bool scene3d_physics_roots_admit(Scene3dPhysics* state, Scene3dAnimationState* animation, bool publish,
                                 char* diagnostic, size_t capacity);
Item scene3d_physics_root_snapshot(Scene3dPhysics* state);
bool scene3d_physics_replay(Scene3dPhysics* state, size_t ticks, Scene3dAnimationState** animation);
bool scene3d_physics_replay_events(Scene3dPhysics* state, size_t from, size_t to, Scene3dAnimationEvents* events);
enum Scene3dPhysicsRefresh { SCENE3D_PHYSICS_KEEP, SCENE3D_PHYSICS_REBUILD, SCENE3D_PHYSICS_REJECT };
Scene3dPhysicsRefresh scene3d_physics_refresh(Scene3dPhysics* state, DomElement* root, char* diagnostic,
                                              size_t capacity);
void scene3d_physics_follow_rebuild(Scene3dPhysics* state, DomNode* old_root, DomNode* new_root);
uint64_t scene3d_physics_generation(Scene3dPhysics* state);
uint64_t scene3d_physics_world_identity(Scene3dPhysics* state);
bool scene3d_physics_pose(Scene3dPhysics* state, DomElement* node, Scene3dMatrix* world);
Item scene3d_physics_operate(Scene3dPhysics* state, Item request);
bool scene3d_physics_advance(Scene3dPhysics* state, size_t ticks, PhysicsState* result, PhysicsError* error);
