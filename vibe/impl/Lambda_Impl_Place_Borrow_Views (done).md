# Place borrows and numeric-view snapshots

**Completed:** 2026-11-03. **Rulings:** S9.1.2, S9.1.3v3, S9.2.2v2,
D5.3.1, D5.4.4, D5.4.6; async requalification under D8.3.2.

## Completion record

- Workspace: `/Users/henryluo/Projects/lambda`, `master`, base `6b68618a2`
  plus this task's uncommitted changes. The shared tree also contains
  concurrent MVP/RDB work; those edits were preserved.
- The user approved caller-rooted place homes, writable scalar places,
  partial-error publication and snapshot view values on 2026-11-03.
  S9.1.3v3 / S9.2.2v2 / D5.4.6 and CW32v3/CW33v2 record that decision.
- LR12-10 and LR12-14 are fixed; LR01-17's September report is superseded
  by the October runtime-async implementation and requalified here. All
  three records moved to the fixed issue ledger with their original reports.
- Final debug build and **`make test-lambda-baseline`: 8,718/8,718** passed
  with loopback access (2,112 input + 6,606 runtime).
- Async: 35 executable scripts across interp/AUTO/JIT, plain and
  forced/poisoned GC: **210/210** passed.
- The broader COW/view sweep passed **382/384**; its two failures are the
  existing strict-T0 `AST_NODE_SYS_FUNC` exclusion in `cow_place_mutator`,
  repeated with and without forced GC. AUTO/JIT pass that fixture.
- The promotion fixture also passed all three tiers with function/loop
  thresholds set to 1 and forced/poisoned GC. Its output is
  `[480, 12, 40, 12, 0]`. A separate out-of-range-store probe confirmed
  partial-place publication on E302 across all three tiers under forced GC.
- No engine work remains for these records. The strict-T0 builtin exclusion
  above is outside this task. The formal-index generator retains its existing
  warning that DO25 is defined twice.

## Implementation

`interp.cpp` and `transpile-mir.cpp` transport caller-owned precise Item
homes and rooted, once-evaluated path keys. Scalar leaves use the same
protocol as containers. Both tiers publish replacements through the original
path before the caller consumes the result, including error results.
Unchanged installed leaves skip reinstallation; scalar-only sibling arguments
allow an existing unique leaf to stay in place. A plain container argument
may retain an ancestor, so its home leaf is isolated before the call.
Borrowed keys bind `last` against their actual prefix receiver.

The MIR publication path roots a fresh result before allocation and consumes
a pending return companion before calling a path setter. Direct, imported
and dynamic boxed edges use the same home/publication helpers. Native scalar
adapters preserve their declared lanes. Existing shape-checked field borrows
and the no-repeated-root-prepare optimization remain in use.

`array_num_get` materializes an ordinary row value;
`array_num_get_borrowed` is confined to internal path writers. Numeric clones
materialize views, preserving exact element lanes and logical dimensions.
Public `subview`, `reshape`, `transpose` and `ravel` constructors describe
private source snapshots; their ordinary values cannot update their source.
Read-only views and aliases still use ordinary COW at later value boundaries.
The obsolete shared-base overlap rejection for independently created subview
values was replaced by a positive regression.

Materialized rows exposed an existing packing defect: a vector and a matrix
with equal leaf counts were folded into the same tensor rank, flattening
Kruskal's accumulator. Packing now verifies sibling ranks and rejects list
values as rows. The maze golden remains unchanged. The packed-store MIR
sidecar now requires the internal borrowed index getter in its fallback.

## Qualification and evidence

- Final build: `temp/place-borrow/build-final3.log`.
- Required full gate: `temp/place-borrow/baseline-final.log`, exit 0,
  **8,718/8,718**; `test_output/` holds the individual harness logs.
- Tier/GC sweep: `temp/place-borrow/focused-final2.log` and JSON report,
  **382/384**, with the two strict-T0 exclusions described above.
- Async sweep: `temp/place-borrow/async-verified.log` and
  `async-final.json`, **210/210**; the imported worker helper is not an
  executable regression.
- Focused COW profile gate: `temp/place-borrow/opt.log`, **1/1**.
- The affected emission cases and remaining procart/DOCX failures pass;
  the final full gate includes all of them. Earlier baseline and focused
  logs are superseded by these results.
- New paired fixtures: `place_borrow_homes`, `place_borrow_scalar_homes`,
  `place_borrow_promoted`, `matrix_row_snapshots`, `view_value_borrows`.
  Existing goldens that expected subview write-through were corrected;
  ordinary row metadata now reports owned storage through `is_view`.
