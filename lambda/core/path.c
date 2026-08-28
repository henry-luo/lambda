/**
 * path.c - Lambda Path implementation
 *
 * Paths are segmented symbols for file/URL navigation.
 * A path is a linked chain of segments from leaf to root.
 *
 * Example: file.etc.hosts
 *   Path("hosts") -> Path("etc") -> Path("file") -> ROOT_SENTINEL
 */

#include "../../lib/strbuf.h"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include "../../lib/arraylist.h"
#include "../../lib/hashmap.h"
#include "../../lib/shell.h"
#include "../../lib/mem.h"
#include "../lambda.h"
#include "lambda-path.h"
#include "../runtime/sysinfo.h"

#include <string.h>
#include <stdlib.h>

// Target API functions used by fn_exists() (defined in target.cpp, declarations in lambda.hpp)
extern Target* item_to_target(uint64_t item, Url* cwd);
extern bool target_exists(Target* target);
extern void target_free(Target* target);

#ifndef LAMBDA_PATH_RUNTIME_IMPLEMENTATION

static PathPoolProvider path_pool_provider = NULL;

void path_register_pool_provider(PathPoolProvider provider) {
    // Keep runtime TLS ownership above lambda-io; allocation always asks the
    // registered current-runtime provider rather than retaining a stale pool.
    path_pool_provider = provider;
}

Pool* path_get_pool(void) {
    return path_pool_provider ? path_pool_provider() : NULL;
}

// Root sentinel - parent of all scheme roots (has NULL parent itself)
static Path ROOT_SENTINEL = {
    LMD_TYPE_PATH,  // type_id
    0,              // flags
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
    "..",     // PATH_SCHEME_PARENT (legacy only)
    "/"       // PATH_SCHEME_LOGICAL
};

static Path* path_root_of(Path* path) {
    if (!path) return NULL;
    while (path->parent && path->parent != &ROOT_SENTINEL) {
        path = path->parent;
    }
    return path;
}

static bool path_is_child(Path* path) {
    return path && path->parent && path->parent != &ROOT_SENTINEL;
}

static Path* path_alloc_op(Pool* pool, Path* base, LPathSegmentType type,
                           const char* name, size_t len, int64_t int_value) {
    if (!pool || !base) return NULL;
    Path* path = (Path*)pool_calloc(pool, sizeof(Path));
    if (!path) return NULL;
    path->type_id = LMD_TYPE_PATH;
    path->flags = 0;
    PATH_SET_SEG_TYPE(path, type);
    path->parent = base;
    path->root_scheme = base->root_scheme;
    path->authority_kind = base->authority_kind;
    path->authority_name = base->authority_name;
    path->int_value = int_value;
    if (name && len > 0 && type == LPATH_SEG_NORMAL) {
        char* name_copy = (char*)pool_alloc(pool, len + 1);
        memcpy(name_copy, name, len);
        name_copy[len] = '\0';
        path->name = name_copy;
    }
    return path;
}

/**
 * Initialize root scheme paths.
 * Call this once at runtime startup.
 */
void path_init(void) {
    if (scheme_roots[0] != NULL) {
        return;  // already initialized
    }

    Pool* pool = path_get_pool();
    if (!pool) {
        log_error("path_init: no registered pool provider");
        return;
    }

    for (int i = 0; i < PATH_SCHEME_COUNT; i++) {
        Path* root = (Path*)pool_calloc(pool, sizeof(Path));
        root->type_id = LMD_TYPE_PATH;
        root->flags = 0;
        root->name = scheme_names[i];  // static strings, no need to intern
        root->parent = &ROOT_SENTINEL;
        root->root_scheme = (uint8_t)i;
        root->authority_kind = (i == PATH_SCHEME_FILE) ? PATH_AUTHORITY_LOCAL : PATH_AUTHORITY_NONE;
        scheme_roots[i] = root;
    }

    log_debug("path_init: initialized %d scheme roots", PATH_SCHEME_COUNT);
}

/**
 * Reset path scheme roots so path_init() re-runs on next use.
 * Must be called between scripts in batch mode since scheme_roots are
 * allocated from the script's pool, which is destroyed between scripts.
 */
void path_reset(void) {
    for (int i = 0; i < PATH_SCHEME_COUNT; i++) {
        scheme_roots[i] = NULL;
    }
    log_debug("path_reset: cleared %d scheme roots", PATH_SCHEME_COUNT);
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

    Pool* pool = path_get_pool();
    if (!pool) {
        log_error("path_append_len: no registered pool provider");
        return NULL;
    }

    return path_alloc_op(pool, parent, LPATH_SEG_NORMAL, segment, len, 0);
}

Path* path_new_authority(Pool* pool, int scheme, const char* authority) {
    if (!pool || !authority || !*authority) return NULL;
    if (scheme < 0 || scheme >= PATH_SCHEME_COUNT) return NULL;
    Path* root = path_new(pool, scheme);
    if (!root) return NULL;
    root->authority_kind = PATH_AUTHORITY_NAMED;
    size_t len = strlen(authority);
    char* copy = (char*)pool_alloc(pool, len + 1);
    memcpy(copy, authority, len);
    copy[len] = '\0';
    root->authority_name = copy;
    return root;
}

/**
 * Get the scheme name for a path (file, http, https, sys, etc.)
 */
const char* path_get_scheme_name(Path* path) {
    if (!path) return NULL;
    Path* root = path_root_of(path);
    if (!root) return NULL;
    if ((PathScheme)root->root_scheme == PATH_SCHEME_LOGICAL) return "/";
    return root->name;
}

/**
 * Check if path is a root scheme (no segments after scheme).
 */
bool path_is_root(Path* path) {
    if (!path) return false;
    return path->parent == &ROOT_SENTINEL || PATH_GET_SEG_TYPE(path) == LPATH_SEG_ROOT;
}

/**
 * Get the scheme type of a path.
 * Walks to root and returns the scheme identifier.
 * Returns -1 if path is invalid.
 */
PathScheme path_get_scheme(Path* path) {
    if (!path) return (PathScheme)-1;
    return (PathScheme)path_root_of(path)->root_scheme;
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
           scheme == PATH_SCHEME_SYS ||
           scheme == PATH_SCHEME_LOGICAL;
}

/**
 * Get the depth of a path (number of segments including scheme).
 */
int path_depth(Path* path) {
    int depth = 0;
    while (path && path->parent && path->parent != &ROOT_SENTINEL) {
        depth++;
        path = path->parent;
    }
    if (path && path->parent == &ROOT_SENTINEL) {
        depth++;  // count the scheme root itself
    }
    return depth;
}

static void path_print_name(StrBuf* out, const char* seg) {
    const char* text = seg ? seg : "";
    bool needs_quote = *text == '\0';
    bool all_digits = *text != '\0';
    for (const char* c = text; *c; c++) {
        if (*c < '0' || *c > '9') all_digits = false;
        if (*c == '.' || *c == ' ' || *c == '@' || *c == '#' ||
            *c == '$' || *c == '%' || *c == '&' || *c == '?' ||
            *c == '=' || *c == ':' || *c == '-' || *c == '*') {
            needs_quote = true;
            break;
        }
    }
    // Numeric-looking names must stay quoted so NameKey("1") cannot
    // canonicalize into the distinct IntKey(1) spelling.
    if (all_digits) needs_quote = true;
    if (needs_quote) strbuf_append_char(out, '\'');
    strbuf_append_str(out, text);
    if (needs_quote) strbuf_append_char(out, '\'');
}

static void path_print_op(StrBuf* out, Path* op, bool separator) {
    if (separator) strbuf_append_char(out, '.');
    switch (PATH_GET_SEG_TYPE(op)) {
        case LPATH_SEG_WILDCARD: strbuf_append_char(out, '*'); break;
        case LPATH_SEG_WILDCARD_REC: strbuf_append_str(out, "**"); break;
        case LPATH_SEG_DYNAMIC: strbuf_append_str(out, "<dynamic>"); break;
        case LPATH_SEG_PARENT: strbuf_append_str(out, "~~"); break;
        case LPATH_SEG_ROOT: strbuf_append_char(out, '/'); break;
        case LPATH_SEG_INT: strbuf_append_int64(out, op->int_value); break;
        default: path_print_name(out, op->name); break;
    }
}

/** Convert a path to its canonical Lambda spelling. */
void path_to_string(Path* path, void* out_ptr) {
    StrBuf* out = (StrBuf*)out_ptr;
    if (!path || !out) return;

    Path* root = path_root_of(path);
    ArrayList* ops = arraylist_new(8);
    for (Path* p = path; p && p != root; p = p->parent) {
        arraylist_append(ops, p);
    }

    PathScheme scheme = (PathScheme)root->root_scheme;
    if (scheme == PATH_SCHEME_LOGICAL) {
        strbuf_append_char(out, '/');
    } else if (scheme == PATH_SCHEME_REL) {
        // S16.9.4: a relative path is spelled `\.a.b`. The escape is load-bearing,
        // not cosmetic — printed bare, `.rect` re-parses as part of a qualified
        // name, so `<svg \.rect>` would silently come back as tag `svg.rect`.
        // OS/filesystem rendering is unaffected: path_to_os_path builds its own
        // `./` form and never routes through here.
        strbuf_append_str(out, "\\.");
    } else if (scheme == PATH_SCHEME_FILE) {
        strbuf_append_str(out, "file.");
        if (root->authority_kind == PATH_AUTHORITY_NAMED) {
            // Quote hostnames that are not one identifier token so a dotted or
            // hyphenated authority cannot be reparsed as path child keys.
            path_print_name(out, root->authority_name);
        } else {
            strbuf_append_char(out, '/');
        }
    } else if (scheme == PATH_SCHEME_PARENT) {
        // same escape as PATH_SCHEME_REL above: bare `.~~` no longer parses.
        strbuf_append_str(out, "\\.~~");
    } else {
        strbuf_append_str(out, scheme_names[scheme]);
    }

    for (int i = ops->length - 1; i >= 0; i--) {
        bool separator = !(scheme == PATH_SCHEME_REL && i == ops->length - 1);
        path_print_op(out, (Path*)arraylist_get(ops, i), separator);
    }
    arraylist_free(ops);
}

bool path_equal(Path* left, Path* right) {
    if (left == right) return true;
    if (!left || !right) return false;
    StrBuf* left_buf = strbuf_new();
    StrBuf* right_buf = strbuf_new();
    path_to_string(left, left_buf);
    path_to_string(right, right_buf);
    bool equal = left_buf->length == right_buf->length &&
        memcmp(left_buf->str, right_buf->str, left_buf->length) == 0;
    strbuf_free(left_buf);
    strbuf_free(right_buf);
    return equal;
}

uint64_t path_hash(Path* path, uint64_t seed0, uint64_t seed1) {
    if (!path) return hashmap_sip("path:null", 9, seed0, seed1);
    StrBuf* buf = strbuf_new();
    path_to_string(path, buf);
    uint64_t hash = hashmap_sip(buf->str, buf->length, seed0, seed1);
    strbuf_free(buf);
    return hash;
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

static void path_append_os_component(StrBuf* out, Path* op) {
    LPathSegmentType type = PATH_GET_SEG_TYPE(op);
    if (type == LPATH_SEG_INT) {
        strbuf_append_int64(out, op->int_value);
    } else {
        strbuf_append_str(out, path_get_os_segment_name(op));
    }
}

/**
 * Convert path to OS file path (e.g., "/etc/hosts" or "C:\Users\name").
 * Handles wildcards properly using segment type flags.
 */
void path_to_os_path(Path* path, void* out_ptr) {
    StrBuf* out = (StrBuf*)out_ptr;
    if (!path || !out) return;
    if (path_get_scheme(path) == PATH_SCHEME_FILE &&
            !path_file_authority_is_local(path)) {
        log_error("path_to_os_path: remote file authority is not supported");
        return;
    }
    if (path_get_scheme(path) == PATH_SCHEME_LOGICAL) {
        log_error("path_to_os_path: logical root must be qualified first");
        return;
    }

    Path* root = path_root_of(path);
    ArrayList* ops = arraylist_new(8);
    for (Path* p = path; p && p != root; p = p->parent) {
        arraylist_append(ops, p);
    }
    PathScheme scheme = (PathScheme)root->root_scheme;
    bool skip_drive_component = false;

    if (scheme == PATH_SCHEME_REL) {
        strbuf_append_str(out, "./");
    } else if (scheme == PATH_SCHEME_PARENT) {
        strbuf_append_str(out, "../");
    } else if (scheme == PATH_SCHEME_FILE || scheme == PATH_SCHEME_LOGICAL) {
#ifdef _WIN32
        if (ops->length > 0) {
            Path* first = (Path*)arraylist_get(ops, ops->length - 1);
            const char* drive = path_get_os_segment_name(first);
            if (strlen(drive) == 1 && ((drive[0] >= 'A' && drive[0] <= 'Z') ||
                                        (drive[0] >= 'a' && drive[0] <= 'z'))) {
                strbuf_append_char(out, drive[0]);
                strbuf_append_str(out, ":\\");
                skip_drive_component = true;
            } else {
                strbuf_append_char(out, '\\');
            }
        } else {
            strbuf_append_char(out, '\\');
        }
#else
        strbuf_append_char(out, '/');
#endif
    } else {
        strbuf_append_str(out, scheme_names[scheme]);
        strbuf_append_str(out, "://");
    }

    for (int i = ops->length - 1; i >= 0; i--) {
        if (skip_drive_component && i == ops->length - 1) continue;
        Path* op = (Path*)arraylist_get(ops, i);
        LPathSegmentType type = PATH_GET_SEG_TYPE(op);
        if (type == LPATH_SEG_ROOT) continue;
        if (type == LPATH_SEG_PARENT) {
            strbuf_append_str(out, "../");
            continue;
        }
        if (out->length > 0) {
            char last = out->str[out->length - 1];
            if (last != '/' && last != '\\') strbuf_append_char(out, '/');
        }
        path_append_os_component(out, op);
    }
    arraylist_free(ops);
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
    if (!pool) {
        // Every new segment must use its caller's owning context pool; a
        // missing owner used to dereference NULL after a batch context reset.
        log_error("path_append_segment_typed: NULL pool");
        return NULL;
    }

    return path_alloc_op(pool, parent, seg_type, segment,
                         segment ? strlen(segment) : 0, 0);
}

// ============================================================================
// New Path API: path_new, path_extend, path_wildcard, path_wildcard_recursive
// ============================================================================

/**
 * Create a new path starting with the given scheme.
 * This returns the root path for the scheme.
 */
Path* path_new(Pool* pool, int scheme) {
    if (!pool) {
        log_error("path_new: NULL pool");
        return NULL;
    }
    if (scheme < 0 || scheme >= PATH_SCHEME_COUNT) {
        log_error("path_new: invalid scheme %d", scheme);
        return NULL;
    }
    // Compiled paths receive the owning pool explicitly.  Do not route them
    // through cached process-global roots, which may belong to a prior batch.
    Path* root = (Path*)pool_calloc(pool, sizeof(Path));
    if (!root) return NULL;
    root->type_id = LMD_TYPE_PATH;
    root->name = scheme_names[scheme];
    root->parent = &ROOT_SENTINEL;
    root->root_scheme = (uint8_t)scheme;
    root->authority_kind = (scheme == PATH_SCHEME_FILE) ? PATH_AUTHORITY_LOCAL : PATH_AUTHORITY_NONE;
    return root;
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

Path* path_extend_int(Pool* pool, Path* base, int64_t value) {
    if (!base || value < 0) return base;
    return path_alloc_op(pool, base, LPATH_SEG_INT, NULL, 0, value);
}

Path* path_select_parent(Pool* pool, Path* base) {
    if (!base) return NULL;
    PathScheme scheme = path_get_scheme(base);
    LPathSegmentType type = PATH_GET_SEG_TYPE(base);

    if (type == LPATH_SEG_PARENT && scheme == PATH_SCHEME_REL) {
        return path_alloc_op(pool, base, LPATH_SEG_PARENT, NULL, 0, 0);
    }
    if (path_is_child(base)) return base->parent;
    if (scheme == PATH_SCHEME_REL) {
        return path_alloc_op(pool, base, LPATH_SEG_PARENT, NULL, 0, 0);
    }
    // Anchored roots clamp at their anchor. This is the closed path algebra
    // required by S2.4.2v3; ordinary values use the dynamic navigation path.
    return base;
}

Path* path_select_root(Pool* pool, Path* base) {
    if (!base) return NULL;
    Path* root = path_root_of(base);
    if (path_get_scheme(base) != PATH_SCHEME_REL) return root;
    if (PATH_GET_SEG_TYPE(base) == LPATH_SEG_ROOT && base->parent == root) return base;
    return path_alloc_op(pool, root, LPATH_SEG_ROOT, NULL, 0, 0);
}

bool path_file_authority_is_local(Path* path) {
    if (!path || path_get_scheme(path) != PATH_SCHEME_FILE) return false;
    Path* root = path_root_of(path);
    if (root->authority_kind != PATH_AUTHORITY_NAMED) return true;
    char* current = shell_get_hostname();
    bool local = current && root->authority_name && strcmp(current, root->authority_name) == 0;
    if (current) mem_free(current);
    return local;
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

    ArrayList* ops = arraylist_new(8);
    Path* suffix_root = path_root_of(suffix);
    for (Path* p = suffix; p && p != suffix_root; p = p->parent) {
        arraylist_append(ops, p);
    }
    Path* result = base;
    for (int i = ops->length - 1; i >= 0; i--) {
        Path* op = (Path*)arraylist_get(ops, i);
        LPathSegmentType type = PATH_GET_SEG_TYPE(op);
        if (type == LPATH_SEG_PARENT) result = path_select_parent(pool, result);
        else if (type == LPATH_SEG_ROOT) result = path_select_root(pool, result);
        else if (type == LPATH_SEG_INT) result = path_extend_int(pool, result, op->int_value);
        else result = path_append_segment_typed(pool, result, op->name, type);
    }
    arraylist_free(ops);
    return result;
}

// Qualify the logical resolver root through the default local-file mount.
// Qualification returns a new spine and leaves the source reference intact.
Path* path_qualify_default(Pool* pool, Path* path) {
    if (!pool || !path) return path;
    if (path_get_scheme(path) != PATH_SCHEME_LOGICAL) return path;
    return path_concat(pool, path_new(pool, PATH_SCHEME_FILE), path);
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

// ============================================================================
// Path iteration support - lazy loading for directories and files
// ============================================================================
// This section requires runtime support (heap_calloc, fn_input1, etc.)
// and is only compiled when not building standalone input library
// Use PATH_NO_ITERATION to exclude this section (more specific than LAMBDA_STATIC)

#endif // LAMBDA_PATH_RUNTIME_IMPLEMENTATION

#ifdef LAMBDA_PATH_RUNTIME_IMPLEMENTATION
extern Pool* path_get_pool(void);
#endif

#ifndef PATH_NO_ITERATION

#include "../../lib/file.h"
#include "../../lib/file_utils.h"
#include "../../lib/arraylist.h"
#include "../../lib/url.h"
#include "../../lib/mem.h"

// Extern declaration for datetime_from_unix (defined in datetime.c)
// In C, DateTime is uint64_t (packed bit field), so we declare return type as uint64_t*
extern uint64_t* datetime_from_unix(Pool* pool, int64_t unix_timestamp);

static void path_apply_stat_metadata(Path* path, Pool* pool, FileStat fs);

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
    
    Pool* pool = path_get_pool();
    if (!pool) {
        log_error("path_load_metadata: no registered pool provider");
        return;
    }
    
    StrBuf* path_buf = strbuf_new();
    path_to_os_path(path_qualify_default(pool, path), path_buf);
    
    FileStat fs = file_stat(path_buf->str);
    path_apply_stat_metadata(path, pool, fs);
    
    path->flags |= PATH_FLAG_META_LOADED;
    strbuf_free(path_buf);
}

// Forward declarations for path resolution
static Item resolve_directory_children(Path* parent_path, const char* dir_path);
static Item resolve_file_content(Path* path, const char* file_path);
static Item expand_wildcard(Path* base_path, const char* dir_path, bool recursive);

static void path_apply_stat_metadata(Path* path, Pool* pool, FileStat fs) {
    if (!path || !pool || !fs.exists) return;
    PathMeta* meta = (PathMeta*)pool_calloc(pool, sizeof(PathMeta));
    if (!meta) return;
    meta->size = fs.size;
    meta->modified = *datetime_from_unix(pool, (int64_t)fs.modified);
    meta->flags = 0;
    if (fs.is_dir) meta->flags |= PATH_META_IS_DIR;
    if (fs.is_symlink) meta->flags |= PATH_META_IS_LINK;
    meta->mode = (fs.mode >> 6) & 0x07;
    path->meta = meta;
    path->flags |= PATH_FLAG_META_LOADED;
}

// Extern declaration for heap_strcpy (defined in lambda-mem.cpp)
extern String* heap_strcpy(const char* src, int64_t len);

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
        path_to_os_path(path_qualify_default(path_get_pool(), parent), path_buf);
        
        bool recursive = PATH_GET_SEG_TYPE(path) == LPATH_SEG_WILDCARD_REC;
        Item result = expand_wildcard(parent, path_buf->str, recursive);
        
        strbuf_free(path_buf);
        path->result = result;
        return result;
    }
    
    // Convert path to OS path string
    StrBuf* path_buf = strbuf_new();
    path_to_os_path(path_qualify_default(path_get_pool(), path), path_buf);
    const char* os_path = path_buf->str;
    
    // Check if directory or file
    FileStat fs = file_stat(os_path);
    if (!fs.exists) {
        strbuf_free(path_buf);
        log_debug("path_resolve_for_iteration: path does not exist: %s", os_path);
        return ITEM_NULL;
    }
    
    Item result;
    if (fs.is_dir) {
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
    ArrayList* entries = dir_list(dir_path);
    if (!entries) {
        log_debug("resolve_directory_children: directory does not exist or not accessible: %s", dir_path);
        return ITEM_NULL;
    }
    
    Pool* pool = path_get_pool();
    if (!pool) {
        for (int i = 0; i < entries->length; i++) dir_entry_free((DirEntry*)entries->data[i]);
        arraylist_free(entries);
        log_error("resolve_directory_children: no registered pool provider");
        return ITEM_ERROR;
    }
    
    // Create result list (will be empty for empty directories)
    List* children = (List*)heap_calloc(sizeof(List), LMD_TYPE_ARRAY);
    children->type_id = LMD_TYPE_ARRAY;
    
    for (int i = 0; i < entries->length; i++) {
        DirEntry* entry = (DirEntry*)entries->data[i];
        
        // Create child path extending parent
        Path* child_path = path_extend(pool, parent_path, entry->name);
        if (!child_path) {
            dir_entry_free(entry);
            continue;
        }
        
        // Load metadata for the child (but NOT content)
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->name);
        
        FileStat fs = file_stat(full_path);
        path_apply_stat_metadata(child_path, pool, fs);
        
        // Add child path to list (cast Path* to uint64_t since Item is uint64_t in C)
        list_push(children, (Item)(uint64_t)child_path);
        dir_entry_free(entry);
    }
    
    arraylist_free(entries);
    return (Item)(uint64_t)children;
}

// External declaration for input system
extern RetItem fn_input1(Item url);

/**
 * Load and parse file content.
 * Auto-detects content type from extension/MIME.
 * Returns parsed structure (String, Map, Element, etc.)
 */
static Item resolve_file_content(Path* path, const char* file_path) {
    // Build file:// URL string for input system (percent-encodes, cross-platform)
    char* file_url = url_from_local_path(file_path);
    if (!file_url) return ITEM_ERROR;
    String* url_str = heap_strcpy(file_url, strlen(file_url));
    mem_free(file_url);

    // Use existing input system to load and parse
    RetItem content_ri = fn_input1(s2it(url_str));
    
    return ri_to_item(content_ri);
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
    if (!path_get_pool()) {
        log_error("expand_wildcard: no registered pool provider");
        return ITEM_ERROR;
    }
    
    // Create result list
    List* matches = (List*)heap_calloc(sizeof(List), LMD_TYPE_ARRAY);
    matches->type_id = LMD_TYPE_ARRAY;
    
    expand_wildcard_recursive(base_path, dir_path, recursive, matches, 0, 16);
    
    return (Item)(uint64_t)matches;
}

static void expand_wildcard_recursive(Path* base, const char* dir_path, 
                                       bool recursive, List* matches,
                                       int depth, int max_depth) {
    if (depth > max_depth) return;
    
    ArrayList* entries = dir_list(dir_path);
    if (!entries) return;
    
    Pool* pool = path_get_pool();
    if (!pool) {
        for (int i = 0; i < entries->length; i++) dir_entry_free((DirEntry*)entries->data[i]);
        arraylist_free(entries);
        return;
    }
    
    for (int i = 0; i < entries->length; i++) {
        DirEntry* entry = (DirEntry*)entries->data[i];
        
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->name);
        
        FileStat fs = file_stat(full_path);
        if (!fs.exists) {
            dir_entry_free(entry);
            continue;
        }
        
        // Create child path
        Path* child = path_extend(pool, base, entry->name);
        if (!child) {
            dir_entry_free(entry);
            continue;
        }
        
        path_apply_stat_metadata(child, pool, fs);
        
        // Add to matches (cast Path* to uint64_t since Item is uint64_t in C)
        list_push(matches, (Item)(uint64_t)child);
        
        // Recurse into subdirectories for **
        if (recursive && fs.is_dir) {
            expand_wildcard_recursive(child, full_path, true, matches, depth + 1, max_depth);
        }

        dir_entry_free(entry);
    }
    
    arraylist_free(entries);
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
