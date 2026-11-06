#pragma once

struct DomElement;
struct PhysicsScene;
struct PhysicsEvent;
bool lambda_scene3d_physics_event(DomElement* target, const PhysicsScene* scene,
                                  const PhysicsEvent* event, const char* type);
