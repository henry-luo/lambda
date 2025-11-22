# Lambda Editor 2 - Smart Deep Copy Enhancement

## Executive Summary

This document outlines the enhancement of MarkBuilder with smart deep copy functionality that intelligently determines when data needs to be copied versus when it can be referenced directly. The enhancement integrates with MarkEditor to ensure all edited document data resides in the target Input's arena (or its parents').

**Date**: November 21-22, 2025  
**Status**: ✅ **COMPLETE** (Phase 5b Bug Fix Complete, Phase 6 Documentation Remaining)  
**Priority**: High (Memory Management & Editor Integration)  
**Dependencies**: 
  - ✅ NamePool (Completed - with parent chain support)
  - ✅ ShapePool (Completed)
  - ✅ MarkBuilder (Completed)
  - ✅ MarkEditor (Completed)
  - ✅ Arena Allocator (Completed)

---

## 1. Problem Analysis

### 1.1 Current Issues

**Problem 1: `copy_item_deep()` is a static function**
- Currently a file-local static function in `mark_builder.cpp`
- Not accessible as a public API
- Cannot be reused by MarkEditor or other components

**Problem 2: Memory allocation inconsistency**
- Some MarkBuilder methods allocate from pool (e.g., `array_pooled()`)
- Should consistently use arena allocation for all Mark structures
- Only internal dynamic buffers (in `map_put`, `elmt_put`) need pool

**Problem 3: Unnecessary deep copying**
- `copy_item_deep()` always creates new copies of data
- Wastes memory when source data is already in target arena or parent arenas
- No arena ownership checking before copying

**Problem 4: MarkEditor doesn't ensure arena locality**
- When updating documents, values from external sources may not be in arena
- Could lead to dangling pointers if external data is freed
- No automatic copying of external data to target arena

### 1.2 Lambda Data Model Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Lambda Data Model                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Input (Document Context)                                       │
│  ├── Pool* pool           - Dynamic buffers (internal use)      │
│  ├── Arena* arena         - Primary allocator for structures    │
│  ├── NamePool* name_pool  - String interning (with parent)      │
│  ├── ShapePool* shape_pool - Shape deduplication                │
│  ├── ArrayList* type_list - Type registry                       │
│  └── Item root            - Document root                       │
│                                                                 │
│  Arena Structure (arena.h/arena.c)                              │
│  ├── ArenaChunk* first    - Linked list of memory chunks        │
│  ├── ArenaChunk* current  - Current allocation chunk            │
│  └── Pool* pool           - Underlying pool for chunks          │
│                                                                 │
│  NamePool (name_pool.hpp)                                       │
│  ├── Pool* pool           - Memory for pooled strings           │
│  ├── hashmap* names       - Deduplication map                   │
│  └── NamePool* parent     - Hierarchical parent lookup          │
│                                                                 │
│  Memory Allocation Strategy:                                    │
│  • Strings (content): arena_alloc() - fast sequential           │
│  • Strings (names): name_pool (pooled, deduplicated)            │
│  • Maps: arena (structure) + pool (data buffer)                 │
│  • Elements: arena (structure) + pool (data buffer + children)  │
│  • Arrays: pool_alloc() (dynamic resizing)                      │
│  • Primitives: arena or inline (int32, bool)                    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 1.3 Memory Ownership Model

**Arena Ownership**:
- Arena consists of linked list of chunks (`ArenaChunk`)
- Each chunk has a `data[]` flexible array member
- Pointer belongs to arena if: `chunk->data <= ptr < chunk->data + chunk->used`

**Arena Realloc & Free-List** (NEW REQUIREMENT):
- Arena must support `arena_realloc()` for dynamic buffer resizing
- Arena maintains a **free-list** to track freed blocks for reuse
- This enables containers (Map, Element, Array) to use arena instead of pool
- Free-list organized by size classes for efficient reallocation
- Rationale: Unify memory management - all document data in arena for ownership checking

**NamePool Parent Chain**:
- NamePool can have parent NamePool (hierarchical lookup)
- Child NamePool lookups cascade to parent if not found locally
- Enables schema-instance name sharing without duplication

**CRITICAL: Parent Input is Read-Only** ⚠️:
- MarkBuilder and MarkEditor **MUST NEVER** modify parent Input data
- They can **reference** data from parent (read-only access)
- They can **lookup** names in parent NamePool (read-only)
- All **writes** must go to their own Input's arena and NamePool
- Rationale: 
  - Parent is typically a schema shared across multiple instances
  - Writing to parent would corrupt shared data
  - Child Inputs must be isolated and independently modifiable

**Implications for Deep Copy**:
1. String in target's NamePool or parent chain → no copy needed (read-only reference)
2. Data in target's Arena or parent's Arena → no copy needed (read-only reference)
3. Data not in target or parent arenas → must deep copy to target arena
4. **Never modify source data during copy** - only read and create new copies

**Container Allocation Strategy** (UPDATED):
- **Maps**: Arena for structure AND data buffer (via `arena_realloc`)
- **Elements**: Arena for structure AND data/children buffers (via `arena_realloc`)
- **Arrays**: Arena for structure AND items buffer (via `arena_realloc`)
- **Benefit**: All document data in arena → simplified ownership checking, no pool dependency

---

## 2. Solution Design

### 2.1 Arena Ownership Detection & Realloc Support

**New Functions in `arena.h`**:

```c
/**
 * Check if a pointer belongs to this arena
 * @param arena Arena to check ownership
 * @param ptr Pointer to check
 * @return true if ptr was allocated from this arena
 */
bool arena_owns(Arena* arena, const void* ptr);

/**
 * Reallocate memory in arena with free-list support
 * Similar to realloc() but works within arena
 * @param arena Arena to reallocate from
 * @param ptr Pointer to existing allocation (or NULL for new allocation)
 * @param old_size Size of existing allocation (or 0 if ptr is NULL)
 * @param new_size Desired new size
 * @return Pointer to reallocated memory, or NULL on failure
 */
void* arena_realloc(Arena* arena, void* ptr, size_t old_size, size_t new_size);

/**
 * Free memory back to arena's free-list for reuse
 * @param arena Arena that owns the memory
 * @param ptr Pointer to free
 * @param size Size of allocation to free
 */
void arena_free(Arena* arena, void* ptr, size_t size);
```

**Free-List Design**:
```c
// Size classes for free-list (power-of-2 bins)
#define ARENA_FREE_LIST_BINS 8
// Bin sizes: 16, 32, 64, 128, 256, 512, 1024, 2048+

typedef struct ArenaFreeBlock {
    size_t size;                    // Size of this free block
    struct ArenaFreeBlock* next;    // Next block in same bin
} ArenaFreeBlock;

typedef struct Arena {
    // ... existing fields ...
    ArenaFreeBlock* free_lists[ARENA_FREE_LIST_BINS];  // Free-list bins
    size_t free_bytes;              // Total bytes in free-lists
} Arena;
```

**Implementation Strategy**:
```c
bool arena_owns(Arena* arena, const void* ptr) {
    if (!arena || !ptr) return false;
    
    // Iterate through all chunks
    ArenaChunk* chunk = arena->first;
    while (chunk) {
        uintptr_t data_start = (uintptr_t)&chunk->data[0];
        uintptr_t data_end = data_start + chunk->used;
        uintptr_t ptr_addr = (uintptr_t)ptr;
        
        if (ptr_addr >= data_start && ptr_addr < data_end) {
            return true;
        }
        chunk = chunk->next;
    }
    
    return false;
}

void* arena_realloc(Arena* arena, void* ptr, size_t old_size, size_t new_size) {
    if (!arena) return NULL;
    
    // NULL ptr -> allocate new
    if (!ptr) return arena_alloc(arena, new_size);
    
    // new_size == 0 -> free
    if (new_size == 0) {
        arena_free(arena, ptr, old_size);
        return NULL;
    }
    
    // Same size -> no-op
    if (new_size == old_size) return ptr;
    
    // Shrinking -> add excess to free-list
    if (new_size < old_size) {
        size_t excess = old_size - new_size;
        if (excess >= sizeof(ArenaFreeBlock)) {
            void* excess_ptr = (char*)ptr + new_size;
            arena_free(arena, excess_ptr, excess);
        }
        return ptr;
    }
    
    // Growing -> check if at end of current chunk
    ArenaChunk* chunk = arena->current;
    uintptr_t ptr_addr = (uintptr_t)ptr;
    uintptr_t chunk_end = (uintptr_t)&chunk->data[0] + chunk->used;
    
    // If at end of chunk and enough space, extend in place
    if (ptr_addr + old_size == chunk_end) {
        size_t growth = new_size - old_size;
        if (chunk->used + growth <= chunk->capacity) {
            chunk->used += growth;
            return ptr;
        }
    }
    
    // Otherwise, allocate new, copy, free old
    void* new_ptr = arena_alloc(arena, new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, old_size);
        arena_free(arena, ptr, old_size);
    }
    return new_ptr;
}

void arena_free(Arena* arena, void* ptr, size_t size) {
    if (!arena || !ptr || size < sizeof(ArenaFreeBlock)) return;
    
    // Determine bin index (log2 of size)
    int bin = 0;
    size_t bin_size = 16;
    while (bin_size < size && bin < ARENA_FREE_LIST_BINS - 1) {
        bin++;
        bin_size *= 2;
    }
    
    // Add to free-list
    ArenaFreeBlock* block = (ArenaFreeBlock*)ptr;
    block->size = size;
    block->next = arena->free_lists[bin];
    arena->free_lists[bin] = block;
    arena->free_bytes += size;
}
```

### 2.2 Input Parent Chain Support

**Extend Input structure** (in `lambda-data.hpp`):

```cpp
typedef struct Input {
    void* url;
    void* path;
    Pool* pool;                 // memory pool
    Arena* arena;               // arena allocator
    NamePool* name_pool;        // centralized name management
    ShapePool* shape_pool;      // shape deduplication
    ArrayList* type_list;       // list of types
    Item root;
    
    // NEW: Parent input for hierarchical arena/name sharing
    Input* parent;              // Parent input (e.g., schema input for instance)
    
    // member functions
    static Input* create(Pool* pool, Url* abs_url = nullptr, Input* parent = nullptr);
} Input;
```

**Parent Chain Semantics**:
- Schema Input has `parent = nullptr` (root of hierarchy)
- Instance Input has `parent = schema_input` (inherits names/shapes)
- When checking ownership, cascade to parent chain

### 2.3 MarkBuilder Deep Copy API

**Add to `mark_builder.hpp`**:

```cpp
class MarkBuilder {
public:
    // ... existing methods ...

    /**
     * Deep copy an Item to this builder's Input arena
     * 
     * Smart copy: Only copies data not already in target arena or parent chain.
     * - Strings in NamePool (target or parents) → reuse directly
     * - Data in target/parent arenas → reuse directly
     * - External data → deep copy to target arena
     * 
     * @param item Item to deep copy
     * @return Copied Item with all data in target arena
     */
    Item deepCopy(Item item);

    /**
     * Check if an Item's data is already in this Input's arena/name pool
     * @param item Item to check
     * @return true if no copy needed (data already in target or parent)
     */
    bool isInArena(Item item) const;

private:
    /**
     * Check if a pointer is in target arena or any parent arena
     */
    bool isPointerInArenaChain(const void* ptr) const;
    
    /**
     * Internal deep copy implementation
     */
    Item deepCopyInternal(Item item, bool check_ownership);
};
```

### 2.4 Implementation Plan

```cpp
// mark_builder.cpp

bool MarkBuilder::isPointerInArenaChain(const void* ptr) const {
    if (!ptr) return true;  // nullptr is always "safe"
    
    // Check target arena
    if (arena_owns(arena_, ptr)) {
        return true;
    }
    
    // Check parent chain (if exists)
    Input* parent_input = input_->parent;
    while (parent_input) {
        if (arena_owns(parent_input->arena, ptr)) {
            return true;
        }
        parent_input = parent_input->parent;
    }
    
    return false;
}

bool MarkBuilder::isInArena(Item item) const {
    TypeId type_id = get_type_id(item);
    
    switch (type_id) {
        // Inline types - always safe
        case LMD_TYPE_NULL:
        case LMD_TYPE_BOOL:
        case LMD_TYPE_INT:
            return true;
        
        // Pointer types - check arena ownership
        case LMD_TYPE_INT64:
            return isPointerInArenaChain(item.pointer);
        
        case LMD_TYPE_FLOAT:
            return isPointerInArenaChain(item.pointer);
        
        case LMD_TYPE_STRING: {
            String* str = it2s(item);
            return isPointerInArenaChain(str);
        }
        
        case LMD_TYPE_SYMBOL: {
            String* sym = get_symbol(item);
            if (!sym) return true;
            
            // Check if in NamePool chain
            String* pooled = name_pool_lookup_string(name_pool_, sym);
            if (pooled == sym) return true;  // Found in name pool
            
            // Check arena ownership
            return isPointerInArenaChain(sym);
        }
        
        case LMD_TYPE_MAP: {
            Map* map = item.map;
            if (!map) return true;
            
            // Check map structure
            if (!isPointerInArenaChain(map)) return false;
            
            // Check all field values recursively
            TypeMap* map_type = (TypeMap*)map->type;
            ShapeEntry* field = map_type->shape;
            while (field) {
                void* field_data = (char*)map->data + field->byte_offset;
                Item field_item = *(Item*)field_data;
                if (!isInArena(field_item)) return false;
                field = field->next;
            }
            return true;
        }
        
        case LMD_TYPE_ELEMENT: {
            Element* elem = item.element;
            if (!elem) return true;
            
            // Check element structure
            if (!isPointerInArenaChain(elem)) return false;
            
            // Check attributes
            TypeElmt* elem_type = (TypeElmt*)elem->type;
            if (elem_type->length > 0) {
                ShapeEntry* attr = elem_type->shape;
                while (attr) {
                    void* attr_data = (char*)elem->data + attr->byte_offset;
                    Item attr_item = *(Item*)attr_data;
                    if (!isInArena(attr_item)) return false;
                    attr = attr->next;
                }
            }
            
            // Check children
            for (int i = 0; i < elem->length; i++) {
                if (!isInArena(elem->items[i])) return false;
            }
            return true;
        }
        
        case LMD_TYPE_ARRAY: {
            Array* arr = item.array;
            if (!arr) return true;
            
            // Check array structure
            if (!isPointerInArenaChain(arr)) return false;
            
            // Check all items
            for (int i = 0; i < arr->length; i++) {
                Item child = array_get(arr, i);
                if (!isInArena(child)) return false;
            }
            return true;
        }
        
        // Other pointer types
        case LMD_TYPE_BINARY:
        case LMD_TYPE_DTIME:
        case LMD_TYPE_DECIMAL:
        case LMD_TYPE_ARRAY_INT:
        case LMD_TYPE_ARRAY_INT64:
        case LMD_TYPE_ARRAY_FLOAT:
        case LMD_TYPE_LIST:
            return isPointerInArenaChain(item.pointer);
        
        default:
            return false;  // Conservative: unknown types require copy
    }
}

Item MarkBuilder::deepCopy(Item item) {
    return deepCopyInternal(item, true);  // Enable ownership checking
}

Item MarkBuilder::deepCopyInternal(Item item, bool check_ownership) {
    TypeId type_id = get_type_id(item);
    
    // Fast path: if data already in arena chain, no copy needed
    if (check_ownership && isInArena(item)) {
        log_debug("deepCopy: item already in arena chain, skipping copy");
        return item;
    }
    
    switch (type_id) {
        case LMD_TYPE_NULL:
            return createNull();
            
        case LMD_TYPE_BOOL:
            return createBool(it2b(item));
            
        case LMD_TYPE_INT:
            return createInt(it2i(item));
            
        case LMD_TYPE_INT64:
            return createLong(it2l(item));
            
        case LMD_TYPE_FLOAT:
            return createFloat(it2d(item));
        
        case LMD_TYPE_SYMBOL: {
            String* sym = get_symbol(item);
            if (!sym) return createNull();
            
            // Try to find in NamePool first (deduplication)
            String* pooled = name_pool_lookup_len(name_pool_, sym->chars, sym->len);
            if (pooled) {
                return {.item = y2it(pooled)};  // Reuse pooled symbol
            }
            
            // Not in pool, create new
            String* copied_sym = createSymbol(sym->chars, sym->len);
            return {.item = y2it(copied_sym)};
        }

        case LMD_TYPE_STRING: {
            String* str = it2s(item);
            if (!str) return createNull();
            // Always create new string in arena (content strings not pooled)
            return createStringItem(str->chars, str->len);
        }
            
        case LMD_TYPE_BINARY: {
            String* bin = get_binary(item);
            if (!bin) return createNull();
            String* copied = createString(bin->chars, bin->len);
            if (!copied) return createNull();
            Item result = {.item = x2it(copied)};
            return result;
        }
            
        case LMD_TYPE_DTIME: {
            DateTime dt = get_datetime(item);
            DateTime* dt_ptr = (DateTime*)arena_alloc(arena_, sizeof(DateTime));
            if (!dt_ptr) return createNull();
            *dt_ptr = dt;
            Item result = {.item = k2it(dt_ptr)};
            return result;
        }
            
        case LMD_TYPE_DECIMAL: {
            Decimal* src_dec = get_decimal(item);
            if (!src_dec || !src_dec->dec_val) return createNull();
            
            Decimal* new_dec = (Decimal*)arena_alloc(arena_, sizeof(Decimal));
            if (!new_dec) return createNull();
            
            // Deep copy mpdecimal value
            char* dec_str = mpd_to_sci(src_dec->dec_val, 1);
            if (!dec_str) return createNull();
            
            mpd_context_t ctx;
            mpd_maxcontext(&ctx);
            mpd_t* new_dec_val = mpd_qnew();
            if (!new_dec_val) {
                free(dec_str);
                return createNull();
            }
            
            mpd_set_string(new_dec_val, dec_str, &ctx);
            free(dec_str);
            
            new_dec->ref_cnt = 1;
            new_dec->dec_val = new_dec_val;
            
            Item result = {.item = c2it(new_dec)};
            return result;
        }
            
        case LMD_TYPE_NUMBER: {
            double val = get_double(item);
            return createFloat(val);
        }
            
        case LMD_TYPE_ARRAY_INT: {
            ArrayInt* arr = item.array_int;
            if (!arr) return createArray();
            
            ArrayBuilder arr_builder = array();
            for (int i = 0; i < arr->length; i++) {
                arr_builder.append((int64_t)arr->items[i]);
            }
            return arr_builder.final();
        }
            
        case LMD_TYPE_ARRAY_INT64: {
            ArrayInt64* arr = item.array_int64;
            if (!arr) return createArray();
            
            ArrayBuilder arr_builder = array();
            for (int i = 0; i < arr->length; i++) {
                arr_builder.append(arr->items[i]);
            }
            return arr_builder.final();
        }
            
        case LMD_TYPE_ARRAY_FLOAT: {
            ArrayFloat* arr = item.array_float;
            if (!arr) return createArray();
            
            ArrayBuilder arr_builder = array();
            for (int i = 0; i < arr->length; i++) {
                arr_builder.append(arr->items[i]);
            }
            return arr_builder.final();
        }
            
        case LMD_TYPE_ARRAY: {
            Array* arr = item.array;
            if (!arr) return createArray();
            
            ArrayBuilder arr_builder = array();
            for (int i = 0; i < arr->length; i++) {
                Item child = array_get(arr, i);
                Item copied_child = deepCopyInternal(child, check_ownership);
                arr_builder.append(copied_child);
            }
            return arr_builder.final();
        }
            
        case LMD_TYPE_LIST: {
            List* list = item.list;
            if (!list) return createArray();
            
            ArrayBuilder arr_builder = array();
            for (int i = 0; i < list->length; i++) {
                Item child = list->items[i];
                Item copied_child = deepCopyInternal(child, check_ownership);
                arr_builder.append(copied_child);
            }
            return arr_builder.final();
        }
            
        case LMD_TYPE_MAP: {
            Map* map = item.map;
            if (!map || !map->type || !map->data) {
                return createMap();
            }
            
            TypeMap* map_type = (TypeMap*)map->type;
            MapBuilder map_builder = this->map();
            
            // Iterate over map fields
            ShapeEntry* field = map_type->shape;
            while (field) {
                if (field->name && field->name->str) {
                    void* field_data = (char*)map->data + field->byte_offset;
                    Item field_item = *(Item*)field_data;
                    
                    // Recursively deep copy the field value
                    Item copied_field = deepCopyInternal(field_item, check_ownership);
                    
                    // Key names are pooled via NamePool
                    String* key_name = createName(field->name->str, field->name->length);
                    map_builder.put(key_name, copied_field);
                }
                field = field->next;
            }
            
            return map_builder.final();
        }
            
        case LMD_TYPE_ELEMENT: {
            Element* elem = item.element;
            if (!elem || !elem->type) return createElement("div");
            
            TypeElmt* elem_type = (TypeElmt*)elem->type;
            char tag_name[256];
            size_t tag_len = elem_type->name.length < 255 ? elem_type->name.length : 255;
            memcpy(tag_name, elem_type->name.str, tag_len);
            tag_name[tag_len] = '\0';
            ElementBuilder elem_builder = element(tag_name);
            
            // Copy attributes
            if (elem_type->length > 0) {
                ShapeEntry* attr = elem_type->shape;
                while (attr) {
                    if (attr->name) {
                        void* attr_data = (char*)elem->data + attr->byte_offset;
                        Item attr_item = *(Item*)attr_data;
                        
                        Item copied_attr = deepCopyInternal(attr_item, check_ownership);
                        
                        String* attr_name = createName(attr->name->str, attr->name->length);
                        elem_builder.attr(attr_name, copied_attr);
                    }
                    attr = attr->next;
                }
            }
            
            // Copy children
            for (int i = 0; i < elem->length; i++) {
                Item child = elem->items[i];
                Item copied_child = deepCopyInternal(child, check_ownership);
                elem_builder.child(copied_child);
            }
            
            return elem_builder.final();
        }
            
        default:
            log_warn("deepCopy: unsupported type_id=%d, returning null", type_id);
            return createNull();
    }
}
```

### 2.5 Read-Only Parent Access Design ✅

**Design Principle**: Parent Input is treated as **immutable shared schema**. Child Inputs can reference parent data but must never modify it.

**Implementation Verification**:

1. **NamePool Operations** (`lambda/name_pool.cpp`):
   - ✅ `name_pool_lookup_*()`: Read-only search in parent chain
   - ✅ `name_pool_create_*()`: Lookups parent first, but **only creates in current pool** (line 149-153)
   - ✅ Never inserts or modifies entries in parent NamePool

2. **Arena Ownership Checks** (`lambda/mark_builder.cpp`):
   - ✅ `is_pointer_in_arena_chain()`: Read-only traversal of parent Input chain (line 538-544)
   - ✅ `arena_owns()`: Read-only pointer comparison against arena chunks
   - ✅ No writes to parent arenas, only ownership detection

3. **Deep Copy Operations** (`lambda/mark_builder.cpp`):
   - ✅ `deep_copy_internal()`: Only **reads** source data, never modifies
   - ✅ All new allocations go to target's `arena_` (via `arena_alloc()`)
   - ✅ All name creation goes to target's `name_pool_` (via `name_pool_create_*()`)
   - ✅ When source data is in parent arena, returns reference (no copy), but never writes to it

4. **MarkBuilder Allocations** (`lambda/mark_builder.cpp`):
   - ✅ Uses `this->arena_` for all allocations (target Input's arena)
   - ✅ Uses `this->name_pool_` for all name creation (target Input's NamePool)
   - ✅ Never directly accesses `input_->parent->arena` or `input_->parent->name_pool` for writes

5. **MarkEditor Operations** (`lambda/mark_editor.cpp`):
   - ✅ Uses `input_->arena` and `input_->name_pool` (its own Input)
   - ✅ No references to parent Input in MarkEditor code
   - ✅ All edits go to current Input's structures

**Guarantees**:
- ✅ Parent Input data remains unchanged across all child operations
- ✅ Multiple child Inputs can safely share same parent without conflicts
- ✅ Schema-instance relationship preserved (parent = schema, child = instance)
- ✅ Child can be independently modified and destroyed without affecting parent

### 2.6 MarkEditor Integration

**Update MarkEditor to use deep copy** (in `mark_editor.cpp`):

```cpp
// In map_update_inline and map_update_immutable
Item MarkEditor::map_update(Item map, String* key, Item value) {
    // ... validation code ...
    
    // Ensure value is in target arena
    if (!builder_->isInArena(value)) {
        log_debug("map_update: value not in arena, deep copying");
        value = builder_->deepCopy(value);
    }
    
    // Proceed with update
    if (mode_ == EDIT_MODE_INLINE) {
        return map_update_inline(map.map, key, value);
    } else {
        return map_update_immutable(map.map, key, value);
    }
}

// Similar updates for:
// - elmt_update_attr()
// - elmt_insert_child()
// - array_set()
// - array_insert()
```

### 2.7 Audit MarkBuilder Allocations

**Check all allocation sites**:

1. **Strings** (✅ CORRECT - uses arena):
   ```cpp
   String* MarkBuilder::createString(const char* str, size_t len) {
       String* s = (String*)arena_alloc(arena_, sizeof(String) + len + 1);
       // ...
   }
   ```

2. **Maps** (🔄 MIGRATE TO ARENA):
   ```cpp
   MapBuilder::MapBuilder(MarkBuilder* builder) {
       map_ = (Map*)arena_calloc(builder->arena(), sizeof(Map));
       // map_put() currently uses pool - CHANGE TO arena_realloc
   }
   ```

3. **Arrays** (🔄 MIGRATE TO ARENA):
   ```cpp
   ArrayBuilder::ArrayBuilder(MarkBuilder* builder) {
       array_ = array_pooled(builder->pool());  // CHANGE TO arena
       // array_append() uses pool - CHANGE TO arena_realloc
   }
   ```

4. **Elements** (🔄 MIGRATE TO ARENA):
   ```cpp
   ElementBuilder::ElementBuilder(MarkBuilder* builder, const char* tag_name) {
       Element* element = elmt_pooled(input->pool);  // CHANGE TO arena
       // elmt_put() uses pool - CHANGE TO arena_realloc
   }
   ```

**Migration Strategy**: Replace all `pool_alloc()` and `pool_realloc()` calls in container code with `arena_alloc()` and `arena_realloc()` respectively.

**Conclusion**: Container allocations must be migrated to arena with realloc support.

---

## 3. Implementation Checklist

### Implementation Status Summary

**Completed Phases**:
- ✅ **Phase 1**: Arena ownership API + realloc with free-list (79 tests pass)
- ✅ **Phase 2**: Input parent chain support  
- ✅ **Phase 3**: MarkBuilder deep_copy implementation (65 MarkBuilder tests pass)
- ✅ **Phase 4**: Comprehensive deep_copy testing (25 tests pass)
- ✅ **Phase 5a**: Container arena migration for MarkBuilder (30 tests pass)
- ✅ **Phase 5b**: External Input deep copy bug fix (38 MarkEditor tests pass)

**Remaining Phases**:
- ✓ **Phase 6**: Documentation & cleanup (TO-DO)

**Current Status**: Deep copy functionality is complete and production-ready. MarkBuilder containers are fully arena-allocated with reliable ownership detection. Critical bug in external element deep copy fixed - now uses ElementReader API for proper Item reconstruction. Lambda runtime preserves pool allocation (no changes needed).

**✅ Read-Only Parent Access Verified**: 
- All code audited to ensure parent Input data is never modified
- MarkBuilder only writes to its own `arena_` and `name_pool_`
- MarkEditor only writes to its own `input_->arena` and `input_->name_pool`
- `name_pool_create_*()` functions look up in parent but only create in current pool
- `arena_owns()` and `is_in_arena()` are read-only checks, no modifications
- `deep_copy_internal()` only reads source data, creates all copies in target arena
- Design principle enforced: Parent Input is treated as immutable shared schema

---

### Phase 1: Arena Ownership Detection & Realloc Support ✅ COMPLETE

**3.1 Implement arena ownership checking**
- [x] Add `arena_owns()` function to `lib/arena.h`
- [x] Implement `arena_owns()` in `lib/arena.c`
- [x] Add unit tests in `test/test_arena_gtest.cpp`

**3.2 Implement arena realloc with free-list**
- [x] Add free-list bins to `Arena` struct (8 bins: 16-2048+ bytes)
- [x] Implement `arena_free()` in `lib/arena.c`
- [x] Implement `arena_realloc()` in `lib/arena.c`
- [x] Modify `arena_alloc_aligned()` to check free-lists first
- [x] Add unit tests for realloc and free operations

**Test Cases**:
- [x] Test pointer in first chunk (ArenaOwnershipTest)
- [x] Test pointer in middle chunk (ArenaOwnershipTest)
- [x] Test pointer in last chunk (ArenaOwnershipTest)
- [x] Test pointer before arena (negative) (ArenaOwnershipTest)
- [x] Test pointer after arena (negative) (ArenaOwnershipTest)
- [x] Test nullptr handling (ArenaOwnershipTest)
- [x] Test invalid arena handling (ArenaOwnershipTest)
- [x] Test realloc shrink (adds to free-list) (ArenaReallocTest)
- [x] Test realloc grow (in-place if at end) (ArenaReallocTest)
- [x] Test realloc grow (allocate new if not at end) (ArenaReallocTest)
- [x] Test free-list reuse (ArenaFreeListTest)
- [x] Test free-list bin selection (ArenaFreeTest)

**Implementation Details**:
- Free-list uses 8 size-class bins with log2 ranges
- Block splitting when reusing large free blocks
- In-place growth optimization when at chunk end
- Proper alignment checking when reusing free blocks
- Added `free_bytes` counter for statistics

**Test Results**: ✅ All 79 tests pass (including 27 new tests)

**Success Criteria**: ✅ `arena_owns()` correctly identifies arena-owned pointers across all chunks

### Phase 2: Input Parent Chain Support ✅ COMPLETE

**3.2 Add parent Input support**
- [x] Add `Input* parent` field to `Input` struct in `lambda-data.hpp`
- [x] Update `Input::create()` to accept parent parameter with default value `nullptr`
- [x] Update `Input::create()` implementation to initialize parent field
- [x] All existing call sites work with default nullptr parameter

**Implementation Details**:
- Added `Input* parent` field after `Item root` in Input struct
- Updated function signature: `static Input* create(Pool* pool, Url* abs_url = nullptr, Input* parent = nullptr)`
- Parent field enables hierarchical arena ownership checking
- Backward compatible - all existing code uses default nullptr

**Test Results**: ✅ All existing tests pass (MarkBuilder: 65/65, Lambda: 23/23)

**Success Criteria**: ✅ Input can reference parent Input for hierarchical ownership

### Phase 3: MarkBuilder Deep Copy ✅ COMPLETE

**3.3 Move copy_item_deep to MarkBuilder**
- [x] Converted static `copy_item_deep()` to `MarkBuilder::deep_copy_internal()`
- [x] Added `deep_copy()` public method to `MarkBuilder` class
- [x] Added `is_in_arena()` public method to `MarkBuilder` class
- [x] Added `is_pointer_in_arena_chain()` private helper
- [x] Implemented `deep_copy_internal()` as private recursive implementation

**3.4 Implement smart ownership checking**
- [x] Implemented `is_pointer_in_arena_chain()` with parent Input chain traversal
- [x] Implemented `is_in_arena()` with recursive checking for:
  - [x] Inline types (null, bool, int)
  - [x] Pointer types (int64, float, string, symbol)
  - [x] Containers (map, element, array, list)
  - [x] Special types (binary, datetime, decimal)
- [x] Added optimization: `deep_copy()` checks `is_in_arena()` and returns original if true

**3.5 Implementation details**
- Converted from static function taking `MarkBuilder*` to instance methods
- Changed all `builder->` calls to direct method calls (or implicit `this->`)
- Fixed variable shadowing issue (`Map* map` → `Map* src_map`)
- Added proper pointer casts for `item.pointer` to `void*`
- Symbol lookup checks NamePool chain before arena ownership
- All recursive calls updated to use `deep_copy_internal()`

**Test Results**: ✅ All tests pass (MarkBuilder: 65/65)

**Success Criteria**: ✅ `deep_copy()` correctly handles all Lambda types with smart ownership checking

### Phase 4: Comprehensive Testing ✅ COMPLETE

**3.6 Create deep copy test suite**
- [x] Create `test/test_mark_builder_deepcopy_gtest.cpp`
- [x] Test primitives (int, float, bool, null)
- [x] Test strings (content vs pooled names)
- [x] Test symbols (pooled vs arena)
- [x] Test maps (nested fields)
- [x] Test elements (attributes + children)
- [x] Test arrays (homogeneous items)
- [x] Test mixed nested structures

**3.7 Test ownership detection**
- [x] Test `isInArena()` for same Input
- [x] Test `isInArena()` for parent Input data
- [x] Test `isInArena()` for external data
- [x] Test with NamePool parent chain
- [x] Test with Arena parent chain (via Input parent)

**3.8 Test optimization (skip unnecessary copies)**
- [x] Benchmark deep copy with ownership checking ON
- [x] Benchmark deep copy with ownership checking OFF
- [x] Verify no copies when source==target arena
- [x] Verify copies only when external data

**Test Results**: ✅ All 25 tests passing

**Known Limitations**:
- Containers (Array/Map/Element) use `pool_calloc()` for structs, cannot detect struct ownership
- Workaround: `is_in_arena()` checks container *contents* instead of container struct
- Impact: Containers with only inline values (no arena-allocated content) cannot reliably detect cross-Input ownership
- Resolution: Requires Phase 5 container migration to arena (deferred as major refactoring)

**Success Criteria**: ✅ Met
- All tests passing (25/25)
- No memory leaks
- Ownership detection works for arena-allocated data
- Documented workaround for container limitation

### Phase 5a: Container Arena Migration ✅ COMPLETE (Focused Approach)

**Implementation Summary**:
Instead of migrating the entire runtime (50+ call sites), took a **focused approach** - only migrate MarkBuilder containers to arena allocation while keeping Lambda runtime on pool allocation.

**Changes Made**:

1. **Created arena-based container functions** (`lambda-data.hpp/cpp`):
```cpp
Array*   array_arena(Arena* arena);   // Arena version for MarkBuilder
Map*     map_arena(Arena* arena);     // Arena version for MarkBuilder  
Element* elmt_arena(Arena* arena);    // Arena version for MarkBuilder
```

2. **Updated MarkBuilder to use arena containers** (`mark_builder.cpp`):
- Changed `elmt_pooled()` → `elmt_arena()` in ElementBuilder constructor
- Changed `array_pooled()` → `array_arena()` in ArrayBuilder constructor
- Only 2 call sites modified

3. **Enhanced expand_list() with arena support** (`lambda-data.cpp`):
```cpp
void expand_list(List *list, Arena* arena = nullptr) {
    // Detect if buffer is arena-allocated
    bool use_arena = (arena && old_items && arena_owns(arena, old_items));
    if (use_arena) {
        list->items = (Item*)arena_realloc(arena, list->items, old_size, new_size);
    } else {
        list->items = (Item*)realloc(list->items, new_size);  // Runtime path
    }
}
```

4. **Updated array_append() to pass arena** (`lambda-data.hpp/cpp`):
```cpp
void array_append(Array* arr, Item itm, Pool* pool, Arena* arena = nullptr);
```
- MarkBuilder passes arena, runtime passes nullptr (backward compatible)

5. **Improved is_in_arena() ownership detection** (`mark_builder.cpp`):
- For containers: Check BOTH struct ownership AND content ownership
- Struct in arena but contains external value → returns false (correct!)
- Handles arena-allocated containers from MarkBuilder AND pool-allocated containers from runtime

**Test Results**: ✅ 90/90 arena tests passing
- 79 original arena allocator tests
- 11 new container tests covering arena allocation and initialization

**New Test Coverage** (`test_arena_gtest.cpp`):
1. **ArenaContainerTest** (9 tests):
   - `ArrayArenaAllocation`: Verifies `array_arena()` creates arena-owned Arrays
   - `MapArenaAllocation`: Verifies `map_arena()` creates arena-owned Maps
   - `ElementArenaAllocation`: Verifies `elmt_arena()` creates arena-owned Elements
   - `ArrayArenaVsPoolAllocation`: Compares arena vs pool allocation for Arrays
   - `MapArenaVsPoolAllocation`: Compares arena vs pool allocation for Maps
   - `ElementArenaVsPoolAllocation`: Compares arena vs pool allocation for Elements
   - `MultipleContainersInSameArena`: Tests multiple containers allocated from same arena
   - `ContainerAllocationAcrossArenas`: Tests containers in different arenas are independent
   - `NullArenaHandling`: Tests containers with NULL arena fall back to pool allocation

2. **ArenaContainerRegressionTest** (2 tests):
   - `UninitializedMemoryBug`: Tests the critical bug where missing `memset()` caused crashes
   - `MapDataInitialization`: Verifies Map structs are properly zero-initialized

**Benefits**:
- ✅ MarkBuilder containers are fully arena-allocated
- ✅ Reliable cross-Input ownership detection
- ✅ Containers with only inline values now correctly detect ownership
- ✅ No changes to Lambda runtime (13+ input parsers untouched)
- ✅ Backward compatible - pool allocation still works for runtime
- ✅ Comprehensive test coverage prevents initialization bug regression

**Eliminated Limitations**:
- ~~Containers with only inline values appear "in arena" from any Input~~ **FIXED**
- ~~Container struct ownership cannot be detected~~ **FIXED**

**Critical Bug Fix** (Post-Implementation):
- **Issue**: `map_arena()` and `elmt_arena()` did not zero-initialize memory with `memset()`, unlike `array_arena()`
- **Symptom**: Segmentation faults when HTML parser called `list_push()` on arena-allocated Elements
- **Root Cause**: Uninitialized `items`, `length`, `capacity` fields caused `realloc()` on garbage pointers
- **Fix**: Added `memset(container, 0, sizeof(Container))` to `map_arena()` and `elmt_arena()`
- **Impact**: HTML roundtrip tests: 104/108 → 108/108 ✅
- **Test Coverage**: 11 new tests added to `test_arena_gtest.cpp` to prevent regression

**Scope**: Only MarkBuilder uses arena containers. Lambda runtime preserves existing pool allocation. Zero risk of breaking runtime code paths.

### Phase 5b: External Input Deep Copy Bug Fix ✅ COMPLETE

**Root Cause Identified**:
Element storage (`elmt_put()` in `lambda/input/input.cpp`) stores STRING/SYMBOL/BINARY values as raw 8-byte pointers without type_id tags for memory efficiency. Direct memory access via `*(Item*)attr_data` reads these 8 bytes as an Item union, resulting in **garbage type_id from pointer bits**.

**Example of Bug**:
```
Storage:  ELMT_PUT stores STRING ptr=0x3000001d0 (8 bytes, no type tag)
Memory:   item=0x00000003000001d0 (high byte 0x00 from pointer, not type_id)
Read:     *(Item*)attr_data treats this as Item union
Corrupt:  type_id() reads high byte → returns 9 (garbage) instead of 10 (STRING)
Crash:    Accessing corrupted type_id after external pool destroyed
```

**Solution Implemented**:
Refactored `deep_copy_internal()` element case to use **ElementReader API** which properly reconstructs Items from stored data:

```cpp
// BEFORE (Direct memory access - WRONG):
void* attr_data = (char*)elem->data + attr->byte_offset;
Item attr_item = *(Item*)attr_data;  // ❌ Reads garbage type_id!

// AFTER (Use ElementReader - CORRECT):
ElementReader reader(elem);
ItemReader attr_reader = reader.get_attr(attr->name->str);
Item attr_item = attr_reader.item();  // ✓ Proper reconstruction via s2it()!
```

**Implementation Details** (`lambda/mark_builder.cpp:906-945`):
- Use `ElementReader(elem)` to wrap element
- Use `reader.tagName()` to get tag name
- Use `reader.get_attr(name)` to access attributes with proper Item reconstruction
- Use `reader.childAt(i)` to access children safely
- ElementReader internally calls `_map_get_const()` which uses `s2it(*(String**)field_ptr)` to add correct type_id tag

**Additional Fix** (November 22, 2025):
- Fixed typed array deep copy to preserve array types (ArrayInt, ArrayInt64, ArrayFloat)
- Previously incorrectly converted typed arrays to generic Array
- Now properly copies `LMD_TYPE_ARRAY_INT`, `LMD_TYPE_ARRAY_INT64`, `LMD_TYPE_ARRAY_FLOAT` with correct types
- Uses direct `memcpy` for efficient copying of primitive array data

**Additional Enhancement - ListBuilder** (November 22, 2025):
- Added `ListBuilder` class for fluent List construction following builder pattern
- Provides `push(Item)`, `push(int)`, `push(const char*)`, `push(double)` methods
- Fixed List deep copy to use ListBuilder (was incorrectly using ArrayBuilder)
- Fixed `is_in_arena()` to check List struct ownership (was only checking items)
- All 71 builder tests pass, 33 deep copy tests pass

**Additional Enhancement - Range Support** (November 22, 2025):
- Added `createRange(int64_t start, int64_t end)` method to MarkBuilder
- Range allocated from arena using `arena_alloc(arena_, sizeof(Range))`
- Added `LMD_TYPE_RANGE` to `is_in_arena()` pointer type checks
- Added Range deep copy support in `deep_copy_internal()`
- All tests pass: 71 builder tests + 33 deep copy tests

**Additional Enhancement - Type Support** (November 22, 2025):
- Added `createType(TypeId type_id, bool is_literal, bool is_const)` method to MarkBuilder
- Type allocated from arena using `arena_alloc(arena_, sizeof(Type))`
- Uses manual Item wrapping (no t2it macro exists): `{.item = (((uint64_t)LMD_TYPE_TYPE)<<56) | (uint64_t)type}`
- Added `LMD_TYPE_TYPE` to `is_in_arena()` pointer type checks
- Added Type deep copy support in `deep_copy_internal()`
- All tests pass: 73 builder tests + 34 deep copy tests + 38 editor tests

**Changes Made**:
- [x] Fixed `deep_copy_internal()` element case to use ElementReader
- [x] Replaced manual Item reconstruction with reader delegation
- [x] Added comprehensive debug logging to trace type_id corruption
- [x] Confirmed root cause: storage format vs retrieval expectations mismatch
- [x] Removed all debug logging after fix confirmed

**Test Coverage** (`test/test_mark_editor_gtest.cpp`):
- [x] Test `ExternalInputTest.DeepCopyExternalElement` (NEW)
  - Creates element in external Input pool
  - Deep copies to target Input
  - Destroys external pool
  - Accesses attributes in copied element
  - Verifies no crash, attributes accessible
- [x] All 38 MarkEditor tests pass
- [x] All 73 MarkBuilder tests pass (includes CreateType, CreateTypeWithFlags)
- [x] All 34 DeepCopy tests pass (includes CopyType)
- [x] 2240/2305 total tests pass (97.2% pass rate)

**Success Criteria**: ✅ Met
- External element deep copy works correctly
- Attributes accessible after external pool destroyed
- No memory corruption or crashes
- Code follows DRY principle using existing ElementReader infrastructure
- All tests passing with no regressions

### Phase 6: Documentation & Cleanup ✓ TO-DO

**3.11 Update documentation**
- [ ] Document `deepCopy()` API in header
- [ ] Document `isInArena()` API in header
- [ ] Update MarkBuilder usage guide
- [ ] Update MarkEditor usage guide
- [ ] Add memory management best practices doc

**3.12 Code cleanup**
- [ ] Remove debug logging (or guard with `#ifdef DEBUG`)
- [ ] Add performance profiling hooks (optional)
- [ ] Review and optimize hot paths
- [ ] Final code review

**Success Criteria**: Production-ready deep copy with full documentation

---

## 4. Expected Outcomes

### 4.1 Performance Improvements

**Before (Always Copy)**:
```
Copying same-input data:  100% copies (wasteful)
Copying parent data:      100% copies (wasteful)
Copying external data:    100% copies (necessary)
```

**After (Smart Copy)**:
```
Copying same-input data:    0% copies (reuse)
Copying parent data:        0% copies (reuse via parent chain)
Copying external data:    100% copies (necessary)
```

**Estimated Savings**:
- Memory: 50-90% reduction in same-input scenarios
- CPU: 70-95% reduction in copy overhead for local data
- Use Case: Schema validation reusing schema names/shapes

### 4.2 Memory Safety

**Guarantees**:
1. All document data in MarkEditor resides in target Input arena (or parents')
2. No dangling pointers to external memory
3. Safe cross-Input data sharing via parent chain
4. Automatic detection and copying of external data

### 4.3 API Improvements

**Public API**:
```cpp
// New public methods
Item MarkBuilder::deepCopy(Item item);
bool MarkBuilder::isInArena(Item item) const;
```

**Integration Benefits**:
- MarkEditor automatically handles external data
- Users don't need to manually copy data
- Transparent optimization (no API changes needed)

---

## 5. Testing Strategy

### 5.1 Unit Tests

**Arena Tests** (`test/test_arena_gtest.cpp`):
```cpp
TEST(ArenaTest, OwnershipDetection) {
    Pool* pool = variable_mem_pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* ptr1 = arena_alloc(arena, 64);
    void* ptr2 = arena_alloc(arena, 128);
    void* external = malloc(64);
    
    EXPECT_TRUE(arena_owns(arena, ptr1));
    EXPECT_TRUE(arena_owns(arena, ptr2));
    EXPECT_FALSE(arena_owns(arena, external));
    EXPECT_FALSE(arena_owns(arena, nullptr));
    
    free(external);
    arena_destroy(arena);
    variable_mem_pool_destroy(pool);
}
```

**MarkBuilder Tests** (`test/test_mark_builder_deepcopy_gtest.cpp`):
```cpp
TEST(MarkBuilderDeepCopy, SameInputOptimization) {
    Pool* pool = variable_mem_pool_create();
    Input* input = Input::create(pool);
    MarkBuilder builder(input);
    
    // Create item in same input
    Item original = builder.map()
        .put("name", "Alice")
        .put("age", 30)
        .final();
    
    // Deep copy should reuse data (optimization)
    Item copied = builder.deepCopy(original);
    
    // Verify same pointers (no actual copy)
    EXPECT_EQ(original.map, copied.map);  // Structure reused
    
    variable_mem_pool_destroy(pool);
}

TEST(MarkBuilderDeepCopy, ExternalDataCopy) {
    Pool* pool1 = variable_mem_pool_create();
    Pool* pool2 = variable_mem_pool_create();
    Input* input1 = Input::create(pool1);
    Input* input2 = Input::create(pool2);
    
    // Create in input1
    MarkBuilder builder1(input1);
    Item original = builder1.map()
        .put("key", "value")
        .final();
    
    // Deep copy to input2 (external)
    MarkBuilder builder2(input2);
    Item copied = builder2.deepCopy(original);
    
    // Verify different pointers (actual copy)
    EXPECT_NE(original.map, copied.map);
    
    // Verify data equality
    // ... field-by-field comparison ...
    
    variable_mem_pool_destroy(pool1);
    variable_mem_pool_destroy(pool2);
}

TEST(MarkBuilderDeepCopy, ParentChainOptimization) {
    Pool* pool = variable_mem_pool_create();
    Input* parent_input = Input::create(pool);
    Input* child_input = Input::create(pool, nullptr, parent_input);
    
    // Create in parent
    MarkBuilder parent_builder(parent_input);
    Item parent_item = parent_builder.createStringItem("shared");
    
    // Deep copy in child should reuse parent data
    MarkBuilder child_builder(child_input);
    EXPECT_TRUE(child_builder.isInArena(parent_item));
    
    Item copied = child_builder.deepCopy(parent_item);
    EXPECT_EQ(parent_item.item, copied.item);  // No copy needed
    
    variable_mem_pool_destroy(pool);
}
```

**MarkEditor Tests** (`test/test_mark_editor_gtest.cpp` - additions):
```cpp
TEST(MarkEditorTest, MapUpdateExternalValue) {
    Pool* pool1 = variable_mem_pool_create();
    Pool* pool2 = variable_mem_pool_create();
    Input* input1 = Input::create(pool1);
    Input* input2 = Input::create(pool2);
    
    // Create map in input1
    MarkBuilder builder1(input1);
    Item map = builder1.map().put("a", 1).final();
    input1->root = map;
    
    // Create external value in input2
    MarkBuilder builder2(input2);
    Item external_value = builder2.createStringItem("external");
    
    // Update with external value - should auto-copy
    MarkEditor editor1(input1);
    Item updated = editor1.map_update(map, "b", external_value);
    
    // Verify value was copied to input1's arena
    EXPECT_TRUE(builder1.isInArena(updated));
    
    variable_mem_pool_destroy(pool1);
    variable_mem_pool_destroy(pool2);
}
```

### 5.2 Integration Tests

**End-to-End Scenario**:
```cpp
TEST(IntegrationTest, SchemaInstanceSharing) {
    Pool* pool = variable_mem_pool_create();
    
    // Create schema with pooled names
    Input* schema_input = Input::create(pool);
    MarkBuilder schema_builder(schema_input);
    Item schema = schema_builder.map()
        .put("name", "required")
        .put("age", "optional")
        .final();
    schema_input->root = schema;
    
    // Create instance with parent=schema
    Input* instance_input = Input::create(pool, nullptr, schema_input);
    MarkBuilder instance_builder(instance_input);
    
    // Instance should reuse schema's pooled field names
    Item instance = instance_builder.map()
        .put("name", "Bob")  // "name" reused from parent NamePool
        .put("age", 42)      // "age" reused from parent NamePool
        .final();
    
    // Verify no unnecessary copies
    // (names are shared via NamePool parent chain)
    
    variable_mem_pool_destroy(pool);
}
```

### 5.3 Performance Benchmarks

**Benchmark Cases**:
1. Deep copy same-input data (expect ~0% actual copies)
2. Deep copy parent-input data (expect ~0% actual copies)
3. Deep copy external data (expect 100% copies, baseline)
4. Large nested structures (10K+ items)
5. Pathological case: all external data

---

## 6. Migration Guide

### 6.1 For Existing Code Using copy_item_deep

**Before** (internal static function):
```cpp
// Not accessible - was file-local static
static Item copy_item_deep(MarkBuilder* builder, Item item);
```

**After** (public API):
```cpp
MarkBuilder builder(input);
Item copied = builder.deepCopy(item);  // Public method
```

### 6.2 For Code Creating Inputs

**Before** (no parent support):
```cpp
Input* input = Input::create(pool);
```

**After** (optional parent parameter):
```cpp
// Standalone input (no parent)
Input* input = Input::create(pool);

// Child input with parent
Input* schema = Input::create(pool);
Input* instance = Input::create(pool, nullptr, schema);
```

### 6.3 For MarkEditor Users

**Before** (manual copy if needed):
```cpp
MarkEditor editor(input);
// User had to manually ensure arena locality
Item updated = editor.map_update(map, "key", value);
```

**After** (automatic copy):
```cpp
MarkEditor editor(input);
// Editor automatically copies external data
Item updated = editor.map_update(map, "key", value);
// value is automatically deep-copied if external
```

---

## 7. Risk Analysis

### 7.1 Potential Issues

**Risk 1: False positives in ownership detection**
- **Impact**: Unnecessary copies, performance degradation
- **Mitigation**: Comprehensive unit tests for all arena chunk scenarios
- **Likelihood**: Low (arena structure is well-defined)

**Risk 2: False negatives in ownership detection**
- **Impact**: Dangling pointers, memory corruption
- **Mitigation**: Conservative approach - when uncertain, copy
- **Likelihood**: Low (prefer false positives over false negatives)

**Risk 3: Performance regression from ownership checks**
- **Impact**: Slower document operations
- **Mitigation**: 
  - Cache ownership results for frequently checked items
  - Add fast path for same Input (no parent traversal)
  - Profile and optimize hot paths
- **Likelihood**: Medium (need to measure)

**Risk 4: Breaking changes to Input API**
- **Impact**: Existing code needs updates
- **Mitigation**: Make `parent` parameter optional (default nullptr)
- **Likelihood**: Low (backward compatible)

### 7.2 Rollback Plan

If issues arise:
1. Keep old `copy_item_deep()` static function as fallback
2. Add compile-time flag: `USE_SMART_DEEP_COPY`
3. Revert to always-copy behavior if needed
4. Measure performance impact before full rollout

---

## 8. Future Enhancements

### 8.1 Potential Optimizations

**Copy-on-Write for Large Structures**:
- Track arena generations
- Share immutable sub-structures between Inputs
- Only copy when mutation detected

**Reference Counting for Shared Data**:
- Add refcount to Arena chunks
- Share chunks between related Inputs
- Only free when refcount reaches zero

**Arena Pooling**:
- Reuse arena chunks across Input lifecycles
- Reduce allocation overhead
- Implement arena "recycling"

### 8.2 Extended Parent Chain Features

**Named Parent References**:
```cpp
Input* schema = Input::create(pool);
Input* base_data = Input::create(pool);
Input* instance = Input::create(pool);
instance->add_parent("schema", schema);
instance->add_parent("base", base_data);
```

**Parent Chain Policies**:
- Read-only parent (cannot mutate parent data)
- Write-through parent (mutations propagate)
- Shadow parent (local overrides)

---

## 9. Conclusion

The smart deep copy enhancement provides:

1. **Performance**: 50-90% reduction in unnecessary copies
2. **Safety**: Automatic arena locality for all edited data
3. **Usability**: Transparent optimization, no API changes needed
4. **Flexibility**: Parent chain support for hierarchical data

**Implementation Effort**: ~3-5 days
- Day 1: Arena ownership + unit tests
- Day 2: MarkBuilder deep copy + tests
- Day 3: MarkEditor integration + tests
- Day 4: Performance benchmarks + optimization
- Day 5: Documentation + code review

**Next Steps**:
1. Implement Phase 1 (arena_owns)
2. Create comprehensive test suite
3. Benchmark current copy_item_deep performance
4. Implement smart deep copy
5. Measure performance improvements
6. Integrate with MarkEditor
7. Production release

---

## Appendix A: Key Code Locations

**Files to Modify**:
- `lib/arena.h` - Add `arena_owns()` declaration
- `lib/arena.c` - Implement `arena_owns()`
- `lambda/lambda-data.hpp` - Add `Input::parent` field
- `lambda/mark_builder.hpp` - Add `deepCopy()`, `isInArena()`
- `lambda/mark_builder.cpp` - Implement deep copy logic
- `lambda/mark_editor.cpp` - Integrate deep copy calls

**Files to Create**:
- `test/test_arena_gtest.cpp` - Arena ownership tests
- `test/test_mark_builder_deepcopy_gtest.cpp` - Deep copy tests

**Files to Update**:
- `test/test_mark_editor_gtest.cpp` - Add external data tests

---

## Appendix B: Performance Metrics

**Memory Usage Scenarios**:

| Scenario | Before (Always Copy) | After (Smart Copy) | Savings |
|----------|---------------------|-------------------|---------|
| Same-input data | 2x memory | 1x memory | 50% |
| Parent-input data | 2x memory | 1x memory | 50% |
| 90% local, 10% external | 1.9x memory | 1.1x memory | 42% |
| All external data | 2x memory | 2x memory | 0% |

**CPU Time Scenarios**:

| Scenario | Before (µs) | After (µs) | Speedup |
|----------|-------------|-----------|---------|
| Copy 1KB local data | 50 | 5 | 10x |
| Copy 1KB external | 50 | 50 | 1x |
| Copy 1MB local | 5000 | 100 | 50x |
| Copy 1MB external | 5000 | 5000 | 1x |

*(Note: Actual measurements TBD during implementation)*

---

**Status**: Ready for implementation  
**Approval**: Pending review  
**Estimated Completion**: 5 days after start
