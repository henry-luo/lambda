# VMap: Virtual Map Type for Lambda

## Summary

Add a new runtime type `LMD_TYPE_VMAP` that wraps an arbitrary map implementation behind a **vtable** of function pointers. To the Lambda language, a VMap behaves identically to a regular Map — `type(vmap)` returns `"map"`, field access works with `.key` and `[key]`, and `for k, v at vmap` iterates as expected. The difference is purely internal: whereas a `Map` is a fixed shape struct, a VMap delegates all operations through a pluggable vtable, enabling hash-map, tree-map, or any future map backend without changing the language runtime call sites.

The first concrete VMap backend is a **HashMap** (using `lib/hashmap.h`) that supports arbitrary `Item` keys via identity/value hashing. This enables memoization, caching, and dynamic key-value construction — critical for performance of functional programs like `readability2.ls`.

---

## Motivation

### Current Map Limitations

Lambda's `Map` type is a **statically-shaped record**:

```
struct Map : Container {
    void* type;      // TypeMap* → linked list of ShapeEntry
    void* data;      // packed byte buffer
    int data_cap;
};
```

- **Keys are fixed at shape-definition time** — the set of field names is defined by the `TypeMap`'s `ShapeEntry` linked list and cannot change after construction.
- **Keys must be strings or symbols** — `map_get()` rejects non-string keys.
- **Lookup is O(n) linear scan** — acceptable for small JSON-like records (5-20 fields), but prohibitive for large dynamic collections.
- **No dynamic key insertion** — `fn_map_set()` can only update existing fields within a pre-defined shape; it cannot add new keys.

This design is excellent for structured data (JSON objects, parsed HTML attributes) but cannot support:

1. **Memoization caches** — mapping `Element*` → computed value for O(1) re-lookup
2. **Dynamic accumulators** — building a key→value collection where keys are discovered at runtime
3. **Arbitrary-key dictionaries** — keying by integer, tuple, or container

### Performance Impact

Profiling of `readability2.ls` (see `Readability_Tune.md`) revealed that **O(n²) redundant `get_inner_text` calls** dominate runtime. A memoization cache keyed on Element pointers would reduce this to O(n) — but Lambda has no runtime type that supports this pattern today.

---

## Design

### New Runtime Type: `LMD_TYPE_VMAP`

```c
// in lambda.h EnumTypeId
enum EnumTypeId {
    ...
    LMD_TYPE_MAP,
    LMD_TYPE_VMAP,      // ← new, immediately after MAP
    LMD_TYPE_ELEMENT,
    ...
};
```

**Type identity**: `type(vmap)` returns `"map"`. The `get_type_name()` function maps `LMD_TYPE_VMAP` → `"map"`. This means Lambda scripts see no difference — VMap and Map are interchangeable from the language perspective.

### VMap Struct

```cpp
// in lambda.hpp

// Vtable for virtual map dispatch
struct VMapVtable {
    Item    (*get)(void* data, Item key);                    // map[key]
    void    (*set)(void* data, Item key, Item value);        // map[key] = value (pn: mutate in place)
    Item    (*set_immutable)(void* data, Item key, Item value); // map_set() → new VMap (fn: copy-on-write)
    int64_t (*count)(void* data);                            // len(map)
    ArrayList* (*keys)(void* data);                          // item_keys() → ArrayList<String*> of hash-strings
    Item    (*at_index)(void* data, int64_t index);          // get key at index (for for-loop)
    Item    (*value_at_index)(void* data, int64_t index);    // get value at index
    bool    (*has_key)(void* data, Item key);                // key ∈ map
    void    (*destroy)(void* data);                          // free backing store
};

struct VMap : Container {
    void* data;                // opaque pointer to backing implementation
    VMapVtable* vtable;        // dispatch table
};
```

**Memory layout**: `VMap` is 24 bytes (4 Container + 8 data + 8 vtable + 4 padding), comparable to `Map`'s 24 bytes. Reference counting and GC integration inherit from `Container`.

### Vtable Operations (Phase 1)

Phase 1 requires only the core access and iteration operations.

| Vtable slot | Purpose | Phase |
|---|---|---|
| `get` | `map[key]` / `map.key` / `map_get(vmap, key)` | **1** |
| `set` | Insert/update a key-value pair. In `fn` context this returns a **new VMap** (immutable). In `pn` context this mutates in-place. | **1** |
| `count` | `len(map)` — number of entries | **1** |
| `keys` | `item_keys()` for `for k at map` iteration. Returns `ArrayList*` of `String*` — each key is represented as its **hash-string** (see Iteration section). | **1** |
| `at_index` | Get key at index i — enables index-based for-loop | **1** |
| `value_at_index` | Get value at index i — enables `for k, v at map` | **1** |
| `has_key` | `key in map` membership test | 2 |
| `destroy` | Destructor called when ref_cnt → 0 | **1** |

### HashMap Backend (First VMap Implementation)

The first backend wraps `lib/hashmap.h` — a proven, high-performance open-addressed hash table using Robin Hood hashing.

```cpp
// HashMap entry: stored in hashmap as element
struct HashMapEntry {
    Item key;
    Item value;
};

// HashMap backing data
struct HashMapData {
    HashMap* table;           // lib/hashmap.h instance
    ArrayList* key_order;     // insertion-order key list (for deterministic iteration)
    int64_t count;
};
```

**Key hashing strategy**: Hash the 64-bit `Item` value directly.

- **Pointer-identity keys** (Element, List, Map, Array): hash the pointer address. Two references to the same container are equal; two distinct containers with identical contents are not. This is the correct semantics for memoization caches.
- **Value-identity keys** (int, bool, null, symbol): hash the packed bit pattern. Equal values produce equal hashes.
- **String keys**: hash the string content (via `hashmap_sip` on char data). Equal strings match regardless of allocation identity.

```c
uint64_t vmap_hash_item(const void* entry, uint64_t seed0, uint64_t seed1) {
    const HashMapEntry* e = (const HashMapEntry*)entry;
    Item key = e->key;
    TypeId type_id = get_type_id(key);

    switch (type_id) {
    case LMD_TYPE_STRING: {
        const char* s = key.get_chars();
        return hashmap_sip(s, strlen(s), seed0, seed1);
    }
    case LMD_TYPE_SYMBOL: {
        const char* s = key.get_chars();
        return hashmap_sip(s, strlen(s), seed0, seed1);
    }
    default:
        // pointer-identity or packed-value: hash the raw 64-bit Item
        return hashmap_sip(&key.item, sizeof(uint64_t), seed0, seed1);
    }
}

int vmap_compare_item(const void* a, const void* b, void* udata) {
    const HashMapEntry* ea = (const HashMapEntry*)a;
    const HashMapEntry* eb = (const HashMapEntry*)b;
    Item ka = ea->key, kb = eb->key;
    TypeId ta = get_type_id(ka), tb = get_type_id(kb);
    if (ta != tb) return 1;  // different types → not equal

    switch (ta) {
    case LMD_TYPE_STRING:
    case LMD_TYPE_SYMBOL:
        return strcmp(ka.get_chars(), kb.get_chars());
    default:
        return (ka.item == kb.item) ? 0 : 1;
    }
}
```

**Iteration order**: The `key_order` ArrayList preserves insertion order. `keys()` returns this list directly. `at_index(i)` and `value_at_index(i)` index into it. This gives deterministic, predictable iteration — matching user expectations and JSON-like semantics.

---

## Runtime Integration Points

The following existing functions need `LMD_TYPE_VMAP` dispatch branches:

### 1. `item_attr(Item data, const char* key)` — Field Access

```cpp
// lambda-data-runtime.cpp, item_attr()
case LMD_TYPE_VMAP: {
    VMap* vmap = data.vmap;
    Item key_item = {.item = s2it(heap_create_name(key))};
    return vmap->vtable->get(vmap->data, key_item);
}
```

### 2. `item_keys(Item data)` — Key Enumeration

```cpp
// lambda-data-runtime.cpp, item_keys()
case LMD_TYPE_VMAP: {
    VMap* vmap = data.vmap;
    return vmap->vtable->keys(vmap->data);
    // Returns ArrayList<String*> of hash-string keys.
    // For string/symbol keys: the key string itself.
    // For non-string keys: a synthetic string like "__vmap_0x1a2b3c4d"
    //   derived from the key's hash code.
}
```

The `item_attr` VMap dispatch (§1) accepts these hash-strings and maps them back to the original key internally via an internal reverse lookup in `HashMapData`.

### 3. `map_get(Map* map, Item key)` — Generic Key Lookup

The current `map_get` only handles `Map*`. Option A: extend `map_get` to check `type_id` and dispatch. Option B: add `vmap_get` and update call sites.

**Recommended: Option A** — check `container->type_id` at entry:

```cpp
Item map_get(Map* map, Item key) {
    if (!map) return ItemNull;
    if (map->type_id == LMD_TYPE_VMAP) {
        VMap* vmap = (VMap*)map;
        return vmap->vtable->get(vmap->data, key);
    }
    // existing Map logic...
}
```

### 4. `fn_map_set(Item map, Item key, Item value)` — Procedural Assignment (`pn` context)

In `pn` context, `m[key] = value` mutates the VMap in place:

```cpp
void fn_map_set(Item map_item, Item key, Item value) {
    TypeId type_id = get_type_id(map_item);
    if (type_id == LMD_TYPE_VMAP) {
        VMap* vmap = map_item.vmap;
        vmap->vtable->set(vmap->data, key, value);  // in-place mutation
        return;
    }
    // existing Map logic...
}
```

In `fn` context, mutation is forbidden. Instead, use the `map_set(m, key, value)` system function which returns a **new VMap**:

```cpp
Item sys_map_set(Item map_item, Item key, Item value) {
    TypeId type_id = get_type_id(map_item);
    if (type_id == LMD_TYPE_VMAP) {
        VMap* vmap = map_item.vmap;
        // shallow-copy the hash table, insert the new entry, return new VMap
        return vmap->vtable->set_immutable(vmap->data, key, value);
    }
    // existing Map: return new map with field added/updated
    ...
}
```

### 5. `fn_member(Item, Item)` and `fn_index(Item, Item)` — Interpreter Access

```cpp
// lambda-eval.cpp, fn_member()
case LMD_TYPE_VMAP: {
    VMap* vmap = item.vmap;
    return vmap->vtable->get(vmap->data, key);
}
```

### 6. `get_type_name()` — Type Identity

```cpp
case LMD_TYPE_VMAP: return "map";   // VMap is-a Map to language users
```

### 7. Transpiler For-Loop (`transpile.cpp`) — Piggyback on MAP Path

The transpiler does **not** generate any VMap-specific code. The existing MAP iteration pattern is reused as-is:

```c
// Transpiler-generated code for: for k, v at some_map
Item it = <map_expr>;
ArrayList* _attr_keys = item_keys(it);   // dispatches to VMap vtable
for (int _ki = 0; _attr_keys && _ki < _attr_keys->length; _ki++) {
    String* _k = _attr_keys->data[_ki];             // hash-string key
    Item _v = item_attr(it, _k->chars);              // vtable→get
}
```

This works because `item_keys()` and `item_attr()` both have `LMD_TYPE_VMAP` dispatch cases (see §1 and §2 above). No transpiler changes needed.

**Key representation in `for k, v at vmap`**: Since the existing transpiled code expects `item_keys()` to return `ArrayList<String*>` and uses `item_attr(it, string_key)` to look up values, VMap's `keys()` vtable function returns **hash-string** representations of each key. For string/symbol keys, the hash-string is the key itself. For non-string keys (elements, integers, containers), the hash-string is a synthetic string derived from the key's hash code (e.g. `"__vmap_0x1a2b3c4d"`). The `item_attr` VMap dispatch then looks up by this hash-string, which maps back to the original key internally.

This means `k` in `for k, v at vmap` is always a `String` — consistent with the existing MAP iteration contract. Scripts that need the original typed key can use `value_at_index` through a dedicated system function in a future phase.

### 8. Reference Counting / Memory

```cpp
// in heap_release / ref_dec:
case LMD_TYPE_VMAP: {
    VMap* vmap = (VMap*)container;
    if (vmap->vtable->destroy) {
        vmap->vtable->destroy(vmap->data);
    }
    free(vmap);
    break;
}
```

---

## Lambda Script API

### Constructor: `map()`

The existing `map()` system function is extended to **auto-dispatch** based on key types:

```
// String/symbol keys → normal Map (existing behavior)
let m = map(:name, "Alice", :age, 30)

// Non-string keys → HashMap-backed VMap (new)
let cache = map(element, "text", other_elem, "more text")

// Empty map → normal Map (existing behavior)
let m = map()
```

**Auto-dispatch rule**: If any key in the argument list is not a string or symbol, construct a HashMap-backed VMap. Otherwise, construct a normal shape-based Map. This is decided at construction time based on the first key's type.

### Functional Context (`fn`) — Immutable Maps

In a functional `fn` context, VMap is **immutable**. Insertion returns a **new VMap** with the entry added (copy-on-write):

```
// Lambda Script (functional fn context)
let cache = map(elem1, get_inner_text(elem1))
let cache2 = map_set(cache, elem2, get_inner_text(elem2))  // returns new vmap
// cache still has 1 entry, cache2 has 2 entries
```

The `map_set(m, key, value)` system function:
- If `m` is a normal Map → returns a new Map with the field added/updated (existing behavior)
- If `m` is a VMap → calls `vtable->set(data, key, value)` which returns a new VMap (shallow copy of the hash table + insert)

### Procedural Context (`pn`) — Mutable Maps

In a procedural `pn` context, VMap supports in-place mutation via index assignment:

```
// Lambda Script (procedural pn context)
let cache = map()
cache[element] = computed_text     // fn_map_set → vtable->set (mutates in place)
let text = cache[element]          // map_get → vtable->get
```

### Future: `map()` from Key-Value Pairs

```
// Construct from list of pairs
let m = map([[k1, v1], [k2, v2], ...])

// Construct from two parallel lists
let m = map(keys, values)
```

---

## Implementation Plan

### Phase 1: Core VMap + HashMap + `map()` Auto-Dispatch

1. Add `LMD_TYPE_VMAP` to `EnumTypeId` in `lambda.h`
2. Define `VMapVtable` and `VMap` structs in `lambda.hpp`
3. Add `VMap*` to the `Item` union in `lambda.h`
4. Implement HashMap backend:
   - `hashmap_vmap_get()`, `hashmap_vmap_set()` (immutable: returns new VMap), `hashmap_vmap_count()`, `hashmap_vmap_keys()`, `hashmap_vmap_at_index()`, `hashmap_vmap_value_at_index()`, `hashmap_vmap_destroy()`
   - Wrap in a static `VMapVtable hashmap_vtable = { ... };`
5. Add `LMD_TYPE_VMAP` cases to `item_attr`, `item_keys`, `map_get`, `fn_member`, `fn_index`
6. Map `LMD_TYPE_VMAP` → `"map"` in `get_type_name()`
7. Extend `map()` system function: detect non-string keys → construct VMap instead of Map
8. Implement `map_set(m, key, value)` system function (returns new VMap in `fn` context)
9. Add ref-counting / destroy support
10. Add `LMD_TYPE_VMAP` case to `fn_map_set()` for `pn` context (in-place mutation)

### Phase 2: Extended Operations

1. Implement `has_key` vtable slot — `key in map` membership test
2. Pipe operator `|` support for VMap (should already work via MAP path)
3. Pattern matching on VMap in `match` expressions

### Phase 3: Additional Backends

- **TreeMap** — sorted iteration by key
- **WeakMap** — entries whose values can be GC'd
- **ReadOnlyMap** — frozen snapshot of a HashMap

---

## Compatibility & Invariants

| Property | Guarantee |
|---|---|
| `type(vmap)` | Returns `"map"` |
| `vmap.field` | Works if field is a string key |
| `vmap[key]` | Works for any key type |
| `for k, v at vmap` | Iterates in insertion order |
| `vmap \| fn(k, v) ...` | Pipe iteration works via `item_keys` |
| Existing Map code | **Zero changes** — all existing Map behavior is preserved |
| Transpiled C code | No changes needed for Phase 1 (runtime dispatch) |

---

## Struct Layout Reference

For reference, the current and proposed struct layouts:

```
Current Map (24 bytes):
┌──────────────┬──────────────┬──────────────┐
│ Container(4) │ type*(8)     │ data*(8)     │ data_cap(4)
└──────────────┴──────────────┴──────────────┘
              TypeMap*          packed bytes

Proposed VMap (24 bytes):
┌──────────────┬──────────────┬──────────────┐
│ Container(4) │ data*(8)     │ vtable*(8)   │
└──────────────┴──────────────┴──────────────┘
              HashMapData*     VMapVtable*
```

Both fit in the same allocation size class, keeping memory overhead unchanged.

---

## Design Decisions

| Decision             | Resolution                                                              |
| -------------------- | ----------------------------------------------------------------------- |
| Constructor name     | `map()` — auto-dispatches based on key type (not `hashmap()`)           |
| Transpiler approach  | **Option A: Piggyback on MAP path** — no transpiler changes             |
| For-loop key type    | `k` in `for k, v at vmap` is a `String` (hash-string of the actual key) |
| Immutability in `fn` | **Yes** — `map_set()` returns a new VMap; original is unchanged         |
| Mutability in `pn`   | **Yes** — `m[k] = v` mutates in place via `fn_map_set`                  |
| Equality (`==`)      | **Deferred** — not implemented in Phase 1; to be decided later          |
| `type(vmap)`         | Returns `"map"` for seamless interop                                    |

## Open Questions

1. **Equality semantics** — Should `vmap_a == vmap_b` compare contents (deep equality) or identity (pointer equality)? Deferred to a future phase.

2. **String key optimization** — When a VMap has only string/symbol keys, should it auto-downgrade to a normal Map shape for compatibility with transpiled field-offset code? Likely no — VMap should always use vtable dispatch for simplicity.

3. **Copy-on-write cost** — In functional `fn` context, `map_set` creates a shallow copy of the entire hash table. For large maps with frequent inserts, this is O(n). Consider a persistent hash-array-mapped-trie (HAMT) backend if this becomes a bottleneck.
