#pragma once
#include "transpiler.hpp"

// Common immutable VMap carrier for task-owned native IO resources. The task
// owns native cleanup; the carrier never releases or traces native state.
Item io_resource_get(void* data, Item key);
void io_resource_set(void* data, Item key, Item value);
int64_t io_resource_count(void* data);
SymbolKeyList* io_resource_keys(void* data);
Item io_resource_at(void* data, int64_t index);
void io_resource_destroy(void* data);
void io_resource_trace(void* data, gc_heap* gc);
VMap* io_resource_handle_new(VMapVtable* vtable, void* brand, void* data);
