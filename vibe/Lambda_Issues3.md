# Proposal: Structured Test Enhancement — Phase 3

**Date**: March 3, 2026
**Status**: Proposal
**Prerequisite**: Phase 1 & 2 complete (see `Lambda_Testing_Structured.md`)
**Scope**: Lambda engine — new language features since Phase 2, structural coverage gaps

---

## 1. Executive Summary

Since the Phase 2 structured tests were completed (Feb 20, 2026), Lambda has received ~777 commits adding major language features: object types with methods, namespace declarations, `that` constraints, set operators, enhanced error handling with `T^E` types, spread operator, match expressions, typed arrays, method-style calls, datetime member access, path wildcards, procedural mutation, `cmd()`/`clock()`, and more. The structured test suite (`test/std/`) has not kept pace — the entire `core/` directory tree described in the Phase 2 proposal (57 test files for datatypes, operators, functions, statements) is missing, leaving only 19 tests in `test/std/` (boundary, integration, negative, performance).

Meanwhile, `test/lambda/` contains ~90 organic test scripts that incidentally cover many new features but are not structurally organized or exhaustive. This proposal fills the gap: systematic coverage for every new feature, rebuilding the `core/` tests, and adding Phase-3 deep robustness tests.

### Current State vs. Phase 2 Report

| Metric | Phase 2 Report (Feb 8) | Actual (Mar 3) | Gap |
|--------|------------------------|-----------------|-----|
| `test/std/` total `.ls` files | 76 | **19** | **57 missing** (entire `core/` tree) |
| `test/std/core/datatypes/` | 19 files | **0** | Directory absent |
| `test/std/core/operators/` | 14 files | **0** | Directory absent |
| `test/std/core/functions/` | 16 files | **0** | Directory absent |
| `test/std/core/statements/` | 8 files | **0** | Directory absent |
| `test/std/boundary/` | 10 files | **10** | ✅ Present |
| `test/std/integration/` | 3 files | **3** | ✅ Present |
| `test/std/negative/` | 4 files | **4** | ✅ Present |
| `test/std/performance/` | 2 files | **2** | ✅ Present |
| `test/lambda/` (organic) | ~85 files | **~90 files** | Grew organically |

### New Features Not Covered by Structured Tests

| Feature Category | Feature Count | Organic Tests | Structured Tests |
|-----------------|---------------|---------------|-----------------|
| Object types (methods, inheritance, defaults, constraints, mutation, update, pattern) | 7 sub-features | 8 files in `test/lambda/` | **0** |
| Namespace declarations (tags, attributes, symbols, scoping) | 4 sub-features | 1 file | **0** |
| Match expressions (type/literal/symbol/or-patterns, `~`, nested, procedural) | 6 sub-features | 2 files | **0** |
| Error handling (`T^E`, `raise`, `^` propagation, `let a^err`, `^err` check) | 5 sub-features | 3 files | **0** |
| Spread operator (`*expr`, spreadable for, spreadable null) | 3 sub-features | 1 file | **0** |
| `that` constraints (type-level, object-level, `that` filter pipe) | 3 sub-features | 1 file | **0** |
| Set operators (`&`, `\|`, `!`) | 3 operators | 0 files | **0** |
| Method-style calls (chaining, various categories) | 1 feature | 1 file | **0** |
| Typed arrays (`int[]`, `float[]`, auto-conversion) | 3 sub-features | 1 file | **0** |
| DateTime members (20+ properties, `.format()`, constructors) | 3 sub-features | 1 file | **0** |
| Procedural mutation (array/map/element assignment, type widening) | 5 sub-features | 8 files in `test/lambda/proc/` | **0** |
| Path literals (schemes, wildcards, `sys.*`, concatenation) | 5 sub-features | 1 file | **0** |
| Query expressions (`?T`, `.?T`, `[T]`, chaining) | 4 sub-features | 2 files | **0** |
| Vector/linear algebra (cumsum, dot, norm, argmin/argmax) | 6 functions | 4 files | **0** |
| Statistical functions (variance, deviation, quantile, prod) | 4 functions | 0 files | **0** |
| Import system (relative, alias, multi, circular) | 4 sub-features | 6 files | **0** |
| String patterns (definition, matching, pattern-aware functions) | 3 sub-features | 3 files | **0** |
| `cmd()` / `clock()` / `io.fetch()` | 3 functions | 3 files in `proc/` | **0** |
| Bitwise operators (`band`, `bor`, `bxor`, etc.) | 1 feature | 1 file | **0** |
| For map iteration (`for k,v at map`) | 1 feature | 0 dedicated | **0** |
| Truthiness rules (0 truthy, `[]` truthy, `""` falsy, error falsy) | 1 feature | 0 dedicated | **0** |
| `pub` exports | 1 feature | tested via import | **0** |

---

## 2. Goals

1. **Rebuild `core/`**: Recreate the 57 missing core test files from Phase 2 with updated syntax and expanded coverage for new features
2. **Add new feature tests**: Create structured tests for every major feature added since February 2026
3. **Deepen boundary testing**: Cross-type interactions with new types (objects, elements), mutation boundary tests, error handling boundaries
4. **Expand negative tests**: Parser recovery, type errors on new features, mutation errors
5. **Expand integration tests**: End-to-end scenarios combining multiple new features

---

## 3. Complete Feature Coverage Matrix

### 3.1 Data Types — Tests Needed

The original Phase 2 planned 19 datatype test files. We need to rebuild these **plus** add coverage for new type features.

| Type | Basic Test | Conversion | Comparison | Collections | Null | New Features to Cover | Test File |
|------|:---:|:---:|:---:|:---:|:---:|---|---|
| null | ✅ | — | ✅ | ✅ | — | Truthiness (falsy) | `null_basic.ls` |
| bool | ✅ | ✅ | ✅ | ✅ | ✅ | Truthiness (truthy/falsy) | `boolean_basic.ls` |
| int | ✅ | ✅ | ✅ | ✅ | ✅ | Truthiness (0 is truthy), `int[]` typed arrays | `integer_basic.ls` |
| int64 | ✅ | ✅ | ⚠️ | ❌ | ❌ | `int64()` conversion, large values | `integer64_basic.ls` |
| float | ✅ | ✅ | ✅ | ✅ | ✅ | `float[]` typed arrays, `inf`/`nan`/`-inf` | `float_basic.ls` |
| decimal | ✅ | ✅ | ✅ | ❌ | ❌ | `123.456n` literal, `decimal()` | `decimal_basic.ls` |
| string | ✅ | ✅ | ✅ | ✅ | ✅ | **Empty string → null**, patterns, method calls | `string_basic.ls` |
| symbol | ✅ | ✅ | ✅ | ❌ | ❌ | **Empty symbol → null**, `symbol()`, namespaced symbols | `symbol_basic.ls` |
| binary | ✅ | ✅ | ❌ | ❌ | ❌ | Hex/base64 literals | `binary_basic.ls` |
| datetime | ✅ | ✅ | ❌ | ❌ | ❌ | **Member properties**, `.format()`, constructors, sub-types | `datetime_basic.ls` |
| array | ✅ | ✅ | ✅ | ✅ | ✅ | **Typed arrays** (`int[]`, `float[]`), mutation, slicing | `array_basic.ls` |
| list | ✅ | — | ✅ | ❌ | ❌ | Immutability, `head`/`tail` equiv, spread | `list_basic.ls` |
| map | ✅ | — | ✅ | ❌ | ❌ | **Dynamic `map()`**, mutation, spread, wrapping | `map_basic.ls` |
| element | ✅ | — | ❌ | ❌ | ❌ | **Mutation**, `.name`, query, namespaced attrs | `element_basic.ls` |
| range | ✅ | — | ❌ | ❌ | ❌ | `in` containment, for iteration | `range_basic.ls` |
| path | ❌ | — | ❌ | ❌ | ❌ | **Schemes**, wildcards, `sys.*`, concatenation | `path_basic.ls` (**NEW**) |
| object | ❌ | — | ❌ | ❌ | ❌ | **Methods, inheritance, defaults, constraints, mutation** | `object_basic.ls` (**NEW**) |
| function | ✅ | — | ❌ | ❌ | ❌ | Closures, higher-order, named args, variadic | `function_basic.ls` |
| error | ✅ | — | ❌ | ❌ | ❌ | **`T^E`, chaining, `.source`, code/message** | `error_basic.ls` |
| type | ❌ | — | ❌ | ❌ | ❌ | **First-class types**, `type()`, type comparison | `type_basic.ls` (**NEW**) |

**New tests needed**: 20 files (17 rebuilt + 3 new types: path, object, type)

### 3.2 Operators — Tests Needed

Rebuild the 14 original operator tests + add 6 new operator categories:

| Operator | Basic | Cross-Type | Boundary | Phase 2 Status | New Coverage Needed | Test File |
|----------|:---:|:---:|:---:|---|---|---|
| `+` `-` `*` `/` | ✅ | ✅ | ✅ | In boundary tests | Vector arithmetic variants | `arithmetic_basic.ls` |
| `div` | ✅ | ❌ | ❌ | Was `integer_division.ls` | Negative, float, boundary | `integer_division.ls` |
| `%` | ✅ | ❌ | ❌ | Was `modulo.ls` | Negative modulo, float mod | `modulo.ls` |
| `**` | ✅ | ❌ | ❌ | Was `exponent.ls` | Large exponents, fractional | `exponent.ls` |
| `++` | ✅ | ❌ | ❌ | Was `concatenation.ls` | Path concat, list concat, symbol concat | `concatenation.ls` |
| `and` `or` `not` | ✅ | ⚠️ | ❌ | 3 files | **Short-circuit**, truthiness rules | `logical_ops.ls` |
| `==` `!=` `<` `>` `<=` `>=` | ✅ | ✅ | ✅ | In boundary tests | Null comparisons, cross-type semantics | `comparison_ops.ls` |
| `is` | ✅ | ✅ | ❌ | Was `type_check_is.ls` | **Object `is` (nominal)**, `is not`, subtype hierarchy | `type_check_is.ls` |
| `in` | ✅ | ❌ | ❌ | Was `membership_in.ls` | **Range containment**, string containment | `membership_in.ls` |
| `\|` (pipe) | ✅ | ❌ | ❌ | Was `pipe_operator.ls` | **`~#` index/key**, aggregated pipe, chained | `pipe_operator.ls` |
| `where` | ✅ | ❌ | ❌ | Was `where_filter.ls` | On maps, with `~#`, empty result | `where_filter.ls` |
| `to` (range) | ✅ | ❌ | ❌ | Was `range_operator.ls` | Match case ranges, negative ranges | `range_operator.ls` |
| `?` `.?` (query) | ❌ | ❌ | ❌ | **Not in std** | **Recursive, self-inclusive, type queries** | `query_operator.ls` (**NEW**) |
| `[T]` (child query) | ❌ | ❌ | ❌ | **Not in std** | **Child-level type query, chaining** | `child_query_operator.ls` (**NEW**) |
| `&` `\|` `!` (set) | ❌ | ❌ | ❌ | **Not in std** | **Intersection, union, exclusion** | `set_operators.ls` (**NEW**) |
| `*` (spread) | ❌ | ❌ | ❌ | **Not in std** | **Array/list/map spread** | `spread_operator.ls` (**NEW**) |
| `\|>` `\|>>` (pipe output) | ❌ | ❌ | ❌ | **Not in std** | **Write, append to file** | `pipe_output.ls` (**NEW**) |
| `^` (error propagation) | ❌ | ❌ | ❌ | **Not in std** | **Propagate, unwrap, chain** | `error_propagation_op.ls` (**NEW**) |
| `that` (constraint/filter) | ❌ | ❌ | ❌ | **Not in std** | **Type constraint, pipe filter** | `that_operator.ls` (**NEW**) |
| Bitwise (`band` etc.) | ❌ | ❌ | ❌ | **Not in std** | **`band`, `bor`, `bxor`, `bnot`, `bshl`, `bshr`** | `bitwise_ops.ls` (**NEW**) |
| Vector `+` `*` etc. | ✅ | ❌ | ❌ | Was `vector_arithmetic.ls` | **Scalar-vector, vector-vector, broadcasting** | `vector_arithmetic.ls` |
| Precedence | ✅ | — | — | Was `precedence.ls` | Add new operators to precedence tests | `precedence.ls` |

**New tests needed**: 22 files (14 rebuilt + 8 new operators)

### 3.3 System Functions — Tests Needed

Rebuild the 16 original function tests + add 14 new function categories:

| Function Group | Functions | Phase 2 Status | New Coverage Needed | Test File |
|---------------|-----------|---|---|---|
| Collection core | `slice`, `reverse`, `sort`, `unique`, `concat`, `set` | 4 files existed | **`set()` if available**, `sort('desc)` | `collection_basic.ls` |
| Collection take/drop/zip/fill | `take`, `drop`, `zip`, `fill`, `range()` | 3 files existed | Range with step | `collection_construction.ls` |
| String core | `replace`, `split`, `find` | 3 files existed | **Pattern-aware variants**, `split(s,p,true)` keep delimiters | `string_pattern_funcs.ls` |
| String manipulation | `upper`, `lower`, `trim`, `trim_start`, `trim_end`, `starts_with`, `ends_with`, `contains` | 2 files existed | `normalize()` | `string_manipulation.ls` |
| String index | `index_of`, `slice` on strings | 1 file existed | Negative indices, Unicode | `string_index.ls` |
| Math basic | `abs`, `round`, `floor`, `ceil`, `sign` | 1 file existed | **`sign()`**, vectorized math | `math_basic.ls` |
| Math advanced | `sqrt`, `log`, `log10`, `exp`, `sin`, `cos`, `tan` | **Not in std** | **All trig/log functions** | `math_advanced.ls` (**NEW**) |
| Math min/max | `min(a,b)`, `max(a,b)`, `min(vec)`, `max(vec)` | **Not in std** | **Two-arg and vector forms** | `math_minmax.ls` (**NEW**) |
| Stats basic | `sum`, `avg`, `mean`, `median` | 1 file existed | `prod()` | `stats_basic.ls` |
| Stats advanced | `variance`, `deviation`, `quantile`, `prod` | **Not in std** | **All 4 functions** | `stats_advanced.ls` (**NEW**) |
| Vector algebra | `dot`, `norm`, `cumsum`, `cumprod`, `argmin`, `argmax` | **Not in std** | **All 6 functions** | `vector_algebra.ls` (**NEW**) |
| Type conversion | `int()`, `float()`, `string()`, `symbol()`, `binary()`, `number()`, `decimal()`, `int64()` | 1 file existed | **All 8 converters, edge cases** | `type_conversion.ls` |
| Type inspection | `type()`, `name()`, `len()` | **Not in std** | **All 3 functions** | `type_inspection.ls` (**NEW**) |
| Map/filter/reduce | `map()`, `filter()`, `reduce()` | 1 file existed | **`map()` constructor**, `all()`, `any()` | `map_filter_reduce.ls` |
| Variadic | `varg()`, `varg(n)` | **Not in std** | **Define variadic fn, test varg** | `variadic_args.ls` (**NEW**) |
| DateTime | `datetime()`, `date()`, `time()`, `today()`, `justnow()` | **Not in std** | **Constructors, member props, `.format()`** | `datetime_funcs.ls` (**NEW**) |
| I/O pure | `input()`, `exists()`, `format()` | **Not in std** | **Parse JSON/YAML, format output** | `io_pure.ls` (**NEW**) |
| I/O procedural | `print()`, `output()`, `cmd()`, `clock()`, `io.*` | **Not in std** | **Shell exec, timing, file ops** | `io_procedural.ls` (**NEW**) |

**New tests needed**: 18 files (9 rebuilt + 9 new categories)

### 3.4 Language Constructs — Tests Needed

Rebuild the 8 original statement tests + add 15 new construct categories:

| Construct | Phase 2 Status | New Coverage Needed | Test File |
|-----------|---|---|---|
| `let` binding | Was `let_binding.ls` | Destructuring, type annotations | `let_binding.ls` |
| `if` expression | Was `if_expression.ls` | **Block else**, expression else, map/block ambiguity | `if_expression.ls` |
| `for` expression | Was `for_expression.ls` | **Spreadable arrays**, empty results, spreading into collections | `for_expression.ls` |
| `for` clauses | Was `for_let.ls` etc. | **`limit`/`offset`**, `order by desc`, `let` clause, `where` clause | `for_clauses.ls` |
| `for` map iteration | **Not in std** | **`for k at map`**, `for k,v at map`**, with where | `for_map_iteration.ls` (**NEW**) |
| `for` multiple variables | **Not in std** | **`for x in xs, y in ys`** | `for_multi_variable.ls` (**NEW**) |
| `match` expression | Was `match_expression.ls` | **Type, literal, symbol, or-patterns, `~`, nested, mixed arms** | `match_expression.ls` |
| Function definition | Was `function_definition.ls` | **Named args, optional `?`, defaults, variadic `...`** | `function_definition.ls` |
| Arrow functions | **Not in std** | **`(x) => x*2`**, type inference, closures | `arrow_functions.ls` (**NEW**) |
| Closures | **Not in std** | **Capture semantics, mutable captures in `pn`, nested closures** | `closures.ls` (**NEW**) |
| Higher-order functions | **Not in std** | **Functions as args, returning fns, `compose` pattern** | `higher_order.ls` (**NEW**) |
| Module/import | **Not in std** | **Relative import, alias, multi-import, `pub` exports** | `import_module.ls` (**NEW**) |
| Type declarations | **Not in std** | **Type alias, union types, object types, element types** | `type_declarations.ls` (**NEW**) |
| String patterns | **Not in std** | **Pattern definition, `is` matching, `match` with patterns** | `string_patterns.ls` (**NEW**) |
| Object types | **Not in std** | **Fields, methods, inheritance, defaults, constraints, literals, `is`** | `object_types.ls` (**NEW**) |
| Namespace | **Not in std** | **Declaration, namespaced tags/attrs/symbols, scoping** | `namespace_decl.ls` (**NEW**) |
| Error handling | **Not in std** | **`T^E` return, `raise`, `^`, `let a^err`, compile enforcement** | `error_handling.ls` (**NEW**) |
| Procedural (`pn`) | **Not in std** | **`var`, assignment, `while`, `break`/`continue`, `return`** | `procedural_basics.ls` (**NEW**) |
| Mutation (proc) | **Not in std** | **Array/map/element assignment, type widening** | `mutation.ls` (**NEW**) |
| Pipe output (`\|>` `\|>>`) | **Not in std** | **Write and append to file** | `pipe_output_stam.ls` (**NEW**) |
| `that` constraints | **Not in std** | **Type constraints, object constraints** | `that_constraints.ls` (**NEW**) |
| Query expressions | **Not in std** | **`?T`, `.?T`, `[T]`, chaining** | `query_expressions.ls` (**NEW**) |
| Truthiness | **Not in std** | **0 truthy, `[]` truthy, `""` falsy, error falsy, `or` idiom** | `truthiness.ls` (**NEW**) |

**New tests needed**: 23 files (5 rebuilt + 18 new constructs)

---

## 4. Updated Boundary & Stress Tests

### 4.1 New Boundary Tests Needed

The existing 10 boundary tests cover numeric/string/collection/nesting limits and cross-type interactions. We need additional boundary tests for new features:

| Test File | What It Tests | Priority |
|-----------|--------------|----------|
| `boundary/object_limits.ls` | Many fields, deep inheritance, many methods, constraint edge cases | High |
| `boundary/datetime_limits.ls` | Year extremes, leap years, edge timestamps, timezone boundaries | High |
| `boundary/error_chain_depth.ls` | Deep error chaining (error wrapping error wrapping error...) | Medium |
| `boundary/mutation_limits.ls` | Rapid type changes, many map mutations, array grow/shrink | High |
| `boundary/match_exhaustiveness.ls` | Many arms, deep nesting, large or-patterns | Medium |
| `boundary/path_edge_cases.ls` | Path with spaces, dots, special chars, long paths | Medium |
| `boundary/spread_limits.ls` | Large spread, nested spread, spread of empty collections | Medium |
| `boundary/query_depth.ls` | Deep element nesting, wide trees, query returning thousands | Medium |
| `boundary/vector_large.ls` | Very large vectors (100K+), cumsum overflow, norm precision | Medium |
| `boundary/typed_array_limits.ls` | `int[]`/`float[]` large allocation, auto-conversion boundaries | High |

### 4.2 Cross-Type Interaction Matrix Update

The existing 5 cross-type tests cover arithmetic, comparison, concat, logical, and equality across basic types. We need to extend for new types:

| Test File | Types Tested | Coverage |
|-----------|-------------|----------|
| `boundary/cross_type_object.ls` | Object `is` nominal vs structural, object == map, object in collection | **NEW** |
| `boundary/cross_type_error.ls` | Error `is`, error truthiness, error `or` default, error in collection | **NEW** |
| `boundary/cross_type_datetime.ls` | DateTime arithmetic (if supported), comparison, in collection | **NEW** |
| `boundary/cross_type_path.ls` | Path equality, path ++ string, path in collection | **NEW** |

---

## 5. Updated Negative Tests

The existing 4 negative tests cover division by zero, error propagation, index OOB, and type mismatch. We need more:

| Test File | Error Scenario | Expected |
|-----------|---------------|----------|
| `negative/unhandled_error.ls` | Call error-returning fn without `^` or `let a^err` | **Compile error E228** |
| `negative/raise_in_pure_fn.ls` | Use `raise` in fn with plain `T` return | **Compile error** |
| `negative/immutable_reassign.ls` | Reassign `let` binding, reassign fn parameter | **Error E211** |
| `negative/mutation_in_fn.ls` | Use `var`, assignment, `while` inside `fn` | **Compile error** |
| `negative/wrong_arg_count.ls` | Too few/many args to typed function | **Error E206** |
| `negative/undefined_variable.ls` | Reference undefined name | **Error E202** |
| `negative/undefined_function.ls` | Call non-existent function | **Error E203** |
| `negative/circular_import.ls` | Module A imports B imports A | **Error, no crash** |
| `negative/object_constraint_fail.ls` | Construct object that violates `that` constraint | **Runtime error** |
| `negative/typed_array_type_error.ls` | Assign string to `var x: int[] = [1]` | **Compile error** |
| `negative/namespace_conflict.ls` | Variable name same as namespace prefix | **Compile error** |
| `negative/stack_overflow_mutual.ls` | Mutually recursive fns without base case | **Error, no crash** |

---

## 6. Updated Integration Tests

The existing 3 integration tests cover computation, transformation, and functional patterns. Add scenarios combining new features:

| Test File | Scenario | Features Exercised |
|-----------|----------|-------------------|
| `integration/object_pipeline.ls` | Define object types → create instances → pipe transform → query → format | Object types, pipe, query, format |
| `integration/error_safe_pipeline.ls` | Read file → parse → validate → transform, with `^` propagation throughout | Error handling, `T^E`, `let a^err`, `or` default |
| `integration/document_processing.ls` | Parse HTML → query for elements → namespace-aware transform → output | Elements, query, namespace, format |
| `integration/procedural_workflow.ls` | `pn main()` with mutation, loops, I/O, error handling, clock timing | `pn`, `var`, `while`, mutation, `cmd()`, `clock()` |
| `integration/data_science.ls` | Load CSV → vector ops → stats → visualization data | `input()`, vectors, variance/deviation, dot/norm |
| `integration/match_dispatch.ls` | Match on typed data, dispatch to handlers, accumulate results | Match, type patterns, closures, higher-order |
| `integration/import_module_system.ls` | Multi-module project with `pub` exports, type sharing, alias imports | Import, pub, modules, type declarations |
| `integration/string_validation.ls` | Define string patterns → validate user input → classify with match | String patterns, `is`, match, find/replace |

---

## 7. Proposed Directory Structure

```
test/std/
├── generate_expected.sh
├── core/
│   ├── datatypes/                    ← 20 files (rebuilt + 3 new)
│   │   ├── null_basic.ls / .expected
│   │   ├── boolean_basic.ls / .expected
│   │   ├── integer_basic.ls / .expected
│   │   ├── integer64_basic.ls / .expected
│   │   ├── float_basic.ls / .expected
│   │   ├── decimal_basic.ls / .expected
│   │   ├── string_basic.ls / .expected
│   │   ├── symbol_basic.ls / .expected
│   │   ├── binary_basic.ls / .expected
│   │   ├── datetime_basic.ls / .expected
│   │   ├── array_basic.ls / .expected
│   │   ├── list_basic.ls / .expected
│   │   ├── map_basic.ls / .expected
│   │   ├── element_basic.ls / .expected
│   │   ├── range_basic.ls / .expected
│   │   ├── path_basic.ls / .expected           ← NEW
│   │   ├── object_basic.ls / .expected         ← NEW
│   │   ├── function_basic.ls / .expected
│   │   ├── error_basic.ls / .expected
│   │   └── type_basic.ls / .expected           ← NEW
│   ├── operators/                    ← 22 files (rebuilt + 8 new)
│   │   ├── arithmetic_basic.ls / .expected
│   │   ├── integer_division.ls / .expected
│   │   ├── modulo.ls / .expected
│   │   ├── exponent.ls / .expected
│   │   ├── concatenation.ls / .expected
│   │   ├── logical_ops.ls / .expected
│   │   ├── comparison_ops.ls / .expected
│   │   ├── type_check_is.ls / .expected
│   │   ├── membership_in.ls / .expected
│   │   ├── pipe_operator.ls / .expected
│   │   ├── where_filter.ls / .expected
│   │   ├── range_operator.ls / .expected
│   │   ├── vector_arithmetic.ls / .expected
│   │   ├── precedence.ls / .expected
│   │   ├── query_operator.ls / .expected       ← NEW
│   │   ├── child_query_operator.ls / .expected ← NEW
│   │   ├── set_operators.ls / .expected        ← NEW
│   │   ├── spread_operator.ls / .expected      ← NEW
│   │   ├── pipe_output.ls / .expected          ← NEW
│   │   ├── error_propagation_op.ls / .expected ← NEW
│   │   ├── that_operator.ls / .expected        ← NEW
│   │   └── bitwise_ops.ls / .expected          ← NEW
│   ├── functions/                    ← 18 files (rebuilt + 9 new)
│   │   ├── collection_basic.ls / .expected
│   │   ├── collection_construction.ls / .expected
│   │   ├── string_pattern_funcs.ls / .expected
│   │   ├── string_manipulation.ls / .expected
│   │   ├── string_index.ls / .expected
│   │   ├── math_basic.ls / .expected
│   │   ├── math_advanced.ls / .expected        ← NEW
│   │   ├── math_minmax.ls / .expected          ← NEW
│   │   ├── stats_basic.ls / .expected
│   │   ├── stats_advanced.ls / .expected       ← NEW
│   │   ├── vector_algebra.ls / .expected       ← NEW
│   │   ├── type_conversion.ls / .expected
│   │   ├── type_inspection.ls / .expected      ← NEW
│   │   ├── map_filter_reduce.ls / .expected
│   │   ├── variadic_args.ls / .expected        ← NEW
│   │   ├── datetime_funcs.ls / .expected       ← NEW
│   │   ├── io_pure.ls / .expected              ← NEW
│   │   └── io_procedural.ls / .expected        ← NEW
│   └── statements/                   ← 23 files (rebuilt + 18 new)
│       ├── let_binding.ls / .expected
│       ├── if_expression.ls / .expected
│       ├── for_expression.ls / .expected
│       ├── for_clauses.ls / .expected
│       ├── for_map_iteration.ls / .expected    ← NEW
│       ├── for_multi_variable.ls / .expected   ← NEW
│       ├── match_expression.ls / .expected
│       ├── function_definition.ls / .expected
│       ├── arrow_functions.ls / .expected      ← NEW
│       ├── closures.ls / .expected             ← NEW
│       ├── higher_order.ls / .expected         ← NEW
│       ├── import_module.ls / .expected        ← NEW
│       ├── type_declarations.ls / .expected    ← NEW
│       ├── string_patterns.ls / .expected      ← NEW
│       ├── object_types.ls / .expected         ← NEW
│       ├── namespace_decl.ls / .expected       ← NEW
│       ├── error_handling.ls / .expected       ← NEW
│       ├── procedural_basics.ls / .expected    ← NEW
│       ├── mutation.ls / .expected             ← NEW
│       ├── pipe_output_stam.ls / .expected     ← NEW
│       ├── that_constraints.ls / .expected     ← NEW
│       ├── query_expressions.ls / .expected    ← NEW
│       └── truthiness.ls / .expected           ← NEW
├── boundary/                         ← 24 files (10 existing + 14 new)
│   ├── numeric_limits.ls / .expected           existing
│   ├── string_limits.ls / .expected            existing
│   ├── collection_limits.ls / .expected        existing
│   ├── nesting_limits.ls / .expected           existing
│   ├── function_limits.ls / .expected          existing
│   ├── cross_type_arithmetic.ls / .expected    existing
│   ├── cross_type_comparison.ls / .expected    existing
│   ├── cross_type_concat.ls / .expected        existing
│   ├── cross_type_equality.ls / .expected      existing
│   ├── cross_type_logical.ls / .expected       existing
│   ├── object_limits.ls / .expected            ← NEW
│   ├── datetime_limits.ls / .expected          ← NEW
│   ├── error_chain_depth.ls / .expected        ← NEW
│   ├── mutation_limits.ls / .expected          ← NEW
│   ├── match_exhaustiveness.ls / .expected     ← NEW
│   ├── path_edge_cases.ls / .expected          ← NEW
│   ├── spread_limits.ls / .expected            ← NEW
│   ├── query_depth.ls / .expected              ← NEW
│   ├── vector_large.ls / .expected             ← NEW
│   ├── typed_array_limits.ls / .expected       ← NEW
│   ├── cross_type_object.ls / .expected        ← NEW
│   ├── cross_type_error.ls / .expected         ← NEW
│   ├── cross_type_datetime.ls / .expected      ← NEW
│   └── cross_type_path.ls / .expected          ← NEW
├── integration/                      ← 11 files (3 existing + 8 new)
│   ├── complex_computation.ls / .expected      existing
│   ├── data_transformation.ls / .expected      existing
│   ├── functional_patterns.ls / .expected      existing
│   ├── object_pipeline.ls / .expected          ← NEW
│   ├── error_safe_pipeline.ls / .expected      ← NEW
│   ├── document_processing.ls / .expected      ← NEW
│   ├── procedural_workflow.ls / .expected      ← NEW
│   ├── data_science.ls / .expected             ← NEW
│   ├── match_dispatch.ls / .expected           ← NEW
│   ├── import_module_system.ls / .expected     ← NEW
│   └── string_validation.ls / .expected        ← NEW
├── negative/                         ← 16 files (4 existing + 12 new)
│   ├── division_by_zero.ls / .expected         existing
│   ├── error_propagation.ls / .expected        existing
│   ├── index_out_of_bounds.ls / .expected      existing
│   ├── type_mismatch.ls / .expected            existing
│   ├── unhandled_error.ls / .expected          ← NEW
│   ├── raise_in_pure_fn.ls / .expected         ← NEW
│   ├── immutable_reassign.ls / .expected       ← NEW
│   ├── mutation_in_fn.ls / .expected           ← NEW
│   ├── wrong_arg_count.ls / .expected          ← NEW
│   ├── undefined_variable.ls / .expected       ← NEW
│   ├── undefined_function.ls / .expected       ← NEW
│   ├── circular_import.ls / .expected          ← NEW
│   ├── object_constraint_fail.ls / .expected   ← NEW
│   ├── typed_array_type_error.ls / .expected   ← NEW
│   ├── namespace_conflict.ls / .expected       ← NEW
│   └── stack_overflow_mutual.ls / .expected    ← NEW
└── performance/                      ← 2 files (existing)
    ├── large_data.ls / .expected               existing
    └── recursive_perf.ls / .expected           existing
```

---

## 8. Test Content Specifications

### 8.1 Object Types (`core/statements/object_types.ls`)

```lambda
// Test: Object Types — Fields, Methods, Inheritance, Defaults, Constraints
// Layer: 3 | Category: statement | Covers: object type system

// Basic object type with fields and method
type Point {
    x: float, y: float;
    fn magnitude() => sqrt(x**2 + y**2)
}

let p = {Point x: 3.0, y: 4.0}
p.x                          // 3
p.y                          // 4
p.magnitude()                // 5
p is Point                   // true
p is object                  // true
p is map                     // true
{x: 1.0, y: 2.0} is Point   // false (plain map, nominal check)

// Inheritance
type Circle : Point {
    radius: float;
    fn area() => 3.14159 * radius ** 2
}

let c = {Circle x: 0.0, y: 0.0, radius: 5.0}
c.area()                     // ~78.54
c is Circle                  // true
c is Point                   // true (inheritance)

// Default values
type Counter {
    value: int = 0;
    fn double() => value * 2
}
let ct = {Counter}
ct.value                     // 0
ct.double()                  // 0

// Object update (copy with overrides)
let p2 = {Point p, x: 10.0}
p2.x                         // 10
p2.y                         // 4 (from p)

// Constraints
type PositiveInt {
    value: int that (~ > 0);
}
```

### 8.2 Match Expressions (`core/statements/match_expression.ls`)

```lambda
// Test: Match Expressions — Type, Literal, Symbol, Or-Patterns, Nested
// Layer: 3 | Category: statement | Covers: match expression

// Type patterns
fn describe(value) => match value {
    case int: "integer"
    case string: "text"
    case bool: "boolean"
    default: "other"
}
describe(42)         // "integer"
describe("hello")    // "text"
describe(true)       // "boolean"
describe([1,2])      // "other"

// Literal patterns
fn status(code: int) => match code {
    case 200: "OK"
    case 404: "Not Found"
    case 500: "Server Error"
    default: "Unknown"
}
status(200)          // "OK"
status(404)          // "Not Found"

// Symbol patterns
fn color(level) => match level {
    case 'info: "blue"
    case 'warn: "yellow"
    case 'error: "red"
    default: "white"
}
color('info)         // "blue"

// Or-patterns
fn day_type(day) => match day {
    case 'mon | 'tue | 'wed | 'thu | 'fri: "weekday"
    case 'sat | 'sun: "weekend"
    default: "unknown"
}
day_type('mon)       // "weekday"
day_type('sat)       // "weekend"

// Range patterns
fn grade(score: int) => match score {
    case 90 to 100: "A"
    case 80 to 89: "B"
    case 70 to 79: "C"
    default: "F"
}
grade(95)            // "A"

// Current item reference ~
fn check(n: int) => match n {
    case 0: "zero"
    case int: if (~ > 0) "positive" else "negative"
}
check(5)             // "positive"
check(-3)            // "negative"

// Match in let binding
let label = match 'ok {
    case 'ok: "success"
    case 'error: "failure"
}
label                // "success"
```

### 8.3 Error Handling (`core/statements/error_handling.ls`)

```lambda
// Test: Error Handling — T^E return, raise, ^ propagation, let a^err
// Layer: 3 | Category: statement | Covers: error handling system

// Function that can raise error
fn divide(a, b) int^ {
    if (b == 0) raise error("division by zero")
    else a / b
}

// Error propagation with ^
fn compute(x) int^ {
    let a = divide(10, x)^
    a + 5
}

// Error destructuring
let result^err = divide(10, 0)
(result == null)     // true
(^err)               // true (error check shorthand)
err.message          // "division by zero"

// Successful case
let val^err2 = divide(10, 2)
val                  // 5
(^err2)              // false

// Error chaining
fn load(path) string^ {
    let data^err = divide(10, 0)
    if (^err) raise error("load failed", err)
    else string(data)
}

// error or default (error is falsy)
let safe = divide(10, 0) or 0
safe                 // 0

// Error type checking
let e = error("test error")
e is error           // true
e.code               // 318 (user_error default)
```

### 8.4 Query Expressions (`core/operators/query_operator.ls`)

```lambda
// Test: Query Operators — ?T recursive, .?T self-inclusive, [T] child-level
// Layer: 3 | Category: operator | Covers: query expressions

let doc = <html;
    <head; <title; "Test">>
    <body;
        <div class: "main";
            <p; "Hello">
            <p; "World">
            <span; 42>
        >
        <div class: "footer";
            <p; "Footer">
        >
    >
>

// Recursive query ?
doc?<p>              // all <p> elements at any depth

// Self-inclusive .?
let div = <div; <div; "inner">>
div.?<div>           // includes div itself

// Child-level query [T]
[1, "hello", 3, true][int]      // (1, 3)
{name: "Alice", age: 30}[string]  // ("Alice")

// Chaining queries
doc?<div>[<p>]       // <p> children of all <div>s
```

### 8.5 Truthiness (`core/statements/truthiness.ls`)

```lambda
// Test: Truthiness Rules — Lambda-specific truthy/falsy semantics
// Layer: 3 | Category: statement | Covers: truthiness system

// 0 is TRUTHY in Lambda (unlike JS/Python)
(if (0) "yes" else "no")           // "yes"

// Empty collections are TRUTHY
(if ([]) "yes" else "no")          // "yes"
(if ({}) "yes" else "no")          // "yes"

// null is FALSY
(if (null) "yes" else "no")        // "no"

// false is FALSY
(if (false) "yes" else "no")       // "no"

// error is FALSY (enables `or` default pattern)
let e = error("test")
(if (e) "yes" else "no")           // "no"

// or with falsy values
null or "default"                   // "default"
false or "fallback"                 // "fallback"
error("x") or 42                   // 42

// or with truthy values
0 or "default"                     // 0 (0 is truthy!)
[] or "default"                    // [] (empty array is truthy!)
"hello" or "default"              // "hello"
```

### 8.6 Spread Operator (`core/operators/spread_operator.ls`)

```lambda
// Test: Spread Operator — *expr in arrays, lists, function calls
// Layer: 3 | Category: operator | Covers: spread

let a = [1, 2, 3]

// Array spread
[0, *a, 4]                         // [0, 1, 2, 3, 4]

// List spread
let b = (10, 20)
(*a, *b)                           // (1, 2, 3, 10, 20)

// Nested spreading
let nested = [[1, 2], [3, 4]]
[*nested[0], *nested[1]]           // [1, 2, 3, 4]

// Spreadable for-expression
[for (i in 1 to 3) i * 10]         // [10, 20, 30]
[0, for (x in [1, 2]) x * 5, 99]   // [0, 5, 10, 99]

// Spreadable null (empty for)
[for (i in []) i]                   // []
[1, for (i in []) i, 2]            // [1, 2]
```

### 8.7 DateTime Members (`core/datatypes/datetime_basic.ls`)

```lambda
// Test: DateTime — Literals, member properties, format, constructors, sub-types
// Layer: 3 | Category: datatype | Covers: datetime type system

// DateTime literal
let dt = t'2025-04-26T10:30:45'

// Date properties
dt.year              // 2025
dt.month             // 4
dt.day               // 26
dt.weekday           // (0-6)
dt.quarter           // 2

// Time properties
dt.hour              // 10
dt.minute            // 30
dt.second            // 45

// Meta properties
dt.is_date           // true
dt.is_time           // true
dt.is_leap_year      // false
dt.days_in_month     // 30

// Extraction
dt.date              // t'2025-04-26'
dt.time              // t'10:30:45'

// Sub-type checks
t'2025-04-26' is date       // true
t'2025-04-26' is datetime   // true
t'10:30:00' is time         // true

// Format
dt.format("YYYY-MM-DD")     // "2025-04-26"
dt.format('iso)              // ISO 8601 string

// Constructors
datetime(2025, 4, 26)        // t'2025-04-26'
date(2025, 4, 26)            // t'2025-04-26'
time(10, 30, 45)             // t'10:30:45'
```

### 8.8 Namespace (`core/statements/namespace_decl.ls`)

```lambda
// Test: Namespace — Declaration, namespaced tags/attrs, scoping
// Layer: 3 | Category: statement | Covers: namespace system

namespace svg: 'http://www.w3.org/2000/svg'

// Namespaced element tag
let rect = <svg.rect svg.width: 100, svg.height: 50>

// Access namespaced attribute
rect.svg.width       // 100
rect.svg.height      // 50

// Qualified symbols
string(svg.rect)     // "svg.rect"
svg.rect == svg.rect // true

// Unqualified tags still work
let div = <div class: "main">
div.class            // "main"
```

### 8.9 For Map Iteration (`core/statements/for_map_iteration.ls`)

```lambda
// Test: For Map Iteration — keys only, key-value pairs, with where
// Layer: 3 | Category: statement | Covers: for-at map iteration

// Keys only
for (k at {a: 1, b: 2, c: 3}) k
// ["a", "b", "c"]

// Key-value pairs
for (k, v at {a: 1, b: 2, c: 3}) k ++ "=" ++ string(v)
// ["a=1", "b=2", "c=3"]

// With where filter
for (k, v at {a: 1, b: 5, c: 2} where v > 2) k
// ["b"]

// Dynamic map
let m = map(["x", 10, "y", 20])
for (k at m) k
// ["x", "y"]
```

### 8.10 Pipe with `~#` (`core/operators/pipe_operator.ls`)

```lambda
// Test: Pipe — basic, ~# index/key, aggregated, chained, where
// Layer: 3 | Category: operator | Covers: pipe expressions

// Basic auto-mapping
[1, 2, 3] | ~ * 2                  // [2, 4, 6]

// ~# gives index for arrays
['a', 'b', 'c'] | {index: ~#, value: ~}
// [{index: 0, value: 'a'}, ...]

// ~# gives key for maps
{a: 1, b: 2} | {key: ~#, val: ~}
// [{key: 'a', val: 1}, ...]

// Scalar pipe
42 | ~ * 2                         // 84

// Aggregated pipe (no ~)
[3, 1, 4, 1, 5] | sum              // 14
[3, 1, 4, 1, 5] | sort             // [1, 1, 3, 4, 5]

// Chained
[1, 2, 3, 4, 5] | ~ ** 2 | ~ + 1  // [2, 5, 10, 17, 26]

// Where
[1, 2, 3, 4, 5] where ~ > 3       // [4, 5]
```

---

## 9. Implementation Roadmap

### Phase 3A: Rebuild Core Foundation (Week 1-2)

| Task | Files | Effort | Impact |
|------|-------|--------|--------|
| Rebuild `core/datatypes/` (17 original + 3 new: path, object, type) | 20 | 3 days | Restores structural type coverage |
| Rebuild `core/operators/` (14 original + 8 new) | 22 | 3 days | Restores operator coverage, adds query/set/spread/error ops |
| Rebuild `core/functions/` (9 original + 9 new) | 18 | 2 days | Restores function coverage, adds stats/vector/datetime/IO |
| Rebuild `core/statements/` (5 original + 18 new) | 23 | 3 days | Most critical — all new language constructs |
| Generate expected outputs | all | 0.5 days | Run against lambda.exe, validate |

**Subtotal**: 83 test files, ~11.5 days

### Phase 3B: Expand Boundary & Negative (Week 3)

| Task | Files | Effort | Impact |
|------|-------|--------|--------|
| New boundary tests (object, datetime, mutation, match, path, spread, query, vector, typed array) | 10 | 2 days | Catches crash-class bugs in new features |
| New cross-type interaction tests (object, error, datetime, path) | 4 | 1 day | Type-dispatch bugs |
| New negative tests (12 error scenarios) | 12 | 2 days | Compile-time and runtime error coverage |

**Subtotal**: 26 test files, ~5 days

### Phase 3C: Integration & Polish (Week 4)

| Task | Files | Effort | Impact |
|------|-------|--------|--------|
| New integration tests (8 end-to-end scenarios) | 8 | 3 days | Feature interaction coverage |
| Update `simple_test_runner.sh` for procedural tests (`pn main()`) | 1 | 0.5 days | Support `lambda.exe run` mode |
| Validate all tests pass, fix any discovered bugs | — | 1.5 days | Quality gate |

**Subtotal**: 9 test files, ~5 days

### Phase 3 Total

| Metric | Count |
|--------|-------|
| New test files to create | **117** |
| Total `test/std/` after Phase 3 | **136** (19 existing + 117 new) |
| Estimated effort | **~4 weeks** |

---

## 10. Feature Coverage Summary

### Features Covered by Existing `test/lambda/` But Missing from `test/std/`

These are features that have organic tests but lack structured, specification-driven coverage:

| Feature | `test/lambda/` Files | Structured Coverage |
|---------|---------------------|-------------------|
| Object types | `object.ls` + 7 `object_*.ls` | Phase 3: `object_types.ls`, `object_basic.ls` |
| Namespace | `namespace.ls` (149 lines) | Phase 3: `namespace_decl.ls` |
| Match expression | `match_expr.ls`, `match_string_pattern.ls` | Phase 3: `match_expression.ls` |
| Error handling | `error_handling.ls`, `error_propagation.ls`, `error_union_param.ls` | Phase 3: `error_handling.ls`, `error_propagation_op.ls` |
| Spread | `spread.ls` (51 lines) | Phase 3: `spread_operator.ls` |
| Constrained types | `constrained_type.ls` (76 lines) | Phase 3: `that_constraints.ls`, `that_operator.ls` |
| Method calls | `method_call.ls` (61 lines) | Phase 3: covered in `type_inspection.ls`, function tests |
| Typed arrays | `array_float.ls` | Phase 3: `array_basic.ls`, `typed_array_limits.ls` |
| DateTime | `datetime.ls` (171 lines) | Phase 3: `datetime_basic.ls`, `datetime_funcs.ls` |
| Vectors | 4 `vector_*.ls` files | Phase 3: `vector_arithmetic.ls`, `vector_algebra.ls` |
| Path | `path.ls` (175 lines) | Phase 3: `path_basic.ls`, `path_edge_cases.ls` |
| Query | `query.ls`, `child_query.ls` | Phase 3: `query_operator.ls`, `child_query_operator.ls`, `query_expressions.ls` |
| String patterns | `string_pattern.ls`, `string_pattern_ops.ls` | Phase 3: `string_patterns.ls`, `string_pattern_funcs.ls` |
| Pipe/where | `pipe_where.ls` (151 lines) | Phase 3: `pipe_operator.ls`, `where_filter.ls` |
| Import | 6 `import_*.ls` files | Phase 3: `import_module.ls` |
| Closures | `closure.ls`, `closure_advanced.ls` | Phase 3: `closures.ls` |
| For clauses | `for_clauses_test.ls` (153 lines) | Phase 3: `for_clauses.ls`, `for_map_iteration.ls` |

### Features with NO Existing Tests (Neither Organic nor Structured)

| Feature | Risk | Phase 3 Test |
|---------|------|-------------|
| Set operators (`&`, `\|`, `!`) | Medium — undocumented behavior on edge cases | `set_operators.ls` |
| `that` as pipe filter (`items that ~ > 0`) | Medium — may conflict with `where` | `that_operator.ls` |
| `for` with multiple loop variables | Medium — interaction with spread unclear | `for_multi_variable.ls` |
| Truthiness rules (0 truthy, error falsy) | High — affects all conditional logic | `truthiness.ls` |
| Bitwise operators (in structured test) | Low — niche feature | `bitwise_ops.ls` |
| `variance()`, `deviation()`, `quantile()` | Medium — numerical precision | `stats_advanced.ls` |
| `dot()`, `norm()`, `cumsum()`, `cumprod()`, `argmin()`, `argmax()` | Medium — edge cases (empty, single elem) | `vector_algebra.ls` |
| `normalize()` (Unicode) | Low — rarely used | `string_manipulation.ls` |
| Type as first-class value (`let T = int; x is T`) | Medium — uncommon pattern | `type_basic.ls` |
| `pub` exports (dedicated test) | Low — tested via import | `import_module.ls` |
| Pipe output `\|>` / `\|>>` (in structured test) | Medium — file I/O side effects | `pipe_output.ls` |
| `cmd()` (in structured test) | Medium — platform-dependent | `io_procedural.ls` |
| `clock()` (in structured test) | Low — timing function | `io_procedural.ls` |
| `map()` constructor function | Medium — VMap creation | `map_basic.ls` |
| Null-safe member access chain | Low — tested incidentally | `null_basic.ls` |
| Safe index access (OOB → null) | Low — tested in negative | `array_basic.ls` |
| Negative array indexing | Low — tested incidentally | `array_basic.ls` |
| `int64()` conversion function | Medium — large number handling | `type_conversion.ls` |
| `prod()` function | Low — simple multiplication | `stats_basic.ls` |
| `sign()` function | Low — simple | `math_basic.ls` |

---

## 11. Test Runner Updates

### 11.1 Support for Procedural Tests

Some new tests (mutation, pipe output, procedural basics, IO) require `pn main()` and must be run with `lambda.exe run` instead of plain `lambda.exe`. The test runner needs a convention:

```bash
# If a .ls file starts with "// Mode: procedural", run with `lambda.exe run`
if head -3 "$f" | grep -q "// Mode: procedural"; then
    out=$(./lambda.exe run "$f" 2>/dev/null)
else
    out=$(./lambda.exe "$f" 2>/dev/null)
fi
```

### 11.2 Support for Negative Tests

Negative tests expect non-zero exit codes and specific error messages. Add to runner:

```bash
# If in negative/ directory, expect error output
if [[ "$f" == *"/negative/"* ]]; then
    out=$(./lambda.exe "$f" 2>&1)
    # Filter to just error message lines
    out=$(echo "$out" | grep -E "^(Error|error|E[0-9]{3})" | head -5)
fi
```

### 11.3 Support for Import Tests

Import tests need modules co-located. Convention: `import_module.ls` + `import_module_helper.ls` in the same directory, or a subdirectory `import_module/`.

---

## 12. Success Metrics

| Metric | Current (Mar 3) | After Phase 3 | Target |
|--------|-----------------|---------------|--------|
| `test/std/` total files | 19 | **136** | 150+ |
| Core datatype tests | 0 | **20** | 20 |
| Core operator tests | 0 | **22** | 22 |
| Core function tests | 0 | **18** | 18 |
| Core statement tests | 0 | **23** | 23 |
| Boundary tests | 10 | **24** | 30+ |
| Negative tests | 4 | **16** | 20+ |
| Integration tests | 3 | **11** | 15+ |
| New features covered | 0/20 categories | **20/20** | 20/20 |
| Object type tests | 0 | **3** (datatype + statement + boundary) | 3 |
| Error handling tests | 0 structured | **3** (statement + operator + negative) | 3 |
| Match expression tests | 0 structured | **2** (statement + boundary) | 2 |
| Query expression tests | 0 structured | **4** (2 operators + statement + boundary) | 4 |
| Truthiness test | 0 | **1** | 1 |

---

## 13. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Tests fail due to syntax changes since Feb 2026 | **High** | Medium | Use `generate_expected.sh` to baseline against current engine |
| Some features documented but not yet implemented | Medium | Low | Run each test incrementally, skip unimplemented features |
| Procedural tests have side effects (file writes) | **High** | Medium | Use `./temp/` for all file output, clean up in tests |
| Import tests need helper modules | Medium | Low | Co-locate helper files, document convention |
| Non-deterministic functions (`datetime()`, `clock()`) | Medium | Low | Test structure/type only, not exact values |
| Platform differences for `cmd()` tests | Medium | Medium | Use platform-neutral commands (`echo`, string ops) |
| Test count overwhelms CI runtime | Low | Medium | Phase runner: core/boundary first, integration last |

---

## 14. Summary

The gap between the Phase 2 proposal's claims (76 structured tests) and reality (19 tests) plus 90+ new language features creates a significant structural coverage deficit. This Phase 3 proposal:

1. **Rebuilds the 57 missing `core/` tests** with updated Lambda syntax and expanded scope
2. **Adds 60 new tests** covering every major feature added since February 2026
3. **Extends boundary testing** to new types (objects, datetime, paths) and new features (mutation, match, query)
4. **Expands negative testing** to 16 error scenarios covering compile-time and runtime enforcement
5. **Adds 8 integration tests** combining multiple new features in realistic scenarios

The result: **136 structured tests** covering the full Lambda language specification, organized by feature category with clear traceability from spec to test.

---

## 15. Implementation Status & Test Run Results

**Date**: March 3, 2026
**Status**: All 135 test files implemented — 62 files require syntax fixes

### 15.1 File Count Summary

| Directory | Files | OK | FAIL | Pass Rate |
|-----------|-------|-----|------|-----------|
| `core/datatypes/` | 20 | 15 | 5 | 75% |
| `core/operators/` | 21 | 18 | 3 | 86% |
| `core/functions/` | 18 | 10 | 8 | 56% |
| `core/statements/` | 23 | 6 | 17 | 26% |
| `boundary/` | 24 | 12 | 12 | 50% |
| `negative/` | 16 | 7 | 9 | 44% |
| `integration/` | 11 | 3 | 8 | 27% |
| `performance/` | 2 | 2 | 0 | 100% |
| **Total** | **135** | **73** | **62** | **54%** |

### 15.2 Passing Tests (73 files)

<details>
<summary>Full list of passing tests</summary>

**core/datatypes (15):** array_basic, boolean_basic, datetime_basic, decimal_basic, element_basic, error_basic, float_basic, integer_basic, map_basic, null_basic, object_basic, path_basic, range_basic, symbol_basic, type_basic

**core/operators (18):** arithmetic_basic, child_query_operator, comparison_ops, concatenation, error_propagation_op, exponent, integer_division, logical_ops, modulo, precedence, query_operator, range_operator, set_operators, spread_operator, that_operator, type_check_is, vector_arithmetic, where_filter

**core/functions (10):** collection_construction, io_pure, math_advanced, math_minmax, stats_advanced, stats_basic, string_manipulation, string_pattern_funcs, type_inspection, vector_algebra

**core/statements (6):** for_clauses, for_expression, for_map_iteration, for_multi_variable, let_binding, match_expression

**boundary (12):** collection_limits, cross_type_arithmetic, cross_type_comparison, cross_type_concat, cross_type_equality, cross_type_logical, error_chain_depth, function_limits, mutation_limits, nesting_limits, numeric_limits, string_limits

**negative (7):** division_by_zero, error_propagation, immutable_reassign, index_out_of_bounds, namespace_conflict, type_mismatch, wrong_arg_count

**integration (3):** complex_computation, data_transformation, functional_patterns

**performance (2):** large_data, recursive_perf

</details>

### 15.3 Failing Tests (62 files) — Detailed Error Analysis

#### 15.3.1 CRASHES (2 files) — Engine Bugs

| File | Exit Code | Error |
|------|-----------|-------|
| `boundary/match_exhaustiveness.ls` | **138 (Bus Error)** | Crash during JIT execution — likely MIR codegen bug with exhaustive match patterns |
| `negative/circular_import.ls` | **139 (Segfault)** | Crash when processing circular import — missing graceful error for circular dependencies |

#### 15.3.2 Failing By Issue Category

---

## 16. Discovered Issues — Syntax & API Reference

All issues are categorized by root cause. Each issue lists the affected files and the fix required.

### Issue #1: `fn(x) => body` anonymous function syntax

**Error**: `Unexpected syntax near 'fn'` or `Unexpected syntax near '='`
**Root cause**: Lambda anonymous functions use `(x) => body`, not `fn(x) => body`. The `fn` keyword is only for named function declarations.
**Fix**: Remove `fn` prefix from anonymous lambdas: `fn(x) => x * 2` → `(x) => x * 2`

| Affected file | Error message |
|---------------|---------------|
| `core/statements/closures.ls` | `Unexpected syntax near 'fn' [fn]` |
| `core/statements/higher_order.ls` | `Unexpected syntax near '=' [=]` (from `fn(x) => ...`) |
| `core/statements/import_module.ls` | `Unexpected syntax near '=' [=]` |
| `core/statements/namespace_decl.ls` | `Unexpected syntax near '=' [=]` (inside `map(fn(e) => ...)`) |
| `integration/import_module_system.ls` | `Unexpected syntax near '=' [=]` |
| `integration/match_dispatch.ls` | `Unexpected syntax near '=' [=]` |

### Issue #2: Default parameter syntax `param = default`

**Error**: `Unexpected syntax near '=' [=]`
**Root cause**: Lambda does not support default parameter values in function signatures using `param = value` syntax.
**Fix**: Remove default parameters; use overloaded functions or explicit null checks instead.

| Affected file | Error message |
|---------------|---------------|
| `core/statements/function_definition.ls` | `Unexpected syntax near '=' [=]` at param default |
| `core/functions/map_filter_reduce.ls` | `Unexpected syntax near '=' [=]` at `(acc = 0)` |
| `core/statements/closures.ls` | (combined with Issue #1) |
| `core/statements/higher_order.ls` | (combined with Issue #1) |
| `core/statements/import_module.ls` | (combined with Issue #1) |
| `integration/import_module_system.ls` | (combined with Issue #1) |
| `integration/match_dispatch.ls` | (combined with Issue #1) |

### Issue #3: `filter()` function does not exist

**Error**: `call to undefined function 'filter'`
**Root cause**: Lambda has no built-in `filter()` function. Filtering is done with `where` operator or `that` pattern in comprehensions.
**Fix**: Replace `filter(arr, pred)` or `arr | filter(pred)` with `arr where pred` or `[for (x in arr) if (pred(x)) x]`.

| Affected file | Error message |
|---------------|---------------|
| `core/statements/truthiness.ls` | `call to undefined function 'filter'` |
| `core/statements/query_expressions.ls` | `call to undefined function 'filter'` |
| `core/statements/string_patterns.ls` | (uses `filter` in combination with pattern) |
| `boundary/cross_type_error.ls` | `call to undefined function 'filter'` |
| `boundary/cross_type_object.ls` | `call to undefined function 'filter'` |
| `boundary/typed_array_limits.ls` | `call to undefined function 'filter'` |
| `integration/data_science.ls` | `call to undefined function 'filter'` (×5) |

### Issue #4: `map()` function does not exist

**Error**: `Unexpected syntax near '='` or undefined identifier errors
**Root cause**: Lambda has no built-in `map()` function. Mapping is done with pipe transform `| ~.field`, for-comprehension, or `| expr`.
**Fix**: Replace `map(arr, fn)` or `arr | map(fn)` with `[for (x in arr) fn(x)]` or pipe transforms.

| Affected file | Error message |
|---------------|---------------|
| `core/statements/namespace_decl.ls` | (combined with `fn(x) =>` issue) |
| `integration/object_pipeline.ls` | `Unexpected syntax` from `map((p) => ...)` |
| `integration/match_dispatch.ls` | (combined with other issues) |

### Issue #5: `reduce()` function does not exist

**Error**: `call to undefined function 'reduce'`
**Root cause**: Lambda has no built-in `reduce()`. Use `sum()`, recursive functions, or for-comprehension with accumulation.
**Fix**: Replace `reduce(arr, fn, init)` with `sum(arr)` where applicable, or rewrite as recursive function.

| Affected file | Error message |
|---------------|---------------|
| `core/functions/map_filter_reduce.ls` | (combined with default param issue) |
| `boundary/vector_large.ls` | `call to undefined function 'reduce'` |

### Issue #6: `round()` function does not exist

**Error**: `call to undefined function 'round'`
**Root cause**: Lambda does not have a `round()` function. Rounding may need alternative approach.
**Fix**: Remove or replace `round()` calls.

| Affected file | Error message |
|---------------|---------------|
| `core/functions/math_basic.ls` | `call to undefined function 'round'` |

### Issue #7: `slice()` and `concat()` functions do not exist

**Error**: `call to undefined function 'slice'` / `call to undefined function 'concat'`
**Root cause**: Lambda does not have standalone `slice()` or `concat()` functions. Use array indexing with ranges or `++` concatenation.
**Fix**: Replace `slice(arr, start, end)` with `arr[start to end]`; replace `concat(a, b)` with `a ++ b`.

| Affected file | Error message |
|---------------|---------------|
| `core/datatypes/string_basic.ls` | `call to undefined function 'slice'` |
| `core/functions/string_index.ls` | `call to undefined function 'slice'` |
| `core/functions/collection_basic.ls` | `call to undefined function 'concat'` + `call to undefined function 'slice'` |

### Issue #8: `number()` function does not exist

**Error**: `failed to resolve native fn/pn: fn_number` (link-time error)
**Root cause**: Lambda has no generic `number()` conversion function. Use `int()` or `float()` specifically.
**Fix**: Replace `number(x)` with `int(x)` or `float(x)`.

| Affected file | Error message |
|---------------|---------------|
| `core/functions/type_conversion.ls` | `failed to resolve native fn/pn: fn_number` |

### Issue #9: `0L` int64 literal suffix not supported

**Error**: `Unexpected syntax near '0L'` / `Unexpected syntax near 'L'`
**Root cause**: Lambda does not support `L` suffix for int64 literals. Use `int64()` constructor.
**Fix**: Replace `42L` with `int64(42)`.

| Affected file | Error message |
|---------------|---------------|
| `core/datatypes/integer64_basic.ls` | `Unexpected syntax near '0L'` and `Unexpected syntax near 'L'` (×22 errors) |

### Issue #10: `x'...'` binary literal prefix not supported

**Error**: `Unexpected syntax near 'x'`
**Root cause**: Lambda binary literals use different prefix syntax.
**Fix**: Use `b'\x48\x65\x6C'` or the correct Lambda binary syntax.

| Affected file | Error message |
|---------------|---------------|
| `core/datatypes/binary_basic.ls` | `Unexpected syntax near 'x' [identifier]` (×11 errors) |

### Issue #11: `0xFF` / `0b1010` hex/binary integer literals not supported

**Error**: `Unexpected syntax near '0xF0'`
**Root cause**: Lambda does not support hexadecimal or binary integer literal syntax.
**Fix**: Convert hex/binary to decimal equivalents: `0xFF` → `255`, `0b1010` → `10`.

| Affected file | Error message |
|---------------|---------------|
| `core/operators/bitwise_ops.ls` | `Unexpected syntax near '0x...'` (×17 errors) |

### Issue #12: `()` empty tuple/list is not valid syntax

**Error**: `Unexpected syntax near ')'`
**Root cause**: Lambda does not support `()` as an empty list literal. Lists require at least one element or use a different constructor.
**Fix**: Use `list()` or remove empty list tests.

| Affected file | Error message |
|---------------|---------------|
| `core/datatypes/list_basic.ls` | `Unexpected syntax near ')'` |

### Issue #13: `if` without `else` is not allowed in functional mode

**Error**: `Unexpected syntax near 'if (true) "only if"'`
**Root cause**: Lambda `if` is an expression that always requires an `else` branch to produce a value.
**Fix**: Always provide `else` clause: `if (cond) x else null`.

| Affected file | Error message |
|---------------|---------------|
| `core/statements/if_expression.ls` | `Unexpected syntax near 'if (true) "only if"'` |

### Issue #14: Arrow function with block body `{ }` interpreted as map

**Error**: `Unexpected syntax near '2' [primary_expr]`
**Root cause**: In Lambda, `(x) => { ... }` interprets `{ ... }` as a map literal, not a block body. Block bodies with local variables require `pn` (procedural function).
**Fix**: Use `(x) => expr` for single expressions, or define a `pn` for multi-statement bodies.

| Affected file | Error message |
|---------------|---------------|
| `core/statements/arrow_functions.ls` | `Unexpected syntax near '2' [primary_expr]` at block body |

### Issue #15: `fn name(...args)` variadic syntax not supported

**Error**: `Unexpected '...' — not a spread operator in Lambda`
**Root cause**: Lambda uses `...` keyword in function signature differently — variadics use `...` param marker with `varg()` / `varg(n)` to access arguments.
**Fix**: Use Lambda's variadic syntax: `fn name(required, ...) => ...` with `varg()` inside body.

| Affected file | Error message |
|---------------|---------------|
| `core/functions/variadic_args.ls` | `Unexpected '...' — not a spread operator in Lambda` (×4) |
| `core/statements/function_definition.ls` | `Unexpected '...' — not a spread operator in Lambda` |

### Issue #16: `that value > 0` constraint syntax is wrong

**Error**: `Unexpected syntax near 'that value >'`
**Root cause**: Object type `that` constraints use `that (predicate-with-tilde)` syntax, not `that field op value`. The tilde `~` refers to the current object.
**Fix**: Replace `that value > 0` with `that (~.value > 0)` for field constraints, or `that (~ > 0)` for scalar constraints.

| Affected file | Error message |
|---------------|---------------|
| `core/statements/that_constraints.ls` | `Unexpected syntax near 'that value >'` (×13 errors) |
| `core/statements/object_types.ls` | (combined with type field syntax) |
| `negative/object_constraint_fail.ls` | `Unexpected syntax near '<='` in `that min <= max` |
| `core/statements/type_declarations.ls` | (combined with other issues) |

### Issue #17: Object type field syntax errors (defaults, colons)

**Error**: `Unexpected syntax near ':'` or `Unexpected syntax near '"localhost"'`
**Root cause**: Object type field declarations with default values (`field: type = default`) or certain complex type definitions have syntax that doesn't match Lambda's parser expectations.
**Fix**: Review and correct object type declaration syntax per Lambda spec.

| Affected file | Error message |
|---------------|---------------|
| `core/statements/object_types.ls` | `Unexpected syntax near ':'` (×23 errors) |
| `core/statements/type_declarations.ls` | `Unexpected syntax near '"localhost"'` |
| `integration/object_pipeline.ls` | `Unexpected syntax near ':'` |
| `boundary/object_limits.ls` | `Unexpected syntax near 'f06: int = 6'` (field defaults in type) |

### Issue #18: `&` used for string concatenation instead of `++`

**Error**: `Unexpected syntax near '&'`
**Root cause**: Lambda string concatenation uses `++`, not `&`.
**Fix**: Replace `&` with `++` in all string concatenation expressions.

| Affected file | Error message |
|---------------|---------------|
| `integration/document_processing.ls` | `Unexpected syntax near '<th> "Item " &'` |
| `integration/object_pipeline.ls` | (combined with other issues) |
| `integration/procedural_workflow.ls` | (combined with other issues) |

### Issue #19: `today()` is a procedure — cannot call in functional mode

**Error**: `procedure 'today' cannot be called in a function` (E224)
**Root cause**: `today()` is impure (depends on system clock) and is marked as a procedure. It can only be called from `pn` functions.
**Fix**: Move `today()` calls to procedural mode test, or test `date()` constructor instead.

| Affected file | Error message |
|---------------|---------------|
| `core/functions/datetime_funcs.ls` | `procedure 'today' cannot be called in a function` (E224, ×2) |

### Issue #20: `cmd()` error must be handled — E228

**Error**: `error from 'result' must be handled` (E228)
**Root cause**: `cmd()` returns `T^E` error union. Callers must use `let result^err = cmd(...)` or `cmd(...)?` to handle the potential error.
**Fix**: Add error destructuring: `let result^err = cmd("echo hello")`.

| Affected file | Error message |
|---------------|---------------|
| `core/functions/io_procedural.ls` | `error from 'result' must be handled` (E228) |

### Issue #21: `fn name(params) { body }` uses `fn` instead of `pn`

**Error**: `Function body requires '=>' or '{...}'`
**Root cause**: In Lambda, `fn` requires expression body with `=>`. Block body functions with `{ }` must use `pn` (procedural function).
**Fix**: Replace `fn name(params) { body; return x }` with either `fn name(params) => expr` or `pn name(params) { body; return x }`.

| Affected file | Error message |
|---------------|---------------|
| `core/statements/error_handling.ls` | `Function body requires '=>' or '{...}'` |
| `integration/error_safe_pipeline.ls` | `Function body requires '=>' or '{...}'` |

### Issue #22: Bare comparisons at expression level

**Error**: `Expected ';'`
**Root cause**: Bare comparison expressions like `d1 < d2` at the top level can be ambiguous with element syntax `<`. Parentheses are required.
**Fix**: Wrap comparisons in parens: `d1 < d2` → `(d1 < d2)`.

| Affected file | Error message |
|---------------|---------------|
| `boundary/cross_type_datetime.ls` | `Expected ';'` at `< d2` |
| `boundary/datetime_limits.ls` | `Expected ';'` (×2) |
| `boundary/cross_type_path.ls` | `Unexpected syntax near '<'` |

### Issue #23: `/http://...` path URL scheme syntax not supported

**Error**: `Unexpected syntax near '/http:'`
**Root cause**: Lambda path literals don't support URL scheme syntax like `/http://example.com`.
**Fix**: Remove URL-scheme path tests or use string representation.

| Affected file | Error message |
|---------------|---------------|
| `boundary/path_edge_cases.ls` | `Unexpected syntax near '/http:'` (×4 errors) |
| `boundary/cross_type_path.ls` | `Unexpected syntax near '/http:'` |

### Issue #24: `| sum` / `| sort` / `| len` bare identifier pipe operand

**Error**: `undefined identifier 'sum'` / `undefined identifier 'sort'` / `undefined identifier 'len'`
**Root cause**: Piping to a bare identifier (`arr | sum`) doesn't work — the identifier is not resolved as a function call. Need `sum(arr)` syntax instead.
**Fix**: Replace `arr | sum` with `sum(arr)`, or use `| ~expression` pipe form.

| Affected file | Error message |
|---------------|---------------|
| `core/operators/pipe_operator.ls` | `undefined identifier 'sum'`, `undefined identifier 'sort'`, `undefined identifier 'len'` |

### Issue #25: `string pattern = [char-class]` declaration syntax

**Error**: `Unexpected syntax near 'letters = [a'`
**Root cause**: String pattern declarations use `string name = ^regex$` syntax (without brackets for the variable name assignment). The specific syntax for character class patterns needs correction.
**Fix**: Use correct Lambda regex pattern syntax: `string name = ^[a-zA-Z]+$` (ensure correct declaration format).

| Affected file | Error message |
|---------------|---------------|
| `core/statements/string_patterns.ls` | `Unexpected syntax near 'letters = [a'` (×17 errors) |
| `integration/string_validation.ls` | `Unexpected syntax near 'email_pat = [a'` (×23 errors) |

### Issue #26: `while (true)` and procedural `var` scoping issues

**Error**: `Unexpected syntax near 'while (true) {'` or `Unexpected syntax near 'var product = 0'`
**Root cause**: Certain procedural constructs (`while`, `var` with nested loops) have scoping or syntax restrictions.
**Fix**: Review procedural code structure; may need restructuring of nested loops and variable declarations.

| Affected file | Error message |
|---------------|---------------|
| `core/statements/procedural_basics.ls` | `Unexpected syntax near 'while (true) {'` and `Unexpected syntax near 'var product = 0'` |
| `core/statements/mutation.ls` | `Unexpected syntax near 'while (idx < 10) {'` |

### Issue #27: `|>` pipe-to-file syntax issues

**Error**: `Unexpected syntax near '|'`
**Root cause**: Pipe-to-file operator `|>` has specific syntax requirements that aren't met in the test.
**Fix**: Review correct `|>` usage — may need different indentation or expression structure.

| Affected file | Error message |
|---------------|---------------|
| `core/statements/pipe_output_stam.ls` | `Unexpected syntax near '|' [|]` (×3 errors) |

### Issue #28: `function_basic.ls` type argument mismatch

**Error**: `argument 2 has incompatible type 10, expected 22` (E201)
**Root cause**: Passing a `string` where `string?` (optional string) is expected, or type mismatch in function call.
**Fix**: Review the function signature and call site for type compatibility.

| Affected file | Error message |
|---------------|---------------|
| `core/datatypes/function_basic.ls` | `argument 2 has incompatible type 10, expected 22` at line 41 |

### Issue #29: Query expression `child` identifier and variable scoping

**Error**: `undefined identifier 'child'`, `undefined identifier 'a'`, `undefined identifier 'b'`
**Root cause**: Query expressions with `?` operator produce elements, but subsequent variable references may be scoped incorrectly.
**Fix**: Review query expression usage and variable binding in query context.

| Affected file | Error message |
|---------------|---------------|
| `core/statements/query_expressions.ls` | `undefined identifier 'child'`, `undefined identifier 'a'`, `undefined identifier 'b'` |
| `boundary/query_depth.ls` | `undefined identifier 'child'` |

### Issue #30: Dynamic field access syntax

**Error**: `Unexpected syntax near '('`
**Root cause**: `obj.(expr)` for dynamic field access may not be supported or uses different syntax.
**Fix**: Use alternative approach for dynamic dispatch.

| Affected file | Error message |
|---------------|---------------|
| `integration/match_dispatch.ls` | `Unexpected syntax` at `handlers.(action)(arg)` |

### Issue #31: Namespace member access uses `.` not `:`

**Error**: `Unterminated string literal` / `Unexpected syntax near 'lang:'`
**Root cause**: Tests use `ns:member` syntax but Lambda uses `ns.member` for namespace access. Also, namespace element attribute syntax may differ.
**Fix**: Replace `:` with `.` for namespace member access.

| Affected file | Error message |
|---------------|---------------|
| `core/statements/namespace_decl.ls` | `Unterminated string literal: "container">` and `Unexpected syntax near 'lang:'` |

### Issue #32: Procedural workflow syntax issues

**Error**: `Expected 'identifier'`
**Root cause**: Procedural test has structural syntax issues with variable declarations and assignments in loops.
**Fix**: Review procedural code for Lambda-correct `pn` syntax.

| Affected file | Error message |
|---------------|---------------|
| `integration/procedural_workflow.ls` | `Expected 'identifier'` and `Unexpected syntax near '= current'` |

### Issue #33: `str()` function — may need `string()`

**Error**: `Unexpected syntax` (combined with `&` issue)
**Root cause**: `str()` may not exist in Lambda; use `string()` for type conversion.
**Fix**: Replace `str(x)` with `string(x)`.

| Affected file | Error message |
|---------------|---------------|
| `integration/document_processing.ls` | (combined with `&` concat issue) |

### Issue #34: `typed_array_type_error.ls` syntax issue

**Error**: Parse error on typed array declaration
**Root cause**: Typed array type assertion syntax `let arr: float[] = [...]` may have restrictions that cause parse errors with intentionally wrong types.
**Fix**: Review how to properly trigger typed array type errors at compile time.

| Affected file | Error message |
|---------------|---------------|
| `negative/typed_array_type_error.ls` | Parse error at `let farr: float[] = [1.0, "bad", 3.0]` |

### Issue #35: Spread of empty collections `*[]`

**Error**: `Unexpected syntax near '// Test: Spread Limits'` (parse fails at file start)
**Root cause**: `*[]` (spread of empty array literal) or `*()` may trigger parser issues.
**Fix**: Test spread with non-empty collections, or assign to variable first.

| Affected file | Error message |
|---------------|---------------|
| `boundary/spread_limits.ls` | Parse error — entire file fails at line 1 |

---

### Issue #36: MIR JIT — untyped param evaluates to 0 in nested loops with `float[]` params

**Error**: When a function has multiple `float[]` typed parameters and one untyped parameter (e.g., `dt`), using the untyped parameter in arithmetic expressions inside nested `while` loops causes it to evaluate to 0.
**Root cause**: MIR JIT code generation bug — untyped parameter value is lost/zeroed when used inside nested loop bodies with typed array parameters.
**Workaround**: Explicitly type the parameter as `float` (e.g., `dt: float` instead of `dt`).
**Reproduction**:
```lambda
pn advance(bx: float[], bvx: float[], bmass: float[], dt) {
    var i: int = 0
    while (i < 3) {
        var j: int = i + 1
        while (j < 3) {
            var dx = bx[i] - bx[j]
            var d_sq = dx * dx
            var dist = math.sqrt(d_sq)
            var mag = dt / (d_sq * dist)  // mag is 0! dt is treated as 0
            j = j + 1
        }
        i = i + 1
    }
}
```

| Affected file | Fix applied |
|---------------|-------------|
| `beng/nbody.ls` | Changed `dt` → `dt: float` |
| `awfy/nbody2.ls` | Changed `dt` → `dt: float` |

---

## 17. Negative Tests — Expected vs Actual Behavior

Some negative tests fail as **expected** (producing errors is their purpose). Others fail for the **wrong reason** (syntax issues prevent the intended error from being tested).

### 17.1 Correctly Failing (error produced as intended)

| File | Expected Behavior | Actual Outcome | Status |
|------|-------------------|----------------|--------|
| `negative/mutation_in_fn.ls` | Reject `var`/`while` in `fn` | E: `'var' statement is only allowed in pn` | ✅ Correct error |
| `negative/raise_in_pure_fn.ls` | Runtime error from `raise` | Runtime error E318: `not allowed` | ✅ Correct error |
| `negative/stack_overflow_mutual.ls` | Detect infinite recursion | E308: `Stack overflow — likely infinite recursion` | ✅ Correct error |
| `negative/undefined_variable.ls` | Reject undefined identifiers | `undefined identifier 'nonexistent_var'` | ✅ Correct error |
| `negative/undefined_function.ls` | Reject undefined function calls | `call to undefined function 'nonexistent_function'` | ⚠️ Correct + bonus `unknown binary operator 23` |

### 17.2 Failing for Wrong Reason (syntax prevents intended test)

| File | Intended Test | Actual Error | Fix Needed |
|------|---------------|--------------|------------|
| `negative/object_constraint_fail.ls` | Reject objects violating `that` constraint | `Unexpected syntax near '<='` in `that min <= max` | Fix `that` syntax (Issue #16) |
| `negative/typed_array_type_error.ls` | Reject wrong types in typed array | Parse error on typed array with wrong types | Fix typed array assertion (Issue #34) |
| `negative/unhandled_error.ls` | Require error handling on `T^E` return | Fails to compile (expected, but `.expected` not generated) | Generate `.expected` with error output |
| `negative/circular_import.ls` | Graceful error on circular imports | **CRASH (exit 139, Segfault)** | Engine bug — needs fix |

---

## 18. Issue Priority & Fix Plan

### 18.1 Priority Matrix

| Priority | Issue # | Description | Files Affected | Complexity |
|----------|---------|-------------|----------------|------------|
| **P0-CRASH** | — | `match_exhaustiveness.ls` bus error (exit 138) | 1 | Engine bug |
| **P0-CRASH** | — | `circular_import.ls` segfault (exit 139) | 1 | Engine bug |
| **P1-HIGH** | #1 | `fn(x) =>` anonymous syntax → `(x) =>` | 6 | Simple find-replace |
| **P1-HIGH** | #2 | Default params `= value` not supported | 7 | Remove/restructure |
| **P1-HIGH** | #3 | `filter()` → `where` or comprehension | 7 | Rewrite expressions |
| **P1-HIGH** | #16 | `that value >` → `that (~.value > 0)` | 4 | Syntax correction |
| **P1-HIGH** | #21 | `fn { }` → `pn { }` or `fn =>` | 2 | Change fn to pn |
| **P2-MED** | #4 | `map()` → pipe or comprehension | 3 | Rewrite expressions |
| **P2-MED** | #5 | `reduce()` → sum/recursion | 2 | Rewrite logic |
| **P2-MED** | #7 | `slice()`/`concat()` → range indexing/`++` | 3 | API replacement |
| **P2-MED** | #9 | `42L` → `int64(42)` | 1 | Simple replacement |
| **P2-MED** | #10 | `x'...'` binary prefix | 1 | Syntax correction |
| **P2-MED** | #11 | `0xFF` → `255` decimal | 1 | Convert literals |
| **P2-MED** | #13 | `if` without `else` | 1 | Add `else null` |
| **P2-MED** | #15 | Variadic `...args` → `...` + `varg()` | 2 | Rewrite syntax |
| **P2-MED** | #17 | Object type field defaults | 4 | Restructure types |
| **P2-MED** | #18 | `&` → `++` for concat | 3 | Simple replace |
| **P2-MED** | #22 | Bare `<` comparisons → parens | 3 | Wrap in parens |
| **P2-MED** | #25 | String pattern declaration syntax | 2 | Fix regex patterns |
| **P3-LOW** | #6 | `round()` doesn't exist | 1 | Remove/replace |
| **P3-LOW** | #8 | `number()` → `int()`/`float()` | 1 | Simple replace |
| **P3-LOW** | #12 | `()` empty list syntax | 1 | Use constructor |
| **P3-LOW** | #14 | Arrow block body → separate fn | 1 | Restructure |
| **P3-LOW** | #19 | `today()` is procedure | 1 | Move to procedural |
| **P3-LOW** | #20 | `cmd()` error handling | 1 | Add destructuring |
| **P3-LOW** | #23 | URL path schemes | 2 | Remove/replace |
| **P3-LOW** | #24 | `| sum` bare pipe | 1 | Use `sum(arr)` |
| **P3-LOW** | #26 | `while`/`var` scoping | 2 | Restructure |
| **P3-LOW** | #27 | `|>` pipe-to-file syntax | 1 | Fix syntax |
| **P3-LOW** | #28 | Type argument mismatch | 1 | Fix types |
| **P3-LOW** | #29 | Query `child` identifier | 2 | Fix query usage |
| **P3-LOW** | #30 | Dynamic field access | 1 | Alternative approach |
| **P3-LOW** | #31 | Namespace `:` → `.` | 1 | Simple replace |
| **P3-LOW** | #32 | Procedural workflow syntax | 1 | Restructure |
| **P3-LOW** | #33 | `str()` → `string()` | 1 | Simple replace |
| **P3-LOW** | #34 | Typed array error syntax | 1 | Fix assertion |
| **P3-LOW** | #35 | Spread empty `*[]` | 1 | Restructure |

### 18.2 Fix Effort Estimate

| Category | Fix Type | Est. Files | Est. Time |
|----------|----------|-----------|-----------|
| Simple find-replace | `fn(x) =>` → `(x) =>`, `&` → `++`, `0L` → `int64()`, etc. | ~20 | 30 min |
| Expression rewrite | `filter()` → `where`, `map()` → comprehension, `reduce()` → recursion | ~15 | 1 hour |
| Syntax restructure | `that` constraints, object defaults, string patterns, procedural blocks | ~15 | 1.5 hours |
| Negative test fixes | `.expected` generation for error-producing tests | ~5 | 15 min |
| Engine bugs (P0) | Requires C++ debugging of MIR JIT and import resolver | 2 | Unknown |
| **Total** | | **~57** | **~3.5 hours** (excl. engine bugs) |

---

*Status: Implemented — 135 test files created, 73 passing, 62 requiring fixes*
