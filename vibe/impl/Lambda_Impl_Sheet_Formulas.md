# Sheet Formula Calls

## Current checkpoint

- **Goal:** broaden Sheet's Excel formula profile and compile formula syntax
  into dynamically dispatched Lambda function calls, using `call(f, args)`
  (**S12.3.4v2**, **D7.2.2**).
- **Worktree:** Lambda-opus `master` at `40d5f25a1`, 2026-11-12. Preserve the
  pre-existing XLSX changes and all unrelated worktree changes.
- **Design:** parse spreadsheet syntax into a bounded call plan; dispatch each
  supported spreadsheet function to its own pure Lambda function. Retain cell
  references, cycle detection, error propagation, and imported cached-result
  invalidation. Handle conditional formulas lazily.
- **Scope:** add common arithmetic, statistical, text and logical functions;
  document the exact supported subset and Excel compatibility limits.
- **State:** parser builds callable closures; each of 50 registered functions and
  operator dispatches with `call`. Lazy IF/IFERROR/IFNA and error inspection
  use thunks. Range shape supports INDEX/MATCH/VLOOKUP/HLOOKUP, conditional
  aggregates and SUMPRODUCT; scalar references preserve aggregate coercion.
  Existing XLSX fallback fixture uses unsupported XLOOKUP. Dynamic calls are
  capped at 16 arguments per **S12.3.1**, checked as spreadsheet errors.
- **Verification:** the final 75 formula assertions pass under forced T0,
  JIT, and JIT with forced/poisoned GC. Seven focused Lambda scripts pass;
  the new UI replay passes 5/5 assertions, XLSX opening 10/10, and the
  existing editor 23/23. Earlier file and ribbon UI replays passed 12/12
  and 7/7. Logs are under `temp/sheet-formula-*`.
  The previous baseline build was rejected by automatic approval review because
  it would write build outputs and logs in sibling repositories.
- **Next:** aggregate baselines remain separately blocked pending permission;
  the bounded Excel compatibility profile is documented in the demo README.
