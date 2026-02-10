/**
 * target.c - Unified I/O target handling for Lambda
 *
 * This module provides a unified Target struct for handling I/O operations
 * across different sources: URL strings (file://, http://, https://) and
 * Lambda's cross-platform Path objects.
 *
 * Key design principles:
 * - Lambda code uses URLs or Lambda Paths (not native OS paths)
 * - Relative paths are resolved against the current working directory
 * - All path resolution is cross-platform
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/stat.h>

#include "lambda-data.hpp"
#include "../lib/strbuf.h"
#include "../lib/log.h"
#include "../lib/url.h"
#include "../lib/hashmap.h"

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

// item_type_id - needed for item_to_target()
#ifdef SIMPLE_SCHEMA_PARSER
// Provide local implementation when lambda-eval.cpp is not linked
static inline TypeId local_item_type_id(Item item) {
    return item.type_id();
}
#define item_type_id local_item_type_id
#else
extern "C" TypeId item_type_id(Item item);
#endif

extern "C" {

/**
 * Get the current working directory as a file:// URL.
 * Caller must free the returned Url.
 */
static Url* get_cwd_url(void) {
    char cwd_buf[4096];
    if (!getcwd(cwd_buf, sizeof(cwd_buf))) {
        log_error("target: failed to get cwd: %s", strerror(errno));
        return NULL;
    }

    // Convert to file:// URL
    StrBuf* url_buf = strbuf_new();
    strbuf_append_str(url_buf, "file://");

#ifdef _WIN32
    // Windows: convert C:\path to /C:/path
    if (cwd_buf[1] == ':') {
        strbuf_append_char(url_buf, '/');
    }
#endif

    // Append path, converting backslashes to forward slashes
    for (char* p = cwd_buf; *p; p++) {
        if (*p == '\\') {
            strbuf_append_char(url_buf, '/');
        } else {
            strbuf_append_char(url_buf, *p);
        }
    }

    // Ensure trailing slash for directory
    if (url_buf->length > 0 && url_buf->str[url_buf->length - 1] != '/') {
        strbuf_append_char(url_buf, '/');
    }

    Url* url = url_parse(url_buf->str);
    strbuf_free(url_buf);
    return url;
}

/**
 * Determine scheme from URL string prefix.
 */
static TargetScheme scheme_from_url_string(const char* url_str) {
    if (!url_str) return TARGET_SCHEME_UNKNOWN;

    if (strncmp(url_str, "file://", 7) == 0) return TARGET_SCHEME_FILE;
    if (strncmp(url_str, "http://", 7) == 0) return TARGET_SCHEME_HTTP;
    if (strncmp(url_str, "https://", 8) == 0) return TARGET_SCHEME_HTTPS;
    if (strncmp(url_str, "sys://", 6) == 0) return TARGET_SCHEME_SYS;
    if (strncmp(url_str, "ftp://", 6) == 0) return TARGET_SCHEME_FTP;
    if (strncmp(url_str, "data:", 5) == 0) return TARGET_SCHEME_DATA;

    // No explicit scheme - treat as relative file path
    return TARGET_SCHEME_FILE;
}

/**
 * Determine scheme from parsed Url.
 */
static TargetScheme scheme_from_url(Url* url) {
    if (!url) return TARGET_SCHEME_UNKNOWN;

    switch (url->scheme) {
        case URL_SCHEME_FILE: return TARGET_SCHEME_FILE;
        case URL_SCHEME_HTTP: return TARGET_SCHEME_HTTP;
        case URL_SCHEME_HTTPS: return TARGET_SCHEME_HTTPS;
        case URL_SCHEME_SYS: return TARGET_SCHEME_SYS;
        case URL_SCHEME_FTP: return TARGET_SCHEME_FTP;
        case URL_SCHEME_DATA: return TARGET_SCHEME_DATA;
        default: return TARGET_SCHEME_UNKNOWN;
    }
}

/**
 * Determine scheme from Lambda Path.
 */
static TargetScheme scheme_from_path(Path* path) {
    if (!path) return TARGET_SCHEME_UNKNOWN;

    PathScheme path_scheme = path_get_scheme(path);
    switch (path_scheme) {
        case PATH_SCHEME_FILE: return TARGET_SCHEME_FILE;
        case PATH_SCHEME_HTTP: return TARGET_SCHEME_HTTP;
        case PATH_SCHEME_HTTPS: return TARGET_SCHEME_HTTPS;
        case PATH_SCHEME_SYS: return TARGET_SCHEME_SYS;
        case PATH_SCHEME_REL:
        case PATH_SCHEME_PARENT:
            return TARGET_SCHEME_FILE;  // relative paths are local files
        default:
            return TARGET_SCHEME_UNKNOWN;
    }
}

// fixed seeds for target URL hashing (arbitrary constants)
#define TARGET_HASH_SEED0  0x12AE406AB1E59A3CULL
#define TARGET_HASH_SEED1  0x7F4A519D3E2B8C01ULL

/**
 * Compute hash for a target based on its normalized URL string.
 * Uses SipHash for fast, collision-resistant hashing.
 */
static uint64_t target_compute_hash(Target* target) {
    if (!target) return 0;

    StrBuf* buf = strbuf_new();
    target_to_url_string(target, buf);

    uint64_t hash = 0;
    if (buf->length > 0) {
        hash = hashmap_sip(buf->str, buf->length, TARGET_HASH_SEED0, TARGET_HASH_SEED1);
    }

    strbuf_free(buf);
    return hash;
}

/**
 * Convert an Item to a Target.
 *
 * Accepts:
 * - String: parsed as URL (with optional cwd for relative paths)
 * - Symbol: parsed as URL (with optional cwd for relative paths)
 * - Path: used directly as Lambda path
 *
 * Returns NULL on error.
 *
 * Note: Takes uint64_t instead of Item to avoid C/C++ type mismatch.
 * In C, Item is uint64_t; in C++, Item is a struct with .item field.
 */
Target* item_to_target(uint64_t item, Url* cwd) {
    Item it; it.item = item;
    TypeId type_id = item_type_id(it);

    Target* target = (Target*)calloc(1, sizeof(Target));
    if (!target) {
        log_error("item_to_target: failed to allocate Target");
        return NULL;
    }

    if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL) {
        // Parse as URL - extract chars from tagged pointer (high byte is type tag)
        const char* url_str;
        if (type_id == LMD_TYPE_SYMBOL) {
            Symbol* sym = (Symbol*)(item & 0x00FFFFFFFFFFFFFFULL);
            if (!sym || !sym->chars) {
                log_error("item_to_target: symbol is null");
                free(target);
                return NULL;
            }
            url_str = sym->chars;
        } else {
            String* str = (String*)(item & 0x00FFFFFFFFFFFFFFULL);
            if (!str || !str->chars) {
                log_error("item_to_target: string is null");
                free(target);
                return NULL;
            }
            url_str = str->chars;
        }
        log_debug("item_to_target: parsing URL '%s'", url_str);

        // Store original string for relative path preservation
        target->original = url_str;

        // Parse URL with optional base (cwd)
        Url* url;
        if (cwd) {
            url = url_parse_with_base(url_str, cwd);
        } else {
            // Check if relative path - need to resolve against cwd
            if (url_str[0] != '/' && strncmp(url_str, "file://", 7) != 0 &&
                strncmp(url_str, "http://", 7) != 0 && strncmp(url_str, "https://", 8) != 0 &&
                strncmp(url_str, "sys://", 6) != 0) {
                // Relative path - get cwd as base
                Url* cwd_url = get_cwd_url();
                if (cwd_url) {
                    url = url_parse_with_base(url_str, cwd_url);
                    url_destroy(cwd_url);
                } else {
                    url = url_parse(url_str);
                }
            } else {
                url = url_parse(url_str);
            }
        }

        if (!url) {
            log_error("item_to_target: failed to parse URL '%s'", url_str);
            free(target);
            return NULL;
        }

        target->type = TARGET_TYPE_URL;
        target->url = url;
        target->scheme = scheme_from_url(url);
        target->url_hash = target_compute_hash(target);

        log_debug("item_to_target: created URL target (scheme=%d, hash=0x%llx)", target->scheme, (unsigned long long)target->url_hash);
        return target;
    }
    else if (type_id == LMD_TYPE_PATH) {
        // Use Lambda Path directly - Path is a container, use direct cast
        Path* path = (Path*)(uint64_t)item;
        if (!path) {
            log_error("item_to_target: path is null");
            free(target);
            return NULL;
        }

        target->type = TARGET_TYPE_PATH;
        target->path = path;
        target->scheme = scheme_from_path(path);
        target->url_hash = target_compute_hash(target);

        log_debug("item_to_target: created Path target (scheme=%d, hash=0x%llx)", target->scheme, (unsigned long long)target->url_hash);
        return target;
    }
    else {
        log_error("item_to_target: unsupported type %d (expected string, symbol, or path)", type_id);
        free(target);
        return NULL;
    }
}

/**
 * Convert Target to local OS file path.
 *
 * - For URL targets: extracts pathname and resolves against cwd
 * - For Path targets: converts to OS path
 * - Returns NULL for remote URLs (http, https)
 *
 * Returns StrBuf* that caller must free.
 */
void* target_to_local_path(Target* target, Url* cwd) {
    if (!target) return NULL;

    // Remote URLs cannot be converted to local path
    if (target->scheme == TARGET_SCHEME_HTTP || target->scheme == TARGET_SCHEME_HTTPS) {
        log_debug("target_to_local_path: cannot convert remote URL to local path");
        return NULL;
    }

    StrBuf* path_buf = strbuf_new();

    if (target->type == TARGET_TYPE_URL) {
        Url* url = target->url;
        if (!url || !url->pathname) {
            strbuf_free(path_buf);
            return NULL;
        }

        const char* pathname = url->pathname->chars;

#ifdef _WIN32
        // Windows: handle /C:/path format
        if (pathname && pathname[0] == '/' &&
            ((pathname[1] >= 'A' && pathname[1] <= 'Z') ||
             (pathname[1] >= 'a' && pathname[1] <= 'z')) &&
            pathname[2] == ':') {
            // Skip leading slash for Windows drive paths
            pathname++;
        }
#endif

        if (pathname) {
            strbuf_append_str(path_buf, pathname);
        }
    }
    else if (target->type == TARGET_TYPE_PATH) {
        Path* path = target->path;
        if (!path) {
            strbuf_free(path_buf);
            return NULL;
        }

        // Check if relative path - resolve against cwd
        PathScheme path_scheme = path_get_scheme(path);
        if (path_scheme == PATH_SCHEME_REL || path_scheme == PATH_SCHEME_PARENT) {
            // Get cwd if not provided
            Url* effective_cwd = cwd;
            Url* temp_cwd = NULL;
            if (!effective_cwd) {
                temp_cwd = get_cwd_url();
                effective_cwd = temp_cwd;
            }

            if (effective_cwd && effective_cwd->pathname) {
                // Start with cwd pathname (without trailing slash)
                const char* cwd_path = effective_cwd->pathname->chars;
                size_t cwd_len = strlen(cwd_path);
                if (cwd_len > 0 && cwd_path[cwd_len - 1] == '/') {
                    strbuf_append_str_n(path_buf, cwd_path, cwd_len - 1);
                } else {
                    strbuf_append_str(path_buf, cwd_path);
                }
                strbuf_append_char(path_buf, '/');
            }

            // Append relative path
            StrBuf* rel_buf = strbuf_new();
            path_to_os_path(path, rel_buf);
            strbuf_append_str(path_buf, rel_buf->str);
            strbuf_free(rel_buf);

            if (temp_cwd) {
                url_destroy(temp_cwd);
            }
        } else {
            // Absolute path - convert directly
            path_to_os_path(path, path_buf);
        }
    }

    log_debug("target_to_local_path: result='%s'", path_buf->str);
    return path_buf;
}

/**
 * Get URL string representation of Target.
 *
 * - For URL targets: returns href
 * - For Path targets: converts to URL string
 *
 * Writes to out_buf (StrBuf*) and returns the string.
 */
const char* target_to_url_string(Target* target, void* out_buf) {
    if (!target || !out_buf) return NULL;

    StrBuf* buf = (StrBuf*)out_buf;

    if (target->type == TARGET_TYPE_URL) {
        Url* url = target->url;
        if (url && url->href) {
            strbuf_append_str(buf, url->href->chars);
        }
    }
    else if (target->type == TARGET_TYPE_PATH) {
        Path* path = target->path;
        if (path) {
            path_to_string(path, buf);
        }
    }

    return buf->str;
}

/**
 * Check if target is a local file (file:// or relative path).
 */
bool target_is_local(Target* target) {
    if (!target) return false;
    return target->scheme == TARGET_SCHEME_FILE || target->scheme == TARGET_SCHEME_SYS;
}

/**
 * Check if target is a remote URL (http:// or https://).
 */
bool target_is_remote(Target* target) {
    if (!target) return false;
    return target->scheme == TARGET_SCHEME_HTTP || target->scheme == TARGET_SCHEME_HTTPS;
}

/**
 * Check if target is a directory (local targets only).
 * Returns false for remote URLs or if stat fails.
 */
bool target_is_dir(Target* target) {
    if (!target) return false;

    // Only check local targets
    if (!target_is_local(target)) return false;

    struct stat st;

    if (target->type == TARGET_TYPE_URL && target->url) {
        const char* pathname = url_get_pathname(target->url);
        if (!pathname) return false;

#ifdef _WIN32
        // Windows: handle /C:/path format
        if (pathname[0] == '/' &&
            ((pathname[1] >= 'A' && pathname[1] <= 'Z') ||
             (pathname[1] >= 'a' && pathname[1] <= 'z')) &&
            pathname[2] == ':') {
            pathname++;
        }
#endif

        if (stat(pathname, &st) == 0 && S_ISDIR(st.st_mode)) {
            return true;
        }
    }
    else if (target->type == TARGET_TYPE_PATH && target->path) {
        StrBuf* path_buf = strbuf_new();
        path_to_os_path(target->path, path_buf);
        bool is_dir = (stat(path_buf->str, &st) == 0 && S_ISDIR(st.st_mode));
        strbuf_free(path_buf);
        return is_dir;
    }

    return false;
}

/**
 * Check if target exists (file or directory).
 * For local targets: uses stat()
 * For remote URLs: returns false (would need HTTP HEAD request)
 */
bool target_exists(Target* target) {
    if (!target) return false;

    // Remote URLs would need HTTP HEAD request - not supported yet
    if (target_is_remote(target)) {
        log_debug("target_exists: remote URLs not supported yet");
        return false;
    }

    struct stat st;

    if (target->type == TARGET_TYPE_URL && target->url) {
        const char* pathname = url_get_pathname(target->url);
        if (!pathname) return false;

#ifdef _WIN32
        // Windows: handle /C:/path format
        if (pathname[0] == '/' &&
            ((pathname[1] >= 'A' && pathname[1] <= 'Z') ||
             (pathname[1] >= 'a' && pathname[1] <= 'z')) &&
            pathname[2] == ':') {
            pathname++;
        }
#endif

        return (stat(pathname, &st) == 0);
    }
    else if (target->type == TARGET_TYPE_PATH && target->path) {
        StrBuf* path_buf = strbuf_new();
        path_to_os_path(target->path, path_buf);
        bool exists = (stat(path_buf->str, &st) == 0);
        strbuf_free(path_buf);
        return exists;
    }

    return false;
}

/**
 * Free a Target and its contents.
 * Note: Does NOT free Path objects (they may be shared).
 */
void target_free(Target* target) {
    if (!target) return;

    if (target->type == TARGET_TYPE_URL && target->url) {
        url_destroy(target->url);
        target->url = NULL;
    }
    // Don't free Path - it may be shared/managed elsewhere

    free(target);
}

/**
 * Check if two targets refer to the same resource.
 * Compares by pre-computed URL hash for fast equality.
 * Both NULL targets are considered equal.
 */
bool target_equal(Target* a, Target* b) {
    if (a == b) return true;         // same pointer or both NULL
    if (!a || !b) return false;      // one NULL, one not
    return a->url_hash == b->url_hash;
}

} // extern "C"
