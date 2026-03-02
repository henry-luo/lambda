# Lambda Module System Design Proposal

## Overview

This document proposes enhancements to Lambda's module system to improve code organization, reusability, and developer experience. The design follows Lambda's functional philosophy while providing modern module features.

---

## Table of Contents

1. [Current State](#current-state)
2. [Design Goals](#design-goals)
3. [Module Syntax](#module-syntax)
4. [Import Mechanisms](#import-mechanisms)
5. [Export Mechanisms](#export-mechanisms)
6. [Module Resolution](#module-resolution)
7. [Standard Library](#standard-library)
8. [Implementation Plan](#implementation-plan)

---

## Current State

Lambda supports the following module functionality:

```lambda
// Relative import (resolved relative to importing script's directory)
import .path.to.module

// Absolute import (resolved relative to CWD/project root)
import path.to.module

// Symbol import (for built-in modules)
import 'chart'

// Aliased import (grammar exists, not yet fully functional)
import alias: .module

// Public exports — values, functions, procedures
pub PI = 3.14159
pub fn add(a: int, b: int) => a + b
pub pn log(msg) { print(msg) }

// Public type exports — type aliases and object types
pub type UserId = int
pub type Counter {
    value: int = 0;
    fn double() => value * 2
}

// Error destructuring at module scope
pub data^err = input("config.json", 'json')
```

### Already Implemented

| Feature | Status |
|---------|--------|
| Script-relative imports (`.module`) | ✅ Imports resolve relative to importing file |
| Absolute imports (`module`) | ✅ Resolve relative to CWD/project root |
| Path normalization | ✅ `realpath()` canonical dedup prevents redundant compilation |
| Public type exports (`pub type`) | ✅ Type aliases + object types with methods |
| Error destructuring at module scope | ✅ `pub a^err = expr` works across modules |
| Better error messages | ✅ Shows resolved path + importing script on failure |

### Remaining Limitations

| Limitation | Impact |
|------------|--------|
| All-or-nothing imports | Namespace pollution, unclear dependencies |
| No qualified access | Name collisions across modules |
| No re-exports | Cannot create facade modules |
| No circular import detection | Potential infinite loops |
| No standard library paths | Only relative/CWD paths supported |

---

## Design Goals

1. **Selective Imports**: Import only what you need
2. **Namespace Control**: Qualified vs unqualified access
3. ~~**Type Exports**: First-class type sharing across modules~~ ✅ Done
4. **Re-exports**: Create module facades and aggregations
5. ~~**Clear Resolution**: Predictable module path resolution~~ ✅ Done (script-relative + path normalization)
6. **Backward Compatibility**: Existing code continues to work

---

## Module Syntax

### Module Declaration (Optional)

Modules can optionally declare metadata at the top of the file:

```lambda
// Optional module declaration
module {
    name: "math_utils",
    version: "1.0.0",
    description: "Mathematical utility functions"
}

// Module contents follow...
pub fn factorial(n: int) => ...
```

The module declaration is optional - files without it are still valid modules.

---

## Import Mechanisms

### 1. Basic Import (Current - Unchanged)

Imports all public names into the local namespace:

```lambda
import .math.utils
// All pub names from math/utils.ls are available directly
```

### 2. Selective Import

Import specific names only:

```lambda
// Import specific items
import .math.utils: factorial, fibonacci

// Import with renaming
import .math.utils: factorial, fibonacci as fib

// Import types
import .types: User, UserId
```

**Grammar Addition:**
```javascript
import_selective: $ => seq(
    'import',
    field('module', $.import_module),
    ':',
    $.import_specifier,
    repeat(seq(',', $.import_specifier))
),

import_specifier: $ => choice(
    $.identifier,                                    // factorial
    seq($.identifier, 'as', $.identifier),          // fibonacci as fib
    '*'                                              // all exports
)
```

### 3. Namespace Import

Import module as a namespace (qualified access only):

```lambda
// Import as namespace
import .math.utils as math

// Access via qualified names
math.factorial(5)
math.PI

// Direct access NOT available
factorial(5)  // Error: undefined
```

**Grammar Addition:**
```javascript
import_namespace: $ => seq(
    'import',
    field('module', $.import_module),
    'as',
    field('alias', $.identifier)
)
```

### 4. Wildcard Import (Explicit)

Explicitly import all names (same as current behavior, but explicit):

```lambda
import .math.utils: *

// All names available directly
factorial(5)
PI
```

### 5. Conditional Import

Import that doesn't fail if module is missing:

```lambda
// Returns null if module not found
import? .optional.feature

// Usage with null check
if (optional_func != null) {
    optional_func()
}
```

### Import Syntax Summary

| Syntax | Behavior |
|--------|----------|
| `import .mod` | Import all pub names (current behavior) |
| `import .mod: a, b` | Import only `a` and `b` |
| `import .mod: a as x` | Import `a` renamed to `x` |
| `import .mod as m` | Namespace import, access via `m.` |
| `import .mod: *` | Explicit wildcard (same as basic) |
| `import? .mod` | Conditional import (returns null if missing) |

---

## Export Mechanisms

### 1. Public Declarations (Implemented)

```lambda
pub PI = 3.14159
pub fn add(a: int, b: int) => a + b
pub type UserId = int
pub type Counter { value: int = 0; fn double() => value * 2 }
```

> **Note:** Type exports (`pub type T = ...` and `pub type T { ... }`) are fully implemented.
> Both type aliases and object types with methods work across modules, including
> `is` type checking, object construction, and method calls.

### 2. Re-exports

Re-export items from other modules:

```lambda
// math/index.ls - facade module

// Re-export specific items
pub import .trig: sin, cos, tan
pub import .algebra: solve, factor

// Re-export all from a module
pub import .constants: *

// Re-export with rename
pub import .advanced: matrix_multiply as matmul
```

Consumers can then import from the facade:

```lambda
// Consumer code
import .math: sin, cos, solve, PI, matmul
```

**Grammar Addition:**
```javascript
pub_import: $ => seq(
    'pub',
    'import',
    field('module', $.import_module),
    ':',
    choice(
        '*',
        seq($.import_specifier, repeat(seq(',', $.import_specifier)))
    )
)
```

### 4. Export List (Alternative Syntax)

Explicit export list at module end:

```lambda
// math.ls
let PI = 3.14159
let E = 2.71828

fn factorial(n: int) => ...
fn fibonacci(n: int) => ...
fn helper(x: int) => ...  // internal

// Explicit exports at end
export PI, E, factorial, fibonacci
```

### Export Visibility Summary

| Declaration | Visibility | Status |
|-------------|------------|--------|
| `pub x = ...` | Public (exported) | ✅ Implemented |
| `pub fn f()` | Public function | ✅ Implemented |
| `pub pn p()` | Public procedure | ✅ Implemented |
| `pub type T` | Public type | ✅ Implemented |
| `pub import` | Re-export | ⬜ Not yet |
| `let x = ...` | Private (module-local) | ✅ Implemented |
| `fn f()` | Private function | ✅ Implemented |
| `type T` | Private type | ✅ Implemented |

---

## Module Resolution

### Current Resolution (Implemented)

Two-tier resolution is implemented:

| Syntax | Resolution | Status |
|--------|------------|--------|
| `.module` | Relative to importing script's directory | ✅ Implemented |
| `.path.to.module` | Relative: `<script_dir>/path/to/module.ls` | ✅ Implemented |
| `module` (no dot) | Relative to CWD/project root | ✅ Implemented |

Path normalization via `realpath()` prevents redundant compilation when the same file is imported via different relative paths.

### Future Resolution Extensions

| Syntax | Resolution | Status |
|--------|------------|--------|
| `..parent.module` | Parent: `../parent/module.ls` | ⬜ Not yet |
| `std.math` | Standard library: `<std>/math.ls` | ⬜ Not yet |
| `@pkg.module` | Package: `<packages>/pkg/module.ls` | ⬜ Not yet |

### Project Configuration

Optional `lambda.json` for path aliases and configuration:

```json
{
    "name": "my-project",
    "version": "1.0.0",
    "paths": {
        "@utils": "./src/utils",
        "@components": "./src/components"
    },
    "dependencies": {
        "lambda-charts": "^2.0.0"
    }
}
```

Usage with aliases:

```lambda
import @utils.helpers: format_date, parse_number
import @components.button: Button
```

### Index Files

Directories can have an `index.ls` that serves as the default module:

```
math/
├── index.ls      <- imported when using `import .math`
├── trig.ls
├── algebra.ls
└── constants.ls
```

```lambda
// math/index.ls
pub import .trig: *
pub import .algebra: *
pub import .constants: *

// Consumers just import the directory
import .math: sin, cos, solve, PI
```

---

## Standard Library

### Proposed Structure

```
std/
├── prelude.ls           # Auto-imported basics
├── math/
│   ├── index.ls
│   ├── basic.ls         # abs, round, floor, ceil, sign
│   ├── trig.ls          # sin, cos, tan, asin, acos, atan
│   ├── exp.ls           # exp, log, log10, log2, pow
│   └── stats.ls         # mean, median, variance, stddev
├── collections/
│   ├── index.ls
│   ├── array.ls         # sort, filter, map, reduce, find
│   ├── map.ls           # keys, values, entries, merge
│   └── set.ls           # union, intersection, difference
├── string/
│   ├── index.ls
│   ├── core.ls          # len, slice, concat, repeat
│   ├── search.ls        # contains, starts_with, ends_with, index_of
│   ├── transform.ls     # upper, lower, trim, pad, replace
│   └── format.ls        # format, template, printf
├── io/
│   ├── index.ls
│   ├── file.ls          # read, write, exists, delete
│   └── net.ls           # fetch, http_get, http_post
├── datetime/
│   ├── index.ls
│   ├── core.ls          # datetime, date, time, now, today
│   ├── format.ls        # format, parse
│   └── calc.ls          # add, subtract, diff
└── types/
    ├── index.ls
    └── common.ls        # Result, Option, etc.
```

### Prelude (Auto-imported)

The prelude contains the most commonly used functions, automatically available without import:

```lambda
// std/prelude.ls - Always available

// Type functions
pub len, type, string, int, float, bool, symbol

// Collection basics
pub sum, min, max, avg, count

// I/O basics
pub print, input, format

// Error handling
pub error
```

### Standard Library Usage

```lambda
// Import from standard library
import std.math: sin, cos, PI
import std.collections.array: sort, unique
import std.string.format: template

// Or use namespace import
import std.math as math
math.sin(math.PI / 2)

// Or import entire category
import std.math: *
sin(PI / 2)
```

---

## Error Handling

### Circular Import Detection

Track import stack during module loading:

```lambda
// a.ls
import .b  // b imports a -> circular!

// Error message:
// Circular import detected:
//   a.ls imports b.ls
//   b.ls imports a.ls
//   ^ creates cycle
```

> **Note:** Path normalization (`realpath()`) is implemented, which prevents false-negative cycle detection from different relative paths to the same file. Explicit circular import detection (tracking import stack) is not yet implemented.

### Module Not Found (Implemented)

Import failures now show the resolved path and importing script:

```
Error: Failed to import module '.missing_mod'
  Resolved path: /project/src/missing_mod.ls
  Importing script: /project/src/main.ls
```

### Import Errors

Handle errors in imported modules gracefully:

```lambda
import .broken_module

// Error:
// Failed to import ./broken_module.ls
// Caused by: Syntax error at line 15, column 8
//   unexpected token '}'
```

---

## Implementation Plan

### Phase 1: Selective Imports (High Priority)

**Goal**: Allow importing specific names from modules

**Changes Required**:
1. Grammar: Add `import_selective` rule
2. AST: Extend `AstImportNode` with specifier list
3. `build_ast.cpp`: Parse selective imports
4. `declare_module_import()`: Filter names based on specifiers

**Estimated Effort**: 2-3 days

### Phase 2: Namespace Imports (High Priority)

**Goal**: Import module as namespace for qualified access

**Changes Required**:
1. Grammar: Add `import_namespace` rule  
2. AST: Add namespace flag to `AstImportNode`
3. Name resolution: Support `namespace.name` access
4. `transpile-mir.cpp`: Generate qualified access code

**Estimated Effort**: 3-4 days

### ~~Phase 3: Type Exports~~ ✅ Implemented

Public type exports (`pub type T = ...` and `pub type T { ... }`) are fully working.
See `Lambda_Module2.md` §8.4 for implementation details.

### Phase 4: Re-exports (Medium Priority)

**Goal**: Allow modules to re-export from dependencies

**Changes Required**:
1. Grammar: Add `pub_import` rule
2. AST: New node type for re-exports
3. Two-pass module loading for re-export resolution

**Estimated Effort**: 3-4 days

### Phase 5: Standard Library (Medium Priority)

**Goal**: Organize built-in functions into standard library

**Changes Required**:
1. Create `std/` directory structure
2. Move built-in function wrappers to `.ls` files
3. Implement prelude auto-import
4. Update documentation

**Estimated Effort**: 1-2 weeks

### Phase 6: Path Resolution (Partially Done)

**Implemented:**
- ✅ Script-relative resolution (`.module` resolves from importing file's directory)
- ✅ CWD-relative resolution (`module` without dot resolves from project root)
- ✅ Path normalization via `realpath()` for deduplication

**Remaining:**
1. Add `lambda.json` configuration support
2. Support `@alias` syntax
3. Index file resolution
4. Standard library path resolution

**Estimated Effort**: 2-3 days

### Phase 7: Conditional Imports (Low Priority)

**Goal**: Graceful handling of optional dependencies

**Changes Required**:
1. Grammar: Add `import?` syntax
2. Return null instead of error for missing modules
3. Runtime null checks for conditional module access

**Estimated Effort**: 1-2 days

---

## Migration Guide

### Backward Compatibility

All existing code continues to work:

```lambda
// This still works exactly as before
import .module

// Equivalent to new explicit syntax:
import .module: *
```

### Recommended Migration

```lambda
// Before (imports everything)
import .utils
import .math

// After (explicit about dependencies)
import .utils: format_date, validate_email
import .math: sin, cos, PI as pi
```

---

## Examples

### Example 1: Math Library

```lambda
// math/constants.ls
pub PI = 3.14159265358979
pub E = 2.71828182845904
pub PHI = 1.61803398874989

// math/trig.ls
import .constants: PI

pub fn sin(x: float) -> float => _builtin_sin(x)
pub fn cos(x: float) -> float => _builtin_cos(x)
pub fn tan(x: float) -> float => sin(x) / cos(x)

pub fn deg_to_rad(deg: float) -> float => deg * PI / 180.0
pub fn rad_to_deg(rad: float) -> float => rad * 180.0 / PI

// math/index.ls
pub import .constants: PI, E, PHI
pub import .trig: sin, cos, tan, deg_to_rad, rad_to_deg

// main.ls
import .math: sin, cos, PI, deg_to_rad

let angle = deg_to_rad(45.0)
let result = sin(angle) ^ 2 + cos(angle) ^ 2  // Should be 1.0
```

### Example 2: Application with Types

```lambda
// types/user.ls
pub type UserId = int
pub type Email = string

pub type User = {
    id: UserId,
    name: string,
    email: Email,
    active: bool,
    created_at: datetime
}

pub type UserList = [User]

// services/user_service.ls
import .types.user: User, UserId, UserList

pub fn find_user(users: UserList, id: UserId) -> User? {
    (for (u in users) if (u.id == id) u else null)[0]
}

pub fn active_users(users: UserList) -> UserList {
    (for (u in users) if (u.active) u else null)
}

pub fn create_user(id: UserId, name: string, email: string) -> User {
    {
        id: id,
        name: name,
        email: email,
        active: true,
        created_at: now()
    }
}

// main.ls
import .types.user: User, UserList
import .services.user_service as users

let user_list: UserList = [
    users.create_user(1, "Alice", "alice@example.com"),
    users.create_user(2, "Bob", "bob@example.com")
]

let alice = users.find_user(user_list, 1)
let active = users.active_users(user_list)
```

### Example 3: Conditional Features

```lambda
// main.ls

// Core imports (required)
import .data: load_data, process_data

// Optional visualization (may not be installed)
import? .visualization as viz

fn main() {
    let data = load_data("input.json")
    let result = process_data(data)
    
    // Use visualization if available
    if (viz != null) {
        viz.plot(result)
    } else {
        print(format(result, 'json'))
    }
}
```

---

## Summary

This proposal enhances Lambda's module system with:

| Feature | Benefit | Status |
|---------|---------|--------|
| Selective imports | Clear dependencies, smaller namespaces | ⬜ Not yet |
| Namespace imports | Avoid collisions, explicit origins | ⬜ Not yet |
| Type exports | Share types across modules | ✅ Done |
| Re-exports | Create clean module facades | ⬜ Not yet |
| Standard library | Organized, discoverable built-ins | ⬜ Not yet |
| Path resolution | Flexible project organization | ✅ Partially done |
| Conditional imports | Optional dependencies | ⬜ Not yet |
| Error destructuring | `pub a^err = expr` at module scope | ✅ Done |
| Better error messages | Resolved path + source shown on failure | ✅ Done |

The design maintains backward compatibility while providing modern module features expected by developers from languages like TypeScript, Rust, and Python.

---

## References

- Current implementation: [runner.cpp](../lambda/runner.cpp), [build_ast.cpp](../lambda/build_ast.cpp)
- Grammar: [grammar.js](../lambda/tree-sitter-lambda/grammar.js)
- Language reference: [Lambda_Reference.md](../doc/Lambda_Reference.md)
