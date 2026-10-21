#include <gtest/gtest.h>
#include "../lib/physics/physics.h"
#include "../lib/physics/physics_math.h"

struct PhysicsTest : testing::Test {
    PhysicsBodyDesc bodies[4]={};PhysicsColliderDesc colliders[4]={};PhysicsDesc desc={};
    PhysicsWorld* world=nullptr;PhysicsError error={};PhysicsState state={};
    void SetUp() override {
        desc.gravity={0,-9.81,0};desc.timestep=1.0/120;desc.length_scale=1;desc.iterations=16;desc.contact_limit=256;desc.event_limit=256;
        desc.bodies=bodies;desc.colliders=colliders;desc.body_count=desc.collider_count=2;
        for(unsigned i=0;i<4;i++) {
            bodies[i].id=i+1;bodies[i].pose.rotation.w=1;bodies[i].gravity_scale=1;bodies[i].motion=i?PHYSICS_DYNAMIC:PHYSICS_FIXED;
            colliders[i].id=10+i;colliders[i].body=i+1;colliders[i].pose.rotation.w=1;colliders[i].shape=PHYSICS_BOX;
            colliders[i].size={1,1,1};colliders[i].radius=.5;colliders[i].density=1;colliders[i].friction=.5;colliders[i].membership=colliders[i].filter=UINT32_MAX;
        }
        bodies[0].pose.position={0,-.5,0};colliders[0].size={20,1,20};bodies[1].pose.position={0,3,0};
    }
    void TearDown() override {physics_destroy(world);}
    void create() {ASSERT_TRUE(physics_create(&desc,&world,&error))<<(error.message?error.message:"");}
    void step(size_t count) {ASSERT_TRUE(physics_step(world,count,&state,&error))<<(error.message?error.message:"");}
};
TEST_F(PhysicsTest, FreeFallAndMass) {
    desc.body_count=desc.collider_count=1;bodies[0].motion=PHYSICS_DYNAMIC;bodies[0].pose.position={0,10,0};colliders[0].size={2,3,4};colliders[0].density=5;
    create();step(120);ASSERT_EQ(state.body_count,1u);EXPECT_DOUBLE_EQ(state.bodies[0].mass,120);
    EXPECT_NEAR(state.bodies[0].velocity.y,-9.81,1e-12);EXPECT_NEAR(state.bodies[0].pose.position.y,10-9.81*121/240,1e-10);
}
TEST_F(PhysicsTest, BoxRestingContactAndSleep) {
    create();for(unsigned i=0;i<1200;i++)step(1);
    EXPECT_NEAR(state.bodies[1].pose.position.y,.5,.01);EXPECT_NEAR(plen(state.bodies[1].velocity),0,.01);
    EXPECT_TRUE(state.bodies[1].sleeping);EXPECT_NEAR(state.bodies[1].pose.rotation.w,1,.01);
}
TEST_F(PhysicsTest, SphereRestingContact) {
    colliders[1].shape=PHYSICS_SPHERE;create();for(unsigned i=0;i<1200;i++)step(1);
    EXPECT_NEAR(state.bodies[1].pose.position.y,.5,.01);EXPECT_NEAR(state.bodies[1].velocity.y,0,.01);
}
TEST_F(PhysicsTest, OffsetCompoundMassAndAngularImpulse) {
    desc.gravity={};desc.body_count=1;desc.collider_count=2;bodies[0].motion=PHYSICS_DYNAMIC;
    colliders[0].size={1,1,1};
    colliders[0].body=colliders[1].body=1;colliders[0].pose.position={-1,0,0};colliders[1].pose.position={1,0,0};
    create();PhysicsCommand c={};c.kind=PHYSICS_IMPULSE;c.body=1;c.value={0,2,0};c.point={1,0,0};c.at_point=true;
    ASSERT_TRUE(physics_apply(world,&c,1,&error));ASSERT_TRUE(physics_read(world,&state,&error));
    EXPECT_DOUBLE_EQ(state.bodies[0].mass,2);EXPECT_NEAR(state.bodies[0].centre_of_mass.x,0,1e-12);
    EXPECT_DOUBLE_EQ(state.bodies[0].velocity.y,1);EXPECT_NEAR(state.bodies[0].angular_velocity.z,2.0/(2.0+1.0/3.0),1e-12);
}
TEST_F(PhysicsTest, SensorEventsAndTeleportQuery) {
    desc.gravity={};colliders[0].sensor=true;bodies[1].pose.position={0,0,0};create();step(1);
    ASSERT_EQ(state.event_count,1u);EXPECT_TRUE(state.events[0].started);EXPECT_TRUE(state.events[0].sensor);
    PhysicsCommand c={};c.kind=PHYSICS_TELEPORT;c.body=2;c.pose={{30,0,0},{0,0,0,1}};ASSERT_TRUE(physics_apply(world,&c,1,&error));
    PhysicsQuery q={};q.kind=PHYSICS_RAY;q.origin={30,10,0};q.direction={0,-1,0};q.distance=20;q.membership=q.filter=UINT32_MAX;PhysicsHit hit={};
    ASSERT_TRUE(physics_query(world,&q,1,&hit,&error));ASSERT_TRUE(hit.found);EXPECT_EQ(hit.body,2u);EXPECT_NEAR(hit.distance,9.5,1e-12);
    step(1);ASSERT_EQ(state.event_count,1u);EXPECT_FALSE(state.events[0].started);
}
TEST_F(PhysicsTest, InvalidBatchDoesNotApplyPrefix) {
    create();PhysicsCommand commands[2]={};commands[0].kind=PHYSICS_IMPULSE;commands[0].body=2;commands[0].value={1,0,0};commands[1].body=999;
    EXPECT_FALSE(physics_apply(world,commands,2,&error));physics_read(world,&state,&error);EXPECT_DOUBLE_EQ(state.bodies[1].velocity.x,0);
}
TEST_F(PhysicsTest, RotatedBoxesDoNotUseAabbAsContacts) {
    desc.gravity={};bodies[0].pose.position={0,0,0};bodies[0].pose.rotation=peuler({0,0,acos(-1.0)/4});colliders[0].size={4,.1,.1};
    bodies[1].pose.position={0,1,0};bodies[1].pose.rotation=bodies[0].pose.rotation;colliders[1].size={4,.1,.1};
    create();step(1);EXPECT_EQ(state.event_count,0u);EXPECT_EQ(state.contact_count,0u);
}
TEST_F(PhysicsTest, ForceActsOnceAcrossMultipleTicks) {
    desc.gravity={};create();PhysicsCommand c={};c.kind=PHYSICS_FORCE;c.body=2;c.value={120,0,0};
    ASSERT_TRUE(physics_apply(world,&c,1,&error));step(10);EXPECT_NEAR(state.bodies[1].velocity.x,1,1e-12);
}
TEST_F(PhysicsTest, IndependentWorldsAndDeterministicReplay) {
    create();PhysicsWorld* other=nullptr;ASSERT_TRUE(physics_create(&desc,&other,&error));PhysicsState second={};
    for(unsigned i=0;i<300;i++){step(1);ASSERT_TRUE(physics_step(other,1,&second,&error));
        EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.y,second.bodies[1].pose.position.y);}
    physics_destroy(other);
}
TEST_F(PhysicsTest, ElasticSpheresConserveMomentumAndReverseVelocities) {
    desc.gravity={};bodies[0].motion=PHYSICS_DYNAMIC;bodies[0].pose.position={-1,0,0};bodies[1].pose.position={1,0,0};
    bodies[0].velocity={1,0,0};bodies[1].velocity={-1,0,0};
    colliders[0].shape=colliders[1].shape=PHYSICS_SPHERE;colliders[0].restitution=colliders[1].restitution=1;
    create();step(200);EXPECT_NEAR(state.bodies[0].velocity.x,-1,1e-12);EXPECT_NEAR(state.bodies[1].velocity.x,1,1e-12);
    EXPECT_NEAR(state.bodies[0].mass*state.bodies[0].velocity.x+state.bodies[1].mass*state.bodies[1].velocity.x,0,1e-12);
}
TEST_F(PhysicsTest, EventOverflowFaultsWorldAndPreservesCompletedTick) {
    desc.gravity={};desc.body_count=desc.collider_count=4;desc.event_limit=1;
    for(unsigned i=0;i<4;i++){bodies[i].pose.position={0,0,0};colliders[i].sensor=true;}
    create();EXPECT_FALSE(physics_step(world,1,&state,&error));EXPECT_EQ(error.tick,0u);
    EXPECT_FALSE(physics_step(world,1,&state,&error));EXPECT_TRUE(physics_read(world,&state,&error));EXPECT_EQ(state.tick,0u);
    EXPECT_EQ(state.event_count,0u);EXPECT_DOUBLE_EQ(state.bodies[1].velocity.y,0);
}

TEST_F(PhysicsTest, RejectsOverflowingColliderBounds) {
    desc.body_count=desc.collider_count=1;
    bodies[0].pose.position={1.5e308,0,0};colliders[0].pose.position={1.5e308,0,0};
    EXPECT_FALSE(physics_create(&desc,&world,&error));EXPECT_EQ(world,nullptr);
}
TEST_F(PhysicsTest, NumericalFaultRetainsCompletedSnapshot) {
    desc.body_count=desc.collider_count=1;desc.gravity={};
    bodies[0].motion=PHYSICS_DYNAMIC;bodies[0].pose.position={1e308,0,0};bodies[0].velocity={1e308,0,0};desc.timestep=2;
    colliders[0].size={1,1,1};create();
    EXPECT_FALSE(physics_step(world,1,&state,&error));ASSERT_TRUE(physics_read(world,&state,&error));
    EXPECT_EQ(state.tick,0u);EXPECT_DOUBLE_EQ(state.bodies[0].pose.position.x,1e308);
}
TEST_F(PhysicsTest, KinematicTargetAndCollisionMasks) {
    desc.gravity={};bodies[0].motion=PHYSICS_KINEMATIC_POSITION;bodies[0].pose.position={0,0,0};
    bodies[1].pose.position={0,0,0};colliders[0].membership=1;colliders[0].filter=1;colliders[1].membership=2;colliders[1].filter=2;
    create();PhysicsCommand c={};c.kind=PHYSICS_TARGET;c.body=1;c.pose={{2,0,0},{0,0,0,1}};
    ASSERT_TRUE(physics_apply(world,&c,1,&error));step(1);
    EXPECT_DOUBLE_EQ(state.bodies[0].pose.position.x,2);EXPECT_EQ(state.event_count,0u);
    step(1);EXPECT_DOUBLE_EQ(state.bodies[0].velocity.x,0);
}
TEST_F(PhysicsTest, GravityChangeWakesSleepingBodies) {
    desc.body_count=desc.collider_count=1;desc.gravity={};bodies[0].motion=PHYSICS_DYNAMIC;
    colliders[0].size={1,1,1};create();step(120);ASSERT_TRUE(state.bodies[0].sleeping);
    PhysicsCommand c={};c.kind=PHYSICS_GRAVITY;c.value={0,-1,0};
    ASSERT_TRUE(physics_apply(world,&c,1,&error));step(1);
    EXPECT_FALSE(state.bodies[0].sleeping);EXPECT_LT(state.bodies[0].velocity.y,0);
}
TEST_F(PhysicsTest, CommandOverflowCannotPublishInfiniteVelocity) {
    create();PhysicsCommand commands[2]={};
    for(auto& command:commands){command.kind=PHYSICS_IMPULSE;command.body=2;command.value={1e308,0,0};}
    EXPECT_FALSE(physics_apply(world,commands,2,&error));ASSERT_TRUE(physics_read(world,&state,&error));
    EXPECT_DOUBLE_EQ(state.bodies[1].velocity.x,0);EXPECT_EQ(state.tick,0u);
    EXPECT_FALSE(physics_step(world,1,&state,&error));
}
TEST_F(PhysicsTest, ConfigureKeepsSimulatedStateAndRecomputesMass) {
    create();step(30);PhysicsVec position=state.bodies[1].pose.position,velocity=state.bodies[1].velocity;
    colliders[1].density=3;colliders[1].friction=.9;bodies[1].linear_damping=.5;
    ASSERT_TRUE(physics_configure(world,bodies,2,colliders,2,&error))<<(error.message?error.message:"");
    ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_EQ(state.tick,30u);EXPECT_DOUBLE_EQ(state.bodies[1].mass,3);
    EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.y,position.y);EXPECT_DOUBLE_EQ(state.bodies[1].velocity.y,velocity.y);
    // rejected edits, at validation or at mass recomputation, keep the previous configuration
    bodies[1].motion=PHYSICS_FIXED;EXPECT_FALSE(physics_configure(world,bodies,2,colliders,2,&error));
    bodies[1].motion=PHYSICS_DYNAMIC;colliders[1].density=0;EXPECT_FALSE(physics_configure(world,bodies,2,colliders,2,&error));
    ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_DOUBLE_EQ(state.bodies[1].mass,3);step(1);
}
TEST_F(PhysicsTest, AngularVelocityCommandSetsSpin) {
    desc.gravity={};create();PhysicsCommand c={};c.kind=PHYSICS_ANGULAR_VELOCITY;c.body=2;c.value={0,1,0};
    ASSERT_TRUE(physics_apply(world,&c,1,&error));ASSERT_TRUE(physics_read(world,&state,&error));
    EXPECT_DOUBLE_EQ(state.bodies[1].angular_velocity.y,1);
}
