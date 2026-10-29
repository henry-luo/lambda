# Radiant Scene3D gaming implementation

**Started:** 2026-10-28. **Status:** M0 relocated with focused passing evidence;
M1 coordination and M2 discrete geometry/resources, exact queries/casts and the
first single-sphere CCD profile are implemented, with focused passing evidence
and recorded qualification limits. M3 joints, sampled-world capsule movement,
physical bone bindings and live ragdoll handoff/recovery/replay are implemented
with focused passing evidence. Broader M2 CCD, remaining M3/M4 profiles and
M5–M12 native mechanisms and the deferred M13 Lambda packages are outstanding.
Native graph/grid shortest paths, Scene3D asset ownership and point-agent
surface corridors/grounded waypoints, clearance cells/polygon regions and
source-bake jobs are implemented. Explicit links are implemented with focused passing evidence and recorded
aggregate limits. Bounded baked-surface capsule agents now include native ramp
and step following; final qualification is recorded below. Dirty tiles, broader
profiles and avoidance remain open. Limits below remain open;
the full native gaming roadmap is not delivered.

Design: [Radiant_Design_Scene3D.md](../radiant/Radiant_Design_Scene3D.md).
The user authorized implementation of the native M0–M12 scope. On 2026-10-29,
the user removed all editor/authoring UI from the proposal. Delivery now covers
runtime features, script APIs, validation/serialization and inspection data;
visual editors, preview panels, scrubbers, overlays and dedicated authoring
tools are excluded. Earlier editor references below are historical and no
longer define outstanding work. The four Lambda Script gameplay algorithm
scopes in design §13 remain KIV, with inspection records rather than new UI.

The user then separated delivery into core C/C++ services (M0–M12) and one
later Lambda Script package phase (M13; design §11.2). Logical animation graphs
and blend weights, navigation/movement policy, particle presets/composition,
audio orchestration, lighting/bake helpers and deformable factories now belong
to M13. Earlier phases provide native kernels, safety checks, snapshots and
minimal host bindings/fixtures; no interim C++ implementation of that policy
is requested. Existing packages and implemented behavior remain supported.
This roadmap supersedes earlier phase allocations in the historical entries.

Preserve **S12.1.1v3 / S12.1.5v2**, **D2.11.4**, **D4.5.1v4–D4.5.3v2**,
**D5.3.3**, **D7.1.1–D7.1.3**, **D7.3.4–D7.3.5** and **D7.5.3v2–D7.5.4**
in the [formal semantics](../../doc/Lambda_Formal_Semantics.md) and
[formal design](../../doc/Lambda_Formal_Design.md). This implementation record
does not revise those contracts.

## Delivery sequence

| Stage | Outcome | Status |
|---|---|---|
| M0 | Existing Scene3D sources live under `radiant/scene3d/`, with corrected consumers/build discovery and unchanged behavior. | Implemented; aggregate qualification limited |
| M1 | One native scene coordinator, fixed tick phases, reconciliation, capabilities/counters and the later package command/snapshot boundary. | Existing coordinator implemented; generation/target-tick batch extensions pending; aggregate qualification limited |
| M2 | Capsule/convex/mesh queries and bounded CCD. | Discrete geometry/resources, exact overlaps, translation casts, all-hit queries and single-sphere CCD implemented; aggregate qualification limited; broader CCD remains open |
| M3 | Joints/motors, character movement and ragdoll integration. | Joints, sampled-world capsule movement, physical bone bindings and live ragdoll modes/replay implemented; focused checks pass, aggregate qualification limited; animated batch sampling, pushing/future-motion and wider CCD coupling pending |
| M4 | Navigation assets, A*, agent movement and avoidance. | Graph/grid A*, Scene3D assets, point-agent surfaces, clearance bakes/source jobs, explicit links and bounded capsule agents with static ramp/step following implemented; qualification below; dirty tiles, broader profiles and avoidance pending |
| M5 | Native clip/layer sampling/blending, masks, timing/root motion, crossfade commands and snapshots. | Pending; logical graphs/blend-space weights moved to M13 |
| M6 | Native CPU/GPU emitter/update/collision/curve kernels, trails and bounded spawn queues. | Pending; presets/effect/sub-emitter rules moved to M13 |
| M7 | Native spatial audio, bus/DSP execution, sample scheduling and microphone acquisition. | Pending; music/voice/occlusion policy and factories moved to M13 |
| M8 | Genuine 2D physics/controller/navigation integration. | Pending |
| M9 | Shared depth/normal passes, SSAO, SSR and DOF. | Pending |
| M10 | Lightmap/probe baking and explicit bake jobs. | Pending |
| M11 | Budgeted dynamic indirect lighting and volumetric fog. | Pending |
| M12 | Surface soft-body kernels and combined native-service qualification. | Pending; topology/pin/material factories moved to M13 |
| M13 | Dedicated Lambda packages for graph control, policy, factories/presets, orchestration, serialization and deterministic decision replay. | Deferred until M0–M12 native acceptance; active requested scope, not KIV |

## M0: source relocation

**Outcome:** preserve current native rendering, scene physics, shared animation,
WebGL and export behavior while giving Scene3D one owned source directory.

**Owner and cause:** the existing Radiant build source pattern only discovers
top-level `.cpp` files. Moving the sources without updating discovery, relative
includes and explicit consumers would omit their definitions or reuse orphaned
archive members. Developer Guide §7.3 describes the archive hazard.

**Scope:** move the twelve files in design Appendix A, correct source/header
references and source links, and keep reusable graphics, animation, physics and
audio providers in their existing layers. Source behavior changes wait for M1.

**Acceptance:** the native host and Scene3D test targets build from the moved
sources; current numerical/render/ownership/lifecycle cases pass; old source
paths have no live consumers; source/header counts do not grow; the applicable
Radiant aggregate and module-boundary gate run without weakened assertions.

**Verification budget:** capture the existing focused native result, perform
the move/build, run Scene3D/shared-animation/WebGL consumers, then qualify the
Radiant milestone aggregate once. Check includes, generated build discovery,
header relocation accounting and diff whitespace. Additional checks require a
new failure or code change that warrants them. Artifacts live under
`temp/scene3d-gaming/`.

**Pre-existing limit:** before edits, the coherent-header lint reports 70
Radiant headers against its 24-header cap, with no newly introduced violations.
Retain that failure and cap; relocating existing headers must not conceal the
debt or add new header exceptions.

### M0 evidence

- All twelve files moved; a content comparison against HEAD verifies that the
  relocation itself changed only includes. Shared providers stayed in place.
- Updated both monolithic `source_dirs` and static-library `source_patterns` in
  `build_lambda_config.json`. Purged orphaned objects, dependency files and the
  old Scene3D-bearing archive before rebuilding. No generated Lua was edited.
- Before relocation, native Scene3D passes **81/81**. After relocation,
  `make test-scene3d` passes **104/104** integration/contract cases and **81/81**
  native cases. Build, relocation-content, whitespace and four header-ratchet
  regression checks pass.
- The broader Radiant run returned nonzero. Its UI run records
  `diff_repository` at 12/14 assertions; a later separate result for that fixture
  records 14/14. Shared layout reports changed during inspection, so the run's
  fallback 0/3294 layout summary is not a usable layout qualification receipt.
  The later layout report records 3152 full passes, 136 partials and six
  failures. Neither result is a green aggregate. Use an isolated output root
  for the next qualification, rather than interpreting replaced shared files.
- The header ratchet still reports **70/24**, with no newly introduced header
  violations. Its cap and existing-debt reporting were preserved. The checker
  now enumerates individual untracked headers and recognizes unambiguous
  same-name moves of deleted tracked headers; copies/new headers still fail.

Artifacts: `temp/scene3d-gaming/m0-before.json`, `m0-scene3d.log`,
`m0-radiant-baseline.log`, `m0-relocation-check.txt` and `m0-build.log`.

## M1: scene coordinator

**Outcome:** one document-owned coordinator and scheduler entry per scene,
reusing existing physics, mixer, event queue, native ownership and precise roots.

**Owner and cause:** physics and animation previously registered independent
clock drivers. Frame sampling and service lifetime had no shared scene boundary.
The new `scene3d_runtime.cpp` owns phase order, fixed-tick pacing, pause/step,
time scale, debt/error counters and scheduler lifetime. The renderer retains
that runtime across camera edits, compatible regeneration and context recovery.

**Compatibility:** active physics uses bounded fixed ticks. Animation-only
views sample the selected virtual frame directly (**D2.11.4**); their fractional
sample is accounted for when physics joins at the next fixed boundary. The
existing `NativeClipUsesDocumentSchedulerAndParksAfterClampedFinish` fixture
remains unchanged. A paused or idle service does not leave a second driver.

**Public surface:** `lambda.scene3d.runtime` supplies procedural `play`, `pause`,
`step`, `seek`, `reset`, `snapshot`, `time_scale` and `capabilities` through the
existing `radiant-dom` module (**S12.1.1v3 / D7.5.3v2**). Seek/reset rebuild
physics from authored input, suppress replay cues and stop at a fixed-tick
boundary within 100000 ticks; they do not restore live command history.
Snapshots and capability reports do not advance state. Unsupported later-stage
features report false instead of acquiring placeholder implementations.

**Rooting:** commands remain rooted while service preparation or event creation
can allocate (**D5.3.3**). State results are built with `RootedValueBuilder` and
returned directly; its VMaps are not fed into an authored-Map command reader.

**Acceptance:** exact shared animation/body motion; one driver; bounded catch-up
with retained debt; no extra tick on repeated reads/timestamps; pause/manual
step/time scale; fractional visual-to-physics handoff; authored-state seek/reset;
camera/context preservation; detached-scene cleanup; live Lambda/JS host calls;
and collecting/poisoning GC at the native boundary.

**Verification budget:** focused native runtime/physics/animation/GC checks,
then the complete existing Scene3D/View controls. Follow with required core and
Radiant qualification under isolated artifact roots and the module-boundary
check. Rebuild before consumers; never replace a host while a gate uses it.

**Focused evidence:** the complete Scene3D group passes **106/106** View/contract
cases and **84/84** native cases. The runtime command fixture also passes all
**22 assertions** with collection forced on every allocation and freed memory
poisoned. The unchanged legacy timing fixture passes. A subsequent live
`max-substeps` reconciliation fix is covered by the retained-debt native case
and the M1 aggregate below.

The first core baseline passes **6272/6273**. Its one failure is the Greetings
popup undo/redo fixture still calling removed model-local history helpers. The
application and the fixture's earlier checks use the shared document package.
Updating those three calls to the shared API restores the existing golden;
the focused test passes. The fresh `make test-lambda-baseline` passes
**6273/6273** runtime checks and **8385/8385** including input checks. Its native
script runner passes **1894/1894**. Artifacts are isolated under
`temp/scene3d-gaming/m1-core-results-final/`; the gate log is
`temp/scene3d-gaming/m1-core-gate-final.log`.

The module-boundary gate builds its validation DSOs, then rejects six existing
deferred imports in DOM selection, repaint and media-source integration. Their
call sites precede this change; no allowlist is expanded. The gate remains
nonzero (**140 imports / 165 baseline**, including the six unlisted symbols).
See `temp/scene3d-gaming/m1-module-boundary.log`. Required lint, whitespace and
the four header-ratchet regression checks pass; the pre-existing header-count
failure remains as recorded under M0.

The isolated Radiant aggregate finishes with **6476 passes, 140 partials, six
failures and two skips**. Native Scene3D passes **84/84**, UI automation
**743/743**, and View **246 passed / one skipped**. The six layout failures
repeat the earlier text/inline/sizing result: **3152 full passes, 136 partials,
six failures**. No Scene3D failure is reported. This is a qualification limit,
not a green Radiant baseline. Artifacts:
`temp/scene3d-gaming/m1-radiant-results/` and `m1-radiant-gate.log`.

## M2 first delivery: capsule collision profile

**Outcome:** authored capsules participate in the existing native solver and
queries, providing the collision primitive needed by the later character
controller. This is a bounded first delivery within M2, not completion of the
convex, mesh/heightfield and CCD profiles.

**Owner and scope:** extend `lib/physics/` geometry, mass properties and narrow
phase, then admit the descriptor through the existing shared scene compiler.
Keep both batch Lambda physics and live Scene3D on that provider (**D7.3.4**).
Use local-Y capsules with radius and a cylindrical `height` (zero admits a
sphere); rotations and compound offsets retain their existing meanings.

**Acceptance:** capsule mass/inertia and offset impulses; vertical and horizontal
floor contacts; sphere/capsule and rotated capsule pairs; exact point/ray tests
including end caps and inside starts; sensor/filter behavior; invalid dimensions
rejected atomically; live descriptor edits preserve simulated body state.

**Verification budget:** focused C solver regressions, batch-script goldens and
one live Scene3D case, followed by the required aggregate gates after this
delivery stabilizes. General convex recovery, environment BVHs, exact swept
queries and CCD remain subsequent M2 work; none is reported available early.

**Implemented:** `PHYSICS_CAPSULE` shares the native round-shape geometry with
spheres. Segment/segment and piecewise-quadratic segment/box distance feed the
existing contacts and sensors; intersecting box segments use separating-face
normals. Horizontal capsule/box support retains endpoint contacts. Cylinder
and hemisphere masses/inertias combine in the existing compound-body frame.
Point/ray queries test the cylinder and hemispheres in collider-local space.
The shared scene compiler admits `height`, including metadata authoring.
Existing sphere/box mass arithmetic is retained. Runtime capabilities add
`rigid_capsule:true`, with CCD/navigation/graphs still false.

**Focused evidence:** **24/24** native physics tests, **85/85** native Scene3D
tests and **106/106** Scene3D View/contract tests pass. The new batch capsule
golden returns `[]` on interpreter and JIT, also with forced collection and
freed-memory poisoning. The authored-DOM case verifies height edits preserve
tick, body pose/velocity and the scene runtime. Integer-cast lint, whitespace
and header-ratchet regression checks pass. A final mass-arithmetic preservation
edit is included in the following aggregate qualification; focused receipts
precede that edit. The final core/Radiant receipts below use M2 artifact roots
because the shared physics descriptor/provider changed.

**Final core receipt:** `make test-lambda-baseline` passes **6274/6274** runtime
checks and **8386/8386** including input, with **1895/1895** in the native script
runner. The new capsule golden is included. The final native physics build
passes **24/24** after the arithmetic preservation edit. The live runtime
fixture passes **23 events / 22 assertions** with forced/poisoned GC on the
final host. See `m2-core-gate.log`, `m2-core-results/`, `m2-physics-final.json`
and `m2-runtime-forced-gc.json` under `temp/scene3d-gaming/`.

**Final Radiant receipt:** the required aggregate exits nonzero with **6270
passes, 136 partials, seven failures and one skip**. Six failures reproduce the
same layout result (**3152 full passes / 136 partials / six failures**). The
remaining failure is a render-adapter idle timeout after its log prints the
passing baseline verdict. Native Scene3D passes **85/85**, UI automation
**743/743**, and View **246 passed / one skipped**.

The render adapter then passes both a direct isolated run and an isolated
selection through the original shared harness: **207 passes, four expected
partials, one skip, zero failures**. Its stall is not reproduced and the cause
is unconfirmed; no timeout, assertion, baseline or harness code is changed.
Keep the failed full-run receipt under `m2-radiant-results/`, with separate
`m2-render-isolated.json` and `m2-render-harness-results/` receipts. Neither
those reruns nor the standing header/module-boundary debt make the aggregate
green. No Scene3D or physics regression is observed in the selected checks.

The capsule delivery is complete within its stated discrete-contact/point/ray
profile. Continue M2 with general convex collision, environment meshes and
heightfields, exact overlaps/casts and bounded CCD; M3–M12 and the KIV boundary
remain as listed above.

## M2 next delivery: convex hulls and closest-point queries

**Outcome:** admit bounded 3D convex point clouds for static, dynamic and
kinematic bodies through the same C provider and Mark/DOM compiler
(**D7.1.1–D7.1.3 / D7.3.4**). Preserve pure batch calls and procedural live
ownership (**S12.1.1v3 / S12.1.5v2**).

**Scope:** cook a closed convex hull from 4–64 finite local vertices, with
coplanar face merging, exact uniform-density volume/centre/full inertia,
polytope contact manifolds, primitive/hull contacts, point/ray tests and bounded
GJK closest-point queries between named colliders. World creation and compatible
configuration edits copy geometry; rejection keeps existing geometry and simulated state.
Inline authored vertices are admitted first; referenced resources, concave
environments, shape casts and CCD remain later M2 work.

**Acceptance:** tetrahedron mass and off-diagonal inertia; a point-cloud cube
matches analytic box physics; rotated hull/box/hull contacts and stable floor
rest; sphere/capsule versus hulls; separation versus AABB false positives;
closest-point witnesses versus analytic primitive oracles; duplicate/interior
points and invalid lower-dimensional clouds; source-buffer ownership and
atomic geometry edits; live DOM edits retain world/runtime and repeated
reads do not recook unchanged geometry. Explicit errors cover quotas,
degeneracy and exhausted distance iteration budgets.

**Verification budget:** focused native geometry/solver tests, interpreter/JIT
goldens and a live Scene3D regression, with forced/poisoned GC at the authoring
boundary. Then required core and Radiant aggregates once, using isolated
`temp/scene3d-convex/` artifacts. Preserve failing receipts and isolate only
failed components; do not weaken existing gates or unrelated baselines.

Background references: [Oxford's GJK research page](https://www.cs.ox.ac.uk/stephen.cameron/distances/)
and [Mirtich's polyhedral mass-properties work](https://people.eecs.berkeley.edu/~jfc/mirtich/massProps.html).
The implementation is Lambda-owned; no third-party source is copied or patched.

**Implemented:** `physics_geometry.c/.h` owns cooking, polygon faces/edges,
polyhedral integrals, support mappings, SAT/clipped manifolds, segment/triangle
witnesses and bounded GJK. Analytic primitive fast paths remain in the shared
solver. Its collider arrays stage freshly owned hulls before replacing old
geometry, and rollback restores body state and prior hull ownership. Shared
clipping/segment helpers were promoted, not copied into a second provider.
The scene compiler admits nested/flat arrays and numeric text, binds pointers
after its point buffer has finished growing, and compares content rather than
pointer addresses during reconciliation. Rejected compatible edits have a
distinct refresh result, preserving the old live world instead of destroying
it through the structural-replacement path (**S12.1.5v2**).

**Focused receipts:** all **36** native physics, **86** native Scene3D and
**107** View/contract cases pass. The new live case passes all **17 assertions**
under forced/poisoned GC. The convex batch golden passes interpreter and JIT
with the same GC stress. Integer-cast lint, diff whitespace and the four header
ratchet regressions pass. The boundary gate builds its DSOs and retains exactly
the six previously recorded deferred imports (**140 / 165**); no new allowlist
is added.

**Sanitizer coverage:** all **36** native physics cases pass with the solver,
geometry and test sources instrumented by AddressSanitizer; object symbols
confirm instrumented memory accesses. The ASan host also passes the batch
golden on both tiers with forced/poisoned GC after linking those native objects.
Native test instrumentation uses `debug_asan_native` with explicit
`CFLAGS/CXXFLAGS/LDFLAGS=-fsanitize=address`; normal test executables are
restored afterward. Leak and container-overflow checks are disabled; this is
address/lifetime coverage, not a leak qualification. Live async ASan aborts in
`fiber_stack_prime` and reproduces in the existing non-convex runtime fixture;
see [LR12-43](../Lambda_Issue_Ledger.md#lr12-43). The cause is not confirmed and
the failure is retained; no async fixture or sanitizer stack check is weakened.

**Initial core receipt:** the required gate records **8390/8391** including input,
with **6278/6279** runtime checks. All **1896/1896** Lambda script cases pass,
including the new convex golden. Its one failure is
`Tiers/HotwireHttpTest.ClientAndServer/1`: output stops after the initial frame
load rather than reaching the remaining HTTP/navigation assertions. That exact
test passes in isolation with its unchanged golden. Retain the nonzero
aggregate and the isolated pass; do not infer a green full gate from them.
Artifacts: `core-gate.log`, `core-results/`, `serve-isolated.json` under
`temp/scene3d-convex/`.

**Radiant receipt:** the required aggregate completes with **6479 passes,
140 partials, six failures and two skips**. Its native Scene3D group passes
**86/86**, UI automation **743/743**, and View **247 passed / one skipped**.
Render comparison passes **207 cases**, with four expected partials and one
skip. All six failures are the previously recorded text/inline/sizing layout
cases (**3152 full passes / 136 partials / six failures**). The failed receipt
is retained under `radiant-results/` with `radiant-gate.log`; the render-adapter
timeout from the earlier capsule aggregate is not reproduced in this run.

**Final core receipt:** the unchanged required rerun passes **8391/8391**:
**2112/2112** input and **6279/6279** runtime checks, including the HTTP suite
at **152/152** and the native script runner at **1896/1896**. No assertion,
concurrency setting or expected output is changed. Keep the initial failure
and `core-final-results/` / `core-final-gate.log` separately.

Final acceptance review added one positive rotated-overlap case, complementing
the earlier rotated-separation case. All **36/36** physics cases pass both
normally and with the solver/geometry/test sources instrumented by ASan; the
engine is unchanged from the aggregates. Final focused receipts are
`native-final.json` and `asan-native-final.json`. The ordinary test executable
is restored after sanitizer qualification. Header lint still records **70/24**
with zero new Radiant header violations; the new header is the shared lower
geometry provider. These standing limits and LR12-43 prevent a claim that all
qualification gates are green.

This delivery remains part of M2; mesh/heightfield
environments, geometry references, general shape overlaps/casts and CCD are
still pending.

## M2 third delivery: mesh and heightfield environments

**Outcome and scope:** admit indexed triangle meshes and triangulated XZ
heightfields for fixed/kinematic environment bodies, colliding with the existing
sphere, capsule, box and convex profiles. Share owned geometry, BVH traversal,
triangle features, queries and staged replacement in `lib/physics/` beneath
Mark and live Scene3D (**D7.1.1–D7.1.3 / D7.3.4**). Preserve procedural live
ownership and pure batch calls (**S12.1.1v3 / S12.1.5v2 / D5.3.3**).

The first profile includes finite inline indexed data, numeric height samples,
front/two-sided surfaces, exact-position welding, edge/vertex adjacency and
coplanar seam filtering, bounded contact manifolds, ray/surface-point queries
and convex-to-surface closest-point queries. Dynamic concave bodies and
mesh/mesh distance queries reject explicitly. Mesh resource references,
arbitrary shape overlaps/casts and CCD remain later M2 work.

**Acceptance:** primitive/convex rest on indexed floors; concave gaps do not
become solid hulls; winding/backfaces, rotated surfaces and moving platforms;
capsules/boxes cross coplanar triangulation seams; height samples and normals
match their declared tessellation; ray/BVH results match an independent flat
triangle oracle; sparse queries prune distant triangles; bad indices,
degenerate/nonmanifold geometry and exhausted work quotas return errors;
source buffers are owned; compatible edits/rejection preserve live state.

**Verification budget:** focused native geometry/solver regressions, paired
Lambda goldens on interpreter and JIT, and a live fixture under forced/poisoned
GC. Qualify native geometry with ASan, retaining the known LR12-43 live-async
limit. Then required core/Radiant gates once with isolated
`temp/scene3d-mesh/` receipts; investigate failures without weakening gates.

BVH background: [PBRT's bounding-volume hierarchy chapter](https://pbr-book.org/4ed/Primitives_and_Intersection_Acceleration/Bounding_Volume_Hierarchies).
The implementation extends Lambda-owned geometry; vendor code stays unchanged.

### Implemented environment profile

`lib/physics/physics_mesh.c` owns the cooked surface provider, below
`lambda/core/physics_scene.*` and the Radiant adapter (**D7.1.1–D7.1.3**).
Indexed CCW meshes and XZ heightfields admit fixed/kinematic owners, density
zero, rigid local poses and inline numeric data through Mark, metadata and HTML.
Heightfields use row-major samples, positive X/Z cell spacing and an explicit
upward diagonal tessellation; they are triangle surfaces rather than bilinear
patches or enclosed solids. Source buffers, including height samples, are copied.

Cooking welds exact positions, rejects degenerate/duplicate faces, invalid
indices, inconsistent winding and nonmanifold edges, builds edge adjacency and
bounded vertex fans, then constructs an immutable median-split BVH with four
triangles per leaf. Source vertices are charged before welding, preventing
repeated points from bypassing the world's owned-storage quota. Native limits:
65536 source points per world, 131072 triangles per surface, 262144 per world,
128 incident triangles per welded vertex and 8192 triangle tests per query or
contact pair. Exhaustion is an error, not a successful miss.

Triangle convex views reuse support maps, SAT/clipping, round/segment witnesses
and GJK without inventing volume/mass. Flat internal edge/vertex normals are
filtered, and coplanar constraints merge into at most 32 four-point patches.
Sharp creases retain actual triangle features; general crease-fan smoothing is
still partial. One-sided contacts admit a front-side convex centre (mass centre
for hulls); two-sided contacts orient the face toward it. Rays honor winding;
point queries test the surface, and closest-distance queries ignore sidedness.
Mesh/mesh contact or distance operations and dynamic concave owners reject.
AABB queries keep their previous broad-phase meaning.

The gap acceptance exposed a reversed barycentric inequality in the existing
triangle-closest helper. Correcting its BC edge test prevents exterior points
from being classified as interior face points. The shared helper also normalizes
edge lengths before forming fourth-order barycentric products; meshes and the
existing convex/round paths use the same repair.

The compiler binds vertex/index/height spans after buffer growth and compares
contents rather than addresses. Compatible live edits stage the owned cook and
retain ticks, poses and velocities; rejected edits retain the old world, and
unrelated edits do not wake sleeping bodies (**S12.1.5v2 / D5.3.3**).
Capabilities now expose `mesh_environment` and `heightfield_environment`; CCD
remains false. Collision resource references, exact general overlaps, shape
casts and CCD remain later M2 deliveries; M3 is not started.

### Environment acceptance and qualification

Normal native physics: **53/53**, including four convex profiles at rest,
seam crossing, concave gaps, sidedness, point/distance witnesses, rotated and
moving platforms, heightfield normals/tessellation, deep ownership and rejected
replacement, topology/work/storage quotas and mesh/mesh rejection. A seeded
independent Moller–Trumbore oracle checks 96 rays against an 8192-triangle grid;
each query tests fewer than 64 triangles. A dense 2048-triangle floor keeps at
most four resting constraints. Rotation/scale queries cover 1e-6 through 1e6.
Receipt: `temp/scene3d-mesh/native-qualified.json`.

Full native Scene3D: **87/87**. The new retained-state case checks camera edits,
invalid indices, compatible vertices and height changes, including sleeping
state and tick preservation. Receipt: `scene-native.json`.

`test/lambda/physics/mesh.ls` and its golden pass on interpreter and JIT under
`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`, as does the existing convex
golden. The registered live fixture passes all **17 events** under those same
GC settings, checking actual Lambda/JS calls, mesh and height edits, retained
motion, refreshed query witnesses, a capture and capability reporting.
Receipt: `live-forced-gc.json` (before the final numerical/quota hardening;
the milestone aggregate supplies the final bridge regression).

Integer-cast lint and local document-link/event-inventory checks pass.
Sanitizer and required aggregate receipts follow below when completed.

**Sanitizer receipt:** all **53/53** native physics checks pass with the solver,
convex geometry, new mesh provider and GTest source instrumented (verified
`__asan_report_*`/`__asan_mem*` references in their objects). The supported ASan
host also passes the mesh golden on interpreter and JIT with forced/poisoned
GC. `detect_leaks=0:detect_container_overflow=0` is recorded; no claim of leak
qualification is made. Receipts: `asan-native.json`, `asan-mesh-*.out/.err`.
The ordinary native executable is restored after this run.

The live ASan fixture reproduces **LR12-43**, aborting in
`fiber_stack_prime` (`lib/fiber.c:187`, stack-use-after-scope during async
activation), before completing its events. Receipt: `asan-live.log`.
No fiber code, stack-use checking or precise roots are weakened; the cause
remains unconfirmed and live sanitizer qualification remains open.

**Boundary/header audits:** the header ratchet still fails at **70/24**, with
zero newly violating Radiant headers (50 non-allow-listed / 33 mirrors).
The new geometry implementation and APIs belong to the shared lower provider.
No cap or import allow-list is raised.

**Core milestone receipt:** the required `make test-lambda-baseline` passes
**8393/8393** with isolated `temp/scene3d-mesh/core-results/` and
`core-gate.log`. No retry or changed expected output is needed. The workspace
also contains concurrent plant-package work, which is left untouched; this
receipt describes the tested workspace rather than attributing every added
case to Scene3D.

`make check-module-boundary` rebuilds/validates strict DSOs, then rejects the
same six previously recorded deferred imports (140 imports, baseline 165):
`view_get_client_matrix`, `doc_state_request_repaint`,
`view_geometry_block_viewport_origin`,
`editing_geometry_text_control_for_each_selection_rect`,
`_dom_boundary_from_source_pos` and `_radiant_dom_m4b_src_get`.
Receipt: `module-boundary.log`; the allowance remains unchanged.

**Radiant milestone receipt:** required `make test-radiant-baseline` completes
with **8078 passed / 140 partial / 413 failed / two skipped** (8633 checks).
Native Scene3D passes **87/87**, UI automation **743/743**, View **248 passed /
one skipped**, and render comparison **207 passed / four expected partials /
one skipped**. The new live mesh/heightfield fixture passes in the aggregate.
Artifacts: `temp/scene3d-mesh/radiant-gate.log` and `radiant-results/`.

The current base (`16bae996b`, including writing-mode support `081ec0588`)
adds `wpt-css-writing-modes` to the required layout selection. That suite has
**1573 passes / 407 failures**, and was absent from the previous convex
receipt. Every layout category shared with that receipt has unchanged counts:
**3152 full passes / 136 partial / six failures**. Combined layout receipt:
**4725 full passes / 136 partial / 413 failures**. Preserve the expanded failed
gate; do not infer that it passes from the focused geometry evidence or remove
the added suite. No layout source or gate configuration is changed here.

This delivery finishes the bounded environment profile, with the documented
sharp-crease limit, LR12-43 and standing layout/boundary/header limits still
open. M2 remains partial until resource references, general shape
queries/casts and admitted CCD are delivered; M3–M12 remain pending.

The final qualified binary also passes the new live fixture again under forced
and poisoned GC: **17/17 events**, with all assertions passing. Receipt:
`temp/scene3d-mesh/live-final-forced-gc.json` and its `.log`. Document links,
event inventory and `git diff --check` pass. No engine changes follow the
aggregate receipts.


## M2 fourth delivery: referenced collision geometry

**Implemented, 2026-10-29; qualification below.** Deliver the resource-backed geometry portion of M2.
The compiler previously rejected every collider `geometry` reference despite
already sharing geometry ingestion between Mark, metadata and live DOM. Extend
that same compiler below Radiant (**D7.1.1–D7.1.3 / D7.3.4**) and keep source data
owned across calls (**D4.5.1v4 / D5.3.3**). Constructors/batch validation remain
pure; live edits follow **S12.1.1v3 / S12.1.5v2**.

**Finite profile:** colliders may name same-scene static `geometry type:buffer`
resources for convex points/indexed or unindexed triangle meshes. Physics-only
`collision-geometry` resources, version 1, supply the existing convex, mesh and
heightfield data; constructors live in `lambda.physics`. They are definitions
inside `resources`, without graphics allocation. Collider transforms, materials
and surface sidedness remain per collider. Shape references cannot alias other
resources or combine with inline geometry. Skinned/morphed buffer resources and
primitive visual geometry are outside this collision-resource profile.

**Acceptance:** forward references and shared shape data; equivalent inline
and referenced trajectories/mass/ray/distance results; Mark/metadata/HTML
parity; missing/duplicate/wrong-kind/wrong-shape/version errors; no silent
inline fallback; owned data and unchanged geometry quotas; live shared-resource
updates preserve ticks/poses/velocity and unchanged edits preserve sleep; failed
updates preserve the previous world and compatible recovery works. Test both
physics-only resources and existing render buffers, including unindexed meshes.

**Verification budget:** paired interpreter/JIT goldens with forced/poisoned
GC, focused native compiler/retained-world cases, a registered live reference
fixture, then the complete Scene3D group and required core/Radiant aggregates
once the code settles. Use isolated `temp/scene3d-resources/` receipts. Target
ownership checks with the supported ASan host; retain LR12-43's established
live-async limit. Existing layout/header/module-audit failures remain separate.
Shape casts, exact general overlaps and CCD remain the next M2 increments.


**Implemented profile and focused receipts:**

The compiler indexes the existing bounded scene ID namespace before compiling
bodies, admitting forward references without depending on renderer state. A
shared source reader serves Mark/DOM and geometry ingestion is extracted once
for inline, render-buffer and physics-only definitions. Numeric spans are copied
and bound after buffer growth, then copied into the native world. Each instance
retains the existing storage/cooking charge; this adds no deduplicated native
cook cache and does not change quotas. Unused physics definitions receive
envelope validation; geometry ingestion/cooking occurs for referenced instances.
Empty render index streams follow the
renderer's unindexed-triangle meaning. Only version-1 physics definitions inside
`resources` are admitted; aliases/transforms and mixed inline data reject.

Compatible shared-resource edits use existing staged configuration and content
comparison, preserving ticks/poses/velocity and avoiding wakes for unchanged
geometry. The capability is `collision_resources`; `ccd` remains false.

- New resources, existing mesh and convex goldens: **6/6** interpreter/JIT runs
  with collection every allocation and freed-memory poisoning. The resource
  fixture checks exact inline/reference trajectory, mass and query equality,
  metadata and pure scene admission, missing/duplicate/mismatched resources,
  versions, animated buffers, aliasing and inline/reference exclusion.
- New native retained-world/resource-quota cases: **2/2 pass**. Shared hull
  edits update both bodies, unchanged buffers keep sleep, missing/version/bad
  geometry rejection keeps the old world, and three shared heightfields still
  hit the existing per-world owned-storage bound. `native-resources.json`.
- Registered Lambda/JS live replay: **19 events / 19 assertions pass**, with
  forced/poisoned GC. It edits render-buffer mesh, heightfield and shared convex
  definitions, checks motion/mass/distance/ray updates and capability reporting,
  captures actual rendering and steps again without resetting the clock.
  `live-forced-gc.json` and `.log`.
- Supported `make build-debug-asan` host: new resource golden passes both tiers
  with forced/poisoned GC. `asan-resources-*.out/.err`. The compiler object has
  ASan instrumented accesses (`asan-compiler-symbols.txt`). Leak and container
  overflow detection are disabled as in the previous M2 receipts.
- Live ASan reproduces **LR12-43**, stack-use-after-scope at
  `fiber_stack_prime`, `lib/fiber.c:187`, before completing the async fixture.
  `asan-live.log` retains the report; live sanitizer qualification stays open.
  No fiber or rooting behavior, sanitizer check or fixture is weakened.
- `doc/Lambda_Physics.md` code-block gate passes all **4** units, including the
  new resource example. Event registration, golden pairing and whitespace
  checks pass. Required aggregate receipts follow after completion.


**Core milestone receipt:** required `make test-lambda-baseline` passes
**8405/8405**: 2112 input and 6293 runtime checks, including the new resource
golden. Artifacts: `temp/scene3d-resources/core-gate.log` and `core-results/`.
The tested workspace includes the preceding merged ledger fixes; the total
increase is not attributed solely to this resource profile. No expected
result, timeout, concurrency setting or gate is changed.


**Radiant milestone receipt:** required `make test-radiant-baseline` completes
with **7874 passed / 136 partial / 414 failed / one skipped** (8425 checks).
Native Scene3D passes **89/89**, UI automation **743/743**, View **249 passed /
one skipped**, and page snapshots **51/51**. The registered resource fixture
passes in the View aggregate. `radiant-gate.log` and `radiant-results/` retain
the full failed run.

The layout receipt is unchanged from the preceding mesh milestone:
**4725 full passes / 136 partial / 413 failures**, including 407 writing-mode
failures and the same six text/inline/sizing cases. No layout source, image
tolerance, timeout, test selection or gate is changed for this feature.

The additional failure is render-adapter startup: it reports `lambda.exe`
missing at 07:31:15 SGT; the executable is present afterward with modification
time 07:31:18. The shared build log was also updated during that interval.
This is evidence of executable replacement during qualification, without
identifying which process initiated it. All implementation source hashes stay
unchanged (`qualified-source-hashes.json`). Preserve the startup failure.
The exact original render adapter rerun passes **207 cases / four expected
partials / one skipped / zero failures** (`render-isolated.json` and `.log`).
This isolated result does not turn the failed aggregate into a green full run.

After replacement, the current executable also passes the new resource golden
on interpreter/JIT with forced/poisoned GC, and the new live fixture again:
**19 events / 19 assertions pass** (`resources-final-*.out/.err`,
`live-final-forced-gc.json` and `.log`). Its fingerprint is stable across the
isolated checks: SHA-256 `0c0bdca95370b702f3fa27bbcdf1b7927d2318a5d779081b998fe5f6e33f104b`.

**Completion:** the bounded referenced-collision-geometry profile is implemented
and its numerical, native/live, ownership, rejection and unchanged-quota cases
pass. Pure constructors and source/world ownership continue to follow
**S12.1.1v3 / S12.1.5v2 / D4.5.1v4–D4.5.3v2 / D5.3.3**. Standing live-ASan and
layout limits remain open; unchanged module/header audits were not rerun because
this delivery adds no lower-layer exports, module imports or header ownership
changes. M2 remains partial: exact general overlaps, shape casts and admitted
CCD are next; M3–M12 and the Lambda gameplay KIV scopes remain pending.


## Persistent implementation goal — 2026-10-29

The user requested all outstanding native M0–M12 work as an active goal. Finish
M2 queries/casts/CCD, then deliver M3–M12 in dependency order with bounded
acceptance and milestone gates. The four gameplay-algorithm families in design
§13 remain Lambda Script KIV. Preserve all prior uncommitted resource work and
its qualification. Known unrelated layout failures remain separately recorded;
new native ownership or algorithm failures must be investigated at their owner.

## M2 next delivery: exact overlaps, shape casts and all-hit queries

**Active scope:** reuse the existing named collider geometry for exact shape
overlap and translation-only casts, with an optional world translation offset.
Support spheres, capsules, boxes and cooked hulls against those shapes and
fixed/kinematic mesh/heightfield environments. Target poses are sampled, not
advanced. AABB retains its broad-phase meaning. Add sorted all-hit output to
existing queries, bounded by explicit result capacity and overall script output
limits. Querying does not advance or mutate simulation (**S12.1.1v3 / D2.11.4**).

**Owner:** reuse GJK closest witnesses and immutable mesh BVHs in the C provider
(**D7.1.1–D7.1.3**); expose them through the same pure/live query conversion.
Conservative advancement uses separating-distance witnesses and bounded
iterations; mesh casts visit swept-volume BVH candidates instead of assuming
the nearest triangle at the initial pose is the only possible hit. Initial
overlap is an explicit zero-distance result. Numeric/stall/work/output budget
exhaustion is an error. Source-body colliders are excluded from shape queries.

**Acceptance:** analytical sphere/box sweeps, capsule/hull pairs, rotated
geometry, near-parallel paths, gaps/backfaces/heightfields, offset queries,
initial overlaps, deterministic all-hit ordering and filters, read-only ticks,
invalid source/parameters/capacity and exhausted iterations. Compare seeded
convex sweeps with independent box/slab oracles and mesh sweeps with a flat
triangle oracle; stress resource/query conversion on both tiers and live DOM.

**Verification budget:** focused C/provider tests and paired script/live checks
during this increment, then finish CCD before the next full M2 core/Radiant
qualification. Shared query code and helpers must be extracted rather than
copied. Retain prior ASan live-async limits until their cause is repaired.

Algorithm background: [Box2D collision documentation](https://box2d.org/documentation/md_collision.html)
and [Catto, Continuous Collision (GDC 2013)](https://box2d.org/files/ErinCatto_ContinuousCollision_GDC2013.pdf).
These describe algorithm families; Lambda owns the implementation and its
independent numeric acceptance, without modifying or copying vendored code.


**Current query/geometry evidence:** native tests **59/59 pass**, including
128 seeded rotated box/slab oracles, rotated capsule/plane support controls,
and 128 sphere casts against mesh/heightfield data checked by an independent
face/edge/vertex quadratic oracle.
Thin-wall, offset, initial-overlap, all-hit sorting/capacity, AABB false-positive
and mesh sidedness checks pass. The new paired script checks all query modes,
filtering, retained source state and explicit errors. Receipts are under
`temp/scene3d-casts/`. Cast/resource/mesh/convex/capsule goldens pass on interpreter,
JIT and automatic execution with forced collection every allocation and poisoned
freed storage (**15/15**). The registered live bridge fixture passes all **17/17**
events under the same GC settings, and the native Scene3D suite passes **89/89**.

The initial independent capsule/plane and thin-wall script checks exposed GJK
nonconvergence. `gjk-trace.log` records a simplex whose true tiny separation was
contaminated by cancelling weighted coordinates on long edges; that spurious
transverse residual repeatedly selected the same ineffective support feature.
Gram normal equations also squared skinny-simplex conditioning. GJK now uses
reorthogonalized QR for admissible barycentrics and geometric edge/face
projections for the search direction and distance normal. Iteration budgets
and overlap tolerance are unchanged; temporary trace code was removed.
The earlier failed receipts (`native-qr.*`, `oracles-first.*`, `golden-qr.*`)
remain separate from `native-feature-projection.*`. No miss or tolerance was
changed to hide a solver failure.

CCD acceptance additionally exposed an edge/face norm tie: a face projection
was closer in exact arithmetic, but the norm rounded to the same value as a
diagonal edge projection. Repeated support selection then cycled at a residual
just above convergence. The reducer now prefers the admissible higher-dimensional
feature within arithmetic roundoff and recomputes projections after zero-weight
reduction. Separated polyhedral witnesses now resolve support gaps to arithmetic
precision rather than accepting an avoidable angular residual at the spatial
overlap tolerance; the overlap threshold and iteration limit remain unchanged.
`native-ccd-trace.log` preserves the reduced input and repeated direction;
`ClosestFaceWinsRoundoffTiesNearItsTriangulationDiagonal` retains the regression.
Sphere casts also sample the centre-to-target normal at TOI, because the prior
advancement plane's small angular error measurably deflected high-speed impulses.
Rotated rounded/polyhedral controls also showed that smooth support could
converge too slowly near contact. Sphere/capsule distances now reuse the existing
contact kernel's exact segment/face/edge witnesses; round/round pairs reuse the
existing closest-segment helper. Overlap witnesses retain a common intersection
point. This changes the shared geometry algorithm for the full shape families,
without raising the GJK budget or converting nonconvergence to a miss.

## M2 CCD first delivery: single-sphere projectiles

**Outcome and scope:** a dynamic body with one solid sphere can opt into `ccd`.
Its geometry is invariant under spin, including an offset body origin. Eligible
pairs are sphere/sphere dynamics and sphere against fixed or translating
kinematic primitive, hull, mesh or heightfield environments. Rotating nonsphere
targets and dynamic nonsphere targets in the swept candidate set reject with an
explicit error. Sensors retain discrete sampling. Compound/capsule/hull CCD and
rotating nonsphere coverage remain outstanding; this is a bounded first CCD
profile, not completion of the full M2 collision family.

**Owner:** the existing C provider applies forces/damping once per whole tick,
finds the earliest relative-motion cast, advances all bodies to that time,
solves the impact and integrates the remaining time. Discrete and swept pairs
publish one ordered event diff at the tick boundary. Precise snapshots retain
the last completed tick after a fault (**D2.11.4 / D4.5.3v2**). No script/native
handle or evaluator ownership changes (**S12.1.1v3 / D7.1.1–D7.1.3**).

**Acceptance:** thin-wall stop, restitution/remainder, relative sphere motion,
translating wall, gravity/resting contact, spin invariance, mesh/heightfield,
atomic enable/disable/invalid edit, pair/filter behavior, independent replay,
explicit impact/work exhaustion and faulted-read snapshot. Add pure tier/GC and
live pause/step/reconfigure cases. Initial touching/separating contacts must not
consume the impact budget forever; forces must not multiply with substeps.

**Budgets and verification:** 32 impacts, 65536 candidate pair casts and 8192
triangle tests per tick, with each cast bounded by 128 iterations. Exhaustion
faults the step instead of silently missing. Run the focused provider suite and
new paired/live fixtures in the edit loop, supported ASan for ownership paths,
then one core/Radiant aggregate for the completed query/CCD increment. Carry
standing aggregate/ASan limits separately from new results. Broader nonsphere
CCD remains part of the active all-scopes goal.

**Focused result:** native physics **70/70**, native Scene3D **89/89**, and
the six affected live cases **6/6** pass. The new CCD live fixture checks all
**15/15** events, including in-place CCD enable/disable with ticks preserved.
CCD/casts/invalid/resources/mesh/convex/capsule goldens pass across interpreter,
JIT and automatic execution with GC every allocation and freed-memory poisoning
(**21/21**). Independent reflection checks cover 64 rotated box/hull impacts;
analytical controls cover 128 rotated slab casts and 128 mesh/heightfield sphere
casts. Final receipts use `qualified-*` and `native-ccd-final.*` under
`temp/scene3d-casts/`. A concurrent external rebuild removed `lambda.exe` midway
through the earlier tier sweep; the final focused sweep and View run use the
frozen `lambda-qualified.exe` and `source-hashes.json` records that boundary.
The sanitizer/aggregate results and subsequent contact-capacity repair are
recorded below; their limits remain open.

**Sanitizer / boundary result:** `make build-debug-asan` succeeds. CCD, cast,
resource and mesh goldens pass on interpreter and JIT with forced/poisoned GC
(**8/8**). `asan-instrumentation.json` confirms ASan references in the provider,
geometry/BVH, scene compiler, runtime query bridge and live-physics objects.
The live CCD fixture reproduces **LR12-43** at `fiber_stack_prime` line 187
before gameplay assertions; retain that qualification failure without suppressing
stack-use-after-scope. Leak checks remain disabled as in the standing supported
host qualification. Physics documentation blocks pass **4/4**.
`module-boundary.log` retains the same six deferred Class-F imports already
recorded for M0; no new imports were added by the query/CCD work.

**Core aggregate:** the required command finishes **8289/8409** (input
**2112/2112**, runtime **6177/6297**). Its 120 failures comprise missing profiling
output (**118** JS optimization cases), the debug-only GC verifier (**1**) and
the MIR ratchet selecting its release/default budget (**1**). The shared host
changed from the frozen debug binary to a smaller nonprofiling binary during
qualification; no engine source fingerprint changed. Replaying the affected
suites with the frozen debug host passes **121/121**, **55/55** and **20/20**,
respectively. The MIR rerun uses the existing isolated-workspace helper, with
unchanged budgets. Preserve the raw failed aggregate and separate profile-correct
receipts; this is combined evidence, not a single green aggregate run. See
`core-results/`, `profile-*` and `source-hashes.json` under the receipt directory.

**Final quota repair:** after the aggregate's executable was frozen, a new
provider regression confirmed that CCD's initial-contact normal probe appended
temporary contacts into the persistent contact buffer. A resting sphere could
therefore exhaust `contact_limit:1` while owning just one contact. Contact
generation now accepts a bounded writer; the same manifold code writes either
tick contacts or independent stack scratch. No quota is raised and no manifold
algorithm is duplicated. `native-ccd-capacity-before.*` records the failure;
`native-contact-writer.*` passes **71/71**. The updated provider also passes
native Scene3D **89/89**, both affected live fixtures (**2/2**, 32 events), and
CCD/cast/mesh goldens on interpreter/JIT with forced/poisoned GC (**6/6**).
`writer-*`, `lambda-final.exe` and `writer-hashes.json` identify this subsequent
small provider change separately from the aggregate's frozen executable.
The supported ASan profile is rebuilt for this repair; the same CCD/cast/mesh
tier matrix passes **6/6** with forced/poisoned GC (`asan-writer-*`).

**Radiant aggregate:** the required command finishes **8083 passed / 140
partial / 413 failed / 2 skipped** across **8638** checks. All failures belong
to layout: text **1**, inline **1**, sizing **4**, writing modes **407**. The
layout totals and affected-category counts match the preceding resource
increment (**4725 passed / 136 partial / 413 failed**). UI automation passes
**743/743**, native Scene3D **89/89**, View **251 passed / 1 skipped**, page
snapshots **51/51**, and render **207 passed / 4 expected partial / 1 skipped**.
This run uses the pre-quota-repair frozen host; the provider-only repair has
the separate focused/native/tier/ASan checks immediately above. No broad check
is reported green and no layout threshold was changed. Receipts are
`radiant-gate.log` and `radiant-results/` under `temp/scene3d-casts/`.

**Current checkpoint (2026-10-29):** the exact-query and first CCD increment is
implemented with the final provider qualification above. Core's raw build-profile
failure, the standing layout counts, six boundary imports and **LR12-43** remain
qualification limits. All engine source fingerprint changes since the aggregate
snapshot are confined to the documented contact-writer repair and its regression;
documentation was updated separately. The all-native-gaming goal stays active.
M3 implementation starts with the plan below; M3–M12 and the wider CCD matrix
are not delivered. The four gameplay-policy scopes remain Lambda Script KIV.

## Next playable delivery: M3 constraints and character movement

The admitted single-sphere CCD profile satisfies the M2 thin-wall core case;
the wider nonsphere/rotation matrix stays in the active full-scope backlog.
Prioritize articulated movement next, then retain the rest of M2 for combined
qualification. This follows the design's bounded milestone rule rather than
treating all collision combinations as a prerequisite for every game feature.

**First M3 slice:** shared native constraint rows and owned joint descriptions,
starting with distance/spring and point constraints. Authoring remains pure;
live edits preserve the world and invalidate only affected impulses
(**S12.1.1v3 / S12.1.5v2 / D7.1.1–D7.1.3**). Subsequent slices add fixed, hinge,
prismatic and cone/twist limits/motors, breaking, character motion and ragdolls.
Do not advertise the complete joint/controller capability before its admitted
pure/live path works. Gameplay AI policy remains KIV.

**Acceptance and budget:** analytical anchor/length controls, pendulum and
spring decay, a constrained chain with bounded drift, collision filtering for
connected bodies, singular/budget/error cases and deterministic replay. Then
exercise constructors, metadata/DOM edits and pause/step/reset through the
shared scene compiler. Focused provider tests precede paired tier/GC and live
checks; qualify the completed M3 slice once, preserving the standing layout,
boundary and live-ASan limits. Reuse contact impulse/inertia machinery and
coordinated stepping; do not copy the solver for each joint kind.

Design references for the next slice: [PhysX joint frames, distance constraints
and implicit spring rows](https://nvidia-omniverse.github.io/PhysX/physx/5.4.1/docs/Joints.html)
and [Box2D distance-joint authoring](https://box2d.org/documentation/group__distance__joint.html).
Use body-origin local anchors so collider/mass edits do not reinterpret authored
attachments. Specify physical stiffness/damping units and test an implicit
spring update against an independent oscillator, rather than inventing arbitrary
per-frame spring gains. These are algorithm/authoring references, not a change
of native engine or authorization to copy vendor code.

## M3 joint delivery — 2026-10-29 (qualification in progress)

The native provider now owns distance/spring, spherical, fixed, hinge,
prismatic and circular cone/twist descriptions. Local frames, bounded motors,
limits, net-wrench breaking, reaction snapshots, connected-body collision policy,
and shared dynamic sleep/wake islands use the existing world. The pure
`physics.joint` constructor and shared scene compiler support forward ordinary
and metadata body references. Live motor/frame/enabled edits preserve ticks and
motion; unchanged broken joints remain broken. Body/joint sample storage is
charged together. Character movement/ragdolls and joint/CCD TOI coupling remain
open; this slice does not complete M3 or the full goal.

Ownership/effects remain **S12.1.1v3 / S12.1.5v2**, **D2.11.4**,
**D4.5.3v2**, **D5.3.3** and **D7.1.1–D7.1.3**. Joint values/snapshots are copied
through the existing precise-root builders; no raw live-world handles or
native gameplay-policy framework are added.

**Solver evidence and root repairs:**

- Contact and joint rows share impulse, inertia and effective-mass operations.
  Independent spring Euler recurrence, motor torque and parallel-axis inertia
  controls substantiate the solver rather than mirroring its implementation.
- The first cone test exposed second-order drift after velocity constraints.
  Bounded position correction reuses joint rows and contact manifolds without
  changing velocity impulses or increasing the requested iteration count.
- The default-budget live offset hinge exposed slow sequential convergence
  between anchor locks and the limit/motor. A normalized Cholesky block resolves
  bilateral rows, and a Schur complement projects bounded effort through that
  block, using attachment-constrained inertia. The reduced native failure is
  `native-offset-default-before.*`; `OffsetMotorResponseMatchesParallelAxisInertia`
  passes at one velocity iteration. No drift assertion was relaxed.
- Geometry validation includes inactive/broken exported frames before body
  snapshots commit. Descriptor and authored command changes use one staging
  transaction, including body/joint snapshot arrays, gravity, forces and contact
  cache metadata. A rejected combined edit retains old published views and a
  usable world. Unchanged descriptors and unrelated contact/joint warm starts
  survive; only affected bodies/caches are invalidated.

**Current checks:** the final native provider passes **91/91**, including rigid
transform invariance. Both native joint-edit cases pass. The final
joint/CCD/cast/resource/mesh/convex/capsule/invalid golden matrix passes **24/24**
on interpreter/JIT/AUTO with forced collection and freed-memory poisoning.
Five live joint/CCD/cast/resource/coordinator replays pass with those GC controls;
the joint replay includes **12/12** events. The supported ASan host passes
**8/8** joint/CCD/cast/resource goldens across interpreter/JIT (`detect_leaks=0`);
the native GTest harness itself is not sanitizer-instrumented. Live ASan still
aborts in the existing **LR12-43** `fiber_stack_prime` startup path, before the
fixture completes. No suppression or conservative scanning was added.

The earlier broader native Scene3D run aborted in
`GltfAndGlbRoundTripsKeepIndependentMapPixels`. The isolated debugger traced the
concurrent numeric-attribute getter constructing `MarkBuilder` from the
fixture's empty document `Input`. The concurrent DOM owner repair subsequently
removed that abort: the final frozen native Scene3D selection passes **91/91**
in the character qualification below. The joint module-boundary gate reports
the same six Class-F imports as the preceding M2 checkpoint; new character
exports still need their boundary build. Receipts live under
`temp/scene3d-joints/` and `temp/scene3d-character/`.

A reentrant authored edit is rejected before configuration or structural
replacement during coordinator sampling. The shared read-only
`scene3d_runtime_can_configure` guard prevents a rejected refresh from changing a
motor or invalidating published spans before the coordinator rejects its pacing
change. `ReentrantJointEditRejectsBeforeAnyWorldOrSnapshotMutation` checks the
subsequent motor direction. Native limits and remaining CCD/character/ragdoll
restrictions are documented in [Lambda Physics](../../doc/Lambda_Physics.md#joints).


## M3 next slice: capsule character movement

**Outcome and boundary:** script-requested capsule displacement yields achieved
motion, grounding and copied contacts, through the shared C geometry provider.
Scene-specific commands/publication remain in `radiant/scene3d/`; pure batch
requests retain **S12.1.1v3 / D7.1.1–D7.1.3**, and controller-owned transforms
retain **S12.1.5v2 / D4.5.3v2**. Gameplay perception and action selection stay KIV.

**First acceptance:** unobstructed displacement; thin-wall sweeps and oblique
slide; floor/ceiling and slope classification; bounded initial-overlap recovery;
step-up/down and low-ceiling rejection; translating-platform carry with explicit
support identity; unchanged query worlds and rejected/budget-exhausted movement.
Then connect paired pure/tier/GC and live pause/step/edit checks. Ragdoll pose
handoff follows character movement. Rotation/deformation/CCD coupling limits
remain explicit until their later full-scope delivery.

**Owner/hypothesis:** current casts already provide capsule-to-convex/environment
TOI but no displacement resolution or grounding policy. Reuse those casts and
existing contact generation rather than build another collision engine. A
bounded sweep/slide loop, contact skin, slope test and alternative up/forward/down
step path own the movement result; application gravity/jump policy remains script.
Results identify the achieved target for the existing kinematic-body command.

**Verification budget:** focused numerical controls first, then existing physics
neighbors, paired scripts and one causal live replay. Qualify once after the
native/pure/live slice is stable; retain the standing aggregate and ASan limits.
Algorithm references: [PhysX capsule controllers, skin, stepping and slopes](https://nvidia-omniverse.github.io/PhysX/physx/5.4.1/docs/CharacterControllers.html),
[Godot requested/achieved motion and moving-platform contracts](https://docs.godotengine.org/en/stable/classes/class_characterbody3d.html).
These references do not authorize vendor edits or introduce autonomous game AI.


## M3 capsule movement delivery — 2026-10-29 (qualification in progress)

The C provider exposes a query-only, bounded capsule controller. It reuses
owned geometry, shared cast pruning and every existing contact manifold path.
A private two-collider world view supplies recovery contacts without writing
live collider caches. Floor snapping, slope/ceiling classification, corner
clipping, stair alternatives and previous-support pose carry return copied
motion/contact data. Triangle work is charged across recovery and casts.

`physics.move` and mixed `{type:"move",...}` queries expose the pure path.
`lambda.physics.scene.move` computes and queues a kinematic target in one host
operation, preserving **S12.1.1v3 / S12.1.5v2 / D4.5.3v2 / D5.3.3 /
D7.1.1–D7.1.3**. The next existing physics tick owns pose publication. It adds no
independent scheduler, renderer or gameplay-policy framework. Dynamic pushing,
predicted moving-obstacle collision, ragdolls and wider CCD remain open.

Focused native provider evidence passes **107/107**, including 16 movement
cases (`temp/scene3d-character/native-final.*`). The walk-off-stair case exposed
roundoff between discrete contact and cast shells: recovery now uses a slightly
larger shell than casting, including nearly exact skin contact. This fixes the
cause without skipping a touching mesh or relaxing its movement assertion.
The tall-stair control also requires actual upward progress before selecting
an alternate path as a successful step. A shared mesh-work counter prevents
recovery plus subsequent casts from each consuming a separate triangle quota.

The pure/neighboring matrix passes **27/27** on interpreter/JIT/AUTO with forced
collection and freed-memory poisoning. The new live character replay and the
existing joint replay pass **24/24** events on the preceding frozen live build;
final aggregate/sanitizer qualification follows the last recovery/work-budget
repair. Limits and actual defaults are published in
[Lambda Physics](../../doc/Lambda_Physics.md#capsule-character-movement).

**Qualification setup:** the first required core run completed its input gate
**2112/2112**, then reported **3208/5378** runtime checks. Its receipts show both
an incorrectly retained nonprofiling host for profile probes and the shared
`lambda.exe` disappearing mid-run (exit 127 / missing file). The smaller runtime
count is not a valid coverage floor. A unique-target debug link now supplies a
frozen profiling host. The next aggregate uses the existing runner in an
isolated workspace and immutable native copies; source directories remain
readable for structural tests. The first launches of three copied native
runners exposed their relative node-core dylib path; restoring the matching
`modules` link fixes that setup. Preserve those raw failures and recheck the
failed components separately; do not change tests, budgets or thresholds.

## M3 next slice: ragdoll pose handoff

**Outcome:** a declared skeletal bone/body mapping can move from animation to
physical fall, then recover continuously, through the existing body world and
skin palette. Activation transfers sampled linear/angular velocity. Physical
bones own final poses while animation keeps a separate recovery destination
(**S12.1.5v2 / D4.5.3v2**).

**Prerequisites and owners:** the provider currently treats a motion-role edit
as structural, and the scene adapter rejects transform tracks on every physical
binding. Implement an atomic compatible role/constraint transition, then fixed
tick animation-to-kinematic sampling and explicit bone bind offsets. Share
`scene3d_world`, physics pose publication and existing quaternion interpolation;
do not duplicate skinning or create post-animation competing pose writers.
Animated/recovery collision and connected-body filtering need explicit profiles.

**Acceptance and budget:** role changes retain world/tick and reject invalid
whole transitions; activation preserves pose and transfers velocities; a limited
articulated fall meets anchor/swing bounds; recovery starts at the physical pose
and ends at the current animation pose without a one-frame jump; compatible
edits, pause/reset/seek and teardown preserve their owners. Numeric provider and
native palette checks precede paired scripts and one causal live fall/recovery
replay. Run required aggregate and boundary checks once at the completed slice;
retain the existing live-ASan and unrelated layout/DOM qualification limits.


### Capsule movement qualification checkpoint

The final provider passes **107/107** (`native-final.*`); frozen native
Scene3D passes **91/91** (`radiant-results/test_scene3d_unit_gtest_results.json`).
The final interpreter/JIT/AUTO forced/poisoned-GC matrix passes **27/27**
(`goldens-*.json`), and the documented capsule example executes successfully.
The final full script corpus passes **1914/1914**. Owned source hashes, the
profiling CLI hash and 61 native executable snapshots are recorded once in
`qualification.json`; `qualified-source/` preserves the exact owned source
boundary for the next slice. Artifacts are under `temp/scene3d-character/`.

The isolated core aggregate records **5394/5400**, including three synthetic
loader failures before the native module dependency link was restored, and
three file-utility failures because this workspace lacked its expected
`Makefile`. The three native suites subsequently pass **902/902** with the
same immutable executables/host; the exact three file-utility cases pass
**3/3** after supplying that source file. Profile checks pass **121/121** and
**55/55**, MIR budgets **20/20**, GC stress **350/350**, TypeScript **20/20**;
input remains **2112/2112**. Raw aggregate receipts are preserved rather than
rewritten as a green run. The earlier custom isolated recheck stopped at a
runner alias mistaken for an executable; the registered aggregate subsequently
runs that alias normally without changing its Test262 harness.

The Radiant aggregate is still running. Its CSS-memory budget fails in the
frozen isolated workspace. A repository-root comparison passes, but source
inspection shows that harness hardcodes `./lambda.exe` and ignores the viewer
override; the comparison used a different host hash and is **not** frozen-host
qualification. Keep that failure open; no budget or expectation was changed.
Final character ASan and new-export boundary checks remain pending until the
aggregate finishes. Full M3 (pushing/future-motion coupling and ragdolls), wider
M2 CCD and M4–M12 remain outstanding. The all-scope goal is active.


### Final character receipts and M3 role-transition prerequisite

The frozen character ASan host passes **10/10** character/joint/CCD/cast/resource
goldens across interpreter/JIT with forced collection and freed-memory poisoning
(`detect_leaks=0`). The character-export boundary build reports the same six
Class-F imports (**140 total vs. 165 baseline**) as the preceding checkpoint;
no new character import violation appears. Header inventory is unchanged.

The Radiant aggregate returned **8084 passes / 140 partials / 417 failures /
2 skips** across 8643 checks. Layout retains the previous **4725 passes /
136 partials / 413 failures**. UI records **742/744**, failing `blade_components`
and `doc_editor_readme_svg`; View records **252 passes / 1 failure / 1 skip**,
with the typed-text material diagnostic control failing. The CSS-memory failure
is the other non-layout failure. Native Scene3D is **91/91**, rendering records
all 211 admitted baseline cases, and page snapshots pass **51/51**.

Current-source investigation finds that the concurrent DOM numeric getter had
widened Scene3D's strict text lane. Its owner subsequently restored the typed
string/symbol guard in `scene3d_text`; the current role-capable host passes that
unchanged diagnostic fixture together with character and joint replays
(**3/3**, forced/poisoned GC). The two UI cases also pass on that host against
current fixtures; the Blade expectation was concurrently corrected by its
separate owner. These later receipts do not rewrite the frozen aggregate.

The next prerequisite now permits compatible body-role transitions through the
native **full-description** transaction and retained scene reconciliation.
Activation retains achieved poses/measured velocities; old targets and effort
are discarded on role changes, fixed roles stop, and role/geometry/constraint
validation shares the same candidate. Disabled joints may connect non-dynamic
bodies. The legacy geometry/property configuration API keeps its old role
restriction. Invalid fixed velocity commands reject the whole staged change.
**S12.1.5v2 / D4.5.3v2 / D7.1.1–D7.1.3** still govern the owners/effects.

Six reduced prerequisite cases first reported **1/6**; after the repair the
provider passes **114/114**, including the additional fixed-command rollback
case. Retained scene role edits and both existing joint-edit guards pass
**3/3**. The role/inactive-joint/FBX/character/joint matrix passes **15/15**
across interpreter/JIT/AUTO with forced/poisoned GC. The shared rotation
conversion was promoted from the existing FBX helper to `lib/math_utils.h`
before adding a bone-pose consumer; no third copy is added. Half-turn/composed
rotation and scene controls pass **4/4**, and native FBX checks pass **4/4**.
Ragdoll bone mapping/fall/recovery itself is still pending. These receipts live
under `temp/scene3d-ragdoll/`; shared-pose-sampler qualification is next.

## M3 physical bone and ragdoll delivery — 2026-10-29 (qualification in progress)

**S12.1.1v3 / S12.1.5v2 / D2.11.4 / D4.5.1v4–D4.5.3v2 /
D5.3.3 / D7.1.1–D7.1.3 / D7.3.4–D7.3.5** continue to govern effects,
scene authority, precise roots, ownership and ticking. No formal ruling changes.
The shared compiler now validates explicit bone/body bind frames, forward
references, unique bindings, quotas and rigid authored ancestry. Native physics
publishes `body_world * inverse(body_in_bone)` through the existing skeleton
palette; it does not duplicate skinning. Optional copied `bones` snapshots and
trace quotas include these bindings. Disabled bindings expose null poses while
body simulation continues. Animation transforms remain an independent recovery
destination while the physical owner supplies the complete final rigid pose.

The live `ragdoll` description admits 16 controllers with 1–64 bound bones
apiece. Animated mode uses kinematic targets, zero collider masks and suspended
touching constraints. Physical activation restores authored dynamics, filters
and constraints while retaining achieved poses and measured velocities.
Recovery blends captured physical world poses toward the moving animation
sample, using the existing shortest-arc quaternion interpolation, then resumes
animation following. Compatible edits retain the world/tick. Enable toggles
now retain broken joints; only a changed constraint definition repairs them.
This removes the implicit fracture repair that mode suspension would otherwise
cause. The native API exposes upcoming kinematic targets in copied body states,
so a parent target is sampled for the same tick rather than one tick late.

Scene seek stages an independent native mixer/world and samples authored input
through the shared animation-before-physics coordinator. Controller transitions
still run in the staged mixer, while DOM animation/contact/joint cues and
staged repaint requests are suppressed. Success transfers the animation service
and world after the complete replay. Failed later samples preserve both old
owners and their published poses. Replay reconstructs the current authored
mode, not past script commands. Explicit live physics batches with controllers
also use that coordinator and retain copied events across completed ticks.

Review found that position-kinematic velocity was measured at the body origin,
although contact/integration velocity uses the mass centre. The provider now
measures target centre displacement, so an offset collider rotating under a
target transfers the correct velocity into dynamics. Velocity-kinematic parent
prediction shares the provider's mass-centre/origin integration helper. The
independent quarter-turn/linear-centre oracle and rotating-parent orbit case
cover this repair.

The first physical-bone milestone passed **96/96** native Scene3D cases,
**33/33** interpreter/JIT/AUTO physics goldens with forced/poisoned GC, and
**3 live fixtures / 44 events**. A subsequent pose-ownership test initially
sampled a repeating clip exactly at its wrap boundary; the corrected interior
sample verifies the intended animation scale while the physical frame remains
rigid. The first mode/replay milestone passed **100/100** native cases and
**51/51** physics goldens across all three tiers. Its first live run passed
three of four fixtures: the new fixture inspected GPU counts immediately after
an invalid projection, before repairing/reprojecting its surface. An explicit
repaired surface capture now precedes that unchanged equality check; no engine
resource budget was weakened. Final replay/COM qualification follows below.

The earlier physical-bone ASan host passes **10/10** paired script cases across
interpreter/JIT with forced/poisoned GC (`detect_leaks=0`); that receipt predates
the new controller/replay code. Its export boundary reports the same six
standing Class-F imports, **140 total vs. 165 baseline**. The final controller
ASan and boundary checks are separate. Native graphics GTests are not sanitizer
instrumented. The existing live-fibre ASan startup limitation remains open.

The [admitted public profile](../../doc/Lambda_Physics.md#physical-bones-and-ragdoll-modes)
records rigid destinations, physical-ancestor restrictions, immediate animated
snap versus recovery, physical-only batch simulation and replay semantics.
Animated batch sampling, dynamic/cross-controller physical ancestry, character
pushing/future-motion collision and joint/CCD TOI coupling remain outstanding.
M4–M12 and wider M2 CCD also remain open. The four gameplay-algorithm scopes stay
Lambda Script KIV. The all-scope goal remains active. Artifacts are under
`temp/scene3d-ragdoll/`; preserve earlier raw aggregate receipts separately.

### Ragdoll mode/replay qualification checkpoint

The final source boundary passes **115/115** native provider cases and
**104/104** native Scene3D cases. All 17 physics goldens pass across
interpreter/JIT/AUTO with forced collection and freed-memory poisoning
(**51/51**); the final instrumented ASan host passes those same scripts on
interpreter/JIT (**34/34**, `detect_leaks=0`). These are script/host sanitizer
receipts, not instrumentation of the native graphics test executable.

The corrected live fixtures pass **5/5** on the frozen current host with
forced/poisoned GC: ragdoll modes/replay, physical bone bindings, capsule
movement, joints and shared runtime controls. The ragdoll replay alone executes
39 events, verifying activation pose/velocity, copied mode inspection,
collision mask restoration/suspension, no-jump recovery, actual changed skin
pixels, retained edits, reset, authored physical seek and a loop-crossing seek
that emits no gameplay cue. The Radiant dimension-cast lint passes. Final
boundary and aggregate qualification are still running; the all-scope goal
remains active.

The final export-boundary run reports the same six standing Class-F imports
(**140 total vs. 165 baseline**), with no added ragdoll import violation. The
final live-ASan attempt again aborts before the fixture at
`lib/fiber.c:187 fiber_stack_prime` with `stack-use-after-scope` (LR12-43).
Keep that qualification limit open; no sanitizer suppression or native-stack
GC scanning was added. The normal-host five-fixture receipt is **106 events**
(39 ragdoll, 20 physical-bone, 12 character, 12 joint, 23 runtime).

The frozen Lambda baseline completes **6307/6307**, including **1921/1921**
Lambda tests, **350/350** MIR GC-stress cases, **847/847** JS MVP/LMD cases and
**121/121 + 55/55** optimization checks. The broader registered input selection
records **4562/4589**: three modular-wiki cases, seven wiki-format cases, five
PDF/RTL visual cases and twelve script-format round trips fail. One modular-wiki
case also fails from the repository root using the same immutable executable;
its native formatter reports that MediaWiki output needs the runtime/package
API. Other wiki conversion failures report an `input_type` option error;
round-trip failures include AsciiDoc/Man document differences. These are raw
failed qualification receipts, not a green input aggregate. No related source,
expectation or test harness was modified for this gaming delivery.

The normal-host Radiant aggregate is still progressing through its UI inventory.
The source boundary and immutable host/native hashes are recorded in
`temp/scene3d-ragdoll/qualification.json`; matching source copies live under
`qualified-source/`. The initial sandboxed input pilot was stopped and its raw
partial artifacts retained; qualification uses the separate host-enabled run
with loopback access. Later targeted checks do not rewrite raw aggregate data.

### Next bounded delivery: M4 graph/grid shortest paths

After the ragdoll qualification checkpoint, deliver native graph/grid A* before
navmesh baking, crowd motion and authoring. Keep constructors and bounded path
queries pure (**S12.1.1v3 / D7.1.1–D7.1.3 / D7.2.4v4**); scene services later
reuse the same context-free provider through the existing host boundary.
Accept stable node IDs, finite nonnegative costs, filters and explicit search
budgets. Use an admissible metric or a zero heuristic. Distinguish complete,
partial, unreachable and budget-exhausted results, with copied paths and work
counters. Acceptance compares weighted routes to an independent shortest-path
oracle, verifies deterministic ties/filters and separate budget outcomes, and
checks 2D/3D grids including blocked-corner traversal. Pair native provider
checks with interpreter/JIT/AUTO goldens and applicable ownership/effect gates.
Navmeshes, clearance baking, generation-checked rebakes, agents and avoidance
remain separately tracked M4 work; no graph-only capability claims those.

### Final ragdoll aggregate receipts

The frozen Radiant aggregate completes **8102 passes / 140 partials /
414 failures / 2 skips** across 8658 checks. Native Scene3D passes **104/104**,
UI **744/744**, View **255 passes / 1 skip**, page load **111/111**, DOM
**165/165**, and page snapshots **51/51**. Render baseline records **207 passes /
4 partials / 1 skip**. Layout retains the preceding **4725 passes /
136 partials / 413 failures**. The remaining failure is the same CSS-memory
budget lane: `bschool` initial/recascade CSS-role live bytes are **739712**
against **731361** admitted bytes. That harness ran from the isolated workspace
whose `./lambda.exe` resolves to the frozen qualified host; this is a same-host
failure, unlike the older comparison involving the shared root executable.
No layout expectation or memory budget was changed.

The ragdoll core slice is implemented and its focused acceptance is qualified,
with the aggregate and live-ASan limits above retained. The full gaming goal
remains active; M4 graph/grid shortest paths is the next bounded delivery.
Outstanding M2/M3 profiles remain in the delivery table rather than being
silently counted complete. All 25 recorded source/header/test hashes still
match the frozen boundary after qualification. Raw pilot, input, Lambda and
Radiant receipts remain separate under `temp/scene3d-ragdoll/`.

After qualification, a root-cause comment was added at the tested mass-centre
velocity calculation. It changes no executable behavior; the manifest retains
the compiled source hash/copy and records the annotated source separately.

## M4 graph/grid pathfinding delivery — 2026-10-29

The native immutable graph provider and `lambda.navigation` constructors,
validation and bounded path queries now implement the first M4 slice
(**S12.1.1v3 / D5.3.3 / D7.1.1–D7.1.3 / D7.2.4v4**). It owns copied node,
edge and guard data, uses stable IDs and weighted admissible A*, and returns
separate complete, partial, unreachable and budget-exhausted outcomes. The
metric uses a global lower bound on cost per world distance; zero-cost edges
safely reduce it to zero. Both 2D and 3D grids honor costs, group filters and
all intermediate cells of a diagonal, including 3D face/edge clearance cells.
Search, graph size and output ownership have explicit bounded contracts; see
[Navigation](../../doc/Lambda_Navigation.md).

The existing priority queue gained custom comparison, relocation notifications
and indexed key updates, preserving its integer-priority API. This reuses its
heap operations instead of introducing another heap or quantizing costs.
The former four queue tests lived in an unregistered legacy network file; they
were moved into a registered standalone queue suite alongside two new indexed
ordering cases. Native navigation tests use independent Bellman–Ford across
seeded weighted graphs, source-order/ownership controls, fractional decrease
keys, filter/guard budgets and analytic grid geometry. The compiler lives in
LowerCore and its runtime result adapter roots all arguments/builders; no pure
package imports Radiant internals or creates graphics/audio resources.

Focused qualification passes **12/12** native navigation cases and **6/6** queue
cases. Navigation and physics paired goldens pass **57/57** across
interpreter/JIT/AUTO and **38/38** through the instrumented ASan host on
interpreter/JIT (`detect_leaks=0`). Navigation uses forced collection on every
allocation plus freed-memory poisoning, including longer result retention and
Unicode IDs. Physics retains its preceding forced/poisoned controls. Neighbor
checks pass **104/104** native Scene3D cases and **10/10** network scheduling
cases. The final module-boundary check still reports the same six standing
Class-F imports (**140 total vs. 165 baseline**), with no new navigation import
violation.

The first static-library gate found that the new compiler was absent from the
explicit lambda-data/Core source lists. Those two inventories were corrected
in `build_lambda_config.json` and regenerated; gate builds then succeeded.
The raw core aggregate records **6305/6307**, with two HTTP self-spawn cases
failing after the shared `test/test_serve_gtest.exe` disappeared. The isolated
test overlay now supplies every immutable native executable, including files
missing from the shared tree. The exact three HTTP tiers then pass **3/3** on
the same frozen host/binary. This corrects environment setup without altering
a test expectation or HTTP implementation; raw failures remain preserved.

The navigation directory was subsequently added to the explicit Lambda golden
discovery inventory. Exact registered navigation checks are separate from the
raw aggregate above. An initial broad name filter also matched an unrelated
Word navigation fixture under forced-GC settings; that receipt is not used as
navigation qualification. The exact navigation prefix is now used. Release
build/export checks pass, both new goldens pass on the installed root release
host, and the packaged `release/lambda` passes the path fixture from its own
working directory using the packaged navigation module. Artifacts and source
hashes are under `temp/scene3d-navigation/`.

This delivers graph/grid shortest paths only. Scene3D asset reconciliation,
navmesh clearance/baking, generation-checked publication, fair agent jobs,
links/corridors/funnel smoothing and obstacle avoidance remain M4 work.
The broad live `navigation` capability stays false. Outstanding M2/M3 profiles,
M5–M12 and the four Lambda Script KIV scopes remain as previously recorded;
the all-scope goal is active.

The exact registered navigation prefix passes **2/2** on each of interpreter,
JIT and AUTO with forced collection on every allocation and freed-memory
poisoning (**6/6**). This is the new-directory registration receipt; it does not
rewrite the preceding raw aggregate or the overly broad name-filter attempt.
The latter is retained as diagnostic-only data. All raw failed HTTP cases have
successful exact rechecks after fixing the isolated executable tree.

The next M4 slice integrates versioned graph/grid assets with the existing scene
host boundary. Stage a fresh immutable compiler owner for each asset edit;
publish only valid candidates, preserve unaffected paths/clock on unrelated
camera edits, and invalidate jobs by asset generation before releasing their
old graph. Then add the 2D/3D surface/clearance bake, corridor/link/funnel and
agent work from design §5. Native source placement follows §14: reusable
context-free providers stay below runtime/Radiant; Scene3D integration and bake
services live under `radiant/scene3d/`. General gameplay policies remain KIV.

## M4 Scene3D graph/grid asset ownership — implemented, 2026-10-29

**Outcome:** reuse the pure immutable provider through the existing live
`radiant.scene3d` boundary without acquiring a physics world or a GPU context
(**S12.1.1v3 / D4.5.2 / D5.3.3 / D7.1.3 / D7.5.3v2**). Scene resources own
versioned graph/grid assets. Queries return asset generations; stale requests
reject, and explicit current-generation checks detect invalid authored edits.

**Owner and implementation:** `Scene3dEntry` owns navigation separately from
its disposable graphics projection. Direct scene resources accept typed graph/
grid attributes or a `data` map/JSON object. A call-local JSON Input is destroyed
with its pool; the compiler retains copied native data only. Reconciliation
stages all changed assets before retiring old owners. Equivalent compiled data
keeps its generation; explicit revision, identity replacement, removal/re-add or
changed graph data publishes a new generation. A malformed candidate preserves
the whole previous cache and reports rejection. Tick-sampling rejection remains
retryable after the tick, rather than poisoning that authored epoch.

**Acceptance:** graph/grid live package queries match pure-provider results;
repeated queries reuse their compiled owners; camera changes do not recompile
assets; navigation-only edits do not reproject graphics; context loss preserves
CPU graph/generation; template rebuilds retain equivalent graphs; multi-asset
edits commit together or retain the whole prior cache; malformed data, placement,
transform/schema errors and stale generations reject; revision/removal/re-add
invalidates old paths; cache reconciliation leaves the scene clock intact;
reentrant authored edits reject before publication and retry after sampling.

**Verification budget:** focused cache and live-package native cases first;
then the complete native Scene3D suite, graph/queue neighbors, pure navigation
and physics tier/GC controls, source/header/boundary lint, and required Lambda/
Radiant baseline gates on frozen binaries and isolated outputs. Preserve raw
failures and distinguish standing/environmental failures from successful exact
rechecks. Artifacts live under `temp/scene3d-navigation-assets/`.

This milestone does not implement navmesh clearance/baking, surface corridors,
links, asynchronous publication, agents or avoidance. The broad navigation
capability remains false, and the all-scope goal stays active.

The lifecycle review reproduced generation reuse after viewport release: both
the recreated cache and another cache in the same document initially published
generation 1. The regression fails before the repair. Generation allocation now
belongs to the document registry, which survives viewport retirement; staged
candidate publication advances that shared sequence only on commit
(**D4.5.2 / D7.5.3v2**). Focused and final receipts below distinguish this repair
from the earlier frozen aggregate executables.

### Asset milestone qualification

The final lifecycle repair and shared acquisition helper pass **111/111** native
Scene3D cases and all **7/7** navigation cases with every-allocation GC and
freed-memory poisoning. Pure navigation/physics and the new typed template
paired golden pass **60/60** across interpreter/JIT/AUTO and **40/40** through
the instrumented ASan host across interpreter/JIT (`detect_leaks=0`). The
optimized release host passes **20/20**, and its host-export check passes
**284** listed exports. Native graph and queue neighbors pass **12/12** and
**6/6**. The new live package handles JSON and typed assets, stored route
invalidation, template redraws and queries without graphics/physics acquisition.

The frozen broad checkpoint passes Lambda core **6309/6309**. Its required
input companion remains **4562/4589**, with the same 27 failing cases in modular
wiki/markup, script-format round trips and PDF/visual behavior recorded at the
preceding checkpoint. The full Radiant result is **8108 passed, 140 partial,
414 failed, 2 skipped** out of 8664: native Scene3D **110/110** at that checkpoint,
UI **744/744**, viewer **255 passed / 1 skipped**, DOM **165/165**, page loads
**111/111**, snapshots **51/51**, render **207 passed / 4 partial / 1 skipped**.
Layout records **4725 passed / 136 partial / 413 failed**; the remaining failure
is the standing CSS-memory case. These are not green aggregate gates. The
final document-sequence repair is qualified separately by the failing-before
regression, final native **111/111**, forced-GC **7/7**, and current-host checks;
the earlier raw checkpoint is preserved rather than rewritten.

The final strict Radiant boundary DSO rebuild links. The boundary checker still
fails for the same six standing Class-F imports (**140 total vs. 165 baseline**),
with no added navigation import. Dimension lint and diff whitespace checks pass.
Navigation declarations live in the existing `scene3d.hpp`; the header checker
reports no new violation, while its standing **70/24** count remains open. The
previously recorded live-ASan fiber limitation **LR12-43** is not suppressed.

Artifacts: `temp/scene3d-navigation-assets/qualification-lifecycle-final.json`,
`qualified-lifecycle-source/`, `generation-before.json`, final native/GC and
focused receipts, and separate `lambda-results/`, `input-results/` and
`radiant-results/` raw reports. The final release executable is
`temp/scene3d-navigation-assets/lambda-release-final.exe`; the workspace
executable was in use and was not replaced. No test expectation, boundary
allow-list or header cap was weakened.

The next M4 milestone is surface navigation: a shared, normalized closest-point
primitive; immutable triangulated 2D/3D surfaces with slope/area filtering;
polygon corridors and grounded funnel waypoints; genuine radius/clearance
baking; links and generation-checked jobs; then controller-backed agents and
bounded local avoidance. Do not represent portal shortening alone as radius
clearance, or centroid-graph cost as globally optimal Euclidean travel. Reuse the
existing provider, precise result builders and source/pose readers. Gameplay
perception, decision-making, coordination and inspection remain Lambda Script
KIV. All broader M2/M3 profiles and M5–M12 remain in the active all-scope goal.

## M4 point-agent surface component — implemented, 2026-10-29

**Outcome:** supply native walkable-surface connectivity and grounded paths as
prerequisites for clearance baking and controller-backed agents. Preserve the
full M4 acceptance in design §5; a triangulated point-agent profile is an
intermediate component, not completion of clearance/agents or of the goal.

**Owner and approach:** promote the existing normalized triangle closest-point
algorithm into shared numeric helpers, retaining the physics API as a wrapper.
An immutable lower provider owns 2D/3D triangulated surfaces and uses the existing
weighted A* graph for corridors. Native result conversion and Scene3D assets
reuse their existing adapters (**S12.1.1v3 / D5.3.3 / D7.1.1–D7.1.3 /
D7.2.4v4 / D7.5.3v2**). For 3D walking, use an explicit Y-up projection, slope
filtering and height interpolation at crossed portals. Surface-path cost must
identify the centroid graph metric separately from actual waypoint length.

**Acceptance:** analytic planar/turned corridors; a hole and disconnected
islands; sloped/filtered/disabled areas; nearest-surface endpoint projection;
3D height changes retained along straight horizontal segments; weighted route
selection; exact welded adjacency; source ownership, malformed/nonmanifold
geometry and numerical limits; bounded projection/search/funnel work; copied
results and generation checks through pure and live APIs. Positive radius must
ultimately use genuine domain erosion/clearance baking, not portal shortening.
Follow this component with radius/height/step-aware baking, links/jobs and agents.

**Verification budget:** focused native geometry/provider cases and independent
analytic controls while editing; neighboring physics/native navigation cases
for the shared primitive; paired Lambda fixtures across interpreter/JIT/AUTO,
every-allocation GC/poisoning and ASan; focused Scene3D lifecycle integration.
Run required broad gates at a coherent milestone boundary and preserve the
standing aggregate limits. Temporary artifacts remain under
`temp/scene3d-navigation-surfaces/`.

The corridor/straight-path split and clearance-erosion requirements also match
[Detour's query contract](https://recastnav.com/classdtNavMeshQuery.html) and
[Recast's radius/height settings](https://recastnav.com/structrcConfig.html).
These references inform the geometry contract; no vendored implementation is
modified or copied into the repository.

### Surface component implementation and focused evidence

`lib/navigation_mesh.c` now owns welded triangulated 2D/3D surfaces, manifold
adjacency, per-face costs/groups/disabled flags, Y-up slope admission, bounded
nearest-face projection, weighted A* corridors and grounded funnel paths. It
retains height changes at crossed portals and reports actual polyline length
and area-weighted travel cost separately from centroid-graph cost. Partial
paths retain the requested goal projection separately from the reached end.
Search/projection/assembly exhaustion never publishes an unverified movement
path. This is the point-agent surface component; it does not satisfy positive
radius/height/step clearance, links, jobs or agents.

The old physics triangle closest-point implementation moved into
`lib/math_utils.h`; the physics API delegates to that shared primitive. Native
navigation now shares its vector predicates/distance helpers rather than
copying them. The compiler uses one graph/grid/mesh kind table, and the existing
pure/live result adapter handles all three kinds with precise roots
(**D5.3.3 / D7.1.1–D7.1.3 / D7.2.4v4**). Surface assets use the existing atomic
scene cache and document-wide generations; equality includes surface geometry
and area costs even when centroids/graphs are identical. A combined scene
surface-vertex quota bounds retained geometry.

A seeded maze found a real lifting failure: normalized endpoint projection
moved an on-edge point by one ulp, and near-parallel determinant division
amplified that error. The repair classifies parallel/collinear cases with an
arithmetic roundoff bound before division. The failing receipt and diagnostic
are preserved under `temp/scene3d-navigation-surfaces/`; no centroid fallback,
fixture-specific offset or relaxed expected path was added. The independent
grid-connectivity/domain oracle now passes all 40 seeded mazes, with additional
collinear controls at scales `1e-100`, `1` and `1e100`. An explicit maximum-slope/vertical-face control also passes with the existing
admission rule.

Focused current checks pass **27/27** native graph/surface/geometry cases,
**115/115** physics cases, **9/9** live/native navigation cases both normally
and under every-allocation GC/poisoning, and **63/63** paired navigation/physics/
typed-template checks on interpreter/JIT/AUTO. The instrumented ASan host passes
**42/42** on interpreter/JIT (`detect_leaks=0`). The typed template golden was
extended only with its newly authored mesh asset. Broad gates and release
receipts follow after completion; source/binary hashes and isolated raw outputs
are under `temp/scene3d-navigation-surfaces/qualification.json`.

For the next clearance/bake component, the existing CPU geometry reader is
currently coupled to `Scene3dBuild`/the projection pool. Extract a real CPU
reader/collector with caller-owned storage and located diagnostics; do not
construct a fake render entry or require a GPU projection to acquire geometry.
Collect explicitly selected world-transformed raw geometry, including obstacles
and ceilings, rather than assuming admitted point-mesh faces alone prove
clearance. Use the existing `lib/thread_pool.h` submission API for immutable
worker inputs, document-owned cancellation, and generation-checked publication
on the owner thread. Empty bakes must be valid empty domains, not fabricated
disabled triangles. Clearance erosion must cover radius, headroom and step
constraints; source/profile hashes and grid precision belong in the saved bake
metadata. Off-mesh links, dirty jobs, controller-backed agents, avoidance and
the remaining native gaming roadmap stay active.

### Surface component aggregate checkpoint

The same frozen source checkpoint passes Lambda runtime **6310/6310**. The
input companion remains **4562/4589** with the recorded 27 markup/round-trip/
PDF failures. Full Radiant records **8111 passed, 140 partial, 414 failed,
2 skipped** out of 8667: native Scene3D **113/113**, UI **744/744**, viewer
**255 passed / 1 skipped**, DOM **165/165**, page loads **111/111**, snapshots
**51/51**, render **207 passed / 4 partial / 1 skipped**. Layout remains
**4725 passed / 136 partial / 413 failed**, plus the standing CSS-memory failure.
These broad input/Radiant gates remain non-green; raw reports are preserved.
The optimized standalone release passes **21/21** focused goldens and **284**
listed host exports. It is available at
`temp/scene3d-navigation-surfaces/lambda-surface-release.exe`; the workspace
binary was not replaced.

The strict lib/Core/IO/Radiant boundary images build. The module checker retains
the same six standing Class-F failures (**140 imports vs. 165 baseline**), with
no added navigation import. Dimension lint and diff whitespace pass. No new
Radiant header was added; the header checker still reports its standing
**70/24** count. Live-ASan qualification retains the previously recorded
**LR12-43** fiber limitation; the passing 42-case ASan matrix does not claim
coverage beyond it. No expectation, allow-list or cap was weakened.

This closes the point-agent surface/corridor component only. Positive-radius
clearance, headroom/step baking, source collection, links, jobs, agents and
avoidance remain M4 requirements. The broad `navigation` capability remains
false; `navigation_surface` reports this implemented component. The all-scope
goal, broader M2/M3 profiles and M5–M12 remain active, and the four gameplay
policy/inspection scopes remain Lambda Script KIV.

## M4 clearance baking and publication — active milestone, 2026-10-29

The preceding turn delivered the surface component and verified it; it was
progress. This milestone now implements the actual producer for radius/height/
step-compatible navigation, with explicit source selection and immutable bake
inputs (**D4.5.2 / D5.3.3 / D7.1.1–D7.1.3 / D7.5.3v2**). The all-scope goal and
remaining M4/M5–M12 requirements are unchanged.

First extract the existing geometry decoder into a real CPU source interface,
then collect explicitly selected, world-transformed source meshes with copied
provenance. Build a bounded raster/spans representation with conservative floor
coverage and obstruction/headroom tests, erode the navigable domain for the
agent footprint, validate steps, and emit ordinary surface connectivity with
bake metadata. Empty bakes publish valid empty domains. Source/profile identity
and document generation checks must prevent stale job publication.

Acceptance includes a floor with a hole, narrow/wide passages at positive
radius, low/high ceilings, traversable/blocked steps and stacked levels; source
transforms/reference reuse, source edits during a job, cancellation, empty
results and metadata/reopen. Repeated camera edits must not bake again. Native
geometry/kernel controls and focused pure/live ownership tests run while
editing; required broad gates run at the milestone boundary. Artifacts use
`temp/scene3d-navigation-bake/`; existing unrelated aggregate limits remain
visible rather than being masked.


### Clearance producer/component checkpoint — 2026-10-29

Implemented the CPU geometry interface and conservative cell producer inside the
active bake/publication milestone. This is progress, not closure of M4 or of the
all-scope goal (**D4.5.2 / D5.3.3 / D7.1.1–D7.1.3 / D7.2.4v4 / D7.5.3v2**).

- The renderer now uses the extracted CPU geometry decoder. Explicit static
  mesh selections copy referenced/inline geometry in authored world coordinates,
  with a combined unique-vertex/triangle budget and canonical SHA256 source hash.
  Collection does not acquire a Scene3D runtime, projection or GPU resources.
  Instanced/skinned/morphed source poses require a later explicit snapshot
  profile; unsupported inputs reject without replacing the caller's snapshot.
- `lib/navigation_bake.cpp` clips triangles into bounded columns, merges vertical
  spans, verifies full supporting-floor union with an edge-event sweep, checks
  quantized headroom/steps and erodes full output cells for the horizontal disk.
  Nonwalkable geometry participates in obstruction, closed props include their
  bottom faces, and stacked floors remain separate. The radius neighborhood
  reads immutable candidates, so updated masks/costs cannot cascade by iteration
  order. Empty inputs/eroded results are real empty domains.
- `lambda.navigation.bake` describes this pure CPU producer. The existing path
  provider returns compatible-profile cell routes, projection/assembly budgets,
  partial endpoints and the baked coordinate/profile metadata. Scene3D
  `navigation-bake` resources cache native owners, retain camera-independent
  generations, version profile changes and retain prior owners on invalid edits.
  The separate point-agent mesh also now accepts explicitly empty paired arrays.
- The first representation deliberately retains four-neighbor cells and quantized
  floor levels. It is conservative and can need finer resolution for passages;
  it does not claim polygonized/funnel paths on the original geometry. Physics
  still resolves actual support and motion. The public capability is
  `navigation_bake_cells`; broad `navigation` stays false.

Focused evidence under `temp/scene3d-navigation-bake/`: the native provider's
41 cases include a seeded independent whole-rectangle clearance oracle (32 maps,
four radii), tiny interior holes/duplicate overlaps, ceilings, physical risers,
stacked levels, profile admission, empty domains and work/cancellation failure.
All 117 native Scene3D cases and 115 physics cases pass after decoder extraction.
Four navigation goldens pass in all three tiers under every-allocation GC and
poisoning (12/12); the ASan host passes interpreter/JIT (8/8). A separate native
ASan/UBSan harness instruments the navigation/mesh/bake/queue algorithms and
runs the same 41 cases; utility allocation/logging helpers and GTest remain
uninstrumented. Leak detection is unavailable on this macOS platform and is
explicitly disabled. These are correctness/lifetime checks, not benchmarks.

The final metadata, empty-cache and rendered/context-recovery checks also pass.
The frozen focused checkpoint is `temp/scene3d-navigation-bake/qualification-focus.json`
with the matching source/binary hashes and logs. Export validation passes for
284 host symbols, dimension lint and whitespace checks pass, and no new Radiant
header is introduced. Header lint retains the standing 70/24 inventory failure.
Strict boundary images build; the checker retains exactly the same six standing
Class-F imports (140 versus baseline 165), with no exception changes. Required
aggregate qualification remains due at the full bake/publication milestone
boundary. The previous surface milestone's raw aggregate failures remain
recorded above; no new aggregate result is claimed.

Next work remains the immutable background-job/publication adapter over selected
scene sources, settings/source hashes in saved compiled assets, stale/cancelled
job rejection, polygonization/links, then agents and avoidance. Main-thread
source collection exists internally but is not yet exposed as a background bake
operation. M2/M3 wider profiles and M5–M12 are still outstanding. All four
script-policy/inspection scopes remain KIV.

### Background bake publication — active implementation, 2026-10-29

The preceding goal turn was progress: it delivered and focused-qualified the
clearance cell producer. This turn adds source-descriptor assets and native
background jobs with copied geometry/settings, cooperative cancellation,
main-owner generation/provenance validation and explicit publication
(**D4.5.2 / D5.3.3 / D7.1.1–D7.1.3 / D7.5.3v2**). The finite acceptance cases
are start/poll/publish against a paused viewport, source/settings edits before
publication, superseding jobs, cancellation, empty results, template/context
lifetime and saved metadata/reopen. Workers receive no DOM/Item/reader pointers.
Source fingerprint checks exclude the camera; SHA256 snapshots and source
references are checked again before publication. Artifacts use
`temp/scene3d-navigation-jobs/`. Required aggregate qualification follows this
bake/publication boundary; M4 polygonization/links/agents and all later stages
remain part of the active goal.


### Background source jobs and saved cells — implementation checkpoint, 2026-10-29

Source-selected `navigation-bake` descriptors now support explicit native
start/poll/publish/cancel/release/export operations through
`lambda.navigation.scene`. This completes the immutable job/publication and
saved-cell part of the active clearance milestone; polygonization, links,
agents and avoidance remain M4 work (**D4.5.2 / D5.3.3 / D7.1.1–D7.1.3 /
D7.2.4v4 / D7.5.3v2**).

- Each viewport lazily owns one worker. At most eight retained jobs and the
  combined selected-triangle quota bound copied inputs. Worker state contains
  native geometry/settings/results only; progress uses atomics and result
  publication uses release/acquire. Cancellation and joining precede teardown.
  Export/release keep immutable asset ownership independent from the job.
- Publication runs on the main owner outside tick sampling. It verifies the
  actual root, target generation, latest request, settings hash, fresh source
  hash and selected node identities before swapping one compiled owner. Source
  edits, supersession and actual root replacement reject old unpublished work.
  Camera edits/context recovery preserve navigation. Equivalent typed redraws
  retain pending work and previously published generations.
- Version-1 compiled-cell maps preserve Y-up coordinates, source/settings
  SHA256, complete clearance settings and exact slope radians. Loading checks
  canonical cell order, bounds, headroom, masks, costs and budgets, then uses
  the shared graph builder. Empty arrays remain empty domains. Settings hashes
  normalize signed zero for JSON round trips. Stored artifacts retain source
  provenance; they do not sample a newly edited render scene.
- Transport DTOs now use the existing ordinary-map builder, exposed through
  `RootedValueBuilder::DATA_MAP`. The previous VMap output was unreadable by
  LowerCore's MarkReader and serialized an empty bake as `{}` at the JS
  membrane. The correction shares rooted map construction rather than adding
  another reader representation (**S12.1.1v3 / D2.11.4 / D5.3.3**).
- Typed fixture imports exposed a native-export loader defect: a resident T0
  module had no complete MIR export image, and an existing hot satellite was
  mistaken for one. Export compilation now addresses the initialized T0 slab,
  appends this image's property-key suffix and retains any published satellite
  as a private Script-owned image. It neither reruns the initializer nor
  promotes this execution-local image into the shared cache. Cold exports,
  retained old native callbacks, T0 callbacks and repeat imports are covered
  (**D8.1.1v18 / D8.5.1v7 / D7.2.2**).

Frozen milestone evidence is under `temp/scene3d-navigation-jobs/`, with source,
host and 132 runner hashes in `qualification-gates.json`. All 124 native Scene3D
cases, 115 physics cases, 42 navigation provider cases and nine digest cases
pass. Seven live job cases also pass every-allocation GC/poisoning; native
resident-export regressions pass repeated fresh runtimes and forced GC. The
42-script matrix passes debug interpreter/JIT/AUTO, full-ASan interpreter/JIT
and optimized AUTO, all with allocation-GC/poisoning. These are correctness
checks, not performance evidence.

A selective ASan/UBSan build instruments the navigation algorithms, canonical
digest fields, LowerCore adapter, runtime navigation adapter, Scene3D source/job
owners and native harness. Its 11 bake/source/publication/teardown cases pass;
the independently instrumented provider also passes all 42 cases. Other runtime,
rendering and utility archive objects remain uninstrumented in this selective
lane. Leak detection is unavailable on this macOS platform and disabled. The
previous whole-live-host LR12-43 fiber sanitizer limit is not claimed resolved.

The full Lambda baseline passes 6,317/6,317. Input remains 4,562/4,589 with exactly
the same 27 named failures as the preceding checkpoint. The completed Radiant
aggregate records 8,122 pass / 140 partial / 414 fail / 2 skip (8,678 total),
with the same reported failure names and totals as the previous surface checkpoint.
Scene3D passes 124/124 inside the aggregate, and UI automation passes 744/744.
The 413 layout failures and one CSS-cascade-memory failure remain qualification
limits; no assertions, masks or goldens were relaxed. Host exports pass
284 symbols; dimension lint and whitespace pass. No new Radiant header is
introduced; header lint retains its standing 70/24 inventory failure. Strict
boundary images build after declaring the core image's crypto link dependency
for compiled-bake fingerprints; the checker still reports the same six standing
Class-F imports (140 versus baseline 165), with no ratchet exception changes.

The full goal remains active. Remaining clearance work is polygonized surface
connectivity/grounded funnel routes and links, followed by native agent movement
and bounded avoidance. Wider M2 CCD/M3 movement profiles and M5–M12 remain
outstanding; all four gameplay-policy/inspection scopes remain Lambda Script
KIV.


### Polygonized bake regions — active implementation, 2026-10-29

The previous turn was progress: background publication, saved cells and resident
native-export lifetimes are implemented, with focused and Lambda gates passing;
the Radiant aggregate subsequently completed with unchanged named failures
(recorded above). Its frozen runners and source receipt remain under `temp/scene3d-navigation-jobs/` while this independent
M4 delivery starts (**D4.5.2 / D5.3.3 / D7.1.1–D7.1.3 / D7.5.3v2**).

Add an explicit `representation:"polygons"` bake profile. Partition the admitted
cell domain into deterministic convex rectangles with matching floor/ceiling,
area cost and groups; derive shared portals including partial boundaries, and
route across the resulting region graph. Reuse the existing numerical funnel
and grounded-waypoint assembly. Step portals retain departure/arrival heights;
filters, layered surfaces, partial endpoints and projection/search/assembly
budgets remain explicit. Saved cells reconstruct the same selected profile.
The existing cell profile keeps its wire fingerprint and semantics.

Acceptance: an open floor merges to one region and routes directly; an L-shaped
floor and hole route remain inside the eroded domain; T-junction portals join
unequal rectangles; area/filter boundaries remain distinct; stacks stay separate
and admitted steps retain both heights. Independent sampled-domain controls,
profile/empty/import equivalence, cancellation and live/save/reopen checks
challenge the implementation. Focused native and pure/live checks run while
editing; qualification follows the finite region/portal milestone. Links,
agents/avoidance, remaining M2/M3 profiles and M5–M12 retain their full scope.
Artifacts use `temp/scene3d-navigation-regions/`.


### Convex-region producer and portal routes — focused checkpoint, 2026-10-29

The explicit `representation:"polygons"` profile is implemented over the same
conservative clearance cells. This is progress toward M4, not closure of M4 or
of the full goal (**D4.5.2 / D5.3.3 / D7.1.1–D7.1.3 / D7.2.4v4 /
D7.5.3v2**).

- A deterministic rectangle partition preserves floor, ceiling, cost and group
  boundaries. Directed shared intervals join unequal region edges; stacked
  floors remain separate, and the original step/headroom admission still owns
  connectivity. The selected profile builds one primary graph, so scene graph
  quotas count region nodes rather than backing cell records.
- The triangle-mesh funnel/grounded assembler is now shared by both producers.
  Region portals provide separate departure/arrival heights, preserving explicit
  vertical steps. Portal lookup and funnel/crossing work share the assembly
  budget; partial endpoints and failure/exhaustion remain distinct. Graph metric
  and weighted grounded travel remain separate quantities. The chosen centroid
  corridor does not promise globally minimum weighted geometric travel.
- Pure/live profile maps, result domain/region IDs, saved-cell reconstruction,
  generation changes and `navigation_bake_polygons` expose the feature. The cell
  profile remains available and retains its wire hash; a frozen previous host
  and the new validated host produce the same legacy-profile fingerprint.
  Background jobs retain the original immutable inputs/provenance checks.
- A new invalid-tag control exposed a C++ enum-load violation under UBSan before
  the native validation check could reject it. The descriptor now stores a
  `uint32_t` tag until validation. The invalid value is still tested and rejected;
  no sanitizer check was suppressed. A live test also initially used the wrong
  polling global; it now follows the existing fixture's `jobToken` contract.

The frozen validated hosts/runners, source hashes and copied source receipt are
in `temp/scene3d-navigation-regions/qualification-gates.json` and
`qualified-sources/`. All 126 native Scene3D cases and all 51 provider cases pass,
as do 115 physics controls and nine digest controls. Nine live cell/polygon job
cases pass every-allocation GC/poisoning. The 48-script matrix passes debug
interpreter/JIT/AUTO, full-ASan interpreter/JIT and optimized AUTO, with forced
GC/poisoning. Numeric/domain controls include concavity, holes, partial-edge
portals, headroom/area separation, zero costs, explicit steps and stacks,
source reordering and scales from `1e-100` to `1e100`. Forty-eight seeded domains
agree with an independent four-neighbor reachability oracle and sampled route
containment. These are correctness checks, not benchmarks.

Selective ASan/UBSan instruments the provider and shared funnel, and passes all
51 cases, including invalid-tag rejection and late work-budget failure. The
instrumented source/runtime/job-owner lane passes 13 publication, reload,
recovery and teardown cases under forced GC. Other ordinary runtime/rendering
archive objects remain uninstrumented in that lane; leak detection is disabled
because it is unavailable on this macOS platform. The standing whole-live-host
fiber sanitizer limit is unchanged.

Input qualification remains 4,562/4,589, with exactly the same 27 named failures
as the completed background-job checkpoint. The rebuilt Lambda and Radiant
aggregate gates are running sequentially. Host exports pass 284 symbols,
dimension lint and whitespace pass, and strict boundary images build. The
checker still reports the same six standing Class-F imports (140 versus
baseline 165), without exception changes. No new Radiant header was introduced;
the standing header inventory limit remains unrelated debt.

Next M4 work remains explicit links, dirty-tile updates, agent/controller motion,
bounded avoidance and navigation authoring/inspection overlays. Wider M2/M3
profiles and M5–M12 remain outstanding. The four gameplay-policy/inspection
scopes remain Lambda Script KIV; the full goal stays active.


#### Region storage accounting correction — 2026-10-29

A review control showed that counting only merged region nodes let three
32,768-cell bakes enter a cache whose prior combined storage limit was 65,536.
The new regression failed before the repair. Scene statistics now retain actual
graph nodes separately from `storage_nodes`: bake cells contribute their backing
count, and other assets contribute graph nodes. One shared admission helper
checks graph, vertex and storage limits in both staged reconciliation and
background publication (**D4.5.2 / D5.3.3 / D7.1.1–D7.1.3**).

The corrected cases retain the old cache/generations after overflow, reject a
ready job whose backing cells exceed the combined limit, and allow that same
immutable job to publish after another asset is removed. Four live/cache
polygon cases pass. The shared test owner is extracted from the existing native
navigation owners rather than duplicated for the third case.

The earlier validated-host aggregates completed before this correction:
Lambda 6,318/6,318; Radiant 8,228 pass / 429 partial / 21 fail / 2 skip
(8,680 total). The reported failure categories are a subset of the earlier
checkpoint, but the large layout status change is not attributed to navigation;
the shared worktree changed between checkpoints. Raw results are retained in
`qualification-validated.json` and the `*-validated-results/` folders. The final
quota-corrected source is being rebuilt and requalified; those previous results
are not presented as proof for the corrected binary. Provider algorithms and
wire formats are unchanged. The full goal and all remaining scopes stay active.


#### Final quota-corrected focused receipt — 2026-10-29

`qualification-gates.json` now selects the unique `lambda-regions-*-quota`
hosts and `aggregate-quota-bin/` runners; `quota-sources/` preserves the checked
source, with additional workspace hashes for concurrent edits. The current
baseline inventory was re-read: 72 input, 31 Lambda and 29 Radiant runner entries,
with no additions or removals from the frozen selection.

All 128 native Scene3D cases, 51 navigation provider cases, 115 physics controls
and nine digest controls pass. Eleven live cell/polygon cases pass forced GC;
the 48-script debug/ASan/optimized matrix passes again. The instrumented
source/job-owner lane now passes all 15 cases, including backing-storage
admission and publication retry. Provider source hashes are unchanged from the
51-case ASan/UBSan receipt; `provider-source-comparison.json` records the check.
Host exports (284), dimension lint and whitespace pass; strict boundary images
build and the checker retains the same six standing deferred imports. Header
inventory remains 70/24 with no new Radiant header.

The final input/Lambda/Radiant gates run sequentially against these artifacts;
`aggregate-quota-run.json` accumulates their exits and the `*-results/` folders
contain the current receipts. Final aggregate results still need collection.
The feature's focused acceptance is complete; broader qualification and the
remaining M4/M2/M3/M5–M12 scope are still part of the active full goal.


## M4 explicit surface links — active implementation, 2026-10-29

The preceding goal turn was progress: polygon regions, grounded portal routes
and backing-storage admission were implemented and focused-qualified. Its final
aggregate pipeline remains a separately frozen process. This delivery adds
explicit links to immutable 2D/3D surface assets and baked domains, preserving
owned native inputs and generation-checked scene publication
(**D4.5.2 / D5.3.3 / D7.1.1–D7.1.3 / D7.2.4v4 / D7.5.3v2**).

Reuse one native surface view for projection, clearance admission and grounded
subcorridor assembly. Link marker nodes distinguish parallel authored actions
without duplicating the surface/funnel algorithms. Markers cannot become
partial route endpoints. Traversals retain IDs, projected endpoints, direction,
cost, clearance and opaque action/duration metadata; ordinary travel stays
on the surface. The path service describes a traversal; agent execution follows
in the next delivery and arbitrary gameplay callbacks remain outside native
traversal/solving.

Acceptance includes disconnected rooms and 2D floors, one-way/reverse and
parallel links, filters/disabled states, width/height admission, endpoint snap
limits, link projection/search/assembly budgets, retained owner teardown,
partial endpoints, zero-length/zero-cost actions, save/reopen, and live edits
that update link connectivity without rebaking unchanged geometry. Native
controls and pure/live goldens run while editing; required gates qualify the
finite link milestone. Existing processes/fixtures stay frozen until their
qualification completes. Artifacts use `temp/scene3d-navigation-links/`.


#### Previous polygon aggregate receipt collected

The frozen quota-corrected pipeline completed: input 4,562/4,589 (27 failures),
Lambda 6,316/6,319 (three failures), and Radiant 8,230 pass / 429 partial /
21 fail / two skip. Radiant failures remain the cascade-memory case and
20 writing-mode cases. The three Lambda failures are the two validator fixtures
`element_content_pattern` and `ledger_literal_contracts`, with the latter also
repeated by `Tune27FixturesHonorTierSupport`. A fresh control host built with the
concurrent validator ownership fix passes both fixtures in interpreter/JIT/auto
(six executions); that control does not replace a full aggregate receipt.
Evidence is in `temp/scene3d-navigation-regions/qualification-gates.json` and
`temp/scene3d-navigation-links/literal-validation-control.json`. No fixture
expectation was weakened. The current link milestone rebuilds all selected
runners and reruns the required gates against its own frozen host.


#### Explicit links: focused receipt and shared ownership repair

The native provider now joins disconnected mesh/cell/region surfaces through
owned, bounded explicit links. Live link-only edits reuse published geometry;
invalid attachments preserve the working generation, changed action metadata
stales pending jobs, and exports retain the job's captured definitions.
`navigation_links` is advertised independently of the still-incomplete broad
`navigation` capability. The public API/clearance/work contracts are documented
in [Navigation](../../doc/Lambda_Navigation.md).

The typed cell-bake control exposed a missing segment entry-area ID in the
shared route assembler. It is now supplied from the borrowed corridor, and the
native clearance control covers both cell and polygon representations. A
zero-snap mesh control also preserves exactly on-surface points without
accepting its one-ulp off-plane neighbor.

Forced GC exposed a shared JSON ownership defect: `JSON.parse` returned
Input-arena containers that did not trace later GC-owned replacement properties
or elements. It now uses the existing `fn_mutable_value` deep owner conversion
(**D5.3.3**), preserving branded shapes and arrays. The AST regression failed
before this change; both AST/MIR now pass with collection/poisoning on every
allocation. An independent full-host debug/ASan/optimized matrix passes all six
executions. The regression installs its own collection schedule. A format
round-trip harness also now honors the shared runner's executable override
instead of reaching the missing checkout executable. Assertions/fixtures stay
unchanged.

Focused qualification: 134 native Scene3D cases and 61 navigation provider
cases pass; all 54 pure navigation/typed-fixture executions pass across
interpreter/JIT/auto and debug/ASan/optimized hosts. The provider ASan/UBSan
lane passes all 61. The selective
instrumented native/provider/LowerCore/runtime/JSON/source/job-owner lane
passes all 28 navigation cases with forced GC. Other ordinary runtime and
rendering archive objects remain uninstrumented in that lane; the separate
full ASan host covers the shared JSON owner conversion. Leak detection is
disabled on this macOS platform. Host exports pass 284 symbols; dimension lint
and whitespace pass. Strict boundary images build; the checker retains the
same six standing deferred Class-F imports (140 versus baseline 165).

`temp/scene3d-navigation-links/qualification-json-gates.json` freezes the
post-repair hosts, source hashes and 132 native runners. Required aggregate
qualification is still pending for this final generation. Earlier pre-repair
receipts and diagnostics remain separate; they are not transferred to this
binary. The full native gaming goal remains active.

### Next bounded M4 delivery: native agents and actual movement

Add one scene-owned agent service before animation and physics, following
design §3.2. Reuse the
compiled surface/link query provider and the existing sampled capsule
controller; navigation requests desired motion and physics reports achieved
motion. Agent state owns its target, copied corridor/waypoints, asset/path
generations, speed/acceleration limits and repath policy. Fair scheduling shares
a per-tick search budget. No script callback runs inside navigation or solving
(**D4.5.2 / D5.3.3 / D7.1.1–D7.1.3 / D7.5.3v2**).

Acceptance: a capsule follows a route around a hole; incompatible clearance
rejects attachment; changed/deleted assets invalidate paths before another
movement command; target changes, cancellation, pause/step, equivalent
templates and context recovery preserve the declared owners. Traversal links
produce an explicit action state/event and cannot silently teleport a body.
Controller contacts/achieved displacement feed bounded stuck/repath behavior.
Add deterministic local disc-neighbor avoidance through a reusable spatial
index, with explicit work/neighbor limits, crossing-agent and narrow-passage
yield controls. Yield does not claim deadlock-free crowd planning; gameplay
task/formation/reservation policy remains Lambda Script KIV.

Use native numeric/oracle and collision controls while editing, then one live
owner/GC lane and required aggregate gates after the finite acceptance cases
pass. Dirty tiles and authoring overlays remain subsequent M4 work; the wider
M2/M3 profiles and M5–M12 stay on the active full-goal checklist.


#### Final link qualification checkpoint

The pre-JSON-repair aggregate completed: Radiant 8,236 pass / 429 partial /
21 fail / two skip, with the same cascade-memory/writing-mode failure
categories. Its Lambda result is 6,318/6,320, failing the concurrently changing
chart dashboard/startup-geometry fixtures; the earlier validator fixture
failures are absent. Input is 4,533/4,591, including format round-trip process
failures caused by the missing checkout executable. These receipts are kept
under `*-results/`; they are not final post-repair qualification.

The final sequential input/Lambda/Radiant pipeline now uses
`qualification-json-gates.json`, `aggregate-json-bin/`, the unique
`lambda-links-*-json-final` hosts and `*-json-results/`. Its progress/results
are `aggregate-json-progress.log` / `aggregate-json-run.json`. The 54-script
post-repair matrix, 28-case instrumented owner lane, six-execution full-host
JSON matrix and 284-symbol optimized export check all pass. Final aggregate
results still need collection before the next implementation milestone.


## M4 agent movement: active implementation

The previous goal turn changed authoritative state: explicit links and native
ownership tests were added, and the JSON parser owner conversion was repaired.
The final link aggregate remains a verified live process while this delivery
implements its next dependency. Begin with a reusable native route follower:
copy verified route/action data; use actual achieved positions for progress;
bound steering/waypoint work; stop on stale generations, off-path motion and
stalled progress; require a generation-checked physical-arrival acknowledgement
for a link. Then bind scene-owned targets and capsule movement through the
fixed-stage coordinator. No autonomous gameplay action/goal selection is added
(**D4.5.2 / D5.3.3 / D7.1.1–D7.1.3 / D7.5.3v2**). Artifacts use
`temp/scene3d-navigation-agents/`. The existing finite acceptance and remaining
M2–M12 checklist are unchanged.


#### Explicit links: final aggregate collected

The post-repair frozen generation completed all required aggregate runs:
Lambda **6,321/6,321**; input **4,564/4,591** (27 failures); Radiant **8,236
pass / 429 partial / 21 fail / two skip** (8,688 total). The input failures
are the wiki formatter/modular cases, five PDF/render cases and twelve
AsciiDoc/man/Typst round-trip cases. The round-trip runner now uses its frozen
host, so its remaining failures show document differences instead of missing
process errors; mail/calendar/card routes run successfully. Radiant retains
the cascade-memory case and 20 writing-mode failures. No assertion or baseline
expectation was weakened. The navigation/JSON-focused lanes remain passing.

Evidence: `temp/scene3d-navigation-links/aggregate-json-run.json`,
`qualification-json-gates.json`, `*-json-results/test_summary.json` and
`final-json-goldens.json`. This closes the explicit-link delivery with those
aggregate qualification limits. Agent/controller work uses a later source
generation; these receipts are not evidence for its new code. The full goal
and remaining native gaming scopes remain active.


### Agent/controller foundations checkpoint

`lib/navigation_agent.cpp` now owns copied verified waypoint/action routes and
tracks progress only from achieved foot positions. Steering has speed/ramp
limits, explicit arrival/deviation thresholds, a per-update waypoint budget,
cumulative progress timeout and generation invalidation. Partial routes end
without claiming target arrival. Link actions stop and require a matching
path generation/ID plus an achieved arrival position before continuing; even
adjacent zero-length actions remain separate. Ten native follower cases and
all 71 provider cases pass, including the selective provider ASan/UBSan lane.

Pure script queries and the upcoming scene agent service now share LowerCore
query parsing and native mesh/bake/link dispatch. All 21 navigation golden
executions pass across interpreter/JIT/auto with forced GC. The shared capsule
profile reader uses the controller's existing source admission; it reports
achieved body/collider frames without consuming queued targets. The native
scene adapter reuses the promoted movement parser and ordinary target commit
without allocating script values in a tick (**D7.1.1–D7.1.3 / D7.2.4v4**).
All 117 physics controls and the new scene adapter case pass.

The coordinator now has a navigation phase before animation/physics. Its two
new ordering/fixed-step cases and three existing runtime controls pass. Two
further regressions exposed missing source admission: a direct fixed step
ignored changed authored velocity, and a bad collider edit still advanced the
clock/world. Both fail on the recorded predecessor. Main-owner source
preparation is being moved before tick sampling, with repairable rejection
and no callback into script (**S12.1.5v2 / D4.5.2 / D5.3.3**). This is a
prerequisite for stale-path safety, not a complete scene agent service.

Scene targets/fair query scheduling, controller-backed following, path events,
repath/lifecycle behavior and avoidance still need integration and qualification.
The `navigation_agents` capability is not advertised. The full goal remains
active; these focused foundations do not close M4.


### Agent foundations: verified source-admission checkpoint

The two failing fixed-tick regressions now pass. The scene entry binds a
main-owner admission callback, and both automatic advance and explicit step
prepare authored sources before sampling. Rejected edits leave the old
clock/world repairable. Scheduler changes during admission do not cancel the
callback's own driver. A coordinated physics batch also prevents admission
from deleting the owner whose public method is still active; a structural edit
can be admitted after that batch returns. This follows **S12.1.5v2 / D4.5.2 /
D5.3.3**, without revising a formal ruling.

Focused evidence: all **141** native Scene3D cases, **71** navigation provider
cases and **117** physics controls pass. Selective ASan/UBSan passes the 71
provider and 117 physics cases. The instrumented coordinator/scene/physics
owners, LowerCore queries, native kernels and harness pass **19** source,
controller, reentrant and ragdoll controls with forced GC. Other ordinary
runtime/rendering archive objects in that owner lane remain uninstrumented;
leak detection is disabled on this platform. The unique foundation host passes
**27** navigation/typed-fixture golden executions under interpreter/JIT/auto and
forced GC. Dimension lint, whitespace and the 284-symbol host export check pass.

Strict boundary images build; the checker still reports the same six standing
Class-F imports (140 versus baseline 165), with no exception changes. No new
Radiant header was introduced.

`temp/scene3d-navigation-agents/foundation-checkpoint.json` records source and
artifact hashes with a local source snapshot; logs referenced there contain the
receipts. This is an intermediate M4 dependency checkpoint. The preceding
link milestone's aggregate limits remain visible; the full agent acceptance
and its required aggregate gates have not been claimed complete.

Next implement scene-owned target/path state in the existing navigation owner,
using the new native follower and character adapter. Retain separate agent
command and asset/path generations, body node refs and a physics-world identity
across reset/replacement. The existing physics presentation generation changes
on every tick/command and cannot stand in for world identity. Derive the Y-up
foot anchor from the achieved capsule frame; clearance must cover collider
radius/height plus controller skin, slope and step ability. Constrain motion
to the navigable domain before issuing controller targets; preferred velocity
alone does not prove a step stays on a walkable surface. Add fair bounded path
work, link action state/events, lifecycle/repath behavior and then shared local
avoidance. Scene playback must respect authored paused physics and explicit
coordinator stepping. The gameplay policy/inspection scopes remain script KIV.


## M4 scene agents: surface motion and live targets — active

The previous goal turn was progress: native route following, capsule adapters
and pre-sampling source admission landed with focused evidence. This delivery
adds bounded connected-face motion before scene agents consume preferred
velocity. Reuse the surface view, graph admission and shared grounded waypoint
appender; walk ordinary polygon adjacency with a cached start face instead of
rebuilding an A* search every tick. Stop at disconnected/filtered boundaries,
report work exhaustion separately, preserve slope/floor-height changes, and
never turn an explicit link into an implicit body teleport. Vertex crossings
need a bounded face-fan search so an unlucky first neighbor cannot hide an
admitted continuation. Then integrate scene-owned targets, path versions,
controller feedback, events and fair query scheduling. Native ownership and
effect contracts remain **D4.5.2 / D5.3.3 / D7.1.1–D7.1.3 / D7.2.4v4 /
D7.5.3v2**. The full native gaming scope and script KIV policy scopes remain
unchanged.


### Wrap-up checkpoint: bounded surface motion and world identity

The user requested wrapping up before complete scene-agent integration. Native
surface motion now follows connected convex faces using cached face hints,
shared graph admission and grounded waypoint assembly. It stops at holes,
disconnected/filtered boundaries and explicit work limits; it retains slope
and floor-height changes and never follows explicit links implicitly. The
vertex-fan traversal searches beyond an admitted dead end. All **79** provider
cases pass, including the instrumented ASan/UBSan lane and a seeded independent
rectangle-interval oracle.

Physics worlds now have document-owned monotonic identities independent of
presentation/tick generation. Ordinary ticks and controller targets retain
identity; discontinuous edits, reset/replacement and adopted ragdoll replay
change it. Rejected edits retain the working identity. Nine focused native
identity/coordinator/character controls pass. All **143** native Scene3D
cases pass; the final log is `motion-worlds-full-scene.log`. Whitespace
checks pass. The preceding milestone's aggregate and boundary limits remain
visible; these checks do not claim complete agent or full gaming qualification.

Current source/artifact hashes and receipts are recorded in
`temp/scene3d-navigation-agents/motion-world-identity-checkpoint.json`.
No scene-agent target service or avoidance capability is advertised yet.
Next resume scene-owned targets/path versions, body refs and world identities,
fair path scheduling, controller feedback and path/link events using these
verified native helpers. Retain the remaining M4 dirty-tile/authoring work,
wider M2/M3 profiles and M5–M12. The four gameplay-policy/inspection scopes
remain Lambda Script KIV. The full objective is unfinished. Work stops here
at the user's wrap-up request; the goal service reports no active goal for
this thread.

## M4 bounded baked-surface agents — implementation resumed

Deliver named, document-owned capsule agents through the existing navigation
command boundary, sharing the native follower, surface walker and character
controller. Preserve **S12.1.1v3 / S12.1.5v2 / D4.5.2 / D5.3.3 / D7.5.3v2**.
The admitted profile is a Y-up capsule on a clearance-baked 3D domain. Keep
script policy and explicit link execution outside the native solver.

Acceptance: explicit fixed ticks move a capsule toward a target and report
arrival from achieved positions; paused physics does not acquire an automatic
navigation clock; world/asset changes invalidate and repath safely; invalid
commands preserve the old target; duplicate body writers are rejected; path
work rotates fairly across agents; controller sliding/recovery cannot commit
outside the verified domain; links stop for a generation-checked arrival
acknowledgement. Release and document teardown retire all native ownership.

Bound the service to 32 agents, four searches per tick, per-query work ceilings
and copied route limits. Focused native bridge/lifecycle controls, neighboring
coordinator/physics tests, forced GC and selective owner sanitizers precede
the required aggregate and boundary gates. Store receipts under
`temp/scene3d-navigation-live-agents/`. Avoidance, dirty tiles, wider physics
profiles and M5–M12 remain subsequent milestones.


### Bounded planar agent profile: implementation and focused evidence

Named native agents now attach to copied capsule controller descriptions and
published clearance bakes through `lambda.navigation.scene`. Target, snapshot,
cancel, release and link-completion commands share the existing Radiant waist.
Commands, paths, assets and physics worlds have separate opaque identities.
The service retains native routes and body references, with no retained script
request values. One body admits one navigation writer. Queries rotate fairly
under the declared work/route quotas; incomplete searches remain distinguishable
from unreachable domains. The broad navigation capability is still incomplete.

Automatic movement respects playing physics. Explicit scene and physics batches
sample navigation before animation/physics. Actual completed foot positions
control progress, arrival and stall detection. Intended motion and computed
controller targets pass the shared surface walker before committing ordinary
kinematic targets. Links wait for script-owned traversal and matching path/action
acknowledgement at the achieved arrival endpoint; completing one selects its
retained arrival polygon. Events copy rooted details into the existing timing
queue, so handlers run after sampling and details survive agent release.

The delivered profile qualifies planar support floors and explicit links.
Automatic ramp/stair/down-step negotiation and wider terrain following remain
outstanding, along with local avoidance and dirty tiles.
M4 is partial; wider M2/M3 profiles and M5–M12 remain open. The four gameplay
policy/inspection scopes remain Lambda Script KIV.

Two new regressions identify shared prerequisites rather than fixture-specific
adjustments. A free controller move can end exactly on its cast skin without
reporting a cast hit; a following floor snap then began in unresolved overlap.
The controller now restores its existing recovery shell after that free move.
The independent repeated-diagonal native regression fails on the predecessor
and passes after the repair. Agent surface guards consume the controller's
reported length-scale precision, independently of caller skin, so an admitted
small skin does not reject the normal recovery separation.

A body-name reuse test verifies that agents cannot hijack a replacement body.
Explicit template reference reconciliation can transfer the attachment. That
case exposed a viewport-only rebuild notification: nested template replacements
were skipped. Retained viewports now notify their services for nested rebuilds;
physics walks the still-live detached old subtree to remap its native bindings.
Unrelated LOD references retain their identity. These changes preserve
**S12.1.5v2 / D4.5.2 / D5.3.3**, without revising a formal ruling.

Final focused checks pass: **156/156** native Scene3D cases, **118/118** physics
controls and **79/79** navigation provider cases. Selective ASan/UBSan passes
**17** scene agent/coordinator/owner controls with every-allocation GC and freed
memory poisoning; native physics ASan/UBSan passes all **118** cases. Other
ordinary runtime/render archive objects in the selective owner lane remain
uninstrumented; leak detection is disabled on this platform. The live agent
bridge passes AST/interpreter, MIR/JIT and AST/auto with forced GC. All **27**
existing navigation and typed-fixture golden executions pass on the final host
across interpreter/JIT/auto with forced GC. No existing golden was changed.

Artifacts are under `temp/scene3d-navigation-live-agents/`: `scene3d-final.log`,
`physics-precision.log`, `navigation-controls.log`,
`agents-final-sanitized-gc.log`, `physics-sanitized-final-tests.log`,
`bridge-final-matrix.json` and `navigation-final-goldens.json`.
The final aggregate qualification and its limits are recorded below.


### Final aggregate qualification and next delivery

The final frozen host/runners use the standard module profile. Input records
**4,567/4,594**, retaining 27 failures. The Lambda baseline selection records
**6,323/6,324**: the Word `proc__margins_export` fixture failed in the full run
and passed all three isolated reruns against the same host. The aggregate
failure remains recorded; those reruns do not make the complete gate green.

Radiant records **8,056 pass / 425 partial / 22 fail / 1 skip** (8,504 reported).
The failures comprise the standing cascade-memory gate and 20 writing-mode
cases, plus a render-runner timeout. Before that timeout, the visual renderer
printed **207 passed / 4 expected failures / 1 skipped** comparisons, then
produced no output for 180 seconds; process completion is unqualified. Native
Scene3D passes **156/156** inside this aggregate, and UI automation passes
**745/745**. No expected results, budgets or failure assertions were relaxed.

Strict module boundary images build. The checker still reports the same six
standing Class-F imports, **140 versus baseline 165**, with no exception
changes. The 284-symbol host export check across six modules, dimension lint
and whitespace checks pass. These results do not claim a green full aggregate
or completion of the gaming roadmap.

Earlier aggregate attempts exhausted disk space; their partial receipts are
not final qualification evidence. A copied test bundle also omitted the root
module profile, disabling Buffer; the final bundle includes it. Superseded
intermediate runner copies were pruned while retaining logs/manifests and the
latest link/agent qualification artifacts. Final metadata, implementation
hashes, copied source snapshots and receipt paths live in
`temp/scene3d-navigation-live-agents/final-checkpoint.json` and
`qualification-v2.json`. The qualified implementation sources remained
unchanged during the final run.

Next finish controller-backed terrain following for ramps and step-up/down,
then shared local avoidance. Preserve achieved physical vertical motion and
baked clearance/component identity rather than forcing a capsule onto a
conservative cell floor. Follow with dirty-tile work, wider M2/M3
profiles and M5–M12 native kernels. Finish/qualify the package-facing tick and
generation command/snapshot boundary before beginning M13's separate Lambda
package implementation. The native agent milestone is partial navigation
support; the complete outstanding gaming objective remains unfinished.

## M4 native terrain following — implemented; aggregate qualification limited

Separate achieved capsule foot positions from conservative baked navigation
heights. Reuse the controller for vertical motion and the surface walker for
bounded connected-domain motion. Bake support envelopes retain measured
per-cell raster spans and layer ceilings; do not use a caller skin or a
fixture-specific vertical tolerance to select a floor. Preserve **S12.1.5v2 /
D2.11.4 / D4.5.2 / D5.3.3 / D7.5.3v2**.

Acceptance: climb/descend an analytic ramp, cross an admitted step up/down,
retain stacked-floor identity/headroom, reject unsupported arrival and keep
existing wall/recovery, budget, link and lifecycle controls. Native support and
follower tests cover stored cell/polygon profiles and vertical seam steering.
Reproduce ramp/step failures against the predecessor, then focused native
controls, full Scene3D/navigation/physics controls, forced-GC bridge and required
aggregate gates once stable. Artifacts: `temp/scene3d-navigation-terrain/`.
This adds no M13 package policy; avoidance and the native roadmap remain open.

The predecessor fails both new ramp/step cases (`before-native.log`). The
implementation passes all 162 working-tree Scene3D cases, 81 native navigation
cases and 118 physics controls. Selective ASan/UBSan passes 19 agent/bridge
cases with every-allocation GC and freed-memory poisoning. Instrumented units
include navigation kernels, conversion, scene owners, physics and the harness;
other linked runtime/render objects remain ordinary builds, and leak detection
is disabled. These focused receipts do not claim a green aggregate.

Actual feet remain separate from copied `navigation_position`. Native ground
steering skips vertical raster seams, while explicit link completion verifies
the projected endpoint and grounded arrival layer. Ground checks precede
progress/arrival; controller settling commits a verified target first. Support
spans survive version-1 cell export/import as an optional field; missing spans
mean exact-floor support, and invalid spans reject. The default agent projection
budget accounts for support selection plus both endpoint passes under the same
4,096-test cap. Authored snap distance is preserved. This is static baked-domain
Y-up capsule terrain following, not arbitrary moving-domain/agent support.

Final review also updates `navigation_position` immediately after an accepted
link handoff. The refreshed executable passes all 162 Scene3D cases, the final
selective sanitizer/forced-GC lane passes all 19 cases, and the live bridge
passes interpreter/JIT/auto. The seven navigation goldens pass across all three
tiers with forced/poisoned GC (21 executions). These receipts are
`scene3d-link.log`, `agents-link-sanitized-gc.log`, `bridge-final-matrix.json`
and `navigation-matrix.json`.

The frozen aggregate checkpoint precedes that localized snapshot refresh.
Library passes **1,917/1,917**. Input records **4,567/4,594**, with the same 27
failure identities as the preceding checkpoint (`input-failure-comparison.json`).
Lambda records **6,293/6,326**: 32 failures share one killed MIR reference batch,
and one HTTP case also exits 137. The unmodified MIR stress suite passes all
350 cases when run alone; the HTTP case passes three isolated reruns against
the same frozen host. The termination cause remains unattributed and the
original aggregate remains failed. Radiant records **8,270 passed / 429 partial /
21 failed / 2 skipped**: the cascade-memory gate and 20 writing-mode cases fail.
Its Scene3D cases and UI automation pass (162 and 746 respectively), and the
render runner completes on this checkpoint. Full aggregates were not repeated
for the final link-position assignment; the affected full native suite, bridge
tiers and sanitizer lane were refreshed instead.

Strict boundary DSOs build; the checker still reports the same six standing
Class-F imports, 140 total versus baseline 165. No boundary exception was
changed. The 284-symbol/six-module host export check, dimension lint and
whitespace checks pass. Aggregate artifacts and the final source/binary
checkpoint are under `temp/scene3d-navigation-terrain/`. No performance claim is
made from these debug correctness runs.

Next native milestone: bounded neighbor lookup/local avoidance integrated with
the existing surface/controller checks. Follow with dirty-tile updates and the
remaining native M2–M12 profiles. Repath/yield policy, graph decisions and other
high-level packages remain deferred to M13; editor interfaces remain excluded.
