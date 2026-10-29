# Lambda issue-ledger fixes under existing rulings

**Date:** 2026-10-28. **Status:** implementation complete. **Scope:** the 19 reproduced items selected from the live
ledger, plus four small related fixes. No semantic or design ruling changed.

## Result

Twenty-two records are resolved; LR07-40 is partial. Duplicate direct-call
arguments now produce E206. Unknown argument names retain their existing
behavior and remain in the active ledger. Pending-ruling issues were not
implemented.

| Records | Authority | Change and regression coverage |
|---|---|---|
| LR07-22, LR07-28 | S1.6; D3.3.3v3 | Representation-changing stores invalidate inferred element lanes, including list splices, dynamic values, bool specialization and loop binders. Numeric specializations retain their runtime element-tag guard; a generic bool Array cannot prove its members from a container tag. `proc/ledger_lane_widening.ls`. |
| LR07-30 | S1.6; S11.1.3 | A range-annotated mutable binding uses a boxed member carrier, preserving an admitted float's kind. Same fixture. |
| LR07-43 | S12.2.1; D2.4.1–D2.4.3 | Unknown assignments widen an inferred scalar before loop emission. Proven native expressions and enforced scalar return contracts preserve closed numeric/string lanes. JSON fractional accumulation is 3.75, not 3. Same fixture. |
| LR03-36 | S12.2.1; D3.3.3v3 | Compact/N-D literal selection follows widened dependencies through immutable aliases and nested expressions. Direct, let-alias, nested and arithmetic cases are covered in both tiers. Same fixture. |
| LR12-33 | S9.1.3 | Plain container-capable parameters snapshot before a sibling var borrow writes. The var root detaches and refreshes its numeric layout. Generic maps, nominal Box, typed arrays and an indexed array loop share one argument with their plain snapshot. `proc/ledger_cow_value_boundaries.ls`. |
| LR12-35, LR12-40 | S9.1.2 | Member/index reads and operand-returning expressions cross the alias binding boundary. MIR source roots also record possible sharing, so writes through either the copy or the original detach. Includes `or`, `if`, `match`, `that` and member copies. Same fixture. |
| LR12-42 | S1.6; S8.2.1v4 | T0 admits a constant scalar string/symbol index-assignment key through the existing COW setter. Same fixture. |
| LR03-44 | S11.1.1v3; S11.2.1 | Packed array kind checks cannot discharge literal element membership or bracket slots. `ledger_literal_contracts.ls`. |
| LR13-14, LR13-15 | S11.1.6v3 | A zero-slot bracket pattern checks exact length. Present null fields admit null/union contracts while field absence remains a separate decision; zero-minimum run types keep their behavior. Same fixture plus `type_families.ls`. |
| LR03-45 | S11.1.1v3 | Static boundary checking preserves that an initializer is a type value; `let kind: type = int` reaches the type-value boundary. Same fixture. |
| LR02-34 | S2.5.3; S2.5.4v2; S16.6.7v2 | Shared declaration classification distinguishes anonymous function values from named definitions. `ledger_arrow_values.ls`; the formerly parked std closures fixture is active with its existing expected output. |
| LR02-29 | S10.1.2v4 | A provably non-callable pipe body is E312; an unknown body has the same rich E212 on T0 and JIT. Two negative fixtures distinguish static proof from dynamic checking. |
| LR02-30 | S11.1.1v3 | Type-position tokens reuse the production lexer and shared value literal decoders. Datetime, binary, decimal, suffixed and signed singleton types retain their payloads; sized-integer literal descriptors survive T0 alias resolution. `ledger_literal_type_syntax.ls` and both S16 harnesses. |
| LR02-31 | S7.4.2 | Binding and parameter type positions accept `T^` as a value-error union; function-return carets keep their enforcing channel. Same fixture and S16 harnesses. |
| LR10-19 | S7.4.2 | Function completion diagnoses an unlicensed raise before return-type validation. Plain and value-error-return functions have negative fixtures. Existing closure, DOCX, UI and Scene3D callers were migrated to explicit raising or value-error forms. |
| LR07-40, partial | Lambda_Func call contract; S12.3.2 for the dynamic restrictions | Duplicate named/named and positional/named slots are rejected before normalization discards an argument. Positional arguments after named ones still fill the next empty slot. Unknown names are untouched. Two negative fixtures plus `interp_variadic.ls`. |
| LR10-16 | S7.5.1v2 | Error acknowledgement resolves arguments by parameter name, including out-of-order names; the wrong receiving parameter still produces E228. `ledger_named_error_ack.ls` and a negative fixture. |
| LR11-7 | S8.2.2v6; D4.6.1v4 | Symbol documentation now describes NameKey identity and the actual arena allocator, without promising pooling or pointer comparison. |
| LR11-9 | Existing parser severity contract | Input notes log at debug, warnings at warning and real parse errors at error. CSV reads no longer print a false zero-error report. |
| LR13-16 | Existing validator diagnostic contract | Literal mismatch diagnostics use the literal contract formatter rather than only its kind. The CLI now reports expected `"debug"`, instead of expected string/got string. |

Fixture paths in the table are relative to `test/lambda/`, except the std and
S16 harness references. All new Lambda fixtures have expected-result files.

## Emission review

The first qualification exposed excessive widening of closed arithmetic and
unnecessary loss of numeric call-site specialization. Those were corrected:
the shared widening decision uses real native proof or an enforced scalar
return contract, and ArrayNum specializations retain their runtime carrier
guard. Explicit typed stores preserve their element contract, while resize
and escape calls still invalidate layout facts.

Four MIR sidecars changed for intended behavior, retaining their other checks:

- `tune30_inline_arg_interval`: its two loop copies now detach a shared var
  root, adding four pointer masks (4 → 8). The inline/slow-arithmetic ratchets
  are unchanged.
- `tune26_var_readonly_peer`: the plain input's snapshot can alias the var
  destination. The checked miss uses `lambda_array_set_checked_inplace_lane`
  after detachment; dense peer reads remain covered.
- `cw34_rmw_borrow`: one snapshot mark protects the generic plain `val` input.
  Both RMW handles still use `cow_bind_rmw_handle`, with no `cow_bind_var` and
  no extra store-back capture.
- `tune21_var_witness_forward`: an unproven bool Array uses the boxed setter
  in `set_row`; the numeric `taken` witness remains in `place`. Inference
  does not invent an enforced bool[] contract.

Exact module budgets increase by eight instructions for DeltaBlue and three
for Prettier. Each added instruction is `cow_bind_var` at a borrowed binding;
Prettier's `print_node` grows by one. Root/frame ceilings are unchanged. These
are correctness changes, with no performance claim from debug builds.

## Verification

- Six new positive fixtures fail on the preserved pre-fix executable on all
  three tiers (18/18 control failures).
- Six positive and seven negative fixtures pass on interp, jit and auto:
  39/39 normally and 39/39 with forced GC plus freed-memory poisoning.
- C S16 conformance: 428/428. Reference Tree-sitter S16 conformance: 414/414.
  The reference grammar was regenerated from `grammar.js`.
- Focused compatibility checks cover named/variadic calls, occurrence types,
  closures, DOCX, UI and Scene3D. HTTP fixtures pass with loopback permission.
- MIR emission: 279/279 after the reviewed expectation changes; MIR size
  ratchet: 20/20.
- Optimization contracts: 55/55, including the resize-after-COW length cache.
  Explicit scalar stores preserve their lane, while resize/escape calls still
  invalidate cached length and layout.
- The final aggregate run has one HTTP fixture failure: the browser stopped
  after frame loading before completing navigation. An isolated rerun of the
  complete HTTP suite passes 152/152. The final `make test-lambda-baseline` result is **8397/8398** (input 2112/2112, runtime 6285/6286); all other suites passed. Log: `temp/ledger-triage/lambda-baseline-qualified.log`.
- Test262 baseline was not run in this pass. The Lambda baseline's JS suites
  run separately from that gate.
- Ledger archive counts, links to moved issue anchors, and `git diff --check`
  pass. The formal index was regenerated; its existing duplicate-DO25 warning
  remains outside this change.

Local evidence is under `temp/ledger-triage/`: focused/control JSON, S16 logs,
before/after MIR, CSV logs, literal-diagnostic logs and aggregate logs. Those
artifacts are reproducible working evidence, not committed fixtures.
