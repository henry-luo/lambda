#include <gtest/gtest.h>
#include "../lib/navigation.h"
#include <math.h>
#include <float.h>

struct NavigationTest : testing::Test {
    NavigationGraph* graph=nullptr;NavigationError error={};NavigationPath path={};
    NavigationNodeDesc nodes[16]={};NavigationEdgeDesc edges[256]={};NavigationDesc desc={2,nodes,0,edges,0};
    void TearDown()override{navigation_path_clear(&path);navigation_destroy(graph);}
    void node(uint64_t id,double x,double y=0,uint32_t membership=UINT32_MAX){nodes[desc.node_count++]={id,{x,y,0},membership,false};}
    void edge(uint64_t from,uint64_t to,double cost,uint32_t membership=UINT32_MAX){auto& e=edges[desc.edge_count++];e.from=from;e.to=to;e.cost=cost;e.membership=membership;}
    void create(){ASSERT_TRUE(navigation_create(&desc,&graph,&error))<<(error.message?error.message:"");}
    NavigationQuery query(uint64_t start,uint64_t goal){return {start,goal,UINT32_MAX,NAVIGATION_MAX_NODES,NAVIGATION_MAX_EDGES,true,false};}
    void find(NavigationQuery q){navigation_path_clear(&path);ASSERT_TRUE(navigation_find(graph,&q,&path,&error))<<(error.message?error.message:"");}
};
TEST_F(NavigationTest, WeightedDetourUsesAnAdmissibleMetricAndCopiesItsSources) {
    node(1,0);node(2,1);node(3,1000);node(4,2);edge(1,2,1);edge(2,4,20);edge(1,3,.1);edge(3,4,0);create();
    edges[2].cost=100;nodes[2].position.x=-1000;find(query(1,4));
    ASSERT_EQ(path.status,NAVIGATION_COMPLETE);ASSERT_EQ(path.count,3u);EXPECT_EQ(path.nodes[1],3u);EXPECT_NEAR(path.cost,.1,1e-15);
    NavigationNodeDesc copied={};ASSERT_TRUE(navigation_node(graph,3,&copied));EXPECT_DOUBLE_EQ(copied.position.x,1000);
}
TEST_F(NavigationTest, FractionalDecreaseKeysChooseTheLowerCostRoute) {
    node(1,0);node(2,1);node(3,2);node(4,3);edge(1,2,10);edge(1,3,1);edge(3,2,.00001);edge(2,4,.1);edge(3,4,2);create();
    auto q=query(1,4);q.euclidean=false;find(q);ASSERT_EQ(path.count,4u);EXPECT_EQ(path.nodes[1],3u);EXPECT_EQ(path.nodes[2],2u);EXPECT_NEAR(path.cost,1.10001,1e-14);
}
TEST_F(NavigationTest, EqualCostTiesIgnoreSourceOrdering) {
    node(1,0);node(3,1,1);node(2,1,-1);node(4,2);edge(1,3,1);edge(3,4,1);edge(1,2,1);edge(2,4,1);create();
    find(query(1,4));ASSERT_EQ(path.count,3u);EXPECT_EQ(path.nodes[1],2u);
    navigation_destroy(graph);graph=nullptr;
    for(size_t i=0;i<2;i++){auto n=nodes[i];nodes[i]=nodes[3-i];nodes[3-i]=n;auto e=edges[i];edges[i]=edges[3-i];edges[3-i]=e;}
    create();find(query(1,4));ASSERT_EQ(path.count,3u);EXPECT_EQ(path.nodes[1],2u);
}
TEST_F(NavigationTest, FiltersClearanceAndBudgetsHaveSeparateOutcomes) {
    node(1,0);node(2,1);node(3,2);node(4,3,0,2);edge(1,2,1);edge(2,3,1,2);create();
    auto q=query(1,3);q.filter=1;find(q);EXPECT_EQ(path.status,NAVIGATION_UNREACHABLE);EXPECT_EQ(path.count,0u);
    q.allow_partial=true;find(q);EXPECT_EQ(path.status,NAVIGATION_PARTIAL);ASSERT_EQ(path.count,2u);EXPECT_EQ(path.nodes[1],2u);
    q.filter=UINT32_MAX;q.max_expansions=1;find(q);EXPECT_EQ(path.status,NAVIGATION_EXHAUSTED);EXPECT_EQ(path.expanded,1u);
    q.allow_partial=false;q.max_expansions=100;q.max_edge_tests=0;find(q);EXPECT_EQ(path.status,NAVIGATION_EXHAUSTED);EXPECT_EQ(path.edge_tests,0u);EXPECT_EQ(path.count,0u);
    navigation_destroy(graph);graph=nullptr;edges[1].guards[0]=4;edges[1].guard_count=1;create();q=query(1,3);q.filter=1;find(q);EXPECT_EQ(path.status,NAVIGATION_UNREACHABLE);
}
TEST_F(NavigationTest, ErrorsPreserveResultAndInvalidDescriptionsRejectBeforePublication) {
    node(1,0);node(2,1);edge(1,2,1);create();auto q=query(1,99);path.cost=123;path.status=NAVIGATION_PARTIAL;
    EXPECT_FALSE(navigation_find(graph,&q,&path,&error));EXPECT_DOUBLE_EQ(path.cost,123);EXPECT_EQ(path.status,NAVIGATION_PARTIAL);
    NavigationGraph* rejected=nullptr;edges[0].cost=-1;EXPECT_FALSE(navigation_create(&desc,&rejected,&error));EXPECT_EQ(rejected,nullptr);
    edges[0].cost=NAN;EXPECT_FALSE(navigation_create(&desc,&rejected,&error));edges[0].cost=1;nodes[1].id=1;EXPECT_FALSE(navigation_create(&desc,&rejected,&error));
    nodes[1].id=2;edges[0].to=99;EXPECT_FALSE(navigation_create(&desc,&rejected,&error));edges[0].to=2;desc.node_count=NAVIGATION_MAX_NODES+1;EXPECT_FALSE(navigation_create(&desc,&rejected,&error));
}
TEST_F(NavigationTest, OverflowIsAnErrorRatherThanAnUnreachablePath) {
    node(1,0);node(2,1);node(3,2);edge(1,2,1e308);edge(2,3,1e308);create();auto q=query(1,3);q.euclidean=false;
    EXPECT_FALSE(navigation_find(graph,&q,&path,&error));EXPECT_NE(strstr(error.message,"path cost"),nullptr);EXPECT_EQ(path.nodes,nullptr);
}
TEST_F(NavigationTest, SeededGraphsAgreeWithIndependentBellmanFord) {
    uint32_t seed=0xa51c45d3;
    auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
    for(unsigned trial=0;trial<180;trial++) {
        desc.node_count=desc.edge_count=0;
        for(unsigned i=0;i<8;i++)node(i+1,(random()%100)*.1,(random()%100)*.1);
        for(unsigned i=0;i<8;i++)for(unsigned j=0;j<8;j++)if(i!=j&&random()%3)edge(i+1,j+1,(random()%100)*.013);
        create();double distance[8];for(auto& d:distance)d=INFINITY;distance[0]=0;
        // independent edge relaxation needs neither a frontier heap nor geometric heuristic.
        for(unsigned iteration=0;iteration<7;iteration++)for(size_t i=0;i<desc.edge_count;i++) {
            const auto& e=edges[i];distance[e.to-1]=fmin(distance[e.to-1],distance[e.from-1]+e.cost);
        }
        for(unsigned mode=0;mode<2;mode++) {
            auto q=query(1,8);q.euclidean=mode!=0;find(q);
            if(isfinite(distance[7])){EXPECT_EQ(path.status,NAVIGATION_COMPLETE)<<trial;EXPECT_NEAR(path.cost,distance[7],1e-11)<<trial;}
            else EXPECT_EQ(path.status,NAVIGATION_UNREACHABLE)<<trial;
        }
        navigation_destroy(graph);graph=nullptr;
    }
}
TEST_F(NavigationTest, TwoDimensionalGridRoutesAroundAWallAndRejectsCornerCuts) {
    bool blocked[25]={};for(unsigned y=0;y<4;y++)blocked[2+5*y]=true;
    NavigationGridDesc grid={2,{5,5,1},{0,0,0},{1,1,1},nullptr,nullptr,blocked,false};
    ASSERT_TRUE(navigation_create_grid(&grid,&graph,&error));find(query(1,5));EXPECT_EQ(path.status,NAVIGATION_COMPLETE);EXPECT_DOUBLE_EQ(path.cost,12);EXPECT_EQ(path.count,13u);
    navigation_destroy(graph);graph=nullptr;grid.size[0]=grid.size[1]=2;grid.diagonals=true;blocked[1]=blocked[2]=true;
    ASSERT_TRUE(navigation_create_grid(&grid,&graph,&error));find(query(1,4));EXPECT_EQ(path.status,NAVIGATION_UNREACHABLE);
    navigation_destroy(graph);graph=nullptr;blocked[1]=false;ASSERT_TRUE(navigation_create_grid(&grid,&graph,&error));find(query(1,4));EXPECT_DOUBLE_EQ(path.cost,2);
}
TEST_F(NavigationTest, ThreeDimensionalDiagonalsCheckAllIntermediateCells) {
    bool blocked[8]={};NavigationGridDesc grid={3,{2,2,2},{4,5,6},{1,1,1},nullptr,nullptr,blocked,true};
    ASSERT_TRUE(navigation_create_grid(&grid,&graph,&error));find(query(1,8));EXPECT_NEAR(path.cost,sqrt(3),1e-14);
    NavigationNodeDesc goal={};ASSERT_TRUE(navigation_node(graph,8,&goal));EXPECT_DOUBLE_EQ(goal.position.x,5);EXPECT_DOUBLE_EQ(goal.position.y,6);EXPECT_DOUBLE_EQ(goal.position.z,7);
    navigation_destroy(graph);graph=nullptr;blocked[1]=true;ASSERT_TRUE(navigation_create_grid(&grid,&graph,&error));find(query(1,8));EXPECT_NEAR(path.cost,1+sqrt(2),1e-14);
}
TEST_F(NavigationTest, DiagonalClearanceRespectsAgentFilters) {
    uint32_t membership[4]={1,2,1,1};NavigationGridDesc grid={2,{2,2,1},{},{1,1,1},nullptr,membership,nullptr,true};
    ASSERT_TRUE(navigation_create_grid(&grid,&graph,&error));auto q=query(1,4);q.filter=1;find(q);EXPECT_DOUBLE_EQ(path.cost,2);
    q.filter=3;find(q);EXPECT_NEAR(path.cost,sqrt(2),1e-14);
}
TEST_F(NavigationTest, ZeroCostsStayValidAndGridQuotasRejectWithoutAccessingBuffers) {
    double costs[4]={};NavigationGridDesc grid={2,{2,2,1},{},{1,1,1},costs,nullptr,nullptr,true};
    ASSERT_TRUE(navigation_create_grid(&grid,&graph,&error));find(query(1,4));EXPECT_DOUBLE_EQ(path.cost,0);
    NavigationGraph* rejected=nullptr;costs[0]=-1;EXPECT_FALSE(navigation_create_grid(&grid,&rejected,&error));costs[0]=0;
    grid.size[0]=NAVIGATION_MAX_NODES;grid.size[1]=2;EXPECT_FALSE(navigation_create_grid(&grid,&rejected,&error));EXPECT_EQ(rejected,nullptr);
}
TEST_F(NavigationTest, AlreadyReachedAndBlockedEndpointsNeedNoSearchBudget) {
    node(1,0);node(2,1,0,2);create();auto q=query(1,1);q.max_expansions=q.max_edge_tests=0;find(q);
    EXPECT_EQ(path.status,NAVIGATION_COMPLETE);ASSERT_EQ(path.count,1u);EXPECT_EQ(path.nodes[0],1u);EXPECT_EQ(path.expanded,0u);
    q.goal=2;q.filter=1;find(q);EXPECT_EQ(path.status,NAVIGATION_UNREACHABLE);EXPECT_EQ(path.count,0u);EXPECT_EQ(path.expanded,0u);
}

#include "../lib/arraylist.hpp"
#include "../lib/math_utils.h"

static NavigationMeshQuery world_query(NavigationVec start,NavigationVec goal) {
    return {start,goal,{0,0,UINT32_MAX,NAVIGATION_MAX_NODES,NAVIGATION_MAX_EDGES,true,false},1000,2*NAVIGATION_MAX_NODES,16*NAVIGATION_MAX_EDGES};
}
struct NavigationMeshTest : testing::Test {
    lam::ArrayList<NavigationVec> vertices;
    lam::ArrayList<NavigationTriangleDesc> triangles;
    NavigationMesh* mesh=nullptr;NavigationMeshPath path={};NavigationError error={};
    unsigned dimensions=2;double slope=acos(-1.0)/3;bool two_sided=false;
    void TearDown()override{navigation_mesh_path_clear(&path);navigation_mesh_destroy(mesh);}
    void vertex(double x,double y,double z=0){ASSERT_TRUE(vertices.append({x,y,z}));}
    void triangle(uint32_t a,uint32_t b,uint32_t c,double cost=1,uint32_t groups=UINT32_MAX,bool disabled=false){ASSERT_TRUE(triangles.append({{a,b,c},groups,cost,disabled}));}
    void cell(double x,double y,double cost=1,uint32_t groups=UINT32_MAX) {
        uint32_t base=(uint32_t)vertices.size();vertex(x,y);vertex(x+1,y);vertex(x+1,y+1);vertex(x,y+1);
        triangle(base,base+1,base+2,cost,groups);triangle(base,base+2,base+3,cost,groups);
    }
    NavigationMeshDesc description(){return {dimensions,vertices.data(),vertices.size(),triangles.data(),triangles.size(),slope,two_sided};}
    void create(){auto desc=description();ASSERT_TRUE(navigation_mesh_create(&desc,&mesh,&error))<<(error.message?error.message:"");}
    NavigationMeshQuery query(NavigationVec start,NavigationVec goal) {
        return world_query(start,goal);
    }
    void find(NavigationMeshQuery q){navigation_mesh_path_clear(&path);ASSERT_TRUE(navigation_mesh_find(mesh,&q,&path,&error))<<(error.message?error.message:"")<<" at "<<error.id;}
    void point(NavigationVec p,double x,double y,double z=0){EXPECT_NEAR(p.x,x,1e-12);EXPECT_NEAR(p.y,y,1e-12);EXPECT_NEAR(p.z,z,1e-12);}
};

TEST(NavigationGeometry, SharedClosestPointPreservesVerticesEdgesFacesAndScale) {
    for(double scale:{1e-100,1.0,1e100}) {
        double a[3]={},b[3]={2*scale,0,0},c[3]={0,2*scale,0},p[3]={.5*scale,.5*scale,3*scale},out[3]={};
        ASSERT_TRUE(math_triangle_closest(a,b,c,p,out));EXPECT_NEAR(out[0]/scale,.5,1e-14);EXPECT_NEAR(out[1]/scale,.5,1e-14);EXPECT_DOUBLE_EQ(out[2],0);
        p[0]=p[1]=2*scale;ASSERT_TRUE(math_triangle_closest(a,b,c,p,out));EXPECT_NEAR(out[0]/scale,1,1e-14);EXPECT_NEAR(out[1]/scale,1,1e-14);
        p[0]=-scale;p[1]=scale;ASSERT_TRUE(math_triangle_closest(a,b,c,p,out));EXPECT_NEAR(out[0]/scale,0,1e-14);EXPECT_NEAR(out[1]/scale,1,1e-14);
        p[0]=3*scale;p[1]=-scale;ASSERT_TRUE(math_triangle_closest(a,b,c,p,out));EXPECT_NEAR(out[0]/scale,2,1e-14);EXPECT_NEAR(out[1]/scale,0,1e-14);
    }
    double invalid[3]={},p[3]={1,2,3},retained[3]={4,5,6};EXPECT_FALSE(math_triangle_closest(invalid,invalid,invalid,p,retained));EXPECT_DOUBLE_EQ(retained[0],4);
}
TEST_F(NavigationMeshTest, WeldedPlanarSurfacesOwnTheirDataAndPullADirectPath) {
    cell(0,0);cell(1,0);create();auto q=query({.1,.2,0},{1.9,.8,0});find(q);
    ASSERT_EQ(path.corridor.status,NAVIGATION_COMPLETE);EXPECT_EQ(navigation_edge_count(navigation_mesh_graph(mesh)),6u);
    EXPECT_NEAR(path.length,hypot(1.8,.6),1e-12);EXPECT_NEAR(path.travel_cost,path.length,1e-12);
    ASSERT_GE(path.point_count,2u);point(path.points[0],.1,.2);point(path.points[path.point_count-1],1.9,.8);
    EXPECT_EQ(path.portal_count+1,path.corridor.count);double expected=path.length;
    vertices[0].x=99;triangles[0].disabled=true;triangles[0].cost=99;find(q);EXPECT_NEAR(path.length,expected,1e-12);EXPECT_NEAR(path.travel_cost,expected,1e-12);
}
TEST_F(NavigationMeshTest, EmptySurfacesAreValidUnreachableDomains) {
    create();EXPECT_EQ(navigation_mesh_vertex_count(mesh),0u);EXPECT_EQ(navigation_node_count(navigation_mesh_graph(mesh)),0u);
    auto q=query({0,0,0},{1,1,0});q.max_projection_tests=q.max_portal_tests=0;find(q);
    EXPECT_EQ(path.corridor.status,NAVIGATION_UNREACHABLE);EXPECT_EQ(path.point_count,0u);EXPECT_EQ(path.projection_tests,0u);
    NavigationMesh* other=nullptr;auto desc=description();ASSERT_TRUE(navigation_mesh_create(&desc,&other,&error));EXPECT_TRUE(navigation_mesh_equal(mesh,other));navigation_mesh_destroy(other);
    vertex(0,0);desc=description();EXPECT_FALSE(navigation_mesh_create(&desc,&other,&error));EXPECT_EQ(other,nullptr);
}

struct NavigationBakeTest : testing::Test {
    lam::ArrayList<NavigationBakeTriangle> triangles;
    NavigationBakeSettings settings={{0,-1,0},{7,6,7},1,.125,0,1.5,.375,acos(-1.0)/4,NAVIGATION_MAX_NODES,NAVIGATION_BAKE_MAX_FRAGMENTS,NAVIGATION_BAKE_MAX_WORK};
    NavigationBake* bake=nullptr;NavigationError error={};NavigationBakeStats stats={};NavigationPath path={};
    void TearDown()override{navigation_path_clear(&path);navigation_bake_destroy(bake);}
    void rectangle(double x0,double z0,double x1,double z1,double y=0,bool walkable=true,uint32_t groups=UINT32_MAX,double cost=1) {
        NavigationBakeTriangle a={{{x0,y,z0},{x0,y,z1},{x1,y,z1}},groups,cost,walkable},b={{{x0,y,z0},{x1,y,z1},{x1,y,z0}},groups,cost,walkable};
        ASSERT_TRUE(triangles.append(a));ASSERT_TRUE(triangles.append(b));
    }
    NavigationBakeDesc description(){return {triangles.data(),triangles.size(),settings,nullptr,nullptr};}
    void create(){navigation_bake_destroy(bake);bake=nullptr;auto desc=description();ASSERT_TRUE(navigation_bake_create(&desc,&bake,&error))<<(error.message?error.message:"")<<" at "<<error.id;ASSERT_TRUE(navigation_bake_stats(bake,&stats));}
    uint64_t node(size_t x,size_t z,double y=0) {
        NavigationBakeCell cell={};for(uint64_t id=1;id<=stats.cells;id++)if(navigation_bake_cell(bake,id,&cell)&&cell.x==x&&cell.z==z&&cell.floor==y)return id;return 0;
    }
    void find(uint64_t start,uint64_t goal,uint32_t filter=UINT32_MAX) {
        ASSERT_NE(start,0u);ASSERT_NE(goal,0u);navigation_path_clear(&path);
        NavigationQuery query={start,goal,filter,NAVIGATION_MAX_NODES,NAVIGATION_MAX_EDGES,true,false};
        ASSERT_TRUE(navigation_find(navigation_bake_graph(bake),&query,&path,&error))<<(error.message?error.message:"");
    }
};
struct NavigationDirtyTileTest : NavigationBakeTest {
    NavigationBake* updated = nullptr;
    NavigationBake* oracle = nullptr;
    NavigationBakeUpdate update = {nullptr, 2, 16};
    void TearDown() override {
        navigation_bake_destroy(updated);
        navigation_bake_destroy(oracle);
        NavigationBakeTest::TearDown();
    }
    void compare() {
        navigation_bake_destroy(updated);
        navigation_bake_destroy(oracle);
        updated = oracle = nullptr;
        auto desc = description();
        update.previous = bake;
        ASSERT_TRUE(navigation_bake_update(&desc, &update, &updated, &error)) << error.message;
        ASSERT_TRUE(navigation_bake_create(&desc, &oracle, &error)) << error.message;
        EXPECT_TRUE(navigation_bake_equal(updated, oracle));
    }
};
TEST_F(NavigationDirtyTileTest, AddedAndRemovedObstaclesReuseRawCellsAndMatchFullBakes) {
    for (uint32_t representation : {uint32_t(NAVIGATION_BAKE_CELLS), uint32_t(NAVIGATION_BAKE_POLYGONS)}) {
        settings.representation = representation;
        settings.radius = .25;
        triangles.clear();
        rectangle(0, 0, 7, 7);
        create();
        rectangle(3.25, 3.25, 3.75, 3.75, .5, false);
        compare();
        ASSERT_NE(updated, nullptr);
        NavigationBakeStats changed = {};
        ASSERT_TRUE(navigation_bake_stats(updated, &changed));
        EXPECT_EQ(changed.dirty_tiles, 1u);
        EXPECT_EQ(changed.raster_columns, 4u);
        EXPECT_EQ(changed.reused_columns, 45u);
        EXPECT_LT(changed.cells, stats.cells);
        NavigationBakeStats original = {};
        ASSERT_TRUE(navigation_bake_stats(bake, &original));
        EXPECT_EQ(original.cells, 25u);
        // the eroded obstacle neighborhood must be recoverable from pre-erosion cells.
        navigation_bake_destroy(bake);
        bake = navigation_bake_retain(updated);
        triangles.clear();
        rectangle(0, 0, 7, 7);
        compare();
        ASSERT_TRUE(navigation_bake_stats(updated, &changed));
        EXPECT_EQ(changed.cells, 25u);
        EXPECT_EQ(changed.dirty_tiles, 1u);
    }
}
TEST_F(NavigationDirtyTileTest, EditsAcrossTileSeamsRetainStackedHeadroomAndAreaErosion) {
    settings.representation = NAVIGATION_BAKE_POLYGONS;
    settings.radius = .25;
    rectangle(0, 0, 7, 7);
    rectangle(0, 0, 7, 7, 3);
    rectangle(1.75, 1.75, 2.25, 2.25, 1.6, false);
    create();
    triangles.clear();
    rectangle(0, 0, 7, 7);
    rectangle(0, 0, 7, 7, 3);
    rectangle(1.75, 1.75, 2.25, 2.25, 1.4, false);
    compare();
    NavigationBakeStats changed = {};
    ASSERT_TRUE(navigation_bake_stats(updated, &changed));
    EXPECT_EQ(changed.dirty_tiles, 4u);
    EXPECT_EQ(changed.raster_columns, 16u);
    EXPECT_LT(changed.cells, stats.cells);
    navigation_bake_destroy(bake);
    bake = navigation_bake_retain(updated);
    triangles[4].walkable = triangles[5].walkable = true;
    triangles[4].groups = triangles[5].groups = 1;
    triangles[4].cost = triangles[5].cost = 3;
    compare();
}
TEST_F(NavigationDirtyTileTest, CanonicalMultisetsIgnoreReorderingAndRetainCopiedInputs) {
    rectangle(0, 0, 7, 7);
    rectangle(2.25, 2.25, 2.75, 2.75, .5, false);
    ASSERT_TRUE(triangles.append(triangles[2]));
    create();
    for (size_t i = 0; i < triangles.size() / 2; i++) {
        auto temporary = triangles[i];
        triangles[i] = triangles[triangles.size() - i - 1];
        triangles[triangles.size() - i - 1] = temporary;
    }
    compare();
    NavigationBakeStats unchanged = {};
    ASSERT_TRUE(navigation_bake_stats(updated, &unchanged));
    EXPECT_EQ(unchanged.dirty_tiles, 0u);
    EXPECT_EQ(unchanged.raster_columns, 0u);
    EXPECT_EQ(unchanged.reused_columns, 49u);
    // remove one duplicate only; the canonical diff must preserve multiplicities.
    ASSERT_TRUE(triangles.remove(0));
    compare();
    ASSERT_TRUE(navigation_bake_stats(updated, &unchanged));
    EXPECT_EQ(unchanged.dirty_tiles, 1u);
}
TEST_F(NavigationDirtyTileTest, InvalidBasesAndBudgetsNeverChangeThePreviousDomain) {
    rectangle(0, 0, 7, 7);
    create();
    auto desc = description();
    update.previous = bake;
    update.tile_size = 0;
    EXPECT_FALSE(navigation_bake_update(&desc, &update, &updated, &error));
    EXPECT_EQ(updated, nullptr);
    update.tile_size = 2;
    update.max_dirty_tiles = 1;
    rectangle(1.75, 1.75, 2.25, 2.25, .5, false);
    desc = description();
    EXPECT_FALSE(navigation_bake_update(&desc, &update, &updated, &error));
    EXPECT_NE(strstr(error.message, "dirty tile quota"), nullptr);
    EXPECT_EQ(updated, nullptr);
    update.max_dirty_tiles = 16;
    desc.proceed = [](void*, NavigationBakeStage stage, size_t) { return stage != NAVIGATION_BAKE_EROSION; };
    EXPECT_FALSE(navigation_bake_update(&desc, &update, &updated, &error));
    EXPECT_NE(strstr(error.message, "cancelled"), nullptr);
    EXPECT_EQ(updated, nullptr);
    desc.proceed = nullptr;
    desc.settings.height += .1;
    EXPECT_FALSE(navigation_bake_update(&desc, &update, &updated, &error));
    EXPECT_NE(strstr(error.message, "matching settings"), nullptr);
    desc = description();
    triangles.back().cost = NAN;
    EXPECT_FALSE(navigation_bake_update(&desc, &update, &updated, &error));
    EXPECT_NE(strstr(error.message, "invalid source triangle"), nullptr);
    EXPECT_EQ(updated, nullptr);
    ASSERT_TRUE(navigation_bake_from_cells(&settings, nullptr, 0, &oracle, &error));
    update.previous = oracle;
    EXPECT_FALSE(navigation_bake_update(&desc, &update, &updated, &error));
    EXPECT_NE(strstr(error.message, "source-backed base"), nullptr);
    find(node(0, 0), node(6, 6));
    EXPECT_EQ(path.status, NAVIGATION_COMPLETE);
}
TEST_F(NavigationDirtyTileTest, SeededEditsMatchFullRebuildsForBothRepresentations) {
    uint32_t seed = 0x73a1285;
    auto random = [&]() {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };
    settings.high.x = 9.5;
    settings.radius = .25;
    for (uint32_t representation : {uint32_t(NAVIGATION_BAKE_CELLS), uint32_t(NAVIGATION_BAKE_POLYGONS)}) {
        settings.representation = representation;
        triangles.clear();
        rectangle(0, 0, 9.5, 7);
        create();
        for (unsigned trial = 0; trial < 32; trial++) {
            SCOPED_TRACE(trial);
            triangles.clear();
            rectangle(0, 0, 9.5, 7);
            for (unsigned i = 0; i < 3; i++) {
                double x = (random() % 9) + .25, z = (random() % 6) + .25;
                rectangle(x, z, x + .25, z + .25, .5, false);
            }
            compare();
            ASSERT_NE(updated, nullptr);
            navigation_bake_destroy(bake);
            bake = navigation_bake_retain(updated);
        }
    }
}
TEST_F(NavigationDirtyTileTest, WorkExhaustionAndEmptyTransitionsKeepImmutableOwnership) {
    rectangle(0, 0, 7, 7);
    create();
    settings.max_work = stats.work;
    create();
    update.previous = bake;
    for (unsigned i = 0; i < 1024; i++)
        rectangle(20, 20, 21, 21);
    auto desc = description();
    EXPECT_FALSE(navigation_bake_update(&desc, &update, &updated, &error));
    EXPECT_NE(strstr(error.message, "work budget exhausted"), nullptr);
    EXPECT_EQ(updated, nullptr);
    triangles.clear();
    settings.max_work = NAVIGATION_BAKE_MAX_WORK;
    create();
    rectangle(0, 0, 7, 7);
    compare();
    NavigationBakeStats changed = {};
    ASSERT_TRUE(navigation_bake_stats(updated, &changed));
    EXPECT_EQ(changed.cells, 49u);
    navigation_bake_destroy(bake);
    bake = navigation_bake_retain(updated);
    triangles.clear();
    compare();
    ASSERT_TRUE(navigation_bake_stats(updated, &changed));
    EXPECT_EQ(changed.cells, 0u);
    EXPECT_EQ(navigation_bake_graph(updated), nullptr);
}
TEST_F(NavigationDirtyTileTest, BoundaryWallsInvalidateBothSidesAndClippedEdgeTiles) {
    settings.high.x = 6.5;
    rectangle(0, 0, 6.5, 7);
    create();
    ASSERT_TRUE(triangles.append({{{4, 0, 3.25}, {4, 1, 3.25}, {4, 0, 3.75}}, UINT32_MAX, 1, false}));
    compare();
    NavigationBakeStats changed = {};
    ASSERT_TRUE(navigation_bake_stats(updated, &changed));
    EXPECT_EQ(changed.dirty_tiles, 2u);
    EXPECT_EQ(changed.raster_columns, 8u);
    navigation_bake_destroy(bake);
    bake = navigation_bake_retain(updated);
    triangles.clear();
    rectangle(0, 0, 6.5, 7);
    rectangle(6.125, 6.125, 6.375, 6.375, .5, false);
    compare();
    ASSERT_TRUE(navigation_bake_stats(updated, &changed));
    EXPECT_EQ(changed.dirty_tiles, 3u);
    EXPECT_EQ(changed.raster_columns, 9u);
}
TEST_F(NavigationDirtyTileTest, SmallDomainsAdmitNormalTileDefaults) {
    settings.high = {1, 6, 1};
    settings.max_cells = 1;
    rectangle(0, 0, 1, 1);
    create();
    update.tile_size = 8;
    update.max_dirty_tiles = 64;
    compare();
    NavigationBakeStats changed = {};
    ASSERT_TRUE(navigation_bake_stats(updated, &changed));
    EXPECT_EQ(changed.reused_columns, 1u);
    triangles[0].cost = triangles[1].cost = 2;
    compare();
    ASSERT_TRUE(navigation_bake_stats(updated, &changed));
    EXPECT_EQ(changed.dirty_tiles, 1u);
    EXPECT_EQ(changed.raster_columns, 1u);
}
TEST_F(NavigationBakeTest, FlatCoverageProducesOwnedCellsAndFourNeighborRoutes) {
    rectangle(0,0,7,7);create();EXPECT_EQ(stats.columns,49u);EXPECT_EQ(stats.cells,49u);EXPECT_EQ(stats.edges,168u);
    triangles[0].vertices[0]={99,99,99};find(node(0,0),node(6,6));EXPECT_EQ(path.status,NAVIGATION_COMPLETE);EXPECT_DOUBLE_EQ(path.cost,12);
    NavigationBakeCell cell={};ASSERT_TRUE(navigation_bake_cell(bake,node(2,3),&cell));EXPECT_DOUBLE_EQ(cell.floor,0);EXPECT_DOUBLE_EQ(cell.ceiling,6);
    NavigationBakeSettings copied={};ASSERT_TRUE(navigation_bake_settings(bake,&copied));EXPECT_DOUBLE_EQ(copied.height,1.5);EXPECT_EQ(copied.max_cells,NAVIGATION_MAX_NODES);
}
TEST_F(NavigationBakeTest, RadiusErodesWholeCellsRatherThanOnlyTheirCenters) {
    rectangle(0,0,7,7);settings.radius=.25;create();EXPECT_EQ(stats.candidates,49u);EXPECT_EQ(stats.cells,25u);EXPECT_EQ(node(0,3),0u);EXPECT_NE(node(1,3),0u);
    settings.radius=1.1;create();EXPECT_EQ(stats.cells,9u);EXPECT_EQ(node(1,3),0u);EXPECT_NE(node(2,3),0u);
    for(uint64_t id=1;id<=stats.cells;id++) {
        NavigationBakeCell cell={};ASSERT_TRUE(navigation_bake_cell(bake,id,&cell));
        EXPECT_GE((double)cell.x,settings.radius);EXPECT_GE(7-cell.x-1,settings.radius);EXPECT_GE((double)cell.z,settings.radius);EXPECT_GE(7-cell.z-1,settings.radius);
    }
    settings.radius=4;create();EXPECT_EQ(stats.cells,0u);EXPECT_EQ(navigation_bake_graph(bake),nullptr);
}
TEST_F(NavigationBakeTest, TinyInteriorHolesAndDuplicateOverlapCannotMasqueradeAsFullCoverage) {
    settings.high.x=settings.high.z=1;
    // the hole misses the cell centre and all four corners.
    rectangle(0,0,.2,1);rectangle(.21,0,1,1);rectangle(.2,0,.21,.2);rectangle(.2,.21,.21,1);create();EXPECT_EQ(stats.cells,0u);
    triangles.clear();NavigationBakeTriangle half={{{0,0,0},{0,0,1},{1,0,0}},UINT32_MAX,1,true};
    for(unsigned i=0;i<3;i++)ASSERT_TRUE(triangles.append(half));create();EXPECT_EQ(stats.cells,0u);
}
TEST_F(NavigationBakeTest, HeadroomUsesConservativeVerticalQuantizationAndIncludesEquality) {
    rectangle(0,0,7,7);rectangle(0,0,7,7,1.5,false);create();EXPECT_EQ(stats.cells,49u);
    triangles.clear();rectangle(0,0,7,7);rectangle(0,0,7,7,1.49,false);create();EXPECT_EQ(stats.cells,0u);
    triangles.clear();rectangle(0,0,7,7);rectangle(0,0,7,7,1.6,false);create();EXPECT_EQ(stats.cells,49u);
}
TEST_F(NavigationBakeTest, StackedLevelsRemainSeparateAndClosedObstacleBottomsBlockTheirInterior) {
    rectangle(0,0,7,7);rectangle(0,0,7,7,3);create();EXPECT_EQ(stats.cells,98u);EXPECT_NE(node(3,3,3),0u);
    find(node(3,3),node(3,3,3));EXPECT_EQ(path.status,NAVIGATION_UNREACHABLE);
    triangles.clear();rectangle(0,0,7,7);rectangle(3,3,4,4,0,false);rectangle(3,3,4,4,2,false);create();
    EXPECT_EQ(node(3,3),0u);EXPECT_NE(node(2,3),0u);settings.radius=.1;create();EXPECT_EQ(node(2,3),0u);EXPECT_NE(node(1,3),0u);
}
TEST_F(NavigationBakeTest, SupportEnvelopesRetainLayerCeilingsAcrossStoredRepresentations) {
    settings.high={1,6,1};NavigationBakeCell cells[]={{0,0,0,1.5,1,UINT32_MAX},{0,0,1.75,4,1,UINT32_MAX,1.5}};
    for(uint32_t representation:{(uint32_t)NAVIGATION_BAKE_CELLS,(uint32_t)NAVIGATION_BAKE_POLYGONS}) {
        settings.representation=representation;navigation_bake_destroy(bake);bake=nullptr;
        ASSERT_TRUE(navigation_bake_from_cells(&settings,cells,2,&bake,&error));NavigationBakeSupport lower={},upper={};
        ASSERT_TRUE(navigation_bake_support(bake,1,{.5,0,.5},&lower));ASSERT_TRUE(navigation_bake_support(bake,2,{.5,1.6,.5},&upper));
        EXPECT_DOUBLE_EQ(lower.position.y,0);EXPECT_DOUBLE_EQ(lower.ceiling,1.5);
        EXPECT_DOUBLE_EQ(upper.position.y,1.75);EXPECT_DOUBLE_EQ(upper.low,1.5);EXPECT_DOUBLE_EQ(upper.ceiling,4);
        auto before=upper;EXPECT_FALSE(navigation_bake_support(bake,3,{.5,0,.5},&upper));EXPECT_EQ(memcmp(&before,&upper,sizeof(upper)),0);
        EXPECT_FALSE(navigation_bake_support(bake,2,{NAN,0,0},&upper));EXPECT_EQ(memcmp(&before,&upper,sizeof(upper)),0);
    }
    NavigationBake* rejected=nullptr;cells[1].support_span=-1;EXPECT_FALSE(navigation_bake_from_cells(&settings,cells,2,&rejected,&error));EXPECT_EQ(rejected,nullptr);
    cells[1].support_span=NAN;EXPECT_FALSE(navigation_bake_from_cells(&settings,cells,2,&rejected,&error));EXPECT_EQ(rejected,nullptr);
    cells[1].support_span=10;EXPECT_FALSE(navigation_bake_from_cells(&settings,cells,2,&rejected,&error));EXPECT_EQ(rejected,nullptr);
}
TEST_F(NavigationBakeTest, QuantizedStepsAndHeadroomConstrainAdjacency) {
    rectangle(0,0,3,7);rectangle(3,0,7,7,.25);
    ASSERT_TRUE(triangles.append({{{3,0,0},{3,.25,0},{3,.25,7}},UINT32_MAX,1,false}));
    ASSERT_TRUE(triangles.append({{{3,0,0},{3,.25,7},{3,0,7}},UINT32_MAX,1,false}));
    create();find(node(0,3),node(6,3,.25));EXPECT_EQ(path.status,NAVIGATION_COMPLETE);
    settings.max_step=.125;create();find(node(0,3),node(6,3,.25));EXPECT_EQ(path.status,NAVIGATION_UNREACHABLE);
    settings.max_step=.375;rectangle(0,0,7,7,1.5,false);create();EXPECT_EQ(node(6,3,.25),0u);EXPECT_NE(node(0,3),0u);
}
TEST_F(NavigationBakeTest, SlopesAndVerticalWallsDoNotBecomeUnsupportedFloatingFloors) {
    NavigationBakeTriangle ramp={{{0,0,0},{0,0,7},{7,3.5,0}},UINT32_MAX,1,true};ASSERT_TRUE(triangles.append(ramp));create();EXPECT_GT(stats.cells,0u);
    settings.max_slope=.1;create();EXPECT_EQ(stats.cells,0u);
    triangles.clear();rectangle(0,0,7,7);settings.max_slope=acos(-1.0)/4;
    NavigationBakeTriangle wall={{{3.5,0,0},{3.5,3,0},{3.5,0,7}},UINT32_MAX,1,false};ASSERT_TRUE(triangles.append(wall));create();
    EXPECT_EQ(node(3,1),0u);EXPECT_NE(node(2,1),0u);find(node(1,1),node(5,1));EXPECT_EQ(path.status,NAVIGATION_UNREACHABLE);
}
TEST_F(NavigationBakeTest, RadiusMasksAndCostsUseAnImmutableNeighborhood) {
    rectangle(0,0,3,7,0,true,1,2);rectangle(3,0,7,7,0,true,3,1);settings.radius=.1;create();
    NavigationBakeCell cell={};ASSERT_TRUE(navigation_bake_cell(bake,node(3,3),&cell));EXPECT_EQ(cell.groups,1u);EXPECT_DOUBLE_EQ(cell.cost,2);
    ASSERT_TRUE(navigation_bake_cell(bake,node(4,3),&cell));EXPECT_EQ(cell.groups,3u);EXPECT_DOUBLE_EQ(cell.cost,1);
    find(node(4,3),node(3,3),2);EXPECT_EQ(path.status,NAVIGATION_UNREACHABLE);
}
TEST_F(NavigationBakeTest, EmptyInputCancellationAndWorkLimitsNeverPublishPartialDomains) {
    create();EXPECT_EQ(stats.cells,0u);EXPECT_EQ(navigation_bake_graph(bake),nullptr);navigation_bake_destroy(bake);bake=nullptr;
    rectangle(0,0,7,7);auto desc=description();desc.proceed=[](void*,NavigationBakeStage,size_t){return false;};
    EXPECT_FALSE(navigation_bake_create(&desc,&bake,&error));EXPECT_EQ(bake,nullptr);EXPECT_NE(strstr(error.message,"cancelled"),nullptr);
    desc.proceed=[](void*,NavigationBakeStage stage,size_t){return stage!=NAVIGATION_BAKE_READY;};
    EXPECT_FALSE(navigation_bake_create(&desc,&bake,&error));EXPECT_EQ(bake,nullptr);EXPECT_NE(strstr(error.message,"cancelled"),nullptr);
    desc=description();desc.settings.max_work=2;EXPECT_FALSE(navigation_bake_create(&desc,&bake,&error));EXPECT_EQ(bake,nullptr);EXPECT_NE(strstr(error.message,"work budget"),nullptr);
    desc=description();desc.settings.max_fragments=1;EXPECT_FALSE(navigation_bake_create(&desc,&bake,&error));EXPECT_EQ(bake,nullptr);EXPECT_NE(strstr(error.message,"fragment quota"),nullptr);
    desc=description();desc.settings.cell_size=0;EXPECT_FALSE(navigation_bake_create(&desc,&bake,&error));EXPECT_EQ(bake,nullptr);
    desc=description();triangles[0].vertices[0].x=NAN;EXPECT_FALSE(navigation_bake_create(&desc,&bake,&error));EXPECT_EQ(bake,nullptr);
}
TEST_F(NavigationBakeTest, WorldQueriesRespectClearanceAndPreserveProjectionWorkOnExhaustion) {
    rectangle(0,0,7,7);settings.radius=.25;create();NavigationMeshPath surface={};
    NavigationBakeQuery query={{{1.25,0,1.25},{5.75,0,5.75},{0,0,UINT32_MAX,NAVIGATION_MAX_NODES,NAVIGATION_MAX_EDGES,true,false},1,2*NAVIGATION_MAX_NODES,1000},.25,1.5,.375};
    ASSERT_TRUE(navigation_bake_find(bake,&query,&surface,&error));EXPECT_EQ(surface.corridor.status,NAVIGATION_COMPLETE);EXPECT_GT(surface.point_count,2u);EXPECT_EQ(surface.portal_count,0u);
    for(size_t i=0;i<surface.point_count;i++){EXPECT_GE(surface.points[i].x,1);EXPECT_LE(surface.points[i].x,6);EXPECT_DOUBLE_EQ(surface.points[i].y,0);}
    navigation_mesh_path_clear(&surface);query.radius=.3;EXPECT_FALSE(navigation_bake_find(bake,&query,&surface,&error));EXPECT_EQ(surface.points,nullptr);
    query.radius=.25;query.height=1.6;EXPECT_FALSE(navigation_bake_find(bake,&query,&surface,&error));query.height=1.5;query.max_step=.25;
    EXPECT_FALSE(navigation_bake_find(bake,&query,&surface,&error));query.max_step=.375;query.path.max_portal_tests=0;
    ASSERT_TRUE(navigation_bake_find(bake,&query,&surface,&error));EXPECT_EQ(surface.corridor.status,NAVIGATION_EXHAUSTED);EXPECT_EQ(surface.point_count,0u);EXPECT_EQ(surface.projection_tests,50u);
    navigation_mesh_path_clear(&surface);query.path.max_projection_tests=1;
    ASSERT_TRUE(navigation_bake_find(bake,&query,&surface,&error));EXPECT_EQ(surface.corridor.status,NAVIGATION_EXHAUSTED);EXPECT_EQ(surface.projection_tests,1u);EXPECT_EQ(surface.start_polygon,0u);
    navigation_mesh_path_clear(&surface);
}
TEST_F(NavigationBakeTest, PartialWorldQueriesKeepTheTargetAndEndAtTheReachableCell) {
    rectangle(0,0,3,7);rectangle(4,0,7,7);create();NavigationMeshPath surface={};
    NavigationBakeQuery query={{{.2,0,.5},{6.8,0,.5},{0,0,UINT32_MAX,NAVIGATION_MAX_NODES,NAVIGATION_MAX_EDGES,true,true},1,2*NAVIGATION_MAX_NODES,1000},0,1.5,.375};
    ASSERT_TRUE(navigation_bake_find(bake,&query,&surface,&error));EXPECT_EQ(surface.corridor.status,NAVIGATION_PARTIAL);EXPECT_DOUBLE_EQ(surface.projected_goal.x,6.8);EXPECT_DOUBLE_EQ(surface.path_end.x,3);
    ASSERT_GT(surface.point_count,0u);EXPECT_DOUBLE_EQ(surface.points[surface.point_count-1].x,3);navigation_mesh_path_clear(&surface);
}
TEST_F(NavigationBakeTest, EqualityIncludesClearanceAndCeilingsBeyondGraphConnectivity) {
    rectangle(0,0,7,7);create();NavigationBake* other=nullptr;auto desc=description();ASSERT_TRUE(navigation_bake_create(&desc,&other,&error));EXPECT_TRUE(navigation_bake_equal(bake,other));
    navigation_bake_destroy(other);desc.settings.high.y=5;ASSERT_TRUE(navigation_bake_create(&desc,&other,&error));
    EXPECT_TRUE(navigation_equal(navigation_bake_graph(bake),navigation_bake_graph(other)));EXPECT_FALSE(navigation_bake_equal(bake,other));navigation_bake_destroy(other);
}
TEST_F(NavigationBakeTest, StoredCellsRebuildTheSameDomainAndRetainedOwnersOutliveTheJob) {
    char original_hash[65],zero_hash[65];ASSERT_TRUE(navigation_bake_settings_hash(&settings,original_hash));
    auto signed_zero=settings;signed_zero.low.x=-0.0;ASSERT_TRUE(navigation_bake_settings_hash(&signed_zero,zero_hash));EXPECT_STREQ(original_hash,zero_hash);
    rectangle(0,0,7,7);settings.radius=.25;create();lam::ArrayList<NavigationBakeCell> cells;
    for(uint64_t id=1;id<=stats.cells;id++){NavigationBakeCell cell={};ASSERT_TRUE(navigation_bake_cell(bake,id,&cell));ASSERT_TRUE(cells.append(cell));}
    NavigationBake* loaded=nullptr;ASSERT_TRUE(navigation_bake_from_cells(&settings,cells.data(),cells.size(),&loaded,&error));EXPECT_TRUE(navigation_bake_equal(bake,loaded));
    auto* retained=navigation_bake_retain(loaded);navigation_bake_destroy(loaded);EXPECT_TRUE(navigation_bake_equal(bake,retained));navigation_bake_destroy(retained);
    ASSERT_TRUE(navigation_bake_from_cells(&settings,nullptr,0,&loaded,&error));NavigationBakeStats empty={};ASSERT_TRUE(navigation_bake_stats(loaded,&empty));EXPECT_EQ(empty.cells,0u);navigation_bake_destroy(loaded);
    cells[1]=cells[0];EXPECT_FALSE(navigation_bake_from_cells(&settings,cells.data(),cells.size(),&loaded,&error));EXPECT_EQ(loaded,nullptr);
    cells[0].ceiling=cells[0].floor+settings.height-.1;EXPECT_FALSE(navigation_bake_from_cells(&settings,cells.data(),1,&loaded,&error));EXPECT_EQ(loaded,nullptr);
    cells[0].ceiling=6;cells[0].groups=0;EXPECT_FALSE(navigation_bake_from_cells(&settings,cells.data(),1,&loaded,&error));EXPECT_EQ(loaded,nullptr);
    EXPECT_FALSE(navigation_bake_from_cells(&settings,nullptr,1,&loaded,&error));EXPECT_EQ(loaded,nullptr);
}
TEST_F(NavigationBakeTest, SeededFloorPatchesAgreeWithIndependentRectangleClearance) {
    uint32_t seed=0xd4ad53b1;auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
    for(unsigned trial=0;trial<32;trial++) {
        bool floor[49]={};uint32_t groups[49]={};double costs[49]={};triangles.clear();
        for(size_t z=0;z<7;z++)for(size_t x=0;x<7;x++) {
            size_t index=x+7*z;floor[index]=random()%5!=0;groups[index]=random()%2?1:3;costs[index]=(random()%5)*.25;
            if(floor[index])rectangle(x,z,x+1,z+1,0,true,groups[index],costs[index]);
        }
        for(double radius:{0.0,.1,.75,1.1}) {
            settings.radius=radius;create();size_t expected=0;
            for(size_t z=0;z<7;z++)for(size_t x=0;x<7;x++) {
                bool admitted=floor[x+7*z];uint32_t mask=UINT32_MAX;double cost=0;
                // expand every entire candidate rectangle by a disk, independently of raster spans.
                for(int dz=-3;dz<=3&&admitted;dz++)for(int dx=-3;dx<=3;dx++) {
                    if((dx||dz)&&hypot(fmax(abs(dx)-1,0),fmax(abs(dz)-1,0))>=radius)continue;
                    int cx=(int)x+dx,cz=(int)z+dz;
                    if(cx<0||cz<0||cx>=7||cz>=7||!floor[cx+7*cz]){admitted=false;break;}
                    mask&=groups[cx+7*cz];cost=fmax(cost,costs[cx+7*cz]);
                }
                EXPECT_EQ(node(x,z)!=0,admitted)<<trial<<" radius "<<radius<<" at "<<x<<","<<z;
                if(admitted){expected++;NavigationBakeCell cell={};ASSERT_TRUE(navigation_bake_cell(bake,node(x,z),&cell));EXPECT_EQ(cell.groups,mask);EXPECT_DOUBLE_EQ(cell.cost,cost);}
            }
            EXPECT_EQ(stats.cells,expected);
        }
    }
}
TEST_F(NavigationMeshTest, TurningCorridorUsesItsInsideCorner) {
    cell(0,0);cell(1,0);cell(2,0);cell(2,1);cell(2,2);create();find(query({.2,.5,0},{2.5,2.8,0}));
    ASSERT_EQ(path.corridor.status,NAVIGATION_COMPLETE);EXPECT_NEAR(path.length,2*hypot(1.8,.5),1e-12);
    bool corner=false;for(size_t i=0;i<path.point_count;i++)corner|=fabs(path.points[i].x-2)<1e-12&&fabs(path.points[i].y-1)<1e-12;EXPECT_TRUE(corner);
}
TEST_F(NavigationMeshTest, HoleRoutesStayInTheWalkableDomain) {
    for(unsigned y=0;y<5;y++)for(unsigned x=0;x<5;x++)if(x!=2||y!=2)cell(x,y);create();find(query({.5,2.5,0},{4.5,2.5,0}));
    ASSERT_EQ(path.corridor.status,NAVIGATION_COMPLETE);EXPECT_GE(path.length,2*sqrt(2.5)+1-1e-12);EXPECT_LT(path.length,6);
    for(size_t i=1;i<path.point_count;i++)for(unsigned sample=0;sample<=32;sample++) {
        double t=sample/32.0,x=path.points[i-1].x*(1-t)+path.points[i].x*t,y=path.points[i-1].y*(1-t)+path.points[i].y*t;
        EXPECT_GE(x,-1e-12);EXPECT_LE(x,5+1e-12);EXPECT_GE(y,-1e-12);EXPECT_LE(y,5+1e-12);
        EXPECT_FALSE(x>2+1e-12&&x<3-1e-12&&y>2+1e-12&&y<3-1e-12);
    }
}
TEST_F(NavigationMeshTest, GroundedWaypointsRetainHeightChangesAndAreaCosts) {
    dimensions=3;double heights[]={0,0,1,1};
    for(unsigned x=0;x<3;x++) {
        uint32_t base=(uint32_t)vertices.size();vertex(x,heights[x],0);vertex(x,heights[x],1);vertex(x+1,heights[x+1],1);vertex(x+1,heights[x+1],0);
        triangle(base,base+1,base+2,x==1?2:1);triangle(base,base+2,base+3,x==1?2:1);
    }
    create();find(query({.1,0,.5},{2.9,1,.5}));ASSERT_EQ(path.corridor.status,NAVIGATION_COMPLETE);
    EXPECT_NEAR(path.length,1.8+sqrt(2),1e-12);EXPECT_NEAR(path.travel_cost,1.8+2*sqrt(2),1e-12);
    bool lower=false,upper=false;
    for(size_t i=0;i<path.point_count;i++){auto p=path.points[i];lower|=fabs(p.x-1)<1e-12&&fabs(p.y)<1e-12;upper|=fabs(p.x-2)<1e-12&&fabs(p.y-1)<1e-12;EXPECT_NEAR(p.z,.5,1e-12);}
    EXPECT_TRUE(lower);EXPECT_TRUE(upper);
}
TEST_F(NavigationMeshTest, ProjectionReportsTheRequestedTargetSeparatelyFromPartialReachability) {
    cell(0,0);cell(2,0);create();auto q=query({-.5,.25,0},{2.75,.25,0});q.search.allow_partial=true;find(q);
    ASSERT_EQ(path.corridor.status,NAVIGATION_PARTIAL);point(path.projected_start,0,.25);point(path.projected_goal,2.75,.25);point(path.path_end,1,.25);
    EXPECT_NEAR(path.start_distance,.5,1e-12);EXPECT_NEAR(path.goal_distance,0,1e-12);point(path.points[path.point_count-1],1,.25);
    q.max_projection_distance=.1;find(q);EXPECT_EQ(path.corridor.status,NAVIGATION_UNREACHABLE);EXPECT_EQ(path.start_polygon,0u);EXPECT_EQ(path.corridor.count,0u);
}
TEST_F(NavigationMeshTest, WeightedAreasChooseTheLowCostCorridorAndFiltersExcludeEndpoints) {
    for(unsigned y=0;y<3;y++)for(unsigned x=0;x<3;x++)cell(x,y,x==1&&y==1?100:1,x==2?2:1);create();
    auto q=query({.25,1.25,0},{2.75,1.25,0});find(q);ASSERT_EQ(path.corridor.status,NAVIGATION_COMPLETE);
    for(size_t i=0;i<path.corridor.count;i++){EXPECT_NE(path.corridor.nodes[i],9u);EXPECT_NE(path.corridor.nodes[i],10u);}EXPECT_LT(path.travel_cost,10);
    q.search.filter=1;q.max_projection_distance=0;find(q);EXPECT_EQ(path.corridor.status,NAVIGATION_UNREACHABLE);EXPECT_EQ(path.goal_polygon,0u);
}
TEST_F(NavigationMeshTest, SlopeAndWindingAreExplicitThreeDimensionalWalkingRules) {
    dimensions=3;slope=.2;vertex(0,0,0);vertex(0,0,1);vertex(1,1,0);triangle(0,1,2);create();find(query({.25,.25,.25},{.5,.5,.25}));EXPECT_EQ(path.corridor.status,NAVIGATION_UNREACHABLE);
    navigation_mesh_destroy(mesh);mesh=nullptr;slope=1;create();find(query({.25,.25,.25},{.5,.5,.25}));EXPECT_EQ(path.corridor.status,NAVIGATION_COMPLETE);
    navigation_mesh_destroy(mesh);mesh=nullptr;triangles[0].vertices[1]=2;triangles[0].vertices[2]=1;create();find(query({.25,.25,.25},{.5,.5,.25}));EXPECT_EQ(path.corridor.status,NAVIGATION_UNREACHABLE);
    navigation_mesh_destroy(mesh);mesh=nullptr;two_sided=true;create();find(query({.25,.25,.25},{.5,.5,.25}));EXPECT_EQ(path.corridor.status,NAVIGATION_COMPLETE);
}

TEST_F(NavigationMeshTest, MaximumSlopeIsInclusiveWithoutAdmittingVerticalFaces) {
    dimensions=3;slope=acos(-1.0)/4;vertex(0,0,0);vertex(0,0,1);vertex(1,1,0);triangle(0,1,2);create();
    auto q=query({.25,.25,.25},{.5,.5,.25});find(q);ASSERT_EQ(path.corridor.status,NAVIGATION_COMPLETE);
    navigation_mesh_destroy(mesh);mesh=nullptr;slope-=1e-6;create();find(q);EXPECT_EQ(path.corridor.status,NAVIGATION_UNREACHABLE);
    navigation_mesh_destroy(mesh);mesh=nullptr;vertices[2]={0,1,0};slope=nextafter(acos(0),0);create();find(q);EXPECT_EQ(path.corridor.status,NAVIGATION_UNREACHABLE);
}
TEST_F(NavigationMeshTest, WorkLimitsNeverPublishAnUnverifiedMovementPath) {
    cell(0,0);cell(1,0);create();auto q=query({.25,.25,0},{1.75,.25,0});q.max_projection_tests=1;find(q);
    EXPECT_EQ(path.corridor.status,NAVIGATION_EXHAUSTED);EXPECT_EQ(path.start_polygon,0u);EXPECT_EQ(path.projection_tests,1u);EXPECT_EQ(path.point_count,0u);
    q.max_projection_tests=2*NAVIGATION_MAX_NODES;q.max_portal_tests=0;find(q);EXPECT_EQ(path.corridor.status,NAVIGATION_EXHAUSTED);EXPECT_EQ(path.corridor.count,0u);EXPECT_EQ(path.point_count,0u);
    q.max_portal_tests=100;q.search.max_expansions=0;q.search.allow_partial=true;find(q);EXPECT_EQ(path.corridor.status,NAVIGATION_EXHAUSTED);EXPECT_GT(path.point_count,0u);
}
TEST_F(NavigationMeshTest, InvalidGeometryAndQueriesRejectBeforePublishingOrMutatingOutputs) {
    cell(0,0);auto desc=description();NavigationMesh* rejected=nullptr;triangles[0].vertices[0]=99;
    EXPECT_FALSE(navigation_mesh_create(&desc,&rejected,&error));EXPECT_EQ(rejected,nullptr);triangles[0].vertices[0]=0;
    vertices[1]=vertices[0];EXPECT_FALSE(navigation_mesh_create(&desc,&rejected,&error));vertices[1]={1,0,0};
    triangles[0].cost=-1;EXPECT_FALSE(navigation_mesh_create(&desc,&rejected,&error));triangles[0].cost=1;create();auto q=query({0,0,0},{1,1,0});q.start.x=NAN;path.length=123;
    EXPECT_FALSE(navigation_mesh_find(mesh,&q,&path,&error));EXPECT_DOUBLE_EQ(path.length,123);
    desc.vertex_count=NAVIGATION_MAX_VERTICES+1;EXPECT_FALSE(navigation_mesh_create(&desc,&rejected,&error));
}
TEST_F(NavigationMeshTest, DuplicateAndNonmanifoldTopologyRejectsInsteadOfInventingPortals) {
    vertex(0,0);vertex(1,0);vertex(0,1);vertex(1,-1);vertex(.5,2);triangle(0,1,2);triangle(1,0,3);triangle(0,1,4);
    auto desc=description();NavigationMesh* rejected=nullptr;EXPECT_FALSE(navigation_mesh_create(&desc,&rejected,&error));EXPECT_NE(strstr(error.message,"nonmanifold"),nullptr);
    triangles.clear();triangle(0,1,2);triangle(2,0,1);desc=description();EXPECT_FALSE(navigation_mesh_create(&desc,&rejected,&error));EXPECT_NE(strstr(error.message,"duplicate"),nullptr);
}

TEST_F(NavigationMeshTest, SeededCorridorsAgreeWithGridConnectivityAndStayInsideTheirDomain) {
    for(unsigned trial=0;trial<40;trial++) {
        SCOPED_TRACE(trial);
        bool blocked[49]={},visited[49]={};unsigned queue[49]={},head=0,tail=1;
        for(unsigned i=1;i<48;i++)blocked[i]=math_splitmix64_unit(0x6f2a31+trial*49+i)<.24;
        while(head<tail) {
            unsigned current=queue[head++],x=current%7,y=current/7;const int offsets[][2]={{-1,0},{1,0},{0,-1},{0,1}};
            visited[current]=true;
            for(const auto& offset:offsets) {
                int nx=(int)x+offset[0],ny=(int)y+offset[1];if(nx<0||nx>=7||ny<0||ny>=7)continue;
                unsigned next=(unsigned)nx+7*(unsigned)ny;
                if(blocked[next]||visited[next])continue;visited[next]=true;queue[tail++]=next;
            }
        }
        for(unsigned y=0;y<7;y++)for(unsigned x=0;x<7;x++)if(!blocked[x+7*y])cell(x,y);create();
        auto q=query({.25,.25,0},{6.75,6.75,0});q.max_projection_distance=1e-8;find(q);
        if(!visited[48]){EXPECT_EQ(path.corridor.status,NAVIGATION_UNREACHABLE)<<trial;EXPECT_EQ(path.point_count,0u);}
        else {
            ASSERT_EQ(path.corridor.status,NAVIGATION_COMPLETE)<<trial;EXPECT_GE(path.length,hypot(6.5,6.5)-1e-12);EXPECT_NEAR(path.travel_cost,path.length,1e-12);
            ASSERT_GE(path.point_count,2u);
            for(size_t i=1;i<path.point_count;i++)for(unsigned sample=0;sample<=16;sample++) {
                double t=sample/16.0,x=path.points[i-1].x*(1-t)+path.points[i].x*t,y=path.points[i-1].y*(1-t)+path.points[i].y*t;bool admitted=false;
                for(unsigned cy=0;cy<7;cy++)for(unsigned cx=0;cx<7;cx++)
                    admitted|=!blocked[cx+7*cy]&&x>=cx-1e-12&&x<=cx+1+1e-12&&y>=cy-1e-12&&y<=cy+1+1e-12;
                EXPECT_TRUE(admitted)<<"trial "<<trial<<" point "<<x<<","<<y;
            }
        }
        navigation_mesh_path_clear(&path);navigation_mesh_destroy(mesh);mesh=nullptr;vertices.clear();triangles.clear();
    }
}

TEST_F(NavigationMeshTest, CollinearPortalsHandleProjectionRoundoffAcrossScales) {
    const double scales[]={1e-100,1,1e100};
    for(double scale:scales) {
        SCOPED_TRACE(scale);for(unsigned y=0;y<3;y++)for(unsigned x=0;x<3;x++)cell(x,y);
        for(auto& p:vertices){p.x*=scale;p.y*=scale;}create();
        auto q=query({.25*scale,.25*scale,0},{2.75*scale,2.75*scale,0});q.max_projection_distance=scale*1e-10;find(q);
        ASSERT_EQ(path.corridor.status,NAVIGATION_COMPLETE);EXPECT_NEAR(path.length/scale,hypot(2.5,2.5),1e-12);
        navigation_mesh_path_clear(&path);navigation_mesh_destroy(mesh);mesh=nullptr;vertices.clear();triangles.clear();
    }
}

TEST_F(NavigationMeshTest, SurfaceEqualityIncludesGeometryAndAreaCostsWithIdenticalGraphs) {
    vertex(0,0);vertex(3,0);vertex(0,3);triangle(0,1,2);create();
    NavigationVec changed[]={{0,0,0},{6,0,0},{-3,3,0}};auto desc=description();desc.vertices=changed;
    NavigationMesh* other=nullptr;ASSERT_TRUE(navigation_mesh_create(&desc,&other,&error));
    EXPECT_TRUE(navigation_equal(navigation_mesh_graph(mesh),navigation_mesh_graph(other)));EXPECT_FALSE(navigation_mesh_equal(mesh,other));navigation_mesh_destroy(other);other=nullptr;
    desc.vertices=vertices.data();triangles[0].cost=3;ASSERT_TRUE(navigation_mesh_create(&desc,&other,&error));
    EXPECT_TRUE(navigation_equal(navigation_mesh_graph(mesh),navigation_mesh_graph(other)));EXPECT_FALSE(navigation_mesh_equal(mesh,other));navigation_mesh_destroy(other);
}


struct NavigationBakeRegionTest : NavigationBakeTest {
    NavigationMeshPath route={};
    NavigationBakeRegionTest(){settings.representation=NAVIGATION_BAKE_POLYGONS;}
    void TearDown()override{navigation_mesh_path_clear(&route);NavigationBakeTest::TearDown();}
    NavigationBakeQuery query(NavigationVec start,NavigationVec goal) {
        auto q=world_query(start,goal);q.max_projection_distance=0;return {q,settings.radius,settings.height,settings.max_step};
    }
    void find_route(NavigationBakeQuery q) {
        navigation_mesh_path_clear(&route);error={};ASSERT_TRUE(navigation_bake_find(bake,&q,&route,&error))<<(error.message?error.message:"")<<" at "<<error.id;
    }
    void sampled_floor(const bool* floor) {
        for(size_t i=1;i<route.point_count;i++)for(unsigned sample=0;sample<=128;sample++) {
            double t=sample/128.0,x=route.points[i-1].x*(1-t)+route.points[i].x*t,z=route.points[i-1].z*(1-t)+route.points[i].z*t;
            bool inside=false;
            for(size_t cz=0;cz<7;cz++)for(size_t cx=0;cx<7;cx++)if(floor[cx+7*cz]&&x>=cx-1e-12&&x<=cx+1+1e-12&&z>=cz-1e-12&&z<=cz+1+1e-12)inside=true;
            ASSERT_TRUE(inside)<<"segment "<<i<<" at "<<x<<","<<z;
        }
    }
};
TEST_F(NavigationBakeRegionTest, OpenClearanceFloorMergesAndPullsADirectRoute) {
    rectangle(0,0,7,7);settings.radius=.25;create();ASSERT_NE(bake,nullptr);EXPECT_EQ(stats.cells,25u);
    EXPECT_EQ(navigation_node_count(navigation_bake_graph(bake)),1u);EXPECT_EQ(navigation_bake_region_count(bake),1u);
    NavigationBakeRegion region={};ASSERT_TRUE(navigation_bake_region(bake,1,&region));EXPECT_EQ(region.x,1u);EXPECT_EQ(region.z,1u);EXPECT_EQ(region.width,5u);EXPECT_EQ(region.depth,5u);
    find_route(query({1.2,0,1.3},{5.7,0,5.4}));ASSERT_EQ(route.corridor.status,NAVIGATION_COMPLETE);ASSERT_EQ(route.point_count,2u);EXPECT_EQ(route.portal_count,0u);
    EXPECT_NEAR(route.length,hypot(4.5,4.1),1e-12);EXPECT_NEAR(route.travel_cost,route.length,1e-12);EXPECT_EQ(route.projection_tests,2u);
}
TEST_F(NavigationBakeRegionTest, ConcaveFloorAndHoleKeepFunnelSegmentsInTheDomain) {
    rectangle(0,0,7,2);rectangle(0,2,2,7);create();ASSERT_NE(bake,nullptr);EXPECT_EQ(navigation_bake_region_count(bake),2u);
    find_route(query({.5,0,6.5},{6.5,0,.5}));ASSERT_EQ(route.corridor.status,NAVIGATION_COMPLETE);EXPECT_NEAR(route.length,2*hypot(1.5,4.5),1e-12);
    bool floor[49]={};for(size_t z=0;z<7;z++)for(size_t x=0;x<7;x++)floor[x+7*z]=x<2||z<2;sampled_floor(floor);
    triangles.clear();rectangle(0,0,7,2);rectangle(0,5,7,7);rectangle(0,2,2,5);rectangle(5,2,7,5);create();
    find_route(query({.5,0,3.5},{6.5,0,3.5}));ASSERT_EQ(route.corridor.status,NAVIGATION_COMPLETE);EXPECT_NEAR(route.length,3+2*hypot(1.5,1.5),1e-12);
    for(size_t z=0;z<7;z++)for(size_t x=0;x<7;x++)floor[x+7*z]=x<2||x>=5||z<2||z>=5;sampled_floor(floor);
}
TEST_F(NavigationBakeRegionTest, PartialBoundaryPortalsRetainAreaFiltersAndWeightedTravel) {
    rectangle(0,0,7,2,0,true,3);rectangle(0,2,3,7,0,true,1);rectangle(3,2,7,7,0,true,2,3);create();
    ASSERT_NE(bake,nullptr);EXPECT_EQ(navigation_bake_region_count(bake),3u);auto q=query({5,0,.5},{5,0,6.5});find_route(q);
    ASSERT_EQ(route.corridor.status,NAVIGATION_COMPLETE);ASSERT_EQ(route.portal_count,1u);EXPECT_DOUBLE_EQ(route.portals[0].left.z,2);EXPECT_DOUBLE_EQ(route.portals[0].right.z,2);
    EXPECT_DOUBLE_EQ(route.portals[0].left.x,3);EXPECT_DOUBLE_EQ(route.portals[0].right.x,7);EXPECT_NEAR(route.length,6,1e-12);EXPECT_NEAR(route.travel_cost,15,1e-12);
    q.path.search.filter=1;find_route(q);EXPECT_EQ(route.corridor.status,NAVIGATION_UNREACHABLE);EXPECT_EQ(route.goal_polygon,0u);
}
TEST_F(NavigationBakeRegionTest, StepsRetainBothHeightsAndStackedLayersNeverJoin) {
    rectangle(0,0,3,7);rectangle(3,0,7,7,.25);
    ASSERT_TRUE(triangles.append({{{3,0,0},{3,.25,0},{3,.25,7}},UINT32_MAX,1,false}));
    ASSERT_TRUE(triangles.append({{{3,0,0},{3,.25,7},{3,0,7}},UINT32_MAX,1,false}));create();ASSERT_NE(bake,nullptr);
    find_route(query({.2,0,3.5},{6.8,.25,3.5}));ASSERT_EQ(route.corridor.status,NAVIGATION_COMPLETE);bool step=false;
    for(size_t i=1;i<route.point_count;i++) {
        auto a=route.points[i-1],b=route.points[i];
        if(a.y!=b.y){EXPECT_DOUBLE_EQ(a.x,b.x);EXPECT_DOUBLE_EQ(a.z,b.z);EXPECT_NEAR(b.y-a.y,.25,1e-12);step=true;}
    }
    EXPECT_TRUE(step);EXPECT_NEAR(route.length,6.85,1e-12);
    settings.max_step=.125;create();find_route(query({.2,0,3.5},{6.8,.25,3.5}));EXPECT_EQ(route.corridor.status,NAVIGATION_UNREACHABLE);
    triangles.clear();settings.max_step=.375;rectangle(0,0,7,7);rectangle(0,0,7,7,3);create();EXPECT_EQ(navigation_bake_region_count(bake),2u);
    find_route(query({.2,0,.5},{6.8,3,.5}));EXPECT_EQ(route.corridor.status,NAVIGATION_UNREACHABLE);
}
TEST_F(NavigationBakeRegionTest, SavedCellsRebuildThePartitionAndKeepProfileIdentity) {
    rectangle(0,0,7,2);rectangle(0,2,2,7);create();ASSERT_NE(bake,nullptr);lam::ArrayList<NavigationBakeCell> cells;
    for(uint64_t id=1;id<=stats.cells;id++){NavigationBakeCell cell={};ASSERT_TRUE(navigation_bake_cell(bake,id,&cell));ASSERT_TRUE(cells.append(cell));}
    NavigationBake* loaded=nullptr;ASSERT_TRUE(navigation_bake_from_cells(&settings,cells.data(),cells.size(),&loaded,&error));EXPECT_TRUE(navigation_bake_equal(bake,loaded));
    EXPECT_EQ(navigation_bake_region_count(loaded),2u);navigation_bake_destroy(loaded);
    char polygons[65],cells_hash[65];ASSERT_TRUE(navigation_bake_settings_hash(&settings,polygons));auto legacy=settings;legacy.representation=NAVIGATION_BAKE_CELLS;
    ASSERT_TRUE(navigation_bake_settings_hash(&legacy,cells_hash));EXPECT_STRNE(polygons,cells_hash);
    ASSERT_TRUE(navigation_bake_from_cells(&settings,nullptr,0,&loaded,&error));EXPECT_EQ(navigation_bake_region_count(loaded),0u);navigation_bake_destroy(loaded);
    triangles.clear();create();find_route(query({0,0,0},{1,0,1}));EXPECT_EQ(route.corridor.status,NAVIGATION_UNREACHABLE);EXPECT_EQ(route.projection_tests,0u);
}
TEST_F(NavigationBakeRegionTest, SearchProjectionAssemblyAndConstructionLimitsRemainExplicit) {
    rectangle(0,0,7,2);rectangle(0,2,2,7);create();ASSERT_NE(bake,nullptr);auto q=query({.5,0,6.5},{6.5,0,.5});
    q.path.max_portal_tests=0;find_route(q);EXPECT_EQ(route.corridor.status,NAVIGATION_EXHAUSTED);EXPECT_EQ(route.point_count,0u);EXPECT_EQ(route.corridor.count,0u);
    q=query({.5,0,6.5},{6.5,0,.5});q.path.max_projection_tests=1;find_route(q);EXPECT_EQ(route.corridor.status,NAVIGATION_EXHAUSTED);EXPECT_EQ(route.projection_tests,1u);
    q=query({.5,0,6.5},{6.5,0,.5});q.path.search.max_expansions=0;q.path.search.allow_partial=true;find_route(q);EXPECT_EQ(route.corridor.status,NAVIGATION_EXHAUSTED);EXPECT_GT(route.point_count,0u);
    auto desc=description();NavigationBake* rejected=nullptr;desc.proceed=[](void*,NavigationBakeStage stage,size_t){return stage!=NAVIGATION_BAKE_GRAPH;};
    EXPECT_FALSE(navigation_bake_create(&desc,&rejected,&error));EXPECT_EQ(rejected,nullptr);EXPECT_NE(strstr(error.message,"cancelled"),nullptr);
    desc=description();desc.settings.max_work=stats.work-1;EXPECT_FALSE(navigation_bake_create(&desc,&rejected,&error));EXPECT_EQ(rejected,nullptr);
    EXPECT_NE(strstr(error.message,"work budget"),nullptr);
    desc=description();desc.settings.representation=99;EXPECT_FALSE(navigation_bake_create(&desc,&rejected,&error));EXPECT_EQ(rejected,nullptr);
}
TEST_F(NavigationBakeRegionTest, SeededPartitionsAgreeWithIndependentGridReachabilityAndDomainSamples) {
    uint32_t seed=0xb329126du;auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
    for(unsigned trial=0;trial<48;trial++) {
        bool floor[49]={},visited[49]={};for(auto& value:floor)value=random()%5!=0;floor[0]=floor[48]=true;
        lam::ArrayList<NavigationBakeCell> cells;for(size_t z=0;z<7;z++)for(size_t x=0;x<7;x++)if(floor[x+7*z])ASSERT_TRUE(cells.append({x,z,0,6,1,1}));
        navigation_bake_destroy(bake);bake=nullptr;ASSERT_TRUE(navigation_bake_from_cells(&settings,cells.data(),cells.size(),&bake,&error))<<trial;
        size_t queue[49]={},begin=0,end=1;visited[0]=true;
        while(begin<end) {
            size_t index=queue[begin++],x=index%7,z=index/7;const int offsets[4][2]={{-1,0},{1,0},{0,-1},{0,1}};
            for(auto& offset:offsets){int64_t nx=(int64_t)x+offset[0],nz=(int64_t)z+offset[1];if(nx<0||nz<0||nx>=7||nz>=7)continue;
                size_t neighbor=(size_t)nx+7*(size_t)nz;if(floor[neighbor]&&!visited[neighbor]){visited[neighbor]=true;queue[end++]=neighbor;}}
        }
        find_route(query({.25,0,.25},{6.75,0,6.75}));ASSERT_EQ(route.corridor.status,visited[48]?NAVIGATION_COMPLETE:NAVIGATION_UNREACHABLE)<<trial;
        if(visited[48]){EXPECT_GE(route.length,hypot(6.5,6.5)-1e-12);sampled_floor(floor);}
    }
}

TEST_F(NavigationBakeRegionTest, ScaledFunnelsAndSourceOrderPreserveRegionGeometry) {
    for(double scale:{1e-100,1.0,1e100}) {
        triangles.clear();settings.low={0,-scale,0};settings.high={7*scale,6*scale,7*scale};settings.cell_size=scale;settings.cell_height=.125*scale;
        settings.height=1.5*scale;settings.max_step=.375*scale;
        rectangle(0,0,7*scale,2*scale);rectangle(0,2*scale,2*scale,7*scale);create();ASSERT_NE(bake,nullptr);EXPECT_EQ(navigation_bake_region_count(bake),2u);
        find_route(query({.5*scale,0,6.5*scale},{6.5*scale,0,.5*scale}));ASSERT_EQ(route.corridor.status,NAVIGATION_COMPLETE);EXPECT_NEAR(route.length/scale,2*hypot(1.5,4.5),1e-12);
        auto* original=navigation_bake_retain(bake);
        for(size_t i=0;i<triangles.size()/2;i++){auto swap=triangles[i];triangles[i]=triangles[triangles.size()-1-i];triangles[triangles.size()-1-i]=swap;}
        create();EXPECT_TRUE(navigation_bake_equal(original,bake));navigation_bake_destroy(original);
    }
}
TEST_F(NavigationBakeRegionTest, CeilingAndZeroCostAreaBoundariesRemainIndependentDomains) {
    NavigationBakeCell cells[2]={{0,0,0,2,1,1},{1,0,0,6,1,1}};
    ASSERT_TRUE(navigation_bake_from_cells(&settings,cells,2,&bake,&error));EXPECT_EQ(navigation_bake_region_count(bake),2u);
    NavigationBakeRegion first={},second={};ASSERT_TRUE(navigation_bake_region(bake,1,&first));ASSERT_TRUE(navigation_bake_region(bake,2,&second));
    EXPECT_DOUBLE_EQ(first.ceiling,2);EXPECT_DOUBLE_EQ(second.ceiling,6);find_route(query({.25,0,.5},{1.75,0,.5}));ASSERT_EQ(route.corridor.status,NAVIGATION_COMPLETE);EXPECT_NEAR(route.length,1.5,1e-12);
    navigation_bake_destroy(bake);bake=nullptr;cells[0].ceiling=6;cells[0].cost=0;cells[1].cost=2;
    ASSERT_TRUE(navigation_bake_from_cells(&settings,cells,2,&bake,&error));EXPECT_EQ(navigation_bake_region_count(bake),2u);
    find_route(query({.25,0,.5},{1.75,0,.5}));ASSERT_EQ(route.corridor.status,NAVIGATION_COMPLETE);EXPECT_NEAR(route.travel_cost,1.5,1e-12);
}

TEST_F(NavigationTest, TransitMarkersNeverBecomePartialDestinations) {
    node(1,0);node(2,9);node(3,10);nodes[1].transit=true;edge(1,2,0);create();auto q=query(1,3);q.allow_partial=true;find(q);
    ASSERT_EQ(path.status,NAVIGATION_PARTIAL);ASSERT_EQ(path.count,1u);EXPECT_EQ(path.nodes[0],1u);
}
struct NavigationLinksTest : NavigationMeshTest {
    lam::ArrayList<NavigationLinkDesc> declarations;NavigationLinks* linked=nullptr;size_t attachment_budget=NAVIGATION_MAX_ATTACHMENT_TESTS;
    void TearDown()override{navigation_links_destroy(linked);NavigationMeshTest::TearDown();}
    void link(uint64_t id,NavigationVec start,NavigationVec end) {
        NavigationLinkDesc desc={};desc.id=id;desc.start=start;desc.end=end;desc.width=1;desc.height=DBL_MAX;desc.duration=.5;
        desc.membership=UINT32_MAX;desc.type=42;desc.automatic_cost=true;desc.max_projection_distance=.01;ASSERT_TRUE(declarations.append(desc));
    }
    void create_links() {
        navigation_links_destroy(linked);linked=nullptr;NavigationSurfaceView surface={};ASSERT_TRUE(navigation_mesh_surface(mesh,&surface));
        NavigationLinksDesc desc={declarations.data(),declarations.size(),attachment_budget};
        ASSERT_TRUE(navigation_links_create(&surface,&desc,&linked,&error))<<(error.message?error.message:"")<<" at "<<error.id;
    }
    NavigationSurfaceQuery linked_query(NavigationVec start,NavigationVec goal){return {NavigationMeshTest::query(start,goal),0,0,0};}
    void route(NavigationSurfaceQuery q) {
        navigation_mesh_path_clear(&path);error={};ASSERT_TRUE(navigation_links_find(linked,&q,&path,&error))<<(error.message?error.message:"")<<" at "<<error.id;
    }
    void rooms(){cell(0,0);cell(3,0);create();link(7,{.75,.5,0},{3.25,.5,0});create_links();}
};
TEST_F(NavigationLinksTest, DisconnectedRoomsRetainActionEndpointsDirectionAndCost) {
    rooms();ASSERT_NE(linked,nullptr);auto q=linked_query({.25,.25,0},{3.75,.75,0});route(q);
    ASSERT_EQ(path.corridor.status,NAVIGATION_COMPLETE);ASSERT_EQ(path.traversal_count,1u);ASSERT_EQ(path.point_count,4u);
    auto action=path.traversals[0];EXPECT_EQ(action.id,7u);EXPECT_EQ(action.type,42u);EXPECT_FALSE(action.reverse);EXPECT_DOUBLE_EQ(action.duration,.5);
    EXPECT_EQ(action.start_point,1u);EXPECT_EQ(action.end_point,2u);point(path.points[action.start_point],.75,.5);point(path.points[action.end_point],3.25,.5);
    EXPECT_NEAR(path.length,2.5+2*hypot(.5,.25),1e-12);EXPECT_NEAR(path.travel_cost,path.length,1e-12);
    auto reverse=linked_query(q.path.goal,q.path.start);route(reverse);EXPECT_EQ(path.corridor.status,NAVIGATION_UNREACHABLE);
    declarations[0].bidirectional=true;create_links();route(reverse);ASSERT_EQ(path.corridor.status,NAVIGATION_COMPLETE);ASSERT_EQ(path.traversal_count,1u);EXPECT_TRUE(path.traversals[0].reverse);
    point(path.traversals[0].start,3.25,.5);point(path.traversals[0].end,.75,.5);
}
TEST_F(NavigationLinksTest, ParallelActionsFiltersAndDisabledStateSelectTheActualTraversal) {
    cell(0,0);cell(3,0);create();link(11,{.75,.5,0},{3.25,.5,0});link(22,{.75,.5,0},{3.25,.5,0});
    declarations[0].automatic_cost=declarations[1].automatic_cost=false;declarations[0].cost=100;declarations[1].cost=0;
    declarations[0].membership=1;declarations[1].membership=2;create_links();auto q=linked_query({.25,.25,0},{3.75,.75,0});route(q);
    ASSERT_EQ(path.traversal_count,1u);EXPECT_EQ(path.traversals[0].id,22u);EXPECT_DOUBLE_EQ(path.traversals[0].cost,0);
    q.path.search.filter=1;route(q);ASSERT_EQ(path.traversal_count,1u);EXPECT_EQ(path.traversals[0].id,11u);
    declarations[0].disabled=true;create_links();route(q);EXPECT_EQ(path.corridor.status,NAVIGATION_UNREACHABLE);EXPECT_EQ(path.traversal_count,0u);
    q.path.search.filter=2;route(q);ASSERT_EQ(path.traversal_count,1u);EXPECT_EQ(path.traversals[0].id,22u);
}
TEST_F(NavigationLinksTest, ZeroLengthActionsPreserveDistinctDepartureAndArrivalIndices) {
    cell(0,0);cell(1,1);create();link(9,{1,1,0},{1,1,0});declarations[0].start_polygon=1;declarations[0].end_polygon=3;
    declarations[0].max_projection_distance=0;create_links();route(linked_query({.25,.25,0},{1.75,1.75,0}));
    ASSERT_EQ(path.corridor.status,NAVIGATION_COMPLETE);ASSERT_EQ(path.traversal_count,1u);ASSERT_EQ(path.point_count,4u);
    auto action=path.traversals[0];EXPECT_NE(action.start_point,action.end_point);point(path.points[action.start_point],1,1);point(path.points[action.end_point],1,1);
    EXPECT_DOUBLE_EQ(action.cost,0);EXPECT_NEAR(path.length,hypot(1.5,1.5),1e-12);
}
TEST_F(NavigationLinksTest, RetainedSurfacesAndCopiedDeclarationsOutliveTheirCallers) {
    rooms();NavigationSurfaceView surface={};ASSERT_TRUE(navigation_mesh_surface(mesh,&surface));NavigationLinks* copy=nullptr;
    NavigationLinksDesc desc={declarations.data(),declarations.size(),attachment_budget};ASSERT_TRUE(navigation_links_create(&surface,&desc,&copy,&error));EXPECT_TRUE(navigation_links_equal(linked,copy));
    navigation_links_destroy(copy);declarations[0].duration=3;ASSERT_TRUE(navigation_links_create(&surface,&desc,&copy,&error));EXPECT_FALSE(navigation_links_equal(linked,copy));navigation_links_destroy(copy);
    declarations.clear();navigation_mesh_destroy(mesh);mesh=nullptr;route(linked_query({.25,.25,0},{3.75,.75,0}));ASSERT_EQ(path.corridor.status,NAVIGATION_COMPLETE);
    NavigationLinkAttachment attachment={};ASSERT_TRUE(navigation_links_attachment(linked,0,&attachment));EXPECT_EQ(attachment.description.id,7u);EXPECT_DOUBLE_EQ(attachment.description.duration,.5);
}
TEST_F(NavigationLinksTest, ProjectionSearchAndAssemblyBudgetsNeverIssueUnverifiedActions) {
    rooms();auto q=linked_query({.25,.25,0},{3.75,.75,0});q.path.max_projection_tests=1;route(q);EXPECT_EQ(path.corridor.status,NAVIGATION_EXHAUSTED);EXPECT_EQ(path.projection_tests,1u);
    q=linked_query({.25,.25,0},{3.75,.75,0});q.path.max_portal_tests=0;route(q);EXPECT_EQ(path.corridor.status,NAVIGATION_EXHAUSTED);EXPECT_EQ(path.point_count,0u);EXPECT_EQ(path.traversal_count,0u);EXPECT_EQ(path.corridor.count,0u);
    for(size_t limit=0;limit<4;limit++) {
        q=linked_query({.25,.25,0},{3.75,.75,0});q.path.search.max_expansions=limit;q.path.search.allow_partial=true;route(q);
        ASSERT_EQ(path.corridor.status,NAVIGATION_EXHAUSTED);ASSERT_GT(path.corridor.count,0u);EXPECT_LE(path.corridor.nodes[path.corridor.count-1],navigation_links_polygon_count(linked));
    }
}
TEST_F(NavigationLinksTest, EndpointPinsSnapLimitsAndAttachmentBudgetsRejectAtomically) {
    rooms();NavigationSurfaceView surface={};ASSERT_TRUE(navigation_mesh_surface(mesh,&surface));NavigationLinks* rejected=nullptr;
    NavigationLinksDesc desc={declarations.data(),declarations.size(),1};EXPECT_FALSE(navigation_links_create(&surface,&desc,&rejected,&error));EXPECT_EQ(rejected,nullptr);EXPECT_NE(strstr(error.message,"budget"),nullptr);
    desc.max_attachment_tests=attachment_budget;declarations[0].start={-2,-2,0};EXPECT_FALSE(navigation_links_create(&surface,&desc,&rejected,&error));EXPECT_EQ(rejected,nullptr);
    declarations[0].max_projection_distance=4;ASSERT_TRUE(navigation_links_create(&surface,&desc,&rejected,&error));navigation_links_destroy(rejected);rejected=nullptr;
    declarations[0].start_polygon=99;EXPECT_FALSE(navigation_links_create(&surface,&desc,&rejected,&error));EXPECT_EQ(rejected,nullptr);
    declarations[0].start_polygon=0;declarations[0].cost=-1;EXPECT_FALSE(navigation_links_create(&surface,&desc,&rejected,&error));declarations[0].cost=0;
    auto duplicate=declarations[0];ASSERT_TRUE(declarations.append(duplicate));desc.links=declarations.data();desc.count=declarations.size();EXPECT_FALSE(navigation_links_create(&surface,&desc,&rejected,&error));EXPECT_EQ(rejected,nullptr);
}
TEST_F(NavigationLinksTest, ThreeDimensionalLinksKeepTheirVerticalTraversalExplicit) {
    dimensions=3;vertex(0,0,0);vertex(0,0,1);vertex(1,0,1);vertex(1,0,0);triangle(0,1,2);triangle(0,2,3);
    vertex(3,1,0);vertex(3,1,1);vertex(4,1,1);vertex(4,1,0);triangle(4,5,6);triangle(4,6,7);create();
    link(12,{.5,0,.5},{3.5,1,.5});create_links();route(linked_query({.5,0,.5},{3.5,1,.5}));
    ASSERT_EQ(path.corridor.status,NAVIGATION_COMPLETE);ASSERT_EQ(path.traversal_count,1u);EXPECT_DOUBLE_EQ(path.traversals[0].start.y,0);EXPECT_DOUBLE_EQ(path.traversals[0].end.y,1);EXPECT_NEAR(path.length,sqrt(10),1e-12);
}
TEST_F(NavigationBakeRegionTest, LinkedBakesApplyAgentAndLinkClearanceWithoutMutatingTheDomain) {
    for(unsigned representation:{NAVIGATION_BAKE_CELLS,NAVIGATION_BAKE_POLYGONS}) {
    settings.representation=representation;settings.radius=.25;NavigationBakeCell cells[2]={{0,0,0,6,1,1},{2,0,0,6,1,1}};
    ASSERT_TRUE(navigation_bake_from_cells(&settings,cells,2,&bake,&error));NavigationSurfaceView surface={};ASSERT_TRUE(navigation_bake_surface(bake,&surface));
    NavigationLinkDesc link={};link.id=1;link.start={.5,0,.5};link.end={2.5,0,.5};link.width=.4;link.height=1.4;link.automatic_cost=true;link.membership=1;
    NavigationLinksDesc desc={&link,1,100};NavigationLinks* linked=nullptr;ASSERT_TRUE(navigation_links_create(&surface,&desc,&linked,&error));
    NavigationSurfaceQuery q={world_query(link.start,link.end),.25,1.3,.375};NavigationMeshPath result={};
    ASSERT_TRUE(navigation_links_find(linked,&q,&result,&error));EXPECT_EQ(result.corridor.status,NAVIGATION_UNREACHABLE);navigation_mesh_path_clear(&result);
    q.radius=.1;q.height=1.5;ASSERT_TRUE(navigation_links_find(linked,&q,&result,&error));EXPECT_EQ(result.corridor.status,NAVIGATION_UNREACHABLE);navigation_mesh_path_clear(&result);
    q.height=1.3;ASSERT_TRUE(navigation_links_find(linked,&q,&result,&error));EXPECT_EQ(result.corridor.status,NAVIGATION_COMPLETE);EXPECT_EQ(result.traversal_count,1u);EXPECT_DOUBLE_EQ(result.travel_cost,2);navigation_mesh_path_clear(&result);
    q.radius=.3;EXPECT_FALSE(navigation_links_find(linked,&q,&result,&error));EXPECT_EQ(result.points,nullptr);navigation_links_destroy(linked);
    EXPECT_EQ(navigation_node_count(navigation_bake_graph(bake)),2u);EXPECT_EQ(navigation_edge_count(navigation_bake_graph(bake)),0u);    navigation_bake_destroy(bake);bake=nullptr;
    }
}

TEST_F(NavigationMeshTest, OnSurfaceEndpointsKeepTheirCoordinatesAtZeroSnapDistance) {
    cell(0,0);create();auto q=query({.75,.5,0},{.25,.5,0});q.max_projection_distance=0;find(q);
    ASSERT_EQ(path.corridor.status,NAVIGATION_COMPLETE);EXPECT_DOUBLE_EQ(path.start_distance,0);EXPECT_DOUBLE_EQ(path.goal_distance,0);point(path.projected_start,.75,.5);point(path.projected_goal,.25,.5);
    navigation_mesh_destroy(mesh);mesh=nullptr;vertices.clear();triangles.clear();dimensions=3;
    vertex(0,0,0);vertex(0,1,1);vertex(1,1,1);vertex(1,0,0);triangle(0,1,2);triangle(0,2,3);create();
    q=query({.75,.5,.5},{.25,.5,.5});q.max_projection_distance=0;find(q);ASSERT_EQ(path.corridor.status,NAVIGATION_COMPLETE);EXPECT_DOUBLE_EQ(path.start_distance,0);EXPECT_DOUBLE_EQ(path.goal_distance,0);
    point(path.projected_start,.75,.5,.5);point(path.projected_goal,.25,.5,.5);
    q.start.y=nextafter(.5,INFINITY);find(q);EXPECT_EQ(path.corridor.status,NAVIGATION_UNREACHABLE);
}

struct NavigationFollowerTest : testing::Test {
    NavigationFollower* follower=nullptr;NavigationError error={};NavigationFollowState state={};
    NavigationFollowerDesc desc={2,4,2,.015625,.03125,.5,1,.125,8};
    NavigationVec points[8]={{0,0,0},{2,0,0}};NavigationTraversal actions[4]={};
    NavigationMeshPath path={};
    void SetUp()override{path.points=points;path.point_count=2;path.corridor.status=NAVIGATION_COMPLETE;}
    void TearDown()override{navigation_follower_destroy(follower);}
    void create(){ASSERT_TRUE(navigation_follower_create(&desc,&follower,&error));ASSERT_TRUE(navigation_follower_assign(follower,&path,7,&error))<<(error.message?error.message:"");}
    void observe(NavigationVec position,double delta=.25,uint64_t generation=7){ASSERT_TRUE(navigation_follower_step(follower,position,generation,delta,&state,&error))<<(error.message?error.message:"");}
    void action(size_t index,uint64_t id,size_t departure){actions[index]={id,42,false,points[departure],points[departure+1],departure,departure+1,0,.5};path.traversals=actions;path.traversal_count=index+1;}
};
TEST_F(NavigationFollowerTest, CopiesRoutesAndAdvancesOnlyFromAchievedPositions) {
    points[2]={2,2,0};path.point_count=3;create();points[1]={100,100,0};observe({0,0,0});
    EXPECT_EQ(state.status,NAVIGATION_FOLLOW_MOVING);EXPECT_EQ(state.waypoint,1u);EXPECT_DOUBLE_EQ(state.distance,2);EXPECT_GT(state.displacement.x,0);
    observe({0,0,0});EXPECT_EQ(state.waypoint,1u);observe({2,0,0});EXPECT_EQ(state.waypoint,2u);EXPECT_EQ(state.status,NAVIGATION_FOLLOW_MOVING);
    observe({2,2,0});EXPECT_EQ(state.status,NAVIGATION_FOLLOW_ARRIVED);EXPECT_DOUBLE_EQ(state.displacement.x,0);EXPECT_DOUBLE_EQ(state.displacement.y,0);EXPECT_TRUE(state.complete);
}
TEST_F(NavigationFollowerTest, StuckDetectionUsesCumulativeActualProgressRatherThanIssuedMovement) {
    create();observe({0,0,0});for(unsigned i=0;i<4;i++)observe({0,0,0});EXPECT_EQ(state.status,NAVIGATION_FOLLOW_STUCK);EXPECT_DOUBLE_EQ(state.stalled_time,1);
    ASSERT_TRUE(navigation_follower_assign(follower,&path,7,&error));observe({0,0,0});
    for(unsigned i=1;i<=4;i++)observe({i*.03125,0,0});EXPECT_EQ(state.status,NAVIGATION_FOLLOW_MOVING);EXPECT_DOUBLE_EQ(state.stalled_time,0);
}
TEST_F(NavigationFollowerTest, SpeedAndAccelerationLimitsApplyToPreferredSteering) {
    points[1]={100,0,0};create();NavigationVec position={};double previous=0;
    for(unsigned i=0;i<24;i++) {
        observe(position,.125);EXPECT_EQ(state.status,NAVIGATION_FOLLOW_MOVING);EXPECT_LE(state.velocity.x,4);EXPECT_NEAR(state.velocity.y,0,1e-15);
        EXPECT_LE(state.velocity.x-previous,.25+1e-12);EXPECT_DOUBLE_EQ(state.displacement.x,state.velocity.x*.125);
        position.x+=state.displacement.x;previous=state.velocity.x;
    }
    EXPECT_DOUBLE_EQ(previous,4);
}
TEST_F(NavigationFollowerTest, WaypointWorkBudgetsRetainThePendingCursorAndIssueNoUncheckedStep) {
    desc.max_waypoints=2;for(unsigned i=0;i<5;i++)points[i]={};points[5]={3,0,0};path.point_count=6;create();
    observe({});EXPECT_TRUE(state.budget_exhausted);EXPECT_EQ(state.waypoint_tests,2u);EXPECT_EQ(state.waypoint,2u);EXPECT_DOUBLE_EQ(state.displacement.x,0);
    observe({});EXPECT_TRUE(state.budget_exhausted);EXPECT_EQ(state.waypoint,4u);
    observe({});EXPECT_FALSE(state.budget_exhausted);EXPECT_EQ(state.waypoint,5u);EXPECT_GT(state.displacement.x,0);
}
TEST_F(NavigationFollowerTest, StaleGenerationsAndOffPathMotionStopBeforeAnotherMovementRequest) {
    create();observe({});observe({},.25,8);EXPECT_EQ(state.status,NAVIGATION_FOLLOW_STALE);EXPECT_DOUBLE_EQ(state.displacement.x,0);
    ASSERT_TRUE(navigation_follower_assign(follower,&path,8,&error));observe({0,1,0},.25,8);EXPECT_EQ(state.status,NAVIGATION_FOLLOW_OFF_PATH);EXPECT_DOUBLE_EQ(state.velocity.x,0);
    NavigationFollowState sentinel={};sentinel.status=NAVIGATION_FOLLOW_ARRIVED;
    EXPECT_FALSE(navigation_follower_step(follower,{NAN,0,0},8,.25,&sentinel,&error));EXPECT_EQ(sentinel.status,NAVIGATION_FOLLOW_ARRIVED);
    ASSERT_TRUE(navigation_follower_read(follower,&state));EXPECT_EQ(state.status,NAVIGATION_FOLLOW_OFF_PATH);
}
TEST_F(NavigationFollowerTest, ExplicitActionsRequireMatchingGenerationAndAchievedArrival) {
    points[1]={1,0,0};points[2]={4,0,0};points[3]={5,0,0};path.point_count=4;action(0,77,1);create();
    observe({});observe({1,0,0});EXPECT_EQ(state.status,NAVIGATION_FOLLOW_ACTION);EXPECT_EQ(state.action,77u);EXPECT_DOUBLE_EQ(state.displacement.x,0);
    observe({1,0,0},4);EXPECT_EQ(state.status,NAVIGATION_FOLLOW_ACTION);EXPECT_DOUBLE_EQ(state.stalled_time,0);
    EXPECT_FALSE(navigation_follower_complete_link(follower,8,77,{4,0,0},&error));EXPECT_FALSE(navigation_follower_complete_link(follower,7,78,{4,0,0},&error));
    EXPECT_FALSE(navigation_follower_complete_link(follower,7,77,{1,0,0},&error));observe({4,0,0});EXPECT_EQ(state.status,NAVIGATION_FOLLOW_ACTION);
    ASSERT_TRUE(navigation_follower_complete_link(follower,7,77,{4,0,0},&error));observe({4,0,0});EXPECT_EQ(state.status,NAVIGATION_FOLLOW_MOVING);EXPECT_EQ(state.traversal,1u);EXPECT_EQ(state.action,0u);
    observe({5,0,0});EXPECT_EQ(state.status,NAVIGATION_FOLLOW_ARRIVED);
}
TEST_F(NavigationFollowerTest, AdjacentZeroLengthActionsStillPauseIndependently) {
    for(unsigned i=0;i<3;i++)points[i]={};points[3]={2,0,0};path.point_count=4;action(0,1,0);action(1,2,1);create();
    observe({});EXPECT_EQ(state.status,NAVIGATION_FOLLOW_ACTION);EXPECT_EQ(state.action,1u);
    ASSERT_TRUE(navigation_follower_complete_link(follower,7,1,{},&error));observe({});EXPECT_EQ(state.status,NAVIGATION_FOLLOW_ACTION);EXPECT_EQ(state.action,2u);
    ASSERT_TRUE(navigation_follower_complete_link(follower,7,2,{},&error));observe({});EXPECT_EQ(state.status,NAVIGATION_FOLLOW_MOVING);EXPECT_EQ(state.waypoint,3u);
}
TEST_F(NavigationFollowerTest, PartialRouteEndsDoNotClaimTheRequestedTargetWasReached) {
    path.corridor.status=NAVIGATION_PARTIAL;create();observe({});observe(points[1]);EXPECT_EQ(state.status,NAVIGATION_FOLLOW_PATH_END);EXPECT_FALSE(state.complete);
    navigation_follower_cancel(follower);ASSERT_TRUE(navigation_follower_read(follower,&state));EXPECT_EQ(state.status,NAVIGATION_FOLLOW_IDLE);EXPECT_EQ(state.generation,0u);
    observe({},.25,99);EXPECT_EQ(state.status,NAVIGATION_FOLLOW_IDLE);
}
TEST_F(NavigationFollowerTest, InvalidDescriptionsAndRoutesPreserveTheWorkingFollower) {
    create();observe({});auto before=state;NavigationFollower* rejected=nullptr;auto bad=desc;bad.max_acceleration=0;
    EXPECT_FALSE(navigation_follower_create(&bad,&rejected,&error));EXPECT_EQ(rejected,nullptr);
    points[1].x=INFINITY;EXPECT_FALSE(navigation_follower_assign(follower,&path,8,&error));points[1]={2,0,0};
    action(0,1,0);actions[0].end_point=7;EXPECT_FALSE(navigation_follower_assign(follower,&path,8,&error));
    EXPECT_FALSE(navigation_follower_step(follower,{},7,0,&state,&error));EXPECT_EQ(state.status,before.status);EXPECT_EQ(state.generation,before.generation);
    observe({.25,0,0});EXPECT_EQ(state.status,NAVIGATION_FOLLOW_MOVING);EXPECT_EQ(state.generation,7u);
}
TEST_F(NavigationFollowerTest, ThreeDimensionalRoutesRetainTheVerticalMotionAndScale) {
    desc.dimensions=3;points[1]={2,1,2};create();observe({});EXPECT_GT(state.velocity.x,0);EXPECT_GT(state.velocity.y,0);EXPECT_GT(state.velocity.z,0);
    EXPECT_NEAR(state.velocity.x,2*state.velocity.y,1e-15);EXPECT_NEAR(state.velocity.z,state.velocity.x,1e-15);
    observe(points[1]);EXPECT_EQ(state.status,NAVIGATION_FOLLOW_ARRIVED);
}
TEST_F(NavigationFollowerTest, GroundSteeringSkipsVerticalFloorSeamsWithoutLosingLinkHeightChecks) {
    desc.dimensions=3;desc.horizontal=true;points[1]={1,0,0};points[2]={1,.25,0};points[3]={2,.25,0};path.point_count=4;
    create();observe({});observe(points[1]);EXPECT_EQ(state.waypoint,3u);EXPECT_GT(state.displacement.x,0);EXPECT_DOUBLE_EQ(state.displacement.y,0);
    navigation_follower_destroy(follower);follower=nullptr;action(0,42,1);create();observe({});observe(points[1]);ASSERT_EQ(state.status,NAVIGATION_FOLLOW_ACTION);
    EXPECT_FALSE(navigation_follower_complete_link(follower,7,42,points[1],&error));
    ASSERT_TRUE(navigation_follower_complete_link(follower,7,42,points[2],&error));observe(points[2]);observe(points[3]);EXPECT_EQ(state.status,NAVIGATION_FOLLOW_ARRIVED);
}

struct NavigationMotionProbe {
    NavigationSurfaceView view={};NavigationSurfaceMotion motion={};NavigationError error={};
    NavigationSurfaceMove query(NavigationVec start,NavigationVec goal,uint64_t polygon=1) {
        auto path=world_query(start,goal);path.max_projection_distance=0;
        return {{path,view.radius,view.clearance?view.height:0,view.max_step},polygon,NAVIGATION_MOVE_MAX_POLYGONS,4096};
    }
    void walk(NavigationSurfaceMove move){error={};ASSERT_TRUE(navigation_surface_walk(&view,&move,&motion,&error))<<(error.message?error.message:"")<<" at "<<error.id;}
};
TEST_F(NavigationMeshTest, SurfaceMotionWalksConnectedFacesAcrossScales) {
    for(double scale:{1e-100,1.0,1e100}) {
        cell(0,0);cell(1,0);for(auto& p:vertices){p.x*=scale;p.y*=scale;}create();NavigationMotionProbe probe;ASSERT_TRUE(navigation_mesh_surface(mesh,&probe.view));
        probe.walk(probe.query({.2*scale,.1*scale,0},{1.8*scale,.9*scale,0}));ASSERT_EQ(probe.motion.status,NAVIGATION_COMPLETE);
        EXPECT_DOUBLE_EQ(probe.motion.fraction,1);EXPECT_NEAR(probe.motion.end.x/scale,1.8,1e-12);EXPECT_NEAR(probe.motion.end.y/scale,.9,1e-12);
        EXPECT_NEAR(probe.motion.length/scale,hypot(1.6,.8),1e-12);EXPECT_GT(probe.motion.polygon_count,1u);EXPECT_EQ(probe.motion.point_count,2u);
        navigation_mesh_destroy(mesh);mesh=nullptr;vertices.clear();triangles.clear();
    }
}
TEST_F(NavigationMeshTest, SurfaceMotionStopsAtDisconnectedBoundariesInsteadOfProjectingAcrossAGap) {
    cell(0,0);cell(3,0);create();NavigationMotionProbe probe;ASSERT_TRUE(navigation_mesh_surface(mesh,&probe.view));
    probe.walk(probe.query({.2,.1,0},{3.8,.9,0}));ASSERT_EQ(probe.motion.status,NAVIGATION_PARTIAL);point(probe.motion.end,1,.1+.8*(.8/3.6));
    EXPECT_NEAR(probe.motion.fraction,.8/3.6,1e-12);EXPECT_EQ(probe.motion.polygon,1u);
}
TEST_F(NavigationMeshTest, SurfaceMotionSearchesTheVertexFanBeyondAFilteredDeadEnd) {
    cell(0,0,1,1);cell(1,0,1,1);cell(0,1,1,2);cell(1,1,1,1);create();NavigationMotionProbe probe;ASSERT_TRUE(navigation_mesh_surface(mesh,&probe.view));
    auto q=probe.query({.25,.25,0},{1.75,1.75,0});q.profile.path.search.filter=1;probe.walk(q);ASSERT_EQ(probe.motion.status,NAVIGATION_COMPLETE);
    point(probe.motion.end,1.75,1.75);EXPECT_EQ(probe.motion.point_count,2u);
    for(size_t i=0;i<probe.motion.polygon_count;i++){NavigationNodeDesc node={};ASSERT_TRUE(navigation_node(probe.view.graph,probe.motion.polygons[i],&node));EXPECT_NE(node.membership&1u,0u);}
}
TEST_F(NavigationMeshTest, SurfaceMotionBudgetsReturnVerifiedPrefixesAndInvalidQueriesPreserveTheResult) {
    cell(0,0);create();NavigationMotionProbe probe;ASSERT_TRUE(navigation_mesh_surface(mesh,&probe.view));auto q=probe.query({.25,.125,0},{.75,.25,0});
    q.max_polygon_tests=1;q.max_edge_tests=0;probe.walk(q);ASSERT_EQ(probe.motion.status,NAVIGATION_COMPLETE);
    q.profile.path.goal={2,.25,0};q.max_polygon_tests=0;probe.walk(q);EXPECT_EQ(probe.motion.status,NAVIGATION_EXHAUSTED);EXPECT_EQ(probe.motion.point_count,0u);EXPECT_EQ(probe.motion.polygon_tests,0u);
    q.max_polygon_tests=1;probe.walk(q);EXPECT_EQ(probe.motion.status,NAVIGATION_EXHAUSTED);EXPECT_EQ(probe.motion.edge_tests,0u);point(probe.motion.end,1,.125+.125*(.75/1.75));
    auto before=probe.motion;q.polygon=99;EXPECT_FALSE(navigation_surface_walk(&probe.view,&q,&probe.motion,&probe.error));EXPECT_EQ(probe.motion.status,before.status);EXPECT_DOUBLE_EQ(probe.motion.fraction,before.fraction);
    q.polygon=1;q.profile.path.start.x=NAN;EXPECT_FALSE(navigation_surface_walk(&probe.view,&q,&probe.motion,&probe.error));EXPECT_DOUBLE_EQ(probe.motion.end.x,before.end.x);
    q.profile.path.start={-1,0,0};q.max_polygon_tests=64;q.max_edge_tests=4096;probe.walk(q);EXPECT_EQ(probe.motion.status,NAVIGATION_UNREACHABLE);EXPECT_EQ(probe.motion.point_count,0u);
}
TEST_F(NavigationMeshTest, SurfaceMotionGroundsFoldedSurfacesWithoutFollowingTheRequestsVerticalOffset) {
    dimensions=3;vertex(0,0,0);vertex(0,0,1);vertex(1,0,1);vertex(1,0,0);vertex(2,1,1);vertex(2,1,0);
    triangle(0,1,2);triangle(0,2,3);triangle(3,2,4);triangle(3,4,5);create();find(query({.25,0,.5},{1.75,.75,.5}));ASSERT_EQ(path.corridor.status,NAVIGATION_COMPLETE);
    NavigationMotionProbe probe;ASSERT_TRUE(navigation_mesh_surface(mesh,&probe.view));probe.walk(probe.query({.25,0,.5},{1.75,100,.5},path.start_polygon));
    ASSERT_EQ(probe.motion.status,NAVIGATION_COMPLETE);point(probe.motion.end,1.75,.75,.5);ASSERT_EQ(probe.motion.point_count,3u);point(probe.motion.points[1],1,0,.5);
    EXPECT_NEAR(probe.motion.length,.75+hypot(.75,.75),1e-12);
}
TEST_F(NavigationBakeRegionTest, SurfaceMotionRetainsStepHeightsAndClearanceForBothBakeRepresentations) {
    for(unsigned representation:{NAVIGATION_BAKE_CELLS,NAVIGATION_BAKE_POLYGONS}) {
        settings.representation=representation;settings.radius=.25;NavigationBakeCell cells[3]={{0,0,0,6,1,1},{1,0,.25,6,1,1},{2,0,.5,6,1,1}};
        ASSERT_TRUE(navigation_bake_from_cells(&settings,cells,3,&bake,&error));NavigationMotionProbe probe;ASSERT_TRUE(navigation_bake_surface(bake,&probe.view));
        auto q=probe.query({.25,0,.5},{2.75,0,.5});probe.walk(q);ASSERT_EQ(probe.motion.status,NAVIGATION_COMPLETE);EXPECT_DOUBLE_EQ(probe.motion.end.y,.5);EXPECT_NEAR(probe.motion.length,3,1e-12);
        ASSERT_EQ(probe.motion.point_count,6u);EXPECT_DOUBLE_EQ(probe.motion.points[1].y,0);EXPECT_DOUBLE_EQ(probe.motion.points[2].y,.25);EXPECT_DOUBLE_EQ(probe.motion.points[3].y,.25);EXPECT_DOUBLE_EQ(probe.motion.points[4].y,.5);
        q.profile.max_step=.125;EXPECT_FALSE(navigation_surface_walk(&probe.view,&q,&probe.motion,&probe.error));q.profile.max_step=settings.max_step;q.profile.radius=.5;
        EXPECT_FALSE(navigation_surface_walk(&probe.view,&q,&probe.motion,&probe.error));navigation_bake_destroy(bake);bake=nullptr;
    }
}
TEST_F(NavigationBakeRegionTest, SurfaceMotionCannotFollowStraightSteeringAcrossABakedHole) {
    for(unsigned representation:{NAVIGATION_BAKE_CELLS,NAVIGATION_BAKE_POLYGONS}) {
        settings.representation=representation;settings.radius=.25;
        for(unsigned z=0;z<7;z++)for(unsigned x=0;x<7;x++)if(x!=3||z!=3)rectangle(x,z,x+1,z+1);
        create();auto source=query({1.25,0,3.5},{5.75,0,3.5});find_route(source);ASSERT_EQ(route.corridor.status,NAVIGATION_COMPLETE);
        NavigationMotionProbe probe;ASSERT_TRUE(navigation_bake_surface(bake,&probe.view));probe.walk(probe.query(source.path.start,source.path.goal,route.start_polygon));
        ASSERT_EQ(probe.motion.status,NAVIGATION_PARTIAL);EXPECT_NEAR(probe.motion.end.x,2,1e-12);EXPECT_DOUBLE_EQ(probe.motion.end.z,3.5);EXPECT_LT(probe.motion.fraction,1);
        triangles.clear();
    }
}
TEST_F(NavigationMeshTest, SeededSurfaceMotionAgreesWithAnIndependentRectangleIntervalOracle) {
    uint32_t seed=0x6125e9abu;auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
    for(unsigned trial=0;trial<64;trial++) {
        bool floor[49]={};for(auto& present:floor)present=random()%4!=0;floor[0]=true;
        for(unsigned y=0;y<7;y++)for(unsigned x=0;x<7;x++)if(floor[x+7*y])cell(x,y);
        create();NavigationMotionProbe probe;ASSERT_TRUE(navigation_mesh_surface(mesh,&probe.view));
        for(unsigned request=0;request<4;request++) {
            NavigationVec start={.3125,.15625,0},goal={.1234567+(random()%6400)/1000.0,.2345678+(random()%6300)/1000.0,0};
            double intervals[49][2]={};size_t count=0;
            for(unsigned y=0;y<7;y++)for(unsigned x=0;x<7;x++)if(floor[x+7*y]) {
                double low=0,high=1,origins[2]={start.x,start.y},deltas[2]={goal.x-start.x,goal.y-start.y},minimum[2]={(double)x,(double)y};
                for(unsigned axis=0;axis<2;axis++) {
                    if(deltas[axis]==0){if(origins[axis]<minimum[axis]||origins[axis]>minimum[axis]+1){low=1;high=0;}continue;}
                    double a=(minimum[axis]-origins[axis])/deltas[axis],b=(minimum[axis]+1-origins[axis])/deltas[axis];low=fmax(low,fmin(a,b));high=fmin(high,fmax(a,b));
                }
                if(low>high)continue;size_t at=count++;while(at&&intervals[at-1][0]>low){intervals[at][0]=intervals[at-1][0];intervals[at][1]=intervals[at-1][1];at--;}
                intervals[at][0]=low;intervals[at][1]=high;
            }
            double reachable=0;for(size_t i=0;i<count;i++){if(intervals[i][0]>reachable+1e-12)break;reachable=fmax(reachable,intervals[i][1]);}
            probe.walk(probe.query(start,goal));ASSERT_NE(probe.motion.status,NAVIGATION_EXHAUSTED)<<trial<<","<<request;
            ASSERT_NEAR(probe.motion.fraction,reachable,1e-10)<<trial<<","<<request;EXPECT_EQ(probe.motion.status,reachable==1?NAVIGATION_COMPLETE:NAVIGATION_PARTIAL);
            EXPECT_NEAR(probe.motion.end.x,start.x+(goal.x-start.x)*reachable,1e-10);EXPECT_NEAR(probe.motion.end.y,start.y+(goal.y-start.y)*reachable,1e-10);
        }
        navigation_mesh_destroy(mesh);mesh=nullptr;vertices.clear();triangles.clear();
    }
}

static NavigationAvoidanceAgent crowd_agent(uint64_t id, NavigationVec position, NavigationVec preferred = {}) {
    return {id, position, preferred, .25, 1, 2, .5, 2, true, 31, 31, NAVIGATION_AVOIDANCE_MAX_SOLVER_TESTS};
}
TEST(NavigationAvoidanceTest, ReciprocalTangentMatchesAnalyticSeparationAndExplicitPriority) {
    NavigationAvoidanceAgent agents[] = {crowd_agent(1, {-2, 0, 0}, {2, 0, 0}), crowd_agent(2, {2, 0, 0}, {-2, 0, 0})};
    NavigationAvoidanceResult result[2] = {};
    NavigationError error = {};
    ASSERT_TRUE(navigation_avoidance_step(agents, 2, .1, result, &error)) << error.message;
    EXPECT_EQ(result[0].status, NAVIGATION_AVOIDANCE_ADJUSTED);
    EXPECT_EQ(result[0].neighbors, 1u);
    EXPECT_NEAR(result[0].velocity.x, 2 * (1 - .125 * .125), 1e-14);
    EXPECT_NEAR(result[0].velocity.z, -2 * .125 * sqrt(1 - .125 * .125), 1e-14);
    EXPECT_NEAR(result[0].velocity.x, -result[1].velocity.x, 1e-14);
    EXPECT_NEAR(result[0].velocity.z, -result[1].velocity.z, 1e-14);
    agents[0].priority = 1;
    agents[1].priority = 0;
    ASSERT_TRUE(navigation_avoidance_step(agents, 2, .1, result, &error));
    EXPECT_DOUBLE_EQ(result[0].velocity.x, 2);
    EXPECT_DOUBLE_EQ(result[0].velocity.z, 0);
    NavigationVec velocity[] = {result[0].velocity, result[1].velocity};
    bool clear = false;
    ASSERT_TRUE(navigation_avoidance_verify(agents, velocity, 2, 2, &clear, &error));
    EXPECT_TRUE(clear);
    agents[1].responsive = false;
    agents[0].priority = .5;
    ASSERT_TRUE(navigation_avoidance_step(agents, 2, .1, result, &error));
    EXPECT_EQ(result[1].status, NAVIGATION_AVOIDANCE_DISABLED);
    EXPECT_NEAR(result[0].velocity.z, -4 * .125 * sqrt(1 - .125 * .125), 1e-14);
}
TEST(NavigationAvoidanceTest, CrossingsAndHeadOnRoutesPreserveContinuousDiscSeparation) {
    for (unsigned crossing = 0; crossing < 2; crossing++) {
        NavigationAvoidanceAgent agents[] = {
            crowd_agent(1, {-2, 0, 0}), crowd_agent(2, crossing ? NavigationVec{0, 0, -2} : NavigationVec{2, 0, 0})};
        NavigationVec goals[] = {{2, 0, 0}, crossing ? NavigationVec{0, 0, 2} : NavigationVec{-2, 0, 0}};
        NavigationError error = {};
        for (unsigned tick = 0; tick < 150; tick++) {
            for (unsigned i = 0; i < 2; i++) {
                double x = goals[i].x - agents[i].position.x, z = goals[i].z - agents[i].position.z,
                       distance = hypot(x, z), speed = fmin(2, distance / .05);
                agents[i].preferred =
                    distance ? NavigationVec{x / distance * speed, 0, z / distance * speed} : NavigationVec{};
                agents[i].responsive = distance > .025;
            }
            NavigationAvoidanceResult result[2] = {};
            ASSERT_TRUE(navigation_avoidance_step(agents, 2, .05, result, &error)) << error.message;
            for (auto& r : result) {
                ASSERT_NE(r.status, NAVIGATION_AVOIDANCE_BLOCKED);
                ASSERT_NE(r.status, NAVIGATION_AVOIDANCE_EXHAUSTED);
                EXPECT_LE(hypot(r.velocity.x, r.velocity.z), 2 + 1e-12);
            }
            // independent closest-time oracle protects the complete step, not just sampled endpoints.
            double x = agents[1].position.x - agents[0].position.x, z = agents[1].position.z - agents[0].position.z,
                   vx = result[1].velocity.x - result[0].velocity.x, vz = result[1].velocity.z - result[0].velocity.z,
                   squared = vx * vx + vz * vz, time = squared ? fmax(0, fmin(.05, -(x * vx + z * vz) / squared)) : 0;
            EXPECT_GE(hypot(x + time * vx, z + time * vz), .5 - 1e-12) << crossing << ":" << tick;
            for (unsigned i = 0; i < 2; i++) {
                agents[i].position.x += result[i].velocity.x * .05;
                agents[i].position.z += result[i].velocity.z * .05;
            }
        }
        for (unsigned i = 0; i < 2; i++)
            EXPECT_LT(navigation_vec_distance(agents[i].position, goals[i]), .025) << crossing << ":" << i;
    }
}
TEST(NavigationAvoidanceTest, SharedIndexIsOrderIndependentAndSeparatesVerticalLayers) {
    NavigationAvoidanceAgent agents[] = {crowd_agent(17, {-2, 0, 0}, {2, 0, 0}), crowd_agent(4, {2, 0, 0}, {-2, 0, 0}),
                                         crowd_agent(9, {0, 3, 0}, {0, 0, 2})};
    NavigationAvoidanceResult before[3] = {}, after[3] = {};
    NavigationError error = {};
    ASSERT_TRUE(navigation_avoidance_step(agents, 3, .1, before, &error));
    EXPECT_EQ(before[2].neighbors, 0u);
    EXPECT_EQ(before[2].status, NAVIGATION_AVOIDANCE_CLEAR);
    auto swap = agents[0];
    agents[0] = agents[2];
    agents[2] = swap;
    ASSERT_TRUE(navigation_avoidance_step(agents, 3, .1, after, &error));
    for (unsigned i = 0; i < 3; i++) {
        EXPECT_DOUBLE_EQ(before[i].velocity.x, after[2 - i].velocity.x);
        EXPECT_DOUBLE_EQ(before[i].velocity.z, after[2 - i].velocity.z);
        EXPECT_EQ(before[i].neighbors, after[2 - i].neighbors);
    }
    NavigationAvoidanceAgent sparse[NAVIGATION_AVOIDANCE_MAX_AGENTS];
    NavigationAvoidanceResult results[NAVIGATION_AVOIDANCE_MAX_AGENTS];
    for (unsigned i = 0; i < NAVIGATION_AVOIDANCE_MAX_AGENTS; i++)
        sparse[i] = crowd_agent(i + 1, {100.0 * i, 0, 0}, {1, 0, 0});
    ASSERT_TRUE(navigation_avoidance_step(sparse, NAVIGATION_AVOIDANCE_MAX_AGENTS, .1, results, &error));
    for (auto& result : results) {
        EXPECT_EQ(result.neighbor_tests, 0u);
        EXPECT_EQ(result.solver_tests, 0u);
        EXPECT_DOUBLE_EQ(result.velocity.x, 1);
    }
}
TEST(NavigationAvoidanceTest, WorkExhaustionAndInfeasiblePlansReturnStoppedCandidates) {
    NavigationAvoidanceAgent agents[] = {crowd_agent(1, {0, 0, 0}, {1, 0, 0}), crowd_agent(2, {1, 0, 0}, {-1, 0, 0})};
    NavigationAvoidanceResult result[3] = {};
    NavigationError error = {};
    for (unsigned budget = 0; budget < 3; budget++) {
        auto saved = agents[0];
        if (budget == 0)
            agents[0].max_neighbors = 0;
        else if (budget == 1)
            agents[0].max_neighbor_tests = 0;
        else
            agents[0].max_solver_tests = 0;
        ASSERT_TRUE(navigation_avoidance_step(agents, 2, .1, result, &error));
        EXPECT_EQ(result[0].status, NAVIGATION_AVOIDANCE_EXHAUSTED);
        EXPECT_DOUBLE_EQ(navigation_vec_distance(result[0].velocity, {}), 0);
        agents[0] = saved;
    }
    NavigationAvoidanceAgent trapped[] = {crowd_agent(1, {0, 0, 0}, {.1, 0, 0}), crowd_agent(2, {.4, 0, 0}),
                                          crowd_agent(3, {-.4, 0, 0})};
    trapped[0].max_speed = .1;
    trapped[1].responsive = trapped[2].responsive = false;
    ASSERT_TRUE(navigation_avoidance_step(trapped, 3, .1, result, &error));
    EXPECT_EQ(result[0].status, NAVIGATION_AVOIDANCE_BLOCKED);
}
TEST(NavigationAvoidanceTest, SimultaneousSweepGuardIncludesVerticalTargetsAndAllowsSeparation) {
    NavigationAvoidanceAgent agents[] = {crowd_agent(1, {-1, 0, 0}), crowd_agent(2, {1, 0, 0})};
    NavigationError error = {};
    bool clear = true;
    NavigationVec velocity[] = {{2, 0, 0}, {-2, 0, 0}};
    ASSERT_TRUE(navigation_avoidance_verify(agents, velocity, 2, 1, &clear, &error));
    EXPECT_FALSE(clear);
    agents[1].position.y = 2;
    ASSERT_TRUE(navigation_avoidance_verify(agents, velocity, 2, 1, &clear, &error));
    EXPECT_TRUE(clear);
    velocity[1].y = -2;
    ASSERT_TRUE(navigation_avoidance_verify(agents, velocity, 2, 1, &clear, &error));
    EXPECT_FALSE(clear);
    agents[0].position = {0, 0, 0};
    agents[1].position = {.25, 0, 0};
    velocity[0] = {-1, 0, 0};
    velocity[1] = {1, 0, 0};
    ASSERT_TRUE(navigation_avoidance_verify(agents, velocity, 2, 1, &clear, &error));
    EXPECT_TRUE(clear);
    velocity[0].x = 2;
    ASSERT_TRUE(navigation_avoidance_verify(agents, velocity, 2, 1, &clear, &error));
    EXPECT_FALSE(clear);
}
TEST(NavigationAvoidanceTest, InvalidSnapshotsPreserveAllCallerResults) {
    NavigationAvoidanceAgent agents[] = {crowd_agent(1, {0, 0, 0}), crowd_agent(2, {1, 0, 0})};
    NavigationAvoidanceResult result[2] = {};
    NavigationError error = {};
    result[0].velocity.x = 123;
    result[1].neighbors = 29;
    for (unsigned invalid = 0; invalid < 7; invalid++) {
        auto original = agents[1];
        if (invalid == 0)
            agents[1].id = 1;
        else if (invalid == 1)
            agents[1].radius = NAN;
        else if (invalid == 2)
            agents[1].height = -1;
        else if (invalid == 3)
            agents[1].preferred.y = 1;
        else if (invalid == 4)
            agents[1].priority = 2;
        else if (invalid == 5)
            agents[1].time_horizon = 0;
        else
            agents[1].max_neighbors = NAVIGATION_AVOIDANCE_MAX_AGENTS;
        EXPECT_FALSE(navigation_avoidance_step(agents, 2, .1, result, &error));
        EXPECT_DOUBLE_EQ(result[0].velocity.x, 123);
        EXPECT_EQ(result[1].neighbors, 29u);
        agents[1] = original;
    }
    EXPECT_FALSE(navigation_avoidance_step(agents, 2, 0, result, &error));
    EXPECT_TRUE(navigation_avoidance_step(nullptr, 0, .1, nullptr, &error));
    NavigationVec velocity[] = {{NAN, 0, 0}, {}};
    bool clear = true;
    EXPECT_FALSE(navigation_avoidance_verify(agents, velocity, 2, .1, &clear, &error));
    EXPECT_TRUE(clear);
}
