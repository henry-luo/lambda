#pragma once

// Scoped GC guards are lexical in normal execution, but recovery landings
// must restore the depths recorded before a non-local jump bypasses them.
typedef struct LambdaGcScopeCheckpoint {
    void* heap;
    int defer_collection_depth;
#ifndef NDEBUG
    int no_gc_scope_depth;
#endif
} LambdaGcScopeCheckpoint;
