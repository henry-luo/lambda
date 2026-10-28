# Radiant Scene3D — Native Gaming Systems

**Status:** proposal, 2026-10-28. The user requested the six feature areas below
and selected `radiant/scene3d/` as the implementation directory, including
relocation of the existing Scene3D-owned native sources. Algorithms, API
spellings, quality profiles and milestones in this document are recommendations,
not claims of delivered support or newly ratified language rulings.

**Baseline inspected:** checkout `d53f1a043` plus the current working-tree
Scene3D, outdoor and audio changes. No new engine benchmark or qualification
run was performed for this proposal.

**Scope:** extend native Scene3D with complete staged physics, navigation,
animation graphs and focused authoring tools, advanced lighting/effects,
ready-made particles, and game audio. Preserve composition with HTML/CSS/SVG,
Lambda's effect model, and existing native ownership and module boundaries.
This document proposes the work; it does not move source files itself.

**Relationship to existing designs:** extend the
[native scene and WebGL design](Radiant_Design_WebGL.md),
[physics design](../Lambda_Pkg_Physics.md),
[motion design](../Lambda_Pkg_Motion.md), and
[audio/speech design](Radiant_Design_Speech.md). Their selected foundations
remain in force. Scene3D gaming integration and delivery dependencies live here;
the focused documents retain their detailed contracts. Full implementation
plans and progress belong under `vibe/impl/`, per the
[documentation convention](../../doc/Doc_Convention.md).

## 1. Outcome and scope

Make native Scene3D suitable for small interactive games and simulations without
requiring a JavaScript game framework or a separate engine. A creator should be
able to combine an animated character, a physical environment, agents, effects
and spatial sound with ordinary Lambda scene descriptions and procedural
handlers. Visual tools edit those same descriptions.

| Requested area | Target capability |
|---|---|
| Physics | Genuine 2D and 3D worlds; static, dynamic and kinematic bodies; character movement; joints, motors and limits; CCD; convex/mesh environments; soft bodies and ragdolls. |
| Navigation | Graph/grid A*, 2D navigation polygons and 3D navigation meshes, baking, path following, agents, links and local obstacle avoidance. |
| Animation authoring | Native blend trees, 1D/2D blend spaces and state-machine graphs, with visual editing, scrubbing and runtime inspection. |
| Lighting/effects | Baked lightmaps and indirect-light probes, bounded dynamic diffuse GI, SSAO, screen-space reflections, volumetric fog and depth of field. |
| Particles | Ready-made CPU/GPU emitters, reproducible seeds, lifetime curves, collision profiles, trails and focused authoring tools. |
| Game audio | Positional 2D/3D sources, listener control, Doppler, microphone capture, configurable bus routing and effect chains. |

Every area has a useful first profile and a finite completion milestone (§11).
Soft bodies, 2D physics, dynamic GI and capture remain explicit deliverables,
even though they follow the first playable core. Completion means the stated
profiles work together, not unrestricted parity with every Godot feature.

A complete general game editor, multiplayer replication, gamepad mapping, XR,
mobile/web export, a new graphics backend, and browser API conformance are
separate projects. Focused graph, particle, bake and audio tools are in scope.
Additional gameplay algorithms are kept in view in §13, with Lambda Script as
the preferred implementation layer. These KIV scopes are outside M0–M12's
delivery and acceptance boundaries.

## 2. Current foundation and formal contracts

### 2.1 Reuse the implemented foundation

| Existing facility | Continue from | Remaining gaming work |
|---|---|---|
| Native scenes | `radiant/scene3d.cpp`, `lmd/package/scene3d/`: cameras, meshes, instances, resources, PBR/physical materials, HDR environments, shadows including the current directional-cascade work, fog, culling and LOD | Shared simulation/presentation coordination and the advanced lighting profiles below. |
| Graphics/effects | `radiant/gl_core.*`, shader materials, retained float-target simulations, bloom/FXAA/custom color passes | Reusable depth/normal inputs, visibility histories and bounded volumetric resources. |
| Animation | `radiant/animation_mixer.*`, `animation_value.*`, Scene3D bindings, skinning/morphs, FBX/BVH conversion, retargeting and motion controllers | Graph evaluation, blend spaces, richer transitions and authoring UI. |
| Physics | `lib/physics/`, `lambda/core/physics_scene.*`, `lambda/runtime/lambda-physics.*`, Scene3D adapter | Current sphere/box 3D profile needs the additional shapes, solvers, controllers and dimensions in §4. |
| Audio | `radiant/audio_bus.*`, `audio_reverb.*`, `audio.*`, MIDI/speech and decoded-media services | Game routing, spatialization, Doppler, capture and consistent connection of game sources to the existing output bus. |
| Authoring infrastructure | Lambda UI, `lambda.editor.draw`, editor history/transactions, live DevTools and existing Scene3D demos | Focused tools over serializable gaming descriptions, rather than another editor model. |

The [current Scene3D review](Radiant_Scene3D_Review.md),
[Three-feature delivery record](../impl/Radiant_Impl_Scene3d_Three_Features.md),
[outdoor record](../impl/Radiant_Impl_Scene3d_Eden.md), and
[physics guide](../../doc/Lambda_Physics.md) provide the starting inventory.
Older proposal sections are not evidence that their future stages shipped.

### 2.2 Spec-linkage map

| Authority | Consequence for this proposal |
|---|---|
| **S12.1.1v3 / S12.1.3** | Scene constructors, validation and graph descriptions are pure; live commands, input/capture acquisition and authoring mutations are procedural. No hidden script execution in a constructor. |
| **S12.1.5v2** | The template owns authored intent; native playback/simulation owns transient state. A higher-tier edit overrides the state it touches, rather than competing with another writer. |
| **D4.5.1v4 / D4.5.2 / D4.5.3v2** | Retain native/document-owned copies and generation handles; borrow GC values only during a call, or keep them through an explicit registered root. Preserve compatible downstream state through regeneration. |
| **D5.3.3** | Script callbacks and result conversion use precise `RootFrame` / `Rooted` ownership. No conservative native-stack scanning. |
| **D2.11.4** | A capture settles the frame selected by the virtual timer. Reading the same frame does not independently advance playback or simulation. |
| **D7.1.1–D7.1.3 / D7.3.3–D7.3.4** | Keep static-library/provider layering. Neither the runtime nor a pure package gains a direct dependency on Radiant internals. Source relocation does not relocate lower-layer providers. |
| **D7.4.1v2 / D7.4.2v2 / D7.4.4** | Use branded handles and the existing host-object protocol. Async delivery crosses the established host boundary, with copied results rather than borrowed runtime/DOM pointers. |
| **D7.2.4v4 / D7.5.3v2** | Public helpers use existing package namespaces; live operations reach Radiant through `radiant-dom`. No second public graphics or callback-registration waist. |
| **D7.1.2v2 / D7.5.2** | Resource acquisition uses existing IO providers. Physics, navigation, baking and audio processing do not open files or sockets themselves. |
| **D7.5.4** | All audible game output keeps the document-origin activation policy. Microphone acquisition requires a separate explicit permission contract (§9). |
| **D7.1.4v2 / D7.1.7v6** | Preserve full-host, runtime-only, null-backend and browser-WASM distinctions. This proposal does not add graphics or capture to an excluded profile. |
| **D7.3.5** | Advertised profiles have registered acceptance cases and applicable aggregate gates. A parsed tag is not feature support. |

See [Formal Semantics](../../doc/Lambda_Formal_Semantics.md) and
[Formal Design](../../doc/Lambda_Formal_Design.md). This proposal revises no
formal ruling and creates no new decision-ID series. Uncovered choices below
remain recommendations for ratification before their implementation milestone.

## 3. One native scene runtime

### 3.1 Ownership and integration

Give each scene root one document-owned runtime coordinating persistent physics,
navigation, animation, particles, audio bindings and disposable graphics
projections. Keep these as separate services with explicit dependencies; an
audio source does not require a physics world, and a navigation query does not
create GPU resources.

```mermaid
flowchart TD
    Author[Lambda scene values and focused authoring tools] --> DOM[Existing DOM and scene normalization]
    Commands[Procedural handlers through radiant-dom] --> Runtime[Scene3D native runtime]
    DOM --> Runtime
    Runtime --> Nav[Navigation and avoidance]
    Nav --> Motion[Animation graph and motion requests]
    Motion --> Physics[Existing lib/physics plus new profiles]
    Physics --> Pose[Published body and skeletal poses]
    Pose --> Present[Presentation interpolation and particles]
    Present --> Render[Scene renderer through shared gl_core]
    Present --> Audio[Scene audio through shared window audio bus]
    Render --> Image[Owned image in normal Radiant paint order]
    Runtime --> Events[Queued events at the existing host boundary]
```

The diagram expresses data flow, not a requirement to instantiate every service.
Promote a shared geometry/query helper when multiple consumers need it; do not
copy physics, camera picking, animation interpolation or shader deformation
code into navigation, particles or effects. Start with native structs, explicit
storage and operation tables, following the project's C+ convention.

### 3.2 Tick, presentation and event order

Extend the existing frame scheduler. Each scene has a fixed simulation timestep,
integer tick index, pause/time-scale controls and a presentation interpolation
fraction. Existing independently timed SVG/CSS animations retain their current
contracts; joining a scene timeline is explicit.

For each fixed tick:

1. Admit the queued command batch and completed resource updates. Validate
   structural edits before replacing working state.
2. Update paths/avoidance from the last completed world snapshot. Produce desired
   motion, not direct writes to dynamic-body poses.
3. Evaluate animation parameters and transitions; sample the pre-physics pose
   and root-motion delta. Supply targets to character/kinematic controllers.
4. Step rigid/deformable physics; solve contacts, joints and ragdoll constraints.
5. Apply post-physics skeletal overrides and publish completed poses. Update CPU
   emitter state and immutable audio source/listener parameters.
6. Queue contact, path, animation and particle events for host delivery. Handler
   commands enter a subsequent tick; callbacks never re-enter a running solver.

At presentation, interpolate completed state, update GPU particles to the
selected simulation time, render required passes, and publish one owned image.
The audio device runs on its own sample clock; scene events are timestamped and
mapped onto it instead of making audio wait for rendering (§9).

Keep the existing physics overload policy: bound steps per presentation,
retain unprocessed tick debt, report lag, and pause with a diagnostic if the
configured debt limit is exceeded. Background pause does not accumulate an
unbounded catch-up interval. Never stretch the physics timestep silently.

Seek is a first-class operation. Animation-only scenes sample directly;
physics, stateful particles and dynamic GI require reset/replay or an explicitly
compatible checkpoint. Reset temporal rendering history after seek. A pose
trace is not a resumable solver checkpoint. GPU simulation is not promised to
replay bitwise across devices.

### 3.3 Regeneration, failures and capabilities

Track separate generations for topology, collision data, animation graphs,
render resources, navigation tiles and audio routing. A camera or UI edit must
not reset a world, rebuild every navmesh, restart a clip or reopen the device.
Stable scene IDs plus validated generations bind authored descriptions to
runtime objects; DOM addresses are not persistent identity.

Prepare edits transactionally, then replace only affected services at a tick or
audio-buffer boundary. Preserve the last valid scene on preparation failure and
report the rejected node/property. Device loss retires GPU resources without
discarding CPU worlds, graph state or navigation assets.

Expose a capability report with supported profiles, limits and located reasons
for unavailable features. `auto` may choose a documented cheaper profile and
report the choice; an explicitly required profile fails if unavailable. Never
accept CCD, GPU particles or GI and silently omit their effect.

Bake/rebuild workers consume immutable native snapshots and return owned,
generation-tagged results. They never traverse the live DOM or enter its
evaluator. Cancellation, document teardown or a newer source generation prevents
publication; the worker still releases its buffers. Publish only on the owning
host thread through the established completion route (**D7.4.2v2**). When a job
uses a Lambda worker, retain **D6.3.2** share-nothing isolation rather than
sharing a document's execution state.

Replace the current per-service scene scheduling registrations incrementally
with one coordinator registration. Legacy playback operations update that
coordinator; they cannot leave an old callback advancing a service a second
time. Tick/frame identities guard event and simulation publication across
repaint, capture and template regeneration.

## 4. Complete physics in bounded stages

Extend the selected native C engine in `lib/physics/`; keep its header free of
`Item`, DOM, GL and scheduler types. Scene-specific bindings, controllers and
pose publication belong in `radiant/scene3d/`. Preserve pure batch simulation
and headless consumers through the existing lower-layer scene compiler.
This follows **D7.1.1–D7.1.3**, rather than moving the shared solver upward.

### 4.1 Bodies, collision and CCD

Retain fixed/dynamic/kinematic bodies, compound shapes, sensors, filtering,
mass/inertia, sleeping and query semantics. Add the following explicit profiles:

| Profile | Proposed implementation and boundary |
|---|---|
| 3D primitives/convex | Capsule contacts/casts, convex hulls, support mappings, GJK distance and penetration/manifold recovery; preserve existing analytic/SAT fast paths. Degenerate input and nonconvergence produce diagnostics. |
| Mesh environments | Immutable triangle meshes and heightfields with BVH acceleration, sidedness, adjacency/internal-edge handling and scale-aware tolerances. First support fixed/kinematic environment meshes against admitted convex bodies; arbitrary dynamic concave meshes require explicit convex decomposition. |
| Scene queries | Nearest/all ray hits, point queries, exact admitted shape overlaps and swept shape casts. Preserve the current broad-phase AABB query's distinct meaning. |
| CCD | Relative-motion shape sweeps/time-of-impact with bounded conservative advancement and remaining-time integration. Publish supported pair/rotation coverage; start with spheres/capsules against primitives/static triangles, then convex pairs. |
| Genuine 2D | Separate XY positions, scalar rotation/inertia, 2D broad phase/manifolds and circle/capsule/convex-polygon/edge-chain shapes. Reuse solver-row machinery where valid; do not fake 2D with thin 3D boxes or hidden Z constraints. |

CCD substeps account for one tick exactly once: forces and impulses are not
reapplied as though every substep were a whole tick. Shape deformation/rotation
coverage, iteration exhaustion and maximum impact count are explicit. An
exhausted CCD budget cannot become a successful collision miss.

2D worlds use physical units and an explicit up direction. An orthographic
Scene3D presentation or a separate SVG adapter maps those coordinates to pixels;
CSS's downward Y and a visual Z layer never enter the 2D solver implicitly.
Mixed 2D/3D worlds do not collide without an explicit application-level bridge.

### 4.2 Joints, characters and ragdolls

Share persistent constraint rows, warm starts and island solving between
contacts and joints. Stage fixed, distance/spring, revolute/hinge, prismatic,
spherical and cone/twist constraints with motors, limits and optional break
thresholds. Define local anchor frames, angular units, motor force/torque limits,
and whether connected bodies collide. Validate singular configurations before
stepping and invalidate only affected cached impulses after edits.

The character controller consumes a requested displacement/velocity and returns
achieved motion, contacts and grounding. Use capsule sweeps with a shared
move-and-slide loop, slope limits, step-up/down, floor snapping, ceilings and
moving-platform relative velocity. Define pushing of dynamic bodies separately
from kinematic collision resolution. First deliver the 3D controller, then a
2D counterpart using 2D queries. Neither controller is an autonomous game AI.

Root motion requests movement through the controller; the controller/physics
result owns the character's world transform. Animation may adapt stride to the
achieved motion, but cannot overwrite that transform afterward
(**S12.1.5v2**).

Ragdolls map selected skeletal joints to ordinary rigid bodies/colliders and
joint constraints. Store bind offsets explicitly. Support animated, physical
and blended recovery modes, transferring velocities on activation and restoring
pose continuously on recovery. Physical bones own their final pose; disable
overlapping post-animation writers. Reuse the existing skin palette and pose
publication path rather than creating a second skinned renderer.

### 4.3 Soft bodies

Start with bounded surface meshes for cloth and deformable props. Use a native
position-based solver with timestep-aware compliance (XPBD candidate), stretch
and bend constraints, damping, pinned vertices and rigid-body attachments.
Closed deformable props may add an explicit enclosed-volume constraint; open
cloth does not acquire a volume-preservation claim.
Define mass distribution, substeps, iteration count, collision thickness and
energy limitations; this is a game deformation profile, not an FEM accuracy
claim.

The completion profile includes external rigid contacts, self-collision for
admitted nonadjacent vertex/triangle pairs, and an explicit two-way attachment
coupling policy. Use fixed substep ordering or a tested joint iteration with
the rigid solver; never apply the same coupling impulse twice. Share topology,
BVH and deformation uploads with the rendering/query services. Tearing,
arbitrary volumetric materials and fluid simulation are subsequent work.

**Acceptance:** a capsule climbs allowed stairs, rejects a steep slope and
rides a moving platform; CCD stops an admitted projectile at a thin wall;
joint chains retain bounded drift and obey motors; a ragdoll falls and recovers
without pose jumps; 2D stacks and edge-chain movement behave independently of
render density; pinned cloth contacts a moving body and a deformable prop
retains bounded volume/shape error for its declared constraints. Publish numeric
tolerances and unsupported pairs with each fixture.

## 5. Navigation and pathfinding

Navigation owns traversability and desired motion. Physics owns actual motion.
Keep their collision representations related through explicit source selection,
not by assuming every render triangle is walkable.

### 5.1 Static data and queries

Provide graph/grid A* in 2D/3D with stable node IDs, nonnegative edge costs,
agent filters and a bounded search budget. Use admissible heuristics for the
declared graph metric; arbitrary weighted graphs can use a zero heuristic.
Report complete, partial, unreachable and budget-exhausted results distinctly.

Add navigation assets containing 2D polygons or connected 3D walkable surfaces,
area costs, portals, tile bounds and off-mesh links. Recommended bake pipeline:
collect explicitly selected geometry; classify walkable surfaces; voxelize or
construct the corresponding 2D domain; erode for agent radius/clearance;
partition regions; produce polygons and adjacency; validate portals and links.
Store bake settings, source hashes, coordinate frame and schema version.
Different agent sizes require compatible clearance metadata or separate bakes.

Find a polygon corridor, then apply funnel/string-pulling to obtain waypoints.
Bake static meshes offline first; add bounded dirty-tile rebakes with immutable
input snapshots and generation-checked publication. A door can disable a link
or region immediately; a moving obstacle does not require a full-scene rebake.

### 5.2 Agents and avoidance

An agent stores its target, current path/version, radius, speed/acceleration
limits and repath policy. Query work is scheduled fairly within an explicit
per-tick budget; cache static connectivity without sharing mutable agent state.

Use a shared spatial index for local neighbors and a reciprocal velocity-based
avoidance solver (ORCA-style candidate). Project its preferred velocity back
into navigable space, then request character movement. Physical contacts and
achieved movement feed the next tick. Include priority/yield rules and bounded
stuck detection/replanning; local avoidance alone cannot guarantee deadlock-free
crowd movement or replace narrow-passage path planning.

Events include path ready/failed, target reached, path invalidated and stuck.
They carry agent/path generations and are delivered outside traversal/solving.
An editor overlay shows walkable regions, links, corridors, requested velocity
and achieved velocity using existing debug geometry.

**Acceptance:** A* agrees with a shortest-path oracle on a weighted graph;
2D polygons and a 3D room/corridor bake respect clearance; agents route around
a hole, use a link, react to a closed door and avoid crossing neighbors; a
narrow corridor exposes the declared yield policy; stale tile/path completions
cannot revive deleted geometry. Repeated camera edits do not rebake navigation.

## 6. Animation graphs and focused authoring

Extend the existing mixer and Scene3D bindings. A graph compiles to a native
evaluation plan over existing clips/actions; it is not another animation engine.
Use immutable graph assets plus scene-owned parameters, transition state,
playheads and pose scratch buffers.

| Graph feature | Proposed behavior |
|---|---|
| Clip and blend nodes | Sample a clip, blend two/multiple inputs, or apply an additive layer with explicit reference pose and joint mask. Reuse shared interpolation and normalized quaternion blending. |
| 1D blend space | Sorted sample positions, piecewise-linear weights and declared endpoint clamping. Reject duplicate/invalid samples. |
| 2D blend space | Explicit or generated nondegenerate triangulation and barycentric weights; declare outside-hull clamping/projection. Collinear samples require a 1D profile. |
| State-machine graph | Named states/subgraphs, parameter predicates, transition priority, exit windows, crossfade duration, interruption policy and explicit triggers. Bound transitions per tick. |
| Timing and motion | Time scale, one-shot/layer nodes, phase synchronization, root-motion extraction and event markers. Define behavior for reverse playback, loop crossings and seek. |
| Pose modifiers | A staged two-bone IK/aim profile and the existing retargeted motion path. Apply ragdoll/physical-bone overrides in an explicit post-physics stage. |

Ordinary blend dependencies form an acyclic graph. State-machine transition
cycles are legal control flow, but never recursive same-tick pose evaluation.
Compile-time checks reject dangling clips, skeleton mismatches, invalid masks
and conflicting parameter types. Mutable parameters are data, not arbitrary
script callbacks invoked from graph evaluation.

Define event provenance and contribution thresholds so a blended footstep does
not fire once from every contributing clip. Default seek samples a pose without
replaying gameplay/audio cues; explicit event replay is separate. Native events
enter the existing host delivery queue. Keep the current `<motion-controller>`
working as a simple graph front end, with the same requested-state precedence
and crossfade behavior (**S12.1.5v2**).

The focused animation tool uses Lambda UI and the existing drawing/editor
transaction model to edit graph nodes/connections, parameters, transitions,
joint masks and blend-space points. It saves the same versioned Mark/scene
description that scripts construct. Include undo/redo, validation, a timeline
scrubber, active-state/weight overlays and a live character preview. A static
graph viewer alone does not satisfy visual authoring.

**Acceptance:** a speed parameter smoothly blends idle/walk/run; a 2D space
blends directional locomotion with documented hull behavior; a one-shot jump
interrupts and returns correctly; root motion respects a blocked character;
masked additive aim leaves the lower body unchanged; authoring edits survive
save/reopen and undo; compatible UI regeneration preserves playback.

## 7. Advanced lighting and effects

### 7.1 Shared rendering prerequisites

Keep the selected system-OpenGL backend and shared `gl_core` waist. The current
qualified macOS OpenGL 4.1 profile does not provide a compute-shader baseline;
new techniques must first have CPU or vertex/fragment-pass implementations.
Do not make Vulkan, Metal, WebGPU or hardware ray tracing an implicit dependency.

Extend the existing ordered pass chain with explicit pass inputs, attachment
formats, sample counts, resolves, lifetimes and color domains. Supply sampleable
opaque depth plus normals/roughness, initially through an optional shared
prepass rather than duplicating the forward renderer. Reuse skin/morph/custom
deformation contracts across visible, shadow and prepass geometry; unsupported
custom depth deformation must be diagnosed or explicitly excluded from an effect.

Depth reconstruction shares the camera's projection convention, including
orthographic and infinite-far cameras. Define opaque/masked versus transparent
coverage for every effect. Reuse the existing opaque scene capture where
lifetimes permit, and reject sampling an attachment currently being written.

### 7.2 Deliverable profiles

| Feature | Native proposal and bounded completion profile |
|---|---|
| SSAO | Depth/normal-based horizon or sampled hemisphere occlusion, half-resolution candidate, bilateral reconstruction, material ambient-occlusion combination and an optional history filter. Affect indirect diffuse lighting, not arbitrary final-color darkening. |
| Screen-space reflections | Depth-pyramid ray marching with thickness checks, roughness-aware filtering, confidence and bounded rays/steps. Fall back to the existing environment or an explicitly available probe for off-screen/disoccluded hits. First profile covers opaque surfaces. |
| Depth of field | Camera focus distance, aperture/blur limits and circle-of-confusion from linear depth; separate near/far filtering and foreground-edge treatment. Start without temporal accumulation; declare transparent-depth limitations. |
| Baked lightmaps | A native offline CPU baker over immutable mesh/material/light snapshots, explicit UV charts/atlas padding, direct visibility and seeded finite-bounce diffuse transport. Store lightmap UVs separately from the existing material UV0/UV1 contract. Include seam/dilation controls and versioned source/settings hashes. |
| Indirect-light probes | Bake irradiance plus visibility data for moving characters and objects; interpolate with leak/visibility rejection. Keep this separate from specular environment reflections and from baked static lightmaps. |
| Dynamic diffuse GI | Bounded irradiance-probe cache updated by budgeted native CPU BVH rays: gather direct-lit/emissive surface radiance at ray hits to estimate indirect diffuse illumination, then upload probe/visibility atlases. Invalidate changed geometry/lights, amortize updates and expose convergence/latency. No claim of instant multi-bounce lighting. |
| Volumetric fog | Camera-aligned low-resolution froxel density/extinction/scattering and shadowed direct lighting, integrated along view rays and reconstructed at full resolution. Use layered fragment passes/atlas storage on the existing backend; bounded local volumes follow global fog. |

Recommended pass dependencies: optional geometry prepass and depth hierarchy;
SSAO/SSR inputs; forward opaque lighting with the selected indirect-light
profile; reflection composition; fog integration/composition; transmission and
transparent draws under their declared fog model; DOF/bloom/tone mapping/FXAA in
their validated domains. Implementations must document their precise ordering
and exclusions. Do not force new optional passes on an unchanged basic scene.

Baked and dynamic diffuse contributions have an explicit combination policy;
do not count the same indirect term twice. A baked artifact becomes stale after
relevant source changes; report staleness rather than silently showing it as a
fresh bake. Bake acquisition/output is an explicit procedural host job using
the existing IO service; its numeric kernel reads only its copied snapshot.

Default lightmaps store indirect diffuse light only, leaving direct lighting
on the existing real-time path. An optional baked-direct mode must exclude the
same light contribution from real-time shading of its receivers. Mark static
bake participants and moving probe receivers explicitly. The first CPU transport
profile supports opaque/masked diffuse material response and emissive surfaces
with acquired texture data; arbitrary GLSL and refractive transport require an
explicit bake proxy or a diagnostic, not an invented matching CPU shader.

Temporal resources, when enabled, key on scene/camera/projection, viewport,
density and relevant content generations. Resize, camera cuts, scene edits,
seek and context recovery invalidate the appropriate histories. A deterministic
capture resets/warms a declared history sequence or uses the non-temporal profile
(**D2.11.4**); a repeated read does not accumulate extra samples.

Authoring tools expose bake coverage/staleness, UV charts, probe locations,
effect quality and actual resource cost. Quality settings bound atlas sizes,
rays, march steps, froxels and history bytes. Screen-space approximations retain
their visible limitations; renderer availability is reported through §3.3.

**Acceptance:** SSAO responds to a corner without darkening an isolated plane;
SSR reflects an on-screen object and transitions to its declared fallback;
near/far DOF retains foreground edges; a baked room exhibits diffuse color
bleeding and valid UV seams; a moving character receives probe lighting;
moving light/geometry updates bounded dynamic GI; a fog volume scatters a
shadowed light beam. Identity settings preserve existing output. Include
independent numeric/ray controls, depth/normal debug views, resize/seek/context
recovery, and resource-budget failures, not only attractive screenshots.

## 8. Ready-made particle systems

Build emitter services over existing native instancing, sprites/points and
float-target simulation. CPU and GPU profiles share one normalized description:
seed, capacity, emission rate/bursts, duration/looping, spawn shape, initial
velocity, lifetime and parameter curves. CPU processing is a real supported
profile, not a placeholder for missing graphics.

Provide point/box/sphere/cone emitters first, followed by mesh-surface emission;
local/world simulation space; gravity, drag and bounded noise; color/opacity,
size, rotation and velocity-over-life curves; texture-sheet frames; billboard
or mesh presentation. Presets include smoke, sparks, fire, rain and an explosion.
Presets are ordinary package data, editable and saved through the same schema.

Key randomness by emitter seed, spawn sequence and channel so removing one
particle does not perturb every later spawn. Curves reuse existing sampling
helpers and are compiled to bounded tables when required by the GPU. Document
CPU/GPU precision differences instead of promising identical pixels or events.

Use the existing fixed-step fragment-pass simulation for GPU particles; do not
require compute shaders or read every particle back to the CPU each tick.
CPU collision uses admitted scene queries. GPU collision first uses explicit
analytic planes/volumes; depth-buffer collision is a separate camera-dependent
visual profile and cannot generate authoritative gameplay contacts.

Add bounded ribbon trails, optional sub-emitters and collision response after
the base emitter. Bound live particles, trail segments, recursion depth and
spawns per tick. Capacity policy is authored and observable (drop-new, recycle,
or error); gameplay-relevant CPU event overflow fails explicitly. Pause,
rewind/replay, scene edits and context loss have distinct lifetime behavior.

Particles need conservative authored or derived motion bounds. Culled emitters
follow an explicit simulate/suspend policy, so camera movement cannot silently
change gameplay events. Transparent sorting/overdraw limits remain visible in
the selected profile.

The particle tool edits emitters/curves/presets with live preview, seed locking,
restart/seek, bounding-volume display and CPU/GPU/overdraw counters. Reuse the
animation tool's curve editing and editor transactions.

**Acceptance:** a seeded CPU burst replays its spawn sequence; CPU/GPU presets
share documented timing and shape statistics; a rate edit preserves compatible
live particles; pause/restart and detached-emitter cleanup work; CPU collision,
GPU analytic collision and a ribbon trail produce visible results; sustained
emission remains within authored storage/spawn limits without per-frame growth.

## 9. Game audio and capture

### 9.1 Extend the existing output service

Keep one window output device and document-owned submixes. Add Scene3D source,
listener and routing integration under `radiant/scene3d/`; extend the shared
audio provider only where MIDI, speech, media or other documents need the same
mechanism. Do not create a device/mixer for every viewport or bypass
**D7.5.4**. Existing decoded-audio paths must be connected through the shared
bus as needed; their present routing is not proof that every source already
uses the same path.

Each scene selects one listener initially, either camera-following or explicit.
Support nonpositional UI/music sources, 2D positional panning/attenuation, and
3D position/orientation, distance rolloff and source cones. First output profile
is stereo equal-power panning; HRTF/surround are later quality profiles, not
implicit promises. Spatial occlusion starts as a low-rate, bounded physics ray
query driving gain/low-pass controls; full acoustic propagation is separate.

Doppler uses source/listener velocities and an explicit speed of sound in the
scene's unit system. Teleports reset velocity estimation; apply pitch clamps
and smoothing. Different physics timesteps or presentation density cannot alter
the configured physical units.

### 9.2 Buses, effects and sample-time scheduling

Define named buses beneath the document submix: music, effects, ambience and UI
are presets. Support gain/mute/solo, meters, ordered inserts and explicit sends;
require an acyclic routing graph. Stage low/high-pass filters, parametric EQ,
delay, the existing reverb, compressor/ducking and a final limiter. Reuse the
existing reverb implementation rather than copying it into a Scene3D effect.

Compile routing off the audio callback and publish it at a buffer boundary.
Exchange fixed-capacity command/parameter buffers and owned sample data; the
callback does not allocate, block, fetch resources, touch the DOM/GC or invoke
scripts. Smooth gain/pitch changes, bound voice stealing and preserve effect
tails. Device loss and removal retire sources only after callback readers have
released them.

Map scene cue timestamps to the document sample clock with explicit pause,
time-scale and late-command behavior. Physics slow motion may alter cue rate
without changing pitch; pitch-following is opt-in. Give cues stable event IDs so
render retries do not duplicate sounds. Offline rendering uses the same graph
with virtual time and device-free PCM capture; it is not microphone input.

### 9.3 Microphone input

Capture is an explicit procedural acquisition through the existing IO/host
capability route, with OS permission and a per-document grant. A scene tag may
describe a capture binding but cannot open a microphone during pure evaluation.
The output activation ruling **D7.5.4** is not a microphone permission grant.
This new capture policy requires ratification before the capture milestone.

Use bounded native PCM rings, timestamps, resampling/channel conversion and
overrun diagnostics. Deliver copied buffers/meters at ordinary host boundaries.
Specify close on explicit stop, document teardown, revocation and device loss;
report unavailable/denied states. Apply the scoped resource contract
(**S12.4.1v3–S12.4.3**) where its cleanup implementation is available; never
invent a new implicit resource lifetime to bypass its current implementation
limits. Monitoring to speakers is off by default and, when enabled, passes
through the ordinary audible-output gate. Voice networking, speech recognition
and acoustic echo cancellation are outside this capture profile.

The focused audio tool edits source/listener positions, rolloff, buses, effects
and sends, with meters and offline audition. Device permission UI is host-owned;
saved scenes do not embed permission grants or automatically start capture.

**Acceptance:** source movement produces expected left/right balance and
distance loss; Doppler matches a known velocity case and ignores a teleport;
music/effects routing, mute/solo, ducking and reverb tails work; offline output
matches numeric mixer controls; denied/revoked capture and device loss release
resources; captured PCM arrives with bounded storage. Local/network documents
retain **D7.5.4** activation behavior for every audible game source.

## 10. Public descriptions and authoring boundary

Keep ordinary `<scene3d>` and existing physics/animation elements. Extend the
normalized schema with explicitly versioned descriptions for navigation assets
and agents, animation graphs, particle emitters, lighting/bake profiles, sound
sources/listeners and bus graphs. Preserve existing meanings and defaults;
schema upgrades are explicit and validated.

Recommended package homes are `lambda.physics` for reusable physics and
`lambda.scene3d.navigation`, `.animation`, `.particles`, `.lighting` and `.audio`
for scene descriptions/tools. Extend existing modules where those paths already
exist. These names are proposed package organization, not new shipped APIs.

| Operation category | Contract |
|---|---|
| Construct/normalize/validate | Pure `fn` over supplied data, returning descriptions or structured errors; no device, world or worker acquisition. |
| Existing batch physics | Preserve the existing pure, explicitly bounded simulation/query contract over supplied initial state. |
| Live play/move/target/seek/query | `pn` through the existing `radiant-dom` host operations, reading or commanding the scene-owned services. |
| Bake/import/output/capture | Explicit host job/resource operations; bytes flow through existing IO providers, with progress/cancellation and located errors. |
| Observe | Copied snapshots/events with scene/object generations and tick/sample timestamps; no solver pointers or script-visible GL names. |

Do not expose a pure function that reads a live world's ambient state. A future
pure native navigation helper must live in an appropriate lower provider; it
cannot make `lambda-rt` call into `radiant.a` (**D7.1.3 / D7.3.4**).

The focused tools reuse the same validators, schema, save format and native
preview as scripted descriptions. Undo edits authored intent; it does not
pretend to reverse a running physical world. Preview reset/replay after such
an edit is explicit. Failed edits retain the last working preview.

## 11. Delivery sequence and acceptance boundaries

The sequence favors playable behavior before expensive rendering/deformation
work. Finish and qualify a bounded milestone before expanding its conformance
tail. Dependencies determine order; estimates and hardware budgets are set in
the implementation plan, not invented as calendar promises here.

| Stage | Deliverable and dependencies | Milestone closes when |
|---|---|---|
| M0: relocate | Move existing Scene3D-owned native files into `radiant/scene3d/`; update includes/build/lint ownership. No feature changes. | Existing scene/package/WebGL/shared-animation consumers still build and their required gates retain baseline behavior. |
| M1: runtime coordination | Scene service ownership, fixed-tick phases, generation reconciliation, capabilities and counters. Builds on the current scheduler. | Pause/step/seek, command/event order, UI regeneration and context recovery preserve the declared state owners. |
| M2: collision completion | Capsule/convex queries, fixed mesh/heightfield environments and admitted CCD pairs. | Primitive/mesh contact, thin-wall projectile and sweep/oracle cases pass with explicit pair coverage and exhaustion errors. |
| M3: articulated movement | Joints/motors, 3D character controller and ragdoll transitions; requires M2. | Stair/slope/platform controls, joint chains, motor limits and fall/recovery have numeric and interactive acceptance. |
| M4: navigation | A*, 2D polygons/3D bake, corridors/links, agents and bounded avoidance; uses M2–M3 for 3D movement. | Maze, clearance, door, crossing agents and narrow-passage cases work, including stale rebake rejection. |
| M5: animation tools | Blend trees/spaces/state machines and editable graph preview; reuses M3 movement/poses. | Locomotion/jump/masked-layer graphs run, are visually edited, saved, reopened and inspected without restarting compatible playback. |
| M6: particle tools | CPU/GPU emitters, shared curves, collision profiles and bounded trails/sub-emitters. | Five editable presets, seed/replay controls, visible collision/trails and resource limits work in a native scene. |
| M7: game audio | Source/listener spatialization, Doppler, buses/inserts/sends, offline output and explicit microphone capture. | Spatial/routing/capture cases and focused authoring pass, including output activation, permission denial and device loss. |
| M8: genuine 2D gameplay | 2D bodies/shapes/joints/CCD, controller and navigation integration using the shared service model. | An orthographic/SVG-integrated platform scene passes collision, moving-platform, joint, path and pixel-density independence cases. |
| M9: depth effects | Shared depth/normal contracts, SSAO, SSR and DOF; preserves existing forward/PBR output. | Numeric/visual controls, effect-off identity, fallback behavior and history/resize/resource cases pass. |
| M10: baked indirect light | UV chart/atlas preparation, finite-bounce lightmaps, dynamic-object probes and bake tools. | A room's bounce/seam controls and moving-character probe lighting pass; stale/cancelled bakes cannot publish. |
| M11: dynamic lighting | Budgeted probe GI and volumetric fog on the existing backend. | Moving light/geometry and shadowed fog cases converge within stated quality/work budgets and recover from cuts/seek/context loss. |
| M12: deformable completion | Surface soft bodies, self/external collision and attachment coupling; integrated full-scope scenes. | Cloth/deformable fixtures meet their published bounds; combined 2D and 3D gameplay scenes complete the full verification matrix. |

**First playable core:** M0–M7 gives a 3D character, joints/CCD/ragdolls,
navigation, editable animation, particles and sound with the existing renderer.
**Full requested scope:** M8–M12 adds genuine 2D dynamics, all listed lighting
profiles and bounded soft bodies. None is relabeled complete merely because
another stage demonstrates something visually similar.

## 12. Qualification and performance budget

Use independent evidence at the owning layer, then one integrated consumer.
Register meaningful fixtures in the existing test map; do not create a parallel
runner or change tolerances to hide unsupported behavior (**D7.3.5**).

| Area | Focused evidence |
|---|---|
| Physics | Analytical motion/impulse cases, independent distance/time-of-impact controls, conservation/error envelopes, contact/joint drift and finite solver budgets. |
| Navigation | Dijkstra/A* comparisons, geometric clearance and corridor checks, impossible/partial paths, stale generations and crowd/controller interaction. |
| Animation | Fixed-time pose/weight oracles, quaternion/loop boundaries, masks, transition interruption, event deduplication and saved visual-editor edits. |
| Lighting | Small independent ray/bake controls, effect-off identity, depth/normal correctness, selected image references and temporal invalidation. |
| Particles | Spawn sequence/count/curve controls, bounded storage, CPU/GPU statistical comparisons and native visible effects. |
| Audio | Offline PCM impulses/tones, panning/attenuation/Doppler math, routing/effect-tail controls, callback lifetime and simulated plus real device/capture paths. |
| Integration | A character arena with navigation, physical props, graph locomotion, particles/spatial cues and advanced lighting; a separate genuine 2D scene; editing, pause/seek, save/reopen and teardown. |

During implementation, run focused native/package/View cases. At milestone
boundaries run `make test-scene3d` and the required Radiant baseline; changes to
packages, runtime, shared physics or DOM additionally require the Lambda and
relevant native/module-boundary gates in [the test map](../../test/README.md).
Changes shared with SVG/WebGL/audio retain those consumers' controls. Every
new golden-driven `.ls` gets its `.txt` companion. Use scoped ASan/forced-GC
checks for lifetime and callback changes.

Before each implementation milestone, set workload, reference hardware, viewport,
quality, p50/p95 timing and peak-memory targets from a measured release baseline.
Measure simulation, graph evaluation, navigation, particles, rendering/readback,
page composition, audio callback duration and end-to-end input-to-presentation
separately. A small numerical kernel or cached frame is not evidence of game
frame-rate parity.

Account for CPU/GPU bytes, solver contacts/rows, nav nodes/tiles, pose scratch,
particle/trail storage, effect histories, voices/PCM and bake working sets.
Bound all queues and per-tick/per-buffer work. Steady-state replays must show no
unbounded resource growth; report missed deadlines, lag and dropped noncritical
work. Audio buffers must meet their device deadline under representative scene
load, not only in a mixer microbenchmark.

Use release builds only for performance. Follow
[Developer Guide §7](../../doc/dev/Developer_Guide.md#7-worktrees-and-agent-gotchas)
before worktree testing; never run `make release` in a worktree. Capture all
temporary artifacts under `./temp/`. This documentation-only proposal needs
document/link checks, not engine builds or regression suites.

## 13. KIV: gameplay algorithms in Lambda Script

**Direction (USER, 2026-10-28):** keep the following four gameplay scopes in
view under Scene3D, with Lambda Script preferred for their implementation.
They are future package/tool work, not additional native C++ services required
to complete this proposal. Their detailed APIs and delivery milestones remain
to be designed when a concrete game needs them.

| KIV scope | Candidate Lambda Script responsibilities | Native Scene3D support to reuse |
|---|---|---|
| Perception | Visibility rules, view cones/ranges, hearing from game-world sound events, nearby-entity filtering, attention, and remembered observations with confidence/expiry. | Bounded spatial/overlap queries, ray casts for occlusion, copied entity poses/IDs and timestamped scene events. |
| Decision-making | General gameplay state machines, behavior trees, blackboards, utility scoring and goal/action planning; reusable conditions, actions and interruption policies. | Existing scene snapshots, navigation requests, character commands and animation/audio operations. Policy and action selection stay in script. |
| Coordination | Task assignment, formations, slot/resource reservations, priorities, shared goals, yielding and conflict resolution between agents. | Batched agent/body state, navigation/path results, movement commands and completion events. |
| Inspection | Views of active goals and selected actions, decision/score explanations, behavior/state transitions, bounded decision traces and replay controls. | Existing DevTools/scene inspection, copied snapshots, tick/event identities and the established command/event boundary. |

### 13.1 Package and state ownership

Candidate source home: `lmd/package/scene3d/gameplay/`, imported through the
existing `lambda.scene3d.*` namespace (**D7.2.4v4**). This is a proposed package
location, not a shipped API. Reuse existing package and editor helpers before
introducing another blackboard, history store, graph editor or reservation
implementation.

Keep policy as pure transformations over explicit state, observations, tick/time
and seeded random input. A decision step returns updated gameplay state,
action intents and optional trace records. The procedural handler gathers live
observations and applies the resulting intents through `radiant-dom`
(**S12.1.1v3 / S12.1.3 / D7.5.3v2**). Pure perception/planning functions never
read an ambient live world or call procedural query functions themselves.

Gameplay memory, goals, blackboards and reservations belong to the Lambda
application/package state. Their transitions produce commands for native
services; they do not also own physical poses or animation playheads
(**S12.1.5v2**). General gameplay state machines in this section select goals
and actions; §6's native animation state machines select and blend poses.
An action can request an animation state without merging the two state owners.

### 13.2 Boundaries and useful future profiles

Perception should separate current observations from remembered facts, with
explicit age, uncertainty and invalid/deleted entity handling. Hearing can
consume authored sound stimuli with position, intensity and tick; visibility
uses native geometry queries followed by script-level game rules. Process
observers in bounded batches and share query results where their inputs agree.

Decision-making should start with a small reusable state-machine or behavior-tree
profile before adding utility selection and planning. Declare task statuses,
cancellation and action completion. Bound tree visits, planner expansions and
per-agent work; carrying unfinished work across ticks is explicit script state.
No arbitrary gameplay callback executes inside a native physics/navigation step.

Coordination should use stable entity/task IDs and explicit reservation leases,
expiry and release on cancellation/removal. Resolve competing requests in a
declared order and retain assignment state across ticks. Formations generate
desired slots; native navigation/character services determine achievable motion.

Inspection should explain the policy's inputs, considered choices, selected
intent and observed outcome. Save bounded traces with tick ordering, relevant
inputs, seed/state and policy/schema versions. Decision replay can reproduce
choices from recorded observations; replaying the complete physical world still
requires the native reset/replay or compatible-checkpoint contract in §3.2.
Build inspection views in Lambda UI over the same records used by the policy.

If profiling later identifies a shared hot kernel, extract that kernel under
the existing provider/ownership rules (**D7.1.3 / D7.5.3v2**) while retaining the
gameplay policy in script. Native support remains spatial queries, simulation,
presentation and explicit host operations; avoid introducing a parallel C++
gameplay framework in `radiant/scene3d/` for these KIV scopes.

When promoted to active work, define finite acceptance using one small scenario:
an agent observes and remembers a target, selects an action, coordinates a
reserved task/slot with another agent, and exposes a replayable decision trace.
Qualification and implementation records are added then; these examples create
no new completion gate for the current native gaming milestones.

## Appendix A. Source relocation and implementation map

The user-selected directory is `radiant/scene3d/`. Move the following current
Scene3D-owned files there, initially preserving basenames, symbols and behavior:

```text
radiant/scene3d/
    scene3d.cpp
    scene3d.hpp
    scene3d_math.hpp
    scene3d_source.hpp
    scene3d_animation.cpp
    scene3d_animation.hpp
    scene3d_physics.cpp
    scene3d_physics.hpp
    scene3d_physical_shaders.hpp
    scene3d_post_shaders.hpp
    scene3d_drawable_shaders.hpp
    scene3d_outdoor_shaders.hpp
```

As features land, split cohesive implementation units for runtime/commands,
projection/render passes, navigation, animation graphs, particles, lighting/bakes
and scene audio inside this directory. Extract the shared shape before adding a
third near-identical algorithm/pass. Keep a coherent public Scene3D facade and
consolidated subsystem declarations; do not introduce one header per source.

| Owner | Implementation responsibility |
|---|---|
| `radiant/scene3d/` | All Scene3D-owned native integration, controllers, navigation services, animation graphs, render passes, particles, scene audio, native bake kernels and their shader sources. |
| `lib/physics/` | Existing reusable C solver and its 2D/3D, constraint, CCD and deformable extensions; remains below both runtime and Radiant. |
| `lambda/core/physics_scene.*`, runtime physics adapter | Existing shared description conversion and pure batch APIs; extend in place rather than making them depend upward on Scene3D. |
| `radiant/gl_core.*` | Shared native resource/draw/provider mechanisms used by Scene3D and WebGL. Remains outside the Scene3D directory. |
| `radiant/animation_mixer.*`, `animation_value.*`, SVG/CSS animation and frame clock | Shared sampling/mixing/scheduling infrastructure; promote needed helpers here rather than cloning them inside Scene3D. |
| `radiant/audio_bus.*`, `audio_reverb.*`, audio/device providers | Shared window output, document submix and device/DSP mechanisms. Scene routing/spatial integration goes in the new directory. |
| `lambda/input/`, `lambda/format/`, `lambda/io/`, lower `lib` asset helpers | Existing codecs/acquisition/formatters and context-free providers retain their ownership. |
| `lambda/module/radiant/` | Existing public host adapter; includes the relocated facade without new runtime-to-Radiant imports. |
| `lmd/package/scene3d/`, `lmd/package/physics/` and existing editor/UI packages | Pure descriptions, presets, validation and focused authoring UI; no duplicate native state. |
| `test/lambda/scene3d/`, existing physics tests, `test/scene3d/` and View/native tests | Golden, numerical, visual, lifecycle and authoring acceptance through existing registrations. |

The folder requirement applies to Scene3D-owned code. Moving shared providers
or codecs upward merely to gather all related filenames would break
**D7.1.3 / D7.5.3v2** and remove headless/shared consumers. Newly discovered
cross-layer primitives are promoted to the correct existing lower provider;
their extraction is reviewed explicitly, never hidden behind a forwarding shim.

M0 migration procedure:

1. Inventory includes, explicit build/test source lists, lint paths and document
   links. Move tracked files as renames; include the current outdoor shader
   addition in the inventory. Do not overwrite unrelated working-tree changes.
2. Update callers to `scene3d/scene3d.hpp` or the appropriate consolidated seam;
   fix relative includes inside moved sources. Remove obsolete old-path includes
   in the same change; leave no duplicate implementations or forwarding files.
3. Update `build_lambda_config.json`: its current `radiant/*.cpp` source pattern
   does not discover nested sources. Add the explicit Scene3D directory pattern
   and update any configured per-target references. Generate build files with
   `make`; never edit generated Lua.
4. Reconcile the Radiant coherent-header lint's relocation accounting and allowed
   subsystem seams with no increase in the header-count budget. A renamed
   header must not be treated as permission for new per-file header growth;
   retain reporting of existing debt rather than disabling the rule.
5. Update source links and the focused architecture/test inventory; run the
   migration gates before adding gaming behavior. Then split implementation
   units incrementally as their owning feature milestones require.

Use C++17/C+, project `Str`/`ArrayList`/`HashMap` storage and existing allocation
owners. Layout/presentation coordinates remain `float`; physical solver
precision follows its C-core contract. Promote shared helpers instead of copying
statics. Use project logging with distinct prefixes. No vendor modifications,
manual parser changes, `log.conf` edits or debug-build performance claims.

## Appendix B. Decisions to ratify by milestone

The six areas and source-directory relocation are requested scope. The following
recommendations make that scope concrete; settle each before its dependent
implementation, without treating this proposal as formal-spec ratification.

| Decision | Recommendation / dependency |
|---|---|
| Physics completion profile | Retain the selected native C engine; stage the explicit collision/CCD pair matrix, joint types, XPBD surface model and same-build replay guarantees in M2–M3/M8/M12. |
| Navigation assets | Versioned source-selected polygons/tiles with offline baking first; CPU avoidance and controller-owned actual motion in M4. |
| Graph persistence | Serializable Mark descriptions over the existing mixer, authored with existing editor transactions in M5. |
| Renderer profile | Preserve native OpenGL first; CPU/fragment-pass techniques and explicit required/auto capabilities for M9–M11. |
| Lighting quality | Finite-bounce diffuse lightmaps, probe-lit dynamic objects and budgeted probe-cache dynamic GI; no unbounded ray tracing claim. |
| Particle authority | CPU gameplay events; GPU analytic/depth collision explicitly visual where it cannot provide authoritative contacts in M6. |
| Capture and output | Preserve D7.5.4 output gating; separately ratify document/OS capture permission, buffer access and revocation before M7 capture. |
| Work/resource limits | Set measured workload budgets and supported platform matrices at each milestone; preserve explicit errors and reproducible smaller profiles. |

Principal risks are solver stability and collision coverage, navigation deadlocks,
state ownership between animation/physics, depth/transparent effect interaction,
CPU GI/bake cost on the existing backend, and audio callback/device lifetime.
The staged acceptance cases target these shared risks before visual breadth.
Implementation gaps and failures belong in the existing central issue ledger
and delivery records, not a new competing issue list in this design.

## Appendix C. External design references

Reviewed 2026-10-28. These identify useful feature families and workflows;
Godot's APIs, node model and implementations are not dependencies or a claim
of Lambda compatibility. Pin any comparison executable/assets used by a later
implementation gate. Keep third-party code unmodified and oracles separate
from production providers.

- [Godot physics introduction](https://docs.godotengine.org/en/stable/tutorials/physics/physics_introduction.html): body roles and character-controlled movement.
- [Godot navigation overview](https://docs.godotengine.org/en/stable/tutorials/navigation/navigation_introduction_2d.html): graph/grid and navigation-mesh services, agents and avoidance.
- [Godot AnimationTree](https://docs.godotengine.org/en/stable/tutorials/animation/animation_tree.html): blend trees/spaces and state-machine authoring.
- [Godot 3D rendering features](https://docs.godotengine.org/en/stable/about/list_of_features.html#3d-graphics): lighting/effect families and renderer-dependent availability.
- [Godot particle systems](https://docs.godotengine.org/en/stable/tutorials/3d/particles/index.html): emitters, CPU/GPU processing and lifetime controls.
- [Godot audio buses](https://docs.godotengine.org/en/stable/tutorials/audio/audio_buses.html): routing, inserts and audio authoring.
- [Godot audio feature inventory](https://docs.godotengine.org/en/stable/about/list_of_features.html#audio): positional playback, Doppler and microphone input.
