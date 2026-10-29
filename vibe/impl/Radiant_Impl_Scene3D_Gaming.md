# Radiant Scene3D gaming implementation

**Started:** 2026-10-28. **Status:** M0 relocated with focused passing evidence;
M1 coordination and M2 discrete geometry/resources, exact queries/casts and the
first single-sphere CCD profile are implemented, with focused passing evidence
and recorded qualification limits. M3 joints and sampled-world capsule movement are implemented with focused passing evidence; broader M2 CCD and the rest of M3–M12 are pending. Limits below
remain open; the full native gaming roadmap is not delivered.

Design: [Radiant_Design_Scene3D.md](../radiant/Radiant_Design_Scene3D.md).
The user authorized implementation of the native M0–M12 scope. The four
Lambda Script gameplay algorithm scopes in design §13 remain KIV.

Preserve **S12.1.1v3 / S12.1.5v2**, **D2.11.4**, **D4.5.1v4–D4.5.3v2**,
**D5.3.3**, **D7.1.1–D7.1.3**, **D7.3.4–D7.3.5** and **D7.5.3v2–D7.5.4**
in the [formal semantics](../../doc/Lambda_Formal_Semantics.md) and
[formal design](../../doc/Lambda_Formal_Design.md). This implementation record
does not revise those contracts.

## Delivery sequence

| Stage | Outcome | Status |
|---|---|---|
| M0 | Existing Scene3D sources live under `radiant/scene3d/`, with corrected consumers/build discovery and unchanged behavior. | Implemented; aggregate qualification limited |
| M1 | One native scene coordinator, fixed tick phases, reconciliation, capabilities and counters. | Implemented; aggregate qualification limited |
| M2 | Capsule/convex/mesh queries and bounded CCD. | Discrete geometry/resources, exact overlaps, translation casts, all-hit queries and single-sphere CCD implemented; aggregate qualification limited; broader CCD remains open |
| M3 | Joints/motors, character movement and ragdoll integration. | Joints and sampled-world capsule movement implemented through pure/live APIs, in qualification; pushing/future-motion coupling and ragdolls pending |
| M4 | Navigation assets, A*, agent movement and avoidance. | Pending |
| M5 | Animation graphs/blend spaces and focused authoring. | Pending |
| M6 | CPU/GPU emitter presets, curves, collisions and authoring. | Pending |
| M7 | Spatial game audio, routing/effects and microphone acquisition. | Pending |
| M8 | Genuine 2D physics/controller/navigation integration. | Pending |
| M9 | Shared depth/normal passes, SSAO, SSR and DOF. | Pending |
| M10 | Lightmap/probe baking and authoring. | Pending |
| M11 | Budgeted dynamic indirect lighting and volumetric fog. | Pending |
| M12 | Surface soft bodies and combined full-scope qualification. | Pending |

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
