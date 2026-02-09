# Proposal: Comprehensive & Structural Lambda Robustness Testing

**Date**: February 8, 2026
**Status**: Proposal
**Scope**: Lambda engine — parser, AST builder, transpiler, JIT, runtime, system functions

---

## 1. Executive Summary

Lambda's current test infrastructure is substantial (193 baseline tests, 80 GTest files, 232 script tests) but has grown organically. Coverage is strong in areas that triggered bugs and weak in areas that haven't been exercised. This proposal defines a **structural** approach — testing derived from the language specification, not just from bug reports — to systematically cover all Lambda subsystems and achieve defense-in-depth robustness.

### Current State at a Glance

| Metric | Current |
|--------|---------|
| Baseline tests (must-pass) | 193 |
| Lambda .ls/.txt paired scripts | 55 |
| Std .ls/.expected paired scripts | 42 |
| Negative/error scripts | 40 |
| Fuzzy corpus scripts | 28 |
| GTest C++ files | 80 |

### Key Gaps Identified

| Gap Category | Count | Risk |
|-------------|-------|------|
| Untested data types (Binary, DateTime, Error, Symbol) | 4 | **High** — runtime type-dispatch crashes |
| Untested operators (`div`, `in`, `~#`) | 3 | **Medium** — silent wrong results |
| Untested system functions | 30+ | **Medium** — runtime crashes on edge inputs |
| Untested language constructs (for-group/limit/offset, continue, named args, variadic) | 8+ | **Medium** — AST/transpiler crashes |
| Missing boundary/stress tests | all areas | **High** — the #1 source of production crashes |
| No memory safety tests | all areas | **High** — ref-counting leaks, use-after-free |
| No concurrency/reentrancy tests | runtime | **Low** — single-threaded currently |

---

## 2. Testing Philosophy

### 2.1 Structural Coverage Model

Every test should trace to **one of four layers**:

```
┌─────────────────────────────────────────────┐
│ Layer 4: Integration & System Tests         │  ← End-to-end scripts
│   Complete scripts, format conversion,      │
│   import/module, CLI interface              │
├─────────────────────────────────────────────┤
│ Layer 3: Semantic Tests                     │  ← Language feature tests
│   Each operator, statement, expression,     │
│   type, system function in isolation        │
├─────────────────────────────────────────────┤
│ Layer 2: Subsystem Tests                    │  ← GTest C++ unit tests
│   Parser, AST builder, transpiler, JIT,     │
│   memory pools, name pool, shape pool       │
├─────────────────────────────────────────────┤
│ Layer 1: Boundary & Stress Tests            │  ← Fuzzy, edge cases, limits
│   Null/empty inputs, overflow, deep nesting,│
│   huge data, malformed input, type mixing   │
└─────────────────────────────────────────────┘
```

### 2.2 Test Classification

Every test file should carry metadata (via comments or naming convention):

```
// Test: <descriptive name>
// Layer: 1|2|3|4
// Category: datatype|operator|function|statement|expression|boundary|stress|error
// Covers: <language feature or subsystem>
```

### 2.3 Principles

1. **Specification-driven**: Every grammar rule, type, operator, and system function gets at least one positive and one negative test
2. **Boundary-focused**: Robustness comes from testing limits — empty, null, max, overflow, type-mismatch
3. **Regression-locked**: Every fixed bug gets a permanent test
4. **Layered defense**: Crashes blocked at parsing → AST → transpile → JIT → runtime
5. **Automated discovery**: Tests added by dropping files, no manual registration needed

---

## 3. Structural Test Plan

### 3.1 Data Type Coverage Matrix

For **each data type**, test: literal creation, type conversion, equality, comparison, membership, serialization, null interaction, and error cases.

| Type | Literal | Conversion | `==`/`!=` | `<`/`>` | In Collection | Null Interact | Error Cases | Current |
|------|---------|-----------|-----------|---------|---------------|---------------|-------------|---------|
| null | ✅ | — | ✅ | N/A | ❌ | N/A | ✅ | partial |
| bool | ✅ | ✅ | ✅ | ❌ | ❌ | ❌ | ✅ | partial |
| int | ✅ | ✅ | ✅ | ✅ | ❌ | ❌ | ✅ | good |
| int64 | ✅ | ✅ | ⚠️ | ⚠️ | ❌ | ❌ | ❌ | partial |
| float | ✅ | ✅ | ✅ | ✅ | ❌ | ❌ | ✅ | good |
| decimal | ✅ | ✅ | ⚠️ | ⚠️ | ❌ | ❌ | ❌ | partial |
| string | ✅ | ✅ | ✅ | ❌ | ❌ | ❌ | ✅ | good |
| symbol | ⚠️ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **gap** |
| binary | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **gap** |
| datetime | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **gap** |
| array | ✅ | — | ⚠️ | N/A | ❌ | ❌ | ❌ | partial |
| map | ✅ | — | ❌ | N/A | ❌ | ❌ | ❌ | partial |
| element | ⚠️ | — | ❌ | N/A | ❌ | ❌ | ❌ | **gap** |
| list | ⚠️ | — | ❌ | N/A | ❌ | ❌ | ❌ | **gap** |
| range | ⚠️ | — | ❌ | N/A | ❌ | ❌ | ❌ | **gap** |
| function | ✅ | — | ❌ | N/A | N/A | ❌ | ❌ | partial |
| error | ❌ | — | ❌ | N/A | N/A | ❌ | ❌ | **gap** |

**New tests needed**: ~50 scripts to fill the matrix.

### 3.2 Operator Coverage Matrix

For **each operator**, test: both operand types valid, mixed types, error types (bool, null, string with arithmetic), boundary values, and precedence.

| Operator | Int×Int | Float×Float | Int×Float | String | Null | Bool | Error | Precedence |
|----------|---------|-------------|-----------|--------|------|------|-------|------------|
| `+` | ✅ | ✅ | ✅ | ✅ concat | ✅ err | ✅ err | — | ✅ |
| `-` | ✅ | ✅ | ✅ | ✅ err | ✅ err | ✅ err | — | ✅ |
| `*` | ✅ | ✅ | ✅ | ⚠️ | ✅ err | ✅ err | — | ✅ |
| `/` | ✅ | ✅ | ✅ | ✅ err | ✅ err | ✅ err | — | ✅ |
| `div` | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| `%` | ✅ | ✅ err | ❌ | ❌ | ❌ | ❌ | — | ❌ |
| `^` | ✅ | ✅ | ⚠️ | ❌ | ❌ | ❌ | — | ❌ |
| `++` | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| `and` | N/A | N/A | N/A | ⚠️ | ⚠️ | ✅ | — | ❌ |
| `or` | N/A | N/A | N/A | ⚠️ | ⚠️ | ✅ | — | ❌ |
| `not` | N/A | N/A | N/A | ⚠️ | ⚠️ | ✅ | — | ❌ |
| `==`/`!=` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | ✅ |
| `<`/`>`/`<=`/`>=` | ✅ | ✅ | ✅ | ✅ err | ✅ err | ✅ err | — | ✅ |
| `is` | — | — | — | — | — | — | — | ❌ |
| `in` | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| `\|` (pipe) | ✅ | — | — | — | ❌ | ❌ | — | ❌ |
| `to` (range) | ⚠️ | ❌ | ❌ | ❌ | ❌ | ❌ | — | ❌ |

**New tests needed**: ~30 scripts for untested cells.

### 3.3 System Function Coverage

Organize tests by category. For **each function**, test: valid inputs, edge cases (empty/null/zero), type errors, and boundary values.

#### Priority 1 — Untested Functions (High Risk)

| Function | Test File to Create |
|----------|-------------------|
| `binary(x)` | `std/core/datatypes/binary_basic.ls` |
| `symbol(x)` | `std/core/datatypes/symbol_basic.ls` |
| `datetime(x)` | `std/core/datatypes/datetime_basic.ls` |
| `number(x)` | `std/core/datatypes/number_conversion.ls` |
| `error(msg)` | `std/core/datatypes/error_basic.ls` |
| `slice(v, i, j)` | `std/core/functions/slice.ls` |
| `reverse(v)` | `std/core/functions/reverse.ls` |
| `sort(v)` | `std/core/functions/sort.ls` |
| `unique(v)` | `std/core/functions/unique.ls` |
| `all(v)` / `any(v)` | `std/core/functions/all_any.ls` |
| `take(v,n)` / `drop(v,n)` | `std/core/functions/take_drop.ls` |
| `zip(v1,v2)` | `std/core/functions/zip.ls` |
| `concat(v1,v2)` | `std/core/functions/concat.ls` |
| `fill(n,val)` | `std/core/functions/fill.ls` |
| `index_of(s, sub)` | `std/core/functions/string_index.ls` |
| `trim_start/end` | `std/core/functions/string_trim.ls` |
| `varg()` / `varg(n)` | `std/core/functions/variadic.ls` |
| `today()` / `now()` | `std/core/functions/datetime_funcs.ls` |

#### Priority 2 — Partially Tested (Need Edge Cases)

| Function | Gap |
|----------|-----|
| `int(x)` | Missing: `int("abc")`, `int(null)`, `int(true)`, `int(1e20)` |
| `float(x)` | Missing: `float("abc")`, `float(null)`, `float(inf)` |
| `len(x)` | Missing: `len(null)`, `len(42)`, `len(map)` |
| `abs/round/floor/ceil` | Missing: `abs(null)`, `round("x")`, boundary values |
| `sum/avg/median` | Missing: empty array, single-element, null elements |

### 3.4 Language Construct Coverage

| Construct | Positive Test | Negative/Edge Test | Status |
|-----------|:---:|:---:|--------|
| `let` binding | ✅ | ⚠️ | Need: shadowing, re-binding, unused |
| `if` expression | ✅ | ⚠️ | Need: non-bool condition, nested 5+ deep |
| `if` statement | ✅ | ❌ | Need: missing else, empty body |
| `for` expression | ✅ | ⚠️ | Need: empty collection, nested for |
| `for` where/order | ✅ | ❌ | Need: false-for-all where, stable sort |
| `for` group | ❌ | ❌ | **No tests at all** |
| `for` limit/offset | ❌ | ❌ | **No tests at all** |
| `for` let | ❌ | ❌ | **No tests at all** |
| `while` | ✅ | ❌ | Need: immediate-false, max iterations |
| `break` | ✅ | ❌ | Need: nested break, break with value |
| `continue` | ❌ | ❌ | **No tests at all** |
| `return` | ⚠️ | ❌ | Need: early return, return in nested |
| `fn` definition | ✅ | ⚠️ | Need: 0-param, many-param, recursive |
| `pn` definition | ✅ | ❌ | Need: side-effect verification |
| Arrow function | ✅ | ❌ | Need: in argument position, nested |
| Closure | ✅ | ⚠️ | Need: mutable capture, closure leak |
| `import` | ✅ | ❌ | Need: circular import, missing file |
| Type declaration | ✅ | ❌ | Need: recursive type, invalid type |
| String pattern | ✅ | ❌ | Need: empty pattern, very long match |
| Element literal | ⚠️ | ❌ | Need: standalone tests, nested elements |
| Named arguments | ❌ | ❌ | **No tests at all** |
| Spread arguments | ❌ | ❌ | **No tests at all** |
| Variadic `...` | ❌ | ❌ | **No tests at all** |
| `~#` current index | ❌ | ❌ | **No tests at all** |
| Path wildcards | ❌ | ❌ | **No tests at all** |

---

## 4. Boundary & Stress Testing Plan

### 4.1 Numeric Boundaries

```
Test: boundary_int.ls
──────────────────────
INT56_MAX  = 36028797018963967
INT56_MIN  = -36028797018963968
INT56_MAX + 1   → overflow behavior
INT56_MIN - 1   → overflow behavior
0, -0, 1, -1    → trivial values
Large multiply   → 999999999 * 999999999
Division precision → 1 / 3 * 3
Float extremes   → 1e308, 5e-324, inf, -inf, nan
Decimal extremes → 9999999999999999999999999999d (28+ digits)
```

### 4.2 String Boundaries

```
Test: boundary_string.ls
────────────────────────
Empty string       → ""
Single char        → "x"
Very long string   → "a" repeated 1000000 times (via system function)
Unicode            → "\u{1F600}", surrogate pairs, combining chars
Null bytes         → "\u{0000}" (if supported)
Multi-line         → "line1\nline2\nline3"
Escape sequences   → all: \n \t \\ \" \/ \uXXXX \u{XXXXX}
```

### 4.3 Collection Boundaries

```
Test: boundary_collection.ls
────────────────────────────
Empty array        → []
Single-element     → [42]
Large array        → [1 to 100000]
Deeply nested      → [[[[[[1]]]]]]
Empty map          → {}
Single-field map   → {x: 1}
Many-field map     → 100+ fields
Null in collections → [null, null, null], {x: null}
Mixed-type array   → [1, "two", true, null, 3.14, [5]]
Nested maps        → {a: {b: {c: {d: 1}}}}
```

### 4.4 Control Flow Boundaries

```
Test: boundary_control.ls
─────────────────────────
Deep if nesting    → if/else 50 levels deep
Deep for nesting   → 5 nested for loops
Deep recursion     → fn calling itself 10000 times (with tail call)
Empty for body     → for (x in []) x
For over empty     → for (x in []) "never"
While false        → while (false) { ... }
Immediate break    → for (x in arr) { break }
```

### 4.5 Function Boundaries

```
Test: boundary_function.ls
──────────────────────────
0-parameter fn     → fn zero() => 42
Many-parameter fn  → fn many(a,b,c,d,e,f,g,h) => a+h
Deeply nested closure → 5+ levels of closure capture
Higher-order chain → fn returning fn returning fn
Self-application   → fn apply(f, x) => f(x) applied recursively
Mutual recursion   → fn a calls fn b calls fn a
```

### 4.6 Type Interaction Matrix (Cross-Type Stress)

Test **every type pair** with `==`:

```
Test: cross_type_equality.ls
────────────────────────────
For types A, B in {null, bool, int, float, string, symbol, array, map}:
  A == A  → true (identity)
  A == B  → error or false (type mismatch)
  A != B  → error or true
```

---

## 5. Error Handling & Recovery Tests

### 5.1 Parser Error Recovery

The parser (Tree-sitter) should never crash regardless of input. Test with:

| Input Pattern | Expected Behavior | Test File |
|--------------|-------------------|-----------|
| Empty file | No crash, empty output | `negative/syntax/empty_file.ls` |
| Single token | Error message, no crash | `negative/syntax/single_token.ls` |
| Binary garbage | Error message, no crash | `negative/syntax/binary_garbage.ls` |
| UTF-8 BOM + script | Normal execution | `negative/syntax/utf8_bom.ls` |
| Only comments | No output, no crash | `negative/syntax/only_comments.ls` |
| Unclosed string at EOF | Error, no crash | ✅ exists |
| Unclosed paren at EOF | Error, no crash | ✅ exists |
| 1000+ unclosed parens | Error, no crash | `negative/syntax/deep_unclosed.ls` |
| Extremely long line (10KB) | No crash | `negative/syntax/long_line.ls` |
| Null bytes in source | Error, no crash | `negative/syntax/null_bytes.ls` |

### 5.2 AST Builder Error Recovery

The AST builder should handle all malformed CST nodes. Test with:

| Scenario | Expected | Status |
|----------|----------|--------|
| Comment in parenthesized expr | Error, no crash | ✅ fixed & tested |
| ERROR nodes in CST | Skip gracefully | ❌ needs test |
| Missing required field nodes | Error, no crash | ❌ needs test |
| Duplicate function names | Error, no crash | ✅ tested |

### 5.3 Runtime Error Recovery

| Error Scenario | Expected | Status |
|----------------|----------|--------|
| Division by zero (int) | `error` value | ✅ tested |
| Division by zero (float) | `inf` or `nan` | ✅ tested |
| Index out of bounds | `error` value | ✅ tested |
| Null member access | `null` (safe propagation) | ✅ tested |
| Stack overflow | Error message, no crash | ✅ tested |
| Type mismatch in arithmetic | `error` value | ✅ tested |
| Calling non-function | Error, no crash | ✅ tested |
| Wrong number of arguments | Error, no crash | ✅ tested |
| Infinite loop | Timeout detection | ✅ in fuzzy |
| File not found (read) | Error, no crash | ✅ tested |
| Read invalid format | Error, no crash | ✅ tested |
| Circular imports | Error, no crash | ❌ needs test |
| Self-referential data | Error or correct handling | ❌ needs test |

---

## 6. Memory Safety Tests

### 6.1 Reference Counting

Lambda uses reference counting for memory management. Critical paths:

| Scenario | Risk | Test |
|----------|------|------|
| String passed to multiple containers | Double-free if refcount wrong | ❌ |
| Array element replaced | Leak if old element not released | ❌ |
| Closure captures mutable var | Dangling ref if scope exits | ❌ |
| Large map resize | Old data freed while referenced? | ❌ |
| Deeply nested data freed | Stack overflow in recursive free? | ❌ |

**Proposed approach**: Add a `--memcheck` flag (debug builds only) that validates all refcounts at exit. Use `test_memtrack_gtest.cpp` patterns.

### 6.2 Pool/Arena Integrity

| Scenario | Risk | Test |
|----------|------|------|
| Pool exhaustion | Crash or graceful error? | ❌ |
| Arena exhaustion | Crash or graceful error? | ❌ |
| Name pool collision | Hash collision handling | ⚠️ partial |
| Shape pool cache | Stale shapes after mutation | ❌ |

---

## 7. Fuzzy Testing Enhancement

### 7.1 Current State

- 28 corpus scripts (valid + edge + negative)
- Grammar-based generator (`grammar_gen.cpp`)
- Mutation-based fuzzer (`mutator.cpp`)
- 5-minute default duration, 5-second timeout per test

### 7.2 Proposed Improvements

| Enhancement | Description | Priority |
|-------------|-------------|----------|
| **Expand corpus to 100+** | Add scripts exercising every grammar rule | High |
| **Coverage-guided fuzzing** | Instrument runtime with code coverage, prefer inputs reaching new code | Medium |
| **Differential testing** | Compare interpreter vs JIT output for same script | High |
| **Long-running soak test** | Run fuzzer for 1+ hour in CI nightly | Medium |
| **Input format fuzzing** | Fuzz JSON/XML/HTML/CSS/Markdown parsers too | High |
| **Add ASan/UBSan builds** | Compile with AddressSanitizer to detect memory errors | High |
| **Minimizer** | Auto-minimize crash inputs to smallest reproducer | Low |

### 7.3 Differential Testing (Interpreter vs JIT)

Lambda has both an interpreter path and a JIT path. For any given script, both should produce identical output. Proposed:

```bash
# For each .ls test script:
result_jit=$(./lambda.exe script.ls 2>/dev/null)
result_interp=$(./lambda.exe --interpret script.ls 2>/dev/null)
diff <(echo "$result_jit") <(echo "$result_interp")
```

Any divergence is a bug.

---

## 8. Structured Test Organization

### 8.1 Proposed Directory Structure

```
test/
├── std/                          ← Specification-driven tests
│   ├── core/
│   │   ├── datatypes/            ← One file per type (existing: 28)
│   │   │   ├── integer_basic.ls / .expected
│   │   │   ├── float_basic.ls / .expected
│   │   │   ├── symbol_basic.ls / .expected      ← NEW
│   │   │   ├── binary_basic.ls / .expected      ← NEW
│   │   │   ├── datetime_basic.ls / .expected    ← NEW
│   │   │   ├── error_basic.ls / .expected       ← NEW
│   │   │   ├── element_basic.ls / .expected     ← NEW
│   │   │   ├── list_basic.ls / .expected        ← NEW
│   │   │   └── range_basic.ls / .expected       ← NEW
│   │   ├── operators/            ← One file per operator (existing: 8)
│   │   │   ├── integer_division.ls / .expected  ← NEW (div)
│   │   │   ├── modulo.ls / .expected            ← NEW
│   │   │   ├── exponent.ls / .expected          ← NEW
│   │   │   ├── concatenation.ls / .expected     ← NEW (++)
│   │   │   ├── logical_and.ls / .expected       ← NEW
│   │   │   ├── logical_or.ls / .expected        ← NEW
│   │   │   ├── logical_not.ls / .expected       ← NEW
│   │   │   ├── pipe_operator.ls / .expected     ← NEW
│   │   │   ├── range_operator.ls / .expected    ← NEW
│   │   │   ├── membership_in.ls / .expected     ← NEW
│   │   │   └── type_check_is.ls / .expected     ← NEW
│   │   ├── functions/            ← System functions (NEW category)
│   │   │   ├── collection_slice.ls / .expected
│   │   │   ├── collection_sort.ls / .expected
│   │   │   ├── collection_reverse.ls / .expected
│   │   │   ├── string_index.ls / .expected
│   │   │   ├── string_trim.ls / .expected
│   │   │   ├── math_trig.ls / .expected
│   │   │   └── datetime_funcs.ls / .expected
│   │   └── statements/          ← Control flow (NEW category)
│   │       ├── for_group.ls / .expected
│   │       ├── for_limit_offset.ls / .expected
│   │       ├── continue_statement.ls / .expected
│   │       ├── return_early.ls / .expected
│   │       └── named_arguments.ls / .expected
│   ├── boundary/                 ← Boundary & stress tests (NEW)
│   │   ├── numeric_limits.ls / .expected
│   │   ├── string_limits.ls / .expected
│   │   ├── collection_limits.ls / .expected
│   │   ├── nesting_limits.ls / .expected
│   │   ├── cross_type_equality.ls / .expected
│   │   └── function_limits.ls / .expected
│   ├── integration/              ← End-to-end (existing: 1)
│   │   ├── complex_computation.ls / .expected
│   │   ├── format_conversion.ls / .expected     ← NEW
│   │   ├── import_chain.ls / .expected          ← NEW
│   │   └── mixed_paradigm.ls / .expected        ← NEW
│   ├── negative/                 ← Error cases (existing: 3)
│   │   ├── type_cross_matrix.ls / .expected     ← NEW
│   │   ├── circular_import.ls / .expected       ← NEW
│   │   └── malformed_data.ls / .expected        ← NEW
│   └── performance/              ← Perf regression (existing: 1)
│       ├── large_data.ls / .expected
│       └── recursive_perf.ls / .expected        ← NEW
```

### 8.2 Test Discovery & Registration

All `std/` tests should be auto-discovered (like `test/lambda/` tests today). This requires:

1. A new GTest file `test_std_gtest.cpp` that scans `test/std/` recursively
2. For each `.ls` file with a matching `.expected` file, register a parameterized test
3. Add to the `lambda` baseline suite in `build_lambda_config.json`

### 8.3 Naming Convention

```
test/std/<category>/<subcategory>/<feature>_<aspect>.ls

Examples:
  test/std/core/datatypes/integer_overflow.ls
  test/std/core/operators/modulo_by_zero.ls
  test/std/boundary/nesting_limits.ls
  test/std/negative/circular_import.ls
```

---

## 9. CI/CD Integration

### 9.1 Test Tiers

| Tier | When | Duration | Tests |
|------|------|----------|-------|
| **Smoke** | Every commit | <30s | Core baseline (top 20 scripts) |
| **Baseline** | Every PR | <3min | All baseline suites (must 100%) |
| **Extended** | Nightly | <15min | All suites + fuzzy (5min) |
| **Soak** | Weekly | 1hr | Fuzzy extended + ASan build |

### 9.2 Quality Gates

| Gate | Threshold | Action |
|------|-----------|--------|
| Baseline pass rate | 100% | Block merge |
| Extended pass rate | ≥95% | Warning |
| Fuzzy crash count | 0 new | Block merge |
| Test count regression | Cannot decrease | Block merge |
| Coverage (lines) | ≥70% for new code | Warning |

---

## 10. Implementation Roadmap

### Phase 1: Foundation (Week 1-2) — High Priority

| Task | Effort | Impact |
|------|--------|--------|
| Create `test/std/core/datatypes/` tests for symbol, binary, datetime, error, element, list, range | 3 days | Fills 7 type gaps |
| Create `test/std/core/operators/` tests for `div`, `%`, `^`, `++`, `and/or/not`, `in`, `is`, pipe, range | 2 days | Fills 11 operator gaps |
| Create `test/std/boundary/` numeric, string, collection, nesting limit tests | 2 days | Catches most crash-class bugs |
| Register all existing unregistered negative tests | 0.5 days | Increases effective baseline |
| Auto-discover `test/std/` tests in GTest | 1 day | Enables all new tests |

### Phase 2: Functions & Constructs (Week 3-4) — Medium Priority

| Task | Effort | Impact |
|------|--------|--------|
| Create `test/std/core/functions/` tests for 15+ untested system functions | 3 days | Fills function gaps |
| Create `test/std/core/statements/` tests for for-group/limit/offset, continue, named args | 2 days | Tests grammar constructs |
| Cross-type interaction tests (every type pair with every operator) | 2 days | Catches type-dispatch bugs |
| Enhance fuzzy corpus to 100+ scripts | 1 day | Better crash discovery |

### Phase 3: Deep Robustness (Week 5-6) — Medium Priority

| Task | Effort | Impact |
|------|--------|--------|
| Add ASan/UBSan build configuration | 1 day | Catches memory bugs |
| Implement differential testing (interpreter vs JIT) | 2 days | Catches JIT-specific bugs |
| Input format fuzzing (JSON, XML, HTML, CSS parsers) | 2 days | Hardens input pipeline |
| Memory safety stress tests (large data, many allocs) | 2 days | Catches pool/arena bugs |
| Circular import and self-reference tests | 1 day | Catches infinite loops |

### Phase 4: Polish & Automation (Week 7-8) — Low Priority

| Task | Effort | Impact |
|------|--------|--------|
| CI pipeline setup (smoke/baseline/extended/soak tiers) | 2 days | Prevents regressions |
| Test coverage measurement integration | 1 day | Tracks progress |
| Performance regression test baseline | 1 day | Prevents perf regressions |
| Documentation of test strategy and conventions | 1 day | Knowledge sharing |

---

## 11. Success Metrics

| Metric | Current | Target (Phase 1) | Target (Phase 4) |
|--------|---------|-------------------|-------------------|
| Baseline test count | 193 | 250+ | 350+ |
| Std test scripts | 42 | 80+ | 120+ |
| Negative test coverage | 40 scripts | 55+ | 70+ |
| Data type coverage | 11/20 types | 18/20 types | 20/20 types |
| Operator coverage | 8/18 operators | 16/18 | 18/18 |
| System function coverage | ~40% | ~70% | ~90% |
| Fuzzy corpus size | 28 | 60+ | 100+ |
| Fuzzy crashes (open) | 0 | 0 | 0 |
| Grammar rule coverage | ~60% | ~80% | ~95% |
| New crash bugs from fuzzy | N/A | tracked | trending to 0 |

---

## 12. Summary

The key insight is that **robustness comes from structural coverage, not reactive fixes**. Today's tests are strong where bugs have been found but blind where they haven't. This proposal provides:

1. **Coverage matrices** derived from the language specification — not from bug reports
2. **Boundary tests** that probe the limits where crashes actually happen
3. **Cross-type tests** that exercise the combinatorial space of type interactions
4. **Fuzzy testing upgrades** for continuous crash discovery
5. **Memory safety infrastructure** to catch the hardest bugs
6. **A phased roadmap** that delivers the highest-impact tests first

The goal is not just "more tests" but **tests that provably cover the specification surface**, so that any new language feature automatically reveals what tests it needs.

---

*Status: Proposal — Ready for Review*
