# Proposal: Direct Struct Access for Typed Maps and Objects

## Problem Statement

Currently, every map/object field read or write goes through a **runtime linear scan** of the `ShapeEntry` linked list, comparing field names with `strncmp`:

```c
// map_get: O(n) linear scan per field access
ShapeEntry *field = map_type->shape;
while (field) {
    if (strncmp(field->name->str, key, field->name->length) == 0 && strlen(key) == field->name->length) {
        return _map_read_field(field, map_data);
    }
    field = field->next;
}
```

This applies to:
- **Field reads**: `v.a` → `map_get(v, "a")` → scans ShapeEntry list
- **Field writes**: `v.a = 1` → `fn_map_set(v_item, "a", 1)` → scans ShapeEntry list
- **Object method field loads**: each `fn_member(_self_item, "field")` per field at method entry
- **Object method field write-backs**: each `fn_map_set(_self_item, "field", value)` per assignment

For a map with N fields, each access is O(N). In hot loops or methods that touch many fields, this becomes a significant cost.

## Proposed Solution: Compile-Time Struct Offset Resolution

When the transpiler knows the **exact type** of a map or object at compile time (via type annotation or inference), it should emit **direct byte-offset reads/writes** to the packed data struct, bypassing the runtime field scan entirely.

### Core Insight

Lambda maps are already stored as **packed C structs** at runtime. The `ShapeEntry` linked list defines the layout, and `byte_offset` is pre-computed. The field data lives at `(char*)map->data + field->byte_offset`. The transpiler already knows the full `TypeMap`/`TypeObject` shape at compile time — it just doesn't use that knowledge for code generation.

### What Changes

Instead of emitting calls to `map_get()`/`fn_member()`/`fn_map_set()`, the transpiler emits **inline C code** that reads/writes directly to the packed struct at the known byte offset.

## Detailed Design

### Phase 1: C Struct Type Emission

For each declared map/object type, emit a corresponding C struct definition in the transpiled code header.

**Lambda source:**
```lambda
type Point = {x: float, y: float}
type Person = {name: string, age: int, score: float}
```

**Generated C (in `_transpiled_*.c` preamble):**
```c
// struct for type Point (type_index=2)
typedef struct _type_Point {
    double x;       // offset 0, LMD_TYPE_FLOAT
    double y;       // offset 8, LMD_TYPE_FLOAT
} _type_Point;

// struct for type Person (type_index=3)
typedef struct _type_Person {
    String* name;   // offset 0,  LMD_TYPE_STRING
    int64_t age;    // offset 8,  LMD_TYPE_INT
    double score;   // offset 16, LMD_TYPE_FLOAT
} _type_Person;
```

> **Naming convention**: Struct names use `_type_` prefix + the original Lambda type name (e.g., `_type_Point`, `_type_Person`). This makes transpiled C code easy to read and debug. For anonymous map types (no declared name), fall back to `_type_N` (type_index).

**Field type → C struct member type mapping** (same as existing Lambda → C mapping):

| Lambda Field Type | C Struct Member | Size |
|---|---|---|
| `bool` | `bool` (+ 7 bytes padding) | 8 bytes |
| `int` | `int64_t` | 8 bytes |
| `int64` | `int64_t` | 8 bytes |
| `float` | `double` | 8 bytes |
| `datetime` | `DateTime` | 8 bytes |
| `string` | `String*` | 8 bytes |
| `symbol` | `Symbol*` | 8 bytes |
| `decimal` | `Decimal*` | 8 bytes |
| `binary` | `Binary*` | 8 bytes |
| `list`, `array`, `map`, `element`, etc. | `void*` (container pointer) | 8 bytes |
| `any` / untyped | `TypedItem` (16 bytes: TypeId + Item) | 16 bytes |

> **Alignment**: All fields are 8-byte aligned in the existing packed struct layout. The C struct definition must match exactly — use `_Static_assert(sizeof(_type_Point) == byte_size)` to verify.

### Phase 2: Direct Read — Field Access (`v.field`)

**Current codegen** (`transpile_member_expr`):
```c
// v.x where v : Point
map_get(v, const_s2it(5))    // runtime: scan ShapeEntry list for "x"
```

**Proposed codegen:**
```c
// v.x where v : Point — direct struct access
push_d(((_type_Point*)v->data)->x)  // zero-cost: known offset, known type
```

**Type-specific read emission rules:**

| Field Type | Generated Read Expression | Notes |
|---|---|---|
| `int` | `i2it(st->field)` | Inline int56 boxing |
| `int64` | `push_l(st->field)` | GC nursery allocation |
| `float` | `push_d(st->field)` | GC nursery allocation |
| `bool` | `b2it(st->field)` | Inline bool boxing |
| `string` | `s2it(st->field)` | Tag pointer |
| `symbol` | `y2it(st->field)` | Tag pointer |
| `decimal` | `c2it(st->field)` | Tag pointer |
| container types | `{.container = st->field}` | Direct pointer, no boxing |

Where `st` is `((_type_Point*)map_ptr->data)`.

**When transpiling in unboxed context** (e.g., `float` variable receiving `v.x` where x is `float`):
```c
// let dx: float = v.x
double _dx = ((_type_Point*)v->data)->x;   // no boxing at all!
```

### Phase 3: Direct Write — Field Assignment (`v.field = expr`)

**Current codegen** (`transpile_member_assign_stam`):
```c
// v.x = 3.14 where v : Point
fn_map_set(v_item, const_s2it(5), push_d(3.14))  // runtime: scan, type-check, store
```

**Proposed codegen:**
```c
// v.x = 3.14 where v : Point — direct struct write
((_type_Point*)v->data)->x = ((double)(3.14));
```

**Type-specific write emission rules:**

| Field Type | Generated Write | Notes |
|---|---|---|
| `int` | `st->field = it2i(expr)` or `st->field = val` | Unbox if RHS is Item |
| `float` | `st->field = it2d(expr)` or `st->field = val` | Unbox if RHS is Item |
| `bool` | `st->field = it2b(expr)` or `st->field = val` | Unbox if RHS is Item |
| `string` | `st->field = it2s(expr)` or `st->field = ptr` | Pointer assignment |
| container types | `st->field = (T*)expr.container` | Pointer cast |

When both sides are typed, no boxing/unboxing is needed at all — just a native C assignment.

### Phase 4: Map Literal Construction

**Current codegen** (`transpile_map_expr`):
```c
// let v: Point = {x: 1.0, y: 2.0}
({Map* m = map(2); map_fill(m, push_d(1.0), push_d(2.0)); m;})
```

`map_fill` uses a va_list to iterate ShapeEntry fields and store values — another O(N) scan.

**Proposed codegen:**
```c
// let v: Point = {x: 1.0, y: 2.0} — direct struct init
({Map* m = map(2);
  m->data = heap_data_calloc(sizeof(_type_Point));
  _type_Point* _st = (_type_Point*)m->data;
  _st->x = ((double)(1.0));
  _st->y = ((double)(2.0));
  m;})
```

This eliminates the varargs overhead of `map_fill` and the per-field type-switch in `set_fields`.

### Phase 5: Object Method Field Loading

**Current codegen** (method preamble in `define_func`):
```c
// for each field in object type:
Item _self_item = (uint64_t)(uintptr_t)_self;
double _x = it2d(fn_member(_self_item, s2it(heap_create_name("x"))));
double _y = it2d(fn_member(_self_item, s2it(heap_create_name("y"))));
```

Each field load: `heap_create_name` (allocation) → `s2it` (tag) → `fn_member` (linear scan) → `it2d` (unbox). For an object with 10 fields, that's 10 allocations + 10 scans.

**Proposed codegen:**
```c
Item _self_item = (uint64_t)(uintptr_t)_self;
Object* _self_obj = (Object*)_self;
_type_Point* _self_data = (_type_Point*)_self_obj->data;
double _x = _self_data->x;
double _y = _self_data->y;
```

Zero allocations, zero scans, zero boxing. Direct native reads.

### Phase 6: Object Method Field Write-Back

**Current codegen** (assignment in method body via `pn_method_obj_type`):
```c
// self.x = new_val (inside pn method)
fn_map_set(_self_item, s2it(heap_create_name("x")), push_d(_x));
```

**Proposed codegen:**
```c
// self.x = new_val — direct write-back
_self_data->x = _x;
```

### Phase 7: Function Parameter Passing (Typed Map Params)

When a function parameter has a known map/object type, the callee can use direct struct access on the argument.

```lambda
fn distance(a: Point, b: Point) => sqrt((a.x - b.x)^2 + (a.y - b.y)^2)
```

**Current codegen:**
```c
Item _distance(Item a, Item b) {
    return push_d(sqrt(
        fn_pow_u(it2d(fn_member(a, s2it(heap_create_name("x")))) - it2d(fn_member(b, s2it(heap_create_name("x")))), 2.0) +
        fn_pow_u(it2d(fn_member(a, s2it(heap_create_name("y")))) - it2d(fn_member(b, s2it(heap_create_name("y")))), 2.0)
    ));
}
```

**Proposed codegen:**
```c
Item _distance(Map* a, Map* b) {
    _type_Point* _a = (_type_Point*)a->data;
    _type_Point* _b = (_type_Point*)b->data;
    return push_d(sqrt(
        fn_pow_u(_a->x - _b->x, 2.0) +
        fn_pow_u(_a->y - _b->y, 2.0)
    ));
}
```

No name lookups, no boxing/unboxing — pure arithmetic.

## Applicability Rules

The optimization applies when **all** of these conditions are met:

| Condition | Rationale |
|---|---|
| Variable has a **declared map/object type** (via annotation or inference) | Must know the exact shape at compile time |
| The type has a **fixed shape** (no spread entries, no dynamic fields) | Spread entries (`...other`) require runtime resolution |
| The field name is a **compile-time constant** (identifier, not computed) | Dynamic field names (`m[key]`) must still use runtime lookup |
| The map/object is **not widened to Item** | Widened vars may hold any type at runtime |

### When to Fall Back to Runtime

The transpiler **must** fall back to `map_get`/`fn_map_set`/`fn_member` when:
- The variable type is `any` or unknown
- Field access uses a computed key: `m[some_var]`
- The map type contains spread entries (`...rest`)
- The map was created via `input()` (external data — shape may not match the declared type)
- The variable was type-widened due to inconsistent assignments

## Implementation Plan

### Step 1: Emit C Struct Definitions

**Files**: `transpile.cpp` (new helper function)

Add `emit_struct_typedef(Transpiler* tp, TypeMap* map_type)` that:
1. Iterates the `ShapeEntry` linked list
2. Emits `typedef struct _type_Name { ... } _type_Name;` with proper C types (using the Lambda type name)
3. Adds `_Static_assert(sizeof(_type_Name) == byte_size)` for safety
4. Call this during `transpile_ast_root` for each registered map/object type

### Step 2: Modify `transpile_member_expr`

**Files**: `transpile.cpp`

In the `LMD_TYPE_MAP` / `LMD_TYPE_OBJECT` branches:
1. Check if the object's type has a known `TypeMap*` with fixed shape
2. Look up the field name in the shape at **compile time** (in the transpiler, not in generated code)
3. If found, emit direct `((_type_Name*)obj->data)->field` instead of `map_get()`/`fn_member()`
4. Apply unboxing or boxing as needed based on context

### Step 3: Modify `transpile_member_assign_stam`

**Files**: `transpile.cpp`

In the assignment path:
1. Check if the target object has a typed map/object type
2. Look up the field in the shape at compile time
3. Emit direct `((_type_Name*)obj->data)->field = value` instead of `fn_map_set()`

### Step 4: Modify `transpile_map_expr` / `transpile_object_expr`

**Files**: `transpile.cpp`

When the map literal is typed:
1. Emit `heap_data_calloc(sizeof(_type_N))` for data allocation
2. Cast to `_type_N*` and assign each field directly
3. Skip `map_fill` / `object_fill`

### Step 5: Modify Method Preamble and Write-Back

**Files**: `transpile.cpp` (`define_func` method section)

1. Replace `fn_member(_self_item, ...)` field loads with `_self_data->field` reads
2. Replace `fn_map_set(_self_item, ...)` write-backs with `_self_data->field = val` writes

### Step 6: Modify Function Parameter Handling

**Files**: `transpile.cpp` (`define_func` parameter section)

When a function parameter has a known map/object type:
1. Accept `Map*` or `Object*` instead of `Item` for that parameter
2. Cast `param->data` to `_type_N*` at function entry
3. All field accesses within the function body use direct struct access

> **Note**: This requires matching changes at call sites to pass the map pointer unboxed. The call site already has the map pointer when the argument is typed.

## Performance Impact

| Operation | Current Cost | Proposed Cost | Improvement |
|---|---|---|---|
| Field read (`v.x`) | `heap_create_name` + `s2it` + O(N) scan + type-switch + box | pointer deref + box (or no box if unboxed context) | ~10-50x faster |
| Field write (`v.x = val`) | `heap_create_name` + `s2it` + O(N) scan + `strncmp` + type-check + store | pointer deref + store | ~10-50x faster |
| Map construction | `map_fill` + va_list + N× type-switch | N direct assignments | ~3-5x faster |
| Method field load (N fields) | N× (`heap_create_name` + scan + unbox) | 1 pointer cast + N reads | ~20-100x faster |
| Method field write-back | N× (`heap_create_name` + scan + box + store) | N direct writes | ~20-100x faster |

## Compatibility

- **Runtime data format unchanged**: The packed struct layout in `map->data` is identical. Direct struct access reads/writes the same bytes as `_map_read_field`/`map_field_store`.
- **Type metadata unchanged**: `TypeMap`, `ShapeEntry`, `byte_offset` all stay the same.
- **Fallback preserved**: Untyped maps, dynamic access, and spread maps continue using the existing runtime path.
- **No new runtime functions needed**: This is purely a transpiler-level optimization.
- **MIR direct transpiler**: The same optimization can be applied to `transpile-mir.cpp` as a follow-up, using MIR memory load/store instructions at known offsets.

## Design Decisions

### 1. Nested Typed Maps — Supported

For nested typed maps, chained direct struct access is emitted when both the outer and inner types are statically known.

**Lambda source:**
```lambda
type Inner = {x: float, y: float}
type Outer = {pos: Inner, name: string}

let o: Outer = {pos: {x: 1.0, y: 2.0}, name: "test"}
o.pos.x   // chained access
```

**Generated C:**
```c
// o.pos → direct read of Inner* from Outer's packed struct
// o.pos.x → direct read of double from Inner's packed struct
((_type_Inner*)((Map*)((_type_Outer*)o->data)->pos)->data)->x
```

The transpiler resolves each `.field` step at compile time:
1. `o.pos` → look up `pos` in `Outer`'s shape → field type is `Inner` (a `TypeMap`) → emit struct read for `Inner*`
2. `.x` → look up `x` in `Inner`'s shape → field type is `float` → emit struct read for `double`

Each step is a known-offset pointer dereference. For deeply nested types (`a.b.c.d`), the chain simply extends — still O(1) per field, no runtime scans at any level.

**Applicability**: The nested type must also be a declared type with a fixed shape. If any intermediate field is `any` or an untyped map, the chain breaks and falls back to `map_get()`/`fn_member()` from that point onward.

### 2. Input Data Casting — Future Enhancement (Slow Path for Now)

Maps returned by `input()` are constructed by input parsers (JSON, XML, YAML, etc.) which build shapes dynamically from the input data. The resulting shape may not match a declared type — fields could be missing, have different types, or appear in a different order.

**Current rule**: Maps originating from `input()` always use the **slow path** (`map_get`/`fn_member`/`fn_map_set`).

**Future enhancement**: The input parsers will support schema-directed parsing, where the parser constructs maps according to a declared type definition:
```lambda
let data: MySchema = input("data.json", {schema: MySchema})
```

When schema-directed parsing is used, the parser guarantees the packed struct layout matches the declared type, and the transpiler can safely apply direct struct access. Until then, `input()` results must use runtime field lookup.

**Implementation note**: The transpiler can track data provenance — variables assigned from `input()` calls (or derived from them without explicit type annotation) are marked as "input-sourced" and excluded from the fast path.

### 3. Mutable Field Type Changes — Type-Compatible Fast Path

In procedural (`pn`) code, field assignments that **change the field type** trigger a shape rebuild at runtime (`map_rebuild_for_type_change` in `fn_map_set`). Direct struct access cannot handle this because the struct layout changes.

**Rule**: The transpiler emits direct struct writes only when the assignment is **type-compatible** (same type or safe coercion). When a type change is detected at compile time, it falls back to `fn_map_set()`.

| Assignment | Field Type | Value Type | Path |
|---|---|---|---|
| `v.x = 42` | `int` | `int` | **Fast** — direct write |
| `v.x = 3.14` | `float` | `float` | **Fast** — direct write |
| `v.x = 3.14` | `float` | `int` | **Fast** — int→float widening (lossless) |
| `v.x = 42` | `int` | `int64` | **Fast** — INT↔INT64 safe (same byte size) |
| `v.x = "hello"` | `int` | `string` | **Slow** — type change, fall back to `fn_map_set()` |
| `v.x = val` | `int` | `any` | **Slow** — RHS type unknown, fall back to `fn_map_set()` |

For `var` declarations where the assigned value type is known at compile time and matches the field type, the fast path applies. When the RHS type is `any` or differs from the field type beyond safe coercions, the slow path is used.

**Immutable `let` bindings**: Always safe for fast path reads (the shape never changes after construction).

### 4. Memory Alignment

The current packed struct is already 8-byte aligned per field. The generated C struct must match exactly. A compile-time assertion is emitted to verify:

```c
_Static_assert(sizeof(_type_Point) == byte_size, "struct size mismatch");
```

---

## Implementation Status

### Completed: Phase 2 (Direct Read) and Phase 3 (Direct Write) — Partial

The initial implementation of direct byte-offset reads and writes is live and passing all 467 baseline tests. Rather than emitting named C struct typedefs (Phase 1), the current approach uses **inline byte-offset arithmetic** directly against `map->data`, which achieves the same zero-overhead field access without requiring struct definitions in the transpiled preamble.

#### Files Modified

| File | Changes |
|---|---|
| `lambda/lambda-data.hpp` | Added `const char* struct_name` field to `TypeMap` — stores the type declaration name (e.g., `"Point"`) for eligibility gating |
| `lambda/lambda.h` | Added `Map`, `Object`, `Element` C struct body definitions (inside `#ifndef __cplusplus` block) so generated C code can access `->data` member |
| `lambda/build_ast.cpp` | In `build_assign_expr`: when building a type definition (`type Name = {…}`), propagates the declaration name to `TypeMap::struct_name` |
| `lambda/transpile.cpp` | Core optimization — 7 new helper functions + modifications to `transpile_member_expr` and `transpile_member_assign_stam` |

#### Helper Functions Added (`transpile.cpp`)

| Function | Purpose |
|---|---|
| `resolve_field_type_id(ShapeEntry*)` | Unwraps `TypeType` wrappers on shape entries (type-defined maps store `LMD_TYPE_TYPE` not the inner type) to get the actual data type |
| `find_shape_field_by_name(TypeMap*, name, len)` | Compile-time field lookup by name in the ShapeEntry linked list |
| `has_fixed_shape(TypeMap*)` | Eligibility gate — requires `struct_name != NULL` (named type), all fields named (no spreads), all byte offsets 8-byte aligned |
| `is_direct_access_type(TypeId)` | Returns true for all concrete types (bool, int, float, string, containers, etc.); false for ANY, NULL, ERROR |
| `expr_produces_native_ptr(AstNode*)` | Returns true only for simple variable references or map/object literal expressions — guards against applying `->data` on `Item` (uint64_t) values from parent/member expressions |
| `emit_direct_field_read(Transpiler*, object, field)` | Emits type-specific inline read: e.g., `i2it(*(int64_t*)((char*)(obj)->data+OFFSET))` |
| `emit_direct_field_write(Transpiler*, object, field, value)` | Emits type-specific inline write: e.g., `*(int64_t*)((char*)(obj)->data+OFFSET)=it2i(VALUE)` |

#### Generated Code Example

**Lambda source:**
```lambda
type Point = {x: int, y: int}
let p: Point = {x: 10, y: 20}
p.x + p.y
```

**Generated C (before optimization):**
```c
list_push_spread(ls, fn_add(map_get(_p, const_s2it(0)), map_get(_p, const_s2it(1))));
```

**Generated C (after optimization):**
```c
list_push_spread(ls, fn_add(i2it(*(int64_t*)((char*)(_p)->data+0)), i2it(*(int64_t*)((char*)(_p)->data+8))));
```

Both `map_get` calls (each doing O(N) linear scan + `strncmp` + type-switch) are replaced with direct memory reads at known byte offsets.

#### Eligibility Constraints (Current Implementation)

The optimization only fires when **all** of these conditions are met:

1. **Named type** — `TypeMap::struct_name` is non-NULL (from a `type Name = {…}` declaration). This excludes anonymous map literals whose field types can change at runtime via `fn_map_set` shape rebuild.
2. **Fixed shape** — all fields are named (no spread entries) and all `byte_offset` values are multiples of 8 (sizeof(void*)).
3. **Simple variable reference** — the object expression is a direct variable name or map/object literal. Chained member accesses (`a.b.c`), parent expressions, and function call results produce `Item` values (uint64_t), not native pointers, so `->data` cannot be applied.
4. **Concrete field type** — the resolved field type is not ANY, NULL, or ERROR.

When any condition fails, the transpiler falls back to the existing runtime path (`map_get` / `fn_member` / `fn_map_set`).

#### Bugs Found and Fixed During Implementation

1. **MIR unaligned access** — Map literals use packed byte offsets based on `type_info[type_id].byte_size` (e.g., bool = 1 byte), so field offsets like 42 are not 8-byte aligned. MIR's instruction selector requires aligned memory operands and crashed with `fatal failure in matching insn: mov hr0, u64:42(hr0):pc`. **Fix**: `has_fixed_shape()` rejects types with unaligned offsets.

2. **Item vs native pointer confusion** — Parent expressions (`node..attr`) and chained member accesses inherit the base object's TypeMap but `transpile_expr` produces `Item` (uint64_t), not `Map*`. Generated code like `(fn_member(...))->data` tried struct member access on a uint64_t. **Fix**: `expr_produces_native_ptr()` restricts optimization to simple variable references and literals.

3. **TypeType wrapping** — Type-defined maps (from `type Point = {x: int}`) store `LMD_TYPE_TYPE` wrapper on shape entries, not the inner type like `LMD_TYPE_INT`. The emit functions used the wrong type for boxing/unboxing, producing `(Item)(*(void**)...)` instead of `i2it(*(int64_t*)...)`. **Fix**: `resolve_field_type_id()` unwraps `TypeType` to get the actual stored-data type.

4. **Mutable literal type changes** — Map literals (e.g., `{x: 10, y: 20}`) in procedural code can have fields reassigned to different types (`obj.x = "hello"`), which triggers `fn_map_set` to rebuild the shape and reallocate data. Direct reads/writes using stale compile-time offsets and types produce wrong results. **Fix**: `has_fixed_shape()` requires `struct_name != NULL`, restricting optimization to named types from `type` declarations where field types are guaranteed stable.

#### Test Coverage

- **467/467** baseline tests pass (465 pre-existing + 2 new from `typed_map_direct_access.ls`)
- New test file: `test/lambda/typed_map_direct_access.ls` — exercises direct reads on typed maps with int, string, bool fields, arithmetic on fields, and function parameters with typed map annotations

### Not Yet Implemented

| Phase | Description | Status |
|---|---|---|
| Phase 5 | Object method field loading (`_self_data->field`) | Not started |
| Phase 6 | Object method field write-back | Not started |
| Phase 7 | Typed function parameter passing (`Map*` signatures) | Not started |
| — | Chained member access (`a.b.c` direct access) | Not started — requires propagating native pointer through chain |
| — | Unboxed context reads (no boxing when target is native type) | Not started |

### Completed: Phase 1 (Struct Typedef Emission) and Phase 4 (Direct Map Construction)

Building on the Phase 2/3 inline byte-offset work, Phase 1 emits proper C struct typedefs for named map/object types in the transpiled preamble, and Phase 4 replaces `map_fill()` varargs with direct byte-offset writes during map literal construction.

#### Phase 1: Struct Typedef Emission

Named map types (from `type Name = {…}`) now generate C struct typedefs in the transpiled preamble:

```c
typedef struct _type_Point {
  int64_t x;
  int64_t y;
} _type_Point;
typedef struct _type_Person {
  String* name;
  int64_t age;
  bool active;
} _type_Person;
```

**Implementation**: `emit_struct_typedefs()` iterates `type_list`, finds `TypeMap`/`TypeObject` entries with non-NULL `struct_name`, and emits a C typedef matching the packed data layout. Called once in `transpile_ast_root` between closure env pre-defines and function forward declarations.

**Helper**: `write_c_field_type()` maps Lambda `TypeId` → C type names (bool, int64_t, double, String*, Symbol*, void*, etc.).

#### Phase 4: Direct Map Literal Construction

Map literals with known shapes now bypass `map_fill()` (which uses va_list + per-field type-switch) and instead write directly to the allocated data buffer at compile-time-known byte offsets.

**Before (all maps):**
```c
({Map* m = map(2);
 map_fill(m, i2it(10), i2it(20));})
```

**After (maps with valid shapes):**
```c
({Map* m = map(2);
 m->data=heap_data_calloc(16);
 *(int64_t*)((char*)(m)->data+0)=it2i(i2it(10));
 *(int64_t*)((char*)(m)->data+8)=it2i(i2it(20));
 m->data_cap=16; m;})
```

**Eligibility**: Works for ALL map literals with valid shapes (named or anonymous), not just named types. Requirements: shape is non-null, all fields named (no spreads), all byte offsets aligned, byte_size > 0, all field types are concrete.

#### Files Modified (Phase 1 + Phase 4)

| File | Changes |
|---|---|
| `lambda/transpile.cpp` | Added `write_c_field_type()`, `emit_struct_typedefs()` functions; modified `transpile_map_expr()` for direct construction; added `unwrap_type_type` parameter to `resolve_field_type_id()` |
| `lambda/lambda.h` | Added `heap_data_calloc()` declaration in `extern "C"` block |
| `lambda/mir.c` | Added `heap_data_calloc` to MIR native function resolution table |

#### Bugs Found and Fixed (Phase 1 + Phase 4)

1. **Missing `m;` return value** — Statement expression `({Map* m = map(...); ...; m->data_cap=N;})` returned the integer `N` instead of `Map* m`, causing null pointer crashes. **Fix**: Added `m;` at end of direct construction block.

2. **`heap_data_calloc` not resolvable by MIR** — Declared in `lambda.h` but not registered in MIR's native function table, causing "failed to resolve native fn/pn: heap_data_calloc" error. **Fix**: Added to `mir.c` function registration table.

3. **Symbol/binary fields stored as null** — Used `it2s()` to unbox symbol/binary Items, but `it2s()` only handles `LMD_TYPE_STRING` and returns nullptr for other types. **Fix**: Changed all pointer-type fields (string, symbol, binary, decimal, containers) to use generic tag-stripping: `(void*)((uint64_t)(item) & 0x00FFFFFFFFFFFFFFULL)`.

4. **DateTime fields stored as pointer instead of value** — Default case stored the tagged pointer as `void*`, but DateTime is a value type (stored as uint64_t in the packed struct, not a pointer). **Fix**: Added dedicated `LMD_TYPE_DTIME` case that dereferences the heap pointer: `*(DateTime*)((uint64_t)(item) & mask)`.

5. **`resolve_field_type_id` crash on anonymous maps** — Function blindly cast `Type*` with `type_id == LMD_TYPE_TYPE` to `TypeType*` and accessed `->type`. Anonymous map shape entries store plain `Type` structs (smaller than `TypeType`), causing out-of-bounds memory access. **Fix**: Added `unwrap_type_type` parameter; only unwrap for named types (where shape entries are proper `TypeType` structs).

#### Test Coverage

- **467/467** baseline tests pass
- Direct construction verified on typed_map_direct_access.ls: all 6 map literals use `heap_data_calloc` + byte-offset writes instead of `map_fill`
