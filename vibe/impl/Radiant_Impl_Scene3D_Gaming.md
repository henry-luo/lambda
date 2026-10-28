# Radiant Scene3D gaming implementation

**Started:** 2026-10-28. **Status:** M0 relocated with focused passing evidence;
M1 coordinator and the first M2 capsule profile are implemented with recorded
aggregate limits. The rest of M2 and M3–M12 remain pending. Limits below
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
| M2 | Capsule/convex/mesh queries and bounded CCD. | Capsule profile implemented; aggregate qualification limited; remaining profiles pending |
| M3 | Joints/motors, character movement and ragdoll integration. | Pending |
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
