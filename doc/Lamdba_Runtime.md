# Lambda Runtime Data Management

## Lambda Data Structures
Lambda runtime uses the following design/convention to represent and manage its runtime data:
- for simple scalar types: LMD_TYPE_NULL, LMD_TYPE_BOOL, LMD_TYPE_INT
	- they are packed into Item, with high bits set to TypeId;
- for compound scalar types: LMD_TYPE_INT64, LMD_TYPE_FLOAT, LMD_TYPE_DTIME, LMD_TYPE_DECIMAL, LMD_TYPE_SYMBOL, LMD_TYPE_STRING, LMD_TYPE_BINARY
	- they are packed into item as a tagged pointer. It's a pointer to the actual data, with high bits set to TypeId.
	- LMD_TYPE_INT64, LMD_TYPE_FLOAT, LMD_TYPE_DTIME are stored in a special num_stack at runtime;
	- LMD_TYPE_DECIMAL, LMD_TYPE_SYMBOL, LMD_TYPE_STRING, LMD_TYPE_BINARY are allocated from heap, and reference counted;
- for container types: LMD_TYPE_LIST, LMD_TYPE_RANGE, LMD_TYPE_ARRAY_INT, LMD_TYPE_ARRAY, LMD_TYPE_MAP, LMD_TYPE_ELEMENT
	- they are direct pointers to the container data.
	- all containers extends struct Container, that starts with field TypeId;
	- they are heap allocated, and reference counted;
- Lambda map/LMD_TYPE_MAP, uses a packed struct:
	- its list of fields are defined as a linked list of ShapeEntry;
	- and the actual data are stored as a packed struct;
- Lambda element/LMD_TYPE_ELEMENT, extends Lambda list/LMD_TYPE_LIST, and it's also a map/LMD_TYPE_MAP at the same time;
	- note that it can be casted as List directly, but not Map directly;
- can use get_type_id() function to get the TypeId of an Item in a general manner;

### Lambda Type → C Runtime Type Mapping

When the transpiler emits unboxed (native) C code for typed variables and parameters, each Lambda type maps to a C type:

| Lambda Type | TypeId | C Runtime Type | Boxing | Notes |
|---|---|---|---|---|
| `null` | `LMD_TYPE_NULL` | `Item` | packed | high bits = TypeId, value = 0 |
| `bool` | `LMD_TYPE_BOOL` | `bool` | packed | `b2it()` / `it2b()` |
| `int` | `LMD_TYPE_INT` | `int64_t` | packed (int56) | `i2it()` / `it2i()`. **Changed from `int32_t` to `int64_t`** to support full 56-bit range without truncation |
| `int64` | `LMD_TYPE_INT64` | `int64_t` | tagged pointer | `l2it()` / `it2l()`, stored in num_stack |
| `float` | `LMD_TYPE_FLOAT` | `double` | tagged pointer | `d2it()` / `it2d()`, stored in num_stack |
| `datetime` | `LMD_TYPE_DTIME` | `DateTime` | tagged pointer | stored in num_stack |
| `decimal` | `LMD_TYPE_DECIMAL` | `Decimal*` | tagged pointer | heap-allocated, ref-counted |
| `symbol` | `LMD_TYPE_SYMBOL` | `String*` | tagged pointer | heap-allocated, ref-counted |
| `string` | `LMD_TYPE_STRING` | `String*` | tagged pointer | heap-allocated, ref-counted |
| `binary` | `LMD_TYPE_BINARY` | `String*` | tagged pointer | heap-allocated, ref-counted |
| `list` | `LMD_TYPE_LIST` | `List*` | direct pointer | container, ref-counted |
| `array` | `LMD_TYPE_ARRAY` | `Array*` | direct pointer | container, ref-counted |
| `map` | `LMD_TYPE_MAP` | `Map*` | direct pointer | container, packed struct |
| `element` | `LMD_TYPE_ELEMENT` | `Element*` | direct pointer | extends List, also acts as Map |
| `range` | `LMD_TYPE_RANGE` | `Range*` | direct pointer | container |
| `any` / untyped | `LMD_TYPE_ANY` | `Item` | — | generic tagged value |

> **int32 → int64 change**: Lambda `int` was previously transpiled as C `int32_t`. It now transpiles as `int64_t` to match the 56-bit range of the `i2it()` packed representation. This avoids silent truncation when values exceed 32-bit range (e.g., `deep_sum(10000)` = 5,000,050,000).

### Header Files
Lambda header files defined the runtime data. They are layer one up on the other, from basic data structs, to the full runtime transpiler and runner definition.
- *lambda.h*:
	- the fundamental data structures of Lambda;
	- the C version is for MIR JIT compiler;
		- thus it defines the API of Lambda runtime that is exposed to C2MIR JIT compiler;
	- the C++ version is for the manual-written/AOT-compiled Lambda runtime code;
- *lambda-data.hpp*:
	- the full C++ definitions of the data structures and the API functions to work with the data;
	- input parsers work at this level;
- *ast.hpp*:
	- the AST built from Tree-sitter syntax tree;
	- Lambda validator, formatter works at this level;
- *transpiler.hpp*:
	- the full Lambda transpiler and code runner;

## Function Parameter Handling

#### Parameter Count Mismatch
- **Missing arguments**: automatically filled with `ITEM_NULL` at transpile time
- **Extra arguments**: discarded with warning logged at transpile time
- Enables optional parameter patterns: `if (opt == null) "default" else opt`

#### Type Matching
- Argument types validated against parameter types during AST building
- Type errors accumulate (up to 10) before stopping transpilation
- Compatible types: `int` → `float` (automatic coercion), `ANY` accepts all types

#### Boxing/Unboxing
Primitive ↔ Item conversions at function boundaries:

| Direction | Functions | Use Case |
|-----------|-----------|----------|
| **Boxing** (primitive → Item) | `i2it()`, `l2it()`, `d2it()`, `b2it()`, `s2it()` | Return values from typed functions |
| **Unboxing** (Item → primitive) | `it2i()`, `it2l()`, `it2d()`, `it2b()` | Pass Item args to typed parameters |

## String Memory Management

Lambda uses three distinct string allocation strategies optimized for different use cases:

#### 1. Names (Structural Identifiers)
**Function**: `heap_create_name(const char* str, size_t len)`
**Pooling**: Always pooled in NamePool (string interning)
**Use Cases**:
- Map keys
- Element tag names
- Element attribute names
- Function names
- Variable names
- Any structural identifier that appears multiple times

**Benefits**:
- Same name string always returns same pointer (identity comparison)
- Memory sharing across entire document hierarchy
- Inherits from parent NamePool (schemas share names with instances)

#### 2. Symbols (Short Identifiers)
**Function**: `heap_create_symbol(const char* str, size_t len)`
**Pooling**: Conditionally pooled (only if length ≤ 32 chars)
**Use Cases**:
- Symbol literals in Lambda code: `'mySymbol`
- Short identifier strings
- Enum-like values

**Benefits**:
- Common short symbols are pooled (memory sharing)
- Long symbols fall back to arena allocation (no overhead)

**Size Limit**: `NAME_POOL_SYMBOL_LIMIT = 32` characters

#### 3. Strings (Content Data)
**Function**: `heap_strcpy(const char* str, size_t len)` or `builder.createString()`
**Pooling**: Never pooled (arena allocated)
**Use Cases**:
- User content text
- String values in documents
- Free-form text data
- Anything that's not a structural identifier

**Benefits**:
- Fast arena allocation (no hash lookup overhead)
- No memory overhead for unique content
- Efficient for one-time strings

#### API Decision Guide

| String Type | Function | Pooled? | Use When |
|-------------|----------|---------|----------|
| **Name** | `heap_create_name()` or `builder.createName()` | ✅ Always | Map keys, element tags, attribute names, identifiers |
| **Symbol** | `heap_create_symbol()` | ✅ If ≤32 chars | Symbol literals, short enum-like values |
| **String** | `heap_strcpy()` or `builder.createString()` | ❌ Never | User content, text data, unique values |

**Rule of Thumb**: If it's a structural name that will appear many times, use `createName()`. If it's content data, use `createString()`.

#### NamePool Hierarchy

NamePools support parent-child relationships for schema inheritance:

**Benefits**:
- Schema definitions share names with document instances
- No memory duplication for inherited names
- Efficient for validation and transformation pipelines

## Memory Management

Lambda Script uses automatic memory management with reference counting and memory pools:

### Reference Counting

- All values are automatically reference counted
- Memory is freed when reference count reaches zero
- No manual memory management required

### Memory Pools

- Objects are allocated from memory pools for efficiency
- Pools are automatically managed by the runtime
- Reduces fragmentation and improves performance

### Immutability

- Most data structures are immutable by default
- Immutability eliminates many memory safety issues
- Structural sharing for efficient memory usage

```lambda
// Immutable collections
let list1 = (1, 2, 3);
let list2 = (0, list1...);  // Shares structure with list1

// Mutable collections (arrays)
let arr = [1, 2, 3];
// arr is mutable, but assignment creates new references
```

### Coding Guidelines
- Start comments in lowercase.
- **Add debug logging** for development and troubleshooting.
- **Test with comprehensive nested data structures** and use timeout (default: 5s) to catch hangs early
- **Back up the file** before major refactoring or rewrite. Remove the backup at the end of successful refactoring or rewrite.

### Debugging Transpiled Code
- Check `./_transpiled.c` for the generated C code from the last Lambda script execution
- Useful for debugging type mismatches, boxing/unboxing issues, and function call generation
- Shows how Lambda expressions map to C runtime calls (e.g., `fn_eq()`, `list_push()`, `i2it()`)

## MIR JIT Workarounds

### Swap-Safe Store Functions

MIR's SSA optimizer (at level ≥ 2) can reorder assignments in while loops, breaking swap patterns like:
```c
temp = a + b;  a = b;  b = temp;  // MIR may reorder these
```

The workaround uses external runtime store functions that MIR cannot inline or reorder:

| Function | Signature | Emitted For |
|---|---|---|
| `_store_i64` | `void _store_i64(int64_t* dst, int64_t val)` | `int`, `int64`, `bool` assignments in while loops |
| `_store_f64` | `void _store_f64(double* dst, double val)` | `float` assignments in while loops |

The transpiler emits `_store_i64(&_var, value)` instead of `_var = value` when `while_depth > 0` and the target is a native scalar type. Defined in `lambda-data.cpp`, registered in the MIR import table in `mir.c`.

### Module Wrapper Function Pointers

When a public function in an imported module has typed parameters or a native return type, `fn_call*` dispatchers cannot call it directly (ABI mismatch). The transpiler generates a `_w` wrapper that accepts/returns `Item` and unboxes/boxes internally.

For cross-module calls, these wrappers must be accessible via the module's BSS struct:
- **`write_mod_struct_fields()`** in `transpile.cpp` — emits `_w` wrapper function pointer fields in the `Mod` struct alongside the original function pointers
- **`init_module_import()`** in `runner.cpp` — populates wrapper pointers via `find_func()` using the `_w`-suffixed name
- **`needs_fn_call_wrapper()`** — determines which public functions need wrapper entries (typed params, or native return with no params)
