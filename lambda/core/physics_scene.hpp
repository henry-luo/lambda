#pragma once
#include "mark_reader.hpp"
#include "../../lib/arraylist.hpp"
#include "../../lib/physics/physics.h"

// a read-only source cursor lets Mark and DOM scenes share the same physical compiler.
struct PhysicsSceneSource {
    const char* (*tag)(void* node);
    ItemReader (*field)(void* node,const char* key);
    size_t (*count)(void* node);
    void* (*child)(void* node,size_t index);
    const char* (*text)(void* node,const char* key);
};
struct PhysicsBinding { char id[128];void* node;uint64_t identity; };
struct PhysicsScene {
    PhysicsDesc desc={};
    lam::ArrayList<PhysicsBodyDesc> bodies;
    lam::ArrayList<PhysicsColliderDesc> colliders;
    lam::ArrayList<PhysicsBinding> bindings,ids;
    bool playing=false;
    unsigned max_substeps=8;
    char diagnostic[256]={};
};
bool physics_scene_compile(PhysicsScene* scene,const PhysicsSceneSource& source,void* root);
PhysicsSceneSource physics_mark_source();
const char* physics_scene_id(const PhysicsScene& scene,uint64_t identity);
bool physics_scene_real(ItemReader value,double fallback,double* result);
bool physics_scene_vector(ItemReader value,PhysicsVec fallback,PhysicsVec* result);
