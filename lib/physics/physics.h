#ifndef LAMBDA_PHYSICS_H
#define LAMBDA_PHYSICS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct PhysicsWorld PhysicsWorld;
typedef struct PhysicsVec { double x, y, z; } PhysicsVec;
typedef struct PhysicsQuat { double x, y, z, w; } PhysicsQuat;
typedef struct PhysicsPose { PhysicsVec position; PhysicsQuat rotation; } PhysicsPose;
typedef enum PhysicsMotion { PHYSICS_FIXED, PHYSICS_DYNAMIC, PHYSICS_KINEMATIC_POSITION, PHYSICS_KINEMATIC_VELOCITY } PhysicsMotion;
typedef enum PhysicsShape { PHYSICS_SPHERE, PHYSICS_BOX, PHYSICS_CAPSULE, PHYSICS_CONVEX, PHYSICS_MESH, PHYSICS_HEIGHTFIELD } PhysicsShape;
#define PHYSICS_CONVEX_MAX_VERTICES 64
#define PHYSICS_WORLD_MAX_VERTICES 65536
#define PHYSICS_MESH_MAX_TRIANGLES 131072
#define PHYSICS_WORLD_MAX_TRIANGLES 262144
#define PHYSICS_MESH_QUERY_BUDGET 8192
#define PHYSICS_CAST_MAX_ITERATIONS 128
#define PHYSICS_CCD_MAX_IMPACTS 32
#define PHYSICS_CCD_PAIR_BUDGET 65536
typedef struct PhysicsBodyDesc {
    uint64_t id;
    PhysicsMotion motion;
    PhysicsPose pose;
    PhysicsVec velocity, angular_velocity;
    double linear_damping, angular_damping, gravity_scale;
    bool ccd;
} PhysicsBodyDesc;
typedef struct PhysicsColliderDesc {
    uint64_t id, body;
    PhysicsShape shape;
    PhysicsPose pose;
    PhysicsVec size;
    // capsules extend along local Y; height is the cylinder length, excluding the hemispheres.
    double radius, height, density, friction, restitution;
    uint32_t membership, filter;
    bool sensor;
    // convex point clouds are copied and cooked by creation/configuration.
    const PhysicsVec* vertices;
    size_t vertex_count;
    // environment surfaces own copied data; indices are zero-based CCW triples.
    const uint32_t* indices;
    size_t index_count;
    const double* heights;
    size_t rows, columns;
    double cell_width, cell_depth;
    bool double_sided;
} PhysicsColliderDesc;
typedef enum PhysicsJointKind {
    PHYSICS_JOINT_DISTANCE, PHYSICS_JOINT_SPHERICAL, PHYSICS_JOINT_FIXED,
    PHYSICS_JOINT_HINGE, PHYSICS_JOINT_PRISMATIC, PHYSICS_JOINT_CONE_TWIST
} PhysicsJointKind;
#define PHYSICS_WORLD_MAX_JOINTS 16384
typedef struct PhysicsJointDesc {
    uint64_t id, body_a, body_b;
    PhysicsJointKind kind;
    // attachment frames are relative to body origins; local X is the hinge/slider axis.
    PhysicsPose frame_a, frame_b;
    double length, stiffness, damping;
    double lower_limit, upper_limit, cone_angle;
    double motor_speed, motor_max_force, break_force, break_torque;
    bool disabled, collide_connected, spring, limit_enabled, motor_enabled;
} PhysicsJointDesc;
typedef struct PhysicsDesc {
    PhysicsVec gravity;
    double timestep, length_scale;
    unsigned iterations;
    size_t contact_limit, event_limit;
    const PhysicsBodyDesc* bodies;
    size_t body_count;
    const PhysicsColliderDesc* colliders;
    size_t collider_count;
    const PhysicsJointDesc* joints;
    size_t joint_count;
} PhysicsDesc;
typedef struct PhysicsError { const char* message; uint64_t id, tick; } PhysicsError;
typedef struct PhysicsBodyState {
    uint64_t id;
    PhysicsPose pose;
    PhysicsVec velocity, angular_velocity, centre_of_mass;
    double mass;
    bool sleeping;
    // planned position-kinematic poses let dependent controllers sample the same upcoming tick.
    PhysicsMotion motion;
    PhysicsPose target;
    bool target_set;
} PhysicsBodyState;
typedef struct PhysicsEvent {
    uint64_t tick, collider_a, collider_b, body_a, body_b;
    bool started, sensor;
    // joint is zero for contact/sensor events; break events copy the completed tick's wrench.
    uint64_t joint;
    PhysicsVec force, torque;
} PhysicsEvent;
typedef struct PhysicsJointState {
    uint64_t id, body_a, body_b;
    PhysicsVec anchor_a, anchor_b, force, torque;
    double coordinate, linear_error, angular_error;
    bool enabled, broken, coordinate_valid;
} PhysicsJointState;
typedef struct PhysicsState {
    uint64_t tick;
    double time;
    const PhysicsBodyState* bodies;
    size_t body_count;
    const PhysicsEvent* events;
    size_t event_count, candidate_count, contact_count;
    const PhysicsJointState* joints;
    size_t joint_count;
} PhysicsState;
typedef enum PhysicsCommandKind {
    PHYSICS_IMPULSE, PHYSICS_FORCE, PHYSICS_TORQUE, PHYSICS_TELEPORT,
    PHYSICS_VELOCITY, PHYSICS_WAKE, PHYSICS_TARGET, PHYSICS_GRAVITY, PHYSICS_ANGULAR_VELOCITY
} PhysicsCommandKind;
typedef struct PhysicsCommand {
    PhysicsCommandKind kind;
    uint64_t body;
    PhysicsVec value, point;
    PhysicsPose pose;
    bool at_point, local;
} PhysicsCommand;
typedef enum PhysicsQueryKind { PHYSICS_RAY, PHYSICS_POINT, PHYSICS_OVERLAP, PHYSICS_DISTANCE, PHYSICS_SHAPE_OVERLAP, PHYSICS_SHAPE_CAST } PhysicsQueryKind;
typedef struct PhysicsQuery {
    PhysicsQueryKind kind;
    PhysicsVec origin, direction, size;
    double distance;
    uint32_t membership, filter;
    bool include_sensors;
    uint64_t collider_a, collider_b;
    // zero selects the provider default; source-shape queries use origin as a translation offset.
    unsigned cast_iterations;
} PhysicsQuery;
typedef struct PhysicsHit {
    bool found;
    uint64_t collider, body;
    double distance;
    PhysicsVec point, normal;
    PhysicsVec point_b;
    bool overlapping;
    // mesh diagnostics count tested triangles, including pruned-query misses.
    size_t triangle_tests;
    double fraction;
    unsigned iterations;
} PhysicsHit;

#define PHYSICS_CHARACTER_MAX_CONTACTS 32
typedef struct PhysicsCharacterMove {
    uint64_t body;
    PhysicsVec displacement, up;
    double skin, slope_angle, step_height, snap_distance;
    unsigned max_slides, recovery_iterations, cast_iterations, geometry_budget;
    // a previous support snapshot supplies already-achieved platform motion in this sampled world.
    uint64_t platform;
    PhysicsPose platform_pose;
} PhysicsCharacterMove;
typedef struct PhysicsCharacterContact {
    uint64_t body, collider;
    PhysicsVec point, normal;
} PhysicsCharacterContact;
typedef struct PhysicsCharacterState {
    PhysicsPose pose, platform_pose;
    PhysicsVec requested, achieved, recovery, platform_motion, ground_normal, platform_velocity;
    uint64_t platform, ground_body;
    PhysicsCharacterContact contacts[PHYSICS_CHARACTER_MAX_CONTACTS];
    size_t contact_count, geometry_tests, triangle_tests;
    bool grounded, wall, ceiling, stepped;
} PhysicsCharacterState;

PhysicsCharacterMove physics_character_defaults(double length_scale);
// computes a kinematic capsule target against the sampled world; no body/tick/event mutation.
// the caller applies the returned pose with the ordinary target/teleport command.
// failure preserves both the caller's result and the published world views.
bool physics_character_move(PhysicsWorld* world, const PhysicsCharacterMove* move,
    PhysicsCharacterState* result, PhysicsError* error);

// creation copies descriptors; all later calls require exclusive ownership of the world.
// result views belong to the world; callers copy anything retained across mutation.
bool physics_create(const PhysicsDesc* desc, PhysicsWorld** result, PhysicsError* error);
void physics_destroy(PhysicsWorld* world);
// invalid commands reject the entire batch; forces/torques last for the next tick only.
bool physics_apply(PhysicsWorld* world, const PhysicsCommand* commands, size_t count, PhysicsError* error);
// replaces body damping/gravity scale and every collider descriptor, in creation order, keeping
// each body's simulated pose and velocities; ids, motion types and collider owners must not
// change. Mass properties recompute; a rejected edit leaves the previous configuration.
bool physics_configure(PhysicsWorld* world, const PhysicsBodyDesc* bodies, size_t body_count,
                       const PhysicsColliderDesc* colliders, size_t collider_count, PhysicsError* error);
// full descriptions also change body motion roles atomically with geometry/joints and retain achieved poses.
// role changes discard old targets/effort; fixed roles stop, other roles retain measured velocity.
// unchanged joint caches survive when their body responses remain the same.
bool physics_reconfigure(PhysicsWorld* world, const PhysicsDesc* desc, PhysicsError* error);
// authored descriptor and pose/velocity commands commit together, including their snapshot storage.
bool physics_reconfigure_commands(PhysicsWorld* world, const PhysicsDesc* desc,
    const PhysicsCommand* commands, size_t count, PhysicsError* error);
// compares validated descriptors by values, independently of borrowed buffer addresses.
bool physics_collider_equal(const PhysicsColliderDesc* a, const PhysicsColliderDesc* b);
bool physics_joint_equal(const PhysicsJointDesc* a, const PhysicsJointDesc* b);
// events accumulate across this call's ticks. Numerical/quota faults disable further mutation.
bool physics_step(PhysicsWorld* world, size_t ticks, PhysicsState* result, PhysicsError* error);
// faulted worlds remain readable with the last completed body snapshot.
bool physics_read(PhysicsWorld* world, PhysicsState* result, PhysicsError* error);
// one nearest/first hit per query; PHYSICS_OVERLAP tests AABBs, not exact shape overlap.
bool physics_query(PhysicsWorld* world, const PhysicsQuery* queries, size_t count, PhysicsHit* results, PhysicsError* error);
// one nearest hit per eligible collider, sorted by distance then ID; capacity exhaustion is an error.
bool physics_query_hits(PhysicsWorld* world, const PhysicsQuery* query, PhysicsHit* results, size_t capacity, size_t* count, PhysicsError* error);

#ifdef __cplusplus
}
#endif
#endif
