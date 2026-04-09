# Lambda Script Formal Semantics (PLT Redex)

Formal definition of Lambda Script's operational semantics, type system,
and C representation layer, implemented in PLT Redex (Racket).

## Files

| File | Description |
|------|-------------|
| `lambda-core.rkt` | Language grammar: syntax, values, types, environments, helpers |
| `lambda-eval.rkt` | Big-step evaluator: expression → value (functional core + objects) |
| `lambda-proc.rkt` | Procedural extension: mutable state, while/break/continue/return, pn closures |
| `lambda-object.rkt` | Object type system: type registry, nominal typing, inheritance, constraints |
| `lambda-types.rkt` | Type inference, subtyping, type join |
| `c-repr.rkt` | C representation model: boxing, unboxing, type mapping |
| `tests.rkt` | Comprehensive test suite (Parts 1–7) |

## Setup

```bash
# Install Racket (includes PLT Redex)
brew install minimal-racket          # macOS
# or: apt install racket             # Linux

# Install Redex
raco pkg install redex
```

## Running

```bash
# Run all tests
racket lambda/semantics/tests.rkt

# Interactive exploration in DrRacket or REPL
racket -i -e '(require "lambda/semantics/lambda-eval.rkt")'
```

## Quick Examples

```racket
(require "lambda-eval.rkt")

;; Evaluate: let x = 5 in x + 3
(eval-lambda '() '(let ((x 5)) (add x 3)))
;; → 8

;; Evaluate: [1,2,3] | ~ * 2
(eval-lambda '() '(pipe (array 1 2 3) (mul ~ 2)))
;; → '(array-val 2 4 6)

;; Evaluate: for (x in 1 to 5 where x > 2) x * x
(eval-lambda '() '(for-where x (to-range 1 5) (gt ~ 2) (mul x x)))
;; → '(array-val 9 16 25)
```

## Architecture

```
Lambda Source Semantics (lambda-core.rkt + lambda-eval.rkt)
    │
    │  "What Lambda programs mean"
    │  eval-lambda : env × expr → value
    │
    ├── Object Type System (lambda-object.rkt)
    │   Type registry: nominal types, single inheritance
    │   Construction, constraints, methods (fn/pn), update/spread
    │   Integrated into eval-lambda (fn methods) and eval-proc (pn methods)
    │
    ├── Procedural Extension (lambda-proc.rkt)
    │   eval-proc : σ × ρ × e → Result(tag, value, output)
    │   Store-passing: mutable vars, while/break/continue/return
    │   Delegates pure expressions to eval-lambda
    │
    ├── Type System (lambda-types.rkt)
    │   infer-type : Γ × expr → τ
    │   subtype? : τ × τ → bool
    │   type-join : τ × τ → τ
    │
    └── C Representation (c-repr.rkt)
        lambda-type->c-type : τ → C-type
        required-conversion : C-type × C-type → conversion
        verify-call-args, verify-if-branches
```

## Semantic Status

| Area | Status | Notes |
|------|--------|-------|
| Truthiness of `0` | **Aligned** | Both spec and implementation treat 0 as truthy. `i2it(0)` produces a tagged value with non-zero bits. |
| `and`/`or` return type | **Aligned** | Returns operand value (short-circuit), model follows implementation |
| Boxing/unboxing | **Aligned** | Model matches `type_box_table[]` in `transpile.cpp` (Phase 1 sync) |
| Container unboxing | **Aligned** | Specific functions per container type (it2arr, it2map, it2elmt, etc.) |
| New types (object, uint64, etc.) | **Modeled** | object-type, num-sized-type, uint64-type, array-num-type added |

## Scope

The model covers Lambda's **functional core** (`fn` functions) and **procedural extension** (`pn` functions):

### Functional Core (lambda-eval.rkt)
- All primitive types and collections
- Arithmetic, comparison, logical operators
- If-else, for, pipe, where, match expressions
- Functions, closures, higher-order functions
- Error creation, propagation, destructuring
- Type inference, subtyping, type join
- C-level boxing/unboxing correctness

### Procedural Extension (lambda-proc.rkt)
- Mutable variables (`var` with store-backed locations)
- Variable assignment, type widening
- Array element assignment (with negative indexing)
- Map field assignment (add/update fields)
- While loops with break and continue
- Early return from functions
- Procedural functions (`pn`) with mutable parameters
- Closures with shared mutable state
- Print side effects (output accumulator)
- Statement sequencing with env threading

### Object Type System (lambda-object.rkt + extensions in eval/proc)
- Nominal type definitions with single inheritance
- Object construction with field validation and defaults
- Field-level constraints (`that` predicates on individual fields)
- Object-level constraints (`that` predicates on whole object)
- Pure methods (`fn`) — field access, computation, return value
- Mutation methods (`pn`) — modify fields in-place via store
- Object update/spread syntax (`<Type *:source, field: val>`)
- Self-reference (`~`) in methods and constraints
- Nominal type checking (`is-type`) with inheritance chain walk
- Pattern matching on nominal object types
- Method override (child methods shadow parent methods)

**Not modeled** (deferred):
- I/O operations (`input`, `output`, `cmd`, file pipes)
- Module system (`import`, `pub`)
- CSS layout engine (Radiant)
- String patterns (regex type-level matching)
- Datetime/decimal arithmetic
- Path operations
- Bitwise operations
