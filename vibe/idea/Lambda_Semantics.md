# Lambda Semantics: 3-Level C Implementation Verification

Verifying that the C/C++ implementation is faithful to the PLT Redex formal semantics (`lambda/semantics/`). Three layers of increasing depth, each building on the previous.

## Layer 1: Table Consistency (Static Extraction)

Parse C source with Tree-sitter, extract static tables and enums, diff against Redex model.

### What to extract

| C Source | Tree-sitter Node | Redex Counterpart |
|----------|-------------------|-------------------|
| `EnumTypeId` (28 values) in `lambda.h` | `enum_specifier` | `base-type` in `lambda-core.rkt` |
| `type_box_table[18]` in `transpile.cpp` | `initializer_list` rows | `lambda-type->c-type`, `boxing-function`, `unboxing-function` in `c-repr.rkt` |
| `sys_func_defs[100+]` in `sys_func_registry.c` | `initializer_list` (17 fields each) | `sys-func-catalog` in `c-repr.rkt` |
| `write_type()` switch in `print.cpp` | `case_statement` → string literal | `lambda-type->c-type` in `c-repr.rkt` |
| Boxing macros (`i2it`, `s2it`, ...) in `lambda.h` | `preproc_function_def` | `semantic-boxing-function` in `c-repr.rkt` |
| `is_truthy()` switch in `lambda-eval.cpp` | 4-case `switch_statement` | `truthy?` in `lambda-core.rkt` |

### What it catches

- Drift between model and implementation (already present: `s2it` vs `x2it` for binary, `it2sym` vs `it2s`, missing `LMD_TYPE_OBJECT`, missing `RetItem` system)
- Missing types in either direction
- Sys-func catalog going out of sync with `sys_func_defs[]`

### Implementation

Racket script that:
1. Runs Tree-sitter C parser on the source files
2. Extracts tables into Racket data structures
3. Compares against `c-repr.rkt` exports
4. Fails on any mismatch

Integrable as `make test-redex-tables`.

## Layer 2: Dispatch Structure (Pattern Extraction)

Extract operator/type dispatch decision trees from if-chains and switch statements, verify structural correspondence with Redex evaluation rules.

### Target patterns

**Equality** (`fn_eq_depth()` in `lambda-eval.cpp`):
```
if (type_id == LMD_TYPE_NULL)  → always true
if (type_id == LMD_TYPE_INT)   → get_int56(a) == get_int56(b)
if (IS_NUMERIC_ID(a) && IS_NUMERIC_ID(b)) → it2d(a) == it2d(b)
if (type_id == LMD_TYPE_STRING) → memcmp
...
```
→ Compare against `val-eq?` metafunction clauses in `lambda-core.rkt`

**Comparison** (`fn_lt()` in `lambda-eval.cpp`):
```
if (type_id == LMD_TYPE_INT)   → native <
if (type_id == LMD_TYPE_BOOL)  → BOOL_ERROR
if (type_id == LMD_TYPE_STRING) → strcmp
```
→ Compare against `val-lt?` metafunction clauses

**Transpiler operator dispatch** (`transpile_binary_expr()` in `transpile.cpp`):
```
if (op == OPERATOR_ADD && both_numeric) → emit native +
else → emit fn_add(a, b)
```
→ Verify native fast-path types match the Redex numeric promotion rules

### What it proves

- **Coverage**: every `base-type` in Redex has a corresponding case in C dispatch (no unhandled types)
- **Consistency**: per-type equality/comparison methods agree with Redex rules
- **Promotion**: cross-type numeric coercion matches `promote-to-float`
- **Error cases**: operations that should error (e.g., `lt` on bools) do error

### Implementation

Tree-sitter extracts the if-chain as a decision tree (condition → action pairs). Racket code walks the tree and matches each branch against the corresponding metafunction clause. Structural, not semantic — it checks "same types handled the same way", not "the C code computes the correct result."

## Layer 3: Semantic Equivalence

Verify that the C implementation produces the same results as the Redex evaluator for all inputs. Tree-sitter parsing alone is insufficient here — this is a verification problem, not a parsing problem.

### Why Tree-sitter isn't enough

Consider `fn_add`:
```c
if (ta == LMD_TYPE_INT && tb == LMD_TYPE_INT) {
    int64_t r = get_int56(a) + get_int56(b);
    if (r > INT56_MAX || r < INT56_MIN) return push_l(r);
    return i2it(r);
}
```

Proving this matches `(add integer_1 integer_2) → (+ integer_1 integer_2)` requires reasoning about:
- Bit-level encoding invariants (int56 range, NaN-boxing tag layout)
- Boxing roundtrip correctness (`get_int56(i2it(n)) ≡ n`)
- Overflow semantics (C signed overflow is UB; guard correctness is semantic)
- Pointer semantics (`push_l` allocates on `num_stack`, returns tagged pointer)
- Memory model (GC movement, bump allocation)
- Cross-function contracts (`it2d` on an Item from `push_l` — only correct if TypeId tag matches)

### Approaches (increasing rigor)

| Approach | Proves | Effort |
|----------|--------|--------|
| **Differential fuzzing** | Same input → same output for random programs | Low (weeks) |
| **Symbolic execution** (KLEE/CBMC) | Per-function correctness for all inputs of bounded size | Medium (months) |
| **Verified C** (VST/CompCert) | Formal proof C implements Redex semantics | Very high (academic) |

### Differential fuzzing (recommended)

Use Redex's `redex-check` to generate random Lambda expressions, run through both evaluators, compare.

```racket
(redex-check Lambda e
  (let* ([src      (expr->lambda-source (term e))]
         [expected (eval-lambda '() (term e))]
         [actual   (run-lambda-exe src)])
    (equal? expected actual)))
```

Requires:
- `expr->lambda-source`: serialize Redex AST → Lambda concrete syntax (~100 lines Racket)
- `run-lambda-exe`: shell out to `./lambda.exe`, parse stdout (~30 lines Racket)
- `value→string` normalizer: comparable text from both sides (~50 lines Racket)

Integrable as `make test-redex-fuzz`.

### Known semantic discrepancy

| Area | Redex model (follows spec) | C implementation |
|------|---------------------------|-----------------|
| Truthiness of `0` | Truthy | Falsy (`it2b` returns false) |

Differential fuzzing would surface this immediately.

## Verification Stack Summary

```
Layer 3: Semantic Equivalence
  "C produces same results as Redex for all inputs"
  Method: differential fuzzing (redex-check vs lambda.exe)
  ┃
Layer 2: Dispatch Structure
  "C handles the same types in the same way as Redex"
  Method: Tree-sitter extract if-chains → compare decision trees
  ┃
Layer 1: Table Consistency
  "C tables match Redex tables"
  Method: Tree-sitter extract enums/arrays → diff against c-repr.rkt
```

Each layer subsumes the one below: Layer 3 catching a bug implies Layer 1 or 2 could have caught it structurally, but Layer 3 also catches bugs invisible to static extraction (runtime-dependent behavior, interaction effects, edge cases in arithmetic).

## Build Targets

```makefile
make test-redex-tables   # Layer 1: extract C tables, diff against Redex model
make test-redex-dispatch # Layer 2: extract dispatch trees, check coverage/consistency
make test-redex-fuzz     # Layer 3: differential fuzzing (redex-check vs lambda.exe)
make test-redex          # All three
```

## Prerequisites

```bash
brew install minimal-racket
raco pkg install redex
```

## Files

| File | Purpose |
|------|---------|
| `lambda/semantics/lambda-core.rkt` | Language grammar, values, environments, helpers |
| `lambda/semantics/lambda-eval.rkt` | Big-step evaluator |
| `lambda/semantics/lambda-types.rkt` | Type inference, subtyping |
| `lambda/semantics/c-repr.rkt` | C representation model (verification target for Layer 1) |
| `lambda/semantics/boxing-rules.rkt` | Property-based self-consistency checks |
| `lambda/semantics/tests.rkt` | Test suite |
