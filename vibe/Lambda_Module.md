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

Lambda currently supports basic module functionality:

```lambda
// Relative import (dot-separated path)
import .path.to.module

// Symbol import (for built-in modules)
import 'chart'

// Aliased import
import alias: .module

// Public exports
pub PI = 3.14159
pub fn add(a: int, b: int) => a + b
```

### Current Limitations

| Limitation | Impact |
|------------|--------|
| All-or-nothing imports | Namespace pollution, unclear dependencies |
| No qualified access | Name collisions across modules |
| No type exports | Types must be redefined in each file |
| No re-exports | Cannot create facade modules |
| Limited path resolution | Only relative paths supported |
| No circular import detection | Potential infinite loops |

---

## Design Goals

1. **Selective Imports**: Import only what you need
2. **Namespace Control**: Qualified vs unqualified access
3. **Type Exports**: First-class type sharing across modules
4. **Re-exports**: Create module facades and aggregations
5. **Clear Resolution**: Predictable module path resolution
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

### 1. Public Declarations (Current - Unchanged)

```lambda
pub PI = 3.14159
pub fn add(a: int, b: int) => a + b
```

### 2. Type Exports

Export type definitions for use in other modules:

```lambda
// types.ls
pub type UserId = int
pub type UserName = string

pub type User = {
    id: UserId,
    name: UserName,
    email: string,
    active: bool
}

pub type UserList = [User]
```

```lambda
// main.ls
import .types: User, UserId

fn create_user(id: UserId, name: string) -> User {
    {id: id, name: name, email: "", active: true}
}
```

### 3. Re-exports

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

| Declaration | Visibility |
|-------------|------------|
| `pub x = ...` | Public (exported) |
| `pub fn f()` | Public function |
| `pub type T` | Public type |
| `pub import` | Re-export |
| `let x = ...` | Private (module-local) |
| `fn f()` | Private function |
| `type T` | Private type |

---

## Module Resolution

### Resolution Order

When resolving `import .path.to.module`:

1. **Relative to current file**: `./path/to/module.ls`
2. **Relative to project root**: `<project>/path/to/module.ls`
3. **Standard library**: `<std>/path/to/module.ls`
4. **Installed packages**: `<packages>/path/to/module.ls`

### Path Syntax

| Syntax | Resolution |
|--------|------------|
| `.module` | Relative: `./module.ls` |
| `.path.to.module` | Relative: `./path/to/module.ls` |
| `..parent.module` | Parent: `../parent/module.ls` |
| `std.math` | Standard library: `<std>/math.ls` |
| `@pkg.module` | Package: `<packages>/pkg/module.ls` |

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

Implementation approach:

```cpp
// In load_script() or build_module_import()
thread_local std::vector<const char*> import_stack;

Script* load_script(Runtime *runtime, const char* script_path, ...) {
    // Check for circular import
    for (const char* path : import_stack) {
        if (strcmp(path, script_path) == 0) {
            log_error("Circular import detected");
            print_import_chain(import_stack, script_path);
            return nullptr;
        }
    }
    
    import_stack.push_back(script_path);
    // ... load and compile ...
    import_stack.pop_back();
}
```

### Module Not Found

Clear error messages for missing modules:

```lambda
import .nonexistent.module

// Error:
// Module not found: ./nonexistent/module.ls
// Searched paths:
//   ./nonexistent/module.ls
//   <project>/nonexistent/module.ls
//   <std>/nonexistent/module.ls
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

### Phase 3: Type Exports (High Priority)

**Goal**: Export and import type definitions

**Changes Required**:
1. Extend `declare_module_import()` to handle type definitions
2. Type pool sharing across modules
3. Cross-module type checking

**Estimated Effort**: 2-3 days

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

### Phase 6: Path Resolution (Medium Priority)

**Goal**: Support multiple path resolution strategies

**Changes Required**:
1. Implement path resolver with search paths
2. Add `lambda.json` configuration support
3. Support `@alias` syntax
4. Index file resolution

**Estimated Effort**: 3-4 days

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

| Feature | Benefit |
|---------|---------|
| Selective imports | Clear dependencies, smaller namespaces |
| Namespace imports | Avoid collisions, explicit origins |
| Type exports | Share types across modules |
| Re-exports | Create clean module facades |
| Standard library | Organized, discoverable built-ins |
| Path resolution | Flexible project organization |
| Conditional imports | Optional dependencies |

The design maintains backward compatibility while providing modern module features expected by developers from languages like TypeScript, Rust, and Python.

---

## References

- Current implementation: [runner.cpp](../lambda/runner.cpp), [build_ast.cpp](../lambda/build_ast.cpp)
- Grammar: [grammar.js](../lambda/tree-sitter-lambda/grammar.js)
- Language reference: [Lambda_Reference.md](../doc/Lambda_Reference.md)
