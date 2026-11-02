// input_dir.cpp
// Directory handling for Lambda Script input system
// Implements directory listing as a list of Path items

#include "input.hpp"
#include "../io/mark_builder.hpp"
#include "input-context.hpp"
#include "../io/security_io.h"
#include <string.h>
#include <time.h>

using namespace lambda;
#include <stdlib.h>
#include "../../lib/datetime.h"
#include "../../lib/file.h"
#include "../../lib/file_utils.h"
#include "../../lib/arraylist.h"
#include "../../lib/log.h"

// Returns an Input* representing the directory contents as a list of Path items
// This mirrors how path iteration works in path.c resolve_directory_children()
// original_url: the original URL string before resolution (to detect relative paths)
// directory_path: the resolved absolute path to the directory
Input* input_from_directory_with_io(const char* directory_path,
        const char* original_url, NamePool* name_parent, const SecurityIOClient* io) {
    SecurityDirectory listing = {};
    if (!security_io_enumerate(io, directory_path,
            io ? SECURITY_IO_DIRECTORY_LIMIT : UINT32_MAX, &listing, nullptr)) return nullptr;
    // Create Input
    Input* input = InputManager::create_input_with_name_parent(NULL, name_parent);
    if (!input) {
        security_io_directory_free(&listing);
        return NULL;
    }
    
    Pool* pool = input->pool;
    
    // Determine if original URL was relative
    bool is_relative = original_url && (original_url[0] == '.' && (original_url[1] == '/' || original_url[1] == '\0'));
    
    // Build a base path from the appropriate path string
    Path* base_path;
    const char* p;
    
    if (is_relative) {
        // Use relative scheme with original relative path
        base_path = path_new(pool, PATH_SCHEME_REL);
        p = original_url + (original_url[1] ? 2 : 1);
    } else {
        // Use file scheme with absolute path
        base_path = path_new(pool, PATH_SCHEME_FILE);
        p = directory_path;
        // Skip leading / for absolute paths
        if (p[0] == '/') p++;
    }
    
    // Parse remaining path segments
    while (*p) {
        // Skip any leading slashes
        while (*p == '/') p++;
        if (!*p) break;
        
        // Find end of segment
        const char* seg_start = p;
        while (*p && *p != '/') p++;
        
        // Copy segment
        size_t seg_len = p - seg_start;
        char* segment = pool_dup_n(pool, seg_start, seg_len);
        
        base_path = path_extend(pool, base_path, segment);
    }
    
    // Create result list using pool allocation
    List* children = (List*)pool_calloc(pool, sizeof(List));
    children->type_id = LMD_TYPE_ARRAY;
    
    for (uint32_t i = 0; i < listing.count; i++) {
        SecurityDirectoryEntry* entry = &listing.entries[i];
        
        // Create child path extending base
        Path* child_path = path_extend(pool, base_path, entry->name);
        if (!child_path) continue;
        
        if (entry->has_metadata && !path_apply_stat_metadata(child_path, pool, &entry->metadata)) {
            security_io_directory_free(&listing);
            return nullptr;
        }
        
        // Add child path to list
        Item child_item;
        child_item.item = (uint64_t)child_path;
        list_push_pooled(children, child_item, pool);
        
    }
    security_io_directory_free(&listing);
    input->root.item = (uint64_t)children;
    return input;
}

Input* input_from_directory_with_name_parent(const char* directory_path,
        const char* original_url, bool recursive, int max_depth, NamePool* name_parent) {
    return input_from_directory_with_io(directory_path, original_url, name_parent, nullptr);
}

Input* input_from_directory(const char* directory_path, const char* original_url,
        bool recursive, int max_depth) {
    return input_from_directory_with_name_parent(directory_path, original_url,
        recursive, max_depth, NULL);
}
