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
