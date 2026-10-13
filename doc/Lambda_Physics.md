# Lambda Physics

The initial physics implementation provides a native C 3D rigid-body engine,
physics-bearing `scene3d` elements, pure batch simulation, and the
`lambda.physics` package. It follows **S12.1.1v2**: constructors and batch
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

The initial collider shapes are `'sphere'` and `'box'`. Box `size` gives full
extents. Density defaults to 1, friction to 0.5, restitution to 0, and sensor
density to 0. Dynamic bodies require positive finite mass; mass, centre of
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
`force`, `torque`, `velocity`, `teleport`, `target`, `wake` and `gravity`.
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

## Live scenes

Set `<physics state:'playing'>` for automatic fixed-step playback. The default
is paused. The document owns the world and rendering consumes its poses;
camera edits, repaint, resize and graphics-context recovery preserve simulation
state (**D4.5.1v4 / S12.1.5v2**).

Import `lambda.physics.scene` for procedural `play`, `pause`, `reset`, `step`,
`snapshot`, `events`, `command` and `query` helpers. These call the existing
Radiant host boundary (**D7.5.3**) using a scene DOM node. `snapshot` returns
`{tick,time,bodies}`. JavaScript calls to exported Lambda procedures return
Promises and must await their result.

Contact/sensor notifications are queued after solver execution as
`physicscontactstart`, `physicscontactend`, `physicssensorenter` and
`physicssensorexit` events on the scene root. Their `detail` contains tick,
body IDs and collider IDs. Trace events use `contact-start`, `contact-end`,
`sensor-enter` and `sensor-exit` type strings.

Run the demonstration with:

```sh
./lambda.exe view test/demo/physics/rigid.ls
```

The current profile has discrete sphere/box contacts, sleeping, compound mass
properties and native queries. CCD, joints, convex/triangle-mesh colliders,
2D and deformable bodies require later engine stages. Unsupported requests
return errors. Scoped headless live-world system functions remain gated on
the lexical cleanup contract in **S12.4.2–S12.4.3**.

See the [design](../vibe/Lambda_Pkg_Physics.md) and
[implementation record](../vibe/impl/Lambda_Impl_Physics.md).
