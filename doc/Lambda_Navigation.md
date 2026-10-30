# Navigation

Native navigation provides bounded graph/grid shortest paths, triangulated
surface corridors with grounded waypoints, clearance-aware baked domains and
explicit links
through `lambda.navigation`. Constructors, validation and queries are pure
(**S12.1.1v3 / D7.2.4v4**). The reusable provider owns immutable copied graph data
below runtime/Radiant (**D7.1.1–D7.1.3**); result conversion uses precise roots
(**D5.3.3**). Scene source bakes run in background workers with checked
publication. Dirty-tile rebakes, Scene3D agents and
obstacle avoidance remain outstanding.

```lambda
import nav: lambda.navigation
let world = nav.grid([5,5], {blocked:[[2,0],[2,1],[2,2],[2,3]]})
nav.path(world, [0,0], [4,0])^
```

`graph(nodes, edges, options={})` constructs a version-1 `navigation-graph`
element. `node(id, position, options={})` returns a node map;
`edge(from, to, cost=1.0, options={})` returns a directed edge map. Graph
`dimensions` defaults to 2; use 3 with three-component positions. Node IDs are
nonempty text names shorter than 128 bytes and unique in their graph. Stable
native IDs derive from their bytes; a collision rejects the graph. Nodes and
edges accept `groups` (a uint32 membership mask) and `disabled` (boolean).
Edges also accept `bidirectional` and up to six `guards`: referenced nodes
which must admit the query's filter. Costs must be finite and nonnegative.

`grid(size, options={})` constructs a version-1 `navigation-grid` element.
`size` contains two or three positive cell counts. Coordinates are zero-based,
with X varying fastest, then Y, then Z. Options are:

| Option | Meaning |
|---|---|
| `origin` | Two/three finite world coordinates; defaults to zero. |
| `cell_size` | Positive spacing per axis; defaults to one. |
| `diagonals` | Defaults to false; admits 8 neighbors in 2D or 26 in 3D. |
| `blocked` | An array of cell coordinates. |
| `costs` | Optional flat array, one nonnegative multiplier per cell. Edge cost is world travel distance times the destination cell's multiplier. |
| `groups` | Optional flat array of uint32 membership masks. |

Diagonal traversal requires every intermediate face/edge cell to be traversable
for that query. It cannot cut a blocked or filtered corner. The native profile
admits at most 65,536 nodes/cells and 1,048,576 directed edges; dense diagonal
3D grids can exceed the edge quota before reaching the cell quota.

`validate(source)` returns true or an error. `path(source, start, goal,
options={})` accepts graph node names or grid cell coordinates. Tagged graph/grid
elements and equivalent maps with `type: graph` / `type: grid` are accepted.
Schema `version` defaults to 1 and must equal 1 when supplied. Query options:

| Option | Meaning |
|---|---|
| `filter` | uint32 mask, default all bits. Every visited node, edge and clearance guard must overlap it. |
| `heuristic` | `euclidean` (default) or `zero`. Euclidean distance is scaled by a global lower bound on edge cost per distance, preserving admissibility for weighted and zero-cost graphs. |
| `max_expansions` | Maximum popped nodes; defaults to node count. |
| `max_edge_tests` | Maximum inspected edges, including filtered edges; defaults to edge count. |
| `partial` | Boolean, default false; permits a path to the closest reached node when the goal is unreachable or the budget expires. |

The result contains `status`, `partial`, `nodes`, `points`, `cost`, `expanded`,
`edge_tests`, `updates` and `frontier_peak`. Points contain world coordinates.
An empty path has null cost. An admitted start equal to the goal completes with
one node, zero cost and no expansions, even with zero search budgets.

- `complete`: the graph/grid goal was reached with minimum graph cost; surface
  completion and costs follow the surface contract below.
- `partial`: the reachable domain was exhausted; a path to the closest reached
  node is returned because `partial` was requested.
- `unreachable`: no complete path exists and no partial path was requested, or
  the start itself is blocked/filtered.
- `budget_exhausted`: work stopped at the requested limit; it does not prove
  that the goal is unreachable. With `partial`, the result includes a valid
  provisional path to the closest reached node.

Equal-cost frontier ties use stable native node IDs. Construction and search
initialization also perform bounded work over the admitted graph; expansion
budgets bound traversal rather than graph parsing/copying. Numeric overflow,
unknown references, invalid coordinates and invalid descriptions return errors.
No search invokes script callbacks or performs IO. Each query has independent
working memory; graph ownership remains with its caller.

## Triangulated surfaces

`mesh(vertices, triangles, options={})` creates a version-1 `navigation-mesh`.
Dimensions default to the first vertex's coordinate count (2 or 3); an explicit
`dimensions` must agree with every vertex. 2D uses XY coordinates. 3D uses Y-up
walking surfaces, with `max_slope_degrees` defaulting to 45 and constrained to
`[0,90)`. Downward-facing and excessively steep faces are disabled. Use
`two_sided:true` to admit either face winding subject to the same slope limit.
2D accepts either winding. Degenerate/duplicate triangles, nonmanifold edges,
overlapping neighbors and numerically unsupported geometry return errors.
Both arrays may be empty: this creates a valid unreachable domain, defaulting
to 2D unless `dimensions` is supplied. One empty array with a nonempty partner
is invalid.

```lambda
import nav: lambda.navigation
let floor_mesh = nav.mesh([[0,0],[2,0],[2,1],[0,1]],
    [[0,1,2],[0,2,3]])
nav.path(floor_mesh, [0.25,0.25], [1.75,0.75])^
```

A triangle is a zero-based index triple or a map with `vertices`, nonnegative
`cost` (default 1), uint32 `groups` (default all bits), and boolean `disabled`
(default false). `triangle(indices, options={})` builds that map. Exact duplicate
vertex positions weld; nearby positions remain distinct. The provider owns
normalized copies and admits up to 262,144 source vertices and 65,536 triangles.
Input ordering determines polygon IDs, which are zero-based triangle ordinals.

Surface queries accept world-coordinate endpoints. Both endpoints project to
the nearest admitted face, with explicit bounds and work budgets:

| Option | Meaning |
|---|---|
| `max_projection_distance` | Nonnegative world distance, default 1. No admitted face within it produces `unreachable`. |
| `max_projection_tests` | Total inspected triangles for both endpoints; default twice the triangle count, maximum 131,072. Incomplete searches do not publish a claimed nearest point. |
| `max_portal_tests` | Funnel/lifting work, default `32 * triangle_count + 16`, maximum 16,777,216. Exhaustion returns `budget_exhausted` without an unverified movement path. |

The ordinary search options (`filter`, `heuristic`, graph traversal budgets and
`partial`) also apply. A* selects a minimum-cost corridor in a centroid graph:
an edge costs centroid distance times the destination face's area multiplier.
String pulling selects a straight path within that corridor. Crossed portals
lift the path onto the actual 3D faces; a straight horizontal route retains
waypoints at changes of height/slope. Collinear points may be removed when the
3D segments remain exactly collinear. Neither centroid selection nor string
pulling promises globally optimal Euclidean or area-weighted surface travel.

The result adds these surface fields:

| Field | Meaning |
|---|---|
| `nodes`, `corridor` | The selected polygon ordinals. |
| `points` | Copied, grounded world-coordinate waypoints. |
| `portals` | Left/right world-coordinate pairs between consecutive corridor polygons. |
| `corridor_cost` | The A* centroid-graph cost, separate from travel cost. |
| `length`, `cost` | Grounded polyline length and its area-weighted travel cost; null without a movement path. |
| `projected_start`, `projected_goal` | Nearest admitted endpoint projections, or null when not located within the distance/work bound. |
| `start_polygon`, `goal_polygon` | Polygon ordinals for those projections, or null. |
| `start_distance`, `goal_distance` | Distances from the requested endpoints to their projections, or null. |
| `path_end` | The reached endpoint. A partial result can end on a reachable face while `projected_goal` still identifies the requested target's projection. |
| `projection_tests`, `portal_tests` | Bounded surface-query work counters. |

`complete` means the projected goal was reached, not that an off-mesh requested
point was reached. Projection alone does not move or teleport an agent. A
partial route is grounded inside its reached corridor. Assembly-budget
exhaustion can return no movement path even when a corridor was found.

This is a **point-agent surface domain**. Positive `radius` in a mesh description
or surface query is rejected; use the separate cell bake below for clearance.
Positive-radius controller-backed agents remain outstanding. This
profile must not be used as evidence that positive-radius clearance is baked.

## Explicit surface links

`link(id, start, end, options={})` builds an authored traversal map. Meshes,
triangle bakes, saved-cell bakes and scene source descriptors accept a `links`
array. Graph/grid assets reject nonempty link arrays. Endpoints use the asset's
2D XY or 3D Y-up frame. Links retain owned native data; their construction and
querying remain pure (**S12.1.1v3 / D7.1.1–D7.1.3 / D7.2.4v4**).

```lambda
import nav: lambda.navigation
let jump = nav.link("gap", [1,0,2], [5,0,2],
    {kind:"jump", width:1, height:2, duration:0.4, bidirectional:true})
// pass {links:[jump]} in the mesh/bake constructor options.
```

| Field | Contract |
|---|---|
| `id` | Unique nonempty text shorter than 128 bytes. |
| `start`, `end` | Required finite coordinate arrays matching the surface dimensions. |
| `kind` | Opaque action name, default `"link"`, shorter than 32 bytes. |
| `bidirectional`, `disabled` | Booleans, both default false. |
| `groups` | uint32 membership mask, default all bits; both attached faces must also admit the query. |
| `cost` | Nonnegative traversal cost. Null/absent uses projected endpoint distance. |
| `width`, `height` | Maximum diameter and height admitted by the action. Width defaults to 1; height defaults to the largest finite double. |
| `duration` | Nonnegative action duration metadata, default 0. |
| `max_projection_distance` | Nonnegative endpoint attachment bound, default 1. Zero requires an endpoint on the surface. |
| `start_polygon`, `end_polygon` | Optional zero-based face/cell/region pins. Without pins, find the nearest admitted surface. |

At most 1,024 links are admitted, within the shared 65,536 graph-node quota.
Each link contributes two internal transit nodes. The top-level
`max_attachment_tests` bounds all link endpoint projection work together;
default/maximum 67,108,864. Invalid or unattached endpoints reject construction
atomically, including disabled links. Endpoint attachment uses the entire
surface domain; query masks subsequently admit both faces and the action.
A positive-radius query still requires a compatible clearance bake; link width
alone does not supply clearance for an ordinary triangle mesh.

A* selects links in the same graph as ordinary surface connectivity, preserving
parallel actions and zero costs. Results keep ordinary zero-based surface IDs
in `nodes`/`corridor` and add `links`: ordered traversals containing `id`, `kind`,
`reverse`, projected `start`/`end`, zero-based `start_point`/`end_point`, `cost`
and `duration`. Departure/arrival waypoints remain distinct even for a
zero-length action. Ordinary waypoint segments stay on the surface; an annotated
link span can leave it. `length` includes the endpoint span, while `cost` sums
area-weighted ordinary travel and declared link costs. `corridor_cost` remains
the graph metric. `link_attachment_tests` records construction work.

The service describes actions; it does not execute jumps, teleport bodies or
call script during traversal. Callers must recognize the action and coordinate
actual motion. Internal transit markers cannot become partial path endpoints.
Projection, search and assembly retain their bounded-work/status contracts;
exhausted assembly does not publish an unverified action path.

Live link-only edits rebuild connectivity over the retained bake without
rebaking unchanged source geometry. Equivalent typed-template redraws retain
the generation. Invalid attachments preserve the prior working cache;
changed link metadata makes pending jobs stale. Export retains the definitions
captured by that job, and saved-cell descriptions reopen with those links
(**D4.5.2 / D5.3.3 / D7.5.3v2–D7.5.4**). `navigation_links` advertises this
profile; broad `navigation` remains false pending agents and avoidance.

## Clearance-aware bakes

`bake(triangles, settings, options={})` describes a bounded, Y-up 3D raster bake. Validation
and path queries build an immutable native domain; a Scene3D asset caches it.
The description is a pure value and can be formatted, saved and reopened
(**S12.1.1v3 / D7.1.1–D7.1.3 / D7.2.4v4**). It stores source triangles and
settings; it is not a serialized binary of the compiled cells.

Each triangle contains three XYZ arrays, or a map with `vertices` containing
those arrays, `walkable:true`, `groups:4294967295` and `cost:1`. Upward-facing
triangles within the slope limit can support a floor. All selected triangles,
including nonwalkable triangles, participate in obstruction and ceiling tests.
Use complete obstacle geometry, including closed props' bottom faces: the
producer consumes surfaces and does not infer arbitrary solid interiors.

```lambda
import nav: lambda.navigation
let source = [ [[0,0,0],[0,0,4],[4,0,4]],
               [[0,0,0],[4,0,4],[4,0,0]] ]
let domain = nav.bake(source, {
    low:[0,-1,0], high:[4,4,4],
    cell_size:0.25, cell_height:0.05,
    radius:0.25, height:1.8, max_step:0.3
})
nav.path(domain, [1,0,1], [3,0,3])^
```

| Setting | Contract |
|---|---|
| `low`, `high` | Required finite XYZ bounds with positive extent on every axis. Bounds limit the center domain and available headroom. |
| `cell_size`, `cell_height` | Positive horizontal/vertical resolution; defaults `0.25` / `0.05`. |
| `radius`, `height` | Nonnegative radius and positive standing height; defaults `0.25` / `1.8`. |
| `max_step` | Maximum change between quantized floor levels; default `0.3`, nonnegative and smaller than height. |
| `max_slope_degrees` | Upward-floor slope in `[0,90)`; default `45`. A supplied `max_slope_radians` is authoritative for lossless compiled-asset round trips. |
| `representation` | `"cells"` (default) retains the four-neighbor cell route; `"polygons"` partitions the same clear domain into convex regions and uses portal/funnel routes. |
| `max_cells` | Both raster-column and candidate-cell bound, at most `65536`; the region graph has at most that many nodes. |
| `max_fragments` | At most `1048576` clipped triangle fragments. A column admits at most 64 fragments; coverage has at most 4096 sweep events. |
| `max_work` | At most `67108864` charged raster, coverage, erosion and graph operations. Structural sorts and allocations are separately bounded by the collection limits. |

The producer clips source triangles, merges vertical spans and checks the union
of supporting floor fragments across the entire cell. This detects small holes
that corner/center sampling or summed triangle areas can miss. It rounds solid
spans outward in vertical voxel units, checks ceilings, and erodes **entire
output cells** for the requested horizontal disk. Footprint masks are intersected
and costs use the maximum touched cost, using the original domain throughout
erosion. Stacked floors stay separate. Empty input or complete erosion produces
a valid empty, unreachable domain.

This is a conservative raster approximation: narrow passages can require finer
cells, and floor levels are raised by vertical quantization, within-cell slope
variation and admitted step-height obstructions. Choose `max_step` and resolution
together; it bounds changes between those representative floor levels. The
physics controller still resolves actual support, steps and motion. This
cell profile retains a cell-center route. The polygon profile below smooths
inside the quantized domain; it does not reconstruct the original triangle
surface.

Queries use XYZ endpoints and return `domain:"voxel_cells"`, the baked `profile`,
zero-based `start_cell`/`goal_cell` and cell ordinals in `nodes`/`corridor`.
The profile includes version `1`, frame `"y_up"`, bounds, resolution, slope,
radius, height and step settings.
They project to full clear cells, then connect cell centers through admitted
four-neighbor steps. `points` retain this verified cell route; `portals` is empty.
`length` measures that polyline. `corridor_cost` uses destination-cell edge costs;
`cost` adds the weighted endpoint segments. Existing projection/search/partial
statuses apply. `max_waypoint_tests` bounds assembly and is reported as
`waypoint_tests` (also in the shared `portal_tests` field).

### Polygonized regions

Set `settings.representation:"polygons"` to partition admitted cells into
axis-aligned convex rectangles. Merging requires matching quantized floor,
ceiling, area cost and groups. This preserves clearance/area boundaries and
stacked levels. Shared boundary intervals become directed portals, including
partial edges where unequal rectangles meet. The existing numerical funnel
shortens a selected region corridor. Grounded waypoints retain departure and
arrival heights at a step, so smoothing cannot replace a riser with a diagonal
through unverified support (**D7.1.1–D7.1.3 / D7.2.4v4**).

Results use `domain:"voxel_polygons"`, `profile.representation:"polygons"`,
`region_count` and zero-based region IDs in `nodes`/`corridor` and
`start_polygon`/`goal_polygon`. `portals` contains oriented departure edges;
`points` includes both sides of height transitions. A connected open rectangle
has one region and a direct start-to-goal path. `start_cell`/`goal_cell` belong
to the cell profile and are absent here. IDs are local to an asset generation. Scene snapshots report actual graph
`nodes` separately from `storage_nodes`, which counts backing cells for bakes
and graph nodes for other assets. The existing combined 65,536-unit storage
limit applies during reconciliation and background publication; compression
does not admit additional backing storage.

`corridor_cost` is the weighted region-center graph metric; `length` and `cost`
measure the emitted grounded polyline. Within-region travel uses that area's
cost, and a vertical step uses the destination area's cost. Funnel shortening
applies to the chosen corridor; the centroid graph does not promise globally
minimum weighted geometric travel. `max_portal_tests` bounds portal lookup and
funnel/crossing work, with `max_waypoint_tests` retained as an alias when the
former is absent. Work exhaustion discards an unverified route while retaining
projection/work diagnostics.

Saved `format:"cells"` maps retain the selected representation in their complete
settings. Reopening a polygon profile reconstructs the same deterministic
partition/portals from those declared cells. Its settings fingerprint is
separate from a cell profile; existing cell fingerprints remain unchanged.
`navigation_bake_polygons` advertises this profile. Dirty tiles, agents and
avoidance remain separate M4 work.

Query `radius` and `height` default to the baked profile and must be no larger;
query `max_step` defaults to the baked value and must be at least that value.
An incompatible profile is an error, rather than an unsupported clearance
promise. Smaller agents reuse the conservative domain without opening new space.
Projection/assembly exhaustion never publishes an unverified movement path.

## Scene3D assets

`lambda.navigation.scene` supplies procedural queries over a live document
through the existing `radiant.scene3d` host operation (**S12.1.1v3 /
D7.5.3v2**). Graph data belongs to the document, independently of its GPU
projection and physics world (**D4.5.2**). A query can create the navigation
cache without acquiring graphics or physics resources.

Place definitions directly in a scene's `<resources>`. Typed Lambda scenes can
use `nav.grid([3,3], {id:"routes"})`, `nav.graph(nodes, edges, {id:"roads"})`,
`nav.mesh(vertices, triangles, {id:"floor"})`,
`nav.bake(triangles, settings, {id:"clearance"})`,
or `<navigation-grid id:"routes", data:{type:'grid',size:[3,3]}>`. HTML accepts
the same data map as a JSON object string:

```html
<scene3d id="world">
  <resources>
    <navigation-grid id="routes" version="1"
      data='{"type":"grid","size":[3,3]}'></navigation-grid>
  </resources>
</scene3d>
```

Asset IDs must be nonempty and shorter than 128 bytes. A scene admits at most
64 definitions, the combined graph node/edge quotas above, and 262,144 owned
surface vertices across its assets. Asset `version`
defaults to 1; optional `revision` is a uint32 integer, default 0. Both the
outer asset schema and the data schema validate. The tag must agree with the
data's graph/grid/mesh/bake kind. Assets contain world coordinates, accept no object
transforms, and have no element/non-whitespace text children. JSON strings are
limited to 16 MiB and parsed into call-local storage; native graphs retain
copied values rather than parser/GC pointers (**D5.3.3 / D7.1.3**).

The live module provides:

| Operation | Result |
|---|---|
| `path(node, asset, start, goal, options={}, generation=null)` | The ordinary path result, with `asset`, `generation` and `revision`. A supplied stale generation returns an error. |
| `snapshot(node)` | Asset IDs/generations/revisions; compilation, invalidation and query counters; node/edge/surface-vertex totals; `rejected` and `diagnostic`. On an invalid edit, it reports the last published cache plus the rejection. |
| `current(node, asset, generation)` | Whether this integer generation still names a valid current asset. Invalid authored edits and removed assets return false. |

All authored asset changes validate as one transaction before old graph owners
are retired. A malformed candidate preserves the previous cache; path queries
reject the invalid authored scene until repaired. Equivalent compiled data
keeps its generation across JSON field reordering and equivalent template
redraws. Reordered node arrays can conservatively receive a new generation. A changed
graph, explicit revision change, identity replacement, or removal followed by
re-addition publishes a new generation. A document-owned sequence survives
viewport release/recreation and distinguishes assets across its viewports;
old generations cannot alias a newly created cache. Generations are opaque integers; use
the returned value rather than calculating one. `current` is the explicit
check for a stored result; future queries also accept the stored generation.

Repeated queries and camera edits reuse compiled graphs. Navigation edits do
not reproject GPU resources, context recovery preserves native assets, and
reconciliation does not advance/reset the scene clock (**D2.11.4 / D4.5.2**).
An edit attempted during tick sampling rejects before publication and can be
retried after the tick. Mesh reconciliation compares the owned surface geometry
and area costs as well as its graph: equal centroids cannot conceal changed
portals or travel costs. Clearance/profile changes also version cell bakes.
The polygon profile reuses the same ownership and generation rules. Dirty-tile
rebakes, broader agent profiles and avoidance remain pending.

## Background scene bakes

`lambda.navigation.scene.bake_asset(id, sources, settings, options={})` creates a
`navigation-bake` source descriptor for a scene's resources. HTML can use a
JSON data map with `type:"bake"`, `sources` and `settings`. `sources` contains
1..64 mesh IDs, or selection maps `{id,walkable:true,groups:4294967295,cost:1}`.
Selections are explicit; nonwalkable sources still obstruct standing space.
Both geometry references and inline geometry use the renderer's CPU decoder.

This profile samples authored static mesh positions and parent transforms,
independently of camera, animation/physics poses and GPU projection. It accepts
triangle buffers and box/plane primitives. Instancing, skinning and morphing
require a later explicit pose/instance snapshot profile; shader deformation is
not sampled. The collector copies native coordinates and fingerprints; workers
receive no DOM, script values or callbacks (**D4.5.2 / D5.3.3 /
D7.1.1–D7.1.3 / D7.5.3v2**).

| Live operation | Result/behavior |
|---|---|
| `bake_start(node, asset)` | Collect immutable inputs and queue a job; return its opaque integer token and progress record. A newer job supersedes an older unpublished job for that asset. |
| `bake_poll(node, token)` | Read `status`, `finished`, stage/work, fingerprints, cell count and diagnostic. Progress runs independently of the fixed tick, including paused viewports. |
| `bake_publish(node, token)` | If ready, verify the root/target/settings generation, latest request, source identities and a fresh source hash, then retain one immutable native domain and issue a new asset generation. Otherwise return progress or stale/failure state. |
| `bake_cancel(node, token)` | Request cooperative cancellation of an unpublished job. A published result remains owned by its asset. |
| `bake_release(node, token)` | Release a finished job and its snapshot/result ownership. It does not remove the published asset. Active workers must finish first. |
| `bake_export(node, token)` | Copy a successful, uncancelled, nonstale result to a versioned ordinary data map for serialization. |

Statuses are `queued`, `running`, `ready`, `published`, `cancelling`, `cancelled`,
`failed` and `stale`. Use `finished` to decide when release is permitted.
Invalid requests return errors; worker failure/cancellation/staleness remains
inspectable in its progress record. Tokens and generations must be stored as
integers and passed back without arithmetic or conversion through floating point.

A viewport retains at most eight jobs and 262,144 snapshot triangles across
those jobs. Its bounded pool runs one worker. Release finished records before
starting more. Native work/fragment/cell limits still apply, and publication
checks the scene's combined graph quota. Viewport/document teardown cancels and
joins workers before releasing native storage.

Source descriptors initially report `ready:false` in `snapshot`; paths reject
until publication. Source/configuration edits retire readiness and stale old
jobs. Camera changes do not decode/rebake the source. Equivalent template data
retains a published bake after checking canonical source coordinates when the
cheap identity fingerprint changes. Graphics recovery retains navigation; an
actual viewport root replacement rejects an unpublished old-root job.

Exports use `version:1`, `format:"cells"`, frame `"y_up"`, `source_hash`,
`settings_hash`, complete settings and ordered cell records. Hashes use SHA256
over explicitly encoded fields; settings normalize signed zero so JSON save/reopen
preserves identity. `max_slope_radians` preserves the exact angle and takes
precedence over the convenient degree field. Load the exported map as the data of a
`navigation-bake` asset to rebuild owned connectivity from stored cells without
revoxelizing source triangles. Import checks profile/hash consistency, bounds,
ordering, headroom, masks, costs and quotas. Stored assets declare their baked
clearance; the original source hash records provenance rather than sampling
the current render scene. Rebuild a stored asset when its level source changes.
Cell records may include `support_span`, the measured raster height interval
below `levels[0]`. Export retains positive spans; import validates finite,
nonnegative spans against bake bounds and the admitted raster profile. Missing
spans default to zero, so legacy cells certify exact-floor support rather than
inventing terrain underneath them. Connectivity/profile fingerprints retain
their existing meaning; native asset equality also compares support spans.
An empty cell array is a valid empty domain. The broad `navigation` capability
remains incomplete; `navigation_bake_jobs` identifies this implemented profile.

Native acceptance uses an independent Bellman–Ford oracle, source-order and
ownership controls, fractional priorities, filter/guard and budget cases, and
2D/3D grid geometry. Paired fixtures are in `test/lambda/navigation/`; milestone
and aggregate receipts are recorded in the [gaming implementation log](../vibe/impl/Radiant_Impl_Scene3D_Gaming.md).

## Baked-surface capsule agents

`lambda.navigation.scene` provides document-owned agents over published 3D
clearance bakes (**S12.1.1v3 / S12.1.5v2 / D4.5.2 / D5.3.3 / D7.5.3v2**).
A position-controlled kinematic body must have one solid upright Y-up capsule.
The baked radius must cover the capsule radius plus movement skin, and baked
headroom must cover its complete height plus twice the skin. The controller
must support the bake's slope and step limits. Graphs, grids and point-only
triangle meshes cannot certify this capsule clearance.

The admitted native profile follows flat floors, ramps and steps up/down while
preserving collision feedback and explicit link handoff. Downhill movement and
down-steps need sufficient authored `snap_distance`; the agent does not enlarge
it. Missing physical support, incompatible slopes, insufficient headroom and
departure from the baked layer stop with a diagnostic. A shared sorted-X index
and reciprocal XZ velocity solver handle bounded local avoidance for at most
32 copied agents. Vertically separate layers do not interact. The solver checks
the actual simultaneous capsule sweeps after surface/controller clamps and
stops an unsafe batch before target publication. This local response does not
select goals, replan, retry or resolve deadlocks. The broader `navigation`
capability remains incomplete pending dirty tiles and other profiles.

| Operation | Behavior |
|---|---|
| `agent_attach(node,id,asset,movement,options={})` | Copy a controller description, bind its body and create an idle native follower. `movement` uses the physics movement fields, including the body name. Duplicate IDs/body writers reject. |
| `agent_target(node,id,target,generation=null)` | Queue a finite world-space `[x,y,z]` foot target; replace the previous route atomically after validating the command. |
| `agent_avoidance(node,id,options,generation=null)` | Update `enabled`, `priority` (0–1; default 0.5), `time_horizon` (seconds; default 2) and bounded neighbor/solver budgets. This changes command generation without clearing the route. |
| `agent_snapshot(node,id)` | Read status, achieved foot `position`, projected `navigation_position`, preferred/chosen/achieved velocities, avoidance status and work counters, target, query count and separate command/path/asset/world identities. |
| `agent_cancel(node,id,generation=null)` | Discard the target and route, retaining an idle attachment. |
| `agent_release(node,id,generation=null)` | Destroy the attachment and copied route. The last release unbinds the navigation service. |
| `agent_link_complete(node,id,path_generation,action,generation=null)` | Continue an active link only after its copied arrival endpoint has actually been achieved. Wrong/stale path or action IDs reject. |

`generation` optionally guards commands with the last returned command identity;
pass the exact integer back without arithmetic or float conversion. A new target
or cancellation changes that identity. `path_generation` identifies a computed
route separately. Asset replacement, discontinuous physics edits and reset
invalidate the route and queue a new search against the achieved body position.
Ordinary ticks, targets and camera edits retain its identity. A detached or
incompatible body/domain reports `invalid`; restore it or release the agent.
Equivalent template rebuilds follow body references through the document's
existing identity reconciliation. Seek reconstructs authored physics and then
repaths the current target; historical gameplay commands are not replayed.

Follower options are `speed` (2), `acceleration` (8), `waypoint_distance` (0.05),
`target_distance` (0.05), `path_distance` (1), `stuck_time` (1 second) and
`min_progress` (0.001), in world units/seconds; speed/acceleration apply in XZ.
Ordinary path filter, projection,
partial-route and work options apply. Actual capsule dimensions and controller
step ability override caller-supplied clearance values. The achieved foot is
the capsule bottom minus its skin; normal physics recovery can retain a small
separation above the mathematical floor. Surface guards use four times the
controller's reported length-scale precision, independently of the skin size.
Steering/progress use XZ coordinates projected from achieved positions; baked
floor-height seams do not request vertical motion. The controller owns actual
elevation. Support guards use each cell's measured raster span, the capsule's
normal clearance/step profile and the layer ceiling; a permissive slope setting
cannot invent a lower floor. Arrival requires actual physical ground support
from the preceding completed tick, including after a controller settling move.
Path projection distance applies to navigation-space endpoints; the separate
support guards validate their relationship to the physical foot.

Attach options may include an `avoidance` map; the update operation accepts
the same fields. Priorities are responsibility weights, not automatic
application intent: for an interacting pair, the other agent's share sets how
much correction this agent takes. Lowering an agent's priority makes it yield
more. Setting `enabled:false` makes its preferred motion an obstacle for
responsive neighbors. The X-sweep lookup checks at most 31 other agents per
agent; neighbor count, swept constraints and the two-dimensional projection
also have explicit limits. Exhausted or infeasible controls stop the agent and
report the reason. Completed physical velocity, not last tick's request, feeds
the next follower acceleration update.

The navigation phase runs before animation and physics under the shared scene
coordinator. Automatic movement requires playing physics. Explicit scene and
live physics batches sample attached agents through that same coordinator.
The service verifies intended motion with the bounded surface walker, computes
collision movement without committing it, and verifies the resulting target
again before issuing the ordinary kinematic target. Sliding or recovery cannot
publish a target outside the baked domain. A zero-displacement support sample
precedes steering; settling uses an ordinary verified target before progress
can advance. At most two read-only controller computations run per active agent
tick, each retaining the configured controller work limits. Stalled movement becomes `stuck`;
submit another target to retry after changing the world.

A route reaching an explicit link stops with status `action` and an `action`
record containing its ID/name/kind, direction, duration and copied endpoints.
Script implements the traversal through ordinary movement/target commands and
acknowledges its achieved, grounded arrival on the retained arrival layer.
Ordinary path steering ignores vertical floor seams; explicit link completion
validates the full projected endpoint and its physically grounded layer.
Navigation does not teleport bodies. A
teleport invalidates the old path; its old action acknowledgement then rejects.

Events `navigationpathchanged`, `navigationlinkrequired`, `navigationarrived`
and `navigationstatechanged` carry copied snapshot details through the existing
traced timing queue. Handlers run after sampling, and queued details survive
agent release. Other statuses include `idle`, `queued`, `moving`, `path_end`,
`unreachable`, `budget_exhausted`, `off_path`, `off_surface`, `blocked` and
`invalid`. Terminal failures retain their diagnostic and require a new target
unless a changed world/asset identity triggers repathing.

Each viewport admits at most 32 agents. Four searches per fixed tick share a
round-robin cursor. Each search caps expansions at 2,048, edge tests at 8,192,
support-layer/endpoint projection at 4,096 tests and assembly at 65,536 tests,
honoring smaller caller budgets. The default projection allowance is three
face passes (support selection and two endpoints), capped at 4,096. Each copied
route admits at most 4,096 points and 128 explicit
actions; each follower inspects at most 32 waypoints per tick. Each surface walk
admits 32 polygon tests and 256 edge tests. These limits report exhausted/blocked
outcomes rather than unbounded work. Dirty-tile rebakes, root-motion coupling
and genuine 2D bodies remain outstanding. Editor interfaces
are outside the [Scene3D gaming proposal's revised scope](../vibe/radiant/Radiant_Design_Scene3D.md).
