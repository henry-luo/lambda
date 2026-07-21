#pragma once

// Active runtime heap/root API. These entry points can collect or relocate
// data-zone storage, so only lambda-rt provides them; pool/arena users must
// select their non-collecting construction paths explicitly.
#include "../lambda.h"

// Kept C++ linkage to match the active transpiler API; this is not callable
// from the frozen C2MIR surface.
void* heap_alloc(int size, TypeId type_id);
void heap_init();
void heap_destroy();
String* heap_create_name(const char* str, size_t len);

#ifdef __cplusplus
extern "C" {
#endif
void* heap_data_alloc(size_t size);
void* heap_data_calloc(size_t size);
void heap_register_gc_root(uint64_t* slot);
void heap_unregister_gc_root(uint64_t* slot);
void heap_register_gc_weak(uint64_t* slot,
                           void (*on_clear)(uint64_t*, void*), void* context);
void heap_unregister_gc_weak(uint64_t* slot);
bool heap_register_gc_root_for(Context* runtime, uint64_t* slot);
void heap_unregister_gc_root_for(Context* runtime, uint64_t* slot);
void heap_no_gc_scope_begin(Context* runtime);
void heap_no_gc_scope_end(Context* runtime);

#ifdef __cplusplus
}
#endif
