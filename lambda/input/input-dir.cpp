// input_dir.cpp
// Directory handling for Lambda Script input system
// Implements directory listing as a list of Path items

#include "input.hpp"
#include "../mark_builder.hpp"
#include "input-context.hpp"
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
Input* input_from_directory(const char* directory_path, const char* original_url, bool recursive, int max_depth) {
    if (!file_is_dir(directory_path)) {
        log_error("input_from_directory: not a directory: %s", directory_path);
        return NULL;
    }
    
    ArrayList* entries = dir_list(directory_path);
    if (!entries) {
        log_error("input_from_directory: cannot open directory: %s", directory_path);
        return NULL;
    }
    
    // Create Input
    Input* input = InputManager::create_input(NULL);
    if (!input) {
        // free entries
        for (int i = 0; i < entries->length; i++) {
            dir_entry_free((DirEntry*)entries->data[i]);
        }
        arraylist_free(entries);
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
        p = original_url + 2;  // skip "./"
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
        char* segment = (char*)pool_alloc(pool, seg_len + 1);
        memcpy(segment, seg_start, seg_len);
        segment[seg_len] = '\0';
        
        base_path = path_extend(pool, base_path, segment);
    }
    
    // Create result list using pool allocation
    List* children = (List*)pool_calloc(pool, sizeof(List));
    children->type_id = LMD_TYPE_ARRAY;
    
    int count = entries->length;
    for (int i = 0; i < count; i++) {
        DirEntry* entry = (DirEntry*)entries->data[i];
        
        // Create child path extending base
        Path* child_path = path_extend(pool, base_path, entry->name);
        if (!child_path) continue;
        
        // Load metadata for the child
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", directory_path, entry->name);
        
        FileStat st = file_stat(full_path);
        if (st.exists) {
            PathMeta* meta = (PathMeta*)pool_calloc(pool, sizeof(PathMeta));
            if (meta) {
                meta->size = st.size;
                meta->modified = *datetime_from_unix(pool, (int64_t)st.modified);
                meta->flags = 0;
                if (st.is_dir) meta->flags |= PATH_META_IS_DIR;
                if (st.is_symlink) meta->flags |= PATH_META_IS_LINK;
                meta->mode = (st.mode >> 6) & 0x07;
                child_path->meta = meta;
                child_path->flags |= PATH_FLAG_META_LOADED;
            }
        }
        
        // Add child path to list
        Item child_item;
        child_item.item = (uint64_t)child_path;
        list_push(children, child_item);
        
        dir_entry_free(entry);
    }
    
    arraylist_free(entries);
    input->root.item = (uint64_t)children;
    return input;
}
