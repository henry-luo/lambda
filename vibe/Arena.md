# Chunk-Based Arena Allocator Implementation Plan

## Overview

Design and implement a chunk-based arena allocator (`arena.c`/`arena.h`) that sits on top of the existing `mempool.c` memory pool system. The arena will provide fast, sequential allocations with bulk deallocation, optimized for temporary allocations and parser/compiler workloads.

## Background: Current Memory Pool System

The existing `mempool.c` provides:
- **Backend**: rpmalloc with dedicated heap handles per pool
- **Features**: Thread-safe, general-purpose allocations with individual free capability
- **Usage Pattern**: Used throughout Lambda runtime for AST nodes, strings, formatting contexts
- **Overhead**: Each allocation has rpmalloc's metadata overhead
- **Lifetime**: Pool lives until explicit `pool_destroy()`, but individual items can be freed

## Arena Allocator Goals

### Primary Objectives
1. **Fast Sequential Allocation**: Bump-pointer allocation within chunks (O(1) with no metadata per allocation)
2. **Bulk Deallocation**: Reset entire arena in one operation
3. **Minimal Overhead**: No per-allocation metadata (except chunk headers)
4. **Temporary Scope Support**: Perfect for parser phases, temporary formatting buffers
5. **Pool Integration**: Built on top of existing Pool system for chunk allocation

### Use Cases
- **Parser Temporary Buffers**: Token streams, string accumulation during parsing
- **Formatter Scratch Space**: Building output strings, temporary calculations
- **Compiler Phases**: Single-pass allocations that all die together
- **Test Fixtures**: Allocate test data, run test, destroy all at once

## Design Specification

### Core Structure

```c
// arena.h

typedef struct ArenaChunk ArenaChunk;
typedef struct Arena Arena;

struct ArenaChunk {
    ArenaChunk* next;           // linked list of chunks
    size_t capacity;            // total size of this chunk's data
    size_t used;                // bytes used in this chunk
    unsigned char data[];       // flexible array member for allocation space
};

struct Arena {
    Pool* pool;                 // underlying memory pool for chunk allocation
    ArenaChunk* current;        // current chunk being allocated from
    ArenaChunk* first;          // first chunk in list (for reset)
    size_t chunk_size;          // current chunk size (grows adaptively)
    size_t max_chunk_size;      // maximum chunk size limit
    size_t total_allocated;     // total bytes allocated across all chunks
    size_t total_used;          // total bytes actually used
    unsigned alignment;         // default alignment (typically 8 or 16)
    unsigned chunk_count;       // number of chunks allocated
    unsigned valid;             // validity marker (ARENA_VALID_MARKER)
};
```

### API Functions

```c
// create/destroy operations
Arena* arena_create(Pool* pool, size_t initial_chunk_size, size_t max_chunk_size);
Arena* arena_create_default(Pool* pool);  // uses 4KB initial, 64KB max with adaptive sizing
void arena_destroy(Arena* arena);

// allocation operations
void* arena_alloc(Arena* arena, size_t size);
void* arena_alloc_aligned(Arena* arena, size_t size, size_t alignment);
void* arena_calloc(Arena* arena, size_t size);  // zero-initialized

// string operations
char* arena_strdup(Arena* arena, const char* str);
char* arena_strndup(Arena* arena, const char* str, size_t n);
char* arena_sprintf(Arena* arena, const char* fmt, ...);  // formatted string

// bulk operations
void arena_reset(Arena* arena);        // reset to beginning, keep first chunk
void arena_clear(Arena* arena);        // free all chunks except first, reset

// statistics/debugging
size_t arena_total_allocated(Arena* arena);  // total memory from pool
size_t arena_total_used(Arena* arena);       // actual bytes allocated by user
size_t arena_waste(Arena* arena);            // fragmentation waste
size_t arena_chunk_count(Arena* arena);      // number of chunks
```

## Implementation Details

### Allocation Strategy

**Bump Pointer Allocation**:
1. Check if current chunk has enough space
2. If yes: return pointer, advance bump pointer
3. If no: allocate new chunk, link it, use new chunk
4. Alignment handled by rounding up the current position

**Adaptive Chunk Size Strategy**:
- Start with small chunks (default 4KB) for minimal waste on small workloads
- Grow chunk size exponentially: 4KB → 8KB → 16KB → 32KB → 64KB
- Growth trigger: when a new chunk is needed (indicates continued allocation)
- Maximum chunk size: 64KB (configurable at creation)
- For large single allocations: use `max(current_chunk_size, requested_size + chunk_header)`
- Benefits:
  - Small workloads use minimal memory (4-8KB total)
  - Large workloads quickly scale up to efficient chunk sizes
  - Balances memory efficiency with allocation performance

**Alignment**:
- Default 16-byte alignment for SIMD compatibility
- Round up current position to alignment boundary before allocation
- Ensure chunk data is properly aligned from pool

### Memory Layout

```
Arena Structure (on stack or pool)
  ↓
  pool: Pool* → mempool.c (rpmalloc backend)
  current: ArenaChunk* → [Chunk 3]
  first: ArenaChunk* → [Chunk 1]

Chunk Layout:
┌─────────────────────────────────────┐
│ ArenaChunk Header                   │
│  - next: ArenaChunk*                │
│  - capacity: size_t                 │
│  - used: size_t                     │
├─────────────────────────────────────┤
│ data[0]                             │
│ ↓ (bump pointer advances)           │
│ [allocated region 1]                │
│ [allocated region 2]                │
│ [allocated region 3]                │
│ ... (used = offset to next free)    │
│ [free space remaining]              │
│                                     │
└─────────────────────────────────────┘
```

### Key Algorithms

**arena_alloc(arena, size)**:
```
1. aligned_size = ALIGN_UP(size, arena->alignment)
2. chunk = arena->current
3. if (chunk->used + aligned_size <= chunk->capacity)
     ptr = &chunk->data[chunk->used]
     chunk->used += aligned_size
     arena->total_used += aligned_size
     return ptr
4. else
     // Grow chunk size adaptively (double until max)
     next_chunk_size = min(arena->chunk_size * 2, arena->max_chunk_size)
     arena->chunk_size = next_chunk_size

     // Allocate chunk large enough for request or next size
     chunk_capacity = max(next_chunk_size, aligned_size)
     new_chunk = allocate_chunk(arena->pool, chunk_capacity)

     link new_chunk after current
     arena->current = new_chunk
     arena->chunk_count++
     goto step 2  // use new chunk
```

**arena_reset(arena)**:
```
1. Set all chunks' used = 0
2. arena->current = arena->first
3. arena->total_used = 0
4. Keep all chunks for reuse (no pool_free calls)
```

**arena_clear(arena)**:
```
1. Keep first chunk, reset it (used = 0)
2. Free all other chunks back to pool
3. arena->current = arena->first
4. arena->total_used = 0
5. arena->total_allocated = first_chunk->capacity
```

## Integration with Existing Code

### Pattern 1: Temporary Allocations in Formatters

**Before (current pattern)**:
```c
Pool* temp_pool = pool_create();
// ... allocate temp buffers with pool_alloc(temp_pool, size)
// ... do work
pool_destroy(temp_pool);  // frees everything
```

**After (with arena)**:
```c
Pool* pool = pool_create();
Arena* arena = arena_create_default(pool);  // 4KB initial, grows to 64KB
// ... allocate with arena_alloc(arena, size) - much faster
// ... do work
arena_destroy(arena);      // destroys arena
pool_destroy(pool);        // destroys underlying pool
```

### Pattern 2: Reusable Arena

**For multiple parse/format cycles**:
```c
Pool* pool = pool_create();
Arena* arena = arena_create_default(pool);  // Adaptive sizing

for (each input file) {
    arena_reset(arena);    // reuse memory, keeps grown chunk size
    // ... parse with arena_alloc()
    // ... process
    // Note: chunk_size stays at grown size for next iteration
}

arena_destroy(arena);
pool_destroy(pool);
```

### Pattern 3: Scoped Arena

**For nested scopes**:
```c
Arena* outer = arena_create_default(main_pool);
// ... outer allocations

size_t snapshot = outer->current->used;  // manual snapshot
// ... inner temporary allocations
outer->current->used = snapshot;         // manual restore

// Or better - use nested arenas:
Arena* inner = arena_create(outer_pool, 4096);  // small chunks
// ... temporary work
arena_destroy(inner);  // inner arena gone
```

## Implementation Phases

### Phase 1: Core Implementation ✅ COMPLETE
- [x] Define structures in `arena.h`
- [x] Implement `arena_create()`, `arena_destroy()`
- [x] Implement `arena_alloc()` with alignment and adaptive growth
- [x] Implement chunk allocation and linking
- [x] Add validation macros (ARENA_VALID_MARKER)
- [x] Implement adaptive chunk sizing (double until max)

### Phase 2: Extended Allocations ✅ COMPLETE
- [x] Implement `arena_calloc()` (zero-init)
- [x] Implement `arena_alloc_aligned()` (custom alignment)
- [x] Add overflow protection (SIZE_LIMIT checks)
- [x] Handle edge cases (zero size, null arena)

### Phase 3: String Operations ✅ COMPLETE
- [x] Implement `arena_strdup()`
- [x] Implement `arena_strndup()`
- [x] Implement `arena_sprintf()` with variable args
- [x] String allocations work with adaptive sizing

### Phase 4: Bulk Operations ✅ COMPLETE
- [x] Implement `arena_reset()` for reuse
- [x] Implement `arena_clear()` for partial cleanup
- [x] Safety checks via ARENA_VALID_MARKER validation

### Phase 5: Statistics & Debugging ✅ COMPLETE
- [x] Implement `arena_total_allocated()`
- [x] Implement `arena_total_used()`
- [x] Implement `arena_waste()` calculation
- [x] Implement `arena_chunk_count()`
- [x] Statistics track chunk size growth pattern

## Testing Strategy

### Unit Tests (`test/test_arena_gtest.cpp`) ✅ COMPLETE

**Test Framework**: Google Test (GTest)
**Total Tests**: 54 tests across 3 test suites
**Status**: All tests passing

#### Test Suite Breakdown

**ArenaTest (18 tests)** - Basic functionality:
- ✅ Create/destroy arena
- ✅ Custom chunk sizes
- ✅ Basic allocation
- ✅ Many small allocations
- ✅ Adaptive chunk growth (4KB → 8KB → 16KB → 32KB → 64KB)
- ✅ Large allocations (> chunk_size)
- ✅ Alignment verification (16-byte default)
- ✅ Zero-initialized allocation (calloc)
- ✅ String duplication (strdup, strndup)
- ✅ Formatted strings (sprintf)
- ✅ Reset operation (reuse memory)
- ✅ Clear operation (free extra chunks)
- ✅ Statistics tracking
- ✅ Reuse patterns
- ✅ Null pointer checks
- ✅ Zero-size allocation handling
- ✅ Stress test (1000 varying-size allocations)

**ArenaNegativeTest (19 tests)** - Error handling:
- ✅ Create with null pool
- ✅ Create with zero/invalid sizes
- ✅ Allocate with invalid arena
- ✅ Allocate zero bytes
- ✅ Allocate huge size (> 1GB)
- ✅ Invalid alignment (non-power-of-2, zero)
- ✅ Calloc with null arena
- ✅ String operations with null arena/string
- ✅ Sprintf with null arena/format
- ✅ Reset/clear/destroy null arena (safety)
- ✅ Statistics on null arena
- ✅ Double destroy protection

**ArenaCornerTest (17 tests)** - Edge cases:
- ✅ Single byte allocation
- ✅ Maximum size single allocation (~1GB)
- ✅ Empty string operations
- ✅ Very long strings (10KB+)
- ✅ Alignment boundaries (1, 2, 4, 8, 16, 32, 64, 128, 256 bytes)
- ✅ Alternating small/large allocations
- ✅ Reset after clear
- ✅ Clear after reset
- ✅ Multiple resets preserve chunk size
- ✅ Tiny chunk sizes (64 bytes)
- ✅ Allocation exactly at chunk boundary
- ✅ Sprintf with very long output (1000+ chars)
- ✅ Interleaved allocation and string operations
- ✅ Calloc actually zeroes memory
- ✅ Strndup with exact length
- ✅ Rapid create/destroy cycles (100x)
- ✅ Multiple clear cycles

#### Build Integration ✅ COMPLETE
- Added to `build_lambda_config.json` as part of `lambda-lib`
- Test suite `test_arena_gtest` links against `lambda-lib`
- Builds cleanly with `make build-test`
- Runs via `make test`

#### Memory Safety ✅ VERIFIED
- No memory leaks detected
- Proper cleanup on arena_destroy()
- Safe handling of null pointers
- Double-destroy protection via validity marker
- Alignment correctness verified (256-byte chunk alignment)

## Adaptive Sizing Details

### Growth Pattern
```
Allocation 1:     4KB chunk   (initial)
Chunk exhausted:  8KB chunk   (2x growth)
Chunk exhausted:  16KB chunk  (2x growth)
Chunk exhausted:  32KB chunk  (2x growth)
Chunk exhausted:  64KB chunk  (2x growth, at max)
Chunk exhausted:  64KB chunk  (stays at max)
...
```

### Example Scenarios

**Scenario 1: Small workload (parser with 50 small allocations)**
- Total allocations: ~2KB
- Memory used: 4KB chunk (one chunk only)
- Waste: 2KB (50% efficiency - acceptable for small work)

**Scenario 2: Medium workload (formatter with 5000 small allocations)**
- First 4KB fills → allocate 8KB
- 8KB fills → allocate 16KB
- 16KB fills → allocate 32KB
- Total: ~60KB of allocations across 4 chunks
- Total allocated: 4 + 8 + 16 + 32 = 60KB
- Waste: minimal (last chunk partially used)

**Scenario 3: Large workload (complex document with 100KB allocations)**
- Quickly grows through: 4KB → 8KB → 16KB → 32KB → 64KB
- Then continues with 64KB chunks
- Total: 4 + 8 + 16 + 32 + 64 + 64 = ~188KB for ~100KB of data
- Efficiency: ~53% (good for bump allocator)

**Scenario 4: Large single allocation (50KB request)**
- First chunk 4KB exists but too small
- Allocate max(8KB, 50KB) = 50KB chunk directly
- Next growth: 16KB (continues doubling from 8KB target)
- Handles large objects efficiently without waste

### Reset Behavior
After `arena_reset()`, the chunk size stays at its grown value. This is optimal for reuse scenarios:
```c
Arena* arena = arena_create_default(pool);

// First iteration: grows from 4KB → 64KB
process_document(arena);  // Heavy allocation
arena_reset(arena);       // Keep 64KB chunks

// Second iteration: already at 64KB, no growth needed
process_document(arena);  // Fast! No chunk allocation
arena_reset(arena);
```

## Performance Considerations

### Expected Benefits
- **Allocation Speed**: 10-100x faster than pool_alloc (just pointer bump)
- **Cache Locality**: Sequential allocations = better cache usage
- **Deallocation Speed**: O(1) reset vs O(n) individual frees
- **Memory Overhead**: ~0 bytes per allocation vs rpmalloc's ~16 bytes
- **Adaptive Efficiency**: Small workloads use minimal memory, large workloads scale automatically
- **Optimal for Mixed Workloads**: Starts conservative (4KB), grows to efficient size (64KB)

### Trade-offs
- **Fragmentation**: Unused space at end of chunks (minimized by starting small)
- **No Individual Free**: Must wait for arena_reset/destroy
- **Memory Held**: Arena keeps chunks until clear/destroy (but starts small)
- **Not Thread-Safe**: Each thread should have own arena (pool underneath is thread-safe)
- **Growth Overhead**: First few allocations trigger chunk growth (amortized O(1))

## Safety & Correctness

### Validation
- `ARENA_VALID_MARKER` pattern (0xABCD4321)
- Check arena validity in all operations
- Null pointer checks
- Size overflow checks (SIZE_LIMIT = 1GB)

### Alignment Safety
- Ensure chunk->data is properly aligned (from pool)
- Round allocation sizes up to alignment
- Support custom alignment via `arena_alloc_aligned()`

### Debug Mode
- Optional: Track allocations in debug list
- Optional: Fill freed memory with 0xDD pattern
- Optional: Detect use-after-reset bugs

## Build System Integration

### Premake Configuration ✅ COMPLETE
Added to `build_lambda_config.json`:
```json
{
  "lambda-lib": {
    "type": "static",
    "sources": [
      "lib/arena.c",
      "lib/mempool.c",
      ...
    ],
    "headers": ["lib/"],
    "includes": ["lib/"],
    "links": ["rpmalloc"]
  },
  "test_arena_gtest": {
    "type": "console",
    "sources": ["test/test_arena_gtest.cpp"],
    "links": ["lambda-lib", "gtest"]
  }
}
```

### File Locations ✅ COMPLETE
- **Implementation**: `lib/arena.c` (full implementation)
- **Header**: `lib/arena.h` (public API with documentation)
- **Tests**: `test/test_arena_gtest.cpp` (54 GTest tests)
- **Examples**: `examples/arena_example.c` (5 usage patterns)

### Makefile Targets ✅ WORKING
- `make build` - builds arena.o into lambda-lib.a
- `make build-test` - builds test executables
- `make test` - runs all tests including test_arena_gtest
- Tests integrated into main test suite

## Documentation

### Public API Documentation
- Add comprehensive header comments in `arena.h`
- Explain when to use arena vs pool
- Provide example usage patterns

### Internal Documentation
- Comment chunk allocation strategy
- Explain alignment calculations
- Document edge cases and assumptions

## Future Enhancements (Optional)

### Phase 6+: Advanced Features (Future Work)
- [ ] **Snapshot/Restore**: Save arena state, restore later
- [ ] **Memory Limits**: Set maximum arena size, fail gracefully
- [ ] **Custom Allocators**: Allow arena to use custom pool vs shared pool
- [ ] **Statistics Callbacks**: Hook for tracking allocation patterns
- [ ] **Arena Pools**: Pool of reusable arenas for even faster create/destroy
- [ ] **Performance Benchmarking**: Measure actual speedup vs pool_alloc

### Integration Opportunities (Future Work)
- [ ] Replace temp pools in formatters (format-md.cpp, format-xml.cpp)
- [ ] Use in parser for token/AST temporary storage
- [ ] Use in validator for temporary type checking structures
- [ ] Add to Input system for parsing scratch space
- [ ] Benchmark real-world usage patterns in Lambda runtime

## Success Criteria

1. ✅ Arena allocates correctly with proper alignment (16-byte default, up to 256-byte)
2. ✅ All chunks are tracked and freed on destroy
3. ✅ Reset and clear work without memory leaks
4. ⏳ Performance benchmarking pending (expected 5-10x faster than pool_alloc)
5. ✅ All 54 tests pass with proper memory handling
6. ✅ Zero regressions in existing code
7. ✅ Documentation complete and clear (API docs in arena.h, design in Arena.md)

## Implementation Status: ✅ COMPLETE

**Date Completed**: November 17, 2025

**Summary**:
- Full arena allocator implementation with adaptive chunk sizing (4KB → 64KB)
- Comprehensive test suite with 54 tests covering normal, negative, and corner cases
- All tests passing with proper error handling and memory safety
- Integrated into lambda-lib static library
- Ready for integration into Lambda runtime components

**Key Implementation Details**:
- **Chunk Alignment**: 256-byte alignment for chunk data to support alignments up to 256 bytes
- **Adaptive Growth**: Exponential doubling (4KB → 8KB → 16KB → 32KB → 64KB max)
- **Size Limits**: 1GB SIZE_LIMIT with proper overflow checking including chunk header overhead
- **Accounting**: Tracks aligned sizes in total_used for accurate statistics
- **Safety**: ARENA_VALID_MARKER (0xABCD4321) prevents use-after-destroy

**Robustness Enhancements**:
- Fixed alignment support for alignments > 16 bytes (now supports up to 256 bytes)
- Fixed large allocation handling to account for chunk header overhead
- Fixed allocation accounting to track aligned sizes properly
- Comprehensive negative test coverage (19 tests for invalid inputs)
- Extensive corner case testing (17 tests for edge conditions)

## References

- **Current Pool System**: `lib/mempool.c`, `lib/mempool.h`
- **Usage Patterns**: `lambda/format/*.cpp`, `lambda/input/*.cpp`
- **Build System**: `build_lambda_config.json`, `utils/generate_premake.py`
- **Testing Framework**: Criterion C testing library in `test/`
