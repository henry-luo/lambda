#pragma once
#include "../core/physics_scene.hpp"
Item physics_state_value(const PhysicsScene& scene,const PhysicsState& state,Item selection);
Item physics_events_value(const PhysicsScene& scene,const PhysicsState& state);
Item physics_event_value(const PhysicsScene& scene,const PhysicsEvent& event);
bool physics_commands_read(const PhysicsScene& scene,Item commands,lam::ArrayList<PhysicsCommand>* result,const char** error);
Item physics_queries_value(const PhysicsScene& scene,PhysicsWorld* world,Item queries);
