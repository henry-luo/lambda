#ifndef LAMBDA_NAVIGATION_H
#define LAMBDA_NAVIGATION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NAVIGATION_MAX_NODES 65536
#define NAVIGATION_MAX_EDGES 1048576
#define NAVIGATION_MAX_GUARDS 6

typedef struct NavigationVec {double x,y,z;} NavigationVec;
static inline double navigation_surface_axis(NavigationVec p,unsigned dimensions){return dimensions==2?p.y:p.z;}
double navigation_cross2(double ax,double ay,double bx,double by);
double navigation_surface_turn(NavigationVec origin,NavigationVec a,NavigationVec b,unsigned dimensions);
bool navigation_triangle_normal(NavigationVec a,NavigationVec b,NavigationVec c,double normal[3]);
static inline bool navigation_vec_finite(NavigationVec p){return isfinite(p.x)&&isfinite(p.y)&&isfinite(p.z);}
static inline double navigation_vec_distance(NavigationVec a,NavigationVec b){return hypot(hypot(a.x-b.x,a.y-b.y),a.z-b.z);}
typedef struct NavigationNodeDesc {
    uint64_t id;
    NavigationVec position;
    uint32_t membership;
    bool disabled;
    bool transit; // internal traversal markers cannot be partial route endpoints.
} NavigationNodeDesc;
typedef struct NavigationEdgeDesc {
    uint64_t from,to;
    double cost;
    uint32_t membership;
    bool disabled;
    // intermediate cells must admit the query too, preventing diagonal corner cuts.
    uint64_t guards[NAVIGATION_MAX_GUARDS];
    unsigned guard_count;
} NavigationEdgeDesc;
typedef struct NavigationDesc {
    unsigned dimensions;
    const NavigationNodeDesc* nodes;size_t node_count;
    const NavigationEdgeDesc* edges;size_t edge_count;
} NavigationDesc;
typedef struct NavigationGridDesc {
    unsigned dimensions;
    size_t size[3];
    NavigationVec origin,cell_size;
    const double* costs;
    const uint32_t* memberships;
    const bool* blocked;
    bool diagonals;
} NavigationGridDesc;
typedef struct NavigationGraph NavigationGraph;
typedef struct NavigationError {const char* message;uint64_t id;} NavigationError;
typedef struct NavigationQuery {
    uint64_t start,goal;
    uint32_t filter;
    size_t max_expansions,max_edge_tests;
    bool euclidean,allow_partial;
    bool (*admit_node)(void* owner,uint64_t id);
    void* admission_owner;
} NavigationQuery;
typedef enum NavigationStatus {
    NAVIGATION_COMPLETE,NAVIGATION_PARTIAL,NAVIGATION_UNREACHABLE,NAVIGATION_EXHAUSTED
} NavigationStatus;
typedef struct NavigationPath {
    NavigationStatus status;
    uint64_t* nodes;size_t count;
    double cost;
    size_t expanded,edge_tests,updates,frontier_peak;
} NavigationPath;

// graphs own immutable copies; providers do not perform IO, script calls or scene mutation.
bool navigation_create(const NavigationDesc* desc,NavigationGraph** graph,NavigationError* error);
bool navigation_create_grid(const NavigationGridDesc* desc,NavigationGraph** graph,NavigationError* error);
void navigation_destroy(NavigationGraph* graph);
bool navigation_node(const NavigationGraph* graph,uint64_t id,NavigationNodeDesc* node);
bool navigation_node_at(const NavigationGraph* graph,size_t index,NavigationNodeDesc* node);
bool navigation_edge_at(const NavigationGraph* graph,size_t index,NavigationEdgeDesc* edge);
bool navigation_outgoing_edges(const NavigationGraph* graph,uint64_t node,size_t* first,size_t* count);
bool navigation_node_admitted(const NavigationGraph* graph,uint64_t node,const NavigationQuery* query);
bool navigation_edge_admitted(const NavigationGraph* graph,size_t index,const NavigationQuery* query);
size_t navigation_node_index(const NavigationGraph* graph,uint64_t id);
size_t navigation_node_count(const NavigationGraph* graph);
size_t navigation_edge_count(const NavigationGraph* graph);
// ordering is part of the compiled representation; equality never relies on source pointers or padding.
bool navigation_equal(const NavigationGraph* left,const NavigationGraph* right);
// an empty result receives owned IDs on success. Errors leave the result unchanged.
bool navigation_find(const NavigationGraph* graph,const NavigationQuery* query,NavigationPath* path,NavigationError* error);
void navigation_path_clear(NavigationPath* path);

#define NAVIGATION_MAX_VERTICES 262144
typedef struct NavigationTriangleDesc {
    uint32_t vertices[3],membership;
    double cost;
    bool disabled;
} NavigationTriangleDesc;
typedef struct NavigationMeshDesc {
    unsigned dimensions;
    const NavigationVec* vertices;size_t vertex_count;
    const NavigationTriangleDesc* triangles;size_t triangle_count;
    double max_slope; // radians, Y-up 3D; 2D accepts either winding
    bool two_sided;
} NavigationMeshDesc;
typedef struct NavigationMesh NavigationMesh;
typedef struct NavigationMeshQuery {
    NavigationVec start,goal;
    NavigationQuery search; // node references are selected by endpoint projection
    double max_projection_distance;
    size_t max_projection_tests,max_portal_tests;
} NavigationMeshQuery;
typedef struct NavigationPortal {NavigationVec left,right;} NavigationPortal;
typedef struct NavigationTraversal {
    uint64_t id;uint32_t type;bool reverse;
    NavigationVec start,end;size_t start_point,end_point;double cost,duration;
} NavigationTraversal;
typedef struct NavigationMeshPath {
    NavigationPath corridor;
    NavigationVec* points;size_t point_count;
    NavigationPortal* portals;size_t portal_count;
    NavigationVec projected_start,projected_goal,path_end;
    uint64_t start_polygon,goal_polygon;
    double start_distance,goal_distance,length,travel_cost;
    size_t projection_tests,portal_tests;
    NavigationTraversal* traversals;size_t traversal_count;
} NavigationMeshPath;
typedef struct NavigationSurfaceQuery {NavigationMeshQuery path;double radius,height,max_step;} NavigationSurfaceQuery;
// native-only immutable surface adapters; the retained owner outlives linked routes.
typedef struct NavigationSurfaceView {
    unsigned dimensions;size_t polygon_count;const NavigationGraph* graph;void* owner;
    bool clearance;double radius,height,max_step;
    void* (*retain)(void* owner);
    void (*release)(void* owner);
    bool (*closest)(const void* owner,uint64_t polygon,NavigationVec point,NavigationVec* result);
    double (*unit_cost)(const void* owner,uint64_t polygon);
    bool (*assemble)(const void* owner,const NavigationMeshQuery* query,NavigationMeshPath* path,NavigationError* error);
    bool (*equal)(const void* left,const void* right);
    bool (*polygon)(const void* owner,uint64_t polygon,NavigationVec* vertices,size_t capacity,size_t* count);
} NavigationSurfaceView;
bool navigation_surface_view_valid(const NavigationSurfaceView* view);
bool navigation_surface_profile_valid(const NavigationSurfaceView* view,const NavigationSurfaceQuery* query,NavigationError* error);
bool navigation_surface_project(const NavigationSurfaceView* view,const NavigationMeshQuery* query,NavigationVec source,
    NavigationVec* point,uint64_t* polygon,double* separation,NavigationMeshPath* path,NavigationError* error);
bool navigation_surface_find(const NavigationSurfaceView* view,const NavigationSurfaceQuery* query,NavigationMeshPath* path,NavigationError* error);

#define NAVIGATION_MOVE_MAX_POLYGONS 64
#define NAVIGATION_MOVE_MAX_POINTS (2*NAVIGATION_MOVE_MAX_POLYGONS+2)
#define NAVIGATION_SURFACE_MAX_VERTICES 16
typedef struct NavigationSurfaceMove {
    NavigationSurfaceQuery profile;
    uint64_t polygon;
    size_t max_polygon_tests,max_edge_tests;
} NavigationSurfaceMove;
typedef struct NavigationSurfaceMotion {
    NavigationStatus status;
    NavigationVec start,end;
    double fraction,start_distance,length;
    uint64_t polygon;
    NavigationVec points[NAVIGATION_MOVE_MAX_POINTS];size_t point_count;
    uint64_t polygons[NAVIGATION_MOVE_MAX_POLYGONS];size_t polygon_count;
    size_t polygon_tests,edge_tests;
} NavigationSurfaceMotion;
// follows a straight XY/XZ intent through connected convex faces; never traverses explicit links.
// bounded fixed buffers describe the verified prefix, including discontinuous floor heights.
bool navigation_surface_walk(const NavigationSurfaceView* view,const NavigationSurfaceMove* move,NavigationSurfaceMotion* motion,NavigationError* error);

#define NAVIGATION_MAX_LINKS 1024
#define NAVIGATION_MAX_ATTACHMENT_TESTS 67108864
typedef struct NavigationLinkDesc {
    uint64_t id;NavigationVec start,end;uint64_t start_polygon,end_polygon;
    double cost,width,height,duration,max_projection_distance;
    uint32_t membership,type;bool disabled,bidirectional,automatic_cost;
} NavigationLinkDesc;
typedef struct NavigationLinksDesc {const NavigationLinkDesc* links;size_t count,max_attachment_tests;} NavigationLinksDesc;
typedef struct NavigationLinkAttachment {NavigationLinkDesc description;NavigationVec start,end;uint64_t start_polygon,end_polygon;double cost;} NavigationLinkAttachment;
typedef struct NavigationLinks NavigationLinks;
bool navigation_link_valid(const NavigationLinkDesc* link,unsigned dimensions,NavigationError* error);
bool navigation_link_equal(const NavigationLinkDesc* left,const NavigationLinkDesc* right);
bool navigation_links_create(const NavigationSurfaceView* surface,const NavigationLinksDesc* desc,NavigationLinks** links,NavigationError* error);
void navigation_links_destroy(NavigationLinks* links);
const NavigationGraph* navigation_links_graph(const NavigationLinks* links);
size_t navigation_links_count(const NavigationLinks* links);
size_t navigation_links_polygon_count(const NavigationLinks* links);
size_t navigation_links_attachment_tests(const NavigationLinks* links);
bool navigation_links_attachment(const NavigationLinks* links,size_t index,NavigationLinkAttachment* attachment);
bool navigation_links_equal(const NavigationLinks* left,const NavigationLinks* right);
bool navigation_links_find(const NavigationLinks* links,const NavigationSurfaceQuery* query,NavigationMeshPath* path,NavigationError* error);
// triangle ordinals are stable node IDs (ordinal + 1); exact duplicate positions weld.
bool navigation_mesh_create(const NavigationMeshDesc* desc,NavigationMesh** mesh,NavigationError* error);
void navigation_mesh_destroy(NavigationMesh* mesh);
NavigationMesh* navigation_mesh_retain(NavigationMesh* mesh);
bool navigation_mesh_surface(NavigationMesh* mesh,NavigationSurfaceView* view);
const NavigationGraph* navigation_mesh_graph(const NavigationMesh* mesh);
size_t navigation_mesh_vertex_count(const NavigationMesh* mesh);
bool navigation_mesh_equal(const NavigationMesh* left,const NavigationMesh* right);
// point-agent domain only: clearance baking is a separate producer of navigable center geometry.
bool navigation_mesh_find(const NavigationMesh* mesh,const NavigationMeshQuery* query,NavigationMeshPath* path,NavigationError* error);
void navigation_mesh_path_clear(NavigationMeshPath* path);
// shared surface-result machinery; callers reserve enough point storage before appending.
bool navigation_surface_query_valid(const NavigationMeshQuery* query,const NavigationMeshPath* result,unsigned dimensions);
bool navigation_waypoint_append(NavigationMeshPath* path,NavigationVec point,double unit_cost,NavigationError* error);
void navigation_surface_discard_route(NavigationMeshPath* path);
bool navigation_surface_work(const NavigationMeshQuery* query,NavigationMeshPath* path);
// supplied portals span successive convex regions; arrivals preserve step heights.
// costs has corridor.count entries, arrivals is null for continuous surfaces.
bool navigation_surface_assemble(unsigned dimensions,const NavigationMeshQuery* query,NavigationMeshPath* path,
    const double* costs,const NavigationPortal* arrivals,NavigationError* error);

// a conservative Y-up raster domain. Nodes are full clear cells; four-neighbor
// edges retain steps explicitly instead of smoothing paths across unverified space.
#define NAVIGATION_BAKE_MAX_TRIANGLES 262144
#define NAVIGATION_BAKE_MAX_FRAGMENTS 1048576
#define NAVIGATION_BAKE_MAX_WORK 67108864
typedef struct NavigationBakeTriangle {NavigationVec vertices[3];uint32_t groups;double cost;bool walkable;} NavigationBakeTriangle;
typedef enum NavigationBakeRepresentation {NAVIGATION_BAKE_CELLS,NAVIGATION_BAKE_POLYGONS} NavigationBakeRepresentation;
typedef struct NavigationBakeSettings {
    NavigationVec low,high;
    double cell_size,cell_height,radius,height,max_step,max_slope;
    size_t max_cells,max_fragments,max_work;
    // native descriptions preserve unknown tags until validation, without invalid enum loads.
    uint32_t representation;
} NavigationBakeSettings;
typedef enum NavigationBakeStage {NAVIGATION_BAKE_RASTER,NAVIGATION_BAKE_CLEARANCE,NAVIGATION_BAKE_EROSION,NAVIGATION_BAKE_GRAPH,NAVIGATION_BAKE_READY} NavigationBakeStage;
// native worker cancellation/progress only; adapters must not call scripts from this hook.
typedef bool (*NavigationBakeContinue)(void* owner,NavigationBakeStage stage,size_t work);
typedef struct NavigationBakeDesc {
    const NavigationBakeTriangle* triangles;size_t triangle_count;
    NavigationBakeSettings settings;
    NavigationBakeContinue proceed;void* owner;
} NavigationBakeDesc;
typedef struct NavigationBakeCell {
    size_t x,z;double floor,ceiling,cost;uint32_t groups;
} NavigationBakeCell;
typedef struct NavigationBakeRegion {
    size_t x,z,width,depth;double floor,ceiling,cost;uint32_t groups;
} NavigationBakeRegion;
typedef struct NavigationBakeStats {size_t columns,fragments,candidates,cells,edges,work;} NavigationBakeStats;
typedef struct NavigationBake NavigationBake;
bool navigation_bake_settings_valid(const NavigationBakeSettings* settings,size_t* columns_x,size_t* columns_z,NavigationError* error);
bool navigation_bake_settings_hash(const NavigationBakeSettings* settings,char output[65]);
// empty geometry and fully eroded domains succeed with zero cells and no graph.
bool navigation_bake_create(const NavigationBakeDesc* desc,NavigationBake** bake,NavigationError* error);
// stored cells declare an already baked domain; import validates bounds, ordering,
// headroom, costs and connectivity. Source/settings fingerprints accompany the asset.
bool navigation_bake_from_cells(const NavigationBakeSettings* settings,const NavigationBakeCell* cells,size_t count,NavigationBake** bake,NavigationError* error);
NavigationBake* navigation_bake_retain(NavigationBake* bake);
void navigation_bake_destroy(NavigationBake* bake);
const NavigationGraph* navigation_bake_graph(const NavigationBake* bake);
bool navigation_bake_cell(const NavigationBake* bake,uint64_t id,NavigationBakeCell* cell);
size_t navigation_bake_region_count(const NavigationBake* bake);
bool navigation_bake_region(const NavigationBake* bake,uint64_t id,NavigationBakeRegion* region);
bool navigation_bake_stats(const NavigationBake* bake,NavigationBakeStats* stats);
bool navigation_bake_settings(const NavigationBake* bake,NavigationBakeSettings* settings);
bool navigation_bake_equal(const NavigationBake* left,const NavigationBake* right);
typedef NavigationSurfaceQuery NavigationBakeQuery;
bool navigation_bake_surface(NavigationBake* bake,NavigationSurfaceView* view);
// polygon IDs identify cells or convex regions according to the baked profile.
// point storage/corridor are freed by navigation_mesh_path_clear().
bool navigation_bake_find(const NavigationBake* bake,const NavigationBakeQuery* query,NavigationMeshPath* path,NavigationError* error);

#define NAVIGATION_MAX_ROUTE_POINTS (2*NAVIGATION_MAX_NODES+2)
typedef enum NavigationFollowStatus {
    NAVIGATION_FOLLOW_IDLE,NAVIGATION_FOLLOW_MOVING,NAVIGATION_FOLLOW_ACTION,
    NAVIGATION_FOLLOW_ARRIVED,NAVIGATION_FOLLOW_PATH_END,NAVIGATION_FOLLOW_STALE,
    NAVIGATION_FOLLOW_OFF_PATH,NAVIGATION_FOLLOW_STUCK
} NavigationFollowStatus;
typedef struct NavigationFollowerDesc {
    unsigned dimensions;
    double max_speed,max_acceleration,waypoint_distance,target_distance,path_distance;
    double stuck_time,min_progress;
    size_t max_waypoints;
} NavigationFollowerDesc;
typedef struct NavigationFollowState {
    NavigationFollowStatus status;
    uint64_t generation,action;
    size_t waypoint,traversal,waypoint_tests;
    NavigationVec position,velocity,displacement;
    double distance,stalled_time;
    bool complete,budget_exhausted;
} NavigationFollowState;
typedef struct NavigationFollower NavigationFollower;
// copied routes are native-owned; positions are achieved foot positions in the asset's frame.
bool navigation_follower_create(const NavigationFollowerDesc* desc,NavigationFollower** follower,NavigationError* error);
void navigation_follower_destroy(NavigationFollower* follower);
bool navigation_follower_assign(NavigationFollower* follower,const NavigationMeshPath* path,uint64_t generation,NavigationError* error);
void navigation_follower_cancel(NavigationFollower* follower);
bool navigation_follower_read(const NavigationFollower* follower,NavigationFollowState* state);
// steering never applies body motion; boundary/status stops override acceleration ramping.
bool navigation_follower_step(NavigationFollower* follower,NavigationVec position,uint64_t generation,double delta,NavigationFollowState* state,NavigationError* error);
// action completion requires the matching path generation/ID and an achieved arrival position.
bool navigation_follower_complete_link(NavigationFollower* follower,uint64_t generation,uint64_t action,NavigationVec position,NavigationError* error);
#ifdef __cplusplus
}
#endif
#endif
