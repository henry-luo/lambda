#include "io_resource.hpp"
#include "../../lib/log.h"

Item io_resource_get(void* data, Item key) { (void)data; (void)key; return ItemNull; }
void io_resource_set(void* data, Item key, Item value) {
    (void)data; (void)key; (void)value;
    log_error("io.resource: native IO handles are immutable");
}
int64_t io_resource_count(void* data) { (void)data; return 0; }
SymbolKeyList* io_resource_keys(void* data) { (void)data; return NULL; }
Item io_resource_at(void* data, int64_t index) { (void)data; (void)index; return ItemNull; }
void io_resource_destroy(void* data) { (void)data; }
void io_resource_trace(void* data, gc_heap* gc) { (void)data; (void)gc; }
VMap* io_resource_handle_new(VMapVtable* vtable, void* brand, void* data) {
    VMap* handle = (VMap*)heap_calloc(sizeof(VMap), LMD_TYPE_VMAP);
    if (!handle) return NULL;
    handle->type_id = LMD_TYPE_VMAP;
    handle->vtable = vtable;
    handle->host_type = brand;
    handle->host_data = data;
    handle->data = data;
    return handle;
}
