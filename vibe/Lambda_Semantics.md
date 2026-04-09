# Proposal: Lambda Formal Semantics Enhancement (PLT Redex)

## Summary

Enhance the PLT Redex formal semantics model (`lambda/semantics/`) to:
1. Sync with the current Lambda language design and C implementation
2. Expand coverage to the full language including procedural features
3. Generate expected output that can be verified against the `test/lambda/*.txt` baseline files

## Current State

The Redex model (6 files, ~4200 lines) covers Lambda's **functional core**:

| File | Lines | Scope |
|------|-------|-------|
| `lambda-core.rkt` | 479 | Grammar, values, environments, 16 helper metafunctions |
| `lambda-eval.rkt` | 1089 | Big-step evaluator — all functional expressions |
| `lambda-types.rkt` | 513 | Type inference, subtyping, type join |
| `c-repr.rkt` | 641 | C type mapping, boxing/unboxing, 90+ sys-func catalog |
| `boxing-rules.rkt` | 597 | 10 deterministic + 4 random property checks |
| `tests.rkt` | 875 | ~100 test cases across all modules |

**Covered**: primitives, arithmetic, comparison, logical, string concat, collections (array, list, map, range), member/index, for (5 variants), pipe, where, match, closures, HOF, error handling, spread, type conversions, collection builtins.

**Not covered**: procedural features, object types, named functions, string interpolation, module system, type constraints, destructuring, extended for-clauses, I/O, and ~50% of the test/lambda baseline.

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
| Types, type patterns | ~8 | Partially covered (no object types) |
| Error handling | ~3 | Covered today |
| Objects, mutation | ~8 | **Not covered — needs object model** |
| Numeric specializations (int64, decimal, vectors) | ~12 | **Not covered — needs numeric tower** |
| Transpiler-specific (box/unbox, bitwise) | ~6 | **Not covered — needs c-repr bridge** |
| Procedural (proc/) | ~33 | **Not covered — needs Phase 2** |
| Import/modules | ~4 | **Not covered — needs module model** |
| I/O, SQLite, file system | ~10 | **Out of scope** (external deps) |
| Input parsers (JSON, XML, CSS, ...) | ~55 | **Out of scope** (infrastructure) |
| Chart rendering | ~25 | **Out of scope** (SVG generation) |
| Math/LaTeX | ~28 | **Out of scope** (domain-specific) |
| Validator/schema | ~43 | Potentially feasible, lower priority |

**Feasible target**: ~120–130 of the ~252 tests with `.txt` files can be verified by an enhanced Redex evaluator (functional + procedural + objects). The remaining ~120 depend on external I/O, parser infrastructure, or domain-specific engines.

---

## Phase 1: Sync Model to Current Implementation

**Goal**: Fix drift between Redex model and actual C code. All existing `tests.rkt` pass with corrected definitions.

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

## Phase 2: Procedural Extension

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

## Phase 3: Object Type System

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

**Deliverable**: Extended `lambda-types.rkt` with nominal subtyping. Extended evaluator handles object construction, member access, method dispatch, mutation. Tests cover `test/lambda/object*.ls` (~8 files).

---

## Phase 4: Test Baseline Verification Bridge

**Goal**: Run the Redex evaluator on `test/lambda/*.ls` scripts and compare output against `*.txt` files.

### 4.1 Lambda Source → Redex AST

Build a parser that converts Lambda concrete syntax to Redex s-expressions.

**Option A — Tree-sitter bridge** (recommended):
1. Use the existing Tree-sitter Lambda grammar (`lambda/tree-sitter-lambda/grammar.js`)
2. Write a Racket FFI binding or a Node.js script that parses `.ls` files and emits Redex s-expressions as JSON/S-expr
3. Racket reads the s-expressions and feeds them to `eval-prog`

**Option B — Racket parser**:
1. Write a PEG or recursive-descent parser in Racket for Lambda syntax
2. More self-contained but duplicates the grammar definition

**Option C — Lambda `--ast-json` flag** (simplest):
1. Add a CLI flag `./lambda.exe --emit-ast script.ls` that outputs the AST as JSON
2. Racket reads the JSON and converts to Redex terms
3. Reuses the production parser; no grammar duplication

Option C avoids maintaining two parsers. The AST JSON can be verified against `ast.hpp`'s `AstNodeType` enum, ensuring the Redex model uses the same AST the C evaluator sees.

### 4.2 Output Comparison Harness

```racket
;; verify-test : path → (pass | fail diff)
(define (verify-test ls-path)
  (define ast (parse-lambda-file ls-path))
  (define-values (σ v output) (eval-prog empty-store empty-env ast))
  (define expected (file->lines (path-replace-extension ls-path ".txt")))
  (define actual output)  ; list of stdout lines
  (if (equal? expected actual)
      'pass
      (list 'fail expected actual)))
```

### 4.3 Test Classification

Not all tests are feasible. Classify automatically:

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

### 4.4 Make Targets

```makefile
make test-redex              # Run all Redex verification
make test-redex-unit         # tests.rkt — Redex internal unit tests
make test-redex-properties   # boxing-rules.rkt — property checks
make test-redex-baseline     # Phase 4 — verify against test/lambda/*.txt
```

`make test-redex-baseline` reports:
```
Redex baseline: 128 feasible, 124 pass, 4 fail, 124 skip (I/O/parser)
FAIL: test/lambda/decimal.ls — line 3: expected "3.14159" got "3.1416"
FAIL: test/lambda/object_constraint.ls — line 7: expected "error" got "null"
...
```

**Deliverable**: Parser bridge (Option C recommended), test harness, `make test-redex-baseline` integrated with CI.

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
Phase 1: Sync                    Phase 2: Procedural
  Fix c-repr drift                 Store model
  Add missing eval rules           var/assign/while/break/return
  Named fn definitions             pn closures
  Resolve 0-truthiness             print → output accumulator
           │                                │
           └──────────┬─────────────────────┘
                      │
              Phase 3: Object Types
                Nominal typing
                Methods (fn + pn)
                Constraints (that)
                Inheritance
                      │
              Phase 4: Test Bridge
                .ls parser (via --emit-ast)
                Output comparison harness
                make test-redex-baseline
                      │
              Phase 5: Extended Syntax
                For-clause extensions
                Advanced match
                String interpolation
```

## Line-of-Effort Estimates

| Phase | New/Modified Racket Lines | New Test Cases | Baseline Tests Unlocked |
|-------|--------------------------|----------------|------------------------|
| 1 — Sync | ~200 modified | ~15 | 0 (internal only) |
| 2 — Procedural | ~600 new (`lambda-proc.rkt`) | ~40 | ~30 (`proc/`) |
| 3 — Objects | ~400 new | ~20 | ~8 (`object*.ls`) |
| 4 — Test Bridge | ~300 new (parser + harness) | — | ~128 total |
| 5 — Extended Syntax | ~200 new | ~10 | ~10 additional |

## Open Questions

1. **Truthiness of 0**: Follow spec (truthy) or implementation (falsy)? This affects test output comparison. Recommend: match implementation, update spec later.
2. **AST bridge format**: JSON vs S-expression? JSON from `--emit-ast` is simplest; S-expr is native to Racket.
3. **Scope of numeric tower**: Model int64 and decimal arithmetic fully, or treat as opaque values that pass through? Decimal affects ~5 test files.
4. **Validator/schema tests**: 43 `.ls` files without `.txt` — add expected outputs and include in feasible set?
5. **Parallel or serial phases**: Phases 2 and 3 are independent; could be developed in parallel by different contributors.
