# Proposal: Garbage Collector for Lambda Runtime

## Status: Proposal
## Date: 2025-02-24

---

## Executive Summary

This document proposes replacing Lambda's current **num_stack + reference counting + frame-based cleanup** memory management with a **tri-color mark-and-sweep garbage collector** backed by a region-based allocator. The goal is to eliminate the architectural complexity of num_stack tagged pointers, simplify the transpiler's boxing/unboxing obligations, fix latent memory leaks from closure cycles, and remove the 10-bit/16-bit ref_cnt overflow limits — all while preserving Lambda's performance characteristics and zero-pause scripting semantics.

---

## 1. Current Memory Architecture: Analysis and Pain Points

### 1.1 How Memory Works Today

Lambda uses a four-tier allocation strategy:

| Tier | Allocator | What Lives Here | Lifetime |
|------|-----------|-----------------|----------|
| **num_stack** | Chunked linked-list stack | int64, double, DateTime temporaries | Frame-scoped (reset on `frame_end`) |
| **Heap pool** | rpmalloc heap (`pool_alloc`) | Strings, Symbols, Decimals, Containers, Functions | Ref-counted + frame-scoped + pool-destroy |
| **Arena** | Bump-pointer (on top of Pool) | Input document data (parsed JSON/XML/HTML) | Arena-scoped (bulk destroy) |
| **NamePool** | Hash-interned strings | Map keys, element tags, attribute names | Pool-scoped |

Reference counting is used for heap-allocated objects:
- `String.ref_cnt`: 10-bit field (max 1024 references)
- `Container.ref_cnt`: 16-bit field (max 65535 references)
- `Function.ref_cnt`: 16-bit field

Frame-based cleanup (`frame_start`/`frame_end`) manages temporaries created during collection construction. The `frame_end` function walks the heap entries list backward, freeing objects with `ref_cnt == 0` and skipping objects that have been adopted into live data structures.

### 1.2 Pain Points

#### P1: num_stack Tagged Pointer Complexity

The num_stack exists solely because int64, double, and DateTime values need stable memory addresses for the tagged-pointer Item representation. This creates:

- **Double-boxing bugs**: MIR transpiler must track whether a value is raw or already tagged. `push_l_safe()` was invented as a workaround for INT64 double-boxing, with a known false-positive range at `[2.88e17, 3.60e17]`.
- **Transpiler divergence**: C2MIR and MIR direct transpilers handle num_stack values inconsistently, producing different `TypeId` tags for the same operations.
- **Frame coupling**: `frame_start()` saves `num_stack.total_length`; `frame_end()` resets it. This couples numeric temporary lifetime to collection construction frames, making it impossible to use num_stack values across frame boundaries without boxing to heap.
- **Memory waste**: Each num_value_t is 8 bytes, but the chunked linked-list adds per-chunk overhead (capacity, used, next, prev, index = 40 bytes per chunk). Chunks double in size (16 → 32 → 64 → ...), leading to geometric memory growth for numeric-heavy workloads.

#### P2: Reference Counting Limitations

- **No cycle collection**: Closures can form reference cycles (`Function → closure_env → Container → ... → Function`). Pure functional code (`fn`) avoids this by design, but procedural code (`pn`) with mutable captured variables can leak.
- **Closure env not freed recursively**: When a `Function` is freed, its `closure_env` Items are **not** ref-decremented. Captured containers with bumped ref_cnts leak until `pool_destroy`.
- **Overflow risk**: String `ref_cnt` is 10 bits (max 1024). A string used as a map key across >1024 maps will silently overflow to 0, causing premature free and use-after-free.
- **Manual, error-prone**: Every place that stores a reference must manually `ref_cnt++`, and every place that overwrites must `ref_cnt--`. Missing any increment/decrement is a silent bug (leak or use-after-free). There are 20+ `ref_cnt++`/`ref_cnt--` sites scattered across `lambda-mem.cpp`, `lambda-eval.cpp`, `mark_editor.cpp`, `lambda-decimal.cpp`, `name_pool.cpp`.

#### P3: Frame-Based Cleanup Fragility

- `frame_end()` scans the entire `entries` ArrayList backward, checking each entry's type tag. With large data construction (e.g., 10,000-element arrays), this O(n) scan runs on every collection literal.
- The entries list mixes allocation tracking with frame sentinels, using tagged pointers (`LMD_CONTAINER_HEAP_START << 56 | stack_pos`). This couples three concerns: allocation registration, frame delimitation, and num_stack position saving.
- Infinite loop detection code (`loop_count > original_length + 100`) in `frame_end()` suggests past reliability issues.

#### P4: Dual Ownership Model (is_heap Flag)

Every container has an `is_heap` bit distinguishing heap-allocated (ref-counted, individually freed) from arena-allocated (bulk-freed with arena). Every free path must check this flag:
```cpp
if (!cont->is_heap) return;  // arena-owned, skip
```
This bifurcation doubles the testing surface and creates bugs when arena containers are accidentally passed to heap-managed code paths.

#### P5: Transpiler Complexity

Both transpilers must emit correct `frame_start()`/`frame_end()` brackets around every collection literal, correct `ref_cnt` manipulation for map field stores, and correct boxing/unboxing for every function boundary. The current architecture exposes all of these concerns to transpiler authors. A GC would hide most of this behind a simple allocate-and-forget API.

### 1.3 What Works Well (Preserve These)

| Aspect | Why It Works | GC Must Preserve |
|--------|-------------|-----------------|
| **Arena for input docs** | Bulk-allocate parsed data, bulk-free when done | Keep arena for input paths; GC is for runtime |
| **NamePool interning** | Identity comparison for keys/tags, sharing | Name strings remain interned, not GC'd |
| **pool_destroy as safety net** | All memory freed at context end regardless of leaks | GC heap also destroyed at context end |
| **Item tagged union** | Efficient 64-bit representation with inline scalars | Keep inline int56/bool/null packing |
| **Immutable pure functional data** | No cycles possible in `fn` code | GC benefits procedural `pn` code most |

---

## 2. Proposed Design: Region-Based Tri-Color Mark-and-Sweep GC

### 2.1 Design Goals

1. **Eliminate num_stack**: int64/double/DateTime values allocated inline in GC heap regions, with stable addresses for tagged pointers. No frame-coupled position tracking.
2. **Eliminate manual ref_cnt**: All heap objects traced by GC. No `ref_cnt++`/`ref_cnt--` sites. No overflow risk.
3. **Collect closure cycles**: Tri-color marking traverses the full live object graph, reclaiming cyclic garbage.
4. **Preserve arena model**: Input documents continue using arena allocation. GC manages only runtime-generated data.
5. **Minimal transpiler impact**: Transpilers emit allocation calls (no frame brackets, no ref_cnt management). GC roots registered automatically.
6. **Zero-pause for short scripts**: Scripts that finish quickly never trigger GC — bulk free at context end. GC only runs for long-running or memory-intensive workloads.

### 2.2 Architecture Overview

```
┌──────────────────────────────────────────────────────────┐
│                     EvalContext                          │
│  ┌────────────┐  ┌──────────┐  ┌────────────────────┐   │
│  │  GC Heap   │  │  Arena   │  │    NamePool         │   │
│  │  (runtime) │  │  (input) │  │   (interned names)  │   │
│  │            │  │          │  │                      │   │
│  │  Regions   │  │  Chunks  │  │   Hash table         │   │
│  │  ┌──────┐  │  │  ┌────┐ │  │                      │   │
│  │  │Nursry│  │  │  │ C1 │ │  └────────────────────┘   │
│  │  ├──────┤  │  │  ├────┤ │                            │
│  │  │Tenurd│  │  │  │ C2 │ │  ┌────────────────────┐   │
│  │  ├──────┤  │  │  └────┘ │  │    Root Set          │   │
│  │  │Large │  │  │         │  │  - eval stack         │   │
│  │  └──────┘  │  └─────────┘  │  - global vars        │   │
│  │            │               │  - call frames         │   │
│  └────────────┘               │  - module BSS          │   │
│                               └────────────────────────┘   │
└──────────────────────────────────────────────────────────┘
```

### 2.3 GC Heap Regions

The GC heap is divided into three region types:

#### Nursery Region (Bump Allocator)
- **Purpose**: Fast allocation for short-lived temporaries (equivalent to current frame-scoped allocations).
- **Size**: 256 KB per nursery block. Grows by adding new blocks.
- **Allocation**: Bump pointer — O(1), no fragmentation.
- **Collection**: When nursery is full, run a minor GC (mark live objects, copy survivors to tenured, reset nursery pointer).

```c
typedef struct NurseryRegion {
    uint8_t* base;         // region start
    uint8_t* cursor;       // bump pointer (next free byte)
    uint8_t* limit;        // region end
    struct NurseryRegion* next;  // linked list of nursery blocks
} NurseryRegion;

static inline void* nursery_alloc(GCHeap* gc, size_t size) {
    size = ALIGN_UP(size, 16);
    if (gc->nursery->cursor + size > gc->nursery->limit) {
        gc_minor_collect(gc);  // or allocate new nursery block
    }
    void* ptr = gc->nursery->cursor;
    gc->nursery->cursor += size;
    return ptr;
}
```

#### Tenured Region (Free-List Allocator)
- **Purpose**: Long-lived objects that survived at least one nursery collection.
- **Allocation**: Segregated free-list by size class (16, 32, 64, 128, 256, 512, 1024, large).
- **Collection**: Full mark-and-sweep when tenured space exceeds threshold.

#### Large Object Region
- **Purpose**: Objects >4 KB (large arrays, large strings).
- **Allocation**: Direct `pool_alloc` from underlying rpmalloc heap.
- **Collection**: Tracked in a separate list; swept during full GC.

### 2.4 Object Header

Every GC-managed object gets a small header prepended to its data:

```c
typedef struct GCHeader {
    uint32_t gc_flags;     // mark bits (2), generation (2), type category (4), padding
    uint32_t size;         // allocation size (for sweep/copy)
} GCHeader;

// gc_flags layout:
// bits 0-1: mark color (WHITE=0, GRAY=1, BLACK=2)
// bits 2-3: generation (0=nursery, 1=tenured)
// bits 4-7: object category for tracing (see GCObjectCategory)

typedef enum {
    GC_CAT_NUMERIC,       // int64, double, DateTime — no outgoing pointers
    GC_CAT_STRING,        // String, Symbol, Binary — no outgoing pointers
    GC_CAT_DECIMAL,       // Decimal — has mpd_t* sub-allocation
    GC_CAT_LIST,          // List, Array — trace items[]
    GC_CAT_ARRAY_TYPED,   // ArrayInt, ArrayInt64, ArrayFloat — no outgoing Item pointers
    GC_CAT_MAP,           // Map — trace via ShapeEntry walk
    GC_CAT_ELEMENT,       // Element — trace attrs + content items
    GC_CAT_VMAP,          // VMap — trace via vtable enumerate
    GC_CAT_FUNCTION,      // Function — trace closure_env
    GC_CAT_OBJECT,        // Object — trace via shape
    GC_CAT_CLOSURE_ENV,   // Closure env struct — trace Item fields
} GCObjectCategory;
```

**Size overhead**: 8 bytes per object. This replaces:
- `ref_cnt` fields (2–4 bytes per object, currently embedded)
- `is_heap` flag (1 bit in Container.flags)
- `entries` ArrayList tracking (8 bytes per pointer per allocation)

Net change is approximately neutral to slight improvement in per-object overhead, with significant reduction in bookkeeping data structure size (no entries ArrayList needed).

### 2.5 Item Representation Changes

The core `Item` tagged union is **preserved unchanged**. The key difference is where compound scalar values are stored:

| Type | Current Storage | Proposed Storage |
|------|----------------|-----------------|
| int (int56) | Inline in Item | Inline in Item (unchanged) |
| bool | Inline in Item | Inline in Item (unchanged) |
| null | Inline in Item | Inline in Item (unchanged) |
| int64 | num_stack slot → tagged pointer | GC nursery allocation → tagged pointer |
| double | num_stack slot → tagged pointer | GC nursery allocation → tagged pointer |
| DateTime | num_stack slot → tagged pointer | GC nursery allocation → tagged pointer |
| String | heap_alloc → tagged pointer | GC nursery/tenured → tagged pointer |
| Container | heap_calloc → direct pointer | GC nursery/tenured → direct pointer |

The tagged pointer encoding remains identical:
```c
// These macros stay the same:
#define l2it(ptr) (((uint64_t)LMD_TYPE_INT64 << 56) | (uint64_t)(ptr))
#define d2it(ptr) (((uint64_t)LMD_TYPE_FLOAT << 56) | (uint64_t)(ptr))
```

The only change is that `ptr` now points into a GC region instead of num_stack or pool-allocated memory.

### 2.6 Allocation API

Replace the current multi-function allocation surface with a unified GC allocator:

```c
// Primary allocation — all runtime allocations go through this
void* gc_alloc(GCHeap* gc, size_t size, GCObjectCategory cat);

// Convenience wrappers (replace current functions)
int64_t* gc_alloc_int64(GCHeap* gc, int64_t value);    // replaces push_l()
double*  gc_alloc_double(GCHeap* gc, double value);     // replaces push_d()
DateTime* gc_alloc_datetime(GCHeap* gc, DateTime value);// replaces push_k()
String*  gc_alloc_string(GCHeap* gc, const char* src, int len);  // replaces heap_strcpy()
Array*   gc_alloc_array(GCHeap* gc);                    // replaces array()
List*    gc_alloc_list(GCHeap* gc);                     // replaces list()
Map*     gc_alloc_map(GCHeap* gc);                      // replaces map()
Function* gc_alloc_function(GCHeap* gc);                // replaces heap_calloc(sizeof(Function))

// push_l_safe() is eliminated — no double-boxing possible with GC allocation
```

**Key simplification**: No `frame_start()`/`frame_end()` calls needed. The transpiler just allocates. The GC collects when memory pressure warrants it.

### 2.7 Root Set Management

The GC needs to know which objects are live (reachable). Roots are:

| Root Source | Registration Method |
|-------------|-------------------|
| **Eval result** | `context->result` — always a root |
| **JIT call stack** | Walk native call stack via frame pointers, or use a shadow stack (see below) |
| **Module BSS globals** | Each module's global `let`/`var` variables — registered at module init |
| **Closure envs** | Reached via live Function objects (transitive) |
| **Transpiled local variables** | Shadow stack or conservative stack scan |

#### Shadow Stack (Recommended Approach)

A shadow stack is a side array maintained by the transpiler that records all GC-managed pointers held in local variables:

```c
// Thread-local shadow stack
__thread Item* gc_shadow_stack[MAX_SHADOW_DEPTH];
__thread int gc_shadow_top = 0;

// Transpiler emits these around function bodies:
#define GC_ROOT_PUSH(item_ptr)  gc_shadow_stack[gc_shadow_top++] = (item_ptr)
#define GC_ROOT_POP(n)          gc_shadow_top -= (n)
```

The transpiler emits `GC_ROOT_PUSH` for each local variable that holds a GC-managed pointer, and `GC_ROOT_POP` at function exit. During GC, the shadow stack is scanned as part of the root set.

**Trade-off**: ~2 instructions per local variable per function call. This is comparable to the current `frame_start()`/`frame_end()` overhead (which involves ArrayList append + backward scan).

#### Alternative: Conservative Stack Scanning

Scan the native C stack for values that look like pointers into GC regions. This requires no transpiler changes but is:
- Platform-dependent (need stack bounds)
- May retain false positives (integers that happen to look like heap pointers)
- Already partially supported (Lambda has `_lambda_stack_base` and `_lambda_stack_limit` from its stack overflow protection)

**Recommendation**: Start with shadow stack for correctness, with conservative scanning as a fallback research direction.

### 2.8 Tri-Color Mark-and-Sweep Algorithm

#### Mark Phase

```c
void gc_mark(GCHeap* gc) {
    // 1. Color all objects WHITE (unmarked) — implicit, WHITE=0
    
    // 2. Push all roots onto the mark stack as GRAY
    gc_push_roots(gc);  // eval result, shadow stack, module globals
    
    // 3. Process GRAY objects until mark stack is empty
    while (!mark_stack_empty(gc)) {
        GCHeader* obj = mark_stack_pop(gc);
        gc_trace_object(gc, obj);    // trace outgoing pointers
        obj->gc_flags |= GC_BLACK;  // mark BLACK (fully traced)
    }
}

void gc_trace_object(GCHeap* gc, GCHeader* header) {
    void* obj = (void*)(header + 1);  // data starts after header
    switch (GC_CATEGORY(header)) {
        case GC_CAT_NUMERIC:
        case GC_CAT_STRING:
        case GC_CAT_ARRAY_TYPED:
            break;  // no outgoing pointers
            
        case GC_CAT_LIST: {
            List* list = (List*)obj;
            for (int64_t i = 0; i < list->length; i++) {
                gc_mark_item(gc, list->items[i]);
            }
            break;
        }
        case GC_CAT_MAP: {
            Map* map = (Map*)obj;
            TypeMap* type = (TypeMap*)map->type;
            gc_trace_shape(gc, type->shape, map->data);
            break;
        }
        case GC_CAT_FUNCTION: {
            Function* fn = (Function*)obj;
            if (fn->closure_env) {
                gc_mark_closure_env(gc, fn);  // trace captured Items
            }
            break;
        }
        // ... similar for ELEMENT, VMAP, OBJECT
    }
}

void gc_mark_item(GCHeap* gc, Item item) {
    TypeId tid = get_type_id(item);
    if (tid <= LMD_TYPE_INT) return;  // inline scalar, not a pointer
    
    void* ptr = NULL;
    if (tid >= LMD_TYPE_LIST) {
        ptr = (void*)item.item;  // container: direct pointer
    } else {
        ptr = (void*)(item.item & 0x00FFFFFFFFFFFFFF);  // tagged pointer: mask off tag
    }
    
    if (!gc_is_managed(gc, ptr)) return;  // arena-owned or name-pool, skip
    
    GCHeader* header = GC_HEADER(ptr);  // header is 8 bytes before ptr
    if (GC_COLOR(header) != GC_WHITE) return;  // already marked
    
    header->gc_flags |= GC_GRAY;
    mark_stack_push(gc, header);
}
```

#### Sweep Phase

```c
void gc_sweep(GCHeap* gc) {
    // Sweep nursery: reset bump pointer (all unmarked objects are dead)
    // Survivors were copied to tenured during mark phase
    gc->nursery->cursor = gc->nursery->base;
    
    // Sweep tenured: walk free-list blocks, free WHITE objects
    for (each block in tenured) {
        GCHeader* header = block_start;
        while (header < block_end) {
            if (GC_COLOR(header) == GC_WHITE) {
                gc_finalize_object(header);  // free sub-allocations (mpd_t, items[], data)
                freelist_return(gc, header, header->size);
            } else {
                header->gc_flags &= ~GC_COLOR_MASK;  // reset to WHITE for next cycle
            }
            header = NEXT_OBJECT(header);
        }
    }
    
    // Sweep large objects: free WHITE entries
    for (each entry in gc->large_objects) {
        if (GC_COLOR(entry->header) == GC_WHITE) {
            gc_finalize_object(entry->header);
            pool_free(gc->pool, entry->header);
        }
    }
}
```

#### Collection Triggers

| Trigger | Action |
|---------|--------|
| Nursery full (256 KB used) | Minor GC: mark + copy survivors to tenured |
| Tenured exceeds threshold (e.g., 4 MB) | Full GC: mark all roots, sweep all regions |
| Explicit `gc_collect()` call | Full GC (for testing/debugging) |
| Context teardown | Bulk free via `pool_destroy` (no GC needed) |

### 2.9 Closure Environment Tracing

The critical improvement over ref_cnt: closures are traced correctly.

**Current problem**: `closure_env` is a raw `void*` struct. When a `Function` is freed, its `closure_env` Items are never decremented, leaking captured containers.

**GC solution**: The closure env struct is itself a GC-allocated object with category `GC_CAT_CLOSURE_ENV`. The transpiler records the number and offsets of `Item` fields at closure creation:

```c
typedef struct ClosureEnvMeta {
    int field_count;          // number of Item fields
    // Item fields are packed at the start of the env struct
} ClosureEnvMeta;

// Transpiler emits:
Env_fXXX* env = (Env_fXXX*)gc_alloc(gc, sizeof(Env_fXXX), GC_CAT_CLOSURE_ENV);
// GC traces all Item-sized fields in the env struct
```

During GC tracing, `gc_mark_closure_env()` scans the env's Item fields:

```c
void gc_mark_closure_env(GCHeap* gc, Function* fn) {
    GCHeader* env_header = GC_HEADER(fn->closure_env);
    int field_count = env_header->size / sizeof(Item);  // env is packed Items
    Item* fields = (Item*)fn->closure_env;
    for (int i = 0; i < field_count; i++) {
        gc_mark_item(gc, fields[i]);
    }
}
```

This correctly handles cycles:
```
Function A → closure_env → Item[0] = Container C
Container C → items[5] = Function A
```
The tri-color algorithm marks A as GRAY, traces its env, marks C as GRAY, traces C's items, encounters A again (now GRAY or BLACK), and stops — cycle collected correctly.

### 2.10 Non-GC-Heap Data: Input Documents, Const Pool, and NamePool

A critical design concern: the GC manages only runtime-allocated data. Three categories of objects live **outside** the GC heap and must be handled correctly when the GC encounters pointers to them.

#### 2.10.1 Input Document Data (Arena-Allocated)

When `fn_input("file.json")` runs, it creates an `Input` with its own `Pool` + `Arena`. All parsed containers (`List`, `Map`, `Element`, strings) are bump-allocated from that arena with `is_heap = 0`. These objects have **no `GCHeader`** and must never be traced into or freed by the GC.

**Current handling**: `free_container()` guards with `if (!cont->is_heap) return;`

**GC approach**: The GC uses **pointer range checks** via `gc_is_managed()`. It knows the address bounds of its own nursery and tenured regions. Any pointer falling outside those ranges is treated as an external (non-managed) object:

```c
bool gc_is_managed(GCHeap* gc, void* ptr) {
    // check nursery regions
    for (NurseryRegion* r = gc->nursery; r; r = r->next) {
        if ((uint8_t*)ptr >= r->base && (uint8_t*)ptr < r->limit) return true;
    }
    // check tenured blocks
    for (TenuredBlock* b = gc->tenured->blocks; b; b = b->next) {
        if ((uint8_t*)ptr >= b->base && (uint8_t*)ptr < b->limit) return true;
    }
    // check large object list
    // ... similar bounds check
    return false;  // arena, AST pool, name pool — not managed
}
```

During marking, when `gc_mark_item()` encounters a pointer to an arena-allocated container, `gc_is_managed()` returns false and tracing **stops at that boundary** — it doesn't attempt to mark or free the arena object or its children. The arena's entire lifetime is managed by `Input::pool` → `pool_destroy()`.

**Edge case — cross-domain references**: An arena container's children may point to GC-heap objects (e.g., a runtime-generated string stored into a parsed document via `mark_editor`). This is safe because the GC-managed string is reachable from the runtime variable that triggered the store, not from the arena object. The arena object is simply invisible to the GC.

#### 2.10.2 Const Pool Data (AST Pool-Allocated)

The `Script::const_list` holds compile-time constants — `String*`, `Decimal*`, `double*`, `int64_t*`, `DateTime*` — allocated from the **AST pool** (`Script::pool`), not the runtime heap. Transpiled code accesses them via macros:

```c
const_s2it(index)  →  s2it(rt->consts[index])   // String* from const_list
const_d2it(index)  →  d2it(rt->consts[index])    // double* from const_list
const_l2it(index)  →  l2it(rt->consts[index])    // int64_t* from const_list
const_k2it(index)  →  k2it(rt->consts[index])    // DateTime* from const_list
const_c2it(index)  →  c2it(rt->consts[index])    // Decimal* from const_list
```

These pointers live in the AST pool for the entire script lifetime. They are **never freed individually** — the AST pool is destroyed after the runner finishes.

**GC approach**: Same pointer range check. `const_list` entries are allocated from `Script::pool` (a separate rpmalloc heap), so `gc_is_managed()` returns false. The GC never traces into or collects const data. Crucially, `const_d2it`/`const_l2it` already produce tagged pointers directly to AST pool memory (they bypass num_stack today) — this continues unchanged with the GC.

#### 2.10.3 NamePool Strings (Interned, Pool-Allocated)

`heap_create_name()` goes through the `NamePool`, which interns strings in the context's pool. These are shared structural identifiers (map keys, element tags). They're pool-allocated and live until pool destruction.

**GC approach**: Same treatment as const data — `gc_is_managed()` returns false for name pool strings. They are never collected.

#### 2.10.4 Summary: Ownership Boundary

| Data Category | Allocator | Has GCHeader? | GC Action | Lifetime |
|--------------|-----------|---------------|-----------|----------|
| **Runtime containers** (array, list, map) | GC nursery/tenured | Yes | Trace + collect | Until unreachable |
| **Runtime strings** (`heap_strcpy`) | GC nursery/tenured | Yes | Trace + collect | Until unreachable |
| **Runtime numerics** (int64, double, DateTime) | GC nursery | Yes | Trace + collect | Until unreachable |
| **Closure envs** | GC nursery/tenured | Yes | Trace Items inside | Until Function unreachable |
| **Input doc data** (parsed JSON/XML) | Arena (`Input::arena`) | No | Skip via `gc_is_managed` | Until `arena_destroy` |
| **Const pool** (`Script::const_list`) | AST Pool (`Script::pool`) | No | Skip via `gc_is_managed` | Script lifetime |
| **NamePool strings** | Context Pool | No | Skip via `gc_is_managed` | Context lifetime |

The key invariant: **`gc_is_managed()` is the single gatekeeper**. It checks whether a pointer falls within GC region address ranges. Everything allocated outside those ranges (arena, AST pool, name pool) is invisible to the GC and managed by its own allocator's lifecycle. This replaces the current `is_heap` flag with a more robust pointer-range mechanism that doesn't require tagging every object.

---

## 3. Migration Strategy

### 3.1 Phased Approach

The migration is organized into 5 phases, each independently testable and shippable:

#### Phase 0: GC Infrastructure (no behavior change)
- Implement `GCHeap`, `NurseryRegion`, `GCHeader`, `gc_alloc()`.
- Implement tri-color mark-and-sweep (mark stack, tracing per GC category, sweep).
- Unit tests for allocator and GC correctness.
- **No changes to Lambda runtime yet.**

#### Phase 1: Replace num_stack with GC Nursery
- Replace `push_l()`, `push_d()`, `push_k()` to allocate from GC nursery instead of num_stack.
- Remove `push_l_safe()` — the double-boxing problem disappears because allocation always returns a fresh pointer.
- Remove num_stack position tracking from `frame_start()`/`frame_end()`.
- **Test**: All 113 transpiler tests pass with identical output.

#### Phase 2: Replace Heap Tracking with GC
- Replace `heap_alloc()`/`heap_calloc()` → `gc_alloc()`.
- Remove the `entries` ArrayList and `frame_start()`/`frame_end()`.
- Add shadow stack to transpilers (both C2MIR and MIR direct).
- Remove `is_heap` flag — all GC-managed objects are traced uniformly. Arena objects have no GCHeader and are never traced.
- **Test**: All transpiler tests + input parser tests pass.

#### Phase 3: Remove Reference Counting
- Remove `ref_cnt` fields from `String`, `Symbol`, `Container`, `Function`, `Decimal`.
- Remove all `ref_cnt++`/`ref_cnt--` sites (~20+ locations across 5 files).
- Remove `free_item()`, `free_container()`, `free_map_item()`, `check_memory_leak()`.
- Finalization (freeing sub-allocations like `items[]`, `map->data`, `mpd_t*`) moves to GC sweep.
- **Test**: Full test suite + memory profiling to verify no leaks.

#### Phase 4: Transpiler Simplification
- Remove `frame_start()`/`frame_end()` emission from both transpilers.
- Remove boxing/unboxing special cases for num_stack consistency (e.g., `POST_PROCESS_INT64`).
- Simplify `transpile_box_item()` — no need to distinguish num_stack vs heap boxing.
- Update `_store_i64`/`_store_f64` swap workarounds (may no longer be needed if register allocation changes).
- **Test**: Full test suite + performance benchmarks.

### 3.2 Compatibility Boundary: Arena Objects

Arena-allocated objects (from input parsers via `MarkBuilder`) are **not** GC-managed. They have no `GCHeader` and are never traced. The GC distinguishes arena vs GC objects via pointer range checks:

```c
bool gc_is_managed(GCHeap* gc, void* ptr) {
    // Check if ptr falls within any GC region
    // Nursery, tenured blocks, or large object list
    // Arena pointers will not match any GC region
}
```

When an arena-allocated container is stored in a GC-managed container (e.g., parsed document assigned to a runtime variable), the GC traces into it but does not attempt to free it. The GC category determines whether an object needs finalization:

```c
void gc_finalize_object(GCHeader* header) {
    // Only finalize GC-managed objects (header exists)
    // Arena objects are never passed to this function
}
```

### 3.3 Struct Layout Changes

```c
// BEFORE: String with ref_cnt
typedef struct String {
    uint32_t len:22;
    uint32_t ref_cnt:10;  // REMOVED
    char chars[];
} String;

// AFTER: String without ref_cnt (GC header is separate)
typedef struct String {
    uint32_t len;          // full 32 bits available now
    char chars[];
} String;

// BEFORE: Container with ref_cnt + is_heap
struct Container {
    TypeId type_id;
    uint8_t flags;     // is_content, is_spreadable, is_heap, is_data_migrated
    uint16_t ref_cnt;  // REMOVED
};

// AFTER: Container without ref_cnt
struct Container {
    TypeId type_id;
    uint8_t flags;     // is_content, is_spreadable (is_heap removed)
    // 2 bytes saved per container
};

// GC header is prepended (not embedded)
// [GCHeader 8 bytes][Container data...]
//                    ^ pointer returned to user
```

**Binary compatibility**: The `Item` union and tagged pointer encoding are unchanged. All `type_id()`, `get_string()`, `get_int64()` accessors work identically. The change is invisible to transpiled code.

### 3.4 Files Modified Per Phase

| Phase | Files Modified | Files Removed/Added |
|-------|---------------|-------------------|
| **Phase 0** | None | Add: `lib/gc.h`, `lib/gc.c` |
| **Phase 1** | `lambda-mem.cpp`, `lambda.h` | Remove num_stack from `frame_start`/`frame_end` |
| **Phase 2** | `lambda-mem.cpp`, `transpile.cpp`, `transpile-mir.cpp`, `runner.cpp` | Remove `entries` ArrayList |
| **Phase 3** | `lambda.h`, `lambda.hpp`, `lambda-data.hpp`, `lambda-mem.cpp`, `lambda-eval.cpp`, `mark_editor.cpp`, `lambda-decimal.cpp`, `name_pool.cpp` | Remove `free_item`, `free_container`, `free_map_item`, `check_memory_leak` |
| **Phase 4** | `transpile.cpp`, `transpile-mir.cpp` | Remove frame bracket emission |

---

## 4. Performance Analysis

### 4.1 Allocation Cost

| Operation | Current | Proposed |
|-----------|---------|----------|
| `push_l(int64)` | num_stack push: chunk bounds check + write + tag | GC nursery bump: pointer add + write + tag |
| `heap_strcpy(str)` | pool_alloc + memcpy + entries append | GC nursery bump + memcpy |
| `array()` | heap_calloc + entries append + frame_start | GC nursery bump + zero-fill |
| `list_end()` | frame_end (backward scan) | Nothing (no frame management) |

**Nursery allocation is faster** than `pool_alloc` for small objects because bump allocation is just a pointer increment (1 instruction), while `rpmalloc_heap_alloc` involves thread-local heap lookup and size-class dispatch.

**Overhead removed**: No `arraylist_append` per allocation, no `frame_start`/`frame_end` per collection literal.

### 4.2 Collection Cost

GC collection is not free, but it is amortized:

| Scenario | Frequency | Cost |
|----------|-----------|------|
| **Minor GC** (nursery full) | Every ~256 KB allocated | Mark roots + copy survivors. For typical Lambda scripts with short-lived temporaries, most nursery objects are dead → fast sweep. |
| **Full GC** (tenured threshold) | Rare for short scripts | Mark entire live graph + sweep tenured. Only needed for long-running or memory-intensive workloads. |
| **No GC** (script completes quickly) | Most Lambda scripts | Zero GC overhead. `pool_destroy` at context end frees everything. |

### 4.3 Throughput Impact Estimate

For typical functional Lambda scripts (data transformation, document processing):
- **Allocation**: ~10-20% faster (bump vs pool_alloc, no entries tracking)
- **Collection**: ~0-5% overhead for minor GCs (most objects are dead temporaries)
- **Net**: ~5-15% improvement for allocation-heavy workloads

For procedural scripts with long-running loops and mutation:
- **Allocation**: Same improvement
- **Collection**: Minor GCs more frequent; occasional full GC pauses
- **Net**: Comparable performance with correct cycle collection (currently these scripts leak)

### 4.4 Memory Usage

| Metric | Current | Proposed |
|--------|---------|----------|
| Per-object metadata | `ref_cnt` (2-4 bytes) + `entries` pointer (8 bytes) | `GCHeader` (8 bytes) |
| Global bookkeeping | `entries` ArrayList (8 bytes × N allocs) + num_stack chunks | Shadow stack (8 bytes × M locals) + region metadata |
| Peak memory | Pool retains fragmentation | Nursery compacts on collection |

**Net memory usage**: Comparable for small scripts; better for large data processing (nursery compaction reduces fragmentation vs pool allocator).

---

## 5. Detailed Design: GC Data Structures

### 5.1 GCHeap

```c
typedef struct GCHeap {
    // Regions
    NurseryRegion* nursery;       // bump-allocated young generation
    TenuredRegion* tenured;       // free-list old generation
    ArrayList* large_objects;     // individually tracked large objects
    
    // Mark stack
    GCHeader** mark_stack;
    int mark_top;
    int mark_capacity;
    
    // Root set
    Item** shadow_stack;          // pointer to thread-local shadow stack
    int* shadow_top;              // pointer to thread-local shadow top
    Item* result_root;            // pointer to context->result
    ArrayList* global_roots;      // module BSS variables
    
    // Underlying pool (for tenured/large allocations and bulk cleanup)
    Pool* pool;
    
    // Statistics
    size_t total_allocated;
    size_t nursery_collections;
    size_t full_collections;
    
    // Thresholds
    size_t nursery_size;          // default 256 KB
    size_t tenured_threshold;     // default 4 MB (triggers full GC)
} GCHeap;
```

### 5.2 GC API (lib/gc.h)

```c
// Lifecycle
GCHeap* gc_create(Pool* pool, size_t nursery_size);
void gc_destroy(GCHeap* gc);

// Allocation
void* gc_alloc(GCHeap* gc, size_t size, GCObjectCategory cat);
void* gc_calloc(GCHeap* gc, size_t size, GCObjectCategory cat);

// Numeric convenience (replace num_stack push functions)
int64_t* gc_push_int64(GCHeap* gc, int64_t value);
double* gc_push_double(GCHeap* gc, double value);
DateTime* gc_push_datetime(GCHeap* gc, DateTime value);

// Collection
void gc_collect_minor(GCHeap* gc);   // nursery only
void gc_collect_full(GCHeap* gc);    // all regions
void gc_collect_if_needed(GCHeap* gc);  // check thresholds

// Root management
void gc_add_global_root(GCHeap* gc, Item* root);
void gc_remove_global_root(GCHeap* gc, Item* root);

// Query
bool gc_is_managed(GCHeap* gc, void* ptr);
size_t gc_stats_allocated(GCHeap* gc);

// Shadow stack (thread-local, managed by transpiled code)
void gc_shadow_push(Item* ptr);
void gc_shadow_pop(int count);
```

### 5.3 Integration with EvalContext

```c
typedef struct EvalContext : Context {
    GCHeap* gc;                     // replaces: Heap* heap + num_stack_t* num_stack
    Pool* ast_pool;
    NamePool* name_pool;
    void* type_info;
    Item result;
    mpd_context_t* decimal_ctx;
    SchemaValidator* validator;
    ArrayList* debug_info;
    const char* current_file;
    LambdaError* last_error;
} EvalContext;
```

### 5.4 Transpiled Code Changes

#### Before (current, C2MIR path):

```c
// Array literal [1, 2, 3]
Array* _arr1 = array();  // heap_calloc + frame_start
array_fill(_arr1, 3,
    i2it(1),  // inline int56
    i2it(2),
    i2it(3));   // frame_end called inside

// Float literal 3.14
double* _tmp = num_stack_push_double(context->num_stack, 3.14);
Item _v = {.item = d2it(_tmp)};

// Map field store
str->ref_cnt++;
*(String**)field_ptr = str;
```

#### After (GC path):

```c
// Array literal [1, 2, 3]
Array* _arr1 = gc_alloc_array(gc);
// fill items directly, no frame management
_arr1->items[0] = i2it(1);
_arr1->items[1] = i2it(2);
_arr1->items[2] = i2it(3);
_arr1->length = 3;

// Float literal 3.14
double* _tmp = gc_push_double(gc, 3.14);
Item _v = {.item = d2it(_tmp)};

// Map field store — no ref_cnt
*(String**)field_ptr = str;
```

---

## 6. Risks and Mitigations

| Risk | Severity | Mitigation |
|------|----------|------------|
| **GC pause spikes** for large heaps | Medium | Nursery is small (256 KB); minor GC is fast. Full GC is rare. For interactive use (REPL), trigger GC between evaluations. |
| **Conservative stack scan false positives** | Low | Use shadow stack (precise), not conservative scanning. |
| **Arena/GC pointer confusion** | Medium | `gc_is_managed()` checks region bounds. Arena objects never get GCHeaders. Clear documentation of ownership boundary. |
| **Transpiler regression** | High | Phase 1 can be tested with existing 113-test suite before proceeding. Each phase has its own test checkpoint. |
| **Sub-allocation finalization** | Medium | `gc_finalize_object` must free `mpd_t*` (Decimal), `items[]` (List/Array, if not inline), `data` (Map/Element). These are allocated from the GC pool, so pool_destroy catches leaks. |
| **C2MIR generated code changes** | Medium | C2MIR emits C code that calls runtime functions by name. Renaming `push_l()` → `gc_push_int64()` requires updating import_resolver. Can be done via compatibility macros during transition. |
| **Thread safety** | Low | Lambda is single-threaded per EvalContext. GC state is context-local. No contention. |
| **MIR reordering with GC safepoints** | Medium | GC can only trigger at allocation points (not arbitrary code). MIR's optimizer cannot move allocations across safepoints because they are external calls. |

---

## 7. Alternatives Considered

### 7.1 Keep Ref-Counting, Add Cycle Collector

Add a periodic cycle detection pass (like Python's gc) on top of the existing ref_cnt infrastructure.

**Pros**: Minimal changes to existing code. Immediate collection of non-cyclic garbage.
**Cons**: Does not fix num_stack complexity. Does not simplify transpiler. Adds a second GC mechanism. Python's cycle collector adds ~5-10% overhead.

**Verdict**: Rejected — does not address the primary pain points (num_stack, transpiler complexity, frame management).

### 7.2 Deferred Reference Counting (DRC)

Batch ref_cnt updates using a write buffer, processing them periodically. Add cycle detection with trial deletion.

**Pros**: Incremental collection. Maintains deterministic finalization order.
**Cons**: Still requires ref_cnt fields. Still requires manual increment/decrement sites. Write buffer adds complexity. Doesn't fix num_stack.

**Verdict**: Rejected — too complex for insufficient benefit.

### 7.3 Scope-Based RAII with Move Semantics

Model Lambda values as Rust-style owned types with automatic drop at scope exit. Closures would clone captured values.

**Pros**: Deterministic, zero-overhead collection. No GC pauses.
**Cons**: Requires extensive transpiler changes to emit move/clone operations. Lambda's functional data flow (expressions returning values through nested scopes) maps poorly to strict ownership. Would require a borrow checker to avoid unnecessary clones.

**Verdict**: Rejected — architecturally alien to Lambda's scripting semantics.

### 7.4 Pure Region/Arena (No Tracing GC)

Extend the arena model to all runtime allocations. Each function call gets a region; region freed on return.

**Pros**: Very fast allocation. Deterministic. No GC pauses.
**Cons**: Values returned from functions must be copied to the caller's region. Closures that outlive their defining function require special handling (copy env to longer-lived region). Functional composition chains would create deep region stacks.

**Verdict**: Partially adopted — the nursery region IS an arena. But tracing is needed for long-lived and cross-function objects.

---

## 8. Success Criteria

| Criterion | Measurement |
|-----------|-------------|
| **Correctness** | All 113 transpiler tests pass with identical output |
| **Closure leak fix** | New test: closure cycle is collected (ref_cnt version leaks) |
| **No double-boxing** | `push_l_safe()` removed; no INT64 boxing workarounds needed |
| **Transpiler simplification** | `frame_start`/`frame_end` emission removed from both transpilers |
| **Ref_cnt elimination** | Zero `ref_cnt++`/`ref_cnt--` sites in codebase |
| **Performance** | Allocation-heavy benchmarks ≥ current throughput |
| **Memory** | Peak memory ≤ 1.1× current for representative workloads |

---

## 9. Estimated Effort

| Phase | Estimated Duration | Dependencies |
|-------|-------------------|--------------|
| Phase 0: GC infrastructure | 2–3 weeks | None |
| Phase 1: Replace num_stack | 1–2 weeks | Phase 0 |
| Phase 2: Replace heap tracking | 2–3 weeks | Phase 1 |
| Phase 3: Remove ref_cnt | 1–2 weeks | Phase 2 |
| Phase 4: Transpiler simplification | 1–2 weeks | Phase 3 |
| **Total** | **7–12 weeks** | |

Each phase is independently testable against the existing test suite, allowing incremental validation and the ability to stop at any phase boundary if priorities change.

---

## 10. Appendix: Current Code Inventory

### Files Containing ref_cnt Operations

| File | Operations | Count |
|------|-----------|-------|
| `lambda-mem.cpp` | `ref_cnt--`, `ref_cnt > 0` checks | 8 |
| `lambda-eval.cpp` | `ref_cnt++`, `ref_cnt--` for map field store | 6 |
| `mark_editor.cpp` | `ref_cnt++`, `ref_cnt--` for document mutation | 8 |
| `lambda-decimal.cpp` | `ref_cnt++`, `ref_cnt--` for decimal sharing | 2 |
| `name_pool.cpp` | `ref_cnt++` for parent pool references | 1 |

### Files Containing num_stack Operations

| File | Operations |
|------|-----------|
| `lambda-mem.cpp` | `push_l()`, `push_d()`, `push_k()`, `push_l_safe()`, `frame_start()`, `frame_end()` |
| `lambda-data-runtime.cpp` | `push_l()`, `push_d()` in runtime functions |
| `lambda-eval.cpp` | `push_l()`, `push_d()`, `push_k()` for literal evaluation |
| `transpile.cpp` | `push_l`, `push_d` emission in generated C |
| `transpile-mir.cpp` | `push_l`, `push_d`, `push_l_safe` MIR import/call |
| `runner.cpp` | `num_stack_create()`, `num_stack_destroy()` |

### Files Containing frame_start/frame_end

| File | Context |
|------|---------|
| `lambda-mem.cpp` | Implementation |
| `lambda-data-runtime.cpp` | Called from `array()`, `list()`, `map()`, `elmt()`, `object()`, `*_fill()`, `*_end()` |
| `transpile.cpp` | Emitted in generated C for for-expressions |
| `transpile-mir.cpp` | MIR calls for for-expressions |
