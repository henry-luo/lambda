# Compile Warning Triage & Cleanup Proposal

## Background

A clean debug build of Lambda currently emits **~82,000 compiler warnings** (Apple clang 17, debug_native config). The volume is high enough that real, actionable warnings are buried in noise — meaning new bugs of the same class can be introduced without anyone noticing.

This document categorizes the warnings, recommends which to silence and which to fix, and proposes a staged cleanup plan.

## Warning Inventory

Output of `make 2>&1 | grep -oE '\[-W[a-zA-Z0-9-]+\]' | sort | uniq -c | sort -rn`:

| Count | Flag |
|------:|------|
| 62,137 | `-Wreturn-type-c-linkage` |
| 5,972  | `-Wc99-extensions` |
| 4,694  | `-Wgnu-anonymous-struct` |
| 4,406  | `-Wnested-anon-types` |
| 984    | `-Wcast-function-type-mismatch` |
| 916    | `-Wmissing-field-initializers` |
| 329    | `-Wgnu-include-next` |
| 328    | `-Winclude-next-absolute-path` |
| 317    | `-Wmissing-braces` |
| 317    | `-Wdeprecated-copy` |
| 251    | `-Wwritable-strings` |
| 250    | `-Wreorder-ctor` |
| 242    | `-Wunused-parameter` |
| 218    | `-Wnon-c-typedef-for-linkage` |
| 176    | `-Wunused-variable` |
| 166    | `-Wpointer-bool-conversion` |
| 143    | `-Wunused-function` |
| 139    | `-Wcomment` |
| 121    | `-Wsign-compare` |
| 72     | `-Wstrict-prototypes` |
| 52     | `-Wignored-qualifiers` |
| 47     | `-Wsingle-bit-bitfield-constant-conversion` |
| 43     | `-Wdeprecated-declarations` |
| 42     | `-Wunused-but-set-variable` |
| 24     | `-Wc11-extensions` |
| 17     | `-Wconstant-conversion` |
| 15     | `-Wtautological-constant-out-of-range-compare` |
| 13     | `-Wenum-compare` |
| 11     | `-Wnewline-eof` |
| 11     | `-Wmismatched-tags` |
| 7      | `-Wundefined-bool-conversion` |
| 7      | `-Wextra-semi` |
| 5      | `-Wswitch` |
| 5      | `-Wformat` |
| 3      | `-Wvarargs` |
| 3      | `-Wunused-const-variable` |
| 3      | `-Wsometimes-uninitialized` |
| 3      | `-Wmacro-redefined` |
| 3      | `-Winvalid-offsetof` |
| 2      | `-Wparentheses` |
| 2      | `-Wmicrosoft-redeclare-static` |
| 1 each | `-Wzero-length-array`, `-Wunused-label`, `-Wtrigraphs`, `-Wreturn-type`, `-Wincompatible-pointer-types-discards-qualifiers`, `-Wgnu-conditional-omitted-operand` |

## Category A — Silence (intentional patterns, ~78k warnings)

These reflect deliberate codebase conventions or come from third-party headers we don't control. Fixing them would be massive churn for no correctness gain.

| Flag | Count | Rationale |
|------|------:|-----------|
| `-Wreturn-type-c-linkage` | 62,137 | `Item` is a tagged 64-bit value returned through `extern "C"` for the MIR JIT ABI (see `lambda/lambda.h`). The pattern is core to the architecture — clang can't verify C layout-compatibility, but it is. |
| `-Wc99-extensions` | 5,972 | Compound literals & designated initializers used in C++ headers per the C+ convention documented in `doc/dev/C_Plus_Convention.md`. |
| `-Wgnu-anonymous-struct` | 4,694 | Anonymous structs in `lambda.hpp`, `view.hpp`, `clip_shape.h`, `event.hpp`, `dom_node.hpp`, etc. — pervasive deliberate pattern. |
| `-Wnested-anon-types` | 4,406 | Same root cause. |
| `-Wnon-c-typedef-for-linkage` | 218 | `typedef struct { … } Name;` with C++ members in `view.hpp`. Cosmetic. |
| `-Wgnu-include-next`, `-Winclude-next-absolute-path` | 657 | From third-party headers (thorvg/mpdecimal etc.). Not our code. |
| `-Wstrict-prototypes` | 72 | Mostly third-party C headers. |
| `-Wignored-qualifiers` | 52 | `const` on return-by-value in `thorvg_capi.h`. Third-party. |
| `-Wnewline-eof`, `-Wmismatched-tags`, `-Wextra-semi` | 29 | Pure style. |
| `-Wmissing-field-initializers`, `-Wmissing-braces` | 1,233 | Aggregate-init style; C++ zero-inits the rest. Safe. |
| `-Wunused-parameter` | 242 | High volume from callback/interface conformance (signal handlers, hashmap compare fns, etc.). Per-site `(void)param` suppression is more churn than value. |

## Category B — Fix (real correctness issues, ~270 warnings)

These flag actual bugs or undefined behavior. Each occurrence deserves inspection.

| Flag | Count | Why fix |
|------|------:|---------|
| `-Wreturn-type` | 1 | Missing return → undefined behavior. |
| `-Wsometimes-uninitialized` | 3 | Uninitialized read on some code paths. |
| `-Wundefined-bool-conversion` | 7 | Comparing `this` or array-decay against null — always true/false. |
| `-Wpointer-bool-conversion` | 166 | Same class. Example: `radiant/script_runner.cpp:174` tests `s->chars` where `chars` is an inline array — the check is dead. Each site is a likely-broken null guard. |
| `-Wtautological-constant-out-of-range-compare` | 15 | Comparison that can never succeed. |
| `-Wconstant-conversion` | 17 | Silent value truncation. |
| `-Wsingle-bit-bitfield-constant-conversion` | 47 | Storing values that don't fit (commonly signed-1-bit). Often a logic bug. |
| `-Wenum-compare` | 13 | Comparing values from two unrelated enums — usually the wrong constant. |
| `-Wswitch` | 5 | Missing enum cases — easy to fix, valuable. |
| `-Wvarargs` | 3 | `va_start` misuse → UB. |
| `-Winvalid-offsetof` | 3 | `offsetof` on non-standard-layout type; can silently break. |
| `-Wformat` | 5 | printf/format-arg mismatch. |
| `-Wparentheses` | 2 | Suspect precedence. |
| `-Wmacro-redefined` | 3 | Two headers disagree on macro definition. |
| `-Wdeprecated-declarations` | 43 | Calling APIs scheduled for removal. Plan migrations. |
| `-Wincompatible-pointer-types-discards-qualifiers` | 1 | Drops `const`. |
| `-Wmicrosoft-redeclare-static` | 2 | Static/non-static mismatch. |

## Category C — Clean up opportunistically (code health, ~2,300 warnings)

Low-risk improvements that can be batched per subsystem.

| Flag | Count | Notes |
|------|------:|-------|
| `-Wunused-function` | 143 | **Dead-code candidates** — either delete or mark `static`. Best signal we have for dead code without running coverage. |
| `-Wunused-variable` | 176 | Trivial deletions. |
| `-Wunused-but-set-variable` | 42 | Variable written but never read — often a leftover from refactoring. |
| `-Wunused-const-variable`, `-Wunused-label` | 4 | Trivial. |
| `-Wreorder-ctor` | 250 | Member-init list out of declaration order. Harmless but indicates the ctor diverged from the struct layout — fix in the ~3 affected files (`DomElement`, etc.). |
| `-Wdeprecated-copy` | 317 | User-declared `operator=` makes implicit copy-ctor deprecated. Add `= default;` copy-ctor in one place per type (e.g. `ConstItem` in `lambda.hpp`) — kills hundreds at once. |
| `-Wwritable-strings` | 251 | `char* s = "literal"` should be `const char*`. Real const-correctness; fix in waves per subsystem. |
| `-Wcast-function-type-mismatch` | 984 | Function-pointer casts between incompatible types, typically around callback registration. Often intentional but UB on strict ABIs — audit when convenient. |
| `-Wsign-compare` | 121 | Most benign; a few hide bugs. Fix per file. |
| `-Wcomment` | 139 | Stray `/*` inside block comments. Trivial. |

## Recommended Plan

### Phase 1 — Silence Category A (1 PR)

Add `-Wno-*` flags to the project compile flags in `build_lambda_config.json` for the patterns we've ratified as intentional:

```
-Wno-return-type-c-linkage
-Wno-c99-extensions
-Wno-gnu-anonymous-struct
-Wno-nested-anon-types
-Wno-non-c-typedef-for-linkage
-Wno-gnu-include-next
-Wno-include-next-absolute-path
-Wno-strict-prototypes
-Wno-ignored-qualifiers
-Wno-newline-eof
-Wno-mismatched-tags
-Wno-extra-semi
-Wno-missing-field-initializers
-Wno-missing-braces
-Wno-unused-parameter
```

**Expected result:** warning count drops from ~82,000 to ~2,500.

### Phase 2 — Promote real bugs to errors (1 PR)

So Category B classes cannot regress:

```
-Werror=return-type
-Werror=sometimes-uninitialized
-Werror=undefined-bool-conversion
-Werror=varargs
-Werror=format
-Werror=switch
-Werror=tautological-constant-out-of-range-compare
-Werror=invalid-offsetof
-Werror=enum-compare
```

Fix any sites these promote into errors before merging.

### Phase 3 — Audit `-Wpointer-bool-conversion` (separate task)

The 166 sites in this class are very likely real bugs — null checks against fields that decay from inline arrays and therefore are always non-null. Worth a dedicated pass per subsystem (start with `radiant/script_runner.cpp`).

### Phase 4 — Dead code audit (separate task)

The 143 `-Wunused-function` sites are the best free signal for dead code. Cross-reference with a `cppcheck --enable=unusedFunction --project=compile_commands.json` pass to catch unused non-static functions across translation units that the compiler can't see.

### Phase 5 — Code-health waves (ongoing)

`-Wdeprecated-copy`, `-Wwritable-strings`, `-Wreorder-ctor`, `-Wsign-compare`, `-Wcast-function-type-mismatch` — clean up per subsystem when touching the relevant code. Don't block on a single sweep.

## Success Criteria

- Clean debug build emits **< 100 warnings** after Phase 1.
- Category B regressions are caught at build time after Phase 2.
- Each phase is independently shippable.
