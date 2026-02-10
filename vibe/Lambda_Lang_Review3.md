# Lambda Language Review — Hands-On Experience Report

> **Author**: Claude Opus 4 (GitHub Copilot)
> **Context**: Building `generate_premake_v2.ls` (~1150 lines), a Lambda Script premake5 generator that produces Lua files byte-identical to a Python-generated reference (`premake5.mac.lua`, 10,258 lines).
> **Date**: February 2026

---

## What Works Well

### `fn` Statement-Syntax for String Building
The `fn` statement-syntax for string building (`fn f() { "text"; expr; "text" }`) is genuinely elegant for templating. Each statement contributes a fragment, and they're implicitly concatenated. Once I got the hang of it, it felt more natural than string concatenation or template literals — you just write the output shape directly. This was the primary pattern for all ~30 formatting functions in the premake generator.

Lambda offers three `fn` syntax forms, each suited to different complexity levels:
- `fn f(x) = expr` — single expression (concise one-liners)
- `fn f(x) => expr` — expression form with multi-statement support via tuple syntax `(stmt1, stmt2)`
- `fn f(x) { ... }` — statement form with full block syntax including multi-statement `if/else`

This means multi-statement branches in `fn` are well-supported — no need to extract helper functions for 2-3 line branches.

### Pipe Operator & Functional Composition
The pipe `|>` and data processing chains feel natural and expressive. The `out |> path` pattern for writing output to a file is clean and intuitive. Transforming data through `map`, `where`, `reduce`, `sort`, `join` pipelines reads very cleanly — it's one of the language's strongest features.

### `fn` / `pn` Dual Function Model
The separation between pure functions (`fn`) and procedural functions (`pn`) is a genuinely good design decision. It makes intent clear at the declaration site: `fn` for string formatting and data transformation, `pn` for stateful computation and I/O. The variable binding model is clean: `let` is always init-only (immutable binding), `var` is for mutable variables (reassignment). This makes mutation explicit and intentional. Procedural `pn` functions also support `continue` and `break` in loops, enabling clean early-exit patterns. The premake generator naturally fell into a two-layer architecture:
- **`fn` layer** — ~30 pure functions generating Lua code strings (using `let`)
- **`pn` layer** — ~20 procedural functions computing data structures (using `var` for mutation)

### `input()` for Structured Data
Being able to `input("file.json", "json")` to parse files directly into Lambda data structures is very convenient. It made reading the build config trivial.

### Type Checking with `is`
The `value is Type` operator for type-based branching integrates naturally with `if/else if` chains. Combined with union types (`int | string | bool`), it provides a clean way to handle heterogeneous data without a dedicated `match` keyword.

### Error Destructuring
The error destructuring pattern (`var x^err = ...`) is a nice design. It makes error handling explicit at the call site without the verbosity of try/catch, and integrates cleanly with the `?` propagation operator. The `T^E` return type annotation makes error paths visible in function signatures.

### Implicit Last-Expression Return
In `fn` functions, the last expression being the return value keeps code terse and readable, especially for formatting functions that just compute a string.

---

## Language Design Suggestions

### 1. Null Coalescing Operator
**Suggestion**: A null coalescing operator (`??`) would be useful for providing defaults.

```lambda
// Suggested
let name = user.name ?? "anonymous"
```

Note: `null ++ "text"` was previously producing `"nulltext"`, but this has been **fixed** — it now correctly returns `"text"` (the non-null operand).

### 2. Raw Strings / Multi-line Literals
For generating code (Lua, JSON, etc.), having raw string literals that don't process escape sequences would be helpful:

```lambda
let code = r"backslash-n stays literal: \n"
// or
let code = """
    multi-line
    raw string
"""
```

---

## System Function Suggestions

### 1. Directory Listing
`input("dir", "dir")` now works correctly. Path items support `string()` conversion and metadata property access:

```lambda
// ✅ Directory listing works — returns list of Path items
var entries^err = input("some/dir", "dir")
for e in entries {
    print(e.name)         // leaf filename: "file.txt"
    print(e.path)         // OS path: "./some/dir/file.txt"
    print(e.extension)    // file extension: "txt"
    print(e.is_dir)       // boolean
    print(e.size)         // file size in bytes (int64)
    print(e.modified)     // datetime
    print(e.scheme)       // "file", "rel", etc.
    print(string(e))      // Lambda path notation
}
```

**Root cause**: `fn_string` and `fn_member` didn't handle `LMD_TYPE_PATH`. Path items were stored correctly but had no runtime accessors. Fixed by adding `case LMD_TYPE_PATH` to `fn_string` (using `path_to_string`) and extending `fn_member` with property access for `name`, `path`, `extension`, `scheme`, `depth`, `size`, `modified`, `is_dir`, `is_link`, and `mode`.

### 2. Shell Command Execution in JIT Mode
`cmd()` works in JIT mode — the initial confusion was that it requires error handling (`var result^err = cmd(...)` or `cmd(...)?`) since commands can fail. After fixing three bugs (non-zero exit code not returned as error, uninitialized variable on empty output, trailing newline not trimmed), it now works correctly:

```lambda
// cmd(command) or cmd(command, args) — returns stdout as string
var result^err = cmd("echo", "hello world")
var files^err = cmd("ls")   // 1-arg form also supported
```

The remaining gap is that `cmd()` shell-escapes its arguments, making it hard to pass flags like `-c 'script'` to `sh`. A raw-command form or passthrough mode would help.

### 3. Additional String Utilities

| Function | Description | Priority |
|----------|-------------|----------|
| `trim(str)` | Remove leading/trailing whitespace | High |
| `trim_end(str)` | Remove trailing whitespace | High |
| `pad_left(str, n, char)` | Left-pad string | Medium |
| `pad_right(str, n, char)` | Right-pad string | Medium |
| `contains(str, substr)` | Check if substring exists | High |
| `index_of(str, substr)` | Find substring position | High |
| `replace(str, old, new)` | Replace first occurrence | High |
| `replace_all(str, old, new)` | Replace all occurrences | High |

Some of these may exist but weren't discoverable through the documentation. If they exist, improving discoverability (e.g., a comprehensive string functions reference) would help.

### 4. Array/List Utilities

| Function | Description | Priority |
|----------|-------------|----------|
| `flatten(list)` | Flatten nested lists | Medium |
| `zip(list1, list2)` | Pair elements from two lists | Medium |
| `enumerate(list)` | Return (index, value) pairs | High |
| `find(list, predicate)` | Find first matching element | Medium |
| `group_by(list, key_fn)` | Group elements by key | Medium |

### 5. `arr_merge` Stability
**Problem**: `arr_merge` with nested comprehensions or complex expressions causes malloc crashes. Had to replace with iterative `arr_push` loops.

```lambda
// ❌ Crashes
let result = arr_merge(a, [for x in b: transform(x)])

// ✅ Workaround — iterative arr_push
pn arr_push(arr, item) {
    arr[len(arr)] = item
}
```

---

## Bugs Encountered (Summary)

### Fixed During This Session ✅

| Bug | Root Cause | Fix |
|-----|-----------|-----|
| `else if` parse error in `pn` | Grammar rule `if_stam` didn't allow `else if_stam` | Added `else if_stam` alternative in Tree-sitter grammar |
| `starts_with` / `ends_with` false positives in `if` conditions | Return type was `Item` (64-bit); `b2it(false)` has non-zero type-tag bits, so transpiler's C `if` condition always evaluated truthy | Changed return type from `Item` to `Bool` (`uint8_t`) |
| `split(str, " ")` returned unsplit string | `list_push` was merging adjacent string items | `disable_string_merging` flag added to `fn_split` |
| `null ++ string` → `"nulltext"` | `fn_join` didn't check for null operands | Added null checks — returns the non-null operand |
| `cmd()` not returning errors on failure | Non-zero exit code was ignored; empty output returned null; trailing newline not trimmed | Check exit code, return empty string for no output, strip trailing newlines; added 1-arg `cmd(command)` overload |
| `arr_merge` / `arr ++ arr` crash | `fn_join` only handled `List ++ List`; `Array ++ Array` and typed arrays (`ArrayInt`, `ArrayFloat`, `ArrayInt64`) were not implemented — the Array case was commented out | Implemented full array merge in `fn_join`: same-type merges use direct `memcpy`; cross-type merges convert to generic `Array` via `item_at`. Now supports all combinations: `Array`, `List`, `ArrayInt`, `ArrayInt64`, `ArrayFloat` |

After these fixes, all workaround code in the premake generator was replaced with direct calls to the fixed builtins. The script was simplified by ~60 lines while remaining byte-identical in output.

### Still Open

No remaining bugs from this session — all issues have been fixed. ✅

---

## Overall Impression

Lambda has a strong and opinionated design foundation. The functional-first approach with pipes, pattern matching, and the `fn`/`pn` split is genuinely pleasant to work with. The input/format pipeline for structured data is a standout feature — being able to read JSON, transform it functionally, and output Lua (or any format) in a single script is powerful.

The language *feels* right for its intended purpose (data processing and document transformation). During this session, several critical bugs were fixed — `else if` chains now work in `pn` functions, `split(str, " ")` works correctly, `starts_with`/`ends_with` return proper booleans in conditions, `cmd()` now properly handles errors, trailing newlines, and supports a 1-arg form, and `input("dir", "dir")` now returns usable Path items with full metadata access. These fixes eliminated ~60 lines of workaround code from the premake generator.

The remaining pain points are:

1. **Null coalescing** — a `??` operator would be a natural addition for providing defaults
2. **Standard library gaps** — `pad_left`, `replace_all`, `enumerate`, and other common utilities would reduce boilerplate

The language has excellent bones, and the bugs fixed in this session bring it significantly closer to production-ready for scripting tasks.

### Scale of the Test

The premake generator project was a meaningful stress test: ~1150 lines of Lambda producing 10,258 lines of Lua output, processing a config file with 42 external libraries, 6 test suites, and 108 test entries. Achieving byte-identical output with the Python reference validated that Lambda can handle real-world code generation tasks. After the bug fixes in this session, the script uses idiomatic Lambda throughout — `else if` chains, `starts_with`/`ends_with`/`split` builtins — with no workarounds remaining for the fixed issues.
