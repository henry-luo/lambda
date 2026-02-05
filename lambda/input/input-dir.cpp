// input_dir.cpp
// Directory handling for Lambda Script input system
// Implements directory listing as a list of Path items

#include "input.hpp"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <string.h>
#include <errno.h>
#include <time.h>

using namespace lambda;
#include <stdlib.h>
#include "../../lib/datetime.h"
#include <sys/stat.h>

// Windows compatibility definitions
#ifdef _WIN32
#define lstat stat
#define S_ISLNK(mode) (0)  // Windows doesn't have symbolic links in the same way
#endif

// Helper: check if path is a directory
static int is_directory(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return 1;
    return 0;
}

// Returns an Input* representing the directory contents as a list of Path items
// This mirrors how path iteration works in path.c resolve_directory_children()
// original_url: the original URL string before resolution (to detect relative paths)
// directory_path: the resolved absolute path to the directory
Input* input_from_directory(const char* directory_path, const char* original_url, bool recursive, int max_depth) {
    if (!is_directory(directory_path)) {
        fprintf(stderr, "Not a directory: %s\n", directory_path);
        return NULL;
    }
    
    DIR* dir = opendir(directory_path);
    if (!dir) {
        log_error("input_from_directory: cannot open directory: %s", directory_path);
        return NULL;
    }
    
    // Create Input
    Input* input = InputManager::create_input(NULL);
    if (!input) {
        closedir(dir);
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
    children->type_id = LMD_TYPE_LIST;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Create child path extending base
        Path* child_path = path_extend(pool, base_path, entry->d_name);
        if (!child_path) continue;
        
        // Load metadata for the child
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", directory_path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            PathMeta* meta = (PathMeta*)pool_calloc(pool, sizeof(PathMeta));
            if (meta) {
                meta->size = st.st_size;
                meta->modified = *datetime_from_unix(pool, (int64_t)st.st_mtime);
                meta->flags = 0;
                if (S_ISDIR(st.st_mode)) meta->flags |= PATH_META_IS_DIR;
#ifndef _WIN32
                struct stat lst;
                if (lstat(full_path, &lst) == 0 && S_ISLNK(lst.st_mode)) {
                    meta->flags |= PATH_META_IS_LINK;
                }
#endif
                meta->mode = (st.st_mode >> 6) & 0x07;
                child_path->meta = meta;
                child_path->flags |= PATH_FLAG_META_LOADED;
            }
        }
        
        // Add child path to list
        Item child_item;
        child_item.item = (uint64_t)child_path;
        list_push(children, child_item);
        
        // Recursive traversal if requested
        if (recursive && (max_depth < 0 || max_depth > 0)) {
            struct stat st2;
            if (stat(full_path, &st2) == 0 && S_ISDIR(st2.st_mode)) {
                // Note: recursive not fully implemented here, could recurse into subdirs
                // For now, just listing top-level entries
            }
        }
    }
    
    closedir(dir);
    input->root.item = (uint64_t)children;
    return input;
}
