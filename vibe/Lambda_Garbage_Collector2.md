# Phase 5: Mark-and-Sweep Collection with Dual-Zone Nursery

Status: Steps 1–5 Complete (mark-and-sweep collection fully operational with auto-trigger)
Date: 2026-02-25 (Steps 1–2), 2026-02-26 (Steps 3–5)

---

## 1. Context

Phases 0–4 of the GC migration are complete. The runtime now:
- Allocates all objects via `gc_heap_alloc()`/`gc_heap_calloc()` (tracked by GCHeader intrusive linked list)
- Allocates numeric temporaries (int64, double, DateTime) from a bump-pointer `gc_nursery`
- Has **no** reference counting (`ref_cnt` removed from all structs)
- Has **no** frame management (`frame_start`/`frame_end` removed)
- Frees all memory at context end via `gc_finalize_all_objects()` + `pool_destroy()`

**The missing piece**: There is no mid-execution garbage collection. Memory grows monotonically until context teardown. Long-running scripts, loops that generate temporary data, and procedural code with closure cycles will accumulate unbounded memory.

This proposal designs the **mark-and-sweep collector** that reclaims dead objects during execution.

---

## 2. The Core Problem: Moving Objects Requires Pointer Fixup

The original GC design document (Section 2.3) describes a **copying nursery**: "When nursery is full, run a minor GC (mark live objects, copy survivors to tenured, reset nursery pointer)." However, Section 2.8 describes a **non-moving mark-and-sweep**. These are fundamentally different strategies, and the document does not address the critical question:

**When an object is moved (copied from nursery to tenured), how are all references to it updated?**

Lambda's `Item` type is a 64-bit tagged pointer. A pointer to a `List`, `Map`, `String`, etc. may be stored in:
- The eval result (`context->result`)
- The JIT call stack (local variables in transpiled functions)
- Module BSS globals
- `Item* items` arrays inside other Lists/Arrays/Elements
- `void* data` packed field buffers inside Maps/Elements/Objects
- `void* closure_env` structs inside Functions
- `VMapVtable` backing data (HashMap entries)

If an object is moved to a new address, **every one** of these locations holding a pointer to it must be updated. The number of incoming references to an object is unbounded — a single string could appear in thousands of array slots.

---

## 3. Three Approaches Evaluated

### 3.1 Option A: Copying GC with Forwarding Pointers

The classical generational approach (used by Java, .NET, V8).

**Mechanism**:
1. Copy each live nursery object to the tenured region (new address)
2. Write a **forwarding pointer** at the old nursery location
3. Scan all roots + all tenured-to-nursery references (a "remembered set"), replacing each old pointer with the forwarded address
4. Reset the nursery bump pointer

**Requires a write barrier**: Every time a tenured object stores a pointer to a nursery object, the runtime must record that tenured object in a "remembered set." The transpiler would need to emit barrier code on every `Item` store into a container:

```c
// Transpiler must emit this on every store:
void write_barrier(Container* parent, Item child) {
    if (is_tenured(parent) && is_nursery(child)) {
        remembered_set_add(parent);
    }
}
```

**Pros**:
- Nursery stays a pure bump allocator (O(1) allocation, no fragmentation)
- After collection, nursery is fully reset — zero wasted space
- Battle-tested in production GCs

**Cons**:
- **Write barrier on every store**: ~2-3 instructions per `Item` write into any container. Lambda's transpiled code stores into arrays, maps, and elements constantly — this adds overhead everywhere.
- **Pointer fixup scope is massive**: An `Item` can be stored in `items[]` arrays (List/Array/Element), `data` buffers (Map/Element/Object), closure envs, VMap hash tables. Every store path needs barrier instrumentation.
- **Transpiler changes**: Both C2MIR and MIR direct transpilers must emit write barriers. This is the opposite of the "simplify transpiler" goal from Phase 4.
- **Remembered set overhead**: Data structures to track tenured→nursery pointers add memory and bookkeeping complexity.

**Verdict**: Too invasive for Lambda's architecture. The write barrier requirement touches every container store path in both transpilers, undoing the simplification achieved in Phases 1–4.

### 3.2 Option B: Non-Moving Mark-and-Sweep

Objects stay at their original address forever. GC marks live objects in place, then sweeps dead ones into a free list.

**Mechanism**:
1. **Mark**: Starting from roots, traverse all reachable objects via tri-color marking (as described in the Phase 1 document, Section 2.8). Mark each reachable object's `marked` bit in its GCHeader.
2. **Sweep**: Walk the `all_objects` linked list. Free any object with `marked == 0`. Reset `marked` to 0 for survivors.
3. No object ever moves. No pointer fixup. No write barriers.

**Pros**:
- **Zero pointer fixup**: Objects stay at their address. `Item` tagged pointers remain valid forever.
- **No write barriers**: No transpiler changes needed for store operations.
- **Simple implementation**: Builds directly on the existing `GCHeader` linked list and `gc_finalize_all_objects()` logic.
- **Correct cycle collection**: Tri-color marking handles closure cycles correctly.

**Cons**:
- **Nursery loses bump-allocation benefit after first GC**: Live objects are scattered among dead ones. The nursery degrades into a free-list allocator, losing the O(1) bump allocation advantage.
- **Fragmentation**: Over time, the heap develops holes of various sizes. Allocation requires free-list search (size-class segregated lists mitigate this).
- **No compaction**: Unlike a copying GC, memory is never compacted. Long-lived objects may be interleaved with freed gaps.

**Verdict**: The right foundation. Simple, correct, no transpiler impact. The fragmentation concern is addressed by the dual-zone refinement below.

### 3.3 Option C: Handle Table (Indirection Layer)

All `Item` tagged pointers reference a handle (index into an object table) rather than a raw memory address. Moving an object only requires updating the table entry.

**Mechanism**:
```
Item = tag | handle_index
handle_table[index] -> actual object address
```
Moving an object just updates `handle_table[index]`. All `Item` values holding that index automatically see the new address.

**Pros**:
- Trivial pointer fixup on move (single table entry update)
- Enables compaction, relocation, even cross-heap migration

**Cons**:
- **Breaks Lambda's Item representation**: The tagged pointer scheme packs raw 56-bit memory addresses into 64-bit Items. Switching to handles requires a fundamental redesign of `Item`, all accessor macros (`l2it`, `d2it`, `s2it`, `get_int64`, `get_string`, etc.), and all transpiled code.
- **Extra indirection on every access**: Every `Item` dereference becomes a table lookup + pointer follow. This adds a cache miss on every field access, array index, and function call.
- **Table management**: Handle allocation, deallocation, and compaction add complexity.

**Verdict**: Rejected. Architectural cost is prohibitive — it requires rewriting Lambda's core data representation, all accessor macros, and all transpiled output.

---

## 4. Decision: Non-Moving Mark-and-Sweep (Option B) with Dual-Zone Refinement

### 4.1 Rationale

Option B (non-moving mark-and-sweep) is the right choice because:
1. It requires **zero changes** to `Item` representation, accessor macros, or transpiled output
2. It builds directly on the existing `GCHeader` infrastructure from Phases 0–2
3. It eliminates write barriers, keeping the transpiler simple (Phase 4 goal preserved)
4. It correctly collects closure cycles (the primary motivation for GC)

The main weakness — nursery fragmentation — is addressed by the dual-zone refinement.

### 4.2 Dual-Zone Nursery Design

The key insight: Lambda objects are structurally **two-part**:

| Object | Fixed-Size Struct | Variable-Size Data |
|--------|------------------|--------------------|
| `String` | `{uint32_t len, char chars[]}` | `chars[]` (flexible array, currently inline) |
| `Symbol` | `{uint32_t len, Target* ns, char chars[]}` | `chars[]` (currently inline) |
| `List`/`Array` | `{Container, Item* items, length, extra, capacity}` | `items[]` (separately `malloc`'d) |
| `ArrayInt`/`ArrayInt64`/`ArrayFloat` | `{Container, T* items, length, extra, capacity}` | `items[]` (separately `malloc`'d) |
| `Map` | `{Container, void* type, void* data, int data_cap}` | `data` buffer (separately `calloc`'d) |
| `Element` | `{Container, Item* items, ..., void* type, void* data, ...}` | `items[]` + `data` buffer (both separate) |
| `Object` | `{Container, void* type, void* data, int data_cap}` | `data` buffer (separately `calloc`'d) |
| `Function` | `{type_id, arity, fn_type, ptr, closure_env, name}` | `closure_env` (separately `malloc`'d) |
| `Decimal` | `{uint8_t unlimited, mpd_t* dec_val}` | `mpd_t` sub-allocation |
| `int64_t*` / `double*` / `DateTime*` | 8 bytes | (none) |

The `Item` tagged pointer always points to the **struct** (the fixed-size part). The variable-size data is referenced by a pointer **inside** that struct. This means:

- The struct **must not move** (it's what `Item` pointers reference from potentially thousands of locations)
- The data chunk **has exactly one incoming pointer** (the field inside its owning struct: `items`, `data`, `closure_env`, etc.)

This suggests splitting the nursery into two zones:

```
┌─────────────────────────────────────────────────┐
│                   GC Nursery                     │
│  ┌──────────────────┐  ┌──────────────────────┐  │
│  │   Object Zone     │  │     Data Zone         │  │
│  │  (fixed structs)  │  │  (variable buffers)   │  │
│  │                   │  │                        │  │
│  │  Non-moving       │  │  Bump allocator        │  │
│  │  Free-list mgmt   │  │  Compactable on GC     │  │
│  │  Size-class bins  │  │                        │  │
│  │                   │  │  items[], data,         │  │
│  │  List, Map, Array │  │  chars[], closure_env   │  │
│  │  String, Function │  │                        │  │
│  │  int64, double    │  │                        │  │
│  └──────────────────┘  └──────────────────────┘  │
└─────────────────────────────────────────────────┘
```

**Object Zone** — non-moving, free-list allocator:
- Holds all fixed-size object structs (the things `Item` pointers reference)
- Uses **size-class segregated free lists** (e.g., 8, 16, 32, 48, 64 byte bins) with alignment to reduce fragmentation
- On GC sweep: dead objects returned to their size-class free list. No moving.
- `Item` tagged pointers always remain valid — **zero pointer fixup**.

**Data Zone** — bump allocator, compactable:
- Holds all variable-size data buffers: `items[]`, `data`, `chars[]`, `closure_env`
- Allocation is O(1) bump pointer (same speed as the original nursery proposal)
- On GC: surviving data chunks are **copied** to a tenured data zone, and the one pointer inside the owning struct (`list->items`, `map->data`, `str->chars`, `fn->closure_env`) is updated
- After GC, the nursery data zone is **fully reset** (bump pointer back to start) — zero fragmentation, all space reclaimed
- Exactly **one pointer fixup per surviving object** (the struct→data pointer). Not per-reference, but per-object.

### 4.3 Why This Works

The dual-zone design gives us the benefits of both approaches:

| Property | Object Zone | Data Zone |
|----------|-------------|-----------|
| Allocation speed | Free-list (size-class, fast) | Bump pointer (fastest) |
| Moving on GC? | No (never moves) | Yes (copied to tenured) |
| Pointer fixup on GC? | None | Exactly 1 per surviving object |
| Fragmentation | Controlled by size-class bins | Zero (full reset after GC) |
| `Item` pointer stability | ✅ Guaranteed | N/A (Items never point here) |

The insight is that **pointer fixup is O(survivors), not O(references)**. A `String*` that appears in 10,000 array slots only needs **one** fixup: updating `str->chars` after the chars buffer is copied. The 10,000 array slots all point to the `String` struct in the object zone, which never moves.

### 4.4 Collection Flow

```
Before GC:
  Object Zone: [List_A] [Map_B] [String_C] [dead] [List_D] [dead] [dead]
  Data Zone:   [items_A][data_B][chars_C][dead_data][items_D][dead_data]...

Mark phase:
  - Walk roots, mark List_A, Map_B (reachable)
  - String_C and List_D are unreachable (WHITE)

Sweep + Compact:
  Object Zone: [List_A] [Map_B] [FREE ] [dead] [FREE  ] [dead] [dead]
                                   ^               ^
                         returned to free list

  Data Zone:   [reset to empty — bump pointer back to start]

  Tenured Data: [...][items_A][data_B][...]
                       ^        ^
  Pointer fixup: List_A->items = new_items_A_addr
                 Map_B->data   = new_data_B_addr
```

---

## 5. Detailed Design

### 5.1 Object Zone: Size-Class Free-List Allocator

Object structs have a small set of distinct sizes:

| Type | Struct Size | Size Class |
|------|-------------|------------|
| `int64_t*`, `double*`, `DateTime*` | 8 bytes | 16-byte bin |
| `Decimal` | ~16 bytes | 16-byte bin |
| `String` (header only, see §5.3) | 16 bytes | 16-byte bin |
| `Symbol` (header only, see §5.3) | 24 bytes | 32-byte bin |
| `Range` | ~26 bytes | 32-byte bin |
| `Map`, `Object` | ~24 bytes | 32-byte bin |
| `List`/`Array` | ~34 bytes | 48-byte bin |
| `ArrayInt`/`ArrayInt64`/`ArrayFloat` | ~34 bytes | 48-byte bin |
| `Function` | ~42 bytes | 48-byte bin |
| `Element` | ~58 bytes | 64-byte bin |
| `VMap` | ~18 bytes | 32-byte bin |

Note: All sizes include the 16-byte `GCHeader` prepended to each allocation.

```c
// Size classes: 16, 32, 48, 64, 96, 128 bytes
#define GC_NUM_SIZE_CLASSES 6
static const size_t gc_size_classes[GC_NUM_SIZE_CLASSES] = {16, 32, 48, 64, 96, 128};

typedef struct ObjectZone {
    // per-size-class free lists
    gc_header_t* free_lists[GC_NUM_SIZE_CLASSES];
    
    // slab blocks: each block is a contiguous array of same-size slots
    // Grows by allocating new slabs from the underlying pool when a free list is empty
    struct ObjectSlab {
        uint8_t* base;
        size_t slot_size;
        size_t slot_count;
        size_t next_fresh;      // next un-used slot index (for initial sequential allocation)
        struct ObjectSlab* next;
    }* slabs[GC_NUM_SIZE_CLASSES];
    
    Pool* pool;  // underlying memory pool
} ObjectZone;
```

**Allocation**:
```c
void* object_zone_alloc(ObjectZone* oz, size_t size) {
    int cls = size_class_index(size);           // find smallest class >= size
    size_t actual_size = gc_size_classes[cls];
    
    // 1. Try free list first
    if (oz->free_lists[cls]) {
        gc_header_t* slot = oz->free_lists[cls];
        oz->free_lists[cls] = slot->next;       // pop from free list
        return (void*)(slot + 1);               // return user pointer after header
    }
    
    // 2. Try fresh slot from current slab
    ObjectSlab* slab = oz->slabs[cls];
    if (slab && slab->next_fresh < slab->slot_count) {
        void* ptr = slab->base + (slab->next_fresh * actual_size);
        slab->next_fresh++;
        return (void*)((gc_header_t*)ptr + 1);
    }
    
    // 3. Allocate new slab
    slab = allocate_new_slab(oz, cls, actual_size);
    // ... recurse or assign from new slab
}
```

**Deallocation (during sweep)**:
```c
void object_zone_free(ObjectZone* oz, gc_header_t* header) {
    int cls = size_class_index(header->alloc_size);
    header->next = oz->free_lists[cls];         // push to free list
    oz->free_lists[cls] = header;
}
```

**Alignment**: All size classes are multiples of 16 bytes, ensuring proper alignment for any struct field type. The 16-byte GCHeader is naturally aligned.

### 5.2 Data Zone: Bump Allocator

Variable-size data buffers are allocated from a contiguous bump-allocated region.

```c
typedef struct DataZone {
    uint8_t* base;      // region start
    uint8_t* cursor;    // next free byte (bump pointer)
    uint8_t* limit;     // region end
    
    // Overflow chain: when a single data zone block fills up,
    // allocate another and chain them
    struct DataZone* next;
    
    Pool* pool;         // underlying memory pool
} DataZone;

void* data_zone_alloc(DataZone* dz, size_t size) {
    size = ALIGN_UP(size, 16);  // 16-byte alignment for cache line friendliness
    
    if (dz->cursor + size > dz->limit) {
        // Current block full. Allocate a new block.
        dz = data_zone_grow(dz, size);
    }
    
    void* ptr = dz->cursor;
    dz->cursor += size;
    return ptr;
}
```

**Key property**: No individual deallocation. When a List grows its `items[]` array, the old array is abandoned (wasted space until next GC). This is the standard trade-off for bump allocators — fast allocation in exchange for deferred reclamation.

**On GC**: Surviving data chunks are copied to a **tenured data zone** (or a new nursery data zone). The old data zone blocks are entirely reclaimed. This recovers all wasted space from abandoned array growth, dead temporaries, etc.

### 5.3 String and Symbol Refactoring

Currently, `String` and `Symbol` use C flexible array members — the `chars[]` data is inline with the struct:

```c
// Current layout (single contiguous allocation):
// [GCHeader 16B][String.len 4B][chars... NB]['\0']
typedef struct String {
    uint32_t len;
    char chars[];   // flexible array member — data is inline
} String;
```

For the dual-zone design, we need the struct in the **object zone** (fixed size, non-moving) and the character data in the **data zone** (variable size, compactable). Two approaches:

#### Approach A: Split String into Struct + Data Pointer

```c
typedef struct String {
    uint32_t len;
    char* chars;     // pointer to data zone allocation
} String;
// Object zone: 16B GCHeader + 12B struct (len + chars*) = 28B → 32-byte size class
// Data zone:   len+1 bytes for character data
```

**Pros**: Clean separation. Object zone entries are uniform 32-byte slots. Data zone compaction just copies chars and updates `str->chars`.

**Cons**:
- Extra indirection on every string access (`str->chars` is now a pointer follow instead of `str + offsetof(chars)`)
- 8 bytes overhead per string (the `char*` pointer field)
- Short strings (common in Lambda: map keys, tag names, attribute names) pay disproportionate overhead
- **Breaks all existing code** that accesses `str->chars` expecting inline data (the compiler handles this transparently for flexible array members, but the memory layout changes)

#### Approach B: Keep Strings as Single Allocations in the Object Zone

```c
// Unchanged layout:
typedef struct String {
    uint32_t len;
    char chars[];
} String;
// Object zone: 16B GCHeader + 4B len + (len+1)B chars = variable size
```

Strings are allocated in the object zone as variable-size entries. The object zone free list handles variable sizes via the size-class bins — small strings (≤12 chars) fit in the 32-byte class, medium strings (≤44 chars) fit in the 64-byte class, etc. Larger strings use the large-object allocator.

**Pros**: Zero code changes. No extra indirection. Short strings are cache-local.

**Cons**: Object zone slots are not perfectly uniform (strings span multiple size classes). Some internal fragmentation (a 5-char string uses a 32-byte slot).

#### Recommendation: Approach B (Keep Inline, Size-Class Bins)

Strings in Lambda are overwhelmingly short (map keys like `"name"`, `"age"`, `"id"`; element tags like `"div"`, `"span"`, `"p"`). These are typically interned in the NamePool and never GC-managed at all. Runtime-generated strings (from concatenation, `string()` conversion, etc.) are also usually short.

Approach A adds an extra pointer dereference on **every** string character access across the entire codebase (print, compare, hash, format, etc.) for a marginal improvement in object zone uniformity. The size-class free list in Approach B already handles variable sizes efficiently.

Furthermore, many interned strings (NamePool-allocated) are not GC-managed at all — `gc_is_managed()` returns false. Only runtime-generated strings (from `heap_strcpy()`) live in the GC heap. For these, the size-class approach is sufficient.

If profiling later shows object zone fragmentation is a problem for string-heavy workloads, Approach A can be adopted as a targeted optimization.

### 5.4 Current Sub-Allocation Pattern vs. Data Zone

Today, variable-size data chunks are allocated with `malloc()`/`calloc()` (not from the GC heap):

```c
// Current: lambda-data-runtime.cpp
Map* map(int type_index) {
    Map* mp = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);  // GC heap
    mp->data = calloc(1, byte_size);                          // malloc heap!!
    ...
}

// Current: lambda-data-runtime.cpp - array_fill()
arr->items = (Item*)calloc(cnt, sizeof(Item));                // malloc heap
```

The `gc_finalize_all_objects()` function in `lambda-mem.cpp` explicitly walks all GC objects and `free()`s these sub-allocations at shutdown.

**With the data zone**: These `calloc()`/`malloc()` calls become `data_zone_alloc()`:

```c
// Proposed:
Map* map(int type_index) {
    Map* mp = (Map*)object_zone_calloc(oz, sizeof(Map), LMD_TYPE_MAP);
    mp->data = data_zone_alloc(dz, byte_size);  // bump-allocated, fast
    ...
}

arr->items = (Item*)data_zone_alloc(dz, cnt * sizeof(Item));  // bump-allocated
```

**Benefits**:
- `data_zone_alloc()` is O(1) bump pointer, faster than `malloc()` size-class search
- All data chunks are contiguous in the data zone → better cache locality
- On GC, the entire data zone can be reclaimed at once (surviving chunks copied to tenured)
- No more `free()` calls in `gc_finalize_all_objects()` for sub-allocations — the data zone handles it

### 5.5 Array/List Growth: Abandoned Chunks

When a List grows beyond its capacity, the current code does:
```c
// lambda-data-runtime.cpp: list_push()
arr->items = (Item*)realloc(arr->items, new_capacity * sizeof(Item));
```

With the data zone, `realloc()` is not available (bump allocator). Instead:
```c
void list_push(List* list, Item item) {
    if (list->length >= list->capacity) {
        int64_t new_cap = list->capacity * 2;
        Item* new_items = (Item*)data_zone_alloc(dz, new_cap * sizeof(Item));
        memcpy(new_items, list->items, list->length * sizeof(Item));
        // old items[] is abandoned — space reclaimed on next GC
        list->items = new_items;
        list->capacity = new_cap;
    }
    list->items[list->length++] = item;
}
```

The abandoned old `items[]` chunk wastes space until the next GC compacts the data zone. This is the standard trade-off for bump allocators and is identical to how Go slices, Java `ArrayList` backing arrays, and most GC'd runtimes work.

For typical Lambda workloads (functional data transformation, not repeated mutation), arrays are constructed once and never resized. The abandonment cost is near zero.

### 5.6 Tenured Region

Objects that survive a nursery GC cycle are long-lived. For the initial implementation:

- **Tenured object zone**: Identical structure to nursery object zone (size-class free lists). Surviving object structs stay in place (non-moving). "Old" objects are distinguished by a generation bit in `gc_flags`, not by physical location.
- **Tenured data zone**: A second bump-allocated region. Surviving data chunks are copied here from the nursery data zone during GC.

Future optimization: Full GC could also compact the tenured data zone when it exceeds a threshold. Tenured objects would have their data pointers updated (same single-fixup-per-object approach).

For the initial phase, a simple generation tag suffices:
```c
// In gc_header_t:
// gc_flags bits 2-3: generation
#define GC_GEN_NURSERY  0
#define GC_GEN_TENURED  1
```

Objects in generation 0 (nursery) are collected on every minor GC. Objects promoted to generation 1 (tenured) are only collected during a full GC.

### 5.7 Mark-and-Sweep Algorithm

The core algorithm from the Phase 1 document (Section 2.8) applies directly, using the existing `gc_header_t.marked` field:

```c
// --- Mark Phase ---
void gc_mark(GCHeap* gc) {
    // 1. Push all roots onto the mark stack
    gc_push_roots(gc);  // context->result, shadow stack, module globals
    
    // 2. Trace all reachable objects
    while (!mark_stack_empty(gc)) {
        gc_header_t* obj = mark_stack_pop(gc);
        obj->marked = 1;
        gc_trace_object(gc, obj);  // push reachable children
    }
}

void gc_trace_object(GCHeap* gc, gc_header_t* header) {
    void* obj = (void*)(header + 1);
    uint16_t tag = header->type_tag;
    
    switch (tag) {
        case LMD_TYPE_INT64:
        case LMD_TYPE_FLOAT:
        case LMD_TYPE_DTIME:
        case LMD_TYPE_STRING:
        case LMD_TYPE_SYMBOL:
        case LMD_TYPE_BINARY:
        case LMD_TYPE_DECIMAL:
        case LMD_TYPE_ARRAY_INT:
        case LMD_TYPE_ARRAY_INT64:
        case LMD_TYPE_ARRAY_FLOAT:
        case LMD_TYPE_RANGE:
            break;  // no outgoing Item pointers
        
        case LMD_TYPE_LIST:
        case LMD_TYPE_ARRAY: {
            List* list = (List*)obj;
            for (int64_t i = 0; i < list->length; i++) {
                gc_mark_item(gc, list->items[i]);
            }
            break;
        }
        case LMD_TYPE_MAP:
        case LMD_TYPE_OBJECT: {
            Map* map = (Map*)obj;
            gc_trace_map_data(gc, (TypeMap*)map->type, map->data);
            break;
        }
        case LMD_TYPE_ELEMENT: {
            Element* elmt = (Element*)obj;
            // trace children (inherited from List)
            for (int64_t i = 0; i < elmt->length; i++) {
                gc_mark_item(gc, elmt->items[i]);
            }
            // trace attributes
            gc_trace_map_data(gc, (TypeMap*)elmt->type, elmt->data);
            break;
        }
        case LMD_TYPE_FUNC: {
            Function* fn = (Function*)obj;
            if (fn->closure_env) {
                gc_mark_closure_env(gc, fn);
            }
            break;
        }
        case LMD_TYPE_VMAP: {
            VMap* vm = (VMap*)obj;
            gc_trace_vmap(gc, vm);
            break;
        }
    }
}

void gc_mark_item(GCHeap* gc, Item item) {
    TypeId tid = get_type_id(item);
    if (tid <= LMD_TYPE_INT) return;  // inline scalar (int56, bool, null)
    
    void* ptr = item_to_pointer(item);  // extract raw pointer from tagged Item
    if (!gc_is_managed(gc, ptr)) return;  // arena, name pool, const pool — skip
    
    gc_header_t* header = gc_get_header(ptr);
    if (header->marked) return;  // already visited
    
    header->marked = 1;  // mark GRAY (on stack) + BLACK (traced) collapsed to single bit
    mark_stack_push(gc, header);
}
```

### 5.8 Sweep Phase with Data Compaction

```c
// --- Sweep Phase ---
void gc_sweep(GCHeap* gc) {
    // Phase A: compact surviving data chunks from nursery data zone to tenured data zone
    gc_compact_data_zone(gc);
    
    // Phase B: sweep object zone — free dead objects, return to free list
    gc_header_t* current = gc->all_objects;
    gc_header_t* prev = NULL;
    
    while (current) {
        gc_header_t* next = current->next;
        
        if (current->marked) {
            // Alive — reset mark bit for next cycle
            current->marked = 0;
            prev = current;
        } else {
            // Dead — finalize sub-allocations and return to free list
            gc_finalize_object(current);
            
            // Unlink from all_objects list
            if (prev) prev->next = next;
            else gc->all_objects = next;
            
            // Return to object zone free list
            object_zone_free(gc->object_zone, current);
            gc->object_count--;
        }
        current = next;
    }
    
    // Phase C: reset nursery data zone (all surviving data already copied to tenured)
    data_zone_reset(gc->nursery_data);
}

void gc_compact_data_zone(GCHeap* gc) {
    // Walk all MARKED objects. For each one that has data in the nursery data zone,
    // copy that data to the tenured data zone and update the struct's pointer.
    gc_header_t* current = gc->all_objects;
    while (current) {
        if (!current->marked) { current = current->next; continue; }
        
        void* obj = (void*)(current + 1);
        uint16_t tag = current->type_tag;
        
        switch (tag) {
            case LMD_TYPE_LIST:
            case LMD_TYPE_ARRAY: {
                List* list = (List*)obj;
                if (list->items && is_in_nursery_data(gc, list->items)) {
                    size_t size = list->capacity * sizeof(Item);
                    Item* new_items = (Item*)data_zone_alloc(gc->tenured_data, size);
                    memcpy(new_items, list->items, list->length * sizeof(Item));
                    list->items = new_items;  // single pointer fixup
                }
                break;
            }
            case LMD_TYPE_MAP:
            case LMD_TYPE_OBJECT: {
                Map* map = (Map*)obj;
                if (map->data && is_in_nursery_data(gc, map->data)) {
                    size_t size = map->data_cap > 0 ? map->data_cap
                                  : ((TypeMap*)map->type)->byte_size;
                    void* new_data = data_zone_alloc(gc->tenured_data, size);
                    memcpy(new_data, map->data, size);
                    map->data = new_data;  // single pointer fixup
                }
                break;
            }
            case LMD_TYPE_ELEMENT: {
                Element* elmt = (Element*)obj;
                // compact items[]
                if (elmt->items && is_in_nursery_data(gc, elmt->items)) {
                    size_t size = elmt->capacity * sizeof(Item);
                    Item* new_items = (Item*)data_zone_alloc(gc->tenured_data, size);
                    memcpy(new_items, elmt->items, elmt->length * sizeof(Item));
                    elmt->items = new_items;
                }
                // compact data
                if (elmt->data && is_in_nursery_data(gc, elmt->data)) {
                    size_t size = elmt->data_cap > 0 ? elmt->data_cap
                                  : ((TypeMap*)elmt->type)->byte_size;
                    void* new_data = data_zone_alloc(gc->tenured_data, size);
                    memcpy(new_data, elmt->data, size);
                    elmt->data = new_data;
                }
                break;
            }
            case LMD_TYPE_FUNC: {
                Function* fn = (Function*)obj;
                if (fn->closure_env && is_in_nursery_data(gc, fn->closure_env)) {
                    // env size not stored — retrieve from alloc header or fixed convention
                    size_t size = gc_closure_env_size(fn);
                    void* new_env = data_zone_alloc(gc->tenured_data, size);
                    memcpy(new_env, fn->closure_env, size);
                    fn->closure_env = new_env;
                }
                break;
            }
            // Strings (Approach B): chars inline, no data zone pointer — nothing to compact
            // Decimal: mpd_t* is allocated by libmpdec, not data zone — nothing to compact
        }
        current = current->next;
    }
}
```

### 5.9 `gc_is_managed()` and `is_in_nursery_data()`

```c
// Check if a pointer is within a data zone (nursery or tenured)
bool is_in_nursery_data(GCHeap* gc, void* ptr) {
    DataZone* dz = gc->nursery_data;
    while (dz) {
        if ((uint8_t*)ptr >= dz->base && (uint8_t*)ptr < dz->limit) return true;
        dz = dz->next;
    }
    return false;
}

// Check if a pointer is managed by the GC at all (object zone or data zone)
bool gc_is_managed(GCHeap* gc, void* ptr) {
    // Check object zone slabs
    for (int cls = 0; cls < GC_NUM_SIZE_CLASSES; cls++) {
        for (ObjectSlab* slab = gc->object_zone->slabs[cls]; slab; slab = slab->next) {
            if ((uint8_t*)ptr >= slab->base &&
                (uint8_t*)ptr < slab->base + slab->slot_count * gc_size_classes[cls]) {
                return true;
            }
        }
    }
    // Check data zones (nursery + tenured)
    if (is_in_nursery_data(gc, ptr)) return true;
    if (is_in_tenured_data(gc, ptr)) return true;
    // Check large objects list
    // ...
    return false;
}
```

For performance, `gc_is_managed()` can be optimized with min/max address bounds per zone, avoiding the linked-list walk on every call.

### 5.10 Root Set (Initial Implementation)

For Phase 5, roots are:

| Root | How Scanned |
|------|-------------|
| `context->result` | Single `gc_mark_item()` call |
| Module BSS globals | Registered via `gc_add_global_root()` at module init; scanned linearly |
| Transpiled local variables | **Conservative stack scanning** as initial approach (see below) |

**Conservative stack scanning** (initial approach):
- Lambda already has `_lambda_stack_base` and `_lambda_stack_limit` (from stack overflow protection)
- Scan the native C stack between these bounds, treating every 8-byte-aligned value as a potential `Item`
- For each value, check if it looks like a valid tagged pointer to a GC-managed object: `gc_is_managed(item_to_pointer(value))`
- If so, mark it as a root

**Pros**: Zero transpiler changes. Works with existing MIR JIT output immediately.

**Cons**: False positives (integers that happen to match a GC heap address keep dead objects alive). This is safe (no premature collection) but may cause some dead objects to survive one extra cycle.

**Future**: Replace with shadow stack for precise root scanning (requires transpiler changes to emit `GC_ROOT_PUSH`/`GC_ROOT_POP`).

### 5.11 Collection Triggers

| Trigger | Action |
|---------|--------|
| `data_zone_alloc()` fails (data zone full) | Minor GC: mark + compact data zone + sweep nursery objects |
| Tenured data zone exceeds threshold (e.g., 4 MB) | Full GC: mark + sweep all objects + compact tenured data |
| `gc_collect()` explicit call | Full GC (for testing/debugging) |
| Context teardown | Bulk free via `pool_destroy` (no sweep needed) |

### 5.12 Numeric Temporaries

The current `gc_nursery` (bump-allocated `int64_t`/`double`/`DateTime` values) would be absorbed into the **object zone** (8-byte values → 16-byte bin including GCHeader). This unifies all allocation paths through a single GC heap, eliminating the separate `gc_nursery_t`.

Alternative: Keep the separate `gc_nursery` for numeric temporaries with no GCHeader overhead (since they have no outgoing pointers and don't need tracing). They'd be bulk-freed on GC like the data zone. This saves 16 bytes of GCHeader per numeric value but requires maintaining a separate allocator.

**Recommendation**: Start with unified object zone for simplicity. Optimize with a separate numeric nursery if benchmarks show the per-value overhead is significant.

---

## 6. Closure Environment Data Tracking

Closure environments (`closure_env`) are allocated as data chunks. The transpiler generates a struct with captured `Item` fields:

```c
// Transpiler output for a closure capturing x and y:
typedef struct Env_f42 {
    Item x;
    Item y;
} Env_f42;

// Allocation:
Env_f42* env = (Env_f42*)data_zone_alloc(dz, sizeof(Env_f42));
env->x = captured_x;
env->y = captured_y;
fn->closure_env = env;
```

During GC tracing, the closure env must be scanned for `Item` fields. The GC needs to know how many `Item` fields exist. Options:

1. **Convention**: All closure env `Item` fields are packed at the start. Store the field count in the `Function` struct or a small header in the env.
2. **Fixed layout**: Treat the entire env as packed `Item[]` — field count is `env_size / sizeof(Item)`.

Since closure envs are transpiler-generated with known layouts, option 2 works if all captured values are `Item`-typed (which they are in Lambda — all values are `Item`).

```c
void gc_mark_closure_env(GCHeap* gc, Function* fn) {
    if (!fn->closure_env) return;
    // env_size must be known — store in GCHeader's alloc_size field
    // (env is in data zone, which doesn't have GCHeaders)
    // Instead, store field count in Function struct:
    size_t field_count = fn->closure_field_count;  // new field needed
    Item* fields = (Item*)fn->closure_env;
    for (size_t i = 0; i < field_count; i++) {
        gc_mark_item(gc, fields[i]);
    }
}
```

**Note**: This requires adding a `closure_field_count` field to `Function`, or encoding it in the env allocation. Since `Function` already has unused alignment padding after `arity` (uint8_t), a `uint8_t closure_field_count` fits without increasing the struct size.

---

## 7. Data Zone Sizing

When `list->items` or `map->data` currently use `calloc()`/`malloc()`, those calls are separate from the GC heap. With the data zone, we need to decide how data zone allocations and `free()` calls in `gc_finalize_all_objects()` should interact.

**Phase 5 migration approach**:
1. Replace `calloc()`/`malloc()` for sub-allocations with `data_zone_alloc()`
2. Remove corresponding `free()` calls from `gc_finalize_all_objects()` — the data zone handles this
3. `gc_finalize_all_objects()` only handles `mpd_t*` (Decimal, managed by libmpdec) and any other external library allocations

**Data zone default sizes**:
- Nursery data zone: 256 KB (matches original proposal)
- Tenured data zone: starts at 1 MB, grows as needed
- Triggers full GC when tenured data exceeds 4 MB

---

## 8. Implementation Plan

### Step 1: Object Zone Allocator ✅ Complete
- Implemented `gc_object_zone_t` with size-class free lists in `lib/gc_object_zone.h` / `lib/gc_object_zone.c`
- 7 size classes: 16, 32, 48, 64, 96, 128, 256 bytes (user data, excluding GCHeader)
- Slab-based allocation with per-class slot counts (512/256/256/128/128/64/32 slots per slab)
- Wired `gc_heap_alloc()`/`gc_heap_calloc()` to route ≤256B allocations through object zone
- Large objects (>256B) fall back to `pool_alloc()` from the underlying memory pool
- Forward declaration pattern resolves circular include between `gc_object_zone.h` ↔ `gc_heap.h`
- All 385 Lambda tests pass ✅

**Files created**: `lib/gc_object_zone.h`, `lib/gc_object_zone.c`

### Step 2: Data Zone Allocator ✅ Complete
- Implemented `gc_data_zone_t` bump allocator in `lib/gc_data_zone.h` / `lib/gc_data_zone.c`
- 256 KB default block size, 16-byte alignment, block-chain overflow for large allocations
- `gc_data_zone_reset()`: zeros and resets all block cursors for post-GC nursery clearing
- `gc_data_zone_copy()`: alloc + memcpy helper for GC compaction
- Replaced all `malloc()`/`calloc()` for `items[]`, `data` buffers with `heap_data_alloc()`/`heap_data_calloc()` across:
  - `lambda-data-runtime.cpp`: 8 `malloc` → `heap_data_alloc`, 3 `calloc` → `heap_data_calloc` (array_fill, array_int_new/fill, array_int64_new/fill, array_float_new/fill, map_fill, object_fill, elmt_fill)
  - `lambda-data.cpp`: `expand_list()` — `realloc` → `heap_data_alloc` + `memcpy` (old buffer abandoned)
  - `lambda-eval.cpp`: `convert_specialized_to_generic()` — `calloc` → `heap_data_calloc`, removed 3× `free(old_items)`; `fn_join()` — 3× `malloc` → `heap_data_alloc` for typed array items; `map_rebuild_for_type_change()` — `calloc` → `heap_data_calloc`, removed `free(old_data)` for is_heap containers
  - `lambda-eval-num.cpp`: removed 6× `free()` on GC-managed ArrayFloat objects/items in division-by-zero error paths
- Simplified `gc_finalize_all_objects()` to only handle VMap (`vtable->destroy`) and Decimal (`mpd_del`)
- Added weak symbol fallback in `lambda-data.cpp` for input library linking (overridden by real definition)
- All 385 Lambda tests pass ✅

**Files created**: `lib/gc_data_zone.h`, `lib/gc_data_zone.c`
**Files modified**: `lambda-data-runtime.cpp`, `lambda-data.cpp`, `lambda-eval.cpp`, `lambda-eval-num.cpp`, `lambda-mem.cpp`, `build_lambda_config.json`

### Step 3: Mark Phase ✅ Complete
- Implemented `gc_mark_item()`, `gc_trace_object()`, growable mark stack in `lib/gc_heap.c`
- `gc_trace_object()` handles all Lambda types:
  - List/Array: scan `items[]` for Item references
  - Map/Object: walk `ShapeEntry` linked list, trace pointer-type fields in packed `data` buffer
  - Element: trace both `items[]` children and `data` attributes
  - Function: trace `closure_env` (deferred — needs `closure_field_count`)
  - VMap: deferred (complex external backing store)
  - Scalars (int64, double, String, Symbol, etc.): no outgoing references, nothing to trace
- TypeMap struct offset corrected: `shape` field is at byte offset 32 (Type(2) + padding(6) + length(8) + byte_size(8) + type_index(4) + padding(4) = 32)
- **Type tag constants** corrected to exactly match `lambda.h` `EnumTypeId` enum (RAW_POINTER=0, NULL=1, BOOL=2, INT=3, INT64=4, ..., STRING=10, ..., LIST=12, ..., FUNC=23)
- **`item_to_ptr()` rewritten** for robust pointer extraction:
  - `item == 0` → NULL (literal zero)
  - Tags 1-3 (NULL/BOOL/INT) → NULL (inline values, no pointer)
  - Tag 0 with `item != 0` → container pointer (raw heap address on 64-bit)
  - Tags 4-11 → tagged pointer (mask off upper byte)
  - Tags ≥12 → container (safety fallback)
- **Root scanning implemented**:
  - Root slot registry: `gc_register_root()`/`gc_unregister_root()` with 256-slot array for BSS globals, `context->result`, etc.
  - Conservative stack scanning: `gc_scan_stack()` walks aligned 8-byte values from SP to stack base, marking any that point into the object zone
  - BSS global registration: `register_bss_gc_roots()` in `mir.c` walks MIR modules to find `_gvar_*` BSS items and registers their addresses as GC roots
  - Extra roots parameter in `gc_collect()` for caller-provided Items
- `gc_collect()` full pipeline: mark registered roots → mark extra roots → conservative stack scan → process mark stack (drain gray objects) → compact → sweep → reset nursery
- Re-entrancy guard (`gc->collecting` flag) prevents recursive collection

**Files modified**: `lib/gc_heap.h`, `lib/gc_heap.c`, `lambda/lambda-mem.cpp`, `lambda/transpiler.hpp`, `lambda/mir.c`, `lambda/transpile-mir.cpp`, `lambda/runner.cpp`

### Step 4: Sweep with Data Compaction ✅ Complete
- Implemented `gc_sweep()` in `lib/gc_heap.c`: walks `all_objects` list, frees dead objects to object zone free lists, resets mark bits on survivors
- Implemented `gc_compact_data()`: for each marked object with nursery data zone pointers, copies data to tenured data zone and updates the struct's pointer (single fixup per object)
- Handles List/Array `items[]`, Map/Object/Element `data`, Element `items[]` + `data`, Function `closure_env`
- `gc_data_zone_reset()` called after compaction to reclaim entire nursery data zone
- Full collection flow: mark → compact → sweep → reset
- **Bug fix**: Explicitly freed objects (via `gc_heap_pool_free`) are now correctly returned to the object zone free list during sweep (previously leaked)

**Files modified**: `lib/gc_heap.c`

### Step 5: Collection Triggers ✅ Complete
- Threshold-based auto-trigger: `gc_data_alloc()` checks `gc_data_zone_used() >= gc_threshold` before each allocation
- `GC_DATA_ZONE_THRESHOLD` default: 75% of `GC_DATA_ZONE_BLOCK_SIZE` (192 KB)
- Callback mechanism: `gc_set_collect_callback()` sets the function called when threshold is exceeded
- `heap_gc_collect()` in `lambda-mem.cpp` provides the callback: reads stack bounds (inline asm for SP on ARM64/x86_64), calls `gc_collect()` with full root scanning
- Re-entrancy guard prevents recursive GC during collection
- **26 GC unit tests** in `test/test_gc_heap_gtest.cpp` — all passing ✅
  - Covers: allocation, data zone, root registration, marking, tracing, collection (live/dead), compaction (nursery→tenured + data preservation), multi-cycle, threshold triggers, re-entrancy guard, explicit free, collection stats, conservative stack scanning, stress test (1000 objects)
- **389/389 Lambda baseline tests pass** ✅ (no regressions)

**Files created**: `test/test_gc_heap_gtest.cpp`
**Files modified**: `lib/gc_heap.h`, `lib/gc_heap.c`, `lambda/lambda-mem.cpp`, `build_lambda_config.json`

### Step 6: Closure Cycle Collection — Not Started
- Add `closure_field_count` to `Function` struct
- Update transpiler to emit closure field count
- Implement `gc_mark_closure_env()` that scans closure env `Item` fields
- Write test: closure cycle (Function A → env → Container C → Function A) is properly collected
- Verify: leak that exists under old ref_cnt system is now collected

---

## 9. Risks and Mitigations

| Risk | Severity | Mitigation |
|------|----------|------------|
| **Conservative stack scan false positives** | Low | Safe (retains too much, never too little). Replace with shadow stack later for precision. |
| **Data zone waste from abandoned arrays** | Medium | Only between GC cycles. For typical functional Lambda code, arrays are built once and not resized. Monitor with allocation stats. |
| **Object zone fragmentation** | Low | Size-class bins limit internal fragmentation to at most 2× per object. Lambda has a small number of distinct struct sizes. |
| **Closure env size unknown** | Medium | Add `closure_field_count` to Function struct, or prepend a size tag to env allocation in data zone. |
| **Decimal `mpd_t` finalization** | Low | `mpd_t` is allocated by libmpdec (outside GC). Continue to finalize via `mpd_del()` during sweep. |
| **Map/Element `data` size ambiguity** | Medium | Use `data_cap` field when available; fall back to `((TypeMap*)type)->byte_size` for exact initial size. Or track actual allocated size in a small header in the data zone. |
| **GC during MIR JIT execution** | Medium | GC triggers only at allocation points (safe points). MIR-compiled code's local variables are on the C stack and visible to conservative scanning. |

---

## 10. Success Criteria

| Criterion | Measurement | Status |
|-----------|-------------|--------|
| All existing tests pass | 389 Lambda tests green | ✅ 389/389 pass (Steps 1–5) |
| Radiant tests pass | 2149 Radiant tests (51 pre-existing layout failures, not GC-related) | ✅ No regressions |
| GC unit tests pass | 26 tests covering mark/sweep/compact/trigger/stack-scan | ✅ 26/26 pass |
| Long-running loop memory bounded | Loop allocating 1M temporary arrays stays under constant peak memory | ⬜ Runtime validation pending |
| Closure cycle collected | Test: cyclic closure reference is reclaimed (ref_cnt version leaks) | ⬜ Requires Step 6 |
| Data zone fully reclaims | After GC, nursery data zone usage returns to zero | ✅ CompactMovesNurseryData test verifies |
| No pointer corruption | Stress test: build large data structures, trigger multiple GC cycles, verify data integrity | ✅ StressAllocCollect + CompactPreservesListData tests verify |
| Allocation performance | Benchmark: bump allocation (data zone) ≥ current `malloc()` throughput | ⬜ Requires benchmarking |
---

## 11. Known Issues & Deferred Work

### `mark_editor.cpp` — `realloc()` on Arena-Allocated Items

Three sites in `mark_editor.cpp` use raw `realloc()` to grow `items[]` arrays for Input arena-allocated objects (constructed by `MarkBuilder`). These are **not** GC-managed (they belong to Input arena lifecycle), so `heap_data_alloc` is not the right fix. They should use `arena_alloc` + `memcpy` instead of `realloc`.

| Line | Function | Description |
|------|----------|-------------|
| 1291 | `elmt_insert_child` | Element children `items[]` — single insert |
| 1381 | `elmt_insert_children` | Element children `items[]` — batch insert |
| 1684 | `array_insert` | Array `items[]` |

**Impact**: Low risk — these code paths only execute during procedural element/array mutation of Input-parsed data. The `realloc()` works today because arena backing is ultimately `malloc`-based (`rpmalloc`), but it is technically incorrect as it bypasses the arena's tracking. Should be fixed when MarkEditor is refactored.

### `fn_type()` in `lambda-eval.cpp` — Raw `calloc()`

Line 1498 uses `calloc(1, sizeof(TypeType) + sizeof(Type))` to allocate a `TypeType` wrapper. This is a **type metadata allocation**, not a data container — it is not tracked by GC and is never freed (persists for context lifetime). Low priority; could be moved to `pool_calloc` for consistency.

### VMap Tracing — Deferred

`gc_trace_object()` does not yet trace `VMap` backing data (HashMap entries containing `Item` keys/values). VMap uses an external `vtable->destroy` pattern. Tracing would require a `vtable->trace` callback to walk all stored Items. Deferred until VMaps are used in GC-triggering scenarios.

### Closure Environment Tracing — Partially Deferred

`gc_trace_object()` recognizes `LMD_TYPE_FUNC` with `closure_env` but cannot yet scan the env's `Item` fields without knowing the field count. Requires adding `closure_field_count` to `Function` struct and updating the transpiler (Step 6).