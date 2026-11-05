#pragma once
#include "transpiler.hpp"
#include "../../lib/file.h"

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

// The returned local path is owned by the caller. The guard prevents a
// cancelled task from staging or publishing file content.
extern "C" StrBuf* lambda_get_local_path_from_item(Item item);
bool io_resource_file_guard(void* context, const char* relative,
    FileAnchorAction action, const FileRevision* object);
struct LambdaTask;
// The caller checks file-I/O authority and owns the returned anchor and leaf.
Item io_resource_create_anchor(Item target, const char* operation, LambdaTask* owner,
    FileAnchor** anchor, char** leaf);
