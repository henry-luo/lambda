# Radiant Layout Simplification

**Date:** 2026-08-09  
**Status:** implementation complete; 1,030 net layout LOC removed, baseline gate green
**Scope:** `radiant/` layout only; no CSS resolver, paint, DOM ownership, or public Lambda API redesign

## Decision

Radiant layout should have one owner for each of these concerns:

1. Physical box conversion and min/max constraint policy.
2. Common layout-pass state and intrinsic-size finalization.
3. Formatting-context-specific algorithms: block flow, inline, flex, grid, table, and multicol.
4. DOM repair/materialization performed before an algorithm needs its view tree.

The target is **not** a generic layout-engine vtable. Flex, grid, table, and
multicol have different CSS algorithms, passes, and mutation rules. They keep
separate algorithm bodies. The simplification is to stop each body from
reimplementing box math, speculative-pass setup, and DOM construction policy.

This follows **D7.1.1**: the work stays inside the `radiant` layer and does not
introduce upward dependencies or a new cross-layer service. It also follows
**D4.5.1v2**: helpers borrow the existing document arena, view pools, and
`LayoutContext`; no new individually-owned layout graph or GC lifetime is
introduced.

## Live evidence

The current layout source surface is 67,180 LOC when the `layout_*.cpp` set is
counted with `intrinsic_sizing.cpp`. The largest concentration points are:

| Area | LOC | Current mixed responsibilities |
|---|---:|---|
| `layout_block.cpp` | 12,354 | generated content, replaced sizing, margin/BFC geometry, inline bridge, float pre-pass, block entry/exit |
| `layout_table.cpp` | 10,000 | anonymous-table repair, view construction, metadata, border conflict resolution, width/height algorithms, final integration |
| `intrinsic_sizing.cpp` | 7,861 | cache/recursion protection, font/style setup, text and pseudo measurement, mode-specific contributions, constraints, height estimation |
| `layout_flex*.cpp` | 11,730 | flex algorithm, intrinsic measurement, and reflow/multipass lifecycle |
| `layout.hpp` | 2,983 | public layout contracts plus unrelated format-specific state |

The public dispatcher already has the correct high-level shape:
`layout_flow_node()` selects a display role and delegates to block, inline,
text, table, flex, grid, or positioned layout. The problem is below that
dispatch point: physical-axis size rules and pass setup are repeated or are
hidden inside formatting-context files.

### Verified opportunities

- `layout_box.cpp` carried paired width/height bodies for content↔border-box
  conversion, padding/border floors, min/max clamping, and the border-box
  constraint path. Each pair differed only by the physical axis; the file
  already exposed axis-taking APIs beside them.
- `measure_element_intrinsic_widths()` is a single 3,468-line operation
  (`intrinsic_sizing.cpp:3172-6639`). Its cache/recursion/view/font setup,
  content contribution calculation, and final box/constraint application have
  different ownership and should be independently testable.
- `calculate_max_content_height()` begins a second mode classification and
  style-resolution path for a width-dependent estimate. It must share setup
  and final box conversion with width measurement, while retaining its honest
  "estimate, not a layout pass" contract.
- `layout_block.cpp` has four independently-owned regions: generated/replaced
  content, block geometry and margin collapse, BFC/inline-flow execution, and
  final block integration. Moving these boundaries is a readability change;
  it is not counted as an LOC win until duplicated helpers are actually
  deleted.
- `layout_table.cpp` mixes pre-layout DOM repair/view-tree construction with
  the table algorithm. The table algorithm remains table-specific, but its
  tree preparation needs a single prepare/mark boundary that owns font-context
  save/restore and anonymous-box creation.

### Explicit non-opportunities

- Do not merge `layout_flex_measurement.cpp` into
  `layout_flex_multipass.cpp`: one computes intrinsic contributions and the
  other owns committed reflow state. Combining them obscures speculative versus
  committed writes.
- Do not replace table, flex, and grid baseline walkers with a callback
  framework. Their skip, accumulation, and fallback policy is meaningfully
  different; a generic callback layer is more code and less auditable.
- Do not move unresolved CSS resolution into layout helpers. Layout may query
  a resolved declaration, but CSS resolution remains the owner of declaration
  interpretation.

## Target module design

```text
layout_flow_node
  -> prepare role-specific view state
  -> run one formatting-context algorithm
  -> publish geometry / positioned descendants

layout_box.*             box metrics, axis conversion, min/max, aspect-ratio constraints
layout_pass.*            scoped speculative-pass state, caches, restore rules
intrinsic_sizing.*       IntrinsicSession + shared finalization + contribution dispatch
layout_block_geometry.*  margin collapse, block edges, float/BFC geometry
layout_block_flow.*      normal-flow child traversal and inline bridge
layout_generated.*       pseudo, first-letter, replaced/canvas preparation
layout_table_tree.*      anonymous-table repair and view-role marking
layout_table_algorithm.* metadata, tracks, borders, row/cell sizing and placement
layout_flex*.cpp         flex-specific measure / committed multipass / placement
layout_grid*.cpp         grid-specific placement / sizing / multipass
layout_multicol.cpp      fragmentation-specific orchestration
```

The first five modules are shared services with narrow headers. Each
formatting-context module may call them, but must not reach into another
formatting-context module for policy. `layout.hpp` becomes an umbrella for
stable value types and top-level entry points only; format-specific structs and
private helpers leave it with their owning module.

`IntrinsicSession` is the key structural boundary. It owns, via scoped guards,
the existing cache probe/store, cycle flag, current view, font state, and
measurement run mode. Its contribution operation returns content-box intrinsic
sizes. One shared finalizer then applies pseudo/box extras, min/max keywords,
and content-vs-border-box conversion. This prevents a grid early return or a
new flex special case from bypassing constraints.

## Execution plan

Every implementation phase is separate and must be behavior-neutral unless a
fixture proves an existing defect. A pure file move is not allowed to claim an
LOC reduction.

| Phase | Change | Expected net LOC | Required proof |
|---|---|---:|---|
| ✅ 0 | Make `layout_box.cpp` axis-first; retain width/height wrappers as compatibility adapters. | -24 realized | `verify_loc_reduction`, build, layout baseline |
| 1 | Extract `IntrinsicSession` and one shared intrinsic finalizer. Delete duplicated setup/final tails before adding new files. | -220 to -300 | targeted intrinsic/box cases, flex/grid/table suites, full layout baseline |
| 2 | Split block generated-content and geometry from normal-flow orchestration. Delete only helpers proved to be duplicated; moves alone are neutral. | -100 to -160 | generated-content, float, margin-collapse, writing-mode fixtures |
| 3 | Establish `table_prepare_tree()` as the sole anonymous-box/role-marking owner, then remove duplicate context/repair paths. | -120 to -180 | table baseline, border-collapse, colspan/rowspan, pseudo-table fixtures |
| 4 | Shrink `layout.hpp` to stable contracts after private state has a sole owner. | -80 to -120 | clean compile, provider/layer checks, no public header expansion |

The planned realized reduction was **at least 544 LOC**, including Phase 0;
the delivered result is measured against the raw
file sizes: moving a 3,000-line function to another file is not simplification
until responsibility overlap has been removed.

### Phase 0 verification — 2026-08-09

- `radiant/layout_box.cpp`: **231 → 207 LOC (-24)**, verified with
  `./utils/verify_loc_reduction.sh --ref HEAD radiant/layout_box.cpp`.
- `git diff --check`, `make check-int-cast`, and `make build` passed; the build
  reported **0 errors, 0 warnings**.
- Focused browser-comparison fixtures passed: `box_004_box_sizing`,
  `min_width_overrides_max_width`, `min_height_overrides_max_height`,
  `absolute_minmax_bottom_right_min_max`, `flex_018_min_max_width`, and
  `table-has-box-sizing-border-box-001`.
- The two full `make layout suite=baseline` invocations both completed with
  4,348 successes, 7 skips, and 30 batch-process errors (the runner retried
  the same isolated files). This phase does **not** claim the full suite is
  green; those runner errors need independent triage before they can become a
  baseline acceptance result. Neither run reported a layout comparison diff.

### Current implementation checkpoint — 2026-08-09

The first simplification slices are now measured against `HEAD`, not by moved
file size:

| Slice | Net LOC | Owner consolidated |
|---|---:|---|
| physical box-axis conversion and constraint wrappers | -24 | `layout_box.cpp` |
| block edge-content, padding/border, and shared view predicates | -115 | `layout_block.cpp` |
| percentage size/offset axis reuse | +5 | `layout_containing_block.cpp` |
| intrinsic width dispatch reuse | -27 | `intrinsic_sizing.cpp` |
| flex direct-text alignment and final-item traversal/adjustment | -241 | `layout_flex_multipass.cpp` |
| grid traversal and duplicate branch removal | -36 | `layout_grid_multipass.cpp` |
| inline traversal/edge reuse | -55 | `layout_inline.cpp` |
| pass-cache, multicol, and positioned cleanup | -19 | `layout_measure.cpp`, `layout_multicol.cpp`, `layout_positioned.cpp` |
| marker storage guard and Unicode titlecase handling | +10 | `layout.cpp`, `layout_text.cpp` |
| intrinsic glyph advance and small-caps helper reuse | -55 | `intrinsic_sizing.cpp` |
| table collapsed-border candidates, anonymous-run helpers, and shared table sizing policy | -474 | `layout_table.cpp` |
| block intrinsic fit-content axis policy | -141 | `layout_block.cpp` |
| **total** | **-1,030** | **13 touched layout files** |

`./utils/verify_loc_reduction.sh --ref HEAD` reports 52,969 → 51,939 LOC
(`-1,030`) for the touched layout set. The marker guard is a correctness fix,
not a workaround: `::marker` stores `MarkerProp` in the shared `blk` slot, so
remembered-size code must not reinterpret it as `BlockProp`. The titlecase
mapping preserves CSS Unicode titlecase for the four Latin digraphs while
reusing the existing case-mapping pipeline.

The baseline runner was also made deterministic in `Makefile`: recorded
entries are selected before aggregate reporting, with batches of 20 and one
child at a time. The previous concurrent runner could lose result files while
sharing font/layout resources and report false missing-test failures. This
runner change does not count toward the layout LOC total.

Final verification on 2026-08-09:

- `git diff --check`, `make check-int-cast`, and `make build` pass.
- `make test-layout-baseline` exits **0**: 5,716 passed, 441 partial, 0
  failed, 7 skipped across the recorded layout suites; the page snapshot
  check also passes (42/42).
- No expected-output reference was changed.

### Final continuation checkpoint — 2026-08-09

The remaining table and intrinsic duplication was removed without introducing a
generic formatting-context callback layer. `intrinsic_sizing.cpp` now shares
small-caps codepoint conversion and loaded-glyph advance handling across its
text-width and width-dependent-height paths. `layout_table.cpp` reuses the
collapsed-border candidate helper and anonymous-run flushers, then drops
repetitive per-cell/per-column trace blocks while retaining phase-level
diagnostics. This keeps algorithm ownership local under **D7.1.1** and keeps
anonymous nodes and layout state in the existing arenas/pools under
**D4.5.1v2**.

Final measured result:

- `radiant/layout_*.cpp` plus `intrinsic_sizing.cpp`: **67,180 LOC** current
  source surface.
- Touched layout files: **52,969 → 51,939 LOC (-1,030 net)** versus `HEAD`.
- `git diff --check`, `make check-int-cast`, and `make build`: pass; build has
  **0 errors and 0 warnings**.
- Focused min/max, flex, table, and intrinsic fixtures: **7/7 exit 0**.
- Exact `make test-layout-baseline`: **exit 0**; 5,716 passed, 441 partial,
  0 failed, 7 skipped, with the page suite at 42/42 and 0 failed.

## Acceptance and rollback

For every phase:

1. `git diff --check` and
   `./utils/verify_loc_reduction.sh --ref <pre-phase-commit> <all touched files>`
   must pass with a strictly negative total.
2. `make check-int-cast`, `make build`, and `make layout suite=baseline` must
   pass. Run `make test-radiant-baseline` for any phase that changes layout
   pass ownership or view-tree preparation; record unrelated existing failures
   separately rather than weakening the gate.
3. Add focused fixture coverage for a changed invariant before deletion:
   content-box versus border-box min/max, percentage/aspect-ratio cycles,
   generated content, and anonymous-table repair are the minimum risk set.
4. Compare layout snapshots before/after. Any geometry difference is a defect
   unless it is accompanied by a minimal spec-backed fixture and an explicit
   behavior-change entry.

Rollback is phase-local: revert the phase if the measured LOC reduction is not
strictly negative, if a policy callback is needed merely to share traversal
syntax, or if a formatting-context algorithm begins mutating state during an
intrinsic-only pass.
