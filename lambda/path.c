/**
 * path.c - Lambda Path implementation
 *
 * Paths are segmented symbols for file/URL navigation.
 * A path is a linked chain of segments from leaf to root.
 *
 * Example: file.etc.hosts
 *   Path("hosts") -> Path("etc") -> Path("file") -> ROOT_SENTINEL
 */

#include "../lib/strbuf.h"
#include "../lib/log.h"
#include "../lib/mempool.h"
#include "lambda.h"
#include "sysinfo.h"

#include <string.h>
#include <stdlib.h>

// Forward declaration for EvalContext (defined in lambda-data.hpp as C++ class)
typedef struct EvalContext EvalContext;

// Thread-local eval context (defined in runner.cpp)
extern __thread EvalContext* context;

// Helper functions to access context members (implemented in runner.cpp)
extern Pool* eval_context_get_pool(EvalContext* ctx);

// Root sentinel - parent of all scheme roots (has NULL parent itself)
static Path ROOT_SENTINEL = {
    LMD_TYPE_PATH,  // type_id
    0,              // flags
    0,              // ref_cnt
    NULL,           // name
    NULL            // parent
};

// Predefined root scheme paths
static Path* scheme_roots[PATH_SCHEME_COUNT] = { NULL };

// Scheme names for string conversion
static const char* scheme_names[PATH_SCHEME_COUNT] = {
    "file",   // PATH_SCHEME_FILE
    "http",   // PATH_SCHEME_HTTP
    "https",  // PATH_SCHEME_HTTPS
    "sys",    // PATH_SCHEME_SYS
    ".",      // PATH_SCHEME_REL (relative)
    ".."      // PATH_SCHEME_PARENT
};

/**
 * Initialize root scheme paths.
 * Call this once at runtime startup.
 */
void path_init(void) {
    if (scheme_roots[0] != NULL) {
        return;  // already initialized
    }

    if (!context) {
        log_error("path_init: no context available");
        return;
    }

    Pool* pool = eval_context_get_pool(context);
    if (!pool) {
        log_error("path_init: no pool available");
        return;
    }

    for (int i = 0; i < PATH_SCHEME_COUNT; i++) {
        Path* root = (Path*)pool_calloc(pool, sizeof(Path));
        root->type_id = LMD_TYPE_PATH;
        root->flags = 0;
        root->ref_cnt = 0;
        root->name = scheme_names[i];  // static strings, no need to intern
        root->parent = &ROOT_SENTINEL;
        scheme_roots[i] = root;
    }

    log_debug("path_init: initialized %d scheme roots", PATH_SCHEME_COUNT);
}

/**
 * Get predefined root path for a scheme.
 */
Path* path_get_root(PathScheme scheme) {
    if (scheme < 0 || scheme >= PATH_SCHEME_COUNT) {
        log_error("path_get_root: invalid scheme %d", scheme);
        return NULL;
    }

    if (scheme_roots[0] == NULL) {
        path_init();
    }

    return scheme_roots[scheme];
}

/**
 * Append a segment to a path.
 * Returns a new path with the segment appended.
 */
Path* path_append(Path* parent, const char* segment) {
    if (!segment) {
        log_error("path_append: NULL segment");
        return parent;
    }
    return path_append_len(parent, segment, strlen(segment));
}

/**
 * Append a segment to a path (with explicit length).
 * Copies the segment string into pool memory.
 */
Path* path_append_len(Path* parent, const char* segment, size_t len) {
    if (!parent) {
        log_error("path_append_len: NULL parent");
        return NULL;
    }
    if (!segment || len == 0) {
        log_error("path_append_len: empty segment");
        return parent;
    }

    if (!context) {
        log_error("path_append_len: no context");
        return NULL;
    }

    Pool* pool = eval_context_get_pool(context);
    if (!pool) {
        log_error("path_append_len: no pool available");
        return NULL;
    }

    // Allocate path
    Path* path = (Path*)pool_calloc(pool, sizeof(Path));
    path->type_id = LMD_TYPE_PATH;
    path->flags = 0;
    path->ref_cnt = 0;
    path->parent = parent;

    // Copy segment name into pool memory
    char* name_copy = (char*)pool_alloc(pool, len + 1);
    memcpy(name_copy, segment, len);
    name_copy[len] = '\0';
    path->name = name_copy;

    return path;
}

/**
 * Get the scheme name for a path (file, http, https, sys, etc.)
 */
const char* path_get_scheme_name(Path* path) {
    if (!path) return NULL;

    // Walk to root
    while (path->parent && path->parent != &ROOT_SENTINEL) {
        path = path->parent;
    }

    return path->name;
}

/**
 * Check if path is a root scheme (no segments after scheme).
 */
bool path_is_root(Path* path) {
    if (!path) return false;
    return path->parent == &ROOT_SENTINEL;
}

/**
 * Get the scheme type of a path.
 * Walks to root and returns the scheme identifier.
 * Returns -1 if path is invalid.
 */
PathScheme path_get_scheme(Path* path) {
    if (!path) return (PathScheme)-1;

    // Walk to root
    while (path->parent && path->parent != &ROOT_SENTINEL) {
        path = path->parent;
    }

    // Check against known scheme roots
    for (int i = 0; i < PATH_SCHEME_COUNT; i++) {
        if (path == scheme_roots[i]) {
            return (PathScheme)i;
        }
    }

    return (PathScheme)-1;  // unknown scheme
}

/**
 * Check if a path is absolute (file, http, https, sys).
 * Returns false for relative (.) and parent (..) paths.
 */
bool path_is_absolute(Path* path) {
    PathScheme scheme = path_get_scheme(path);
    return scheme == PATH_SCHEME_FILE ||
           scheme == PATH_SCHEME_HTTP ||
           scheme == PATH_SCHEME_HTTPS ||
           scheme == PATH_SCHEME_SYS;
}

/**
 * Get the depth of a path (number of segments including scheme).
 */
int path_depth(Path* path) {
    int depth = 0;
    while (path && path->parent) {  // stop at ROOT_SENTINEL (parent == NULL)
        depth++;
        path = path->parent;
    }
    return depth;
}

// Forward declaration for segment display helper
static const char* path_get_segment_display(Path* path);

/**
 * Convert path to Lambda path string.
 * New syntax: "/.etc.hosts" for absolute, ".test.file" for relative.
 * Properly handles wildcards using segment type flags.
 */
void path_to_string(Path* path, void* out_ptr) {
    StrBuf* out = (StrBuf*)out_ptr;
    if (!path || !out) return;

    // Collect segments in reverse order (store Path* to access type info)
    Path* segments[64];  // max depth 64
    int count = 0;

    Path* p = path;
    while (p && p->parent && count < 64) {
        segments[count++] = p;
        p = p->parent;
    }
    
    // Handle bare root case (just a root node with no children collected)
    if (count == 0 && path->parent == NULL) {
        // This is a root node itself
        const char* root_name = path->name ? path->name : "";
        strbuf_append_str(out, root_name);
        return;
    }

    // Check scheme type for special handling
    bool is_file_scheme = false;
    bool is_rel_scheme = false;
    bool is_parent_scheme = false;
    if (count > 0) {
        const char* root_name = segments[count - 1]->name;
        is_file_scheme = root_name && strcmp(root_name, "file") == 0;
        is_rel_scheme = root_name && strcmp(root_name, ".") == 0;
        is_parent_scheme = root_name && strcmp(root_name, "..") == 0;
    }
    
    // Handle bare root cases with shorthand notation
    if (count == 1) {
        if (is_file_scheme) {
            strbuf_append_char(out, '/');
            return;
        }
        if (is_rel_scheme) {
            strbuf_append_char(out, '.');
            return;
        }
        if (is_parent_scheme) {
            strbuf_append_str(out, "..");
            return;
        }
    }

    // Output in forward order (root first) with dot separators
    // Lambda path syntax: /etc.hosts (file), .src.main (rel), ..parent.file (parent)
    bool just_output_prefix = false;  // tracks if we just output scheme prefix
    for (int i = count - 1; i >= 0; i--) {
        Path* seg_path = segments[i];
        
        // Handle root segment with shorthand notation
        if (i == count - 1) {
            if (is_file_scheme) {
                strbuf_append_char(out, '/');  // file scheme uses / prefix
                just_output_prefix = true;
                continue;  // skip "file" segment itself
            }
            if (is_rel_scheme) {
                strbuf_append_char(out, '.');  // relative scheme uses . prefix
                just_output_prefix = true;
                continue;  // skip "." segment itself
            }
            if (is_parent_scheme) {
                strbuf_append_str(out, "..");  // parent scheme uses .. prefix
                just_output_prefix = true;
                continue;  // skip ".." segment itself
            }
        }
        
        // Add separator dot before non-root segments (but not right after scheme prefix)
        if (i < count - 1 && !just_output_prefix) {
            strbuf_append_char(out, '.');
        }
        just_output_prefix = false;

        LPathSegmentType seg_type = PATH_GET_SEG_TYPE(seg_path);

        // Wildcards don't need quoting
        if (seg_type == LPATH_SEG_WILDCARD) {
            strbuf_append_char(out, '*');
            continue;
        }
        if (seg_type == LPATH_SEG_WILDCARD_REC) {
            strbuf_append_str(out, "**");
            continue;
        }
        if (seg_type == LPATH_SEG_DYNAMIC) {
            strbuf_append_str(out, "<dynamic>");
            continue;
        }

        // Normal segment - check if needs quoting
        const char* seg = seg_path->name ? seg_path->name : "";
        
        bool needs_quote = false;
        for (const char* c = seg; *c; c++) {
            if (*c == '.' || *c == ' ' || *c == '@' || *c == '#' ||
                *c == '$' || *c == '%' || *c == '&' || *c == '?' ||
                *c == '=' || *c == ':' || *c == '-' || *c == '*') {
                needs_quote = true;
                break;
            }
        }

        if (needs_quote) {
            strbuf_append_char(out, '\'');
            strbuf_append_str(out, seg);
            strbuf_append_char(out, '\'');
        } else {
            strbuf_append_str(out, seg);
        }
    }
}

/**
 * Helper to get segment name for OS path output.
 * Returns "*" or "**" for wildcards.
 */
static const char* path_get_os_segment_name(Path* seg_path) {
    LPathSegmentType seg_type = PATH_GET_SEG_TYPE(seg_path);
    switch (seg_type) {
        case LPATH_SEG_WILDCARD: return "*";
        case LPATH_SEG_WILDCARD_REC: return "**";
        case LPATH_SEG_DYNAMIC: return "<dynamic>";
        default: return seg_path->name ? seg_path->name : "";
    }
}

/**
 * Convert path to OS file path (e.g., "/etc/hosts" or "C:\Users\name").
 * Handles wildcards properly using segment type flags.
 */
void path_to_os_path(Path* path, void* out_ptr) {
    StrBuf* out = (StrBuf*)out_ptr;
    if (!path || !out) return;

    // Collect segments in reverse order (store Path* to access type info)
    Path* segments[64];
    int count = 0;

    Path* p = path;
    while (p && p->parent && count < 64) {
        segments[count++] = p;
        p = p->parent;
    }

    // Check scheme (root segment has name set during init)
    const char* scheme = (count > 0) ? segments[count - 1]->name : NULL;
    bool is_file = scheme && strcmp(scheme, "file") == 0;
    bool is_relative = scheme && (strcmp(scheme, ".") == 0 || strcmp(scheme, "..") == 0);

    // Output path
    if (is_file) {
        // Skip "file" scheme, output as absolute path
#ifdef _WIN32
        // Windows: check for drive letter (e.g., file.C.Users)
        if (count > 1) {
            const char* drive = path_get_os_segment_name(segments[count - 2]);
            if (strlen(drive) == 1 && ((drive[0] >= 'A' && drive[0] <= 'Z') ||
                                        (drive[0] >= 'a' && drive[0] <= 'z'))) {
                strbuf_append_char(out, drive[0]);
                strbuf_append_str(out, ":\\");
                for (int i = count - 3; i >= 0; i--) {
                    if (i < count - 3) strbuf_append_char(out, '\\');
                    strbuf_append_str(out, path_get_os_segment_name(segments[i]));
                }
                return;
            }
        }
#endif
        // Unix-style absolute path
        for (int i = count - 2; i >= 0; i--) {  // skip "file"
            strbuf_append_char(out, '/');
            strbuf_append_str(out, path_get_os_segment_name(segments[i]));
        }
    } else if (is_relative) {
        // Relative path
        strbuf_append_str(out, scheme);  // "." or ".."
        for (int i = count - 2; i >= 0; i--) {
            strbuf_append_char(out, '/');
            strbuf_append_str(out, path_get_os_segment_name(segments[i]));
        }
    } else {
        // Other schemes: output as URL
        if (scheme) {
            strbuf_append_str(out, scheme);
            strbuf_append_str(out, "://");
        }
        for (int i = count - 2; i >= 0; i--) {
            if (i < count - 2) strbuf_append_char(out, '/');
            strbuf_append_str(out, path_get_os_segment_name(segments[i]));
        }
    }
}

/**
 * Get root path by name (for parser integration).
 */
Path* path_get_root_by_name(const char* name) {
    if (!name) return NULL;

    for (int i = 0; i < PATH_SCHEME_COUNT; i++) {
        if (strcmp(scheme_names[i], name) == 0) {
            return path_get_root((PathScheme)i);
        }
    }

    return NULL;  // unknown scheme
}

/**
 * Build a path segment by segment (internal helper).
 * Returns a new Path with the segment appended.
 * segment_type should be one of LPATH_SEG_NORMAL, LPATH_SEG_WILDCARD, etc.
 */
static Path* path_append_segment_typed(Pool* pool, Path* parent, const char* segment, LPathSegmentType seg_type) {
    if (!parent) {
        log_error("path_append_segment_typed: NULL parent");
        return NULL;
    }

    Path* new_path = (Path*)pool_calloc(pool, sizeof(Path));
    new_path->type_id = LMD_TYPE_PATH;
    new_path->ref_cnt = 0;
    new_path->parent = parent;
    PATH_SET_SEG_TYPE(new_path, seg_type);

    if (segment && seg_type == LPATH_SEG_NORMAL) {
        // Copy segment name into pool memory for normal segments
        size_t len = strlen(segment);
        char* name_copy = (char*)pool_alloc(pool, len + 1);
        memcpy(name_copy, segment, len);
        name_copy[len] = '\0';
        new_path->name = name_copy;
    } else {
        // Wildcards don't need name storage (type is in flags)
        new_path->name = NULL;
    }

    return new_path;
}

// ============================================================================
// New Path API: path_new, path_extend, path_wildcard, path_wildcard_recursive
// ============================================================================

/**
 * Create a new path starting with the given scheme.
 * This returns the root path for the scheme.
 */
Path* path_new(Pool* pool, int scheme) {
    (void)pool;  // pool not needed for root paths
    return path_get_root((PathScheme)scheme);
}

/**
 * Extend an existing path with a new normal segment.
 * Returns a new path with the segment appended.
 * The original path is not modified.
 */
Path* path_extend(Pool* pool, Path* base, const char* segment) {
    if (!base) {
        log_error("path_extend: NULL base path");
        return NULL;
    }
    if (!segment) {
        log_error("path_extend: NULL segment");
        return base;
    }
    return path_append_segment_typed(pool, base, segment, LPATH_SEG_NORMAL);
}

/**
 * Extend an existing path with another path's segments.
 * Appends all segments from suffix to base.
 * Skips the scheme root of the suffix (only appends actual path segments).
 * Returns a new path.
 */
Path* path_concat(Pool* pool, Path* base, Path* suffix) {
    if (!base) return suffix;
    if (!suffix) return base;

    // Collect suffix segments info in reverse order
    // Stop before the scheme root (which has parent == &ROOT_SENTINEL)
    struct { const char* name; LPathSegmentType type; } segments[64];
    int count = 0;

    Path* p = suffix;
    while (p && p->parent && p->parent != &ROOT_SENTINEL && count < 64) {
        segments[count].name = p->name;
        segments[count].type = PATH_GET_SEG_TYPE(p);
        count++;
        p = p->parent;
    }

    // Append segments in forward order (root to leaf)
    Path* result = base;
    for (int i = count - 1; i >= 0; i--) {
        result = path_append_segment_typed(pool, result, segments[i].name, segments[i].type);
    }

    return result;
}

/**
 * Create a wildcard segment (*) - matches any single path component.
 * Uses LPATH_SEG_WILDCARD flag instead of storing "*" as string.
 */
Path* path_wildcard(Pool* pool, Path* base) {
    if (!base) {
        log_error("path_wildcard: NULL base path");
        return NULL;
    }
    return path_append_segment_typed(pool, base, NULL, LPATH_SEG_WILDCARD);
}

/**
 * Create a recursive wildcard segment (**) - matches zero or more path components.
 * Uses LPATH_SEG_WILDCARD_REC flag instead of storing "**" as string.
 */
Path* path_wildcard_recursive(Pool* pool, Path* base) {
    if (!base) {
        log_error("path_wildcard_recursive: NULL base path");
        return NULL;
    }
    return path_append_segment_typed(pool, base, NULL, LPATH_SEG_WILDCARD_REC);
}

/**
 * Check if a path segment is a single wildcard (*).
 * Uses the segment type flag, not string comparison.
 */
bool path_is_wildcard(Path* path) {
    if (!path) return false;
    return PATH_GET_SEG_TYPE(path) == LPATH_SEG_WILDCARD;
}

/**
 * Check if a path segment is a recursive wildcard (**).
 * Uses the segment type flag, not string comparison.
 */
bool path_is_wildcard_recursive(Path* path) {
    if (!path) return false;
    return PATH_GET_SEG_TYPE(path) == LPATH_SEG_WILDCARD_REC;
}

/**
 * Check if a path contains any wildcard segments.
 */
bool path_has_wildcards(Path* path) {
    Path* p = path;
    while (p && p->parent) {
        LPathSegmentType seg_type = PATH_GET_SEG_TYPE(p);
        if (seg_type == LPATH_SEG_WILDCARD || seg_type == LPATH_SEG_WILDCARD_REC) {
            return true;
        }
        p = p->parent;
    }
    return false;
}

// ============================================================================
// Path to string conversion - updated for segment types
// ============================================================================

/**
 * Get segment display name for path_to_string.
 * Returns the segment name, or "*"/"**" for wildcards.
 */
static const char* path_get_segment_display(Path* path) {
    if (!path) return "";
    LPathSegmentType seg_type = PATH_GET_SEG_TYPE(path);
    switch (seg_type) {
        case LPATH_SEG_WILDCARD: return "*";
        case LPATH_SEG_WILDCARD_REC: return "**";
        case LPATH_SEG_DYNAMIC: return "<dynamic>";
        default: return path->name ? path->name : "";
    }
}

// ============================================================================
// Path iteration support - lazy loading for directories and files
// ============================================================================
// This section requires runtime support (heap_calloc, fn_input1, etc.)
// and is only compiled when not building standalone input library
// Use PATH_NO_ITERATION to exclude this section (more specific than LAMBDA_STATIC)

#ifndef PATH_NO_ITERATION

#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#endif

// Extern declaration for datetime_from_unix (defined in datetime.c)
// In C, DateTime is uint64_t (packed bit field), so we declare return type as uint64_t*
extern uint64_t* datetime_from_unix(Pool* pool, int64_t unix_timestamp);

/**
 * Check if the leaf segment of a path is a wildcard (* or **).
 */
bool path_ends_with_wildcard(Path* path) {
    if (!path) return false;
    LPathSegmentType seg_type = PATH_GET_SEG_TYPE(path);
    return seg_type == LPATH_SEG_WILDCARD || seg_type == LPATH_SEG_WILDCARD_REC;
}

/**
 * Load path metadata via stat() without loading content.
 * Sets path->meta and PATH_FLAG_META_LOADED flag.
 */
void path_load_metadata(Path* path) {
    if (!path) return;
    if (path->flags & PATH_FLAG_META_LOADED) return;  // already loaded
    
    if (!context) {
        log_error("path_load_metadata: no context");
        return;
    }
    
    Pool* pool = eval_context_get_pool(context);
    if (!pool) {
        log_error("path_load_metadata: no pool");
        return;
    }
    
    StrBuf* path_buf = strbuf_new();
    path_to_os_path(path, path_buf);
    
    struct stat st;
    if (stat(path_buf->str, &st) == 0) {
        PathMeta* meta = (PathMeta*)pool_calloc(pool, sizeof(PathMeta));
        if (meta) {
            meta->size = st.st_size;
            meta->modified = *datetime_from_unix(pool, (int64_t)st.st_mtime);
            meta->flags = 0;
            if (S_ISDIR(st.st_mode)) meta->flags |= PATH_META_IS_DIR;
#ifndef _WIN32
            struct stat lst;
            if (lstat(path_buf->str, &lst) == 0 && S_ISLNK(lst.st_mode)) {
                meta->flags |= PATH_META_IS_LINK;
            }
#endif
            meta->mode = (st.st_mode >> 6) & 0x07;  // owner permissions only
            path->meta = meta;
        }
    }
    
    path->flags |= PATH_FLAG_META_LOADED;
    strbuf_free(path_buf);
}

// Forward declarations for path resolution
static Item resolve_directory_children(Path* parent_path, const char* dir_path);
static Item resolve_file_content(Path* path, const char* file_path);
static Item expand_wildcard(Path* base_path, const char* dir_path, bool recursive);

// Extern declaration for heap_strcpy (defined in lambda-mem.cpp)
extern String* heap_strcpy(char* src, int len);

/**
 * Resolve path content for iteration.
 * - For directories: returns List of child Path items (with metadata)
 * - For files: returns parsed file content (String, Map, etc.)
 * - For wildcards: expands glob pattern to list of paths
 * - Caches result in path->result
 *
 * Returns:
 * - ITEM_NULL if path doesn't exist (ENOENT)
 * - ITEM_ERROR if path exists but can't be accessed
 * - Content Item on success
 */
Item path_resolve_for_iteration(Path* path) {
    if (!path) return ITEM_NULL;

    // dry-run mode: return empty list for filesystem iteration
    if (g_dry_run) {
        log_debug("dry-run: fabricated path_resolve_for_iteration()");
        return ITEM_NULL;
    }
    
    // Already resolved?
    if (path->result != 0) {
        return path->result;
    }
    
    // Handle sys.* paths via sysinfo module
    PathScheme scheme = path_get_scheme(path);
    if (scheme == PATH_SCHEME_SYS) {
        Item result = sysinfo_resolve_path(path);
        // Only cache if resolution succeeded (non-null, non-error)
        // This allows unresolvable sys paths like sys.config to print as paths
        if (result != ITEM_NULL && result != ITEM_ERROR) {
            path->result = result;
        }
        return result;
    }
    
    // Handle wildcards specially
    if (path_ends_with_wildcard(path)) {
        Path* parent = path->parent;
        if (!parent) {
            log_error("path_resolve_for_iteration: wildcard has no parent");
            return ITEM_ERROR;
        }
        
        StrBuf* path_buf = strbuf_new();
        path_to_os_path(parent, path_buf);
        
        bool recursive = PATH_GET_SEG_TYPE(path) == LPATH_SEG_WILDCARD_REC;
        Item result = expand_wildcard(parent, path_buf->str, recursive);
        
        strbuf_free(path_buf);
        path->result = result;
        return result;
    }
    
    // Convert path to OS path string
    StrBuf* path_buf = strbuf_new();
    path_to_os_path(path, path_buf);
    const char* os_path = path_buf->str;
    
    // Check if directory or file
    struct stat st;
    if (stat(os_path, &st) != 0) {
        int err = errno;
        strbuf_free(path_buf);
        
        // Distinguish between "doesn't exist" and "access error"
        if (err == ENOENT || err == ENOTDIR) {
            log_debug("path_resolve_for_iteration: path does not exist: %s", os_path);
            return ITEM_NULL;
        } else {
            log_error("path_resolve_for_iteration: access error for %s: %s", os_path, strerror(err));
            return ITEM_ERROR;
        }
    }
    
    Item result;
    if (S_ISDIR(st.st_mode)) {
        // Directory: list children as Path items
        result = resolve_directory_children(path, os_path);
    } else {
        // File: load and parse content
        result = resolve_file_content(path, os_path);
    }
    
    strbuf_free(path_buf);
    
    // Cache the result (even if null/error, to avoid re-trying)
    path->result = result;
    return result;
}

/**
 * List directory children as Path items.
 * Each child is a new Path extending the parent.
 * File metadata (size, modified, mode) is loaded, but not file content.
 * Returns empty list [] for empty directories.
 */
static Item resolve_directory_children(Path* parent_path, const char* dir_path) {
    DIR* dir = opendir(dir_path);
    if (!dir) {
        int err = errno;
        if (err == ENOENT || err == ENOTDIR) {
            log_debug("resolve_directory_children: directory does not exist: %s", dir_path);
            return ITEM_NULL;
        } else {
            log_error("resolve_directory_children: access error for %s: %s", dir_path, strerror(err));
            return ITEM_ERROR;
        }
    }
    
    if (!context) {
        closedir(dir);
        log_error("resolve_directory_children: no context");
        return ITEM_ERROR;
    }
    
    Pool* pool = eval_context_get_pool(context);
    if (!pool) {
        closedir(dir);
        log_error("resolve_directory_children: no pool");
        return ITEM_ERROR;
    }
    
    // Create result list (will be empty for empty directories)
    List* children = (List*)heap_calloc(sizeof(List), LMD_TYPE_LIST);
    children->type_id = LMD_TYPE_LIST;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Create child path extending parent
        Path* child_path = path_extend(pool, parent_path, entry->d_name);
        if (!child_path) continue;
        
        // Load metadata for the child (but NOT content)
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        
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
        
        // Add child path to list (cast Path* to uint64_t since Item is uint64_t in C)
        list_push(children, (Item)(uint64_t)child_path);
    }
    
    closedir(dir);
    return (Item)(uint64_t)children;
}

// External declaration for input system
extern Item fn_input1(Item url);

/**
 * Load and parse file content.
 * Auto-detects content type from extension/MIME.
 * Returns parsed structure (String, Map, Element, etc.)
 */
static Item resolve_file_content(Path* path, const char* file_path) {
    // Build URL string for input system
    StrBuf* url_buf = strbuf_new();
    strbuf_append_str(url_buf, "file://");
    strbuf_append_str(url_buf, file_path);
    
    String* url_str = heap_strcpy(url_buf->str, url_buf->length);
    strbuf_free(url_buf);
    
    // Use existing input system to load and parse
    Item content = fn_input1(s2it(url_str));
    
    return content;
}

/**
 * Expand wildcard pattern to list of matching paths.
 * For * : matches files/dirs in the directory
 * For **: recursively matches all files/dirs
 */
static void expand_wildcard_recursive(Path* base, const char* dir_path, 
                                       bool recursive, List* matches,
                                       int depth, int max_depth);

static Item expand_wildcard(Path* base_path, const char* dir_path, bool recursive) {
    if (!context) {
        log_error("expand_wildcard: no context");
        return ITEM_ERROR;
    }
    
    // Create result list
    List* matches = (List*)heap_calloc(sizeof(List), LMD_TYPE_LIST);
    matches->type_id = LMD_TYPE_LIST;
    
    expand_wildcard_recursive(base_path, dir_path, recursive, matches, 0, 16);
    
    return (Item)(uint64_t)matches;
}

static void expand_wildcard_recursive(Path* base, const char* dir_path, 
                                       bool recursive, List* matches,
                                       int depth, int max_depth) {
    if (depth > max_depth) return;
    
    DIR* dir = opendir(dir_path);
    if (!dir) return;
    
    Pool* pool = eval_context_get_pool(context);
    if (!pool) {
        closedir(dir);
        return;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) != 0) continue;
        
        // Create child path
        Path* child = path_extend(pool, base, entry->d_name);
        if (!child) continue;
        
        // Load metadata
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
            child->meta = meta;
            child->flags |= PATH_FLAG_META_LOADED;
        }
        
        // Add to matches (cast Path* to uint64_t since Item is uint64_t in C)
        list_push(matches, (Item)(uint64_t)child);
        
        // Recurse into subdirectories for **
        if (recursive && S_ISDIR(st.st_mode)) {
            expand_wildcard_recursive(child, full_path, true, matches, depth + 1, max_depth);
        }
    }
    
    closedir(dir);
}

// ============================================================================
// fn_exists() - Check if path exists using unified Target API
// ============================================================================

/**
 * Check if a path exists (file or directory).
 * Uses unified Target API for consistent path handling.
 * Accepts: String, Symbol, or Path items.
 * Returns: Bool (BOOL_TRUE/BOOL_FALSE) for direct use in C conditions
 */
Bool fn_exists(Item path_item) {
    if (g_dry_run) {
        log_debug("dry-run: fabricated exists() call");
        return BOOL_FALSE;
    }
    log_debug("fn_exists: ENTERED, path_item=0x%llx", (unsigned long long)path_item);
    
    // Use unified Target API
    Target* target = item_to_target(path_item, NULL);
    if (!target) {
        log_debug("fn_exists: failed to convert item to target");
        return BOOL_FALSE;
    }
    
    bool exists = target_exists(target);
    log_debug("fn_exists: target exists=%d", exists);
    
    target_free(target);
    return exists ? BOOL_TRUE : BOOL_FALSE;
}

#endif // PATH_NO_ITERATION
