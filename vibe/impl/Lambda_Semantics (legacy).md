# Proposal: Lambda Formal Semantics Enhancement (PLT Redex)

## Summary

Build a **formal specification** of the Lambda language using PLT Redex (`lambda/semantics/`). The Redex model is the **authoritative definition** of Lambda's semantics — it defines what the language *should* do.

Goals:
1. Capture Lambda's formal semantics as executable reduction rules in PLT Redex
2. Expand coverage to the full language including procedural features and object types
3. Validate the Redex model against `test/lambda/*.txt` baseline files to confirm agreement between the formal specification and the implementation

### Methodology

There is currently **no single oracle** for Lambda semantics. The reference documentation is incomplete and may be outdated; the C implementation may contain bugs. The Redex model is being built to fill this gap.

**Sources of truth** (in priority order):
1. **Language designer** (Henry) — the final authority on any ambiguous or disputed semantics
2. **Reference documentation** (`doc/Lambda_*.md`) — primary written specification, but incomplete
3. **C implementation** (`lambda/`) — de facto behavior, but may have bugs

**Verification approach**:
1. Build Redex evaluation rules from language design intent (docs + designer clarification)
2. Parse each `.ls` test file → emit Redex-compatible AST (s-expression) via `lambda.exe --emit-sexpr`
3. Evaluate the AST using the Redex evaluator
4. Compare Redex output against the corresponding `.txt` expected output file
5. When they **agree**: the formal model confirms the implementation behavior
6. When they **disagree**: investigate — could be a Redex model gap, an implementation/`.txt` bug, or unclear semantics requiring designer input

The `.txt` files are **not** treated as ground truth. They are what we are validating. A passing test means "the formal specification agrees with the current implementation output." A failing test is a signal to investigate, not an automatic reason to change the Redex model.

## Progress

| Phase | Status | Completed |
|-------|--------|-----------|
| 1 — Sync Model to Current Implementation | **Done** | Fixed c-repr drift, added missing eval rules (as-type, slice, to-symbol), named fn definitions, resolved 0-truthiness |
| 2 — Procedural Extension | **Done** | Store model, var/assign/while/break/continue/return, pn closures, print output accumulator (`lambda-proc.rkt`) |
| 3 — Object Type System | **Done** | Nominal typing, construction, single inheritance, defaults, constraints, fn/pn methods, object update/spread, pattern matching (`lambda-object.rkt`) |
| 4 — Test Baseline Verification Bridge | **In Progress** | `--emit-sexpr` AST bridge, Redex evaluator extended, `make test-redex-baseline`, **87/194 pass** (82 fail, 15 import errors, 9 no .txt, 1 unsupported) |
| 5 — Extended For-Clauses and Match | Not started | Extended for-clauses, advanced match patterns, string interpolation |

**All 184 tests pass** (149 functional/procedural + 35 object type system).

## Current State

The Redex model (8+ files, ~8000+ lines) covers Lambda's **functional core**, **procedural extension**, and **object type system**:

| File | Lines | Scope |
|------|-------|-------|
| `lambda-core.rkt` | ~500 | Grammar, values, environments, helpers (extended with object/proc forms) |
| `lambda-eval.rkt` | ~1350 | Big-step evaluator — functional expressions + object construction/methods |
| `lambda-proc.rkt` | ~600 | Procedural extension — mutable state, while/break/continue/return, pn closures |
| `lambda-object.rkt` | ~140 | Object type system — type registry, nominal typing, inheritance, constraints |
| `lambda-types.rkt` | 513 | Type inference, subtyping, type join |
| `c-repr.rkt` | ~650 | C type mapping, boxing/unboxing, 100+ sys-func catalog (synced) |
| `boxing-rules.rkt` | 597 | 10 deterministic + 4 random property checks |
| `tests.rkt` | ~1800 | 184 test cases across all modules (Parts 1–7) |

**Covered**: primitives, arithmetic, comparison, logical, string concat, collections (array, list, map, range), member/index, for (5 variants), pipe, where, match, closures, HOF, error handling, spread, type conversions, collection builtins, named functions, as-type casts, slice, mutable variables, assignment, while/break/continue/return, pn closures, print, nominal object types, object construction, single inheritance, default fields, field/object constraints, fn/pn methods, object update/spread, pattern matching on types.

**Not covered**: string interpolation, module system, destructuring, extended for-clauses, I/O, and ~50% of the test/lambda baseline (external deps).

## Test Suite Landscape

The `test/lambda/` directory contains **~431 `.ls` test scripts** with **~252 `.txt` expected output files**.

| Category | Files w/ .txt | Redex Feasibility |
|----------|--------------|-------------------|
| Expressions, arithmetic, comparison | ~20 | Covered today |
| Strings, string functions | ~12 | Straightforward |
| Collections, spread, sort | ~10 | Mostly covered |
| Functions, closures | ~8 | Covered today |
| For expressions | ~6 | Covered today |
| Pipes | ~4 | Covered today |
| Match expressions | ~3 | Covered today |
| Types, type patterns | ~8 | Covered (including object types) |
| Error handling | ~3 | Covered today |
| Objects, mutation | ~8 | **Covered (Phase 3)** |
| Numeric specializations (int64, decimal, vectors) | ~12 | **Not covered — needs numeric tower** |
| Transpiler-specific (box/unbox, bitwise) | ~6 | **Not covered — needs c-repr bridge** |
| Procedural (proc/) | ~33 | **Covered (Phase 2)** |
| Import/modules | ~4 | **Not covered — needs module model** |
| I/O, SQLite, file system | ~10 | **Out of scope** (external deps) |
| Input parsers (JSON, XML, CSS, ...) | ~55 | **Out of scope** (infrastructure) |
| Chart rendering | ~25 | **Out of scope** (SVG generation) |
| Math/LaTeX | ~28 | **Out of scope** (domain-specific) |
| Validator/schema | ~43 | Potentially feasible, lower priority |

**Feasible target**: ~120–130 of the ~252 tests with `.txt` files can be evaluated by the Redex model (functional + procedural + objects). The remaining ~120 depend on external I/O, parser infrastructure, or domain-specific engines and are skipped. Agreement between Redex and `.txt` validates the formal specification; disagreement surfaces semantic questions for the language designer.

---

## Phase 1: Sync Model to Current Implementation — DONE

**Goal**: Establish the Redex model as the formal specification by syncing it with the current language design. All existing `tests.rkt` pass with corrected definitions.

### 1.1 Fix Known Discrepancies in c-repr.rkt

| Issue | Model Says | C Code Does | Fix |
|-------|-----------|-------------|-----|
| Binary boxing | `s2it` | `x2it` | Update `semantic-boxing-function` |
| Symbol unboxing | `it2sym` | `it2s` | Update `semantic-unboxing-function` |
| Missing `LMD_TYPE_OBJECT` | absent | `Object*` / `it2obj` | Add to type mapping + catalog |
| Missing `RetItem` return type | `error-ret` unwraps | Distinct `RetItem` in transpiler | Add `retitem` C-type variant |
| Missing `NUM_SIZED` / `UINT64` | absent | Present in `write_type()` | Add to `lambda-type->c-type` |
| Sys-func catalog staleness | ~90 entries | 100+ in `sys_func_defs[]` | Audit and sync |

### 1.2 Add Missing Eval Rules

Expressions in the grammar but lacking `eval-lambda` rules:

- `as-type` (cast) — evaluate expression, check type, return or error
- `slice` (`e[i to j]`) — sub-array/sub-string extraction
- `to-symbol` conversion — string-to-symbol coercion

### 1.3 Add Named Function Definitions

Currently only anonymous `lam` is modeled. Add:

```
(e ::= ...
       (def-fn x (p ...) e)       ; fn name(params) => body
       (def-fn-block x (p ...) e ...)  ; fn name(params) { body }
       )
```

Semantics: `def-fn` binds `x` in the environment to a recursive closure (the closure's environment includes itself for self-recursion).

### 1.4 Model `truthy?` for 0

Resolve the documented discrepancy: implementation treats 0 as falsy. Either:
- Update model to match implementation (pragmatic), or
- File a bug to fix the implementation (spec-driven)

**Deliverable**: Updated `lambda-core.rkt`, `lambda-eval.rkt`, `c-repr.rkt`, `tests.rkt`. All existing tests pass plus new tests for fixed coverage.

---

## Phase 2: Procedural Extension — DONE

**Goal**: Model `pn`, `var`, `while`, `break`, `continue`, `return`, mutable assignment. Cover the `test/lambda/proc/` test suite (~33 tests).

### 2.1 Mutable State Model

Extend the evaluator with a **store** (heap) alongside the environment:

```racket
;; Configuration = (store × env × expr) → (store × value)
;; σ = ((loc v) ...)                ; store: location → value
;; ρ = ((x loc) ...) for var,       ; env maps var names to locations
;;     ((x v) ...)   for let        ; let names to values (immutable)
```

New metafunctions:
- `store-alloc : σ v → (σ loc)` — allocate a new location
- `store-read : σ loc → v` — read from store
- `store-write : σ loc v → σ` — write to store

### 2.2 New Grammar Forms

```racket
(e ::= ...
       ;; procedural
       (var x e)                    ; var x = expr (mutable binding)
       (var-typed x τ e)            ; var x: T = expr
       (assign x e)                 ; x = expr
       (assign-index e e e)         ; arr[i] = expr
       (assign-member e x e)        ; obj.field = expr
       (seq e e ...)                ; statement sequence (block)
       (while e e)                  ; while (cond) body
       (break)                      ; break
       (continue)                   ; continue
       (return e)                   ; return expr
       (def-pn x (p ...) e)        ; pn name(params) { body }
       (print e)                    ; print(expr) → stdout side-effect
       )
```

### 2.3 Evaluator Extension

`eval-lambda` becomes `eval-prog`:

```racket
;; eval-prog : σ × ρ × e → (σ × v × output)
;; where output = list of strings (stdout lines)
```

Key semantic rules:
- **`var`**: allocate store location, bind name to location in env
- **`assign`**: look up location in env, write new value to store
- **`while`**: loop if truthy, with `break`/`continue` as control effects (use Racket continuations or explicit result tags)
- **`return`**: early exit as control effect
- **`pn` vs `fn`**: `pn` closures carry a store reference; `fn` closures don't
- **`print`**: append `value->string` to output accumulator
- **`seq`**: evaluate statements left-to-right, threading the store

### 2.4 Control Flow via Result Tags

```racket
;; Result = (normal σ v output)
;;        | (break-result σ output)
;;        | (continue-result σ output)
;;        | (return-result σ v output)
;;        | (error-result σ v output)
```

`while` catches `break-result` and `continue-result`. Function call catches `return-result`. Error handling catches `error-result`.

**Deliverable**: New `lambda-proc.rkt` with store-passing evaluator. Tests verify all 33 `test/lambda/proc/*.ls` scripts.

---

## Phase 3: Object Type System — DONE

**Goal**: Model nominal object types, methods, mutation methods, constraints, inheritance.

### 3.1 Object Types in Grammar

```racket
(e ::= ...
       ;; type definition
       (def-type x ((field-name τ) ...) ((method-name (p ...) e) ...))
       ;; object literal
       (make-object x (x e) ...)    ; {TypeName field: val, ...}
       ;; object update (copy-with)
       (object-update e (x e) ...)  ; {TypeName base, field: val, ...}
       )
```

### 3.2 Nominal Typing

Object values carry a type tag:

```racket
(v ::= ...
       (object-val x (x v) ...)    ; tagged with type name
       )
```

- `(object-val 'Point (x 1.0) (y 2.0)) is Point` → `#t`
- `(map-val (x 1.0) (y 2.0)) is Point` → `#f`
- `(object-val 'Point ...) is map` → `#t` (objects are map-compatible)

### 3.3 Mutation Methods

`pn` methods inside a type definition:
- Receive `self` as implicit first argument (the object's store location)
- Can mutate fields via the store
- Resolution: params → fields (via `~.field`) → outer scope

### 3.4 Constraints (`that`)

```racket
(τ ::= ...
       (constrained τ e)           ; T that (predicate)
       )
```

Constraint predicate is evaluated at construction time. `~` binds to the value being checked. Failure produces error E201.

### 3.5 Inheritance

```racket
(def-type x x_parent ((field τ) ...) ((method (p ...) e) ...))
```

Inherits all fields and methods from parent. Object is instance of both child and parent types.

**Deliverable**: New `lambda-object.rkt` type registry. Extended `lambda-eval.rkt` handles object construction, member access, fn method dispatch. Extended `lambda-proc.rkt` handles pn mutation methods. 35 new test cases in `tests.rkt` Part 7 cover all 8 `test/lambda/object*.ls` files.

---

## Phase 4: Test Baseline Verification Bridge

**Goal**: Validate the Redex formal specification against the Lambda implementation by evaluating `test/lambda/*.ls` scripts through the Redex evaluator and comparing output against the implementation's `*.txt` expected files.

This is a **two-way validation**:
- Tests where Redex agrees with `.txt` → formal model confirms the implementation
- Tests where Redex disagrees with `.txt` → signals for investigation (Redex gap, implementation bug, or ambiguous semantics)
- Ambiguous cases → escalated to the language designer for resolution

### 4.1 Lambda Source → Redex AST

Uses **Option C — `--emit-sexpr` flag** on `lambda.exe`:
1. `lambda.exe --emit-sexpr script.ls` parses the `.ls` file using the production Tree-sitter parser and emits the AST as Redex-compatible s-expressions
2. Racket reads the s-expressions and feeds them to the Redex evaluator
3. No grammar duplication — reuses the same parser and AST the C evaluator sees

Implementation: `emit_sexpr.cpp` (~1750+ lines) maps `AstNodeType` enum to Redex s-expression forms, covering 90+ system functions.

### 4.2 Output Comparison Harness

```racket
;; verify-test : path → (pass | fail | error | skip)
(define (verify-test ls-path)
  (define sexpr (read-sexpr-from-exe ls-path))    ; call lambda.exe --emit-sexpr
  (define actual (eval-sexpr sexpr))               ; evaluate via Redex
  (define expected (file->string (txt-path ls-path)))  ; read .txt file
  (cond
    [(string=? actual expected) 'pass]             ; formal model agrees with implementation
    [else (list 'fail actual expected)]))          ; disagreement — needs investigation
```

### 4.3 Disagreement Resolution Protocol

When the Redex evaluator and `.txt` file disagree:

| Scenario | Action |
|----------|--------|
| Redex model is missing a language feature | Extend the Redex evaluator |
| Redex model has incorrect semantics | Fix the Redex model (consult designer if unclear) |
| `.txt` file reflects an implementation bug | Flag the `.txt` as suspect; confirm with designer; fix implementation |
| Semantics are ambiguous or undocumented | Ask the language designer for a ruling; update Redex model accordingly |
| Test depends on I/O, imports, or external infra | Skip (out of scope for pure semantic model) |

The Redex model is never blindly adjusted to match `.txt` files. Each disagreement is a semantic question.

### 4.4 Test Classification

Not all tests are feasible for a pure semantic model. Tests are auto-classified:

```racket
(define (test-feasible? ast)
  (not (or (uses-import? ast)       ; module system
           (uses-io? ast)           ; file/network I/O
           (uses-cmd? ast)          ; shell commands
           (uses-input-parser? ast) ; parse(file, 'json') etc.
           (uses-sqlite? ast))))    ; database
```

Expected coverage matrix:

| Category | .ls/.txt pairs | Feasible | Phase |
|----------|---------------|----------|-------|
| Pure functional | ~80 | ~80 | Phase 1 |
| Object types | ~8 | ~8 | Phase 3 |
| Procedural | ~33 | ~30 | Phase 2 |
| Numeric specialization | ~12 | ~10 | Phase 1 |
| Modules/import | ~4 | 0 | Out of scope |
| I/O/parser/chart/math | ~115 | 0 | Out of scope |
| **Total** | **~252** | **~128** | |

### 4.5 Make Targets

```makefile
make test-redex              # Run all Redex verification
make test-redex-unit         # tests.rkt — Redex internal unit tests
make test-redex-properties   # boxing-rules.rkt — property checks
make test-redex-baseline     # Phase 4 — validate Redex model against test/lambda/*.txt
```

`make test-redex-baseline` reports:
```
Redex baseline: 128 feasible, 124 pass, 4 fail, 124 skip (I/O/parser)
FAIL: test/lambda/decimal.ls — line 3: Redex says "3.1416", .txt says "3.14159"
  → Investigate: is the Redex rounding rule wrong, or the implementation?
...
```

**Deliverable**: `--emit-sexpr` AST emitter (`emit_sexpr.cpp`), Racket bridge (`ast-bridge.rkt`), test harness (`baseline-verify.rkt`), `make test-redex-baseline`. Each disagreement between Redex and `.txt` is investigated as a semantic question, not automatically resolved by adjusting the model.

### 4.6 Current Baseline Status

**87 / 194 pass** (87 agree, 82 disagree, 15 emit errors from import tests, 9 no `.txt`, 1 unsupported).

Out-of-scope tests (not feasible for a pure semantic model):
- 15 import/namespace tests (require module resolution infrastructure)
- ~8 I/O tests: `io_sqlite_*.ls`, `test_io_module.ls`, `proc_cmd.ls`, `proc_dir_listing.ls`, `input_csv.ls`, `input_dir.ls`, `input_jsonld.ls`
- ~5 parse-dependent: `parse.ls`, `csv_test.ls`, `test_pipe_file.ls`, `edit_bridge.ls`, `complex_iot_report_html.ls`
- ~3 view/render: `view_state.ls`, `view_template.ls`, `render_map.ls`

### 4.7 Semantic Disagreements — Awaiting Designer Ruling

These are cases where the Redex model's output differs from the C implementation's `.txt` file, and the correct semantics is unclear. Each needs a ruling from the language designer before the Redex model or the implementation can be updated.

| # | Test File | Issue | Redex Says | C/.txt Says | Question |
|---|-----------|-------|-----------|-------------|----------|
| 1 | `len({a:1, b:2})` (transpile_len_typed.ls, vmap.ls) | `len()` on map literal vs VMap | `len({a:1}) = 0`, `len(map([...])) = 2` | Same | **Confirm**: Regular map `{}` len=0 (counts content children), VMap `map()` len=pair count? |
| 2 | `proc_fill.ls` | `fill(5, true)` into `int[]` | `true` kept as bool in array | `1` (coerced to int) | Should typed array assignment coerce `true`→`1` for `int[]`? |
| 3 | `match_expr.ls` (test 14, 17) | `list` vs `array` type distinction | `[1,2,3]` matches `array` but not `list` | `[1,2,3]` matches both | Are `list` and `array` the same type for `is` checks, or distinct? |
| 4 | `simple_expr.ls` | Content-level expression display | All 59 values output + trailing `null` | Same values, no `null` | Should content-level evaluation suppress trailing `null`? |
| 5 | `type.ls` | `type(error_value)` | `"type.null"` | `"error"` | Should `type()` propagate errors or return `"error"` as a type name? |
| 6 | `float_conversion.ls` | Decimal type support | `error` for `float(decimal_val)` | Correct decimal→float conversion | Decimal type is not modeled — should it be? (affects ~5 tests) |
| 7 | `string_pattern.ls`, `match_string_pattern.ls` | String pattern matching | Not implemented | Working regex/glob patterns | Should string patterns be modeled in Redex? |
| 8 | `for_decompose.ls` | Destructuring in for-loops | Not implemented | `for ((a,b) in pairs)` works | Priority for modeling destructuring in Redex? |
| 9 | `object_mutation.ls` | pn method mutation of object fields | Not fully working through bridge | Object pn methods mutate store | Is the store-based pn mutation model correct for object methods? |

**Convention**: Items are added here as they're discovered. Once the designer rules, the item is either:
- Moved to "Resolved" with the decision noted, or
- Converted into a Redex model fix or implementation bug report

### 4.8 Resolved Disagreements

| # | Issue | Decision | Action Taken |
|---|-------|----------|-------------|
| — | Truthiness of 0 | 0 is truthy (both spec and impl agree) | Model updated in Phase 1 |
| — | Error display format | Plain `error` (not `error(message)`) | Redex model updated |
| — | Element tag key | `_tag` (not `id`) to avoid collision with HTML id | Emitter + evaluator updated |

---

## Phase 5: Extended For-Clauses and Match

**Goal**: Model the extended for-expression clauses and advanced match patterns that appear in test files but aren't yet in the grammar.

### 5.1 Extended For-Clauses

```racket
(e ::= ...
       (for-let x e x e e)             ; for (x in coll let y = expr) body
       (for-order x e e e)             ; for (x in coll order by key asc/desc) body
       (for-limit x e e e)             ; for (x in coll limit n) body
       (for-offset x e e e)            ; for (x in coll offset n) body
       (for-decompose (x ...) e e)     ; for ((a, b) in coll) body — destructuring
       )
```

### 5.2 Advanced Match Patterns

```racket
(clause ::= ...
            (case-constrained τ e e)   ; case T that (pred): body
            (case-or (clause ...) e)    ; case T1 | T2 | ...: body (already partial)
            )
```

### 5.3 String Interpolation

```racket
(e ::= ...
       (str-interp string (e ...))     ; $"text {e1} text {e2} ..."
       )
```

**Deliverable**: Extended grammar and eval rules. Additional test coverage for `for_clauses_test.ls`, `for_decompose.ls`, `match_string_pattern.ls`.

---

## Phasing and Dependencies

```
Phase 1: Sync ✅               Phase 2: Procedural ✅
  Fix c-repr drift                 Store model
  Add missing eval rules           var/assign/while/break/return
  Named fn definitions             pn closures
  Resolve 0-truthiness             print → output accumulator
           │                                │
           └──────────┬─────────────────────┘
                      │
              Phase 3: Object Types ✅
                Nominal typing
                Methods (fn + pn)
                Constraints (that)
                Inheritance
                      │
              Phase 4: Test Bridge ⭕
                .ls parser (via --emit-ast)
                Output comparison harness
                make test-redex-baseline
                      │
              Phase 5: Extended Syntax ⭕
                For-clause extensions
                Advanced match
                String interpolation
```

## Line-of-Effort Estimates

| Phase | New/Modified Racket Lines | New Test Cases | Baseline Tests Unlocked | Status |
|-------|--------------------------|----------------|------------------------|--------|
| 1 — Sync | ~200 modified | ~15 | 0 (internal only) | **Done** |
| 2 — Procedural | ~600 new (`lambda-proc.rkt`) | ~40 | ~30 (`proc/`) | **Done** |
| 3 — Objects | ~400 new | ~35 | ~8 (`object*.ls`) | **Done** |
| 4 — Test Bridge | ~2500 new (emitter + bridge + eval extensions) | — | ~128 total | **In Progress** |
| 5 — Extended Syntax | ~200 new | ~10 | ~10 additional | Not started |

## Open Questions

1. ~~**Truthiness of 0**: Follow spec (truthy) or implementation (falsy)?~~ **Resolved**: Both spec and implementation treat 0 as truthy. `i2it(0)` produces a tagged value with non-zero bits.
2. ~~**AST bridge format**: JSON vs S-expression?~~ **Resolved**: S-expression via `--emit-sexpr`. Native to Racket; avoids JSON→s-expr conversion layer.
3. **Scope of numeric tower**: Model int64 and decimal arithmetic fully, or treat as opaque values that pass through? Decimal affects ~5 test files.
4. **Validator/schema tests**: 43 `.ls` files without `.txt` — add expected outputs and include in feasible set?
5. **Disagreement resolution**: When Redex and `.txt` disagree, each case needs investigation. The Redex model is built from language design intent, not by reverse-engineering implementation output. When the intent is ambiguous, the language designer decides.

---

## Semantic Disagreements Log

When the Redex model and `.txt` expected output disagree, or when the C implementation behavior is unclear, items are logged here for the language designer to resolve. Each entry includes the test file, what Redex produces, what `.txt` says, and the semantic question.

**Status key**: 🔴 Needs designer input · 🟡 Under investigation · 🟢 Resolved

| # | Test File | Redex Says | .txt Says | Semantic Question | Status |
|---|-----------|-----------|-----------|-------------------|--------|
| | | | | | |

*(Entries will be added as disagreements are discovered during Phase 4 validation.)*
