// Runtime filesystem path providers. Address representation stays in core;
// file metadata, listing and content acquisition use the neutral I/O client.
#include <stdbool.h>
#include "lambda/lambda.h"
#include "lambda/core/lambda-path.h"
#include "lambda/io/security_io.h"
#include "lambda/io/target.h"
#include "side_stack.h"
#include "lambda-error.h"
#include "security_context.h"
#include "sysinfo.h"
#include "lib/strbuf.h"
#include "lib/log.h"
#include "lib/mem.h"
#include "lib/shell.h"
#include <errno.h>
#include <string.h>

extern PathEnumerationResolver g_path_enumeration_resolver;
extern void* eval_context_tls_runtime(void);

#ifndef PATH_NO_ITERATION
static bool path_provider_is_local(Path* path) {
    PathScheme scheme = path_get_scheme(path);
    return scheme == PATH_SCHEME_FILE || scheme == PATH_SCHEME_REL ||
        scheme == PATH_SCHEME_PARENT || scheme == PATH_SCHEME_LOGICAL;
}

// A query step anywhere, or a wildcard with further steps, is enumerated by
// the runtime's force walk (S2.4.6, S2.4.7); a leaf wildcard stays here.
static bool path_needs_runtime_enumeration(Path* path) {
    for (Path* p = path; p && p->parent; p = p->parent) {
        LPathSegmentType type = PATH_GET_SEG_TYPE(p);
        if (type == LPATH_SEG_QUERY) return true;
        if ((type == LPATH_SEG_WILDCARD || type == LPATH_SEG_WILDCARD_REC) && p != path) return true;
    }
    return false;
}


#include "lib/file.h"
#include "lib/file_utils.h"
#include "lib/arraylist.h"
#include "lib/url.h"
#include "lib/mem.h"

/**
 * Check if the leaf segment of a path is a wildcard (* or **).
 */
bool path_ends_with_wildcard(Path* path) {
    if (!path) return false;
    LPathSegmentType seg_type = PATH_GET_SEG_TYPE(path);
    return seg_type == LPATH_SEG_WILDCARD || seg_type == LPATH_SEG_WILDCARD_REC;
}

// PTH34: the force step resolves the LONGEST PREFIX that names a document
// through its provider and navigates the rest in memory, so
// `server.dir.file.node1` needs to know where the document stops and the
// in-document navigation starts. Only a store can answer that, which is why
// this lives beside the OS-path conversion rather than in the evaluator.
//
// Returns the deepest ancestor (`path` included) that exists in the store, or
// NULL when no prefix does. A scheme with no cheap existence probe (http) has
// PTH81: whether a wildcard-free path names an existing local directory, the
// one domain a wildcard step enumerates without reading a file.
bool path_is_directory(Path* path) {
    if (!path) return false;
    if (!path_provider_is_local(path)) return false;
#ifdef LAMBDA_NO_FILE_IO
    return false;
#else
    Pool* pool = path_get_pool();
    if (!pool) return false;
    StrBuf* buf = strbuf_new();
    path_to_os_path(path_qualify_default(pool, path), buf);
    int32_t kind = -1;
    bool acquired = security_io_input_kind(lambda_current_security_io(), buf->str, &kind, NULL);
    strbuf_free(buf);
    return acquired && kind == SECURITY_OBJECT_DIRECTORY;
#endif
}

// no prefix to find: the whole path is the document and the caller forces it.
Path* path_longest_existing_prefix(Path* path) {
    if (!path) return NULL;
    if (!path_provider_is_local(path)) return NULL;
    Pool* pool = path_get_pool();
    if (!pool) return NULL;
#ifndef LAMBDA_NO_FILE_IO
    for (Path* probe = path; probe && !path_is_root(probe); probe = probe->parent) {
        // A wildcard segment is a query, not a location; it can only be the
        // whole reference, never a prefix the trailing steps navigate into.
        if (path_ends_with_wildcard(probe)) return probe == path ? probe : NULL;
        StrBuf* buf = strbuf_new();
        path_to_os_path(path_qualify_default(pool, probe), buf);
        int32_t kind;
        bool acquired = security_io_input_kind(lambda_current_security_io(), buf->str, &kind, NULL);
        strbuf_free(buf);
        if (acquired) return probe;
    }

#endif
    return NULL;
}

/**
 * Load path metadata via stat() without loading content.
 * Sets path->meta and PATH_FLAG_META_LOADED flag.
 */
bool path_load_metadata(Path* path) {
    if (!path) return false;
    const SecurityIOClient* io = lambda_current_security_io();
    if (io && !path_provider_is_local(path)) return false;
    if (!io && (path->flags & PATH_FLAG_META_LOADED)) return true;

    Pool* pool = path_get_pool();
    if (!pool) {
        log_error("path_load_metadata: no registered pool provider");
        return false;
    }

    StrBuf* path_buf = strbuf_new();
    path_to_os_path(path_qualify_default(pool, path), path_buf);

    FileStat fs;
    bool acquired = security_io_stat(io, path_buf->str, &fs, NULL);
    strbuf_free(path_buf);
    // Scoped metadata belongs to this acquisition, not a possibly shared Path
    // literal's earlier context or root binding.
    return acquired && path_apply_stat_metadata(path, pool, &fs);
}

// Forward declarations for path resolution
static Item path_enumerate_locations(Path* base, const char* dir_path, bool recursive);
static Item resolve_file_content(Path* path, const char* file_path);



// Extern declaration for heap_strcpy (defined in lambda-mem.cpp)
extern String* heap_strcpy(const char* src, int64_t len);

static Item path_snapshot_publish(Path* path, const SecurityIOClient* io,
        const SecurityAuthorityStamp* stamp, Item value) {
    if (io) {
        if (value != ITEM_ERROR && !lambda_security_path_cache_store(path, stamp, value)) return ITEM_ERROR;
    } else path->result = value;
    return value;
}

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
    if (lambda_dry_run_enabled()) {
        log_debug("dry-run: fabricated path_resolve_for_iteration()");
        return ITEM_NULL;
    }

    // sys.* values can expire independently; the sysinfo cache owns their TTL.
    PathScheme scheme = path_get_scheme(path);
    const SecurityIOClient* io = lambda_current_security_io();
    // In-memory document references need no external provider (S2.4.4).
    if (scheme == PATH_SCHEME_TEMP) {
        if (path_has_wildcards(path) && g_path_enumeration_resolver)
            return g_path_enumeration_resolver(path);
        return fn_force((Item)(uintptr_t)path);
    }
    if (io && !path_provider_is_local(path)) {
        set_runtime_error(ERR_FILE_ACCESS_DENIED, "path provider is unavailable in this resource scope");
        return ITEM_ERROR;
    }
    if (scheme == PATH_SCHEME_SYS) {
#ifdef LAMBDA_NO_SYSINFO
        log_error("path-profile: sys information provider is excluded");
        return ITEM_ERROR;
#else
        Item result = sysinfo_resolve_path(path);
        // Path formatting reads this result after the force operation returns.
        if (result != ITEM_NULL && result != ITEM_ERROR) path->result = result;
        else path->result = 0;
        return result;
#endif
    }

    SecurityAuthorityStamp stamp = {0};
    if (io) {
        if (!security_io_authority_stamp(io, &stamp, NULL)) {
            set_runtime_error(ERR_FILE_ACCESS_DENIED, "path snapshot authority is unavailable");
            return ITEM_ERROR;
        }
        Item cached;
        if (lambda_security_path_cache_get(path, &stamp, &cached)) return cached;
    }

    // Already resolved?
    if (!io && path->result != 0) {
        return path->result;
    }
    if (g_path_enumeration_resolver && path_needs_runtime_enumeration(path)) {
        Item matches = g_path_enumeration_resolver(path);
        return path_snapshot_publish(path, io, &stamp, matches);
    }

#ifdef LAMBDA_NO_FILE_IO
    log_error("path-profile: external path providers are excluded");
    return ITEM_ERROR;
#else
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
        Item result = path_enumerate_locations(parent, path_buf->str, recursive);

        strbuf_free(path_buf);
        return path_snapshot_publish(path, io, &stamp, result);
    }

    // Convert path to OS path string
    StrBuf* path_buf = strbuf_new();
    path_to_os_path(path_qualify_default(path_get_pool(), path), path_buf);
    const char* os_path = path_buf->str;

    // Check if directory or file
    int32_t kind = -1;
    SecurityIOResult acquisition;
    if (!security_io_input_kind(io, os_path, &kind, &acquisition)) {
        strbuf_free(path_buf);
        if (io && acquisition.system_error != ENOENT) {
            set_runtime_error(ERR_FILE_ACCESS_DENIED, "path iteration is unavailable in this resource scope");
            return ITEM_ERROR;
        }
        return ITEM_NULL;
    }

    Item result;
    if (kind == SECURITY_OBJECT_DIRECTORY) {
        result = path_enumerate_locations(path, os_path, false);
    } else {
        // File: load and parse content
        result = resolve_file_content(path, os_path);
    }

    strbuf_free(path_buf);

    // Scoped acquisitions do not publish heap results into a shared path spine.
    return path_snapshot_publish(path, io, &stamp, result);
#endif
}

// External declaration for input system
extern Item fn_input1(Item url);

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
    return fn_input1(s2it(url_str));
}

// One traversal serves directory iteration, * and **. Scoped walks have
// finite output/depth budgets, and never follow links supplied by a listing.
typedef struct PathWalk {
    const SecurityIOClient* io;
    Pool* pool;
    uint64_t* result;
    uint32_t count;
} PathWalk;

static bool path_walk_directory(PathWalk* walk, Path* base, const char* directory,
        bool recursive, uint32_t depth) {
    if (walk->io && depth >= SECURITY_IO_PATH_DEPTH_LIMIT) {
        set_runtime_error(ERR_IO_ERROR, "path enumeration depth limit exceeded");
        return false;
    }
    uint32_t remaining = walk->io ? SECURITY_IO_DIRECTORY_LIMIT - walk->count : UINT32_MAX;
    if (!remaining) {
        set_runtime_error(ERR_IO_ERROR, "path enumeration entry limit exceeded");
        return false;
    }
    SecurityDirectory listing = {0};
    SecurityIOResult status;
    if (!security_io_enumerate(walk->io, directory, remaining, &listing, &status)) {
        if (!walk->io && depth) return true; // legacy host walks skip unreadable subdirectories
        set_runtime_error(status.decision == SECURITY_ALLOWED ? ERR_IO_ERROR : ERR_FILE_ACCESS_DENIED,
            "path enumeration failed in the active resource scope");
        return false;
    }
    bool ok = true;
    for (uint32_t i = 0; ok && i < listing.count; i++) {
        if (walk->io && walk->count >= SECURITY_IO_DIRECTORY_LIMIT) {
            set_runtime_error(ERR_IO_ERROR, "path enumeration entry limit exceeded");
            ok = false; break;
        }
        SecurityDirectoryEntry* entry = &listing.entries[i];
        Path* child = path_extend(walk->pool, base, entry->name);
        if (!child || (entry->has_metadata &&
                !path_apply_stat_metadata(child, walk->pool, &entry->metadata))) {
            set_runtime_error(ERR_OUT_OF_MEMORY, "path enumeration allocation failed");
            ok = false; break;
        }
        array_push_verbatim((Array*)(uintptr_t)*walk->result, (Item)(uintptr_t)child);
        walk->count++;
        if (recursive && entry->is_directory && !entry->metadata.is_symlink) {
            char* joined = file_path_join(directory, entry->name);
            ok = joined && path_walk_directory(walk, child, joined, true, depth + 1);
            mem_free(joined);
        }
    }
    security_io_directory_free(&listing);
    return ok;
}

static Item path_enumerate_locations(Path* base, const char* directory, bool recursive) {
    Pool* pool = path_get_pool();
    if (!pool) return ITEM_ERROR;
    LambdaRootFrame roots = {0};
    if (eval_context_tls_runtime() && !lambda_root_frame_begin(&roots, 1)) {
        lambda_root_frame_overflow_error();
        return ITEM_ERROR;
    }
    Item fallback = ITEM_NULL;
    uint64_t* result = roots.active ? lambda_root_frame_slot(&roots, 0) : &fallback;
    Array* matches = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    if (!matches) { lambda_root_frame_end(&roots); return ITEM_ERROR; }
    matches->type_id = LMD_TYPE_ARRAY;
    *result = (Item)(uintptr_t)matches;
    // S2.4.6: ** includes its base; * and a directory's children do not.
    if (recursive) array_push_verbatim(matches, (Item)(uintptr_t)base);
    PathWalk walk = {lambda_current_security_io(), pool, result, recursive ? 1 : 0};
    bool ok = path_walk_directory(&walk, base, directory, recursive, 0);
    Item output = ok ? *result : ITEM_ERROR;
    lambda_root_frame_end(&roots);
    return output;
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
    if (lambda_dry_run_enabled()) {
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

    const SecurityIOClient* io = lambda_current_security_io();
    bool exists = false;
    if (!io) exists = target_exists(target);
    else if (target->scheme == TARGET_SCHEME_FILE) {
        StrBuf* path = (StrBuf*)target_to_local_path(target, NULL);
        FileStat fs;
        exists = path && security_io_stat(io, path->str, &fs, NULL) && fs.exists;
        if (path) strbuf_free(path);
    }
    log_debug("fn_exists: target exists=%d", exists);

    target_free(target);
    return exists ? BOOL_TRUE : BOOL_FALSE;
}

#endif // PATH_NO_ITERATION
