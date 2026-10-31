#include <gtest/gtest.h>
#include "../lib/physics/physics.h"
#include "../lib/physics/physics_math.h"
#include "../lib/physics/physics_geometry.h"
#include "../lib/arraylist.hpp"

struct PhysicsTest : testing::Test {
    PhysicsBodyDesc bodies[4]={};PhysicsColliderDesc colliders[4]={};PhysicsDesc desc={};
    PhysicsWorld* world=nullptr;PhysicsError error={};PhysicsState state={};
    PhysicsVec hull_points[2][PHYSICS_CONVEX_MAX_VERTICES]={};
    void hull(unsigned collider,PhysicsVec half={.5,.5,.5},PhysicsVec centre={}) {
        for(unsigned i=0;i<8;i++)hull_points[collider][i]=padd(centre,{(i&1?1:-1)*half.x,(i&2?1:-1)*half.y,(i&4?1:-1)*half.z});
        colliders[collider].shape=PHYSICS_CONVEX;colliders[collider].vertices=hull_points[collider];colliders[collider].vertex_count=8;
    }
    PhysicsHit distance(uint64_t a=10,uint64_t b=11) {
        PhysicsQuery q={};q.kind=PHYSICS_DISTANCE;q.collider_a=a;q.collider_b=b;PhysicsHit hit={};
        EXPECT_TRUE(physics_query(world,&q,1,&hit,&error))<<(error.message?error.message:"");return hit;
    }
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

TEST_F(PhysicsTest, CapsuleMassAndCompoundInertiaMatchCylinderAndHemispheres) {
    desc.body_count=desc.collider_count=1;desc.gravity={};bodies[0].motion=PHYSICS_DYNAMIC;bodies[0].pose.position={};
    auto& c=colliders[0];c.shape=PHYSICS_CAPSULE;c.radius=.5;c.height=2;c.density=3;c.pose.position={1.25,0,0};
    create();ASSERT_TRUE(physics_read(world,&state,&error));
    const double sphere=acos(-1.0)/2,cylinder=3*acos(-1.0)/2;
    const double transverse=cylinder*4.75/12+sphere*(.1+.375+1),axial=(.4*sphere+.5*cylinder)*.25;
    EXPECT_NEAR(state.bodies[0].mass,sphere+cylinder,1e-12);EXPECT_DOUBLE_EQ(state.bodies[0].centre_of_mass.x,1.25);
    PhysicsCommand impulse={};impulse.kind=PHYSICS_IMPULSE;impulse.body=1;impulse.at_point=true;
    impulse.point={2.25,1,0};impulse.value={0,0,1};ASSERT_TRUE(physics_apply(world,&impulse,1,&error));
    ASSERT_TRUE(physics_read(world,&state,&error));
    EXPECT_NEAR(state.bodies[0].angular_velocity.x,1/transverse,1e-12);
    EXPECT_NEAR(state.bodies[0].angular_velocity.y,-1/axial,1e-12);
    EXPECT_NEAR(state.bodies[0].velocity.z,1/(sphere+cylinder),1e-12);
    c.height=0;ASSERT_TRUE(physics_configure(world,bodies,1,colliders,1,&error));
    ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_NEAR(state.bodies[0].mass,sphere,1e-12);
}

TEST_F(PhysicsTest, VerticalAndHorizontalCapsulesSettleOnBoxFloor) {
    colliders[1].shape=PHYSICS_CAPSULE;colliders[1].radius=.5;colliders[1].height=2;
    for(unsigned orientation=0;orientation<2;orientation++) {
        bodies[1].pose.rotation=peuler({0,0,orientation?acos(-1.0)/2:0});create();
        for(unsigned tick=0;tick<1200;tick++)step(1);
        EXPECT_NEAR(state.bodies[1].pose.position.y,orientation?.5:1.5,.01);
        EXPECT_NEAR(plen(state.bodies[1].velocity),0,.01);EXPECT_TRUE(state.bodies[1].sleeping);
        EXPECT_NEAR(plen(state.bodies[1].angular_velocity),0,.01);
        physics_destroy(world);world=nullptr;
    }
}

TEST_F(PhysicsTest, CapsuleSensorsUseSegmentDistanceForSphereAndCrossedCapsules) {
    desc.gravity={};colliders[0].shape=PHYSICS_CAPSULE;colliders[0].height=2;colliders[0].radius=.25;
    colliders[0].sensor=true;bodies[0].pose.position={};
    for(unsigned shape=0;shape<2;shape++) {
        colliders[1].shape=shape?PHYSICS_CAPSULE:PHYSICS_SPHERE;colliders[1].height=2;colliders[1].radius=.25;
        bodies[1].pose.position=shape?PhysicsVec{0,0,.4}:PhysicsVec{0,1.4,0};
        bodies[1].pose.rotation=peuler({0,0,acos(-1.0)/2});create();step(1);
        ASSERT_EQ(state.event_count,1u);EXPECT_TRUE(state.events[0].sensor);EXPECT_TRUE(state.events[0].started);
        EXPECT_EQ(state.contact_count,0u);EXPECT_NEAR(plen(state.bodies[1].velocity),0,1e-12);
        PhysicsCommand command={};command.kind=PHYSICS_TELEPORT;command.body=2;
        command.pose={{0,3,3},{0,0,0,1}};ASSERT_TRUE(physics_apply(world,&command,1,&error));step(1);
        ASSERT_EQ(state.event_count,1u);EXPECT_FALSE(state.events[0].started);
        physics_destroy(world);world=nullptr;
    }
}

TEST_F(PhysicsTest, CapsuleBoxDistanceFindsSegmentInteriorAndRejectsBroadPhaseCorners) {
    desc.gravity={};bodies[0].pose.position={};colliders[0].size={1,1,1};colliders[0].sensor=true;
    colliders[1].shape=PHYSICS_CAPSULE;colliders[1].height=4;colliders[1].radius=.2;
    bodies[1].pose.rotation=peuler({0,0,acos(-1.0)/2});bodies[1].pose.position={0,.6,0};
    create();step(1);ASSERT_EQ(state.event_count,1u);EXPECT_TRUE(state.events[0].started);
    PhysicsCommand command={};command.kind=PHYSICS_TELEPORT;command.body=2;
    command.pose={{0,.69,.69},bodies[1].pose.rotation};ASSERT_TRUE(physics_apply(world,&command,1,&error));step(1);
    ASSERT_EQ(state.event_count,1u);EXPECT_FALSE(state.events[0].started);EXPECT_GT(state.candidate_count,0u);
    command.pose.position={0,0,0};ASSERT_TRUE(physics_apply(world,&command,1,&error));step(1);
    ASSERT_EQ(state.event_count,1u);EXPECT_TRUE(state.events[0].started);
}

TEST_F(PhysicsTest, RotatedCapsuleRayAndPointQueriesRespectCylinderAndHemispheres) {
    desc.body_count=desc.collider_count=1;bodies[0].pose={{2,3,4},peuler({.3,.5,.7})};
    colliders[0].shape=PHYSICS_CAPSULE;colliders[0].radius=.5;colliders[0].height=2;create();
    struct QueryCase {PhysicsQueryKind kind;PhysicsVec origin,direction;bool found;double distance;PhysicsVec normal;};
    const QueryCase cases[]={
        {PHYSICS_RAY,{2,0,0},{-1,0,0},true,1.5,{1,0,0}},
        {PHYSICS_RAY,{0,3,0},{0,-1,0},true,1.5,{0,1,0}},
        {PHYSICS_RAY,{0,-3,0},{0,1,0},true,1.5,{0,-1,0}},
        {PHYSICS_RAY,{2,0,.4},{-1,0,0},true,1.7,{.6,0,.8}},
        {PHYSICS_RAY,{.49,1.49,3},{0,0,-1},false,0,{}},
        {PHYSICS_RAY,{0,0,0},{1,0,0},true,0,{}},
        {PHYSICS_RAY,{2,0,0},{1,0,0},false,0,{}},
        {PHYSICS_POINT,{.49,0,0},{},true,0,{}},
        {PHYSICS_POINT,{.49,1.49,0},{},false,0,{}},
        {PHYSICS_POINT,{0,1.49,0},{},true,0,{}},
        {PHYSICS_POINT,{0,1.51,0},{},false,0,{}}
    };
    for(const auto& test:cases) {
        PhysicsQuery query={};query.kind=test.kind;query.distance=10;query.membership=query.filter=UINT32_MAX;
        query.origin=padd(bodies[0].pose.position,prot(bodies[0].pose.rotation,test.origin));query.direction=prot(bodies[0].pose.rotation,test.direction);
        PhysicsHit hit={};ASSERT_TRUE(physics_query(world,&query,1,&hit,&error));ASSERT_EQ(hit.found,test.found);
        if(test.found){EXPECT_NEAR(hit.distance,test.distance,1e-12);
            if(test.distance>0)EXPECT_NEAR(plen(psub(hit.normal,prot(bodies[0].pose.rotation,test.normal))),0,1e-12);}
    }
}

TEST_F(PhysicsTest, CapsuleValidationAndLiveReconfigurationAreAtomic) {
    desc.body_count=desc.collider_count=1;bodies[0].motion=PHYSICS_DYNAMIC;desc.gravity={};
    colliders[0].shape=PHYSICS_CAPSULE;colliders[0].height=-1;
    EXPECT_FALSE(physics_create(&desc,&world,&error));EXPECT_EQ(world,nullptr);
    colliders[0].height=2;create();step(10);double mass=state.bodies[0].mass;
    const double invalids[]={-1,INFINITY,NAN};
    for(double invalid:invalids) {
        colliders[0].height=invalid;EXPECT_FALSE(physics_configure(world,bodies,1,colliders,1,&error));
        ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_DOUBLE_EQ(state.bodies[0].mass,mass);EXPECT_EQ(state.tick,10u);
    }
    colliders[0].height=1;colliders[0].radius=0;EXPECT_FALSE(physics_configure(world,bodies,1,colliders,1,&error));
    colliders[0].radius=.5;ASSERT_TRUE(physics_configure(world,bodies,1,colliders,1,&error));step(1);
    EXPECT_EQ(state.tick,11u);EXPECT_LT(state.bodies[0].mass,mass);
}

TEST_F(PhysicsTest, ConvexCookingMergesFacesAndIgnoresDuplicateInteriorAndEdgePoints) {
    hull(0);hull_points[0][8]=hull_points[0][0];hull_points[0][9]={0,0,0};hull_points[0][10]={0,.5,.5};
    PhysicsHull* cooked=nullptr;const char* message=nullptr;
    ASSERT_TRUE(physics_hull_create(hull_points[0],11,&cooked,&message))<<(message?message:"");
    EXPECT_EQ(cooked->face_count,6u);EXPECT_EQ(cooked->edge_count,12u);EXPECT_EQ(cooked->axis_count,3u);
    EXPECT_NEAR(cooked->volume,1,1e-12);EXPECT_NEAR(plen(cooked->centre),0,1e-12);
    physics_hull_destroy(cooked);
}

TEST_F(PhysicsTest, ConvexTetrahedronUsesFullInertiaAndTranslatedCentreOfMass) {
    desc.body_count=desc.collider_count=1;desc.gravity={};bodies[0].motion=PHYSICS_DYNAMIC;bodies[0].pose.position={};
    colliders[0].shape=PHYSICS_CONVEX;colliders[0].density=6;colliders[0].vertices=hull_points[0];colliders[0].vertex_count=4;
    for(unsigned variant=0;variant<2;variant++) {
        PhysicsVec offset=variant?PhysicsVec{10,20,-5}:PhysicsVec{};
        hull_points[0][0]=offset;for(unsigned i=0;i<3;i++)hull_points[0][i+1]=padd(offset,pbase(i));
        create();ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_NEAR(state.bodies[0].mass,1,1e-12);
        EXPECT_NEAR(plen(psub(state.bodies[0].centre_of_mass,padd(offset,{.25,.25,.25}))),0,1e-12);
        PhysicsCommand impulse={};impulse.kind=PHYSICS_IMPULSE;impulse.body=1;impulse.value={0,0,1};impulse.at_point=true;
        impulse.point=padd(state.bodies[0].centre_of_mass,{0,1,0});ASSERT_TRUE(physics_apply(world,&impulse,1,&error));
        ASSERT_TRUE(physics_read(world,&state,&error));
        EXPECT_NEAR(state.bodies[0].angular_velocity.x,14,1e-10);EXPECT_NEAR(state.bodies[0].angular_velocity.y,-2,1e-10);
        EXPECT_NEAR(state.bodies[0].angular_velocity.z,-2,1e-10);physics_destroy(world);world=nullptr;
    }
}

TEST_F(PhysicsTest, ConvexBoxAndRoundBodiesSettleOnHullAndBoxFloors) {
    for(unsigned variant=0;variant<5;variant++) {
        colliders[0].shape=colliders[1].shape=PHYSICS_BOX;
        if(variant!=0)hull(0,{10,.5,10});if(variant<3)hull(1);
        if(variant==3){colliders[1].shape=PHYSICS_CAPSULE;colliders[1].height=2;}
        if(variant==4)colliders[1].shape=PHYSICS_SPHERE;
        create();for(unsigned tick=0;tick<1200;tick++)step(1);
        EXPECT_NEAR(state.bodies[1].pose.position.y,variant==3?1.5:.5,.01);EXPECT_NEAR(plen(state.bodies[1].velocity),0,.01);
        EXPECT_TRUE(state.bodies[1].sleeping);EXPECT_NEAR(state.bodies[1].mass,variant<3?1:variant==3?2*acos(-1.0)/3:acos(-1.0)/6,1e-12);
        physics_destroy(world);world=nullptr;
    }
}

TEST_F(PhysicsTest, ConvexThinRotatedHullsRejectAabbFalseContactsAndReturnWitnesses) {
    desc.gravity={};hull(0,{2,.05,.05});hull(1,{2,.05,.05});
    bodies[0].pose.position={};bodies[1].pose.position={0,1,0};bodies[0].pose.rotation=bodies[1].pose.rotation=peuler({0,0,acos(-1.0)/4});
    create();step(1);EXPECT_GT(state.candidate_count,0u);EXPECT_EQ(state.contact_count,0u);EXPECT_EQ(state.event_count,0u);
    auto hit=distance();ASSERT_TRUE(hit.found);EXPECT_FALSE(hit.overlapping);EXPECT_NEAR(hit.distance,sqrt(.5)-.1,1e-8);
    EXPECT_NEAR(plen(psub(hit.point_b,hit.point)),hit.distance,1e-10);EXPECT_NEAR(hit.normal.x,-sqrt(.5),1e-8);EXPECT_NEAR(hit.normal.y,sqrt(.5),1e-8);
}

TEST_F(PhysicsTest, ConvexPointAndRayQueriesUseHullPlanesAfterRotation) {
    desc.body_count=desc.collider_count=1;bodies[0].pose={{2,3,4},peuler({.3,.5,.7})};
    PhysicsVec tetra[]={{0,0,0},{1,0,0},{0,1,0},{0,0,1}};colliders[0].shape=PHYSICS_CONVEX;colliders[0].vertices=tetra;colliders[0].vertex_count=4;create();
    const PhysicsVec points[]={{.2,.2,.2},{.5,.5,.5},{0,0,0}};
    for(unsigned i=0;i<3;i++) {
        PhysicsQuery q={};q.kind=PHYSICS_POINT;q.membership=q.filter=UINT32_MAX;q.origin=padd(bodies[0].pose.position,prot(bodies[0].pose.rotation,points[i]));
        PhysicsHit hit={};ASSERT_TRUE(physics_query(world,&q,1,&hit,&error));EXPECT_EQ(hit.found,i!=1);
    }
    PhysicsQuery q={};q.kind=PHYSICS_RAY;q.origin=padd(bodies[0].pose.position,prot(bodies[0].pose.rotation,{2,.2,.2}));
    q.direction=prot(bodies[0].pose.rotation,{-1,0,0});q.distance=10;q.membership=q.filter=UINT32_MAX;
    PhysicsHit hit={};ASSERT_TRUE(physics_query(world,&q,1,&hit,&error));ASSERT_TRUE(hit.found);EXPECT_NEAR(hit.distance,1.4,1e-10);
    EXPECT_NEAR(plen(psub(hit.normal,prot(bodies[0].pose.rotation,punit({1,1,1})))),0,1e-10);
}

TEST_F(PhysicsTest, ConvexDistanceMatchesAnalyticBoxSphereOracleAcrossRotations) {
    desc.gravity={};hull(0,{1,1,1});colliders[1].shape=PHYSICS_SPHERE;colliders[1].radius=.25;
    PhysicsQuat rotation=peuler({.3,.6,.8});bodies[0].pose={{5,2,1},rotation};
    const PhysicsVec centres[]={{3,0,0},{3,4,0},{0,0,0},{.5,.5,.5},{2,2,2},{0,3,0},{1,1,1}};
    for(const auto& centre:centres) {
        bodies[1].pose={padd(bodies[0].pose.position,prot(rotation,centre)),rotation};create();
        PhysicsVec nearest={fmax(-1.0,fmin(1.0,centre.x)),fmax(-1.0,fmin(1.0,centre.y)),fmax(-1.0,fmin(1.0,centre.z))};
        double expected=fmax(0,plen(psub(centre,nearest))-.25);auto hit=distance();ASSERT_TRUE(hit.found);
        EXPECT_NEAR(hit.distance,expected,1e-7);EXPECT_EQ(hit.overlapping,expected==0);
        EXPECT_NEAR(plen(psub(hit.point_b,hit.point)),expected,1e-7);physics_destroy(world);world=nullptr;
    }
}

TEST_F(PhysicsTest, ConvexDistanceAlsoCoversAnalyticSphereAndCapsulePairs) {
    desc.gravity={};bodies[0].pose.position={};colliders[0].sensor=true;colliders[0].membership=colliders[0].filter=0;
    const PhysicsVec offsets[]={{3,0,0},{0,4,0},{3,4,0},{.5,0,0}};
    for(unsigned shape=0;shape<2;shape++)for(const auto& offset:offsets) {
        colliders[0].shape=colliders[1].shape=shape?PHYSICS_CAPSULE:PHYSICS_SPHERE;colliders[0].height=colliders[1].height=2;
        bodies[1].pose.position=offset;create();double dy=shape?fmax(0,fabs(offset.y)-2):offset.y;
        double expected=fmax(0,plen({offset.x,dy,offset.z})-1);auto hit=distance();EXPECT_NEAR(hit.distance,expected,1e-7);
        EXPECT_EQ(hit.overlapping,expected==0);EXPECT_NEAR(plen(psub(hit.point_b,hit.point)),expected,1e-7);
        physics_destroy(world);world=nullptr;
    }
}

TEST_F(PhysicsTest, ConvexWorldOwnsVerticesAndRejectsGeometryEditsAtomically) {
    desc.gravity={};hull(1);create();step(10);double mass=state.bodies[1].mass,y=state.bodies[1].pose.position.y;
    for(unsigned i=0;i<8;i++)hull_points[1][i].z=0;
    EXPECT_FALSE(physics_configure(world,bodies,2,colliders,2,&error));ASSERT_TRUE(physics_read(world,&state,&error));
    EXPECT_DOUBLE_EQ(state.bodies[1].mass,mass);EXPECT_EQ(state.tick,10u);
    PhysicsQuery q={};q.kind=PHYSICS_RAY;q.origin={0,5,0};q.direction={0,-1,0};q.distance=3;q.membership=q.filter=UINT32_MAX;
    PhysicsHit hit={};ASSERT_TRUE(physics_query(world,&q,1,&hit,&error));EXPECT_NEAR(hit.distance,5-y-.5,1e-10);
    hull(1,{1,1,1});ASSERT_TRUE(physics_configure(world,bodies,2,colliders,2,&error));ASSERT_TRUE(physics_read(world,&state,&error));
    EXPECT_NEAR(state.bodies[1].mass,8,1e-12);EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.y,y);EXPECT_EQ(state.tick,10u);
    for(auto& point:hull_points[1])point={NAN,NAN,NAN};
    ASSERT_TRUE(physics_query(world,&q,1,&hit,&error));EXPECT_NEAR(hit.distance,5-y-1,1e-10);
    EXPECT_FALSE(physics_configure(world,bodies,2,colliders,2,&error));step(1);EXPECT_EQ(state.tick,11u);
}

TEST_F(PhysicsTest, ConvexRejectsDegenerateCloudsAndOversizedInput) {
    desc.body_count=desc.collider_count=1;hull(0);colliders[0].vertex_count=65;
    EXPECT_FALSE(physics_create(&desc,&world,&error));EXPECT_EQ(world,nullptr);
    colliders[0].vertex_count=4;
    for(unsigned i=0;i<4;i++)hull_points[0][i]={double(i),0,0};
    EXPECT_FALSE(physics_create(&desc,&world,&error));EXPECT_EQ(world,nullptr);
    for(unsigned i=0;i<4;i++)hull_points[0][i]={double(i&1),double((i>>1)&1),0};
    EXPECT_FALSE(physics_create(&desc,&world,&error));EXPECT_EQ(world,nullptr);
    hull(0);hull_points[0][3].z=NAN;EXPECT_FALSE(physics_create(&desc,&world,&error));EXPECT_EQ(world,nullptr);
}

TEST_F(PhysicsTest, ConvexPointCloudCookingPreservesMassUnderRotationScaleAndPermutation) {
    uint32_t seed=912367;
    for(unsigned sample=0;sample<12;sample++) {
        PhysicsVec points[32],transformed[32];
        for(auto& point:points){double v[3];for(auto& value:v){seed=1664525*seed+1013904223;value=double(seed)/UINT32_MAX*2-1;}point={v[0],v[1],v[2]};}
        PhysicsHull *a=nullptr,*b=nullptr;const char* message=nullptr;
        ASSERT_TRUE(physics_hull_create(points,32,&a,&message))<<(message?message:"");
        PhysicsQuat rotation=peuler({sample*.13,sample*.21,sample*.17});double scale=sample%3==0?.001:sample%3==1?1:1000;
        for(unsigned i=0;i<32;i++)transformed[31-i]=padd({10,-20,5},pmul(prot(rotation,points[i]),scale));
        ASSERT_TRUE(physics_hull_create(transformed,32,&b,&message))<<(message?message:"");
        EXPECT_NEAR(b->volume/(scale*scale*scale),a->volume,1e-8);
        EXPECT_NEAR(plen(psub(b->centre,padd({10,-20,5},pmul(prot(rotation,a->centre),scale)))),0,1e-8*scale);
        double trace_a=a->inertia[0]+a->inertia[4]+a->inertia[8],trace_b=b->inertia[0]+b->inertia[4]+b->inertia[8];
        EXPECT_NEAR(trace_b/pow(scale,5),trace_a,1e-8);
        for(unsigned i=0;i<32;i++){EXPECT_TRUE(physics_hull_contains(a,points[i]));EXPECT_TRUE(physics_hull_contains(b,transformed[i]));}
        physics_hull_destroy(a);physics_hull_destroy(b);
    }
}

TEST_F(PhysicsTest, ConvexDistanceFindsOctahedronFaceWitnessesAndIntersection) {
    PhysicsVec octahedron[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    desc.gravity={};bodies[0].pose.position={};
    for(auto& c:colliders){c.shape=PHYSICS_CONVEX;c.vertices=octahedron;c.vertex_count=6;}
    for(unsigned sample=0;sample<8;sample++) {
        double d=.25+sample*.3;bodies[1].pose.position={d,d,d};create();auto hit=distance();
        double expected=fmax(0,(3*d-2)/sqrt(3));EXPECT_NEAR(hit.distance,expected,1e-8);EXPECT_EQ(hit.overlapping,expected==0);
        EXPECT_NEAR(plen(psub(hit.point_b,hit.point)),expected,1e-8);physics_destroy(world);world=nullptr;
    }
}

TEST_F(PhysicsTest, ConvexRotatedManifoldsAndSensorsProduceActualContacts) {
    desc.gravity={};bodies[0].pose={{0,0,0},peuler({.2,.4,.3})};
    bodies[1].pose={{.1,.15,.12},peuler({-.4,.6,-.2})};hull(0,{.8,.35,.2});
    for(unsigned variant=0;variant<4;variant++) {
        if(variant%2)hull(1,{.6,.25,.3});else {colliders[1].shape=PHYSICS_BOX;colliders[1].size={1.2,.5,.6};}
        colliders[0].sensor=variant>=2;create();step(1);ASSERT_EQ(state.event_count,1u);
        EXPECT_TRUE(state.events[0].started);EXPECT_EQ(state.events[0].sensor,colliders[0].sensor);
        EXPECT_TRUE(pfinite(state.bodies[1].pose.position));EXPECT_TRUE(pfinite(state.bodies[1].angular_velocity));
        if(colliders[0].sensor){EXPECT_EQ(state.contact_count,0u);EXPECT_NEAR(plen(psub(state.bodies[1].pose.position,bodies[1].pose.position)),0,1e-12);}
        else {EXPECT_GT(state.contact_count,0u);EXPECT_GT(plen(psub(state.bodies[1].pose.position,bodies[1].pose.position)),1e-4);}
        physics_destroy(world);world=nullptr;
    }
}

struct PhysicsMeshTest : PhysicsTest {
    PhysicsVec points[4]={{-10,0,-10},{10,0,-10},{-10,0,10},{10,0,10}};
    uint32_t indices[6]={0,2,1,1,2,3};
    void floor() {
        bodies[0].pose.position={};colliders[0].shape=PHYSICS_MESH;colliders[0].density=0;
        colliders[0].vertices=points;colliders[0].vertex_count=4;colliders[0].indices=indices;colliders[0].index_count=6;
    }
    PhysicsHit ray(PhysicsVec origin,PhysicsVec direction={0,-1,0}) {
        PhysicsQuery q={};q.kind=PHYSICS_RAY;q.origin=origin;q.direction=direction;q.distance=100;q.membership=q.filter=UINT32_MAX;
        PhysicsHit hit={};EXPECT_TRUE(physics_query(world,&q,1,&hit,&error))<<(error.message?error.message:"");return hit;
    }
};
TEST_F(PhysicsMeshTest, MeshAllConvexProfilesSettleAcrossTheDiagonal) {
    floor();
    for(unsigned shape=0;shape<4;shape++) {
        colliders[1].shape=(PhysicsShape)shape;colliders[1].height=2;if(shape==PHYSICS_CONVEX)hull(1);
        create();for(unsigned tick=0;tick<1200;tick++)step(1);
        EXPECT_NEAR(state.bodies[1].pose.position.y,shape==PHYSICS_CAPSULE?1.5:.5,.01)<<shape;
        EXPECT_NEAR(plen(state.bodies[1].velocity),0,.01)<<shape;EXPECT_TRUE(state.bodies[1].sleeping)<<shape;
        physics_destroy(world);world=nullptr;
    }
}
TEST_F(PhysicsMeshTest, MeshCoplanarSeamsDoNotDeflectSlidingBoxesOrCapsules) {
    floor();desc.gravity={};
    for(unsigned shape=0;shape<2;shape++) {
        colliders[1].shape=shape?PHYSICS_CAPSULE:PHYSICS_BOX;colliders[1].height=1;colliders[1].friction=0;
        bodies[1].pose.position={-2,shape?1:.5,0};bodies[1].velocity={2,0,0};create();
        for(unsigned tick=0;tick<240;tick++)step(1);
        EXPECT_NEAR(state.bodies[1].pose.position.x,2,1e-8);EXPECT_NEAR(state.bodies[1].pose.position.z,0,1e-8);
        EXPECT_NEAR(state.bodies[1].pose.position.y,shape?1:.5,1e-8);EXPECT_NEAR(plen(state.bodies[1].angular_velocity),0,1e-8);
        physics_destroy(world);world=nullptr;
    }
}
TEST_F(PhysicsMeshTest, MeshWindingPointAndDistanceQueriesUseSurfaces) {
    floor();desc.gravity={};bodies[1].pose.position={0,2,0};colliders[1].shape=PHYSICS_SPHERE;create();
    auto h=ray({2,4,0});ASSERT_TRUE(h.found);EXPECT_EQ(h.collider,10u);EXPECT_DOUBLE_EQ(h.distance,4);EXPECT_DOUBLE_EQ(h.normal.y,1);
    EXPECT_FALSE(ray({2,-4,0},{0,1,0}).found);
    auto d=distance();EXPECT_NEAR(d.distance,1.5,1e-8);EXPECT_NEAR(d.point.y,0,1e-8);EXPECT_NEAR(d.point_b.y,1.5,1e-8);EXPECT_GT(d.normal.y,.999);
    auto reverse=distance(11,10);EXPECT_NEAR(reverse.distance,d.distance,1e-8);EXPECT_NEAR(reverse.point_b.y,0,1e-8);EXPECT_LT(reverse.normal.y,-.999);
    PhysicsQuery q={};q.kind=PHYSICS_POINT;q.origin={2,-.1,0};q.membership=q.filter=UINT32_MAX;PhysicsHit hit={};
    ASSERT_TRUE(physics_query(world,&q,1,&hit,&error));EXPECT_FALSE(hit.found);
    q.origin.y=0;ASSERT_TRUE(physics_query(world,&q,1,&hit,&error));EXPECT_TRUE(hit.found);
    colliders[0].double_sided=true;ASSERT_TRUE(physics_configure(world,bodies,2,colliders,2,&error));
    h=ray({2,-4,0},{0,1,0});ASSERT_TRUE(h.found);EXPECT_DOUBLE_EQ(h.normal.y,-1);
}
TEST_F(PhysicsMeshTest, MeshBackfacesDoNotPushBodiesAndTwoSidedFacesDo) {
    floor();desc.gravity={};bodies[1].pose.position={0,-.4,0};colliders[1].shape=PHYSICS_SPHERE;
    create();step(1);EXPECT_EQ(state.contact_count,0u);
    colliders[0].double_sided=true;ASSERT_TRUE(physics_configure(world,bodies,2,colliders,2,&error));step(1);
    EXPECT_GT(state.contact_count,0u);EXPECT_LT(state.bodies[1].pose.position.y,-.4);
}
TEST_F(PhysicsMeshTest, MeshConcaveGapDoesNotBecomeASolidHull) {
    floor();desc.gravity={};colliders[0].index_count=3;bodies[1].pose.position={5,.2,5};colliders[1].shape=PHYSICS_SPHERE;
    create();step(1);EXPECT_EQ(state.contact_count,0u);
    auto gap=ray({5,2,6});EXPECT_FALSE(gap.found)<<gap.collider<<":"<<gap.point.x<<","<<gap.point.y<<","<<gap.point.z;
    EXPECT_GT(plen(psub(physics_triangle_closest(points[0],points[2],points[1],{5,0,6}),{5,0,6})),7);
    EXPECT_TRUE(ray({-5,2,-6}).found);EXPECT_GT(distance().distance,6);
}
TEST_F(PhysicsMeshTest, MeshRotatedKinematicPlatformCarriesItsSurfaceAndQueries) {
    floor();desc.gravity={};bodies[0].motion=PHYSICS_KINEMATIC_POSITION;bodies[0].pose.rotation=peuler({0,0,.25});
    bodies[1].pose.position={0,5,0};create();auto h=ray({2,4,0});ASSERT_TRUE(h.found);
    EXPECT_NEAR(h.point.y,2*tan(.25),1e-10);EXPECT_NEAR(h.normal.x,-sin(.25),1e-10);
    PhysicsCommand target={};target.kind=PHYSICS_TARGET;target.body=1;target.pose=bodies[0].pose;target.pose.position.y=1;
    ASSERT_TRUE(physics_apply(world,&target,1,&error));step(1);h=ray({2,4,0});ASSERT_TRUE(h.found);EXPECT_NEAR(h.point.y,1+2*tan(.25),1e-10);
}
TEST_F(PhysicsMeshTest, MeshHeightfieldTessellationUsesSampledTriangles) {
    floor();double heights[]={0,0,0,2};auto& c=colliders[0];c.shape=PHYSICS_HEIGHTFIELD;c.rows=c.columns=2;c.cell_width=c.cell_depth=2;c.heights=heights;
    desc.gravity={};bodies[1].pose.position={20,3,0};create();
    auto a=ray({.5,3,.5}),b=ray({1.5,3,1.5});ASSERT_TRUE(a.found);ASSERT_TRUE(b.found);
    EXPECT_NEAR(a.point.y,0,1e-10);EXPECT_NEAR(b.point.y,1,1e-10);
    EXPECT_NEAR(b.normal.x,-1/sqrt(3.0),1e-10);EXPECT_NEAR(b.normal.y,1/sqrt(3.0),1e-10);
    // the source samples can disappear immediately after cooking.
    heights[3]=100;EXPECT_NEAR(ray({1.5,3,1.5}).point.y,1,1e-10);
}
TEST_F(PhysicsMeshTest, MeshCookingOwnsBuffersAndRejectedReplacementIsAtomic) {
    floor();create();step(20);double y=state.bodies[1].pose.position.y;
    points[0].y=points[1].y=points[2].y=points[3].y=1;
    EXPECT_DOUBLE_EQ(ray({2,4,0}).point.y,0);
    ASSERT_TRUE(physics_configure(world,bodies,2,colliders,2,&error));EXPECT_DOUBLE_EQ(ray({2,4,0}).point.y,1);
    indices[0]=99;EXPECT_FALSE(physics_configure(world,bodies,2,colliders,2,&error));
    ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_EQ(state.tick,20u);EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.y,y);
    EXPECT_DOUBLE_EQ(ray({2,4,0}).point.y,1);
}
TEST_F(PhysicsMeshTest, MeshRejectsInvalidTopologyAndDynamicEnvironments) {
    floor();
    for(unsigned invalid=0;invalid<5;invalid++) {
        PhysicsVec p[5]={{0,0,0},{1,0,0},{0,0,1},{0,0,-1},{0,1,0}};
        uint32_t triangles[9]={0,2,1,0,1,3,0,1,4};colliders[0].vertices=p;colliders[0].vertex_count=5;colliders[0].indices=triangles;colliders[0].index_count=6;
        if(invalid==0)triangles[2]=99;
        if(invalid==1)triangles[2]=triangles[1];
        if(invalid==2){triangles[3]=0;triangles[4]=2;triangles[5]=3;}
        if(invalid==3)colliders[0].index_count=9;
        if(invalid==4)bodies[0].motion=PHYSICS_DYNAMIC;
        EXPECT_FALSE(physics_create(&desc,&world,&error))<<invalid;EXPECT_EQ(world,nullptr);
    }
}
TEST_F(PhysicsMeshTest, MeshExactPositionWeldingFindsDuplicatedSeams) {
    floor();PhysicsVec p[6]={points[0],points[2],points[1],points[1],points[2],points[3]};uint32_t triangles[]={0,1,2,3,4,5};
    colliders[0].vertices=p;colliders[0].vertex_count=6;colliders[0].indices=triangles;create();
    for(unsigned tick=0;tick<1200;tick++)step(1);EXPECT_NEAR(state.bodies[1].pose.position.y,.5,.01);EXPECT_TRUE(state.bodies[1].sleeping);
}

// independent Moller-Trumbore oracle: no BVH, plane projection or shared triangle-distance helper.
static bool mesh_ray_oracle(PhysicsVec origin,PhysicsVec direction,const PhysicsVec* p,double* distance) {
    PhysicsVec e1=psub(p[1],p[0]),e2=psub(p[2],p[0]),h=pcross(direction,e2);double det=pdot(e1,h);
    if(det<=1e-12)return false;PhysicsVec s=psub(origin,p[0]);double u=pdot(s,h)/det;if(u<0||u>1)return false;
    PhysicsVec q=pcross(s,e1);double v=pdot(direction,q)/det;if(v<0||u+v>1)return false;
    double t=pdot(e2,q)/det;if(t<0||t>*distance)return false;*distance=t;return true;
}
TEST_F(PhysicsMeshTest, MeshBvhMatchesFlatTriangleOracleAndPrunesSparseQueries) {
    floor();desc.body_count=desc.collider_count=1;
    const unsigned side=65;lam::ArrayList<double> heights;
    for(unsigned z=0;z<side;z++)for(unsigned x=0;x<side;x++)ASSERT_TRUE(heights.append(.03*x+.02*z+.1*sin(x*.2)));
    auto& c=colliders[0];c.shape=PHYSICS_HEIGHTFIELD;c.rows=c.columns=side;c.cell_width=c.cell_depth=1;c.heights=heights.data();create();
    uint32_t seed=918237;
    for(unsigned sample=0;sample<96;sample++) {
        double values[4];for(auto& v:values){seed=1664525*seed+1013904223;v=double(seed)/UINT32_MAX;}
        PhysicsVec origin={values[0]*70-3,10,values[1]*70-3},direction=punit({values[2]*.4-.2,-1,values[3]*.4-.2});
        auto hit=ray(origin,direction);double best=100;bool found=false;
        for(unsigned z=0;z+1<side;z++)for(unsigned x=0;x+1<side;x++) {
            unsigned a=z*side+x;PhysicsVec p[4]={{double(x),heights[a],double(z)},{double(x+1),heights[a+1],double(z)},
                {double(x),heights[a+side],double(z+1)},{double(x+1),heights[a+side+1],double(z+1)}};
            PhysicsVec first[]={p[0],p[2],p[1]},second[]={p[1],p[2],p[3]};
            found=mesh_ray_oracle(origin,direction,first,&best)||found;found=mesh_ray_oracle(origin,direction,second,&best)||found;
        }
        ASSERT_EQ(hit.found,found)<<sample;if(found)EXPECT_NEAR(hit.distance,best,1e-8)<<sample;
        EXPECT_LT(hit.triangle_tests,64u)<<sample;
    }
}
TEST_F(PhysicsMeshTest, MeshWorkExhaustionReturnsAnErrorAndKeepsTheWorldReadable) {
    floor();desc.body_count=desc.collider_count=1;lam::ArrayList<PhysicsVec> p;lam::ArrayList<uint32_t> triangles;
    for(unsigned i=0;i<9000;i++) {
        for(auto v:{PhysicsVec{-1,i*.001,-1},PhysicsVec{2,i*.001,.1},PhysicsVec{2,i*.001,-1}})ASSERT_TRUE(p.append(v));
        for(unsigned j=0;j<3;j++)ASSERT_TRUE(triangles.append(3*i+j));
    }
    colliders[0].vertices=p.data();colliders[0].vertex_count=p.size();colliders[0].indices=triangles.data();colliders[0].index_count=triangles.size();create();
    PhysicsQuery q={};q.kind=PHYSICS_RAY;q.origin={0,20,0};q.direction={0,-1,0};q.distance=30;q.membership=q.filter=UINT32_MAX;PhysicsHit hit={};
    EXPECT_FALSE(physics_query(world,&q,1,&hit,&error));ASSERT_NE(error.message,nullptr);EXPECT_NE(strstr(error.message,"work quota"),nullptr);
    ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_EQ(state.tick,0u);
    q.origin.x=100;ASSERT_TRUE(physics_query(world,&q,1,&hit,&error));EXPECT_FALSE(hit.found);
}
TEST_F(PhysicsMeshTest, MeshToMeshQueriesRejectInsteadOfUsingConvexSupports) {
    floor();bodies[1].motion=PHYSICS_FIXED;colliders[1]=colliders[0];colliders[1].id=11;colliders[1].body=2;create();
    PhysicsQuery q={};q.kind=PHYSICS_DISTANCE;q.collider_a=10;q.collider_b=11;PhysicsHit hit={};
    EXPECT_FALSE(physics_query(world,&q,1,&hit,&error));EXPECT_NE(strstr(error.message,"unsupported"),nullptr);
}

TEST_F(PhysicsMeshTest, MeshDenseCoplanarGridKeepsABoundedRestingManifold) {
    floor();const unsigned side=33;lam::ArrayList<double> heights;for(unsigned i=0;i<side*side;i++)ASSERT_TRUE(heights.append(0));
    auto& c=colliders[0];c.shape=PHYSICS_HEIGHTFIELD;c.rows=c.columns=side;c.cell_width=c.cell_depth=.125;c.heights=heights.data();
    bodies[1].pose.position={2,2,2};colliders[1].size={2,1,2};create();
    for(unsigned tick=0;tick<1200;tick++){step(1);EXPECT_LE(state.contact_count,4u);}
    EXPECT_NEAR(state.bodies[1].pose.position.y,.5,.01);EXPECT_TRUE(state.bodies[1].sleeping);EXPECT_NEAR(plen(state.bodies[1].angular_velocity),0,.01);
}
TEST_F(PhysicsMeshTest, MeshMovingPlatformSupportsDynamicBodies) {
    floor();bodies[0].motion=PHYSICS_KINEMATIC_VELOCITY;bodies[0].velocity={0,.25,0};bodies[1].pose.position={0,.5,0};create();
    for(unsigned tick=0;tick<240;tick++)step(1);
    EXPECT_NEAR(state.bodies[0].pose.position.y,.5,1e-10);EXPECT_NEAR(state.bodies[1].pose.position.y,1,.01);
    EXPECT_NEAR(state.bodies[1].velocity.y,.25,.01);
}
TEST_F(PhysicsMeshTest, MeshQueriesRemainScaleAndRotationConsistent) {
    floor();desc.body_count=desc.collider_count=1;PhysicsVec original[4];memcpy(original,points,sizeof(points));
    for(double scale:{1e-6,1.0,1e6}) {
        for(unsigned i=0;i<4;i++)points[i]=pmul(original[i],scale);
        bodies[0].pose.rotation=peuler({.2,.3,.4});bodies[0].pose.position=pmul({2,3,4},scale);create();
        PhysicsVec local={2*scale,4*scale,0},normal=prot(bodies[0].pose.rotation,{0,1,0});
        PhysicsVec origin=padd(bodies[0].pose.position,prot(bodies[0].pose.rotation,local));
        PhysicsQuery q={};q.kind=PHYSICS_RAY;q.origin=origin;q.direction=pmul(normal,-1);q.distance=10*scale;q.membership=q.filter=UINT32_MAX;PhysicsHit hit={};
        ASSERT_TRUE(physics_query(world,&q,1,&hit,&error));ASSERT_TRUE(hit.found);EXPECT_NEAR(hit.distance,4*scale,1e-8*scale);
        EXPECT_NEAR(plen(psub(hit.normal,normal)),0,1e-10);
        physics_destroy(world);world=nullptr;
    }
}

TEST_F(PhysicsMeshTest, MeshWorldQuotaChargesInputVerticesBeforeWelding) {
    floor();lam::ArrayList<PhysicsVec> duplicated;
    for(unsigned i=0;i<40000;i++)ASSERT_TRUE(duplicated.append(points[i%4]));
    colliders[0].vertices=duplicated.data();colliders[0].vertex_count=duplicated.size();
    colliders[1]=colliders[0];colliders[1].id=11;colliders[1].body=2;bodies[1].motion=PHYSICS_FIXED;
    EXPECT_FALSE(physics_create(&desc,&world,&error));EXPECT_EQ(world,nullptr);EXPECT_NE(strstr(error.message,"world geometry quota"),nullptr);
}

TEST_F(PhysicsTest, ShapeCastFindsThinWallsAndKeepsWorldState) {
    desc.gravity={0,0,0};bodies[0].pose.position={3,0,0};colliders[0].size={.02,10,10};
    bodies[1].pose.position={0,0,0};colliders[1].shape=PHYSICS_SPHERE;colliders[1].radius=.5;create();
    PhysicsQuery q={};q.kind=PHYSICS_SHAPE_CAST;q.collider_a=11;q.direction={1,0,0};q.distance=10;q.membership=q.filter=UINT32_MAX;
    PhysicsHit hit={};ASSERT_TRUE(physics_query(world,&q,1,&hit,&error))<<(error.message?error.message:"");
    ASSERT_TRUE(hit.found);EXPECT_EQ(hit.collider,10u);EXPECT_NEAR(hit.distance,2.49,1e-7);EXPECT_NEAR(hit.fraction,.249,1e-8);
    EXPECT_NEAR(hit.normal.x,-1,1e-7);EXPECT_FALSE(hit.overlapping);step(0);EXPECT_EQ(state.tick,0u);EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.x,0);
    q.origin={1,0,0};ASSERT_TRUE(physics_query(world,&q,1,&hit,&error));EXPECT_NEAR(hit.distance,1.49,1e-7);
    q.direction={-1,0,0};ASSERT_TRUE(physics_query(world,&q,1,&hit,&error));EXPECT_FALSE(hit.found);
    q.direction={1,0,0};q.origin={3,0,0};ASSERT_TRUE(physics_query(world,&q,1,&hit,&error));
    EXPECT_TRUE(hit.found);EXPECT_TRUE(hit.overlapping);EXPECT_DOUBLE_EQ(hit.distance,0);EXPECT_DOUBLE_EQ(plen(hit.normal),0);
    q.origin={0,0,0};q.cast_iterations=1;EXPECT_FALSE(physics_query(world,&q,1,&hit,&error));
    EXPECT_NE(strstr(error.message,"iteration budget"),nullptr);q.cast_iterations=0;ASSERT_TRUE(physics_query(world,&q,1,&hit,&error));
}

TEST_F(PhysicsTest, InlineSphereQueriesShareCastsFiltersAndLeaveWorldOwnershipUnchanged) {
    desc.gravity = {0, 0, 0};
    bodies[0].pose.position = {3, 0, 0};
    colliders[0].size = {.02, 10, 10};
    bodies[1].pose.position = {6, 0, 0};
    bodies[1].motion = PHYSICS_FIXED;
    create();
    PhysicsQuery query = {};
    query.kind = PHYSICS_SHAPE_CAST;
    query.radius = .5;
    query.origin = {0, 0, 0};
    query.direction = {1, 0, 0};
    query.distance = 10;
    query.membership = query.filter = UINT32_MAX;
    PhysicsHit hit = {};
    ASSERT_TRUE(physics_query(world, &query, 1, &hit, &error)) << (error.message ? error.message : "");
    ASSERT_TRUE(hit.found);
    EXPECT_EQ(hit.collider, 10u);
    EXPECT_NEAR(hit.distance, 2.49, 1e-7);
    EXPECT_NEAR(hit.normal.x, -1, 1e-7);
    PhysicsHit hits[2];
    size_t count = 0;
    ASSERT_TRUE(physics_query_hits(world, &query, hits, 2, &count, &error));
    ASSERT_EQ(count, 2u);
    EXPECT_EQ(hits[1].collider, 11u);
    query.exclude_body = 1;
    ASSERT_TRUE(physics_query(world, &query, 1, &hit, &error));
    EXPECT_EQ(hit.collider, 11u);
    EXPECT_NEAR(hit.distance, 5, 1e-7);
    query.exclude_body = 0;
    query.filter = 0;
    ASSERT_TRUE(physics_query(world, &query, 1, &hit, &error));
    EXPECT_FALSE(hit.found);
    query.filter = UINT32_MAX;
    query.kind = PHYSICS_SHAPE_OVERLAP;
    query.origin = {3, 0, 0};
    ASSERT_TRUE(physics_query(world, &query, 1, &hit, &error));
    EXPECT_TRUE(hit.overlapping);
    query.collider_a = 11;
    EXPECT_FALSE(physics_query(world, &query, 1, &hit, &error));
    query.collider_a = 0;
    query.radius = -1;
    EXPECT_FALSE(physics_query(world, &query, 1, &hit, &error));
    query.radius = .5;
    query.exclude_body = 999;
    EXPECT_FALSE(physics_query(world, &query, 1, &hit, &error));
    step(0);
    EXPECT_EQ(state.tick, 0u);
    EXPECT_EQ(state.body_count, 2u);
    EXPECT_DOUBLE_EQ(state.bodies[0].pose.position.x, 3);
    EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.x, 6);
}

TEST_F(PhysicsTest, ExactOverlapRejectsAabbFalseHitsAndAllQueriesAreSorted) {
    desc.gravity={0,0,0};desc.body_count=desc.collider_count=3;
    bodies[0].pose.position={3,0,0};bodies[1].pose.position={0,0,0};bodies[2].pose.position={6,0,0};bodies[2].motion=PHYSICS_FIXED;
    for(unsigned i=0;i<3;i++){colliders[i].shape=PHYSICS_SPHERE;colliders[i].radius=.5;}create();
    PhysicsQuery q={};q.kind=PHYSICS_RAY;q.origin={-2,0,0};q.direction={1,0,0};q.distance=20;q.membership=q.filter=UINT32_MAX;
    PhysicsHit hits[3];size_t count=0;ASSERT_TRUE(physics_query_hits(world,&q,hits,3,&count,&error));ASSERT_EQ(count,3u);
    EXPECT_EQ(hits[0].collider,11u);EXPECT_EQ(hits[1].collider,10u);EXPECT_EQ(hits[2].collider,12u);
    EXPECT_FALSE(physics_query_hits(world,&q,hits,2,&count,&error));EXPECT_EQ(count,0u);
    q.kind=PHYSICS_SHAPE_OVERLAP;q.collider_a=11;q.origin={2.1,.9,0};ASSERT_TRUE(physics_query(world,&q,1,hits,&error));EXPECT_FALSE(hits[0].found);
    q.origin={2.5,0,0};ASSERT_TRUE(physics_query(world,&q,1,hits,&error));EXPECT_TRUE(hits[0].found);EXPECT_EQ(hits[0].collider,10u);
    q.kind=PHYSICS_SHAPE_CAST;q.origin={0,0,0};ASSERT_TRUE(physics_query_hits(world,&q,hits,3,&count,&error));ASSERT_EQ(count,2u);
    EXPECT_EQ(hits[0].collider,10u);EXPECT_NEAR(hits[0].distance,2,1e-7);EXPECT_EQ(hits[1].collider,12u);EXPECT_NEAR(hits[1].distance,5,1e-7);
}

TEST_F(PhysicsMeshTest, InlineSphereQueriesCastWithoutAWorldSourceCollider) {
    floor();
    desc.body_count = desc.collider_count = 1;
    create();
    PhysicsQuery query = {};
    query.kind = PHYSICS_SHAPE_CAST;
    query.origin = {.5, 3, .5};
    query.direction = {0, -1, 0};
    query.distance = 6;
    query.radius = .25;
    query.membership = query.filter = UINT32_MAX;
    PhysicsHit hit;
    ASSERT_TRUE(physics_query(world, &query, 1, &hit, &error)) << (error.message ? error.message : "");
    ASSERT_TRUE(hit.found);
    EXPECT_NEAR(hit.distance, 2.75, 1e-7);
    EXPECT_NEAR(hit.normal.y, 1, 1e-7);
    EXPECT_GT(hit.triangle_tests, 0u);
    query.origin = {.5, -3, .5};
    query.direction = {0, 1, 0};
    ASSERT_TRUE(physics_query(world, &query, 1, &hit, &error));
    EXPECT_FALSE(hit.found);
    step(0);
    EXPECT_EQ(state.body_count, 1u);
    EXPECT_EQ(state.tick, 0u);
}

TEST_F(PhysicsMeshTest, ShapeCastsUseTriangleBvhAndSurfaceSidedness) {
    floor();bodies[1].pose.position={0,3,0};desc.gravity={0,0,0};colliders[1].shape=PHYSICS_SPHERE;colliders[1].radius=.5;create();
    PhysicsQuery q={};q.kind=PHYSICS_SHAPE_CAST;q.collider_a=11;q.direction={0,-1,0};q.distance=10;q.membership=q.filter=UINT32_MAX;
    PhysicsHit hit={};ASSERT_TRUE(physics_query(world,&q,1,&hit,&error))<<(error.message?error.message:"");
    ASSERT_TRUE(hit.found);EXPECT_NEAR(hit.distance,2.5,1e-7);EXPECT_NEAR(hit.normal.y,1,1e-7);EXPECT_LE(hit.triangle_tests,2u);
    q.origin={0,-6,0};q.direction={0,1,0};ASSERT_TRUE(physics_query(world,&q,1,&hit,&error));EXPECT_FALSE(hit.found);
    q.origin={0,0,0};q.direction={1,0,0};ASSERT_TRUE(physics_query(world,&q,1,&hit,&error));EXPECT_FALSE(hit.found);
}

TEST(PhysicsGeometryCast, RotatedBoxesAgreeWithIndependentSlabSweep) {
    PhysicsColliderDesc da={},db={};da.shape=db.shape=PHYSICS_BOX;da.size={1,.6,1.4};db.size={1.2,1.0,.8};
    uint32_t seed=0x13579bdfu;
    auto random=[&](){seed=1664525u*seed+1013904223u;return double(seed>>8)/double(1u<<24);};
    for(unsigned i=0;i<128;i++) {
        PhysicsQuat rotation=peuler({random()*2,random()*2,random()*2});
        PhysicsVec start={-5,random()*4-2,random()*4-2},delta={10,random()*2-1,random()*2-1},half=pmul(padd(da.size,db.size),.5);
        double near=0,far=1;bool found=true;
        for(unsigned axis=0;axis<3;axis++) {
            double p=paxis(start,axis),v=paxis(delta,axis),h=paxis(half,axis);
            if(v==0){if(p< -h||p>h)found=false;continue;}
            double lo=(-h-p)/v,hi=(h-p)/v;if(lo>hi){double swap=lo;lo=hi;hi=swap;}
            near=fmax(near,lo);far=fmin(far,hi);if(near>far)found=false;
        }
        PhysicsVec shift={100,-30,20};
        PhysicsGeometry a={&da,nullptr,{padd(shift,prot(rotation,start)),rotation}},b={&db,nullptr,{shift,rotation}};
        PhysicsHit hit={};const char* error=nullptr;
        ASSERT_TRUE(physics_geometry_cast(&a,&b,prot(rotation,delta),0,&hit,&error))<<(error?error:"")<<" sample "<<i;
        ASSERT_EQ(hit.found,found)<<" sample "<<i;
        if(found){EXPECT_NEAR(hit.fraction,near,1e-7);EXPECT_NEAR(hit.distance,near*plen(delta),1e-6);EXPECT_NEAR(plen(hit.normal),1,1e-7);}
    }
}

TEST(PhysicsGeometryCast, CapsulePlaneSupportMatchesItsRotatedAxis) {
    PhysicsColliderDesc capsule={},plane={};capsule.shape=PHYSICS_CAPSULE;capsule.radius=.3;capsule.height=2;
    plane.shape=PHYSICS_BOX;plane.size={100,.02,100};
    for(unsigned i=0;i<16;i++) {
        double angle=i*.1;PhysicsQuat rotation=peuler({0,0,angle});
        PhysicsGeometry a={&capsule,nullptr,{{0,3,0},rotation}},b={&plane,nullptr,{{0,0,0},{0,0,0,1}}};
        PhysicsHit hit={};const char* error=nullptr;ASSERT_TRUE(physics_geometry_cast(&a,&b,{0,-10,0},0,&hit,&error))<<(error?error:"");
        ASSERT_TRUE(hit.found);EXPECT_NEAR(hit.distance,3-.01-.3-fabs(cos(angle)),1e-6);EXPECT_NEAR(hit.normal.y,1,1e-6);
    }
}

// independent oracle enumerates face planes, finite edge cylinders and vertex spheres without GJK/BVH.
static double sphere_line_oracle(PhysicsVec start,PhysicsVec velocity,double radius) {
    double a=pdot(velocity,velocity),b=pdot(start,velocity),c=pdot(start,start)-radius*radius;
    if(c<=0)return 0;
    double discriminant=b*b-a*c;
    if(a==0||discriminant<0)return 2;
    double time=(-b-sqrt(discriminant))/a;return time>=0&&time<=1?time:2;
}
static double sphere_triangle_oracle(PhysicsVec start,PhysicsVec velocity,double radius,const PhysicsVec p[3]) {
    double best=2;PhysicsVec normal=punit(pcross(psub(p[1],p[0]),psub(p[2],p[0])));
    double initial=pdot(psub(start,p[0]),normal),speed=pdot(velocity,normal);
    if(speed!=0)for(unsigned side=0;side<2;side++) {
        double signed_radius=side?radius:-radius,time=(signed_radius-initial)/speed;
        if(time<0||time>1)continue;
        PhysicsVec point=psub(padd(start,pmul(velocity,time)),pmul(normal,signed_radius));bool inside=true;
        for(unsigned i=0;i<3;i++)if(pdot(pcross(psub(p[(i+1)%3],p[i]),psub(point,p[i])),normal)<0)inside=false;
        if(inside)best=fmin(best,time);
    }
    for(unsigned i=0;i<3;i++) {
        best=fmin(best,sphere_line_oracle(psub(start,p[i]),velocity,radius));
        PhysicsVec edge=psub(p[(i+1)%3],p[i]),offset=psub(start,p[i]);double length=pdot(edge,edge);
        PhysicsVec perpendicular=psub(offset,pmul(edge,pdot(offset,edge)/length));
        PhysicsVec motion=psub(velocity,pmul(edge,pdot(velocity,edge)/length));double time=sphere_line_oracle(perpendicular,motion,radius);
        if(time<=1) {
            double fraction=pdot(psub(padd(start,pmul(velocity,time)),p[i]),edge)/length;
            if(fraction>=0&&fraction<=1)best=fmin(best,time);
        }
    }
    return best;
}
TEST_F(PhysicsMeshTest, SweptSphereBvhMatchesIndependentTriangleFeatureOracle) {
    floor();desc.gravity={0,0,0};colliders[1].shape=PHYSICS_SPHERE;colliders[1].radius=.25;
    const unsigned size=5;lam::ArrayList<PhysicsVec> vertices;lam::ArrayList<double> heights;lam::ArrayList<uint32_t> indices;
    for(unsigned z=0;z<size;z++)for(unsigned x=0;x<size;x++) {
        double y=.15*sin(double(x)*.7)*cos(double(z)*.6);ASSERT_TRUE(vertices.append({double(x),y,double(z)}));ASSERT_TRUE(heights.append(y));
    }
    for(unsigned z=0;z+1<size;z++)for(unsigned x=0;x+1<size;x++) {
        uint32_t a=z*size+x;uint32_t cell[]={a,a+size,a+1,a+1,a+size,a+size+1};for(uint32_t v:cell)ASSERT_TRUE(indices.append(v));
    }
    uint32_t seed=0x123abc45u;auto random=[&](){seed=1664525u*seed+1013904223u;return double(seed>>8)/double(1u<<24);};
    for(unsigned profile=0;profile<2;profile++) {
        auto& ground=colliders[0];ground.double_sided=true;
        ground.shape=profile?PHYSICS_HEIGHTFIELD:PHYSICS_MESH;ground.vertices=profile?nullptr:vertices.data();ground.vertex_count=profile?0:vertices.size();
        ground.indices=profile?nullptr:indices.data();ground.index_count=profile?0:indices.size();
        ground.rows=ground.columns=profile?size:0;ground.heights=profile?heights.data():nullptr;ground.cell_width=ground.cell_depth=1;
        create();
        for(unsigned sample=0;sample<64;sample++) {
            PhysicsVec start={.6+random()*2.8,2.5,.6+random()*2.8},delta={random()-.5,-5,random()-.5};
            PhysicsCommand command={};command.kind=PHYSICS_TELEPORT;command.body=2;command.pose={start,{0,0,0,1}};
            ASSERT_TRUE(physics_apply(world,&command,1,&error));
            double expected=2;
            for(size_t i=0;i<indices.size();i+=3) {
                PhysicsVec triangle[]={vertices[indices[i]],vertices[indices[i+1]],vertices[indices[i+2]]};
                expected=fmin(expected,sphere_triangle_oracle(start,delta,.25,triangle));
            }
            PhysicsQuery q={};q.kind=PHYSICS_SHAPE_CAST;q.collider_a=11;q.direction=delta;q.distance=plen(delta);q.membership=q.filter=UINT32_MAX;
            PhysicsHit hit={};ASSERT_TRUE(physics_query(world,&q,1,&hit,&error))<<(error.message?error.message:"")<<" profile "<<profile<<" sample "<<sample;
            ASSERT_EQ(hit.found,expected<=1);if(hit.found)EXPECT_NEAR(hit.fraction,expected,1e-7)<<" profile "<<profile<<" sample "<<sample;
        }
        physics_destroy(world);world=nullptr;
    }
}

TEST_F(PhysicsTest, CcdSphereStopsAtThinWallAndRetainsContactEvents) {
    desc.gravity={};desc.timestep=.01;bodies[0].pose.position={3,0,0};colliders[0].size={.02,4,4};
    bodies[1].pose.position={};bodies[1].velocity={1000,0,0};bodies[1].ccd=true;colliders[1].shape=PHYSICS_SPHERE;
    create();step(1);ASSERT_EQ(state.tick,1u);EXPECT_NEAR(state.bodies[1].pose.position.x,2.49,1e-7);
    EXPECT_NEAR(state.bodies[1].velocity.x,0,1e-7);ASSERT_EQ(state.event_count,1u);EXPECT_TRUE(state.events[0].started);
    step(1);EXPECT_EQ(state.event_count,0u);EXPECT_NEAR(state.bodies[1].pose.position.x,2.49,1e-7);
    PhysicsCommand c={};c.kind=PHYSICS_VELOCITY;c.body=2;c.value={-1000,0,0};ASSERT_TRUE(physics_apply(world,&c,1,&error));
    step(1);EXPECT_LT(state.bodies[1].pose.position.x,-7);step(1);ASSERT_EQ(state.event_count,1u);EXPECT_FALSE(state.events[0].started);
}
TEST_F(PhysicsTest, CcdRestitutionIntegratesRemainderAndAppliesForceOnce) {
    desc.gravity={};desc.timestep=.01;bodies[0].pose.position={3,0,0};colliders[0].size={.02,4,4};colliders[0].restitution=1;
    bodies[1].pose.position={};bodies[1].velocity={1000,0,0};bodies[1].ccd=true;colliders[1].shape=PHYSICS_SPHERE;colliders[1].restitution=1;
    create();PhysicsCommand force={};force.kind=PHYSICS_FORCE;force.body=2;force.value={0,100,0};
    ASSERT_TRUE(physics_apply(world,&force,1,&error));step(1);double mass=state.bodies[1].mass;
    EXPECT_NEAR(state.bodies[1].pose.position.x,-5.02,1e-6);EXPECT_NEAR(state.bodies[1].velocity.x,-1000,1e-6);
    // tangent friction acts at impact, so compare the force axis using a friction-free replay.
    physics_destroy(world);world=nullptr;colliders[0].friction=colliders[1].friction=0;create();
    ASSERT_TRUE(physics_apply(world,&force,1,&error));step(1);
    EXPECT_NEAR(state.bodies[1].velocity.y,1/mass,1e-10);EXPECT_NEAR(state.bodies[1].pose.position.y,.01/mass,1e-9);
    step(1);EXPECT_NEAR(state.bodies[1].velocity.y,1/mass,1e-10);
}
TEST_F(PhysicsTest, CcdRelativeSphereMotionConservesMomentumAndEnergy) {
    desc.gravity={};desc.timestep=.01;
    for(unsigned i=0;i<2;i++) {
        bodies[i].motion=PHYSICS_DYNAMIC;bodies[i].pose.position={i?3.0:-3.0,0,0};bodies[i].velocity={i?-1000.0:1000.0,0,0};
        colliders[i].shape=PHYSICS_SPHERE;colliders[i].friction=0;colliders[i].restitution=1;
    }
    bodies[1].ccd=true;create();step(1);
    EXPECT_NEAR(state.bodies[0].velocity.x,-1000,1e-6);EXPECT_NEAR(state.bodies[1].velocity.x,1000,1e-6);
    EXPECT_NEAR(state.bodies[0].pose.position.x,-8,1e-6);EXPECT_NEAR(state.bodies[1].pose.position.x,8,1e-6);
    EXPECT_NEAR(state.bodies[0].velocity.x+state.bodies[1].velocity.x,0,1e-10);ASSERT_EQ(state.event_count,1u);
}
TEST_F(PhysicsTest, CcdMovingKinematicWallUsesRelativeTravel) {
    desc.gravity={};desc.timestep=.01;bodies[0].motion=PHYSICS_KINEMATIC_VELOCITY;bodies[0].pose.position={3,0,0};bodies[0].velocity={-100,0,0};colliders[0].size={.02,4,4};
    bodies[1].pose.position={};bodies[1].velocity={1000,0,0};bodies[1].ccd=true;colliders[1].shape=PHYSICS_SPHERE;
    create();step(1);EXPECT_NEAR(state.bodies[0].pose.position.x,2,1e-9);
    EXPECT_NEAR(state.bodies[1].pose.position.x,1.49,1e-7);EXPECT_NEAR(state.bodies[1].velocity.x,-100,1e-7);
}
TEST_F(PhysicsTest, CcdGravityRestingGapAndSpinPreserveSphereGeometry) {
    bodies[1].pose.position={0,3,0};bodies[1].velocity={0,-1000,0};bodies[1].angular_velocity={0,0,20};
    bodies[1].ccd=true;colliders[1].shape=PHYSICS_SPHERE;colliders[0].friction=colliders[1].friction=0;
    create();step(1);EXPECT_NEAR(state.bodies[1].pose.position.y,.5,1e-7);
    for(unsigned i=0;i<180;i++)step(1);
    EXPECT_NEAR(state.bodies[1].pose.position.y,.5,1e-6);EXPECT_NEAR(state.bodies[1].velocity.y,0,1e-7);
}
TEST_F(PhysicsTest, CcdReconfigurationIsAtomicAndUnsupportedTargetsFaultBeforePublication) {
    bodies[1].ccd=true;EXPECT_FALSE(physics_create(&desc,&world,&error));EXPECT_EQ(world,nullptr);EXPECT_NE(strstr(error.message,"CCD"),nullptr);
    colliders[1].shape=PHYSICS_SPHERE;desc.gravity={};desc.timestep=.01;bodies[1].pose.position={};bodies[1].velocity={1000,0,0};
    bodies[0].pose.position={3,0,0};colliders[0].size={.02,4,4};create();
    colliders[1].shape=PHYSICS_BOX;EXPECT_FALSE(physics_configure(world,bodies,2,colliders,2,&error));
    step(1);EXPECT_NEAR(state.bodies[1].pose.position.x,2.49,1e-7);
    physics_destroy(world);world=nullptr;colliders[1].shape=PHYSICS_SPHERE;bodies[0].motion=PHYSICS_KINEMATIC_VELOCITY;bodies[0].angular_velocity={0,0,1};create();
    EXPECT_FALSE(physics_step(world,1,&state,&error));EXPECT_NE(strstr(error.message,"CCD target"),nullptr);
    ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_EQ(state.tick,0u);EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.x,0);
}
TEST_F(PhysicsTest, CcdImpactBudgetFaultKeepsLastCompletedSnapshot) {
    desc.gravity={};desc.timestep=.1;desc.body_count=desc.collider_count=3;
    bodies[0].pose.position={-1,0,0};colliders[0].size={.02,4,4};colliders[0].restitution=1;
    bodies[2].motion=PHYSICS_FIXED;bodies[2].pose.position={1,0,0};colliders[2].size=colliders[0].size;colliders[2].restitution=1;
    bodies[1].pose.position={};bodies[1].velocity={10000,0,0};bodies[1].ccd=true;colliders[1].shape=PHYSICS_SPHERE;colliders[1].restitution=1;
    create();EXPECT_FALSE(physics_step(world,1,&state,&error));EXPECT_NE(strstr(error.message,"CCD impact quota"),nullptr)<<(error.message?error.message:"");
    ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_EQ(state.tick,0u);EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.x,0);EXPECT_EQ(state.event_count,0u);
}
TEST_F(PhysicsMeshTest, CcdSphereStopsAtIndexedAndHeightfieldSurfaces) {
    floor();double heights[4]={};
    for(unsigned mode=0;mode<2;mode++) {
        if(mode) {physics_destroy(world);world=nullptr;auto& terrain=colliders[0];terrain.shape=PHYSICS_HEIGHTFIELD;terrain.vertices=nullptr;terrain.indices=nullptr;terrain.vertex_count=terrain.index_count=0;
            terrain.rows=terrain.columns=2;terrain.heights=heights;terrain.cell_width=terrain.cell_depth=2;}
        bodies[1].pose.position={1,3,1};bodies[1].velocity={0,-1000,0};bodies[1].ccd=true;colliders[1].shape=PHYSICS_SPHERE;
        create();step(1);EXPECT_NEAR(state.bodies[1].pose.position.y,.5,1e-7);EXPECT_NEAR(state.bodies[1].velocity.y,0,1e-6);
    }
}
TEST(PhysicsGeometryCast, ClosestFaceWinsRoundoffTiesNearItsTriangulationDiagonal) {
    PhysicsColliderDesc point={},box={};point.shape=PHYSICS_BOX;box.shape=PHYSICS_BOX;box.size={.02,4,4};
    PhysicsGeometry a={&point,nullptr,{{-.49000000218160689,1.4898157103337242e-9,-6.5321962777812375e-11},{0,0,0,1}}},
        b={&box,nullptr,{{-1,0,0},{0,0,0,1}}};
    for(unsigned i=0;i<32;i++) {
        a.pose.position.y=(i&1?-1:1)*pow(10,-12.0+(i%8));PhysicsHit hit={};const char* error=nullptr;
        ASSERT_TRUE(physics_geometry_distance(&a,&b,&hit,&error))<<(error?error:"");
        EXPECT_NEAR(hit.distance,.4999999978183931,1e-14);EXPECT_NEAR(hit.normal.x,-1,1e-14);
        EXPECT_NEAR(hit.normal.y,0,1e-14);EXPECT_NEAR(hit.normal.z,0,1e-14);
    }
}
TEST_F(PhysicsTest, CcdRotatedBoxAndHullImpactsMatchIndependentReflection) {
    desc.gravity={};desc.timestep=.01;colliders[1].shape=PHYSICS_SPHERE;bodies[1].pose.position={};bodies[1].ccd=true;
    colliders[0].friction=colliders[1].friction=0;colliders[0].restitution=colliders[1].restitution=1;
    for(unsigned i=0;i<64;i++) {
        auto rotation=peuler({.13*i,.07*i,-.11*i});bodies[0].pose.rotation=rotation;bodies[0].pose.position=prot(rotation,{3,0,0});
        colliders[0].shape=PHYSICS_BOX;colliders[0].size={.02,4,4};if(i&1)hull(0,{.01,2,2});
        bodies[1].velocity=prot(rotation,{1000,0,0});create();step(1);
        PhysicsVec expected_position=prot(rotation,{-5.02,0,0}),expected_velocity=prot(rotation,{-1000,0,0});
        EXPECT_LT(plen(psub(state.bodies[1].pose.position,expected_position)),1e-6)<<i;
        EXPECT_LT(plen(psub(state.bodies[1].velocity,expected_velocity)),1e-5)<<i;
        physics_destroy(world);world=nullptr;
    }
}
TEST_F(PhysicsTest, CcdMultipleElasticImpactsConsumeExactlyOneTick) {
    desc.gravity={};desc.timestep=.01;desc.body_count=desc.collider_count=3;
    bodies[0].pose.position={-1,0,0};colliders[0].size={.02,4,4};colliders[0].restitution=1;
    bodies[2].motion=PHYSICS_FIXED;bodies[2].pose.position={1,0,0};colliders[2].size=colliders[0].size;colliders[2].restitution=1;
    bodies[1].pose.position={};bodies[1].velocity={1000,0,0};bodies[1].ccd=true;colliders[1].shape=PHYSICS_SPHERE;colliders[1].restitution=1;
    create();step(1);EXPECT_NEAR(state.bodies[1].pose.position.x,.2,1e-6);EXPECT_NEAR(state.bodies[1].velocity.x,1000,1e-6);
    ASSERT_EQ(state.event_count,2u);EXPECT_EQ(state.tick,1u);EXPECT_NEAR(state.time,.01,1e-12);
}
TEST_F(PhysicsTest, CcdInitialContactProbeDoesNotSpendPersistentContactCapacity) {
    desc.contact_limit=1;bodies[1].pose.position={0,.5,0};bodies[1].ccd=true;colliders[1].shape=PHYSICS_SPHERE;
    create();ASSERT_TRUE(physics_step(world,1,&state,&error))<<(error.message?error.message:"");
    EXPECT_EQ(state.tick,1u);EXPECT_EQ(state.contact_count,1u);EXPECT_NEAR(state.bodies[1].pose.position.y,.5,1e-8);
}

struct PhysicsJointTest : PhysicsTest {
    PhysicsJointDesc joint={};
    void SetUp() override {
        PhysicsTest::SetUp();desc.joints=&joint;desc.joint_count=1;
        bodies[0].pose.position={};bodies[1].pose.position={0,-2,0};colliders[0].size={.2,.2,.2};colliders[1].shape=PHYSICS_SPHERE;
        joint.id=100;joint.body_a=1;joint.body_b=2;joint.frame_a.rotation.w=joint.frame_b.rotation.w=1;joint.length=2;
    }
    void pin(PhysicsJointKind kind) {
        joint.kind=kind;joint.frame_a.position={0,-1,0};joint.frame_b.position={0,1,0};
    }
};
TEST_F(PhysicsJointTest, DistanceSupportsWeightAndReportsReactionAtItsAnchor) {
    create();step(240);ASSERT_EQ(state.joint_count,1u);EXPECT_NEAR(state.bodies[1].pose.position.y,-2,1e-10);
    EXPECT_LT(state.joints[0].linear_error,1e-10);EXPECT_TRUE(state.joints[0].coordinate_valid);
    EXPECT_NEAR(state.joints[0].force.y,state.bodies[1].mass*9.81,1e-8);EXPECT_NEAR(plen(state.joints[0].torque),0,1e-10);
    EXPECT_TRUE(state.bodies[1].sleeping);
}
TEST_F(PhysicsJointTest, ImplicitSpringMatchesIndependentEulerRecurrence) {
    desc.gravity={};bodies[1].pose.position={2,0,0};joint.length=1;joint.spring=true;joint.stiffness=20;joint.damping=3;
    create();physics_read(world,&state,&error);double mass=state.bodies[1].mass,x=2,v=0,dt=desc.timestep;
    for(unsigned i=0;i<120;i++) {
        v=(v-dt*joint.stiffness*(x-1)/mass)/(1+dt*joint.damping/mass+dt*dt*joint.stiffness/mass);x+=dt*v;
        step(1);EXPECT_NEAR(state.bodies[1].pose.position.x,x,1e-10)<<i;EXPECT_NEAR(state.bodies[1].velocity.x,v,1e-10)<<i;
    }
}
TEST_F(PhysicsJointTest, SphericalAnchorAllowsRotationAndKeepsBoundedDrift) {
    pin(PHYSICS_JOINT_SPHERICAL);bodies[1].velocity={1,0,0};create();step(240);
    EXPECT_LT(state.joints[0].linear_error,.006);EXPECT_GT(fabs(state.bodies[1].pose.rotation.z),.01);EXPECT_FALSE(state.joints[0].coordinate_valid);
}
TEST_F(PhysicsJointTest, FixedFramesBlockRelativeLinearAndAngularMotion) {
    pin(PHYSICS_JOINT_FIXED);create();PhysicsCommand commands[2]={};commands[0].kind=PHYSICS_IMPULSE;commands[0].body=2;commands[0].value={1,2,3};
    commands[1].kind=PHYSICS_TORQUE;commands[1].body=2;commands[1].value={2,3,4};ASSERT_TRUE(physics_apply(world,commands,2,&error));step(240);
    EXPECT_LT(state.joints[0].linear_error,1e-7);EXPECT_LT(state.joints[0].angular_error,1e-7);
    EXPECT_NEAR(state.bodies[1].pose.position.y,-2,1e-7);EXPECT_NEAR(state.bodies[1].pose.rotation.w,1,1e-7);
}
TEST_F(PhysicsJointTest, HingeMotorStopsAtTwistLimitWithoutSwingDrift) {
    desc.gravity={};pin(PHYSICS_JOINT_HINGE);joint.limit_enabled=true;joint.lower_limit=-.3;joint.upper_limit=.3;
    joint.motor_enabled=true;joint.motor_speed=2;joint.motor_max_force=10;create();step(240);
    EXPECT_NEAR(state.joints[0].coordinate,.3,.005);EXPECT_LT(state.joints[0].angular_error,.005);EXPECT_LT(state.joints[0].linear_error,.005);
    EXPECT_NEAR(state.bodies[1].angular_velocity.x,0,.01);
}
TEST_F(PhysicsJointTest, PrismaticMotorHonorsRangeAndBlocksGravityAndRotation) {
    joint.kind=PHYSICS_JOINT_PRISMATIC;bodies[1].pose.position={2,0,0};joint.limit_enabled=true;joint.lower_limit=1;joint.upper_limit=3;
    joint.motor_enabled=true;joint.motor_speed=2;joint.motor_max_force=100;create();step(240);
    EXPECT_NEAR(state.joints[0].coordinate,3,.005);EXPECT_LT(state.joints[0].linear_error,.005);EXPECT_LT(state.joints[0].angular_error,.005);
    EXPECT_NEAR(state.bodies[1].pose.position.y,0,.005);
}
TEST_F(PhysicsJointTest, ConeTwistLimitsSwingAndTwistWithExplicitFrames) {
    desc.gravity={};joint.kind=PHYSICS_JOINT_CONE_TWIST;joint.cone_angle=.4;joint.limit_enabled=true;joint.lower_limit=-.2;joint.upper_limit=.2;
    bodies[1].pose.position={};bodies[1].pose.rotation=peuler({.6,0,.7});bodies[1].angular_velocity={1,2,3};
    create();step(600);EXPECT_LT(state.joints[0].linear_error,.001);EXPECT_LT(state.joints[0].angular_error,.005);
    EXPECT_GE(state.joints[0].coordinate,-.205);EXPECT_LE(state.joints[0].coordinate,.205);
}
TEST_F(PhysicsJointTest, BreakingPublishesOnceAndSameDescriptorDoesNotRepairIt) {
    joint.break_force=.01;create();step(1);ASSERT_EQ(state.event_count,1u);EXPECT_EQ(state.events[0].joint,100u);
    EXPECT_TRUE(state.joints[0].broken);EXPECT_FALSE(state.joints[0].enabled);EXPECT_GT(plen(state.events[0].force),joint.break_force);
    ASSERT_TRUE(physics_reconfigure(world,&desc,&error));step(1);EXPECT_TRUE(state.joints[0].broken);EXPECT_EQ(state.event_count,0u);EXPECT_LT(state.bodies[1].pose.position.y,-2);
    joint.disabled=true;ASSERT_TRUE(physics_reconfigure(world,&desc,&error));joint.disabled=false;ASSERT_TRUE(physics_reconfigure(world,&desc,&error));
    ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_TRUE(state.joints[0].broken);EXPECT_EQ(state.tick,2u);
    bodies[1].motion=PHYSICS_KINEMATIC_POSITION;ASSERT_TRUE(physics_reconfigure(world,&desc,&error));step(1);
    EXPECT_TRUE(state.joints[0].broken);EXPECT_EQ(state.event_count,0u);
    // a real definition edit repairs it, so the candidate needs a dynamic endpoint again.
    joint.break_force=1;EXPECT_FALSE(physics_reconfigure(world,&desc,&error));
    ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_TRUE(state.joints[0].broken);EXPECT_EQ(state.tick,3u);
    bodies[1].motion=PHYSICS_DYNAMIC;ASSERT_TRUE(physics_reconfigure(world,&desc,&error));
    ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_FALSE(state.joints[0].broken);
}
TEST_F(PhysicsJointTest, InvalidReconfigurationRetainsMotionAndCachedJointState) {
    create();step(1);PhysicsWorld* other=nullptr;ASSERT_TRUE(physics_create(&desc,&other,&error));PhysicsState second={};ASSERT_TRUE(physics_step(other,1,&second,&error));
    bodies[1].linear_damping=100;joint.length=0;EXPECT_FALSE(physics_reconfigure(world,&desc,&error));bodies[1].linear_damping=0;joint.length=2;
    PhysicsCommand command={};command.kind=PHYSICS_IMPULSE;command.body=2;command.value={1,0,0};
    ASSERT_TRUE(physics_apply(world,&command,1,&error));ASSERT_TRUE(physics_apply(other,&command,1,&error));
    step(30);ASSERT_TRUE(physics_step(other,30,&second,&error));EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.x,second.bodies[1].pose.position.x);
    EXPECT_DOUBLE_EQ(state.bodies[1].velocity.y,second.bodies[1].velocity.y);physics_destroy(other);
}
TEST_F(PhysicsJointTest, RejectsUnknownBodiesSingularFramesAndUnsupportedCcdCoupling) {
    joint.body_b=99;EXPECT_FALSE(physics_create(&desc,&world,&error));joint.body_b=2;
    joint.frame_b.rotation={};EXPECT_FALSE(physics_create(&desc,&world,&error));joint.frame_b.rotation.w=1;
    bodies[1].ccd=true;EXPECT_FALSE(physics_create(&desc,&world,&error));EXPECT_NE(strstr(error.message,"CCD"),nullptr);bodies[1].ccd=false;
    bodies[1].pose.position={};EXPECT_FALSE(physics_create(&desc,&world,&error));EXPECT_NE(strstr(error.message,"coincident"),nullptr);
}
TEST_F(PhysicsJointTest, ConnectedCollisionPolicyIsAnAuthoredJointProperty) {
    joint.kind=PHYSICS_JOINT_SPHERICAL;bodies[1].pose.position={};desc.gravity={};create();step(1);
    EXPECT_EQ(state.contact_count,0u);EXPECT_EQ(state.event_count,0u);
    joint.collide_connected=true;ASSERT_TRUE(physics_reconfigure(world,&desc,&error));step(1);
    EXPECT_GT(state.contact_count,0u);ASSERT_EQ(state.event_count,1u);EXPECT_TRUE(state.events[0].started);EXPECT_EQ(state.events[0].joint,0u);
}
TEST_F(PhysicsJointTest, UnchangedConfigurationKeepsIndependentSleepingJointsAsleep) {
    desc.body_count=desc.collider_count=4;
    bodies[2].motion=PHYSICS_FIXED;bodies[2].pose.position={10,0,0};bodies[3].pose.position={10,-2,0};colliders[3].shape=PHYSICS_SPHERE;
    PhysicsJointDesc joints[2]={joint,joint};joints[1].id=101;joints[1].body_a=3;joints[1].body_b=4;desc.joints=joints;desc.joint_count=2;
    create();step(240);ASSERT_TRUE(state.bodies[1].sleeping);ASSERT_TRUE(state.bodies[3].sleeping);
    ASSERT_TRUE(physics_reconfigure(world,&desc,&error));ASSERT_TRUE(physics_read(world,&state,&error));
    EXPECT_TRUE(state.bodies[1].sleeping);EXPECT_TRUE(state.bodies[3].sleeping);
    joints[0].length=1.9;ASSERT_TRUE(physics_reconfigure(world,&desc,&error));step(1);
    EXPECT_FALSE(state.bodies[1].sleeping);EXPECT_TRUE(state.bodies[3].sleeping);EXPECT_EQ(state.tick,241u);
}
TEST_F(PhysicsJointTest, SpringAndFloorContactsShareTheIterativeSolver) {
    desc.body_count=3;desc.collider_count=2;
    bodies[0].pose.position={0,-.5,0};colliders[0].size={20,1,20};bodies[1].pose.position={0,2,0};bodies[2].motion=PHYSICS_FIXED;bodies[2].pose.position={0,3,0};
    joint.body_a=3;joint.length=2.6;joint.spring=true;joint.stiffness=10;joint.damping=1;create();step(600);
    EXPECT_NEAR(state.bodies[1].pose.position.y,.499,.002);EXPECT_NEAR(state.bodies[1].velocity.y,0,.002);
    EXPECT_GT(state.contact_count,0u);EXPECT_LT(state.joints[0].force.y,-.9);
}
TEST_F(PhysicsJointTest, MotorTorqueBoundAndBreakWrenchUsePhysicalUnits) {
    desc.gravity={};joint.kind=PHYSICS_JOINT_HINGE;bodies[1].pose.position={};joint.motor_enabled=true;
    joint.motor_speed=100;joint.motor_max_force=.1;joint.break_torque=.05;create();step(1);
    double inertia=.4*state.bodies[1].mass*colliders[1].radius*colliders[1].radius;
    EXPECT_NEAR(state.bodies[1].angular_velocity.x,.1*desc.timestep/inertia,1e-12);EXPECT_NEAR(state.joints[0].torque.x,.1,1e-12);
    ASSERT_EQ(state.event_count,1u);EXPECT_TRUE(state.joints[0].broken);EXPECT_NEAR(state.events[0].torque.x,.1,1e-12);
}
TEST(PhysicsJointIsland, ChainRetainsLengthAndWakesAllConnectedDynamics) {
    const unsigned count=12;PhysicsBodyDesc bodies[count]={};PhysicsColliderDesc colliders[count]={};PhysicsJointDesc joints[count-1]={};
    for(unsigned i=0;i<count;i++) {
        bodies[i].id=i+1;bodies[i].pose={{0,-double(i),0},{0,0,0,1}};bodies[i].motion=i?PHYSICS_DYNAMIC:PHYSICS_FIXED;bodies[i].gravity_scale=1;
        colliders[i].id=100+i;colliders[i].body=i+1;colliders[i].shape=PHYSICS_SPHERE;colliders[i].pose.rotation.w=1;colliders[i].radius=.2;
        colliders[i].density=1;colliders[i].membership=colliders[i].filter=UINT32_MAX;
        if(i){auto& j=joints[i-1];j.id=200+i;j.body_a=i;j.body_b=i+1;j.length=1;j.frame_a.rotation.w=j.frame_b.rotation.w=1;}
    }
    PhysicsDesc d={};d.bodies=bodies;d.body_count=count;d.colliders=colliders;d.collider_count=count;d.joints=joints;d.joint_count=count-1;
    d.gravity={0,-9.81,0};d.timestep=1.0/120;d.length_scale=1;d.iterations=16;d.contact_limit=d.event_limit=256;
    PhysicsWorld* world=nullptr;PhysicsState state={};PhysicsError error={};ASSERT_TRUE(physics_create(&d,&world,&error))<<(error.message?error.message:"");
    ASSERT_TRUE(physics_step(world,600,&state,&error))<<(error.message?error.message:"");
    for(unsigned i=0;i<count-1;i++)EXPECT_LT(state.joints[i].linear_error,.015)<<i;
    for(unsigned i=1;i<count;i++)EXPECT_TRUE(state.bodies[i].sleeping)<<i;
    PhysicsCommand impulse={};impulse.kind=PHYSICS_IMPULSE;impulse.body=count;impulse.value={.02,0,0};ASSERT_TRUE(physics_apply(world,&impulse,1,&error));
    ASSERT_TRUE(physics_step(world,1,&state,&error));for(unsigned i=1;i<count;i++)EXPECT_FALSE(state.bodies[i].sleeping)<<i;
    physics_destroy(world);
}
TEST_F(PhysicsJointTest, OffsetHingeLimitsUseTheAnchorConstrainedInertia) {
    desc.gravity={0,-10,0};desc.timestep=.01;desc.iterations=12;joint.kind=PHYSICS_JOINT_HINGE;
    bodies[0].pose.position={0,1.5,0};bodies[1].pose.position={0,.5,0};colliders[1].radius=.2;joint.frame_b.position={0,1,0};
    joint.limit_enabled=true;joint.lower_limit=-.3;joint.upper_limit=.3;joint.motor_enabled=true;joint.motor_speed=1;joint.motor_max_force=1;
    create();step(80);EXPECT_NEAR(state.joints[0].coordinate,.3,.005);
    joint.motor_speed=-1;ASSERT_TRUE(physics_reconfigure(world,&desc,&error));step(80);EXPECT_NEAR(state.joints[0].coordinate,-.3,.005);
}
TEST_F(PhysicsJointTest, OffsetMotorResponseMatchesParallelAxisInertia) {
    desc.gravity={};desc.timestep=.01;desc.iterations=1;joint.kind=PHYSICS_JOINT_HINGE;
    joint.frame_a.position={0,-1,0};joint.frame_b.position={0,1,0};colliders[1].radius=.2;
    joint.motor_enabled=true;joint.motor_speed=100;joint.motor_max_force=.1;create();step(1);
    double mass=state.bodies[1].mass,inertia=mass*(1+.4*.2*.2),omega=.1*desc.timestep/inertia;
    EXPECT_NEAR(state.bodies[1].angular_velocity.x,omega,1e-12);EXPECT_NEAR(state.bodies[1].velocity.z,-omega,1e-12);
    EXPECT_NEAR(state.joints[0].torque.x,.1,1e-12);EXPECT_NEAR(state.joints[0].force.z,-mass*omega/desc.timestep,1e-12);
}
TEST_F(PhysicsJointTest, InactiveFrameOverflowFaultRetainsThePreviousSnapshot) {
    desc.gravity={};joint.kind=PHYSICS_JOINT_SPHERICAL;joint.disabled=true;joint.frame_b.position={1e308,0,0};create();
    PhysicsCommand command={};command.kind=PHYSICS_TELEPORT;command.body=2;command.pose={{1e308,0,0},{0,0,0,1}};
    EXPECT_FALSE(physics_apply(world,&command,1,&error));ASSERT_TRUE(physics_read(world,&state,&error));
    EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.x,0);EXPECT_EQ(state.tick,0u);
}
TEST_F(PhysicsJointTest, AuthoredDescriptorAndCommandTransactionRetainsOldPublishedViewsOnFailure) {
    desc.gravity={};joint.kind=PHYSICS_JOINT_SPHERICAL;joint.disabled=true;joint.frame_b.position={1e308,0,0};create();
    PhysicsState before={};ASSERT_TRUE(physics_read(world,&before,&error));double mass=before.bodies[1].mass;
    desc.gravity={0,-20,0};colliders[1].density=2;
    PhysicsCommand commands[2]={};commands[0].kind=PHYSICS_FORCE;commands[0].body=2;commands[0].value={100,0,0};
    commands[1].kind=PHYSICS_TELEPORT;commands[1].body=2;commands[1].pose={{1e308,0,0},{0,0,0,1}};
    EXPECT_FALSE(physics_reconfigure_commands(world,&desc,commands,2,&error));
    EXPECT_DOUBLE_EQ(before.bodies[1].mass,mass);EXPECT_DOUBLE_EQ(before.joints[0].anchor_b.x,1e308);
    ASSERT_TRUE(physics_step(world,1,&state,&error));EXPECT_DOUBLE_EQ(state.bodies[1].mass,mass);
    EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.x,0);EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.y,-2);EXPECT_EQ(state.tick,1u);
}
TEST_F(PhysicsJointTest, JointFramesAndReactionsAreRigidTransformInvariant) {
    for(unsigned kind=0;kind<7;kind++) {
        joint={};joint.id=100;joint.body_a=1;joint.body_b=2;joint.kind=kind==6?PHYSICS_JOINT_DISTANCE:(PhysicsJointKind)kind;
        joint.frame_a.rotation.w=joint.frame_b.rotation.w=1;joint.length=2;
        bodies[0].pose={{0,0,0},{0,0,0,1}};bodies[1].pose={{0,-2,0},{0,0,0,1}};bodies[1].velocity={.2,0,.1};bodies[1].angular_velocity={.1,.2,.3};
        if(kind!=0&&kind!=6){joint.frame_a.position={0,-1,0};joint.frame_b.position={0,1,0};}
        if(kind==PHYSICS_JOINT_PRISMATIC){joint.frame_a.position=joint.frame_b.position={};bodies[1].pose.position={1,0,0};}
        if(kind==PHYSICS_JOINT_HINGE||kind==PHYSICS_JOINT_CONE_TWIST){joint.limit_enabled=true;joint.lower_limit=-.4;joint.upper_limit=.4;joint.cone_angle=.6;}
        if(kind==6){joint.spring=true;joint.length=1;joint.stiffness=20;joint.damping=3;}
        desc.gravity={0,-9.81,0};PhysicsBodyDesc original[2]={bodies[0],bodies[1]};create();step(60);
        PhysicsBodyState wanted=state.bodies[1];PhysicsJointState reaction=state.joints[0];physics_destroy(world);world=nullptr;
        PhysicsQuat rotation=peuler({.2+.1*kind,-.3,.4});PhysicsVec translation={5,-3,2};
        for(unsigned i=0;i<2;i++) {
            bodies[i]=original[i];bodies[i].pose.position=padd(translation,prot(rotation,original[i].pose.position));
            bodies[i].pose.rotation=pqmul(rotation,original[i].pose.rotation);bodies[i].velocity=prot(rotation,original[i].velocity);
            bodies[i].angular_velocity=prot(rotation,original[i].angular_velocity);
        }
        desc.gravity=prot(rotation,{0,-9.81,0});create();step(60);
        EXPECT_LT(plen(psub(state.bodies[1].pose.position,padd(translation,prot(rotation,wanted.pose.position)))),1e-7)<<kind;
        EXPECT_LT(plen(psub(state.bodies[1].velocity,prot(rotation,wanted.velocity))),1e-7)<<kind;
        EXPECT_LT(plen(psub(state.joints[0].force,prot(rotation,reaction.force))),1e-6)<<kind;
        EXPECT_LT(plen(psub(state.joints[0].torque,prot(rotation,reaction.torque))),1e-6)<<kind;
        EXPECT_NEAR(state.joints[0].coordinate,reaction.coordinate,1e-7)<<kind;
        physics_destroy(world);world=nullptr;
    }
}

struct PhysicsCharacterTest : PhysicsTest {
    PhysicsCharacterMove movement={};PhysicsCharacterState moved={};
    void SetUp() override {
        PhysicsTest::SetUp();bodies[1].motion=PHYSICS_KINEMATIC_POSITION;bodies[1].pose.position={0,1.001,0};
        colliders[1].shape=PHYSICS_CAPSULE;colliders[1].radius=.5;colliders[1].height=1;
        movement=physics_character_defaults(1);movement.body=2;
        for(unsigned i=2;i<4;i++)bodies[i].motion=PHYSICS_FIXED;
    }
    void move(PhysicsVec displacement) {
        movement.displacement=displacement;ASSERT_TRUE(physics_character_move(world,&movement,&moved,&error))<<(error.message?error.message:"");
    }
    void wall(unsigned index,PhysicsVec position,PhysicsVec size) {
        desc.body_count=desc.collider_count=index+1;bodies[index].pose.position=position;colliders[index].size=size;
    }
};
TEST_F(PhysicsCharacterTest, FreeMovementAndFloorSnapDoNotMutateTheWorld) {
    create();ASSERT_TRUE(physics_read(world,&state,&error));PhysicsBodyState original=state.bodies[1];const PhysicsBodyState* published=state.bodies;
    move({2,0,1});EXPECT_NEAR(moved.achieved.x,2,1e-12);EXPECT_NEAR(moved.achieved.z,1,1e-12);
    EXPECT_TRUE(moved.grounded);EXPECT_EQ(moved.platform,10u);EXPECT_NEAR(moved.pose.position.y,1.001,1e-5);
    EXPECT_NEAR(moved.ground_normal.y,1,1e-8);EXPECT_FALSE(moved.wall);EXPECT_FALSE(moved.ceiling);
    ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_EQ(state.bodies,published);EXPECT_EQ(state.tick,0u);EXPECT_EQ(state.event_count,0u);
    EXPECT_EQ(memcmp(&original,&state.bodies[1],sizeof(original)),0);
    PhysicsCommand jump={};jump.kind=PHYSICS_TELEPORT;jump.body=2;jump.pose={{0,3,0},{0,0,0,1}};ASSERT_TRUE(physics_apply(world,&jump,1,&error));
    movement.snap_distance=0;move({0,2,0});EXPECT_FALSE(moved.grounded);EXPECT_NEAR(moved.pose.position.y,5,1e-12);
}
TEST_F(PhysicsCharacterTest, ProfilesReadAchievedCapsuleFramesWithoutConsumingPendingTargets) {
    bodies[1].pose.rotation=peuler({0,acos(-1.0)/2,0});colliders[1].pose.position={.25,.5,0};create();
    PhysicsCharacterProfile profile={};ASSERT_TRUE(physics_character_profile(world,2,&profile,&error));
    EXPECT_EQ(profile.body,2u);EXPECT_EQ(profile.collider,11u);EXPECT_DOUBLE_EQ(profile.radius,.5);EXPECT_DOUBLE_EQ(profile.height,1);
    EXPECT_DOUBLE_EQ(profile.precision,1e-6);
    PhysicsPose expected=pcompose(bodies[1].pose,colliders[1].pose);EXPECT_NEAR(profile.shape_pose.position.x,expected.position.x,1e-12);EXPECT_NEAR(profile.shape_pose.position.y,expected.position.y,1e-12);EXPECT_NEAR(profile.shape_pose.position.z,expected.position.z,1e-12);
    PhysicsCommand target={};target.kind=PHYSICS_TARGET;target.body=2;target.pose=bodies[1].pose;target.pose.position.x=3;
    ASSERT_TRUE(physics_apply(world,&target,1,&error));ASSERT_TRUE(physics_character_profile(world,2,&profile,&error));EXPECT_DOUBLE_EQ(profile.pose.position.x,0);
    step(1);ASSERT_TRUE(physics_character_profile(world,2,&profile,&error));EXPECT_DOUBLE_EQ(profile.pose.position.x,3);EXPECT_NEAR(profile.shape_pose.position.z,expected.position.z,1e-12);
}
TEST_F(PhysicsCharacterTest, ProfileAdmissionSharesSolidCapsuleAndKinematicRoleChecks) {
    desc.collider_count=3;colliders[2].body=2;colliders[2].sensor=true;create();PhysicsCharacterProfile profile={};
    ASSERT_TRUE(physics_character_profile(world,2,&profile,&error));PhysicsCharacterProfile before=profile;
    EXPECT_FALSE(physics_character_profile(world,1,&profile,&error));EXPECT_EQ(memcmp(&profile,&before,sizeof(profile)),0);
    colliders[2].sensor=false;ASSERT_TRUE(physics_configure(world,bodies,desc.body_count,colliders,desc.collider_count,&error));
    EXPECT_FALSE(physics_character_profile(world,2,&profile,&error));EXPECT_NE(strstr(error.message,"one solid capsule"),nullptr);EXPECT_EQ(memcmp(&profile,&before,sizeof(profile)),0);
    EXPECT_FALSE(physics_character_profile(world,999,&profile,&error));EXPECT_FALSE(physics_character_profile(world,2,nullptr,&error));
}
TEST_F(PhysicsCharacterTest, ThinWallStopsFastMotionAndKeepsTangentialTravel) {
    wall(2,{2,2,0},{.02,4,20});create();move({100,0,3});
    EXPECT_NEAR(moved.pose.position.x,2-.01-.5-.001,1e-5);EXPECT_NEAR(moved.pose.position.z,3,1e-5);
    EXPECT_TRUE(moved.wall);EXPECT_TRUE(moved.grounded);EXPECT_GT(moved.contact_count,1u);
}
TEST_F(PhysicsCharacterTest, TwoWallCornerDoesNotReenterAnEarlierContactPlane) {
    wall(2,{2,2,0},{.02,4,20});wall(3,{0,2,2},{20,4,.02});create();move({10,0,6});
    EXPECT_NEAR(moved.pose.position.x,1.489,1e-5);EXPECT_NEAR(moved.pose.position.z,1.489,1e-5);EXPECT_TRUE(moved.grounded);
}
TEST_F(PhysicsCharacterTest, JumpHitsCeilingWithoutSnappingToTheFloor) {
    wall(2,{0,3,0},{20,.1,20});create();move({1,10,0});
    EXPECT_NEAR(moved.pose.position.y,3-.05-1-.001,1e-5);EXPECT_NEAR(moved.pose.position.x,1,1e-5);
    EXPECT_TRUE(moved.ceiling);EXPECT_FALSE(moved.grounded);
}
TEST_F(PhysicsCharacterTest, RecoveryAndExhaustionLeavePublishedSnapshotsIntact) {
    bodies[1].pose.position.y=.9;create();move({0,0,0});EXPECT_GT(moved.recovery.y,.1);EXPECT_TRUE(moved.grounded);
    PhysicsCharacterState before=moved;movement.geometry_budget=1;
    EXPECT_FALSE(physics_character_move(world,&movement,&moved,&error));EXPECT_NE(strstr(error.message,"quota"),nullptr);
    EXPECT_EQ(memcmp(&before,&moved,sizeof(before)),0);ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.y,.9);
    movement.geometry_budget=4096;move({1,0,0});EXPECT_NEAR(moved.pose.position.x,1,1e-12);
}
TEST_F(PhysicsCharacterTest, StepClimbsAnAdmittedStairAndRejectsATallerStair) {
    wall(2,{2,.1,0},{2,.2,6});create();move({2,0,0});
    EXPECT_TRUE(moved.stepped);EXPECT_TRUE(moved.grounded);EXPECT_NEAR(moved.pose.position.x,2,1e-5);EXPECT_NEAR(moved.pose.position.y,1.201,1e-5);
    colliders[2].size.y=.8;bodies[2].pose.position.y=.4;
    ASSERT_TRUE(physics_reconfigure(world,&desc,&error));PhysicsCommand edit={};edit.kind=PHYSICS_TELEPORT;edit.body=3;edit.pose=bodies[2].pose;
    ASSERT_TRUE(physics_apply(world,&edit,1,&error));move({2,0,0});EXPECT_FALSE(moved.stepped);EXPECT_LT(moved.pose.position.x,1);EXPECT_NEAR(moved.pose.position.y,1.001,1e-5);
}
TEST_F(PhysicsCharacterTest, FloorSnapDescendsPastASteepStairLipWithinItsDropBudget) {
    PhysicsVec vertices[] = {{-2, .25, -2}, {-2, .25, 2}, {0, .25, 2}, {0, .25, -2}, {0, 0, -2}, {0, 0, 2}};
    uint32_t indices[] = {0, 1, 2, 0, 2, 3, 3, 2, 5, 3, 5, 4};
    bodies[1].pose.position = {0, .760002, 0};
    colliders[1].radius = .2;
    colliders[1].height = .6;
    movement.skin = .01;
    for (unsigned mesh = 0; mesh < 2; mesh++) {
        wall(2, mesh ? PhysicsVec{} : PhysicsVec{-1, .125, 0}, {2, .25, 4});
        colliders[2].shape = mesh ? PHYSICS_MESH : PHYSICS_BOX;
        colliders[2].density = 0;
        colliders[2].vertices = vertices;
        colliders[2].vertex_count = 6;
        colliders[2].indices = indices;
        colliders[2].index_count = 12;
        create();
        // the first downward contact is the rounded, nonwalkable lip, above the reachable lower floor.
        movement.snap_distance = .3;
        move({.15, 0, 0});
        EXPECT_TRUE(moved.grounded) << mesh;
        EXPECT_GE(moved.pose.position.x, .21 - 4e-6) << mesh;
        EXPECT_NEAR(moved.pose.position.y, .510002, 4e-6) << mesh;
        EXPECT_LE(.760002 - moved.pose.position.y, movement.snap_distance + 4e-6);
        movement.snap_distance = .1;
        move({.15, 0, 0});
        EXPECT_FALSE(moved.grounded);
        EXPECT_NEAR(moved.pose.position.x, .15, 1e-12);
        EXPECT_NEAR(moved.pose.position.y, .760002, 1e-12);
        physics_destroy(world);
        world = nullptr;
    }
}
TEST_F(PhysicsCharacterTest, LowCeilingRejectsStepButRetainsOrdinaryMovement) {
    wall(2,{2,.1,0},{2,.2,6});wall(3,{0,2.1,0},{20,.1,20});create();move({2,0,0});
    EXPECT_FALSE(moved.stepped);EXPECT_LT(moved.pose.position.x,1);EXPECT_TRUE(moved.grounded);
}
TEST_F(PhysicsCharacterTest, WalkableSlopeClimbsAndSteepSlopeBlocksHorizontalIntent) {
    bodies[0].pose.position={0,-.5,0};bodies[0].pose.rotation=peuler({0,0,.3});colliders[0].size={20,1,20};
    bodies[1].pose.position={0,2,0};movement.snap_distance=2;create();move({2,0,0});
    EXPECT_TRUE(moved.grounded);EXPECT_NEAR(moved.ground_normal.y,cos(.3),1e-7);EXPECT_GT(moved.pose.position.y,1.5);
    physics_destroy(world);world=nullptr;bodies[0].pose.rotation=peuler({0,0,1.1});bodies[1].pose.position={-2,1.001,0};
    movement.snap_distance=.1;create();move({5,0,0});EXPECT_TRUE(moved.wall);EXPECT_FALSE(moved.grounded);EXPECT_NEAR(moved.pose.position.y,1.001,1e-5);
}
TEST_F(PhysicsCharacterTest, PlatformSnapshotsCarryAchievedTranslationAndOrbit) {
    create();move({0,0,0});movement.platform=moved.platform;movement.platform_pose=moved.platform_pose;
    PhysicsCommand command={};command.kind=PHYSICS_TELEPORT;command.body=1;command.pose={{1,-.5,0},{0,0,0,1}};
    ASSERT_TRUE(physics_apply(world,&command,1,&error));move({0,0,0});EXPECT_NEAR(moved.platform_motion.x,1,1e-12);
    EXPECT_NEAR(moved.pose.position.x,1,1e-12);EXPECT_TRUE(moved.grounded);EXPECT_NEAR(moved.platform_velocity.x,120,1e-9);
    movement.platform_pose=command.pose;command.pose.rotation=peuler({0,acos(-1.0)/2,0});ASSERT_TRUE(physics_apply(world,&command,1,&error));
    move({0,0,0});EXPECT_NEAR(moved.pose.position.x,1,1e-10);EXPECT_NEAR(moved.pose.position.z,1,1e-10);
}
TEST_F(PhysicsCharacterTest, MovingPlatformCarryIsSweptAgainstAThinWall) {
    wall(2,{2,2,0},{.02,4,20});create();move({0,0,0});movement.platform=moved.platform;movement.platform_pose=moved.platform_pose;
    PhysicsCommand command={};command.kind=PHYSICS_TELEPORT;command.body=1;command.pose={{5,-.5,0},{0,0,0,1}};
    ASSERT_TRUE(physics_apply(world,&command,1,&error));move({0,0,0});EXPECT_NEAR(moved.pose.position.x,1.489,1e-5);EXPECT_TRUE(moved.wall);
}
TEST_F(PhysicsCharacterTest, MeshAndHeightfieldFloorsUseTheSharedCapsuleKernel) {
    PhysicsVec vertices[]={{-10,0,-10},{-10,0,10},{10,0,-10},{10,0,10}};uint32_t indices[]={0,1,2,2,1,3};double heights[]={0,0,0,0};
    for(unsigned kind=0;kind<2;kind++) {
        bodies[0].pose.position={0,0,0};colliders[0].shape=kind?PHYSICS_HEIGHTFIELD:PHYSICS_MESH;colliders[0].density=0;
        colliders[0].vertices=vertices;colliders[0].vertex_count=4;colliders[0].indices=indices;colliders[0].index_count=6;
        colliders[0].rows=colliders[0].columns=2;colliders[0].heights=heights;colliders[0].cell_width=colliders[0].cell_depth=20;
        if(kind)bodies[0].pose.position={-10,0,-10};create();move({2,-3,1});
        EXPECT_TRUE(moved.grounded);EXPECT_NEAR(moved.pose.position.y,1.001,1e-5);EXPECT_NEAR(moved.pose.position.x,2,1e-5);
        physics_destroy(world);world=nullptr;
    }
}
TEST_F(PhysicsCharacterTest, MovementAndContactsAreRigidTransformInvariant) {
    wall(2,{2,2,0},{.02,4,20});create();move({4,-1,3});PhysicsCharacterState expected=moved;physics_destroy(world);world=nullptr;
    PhysicsQuat rotation=peuler({.3,-.4,.2});PhysicsVec shift={10,-3,20};
    for(size_t i=0;i<desc.body_count;i++){bodies[i].pose.position=padd(shift,prot(rotation,bodies[i].pose.position));bodies[i].pose.rotation=pqmul(rotation,bodies[i].pose.rotation);}
    movement.up=prot(rotation,{0,1,0});create();move(prot(rotation,{4,-1,3}));
    EXPECT_LT(plen(psub(moved.pose.position,padd(shift,prot(rotation,expected.pose.position)))),1e-5);
    EXPECT_LT(plen(psub(moved.ground_normal,prot(rotation,expected.ground_normal))),1e-6);EXPECT_EQ(moved.grounded,expected.grounded);
}
TEST_F(PhysicsCharacterTest, WalkingOffAStairSnapsDownWithinTheDeclaredDistance) {
    wall(2,{2,.1,0},{2,.2,6});bodies[1].pose.position={2,1.201,0};movement.snap_distance=.3;create();move({0,0,4});
    EXPECT_TRUE(moved.grounded);EXPECT_EQ(moved.platform,10u);EXPECT_NEAR(moved.pose.position.y,1.001,1e-5);EXPECT_NEAR(moved.pose.position.z,4,1e-5);
}
TEST_F(PhysicsCharacterTest, InvalidRequestsAndSlideExhaustionRetainTheResultAndWorld) {
    wall(2,{2,2,0},{.02,4,20});create();moved.pose.position={42,43,44};PhysicsCharacterState before=moved;
    PhysicsCharacterMove invalid[4]={movement,movement,movement,movement};
    invalid[0].up={};invalid[1].skin=0;invalid[2].max_slides=33;invalid[3].body=999;
    for(const auto& request:invalid){EXPECT_FALSE(physics_character_move(world,&request,&moved,&error));EXPECT_EQ(memcmp(&before,&moved,sizeof(before)),0);}
    movement.max_slides=1;movement.displacement={100,0,3};EXPECT_FALSE(physics_character_move(world,&movement,&moved,&error));
    EXPECT_NE(strstr(error.message,"slide budget"),nullptr);EXPECT_EQ(memcmp(&before,&moved,sizeof(before)),0);
    ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_EQ(state.tick,0u);EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.x,0);
}
TEST_F(PhysicsCharacterTest, SensorsAndCollisionGroupsRemainQueryFilters) {
    wall(2,{2,2,0},{.02,4,20});colliders[2].sensor=true;create();move({4,0,0});EXPECT_NEAR(moved.pose.position.x,4,1e-12);EXPECT_FALSE(moved.wall);
    colliders[2].sensor=false;colliders[2].membership=2;colliders[1].filter=1;
    ASSERT_TRUE(physics_reconfigure(world,&desc,&error));move({4,0,0});EXPECT_NEAR(moved.pose.position.x,4,1e-12);EXPECT_TRUE(moved.grounded);
}
TEST_F(PhysicsCharacterTest, AggregateTriangleWorkIncludesRecoveryAndCasts) {
    double heights[65*65]={};colliders[0].shape=PHYSICS_HEIGHTFIELD;colliders[0].density=0;colliders[0].rows=colliders[0].columns=65;
    colliders[0].heights=heights;colliders[0].cell_width=colliders[0].cell_depth=1;bodies[0].pose.position={-32,0,-32};
    colliders[1].radius=100;colliders[1].height=0;bodies[1].pose.position={0,100.001,0};create();
    moved.pose.position={42,43,44};PhysicsCharacterState before=moved;
    EXPECT_FALSE(physics_character_move(world,&movement,&moved,&error));EXPECT_NE(strstr(error.message,"work quota"),nullptr);
    EXPECT_EQ(memcmp(&before,&moved,sizeof(before)),0);ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_EQ(state.tick,0u);
}

struct PhysicsRoleTransition : PhysicsTest {};
TEST_F(PhysicsRoleTransition, ActivationKeepsTheClockPoseAndMeasuredKinematicVelocity) {
    bodies[1].motion=PHYSICS_KINEMATIC_POSITION;bodies[1].pose.position={0,5,0};create();
    PhysicsCommand target={};target.kind=PHYSICS_TARGET;target.body=2;target.pose={{1,5,0},peuler({0,.1,0})};
    ASSERT_TRUE(physics_apply(world,&target,1,&error));step(1);PhysicsBodyState animated=state.bodies[1];
    bodies[1].motion=PHYSICS_DYNAMIC;ASSERT_TRUE(physics_reconfigure(world,&desc,&error))<<(error.message?error.message:"");
    ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_EQ(state.tick,1u);
    EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.x,animated.pose.position.x);EXPECT_DOUBLE_EQ(state.bodies[1].pose.rotation.y,animated.pose.rotation.y);
    EXPECT_NEAR(state.bodies[1].velocity.x,120,1e-10);EXPECT_NEAR(state.bodies[1].angular_velocity.y,12,1e-10);
    step(1);EXPECT_EQ(state.tick,2u);EXPECT_NEAR(state.bodies[1].pose.position.x,2,1e-10);
    EXPECT_NEAR(state.bodies[1].pose.position.y,5+desc.gravity.y*desc.timestep*desc.timestep,1e-10);
}
TEST_F(PhysicsRoleTransition, RoleChangesDiscardOldTargetsAndEffortAndFixedMotionStops) {
    desc.gravity={};bodies[1].motion=PHYSICS_KINEMATIC_POSITION;bodies[1].pose.position={0,5,0};create();
    PhysicsCommand target={};target.kind=PHYSICS_TARGET;target.body=2;target.pose={{50,5,0},{0,0,0,1}};ASSERT_TRUE(physics_apply(world,&target,1,&error));
    bodies[1].motion=PHYSICS_DYNAMIC;ASSERT_TRUE(physics_reconfigure(world,&desc,&error));step(1);EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.x,0);
    PhysicsCommand commands[3]={};commands[0].kind=PHYSICS_FORCE;commands[1].kind=PHYSICS_TORQUE;commands[2].kind=PHYSICS_VELOCITY;
    for(auto& command:commands){command.body=2;command.value={120,0,0};}ASSERT_TRUE(physics_apply(world,commands,3,&error));
    bodies[1].motion=PHYSICS_FIXED;ASSERT_TRUE(physics_reconfigure(world,&desc,&error));step(1);
    EXPECT_DOUBLE_EQ(plen(state.bodies[1].velocity),0);EXPECT_DOUBLE_EQ(plen(state.bodies[1].angular_velocity),0);
    bodies[1].motion=PHYSICS_DYNAMIC;ASSERT_TRUE(physics_reconfigure(world,&desc,&error));step(1);
    EXPECT_DOUBLE_EQ(plen(state.bodies[1].velocity),0);EXPECT_DOUBLE_EQ(plen(state.bodies[1].angular_velocity),0);
    bodies[1].motion=PHYSICS_KINEMATIC_POSITION;ASSERT_TRUE(physics_reconfigure(world,&desc,&error));step(1);
    EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.x,0);
}
TEST_F(PhysicsRoleTransition, RoleAndCommandRejectionRetainsTheOldBorrowedViewsAndInertia) {
    desc.gravity={};bodies[1].motion=PHYSICS_KINEMATIC_POSITION;bodies[1].pose.position={0,5,0};colliders[1].shape=PHYSICS_SPHERE;create();
    ASSERT_TRUE(physics_read(world,&state,&error));const PhysicsBodyState* borrowed=state.bodies;PhysicsBodyState previous=borrowed[1];
    bodies[1].motion=PHYSICS_DYNAMIC;PhysicsCommand invalid={};invalid.kind=PHYSICS_TARGET;invalid.body=2;invalid.pose={{1,5,0},{0,0,0,1}};
    EXPECT_FALSE(physics_reconfigure_commands(world,&desc,&invalid,1,&error));ASSERT_TRUE(physics_read(world,&state,&error));
    EXPECT_EQ(state.bodies,borrowed);EXPECT_DOUBLE_EQ(borrowed[1].pose.position.y,previous.pose.position.y);EXPECT_EQ(state.tick,0u);
    ASSERT_TRUE(physics_apply(world,&invalid,1,&error));step(1);EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.x,1);
    PhysicsCommand activate[2]={};activate[0].kind=PHYSICS_TELEPORT;activate[0].body=2;activate[0].pose={{2,6,0},{0,0,0,1}};
    activate[1].kind=PHYSICS_VELOCITY;activate[1].body=2;activate[1].value={1,2,3};
    ASSERT_TRUE(physics_reconfigure_commands(world,&desc,activate,2,&error));
    PhysicsCommand torque={};torque.kind=PHYSICS_IMPULSE;torque.body=2;torque.value={0,1,0};torque.point={3,6,0};torque.at_point=true;
    ASSERT_TRUE(physics_apply(world,&torque,1,&error));ASSERT_TRUE(physics_read(world,&state,&error));
    EXPECT_EQ(state.tick,1u);EXPECT_DOUBLE_EQ(state.bodies[1].pose.position.x,2);EXPECT_DOUBLE_EQ(state.bodies[1].velocity.x,1);
    EXPECT_NEAR(state.bodies[1].angular_velocity.z,1/(.4*state.bodies[1].mass*.25),1e-10);
}
TEST_F(PhysicsRoleTransition, InactiveKinematicJointsActivateAndRejectInvalidWholeTransitions) {
    bodies[0].motion=bodies[1].motion=PHYSICS_KINEMATIC_POSITION;bodies[0].pose.position={0,5,0};bodies[1].pose.position={0,3,0};
    PhysicsJointDesc joint={};joint.id=100;joint.kind=PHYSICS_JOINT_DISTANCE;joint.body_a=1;joint.body_b=2;joint.length=2;
    joint.frame_a.rotation.w=joint.frame_b.rotation.w=1;joint.disabled=true;desc.joints=&joint;desc.joint_count=1;create();
    ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_FALSE(state.joints[0].enabled);
    bodies[1].motion=PHYSICS_DYNAMIC;joint.disabled=false;ASSERT_TRUE(physics_reconfigure(world,&desc,&error));step(10);
    const PhysicsBodyState* borrowed=state.bodies;const PhysicsJointState* constraints=state.joints;double y=borrowed[1].pose.position.y;
    bodies[1].motion=PHYSICS_KINEMATIC_POSITION;EXPECT_FALSE(physics_reconfigure(world,&desc,&error));ASSERT_TRUE(physics_read(world,&state,&error));
    EXPECT_EQ(state.bodies,borrowed);EXPECT_EQ(state.joints,constraints);EXPECT_DOUBLE_EQ(borrowed[1].pose.position.y,y);EXPECT_TRUE(constraints[0].enabled);
    joint.disabled=true;ASSERT_TRUE(physics_reconfigure(world,&desc,&error));step(1);EXPECT_EQ(state.tick,11u);EXPECT_FALSE(state.joints[0].enabled);
}
TEST_F(PhysicsRoleTransition, GeometryAndMotionUseTheSameStagedDescription) {
    desc.gravity={};desc.body_count=desc.collider_count=1;bodies[0].motion=PHYSICS_DYNAMIC;bodies[0].pose.position={0,5,0};colliders[0].shape=PHYSICS_SPHERE;create();
    PhysicsVec vertices[]={{-2,0,-2},{-2,0,2},{2,0,-2},{2,0,2}};uint32_t indices[]={0,1,2,2,1,3};
    bodies[0].motion=PHYSICS_FIXED;colliders[0].shape=PHYSICS_MESH;colliders[0].density=0;
    colliders[0].vertices=vertices;colliders[0].vertex_count=4;colliders[0].indices=indices;colliders[0].index_count=6;
    ASSERT_TRUE(physics_reconfigure(world,&desc,&error))<<(error.message?error.message:"");
    bodies[0].motion=PHYSICS_DYNAMIC;EXPECT_FALSE(physics_reconfigure(world,&desc,&error));step(1);
    PhysicsQuery query={};query.kind=PHYSICS_RAY;query.origin={0,10,0};query.direction={0,-1,0};query.distance=20;query.membership=query.filter=UINT32_MAX;
    PhysicsHit hit={};ASSERT_TRUE(physics_query(world,&query,1,&hit,&error));EXPECT_TRUE(hit.found);EXPECT_NEAR(hit.point.y,5,1e-12);
}
TEST_F(PhysicsRoleTransition, UnchangedRolesKeepPendingForces) {
    desc.gravity={};bodies[1].pose.position={0,5,0};create();PhysicsCommand force={};force.kind=PHYSICS_FORCE;force.body=2;force.value={120,0,0};
    ASSERT_TRUE(physics_apply(world,&force,1,&error));ASSERT_TRUE(physics_reconfigure(world,&desc,&error));step(1);
    EXPECT_NEAR(state.bodies[1].velocity.x,120*desc.timestep/state.bodies[1].mass,1e-12);
}
TEST_F(PhysicsRoleTransition, FixedVelocityCommandRejectsTheWholeTransition) {
    desc.gravity={};bodies[1].pose.position={0,5,0};create();step(1);const PhysicsBodyState* borrowed=state.bodies;
    bodies[1].motion=PHYSICS_FIXED;PhysicsCommand commands[2]={};commands[0].kind=PHYSICS_GRAVITY;commands[0].value={0,-10,0};
    commands[1].kind=PHYSICS_VELOCITY;commands[1].body=2;commands[1].value={1,0,0};
    EXPECT_FALSE(physics_reconfigure_commands(world,&desc,commands,2,&error));ASSERT_TRUE(physics_read(world,&state,&error));EXPECT_EQ(state.bodies,borrowed);
    PhysicsCommand force={};force.kind=PHYSICS_IMPULSE;force.body=2;force.value={1,0,0};ASSERT_TRUE(physics_apply(world,&force,1,&error));
    step(1);EXPECT_EQ(state.tick,2u);EXPECT_NEAR(state.bodies[1].velocity.x,1,1e-12);EXPECT_DOUBLE_EQ(state.bodies[1].velocity.y,0);
}
TEST_F(PhysicsRoleTransition, OffsetKinematicMassTransfersCentreVelocityIntoDynamics) {
    desc.body_count=desc.collider_count=1;desc.gravity={};desc.timestep=.1;bodies[0].motion=PHYSICS_KINEMATIC_POSITION;
    bodies[0].pose.position={};colliders[0].size={1,1,1};colliders[0].pose.position={1,0,0};create();
    PhysicsCommand command={};command.kind=PHYSICS_TARGET;command.body=1;command.pose={{0,0,0},peuler({0,0,acos(-1.0)/2})};
    ASSERT_TRUE(physics_apply(world,&command,1,&error));step(1);
    EXPECT_NEAR(state.bodies[0].velocity.x,-10,1e-12);EXPECT_NEAR(state.bodies[0].velocity.y,10,1e-12);
    EXPECT_NEAR(state.bodies[0].centre_of_mass.x,0,1e-12);EXPECT_NEAR(state.bodies[0].centre_of_mass.y,1,1e-12);
    EXPECT_NEAR(plen(state.bodies[0].pose.position),0,1e-12);
    bodies[0].motion=PHYSICS_DYNAMIC;ASSERT_TRUE(physics_reconfigure(world,&desc,&error));step(1);
    EXPECT_NEAR(state.bodies[0].centre_of_mass.x,-1,1e-12);EXPECT_NEAR(state.bodies[0].centre_of_mass.y,2,1e-12);
}

TEST_F(PhysicsCharacterTest, RepeatedGroundedDiagonalTargetsRetainTheirRecoverySeparation) {
    bodies[0].pose.position={4,-.5,4};bodies[1].pose.position={1.5,.51,1.5};
    colliders[1].radius=.2;colliders[1].height=.6;movement.skin=.01;create();
    for(unsigned tick=0;tick<20;tick++) {
        PhysicsCharacterProfile profile={};ASSERT_TRUE(physics_character_profile(world,2,&profile,&error));
        // a grounded navigation request asks for the same floor rather than accumulating the shell margin.
        movement.displacement={.1,.51-profile.pose.position.y,.1};
        ASSERT_TRUE(physics_character_move(world,&movement,&moved,&error))<<tick<<": "<<(error.message?error.message:"");
        EXPECT_NEAR(moved.pose.position.x,1.5+.1*(tick+1),1e-8);EXPECT_NEAR(moved.pose.position.z,1.5+.1*(tick+1),1e-8);
        PhysicsCommand target={};target.kind=PHYSICS_TARGET;target.body=2;target.pose=moved.pose;
        ASSERT_TRUE(physics_apply(world,&target,1,&error));step(1);
        movement.platform=moved.platform;movement.platform_pose=moved.platform_pose;
    }
}

TEST_F(PhysicsCharacterTest, DescriptorAdmissionAndSharedParametersPreserveTheLiveWorld) {
    desc.gravity = {};
    create();
    ASSERT_TRUE(physics_character_validate_source(&desc, 2, &error));
    bodies[1].motion = PHYSICS_DYNAMIC;
    EXPECT_FALSE(physics_character_validate_source(&desc, 2, &error));
    PhysicsCharacterProfile profile = {};
    EXPECT_TRUE(physics_character_profile(world, 2, &profile, &error));
    bodies[1].motion = PHYSICS_KINEMATIC_POSITION;
    colliders[2] = colliders[1];
    colliders[2].id = 12;
    desc.collider_count = 3;
    EXPECT_FALSE(physics_character_validate_source(&desc, 2, &error));
    EXPECT_TRUE(physics_character_profile(world, 2, &profile, &error));
    struct Source {
        double slides;
        static bool real(void* owner, const char* key, double fallback, double* value) {
            *value = !strcmp(key, "skin") ? .05 : !strcmp(key, "max_slides") ? ((Source*)owner)->slides : fallback;
            return true;
        }
        static bool vector(void*, const char*, PhysicsVec fallback, PhysicsVec* value) {
            *value = fallback;
            return true;
        }
    } source = {1.5};
    PhysicsCharacterParameterSource fields = {&source, Source::real, Source::vector};
    double skin = movement.skin;
    EXPECT_FALSE(physics_character_read_parameters(&fields, &movement));
    EXPECT_DOUBLE_EQ(movement.skin, skin);
    source.slides = 2;
    ASSERT_TRUE(physics_character_read_parameters(&fields, &movement));
    EXPECT_DOUBLE_EQ(movement.skin, .05);
    EXPECT_EQ(movement.max_slides, 2u);
    EXPECT_TRUE(physics_character_parameters_valid(&movement));
    movement.skin = 0;
    EXPECT_FALSE(physics_character_parameters_valid(&movement));
}
