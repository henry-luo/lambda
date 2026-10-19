# Lambda Physics Implementation

**Date:** 2026-10-13. **Status:** initial 3D profile implemented; later design stages remain open.
Design: [Lambda_Pkg_Physics.md](../Lambda_Pkg_Physics.md).
Applies **D7.1.1–D7.1.3**, **D6.4.1**, **D7.2.4 / S17.2.1**,
**S12.1.1v2 / S12.1.5v2**, **D4.5.1v4 / D5.3.3 / D7.5.3**.

## Implemented surface

- Six C operations under `lib/physics/`: create/destroy/apply/step/read/query.
  Binary64 rigid transforms, sphere/box collisions, clipped box manifolds,
  compound centre of mass and full inertia tensors, angular impulses,
  friction/restitution, warm starting, sleeping, sensors and collision filters.
- One source compiler below runtime/Radiant accepts Mark elements and DOM
  sources, including textual DOM attributes. Metadata shorthand and explicit
  body tags enter the same path.
- Pure system functions: `physics_validate`, `physics_simulate`, `physics_query`.
  Bounded traces include initial/final samples, selected bodies, scheduled
  commands and events. Rooted builders own every value across safepoints.
- Document-owned physics state supplies scene poses without rewriting authored
  attributes. The existing animation scheduler drives fixed ticks. Graphics
  resources and physics lifetimes are independent. Live controls use
  `radiant.physics` and the `lambda.physics.scene` package.
- The package supplies constructors, metadata attachment, trajectory/chart
  records, selected energy terms and recorded animation tracks.

## Deliberate initial boundaries

The initial broad phase is sweep-and-prune with exact narrow-phase checks;
the proposed dynamic AABB tree remains a performance extension. Warm starting
uses sorted collider-pair manifolds, avoiding an all-contact quadratic scan.
Sleeping is currently per body; full island management remains open.

Topology is fixed after creation. Core commands cover forces/impulses,
velocities, teleports, gravity, wake and kinematic targets. Persistent forces,
object insertion/removal and material edits are later command extensions.
Structural scene changes currently recreate the world; appearance/camera
changes preserve matching physical descriptions.

Presentation currently uses completed tick poses. Interpolated poses,
tick-sampled kinematic animation, refined edit preservation, checkpoint/replay,
and explicit frozen-frame export controls remain open. Body animation tracks,
CCD and joints are rejected instead of being ignored. The initial query API
returns one hit per request; `aabb` is explicitly a broad-phase query.

`physics_open` and the other headless mutable-world procedures are not exposed
until scoped cleanup and legal return transfer satisfy **S12.4.2–S12.4.3**.
Document-owned worlds already have deterministic teardown.

## Integration fixes

Native tests exposed cached impulses being reapplied to sleeping bodies;
inactive fixed/sleeping pairs now retain their cache without applying it.
Numerical/quota faults retain the last completed body snapshot. Scene playback
refreshes both tick metadata and borrowed poses after a partially completed
batch, so it cannot render poses from one tick with another tick's timestamp.

Document JS imports of Lambda modules exposed inconsistent file-URL handling:
the source loader expected a local path, and live import reads used a different
registry identity. File URLs now use the shared lower-level decoder before
filesystem access and canonical registry key formation. The scene fixture
exercises this path through an ordinary imported Lambda control module.
The same bridge now binds each imported MIR module's defining constant image
before publishing callable exports; previously a procedure reading literal
arrays could dereference an unbound constant slab (**D7.2.1–D7.2.2**).

JavaScript property reads now dispatch ordinary Lambda VMaps as well as host
objects and Promise carriers. Missing properties become `undefined` while
present `null` values stay `null`. Promise reaction construction reserves all
seven exact roots, including the asynchronous domain (**D5.3.3**).

The expanded demo suite exposed a pose-conversion defect: animation quaternion
matrices use row-major storage, while Scene3D uses column-major matrices. The
physics adapter copied the former directly, reversing visible rotations while
collision geometry retained the correct orientation. The adapter and authored
quaternion path now share one conversion helper. A native test compares
rendered axes to the independent vector rotation used by collision detection,
both initially and after stepping (**S12.1.5v2**).

## Verification

macOS ARM64, 2026-10-13. Debug and release builds completed; the native core also passes a strict C11 syntax check.

| Check | Result |
|---|---|
| Native physics GTest | 16/16 |
| Five physics golden fixtures | Pass on `auto`, `interp`, `jit`; forced collection and freed-memory poisoning also pass |
| Permanent physics/bridge MIR GC fixtures | 6/6, including native MIR, randomized GC and Lambda MIR interpretation |
| MIR GC sweep excluding the separately timed-out music-layout fixture | 311/311; the full gate is not green |
| Scene3D native tests after the final fault-publication change | 27/27 |
| Live physics viewer case | 16/16, including queued contact detail, query results, GC and repaint preservation |
| Broader native viewer / UI automation | 177/177 and 569/569 |
| JavaScript baseline / AST harness | 504/504 and 200/200 |
| Lambda structured / math SVG corpus / Test262 preflight | 141/141, 921/921, 1/1 |
| Radiant integer-cast lint / physics diff whitespace | Pass |

The required full baselines were attempted and remain **not green**:

- Lambda's first run and a direct rerun lost `lambda.exe` during concurrent
  builds, producing `ENOENT` failures. A stable executable copy under
  `temp/physics/stable-run` then passed 1393/1394 discovered goldens and all
  68 other runtime cases (1461/1462 total). `edit_view_only` still reports
  `math source survives: [true, false]`; focused runs reproduce it on both
  interpreter and JIT. No golden or runtime workaround was applied.
- The full MIR GC runner timed out at `regression_music_layout` after 180 s
  without output; its orphaned child was stopped. The other 311 cases passed
  in a separate explicitly filtered run. The timeout remains unresolved.
- Radiant reports one unexpected Markdown layout regression:
  `md_nuxt-readme`, element agreement 100% → 99.3%, reproduced in the focused
  Markdown run. Its other native and viewer suites pass; accepted partial
  layout matches remain partial. No reference baseline was changed.

A release smoke benchmark keeps 1000 noncontacting sphere bodies awake with
parallel constant velocities for 600 ticks, selecting one body for the initial
and final trace frames. Five measured runs after one warm-up have a median
**71.7 ms**, including process startup, scene construction, simulation and
result conversion. This sparse workload does not characterize dense stacks.
The final falling-box scene was also captured and visually inspected.

Scratch logs, the release measurement and the preview are under
`temp/physics/` (not committed).

### Expanded playground

Four additional experiments share native scene descriptions, controls and a
responsive gallery in `test/demo/scene3d/physics/`: a 32-tile domino circuit, nine
compound bodies in a switchable-gravity chamber, an 18-marble sorter with a
kinematic paddle and unique sensor arrivals, and a five-material bounce
comparison with batch-generated height curves. Pure traces and live scenes
use the same descriptions (**S12.1.1v2 / S12.1.5v2**). The sphere resource
constructor is shared with Ringworld.

Validation on macOS ARM64, 2026-10-13:

- Six physics golden fixtures, including deterministic checks of all four
  experiments, pass on `auto`, `interp` and `jit`; a JIT run with forced
  collection and freed-memory poisoning also passes.
- Four permanent viewer replays pass all 66 events. The existing rigid-body
  replay and Ringworld's animation/camera replay also pass; the broader
  Scene3D viewer suite passes 54/54.
- Scene3D native tests pass 28/28, including the rendered/collider-axis
  regression. Radiant integer-cast lint and whitespace checks pass.
- Desktop and 375-pixel gallery captures pass and were visually inspected,
  along with the experiment captures. The narrow grid uses zero-minimum
  tracks so intrinsic scene widths cannot enlarge its column.
- A stable executable rerun passes the 51-page snapshot and WPT input-event
  checks (20 passed, 12 unsupported cases skipped). The layout baseline has
  2946 passes, 347 accepted partial matches and one Markdown failure, so the
  full Radiant gate remains not green. No baseline was changed.
- The full native viewer run passes 183/185. Its two photo-editor failures
  both pass a focused rerun against the newest shared executable; that build
  changed while the earlier frozen executable was being tested.

The initial broad run lost the shared `lambda.exe` during concurrent builds;
reruns use a stable copy. A sandboxed page snapshot also failed resource
loading; its normal-access rerun passes. Demo logs and captures are under
`temp/physics-demos/`, with tier and focused viewer logs under `temp/physics/`.

### Gallery navigation and controls

The 2026-10-14 follow-up reproduces controls failing after opening an experiment
from the gallery. Retiring the old document polls the shared libuv loop while
its evaluator is current. A new document's wake callback previously dequeued
its task before the activation rejected that foreign owner. Schedulers now
retain runnable work until their owning `EvalContext` is current
(**D5.4.1**). Navigation workers release the derived JS binding alongside the
evaluator before returning to the worker pool.

Host eval initialization also retains pending Promise and next-tick jobs when
no timer exists yet (**D6.3.1**). Otherwise the assertion turn could discard
the initial native read's completion before the demo started telemetry.
Repeated navigation exposed a separate cache-hit path: a fresh runtime kept
the Lambda import's loading namespace because export publication ran only
after compilation. One shared publication helper now fills uninitialized
namespaces after cross-language loads, including cached MIR shells
(**D5.4.3 / D8.5.1v7**).

The four experiment replays now use native mouse clicks for their buttons.
A fifth replay visits every experiment from the gallery, verifying start,
pause, reset, stepping, camera controls, gravity changes and native-clock
playback in 52 events. It passes all 23 assertions on the rebuilt shared
executable and in an isolated checkout. A native scheduler regression checks
that a foreign-loop wake leaves the task queued and that its owner can then
run it. The focused concurrency suite passes 15/15; the JavaScript baseline
passes 504/504 and the AST harness passes 181/181.

Final verification also passes a 48-event window replay with real elapsed time
(20 assertions), all five permanent physics viewer cases (118 events), 28/28
Scene3D native tests, 312/312 MIR forced-GC cases, 2112/2112 input-parser cases,
141/141 structured cases, 921/921 math SVG cases and the Test262 preflight.
The prior MIR music-layout timeout did not reproduce in this full sweep.
Integer-cast lint and whitespace checks pass.

The required full gates remain not green. Concurrent builds removed the shared
executable during Lambda's first run; a stable-copy rerun passes 1469/1472
cases, with `typed_array_blur_running`, `edit_view_only` and the newly added
`eps_errors` still failing. The two DTNA cases excluded from that rerun had
already passed in the full run. Radiant retains the Markdown layout failure.
Its four document-viewer failures pass against the newest executable, and the
eight initial photo failures also pass there; two newly added asynchronous
photo export cases still fail. The unfiltered concurrency runner first fails
`JubeDom4.OrdinalParityCoversDeclaredMembersAndHusks`, then reports owner-binding
failures in later fixtures; the 15 focused scheduler cases pass separately.
The blur fixture passes in the isolated checkout containing these fixes, and
the older executable also reproduces the editor fixture's math-source failure.
No golden or test was changed to conceal these failures.
