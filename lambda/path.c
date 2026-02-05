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

/**
 * Convert path to Lambda path string (e.g., "file.etc.hosts").
 */
void path_to_string(Path* path, void* out_ptr) {
    StrBuf* out = (StrBuf*)out_ptr;
    if (!path || !out) return;

    // Collect segments in reverse order
    const char* segments[64];  // max depth 64
    int count = 0;

    Path* p = path;
    while (p && p->parent && count < 64) {
        segments[count++] = p->name;
        p = p->parent;
    }

    // Output in forward order (root first)
    for (int i = count - 1; i >= 0; i--) {
        if (i < count - 1) {
            strbuf_append_char(out, '.');
        }

        // Check if segment needs quoting (contains dots or special chars)
        const char* seg = segments[i];
        bool needs_quote = false;
        for (const char* c = seg; *c; c++) {
            if (*c == '.' || *c == ' ' || *c == '@' || *c == '#' ||
                *c == '$' || *c == '%' || *c == '&' || *c == '?' ||
                *c == '=' || *c == ':' || *c == '-') {
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
 * Convert path to OS file path (e.g., "/etc/hosts" or "C:\Users\name").
 */
void path_to_os_path(Path* path, void* out_ptr) {
    StrBuf* out = (StrBuf*)out_ptr;
    if (!path || !out) return;

    // Collect segments in reverse order
    const char* segments[64];
    int count = 0;

    Path* p = path;
    while (p && p->parent && count < 64) {
        segments[count++] = p->name;
        p = p->parent;
    }

    // Check scheme
    const char* scheme = (count > 0) ? segments[count - 1] : NULL;
    bool is_file = scheme && strcmp(scheme, "file") == 0;
    bool is_relative = scheme && (strcmp(scheme, ".") == 0 || strcmp(scheme, "..") == 0);

    // Output path
    if (is_file) {
        // Skip "file" scheme, output as absolute path
#ifdef _WIN32
        // Windows: check for drive letter (e.g., file.C.Users)
        if (count > 1) {
            const char* drive = segments[count - 2];
            if (strlen(drive) == 1 && ((drive[0] >= 'A' && drive[0] <= 'Z') ||
                                        (drive[0] >= 'a' && drive[0] <= 'z'))) {
                strbuf_append_char(out, drive[0]);
                strbuf_append_str(out, ":\\");
                for (int i = count - 3; i >= 0; i--) {
                    if (i < count - 3) strbuf_append_char(out, '\\');
                    strbuf_append_str(out, segments[i]);
                }
                return;
            }
        }
#endif
        // Unix-style absolute path
        for (int i = count - 2; i >= 0; i--) {  // skip "file"
            strbuf_append_char(out, '/');
            strbuf_append_str(out, segments[i]);
        }
    } else if (is_relative) {
        // Relative path
        strbuf_append_str(out, scheme);  // "." or ".."
        for (int i = count - 2; i >= 0; i--) {
            strbuf_append_char(out, '/');
            strbuf_append_str(out, segments[i]);
        }
    } else {
        // Other schemes: output as URL
        if (scheme) {
            strbuf_append_str(out, scheme);
            strbuf_append_str(out, "://");
        }
        for (int i = count - 2; i >= 0; i--) {
            if (i < count - 2) strbuf_append_char(out, '/');
            strbuf_append_str(out, segments[i]);
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
 */
static Path* path_append_segment_pool(Pool* pool, Path* parent, const char* segment) {
    if (!parent || !segment) return parent;

    Path* new_path = (Path*)pool_calloc(pool, sizeof(Path));
    new_path->type_id = LMD_TYPE_PATH;
    new_path->flags = 0;
    new_path->ref_cnt = 0;
    new_path->parent = parent;

    // Copy segment name into pool memory
    size_t len = strlen(segment);
    char* name_copy = (char*)pool_alloc(pool, len + 1);
    memcpy(name_copy, segment, len);
    name_copy[len] = '\0';
    new_path->name = name_copy;

    return new_path;
}

// ============================================================================
// New Path API: path_new, path_extend, path_segment, path_wildcard, path_wildcard_recursive
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
 * Extend an existing path with a new segment.
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
    return path_append_segment_pool(pool, base, segment);
}

/**
 * Extend an existing path with another path's segments.
 * Appends all segments from suffix to base.
 * Returns a new path.
 */
Path* path_concat(Pool* pool, Path* base, Path* suffix) {
    if (!base) return suffix;
    if (!suffix) return base;

    // Collect suffix segments in reverse order
    const char* segments[64];
    int count = 0;

    Path* p = suffix;
    while (p && p->parent && count < 64) {
        segments[count++] = p->name;
        p = p->parent;
    }

    // Append segments in forward order (root to leaf)
    Path* result = base;
    for (int i = count - 1; i >= 0; i--) {
        result = path_append_segment_pool(pool, result, segments[i]);
    }

    return result;
}

/**
 * Create a wildcard segment (*) - matches any single path component.
 * The wildcard is stored as a special segment name.
 */
Path* path_wildcard(Pool* pool, Path* base) {
    if (!base) {
        log_error("path_wildcard: NULL base path");
        return NULL;
    }
    return path_append_segment_pool(pool, base, "*");
}

/**
 * Create a recursive wildcard segment (**) - matches zero or more path components.
 * The wildcard is stored as a special segment name.
 */
Path* path_wildcard_recursive(Pool* pool, Path* base) {
    if (!base) {
        log_error("path_wildcard_recursive: NULL base path");
        return NULL;
    }
    return path_append_segment_pool(pool, base, "**");
}

/**
 * Check if a path segment is a single wildcard (*).
 */
bool path_is_wildcard(Path* path) {
    if (!path || !path->name) return false;
    return path->name[0] == '*' && path->name[1] == '\0';
}

/**
 * Check if a path segment is a recursive wildcard (**).
 */
bool path_is_wildcard_recursive(Path* path) {
    if (!path || !path->name) return false;
    return path->name[0] == '*' && path->name[1] == '*' && path->name[2] == '\0';
}

/**
 * Check if a path contains any wildcard segments.
 */
bool path_has_wildcards(Path* path) {
    Path* p = path;
    while (p && p->parent) {
        if (path_is_wildcard(p) || path_is_wildcard_recursive(p)) {
            return true;
        }
        p = p->parent;
    }
    return false;
}

// ============================================================================
// Legacy fixed-arity path_build functions (for backwards compatibility)
// These are still used by the C2MIR transpiler which doesn't support variadic
// ============================================================================

/**
 * Build a path with 1 segment (for transpiled code).
 */
Path* path_build1(Pool* pool, int scheme, const char* s1) {
    Path* path = path_get_root((PathScheme)scheme);
    return path_append_segment_pool(pool, path, s1);
}

/**
 * Build a path with 2 segments (for transpiled code).
 */
Path* path_build2(Pool* pool, int scheme, const char* s1, const char* s2) {
    Path* path = path_get_root((PathScheme)scheme);
    path = path_append_segment_pool(pool, path, s1);
    return path_append_segment_pool(pool, path, s2);
}

/**
 * Build a path with 3 segments (for transpiled code).
 */
Path* path_build3(Pool* pool, int scheme, const char* s1, const char* s2, const char* s3) {
    Path* path = path_get_root((PathScheme)scheme);
    path = path_append_segment_pool(pool, path, s1);
    path = path_append_segment_pool(pool, path, s2);
    return path_append_segment_pool(pool, path, s3);
}

/**
 * Build a path with 4 segments (for transpiled code).
 */
Path* path_build4(Pool* pool, int scheme, const char* s1, const char* s2, const char* s3, const char* s4) {
    Path* path = path_get_root((PathScheme)scheme);
    path = path_append_segment_pool(pool, path, s1);
    path = path_append_segment_pool(pool, path, s2);
    path = path_append_segment_pool(pool, path, s3);
    return path_append_segment_pool(pool, path, s4);
}

/**
 * Build a path with 5 segments (for transpiled code).
 */
Path* path_build5(Pool* pool, int scheme, const char* s1, const char* s2, const char* s3, const char* s4, const char* s5) {
    Path* path = path_get_root((PathScheme)scheme);
    path = path_append_segment_pool(pool, path, s1);
    path = path_append_segment_pool(pool, path, s2);
    path = path_append_segment_pool(pool, path, s3);
    path = path_append_segment_pool(pool, path, s4);
    return path_append_segment_pool(pool, path, s5);
}

/**
 * Build a path with 6 segments (for transpiled code).
 */
Path* path_build6(Pool* pool, int scheme, const char* s1, const char* s2, const char* s3, const char* s4, const char* s5, const char* s6) {
    Path* path = path_get_root((PathScheme)scheme);
    path = path_append_segment_pool(pool, path, s1);
    path = path_append_segment_pool(pool, path, s2);
    path = path_append_segment_pool(pool, path, s3);
    path = path_append_segment_pool(pool, path, s4);
    path = path_append_segment_pool(pool, path, s5);
    return path_append_segment_pool(pool, path, s6);
}

/**
 * Build a path with 7 segments (for transpiled code).
 */
Path* path_build7(Pool* pool, int scheme, const char* s1, const char* s2, const char* s3, const char* s4, const char* s5, const char* s6, const char* s7) {
    Path* path = path_get_root((PathScheme)scheme);
    path = path_append_segment_pool(pool, path, s1);
    path = path_append_segment_pool(pool, path, s2);
    path = path_append_segment_pool(pool, path, s3);
    path = path_append_segment_pool(pool, path, s4);
    path = path_append_segment_pool(pool, path, s5);
    path = path_append_segment_pool(pool, path, s6);
    return path_append_segment_pool(pool, path, s7);
}

/**
 * Build a path with 8 segments (for transpiled code).
 */
Path* path_build8(Pool* pool, int scheme, const char* s1, const char* s2, const char* s3, const char* s4, const char* s5, const char* s6, const char* s7, const char* s8) {
    Path* path = path_get_root((PathScheme)scheme);
    path = path_append_segment_pool(pool, path, s1);
    path = path_append_segment_pool(pool, path, s2);
    path = path_append_segment_pool(pool, path, s3);
    path = path_append_segment_pool(pool, path, s4);
    path = path_append_segment_pool(pool, path, s5);
    path = path_append_segment_pool(pool, path, s6);
    path = path_append_segment_pool(pool, path, s7);
    return path_append_segment_pool(pool, path, s8);
}
