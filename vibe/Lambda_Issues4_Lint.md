# Lambda Static Analysis Report

**Tools**: cppcheck 2.18.0 · clang-tidy 21.1.0 (LLVM)
**Date**: 2026-03-08
**Scope**: `./lambda/` (all `.cpp`, `.c`, `.h`, `.hpp` files including subdirectories)
**Raw output**: `./temp/cppcheck_lambda.txt` · `./temp/clang_tidy_lambda.txt`

---

## Setup & Reproducing This Report

### Prerequisites

Install the required tools using Homebrew:

```bash
brew install cppcheck
brew install llvm          # provides clang-tidy 21.x at /opt/homebrew/opt/llvm/bin/
brew install bear          # intercepts build commands to generate compile_commands.json
```

> **Note on OCLint**: OCLint 24.11 (cask) is broken on macOS with LLVM 21 — its bundled
> `libc++.1.dylib` and `libunwind.1.dylib` have hardcoded `@rpath` entries pointing to the
> original builder's machine, and the installed LLVM 21.1.0 has ABI symbol incompatibilities
> (`__ZnamSt19__type_descriptor_t` missing). `cppcheck` + `clang-tidy` cover the same checks.

### Step 1 — Run cppcheck

```bash
cppcheck --enable=all --std=c++17 --language=c++ \
  --suppress=missingInclude \
  ./lambda/ \
  2> ./temp/cppcheck_lambda.txt
```

Parse the results:

```bash
python3 ./temp/parse_cppcheck.py
```

### Step 2 — Generate `compile_commands.json` with bear

`clang-tidy` requires a compilation database. Regenerate it whenever source files are added or
build flags change:

```bash
make clean-all
bear -- make build
# produces ./compile_commands.json (~300 entries)
```

### Step 3 — Run clang-tidy

The config is at `./temp/.clang-tidy` (YAML). Run the parallel driver:

```bash
python3 ./temp/run_clang_tidy.py
# produces ./temp/clang_tidy_lambda.txt (~6 MB)
```

The driver uses `ThreadPoolExecutor` with 8 workers and calls:

```bash
/opt/homebrew/opt/llvm/bin/clang-tidy \
  --config-file=./temp/.clang-tidy \
  -p ./compile_commands.json \
  <file.cpp>
```

Checks enabled (see `./temp/.clang-tidy`):

| Check group | Notes |
|-------------|-------|
| `bugprone-*` | All bugprone checks |
| `readability-function-cognitive-complexity` | Threshold: 20 |
| `readability-function-size` | >150 lines · >8 params · >100 statements |
| `misc-unused-parameters` | Unused function params |
| `performance-unnecessary-value-param` | Value params that should be const-ref |
| `modernize-use-nullptr` | `NULL`/`0` → `nullptr` |
| `clang-analyzer-*` | All static analyzer checkers (path-sensitive) |

### Step 4 — Parse clang-tidy results

```bash
python3 ./temp/parse_clang_tidy.py     # summary by check type and file
python3 ./temp/get_analyzer.py         # detailed clang-analyzer findings only
```

---

## Summary by Issue Type

| Count | Tag | Severity | Description |
|------:|-----|----------|-------------|
| 1340 | `cstyleCast` | style | C-style pointer casts |
| 736 | `dangerousTypeCast` | warning | C-style cast that may be an invalid type conversion |
| 266 | `constVariablePointer` | style | Pointer variable could be declared `const` |
| 90 | `unreadVariable` | style | Variable assigned but its value never read (dead assignment) |
| 88 | `passedByValue` | performance | `Item` struct passed by value instead of const reference |
| 87 | `constParameterPointer` | style | Pointer parameter could be `const` |
| 53 | `nullPointerOutOfMemory` | warning | `malloc` result used without NULL check |
| 43 | `knownPointerToBool` | style | Redundant pointer-to-bool conversion |
| 37 | `shadowVariable` | style | Local variable shadows outer variable |
| 36 | `knownConditionTrueFalse` | style | Condition is always true or always false |
| 33 | `variableScope` | style | Variable declared in broader scope than needed |
| 32 | `shadowFunction` | style | Local variable name shadows a function name |
| 24 | `unusedStructMember` | style | Struct field is never read |
| 13 | `allocaCalled` | warning | `alloca()` used — stack overflow risk with large inputs |
| 9 | `noExplicitConstructor` | style | Single-arg constructor should be `explicit` |
| 5 | `internalAstError` | error | cppcheck internal parse error (not a code bug) |
| 3 | `redundantCondition` | style | Condition is redundant given another check |
| 3 | `redundantAssignment` | style | Assignment overwritten before value is used |
| 1 | `nullPointerArithmeticOutOfMemory` | **error** | Pointer arithmetic on a potentially NULL pointer |

---

## Top Files by Issue Count

| Issues | File |
|-------:|------|
| 506 | `lambda/build_ast.cpp` |
| 439 | `lambda/js/transpile_js_mir.cpp` |
| 171 | `lambda/lambda-data.cpp` |
| 115 | `lambda/lambda-data-runtime.cpp` |
| 98 | `lambda/js/build_js_ast.cpp` |
| 77 | `lambda/input/html5/html5_tree_builder.cpp` |
| 75 | `lambda/js/js_runtime.cpp` |
| 72 | `lambda/input/html5/html5_parser.cpp` |
| 69 | `lambda/input/css/dom_element.cpp` |
| 62 | `lambda/input/markup/inline/inline_spans.cpp` |
| 61 | `lambda/input/css/css_value_parser.cpp` |
| 58 | `lambda/input/css/css_parser.cpp` |
| 56 | `lambda/input/input.cpp` |
| 49 | `lambda/input/markup/block/block_list.cpp` |
| 45 | `lambda/input/css/css_style_node.cpp` |
| 44 | `lambda/input/input-latex-ts.cpp` |
| 42 | `lambda/lambda-decimal.cpp` |

---

## Real Errors

### E1 — NULL pointer arithmetic after `malloc` in `build_ast.cpp:1912`

**File**: `lambda/build_ast.cpp:1907–1912`
**Tag**: `nullPointerArithmeticOutOfMemory`
**Severity**: error

```cpp
char* hex_str = (char*)malloc(hex_len + 1);
memcpy(hex_str, hex_start, hex_len);       // L1908 — NULL deref if malloc fails
hex_str[hex_len] = '\0';                   // L1909 — NULL deref
uint32_t code_point = strtoul(hex_str, &endptr, 16);  // L1911 — NULL deref
if (endptr == hex_str + hex_len && ...)    // L1912 — pointer arithmetic on NULL
```

`hex_str` is never checked for NULL before use. The same pattern repeats at `L2229–2235` and `L6609–6614` for `num_str`.

**Fix**: Add a NULL check or replace with `pool_alloc()` (which never returns NULL in Lambda's design).

### E2–E6 — cppcheck Internal Parse Errors (not real code bugs)

These are cppcheck's own AST parser failing on complex ternary expressions. The code itself is valid C++.

| File | Line | Issue |
|------|------|-------|
| `lambda/input/input-csv.cpp` | 140 | cppcheck: ternary lacks `:` in its AST |
| `lambda/input/input-mark.cpp` | 191 | cppcheck: ternary lacks `:` in its AST |
| `lambda/input/input-pdf.cpp` | 347 | cppcheck: ternary lacks `:` in its AST |
| `lambda/input/input-toml.cpp` | 767 | cppcheck: ternary lacks `:` in its AST |
| `lambda/js/js_dom.cpp` | 894 | cppcheck: ternary lacks `:` in its AST |

---

## Warnings

### W1 — Unchecked `malloc` returns (53 occurrences)

`malloc()` is called and the result used immediately without a NULL check. Listed by unique allocation site (each site may have multiple flagged lines):

| File | Lines | Variable |
|------|-------|----------|
| `lambda/build_ast.cpp` | 1907–1912 | `hex_str` |
| `lambda/build_ast.cpp` | 2229–2235 | `num_str` |
| `lambda/build_ast.cpp` | 6609–6614 | `num_str` |
| `lambda/input/input-utils.cpp` | 232–233 | `result` |
| `lambda/input/input-yaml.cpp` | 323–324 | `tmp` |
| `lambda/input/input_file_cache.cpp` | 42–45 | `mgr` |
| `lambda/input/input_pool.cpp` | 37–41 | `mgr` |
| `lambda/input/markup/block/block_list.cpp` | 706, 1132 | `lines_array` |
| `lambda/input/markup/block/block_quote.cpp` | 388, 596–597 | `lines_array`, `lazy_array` |
| `lambda/js/js_scope.cpp` | 15, 169–191 | `tp` |
| `lambda/js/js_typed_array.cpp` | 52–55 | `ta` |

**Note**: Many of these `malloc` calls are in code paths where Lambda's arena/pool allocators would be more appropriate. For the `input_pool` and `input_file_cache` initialization code, adding a NULL check with a `log_error()` + early return would suffice.

### W2 — Dangerous C-style Casts (736 occurrences)

Widespread across `lambda/build_ast.cpp`, `lambda/lambda.hpp`, `lambda/lambda-data.cpp`, and the JS subsystem. cppcheck flags these as potentially invalid type conversions — e.g. casting between unrelated struct pointer types without the inheritance relationship being visible to cppcheck.

Most are intentional in the C+ convention (casting `pool_alloc` void* results, downcasting `AstNode*` subtypes). **Not bugs**, but the high count reflects the C+/C++ hybrid approach across the codebase.

### W3 — `alloca()` calls (13 occurrences, all in `js/transpile_js_mir.cpp`)

| Lines | Context |
|-------|---------|
| 4060, 4070, 4106, 4115 | MIR function call argument stack setup |
| 6401, 6402, 6405 | MIR register alloca |
| 6556, 6557, 6561, 6569 | MIR stack frame |
| 6611, 6620 | MIR stack frame |

`alloca()` allocates on the stack without bounds checks. For JIT codegen with variable-size inputs (e.g. many function arguments), this is a potential stack overflow. Consider capping the input size or using heap allocation with a fixed-size stack fallback.

---

## Style Issues

### S1 — Unread Variables (90 occurrences)

Variables assigned but their value never subsequently read. These are dead assignments — likely leftover from refactoring. Spread across many files; key examples:

| File | Line | Variable | Notes |
|------|------|----------|-------|
| `lambda/build_ast.cpp` | 3918 | `prev_declare` | Dead assignment in declaration handling |
| `lambda/format/format-math-ascii.cpp` | 326, 333 | `base_is_bigop` | Set but never read |
| `lambda/format/format-math-ascii.cpp` | 759 | `chars_before_subsup` | Set but never read |
| `lambda/input/css/css_parser.cpp` | 1543, 1546 | `i` | Loop variable written then replaced |
| `lambda/input/css/css_parser.cpp` | 1862 | `brace_start` | Set but never read |
| `lambda/input/html5/html5_parser.cpp` | 1298 | `node` | Result assigned but discarded |
| `lambda/input/html5/html5_tokenizer.cpp` | 744 | `save_pos` | Save point never used |
| `lambda/input/input-graph-d2.cpp` | 396–415 | `parsed` | Returned value consistently ignored (5 sites) |
| `lambda/input/input-graph-mermaid.cpp` | 409 | `c1` | Char decoded but never used |
| `lambda/input/input-xml.cpp` | 705, 729 | `actual_root_element` | Computed but discarded |
| `lambda/input/input-yaml.cpp` | 1529, 1840 | `ek_anchor` | YAML anchor key unused |
| `lambda/input/input-yaml.cpp` | 1680, 2374, 2379 | `tag` | Tag extracted but never applied |
| `lambda/input/input-yaml.cpp` | 2100, 2104 | `expk` | Expression key unused |
| `lambda/input/input-yaml.cpp` | 2364, 2366 | `had_start` | Flag tracked but never checked |
| `lambda/lambda-data.cpp` | 470–585 | `str`, `sym`, `container`, `fn` | Debug/inspect variables (multiple sites) |
| `lambda/lambda-error.cpp` | 563 | `is_system_entry` | Flag set but not used |
| `lambda/lambda-error.cpp` | 637, 758, 876, 913 | `pos` | Position computed but discarded |
| `lambda/js/js_runtime.cpp` | 583 | `m` | Match result ignored |

### S2 — `passedByValue` for `Item` type (88 occurrences, all in `lambda/format/`)

cppcheck suggests passing `root_item` / `item` parameters as `const Item&`. In Lambda, `Item` is a 64-bit value (a tagged pointer/scalar), so passing by value is equivalent — this is a false positive for the Lambda coding convention.

Affected files: all formatters in `lambda/format/` (`format-css.cpp`, `format-html.cpp`, `format-json.cpp`, `format-markup.cpp`, `format-math.cpp`, `format-md.cpp`, etc.).

### S3 — Always-True/False Conditions (36 occurrences)

Known conditions that are always true or false suggest redundant guards or logical errors. Key examples:

| File | Line | Condition | Issue |
|------|------|-----------|-------|
| `lambda/build_ast.cpp` | 1039 | `q_entry->import` | Always true in this branch |
| `lambda/build_ast.cpp` | 3260 | `inner` | Always true after prior null check |
| `lambda/format/format-markup.cpp` | 492 | `i>0`, `i<len` | Loop invariants that can't be false at that point |
| `lambda/input/css/css_formatter.cpp` | 441 | `j<=selector->compound_selector_count` | Always true |
| `lambda/input/css/css_formatter.cpp` | 785 | `rule` | Always true after assignment |
| `lambda/input/css/css_parser.cpp` | 1602 | `decl->value` | Always true given caller guarantees |

### S4 — Shadow Variables and Functions (37 + 32 = 69 occurrences)

Local variable names that shadow outer variables or function names. Most are minor naming collisions. Notable patterns:

- `lambda/build_ast.cpp`: `fn_type`, `fn_name`, `list`, `map` shadow global function names (10 sites)
- `lambda/input/input-latex-ts.cpp`: `start`, `end`, `len` shadows in adjacent blocks (many repeated sites)
- `lambda/input/css/dom_node.cpp`: `element`, `next`, `node_name` shadow outer scope
- `lambda/input/html5/html5_tree_builder.cpp` L1902: `current` shadows outer variable

### S5 — Unused Struct Members (24 occurrences)

| File | Struct | Members |
|------|--------|---------|
| `lambda/input/css/css_style_node.cpp` | `CollectContext` | `nodes`, `count` |

Most unused struct members are in internal context structs that may be partially implemented or were used in an earlier design.

---

## Notes on False Positives (cppcheck)

The following categories are largely **expected given the C+ coding convention** and do not represent real issues:

- **`cstyleCast` (1340)** — The C+ convention intentionally uses C-style casts for `void*` results from pool/arena allocators and for downcasting `AstNode*` subtypes. Switching to `static_cast<>` everywhere would improve clarity but is a style choice, not a safety issue in this controlled context.
- **`constVariablePointer` / `constParameterPointer` (353)** — Const-correctness improvements. Low risk.
- **`passedByValue` for `Item` (88)** — `Item` is a 64-bit register-sized value. Pass-by-value is optimal.
- **`noExplicitConstructor` (9)** — Implicit constructors in internal types unlikely to cause issues.

---

---

# clang-tidy Analysis (LLVM 21.1.0)

**Tool**: clang-tidy 21.1.0 via LLVM (`/opt/homebrew/opt/llvm/bin/clang-tidy`)
**Compilation DB**: generated with `bear -- make build`
**Files processed**: 159 `.cpp` files in `lambda/` (excluding tree-sitter parsers)
**Checks enabled**: `bugprone-*`, `readability-function-cognitive-complexity`, `readability-function-size`, `misc-unused-parameters`, `performance-unnecessary-value-param`, `modernize-use-nullptr`, `clang-analyzer-*`

## Summary by Check

| Count | Check | Description |
|------:|-------|-------------|
| 1716 | `modernize-use-nullptr` | `0` or `NULL` used instead of `nullptr` |
| 495 | `readability-function-cognitive-complexity` | Functions exceeding cognitive complexity threshold (20) |
| 326 | `bugprone-narrowing-conversions` | Implicit narrowing (e.g. `size_t` → `int`, `int64_t` → `double`) |
| 207 | `bugprone-easily-swappable-parameters` | Adjacent same-typed parameters, easy to pass in wrong order |
| 181 | `readability-function-size` | Functions exceeding line/statement count thresholds |
| 144 | `bugprone-reserved-identifier` | Identifiers starting with `_` (e.g. `_ArrayList`) — reserved by C++ standard |
| 115 | `clang-analyzer-deadcode.DeadStores` | Values assigned but never read (dead stores) |
| 102 | `bugprone-macro-parentheses` | Macro arguments/replacements missing parentheses |
| 95 | `bugprone-branch-clone` | Switch/conditional branches with identical code |
| 63 | `misc-unused-parameters` | Function parameters never used |
| 28 | `bugprone-multi-level-implicit-pointer-conversion` | Multi-level pointer implicit conversion to `void*` |
| 23 | `bugprone-implicit-widening-of-multiplication-result` | `int * int` used as pointer offset — implicit widening |
| 20 | `clang-analyzer-security.ArrayBound` | Out-of-bounds memory access |
| 17 | `clang-analyzer-core.NullDereference` | Path-sensitive null pointer dereference |
| 11 | `clang-analyzer-optin.core.EnumCastOutOfRange` | Value cast to enum is outside valid enum range |
| 10 | `clang-analyzer-unix.cstring.NullArg` | Null pointer passed to string/memory function |
| 7 | `bugprone-bitwise-pointer-cast` | `memcpy` used to type-pun between pointers |
| 6 | `bugprone-assignment-in-if-condition` | Assignment inside `if` condition |
| 5 | `bugprone-casting-through-void` | Pointer downcast through `void*` (use `reinterpret_cast` instead) |
| 5 | `clang-analyzer-optin.portability.UnixAPI` | `malloc`/`calloc`/`alloca` called with size 0 |
| 5 | `bugprone-switch-missing-default-case` | `switch` on non-enum value lacks `default:` |
| 2 | `bugprone-not-null-terminated-result` | `memcpy` result not null-terminated |
| 2 | `bugprone-signed-char-misuse` | `signed char` used in integer context |
| 2 | `bugprone-misplaced-widening-cast` | Cast after multiplication is too late to prevent overflow |
| 1 | `clang-analyzer-core.CallAndMessage` | Called C++ object pointer is null |
| 1 | `clang-analyzer-optin.performance.Padding` | Struct has excessive padding bytes |
| 1 | `clang-analyzer-security.insecureAPI.rand` | `rand()` used — non-cryptographic, poor distribution |
| 1 | `clang-analyzer-unix.Malloc` | Potential memory leak |
| 1 | `bugprone-suspicious-realloc-usage` | `realloc` result stored back into original pointer (leak on failure) |

**Total unique findings**: 3,596

## Top Files by Issue Count

| Issues | File |
|-------:|------|
| 326 | `lambda/input/css/css_properties.cpp` |
| 173 | `lambda/transpile.cpp` |
| 169 | `lambda/js/transpile_js_mir.cpp` |
| 158 | `lambda/lambda-eval.cpp` |
| 155 | `lambda/transpile-mir.cpp` |
| 114 | `lambda/input/css/css_tokenizer.cpp` |
| 105 | `lambda/input/css/css_value_parser.cpp` |
| 101 | `lambda/format/format-utils.cpp` |
| 100 | `lambda/input/css/css_parser.cpp` |
| 75 | `lambda/input/input-toml.cpp` |
| 72 | `lambda/input/css/dom_element.cpp` |
| 69 | `lambda/lambda-eval-num.cpp` |
| 67 | `lambda/lambda-vector.cpp` |
| 64 | `lambda/input/input-yaml.cpp` |

---

## Security & Correctness — clang-analyzer

### A1 — Out-of-Bounds Memory Access (20 occurrences) `[clang-analyzer-security.ArrayBound]`

Path-sensitive analysis found concrete out-of-bounds access paths:

| File | Line | Issue |
|------|------|-------|
| `lambda/format/format-markup.cpp` | 495 | Out of bound access to memory after the end of the string literal |
| `lambda/input/input_http.cpp` | 236 | Potential out of bound access to heap area with tainted (external) index |
| `lambda/input/input.cpp` | 635 | Potential out of bound access to heap area with tainted index |
| `lambda/input/markup/markup_parser.cpp` | 338 | Out of bound access to memory after the end of the heap area |
| `lambda/network/resource_loaders.cpp` | 52 | Potential out of bound access with tainted index |
| `lambda/input/pdf_decompress.cpp` | 375 | Out of bound access after end of heap area |
| `lambda/lambda-vector.cpp` | 1521–1522 | Out of bound access after end of heap area (2 adjacent lines) |
| `lambda/transpile-mir.cpp` | 4033, 4039 | Out of bound access after memory from `alloca` |
| + 10 more | — | — |

The `tainted index` findings (`input_http.cpp`, `input.cpp`, `resource_loaders.cpp`) are particularly important — they indicate paths where an externally-provided value (e.g. from HTTP response or file input) influences an array index without bounds validation.

### A2 — Null Pointer Dereference (17 occurrences) `[clang-analyzer-core.NullDereference]`

Path-sensitive null dereferences in the transpiler and printer:

| File | Line | Issue |
|------|------|-------|
| `lambda/print.cpp` | 1063 | Access to field `is_const` — `type` is null |
| `lambda/print.cpp` | 1102 | Access to field `type_id` — `type` is null |
| `lambda/transpile-call.cpp` | 139 | Access to field `type_id` — `type` is null |
| `lambda/transpile-call.cpp` | 645 | Access to field `next` — `second_arg` is null |
| `lambda/transpile.cpp` | 2760 | Access to `fn_info->is_proc` — `fn_info` is null |
| `lambda/transpile.cpp` | 6125 | Access to `fn_type->returned` — `fn_type` is null |
| `lambda/transpile.cpp` | 6855–6860 | Access to `->type` twice — field `type` is null (2 sites) |
| `lambda/transpile.cpp` | 6998 | Access to `array_type->type` — `array_type` is null |
| `lambda/transpile.cpp` | 7004 | Access to `map_type->type` — `map_type` is null |
| + 7 more | — | — |

### A3 — Null Pointer Passed to String/Memory Functions (10 occurrences) `[clang-analyzer-unix.cstring.NullArg]`

| File | Lines | Issue |
|------|-------|-------|
| `lambda/input/css/css_parser.cpp` | 25, 974, 1008, 1058, 1244 | Null passed as 1st arg to string comparison (`strcmp`/`strncmp`) |
| `lambda/input/input_sysinfo.cpp` | 398 | Null passed as 1st arg to string comparison |
| `lambda/lambda-mem.cpp` | 158 | Null passed as 1st arg to `memcpy` |
| `lambda/mark_editor.cpp` | 476, 573, 1001 | Null passed as 1st arg to `memcpy` (3 sites) |

### A4 — Zero-Size Allocations (5 occurrences) `[clang-analyzer-optin.portability.UnixAPI]`

| File | Line | Issue |
|------|------|-------|
| `lambda/input/input_http.cpp` | 233 | `malloc(0)` — unspecified behavior (may return NULL or non-NULL non-dereferenceable) |
| `lambda/js/js_typed_array.cpp` | 55 | `calloc` with size 0 |
| `lambda/network/resource_loaders.cpp` | 38 | `malloc(0)` |
| `lambda/js/transpile_js_mir.cpp` | 6556 | `alloca(0)` |
| `lambda/transpile-mir.cpp` | 4125 | `alloca(0)` |

### A5 — Enum Cast Out of Range (11 occurrences) `[clang-analyzer-optin.core.EnumCastOutOfRange]`

| File | Lines | Issue |
|------|-------|-------|
| `lambda/input/css/css_properties.cpp` | 693, 856, 871, 895, 904 | Value `0` cast to `CssPropertyId` — enum has no zero-value enumerator |
| `lambda/lambda-eval.cpp` | 377 | Value `10` cast to enum outside valid range |
| `lambda/utf_string.cpp` | 59, 82, 105, 264 | Values 10/18/1034/14 cast to enum — out of valid range |

The `CssPropertyId` case is the most significant — using `0` as a sentinel when the enum doesn't include `0` will silently produce an invalid enum value.

### A6 — Other Analyzer Findings

| Check | File | Line | Issue |
|-------|------|------|-------|
| `clang-analyzer-core.CallAndMessage` | `lambda/validator/ast_validate.cpp` | 54 | Called C++ object pointer is null |
| `clang-analyzer-unix.Malloc` | `lambda/validator/doc_validator.cpp` | 544 | Potential memory leak: `error` pointer |
| `clang-analyzer-security.insecureAPI.rand` | `lambda/js/js_runtime.cpp` | 1497 | `rand()` is non-cryptographic; use `arc4random` |
| `clang-analyzer-optin.performance.Padding` | `lambda/input/css/css_style.hpp` | 1014 | `CssProperty` struct: 14 padding bytes (6 optimal) — reorder fields |

---

## Complexity Analysis

### Top Functions by Cognitive Complexity (threshold: 20)

495 functions exceed the threshold. Highest CC values — these are the hardest to maintain and test:

| CC | File | Line | Function |
|---:|------|------|----------|
| 774 | `lambda/input/markup/inline/inline_spans.cpp` | 44 | `parse_inline_spans()` |
| 653 | `lambda/input/input-latex-ts.cpp` | 1538 | `convert_latex_node()` |
| 575 | `lambda/input/html5/html5_tokenizer.cpp` | 783 | `html5_tokenize_next()` |
| 559 | `lambda/transpile-mir.cpp` | 5356 | `transpile_call()` |
| 529 | `lambda/main.cpp` | 765 | `main()` |
| 516 | `lambda/input/input-latex-ts.cpp` | 212 | `convert_math_node()` |
| 431 | `lambda/input/css/css_parser.cpp` | 808 | `css_parse_simple_selector_from_tokens()` |
| 424 | `lambda/input/markup/block/block_list.cpp` | 811 | `parse_list_structure()` |
| 385 | `lambda/input/input-yaml.cpp` | 1954 | `parse_inline_block_node()` |
| 369 | `lambda/js/js_runtime.cpp` | 1063 | `js_array_method()` |
| 368 | `lambda/lambda-eval.cpp` | 2331 | `fn_member()` |
| 358 | `lambda/js/transpile_js_mir.cpp` | 3603 | `jm_transpile_call()` |
| 305 | `lambda/transpile-call.cpp` | 417 | `transpile_call_expr()` |
| 285 | `lambda/input/css/dom_element.cpp` | 1171 | `dom_element_get_pseudo_element_content_with_counters()` |
| 272 | `lambda/input/css/css_parser.cpp` | 1688 | `css_parse_rule_from_tokens_internal()` |
| 256 | `lambda/input/input-pdf.cpp` | 973 | `parse_pdf()` |
| 220 | `lambda/js/js_dom.cpp` | 880 | `js_dom_get_property()` |
| 220 | `lambda/transpile-mir.cpp` | 6929 | `transpile_expr()` |
| 214 | `lambda/input/input-yaml.cpp` | 892 | `parse_block_scalar()` |
| 211 | `lambda/transpile-mir.cpp` | 3194 | `transpile_let_stam()` |
| 204 | `lambda/input/markup/block/block_detection.cpp` | 158 | `detect_block_type()` |
| 199 | `lambda/input/input-pdf.cpp` | 679 | `parse_pdf_xref_table()` |
| 187 | `lambda/transpile-mir.cpp` | 8916 | `prepass_forward_declare()` |
| 185 | `lambda/transpile-mir.cpp` | 9688 | `run_script_mir()` |
| 176 | `lambda/input/css/dom_node.cpp` | 493 | `print()` |
| 176 | `lambda/input/markup/markup_parser.cpp` | 161 | `parseContent()` |
| 176 | `lambda/transpile-mir.cpp` | 1689 | `get_effective_type()` |
| 175 | `lambda/input/input-xml.cpp` | 452 | `parse_element()` |
| 174 | `lambda/input/markup/block/block_paragraph.cpp` | 178 | `parse_paragraph()` |
| 172 | `lambda/input/css/css_tokenizer.cpp` | 717 | `css_tokenizer_tokenize()` |
| 163 | `lambda/transpile-mir.cpp` | 1914 | `transpile_binary()` |
| 161 | `lambda/transpile-mir.cpp` | 4458 | `transpile_index()` |
| 159 | `lambda/input/input-yaml.cpp` | 1455 | `parse_block_mapping()` |
| 159 | `lambda/transpile.cpp` | 1209 | `transpile_primary_expr()` |
| 151 | `lambda/js/js_dom.cpp` | 1513 | `js_dom_element_method()` |
| 148 | `lambda/print.cpp` | 431 | `print_item()` |
| 148 | `lambda/transpile.cpp` | 6200 | `define_func()` |

**Note**: These complexity scores are not necessarily bugs, but functions above CC=100 are extremely difficult to understand, test, and modify safely. `parse_inline_spans()` at CC=774 is the single highest risk — any bug in this function affects all inline markup rendering.

---

## Bugprone Highlights

### B1 — `realloc` result assigned back to original pointer (1 occurrence)

`lambda/input/input_http.cpp:367` — `response->response_headers` is passed directly to `realloc`. If `realloc` fails and returns NULL, the original buffer pointer is lost (memory leak) and a NULL value is stored into the struct.

**Fix**: Assign to a temporary, check for NULL, then replace:
```c
char* tmp = (char*)realloc(response->response_headers, new_size);
if (!tmp) { /* handle error */ return; }
response->response_headers = tmp;
```

### B2 — `memcpy` used to type-pun between pointers (7 occurrences) `[bugprone-bitwise-pointer-cast]`

Using `memcpy` to reinterpret bit patterns between unrelated pointer types is undefined behavior in C++. Flagged in:
- `lambda/input/css/css_engine.cpp:230`
- `lambda/input/css/css_parser.cpp:598, 712, 785, 1789` (4 sites)
- + 2 more

**Fix**: Use `memcpy` to a local variable of the target type, or use `__builtin_bit_cast` (C++20).

### B3 — `memcpy` result not null-terminated (2 occurrences) `[bugprone-not-null-terminated-result]`

`lambda/input/html5/html5_tokenizer.cpp:736, 770` — `memcpy` copies bytes but the destination buffer is subsequently treated as a C string. If the source has no null terminator within range, this will read past the buffer.

### B4 — Assignment inside `if` condition (6 occurrences) `[bugprone-assignment-in-if-condition]`

`lambda/format/format-math-ascii.cpp:125–130` — Five consecutive `if (x = ...)` assignments. While this can be intentional, it's easy to misread and accidentally introduce bugs during modification.

### B5 — Narrowing Conversions (326 occurrences) `[bugprone-narrowing-conversions]`

326 implicit narrowing conversions throughout the codebase. The highest-risk categories:
- **`size_t` → `int`** (many instances in markup, CSS, YAML parsers): If a container has more than `INT_MAX` elements, this silently truncates.
- **`int64_t` → `double`** (CSS formatter): Loses precision for large integer values.
- **`int` → `char`** (`block_html.cpp:295`): Only safe for ASCII range.

### B6 — `rand()` usage (1 occurrence) `[clang-analyzer-security.insecureAPI.rand]`

`lambda/js/js_runtime.cpp:1497` — `Math.random()` implementation uses `rand()`. For a scripting language, this should use a better pseudo-random generator (e.g. `arc4random_uniform` on macOS/BSD, or a seeded Xorshift).

### B7 — Reserved Identifiers in `lib/` headers (144 occurrences) `[bugprone-reserved-identifier]`

`lib/arraylist.h` uses `_ArrayList` (leading underscore + uppercase = reserved in all scopes by C++ standard). Technically UB but harmless in practice on all targeted compilers. Consider renaming `_ArrayList` → `ArrayList_` or `ArrayList_s`.

---

## Design Issues

### D1 — `CssProperty` struct excessive padding (14 bytes wasted)

`lambda/input/css/css_style.hpp:1014` — `struct CssProperty` has 14 bytes of padding where 6 is optimal. Reordering fields to group by alignment (`name`, `initial_value`, `longhand_props`, `validate_value`, `compute_value`, then smaller fields) would save ~8 bytes per instance. Given that `CssProperty` is used in large arrays/tables, this has a measurable memory impact.

### D2 — `CssPropertyId` enum missing zero value

`lambda/input/css/css_properties.cpp` — `CssPropertyId` is used with `0` as a sentinel value (`CSS_PROPERTY_NONE` or similar) in 5 places, but the enum itself doesn't define a `0` enumerator. This causes the `clang-analyzer-optin.core.EnumCastOutOfRange` findings. Adding a `CSS_PROPERTY_NONE = 0` enumerator would resolve all 5 instances.

### D3 — `main()` has CC=529

`lambda/main.cpp:765` — The `main()` function itself has cognitive complexity 529. All CLI parsing, dispatch, and mode selection is handled in a single function. This should be broken down into sub-functions per command (`run_script`, `run_repl`, `run_convert`, etc.).

---

## Notes on False Positives (clang-tidy)

- **`modernize-use-nullptr` (1716)** — The codebase intentionally uses `{0}` and `NULL` for C-compatible initialization. Low priority for a C+ codebase but easy to fix with a sed one-liner if desired.
- **`bugprone-easily-swappable-parameters` (207)** — Many are in internal APIs where parameter order is well-established by convention (e.g. `(Pool* pool, TypeId type, size_t size)`). Worth reviewing only for public-facing APIs.
- **`bugprone-reserved-identifier` (144)** — All originate from `lib/arraylist.h` (`_ArrayList`). One fix propagates everywhere.
- **`misc-unused-parameters` (63)** — Some are callback signatures that must match a fixed prototype.
