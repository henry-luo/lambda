#pragma once
#include "../core/physics_scene.hpp"
Item physics_state_value(const PhysicsScene& scene,const PhysicsState& state,Item selection,Item ragdolls=ItemNull);
Item physics_events_value(const PhysicsScene& scene,const PhysicsState& state);
Item physics_event_value(const PhysicsScene& scene,const PhysicsEvent& event);
bool physics_commands_read(const PhysicsScene& scene,Item commands,lam::ArrayList<PhysicsCommand>* result,const char** error);
Item physics_queries_value(const PhysicsScene& scene,PhysicsWorld* world,Item queries);
Item physics_move_value(const PhysicsScene& scene,PhysicsWorld* world,Item request);
bool physics_character_read(const PhysicsScene& scene,ItemReader request,PhysicsCharacterMove* movement);
Item physics_character_value(const PhysicsScene& scene, const PhysicsCharacterState& state);
