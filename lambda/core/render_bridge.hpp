#pragma once

#include "value.hpp"

// Borrowed render identities cross the runtime/engine boundary for one rebuild.
typedef struct RenderMapLookup {
    Item source_item;
    const char* template_ref;
} RenderMapLookup;

typedef struct RetransformResult {
    Item parent_result;
    Item new_result;
    Item old_result;
    int child_index;
    int child_count;
    const char* template_ref;
} RetransformResult;
