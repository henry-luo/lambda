#pragma once

// Active runtime heap/root API. These entry points can collect or relocate
// data-zone storage, so only lambda-rt provides them; pool/arena users must
// select their non-collecting construction paths explicitly.
#include "../lambda.h"

// Keep C++ linkage to match the active runtime API.
void* heap_alloc(int size, TypeId type_id);
// Dedicated internal carrier for accessor getter/setter storage. It is not a
// public Item type and must never be allocated as LMD_TYPE_FUNC.
#ifdef __cplusplus
extern "C" void* heap_calloc_js_accessor_cell(size_t size);
#else
void* heap_calloc_js_accessor_cell(size_t size);
#endif
void heap_init();
void heap_destroy();
String* heap_create_name(const char* str, size_t len);
// C++ bridge for resources owned by GC wrappers but allocated outside GC zones.
void heap_gc_destroy_external_payload(void* obj, uint16_t type_tag);

#ifdef __cplusplus
extern "C" {
#endif
void* heap_data_alloc(size_t size);
void* heap_data_alloc_uninit(size_t size);  // caller writes every byte before any read/GC safepoint
void* heap_data_calloc(size_t size);
// Retag an identity-preserving managed container only across a proven
// same-layout transition; the visible header and GC allocation tag move as one.
void heap_retag_container(Container* object, TypeId expected, TypeId replacement);
void heap_register_gc_root(uint64_t* slot);
void heap_unregister_gc_root(uint64_t* slot);
bool heap_try_register_gc_object_root(void* object);
void heap_unregister_gc_object_root(void* object);
bool heap_try_register_gc_root_range(uint64_t* base, int count);
void heap_register_gc_root_range(uint64_t* base, int count);
void heap_unregister_gc_root_range(uint64_t* base);
void heap_register_gc_weak(uint64_t* slot,
                           void (*on_clear)(uint64_t*, void*), void* context);
void heap_unregister_gc_weak(uint64_t* slot);
// Explicit-subject variants are reserved for isolated collector tests and
// cold control-plane teardown where the subject is not the executing owner.
bool heap_register_gc_root_for(Context* runtime, uint64_t* slot);
void heap_unregister_gc_root_for(Context* runtime, uint64_t* slot);
bool heap_register_gc_root_range_for(Context* runtime, uint64_t* base, int count);
// The owning heap's incarnation id (0 when the context has no live heap).
uint64_t heap_generation_for(Context* runtime);
void heap_unregister_gc_root_range_for(Context* runtime, uint64_t* base);
bool heap_try_register_gc_root(uint64_t* slot);
void heap_no_gc_scope_begin(void);
void heap_no_gc_scope_end(void);
void heap_gc_defer_collection_begin(void);
void heap_gc_defer_collection_end(void);
LambdaGcScopeCheckpoint lambda_gc_scope_checkpoint_capture(void);
bool lambda_gc_scope_checkpoint_restore(const LambdaGcScopeCheckpoint* checkpoint);

// Native payloads owned by GC-traced wrappers use these counters for pressure
// only. The opaque owner is valid until that wrapper's finalizer releases it.
typedef enum HeapGcExternalKind {
    HEAP_GC_EXTERNAL_ARRAYBUFFER = 0,
    HEAP_GC_EXTERNAL_JS_DENSE_ARRAY = 1,
    HEAP_GC_EXTERNAL_BINARY = 2,
    HEAP_GC_EXTERNAL_OTHER = 3
} HeapGcExternalKind;
void* heap_gc_external_preflight(size_t bytes, int kind);
void heap_gc_external_record_alloc(void* owner, size_t bytes, int kind);
void heap_gc_external_record_release(void* owner, size_t bytes, int kind);

#ifdef __cplusplus
}
#endif
