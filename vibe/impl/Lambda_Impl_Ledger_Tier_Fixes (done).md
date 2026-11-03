# Lambda — Ledger Tier Fixes

**Status:** complete, 2026-11-03. Implemented and qualified in the main checkout
over `793d58bd5`, with concurrent MVP, resource and SQLite work preserved.
The commit isolates this task's changes from that concurrent work.

## Completion record

The user selected saturation for explicit `int()` conversion and fatal E240
in every mode. Formal Semantics adds **S4.1.6** and revises
**S11.4.12v2**, with companion records in Int_Type §5.9 and Type_Enforcement
TE-23. S7.7.4v2 already fixes the reported inferred-string reassignment case;
a regression now pins it. LR04-9 and LR07-41 are archived. LR07-18 retains
only its separate offset/format effect-policy questions.

Qualification is complete: **8,732/8,732** required Lambda/input baseline
checks and **102/102** focused tier/GC/strictness runs pass. There is no
remaining implementation work in this scope. The compiled debug executable
kept the same SHA-256 during final qualification, recorded in
`temp/ledger-tier-fixes/executable-sha256.txt`.

## Root causes and repairs

### Explicit numeric conversion — S4.1.6, S1.6

T0's `fn_int` kept the pre-v5 fallback kinds: float, decimal and i64 when a
value did not fit int53. MIR's native float conversion already saturated.
The runtime now uses the existing int53 saturation/poison boundary for every
successful conversion. Fractions truncate toward zero. Decimal truncation
happens before binary64 conversion, preserving `9007199254740991.9m` as the
largest finite int. Magnitudes beyond i64 saturate by their exact decimal
sign. Numeric poison converts to the corresponding int closure points.

Text validates the complete length-delimited span: optional sign, ASCII
integer digits and surrounding ASCII whitespace. Valid arbitrarily large
integer text saturates. Fractional/exponent text, malformed suffixes,
embedded NULs and unsupported input kinds produce ordinary errors. Existing
error operands propagate unchanged. The registry names int as success, and
the frontend proves decimal numeric conversion infallible.

The focused review also found `int(null)` returning null on MIR through
nullable numeric lanes. Nullable inputs no longer prove clean conversion or
enter the unchecked native shortcut. The checked Item conversion returns
an error on every tier.

The precise int success contract exposed two optimized producers that
returned boxed Items but let the carrier oracle classify them as native int
lanes: identity over an int/error join and the guarded u32-bitwise conversion.
Both now explicitly publish VALUE_REP_ITEM, preserving operand errors. The
existing compact-bitwise MIR check and numeric/profile regressions remain
unchanged and pass.

### Invalid type cycles — S11.4.12v2, S1.6

Strict mode already diagnosed the original `type U = U | int` reproducer.
Relaxed mode downgraded E240, letting the invalid cycle reach execution and
crash all tiers. The common semantic diagnostic policy now keeps E240 fatal,
covering alias, union-value, occurrence, function-signature, pattern and
binary-layout cycle findings. Valid structural recursive types remain usable
through arrays, maps, function calls and membership checks.

### Inferred string outcomes — S7.7.4v2

The original string reassignment case already retains the error and continues
on all tiers after the prior type/error implementation. The new fixture
covers recovery, later appends, a prior snapshot, capture transport and
repeated calls with low promotion thresholds and forced poisoned GC.

## Migration and verification

The precise registry result removes obsolete error propagation from the
numeric PRNG helpers in `json_gen2`, `matmul2`, `quicksort2` and the Tune30
fixture. One now-provably-clean JSON helper call also loses its caret.
The u64 constructor golden intentionally changes its final int result to inf.
Tune19's generic checked-rebind twin now converts through float, preserving
its genuine deferred int-admission check; its MIR sidecar remains unchanged.
Diagnostic metadata expectations now name int success.

| Check | Result | Evidence under `temp/ledger-tier-fixes/` |
|---|---|---|
| `make test-lambda-baseline` with loopback access | 8,732/8,732; exit 0 | `baseline-final.log` |
| Input / runtime split | 2,112/2,112 + 6,620/6,620 | `baseline-final.log` |
| Main script suite | 2,058/2,058 | `baseline-final.log` |
| MIR emission / GC stress / instruction budgets | 285/285 + 355/355 + 20/20 | `baseline-final.log` |
| Diagnostics / numeric optimization | 328/328 + 55/55 | `baseline-final.log` |
| Focused interp/auto/jit × normal/forced poisoned GC | 102/102 | `qualification-final.json`, `focused-final.log` |
| Targeted benchmark/package int-return helpers | 13/13 compile | `conversion-callers.json` |
| Affected MIR, profile and script reruns | 4/4 + 2/2 + 4/4 | `emission-focused.log`, `opt-focused.log`, `fixtures-focused.log` |
| Formal index, ledger anchors/count, paired goldens, whitespace | pass | final document checks |

The focused matrix checks the valid recursive/string/conversion fixtures and
five invalid cycle forms in strict and relaxed modes. It includes low
function/loop promotion thresholds with precise roots and freed-memory
poisoning. Every new script has a paired golden.

The first baseline passed 8,712/8,732: eight socket cases lacked loopback
permission, while the remaining failures identified the migrations and boxed
producer repair above. The final full baseline supersedes that result. An
additional earlier `make build-test` attempt failed at the unrelated PDF test
helper's duplicate `_read_binary_file` symbol; this target is outside the
required baseline and no PDF source is changed here.

Shared files contain unrelated pending work. `owned-paths.json` records this
task's own paths and the files requiring selective staging.
