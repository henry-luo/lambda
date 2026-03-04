#pragma once
// Path and PathMeta full struct definitions + path enums + macros + Path API.
// Separated from lambda.h to keep the JIT-embedded header slim.
// Include this from path.c (C) and lambda.hpp (C++).
//
// Requires lambda.h to be included first (for TypeId, DateTime, Pool, Item, Bool, Path/PathMeta forward decls).

// Path metadata structure (optional, allocated on demand)
// Stores file/directory metadata without loading content
struct PathMeta {
    int64_t size;           // file size in bytes (-1 for dirs or unknown)
    DateTime modified;      // last modification time
    uint8_t flags;          // is_dir (bit 0), is_link (bit 1)
    uint8_t mode;           // Unix permissions (compressed to 8 bits: rwx for owner)
};

// Path metadata flags
#define PATH_META_IS_DIR   0x01
#define PATH_META_IS_LINK  0x02

// Path segment type flags (stored in flags field)
// These distinguish between regular segments, wildcards, and dynamic segments
// Note: Named with LPATH_ prefix to avoid conflict with validator's PathSegmentType
typedef enum {
    LPATH_SEG_NORMAL = 0,        // regular segment (literal string)
    LPATH_SEG_WILDCARD = 1,      // single wildcard (*) - match one segment
    LPATH_SEG_WILDCARD_REC = 2,  // recursive wildcard (**) - match zero or more segments
    LPATH_SEG_DYNAMIC = 3,       // dynamic segment (runtime-computed, name is NULL until resolved)
} LPathSegmentType;

// Path flags (bits 0-1 for segment type, bit 7 for metadata loaded)
#define PATH_FLAG_META_LOADED  0x80  // bit 7: metadata has been stat'd and loaded

struct Path {
    TypeId type_id;         // LMD_TYPE_PATH
    uint8_t flags;          // segment type (bits 0-1), metadata loaded (bit 7)
    const char* name;       // segment name (interned via name_pool), NULL for wildcards
    Path* parent;           // parent segment (NULL for root schemes)
    uint64_t result;        // cached resolved content (0 = not resolved yet)
    PathMeta* meta;         // optional metadata (NULL until stat'd)
};

// Helper macros for path segment type
#define PATH_GET_SEG_TYPE(p)      ((LPathSegmentType)((p)->flags & 0x03))
#define PATH_SET_SEG_TYPE(p, t)   ((p)->flags = ((p)->flags & 0xFC) | ((t) & 0x03))

// Path scheme identifiers (predefined roots)
typedef enum {
    PATH_SCHEME_FILE = 0,   // file://
    PATH_SCHEME_HTTP,       // http://
    PATH_SCHEME_HTTPS,      // https://
    PATH_SCHEME_SYS,        // sys:// (system info)
    PATH_SCHEME_REL,        // . (relative path)
    PATH_SCHEME_PARENT,     // .. (parent directory)
    PATH_SCHEME_COUNT
} PathScheme;

// Path API (defined in path.c)
void path_init(void);                                   // Initialize root scheme paths
Path* path_get_root(PathScheme scheme);                 // Get predefined root path
Path* path_append(Path* parent, const char* segment);   // Append segment to path
Path* path_append_len(Path* parent, const char* segment, size_t len);
const char* path_get_scheme_name(Path* path);           // Get scheme name (file, http, etc.)
PathScheme path_get_scheme(Path* path);                 // Get scheme type (PATH_SCHEME_FILE, etc.)
bool path_is_root(Path* path);                          // Check if path is a root scheme
bool path_is_absolute(Path* path);                      // Check if path is absolute (not . or ..)
int path_depth(Path* path);                             // Get path depth (segment count)
void path_to_string(Path* path, void* out);             // Convert to string (StrBuf*)
void path_to_os_path(Path* path, void* out);            // Convert to OS path (StrBuf*)

// Wildcard query functions
bool path_is_wildcard(Path* path);                                // Check if segment is *
bool path_is_wildcard_recursive(Path* path);                      // Check if segment is **
bool path_has_wildcards(Path* path);                              // Check if path has any wildcards

// Path content loading (lazy evaluation support)
Item path_load_content(Path* path);                               // Load path content (file/URL)
int64_t path_get_length(Path* path);                              // Get path content length (triggers load)
Item path_get_item(Path* path, int64_t index);                    // Get item at index (triggers load)

// Path resolution for iteration (returns list for dirs, content for files)
Item path_resolve_for_iteration(Path* path);                      // Resolve path for iteration
bool path_ends_with_wildcard(Path* path);                         // Check if leaf is * or **
void path_load_metadata(Path* path);                              // Load metadata via stat()
