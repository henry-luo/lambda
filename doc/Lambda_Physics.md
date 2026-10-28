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
fixed or kinematic. Geometry resource references remain unsupported.

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

`physics.attach(mesh_or_group, options)^` supplies the equivalent description
through a `physics` metadata map. The metadata's body ID is distinct from the
visual node ID. Collider IDs and body IDs must be unique across the scene.

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
named pair of colliders using bounded GJK, including primitive pairs.
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
Adding/removing bodies or colliders, changing body
motion type or collider ownership, or changing the timestep, iterations or
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
native queries. CCD, general shape casts/overlaps, joints, dynamic concave meshes,
2D and deformable bodies require later engine stages. Unsupported requests
return errors. Scoped headless live-world system functions remain gated on
the lexical cleanup contract in **S12.4.2–S12.4.3**.

See the [design](../vibe/Lambda_Pkg_Physics.md),
[initial implementation record](../vibe/impl/Lambda_Impl_Physics.md) and
[Scene3D gaming progress](../vibe/impl/Radiant_Impl_Scene3D_Gaming.md).
