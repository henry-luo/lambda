#include "render.hpp"
#include "../lib/memtrack.h"

// raster surfaces and encoding buffers share the same memory-pressure policy.
bool render_memory_allow_allocation(MemContext* memory, size_t bytes) {
    return memtrack_allow_allocation(memory, bytes);
}
