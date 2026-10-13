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
