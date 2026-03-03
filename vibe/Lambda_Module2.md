# Lambda Module System — Enhancement Proposal (Round 2)

## Summary

This proposal covers three categories of enhancements to the Lambda module system:

1. **Script-relative import resolution** — Change relative imports to resolve from the importing script's directory, not CWD.
2. **Public type exports** — Support `pub type T = ...` to export type definitions across modules.
3. **Aliased imports** — Complete the partially-implemented alias feature (`import alias: .module`).
4. **Error destructuring at module scope** — Fix `let a^err = ...` for global declarations.
5. **Path normalization** — Normalize import paths for reliable deduplication.
6. **Pattern exports** — Support `pub string`/`pub symbol` pattern exports.
7. **Better error messages** — Improve import failure diagnostics.

---

## 1. Script-Relative Import Resolution

### 1.1 Problem

Currently, relative imports (e.g., `import .utils.math`) resolve against `runtime->current_dir`, which is hardcoded to `"./"` (CWD) in `main.cpp`:

```cpp
// main.cpp line 827
runtime.current_dir = const_cast<char*>("./");
```

In `build_ast.cpp`, the import path is constructed by prepending `current_dir`:

```cpp
strbuf_append_format(buf, "%s%.*s", tp->runtime->current_dir,
    (int)ast_node->module.length - 1, ast_node->module.str + 1);
```

This means running a script from a different directory changes which modules are found:

```bash
# Works:
cd /project && lambda script.ls

# Fails to find modules:
lambda /project/script.ls       # CWD != /project
```

Nested imports are also broken: if `A.ls` imports `B.ls`, and `B.ls` imports `C.ls`, `C.ls` is resolved relative to CWD, not relative to `B.ls`. This makes composable library directories impossible.

### 1.2 Industry Survey

Nearly every modern language resolves relative imports **relative to the importing file**, not CWD:

| Language | Mechanism | Resolution Base |
|----------|-----------|-----------------|
| **Python** | `from .module import x` | Importing file's package |
| **JavaScript/TS** | `import './foo'` | Importing file |
| **Rust** | `mod utils;` | File's position in module tree |
| **Go** | `import "./utils"` | Module root |
| **Ruby** | `require_relative './utils'` | Calling file |
| **Haskell** | Module hierarchy | Source directory roots |
| **Elixir** | `alias MyApp.Utils` | Project module namespace |
| **OCaml** | `open My_module` | Build-system source tree |
| **Lua** | `require("lib.utils")` | `package.path` (often CWD — known pain point) |
| **Dart** | `import './utils.dart'` | Importing file |
| **Zig** | `@import("utils.zig")` | Source file |
| **Scala** | `import pkg.Utils` | File-relative package |
| **Elm** | `import Utils` | `src/` root |
| **Swift** | `import MyModule` | Build-system module |

Languages that historically used CWD have moved away from it:
- **Perl 5.26** removed `.` from `@INC` (security fix)
- **PHP** shifted to `__DIR__`-relative includes as best practice
- **Shell** `source ./file.sh` is CWD-relative — widely considered a footgun

**Consensus**: Script-relative is the dominant and recommended approach because:
1. **Predictability** — Scripts work identically regardless of CWD
2. **Composability** — Moving a directory of scripts preserves their imports
3. **Security** — CWD-based resolution risks loading unintended files
4. **Nested imports** — Libraries with internal imports work correctly

### 1.3 Design

Each script carries its own directory as the base for resolving its imports.

**New field on `Script`:**
```cpp
struct Script : Input {
    const char* reference;      // path and name of the script
    const char* directory;      // directory containing this script (for relative imports)
    // ... existing fields ...
};
```

**Resolution rules:**
- For the **main script**: `directory` is extracted from the script's file path (e.g., `"/project/src/main.ls"` → `"/project/src/"`)
- For **imported modules**: `directory` is extracted from the module's resolved path
- For **REPL/stdin**: `directory` defaults to CWD (`"./"`) — preserving current behavior
- The `runtime->current_dir` field is **removed** from import resolution (retained only for REPL convenience)

**Path construction change** in `build_module_import()`:
```cpp
// Before (CWD-relative):
strbuf_append_format(buf, "%s%.*s", tp->runtime->current_dir, ...);

// After (script-relative):
const char* base_dir = tp->directory;  // importing script's directory
strbuf_append_format(buf, "%s%.*s", base_dir, ...);
```

**Main script directory extraction** (in `load_script()` or caller):
```cpp
// Extract directory from script path
const char* extract_directory(const char* path) {
    const char* last_slash = strrchr(path, '/');
    if (!last_slash) return strdup("./");
    int len = (int)(last_slash - path + 1);
    char* dir = (char*)malloc(len + 1);
    memcpy(dir, path, len);
    dir[len] = '\0';
    return dir;
}
```

### 1.4 Implementation Plan

Files to modify:

1. **`lambda/ast.hpp`**: Add `const char* directory` field to `Script` struct.
2. **`lambda/runner.cpp`** (`load_script`): After creating a new `Script`, compute `directory` from the resolved `reference` path. For the main script, extract directory from the CLI-provided path. For imported modules, extract from the resolved module path.
3. **`lambda/build_ast.cpp`** (`build_module_import`): Replace `tp->runtime->current_dir` with `tp->directory` (since `Transpiler` extends `Script`, it inherits `directory`).
4. **`lambda/main.cpp`**: For CLI-invoked scripts, normalize the script path to absolute before passing to `load_script`. For REPL mode, set the main script's `directory` to CWD.
5. **`lambda/transpiler.hpp`**: No change — `Transpiler` extends `Script`, inherits `directory`.

### 1.5 Backward Compatibility

- Scripts run from CWD with relative paths (e.g., `lambda script.ls`, `lambda test/lambda/import.ls`) continue to work identically — directory extraction yields the same effective path.
- Scripts run with absolute paths (e.g., `lambda /project/script.ls`) now correctly resolve imports.
- The only **breaking change** is if users relied on CWD-relative imports where the CWD differs from the script's directory. This is rare and is the exact bug being fixed.

### 1.6 Test Plan

- Existing import tests (`test/lambda/import*.ls`) should pass unchanged (run from project root).
- Add test: run a script via absolute path from a different CWD, verify imports resolve correctly.
- Add test: nested imports (`A imports B`, `B imports C`) where B and C are in subdirectories relative to each other, not to CWD.

---

## 2. Public Type Exports (`pub type`)

### 2.1 Problem

Currently, only `pub fn`, `pub pn`, and `pub let` declarations are exported. Type definitions (`type T = ...` and `type T { ... }`) are module-private — they cannot be shared.

This is a significant limitation. Real-world modules need to export types for:
- **Data contracts**: Module defines a `User` type, callers create/receive `User` objects
- **Validation schemas**: Module exports schema types for use in `is`/`match` checks
- **Object-oriented patterns**: Module defines an object type with methods, callers instantiate it

Currently, the workaround is to define the type in every script that needs it — violating DRY and risking inconsistency.

### 2.2 Design

#### 2.2.1 Syntax

```lambda
// Type alias export
pub type UserId = int;
pub type Result = int | error;
pub type Point = (float, float);
pub type Color = 'red' | 'green' | 'blue';

// Object type export (already uses `type` keyword, add `pub` modifier)
pub type Counter {
    value: int = 0;
    fn double() => value * 2
    pn inc() { value = value + 1 }
}

pub type Circle : Shape { radius: float; }
```

#### 2.2.2 Grammar Changes

Currently `pub_stam` only wraps `assign_expr`:

```javascript
pub_stam: $ => seq(
    'pub', field('declare', $.assign_expr),
    repeat(seq(',', field('declare', $.assign_expr)))
),
```

For `pub type T = ...` (type alias), this already works through `pub_stam` — `pub type T = int | string` parses as `pub` + `type_assign`.

**Wait** — actually `type_stam` uses `type_assign` which is aliased as `assign_expr`. Let me re-examine. The grammar currently has:

```javascript
type_stam: $ => seq(
    'type', field('declare', alias($.type_assign, $.assign_expr)), ...
),
```

So `pub type T = int` would need the grammar to recognize `pub type` as a valid production. Two options:

**Option A**: Extend `pub_stam` to accept `type_stam`-like content:
```javascript
pub_stam: $ => choice(
    // existing: pub x = expr, pub y = expr
    seq('pub', field('declare', $.assign_expr),
        repeat(seq(',', field('declare', $.assign_expr)))),
    // new: pub type T = type_expr
    seq('pub', $.type_stam),
    // new: pub type T { ... } (object type)
    seq('pub', $.object_type),
),
```

**Option B**: Add a `pub` flag to `type_stam` and `object_type` directly:
```javascript
type_stam: $ => seq(
    optional('pub'), 'type', field('declare', alias($.type_assign, $.assign_expr)), ...
),
object_type: $ => seq(
    optional('pub'), 'type', field('name', $.identifier), ...
),
```

**Recommendation**: **Option A** — it keeps `pub` as a uniform modifier prefix and doesn't require changing the `type_stam`/`object_type` rules themselves. The AST builder already maps `SYM_PUB_STAM` to `AST_NODE_PUB_STAM`; nesting `type_stam` or `object_type` inside extends naturally.

However, this introduces `pub type T = ...` using different AST node types than `pub x = 5`. Let's consider the simpler approach:

**Recommended approach (Option A simplified)**: Since `type_stam` already compiles to `alias($.type_assign, $.assign_expr)`, and `pub_stam` accepts `assign_expr`, the grammar change could simply be: make `pub_stam` also accept a `type` keyword prefix within its declare field. But the cleaner path is:

```javascript
pub_stam: $ => choice(
    seq('pub', field('declare', $.assign_expr),
        repeat(seq(',', field('declare', $.assign_expr)))),
    seq('pub', 'type', field('declare', alias($.type_assign, $.assign_expr)),
        repeat(seq(',', field('declare', alias($.type_assign, $.assign_expr))))),
    seq('pub', $.object_type),
),
```

This way:
- `pub type T = int | string` → `AST_NODE_PUB_STAM` with `is_type_define = true`
- `pub type Counter { ... }` → `AST_NODE_PUB_STAM` with child `AST_NODE_OBJECT_TYPE`

#### 2.2.3 AST Changes

**`AstLetNode`** (used for both `let_stam`, `pub_stam`, `type_stam`):
- Add `bool is_type_define` flag (or reuse the existing logic where `symbol == SYM_TYPE_DEFINE`).
- For `pub type`, the node type is `AST_NODE_PUB_STAM` with `is_type_define = true`.

**`AstObjectTypeNode`**:
- Add `bool is_public` flag, set when the object type is wrapped in `pub`.

#### 2.2.4 Import Registration

`declare_module_import()` currently handles `AST_NODE_FUNC`, `AST_NODE_FUNC_EXPR`, `AST_NODE_PROC`, and `AST_NODE_PUB_STAM`. Extend to also handle:

- **`AST_NODE_TYPE_STAM` with pub flag**: Register the type name in the importing scope. The importing script needs access to the type's `Type*` pointer and type index.
- **`AST_NODE_OBJECT_TYPE` with pub flag**: Register the object type name. The importer needs the `TypeObject*` and its type index.

When an imported type is used in the importing script (e.g., `x is Point`, `{Point x: 1, y: 2}`), the transpiler must:
1. Know the type index refers to the **module's** type_list, not the current script's.
2. Use the module's `_mod_const_type()` wrapper when looking up the type.

#### 2.2.5 Transpilation for Exported Types

Two categories:

**A. Type aliases (`pub type T = int | string`):**
- The type definition is a compile-time construct stored in `type_list`.
- When the importing script references the type (e.g., `x is T`), the transpiler must emit code that looks up the type from the module's type_list.
- The module struct gains a `Type*` field for each exported type:
  ```c
  struct Mod1 {
      // ... existing fields ...
      Type* _type_Point;      // exported type definition
  };
  ```
- `_init_mod_vars()` populates this field.
- When transpiling `x is Point` where `Point` is imported, emit: `type_check(x, m1._type_Point)` instead of `type_check(x, const_type(idx))`.

**B. Object types (`pub type Counter { ... }`):**
- Object types have both a type definition **and** methods (which are functions).
- Methods are already handled by the function export mechanism.
- The type definition + constructor needs to be exportable.
- The module struct gains:
  ```c
  struct Mod1 {
      // ... existing fields ...
      int _type_index_Counter;  // type index in module's type_list (for object construction)
  };
  ```
- When the importing script creates `{Counter value: 10}`, the transpiler emits `_mod_map(_type_index_Counter)` using the module's type_list wrapper.

#### 2.2.6 Cross-Module Type Checking

When performing `x is Point` where `Point` is an imported object type:
- Currently `is` checks use `get_type_id()` for primitive types and shape/type-index comparison for objects/maps.
- For imported types, the `type_index` belongs to the module's type_list. The `is` check must compare against the module's type object, not a local type index.
- This is handled by passing the `Type*` pointer (from the module struct) rather than a local type index.

### 2.3 Implementation Plan

1. **Grammar** (`grammar.js`): Extend `pub_stam` to accept `type_stam` content and `object_type`.
2. **Grammar regeneration**: `make generate-grammar`.
3. **AST builder** (`build_ast.cpp`):
   - Handle `pub type` in `build_let_and_type_stam()` — set type_define flag on `PUB_STAM`.
   - Handle `pub object_type` — set `is_public` on `AstObjectTypeNode`.
4. **`declare_module_import()`** (`build_ast.cpp`): Walk module AST for pub type stams and pub object types, register them in the importing scope.
5. **`write_mod_struct_fields()`** (`transpile.cpp`): Add `Type*` fields or type index fields for exported types.
6. **`_init_mod_vars()`** generation (`transpile.cpp`): Populate exported type pointers.
7. **Transpiler** (`transpile.cpp`): When referencing an imported type (in `is` checks, object construction, match patterns), use the module struct field.
8. **`init_module_import()`** (`runner.cpp`): Link exported type pointers at load time.

### 2.4 Test Plan

```lambda
// mod_shapes.ls
pub type Point = {x: float, y: float};
pub type Shape = 'circle' | 'rect' | 'line';
pub type Vec2 {
    x: float, y: float;
    fn len() => sqrt(x**2 + y**2)
    fn scale(f) => {Vec2 ~, x: ~.x*f, y: ~.y*f}
}

// main.ls
import .mod_shapes

let p = {x: 1.0, y: 2.0}
p is Point                    // true

let shape: Shape = 'circle'   // type annotation with imported type

let v = {Vec2 x: 3.0, y: 4.0}
v.len()                       // 5.0
v is Vec2                     // true
```

---

## 3. Aliased Imports (Fix Existing Issue §6.1)

### 3.1 Problem (from Lambda_Module.md §6.1)

Grammar and AST support for aliased imports exist, but they're non-functional. `import math: .utils.math` behaves identically to `import .utils.math` — the alias is ignored.

The `alias` field is parsed and stored on `AstImportNode`, and `push_qualified_name()` is called for aliased imports in `declare_module_import()`. However, the qualified name mechanism doesn't properly namespace the imports.

### 3.2 Why Fix Now

Aliased imports become **essential** when exporting types:
- Without aliases, importing two modules that both export a `Point` type causes a name collision.
- With aliases: `import geo: .geometry` → `geo.Point`, `import ui: .widgets` → `ui.Point`.
- Aliases also improve readability for large imports.

### 3.3 Design

**Accessing imported names via alias:**
```lambda
import math: .utils.math
import geo: .geometry

math.add(1, 2)           // alias-qualified function call
geo.Point                 // alias-qualified type reference
let p: geo.Point = ...    // alias in type annotation
```

**Without alias (current behavior, preserved):**
```lambda
import .utils.math
add(1, 2)                // direct name, no prefix
```

**Name resolution**: When an alias is present, imported names are **only** accessible through the alias prefix — they are **not** injected into the bare scope. This prevents pollution and collisions.

### 3.4 Implementation

1. **`declare_module_import()`**: When alias is present, register the alias as a namespace scope. Do NOT register individual names in the bare scope.
2. **Name resolution** (`build_ast.cpp`): When resolving `math.add`, check if `math` is a registered alias, then look up `add` in the aliased module's exports.
3. **Transpiler**: `math.add(1, 2)` → `m1._add(i2it(1), i2it(2))` — the alias is resolved to the module index at compile time.

---

## 4. Module-Scope Error Destructuring (Fix Existing Issue §6.2)

### 4.1 Problem (from Lambda_Module.md §6.2)

`let doc^err = input(...)` at file scope causes MIR compilation failure: "undeclared identifier `_err`". The error variable is not declared as a global.

### 4.2 Relevance

This is a common pattern for modules that load data files:

```lambda
// data_loader.ls
let data^err = input("config.json", 'json')
if (^err) raise error("Failed to load config", err)
pub fn get_config() => data
```

Without this fix, modules must use the workaround (`?` operator), which silently propagates errors instead of allowing local handling.

### 4.3 Fix

In the transpiler's global variable declaration logic, when processing `let a^err = expr`:
1. Declare both `_a` and `_err` as global BSS variables.
2. For the module `main()` hoisting (§5.5 from Lambda_Module.md), hoist both assignments.

**File**: `lambda/transpile.cpp` — in the global variable declaration pass, ensure the error variable (`_err` or custom name) from error destructuring is also emitted as a global.

---

## 5. Additional Recommendations

### 5.1 Normalize Import Paths for Deduplication

**Current concern**: Script deduplication in `load_script()` compares paths with `strcmp()`. If the same module is imported via different relative paths (e.g., `import .utils.math` from `main.ls` vs `import .math` from `utils/helper.ls`), they resolve to the same file but have different path strings — causing redundant compilation and separate `Script*` objects.

**Recommendation**: After constructing the import path, normalize it to an **absolute canonical path** (via `realpath()` or equivalent) before the deduplication lookup. This ensures the same physical file always maps to the same `Script*`.

```cpp
// In build_module_import() or load_script():
char* canonical = realpath(buf->str, NULL);
if (canonical) {
    ast_node->script = load_script(tp->runtime, canonical, NULL, true);
    free(canonical);
} else {
    // file doesn't exist — report error
}
```

This also prevents circular import false negatives (A imports B via one path, B imports A via a different relative path — cycle not detected because paths differ).

### 5.2 Export String/Symbol Patterns

Currently, named string patterns (`string digits = \d+`) are module-private. They could be useful as shared validation patterns:

```lambda
// patterns.ls
pub string email = \w+ "@" \w+ "." \a[2,6]
pub string phone = \d[3] "-" \d[3] "-" \d[4]

// main.ls
import .patterns
"test@example.com" is email   // true
```

This is a lower-priority enhancement but aligns with the type export work — patterns are part of the type system.

### 5.3 Error Messages for Import Failures

Currently, import failures produce minimal error info (just a log message). Improve with:
- Print the **resolved path** that was attempted
- Print the **importing script's path** for context
- Suggest possible corrections (e.g., "Did you mean `.utils.math`?") if a similar file exists

---

## 6. Priority & Ordering

| # | Enhancement | Priority | Complexity | Rationale |
|---|------------|----------|------------|-----------|
| 1 | Script-relative imports (§1) | **High** | Low | Fundamental correctness fix; every other feature depends on reliable import resolution |
| 2 | Path normalization (§5.1) | **High** | Low | Prevents deduplication bugs; should ship with #1 |
| 3 | Public type exports (§2) | **High** | Medium | Most-requested missing feature; enables real module APIs |
| 4 | Aliased imports (§3) | **Medium** | Medium | Required to avoid name collisions with type exports |
| 5 | Error destructuring fix (§4) | **Medium** | Low | Common pattern in data-loading modules |
| 6 | Pattern exports (§5.2) | **Low** | Low | Nice-to-have; natural extension of type exports |
| 7 | Better error messages (§5.3) | **Low** | Low | Quality-of-life improvement |

**Recommended implementation order**: 1 → 2 → 5 → 3 → 4 → 6 → 7

Items 1 and 2 are foundational fixes that should land first. Item 5 is a straightforward transpiler fix. Items 3 and 4 are the largest features and depend on 1/2 being stable. Items 6 and 7 are polish.

## 7. Test Matrix

| Test Case | Feature | Validates |
|-----------|---------|-----------|
| Import from absolute path (cd elsewhere) | §1 | Script-relative resolution |
| Nested imports in subdirectories | §1 | Recursive script-relative resolution |
| Same module via different relative paths | §5.1 | Path normalization + deduplication |
| `pub type T = int \| string` | §3 | Type alias export |
| `pub type Obj { ... }` | §3 | Object type export |
| `imported_val is ImportedType` | §3 | Cross-module type checking |
| `{ImportedObj field: val}` | §3 | Cross-module object construction |
| `import alias: .mod; alias.func()` | §4 | Aliased function access |
| `import alias: .mod; alias.Type` | §4 | Aliased type access |
| Two modules exporting same name + aliases | §4 | Alias namespace isolation |
| `let x^err = expr` at file scope | §6.2 | Module-scope error destructuring |
| `pub string pat = \d+` + import | §5.2 | Pattern export |

---

## 8. Implementation Progress

### Status Overview

| # | Enhancement | Status | Notes |
|---|------------|--------|-------|
| 1 | Script-relative imports (§1) | ✅ **Done** | Two-tier resolution: `.module` = script-relative, `module` = CWD-relative |
| 2 | Path normalization (§5.1) | ✅ **Done** | `realpath()` canonical dedup in `load_script()` |
| 3 | Public type exports (§2) | ✅ **Done** | Type aliases + object types with methods, both JIT paths |
| 4 | Aliased imports (§3) | ⬜ Not started | |
| 5 | Error destructuring fix (§4) | ✅ **Done** | Fixed in C2MIR transpiler, MIR direct, and AST builder |
| 6 | Pattern exports (§5.2) | ⬜ Not started | |
| 7 | Better error messages (§5.3) | ✅ **Done** | Resolved path + importing script shown on failure |

**All 545 baseline tests pass** (including 139 MIR JIT tests).

### 8.1 Script-Relative Imports — Implementation Details

**Files modified:**
- `lambda/ast.hpp` — Added `const char* directory` field to `Script` struct.
- `lambda/runner.cpp` — Rewrote `load_script()` with `realpath()` normalization and directory extraction. Canonical path is used for deduplication, preventing redundant compilation of the same physical file.
- `lambda/build_ast.cpp` (`build_module_import`) — Two-branch resolution:
  - `.module` (dot prefix) → resolves relative to importing script's `tp->directory`
  - `module` (no dot) → resolves relative to `"./"` (CWD/project root)
- ~50 test files updated to use `.module` syntax for relative imports.

### 8.2 Path Normalization — Implementation Details

Implemented as part of §1 in `load_script()`. After constructing the import path, `realpath()` converts it to an absolute canonical path before the deduplication `strcmp()` lookup. This prevents:
- Redundant compilation when the same file is imported via different relative paths
- False-negative circular import detection

### 8.3 Error Destructuring at Module Scope — Implementation Details

**Files modified:**
- `lambda/build_ast.cpp` (`declare_module_import`) — Exports both the value variable and the `^err` variable across module boundaries. Creates a synthetic `AstNamedNode` for the error variable with `TYPE_ANY`.
- `lambda/transpile.cpp` (`declare_global_var`) — Emits BSS declaration for the error variable name alongside the value variable.
- `lambda/transpile-mir.cpp` (`prepass_create_global_vars`) — Creates BSS entries for error variables. Module import registration also registers error variable BSS addresses.

**Test:** `test/lambda/import_error_destr.ls` + `test/lambda/mod_error_destr.ls`

### 8.4 Public Type Exports — Implementation Details

This was the largest feature. Changes span grammar, AST, and both transpiler backends.

**Grammar** (`lambda/tree-sitter-lambda/grammar.js`):
- Extended `pub_stam` to a `choice` with 3 alternatives:
  1. `pub <assign_expr>, ...` (existing pub vars/functions)
  2. `pub type <type_assign>, ...` (type alias exports)
  3. `pub <object_type>` (object type exports)
- Parser regenerated via `make generate-grammar`.

**AST Builder** (`lambda/build_ast.cpp`):
- `build_let_and_type_stam()` — Detects `anon_sym_type` keyword in `pub_stam` children to set `is_type_definition = true`. Handles `SYM_OBJECT_TYPE` children by calling `build_object_type()` and setting `is_public = true`.
- `build_identifier()` — Fixed TypeType import resolution: when an imported identifier has `LMD_TYPE_TYPE` type (type alias or object type), preserves the full `TypeType` wrapper instead of allocating a bare `Type` struct that loses the inner type pointer.
- `declare_module_import()` — Extended to handle pub_stam children:
  - `AST_NODE_OBJECT_TYPE`: Re-registers `TypeType` in importing script's `type_list` with a new local index, pushes name to scope.
  - Regular assign nodes: Also re-registers type aliases (`TypeType` wrapping `TypeMap`/`TypeObject`/`TypeList`/`TypeArray`) in importing script's `type_list`.
  - Key insight: The same `TypeObject*` pointer is shared between module and importer, so `fn_is()` pointer comparison works correctly across modules.

**C2MIR Transpiler** (`lambda/transpile.cpp`, 8+ locations):
- `transpile_let_stam()` — Added `AST_NODE_OBJECT_TYPE` case that calls `transpile_object_type_method_registration()`.
- `declare_global_var()` — Skips `AST_NODE_OBJECT_TYPE` children (not BSS variables).
- `assign_global_var()` — Calls `transpile_object_type_method_registration()` for object type children.
- `write_mod_struct_fields()` — Skips object type children (types not exported as struct fields).
- Module wrappers — Added `_mod_object_type_set_method` and `_mod_object_type_set_constraint` static functions with `#define` macro redirects, so method registration uses the module's local `type_list`.
- Forward declaration pass — Handles `AST_NODE_PUB_STAM` with object type children to forward-declare methods.

**MIR Direct Transpiler** (`lambda/transpile-mir.cpp`):
- `transpile_ident()` — Added handling for imported `AST_NODE_OBJECT_TYPE` entries: resolves via `transpile_const_type(type_index)` instead of BSS load.
- Added forward declaration for `transpile_const_type()`.

**Test:** `test/lambda/import_pub_types.ls` + `test/lambda/mod_pub_types.ls`
- Tests: `pub type Score = int` (type alias), `pub type Counter { value: int = 0; fn double() => value * 2 }` (object type with methods), `pub x = 42` (regular pub var)
- Validates: variable import, type alias usage (`let s: Score = 99`), object construction (`{Counter value: 5}`), method calls (`c.double()`), cross-module `is` type checking (`c is Counter`)
- Expected output: `42, 99, 5, 10, true` — passes on both C2MIR and MIR direct paths.

### 8.5 Better Error Messages — Implementation Details

Import failure now prints:
- The resolved path that was attempted
- The importing script's path for context

Example:
```
Error: Failed to import module '.missing_mod'
  Resolved path: /project/src/missing_mod.ls
  Importing script: /project/src/main.ls
```
