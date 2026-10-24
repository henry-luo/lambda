# Word typing responsiveness

## Milestone

Make consecutive native keystrokes keep up in the blank document and Studio
example, without losing text, caret position, scroll position, or Undo/Redo.
Keep the ribbon's unchanged controls alive while document text changes.
Performance evidence uses release builds; correctness fixtures have no timing
thresholds. Font enumeration, template state, DOM reconciliation, and repeated
layout are the owning stages investigated with event timings and native CPU
samples.

Acceptance cases:

- individual physical keys, including spaces and arrow-key editing;
- stable ribbon, font selector, and scrollport identities during typing;
- caret and scroll retention, Undo/Redo, and typing after a page break;
- state initializers run once per instance, including initialized nulls, in
  interpreted and MIR execution;
- retained input values, controlled values, metadata observers, CSS attribute
  selectors, and structural changes still update;
- focused checks followed by Lambda and Radiant baseline gates.

## Implementation

**S12.1.3** keeps template bodies pure and mutation in handlers. State defaults
are evaluated only when the central state store has no value for that instance.
Both execution tiers use the existing presence predicate; a stored null is an
initialized value. Word render calculations live in a projection function so
handler activation does not repeat them. Unchanged selection results do not
assign state again.

**D4.5.3 / D4.5.1v4** govern the native presentation boundary. Reconciliation
retains equal branches and advances their evaluator identities together. Changed
elements form the replacement frontier. String metadata can update retained
ancestors; style dependencies keep the ordinary cascade path. CSS-independent
data/ARIA writes still notify observers and advance the DOM mutation epoch.
Structural changes and unsupported attribute values keep the enclosing subtree
replacement path. Native correspondence is checked before retaining branches;
anonymous table fixups and nested updates can change it independently of value
equality. SVG, scene3d, and map viewports remain atomic source resources.
Editing hosts retain their native focus while changed text uses the existing
range rebasing and source-selection completion. Allocation failure falls back
before identity handoff. Rebuilds request repaint explicitly, including when
retained hover/focus owners need no interaction transition.

One-for-one presentation swaps preserve surrounding range offsets and transfer
interior endpoints through the existing node correspondence service. Range
matching uses authored ancestry, excluding anonymous table groups and generated
nodes. Transferring equivalent presentation nodes preserves the logical
selection revision; live shape/table fields can keep their document selection
while retaining control focus. Ordinary DOM removals use their existing
removing steps.

Structural selectors test their ancestor relation before an expensive nth-sibling
scan. This prevents table banding rules from repeatedly scanning font options.
The ruler avoids writing unchanged accessibility values on its geometry frames.
**D7.2.5** keeps that application policy in Lambda.
Re-hitting the same retained hover target performs no pseudo-state transition or
style refresh; resting the pointer on the ribbon must not invalidate it per key.

Floating-toolbar placement updates presentation geometry directly. It does not
assign application state or restore a source selection from a geometry frame.
Frame handlers also commit pending scroll requests when they make no DOM
mutation; page/search navigation must not depend on an incidental ruler write
to flush scrolling. Scroll requests schedule presentation work separately from
box-flow invalidation. The central geometry checkpoint validates viewport,
font, style and native-input dependencies before using existing scroll
geometry. The interactive renderer and EventSim use that same checkpoint.
Flow retains its normal style and constraint validation, including legacy
controls without frozen computed-style records.

## Reproduction and verification

`python3 test/demo/word/benchmark_typing.py --repeat 3 --characters 120`
requires a release build. It records the wall time of each physical input task,
including its event-loop drain, in `temp/word/performance/`. It excludes startup
and outer host painting. Compare runs without concurrent compilation or tests.
This is a diagnostic benchmark, not a machine-dependent CI timing assertion.

Correctness lives in `test/demo/word/tests/typing.json`,
`test/ui/template_state_lazy_init.json`, `test/ui/projection_frontier.json`, and
the structural-selector regression. The Word runner includes the typing case.

Release measurements on 2026-10-24 used a 1440 × 900 viewport, with no concurrent
compilation or tests. Durations include event handling and its event-loop drain;
they exclude startup and outer host painting.

| Document / run | Physical keys | Median | 95th percentile |
| --- | ---: | ---: | ---: |
| Original blank document | 62 | 313.901 ms | 323.519 ms |
| Updated blank document, three runs | 360 | 52.924 ms | 58.207 ms |
| Updated Studio document, three runs | 360 | 59.395 ms | 64.858 ms |
| Updated blank document, sustained run | 600 | 61.859 ms | 105.099 ms |
| Updated Studio document, sustained run | 600 | 69.265 ms | 121.480 ms |

The comparable blank-document median improved by 5.9×. All benchmark text
assertions passed. Sustained runs still show paragraph-length sensitivity:
median durations for the first and last 100 keys were 53.111 / 69.596 ms in the
blank document and 60.724 / 75.732 ms in Studio. These measurements establish an
improvement for these documents, rather than a latency guarantee for arbitrary
document sizes. Reproduce the sustained case with `--repeat 1 --characters 600`.
Raw fixtures, task timings and summaries are under `temp/word/performance/`, with
the `responsive` and `responsive-long` prefixes.

Qualification:

- Word: **168/168 checks passed**: 87 model checks across the execution tiers and
  81 native/export checks. The final native run includes rapid typing, spaces,
  arrow-key edits, scroll retention, Undo/Redo, page-break typing, table selection,
  live table/shape fields, navigation and exports. Combined provenance is in
  `word-qualified-final.json`.
- Radiant: all non-render baseline groups passed. The render adapter timed out
  during the aggregate run; its isolated retry passed (207 passing, four existing
  partials and one skip). Combined results are **6,147 passing, 141 existing
  partials, two skips and no unresolved failures**. The final focused checks also
  passed all 79 DOM range cases and three geometry/correspondence cases.
- Template state and retained-projection fixtures passed in interpreted, automatic
  and MIR modes. CSS selector and attribute-fallback dependency regressions passed.
- Input baseline: **2,112/2,112 passed**. The Lambda baseline retains seven
  independently reproduced issues: one existing PDF golden mismatch, a missing
  DOCX fixture, and five procedural Word export helpers discovered in expression
  mode. Those helpers pass through the Word runner's procedural invocation. An
  additional corpus timeout passed on an isolated retry. These issues were not
  masked or changed for this work.
- The Radiant integer-cast lint, diff whitespace checks and formal-index check
  passed. The index check reports the existing duplicate DO25 warning.
