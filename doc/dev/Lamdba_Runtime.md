# Lambda Runtime Data Management

## Lambda Data Structures
Lambda runtime uses the following design/convention to represent and manage its runtime data:
- for simple scalar types: LMD_TYPE_NULL, LMD_TYPE_BOOL, LMD_TYPE_INT
	- they are packed into Item, with high bits set to TypeId;
- for compound scalar types: LMD_TYPE_INT64, LMD_TYPE_FLOAT, LMD_TYPE_DTIME, LMD_TYPE_DECIMAL, LMD_TYPE_SYMBOL, LMD_TYPE_STRING, LMD_TYPE_BINARY
	- they are packed into item as a tagged pointer. It's a pointer to the actual data, with high bits set to TypeId.
	- LMD_TYPE_INT64, LMD_TYPE_FLOAT, LMD_TYPE_DTIME are stored in a special num_stack at runtime;
	- LMD_TYPE_DECIMAL, LMD_TYPE_SYMBOL, LMD_TYPE_STRING, LMD_TYPE_BINARY are allocated from heap, and reference counted;
- for container types: LMD_TYPE_LIST, LMD_TYPE_RANGE, LMD_TYPE_ARRAY_INT, LMD_TYPE_ARRAY_INT64, LMD_TYPE_ARRAY_FLOAT, LMD_TYPE_ARRAY, LMD_TYPE_MAP, LMD_TYPE_VMAP, LMD_TYPE_ELEMENT
	- they are direct pointers to the container data.
	- all containers extends struct Container, that starts with field TypeId;
	- they are heap allocated, and reference counted;
- Lambda map/LMD_TYPE_MAP, uses a packed struct:
	- its list of fields are defined as a linked list of ShapeEntry;
	- and the actual data are stored as a packed struct;
- Lambda element/LMD_TYPE_ELEMENT, extends Lambda list/LMD_TYPE_LIST, and it's also a map/LMD_TYPE_MAP at the same time;
	- note that it can be casted as List directly, but not Map directly;
- Lambda VMap/LMD_TYPE_VMAP, virtual map with vtable dispatch:
	- supports arbitrary key types and pluggable backends (HashMap, TreeMap, etc.);
	- `type(vmap)` returns "map" — transparent to Lambda scripts;
- can use get_type_id() function to get the TypeId of an Item in a general manner;

### Item Bit Layout

The `Item` type is a 64-bit tagged union defined in `lambda.hpp`. The top 8 bits (`_type_id`) encode the type, the lower 56 bits encode the value or pointer:

```
|  8-bit TypeId  |        56-bit payload        |
| bits 63..56    |        bits 55..0             |
```

**Three categories of storage:**

| Category | TypeId field | 56-bit payload | type_id() method |
|---|---|---|---|
| **Inline scalars** (int, bool, null) | `_type_id > 0` | Value packed directly | Reads `_type_id` |
| **Tagged pointers** (int64, float, datetime, string, symbol, decimal, binary) | `_type_id > 0` | Pointer to heap/stack data | Reads `_type_id` |
| **Container pointers** (list, array, map, element, range, etc.) | `_type_id == 0` | Full 64-bit pointer | Dereferences `Container::type_id` |

The `type_id()` method first checks `_type_id`; if zero, dereferences the pointer to read the container's embedded type ID. This is why container pointers use the full 64 bits (no tag bits stolen from the pointer).

### TypeId Enum Values

| Value | TypeId | Category |
|---|---|---|
| 0 | `LMD_TYPE_RAW_PTR` | raw pointer (untagged) |
| 1 | `LMD_TYPE_NULL` | inline scalar |
| 2 | `LMD_TYPE_BOOL` | inline scalar |
| 3 | `LMD_TYPE_INT` | inline scalar (int56) |
| 4 | `LMD_TYPE_INT64` | tagged pointer (num_stack) |
| 5 | `LMD_TYPE_FLOAT` | tagged pointer (num_stack) |
| 6 | `LMD_TYPE_DECIMAL` | tagged pointer (heap) |
| 7 | `LMD_TYPE_NUMBER` | abstract type (union of int/int64/float/decimal) |
| 8 | `LMD_TYPE_DTIME` | tagged pointer (num_stack) |
| 9 | `LMD_TYPE_SYMBOL` | tagged pointer (heap, pooled ≤32 chars) |
| 10 | `LMD_TYPE_STRING` | tagged pointer (heap) |
| 11 | `LMD_TYPE_BINARY` | tagged pointer (heap) |
| 12 | `LMD_TYPE_LIST` | container pointer |
| 13 | `LMD_TYPE_RANGE` | container pointer |
| 14 | `LMD_TYPE_ARRAY_INT` | container pointer |
| 15 | `LMD_TYPE_ARRAY_INT64` | container pointer |
| 16 | `LMD_TYPE_ARRAY_FLOAT` | container pointer |
| 17 | `LMD_TYPE_ARRAY` | container pointer (generic) |
| 18 | `LMD_TYPE_MAP` | container pointer |
| 19 | `LMD_TYPE_VMAP` | container pointer |
| 20 | `LMD_TYPE_ELEMENT` | container pointer |
| 21 | `LMD_TYPE_TYPE` | type meta |
| 22 | `LMD_TYPE_FUNC` | function pointer |
| 23 | `LMD_TYPE_ANY` | abstract (wildcard type) |
| 24 | `LMD_TYPE_ERROR` | error sentinel |

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
| `array_int` | `LMD_TYPE_ARRAY_INT` | `ArrayInt*` | direct pointer | container, int56 elements |
| `array_int64` | `LMD_TYPE_ARRAY_INT64` | `ArrayInt64*` | direct pointer | container, int64 elements |
| `array_float` | `LMD_TYPE_ARRAY_FLOAT` | `ArrayFloat*` | direct pointer | container, double elements |
| `map` | `LMD_TYPE_MAP` | `Map*` | direct pointer | container, packed struct |
| `vmap` | `LMD_TYPE_VMAP` | `VMap*` | direct pointer | container, vtable dispatch |
| `element` | `LMD_TYPE_ELEMENT` | `Element*` | direct pointer | extends List, also acts as Map |
| `range` | `LMD_TYPE_RANGE` | `Range*` | direct pointer | container |
| `any` / untyped | `LMD_TYPE_ANY` | `Item` | — | generic tagged value |

> **int32 → int64 change**: Lambda `int` was previously transpiled as C `int32_t`. It now transpiles as `int64_t` to match the 56-bit range of the `i2it()` packed representation. This avoids silent truncation when values exceed 32-bit range (e.g., `deep_sum(10000)` = 5,000,050,000).

### Boxing Macros (primitive → Item)

Defined in `lambda.h`. Each takes a native C value and returns a `uint64_t` encoding the Item:

| Macro | Signature | Semantics |
|---|---|---|
| `i2it(val)` | `int64_t → uint64_t` | Range-checked int56: if `INT56_MIN ≤ val ≤ INT56_MAX`, returns `ITEM_INT \| (val & MASK56)`, else `ITEM_ERROR` |
| `b2it(val)` | `uint8_t → uint64_t` | If `val ≥ BOOL_ERROR(2)`, returns `ITEM_ERROR`. Otherwise `(LMD_TYPE_BOOL<<56) \| val` |
| `l2it(ptr)` | `int64_t* → uint64_t` | Tagged pointer: `(LMD_TYPE_INT64<<56) \| ptr`. Returns `ITEM_NULL` if ptr is NULL |
| `d2it(ptr)` | `double* → uint64_t` | Tagged pointer: `(LMD_TYPE_FLOAT<<56) \| ptr`. Returns `ITEM_NULL` if NULL |
| `s2it(ptr)` | `String* → uint64_t` | Tagged pointer: `(LMD_TYPE_STRING<<56) \| ptr`. Returns `ITEM_NULL` if NULL |
| `y2it(ptr)` | `Symbol* → uint64_t` | Tagged pointer: `(LMD_TYPE_SYMBOL<<56) \| ptr`. Returns `ITEM_NULL` if NULL |
| `k2it(ptr)` | `DateTime* → uint64_t` | Tagged pointer: `(LMD_TYPE_DTIME<<56) \| ptr`. Returns `ITEM_NULL` if NULL |
| `c2it(ptr)` | `Decimal* → uint64_t` | Tagged pointer: `(LMD_TYPE_DECIMAL<<56) \| ptr`. Returns `ITEM_NULL` if NULL |
| `x2it(ptr)` | `Binary* → uint64_t` | Tagged pointer: `(LMD_TYPE_BINARY<<56) \| ptr`. Returns `ITEM_NULL` if NULL |

**Int56 range constants:**
```c
#define INT56_MAX  ((int64_t)0x007FFFFFFFFFFFFF)   // +36,028,797,018,963,967
#define INT56_MIN  ((int64_t)0xFF80000000000000LL)  // -36,028,797,018,963,968
```

**Overflow behavior**: `i2it` returns `ITEM_ERROR` if the value exceeds the 56-bit signed range.

### Unboxing Functions (Item → primitive)

Defined in `lambda-data.cpp`. Each takes an Item and extracts the native C value:

| Function | Signature | Semantics |
|---|---|---|
| `it2i(Item)` | `Item → int64_t` | INT→`get_int56()`, INT64→`get_int64()`, FLOAT→cast, BOOL→0/1, ERROR→0 |
| `it2l(Item)` | `Item → int64_t` | INT→`get_int56()`, INT64→`get_int64()`, FLOAT→cast, BOOL→0/1. Returns `INT64_MAX` on unrecognized type |
| `it2d(Item)` | `Item → double` | INT→cast via `get_int56()`, INT64→cast `get_int64()`, FLOAT→`get_double()`, DECIMAL→`decimal_to_double()`, ERROR→`NAN` |
| `it2b(Item)` | `Item → bool` | BOOL→`bool_val`, NULL/ERROR→false, INT→`get_int56()!=0`, FLOAT→`!isnan&&!=0.0`, STRING→`len>0`, others→true |
| `it2s(Item)` | `Item → String*` | STRING→`get_string()`, ERROR→static `"<error>"`, others→`nullptr` |

> **Subtle difference**: `it2i` returns 0 on error; `it2l` returns `INT64_MAX` on unrecognized types. `it2l` is the preferred unboxer for INT64 contexts.

### Boxing Idempotency Properties

This table documents which boxing operations are safe to apply multiple times (idempotent) and which are not. This is critical for transpiler correctness when a value might already be boxed:

| Type                               | Boxing                                              | Idempotent? | Why                                                                                                                                              |
| ---------------------------------- | --------------------------------------------------- | ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| STRING / SYMBOL / DECIMAL / BINARY | inline OR tag on pointer                            | ✅ Yes       | OR-ing the same tag is a no-op                                                                                                                   |
| INT                                | `i2it()` / `emit_box_int` (range check + mask + OR) | ⚠️ Mostly   | mask56 strips any existing tag, then adds INT tag. But a boxed INT64 pointer value (≈2.88e17) exceeds INT56_MAX range check → returns ITEM_ERROR |
| INT64                              | `push_l()` (allocates on num_stack)                 | ❌ No        | Each call allocates new storage; double-boxing creates a pointer-to-a-tagged-pointer                                                             |
| INT64                              | `push_l_safe()`                                     | ✅ Yes       | Checks high byte tag first: if already boxed INT64, returns as-is; if boxed INT, extracts and re-boxes                                           |
| FLOAT                              | `push_d()` (allocates on num_stack)                 | ❌ No        | Same allocation issue as INT64                                                                                                                   |
| DTIME                              | `push_k()` (allocates on num_stack)                 | ❌ No        | Same allocation issue as INT64                                                                                                                   |
| BOOL                               | `b2it()` / `emit_box_bool`                          | ✅ Yes       | Tag is in high bits, value is in low bits                                                                                                        |

### Header Files
Lambda header files defined the runtime data. They are layer one up on the other, from basic data structs, to the full runtime transpiler and runner definition.
- *lambda.h*:
	- the fundamental data structures of Lambda;
	- the C version is for MIR JIT compiler;
		- thus it defines the API of Lambda runtime that is exposed to C2MIR JIT compiler;
	- the C++ version is for the manual-written/AOT-compiled Lambda runtime code;
- *lambda.hpp*:
	- C++ `Item` struct with union members for all tagged pointer variants;
	- `ConstItem` for read-only access;
	- `get_int56()` sign-extension logic;
	- Container structs: `Range`, `List`, `ArrayInt`, `ArrayInt64`, `ArrayFloat`, `Map`, `Element`, `VMap`;
	- Error propagation guard macros: `GUARD_ERROR1/2/3`, `GUARD_BOOL_ERROR1/2`, `GUARD_DATETIME_ERROR1/2/3`;
- *lambda-data.hpp*:
	- the full C++ definitions of the data structures and the API functions to work with the data;
	- input parsers work at this level;
- *ast.hpp*:
	- the AST built from Tree-sitter syntax tree;
	- `SysFuncInfo` struct and `sys_funcs[]` table for system function registration;
	- Lambda validator, formatter works at this level;
- *transpiler.hpp*:
	- the full Lambda transpiler and code runner;

## num_stack: Numeric Value Storage

The `num_stack` (defined in `lib/num_stack.h`, `lib/num_stack.c`) is a chunked linked-list stack used to store compound scalar values (int64, double, DateTime) that are too large to inline in the 56-bit Item payload.

### Data Structures

```c
typedef union {
    int64_t as_long;
    double as_double;
    DateTime as_datetime;
} num_value_t;                          // 8 bytes per value

struct num_chunk {
    num_value_t *data;                  // array of elements
    size_t capacity;                    // max elements in this chunk
    size_t used;                        // currently used elements
    struct num_chunk *next, *prev;      // doubly-linked
    int index;                          // chunk index for debug
};

typedef struct num_stack_t {
    num_chunk_t *head, *tail;           // first/last chunks
    num_chunk_t *current_chunk;         // current write chunk
    size_t current_chunk_position;      // position within current chunk
    size_t total_length;                // total elements across all chunks
    size_t initial_chunk_size;          // initial chunk capacity
} num_stack_t;
```

### Growth Strategy

When the current chunk is full, `allocate_new_chunk()` creates a new chunk with **double the capacity** of the previous one. Default initial capacity is 16 elements.

### Push Functions

Defined in `lambda-mem.cpp`. Each allocates a slot on the num_stack and returns a tagged Item:

| Function | Signature | Semantics |
|---|---|---|
| `push_d(double)` | `double → Item` | Pushes to `num_stack`, returns `{.item = d2it(ptr)}` |
| `push_l(int64_t)` | `int64_t → Item` | Pushes to `num_stack`, returns `{.item = l2it(ptr)}`. Returns `ItemError` if `val == INT64_ERROR` |
| `push_l_safe(int64_t)` | `int64_t → Item` | **MIR JIT workaround**: checks high byte first — if already-boxed INT64, returns as-is; if boxed INT, extracts via `get_int56()` and re-boxes as INT64; otherwise delegates to `push_l()` |
| `push_k(DateTime)` | `DateTime → Item` | Checks for `DATETIME_IS_ERROR()` sentinel first. Pushes to `num_stack`, returns `{.item = k2it(ptr)}` |

All push functions check `context->num_stack != NULL` and return `ItemError` on failure.

### Lifecycle

The `num_stack` lives in `EvalContext` and persists for the duration of script execution. `num_stack_reset_to_index(stack, index)` is used for frame-level cleanup, traversing from the tail and freeing chunks beyond the target index.

## Two Transpiler Architectures

Lambda has two JIT compilation paths that share the same runtime functions but generate code differently:

### C2MIR Transpiler (`transpile.cpp`)

**Pipeline: AST → C source code → c2mir → MIR IR → native**

1. Generates C source code as a string (`StrBuf`) from the Lambda AST
2. Feeds the C code through `c2mir_compile()` (C-to-MIR compiler)
3. MIR is then JIT-compiled to native machine code via `MIR_gen()`

Key characteristics:
- All runtime calls are C function calls by name (resolved at link time via `import_resolver`)
- **Typed arrays fully supported**: checks `TypeArray::nested` to emit `array_int()`, `array_int64()`, `array_float()` or generic `array()`
- Uses C statement expressions `({ ... })` extensively for expression-oriented code
- For-loop iteration dispatches on typed arrays for unboxed element access
- More mature, feature-complete
- This is the **default** path when running `./lambda.exe script.ls`
- Generated C code can be inspected in `./_transpiled*.c` for debugging

### MIR Direct Transpiler (`transpile-mir.cpp`)

**Pipeline: AST → MIR IR instructions directly → native**

1. Builds MIR instructions directly using the `MIR_new_insn()` API
2. Creates functions, registers, labels, and control flow in MIR IR
3. JIT-compiled to native machine code via `MIR_gen()`

Key characteristics:
- Skips C code generation entirely — more efficient compilation
- **Inline boxing operations** (e.g., `emit_box_int()` generates range-check + tag MIR instructions instead of calling `i2it`)
- **Does not yet support typed arrays** — always uses generic `Array*` for all array literals
- **Closure support** with mutable capture via env struct write-back
- **Proc support** with `in_proc` flag and multi-value return path
- **Cross-module calls** for imported functions (resolves wrappers when needed)
- Runtime functions imported via proto/import declarations resolved by the same `import_resolver`
- Used with the `--mir` flag: `./lambda.exe --mir script.ls`

### Comparison of Transpiler Approaches

| Aspect | C2MIR (`transpile.cpp`) | MIR Direct (`transpile-mir.cpp`) |
|---|---|---|
| Code generation | Generates C source text | Generates MIR IR instructions |
| Compilation steps | 2 (C→MIR, MIR→native) | 1 (MIR→native) |
| Typed arrays | ✅ `array_int()`, `array_int64()`, `array_float()` | ❌ Always generic `array()` |
| Inline boxing | ❌ Calls runtime macros | ✅ Inline MIR instructions |
| Closures | ✅ Supported | ✅ Supported (with mutable capture via env write-back) |
| Variadic params | ✅ Supported | ✅ Supported |
| String patterns | ✅ Supported | ✅ Supported |
| Module imports | ✅ Full support | ✅ Supported (cross-module calls with wrapper resolution) |
| Proc support | ✅ Supported | ✅ Supported (`in_proc` flag, multi-value return) |
| Bitwise operators | ✅ Supported | ✅ Native int arg dispatch (band/bor/bxor/bnot/shl/shr) |
| Debugging | Check `_transpiled*.c` | No intermediate output |

**Test coverage**: 113/113 tests pass (90 functional + 26 procedural + 3 chart tests, minus 6 excluded). All tests produce identical output to the C2MIR path.

### System Function Dispatch

Both transpilers dispatch system function calls using the same mechanism:

1. **SysFuncInfo table** (`build_ast.cpp`): 118 entries mapping Lambda function names to metadata (arg count, return type, C function name prefix)
2. **C function naming**: `fn_` prefix for pure functions, `pn_` prefix for procedures. Overloaded functions append arg count: `fn_min1`, `fn_min2`
3. **Import resolution** (`mir.c`): `import_resolver()` does linear scan of 306-entry `func_list[]` array, matching by name

#### SysFuncInfo Structure

```cpp
typedef struct SysFuncInfo {
    SysFunc fn;                 // enum identifier (e.g., SYSFUNC_SUM)
    const char* name;           // Lambda name (e.g., "sum")
    int arg_count;              // expected args (-1 for variadic)
    Type* return_type;          // Lambda return type
    bool is_proc;               // true for side-effecting functions
    bool is_overloaded;         // true if same name with different arg counts
    bool is_method_eligible;    // true if callable as obj.method()
    TypeId first_param_type;    // type constraint on first param
    bool can_raise;             // true if may return error (T^ type)
} SysFuncInfo;
```

#### C Return Type vs Lambda Return Type

The `return_type` in `SysFuncInfo` is the **Lambda-level semantic type**, not the C return type. Some system functions share the same Lambda return type but differ in C:

| C return type | Transpiler handling | Example functions |
|---|---|---|
| `Item` (boxed) | No post-processing needed | `fn_sum`, `fn_add`, `fn_div`, most generic functions |
| `int64_t` (native) | Box with `i2it()`/`emit_box_int` | `fn_len`, `fn_count` |
| `Bool` (native) | Box with `b2it()`/`emit_box_bool` | `fn_eq`, `fn_lt`, `fn_is`, `fn_in` |
| `String*` (native) | Box with `s2it()`/`emit_box_string` | `fn_strcat`, `fn_lower`, `fn_upper` |
| `double` (native) | Box with `d2it()`/`emit_box_float` | `pn_clock` |
| `DateTime` (native) | Box with `k2it()`/`emit_box_dtime` | `fn_datetime0`, `fn_date0` |
| `Type*` (native) | Box with `emit_box_type` | `fn_type` |

The transpiler uses the `SysFunc` enum value in a switch statement to determine the actual C return type. Functions not listed in the switch default to returning `Item` (already boxed).

## Function Parameter Handling

#### Parameter Count Mismatch
- **Missing arguments**: automatically filled with `ITEM_NULL` at transpile time
- **Extra arguments**: discarded with warning logged at transpile time
- Enables optional parameter patterns: `if (opt == null) "default" else opt`

#### Type Matching
- Argument types validated against parameter types during AST building
- Type errors accumulate (up to 10) before stopping transpilation
- Compatible types: `int` → `float` (automatic coercion), `ANY` accepts all types

#### Boxing/Unboxing at Function Boundaries
Primitive ↔ Item conversions at function boundaries:

| Direction | Functions | Use Case |
|-----------|-----------|----------|
| **Boxing** (primitive → Item) | `i2it()`, `l2it()`, `d2it()`, `b2it()`, `s2it()` | Return values from typed functions |
| **Unboxing** (Item → primitive) | `it2i()`, `it2l()`, `it2d()`, `it2b()` | Pass Item args to typed parameters |

#### transpile_box_item: Smart Boxing in MIR Transpiler

The MIR transpiler's `transpile_box_item()` function is a critical gateway that decides **how** to box a sub-expression result into an Item. It must know whether `transpile_expr()` returned a native value (needs boxing) or a boxed Item (return as-is):

| Sub-expression type | transpile_expr returns | transpile_box_item action |
|---|---|---|
| INT literal | native `int64_t` | `emit_box_int()` (inline range check + tag) |
| INT64 literal | boxed Item (via `emit_load_const_boxed`) | return as-is |
| FLOAT literal | boxed Item (via `emit_load_const_boxed`) | return as-is |
| INT + INT binary | native int64 (MIR ADD) | `emit_box_int()` |
| INT / INT binary | native double (MIR DDIV) | `emit_box_float()` |
| INT64 binary (any op) | boxed Item (generic fallback via `fn_add` etc.) | return as-is |
| Comparison (EQ, LT, etc.) | native bool | `emit_box_bool()` |
| System function call | depends on `c_ret_tid` | varies by function |
| Identifier / variable | whatever the variable holds | `emit_box()` by AST type |
| ANY / ERROR / NULL type | boxed Item | return as-is |

**Key challenge**: for INT64 operations, `transpile_expr` sometimes returns raw `int64_t` (literals, fn_int64) and sometimes boxed `Item` (from generic binary fallback). The `push_l_safe()` function was introduced to handle this inconsistency safely.

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

### INT64 Double-Boxing Problem

The core challenge in the MIR transpiler is that `transpile_expr()` returns **inconsistent representations** for INT64 values:

| Source | Returns | Form |
|---|---|---|
| INT64 literal | boxed Item | via `emit_load_const_boxed` |
| `fn_int64(x)` call | raw int64 | via `POST_PROCESS_INT64` unboxing |
| INT64 binary (e.g., `a + b`) | boxed Item | generic fallback through `fn_add` |
| System func returning INT64 | boxed Item | `fn_sum`, `fn_min1`, etc. return Item |

When a boxed INT64 Item is passed to `push_l()` (which expects a raw int64), it allocates a new num_stack entry with the **tagged pointer** as the "value", producing garbage.

**Solution**: `push_l_safe()` detects already-boxed Items by checking the high byte tag before allocating:
```cpp
Item push_l_safe(int64_t val) {
    uint8_t tag = (uint64_t)val >> 56;
    if (tag == LMD_TYPE_INT64) return (Item){.item = (uint64_t)val};  // already boxed
    if (tag == LMD_TYPE_INT)   { /* extract int56, re-box as INT64 */ }
    return push_l(val);  // raw value, box normally
}
```

**False positive range**: raw int64 values in `[2.88e17, 3.60e17]` would have high byte = 4 (LMD_TYPE_INT64), causing `push_l_safe` to treat them as already-boxed. In practice, INT64 values in this range are rare, but this is a known limitation.

### POST_PROCESS_INT64 Macro

When a system function returns a boxed Item (`c_ret_tid == LMD_TYPE_ANY`) but the AST type inference says the result should be INT64 (`call_expr_tid == LMD_TYPE_INT64`), the macro unboxes the result to a raw int64 for consistent native handling in subsequent INT64 operations:

```cpp
#define POST_PROCESS_INT64(result) \
    if (c_ret_tid == LMD_TYPE_ANY && call_expr_tid == LMD_TYPE_INT64) { \
        result = emit_unbox(mt, result, LMD_TYPE_INT64); \
    }
```

### Typed Array Gap

The C2MIR transpiler constructs typed arrays (`ArrayInt`, `ArrayInt64`, `ArrayFloat`) when the element type is known at compile time. The MIR direct transpiler always uses generic `Array*`. This causes behavioral differences in runtime functions like `fn_sum`:

| Array type | `fn_sum` path | Returns |
|---|---|---|
| `ArrayInt` (C2MIR path) | `LMD_TYPE_ARRAY_INT` branch | `push_l(sum)` → INT64 |
| `Array` with INT elements (MIR path) | `LMD_TYPE_ARRAY` branch | Depends on element types |

This mismatch was a source of bugs where `sum([10,20,30])` returned different types depending on which transpiler was used.

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

### BSS Global Variables (MIR Direct)

Module-level `let` variables in the MIR direct transpiler are stored as MIR BSS (Block Started by Symbol) items. This allows functions defined in the same module to access module-level variables:

- A prepass (`prepass_create_global_vars`) scans all top-level `let` nodes and creates BSS items
- `load_global_var` / `store_global_var` emit MIR load/store instructions for BSS items
- An `in_user_func` flag prevents function-internal `let` statements from creating BSS items
- The `GlobalVarEntry` struct maps variable names to their BSS items and type metadata

## MIR Direct Transpiler: Implementation Issues

This section documents issues discovered while implementing the MIR direct transpiler and the solutions adopted. These represent fundamental tensions between Lambda's dynamic type system and MIR's static SSA-based IR.

### Variable Type Widening and Register Type Immutability

**Problem**: In MIR, a register's type is fixed at declaration (e.g., `MIR_T_I64` or `MIR_T_D`). When Lambda code widens a variable's type at runtime — such as an `int` variable being assigned a `float` value — the register type cannot change. This breaks MIR's type expectations:

```lambda
// proc example: variable starts as int, gets assigned float
var n = 10        // MIR register: MIR_T_I64
n = n / 2         // int division → assigns float, but register is still int64
if n <= 1 ...     // MIR_LE on int64 register containing a double → crash
```

**Root cause**: `transpile_assign_stam` detects that the RHS is `FLOAT` but the LHS variable was declared as `INT`. MIR emits `MIR_LE` (integer less-or-equal) on what it thinks is an int64 register, but the value is actually a double bit pattern.

**Solution**: Loop-depth-dependent handling:
- **Inside loops** (`loop_depth > 0`): Truncate float→int via `MIR_D2I` to preserve register type consistency. Loops require stable register types across iterations.
- **Outside loops**: Box the value to `ANY` type via `emit_box` + `MIR_MOV` to a new int64 register. This preserves float precision at the cost of boxing overhead. The variable's `MirVarEntry` is updated: `var->reg = boxed_reg; var->mir_type = MIR_T_I64; var->type_id = LMD_TYPE_ANY`.

**Implication**: MIR register type immutability is a fundamental constraint. Any runtime type widening must either truncate (lossy) or box to ANY (indirect). This is the most architecturally impactful difference from the C transpiler, which uses C variables that can be freely reassigned.

### Bitwise Function Argument Convention Mismatch

**Problem**: Bitwise functions (`fn_band`, `fn_bor`, `fn_bnot`, `fn_shl`, `fn_shr`) expect native `int64_t` arguments, but the MIR transpiler's generic system function dispatch path passes boxed `Item` values via `transpile_box_item`. This produced incorrect results (operations on tagged pointers instead of raw integers).

**Note**: `fn_bxor` worked by coincidence — XOR of two identically-tagged values cancels the tag bits, producing the correct result.

**Solution**: Added dedicated handling in `transpile_call` for bitwise functions (before the generic sys func dispatch). These functions use `transpile_expr` (native values) instead of `transpile_box_item` (boxed Items). If an argument's effective type is `ANY` (e.g., a captured variable), it is unboxed via `emit_unbox` before the call.

**Underlying issue**: The `SysFuncInfo` table does not distinguish between functions that take boxed Items and functions that take native C types. A `NativeArgConvention` field would eliminate this class of bugs (see Suggestion #8).

### Closure Mutable Capture and Env Write-Back

**Problem**: Closures capture variables via an env struct allocated at closure creation time. When a captured variable is mutated inside the closure body, the env struct must be updated — otherwise the mutation is lost when the closure returns.

Three sub-issues were discovered:

#### 1. Missing env write-back on assignment
After `var x = new_value` inside a closure, the new value was stored only in the local MIR register. The env struct still held the old value, so subsequent calls to the closure (or other closures sharing the same env) saw stale data.

**Solution**: Added `env_offset` field to `MirVarEntry` (`-1` = not captured, `≥0` = byte offset in env struct). After each assignment to a captured variable, the transpiler emits:
```
boxed = emit_box(mt, val, type_id)
MIR_MOV  *(env_ptr + env_offset) = boxed
```

#### 2. Boxing mismatch: typed value → ANY variable
Captured variables stored in the env struct are always boxed `Item` values (type `ANY`). When assigning a typed native value (e.g., an `int64_t`) to an ANY variable, the transpiler must box it first. Without this, a raw int64 was stored directly into an Item slot, producing a value with no type tag.

**Solution**: Added an explicit `var_tid == ANY && val_tid != ANY` path in `transpile_assign_stam` that boxes the value before the MOV.

#### 3. Register aliasing in let bindings
`let tmp = a` shared the same MIR register between `tmp` and `a`. When `a` was subsequently mutated (`a = b`), `tmp` was also affected because both names pointed to the same register.

**Solution**: `transpile_let_stam` now copies the value to a new register via `MIR_MOV` (int64) or `MIR_DMOV` (double), ensuring each variable has its own storage.

### Variable Scoping in If Branches

**Problem**: Variables declared inside `if`/`else` branches leaked into the outer scope, causing name collisions. For example:

```lambda
let y = 100
if condition
  let y = 200    // should shadow outer y, not overwrite it
y                // should be 100, not 200
```

Without scope isolation, `let y = 200` in the then-branch overwrote the outer `y` entry in the variable table, and the outer scope saw 200 after the if-statement.

**Solution**: `transpile_if` now calls `push_scope(mt)` before and `pop_scope(mt)` after each branch (both then and else). The scope stack uses a depth counter in the var table, and `pop_scope` removes entries added at the inner depth.

### get_effective_type: Runtime vs AST Types

**Problem**: The AST records the *declared* type of each expression node, but runtime operations can change a variable's effective type (e.g., type widening, captured variable boxing). Using the AST type for code generation decisions after mutations leads to incorrect boxing/unboxing.

**Example**: A variable declared as `int` but widened to `ANY` after assignment still has `LMD_TYPE_INT` in its AST node. If the transpiler uses this to decide `emit_box_int`, it applies integer boxing to what is actually a boxed Item, producing garbage.

**Solution**: `get_effective_type()` checks the variable's `MirVarEntry::type_id` for `IDENT` nodes, which reflects the *current* runtime type after any mutations. This is the authoritative type for code generation decisions:

```cpp
TypeId get_effective_type(MirTranspiler* mt, AstNode* node) {
    TypeId tid = get_type_id(node->type);  // AST-declared type
    if (node->node_type == AST_NODE_IDENT) {
        MirVarEntry* v = find_var(mt, node->str_val);
        if (v && v->type_id == LMD_TYPE_ANY) return LMD_TYPE_ANY;
    }
    return tid;
}
```

### Proc Context Detection

**Problem**: Procedural scripts use `pn main()` with imperative statements and mutable variables. The transpiler must handle `var` declarations, assignment statements, and multi-statement function bodies differently from pure functional expressions.

**Solution**: Added `in_proc` flag to `MirTranspiler`. Detection is two-fold:
1. `transpile_func_def` sets `in_proc = true` when processing a `pn` (procedure) definition
2. `transpile_content` scans top-level nodes for `VAR_STAM` to detect implicit proc context

In proc context, `transpile_content` returns only the last value expression (ignoring intermediate statement results), matching the C transpiler's behavior.

## Typed Array Construction

### Array Type Hierarchy

```
Container (TypeId)
├── Array       (LMD_TYPE_ARRAY = 17)     — generic: each element is a boxed Item
├── ArrayInt    (LMD_TYPE_ARRAY_INT = 14)  — specialized: int64_t elements (int56 values stored as int64)
├── ArrayInt64  (LMD_TYPE_ARRAY_INT64 = 15) — specialized: int64_t elements (full 64-bit)
└── ArrayFloat  (LMD_TYPE_ARRAY_FLOAT = 16) — specialized: double elements
```

All share the same struct layout: `TypeId`, `items*`, `length`, `extra`, `capacity`.

### Construction APIs

| Function | Constructs | Element type |
|---|---|---|
| `array()` | generic `Array*` | boxed `Item` |
| `array_int()` | `ArrayInt*` | `int64_t` (int56 values) |
| `array_int64()` | `ArrayInt64*` | `int64_t` (full range) |
| `array_float()` | `ArrayFloat*` | `double` |
| `array_fill(arr, n, v1, v2, ...)` | fills generic Array | boxed Items |
| `array_int_fill(arr, n, v1, v2, ...)` | fills ArrayInt | raw int64 values |
| `array_int64_fill(arr, n, v1, v2, ...)` | fills ArrayInt64 | raw int64 values |
| `array_float_fill(arr, n, v1, v2, ...)` | fills ArrayFloat | raw double values |

### Type Selection at Compile Time

The C2MIR transpiler checks `TypeArray::nested->type_id` at compile time to select the appropriate typed array constructor:

```cpp
bool is_int_array   = nested->type_id == LMD_TYPE_INT;     // → array_int()
bool is_int64_array = nested->type_id == LMD_TYPE_INT64;   // → array_int64()
bool is_float_array = nested->type_id == LMD_TYPE_FLOAT;   // → array_float()
// otherwise → generic array()
```

### Impact on Runtime Behavior

System functions dispatch on the runtime TypeId of arrays. Using generic `Array*` vs typed arrays leads to different code paths in functions like `fn_sum`, `fn_min1`, `fn_max1`.  The typed array paths are generally simpler and more correct because elements are stored in their native C type, avoiding boxing/unboxing ambiguities.

---

## Suggestions: Making the Runtime More Structured and Easier to Transpile

Based on implementing the MIR direct transpiler to feature-completeness (113/113 tests passing), debugging INT64 boxing issues, closure mutation, type widening, and reconciling behavior between the two transpiler paths, here are architectural improvements that would reduce friction for transpiler authors and eliminate classes of bugs.

### 1. Establish a Canonical Value Representation Contract

**Problem**: `transpile_expr()` in the MIR direct path returns either a raw native value (int64, double) or a boxed Item depending on the expression form. The caller must track which form it received, often incorrectly.

**Suggestion**: Define a clear contract for each expression's return representation:

| Expression type | Returns | Guaranteed by |
|---|---|---|
| INT literal, INT binary op | raw `int64_t` | transpile_expr |
| INT64 literal, INT64 binary op | boxed `Item` (tagged INT64) | transpile_expr |
| FLOAT literal, FLOAT binary op | raw `double` | transpile_expr |
| System func call | `Item` (always boxed) | fn_* functions |
| Variable load | matches variable's declared type | transpile_expr |

The current issue is that INT64 sometimes produces raw int64 (literals) and sometimes boxed Item (binary ops via generic fallback). Pick one and be consistent. Recommendation: **always return boxed Item for INT64**, since most system functions already return boxed Items, and `push_l_safe` exists as a safety net.

### 2. Data-Driven C Return Type in SysFuncInfo

**Problem**: The transpiler uses a hardcoded `switch` in `transpile_box_item()` to decide how to box system function return values (e.g., `fn_add` returns int64 when both args are INT, Item otherwise). Adding or changing a system function requires updating this switch.

**Suggestion**: Extend `SysFuncInfo` with a `c_ret_type` field that precisely describes the C-level return semantics:

```cpp
enum CRetType {
    C_RET_ITEM,     // returns boxed Item (default, safe)
    C_RET_INT64,    // returns raw int64_t (needs emit_box_int64 to produce Item)
    C_RET_DOUBLE,   // returns raw double (needs emit_box_float to produce Item)
    C_RET_BOOL,     // returns raw int64_t 0/1 (needs emit_box_bool)
    C_RET_ADAPTIVE, // return type depends on argument types (fn_add, fn_mul, etc.)
};
```

For `C_RET_ADAPTIVE` functions, a separate per-function handler can inspect argument TypeIds and determine the actual return type. This moves the logic from ad-hoc switch cases into a structured, extensible system.

### 3. Typed Array Construction in MIR Direct Transpiler

**Problem**: The MIR direct transpiler always creates generic `Array*`, even when element types are known at compile time. This causes runtime behavior differences with the C2MIR path.

**Suggestion**: Port the typed array construction from the C2MIR path. When `TypeArray::nested->type_id` is known:
- Emit `array_int()` / `array_int_fill()` for INT elements
- Emit `array_int64()` / `array_int64_fill()` for INT64 elements
- Emit `array_float()` / `array_float_fill()` for FLOAT elements
- Fall back to generic `array()` otherwise

This eliminates behavioral divergence and enables the faster typed-array code paths in runtime functions.

### 4. Idempotent Boxing for All Numeric Types

**Problem**: `push_l_safe` was created as a workaround for INT64 double-boxing. The same class of bug could affect FLOAT and DATETIME boxing in the future if the MIR direct transpiler's value tracking is imprecise.

**Suggestion**: Create `push_d_safe()` and `push_k_safe()` analogous to `push_l_safe()`:

```cpp
Item push_d_safe(double val) {
    // Check if val's int64 reinterpretation has LMD_TYPE_FLOAT tag
    uint64_t bits;
    memcpy(&bits, &val, 8);
    uint8_t tag = bits >> 56;
    if (tag == LMD_TYPE_FLOAT) return (Item){.item = bits};  // already boxed
    return push_d(val);
}
```

Note: For doubles, bit patterns in the NaN space could cause false positives. An alternative is a registry-based approach where `push_d` records recently allocated pointers for quick lookup.

### 5. Uniform Runtime Function Signatures

**Problem**: Some runtime functions return native types (e.g., `fn_len` returns `int64_t`), while others return boxed Items. The transpiler must know each function's return convention to handle results correctly.

**Suggestion**: Standardize on **two categories** of runtime function signatures:

| Category | Signature | When to use |
|---|---|---|
| **Item functions** | `Item fn_foo(Item a, Item b, ...)` | Default. Safe, handles any type. |
| **Native functions** | `int64_t fn_foo_i(int64_t a)` | Performance-critical, type-specialized. |

For each function that currently returns a native type, provide a parallel `_item` variant that returns boxed Item. The transpiler can then always call the `_item` variant for safety, or call the native variant when it can prove the types match. This decouples correctness from optimization.

### 6. Centralized Type Narrowing Table

**Problem**: Type narrowing logic (e.g., "if both args to fn_add are INT, result is INT") is scattered across both transpilers in ad-hoc switch/if chains.

**Suggestion**: Create a centralized narrowing table:

```cpp
struct TypeNarrowEntry {
    SysFunc func_id;
    TypeId  arg1_type;
    TypeId  arg2_type;
    TypeId  result_type;
    CRetType c_ret;
};
```

Both transpilers consult this table to determine the output type and C return convention for any system function call. Changes to narrowing rules require editing only one table, not two codebases.

### 7. Runtime-Validated Boxing in Debug Mode

**Problem**: Boxing bugs are silent — they produce incorrect values rather than crashes, making them hard to detect.

**Suggestion**: In debug builds, add validation assertions to boxing functions:

```cpp
Item push_l_debug(int64_t val) {
    // Assert val doesn't already have a TypeId tag in the high byte
    assert((uint64_t)val >> 56 == 0 && "push_l called with already-tagged value");
    return push_l(val);
}
#ifdef DEBUG
#define push_l(val) push_l_debug(val)
#endif
```

This catches double-boxing at the point of occurrence rather than downstream when wrong values appear.

### 8. Native Argument Convention in SysFuncInfo

**Problem**: Most system functions accept boxed `Item` arguments, but some (notably bitwise functions `fn_band`, `fn_bor`, `fn_bnot`, `fn_shl`, `fn_shr`) expect native `int64_t` arguments. The transpiler has no way to distinguish these two conventions from the `SysFuncInfo` table, requiring hardcoded special-case handling for each such function.

**Discovered via**: Bitwise operators produced incorrect results because the generic dispatch path boxed arguments before passing them. `fn_bxor` worked by coincidence (XOR of identically-tagged values cancels the tag bits).

**Suggestion**: Add a `c_arg_convention` field to `SysFuncInfo`:

```cpp
enum CArgConvention {
    C_ARG_ITEM,     // arguments are boxed Items (default)
    C_ARG_NATIVE,   // arguments are native C types (int64_t, double)
};
```

The transpiler would consult this field instead of maintaining a list of special-cased function names. This is orthogonal to `c_ret_type` (Suggestion #2) — a function can take native args and return a boxed Item, or vice versa.

### 9. First-Class Variable Type Tracking

**Problem**: The MIR transpiler tracks variable types in `MirVarEntry::type_id`, but this is separate from the AST's type annotations. After mutations (type widening, closure capture boxing), the AST type becomes stale. The `get_effective_type()` function bridges this gap, but it only checks for `ANY` — it doesn't handle all possible type changes.

**Discovered via**: After type widening from INT to ANY (outside loops), subsequent code still used the AST's INT type for boxing decisions, producing `emit_box_int` on an already-boxed Item.

**Suggestion**: Make variable type tracking first-class with a unified `VarTypeState`:

```cpp
struct VarTypeState {
    TypeId declared_type;  // original AST declaration
    TypeId current_type;   // updated after each assignment
    MIR_type_t mir_type;   // MIR register type (fixed after declaration)
    bool is_captured;      // part of a closure env
    int env_offset;        // byte offset in env struct (-1 if not captured)
};
```

All type-dependent decisions (boxing, unboxing, comparison instruction selection) should consult `current_type` rather than the AST node's type. This eliminates the need for `get_effective_type()` as a separate concern and makes type tracking explicit.

### 10. Document MIR Register Type Constraints

**Problem**: MIR registers have a fixed type at declaration (`MIR_T_I64` or `MIR_T_D`). This is an inherent SSA constraint, but it has far-reaching consequences for Lambda's dynamic typing that are not obvious to developers working on the transpiler for the first time.

**Key constraints**:
- A variable declared as `int` (register type `MIR_T_I64`) cannot later hold a `double` natively
- Loop variables must maintain stable register types across iterations (no widening inside loops)
- Type widening outside loops requires boxing to `ANY` (the variable's register type changes to `MIR_T_I64` holding a boxed Item)
- The C transpiler has no equivalent constraint — C variables can be freely reassigned

**Suggestion**: Add a "MIR Register Type Constraints" reference section to this document (or as a comment block in `transpile-mir.cpp`) that explicitly lists these constraints. New code that assigns to variables should always check for type mismatches and route through the appropriate widening path. Consider adding a `WIDENING_ASSERT` macro:

```cpp
#define WIDENING_ASSERT(var, val_tid) \
    do { if (var->type_id != LMD_TYPE_ANY && var->type_id != val_tid) \
        log_error("mir: type widening %s: %d -> %d", var_name, var->type_id, val_tid); \
    } while(0)
```

### Summary of Priorities

| Priority | Improvement | Impact | Effort |
|---|---|---|---|
| **High** | Typed arrays in MIR Direct (#3) | Eliminates behavioral divergence | Medium |
| **High** | Data-driven C return type (#2) | Simplifies transpiler, prevents bugs | Medium |
| **High** | Debug-mode boxing validation (#7) | Catches bugs early | Low |
| **High** | Native arg convention (#8) | Eliminates bitwise/native arg bugs | Low |
| **Medium** | First-class variable type tracking (#9) | Prevents stale type bugs | Medium |
| **Medium** | Canonical value representation (#1) | Reduces INT64 confusion | High (refactor) |
| **Medium** | Centralized type narrowing (#6) | Single source of truth | Medium |
| **Medium** | Document register type constraints (#10) | Prevents widening bugs | Low |
| **Low** | Idempotent boxing (#4) | Safety net | Low |
| **Low** | Uniform function signatures (#5) | Cleaner API | High (many functions) |
