# Sheet Formula Calls

## Current checkpoint

### Clock and random formulas, 2026-11-19

- **Status:** direct generative formula evaluation implemented.
- **Goal:** add `TODAY()`, `NOW()`, Excel `RAND()` and the `RANDOM()` alias,
  including generative evaluation and Sheet rendering (**S12.1.3v6**,
  **S12.1.4v7**, **S12.1.6v3**).
- **Calculation:** the generative loop executes each reached call through its
  `@clock` or `@random` procedure. It threads results and their cache through
  argument evaluation, lazy branches, handled errors and cross-sheet references.
  The cache keys include the sheet, cell and invocation path. Spill passes can
  settle geometry using the same readings. A new recalculation starts with an
  empty cache. This follows the snapshot and effect bounds in **S9.1.4v2** and
  **S12.1.4v7**.
- **Rendering:** conditional styles are computed for visible and frozen
  windows before constructing elements. Render-time volatile calls have fresh
  samples, and references to workbook cells use the workbook calculation.
  Effects remain passive (**S12.1.3v6**).
- **Verification:** 21 Sheet, editor-snapshot and virtual-grid scripts pass on
  both interpreter and JIT. The effects, spills and conditional formula scripts
  also pass with forced GC and freed-memory poisoning on both tiers. Seven
  Sheet UI replays pass 67 assertions across effects, conditional formatting,
  formulas, dynamic arrays, split panes and XLSX layout.
  The effects fixture covers both date systems, lazy branches, error recovery,
  same-sheet and cross-sheet dependencies, independent random call sites,
  conditional formulas, random spills and an actual volatile spill cycle.
- **Prior baseline:** the full Lambda baseline completed 6094/6116 tests. After
  fixing the DOM provider test's obsolete path-entry type, the 14 failed
  Lambda script cases all passed in an unrestricted rerun. Two of three server
  failures also passed with loopback access. The remaining server client and
  five JavaScript DOM cases still fail; see **LR12-63** and the existing DOM
  media playback record. These cases do not exercise the Sheet demo.

- **Goal:** expand the existing Excel formula profile substantially while
  preserving dynamically dispatched Lambda calls (**S12.3.4v2**, **D7.2.2**).
- **Worktree:** Lambda-opus `master` at `baea1b16c`, 2026-11-12. Preserve
  unrelated pre-existing changes to `test/README.md` and paged-export notes.
- **Design:** parse spreadsheet syntax into a bounded call plan; dispatch each
  supported spreadsheet function to a Lambda callable. Retain cell
  references, cycle detection, error propagation, and imported cached-result
  invalidation. Handle conditional formulas lazily.
- **Scope:** prioritize wildcards and multi-condition aggregates, modern
  lookups, common text/math/statistics, and date functions where the 1900/1904
  workbook epoch can be preserved correctly. Record remaining limitations.
- **State:** 159 named functions cover criteria and wildcards, lookups, dates,
  text, math, statistics and finance. Cross-sheet scalar/range references,
  cycle detection and CSV/AutoSum use workbook context. XLSX import retains
  its 1900/1904 epoch. Excel's left-to-right exponent precedence is restored.
  The current expansion adds paired regression and covariance, exclusive
  percentiles, rounding multiples, depreciation and workday calendars. Workday
  offsets use binary search; holiday dates are deduplicated.
- **Verification:** 67 new formula assertions pass on interpreter and forced /
  poisoned-GC JIT. Nine focused script goldens pass. UI replays pass formula
  functions 9/9 and XLSX 15/15, including an imported FORECAST.LINEAR formula
  whose computed value differs from its cached value. Logs: `temp/sheet-more-*`.
  Previously, 146 extended assertions passed on forced interpreter, JIT,
  and forced/poisoned-GC JIT. Eight focused Lambda scripts pass; Sheet UI
  replays pass formula functions 7/7, cross-sheet XLSX 14/14, editor 23/23,
  and workbook files 12/12. Test logs are `temp/sheet-expanded-*`.
- **Outstanding:** the workbook fixtures are synthetic; dynamic arrays,
  structured/external references, locale-formatted date text, and the full
  Excel function/error model remain outside this bounded implementation.
  Prior aggregate build approval was rejected because it writes sibling-project
  outputs; do not bypass that rejection.
- **Next:** extend the bounded profile when prioritized; preserve the remaining
  limits and separately blocked aggregate baseline checks in reports.
