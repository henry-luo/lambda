# Lambda Physics

The initial physics implementation provides a native C 3D rigid-body engine,
physics-bearing `scene3d` elements, pure batch simulation, and the
`lambda.physics` package. It follows **S12.1.1v3**: constructors and batch
computation are pure; live scene controls are procedures. Package paths follow
**D7.2.4**, with native entry points under `lambda.sys.*` (**S17.2.1**).

## Scene descriptions

A physics-bearing `<scene3d>` contains one `<physics>` configuration and
`<'rigid-body'>` nodes. Each body contains explicit `<collider>` descriptions
and may contain ordinary meshes. The quoted tag spelling is required for the
hyphenated Lambda element name.

```lambda
import physics: lambda.physics

let scene = <scene3d
    <physics gravity: [0.0, -9.81, 0.0], timestep: 0.01>
    <'rigid-body' id: "ball", type: 'dynamic', position: [0.0, 3.0, 0.0],
        <collider id: "ball-shape", shape: 'sphere', radius: 0.5>
    >
>;
let trace = physics.simulate(scene, {steps: 100, sample_every: 10})^;
physics.chart_data(trace, {body: "ball"})
```

Collider shapes are `'sphere'`, `'box'`, `'capsule'`, `'convex'`, `'mesh'` and `'heightfield'`. Box `size` gives full
extents. Spheres use `radius` (default 0.5). Capsules extend along local Y:
`radius` defaults to 0.5 and `height` defaults to 1, measuring the cylindrical
part between the hemispheres. Total end-to-end length is `height + 2 * radius`.
Radius must be positive and height nonnegative; height zero gives a sphere.
Body and collider rotations orient the capsule axis.

Convex colliders supply `vertices` as 4–64 local points. Lambda accepts nested
triples or a flat numeric array; HTML attributes accept flat numeric text.
The engine cooks the convex hull, merges coplanar faces and copies its geometry
into the world. Duplicate and interior points are allowed. Coplanar/collinear
clouds, nonfinite coordinates and geometry too thin for the cooking tolerance
report errors. The tolerance is relative to the cloud's largest extent
(approximately 1e-9). A world admits at most 65536 source vertices/height samples
across all its colliders, charged before welding.
Volume, centre of mass and the full inertia tensor come from the cooked solid,
including products of inertia and compound offsets. Bodies may be dynamic,
fixed or kinematic. Colliders may also reference same-scene geometry as described below.

```lambda
<collider id: "tetra-shape", shape: 'convex', density: 6.0,
    vertices: [[0,0,0], [1,0,0], [0,1,0], [0,0,1]]>
```

Environment colliders use indexed triangle surfaces, including concave/open
meshes; they must belong to fixed or kinematic bodies and have density zero.
`mesh` requires `vertices` and zero-based CCW `indices`, as nested triples,
flat arrays or flat numeric text. Cooking copies the buffers, welds exact
positions and builds a BVH with edge/vertex adjacency. Invalid indices,
degenerate/duplicate triangles, inconsistent winding and nonmanifold edges
report errors. Open boundaries are allowed.

`heightfield` requires integer `rows`/`columns` (at least two), exactly
`rows * columns` row-major `heights`, and optional `'cell-size':[width,depth]`
(default `[1,1]`, both positive). Heights accept flat arrays, nested rows or
numeric text. Local coordinates are `[column * width, height, row * depth]`,
starting at X/Z zero. Each cell has triangles `(a,a+columns,a+1)` and
`(a+1,a+columns,a+columns+1)`, where `a` is its upper-left sample index.
Interpolation follows these triangles, with upward CCW winding.

```lambda
<collider id: "terrain", shape: 'heightfield', rows: 2, columns: 2,
    heights: [[0.0,0.0], [0.0,2.0]], 'cell-size': [2.0,2.0]>
```

Surfaces default to front-facing collisions/rays; `'double-sided':true` admits
both sides and orients ray normals toward the ray origin. One-sided contacts
admit a convex body's centre on the front side (the hull mass centre for
convex colliders). Discrete contacts support spheres, capsules, boxes and
convex hulls. Coplanar internal-edge/vertex filtering and four-point plane
patches prevent flat tessellation seams from acting as ridges. Sharp creases
retain triangle features; smoothing arbitrary crease fans is outside this
profile. Surfaces have no enclosed solid volume or mass.

A mesh admits at most 131072 triangles; a world at most 262144. A welded
vertex admits at most 128 incident triangles. Queries and contact pairs
admit 8192 triangle tests each; manifolds admit 32 plane patches of four
points. Exhaustion reports an error, never a successful miss. Mesh tolerances
include local extent and coordinate precision. Very thin/large geometry can
still exceed numerical tolerances and report errors.

### Referenced collision geometry

A collider's `geometry` names a resource in the same scene. Forward references
are supported, and IDs use the existing scene-wide namespace. Convex and mesh
colliders can reference `<geometry type:'buffer'>`: `positions` supply local
points; mesh `indices` supply zero-based CCW triangles. An absent or empty index
stream uses consecutive triples. Convex hulls use the points independently of
render indices. Primitive, skinned and morphed render resources are outside this
static collision profile.

Physics-only definitions live inside `<resources>`. `physics.geometry(id,shape,
options)` produces `<'collision-geometry' version:1,...>` for convex, mesh or
heightfield data, using the same `vertices`, `indices`, `rows`, `columns`,
`heights` and `'cell-size'` fields as inline colliders. The collider must declare
the matching shape. These definitions create no graphics buffers. Resource
envelopes are checked during scene compilation; numeric geometry is ingested
for referenced instances.

```lambda
import physics: lambda.physics
let scene=<scene3d <physics>
    <'rigid-body' id:"ground",type:'fixed',
        physics.collider("ground-shape",'mesh',{geometry:"floor-data"})>
    <resources physics.geometry("floor-data",'mesh',{
        vertices:[[0,0,0],[2,0,0],[0,0,2]],indices:[[0,2,1]]})>>;
physics.validate(scene)^
```

References and inline geometry fields are mutually exclusive. Missing IDs,
wrong resource kinds/shapes, duplicate IDs and unsupported versions report
errors. Physics definitions cannot alias another resource or carry transforms.
Collider-local position/rotation, density, friction and sidedness stay on the
collider; transforms from a visual mesh are not inferred or baked into its
referenced coordinates. Data is copied into each collider's owned storage and
charged against the existing per-world quotas; shared authoring does not raise
those limits or imply shared native cooking.

Pure batch compilation and live DOM use the same lower-layer compiler
(**S12.1.1v3 / D7.1.1–D7.1.3**). Compatible live resource edits update every
referencing collider while preserving ticks, poses and velocities. Unchanged
data does not wake sleeping bodies; rejected edits retain the last working
world (**S12.1.5v2 / D4.5.3v2**). Assets are resolved by scene ID, without file or
network acquisition.

Density defaults to 1 for solid primitives/convex colliders and to 0 for
sensors and environment surfaces. Friction defaults to 0.5 and restitution to 0. Dynamic bodies require positive finite mass; mass, centre of
mass and inertia are computed from their colliders. Multiple colliders form
one compound body.

Coordinates use metres, kilograms and seconds by default, with Y up and
right-handed rotations in radians. Quaternion components are `[x,y,z,w]`.
Body transforms and their ancestors must have unit scale and no shear.
Render-only descendants can be scaled independently.

Body types are `'fixed'`, `'dynamic'`, `'kinematic-position'` and
`'kinematic-velocity'`. Kinematic position targets are supplied by commands;
keyframe tracks on physics-owned transforms currently report an error.

### Continuous collision detection

Set `ccd:true` on a dynamic body with exactly one solid sphere collider to
prevent fast projectiles crossing thin obstacles. This first CCD profile admits
other single-sphere dynamic bodies and fixed or translating kinematic
sphere/box/capsule/hull/mesh/heightfield environments. Sphere spin is supported:
its centre follows the centre-of-mass trajectory even with a collider offset.
Compound, capsule and hull bodies cannot enable CCD yet. A swept candidate
with a dynamic nonsphere target or rotating nonsphere environment reports an
unsupported-pair error; rotation is included in candidate bounds so it cannot
silently bypass that check. Collision groups apply. Sensors retain discrete
sampling and do not receive continuous crossing events.

The solver applies forces, gravity and damping once per tick, casts using
relative motion, advances every body to the earliest impact, solves the impact
and integrates the remaining time. A tick admits 32 impacts, 65536 candidate
pair casts and 8192 triangle tests; individual casts admit 128 iterations.
Numerical failure or exhaustion faults the step. Reads then retain the last
completed body snapshot; the failed tick publishes no events. Contact events
report pairs encountered during a completed tick, including swept impacts;
a transient pair ends on the next tick without that pair. The runtime exposes
`ccd:true`, `ccd_sphere:true` and `ccd_convex:false` to identify this coverage.
Enabling/disabling CCD is a compatible live body edit that preserves the clock
and motion (**S12.1.5v2 / D4.5.3v2**).

### Joints

Direct `<joint>` children of `scene3d` connect named bodies. The pure constructor
is `physics.joint(id, kind, body_a, body_b, options={})`. Forward references work
for ordinary and metadata bodies through the shared compiler
(**S12.1.1v3 / D7.1.1–D7.1.3**).

```lambda
import physics: lambda.physics
let scene=<scene3d
    physics.world({timestep:0.01})
    physics.joint("rod",'distance',"anchor","weight",{length:2.0})
    physics.body("anchor",[],{type:'fixed'})
    physics.body("weight",[physics.collider("shape",'sphere')],{position:[0.0,-2.0,0.0]})
>;
physics.simulate(scene,{steps:100,sample_every:100})^
```

| Kind | Constrained motion |
|---|---|
| `distance` | Fixed anchor separation; with `limits:true`, a free interval instead. |
| `spring` | Distance spring with positive `stiffness` in N/m and nonnegative `damping` in N·s/m; integrated implicitly. |
| `spherical` | Coincident anchors; all relative rotation is free. |
| `fixed` | Coincident anchors and matching attachment-frame orientation. |
| `hinge` | Coincident anchors and aligned local X axes; twist about X is free. |
| `prismatic` | Relative translation along frame A's X axis; other translation and relative rotation are locked. |
| `cone-twist` | Coincident anchors, swing inside the required `cone-angle`, and free/limited twist. |

`body-a`/`body-b` name distinct bodies, with at least one dynamic body. Joint
IDs share the scene ID namespace. `anchor-a`/`anchor-b` are local positions
relative to body origins, including compound-body centre-of-mass offsets.
`rotation-a`/`rotation-b` use Euler radians; `quaternion-a`/`quaternion-b` use
`[x,y,z,w]` and are exclusive with the corresponding Euler fields. Defaults are
zero anchors and identity orientations. Frames do not inherit a joint-node
object transform. A distance/spring's omitted `length` is inferred from the
authored attachment positions, including forward references. Explicit lengths
and distance lower limits must be positive; coincident distance anchors reject.

`limits:true` enables `lower-limit`/`upper-limit`. Distance/prismatic limits are
metres. Hinge/cone twist limits are radians within `[-pi,pi]`; twist coordinates
are periodic, not accumulated turn counters. Equal bounds lock the coordinate.
`cone-angle` must lie strictly between zero and pi. An active hinge/cone frame
at the swing/twist singularity reports an error. Inactive/broken joints can
report `coordinate:null` when their free pose has no defined twist coordinate.

`motor:true` enables `motor-speed` and `motor-max-force`. These are m/s and N
for distance/prismatic joints, rad/s and N·m for hinge/cone joints. Effort is
bounded per tick; zero effort gives no drive. Distance motors require spring
or range mode. `spring:true` on a `distance` joint is equivalent to the `spring`
kind; spring and hard range limits can coexist.

`enabled` defaults to true and `collide-connected` to false. Collision disabling
applies to body pairs connected by any enabled, unbroken joint. Geometric
queries retain their existing sensor/group/source-body filtering. Contact and
joint rows share impulse/inertia operations, with a coupled bilateral solve so
offset motors and limits use inertia about the constrained attachment. Bounded
position iterations correct drift; large initial errors recover gradually.
Contacts and joints share dynamic wake/sleep islands.

`break-force` (N) and `break-torque` (N·m) default to zero, meaning unbreakable.
They test the net reaction wrench at frame B's anchor. A completed break tick
emits one `joint-break` record containing `joint`, `body_a`, `body_b`, `force`
and `torque`. Live scenes also queue `physicsjointbreak` with that detail.
Connected collision is restored on the following tick. Reading/re-rendering
an unchanged broken description retains its broken state. Enable/disable
toggles also retain a fracture; changing its constraint definition rebuilds
that joint. This lets animation/physics mode changes suspend constraints
without silently repairing them.

Frames for joint-bearing worlds add `joints` records with IDs, anchors, reaction
force/torque, coordinate, linear/angular error, `enabled` and `broken`.
`bodies` selection filters body samples; joint records remain included. The
one-million sampled-record budget includes bodies and joints. Native worlds
admit 16384 joints and eight rows per joint; scene node/ID quotas still apply.
Unsupported combinations, singular blocks and numerical/work exhaustion report
errors. Faulted reads retain the last published body and joint snapshots.

Compatible live joint edits retain the clock and native motion; unchanged joint
caches and unrelated sleeping bodies survive. Rejected edits retain the working
configuration (**S12.1.5v2 / D4.5.3v2**). Active joints currently reject CCD on
either attached body; a CCD candidate involving a joint-attached target also
rejects. Coupled joint/CCD TOI solving remains open alongside the wider CCD
matrix. See the admitted capsule movement and physical-bone/ragdoll profiles below.

`physics.attach(mesh_or_group, options)^` supplies the equivalent description
through a `physics` metadata map. The metadata's body ID is distinct from the
visual node ID. Collider IDs and body IDs must be unique across the scene.

### Capsule character movement

`physics.move(scene, body, displacement, options={})^` computes a copied target
against a fresh scene's sampled collision world. It does not advance or mutate
that world (**S12.1.1v3 / D7.1.1–D7.1.3**). The body must be
`kinematic-position` with exactly one solid capsule; sensors may accompany it.
The capsule's local Y axis must agree with `up` (either sign; a zero-height
capsule is a sphere). Active joint attachments reject controller movement.

```lambda
import physics: lambda.physics

let scene = <scene3d
    physics.world({gravity:[0.0,0.0,0.0]})
    physics.body("floor", [physics.collider("ground", 'box', {size:[20.0,1.0,20.0]})],
        {type:'fixed',position:[0.0,-0.5,0.0]})
    physics.body("hero", [physics.collider("capsule", 'capsule', {radius:0.5,height:1.0})],
        {type:'kinematic-position',position:[0.0,1.001,0.0]})
>
physics.move(scene, "hero", [2.0,0.0,0.0], {step_height:0.3,snap_distance:0.1})^
```

The controller uses shared capsule casts and contact manifolds for overlap
recovery, sweep/slide, floor and ceiling classification, ascending slope limits,
an alternative up/forward/down stair path, and downward floor snapping. It clips
against all encountered planes so sliding into a corner cannot re-enter an
earlier wall. A positive requested up displacement suppresses floor snapping.
Gravity, jump timing and input policy remain script responsibilities. Dynamic
bodies are sampled obstacles; this profile does not apply pushing impulses or
predict their future motion.

| Option | Default / meaning |
|---|---|
| `up` | `[0,1,0]`, normalized; must be finite and nonzero |
| `skin` | `0.001 × length-scale`, positive; must exceed four times the movement precision |
| `slope_angle` | π/4 radians, strictly between zero and π/2 |
| `step_height`, `snap_distance` | `0.3`, `0.1` times length scale; finite, nonnegative |
| `max_slides`, `recovery_iterations` | 8 each; 1–32 |
| `max_iterations` | 128 per cast; 1–128 |
| `geometry_budget` | 4096 shared cast/manifold calls; 1–65536 |
| `platform`, `platform_pose` | Optional previous support collider ID and copied `{position,quaternion}`; both required together |

Movement precision is `10⁻⁶ × length-scale`. Recovery uses a slightly larger
shell than casts, including exact-contact roundoff; achieved motion includes
that recovery and small separation corrections. Every movement shares an
8192-triangle work budget across mesh contact recovery and casts, and admits up
to 32 distinct collider/normal contacts. Exhausted recovery, slide, geometry,
triangle or contact budgets return an error; they never silently accept a miss.

The result contains `position`, `quaternion`, `requested`, `achieved`, `recovery`,
`grounded`, `wall`, `ceiling`, `stepped`, `ground_normal`, `ground_body`,
`platform`, `platform_pose`, `platform_velocity`, `platform_motion`, copied
`contacts` (`body`, `collider`, `point`, outward `normal`) and work counters.
`ground_body`, `platform` and `platform_pose` are null when ungrounded; ground
normal and velocity vectors are zero. A previous support pose computes its
already-achieved translation/orbit around the unchanged upright capsule; that
carry is swept against other obstacles. It does not assume how far a platform
will move in a future tick. With a matching previous support, platform velocity
is the pose-derived point displacement divided by the world's timestep;
otherwise it is the sampled rigid body's point velocity.

The same request is available in a mixed query array as
`{type:"move",body:"hero",displacement:[2,0,0],...options}`. Ordinary query
results retain their existing shapes. Live `lambda.physics.scene.move(node,
body, displacement, options)` atomically computes and queues the target using
one Radiant host operation. It returns the geometric result immediately; the
existing coordinator applies the target on the next physics tick. This keeps
the final transform with the existing physics owner (**S12.1.5v2 / D4.5.3v2**).
The live `query` form only computes the target. Compatible collider/camera edits
retain the world and tick. `character_controller` / `character_capsule` report
this admitted profile; future-motion controller CCD and explicit
dynamic pushing remain outstanding.

### Physical bones and ragdoll modes

`physics.bind_bone(bone, body_id, options)` binds an identified `bone` to a
same-scene body, including forward references. The equivalent attributes are
`physics-body`, `physics-offset` (body origin in bone coordinates), and either
`physics-rotation` (Euler XYZ radians) or `physics-quaternion` (x/y/z/w).
`physics-enabled` defaults to true. Each body can bind one bone; up to 1024
bindings are admitted. Authored bone ancestors must have rigid TRS transforms.

The physical bone world frame is `body_world * inverse(body_in_bone)`. It feeds
the existing skeleton palette and inverse bind matrices. The physical owner
supplies the complete rigid pose; animation continues independently as a
possible recovery destination (**S12.1.5v2 / D4.5.3v2**). Disabling a binding
restores authored/animated bone transforms while its body continues simulating.
Binding edits retain the existing world and clock. Frames add optional `bones`
records `{id,body,enabled,position,quaternion}`; disabled records have null pose
fields. Body selection filters these records, and trace quotas charge them.

Declare a live controller with
`physics.ragdoll(id, bone_ids, {mode: 'animated', 'recovery-duration': 0.5})`, or
an empty direct `ragdoll` scene child with those attributes. HTML `bones` is a
whitespace-separated ID list. Up to 16 controllers, each with 1–64 bones, are
admitted; a bone can belong to one controller. Its bindings must be enabled and
refer to authored dynamic bodies without CCD.

- `animated` uses position-kinematic bodies, zero collision masks and suspended
  touching constraints. Each shared tick samples the current animation and
  targets `bone_world * body_in_bone`.
- `physical` restores authored dynamic roles, masks and constraints. Activation
  retains the last achieved body poses and measured linear/angular velocities.
- `recover` captures the achieved physical poses and blends toward the moving
  animation destination over `recovery-duration` seconds, using shortest-arc
  quaternion interpolation. It then follows animation. Editing to `animated`
  requests an immediate snap; choose `recover` for a gradual handoff.

Recovery duration lies in `(0,60]`. Animated destinations must stay rigid;
fixed and kinematic body ancestors are supported. Pending kinematic parent
targets are sampled for the upcoming tick. Physical bone ancestors must belong
to the same controller or be fixed. Dynamic/cross-controller physical ancestors
require a later coupling profile. Script chooses modes; native playback supplies pose
and solver transitions, without gameplay decision algorithms.

Live physics snapshots add `ragdolls` records
`{id,mode,requested,elapsed,weight}`. Completed recovery reports `mode: animated`
while retaining `requested: recover`. Compatible mode edits retain the world,
clock and unrelated state. Broken constraints stay broken through suspension.
Explicit physics steps with controllers run animation before physics through
the shared coordinator; reset samples authored animation at zero. Scene seek
replays the **current authored description** from zero through a separate
animation mixer and world, then adopts both after success. It reconstructs
authored modes rather than historical script commands. Replay emits no game
cues; failure keeps the previous world, mixer and displayed poses. Pure `physics.simulate`
accepts physical rigs; animated/recovery batch simulation requires a later
shared animation sampler and currently returns an error. `physics.validate`
checks all controller descriptions. Dynamic pushing/future-motion coupling and
joint/CCD coupling still leave full M3 incomplete.

## Batch simulation and queries

| Function | Result |
|---|---|
| `physics.validate(scene)^` | `true`, or a propagated validation error |
| `physics.simulate(scene, options)^` | `{engine, timestep, ticks, frames, events}` |
| `physics.query(scene, requests)^` | Array of initial-scene query results |
| `physics.trajectory(trace, body)` | Body samples with `tick` and `time` |
| `physics.chart_data(trace, options)` | Records containing position, height, speed and mass |
| `physics.energy(trace, body, gravity)` | Translational kinetic and uniform-gravity potential terms |
| `physics.tracks(trace, body, target)` | Position and quaternion animation tracks for a visual target |

The native spellings are `physics_validate(scene)`,
`physics_simulate(scene, options)` and `physics_query(scene, requests)`.
They return error values on invalid input. Package wrappers expose the
corresponding error channel with postfix `^`.

Simulation options are `steps` (default 120, at most 100000), `sample_every`
(default 1), `bodies` (selected IDs) and `commands`. Frames include tick zero
and the final tick. Playback state and frame pacing do not affect a batch run.
The trace has a limit of one million sampled body records and 65536 events.

Commands use `{type, body, value}` records. Supported types are `impulse`,
`force`, `torque`, `velocity`, `angular_velocity`, `teleport`, `target`, `wake`
and `gravity`.
Teleport/target commands supply `position` and optionally `rotation` or
`quaternion`. `point` supplies an application point; `local:true` selects
body-local vector/point coordinates. An omitted point means centre of mass.
A force or torque applies to the next tick; an impulse applies once.

Scheduled batch commands have the form
`{tick: 0, commands: [{type: "impulse", body: "ball", value: [1,0,0]}]}`.
They run immediately before the named tick's integration step.

Queries use `type:"ray"`, `type:"point"` or `type:"aabb"`. Rays supply
`origin`, `direction` and `distance`; direction is normalized, so distance is
in scene units. AABB queries supply `origin` and full `size` and are explicitly
broad-phase tests. Each request returns its nearest/first matching collider
record, or `null`. Use `sensors:true` to include sensors. Initial-scene queries
do not advance physics; live queries use the current scene world.
Point and ray tests use the actual sphere, oriented box, capsule, convex hull
or triangle surface. A point matches a mesh/heightfield only on its surface
within numerical tolerance, independently of sidedness. AABB tests retain their broader meaning.

`{type:"distance", collider_a:"a-shape", collider_b:"b-shape"}` measures a
named pair of colliders using shared exact rounded-feature witnesses or bounded
GJK for polyhedral pairs.
For one mesh/heightfield and one convex solid, BVH traversal finds the nearest
triangle witnesses. Surface distances are geometric and ignore sidedness;
mesh/mesh distances report an unsupported-operation error. The
result adds `point_b` and `overlapping`: `point` and `point_b` are closest
witnesses on A and B when separated. `distance` is nonnegative and `normal`
points from A toward B when separated.
The `body`/`collider` fields identify B. Intersection or contact within the
numerical tolerance returns zero distance, `overlapping:true` and a zero
normal; witnesses may be interior points in that case. This is not a signed
penetration-depth query. Explicit pairs include
sensors and ignore collision-group filtering. Unknown IDs, numerical failure,
stalled convergence or the 128-iteration budget produce errors, not misses.

### Exact overlaps, casts and all-hit output

`{type:"overlap",collider_a:"probe-shape"}` tests an existing convex source
collider against eligible targets using exact geometry. The source can be a
sphere, capsule, box or hull. `offset:[x,y,z]` translates its sampled pose in
world coordinates without changing its body. Surface overlap is geometric and
ignores mesh sidedness. Every collider on the source body is excluded.

`{type:"cast",collider_a:"probe-shape",direction:[1,0,0],distance:10}` sweeps
that source in a straight line with fixed orientation against sampled target
poses. Targets include convex shapes and mesh/heightfield environments. Casts
honor surface sidedness and suppress coplanar internal seams. Direction is
normalized; zero travel permits a zero direction. `offset` has the same meaning
as for overlap. Cast results add `point_b`, `fraction`, `initial_overlap` and
`iterations`. Distance is travelled distance and fraction is in `[0,1]`. The
normal points outward from the target toward the source. Initial overlap gives
zero distance/fraction and a zero normal. `max_iterations` is an integer from
1 to 128 (default 128); exhaustion or stalled convergence reports an error.

Add `all:true` to any query to return an array of hits instead of one hit or
`null`. Results contain one hit per eligible collider, sorted by distance then
native collider identity; a mesh contributes one collider hit. No hits gives
`[]`. `max_hits` is an integer from 1 to 65536 (default 65536); exceeding it
reports an error rather than truncating. A request batch also admits at most
65536 output hit records. The existing 8192 triangle-test budget applies across
mesh targets in each query.

Queries exclude sensors by default; `sensors:true` includes them. Optional
`groups:{membership,filter}` uses the same bidirectional bit masks as collider
filtering (both default to all bits). Explicit named distance pairs retain their
unfiltered behavior. Exact overlap records add `point_b` and `overlapping`.
All these queries are read-only in batch and live scenes (**S12.1.1v3 /
D2.11.4**); they neither advance the clock nor apply a cast's movement.

## Live scenes

Set `<physics state:'playing'>` for automatic fixed-step playback. The default
is paused. The document owns the world and rendering consumes its poses;
camera edits, repaint, resize and graphics-context recovery preserve simulation
state (**D4.5.1v4 / S12.1.5v2**). So do template re-renders. An authored edit
overrides only what it changes: a new gravity, body position or velocity takes
effect as a command, and damping, gravity scale and collider changes are applied
in place, all without resetting the tick. This includes valid capsule radius,
height, convex/mesh vertices, mesh indices, height samples, cell size, sidedness
and shape edits. Compatible rejected geometry edits
report an error while preserving the previous owned world and simulated state;
unchanged geometry does not wake sleeping bodies (**S12.1.5v2**).
Compatible body motion-role edits also retain the world, clock and achieved
pose. Activating dynamics keeps measured kinematic linear/angular velocity.
Linear velocity follows the mass centre, including offset colliders rotating
under a position target. Velocity-kinematic parent prediction and integration
share that same mass-centre/origin convention.
Changing roles discards pending targets and forces/torques from the previous
role. Entering `fixed` stops both velocities. Geometry, constraints, roles and
authored pose/velocity commands validate and commit together, so an invalid
whole transition keeps the old world and its readable snapshots. Inactive
joints can connect kinematic/fixed bodies; an active unbroken constraint still
requires a dynamic endpoint. Nonzero velocity commands on fixed bodies reject.
These role changes use the native full-descriptor `physics_reconfigure` path;
the legacy `physics_configure` geometry/property API still requires unchanged
motion roles. Adding/removing bodies or colliders, changing collider ownership,
or changing the timestep, iterations or
length scale, restarts the world from the new authored state.

Import `lambda.physics.scene` for procedural `play`, `pause`, `reset`, `step`,
`snapshot`, `events`, `command` and `query` helpers. These call the existing
Radiant host boundary (**D7.5.3v2**) using a scene DOM node. `snapshot` returns
`{tick,time,bodies}`. JavaScript calls to exported Lambda procedures return
Promises and must await their result.

Contact/sensor notifications are queued after solver execution as
`physicscontactstart`, `physicscontactend`, `physicssensorenter` and
`physicssensorexit` events on the scene root. Their `detail` contains tick,
body IDs and collider IDs. Trace events use `contact-start`, `contact-end`,
`sensor-enter` and `sensor-exit` type strings.

Open the Scene3D index and choose a physics experiment:

```sh
./lambda.exe demo scene3d
```

The index has direct tiles for a 32-tile domino cascade, a zero-gravity compound-body
chamber, a kinematic marble sorter with sensor counters, and a restitution
comparison with native trajectory charts. Each has playback controls and an
orbit camera. See the [demo guide](../test/demo/scene3d/physics/README.md).

The original minimal example is also available:

```sh
./lambda.exe view test/demo/scene3d/physics/rigid.ls
```

The current profile has discrete sphere/box/capsule/convex contacts, fixed and
kinematic mesh/heightfield environments, sleeping, compound mass properties and
native exact overlaps, translation casts, sorted all-hit queries and the
single-sphere CCD and joint profiles above. Broader CCD, dynamic concave meshes,
2D and deformable bodies require later engine stages. Unsupported requests
return errors. Scoped headless live-world system functions remain gated on
the lexical cleanup contract in **S12.4.2–S12.4.3**.

See the [design](../vibe/Lambda_Pkg_Physics.md),
[initial implementation record](../vibe/impl/Lambda_Impl_Physics.md) and
[Scene3D gaming progress](../vibe/impl/Radiant_Impl_Scene3D_Gaming.md).
