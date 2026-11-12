# Sheet Formula Calls

## Current checkpoint

- **Goal:** expand the existing Excel formula profile substantially while
  preserving dynamically dispatched Lambda calls (**S12.3.4v2**, **D7.2.2**).
- **Worktree:** Lambda-opus `master` at `72273d32c`, 2026-11-12. Preserve
  unrelated pre-existing changes to `test/README.md` and paged-export notes.
- **Design:** parse spreadsheet syntax into a bounded call plan; dispatch each
  supported spreadsheet function to its own pure Lambda function. Retain cell
  references, cycle detection, error propagation, and imported cached-result
  invalidation. Handle conditional formulas lazily.
- **Scope:** prioritize wildcards and multi-condition aggregates, modern
  lookups, common text/math/statistics, and date functions where the 1900/1904
  workbook epoch can be preserved correctly. Record remaining limitations.
- **State:** 127 named functions cover criteria and wildcards, lookups, dates,
  text, math, statistics and finance. Cross-sheet scalar/range references,
  cycle detection and CSV/AutoSum use workbook context. XLSX import retains
  its 1900/1904 epoch. Excel's left-to-right exponent precedence is restored.
- **Verification:** 146 extended assertions pass on forced interpreter, JIT,
  and forced/poisoned-GC JIT. Eight focused Lambda scripts pass; Sheet UI
  replays pass formula functions 7/7, cross-sheet XLSX 14/14, editor 23/23,
  and workbook files 12/12. Test logs are `temp/sheet-expanded-*`.
- **Outstanding:** the workbook fixtures are synthetic; dynamic arrays,
  structured/external references, locale-formatted date text, and the full
  Excel function/error model remain outside this bounded implementation.
  Prior aggregate build approval was rejected because it writes sibling-project
  outputs; do not bypass that rejection.
- **Next:** final audit of docs, fixtures, and worktree; report the remaining
  limits and the separately blocked aggregate baseline checks.
