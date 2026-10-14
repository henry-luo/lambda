#pragma once
#include "scene3d_math.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lib/physics/physics.h"
struct DomElement;
struct Scene3dPhysics;
Scene3dPhysics* scene3d_physics_create(DomElement* root,char* diagnostic,size_t capacity,bool automatic);
void scene3d_physics_destroy(Scene3dPhysics* state);
bool scene3d_physics_matches(Scene3dPhysics* state,DomElement* root);
uint64_t scene3d_physics_generation(Scene3dPhysics* state);
bool scene3d_physics_pose(Scene3dPhysics* state,DomElement* node,Scene3dMatrix* world);
Item scene3d_physics_operate(Scene3dPhysics* state,Item request);
bool scene3d_physics_advance(Scene3dPhysics* state,size_t ticks,PhysicsState* result,PhysicsError* error);
