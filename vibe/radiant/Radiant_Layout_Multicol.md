# Radiant Multi-column Layout — Structural Enhancement Proposal

## Goal

Bring Radiant's multi-column layout from a post-layout redistribution model to a true fragmentation-based multicol formatting context that can support:

- `column-count`, `column-width`, `column-gap`, `column-rule`
- `column-fill: balance | auto`
- `column-span: all`
- nested multicol containers
- out-of-flow positioning in fragmented multicol containers
- stable relayout when fragmentainer size or child structure changes

This proposal is based on the current `wpt-css-multicol` suite result and the present implementation in [`radiant/layout_multicol.cpp`](/Users/henryluo/Projects/lambda/radiant/layout_multicol.cpp) and [`radiant/layout_block.cpp`](/Users/henryluo/Projects/lambda/radiant/layout_block.cpp).

## Current Test Result

Command used:

```bash
node test/layout/test_radiant_layout.js -c wpt-css-multicol --json
```

Current result snapshot:

- Referenced tests executed: `362`
- Passed: `82`
- Failed: `280`
- Skipped: `342`
- Errors: `0`

High-signal observations from `temp/wpt-css-multicol-results.json`:

- `227 / 280` failures still have `textPassRate == 100`
- average failed `elementPassRate` is about `49.16%`
- `189 / 280` failures have `textPassRate == 100` and `elementPassRate < 80`

Interpretation:

- The primary problem is not text shaping or font metrics.
- The primary problem is structural fragmentation: wrong fragment boundaries, wrong fragment ownership, wrong placement of spanners, and wrong handling of out-of-flow descendants in fragmented coordinate spaces.

## Implementation Checkpoint

After the first implementation slice, the `wpt-css-multicol` suite improved from `82 / 362` to `101 / 362` passing tests with no regressions against the previous checkpoints.

Implemented pieces:

- Added CSS plumbing for `column-height` and `column-wrap`.
- Added used-column tracking so column rules are painted only between columns that receive content.
- Added fragment/group scaffolding inside `layout_multicol.cpp`.
- Added height-limited `column-fill:auto` behavior for definite `height`, `max-height`, and `column-height`.
- Added row wrapping support for `column-wrap:wrap` / unconstrained `auto`.
- Used definite container block-size as the fragmentainer height for `column-height:auto` with explicit `column-wrap:wrap`.
- Excluded absolute/fixed positioned children from in-flow multicol distribution.

New passes in the latest full-suite compare:

- `abspos-after-spanner`
- `column-height-001`
- `column-height-002`
- `column-height-004`
- `column-height-010`
- `column-height-019`

Remaining failures still confirm the main architectural gap: Radiant can now approximate more fragmentainer geometry, but descendants inside a fragmented child are still laid out as one continuous block instead of producing true child fragments.

## Failure Clusters

Largest failing themes:

| Theme | Approx failed tests | Signal |
|---|---:|---|
| balancing / column-height / nested balancing | 55 | current balancing is too coarse and not fragment-aware |
| `column-span: all` / spanners | 49 | spanner split points and post-spanner continuation are structurally wrong |
| nested multicol | 44 | current model does not compose nested fragmentainers cleanly |
| positioned / OOF / abspos / fixed | 28 | containing block resolution across column fragments is incomplete |
| fragmentation / breaking / `column-fill:auto` | 26 | block-only break logic is insufficient |
| rules / gaps / column rule interaction | 16 | painting and geometry are derived from simplified post-layout dimensions |

Representative failures:

- `multicol-span-all-004`, `multicol-span-all-005`, `multicol-span-all-020`
- `multicol-span-all-children-height-004a`, `-004b`, `-006`, `-007`
- `multicol-rule-nested-balancing-001` through `-004`
- `multicol-breaking-001` through `-006`
- `columnfill-auto-max-height-001`, `-002`
- `abspos-after-spanner`, `abspos-in-multicol-with-spanner-crash`
- `fixed-in-multicol-with-transform-container`
- `multicol-nested-006` through `-033`

## Current Architecture

The current implementation is intentionally simple:

1. Compute column width/count.
2. Lay out all content once in a single narrow column.
3. Collect child blocks.
4. Reposition blocks into columns after layout.
5. Treat spanners as full-width blocks inserted between groups.
6. Redistribute inline-only text as a special fallback.

This has three important consequences:

### 1. Fragmentation happens after layout, not during layout

Children are laid out as if they belong to one continuous block formatting context. Later, Radiant mutates `x/y` positions to simulate columns. This means:

- line breaking happened without true fragmentainer boundaries
- block descendants do not know which column fragment they belong to
- nested multicol containers are laid out before their parent's fragmentation model is final

### 2. The engine has no first-class fragment objects

There is no representation of:

- column fragments
- fragmentainer boundaries
- fragment continuation chains
- pre-spanner and post-spanner column groups

Without that, many WPT tests fail because the element tree and geometry need fragment identity, not just shifted coordinates.

### 3. Out-of-flow placement is not fragment-aware

Absolute and fixed descendants need answers to questions like:

- which column fragment contains the static-position placeholder?
- which fragment establishes the containing block?
- how does a spanner split the containing block chain?

Today those questions are mostly answered by normal block layout rules plus late coordinate patching, which is not enough.

## Root Cause Summary

Radiant currently implements multicol as a visual redistribution pass. WPT expects a fragmentation system.

That mismatch explains nearly all high-volume failures:

- balancing failures: because balancing is based on aggregated child heights, not fragment candidates
- spanner failures: because `column-span: all` must split the fragmentation flow, not just insert a full-width box
- breaking failures: because breaks must occur while producing fragments, not after
- nested failures: because child multicol contexts need stable fragmentainer constraints from the parent
- abspos/fixed failures: because static position and containing block geometry must reference fragmentainers

## Proposed Structural Design

### Design Principle

Promote multicol from "reposition children after layout" to "layout into fragmentainers".

### New Core Abstractions

#### 1. `ColumnFragment`

Add an internal fragment record for one produced column:

- column index
- `x/y`
- inline size
- block size
- used block size
- pointer to owning column group
- list of assigned child fragments

This should be layout-only state, not a public DOM node.

#### 2. `ColumnGroup`

A multicol container is not just a list of columns. It is a sequence of column groups separated by spanners.

Each group needs:

- fragmentainer count
- fragmentainer width/gap
- fill mode
- balancing target height
- fragment list
- pre/post spanner boundaries

This is the correct unit for `column-span: all`.

#### 3. `FragmentedFlowCursor`

Introduce a cursor that replaces "single `advance_y` plus later repositioning":

- current column group
- current fragmentainer
- current block offset within that fragmentainer
- remaining block-size budget
- break policy state

This lets normal block and inline layout ask "do I fit here?" while layout is happening.

#### 4. `FragmentAnchor`

For abspos/fixed/static-position integration, track a fragment anchor for in-flow placeholders:

- owning multicol container
- owning column group
- owning fragment index
- local coordinates within fragmentainer

This gives positioned layout a stable place to resolve multicol-specific containing block logic.

## Required Algorithm Changes

### Phase A: Replace post-layout redistribution with fragment production

Instead of laying out everything into one narrow column and then moving children:

1. create column group 0
2. create fragmentainer 0
3. layout child flow into fragmentainers in order
4. when content overflows, advance to the next fragmentainer
5. when a spanner is encountered:
   - close the current column group
   - place the spanner at full inline size
   - create a new column group below it
   - resume fragmentation there

This is the single most important structural change.

### Phase B: Make line layout fragment-aware

The current inline-only fallback is a symptom of missing fragment-aware inline layout.

Instead:

- line layout should see fragmentainer width directly
- line breaks should occur with actual remaining block budget
- line fragments should advance to the next column when they overflow

Once this exists, the special "collect all `TextRect`s and redistribute later" path can be removed.

### Phase C: Model spanners as flow splits

`column-span: all` should not be a repositioned child. It should:

- terminate the previous fragmentainer group
- consume full multicol inline size
- establish a new vertical offset baseline
- restart column balancing below the spanner

This is necessary for:

- `multicol-span-all-*`
- `multicol-span-all-children-height-*`
- `always-balancing-before-column-span`
- many nested multicol tests

### Phase D: Separate `column-fill: balance` from `column-fill: auto`

Current code treats balancing as a simple `ceil(total_height / column_count)` heuristic.

Instead:

- `balance` should run a measuring pass that finds a viable target fragmentainer block-size
- `auto` should fill sequentially using the definite available block-size

Recommended structure:

1. measurement pass produces break candidates and minimal feasible heights
2. balancing search chooses target fragmentainer height
3. commit pass materializes fragments at that target

This can be done with a bounded binary search over target block-size for large groups.

### Phase E: Out-of-flow integration

Update positioned layout so multicol is a first-class containing block source.

Needed behavior:

- static-position placeholders resolve to a specific fragment anchor
- abspos descendants inside multicol use the correct fragmentainer-local geometry
- spanners reset the continuation context for static positions below them
- transformed / clipped / nested multicol containers preserve the correct containing block chain

This is the key to the `abspos-*`, `fixed-*`, and `oof-*` failures.

### Phase F: Paint from fragment geometry, not guessed container height

Column rules should use produced fragmentainer geometry:

- per-group used block-size
- exact gap centers
- actual fragmentainer extents

That avoids rule-height and nested balancing mismatches.

## Recommended Code Restructuring

### 1. Split `layout_multicol.cpp` into clearer responsibilities

Suggested internal structure:

- `layout_multicol_measure.cpp`
- `layout_multicol_fragment.cpp`
- `layout_multicol_positioned.cpp`
- `layout_multicol_paint.cpp`

If file splitting is too large for the first step, keep one file but separate the logic into functions with those roles.

### 2. Add a multicol-specific layout state object

Add a temporary state struct owned by `LayoutContext` during multicol layout:

- active column group
- active fragmentainer
- remaining extent
- break candidates
- placeholder anchors for positioned descendants

This is preferable to passing several loose scalars and mutating `advance_y`.

### 3. Preserve fragment identity during comparison/debugging

Even if fragments stay internal, Radiant should retain enough metadata to debug:

- child fragment index
- originating fragmentainer
- whether the node is a continuation
- whether it starts after a spanner

This will make WPT investigation much easier.

## Implementation Plan

### Stage 1: Fragmentainer skeleton

Implement:

- `ColumnGroup`
- `ColumnFragment`
- fragment-aware flow cursor
- sequential `column-fill:auto`

Expected benefit:

- immediate improvement in `multicol-breaking-*`
- better `column-height-*`
- cleaner nested geometry

### Stage 2: Spanners and group splitting

Implement:

- true spanner split/restart behavior
- post-spanner continuation
- fragment-aware height accumulation around spanners

Expected benefit:

- largest gain in `multicol-span-all-*`
- unblocks `multicol-span-all-children-height-*`

Current implementation status:

- Same-BFC direct descendant spanners inside a block child now split that child into pre-spanner and post-spanner column groups.
- Spanners inside BFC roots, inline-blocks, tables, flex, and grid remain isolated and do not escape to the ancestor multicol flow.
- Group height accounting now uses fragment extents through `ColumnGroup` and `FragmentedFlowCursor`, so post-spanner content resumes below the full-width spanner.
- `break-before: column` / page-break aliases are now stored on block props and force cursor breaks, including overflow columns.
- Split containers now distinguish their visual fragmented border-box union from the flow extent they contribute upward; `multicol-span-all-children-height-004a` and `004b` now pass.
- Explicit `column-count:1` containers now remain multicol-aware for spanner splitting while otherwise using normal flow; `multicol-span-all-children-height-005` now passes in the full suite.
- Remaining work: relayout escaped spanners and their descendants at full-width constraint, and materialize continuation block boxes for a single DOM block split across columns.

### Stage 3: Balancing pass

Implement:

- measurement pass
- balancing target search
- commit pass

Expected benefit:

- `multicol-fill-balance-*`
- `multicol-rule-nested-balancing-*`
- many `multicol-nested-*`

Current implementation status:

- Column groups now run a lightweight target-height search over measured block heights and forced break points before committing placement.
- The CSS initial value for `column-fill` is corrected to `balance`.
- Fragmented monolithic children now store per-column `LayoutFragmentBox` records on the DOM/layout element and expose them in debug JSON as `layout.fragments`.
- Balanced definite-height multicol containers can now fragment a tall single child into a browser-like fragmented border-box union; `multicol-rule-nested-balancing-001` now matches browser geometry.
- Nested multicol projection now composes parent and child fragmentainer slots, including descendant block continuations inside nested columns; `multicol-fill-balance-003` now passes.
- Balanced column groups now allow oversized in-flow blocks to split at the searched fragmentainer target instead of forcing the target back to the monolithic child height; `abspos-after-spanner-static-pos` now matches the browser's pre-spanner 35px/35px split.
- Split wrappers with descendant spanners now compute browser-like fragmented border-box unions for fixed-height content-box, decorated, and single-column auto-fill cases; `multicol-span-all-children-height-004a`, `-005`, and `-006` now pass.
- Recursive nested continuation projection now uses source-order flow offsets for sub-slots inside a parent fragment, avoiding double application of earlier local multicol redistribution.
- Full `wpt-css-multicol` improved from `103 / 362` to `131 / 362` after Stage 3 fragment metadata, union placement, nested descendant continuation projection, split-container visual union accounting, and single-column spanner splitting.
- Remaining work: replace fragment metadata with materialized continuation boxes for painting, hit testing, and full `getClientRects()` behavior, then continue into positioned descendant fragment anchors.

### Stage 4: Positioned descendants in multicol

Implement:

- fragment anchors
- static-position resolution per fragmentainer
- spanner-aware containing block logic

Expected benefit:

- `abspos-*`
- `fixed-*`
- `multicol-oof-*`

Current implementation status:

- Multicol now runs a positioned-descendant post-pass after column group placement.
- Direct out-of-flow children with auto/static insets resolve to the next or previous in-flow fragment anchor, using the multicol container's normal-flow absolute origin.
- Nested out-of-flow children with auto/static insets now resolve against their own in-flow sibling context inside multicol, including previous siblings that were fragmented across columns.
- Positioned descendants nested under `column-span: all` now get a spanner-aware explicit inset correction, converting viewport/root offsets back into the containing block coordinate space used by existing abspos serialization.
- Empty wrapper blocks that contain only escaped spanners now collapse to the post-spanner continuation slot while the escaped spanner keeps contributing to the multicol flow.
- `abspos-after-spanner`, `abspos-containing-block-outside-spanner`, `abspos-autopos-contained-by-viewport-000`, and `abspos-autopos-contained-by-viewport-001` now pass.
- `abspos-after-spanner-static-pos` now passes: the pre-spanner block is split into balanced fragments, the spanner is positioned on the browser-like split boundary, and the out-of-flow static-position box anchors to the following in-flow fragment.

Remaining Stage 4 follow-up:

- Generalize the positioned pass into a dedicated `layout_multicol_positioned.cpp` module once the current monolithic file is split.
- Share containing-block lookup with `layout_positioned.cpp` and `view_pool.cpp` so layout, render, and debug JSON use one spanner-aware coordinate model.
- Extend the same anchor model to fixed-position descendants, transformed containing blocks, clipped containers, and materialized continuation boxes.
- Continue replacing metadata-only continuation records with materialized continuation boxes so positioned layout, painting, hit testing, and future CSSOM-style geometry all consume the same fragment tree.

### Stage 5: Paint/rule cleanup and edge behavior

Implement:

- paint from fragment geometry
- rule placement from computed column group extents
- cleanup of special-case inline redistribution path

Expected benefit:

- `column-rule-*`
- nested rule tests
- better maintenance story

## Prioritization

Highest-value order:

1. fragmentainer skeleton
2. spanner split model
3. balancing pass
4. positioned integration
5. paint cleanup

Do not start with micro-fixes to individual failing tests. The current failures are mostly consequences of the same structural gap, and patching them one-by-one will likely create regressions.

## Risks

### Risk 1: Regressing baseline block layout

Mitigation:

- keep multicol behavior isolated behind `is_multicol_container()`
- reuse existing block/inline layout code through a fragment-aware cursor rather than duplicating layout engines

### Risk 2: Fragment continuations complicate the view tree

Mitigation:

- keep fragment records as layout-time structures first
- only materialize continuation metadata when comparison/rendering actually needs it

### Risk 3: Positioned layout interactions become hard to debug

Mitigation:

- add searchable multicol debug logs for fragment anchors and containing block resolution
- expose fragment identity in debug JSON when a multicol test runs

## Success Criteria

The redesign is succeeding when:

- most new gains come from `span-all`, `breaking`, `nested`, and `positioned` clusters together
- failed tests stop being dominated by element-structure mismatches
- the special inline redistribution path becomes removable
- `layout_multicol.cpp` no longer relies on "layout once, shift later" as its core mechanism

## Recommendation

Proceed with a real multicol fragmentation architecture, not incremental coordinate patching.

The current implementation was a reasonable bootstrap, but the WPT result shows it has reached its ceiling. The remaining failures are exactly the class of problems that fragmentainers, column groups, and fragment-aware containing block resolution are meant to solve.
