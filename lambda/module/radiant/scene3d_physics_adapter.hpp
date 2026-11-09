#pragma once

#include "../../core/data.hpp"
#include "lib/physics/physics.h"

struct DomElement;
struct Scene3dPhysics;
bool scene3d_physics_character_configure(Scene3dPhysics* state, Item request, PhysicsCharacterMove* movement,
                                         PhysicsCharacterProfile* profile, DomElement** body, PhysicsError* error);
Item scene3d_physics_root_snapshot(Scene3dPhysics* state);
Item scene3d_physics_operate(Scene3dPhysics* state, Item request);
