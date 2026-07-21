#ifndef LAMBDA_IO_INPUT_ALLOCATION_CONTEXT_H
#define LAMBDA_IO_INPUT_ALLOCATION_CONTEXT_H

#include <stdbool.h>

typedef struct Pool Pool;
typedef struct Arena Arena;

// Parsing and document construction need only pool/arena ownership and a
// small UI policy. Keeping this separate from rt Context prevents an input
// include from publishing GC roots, stack limits, or runtime scalar homes.
typedef struct InputAllocationContext {
    Pool* pool;
    Arena* arena;
    bool disable_string_merging;
    bool ui_mode;
} InputAllocationContext;

extern __thread InputAllocationContext* input_allocation_context;

#endif
