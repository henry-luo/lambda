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
struct PhysicsPoseBinding {
    void* node;
    uint64_t identity,body;
    size_t body_index;
    PhysicsPose body_in_bone;
    bool enabled;
};
struct PhysicsGeometryOffsets {size_t vertices,indices,heights;};
enum PhysicsRagdollMode {PHYSICS_RAGDOLL_ANIMATED,PHYSICS_RAGDOLL_PHYSICAL,PHYSICS_RAGDOLL_RECOVER};
struct PhysicsRagdollBone {uint64_t identity;size_t binding;};
struct PhysicsRagdollDesc {
    void* node;
    uint64_t identity;
    PhysicsRagdollMode mode;
    double recovery_duration;
    size_t count;
    PhysicsRagdollBone bones[64];
};
struct PhysicsScene {
    PhysicsDesc desc={};
    lam::ArrayList<PhysicsBodyDesc> bodies;
    lam::ArrayList<PhysicsColliderDesc> colliders;
    lam::ArrayList<PhysicsJointDesc> joints;
    lam::ArrayList<bool> joint_inferred_lengths;
    lam::ArrayList<PhysicsBinding> bindings,ids;
    lam::ArrayList<PhysicsPoseBinding> pose_bindings;
    lam::ArrayList<PhysicsRagdollDesc> ragdolls;
    lam::ArrayList<PhysicsVec> vertices;
    lam::ArrayList<uint32_t> indices;
    lam::ArrayList<double> heights;
    lam::ArrayList<PhysicsGeometryOffsets> geometry_offsets;
    bool playing=false;
    unsigned max_substeps=8;
    char diagnostic[256]={};
};
bool physics_scene_compile(PhysicsScene* scene,const PhysicsSceneSource& source,void* root);
PhysicsSceneSource physics_mark_source();
const char* physics_scene_id(const PhysicsScene& scene,uint64_t identity);
bool physics_scene_real(ItemReader value,double fallback,double* result);
bool physics_scene_vector(ItemReader value,PhysicsVec fallback,PhysicsVec* result);
bool physics_scene_colliders_equal(const PhysicsScene& a,const PhysicsScene& b);
bool physics_scene_bound_pose(const PhysicsPoseBinding& binding,const PhysicsState& state,PhysicsPose* pose);
