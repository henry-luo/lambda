#include "render.hpp"
#include "../lib/memtrack.h"

// raster surfaces and encoding buffers share the same memory-pressure policy.
bool render_memory_allow_allocation(MemContext* memory, size_t bytes) {
    size_t limit = 0; memtrack_get_limits(nullptr, nullptr, &limit);
    if (!limit) return true;
    size_t usage = memtrack_get_current_usage();
    if (usage < limit && bytes <= limit - usage) return true;
    mem_context_request_reclaim(memory, MEM_PRESSURE_HIGH, bytes);
    usage = memtrack_get_current_usage();
    return usage < limit && bytes <= limit - usage;
}
