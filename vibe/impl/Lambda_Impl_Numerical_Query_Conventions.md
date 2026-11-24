# Numerical and Query Conventions

## Query migration completion (2026-11-17)

- Scope: migrate all six query functions, nullable return relations and
  error effects under S7.10.7. Numerical changes are ledger work only.
- Authority: S4.2.5, S7.1.2v2, S7.10.2v2 and S7.10.7. Working records are
  Number_Model §1.1 and Sys_Func RF3D/RF3E.
- Checkout: master, HEAD 1323612342784e3d8240a2d8342df89e271fecff plus the
  previously implemented map-concatenation change. Preserve that change and
  the user's move of vibe/vibe/Lambda_Review2.md to vibe/Lambda_Review2.md.
- Numerical policy: NaN/infinity for numerical domain outcomes; ordinary
  errors for incompatible types/shapes and failed structured computations.
  Keep null absence, conversion/admission and sized wrapping contracts.
- Query policy: the six named functions return null for invalid non-error
  inputs; valid empty results retain their normal shape. Preserve error
  operands and system faults rather than converting them to misses.
- Audit host: temp/numerical-query-audit/lambda-before, local debug build,
  SHA256 cadaf6c1f8fcf951e90e71f86dc6b97c2a9bb25433ad4f74f7e152ff6fe11eff.
- Evidence: temp/numerical-query-audit/{_cases.ls,_extra.ls,interp.txt,jit.txt,
  extra-interp.txt,extra-jit.txt} and matching stderr files. All 66 observations
  agree between forced interpreter and forced JIT. These are observations of
  current behavior, including defects, not passing conformance tests.
- Query implementation: input rejection now returns null in all overloads;
  selectors are validated before shape lifting. Fault paths and error operands
  remain distinct. Registry rows carry a null-on-invalid policy, shared AST
  typing derives nullable result relations, and MVP reuses those contracts.
  Null sources still require valid selectors before returning empty results.
- Error effects use ordinary_outcome_has_error: an unknown inferred type is
  not evidence of an error, while explicit any/error parameters and real
  producer errors remain visible (S7.5.1v6). Fused len(find(...)) maps query
  absence to zero, matching the materialized expression.
- Caller migration: editor/format/source_text.raw uses `or ""` at its text
  boundary. The inferred-string error regression now uses int conversion,
  which still produces ordinary errors, instead of an invalid slice.
  Regexredux normalizes input to string at its input boundary and coalesces
  the first replacement into its typed string local. Its output matches the
  pre-migration host and golden exactly, excluding the timing line (SHA256
  bf30de15ef9c7b81fa985930326df1448c709737e796b4e2360a67d0cf81bbc1).
- Focused verification: 60/60 runs over 16 fixtures, interpreter and JIT where
  supported, default GC and LAMBDA_GC_FORCE_EVERY=1. Evidence is under
  temp/query-migration/, especially focused-final.json and matching output.
- Candidate host SHA256:
  4b29fd2337eb4d128c8566535ee43a04d9d4f722230c9b89a1a5f68d1840d648
  (pinned debug host at temp/query-migration/lambda-candidate).
- Release host SHA256:
  a312419db19568fcf1958cf27d7fd13b89f9b69731c035e65b6590a214649fb3
  (pinned at temp/query-migration/lambda-release). The main query regression
  also passes all four release interpreter/JIT/default/forced-GC runs;
  see release-smoke.json.
- Lambda baseline: 7,571/7,574 passed in the aggregate run, including
  2,443/2,444 Lambda scripts. One obsolete MIR fixture still expected invalid
  slice offsets to be errors. It now checks empty join fragments and an
  explicit propagated error without relaxing its MIR checks; the complete
  MIR emission suite subsequently passed 595/595 on the pinned candidate.
- Two failures remain outside the migrated query paths: proc_sync_rtc needs
  unavailable WebRTC support (also reproduced with the pre-migration host);
  native EditBridgeSessionTest.ExecAppliesRichTextBlockCommands fails its
  block-wrap/lift assertions in unchanged editor-bridge code. No old native
  test binary was available to establish whether that failure predates this
  work. Evidence: lambda-summary-final.json, lambda-gtest-final.json,
  edit-bridge-final.json and mir-emission-final.log under temp/query-migration/.
- Test262 baseline: 40,261/40,261 fully passed, zero regressions, 2,652
  skipped outside the selected baseline. The standard target rebuilt the
  release host and completed its exception-catalog gate; see
  temp/query-migration/test262-baseline.log.
- Documentation checks: git diff --check and the generated formal index
  check pass. Query migration is complete; the two unrelated Lambda gate
  failures above remain visible rather than being treated as passing.
- The numerical follow-up was completed on 2026-11-24. See the
  [fixed record](<../Lambda_Issue_Ledger (fixed).md#numerical-domain-outcomes>).

## Numerical findings

The table records the pre-fix reproductions. The 2026-11-24 follow-up
implemented S4.2.5 and S7.10.5v3 for these paths and S4.1.2 for the
integer power overflow case. All focused checks passed on both tiers
with ordinary and forced garbage collection. The final Lambda baseline
passes 6,128/6,128. The pinned Test262 repeat reports zero regressions,
with 40,258 cases fully passing and three passing on isolated retry.

| Operation | Reproduced current result | Required change |
|---|---|---|
| `round(decimal.nan)` / `round(decimal.inf)` | ordinary error | Preserve decimal NaN/infinity as numerical results. |
| `round(1e100m)` | ordinary error | Preserve the finite integral result rather than failing in decimal quantization. |
| Decimal `floor`, `ceil`, `trunc` on infinity | decimal NaN | Preserve signed infinity. |
| `math.norm([nan, 1])` | ordinary error | Return NaN for numeric NaN input. |
| `math.median`, `variance`, `deviation`, `quantile` on a NaN-containing sample | ordinary error | Accept NaN as numeric data and apply each operation's numerical NaN behavior. |
| `sign(nan)` / `sign([nan, 1])` | `0` / `[0, 1]` | Preserve NaN; choose an array lane able to represent it. |
| Unary vector math on a nonnumeric lane, e.g. `math.sqrt(["bad", 1])` | `[nan, 1]` | Reject incompatible element types instead of inventing numerical NaN. |
| `clip([1, "bad"], 0, 2)` / `sign([1, "bad"])` | `[1, nan]` / `[1, 0]` | Distinguish invalid element types from numerical domain outcomes. |
| `math.norm("bad")` | `0` | Reject an unsupported source; it is not an empty numeric collection. |
| `math.median("bad")` / `math.variance("bad")` | `null` | Reject an unsupported source; retain null only for a valid absent result. |
| `9007199254740991 ** 2` | finite float `8.112963841460666e+31` | Existing S4.1.2/SI7 gap: int-domain overflow must saturate to int infinity. |

The domain/validation confusion has two directions. `vector_number_or_null`
(`lambda/runtime/lambda-vector.cpp`) rejects every converted NaN, so the
statistical callers mistake a valid numeric value for invalid input.
`fn_math_norm` makes the same value-based check separately. In contrast,
`vec_unary_math`, `fn_clip` and `fn_sign` convert unchecked lanes through
`item_to_double`, so a conversion failure becomes an apparently valid NaN or
zero. Classification must check the input type separately from its numeric
value; use the existing checked conversion boundary where applicable
(LR03-5), rather than adding per-function exception lists.

`decimal_round` in `lambda/runtime/lambda-decimal-runtime.cpp` explicitly
deletes a NaN result and returns ItemError after quantization. The operation
also uses a finite-precision quantization context for already-integral large
values. Decimal floor/ceil/trunc require a separate infinity-preservation
check. `fn_sign` uses ordered comparisons for the scalar and writes zero
for NaN in its array path, whose current i64 lane cannot hold poison.
`fn_pow` routes the int/int result through floating `pow` and `push_d`,
bypassing int-domain result closure.

The probes already confirm numerical results for real square-root/log/
inverse-trigonometric domain boundaries, exponential overflow, real fractional
power of a negative number, floating and decimal zero division, scalar
conversions of NaN/infinity, and NaN in `sum`, `mean`, `min`, `max`, and `dot`.
Incompatible arithmetic/dot shapes and singular solve inputs remain errors,
as required. This is a focused audit of core operators, math and reductions;
it does not qualify every geometry, image, random, FFT or physics kernel.

## Query findings

The migration replaces these input rejection outcomes:

| Example | Pre-migration result | Implemented S7.10.7 result |
|---|---|---|
| `slice("abc", 0.5, 2)` | error | null |
| `take([1, 2], -1)` | error | null |
| `drop([1, 2], inf)` | error | null |
| `find(7, "x")` | error | null |
| `replace(7, "x", "y")` | error | null |
| `split(7, ",")` | error | null |
| `split([1, 2], 3)` | error | null |
| `take(null, 1)` / `drop(null, 1)` | error | null |

All public overloads and option-validation paths, including numeric array
split, were migrated. Collection selection lives in `lambda-vector.cpp`;
text selection, pattern-domain checks and string query options live in
`lambda-eval.cpp`. Some validation helpers are also used by checked operations
such as `for_window` and `subview`; changing the shared helper's policy would
silently expand the ruling beyond the six named functions. Their validation
remains separate from each public function's null/error policy.

Registry result/effect changes are in `sys_func_registry.c` and
`build_ast.cpp`; `mvp_plan.cpp` reuses shared query typing. A checked source
with variable integer slice offsets now has a nullable result, rather than
an ordinary-error alternative merely because the offset may be non-finite.
Nullable replacement sources also keep their absence alternative, so plain
string return contracts still validate their outcomes. Valid empty selections,
valid search misses and unchanged successful replacements retain their shapes.

The additional `_nullable_slice.ls` compilation probe declares
`fn piece(s: string, a: int, b: int) string? => slice(s, a, b)`. The old host
rejects it with E208 because the inferred result still admits an ordinary
error; see nullable-slice.{txt,err} under the evidence directory. The migrated
query_total regression now covers this contract and absence coalescing.

## Reference behavior

The formal ruling is Lambda's authority; NumPy provides reference examples:

- [Floating-point handling](https://numpy.org/doc/stable/reference/routines.err.html)
  retains numerical domain results by default and makes warning/exception
  handling configurable.
- [Square root](https://numpy.org/doc/stable/reference/generated/numpy.sqrt.html)
  returns NaN for negative real inputs.
- [Sign](https://numpy.org/doc/stable/reference/generated/numpy.sign.html)
  returns NaN for NaN input.
- [Broadcasting](https://numpy.org/doc/stable/user/basics.broadcasting.html)
  rejects incompatible shapes.
- [Linear solve](https://numpy.org/doc/stable/reference/generated/numpy.linalg.solve.html)
  rejects singular and non-square systems.
