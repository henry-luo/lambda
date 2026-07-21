/**
 * Script Runner for Radiant Layout Engine
 *
 * Extracts inline and external <script> source from the HTML Element* tree
 * and executes it via the Lambda JS transpiler with the DomDocument as the
 * DOM context.
 *
 * External scripts are downloaded (HTTP) or read from disk using the same
 * URL resolution infrastructure as CSS and image loading.
 *
 * Pipeline position:
 *   HTML parse → Element* tree → DomElement* tree → CSS cascade
 *   → execute_document_scripts() → optional post-script recascade → layout
 */
#include "../lib/memtrack.h"

#include "radiant.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/js/js_transpiler.hpp"
#include "../lambda/js/js_dom.h"
#include "../lambda/js/js_dom_events.h"
#include "../lambda/js/js_runtime.h"
#include "../lambda/js/js_xhr.h"
#include "../lambda/transpiler.hpp"
#include "../lambda/runtime/gc/gc_heap.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lambda/input/input.hpp"
#include "../lib/log.h"
#include "../lib/mem_factory.h"
#include "layout.hpp"  // MAX_LAYOUT_DEPTH — shared DOM-recursion depth cap
#include "../lib/arraylist.h"
#include "../lib/strbuf.h"
#include "../lib/str.h"
#include "../lib/url.h"
#include "../lib/file.h"
#include "../lib/lru_cache.h"
#include "../lib/hashmap.h"
#include "../lib/hashmap_helpers.h"
#include "../lib/tagged.hpp"
#include "../lib/time_util.h"
#include "../lambda/js/js_event_loop.h"
#include "../lambda/network/network_resource_manager.h"

extern "C" bool js_dom_is_host_driven_loop(void);  // defined in lambda/js/js_dom.cpp

#include <cstring>
#include <cstdlib>
#include <signal.h>
#include <setjmp.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/time.h>
#endif

extern __thread EvalContext* context;
extern __thread Context* input_context;
extern Item transpile_js_module_to_mir(Runtime* runtime, const char* js_source, const char* filename);
extern void jm_cleanup_active_mir(void);
extern void jm_abandon_active_mir_after_signal(void);

// Crash guard for JS JIT execution (catches SIGSEGV/SIGBUS in compiled code)
#ifndef _WIN32
static sigjmp_buf js_exec_jmpbuf;
static volatile sig_atomic_t js_exec_guarded = 0;
static struct sigaction js_exec_old_segv, js_exec_old_bus;
static volatile sig_atomic_t js_exec_timed_out = 0;

// Per-document script timeout: covers parse/transpile plus execution.
static struct sigaction js_exec_old_prof;
#define JS_EXEC_TIMEOUT_BASE_SECONDS 5
#define JS_EXEC_TIMEOUT_MAX_SECONDS 120
#define JS_EXEC_TIMEOUT_ENV_MAX_SECONDS 600
static volatile sig_atomic_t js_batch_cleanup_unsafe = 0;

static void js_exec_timeout_handler(int sig) {
    if (js_exec_guarded) {
        js_exec_timed_out = 1;
        const char* msg = "execute_document_scripts: JS execution timed out by watchdog\n";
        write(STDERR_FILENO, msg, strlen(msg));
        js_exec_guarded = 0;
        sigaction(SIGSEGV, &js_exec_old_segv, NULL);
        sigaction(SIGBUS, &js_exec_old_bus, NULL);
        sigaction(SIGPROF, &js_exec_old_prof, NULL);
        siglongjmp(js_exec_jmpbuf, 2);
    }
}

static void js_exec_crash_handler(int sig, siginfo_t* info, void* ctx) {
    if (js_exec_guarded) {
        // async-signal-safe: use write() instead of log_error()
        const char* msg = (sig == SIGBUS)
            ? "execute_document_scripts: caught SIGBUS during JS execution\n"
            : "execute_document_scripts: caught SIGSEGV during JS execution\n";
        write(STDERR_FILENO, msg, strlen(msg));
        js_exec_guarded = 0;
        sigaction(SIGSEGV, &js_exec_old_segv, NULL);
        sigaction(SIGBUS, &js_exec_old_bus, NULL);
        siglongjmp(js_exec_jmpbuf, 1);
    }
    // not guarded — forward to previous handler
    struct sigaction* old = (sig == SIGSEGV) ? &js_exec_old_segv : &js_exec_old_bus;
    if (old->sa_flags & SA_SIGINFO) {
        old->sa_sigaction(sig, info, ctx);
    } else if (old->sa_handler != SIG_DFL && old->sa_handler != SIG_IGN) {
        old->sa_handler(sig);
    } else {
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

static int js_exec_timeout_seconds(size_t source_len) {
    const char* env = getenv("LAMBDA_JS_EXEC_TIMEOUT_SECONDS");
    if (env && env[0]) {
        char* end = nullptr;
        long parsed = strtol(env, &end, 10);
        if (end != env && parsed > 0) {
            if (parsed > JS_EXEC_TIMEOUT_ENV_MAX_SECONDS) return JS_EXEC_TIMEOUT_ENV_MAX_SECONDS;
            return (int)parsed;
        }
    }

    int seconds = JS_EXEC_TIMEOUT_BASE_SECONDS;
    if (source_len > 32768) {
        // the watchdog covers parse/transpile as well as execution; medium
        // minified browser libraries can exceed 5s in debug without hanging.
        seconds += (int)((source_len + 32767) / 32768) * JS_EXEC_TIMEOUT_BASE_SECONDS;
    }
    if (seconds > JS_EXEC_TIMEOUT_MAX_SECONDS) seconds = JS_EXEC_TIMEOUT_MAX_SECONDS;
    return seconds;
}

static void js_exec_watchdog_arm(int timeout_seconds) {
    struct sigaction timeout_action;
    memset(&timeout_action, 0, sizeof(timeout_action));
    timeout_action.sa_handler = js_exec_timeout_handler;
    sigaction(SIGPROF, &timeout_action, &js_exec_old_prof);

    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    timer.it_value.tv_sec = timeout_seconds;
    setitimer(ITIMER_PROF, &timer, NULL);
}

static void js_exec_watchdog_disarm(void) {
    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    setitimer(ITIMER_PROF, &timer, NULL);
    sigaction(SIGPROF, &js_exec_old_prof, NULL);
}
#endif  // !_WIN32

// Pool/cache from the most recent JS execution.
// Destroyed by script_runner_cleanup_heap() in per-file cleanup (after layout).
static Pool* s_js_reuse_pool = nullptr;
static LruCache* s_script_source_cache = nullptr;
static JsMirCache* s_js_mir_cache = nullptr;
static bool s_retain_js_state = true;
static bool s_execute_external_scripts = true;

extern "C" void script_runner_set_retain_js_state(bool retain) {
    s_retain_js_state = retain;
}

extern "C" void script_runner_set_execute_external_scripts(bool execute) {
    s_execute_external_scripts = execute;
}

extern "C" void script_runner_set_js_mir_cache(JsMirCache* cache) {
    // The layout batch owns the cache. The runner only borrows it so cached
    // code cannot outlive the process-level batch that established its ABI.
    s_js_mir_cache = cache;
}

static void script_runner_cleanup_source_cache() {
    if (s_script_source_cache) {
        lru_cache_free(s_script_source_cache);
        s_script_source_cache = nullptr;
    }
}

extern "C" void script_runner_cleanup_heap() {
    jm_cleanup_deferred_mir();
    if (s_js_reuse_pool) {
        mem_pool_destroy(s_js_reuse_pool);
        s_js_reuse_pool = nullptr;
    }
    script_runner_cleanup_source_cache();
}

typedef enum JsScriptTaskKind {
    JS_SCRIPT_TASK_CLASSIC,
    JS_SCRIPT_TASK_MODULE,
    JS_SCRIPT_TASK_BODY_ONLOAD
} JsScriptTaskKind;

typedef enum JsScriptTaskScheduling {
    JS_SCRIPT_SCHED_POST_DOM,
    JS_SCRIPT_SCHED_ASYNC,
    JS_SCRIPT_SCHED_DEFER,
    JS_SCRIPT_SCHED_AFTER_SCRIPTS
} JsScriptTaskScheduling;

typedef enum JsScriptCompilePolicy {
    JS_SCRIPT_COMPILE_INLINE_IMMEDIATE,
    JS_SCRIPT_COMPILE_EXTERNAL_SEPARATE,
    JS_SCRIPT_COMPILE_EXTERNAL_SOURCE_CACHED,
    JS_SCRIPT_COMPILE_MODULE_SEPARATE,
    JS_SCRIPT_COMPILE_HANDLER_EAGER
} JsScriptCompilePolicy;

typedef enum JsScriptTaskStatus {
    JS_SCRIPT_TASK_READY,
    JS_SCRIPT_TASK_SKIPPED_UNSUPPORTED_TYPE,
    JS_SCRIPT_TASK_SKIPPED_EXTERNAL_DISABLED,
    JS_SCRIPT_TASK_SKIPPED_MODULE_UNSUPPORTED,
    JS_SCRIPT_TASK_SKIPPED_NOMODULE,
    JS_SCRIPT_TASK_SKIPPED_LARGE_DEFER,
    JS_SCRIPT_TASK_SKIPPED_TESTHARNESS_INLINE,
    JS_SCRIPT_TASK_LOAD_FAILED
} JsScriptTaskStatus;

typedef struct JsScriptTask {
    JsScriptTaskKind kind;
    JsScriptTaskScheduling scheduling;
    JsScriptCompilePolicy compile_policy;
    JsScriptTaskStatus status;
    Element* script_element;
    char* type_attr;
    char* src_attr;
    char* resolved_url;
    char* source;
    size_t source_len;
    bool external;
    bool parser_inserted;
    bool async_attr;
    bool defer_attr;
    bool nomodule_attr;
    bool ready_to_execute;
    bool load_blocking;
    bool executed;
    bool source_cache_hit;
    int source_line;
    int source_column;
    int document_order;
} JsScriptTask;

typedef struct JsScriptTaskCollection {
    ArrayList* scripts;
    ArrayList* onload_handlers;
    ArrayList* generated_sources;
    int total_script_elements;
    int inline_scripts;
    int external_scripts;
    int skipped_scripts;
    int onload_handlers_count;
    int async_ready_scripts;
    int defer_scripts;
    int load_blocking_scripts;
    int source_cache_hits;
    int source_cache_misses;
    int source_cache_stale;
    size_t inline_source_bytes;
    size_t external_source_bytes;
    size_t onload_source_bytes;
    bool testharness_seen;
} JsScriptTaskCollection;

typedef struct JsScriptSchedulerQueues {
    ArrayList* post_dom;
    ArrayList* async_ready;
    ArrayList* defer;
} JsScriptSchedulerQueues;

typedef struct JsScriptSourceCacheEntry {
    char* source;
    size_t source_len;
    bool is_http;
    time_t mtime;
    uint64_t file_size;
} JsScriptSourceCacheEntry;

#ifndef NDEBUG
static long script_runner_wall_now_us();
static bool script_task_timing_enabled();
#endif

// forward declaration from dom_element.cpp / cmd_layout.cpp
extern const char* extract_element_attribute(Element* elem, const char* attr_name, Arena* arena);

// ============================================================================
// Helper: get element tag name from TypeElmt
// ============================================================================

/**
 * Get the tag name string from an Element's TypeElmt.
 * Returns nullptr if the element has no type info.
 */
static const char* get_elem_tag(Element* elem) {
    if (!elem || !elem->type) return nullptr;
    TypeElmt* type = (TypeElmt*)elem->type;
    return type->name.str;
}

static size_t get_elem_tag_len(Element* elem) {
    if (!elem || !elem->type) return 0;
    TypeElmt* type = (TypeElmt*)elem->type;
    return type->name.length;
}

/**
 * Check if a Lambda Element has the tag name "script" (case-insensitive).
 */
static bool is_script_element(Element* elem) {
    const char* tag = get_elem_tag(elem);
    if (!tag) return false;
    return str_ieq_const(tag, get_elem_tag_len(elem), "script");
}

/**
 * Check if a Lambda Element has the tag name "body" (case-insensitive).
 */
static bool is_body_element(Element* elem) {
    const char* tag = get_elem_tag(elem);
    if (!tag) return false;
    return str_ieq_const(tag, get_elem_tag_len(elem), "body");
}

/**
 * Extract the text content of a <script> Element.
 * Script elements store their source as child String items in the Element.
 */
static void extract_script_text(Element* script_elem, StrBuf* buf) {
    if (!script_elem) return;
    for (int64_t i = 0; i < script_elem->length; i++) {
        Item child = script_elem->items[i];
        TypeId tid = get_type_id(child);
        if (tid == LMD_TYPE_STRING) {
            String* s = it2s(child);
            if (s && s->len > 0) {
                const char* start = s->chars;
                int len = s->len;
                // strip XHTML CDATA markers: <![CDATA[ ... ]]>
                // also handle // or /* commented variants used in HTML/XHTML polyglots
                const char* cdata_open = strstr(start, "<![CDATA[");
                if (cdata_open && cdata_open - start < 40) {
                    // find the actual CDATA open, skip past it
                    const char* after_open = cdata_open + 9; // skip "<![CDATA["
                    // skip optional newline after CDATA open
                    if (*after_open == '\n') after_open++;
                    else if (*after_open == '\r' && *(after_open + 1) == '\n') after_open += 2;
                    int prefix_len = (int)(after_open - start);
                    start = after_open;
                    len -= prefix_len;
                    // strip trailing ]]> (with optional preceding whitespace)
                    while (len > 3) {
                        const char* end_ptr = start + len - 3;
                        if (end_ptr[0] == ']' && end_ptr[1] == ']' && end_ptr[2] == '>') {
                            // trim trailing whitespace before ]]>
                            while (len > 3 && (start[len - 4] == ' ' || start[len - 4] == '\t' ||
                                                start[len - 4] == '\n' || start[len - 4] == '\r')) {
                                len--;
                            }
                            len -= 3; // remove "]]>"
                            break;
                        }
                        break;
                    }
                }
                strbuf_append_str_n(buf, start, len);
            }
        }
    }
}

static void append_browser_global_sync(StrBuf* buf) {
    strbuf_append_str(buf,
        "\nif (typeof window !== 'undefined') {\n"
        "  if (typeof window.jQuery !== 'undefined') jQuery = window.jQuery;\n"
        "  if (typeof window.$ !== 'undefined') $ = window.$;\n"
        "  else if (typeof jQuery !== 'undefined') $ = jQuery;\n"
        "  if (typeof jQuery !== 'undefined' && typeof window.jQuery === 'undefined') window.jQuery = jQuery;\n"
        "  if (typeof $ !== 'undefined' && typeof window.$ === 'undefined') window.$ = $;\n"
        "}\n");
}

static char* try_join_script_resource(const char* root, const char* abs_path) {
    if (!root || !abs_path || abs_path[0] != '/') return nullptr;
    size_t root_len = strlen(root);
    size_t path_len = strlen(abs_path);
    char* candidate = (char*)mem_alloc(root_len + path_len + 1, MEM_CAT_JS_RUNTIME);
    if (!candidate) return nullptr;
    memcpy(candidate, root, root_len);
    memcpy(candidate + root_len, abs_path, path_len);
    candidate[root_len + path_len] = '\0';
    if (file_exists(candidate)) return candidate;
    mem_free(candidate);
    return nullptr;
}

static char* try_layout_support_script_resource(const char* support_root, const char* src) {
    if (!support_root || !src) return nullptr;

    char* resolved = try_join_script_resource(support_root, src);
    if (resolved) return resolved;

    const char* css_support_prefix = "/css/support";
    size_t css_support_prefix_len = strlen(css_support_prefix);
    if (strncmp(src, css_support_prefix, css_support_prefix_len) == 0 &&
        src[css_support_prefix_len] == '/') {
        return try_join_script_resource(support_root, src + css_support_prefix_len);
    }
    return nullptr;
}

static char* resolve_wpt_absolute_script_path(const char* src, Url* base_url) {
    if (!src || src[0] != '/' || src[1] == '/') return nullptr;

    if (strcmp(src, "/resources/testharness.js") == 0) {
        return mem_strdup("builtin:wpt-testharness.js", MEM_CAT_JS_RUNTIME);
    }
    if (strcmp(src, "/resources/testharnessreport.js") == 0) {
        return mem_strdup("builtin:wpt-testharnessreport.js", MEM_CAT_JS_RUNTIME);
    }
    if (strcmp(src, "/common/rendering-utils.js") == 0) {
        return mem_strdup("builtin:wpt-rendering-utils.js", MEM_CAT_JS_RUNTIME);
    }
    if (strcmp(src, "/css/support/interpolation-testcommon.js") == 0) {
        return mem_strdup("builtin:wpt-interpolation-testcommon.js", MEM_CAT_JS_RUNTIME);
    }
    if (strcmp(src, "/resources/testdriver.js") == 0) {
        return mem_strdup("builtin:wpt-testdriver.js", MEM_CAT_JS_RUNTIME);
    }
    if (strcmp(src, "/resources/testdriver-vendor.js") == 0) {
        return mem_strdup("builtin:wpt-testdriver-vendor.js", MEM_CAT_JS_RUNTIME);
    }

    if (base_url) {
        char* base_local = url_to_local_path(base_url);
        if (base_local) {
            const char* marker = strstr(base_local, "/test/layout/data/");
            size_t marker_len = strlen("/test/layout/data");
            if (!marker) {
                marker = strstr(base_local, "/layout/data/");
                marker_len = strlen("/layout/data");
            }
            if (marker) {
                size_t root_len = (size_t)(marker - base_local) + marker_len;
                char* support_root = (char*)mem_alloc(root_len + strlen("/support") + 1,
                                                      MEM_CAT_JS_RUNTIME);
                if (support_root) {
                    memcpy(support_root, base_local, root_len);
                    memcpy(support_root + root_len, "/support", strlen("/support") + 1);
                    char* resolved = try_layout_support_script_resource(support_root, src);
                    mem_free(support_root);
                    if (resolved) {
                        mem_free(base_local);
                        return resolved;
                    }
                }
            }
            mem_free(base_local);
        }
    }

    char* resolved = try_layout_support_script_resource("test/layout/data/support", src);
    if (resolved) return resolved;
    return try_join_script_resource("ref/wpt", src);
}

/**
 * Resolve a script src attribute to a loadable path/URL, following the same
 * URL resolution logic as CSS stylesheet loading in collect_linked_stylesheets.
 *
 * @param src        The src attribute value (absolute, relative, or full URL)
 * @param base_url   The document base URL for resolving relative paths
 * @param out_is_http Set to true if the resolved path is an HTTP(S) URL
 * @return           Resolved path string owned by the caller, or nullptr on OOM.
 */
static char* resolve_script_url(const char* src, Url* base_url, bool* out_is_http) {
    if (!src) return nullptr;
    *out_is_http = false;

    if (src[0] == '/' && src[1] != '/' && base_url &&
        (base_url->scheme == URL_SCHEME_HTTP || base_url->scheme == URL_SCHEME_HTTPS)) {
        // resolve root-relative remote paths against the document origin
        Url* resolved_url = parse_url(base_url, src);
        if (resolved_url && resolved_url->is_valid &&
            (resolved_url->scheme == URL_SCHEME_HTTP || resolved_url->scheme == URL_SCHEME_HTTPS)) {
            const char* url_str = url_get_href(resolved_url);
            char* resolved_path = mem_strdup(url_str ? url_str : src, MEM_CAT_JS_RUNTIME);
            *out_is_http = true;
            url_destroy(resolved_url);
            return resolved_path;
        } else {
            if (resolved_url) url_destroy(resolved_url);
            return mem_strdup(src, MEM_CAT_JS_RUNTIME);
        }
    } else if (src[0] == '/' && src[1] != '/') {
        char* wpt_path = resolve_wpt_absolute_script_path(src, base_url);
        if (wpt_path) {
            // WPT fixtures use server-root URLs while layout tests run from
            // local files, so resolve known support scripts before falling
            // back to the host filesystem root.
            return wpt_path;
        }
        // absolute local path
        return mem_strdup(src, MEM_CAT_JS_RUNTIME);
    } else if (strstr(src, "://") != nullptr) {
        // full url
        *out_is_http = (strncmp(src, "http://", 7) == 0 || strncmp(src, "https://", 8) == 0);
        return mem_strdup(src, MEM_CAT_JS_RUNTIME);
    } else if (base_url) {
        // resolve relative paths against the base url
        Url* resolved_url = parse_url(base_url, src);
        if (resolved_url && resolved_url->is_valid) {
            if (resolved_url->scheme == URL_SCHEME_HTTP || resolved_url->scheme == URL_SCHEME_HTTPS) {
                const char* url_str = url_get_href(resolved_url);
                char* resolved_path = mem_strdup(url_str ? url_str : src, MEM_CAT_JS_RUNTIME);
                *out_is_http = true;
                url_destroy(resolved_url);
                return resolved_path;
            } else {
                char* local_path = url_to_local_path(resolved_url);
                if (local_path) {
                    url_destroy(resolved_url);
                    return local_path;
                } else {
                    url_destroy(resolved_url);
                    return mem_strdup(src, MEM_CAT_JS_RUNTIME);
                }
            }
        } else {
            if (resolved_url) url_destroy(resolved_url);
            return mem_strdup(src, MEM_CAT_JS_RUNTIME);
        }
    } else {
        return mem_strdup(src, MEM_CAT_JS_RUNTIME);
    }
}

/**
 * Load external script content from a resolved path or URL.
 *
 * @param resolved_path  The resolved file path or URL
 * @param is_http        Whether the path is an HTTP(S) URL
 * @return               Allocated string with script content, or nullptr on failure.
 *                       Caller must free() the returned string.
 */
static bool script_source_cache_enabled() {
    const char* env = getenv("RADIANT_JS_SOURCE_CACHE");
    return !(env && strcmp(env, "0") == 0);
}

static size_t script_source_cache_limit_bytes() {
    const char* env = getenv("RADIANT_JS_SOURCE_CACHE_BYTES");
    if (env && env[0]) {
        char* end = nullptr;
        unsigned long long parsed = strtoull(env, &end, 10);
        if (end != env && parsed > 0) {
            return (size_t)parsed;
        }
    }
    return 8u * 1024u * 1024u;
}

static void script_source_cache_entry_free(const char* key, void* value, size_t bytes, void* udata) {
    (void)key;
    (void)bytes;
    (void)udata;
    JsScriptSourceCacheEntry* entry = (JsScriptSourceCacheEntry*)value;
    if (!entry) return;
    if (entry->source) mem_free(entry->source);
    mem_free(entry);
}

static LruCache* script_source_cache_get() {
    if (!script_source_cache_enabled()) return nullptr;
    if (s_script_source_cache) return s_script_source_cache;

    LruCacheConfig cfg = {};
    cfg.max_entries = 128;
    cfg.max_bytes = script_source_cache_limit_bytes();
    cfg.on_evict = script_source_cache_entry_free;
    s_script_source_cache = lru_cache_new(&cfg);
    if (!s_script_source_cache) {
        log_error("script_runner_cache: failed to initialize external script source cache");
    }
    return s_script_source_cache;
}

static bool script_source_file_metadata(const char* path, time_t* out_mtime, uint64_t* out_size) {
    if (out_mtime) *out_mtime = 0;
    if (out_size) *out_size = 0;
    if (!path || !path[0]) return false;

    struct stat sb;
    if (stat(path, &sb) != 0 || !S_ISREG(sb.st_mode)) {
        return false;
    }
    if (out_mtime) *out_mtime = sb.st_mtime;
    if (out_size) *out_size = (uint64_t)sb.st_size;
    return true;
}

static bool script_source_cache_entry_valid(const char* resolved_path, bool is_http,
                                            JsScriptSourceCacheEntry* entry) {
    if (!entry) return false;
    if (entry->is_http || is_http) {
        return entry->is_http == is_http;
    }

    time_t mtime = 0;
    uint64_t file_size = 0;
    if (!script_source_file_metadata(resolved_path, &mtime, &file_size)) {
        return false;
    }
    return entry->mtime == mtime && entry->file_size == file_size;
}

static char* script_source_cache_lookup(const char* resolved_path, bool is_http,
                                        JsScriptTaskCollection* collection) {
    LruCache* cache = script_source_cache_get();
    if (!cache || !resolved_path || !resolved_path[0]) return nullptr;

    JsScriptSourceCacheEntry* entry = (JsScriptSourceCacheEntry*)lru_cache_get(cache, resolved_path);
    if (!entry) {
        if (collection) collection->source_cache_misses++;
        return nullptr;
    }
    if (!script_source_cache_entry_valid(resolved_path, is_http, entry)) {
        if (collection) collection->source_cache_stale++;
        lru_cache_delete(cache, resolved_path);
        return nullptr;
    }

    char* source = mem_strndup(entry->source ? entry->source : "", entry->source_len, MEM_CAT_JS_RUNTIME);
    if (!source) return nullptr;
    if (collection) collection->source_cache_hits++;
    log_debug("script_runner_cache: external script source cache hit: %s (%zu bytes)",
              resolved_path, entry->source_len);
    return source;
}

static void script_source_cache_store(const char* resolved_path, bool is_http,
                                      const char* source, size_t source_len) {
    LruCache* cache = script_source_cache_get();
    if (!cache || !resolved_path || !resolved_path[0] || !source) return;

    JsScriptSourceCacheEntry* entry = (JsScriptSourceCacheEntry*)mem_calloc(
        1, sizeof(JsScriptSourceCacheEntry), MEM_CAT_JS_RUNTIME);
    if (!entry) return;
    entry->source = mem_strndup(source, source_len, MEM_CAT_JS_RUNTIME);
    if (!entry->source) {
        mem_free(entry);
        return;
    }
    entry->source_len = source_len;
    entry->is_http = is_http;

    if (!is_http && !script_source_file_metadata(resolved_path, &entry->mtime, &entry->file_size)) {
        script_source_cache_entry_free(resolved_path, entry, 0, nullptr);
        return;
    }

    size_t cache_bytes = sizeof(JsScriptSourceCacheEntry) + source_len + 1;
    if (!lru_cache_put(cache, resolved_path, entry, cache_bytes)) {
        script_source_cache_entry_free(resolved_path, entry, cache_bytes, nullptr);
        log_error("script_runner_cache: failed to store external script source: %s", resolved_path);
        return;
    }
    log_debug("script_runner_cache: stored external script source: %s (%zu bytes)",
              resolved_path, source_len);
}

static char* load_script_content(const char* resolved_path, bool is_http) {
    char* content = nullptr;
    if (!is_http && resolved_path && strcmp(resolved_path, "builtin:wpt-testharness.js") == 0) {
        // layout references do not serve WPT testharness.js, so API-test scripts
        // stop at the first harness call instead of leaking assertion-only DOM.
        return mem_strdup("", MEM_CAT_JS_RUNTIME);
    }
    if (!is_http && resolved_path && strcmp(resolved_path, "builtin:wpt-testharnessreport.js") == 0) {
        return mem_strdup("", MEM_CAT_JS_RUNTIME);
    }
    if (!is_http && resolved_path && strcmp(resolved_path, "builtin:wpt-rendering-utils.js") == 0) {
        // Rendering-utils waits stage paint-invalidation mutations; layout JSON baselines capture geometry before those paint-only callbacks run.
        return mem_strdup(
            "(function(){\n"
            "function pending(){ return { then:function(){ return this; }, catch:function(){ return this; }, finally:function(){ return this; } }; }\n"
            "window.waitForAtLeastOneFrame = function(){ return pending(); };\n"
            "window.waitForAnimationFrames = function(){ return pending(); };\n"
            "})();\n",
            MEM_CAT_JS_RUNTIME);
    }
    if (!is_http && resolved_path && strcmp(resolved_path, "builtin:wpt-interpolation-testcommon.js") == 0) {
        // Interpolation helpers build assertion fixtures; layout JSON baselines compare the page before those harness-only nodes exist.
        return mem_strdup(
            "(function(){\n"
            "function noop(){}\n"
            "window.test_interpolation = noop;\n"
            "window.test_no_interpolation = noop;\n"
            "window.test_composition = noop;\n"
            "})();\n",
            MEM_CAT_JS_RUNTIME);
    }
    if (!is_http && resolved_path && strcmp(resolved_path, "builtin:wpt-testdriver.js") == 0) {
        return mem_strdup(
            "(function(){\n"
            "function resolved(){ return { then:function(resolve){ if (resolve) resolve(); return this; }, catch:function(){ return this; } }; }\n"
            "function Actions(){ this.pointerMove=function(){return this;}; this.pointerDown=function(){return this;}; this.pointerUp=function(){return this;}; this.keyDown=function(){return this;}; this.keyUp=function(){return this;}; this.send=function(){return resolved();}; }\n"
            "window.test_driver = window.test_driver || { send_keys:function(){return resolved();}, click:function(){return resolved();}, bless:function(name, fn){ if (fn) fn(); return resolved(); }, Actions:Actions };\n"
            "})();\n",
            MEM_CAT_JS_RUNTIME);
    }
    if (!is_http && resolved_path && strcmp(resolved_path, "builtin:wpt-testdriver-vendor.js") == 0) {
        return mem_strdup("", MEM_CAT_JS_RUNTIME);
    }
    if (is_http) {
        size_t content_size = 0;
        content = download_http_content_cached(resolved_path, &content_size, "./temp/cache");
        if (content) {
            log_debug("script_runner: downloaded external script from URL: %s (%zu bytes)", resolved_path, content_size);
        } else {
            log_warn("script_runner: optional external script unavailable: %s", resolved_path);
        }
    } else {
        content = read_text_file(resolved_path);
        if (content) {
            log_debug("script_runner: loaded external script from file: %s (%zu bytes)", resolved_path, strlen(content));
        } else {
            log_warn("script_runner: optional external script file unavailable: %s", resolved_path);
        }
    }
    return content;
}

static char* load_script_content_with_source_cache(const char* resolved_path, bool is_http,
                                                  JsScriptTaskCollection* collection,
                                                  bool* out_cache_hit) {
    if (out_cache_hit) *out_cache_hit = false;

#ifndef NDEBUG
    bool timing_enabled = script_task_timing_enabled();
    long load_start_us = timing_enabled ? script_runner_wall_now_us() : 0;
#endif
    char* cached = script_source_cache_lookup(resolved_path, is_http, collection);
    if (cached) {
        if (out_cache_hit) *out_cache_hit = true;
#ifndef NDEBUG
        if (timing_enabled) {
            log_notice("script_runner_timing: phase=source-load kind=%s status=source-cache-hit wall_us=%ld bytes=%zu src=%s",
                       is_http ? "remote" : "local",
                       script_runner_wall_now_us() - load_start_us,
                       strlen(cached),
                       resolved_path ? resolved_path : "<null>");
        }
#endif
        return cached;
    }

    char* content = load_script_content(resolved_path, is_http);
    if (content) {
        script_source_cache_store(resolved_path, is_http, content, strlen(content));
    }
#ifndef NDEBUG
    if (timing_enabled) {
        log_notice("script_runner_timing: phase=source-load kind=%s status=%s wall_us=%ld bytes=%zu src=%s",
                   is_http ? "remote" : "local",
                   content ? "loaded" : "load-failed",
                   script_runner_wall_now_us() - load_start_us,
                   content ? strlen(content) : 0,
                   resolved_path ? resolved_path : "<null>");
    }
#endif
    return content;
}

#ifndef NDEBUG
static const char* script_task_kind_name(JsScriptTaskKind kind) {
    switch (kind) {
        case JS_SCRIPT_TASK_CLASSIC: return "classic";
        case JS_SCRIPT_TASK_MODULE: return "module";
        case JS_SCRIPT_TASK_BODY_ONLOAD: return "body-onload";
        default: return "unknown";
    }
}

static const char* script_task_scheduling_name(JsScriptTaskScheduling scheduling) {
    switch (scheduling) {
        case JS_SCRIPT_SCHED_POST_DOM: return "post-dom";
        case JS_SCRIPT_SCHED_ASYNC: return "async";
        case JS_SCRIPT_SCHED_DEFER: return "defer";
        case JS_SCRIPT_SCHED_AFTER_SCRIPTS: return "after-scripts";
        default: return "unknown";
    }
}

static const char* script_compile_policy_name(JsScriptCompilePolicy policy) {
    switch (policy) {
        case JS_SCRIPT_COMPILE_INLINE_IMMEDIATE: return "inline-immediate";
        case JS_SCRIPT_COMPILE_EXTERNAL_SEPARATE: return "external-separate";
        case JS_SCRIPT_COMPILE_EXTERNAL_SOURCE_CACHED: return "external-source-cached";
        case JS_SCRIPT_COMPILE_MODULE_SEPARATE: return "module-separate";
        case JS_SCRIPT_COMPILE_HANDLER_EAGER: return "handler-eager";
        default: return "unknown";
    }
}

static const char* script_task_status_name(JsScriptTaskStatus status) {
    switch (status) {
        case JS_SCRIPT_TASK_READY: return "ready";
        case JS_SCRIPT_TASK_SKIPPED_UNSUPPORTED_TYPE: return "skipped-unsupported-type";
        case JS_SCRIPT_TASK_SKIPPED_EXTERNAL_DISABLED: return "skipped-external-disabled";
        case JS_SCRIPT_TASK_SKIPPED_MODULE_UNSUPPORTED: return "skipped-module-unsupported";
        case JS_SCRIPT_TASK_SKIPPED_NOMODULE: return "skipped-nomodule";
        case JS_SCRIPT_TASK_SKIPPED_LARGE_DEFER: return "skipped-large-defer";
        case JS_SCRIPT_TASK_SKIPPED_TESTHARNESS_INLINE: return "skipped-testharness-inline";
        case JS_SCRIPT_TASK_LOAD_FAILED: return "load-failed";
        default: return "unknown";
    }
}

static bool script_task_diagnostics_enabled() {
    const char* env = getenv("RADIANT_JS_TASK_DIAGNOSTICS");
    return env && env[0] && strcmp(env, "0") != 0;
}

static bool script_task_timing_enabled() {
    const char* env = getenv("RADIANT_JS_TASK_TIMING");
    return env && env[0] && strcmp(env, "0") != 0;
}
#endif

static const size_t JS_EXTERNAL_SCRIPT_BUDGET_BYTES = 16u * 1024u * 1024u;
static const size_t JS_TOTAL_SCRIPT_BUDGET_BYTES = 128u * 1024u * 1024u;

static size_t script_byte_limit_from_env(const char* name, size_t fallback) {
    const char* env = getenv(name);
    if (env && env[0]) {
        char* end = nullptr;
        long parsed = strtol(env, &end, 10);
        if (end != env) {
            if (parsed <= 0) return (size_t)-1;
            return (size_t)parsed;
        }
    }
    return fallback;
}

static size_t script_prelayout_defer_limit_bytes() {
    return script_byte_limit_from_env(
        "RADIANT_JS_PRELAYOUT_DEFER_BYTES", JS_EXTERNAL_SCRIPT_BUDGET_BYTES);
}

static size_t script_external_compile_limit_bytes() {
    // browsers do not enforce small source-byte caps; this guard only protects
    // Radiant from pathological fixture input while allowing real libraries.
    return script_byte_limit_from_env(
        "RADIANT_JS_EXTERNAL_SCRIPT_BYTES", JS_EXTERNAL_SCRIPT_BUDGET_BYTES);
}

static size_t script_total_compile_limit_bytes() {
    // page script graphs can include many files, so total budget must be much
    // larger than the per-file guard to stay browser-compatible.
    return script_byte_limit_from_env(
        "RADIANT_JS_TOTAL_SCRIPT_BYTES", JS_TOTAL_SCRIPT_BUDGET_BYTES);
}

#ifndef NDEBUG
static long script_runner_wall_now_us() {
#ifndef _WIN32
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000000L + (long)tv.tv_usec;
#else
    return 0;
#endif
}

static long script_runner_timing_total_us(const JsMirPhaseTiming* timing) {
    if (!timing) return 0;
    return timing->parse_us + timing->ast_us + timing->early_us +
        timing->imports_us + timing->mir_us + timing->link_us +
        timing->execute_us + timing->cleanup_us;
}
#endif  // !NDEBUG

static bool element_has_attr_ci(Element* elem, const char* attr_name) {
    if (!elem || !attr_name || !attr_name[0]) return false;

    char lower_name[128];
    size_t i = 0;
    for (; attr_name[i] && i < sizeof(lower_name) - 1; i++) {
        char c = attr_name[i];
        lower_name[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 0x20) : c;
    }
    lower_name[i] = '\0';

    if (elem->has_attr(lower_name)) return true;
    return elem->has_attr(attr_name);
}

static bool script_runner_module_scripts_enabled() {
    // DOM3 ships the bounded browser module pipeline. Keeping this behind an
    // environment switch made identical documents execute differently across
    // hosts and prevented ordinary `<script type="module">` from being a DOM
    // capability at all.
    return true;
}

static JsScriptTask* script_task_new(JsScriptTaskKind kind, int document_order) {
    JsScriptTask* task = (JsScriptTask*)mem_calloc(1, sizeof(JsScriptTask), MEM_CAT_JS_RUNTIME);
    if (!task) return nullptr;
    task->kind = kind;
    task->scheduling = (kind == JS_SCRIPT_TASK_BODY_ONLOAD)
        ? JS_SCRIPT_SCHED_AFTER_SCRIPTS
        : JS_SCRIPT_SCHED_POST_DOM;
    task->compile_policy = (kind == JS_SCRIPT_TASK_BODY_ONLOAD)
        ? JS_SCRIPT_COMPILE_HANDLER_EAGER
        : JS_SCRIPT_COMPILE_INLINE_IMMEDIATE;
    task->status = JS_SCRIPT_TASK_READY;
    task->parser_inserted = true;
    task->document_order = document_order;
    return task;
}

static void script_task_free(JsScriptTask* task) {
    if (!task) return;
    if (task->type_attr) mem_free(task->type_attr);
    if (task->src_attr) mem_free(task->src_attr);
    if (task->resolved_url) mem_free(task->resolved_url);
    if (task->source) mem_free(task->source);
    mem_free(task);
}

static bool script_task_list_append(ArrayList* list, JsScriptTask* task) {
    if (!list || !task) return false;
    if (!arraylist_append(list, task)) {
        script_task_free(task);
        return false;
    }
    return true;
}

static void script_task_list_free(ArrayList* list) {
    if (!list) return;
    for (int i = 0; i < list->length; i++) {
        script_task_free((JsScriptTask*)arraylist_get(list, i));
    }
    arraylist_free(list);
}

static bool script_task_collection_init(JsScriptTaskCollection* collection) {
    if (!collection) return false;
    memset(collection, 0, sizeof(JsScriptTaskCollection));
    collection->scripts = arraylist_new(8);
    collection->onload_handlers = arraylist_new(1);
    collection->generated_sources = arraylist_new(4);
    if (!collection->scripts || !collection->onload_handlers || !collection->generated_sources) {
        script_task_list_free(collection->scripts);
        script_task_list_free(collection->onload_handlers);
        if (collection->generated_sources) arraylist_free(collection->generated_sources);
        memset(collection, 0, sizeof(JsScriptTaskCollection));
        return false;
    }
    return true;
}

static bool script_scheduler_queues_init(JsScriptSchedulerQueues* queues) {
    if (!queues) return false;
    memset(queues, 0, sizeof(JsScriptSchedulerQueues));
    queues->post_dom = arraylist_new(8);
    queues->async_ready = arraylist_new(4);
    queues->defer = arraylist_new(4);
    if (!queues->post_dom || !queues->async_ready || !queues->defer) {
        if (queues->post_dom) arraylist_free(queues->post_dom);
        if (queues->async_ready) arraylist_free(queues->async_ready);
        if (queues->defer) arraylist_free(queues->defer);
        memset(queues, 0, sizeof(JsScriptSchedulerQueues));
        return false;
    }
    return true;
}

static void script_scheduler_queues_free(JsScriptSchedulerQueues* queues) {
    if (!queues) return;
    if (queues->post_dom) arraylist_free(queues->post_dom);
    if (queues->async_ready) arraylist_free(queues->async_ready);
    if (queues->defer) arraylist_free(queues->defer);
    memset(queues, 0, sizeof(JsScriptSchedulerQueues));
}

static void script_task_collection_free(JsScriptTaskCollection* collection) {
    if (!collection) return;
    script_task_list_free(collection->scripts);
    script_task_list_free(collection->onload_handlers);
    if (collection->generated_sources) {
        for (int i = 0; i < collection->generated_sources->length; i++) {
            strbuf_free((StrBuf*)arraylist_get(collection->generated_sources, i));
        }
        arraylist_free(collection->generated_sources);
    }
    memset(collection, 0, sizeof(JsScriptTaskCollection));
}

static bool script_task_is_executable_classic(JsScriptTask* task) {
    return task && task->status == JS_SCRIPT_TASK_READY &&
        task->kind == JS_SCRIPT_TASK_CLASSIC && task->ready_to_execute;
}

static bool script_task_is_executable_module(JsScriptTask* task) {
    return task && task->status == JS_SCRIPT_TASK_READY &&
        task->kind == JS_SCRIPT_TASK_MODULE && task->ready_to_execute;
}

static bool script_task_is_executable(JsScriptTask* task) {
    return script_task_is_executable_classic(task) || script_task_is_executable_module(task);
}

static void script_task_mark_ready(JsScriptTaskCollection* collection, JsScriptTask* task) {
    if (!task || task->status != JS_SCRIPT_TASK_READY) return;

    task->ready_to_execute = true;
    if (task->scheduling == JS_SCRIPT_SCHED_ASYNC) {
        if (collection) collection->async_ready_scripts++;
    } else if (task->scheduling == JS_SCRIPT_SCHED_DEFER) {
        if (collection) collection->defer_scripts++;
    }
    if (task->load_blocking && collection) {
        collection->load_blocking_scripts++;
    }
}

static char* script_source_from_strbuf(StrBuf* buf) {
    if (!buf || !buf->str || buf->length == 0) {
        return mem_strdup("", MEM_CAT_JS_RUNTIME);
    }
    return mem_strndup(buf->str, buf->length, MEM_CAT_JS_RUNTIME);
}

static bool script_task_collection_retain_strbuf(JsScriptTaskCollection* collection,
                                                 StrBuf* source) {
    if (!collection || !collection->generated_sources || !source) return false;
    return arraylist_append(collection->generated_sources, source);
}

static bool is_js_identifier_start_char(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
        ch == '_' || ch == '$';
}

static bool is_js_identifier_part_char(char ch) {
    return is_js_identifier_start_char(ch) || (ch >= '0' && ch <= '9');
}

static const char* skip_js_ascii_space(const char* p) {
    while (p && (*p == ' ' || *p == '\t' || *p == '\n' ||
                 *p == '\r' || *p == '\f' || *p == '\v')) {
        p++;
    }
    return p;
}

static bool is_js_identifier_boundary(char ch) {
    return ch == '\0' || !is_js_identifier_part_char(ch);
}

static void append_classic_script_window_function_exports(const char* source, StrBuf* script_buf) {
    if (!source || !script_buf) return;

    StrBuf* export_buf = strbuf_new_cap(256);
    const char* cursor = source;
    while ((cursor = strstr(cursor, "function")) != nullptr) {
        char before = cursor == source ? '\0' : cursor[-1];
        if (!is_js_identifier_boundary(before)) {
            cursor += 8;
            continue;
        }
        const char* p = cursor + 8;
        if (!is_js_identifier_boundary(*p) && *p != '*' &&
            *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            cursor += 8;
            continue;
        }
        p = skip_js_ascii_space(p);
        if (*p == '*') {
            p++;
            p = skip_js_ascii_space(p);
        }
        if (!is_js_identifier_start_char(*p)) {
            cursor += 8;
            continue;
        }
        const char* name_start = p;
        p++;
        while (is_js_identifier_part_char(*p)) p++;
        const char* after_name = skip_js_ascii_space(p);
        if (!after_name || *after_name != '(') {
            cursor = p;
            continue;
        }

        strbuf_append_str(export_buf, "\nif (typeof ");
        strbuf_append_str_n(export_buf, name_start, (size_t)(p - name_start));
        strbuf_append_str(export_buf, " === 'function') { window.");
        strbuf_append_str_n(export_buf, name_start, (size_t)(p - name_start));
        strbuf_append_str(export_buf, " = ");
        strbuf_append_str_n(export_buf, name_start, (size_t)(p - name_start));
        strbuf_append_str(export_buf, "; }\n");
        cursor = p;
    }

    if (export_buf->length > 0) {
        strbuf_append_str_n(script_buf, export_buf->str, export_buf->length);
    }
    strbuf_free(export_buf);
}

static void append_body_onload_source(const char* onload, StrBuf* onload_buf) {
    if (!onload || !onload[0] || !onload_buf) return;

    // The MIR transpiler cannot eval() strings, so the common pattern
    // setTimeout('code()', delay) must be transformed: extract the string
    // content and emit it as direct code. Function-reference forms keep their
    // source unchanged because the preamble timer stubs call them directly.
    const char* st = strstr(onload, "setTimeout(");
    if (!st) st = strstr(onload, "setInterval(");
    if (st) {
        int skip_len = (st[3] == 'T' || st[3] == 't') ? 11 : 12; // "setTimeout(" vs "setInterval("
        const char* p = st + skip_len;
        if (*p == '\'' || *p == '"') {
            char quote = *p++;
            const char* code_start = p;
            while (*p && *p != quote) p++;
            if (*p == quote) {
                strbuf_append_str_n(onload_buf, code_start, (size_t)(p - code_start));
                strbuf_append_str(onload_buf, "\n");
                return;
            }
        }

        // onload="setTimeout(test, 0)" depends on resolving a prior classic
        // script's top-level function as a later task callback. Static layout
        // drains zero-delay onload timers before capture, so emit a direct call.
        p = skip_js_ascii_space(p);
        if (p && is_js_identifier_start_char(*p)) {
            const char* name_start = p;
            p++;
            while (is_js_identifier_part_char(*p)) p++;
            const char* after_name = skip_js_ascii_space(p);
            if (after_name && *after_name == ',') {
                strbuf_append_str(onload_buf, "if (typeof window !== 'undefined' && typeof window.");
                strbuf_append_str_n(onload_buf, name_start, (size_t)(p - name_start));
                strbuf_append_str(onload_buf, " === 'function') { window.");
                strbuf_append_str_n(onload_buf, name_start, (size_t)(p - name_start));
                strbuf_append_str(onload_buf, "(); } else if (typeof ");
                strbuf_append_str_n(onload_buf, name_start, (size_t)(p - name_start));
                strbuf_append_str(onload_buf, " === 'function') { ");
                strbuf_append_str_n(onload_buf, name_start, (size_t)(p - name_start));
                strbuf_append_str(onload_buf, "(); }\n");
                return;
            }
        }
    }

    strbuf_append_str(onload_buf, onload);
    strbuf_append_str(onload_buf, "\n");
}

static bool is_supported_classic_script_type(const char* type_attr) {
    if (!type_attr || !type_attr[0]) return true;
    return strcasecmp(type_attr, "text/javascript") == 0 ||
           strcasecmp(type_attr, "application/javascript") == 0 ||
           strcasecmp(type_attr, "text/ecmascript") == 0 ||
           strcasecmp(type_attr, "application/ecmascript") == 0;
}

static bool is_module_script_type(const char* type_attr) {
    return type_attr && strcasecmp(type_attr, "module") == 0;
}

static void emit_body_onload_source(StrBuf* script_buf, ArrayList* onload_tasks) {
    if (!script_buf || !onload_tasks || onload_tasks->length == 0) return;

    for (int i = 0; i < onload_tasks->length; i++) {
        JsScriptTask* task = (JsScriptTask*)arraylist_get(onload_tasks, i);
        if (!task || task->status != JS_SCRIPT_TASK_READY ||
            task->kind != JS_SCRIPT_TASK_BODY_ONLOAD) {
            continue;
        }
        if (task->source && task->source_len > 0) {
            strbuf_append_str_n(script_buf, task->source, task->source_len);
            strbuf_append_char(script_buf, '\n');
        }
    }
}

static void append_browser_document_preamble(StrBuf* script_buf) {
    if (!script_buf) return;
    strbuf_append_str(script_buf,
        "var window = globalThis;\n"
        "var jQuery = undefined;\n"
        "var $ = undefined;\n"
        // PointerEvent is installed natively; advertise the matching touch
        // capability so libraries select their pointer branch in headless UI.
        "var navigator = {\n"
        "  userAgent: 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:139.0) Gecko/20100101 Firefox/139.0',\n"
        // Navigator.appVersion is legacy but still synchronously string-sniffed
        // by established UI libraries such as noUiSlider.
        "  appVersion: '5.0 (Macintosh; Intel Mac OS X 10.15; rv:139.0) Gecko/20100101 Firefox/139.0',\n"
        "  vendor: '', platform: 'MacIntel', language: 'en-US',\n"
        "  languages: ['en-US', 'en'], maxTouchPoints: 1\n"
        "};\n"
        "navigator.sendBeacon = navigator.sendBeacon || function(){ return false; };\n"
        "window.navigator = navigator;\n"
        "window.document = document;\n"
        "document.hidden = false;\n"
        "document.prerendering = false;\n"
        "document.visibilityState = 'visible';\n"
        "document.fonts = document.fonts || {};\n"
        // FontFaceSet.load returns a Promise, but it must remain pending until
        // the native font-resource phase can truthfully report completion.
        "document.fonts.load = document.fonts.load || function(){ return new Promise(function(){}); };\n"
        "function FontFace(family, source, descriptors) {\n"
        "  if (!(this instanceof FontFace)) throw new TypeError('FontFace constructor requires new');\n"
        "  descriptors = descriptors || {};\n"
        "  this.family = String(family);\n"
        "  this.style = descriptors.style || 'normal';\n"
        "  this.weight = descriptors.weight || 'normal';\n"
        "  this.stretch = descriptors.stretch || 'normal';\n"
        "  this.unicodeRange = descriptors.unicodeRange || 'U+0-10FFFF';\n"
        "  this.variant = descriptors.variant || 'normal';\n"
        "  this.featureSettings = descriptors.featureSettings || 'normal';\n"
        "  this.variationSettings = descriptors.variationSettings || 'normal';\n"
        "  this.display = descriptors.display || 'auto';\n"
        "  this.status = 'unloaded';\n"
        "  var face = this;\n"
        "  this.loaded = new Promise(function(resolve, reject) { face.__fontResolve = resolve; face.__fontReject = reject; });\n"
        "}\n"
        "FontFace.prototype.load = function(){ if (this.status === 'unloaded') this.status = 'loading'; return this.loaded; };\n"
        "window.FontFace = FontFace;\n"
        "window.setTimeout = setTimeout;\n"
        "window.setInterval = setInterval;\n"
        "window.clearTimeout = clearTimeout;\n"
        "window.clearInterval = clearInterval;\n"
        "window.requestAnimationFrame = requestAnimationFrame;\n"
        "window.cancelAnimationFrame = cancelAnimationFrame;\n"
        "var screen = undefined;\n"
        "window.screen = screen;\n"
        "function WebSocket(url) { this.send = function(){}; this.close = function(){}; this.addEventListener = function(){}; this.readyState = 3; }\n"
        "function Worker(url) { this.postMessage = function(){}; this.terminate = function(){}; this.addEventListener = function(){}; }\n"
        "window.WebSocket = WebSocket;\n"
        "window.Worker = Worker;\n"
        "// Keep native window EventTarget methods: aliasing them to document splits listener storage from native window dispatch.\n"
        "// getComputedStyle is installed natively; wrapping it here recurses through global lookup.\n"
        "window.scrollTo = function(x, y) {\n"
        "  if (typeof x === 'object' && x !== null) {\n"
        "    y = x.top;\n"
        "    x = x.left;\n"
        "  }\n"
        "  if (typeof x !== 'number' || x !== x) x = 0;\n"
        "  if (typeof y !== 'number' || y !== y) y = 0;\n"
        "  if (x < 0) x = 0;\n"
        "  if (y < 0) y = 0;\n"
        "  window.pageXOffset = x;\n"
        "  window.pageYOffset = y;\n"
        "  window.scrollX = x;\n"
        "  window.scrollY = y;\n"
        "  if (document.documentElement) {\n"
        "    document.documentElement.scrollLeft = x;\n"
        "    document.documentElement.scrollTop = y;\n"
        "  }\n"
        "  if (document.body) {\n"
        "    document.body.scrollLeft = x;\n"
        "    document.body.scrollTop = y;\n"
        "  }\n"
        "  window.dispatchEvent(new Event('scroll'));\n"
        "};\n"
        "// browser pages use window.scroll as an alias for scrollTo; keep the\n"
        "// global callable present so unsupported timing only queues scroll state.\n"
        "window.scroll = window.scrollTo;\n"
        "var scroll = window.scroll;\n"
        "window.scrollBy = function(x, y) {\n"
        "  if (typeof x === 'object' && x !== null) {\n"
        "    y = x.top;\n"
        "    x = x.left;\n"
        "  }\n"
        "  if (typeof x !== 'number' || x !== x) x = 0;\n"
        "  if (typeof y !== 'number' || y !== y) y = 0;\n"
        "  window.scrollTo(window.pageXOffset + x, window.pageYOffset + y);\n"
        "};\n"
        "window.innerWidth = undefined;\n"
        "window.innerHeight = undefined;\n"
        "window.outerWidth = undefined;\n"
        "window.outerHeight = undefined;\n"
        "window.devicePixelRatio = undefined;\n"
        "window.pageXOffset = undefined;\n"
        "window.pageYOffset = undefined;\n"
        "window.scrollX = undefined;\n"
        "window.scrollY = undefined;\n"
        "document.defaultView = window;\n"
    );
}

static bool script_task_collection_has_executable_tasks(JsScriptTaskCollection* collection) {
    if (!collection) return false;
    for (int i = 0; i < collection->scripts->length; i++) {
        JsScriptTask* task = (JsScriptTask*)arraylist_get(collection->scripts, i);
        if (script_task_is_executable(task)) {
            return true;
        }
    }
    for (int i = 0; i < collection->onload_handlers->length; i++) {
        JsScriptTask* task = (JsScriptTask*)arraylist_get(collection->onload_handlers, i);
        if (task && task->status == JS_SCRIPT_TASK_READY &&
            task->kind == JS_SCRIPT_TASK_BODY_ONLOAD && task->source_len > 0) {
            return true;
        }
    }
    return false;
}

static size_t script_task_collection_source_bytes(JsScriptTaskCollection* collection) {
    if (!collection) return 0;
    size_t bytes = collection->inline_source_bytes + collection->external_source_bytes +
        collection->onload_source_bytes + 4096;
    bytes += (size_t)collection->scripts->length * 128;
    bytes += (size_t)collection->onload_handlers->length * 64;
    return bytes;
}

static int loaded_external_scripts = 0;
static int failed_external_scripts = 0;

static void log_script_task_diagnostics(JsScriptTaskCollection* collection) {
    if (!collection) return;

    log_debug("script_runner_tasks: scripts=%d inline=%d external=%d loaded=%d failed=%d skipped=%d onload=%d async_ready=%d defer=%d load_blocking=%d source_cache=[hits:%d misses:%d stale:%d] bytes inline=%zu external=%zu onload=%zu",
        collection->total_script_elements, collection->inline_scripts,
        collection->external_scripts, loaded_external_scripts,
        failed_external_scripts, collection->skipped_scripts,
        collection->onload_handlers_count, collection->async_ready_scripts,
        collection->defer_scripts, collection->load_blocking_scripts,
        collection->source_cache_hits, collection->source_cache_misses,
        collection->source_cache_stale,
        collection->inline_source_bytes,
        collection->external_source_bytes, collection->onload_source_bytes);

#ifndef NDEBUG
    if (!script_task_diagnostics_enabled()) return;

    for (int i = 0; i < collection->scripts->length; i++) {
        JsScriptTask* task = (JsScriptTask*)arraylist_get(collection->scripts, i);
        log_info("script_runner_task: #%d kind=%s scheduling=%s compile=%s status=%s external=%d source_cache_hit=%d ready=%d load_blocking=%d attrs=[async:%d defer:%d nomodule:%d] src=%s bytes=%zu",
            task ? task->document_order : -1,
            task ? script_task_kind_name(task->kind) : "null",
            task ? script_task_scheduling_name(task->scheduling) : "null",
            task ? script_compile_policy_name(task->compile_policy) : "null",
            task ? script_task_status_name(task->status) : "null",
            task && task->external ? 1 : 0,
            task && task->source_cache_hit ? 1 : 0,
            task && task->ready_to_execute ? 1 : 0,
            task && task->load_blocking ? 1 : 0,
            task && task->async_attr ? 1 : 0,
            task && task->defer_attr ? 1 : 0,
            task && task->nomodule_attr ? 1 : 0,
            task && task->resolved_url ? task->resolved_url :
                (task && task->src_attr ? task->src_attr : "<inline>"),
            task ? task->source_len : 0);
    }

    for (int i = 0; i < collection->onload_handlers->length; i++) {
        JsScriptTask* task = (JsScriptTask*)arraylist_get(collection->onload_handlers, i);
        log_info("script_runner_task: #%d kind=%s compile=%s status=%s external=0 src=<body onload> bytes=%zu",
            task ? task->document_order : -1,
            task ? script_task_kind_name(task->kind) : "null",
            task ? script_compile_policy_name(task->compile_policy) : "null",
            task ? script_task_status_name(task->status) : "null",
            task ? task->source_len : 0);
    }
#endif
}

/**
 * Recursively walk the Element* tree, collecting <script> source text
 * (both inline and external) and the body onload handler in document order.
 */
static void collect_scripts_recursive(Element* elem, JsScriptTaskCollection* collection, Url* base_url, int depth) {
    if (!elem) return;

    // guard against stack overflow from deeply nested DOM (fuzzer-found): this
    // pre-order walk runs before layout and uses the native stack per level.
    // Cap at the same depth as layout — content deeper than this is not laid
    // out anyway, so scripts there would never run.
    if (depth >= MAX_LAYOUT_DEPTH) {
        log_error("collect_scripts_recursive: max depth %d exceeded, skipping deeper nodes",
                  MAX_LAYOUT_DEPTH);
        return;
    }

    // check for <body onload="...">
    if (is_body_element(elem)) {
        const char* onload = extract_element_attribute(elem, "onload", nullptr);
        if (onload && onload[0]) {
            StrBuf* onload_buf = strbuf_new_cap(256);
            append_body_onload_source(onload, onload_buf);
            JsScriptTask* task = script_task_new(JS_SCRIPT_TASK_BODY_ONLOAD,
                collection->total_script_elements + collection->onload_handlers_count);
            if (task) {
                task->source = script_source_from_strbuf(onload_buf);
                task->source_len = onload_buf->length;
                collection->onload_handlers_count++;
                collection->onload_source_bytes += task->source_len;
                script_task_list_append(collection->onload_handlers, task);
            }
            strbuf_free(onload_buf);
        }
    }

    // check for <script> elements
    if (is_script_element(elem)) {
        int document_order = collection->total_script_elements++;
        JsScriptTask* task = script_task_new(JS_SCRIPT_TASK_CLASSIC, document_order);
        if (!task) return;
        task->script_element = elem;

        const char* type_attr = extract_element_attribute(elem, "type", nullptr);
        if (type_attr && type_attr[0]) {
            task->type_attr = mem_strdup(type_attr, MEM_CAT_JS_RUNTIME);
        }
        task->async_attr = element_has_attr_ci(elem, "async");
        task->defer_attr = element_has_attr_ci(elem, "defer");
        task->nomodule_attr = element_has_attr_ci(elem, "nomodule");
        bool module_pipeline_enabled = script_runner_module_scripts_enabled();
        bool module_script = is_module_script_type(type_attr);
        if (module_script) {
            task->kind = JS_SCRIPT_TASK_MODULE;
            task->scheduling = task->async_attr ? JS_SCRIPT_SCHED_ASYNC : JS_SCRIPT_SCHED_DEFER;
            task->compile_policy = JS_SCRIPT_COMPILE_MODULE_SEPARATE;
            task->load_blocking = true;
            if (!module_pipeline_enabled) {
                task->status = JS_SCRIPT_TASK_SKIPPED_MODULE_UNSUPPORTED;
                collection->skipped_scripts++;
                script_task_list_append(collection->scripts, task);
                return;
            }
        }
        if (!module_script && module_pipeline_enabled && task->nomodule_attr) {
            task->status = JS_SCRIPT_TASK_SKIPPED_NOMODULE;
            collection->skipped_scripts++;
            script_task_list_append(collection->scripts, task);
            return;
        }
        if (!module_script && !is_supported_classic_script_type(type_attr)) {
            task->status = JS_SCRIPT_TASK_SKIPPED_UNSUPPORTED_TYPE;
            collection->skipped_scripts++;
            script_task_list_append(collection->scripts, task);
            return;
        }

        const char* src_attr = extract_element_attribute(elem, "src", nullptr);
        if (src_attr && src_attr[0]) {
            task->external = true;
            task->src_attr = mem_strdup(src_attr, MEM_CAT_JS_RUNTIME);
            task->load_blocking = true;
            if (task->async_attr) {
                task->scheduling = JS_SCRIPT_SCHED_ASYNC;
            } else if (task->defer_attr) {
                task->scheduling = JS_SCRIPT_SCHED_DEFER;
            }
            task->compile_policy = task->kind == JS_SCRIPT_TASK_MODULE
                ? JS_SCRIPT_COMPILE_MODULE_SEPARATE
                : JS_SCRIPT_COMPILE_EXTERNAL_SEPARATE;
            collection->external_scripts++;
            if (!s_execute_external_scripts) {
                task->status = JS_SCRIPT_TASK_SKIPPED_EXTERNAL_DISABLED;
                collection->skipped_scripts++;
                script_task_list_append(collection->scripts, task);
                return;
            }
            bool is_http = false;
            char* resolved = resolve_script_url(src_attr, base_url, &is_http);
            if (!resolved) {
                task->status = JS_SCRIPT_TASK_LOAD_FAILED;
                failed_external_scripts++;
                script_task_list_append(collection->scripts, task);
                return;
            }
            task->resolved_url = resolved;
            if (strcmp(task->resolved_url, "builtin:wpt-testharness.js") == 0) {
                collection->testharness_seen = true;
            }
            bool source_cache_hit = false;
            char* content = load_script_content_with_source_cache(task->resolved_url, is_http,
                                                                  collection, &source_cache_hit);
            if (content) {
                task->source = content;
                task->source_len = strlen(content);
                task->source_cache_hit = source_cache_hit;
                if (source_cache_hit && task->kind == JS_SCRIPT_TASK_CLASSIC) {
                    task->compile_policy = JS_SCRIPT_COMPILE_EXTERNAL_SOURCE_CACHED;
                }
                collection->external_source_bytes += task->source_len;
                script_task_mark_ready(collection, task);
                loaded_external_scripts++;
            } else {
                task->status = JS_SCRIPT_TASK_LOAD_FAILED;
                failed_external_scripts++;
            }
            script_task_list_append(collection->scripts, task);
            return;
        }

        StrBuf* inline_buf = strbuf_new_cap(256);
        extract_script_text(elem, inline_buf);
        // WPT inline scripts often perform visual DOM setup before calling
        // test()/promise_test(); the built-in harness stubs suppress assertion
        // callbacks, but top-level setup must still run for browser parity.
        if (task->kind == JS_SCRIPT_TASK_CLASSIC && inline_buf->str && inline_buf->length > 0) {
            append_classic_script_window_function_exports(inline_buf->str, inline_buf);
        }
        task->source = script_source_from_strbuf(inline_buf);
        task->source_len = inline_buf->length;
        collection->inline_scripts++;
        collection->inline_source_bytes += task->source_len;
        script_task_mark_ready(collection, task);
        strbuf_free(inline_buf);
        script_task_list_append(collection->scripts, task);
        return;  // don't recurse into script children
    }

    // recurse into child elements
    for (int64_t i = 0; i < elem->length; i++) {
        Item child = elem->items[i];
        TypeId tid = get_type_id(child);
        if (tid == LMD_TYPE_ELEMENT) {
            collect_scripts_recursive(child.element, collection, base_url, depth + 1);
        }
    }
}

static const char* script_task_filename(JsScriptTask* task, char* scratch, size_t scratch_len) {
    if (!task) return "<script-task>";
    if (task->resolved_url && task->resolved_url[0]) return task->resolved_url;
    if (task->src_attr && task->src_attr[0]) return task->src_attr;
    if (task->kind == JS_SCRIPT_TASK_BODY_ONLOAD) return "<body-onload>";
    snprintf(scratch, scratch_len, "<inline-script-%d>", task->document_order);
    return scratch;
}

static void script_eval_context_init(Runtime* runtime, EvalContext* eval_context) {
    eval_context->heap = runtime->heap;
    eval_context->name_pool = runtime->name_pool;
    if (!runtime->type_list) runtime->type_list = arraylist_new(64);
    eval_context->type_list = runtime->type_list;
    eval_context->pool = runtime->heap ? runtime->heap->pool : nullptr;
}

static Item execute_js_source_with_preamble(Runtime* runtime, JsPreambleState* preamble,
                                            const char* source, size_t source_len,
                                            const char* filename, bool refresh_snapshot) {
    if (!runtime || !preamble) return ItemError;
    if (!source) {
        source = "";
        source_len = 0;
    }

    EvalContext task_context = {};
    script_eval_context_init(runtime, &task_context);

    EvalContext* saved_context = context;
    context = &task_context;
    Item result = transpile_js_to_mir_with_preamble_len(runtime, source, source_len, filename, preamble);
    context = saved_context;
    js_mir_accumulate_last_phase_timing(false);

    if (refresh_snapshot && get_type_id(result) != LMD_TYPE_ERROR) {
        preamble_state_update_from_eval_snapshot(preamble);
    }
    return result;
}

static Item execute_js_module_source(Runtime* runtime, const char* source, size_t source_len,
                                     const char* filename) {
    if (!runtime) return ItemError;
    if (!source) {
        source = "";
        source_len = 0;
    }
    (void)source_len;

    EvalContext task_context = {};
    script_eval_context_init(runtime, &task_context);

    EvalContext* saved_context = context;
    context = &task_context;
    Item result = transpile_js_module_to_mir(runtime, source, filename);
    context = saved_context;
    js_mir_accumulate_last_phase_timing(false);

    if (result.item == 0 || get_type_id(result) == LMD_TYPE_NULL) {
        return ItemError;
    }
    return result;
}

static void script_runner_set_ready_state(Runtime* runtime, const char* ready_state) {
    DomDocument* doc = runtime ? (DomDocument*)runtime->dom_doc : nullptr;
    if (doc) {
        doc->js.ready_state = ready_state ? ready_state : "complete";
    }
}

static const char LIFECYCLE_INTERACTIVE_SOURCE[] =
    "if (document && document.dispatchEvent && typeof Event === 'function') {\n"
    "  document.dispatchEvent(new Event('readystatechange'));\n"
    "}\n";
static const char LIFECYCLE_DOM_CONTENT_LOADED_SOURCE[] =
    "if (document && document.dispatchEvent && typeof Event === 'function') {\n"
    // DOMContentLoaded is observable through Window because the browser event
    // bubbles from Document; the default Event constructor flag is false.
    "  document.dispatchEvent(new Event('DOMContentLoaded', { bubbles: true }));\n"
    "}\n";
static const char LIFECYCLE_WINDOW_LOAD_SOURCE[] =
    "if (window && window.dispatchEvent && typeof Event === 'function') {\n"
    "  window.dispatchEvent(new Event('load'));\n"
    // EventTarget dispatch invokes the window `onload` event-handler property
    // after registered listeners; calling it again here duplicated the handler
    // and placed the first invocation before addEventListener callbacks.
    "}\n";

static const char LIFECYCLE_INTERACTIVE_FILENAME[] =
    "<document-readystatechange-interactive>";
static const char LIFECYCLE_DOM_CONTENT_LOADED_FILENAME[] =
    "<document-domcontentloaded>";
static const char LIFECYCLE_COMPLETE_FILENAME[] =
    "<document-readystatechange-complete>";
static const char LIFECYCLE_WINDOW_LOAD_FILENAME[] = "<window-load>";

typedef struct JsLifecycleCacheUnits {
    const JsPreambleState* interactive;
    const JsPreambleState* dom_content_loaded;
    const JsPreambleState* complete;
    const JsPreambleState* window_load;
} JsLifecycleCacheUnits;

static bool execute_lifecycle_snippet(Runtime* runtime, JsPreambleState* preamble,
                                      const JsPreambleState* cached,
                                      const char* source, const char* filename,
                                      DocumentScriptPhaseTiming* timing) {
    Item result;
    if (cached) {
        result = execute_compiled_js_in_current_realm(runtime, cached);
        js_mir_cache_record_instantiation(s_js_mir_cache);
        if (timing) timing->cache_instantiations++;
        js_mir_accumulate_last_phase_timing(false);
    } else {
        result = execute_js_source_with_preamble(
            runtime, preamble, source, strlen(source), filename, false);
    }
    js_microtask_flush();
    if (get_type_id(result) == LMD_TYPE_ERROR) {
        log_error("execute_document_scripts: lifecycle task failed: %s", filename);
        return false;
    }
    return true;
}

static const JsPreambleState* prepare_cached_lifecycle_unit(
    Runtime* runtime, const JsPreambleState* base_preamble,
    const char* source, const char* filename,
    DocumentScriptPhaseTiming* timing) {
    if (!s_js_mir_cache || !runtime || !base_preamble) return nullptr;

    if (timing) timing->cache_lookups++;
    const JsPreambleState* cached = js_mir_cache_lookup(
        s_js_mir_cache, JS_MIR_CACHE_LIFECYCLE,
        source, strlen(source), filename, base_preamble);
    if (cached) {
        if (timing) timing->cache_hits++;
        return cached;
    }
    if (timing) timing->cache_misses++;

    JsPreambleState compiled = {};
    Item result = compile_js_mir_with_preamble_len(
        runtime, source, strlen(source), filename, base_preamble, &compiled);
    js_mir_accumulate_last_phase_timing(false);
    if (get_type_id(result) != LMD_TYPE_ERROR) {
        cached = js_mir_cache_adopt(
            s_js_mir_cache, JS_MIR_CACHE_LIFECYCLE,
            source, strlen(source), filename, base_preamble, &compiled);
        if (cached && timing) timing->cache_compiles++;
    }
    if (!cached) preamble_state_destroy(&compiled);

    // Compile-only lowering may create temporary heap values. Discard them
    // before the document preamble is instantiated so cached code never makes
    // a prior document heap part of its execution contract.
    EvalContext* previous_context = context;
    js_batch_reset();
    runtime_reset_heap(runtime);
    context = previous_context;
    return cached;
}

static void prepare_cached_lifecycle_units(
    Runtime* runtime, const JsPreambleState* base_preamble,
    JsLifecycleCacheUnits* units, DocumentScriptPhaseTiming* timing) {
    if (!units) return;
    memset(units, 0, sizeof(*units));
    if (!s_js_mir_cache || !base_preamble) return;

    units->interactive = prepare_cached_lifecycle_unit(
        runtime, base_preamble, LIFECYCLE_INTERACTIVE_SOURCE,
        LIFECYCLE_INTERACTIVE_FILENAME, timing);
    units->dom_content_loaded = prepare_cached_lifecycle_unit(
        runtime, base_preamble, LIFECYCLE_DOM_CONTENT_LOADED_SOURCE,
        LIFECYCLE_DOM_CONTENT_LOADED_FILENAME, timing);
    units->complete = prepare_cached_lifecycle_unit(
        runtime, base_preamble, LIFECYCLE_INTERACTIVE_SOURCE,
        LIFECYCLE_COMPLETE_FILENAME, timing);
    units->window_load = prepare_cached_lifecycle_unit(
        runtime, base_preamble, LIFECYCLE_WINDOW_LOAD_SOURCE,
        LIFECYCLE_WINDOW_LOAD_FILENAME, timing);
}

static void script_runner_clear_pending_exception(const char* phase_name, const char* filename) {
    if (!js_check_exception()) return;

    const char* message = js_get_exception_message();
    log_error("execute_document_scripts: %s exception in %s%s%s",
              phase_name ? phase_name : "script",
              filename ? filename : "<unknown>",
              message && message[0] ? ": " : "",
              message && message[0] ? message : "");
    (void)js_clear_exception();
}

static bool execute_browser_global_sync(Runtime* runtime, JsPreambleState* preamble,
                                        JsScriptTaskCollection* collection) {
    StrBuf* sync_buf = strbuf_new_cap(256);
    append_browser_global_sync(sync_buf);
    size_t sync_len = sync_buf->length;
    // Generated sync snippets can install functions/listeners that outlive this
    // call; keep the StrBuf wrapper and source alive until script teardown.
    if (!script_task_collection_retain_strbuf(collection, sync_buf)) {
        strbuf_free(sync_buf);
        log_error("execute_document_scripts: failed to retain browser global sync source");
        return false;
    }
    Item result = execute_js_source_with_preamble(runtime, preamble, sync_buf->str,
                                                 sync_len, "<browser-global-sync>", true);
    if (get_type_id(result) == LMD_TYPE_ERROR) {
        script_runner_clear_pending_exception("global-sync", "<browser-global-sync>");
        log_error("execute_document_scripts: browser global sync failed");
        return false;
    }
    return true;
}

static bool script_scheduler_enqueue(JsScriptTaskCollection* collection,
                                     JsScriptSchedulerQueues* queues) {
    if (!collection || !queues) return false;

    for (int i = 0; i < collection->scripts->length; i++) {
        JsScriptTask* task = (JsScriptTask*)arraylist_get(collection->scripts, i);
        if (!script_task_is_executable(task)) continue;

        ArrayList* queue = nullptr;
        if (task->scheduling == JS_SCRIPT_SCHED_ASYNC) {
            queue = queues->async_ready;
        } else if (task->scheduling == JS_SCRIPT_SCHED_DEFER) {
            queue = queues->defer;
        } else {
            queue = queues->post_dom;
        }
        if (!arraylist_append(queue, task)) {
            return false;
        }
    }
    return true;
}

static bool execute_script_task_queue(Runtime* runtime, ArrayList* queue,
                                      JsPreambleState* preamble,
                                      const char* phase_name,
                                      JsScriptTaskCollection* collection) {
    bool fatal_error = false;
    if (!queue) return true;
    size_t accepted_source_bytes = 0;
    size_t total_compile_limit = script_total_compile_limit_bytes();
    for (int i = 0; i < queue->length; i++) {
        JsScriptTask* task = (JsScriptTask*)arraylist_get(queue, i);
        if (!script_task_is_executable(task)) continue;

        char filename_buf[64];
        const char* filename = script_task_filename(task, filename_buf, sizeof(filename_buf));
        const char* source = task->source ? task->source : "";
        size_t external_compile_limit = script_external_compile_limit_bytes();
        if (task->external && task->source_len > external_compile_limit) {
            // Some modern sites ship multi-megabyte bundles that exceed the
            // current LambdaJS browser-compat budget; skip them before MIR
            // compilation can exhaust memory or crash the smoke viewer.
            task->status = JS_SCRIPT_TASK_SKIPPED_LARGE_DEFER;
            log_info("execute_document_scripts: skipping large external script before layout: %zu bytes > %zu (%s)",
                     task->source_len,
                     external_compile_limit,
                     filename ? filename : "<external-script>");
            continue;
        }
        if (total_compile_limit != (size_t)-1 &&
            (accepted_source_bytes >= total_compile_limit ||
             task->source_len > total_compile_limit - accepted_source_bytes)) {
            // Browser smoke loads should keep rendering after the current JS
            // compatibility budget is exhausted, rather than crashing in JIT.
            task->status = JS_SCRIPT_TASK_SKIPPED_LARGE_DEFER;
            log_info("execute_document_scripts: skipping script after total browser JS budget: %zu + %zu > %zu (%s)",
                     accepted_source_bytes,
                     task->source_len,
                     total_compile_limit,
                     filename ? filename : "<script>");
            continue;
        }
        size_t prelayout_defer_limit = script_prelayout_defer_limit_bytes();
        if (task->external &&
            (task->scheduling == JS_SCRIPT_SCHED_DEFER ||
             task->scheduling == JS_SCRIPT_SCHED_ASYNC) &&
            task->source_len > prelayout_defer_limit) {
            task->status = JS_SCRIPT_TASK_SKIPPED_LARGE_DEFER;
            log_info("execute_document_scripts: skipping large %s external script before layout: %zu bytes > %zu (%s)",
                     phase_name ? phase_name : "scheduled",
                     task->source_len,
                     prelayout_defer_limit,
                     filename ? filename : "<external-script>");
            continue;
        }
        accepted_source_bytes += task->source_len;
#ifndef NDEBUG
        bool timing_enabled = script_task_timing_enabled();
        long task_start_us = timing_enabled ? script_runner_wall_now_us() : 0;
#endif
        Item result = task->kind == JS_SCRIPT_TASK_MODULE
            ? execute_js_module_source(runtime, source, task->source_len, filename)
            : execute_js_source_with_preamble(runtime, preamble, source,
                                             task->source_len, filename, true);
#ifndef NDEBUG
        if (timing_enabled) {
            long task_wall_us = script_runner_wall_now_us() - task_start_us;
            JsMirPhaseTiming phase_timing = {};
            js_mir_get_last_phase_timing(&phase_timing);
            log_notice("script_runner_timing: phase=%s order=%d kind=%s status=%s wall_us=%ld mir_total_us=%ld mir_recorded_total_us=%ld parse_us=%ld ast_us=%ld early_us=%ld imports_us=%ld mir_us=%ld link_us=%ld execute_us=%ld cleanup_us=%ld bytes=%zu src=%s",
                     phase_name ? phase_name : "scheduled",
                     task->document_order,
                     script_task_kind_name(task->kind),
                     get_type_id(result) == LMD_TYPE_ERROR ? "error" : "ok",
                     task_wall_us,
                     script_runner_timing_total_us(&phase_timing),
                     phase_timing.total_us,
                     phase_timing.parse_us,
                     phase_timing.ast_us,
                     phase_timing.early_us,
                     phase_timing.imports_us,
                     phase_timing.mir_us,
                     phase_timing.link_us,
                     phase_timing.execute_us,
                     phase_timing.cleanup_us,
                     task->source_len,
                     filename);
        }
#endif
        if (get_type_id(result) == LMD_TYPE_ERROR) {
            script_runner_clear_pending_exception(phase_name, filename);
            log_error("execute_document_scripts: %s script task failed: %s",
                      phase_name ? phase_name : "scheduled", filename);
        }
        if (!execute_browser_global_sync(runtime, preamble, collection)) {
            fatal_error = true;
        }
        js_microtask_flush();
        if (js_check_exception()) {
            script_runner_clear_pending_exception(phase_name, filename);
        }
        task->executed = true;
    }
    return !fatal_error;
}

static bool execute_body_onload_tasks(Runtime* runtime, JsScriptTaskCollection* collection,
                                      JsPreambleState* preamble) {
    StrBuf* onload_buf = strbuf_new_cap(512);
    emit_body_onload_source(onload_buf, collection->onload_handlers);
    if (onload_buf->length == 0) {
        strbuf_free(onload_buf);
        return true;
    }

    Item result = execute_js_source_with_preamble(runtime, preamble, onload_buf->str,
                                                 onload_buf->length, "<body-onload>", true);
    strbuf_free(onload_buf);
    js_microtask_flush();
    if (get_type_id(result) == LMD_TYPE_ERROR) {
        script_runner_clear_pending_exception("body-onload", "<body-onload>");
        log_error("execute_document_scripts: body onload task failed");
        return true;
    }
    if (js_check_exception()) {
        script_runner_clear_pending_exception("body-onload", "<body-onload>");
    }
    return true;
}

static int script_runner_load_block_timeout_ms() {
    const char* env = getenv("RADIANT_JS_LOAD_BLOCK_TIMEOUT_MS");
    if (env && env[0]) {
        char* end = nullptr;
        long parsed = strtol(env, &end, 10);
        if (end != env && parsed >= 0) {
            if (parsed > 30000) return 30000;
            return (int)parsed; // INT_CAST_OK: timeout is bounded to a small millisecond count
        }
    }
    return 250;
}

static bool script_runner_wait_for_load_blockers(Runtime* runtime,
                                                 JsScriptTaskCollection* collection) {
    DomDocument* doc = runtime ? (DomDocument*)runtime->dom_doc : nullptr;
    if (!doc) return true;

    if (!doc->resource_manager) {
        doc->fully_loaded = true;
        if (collection && collection->load_blocking_scripts > 0) {
            log_debug("script_runner_lifecycle: %d script load blockers completed synchronously",
                      collection->load_blocking_scripts);
        }
        return true;
    }

    NetworkResourceManager* mgr = doc->resource_manager;
    resource_manager_flush_layout_updates(mgr);
    if (resource_manager_is_fully_loaded(mgr)) {
        doc->fully_loaded = true;
        return true;
    }

    int timeout_ms = script_runner_load_block_timeout_ms();
    int waited_ms = 0;
    const int poll_ms = 10;
    while (timeout_ms > 0 && waited_ms < timeout_ms) {
        resource_manager_flush_layout_updates(mgr);
        if (resource_manager_is_fully_loaded(mgr)) {
            doc->fully_loaded = true;
            return true;
        }
#ifndef _WIN32
        usleep((useconds_t)poll_ms * 1000);
#endif
        waited_ms += poll_ms;
    }

    int pending = resource_manager_get_pending_count(mgr);
    if (pending > 0) {
        log_warn("script_runner_lifecycle: proceeding to load with %d pending modeled resources after %dms",
                 pending, waited_ms);
    }
    doc->fully_loaded = true;
    return true;
}

static Item execute_document_script_tasks_postdom(Runtime* runtime, JsScriptTaskCollection* collection,
                                                  JsPreambleState* preamble,
                                                  DocumentScriptPhaseTiming* timing) {
    if (!runtime || !collection || !preamble) return ItemError;

    uint64_t phase_start_us = timing ? time_now_us() : 0;
    StrBuf* preamble_buf = nullptr;
#ifndef NDEBUG
    size_t preamble_source_len = 0;
    bool timing_enabled = script_task_timing_enabled();
    long preamble_start_us = timing_enabled ? script_runner_wall_now_us() : 0;
#endif
    Item result;
    JsLifecycleCacheUnits lifecycle_units = {};
    // Parser-blocking classic scripts execute while the document is loading;
    // the previous post-DOM scheduler entered interactive before user code.
    script_runner_set_ready_state(runtime, "loading");
    preamble_buf = strbuf_new_cap(4096);
    append_browser_document_preamble(preamble_buf);
#ifndef NDEBUG
    preamble_source_len = preamble_buf->length;
#endif
    const char* preamble_filename = "<document-preamble>";
    const JsPreambleState* cached_preamble = nullptr;
    if (s_js_mir_cache && !s_retain_js_state) {
        if (timing) timing->cache_lookups++;
        cached_preamble = js_mir_cache_lookup(
            s_js_mir_cache, JS_MIR_CACHE_PREAMBLE,
            preamble_buf->str, preamble_buf->length, preamble_filename, nullptr);
        if (timing) {
            if (cached_preamble) timing->cache_hits++;
            else timing->cache_misses++;
        }
    }

    if (s_js_mir_cache && !s_retain_js_state) {
        if (!cached_preamble) {
            result = compile_js_mir_preamble_len(runtime, preamble_buf->str,
                                                 preamble_buf->length,
                                                 preamble_filename, preamble);
            js_mir_accumulate_last_phase_timing(true);
            if (get_type_id(result) != LMD_TYPE_ERROR) {
                cached_preamble = js_mir_cache_adopt(
                    s_js_mir_cache, JS_MIR_CACHE_PREAMBLE,
                    preamble_buf->str, preamble_buf->length, preamble_filename,
                    nullptr, preamble);
                if (cached_preamble && timing) timing->cache_compiles++;
            }

            // Compile-only preambles allocate a temporary realm. Destroy it
            // before compiling dependent units or instantiating the document.
            EvalContext* previous_context = context;
            js_batch_reset();
            runtime_reset_heap(runtime);
            context = previous_context;
        }

        if (cached_preamble) {
            prepare_cached_lifecycle_units(
                runtime, cached_preamble, &lifecycle_units, timing);
            result = instantiate_js_preamble(runtime, cached_preamble, preamble);
            js_mir_cache_record_instantiation(s_js_mir_cache);
            if (timing) timing->cache_instantiations++;
            js_mir_accumulate_last_phase_timing(true);
        } else {
            preamble_state_destroy(preamble);
            result = ItemError;
        }
    } else {
        result = transpile_js_to_mir_preamble_len(runtime, preamble_buf->str, preamble_buf->length,
                                                  preamble_filename, preamble);
        js_mir_accumulate_last_phase_timing(true);
    }
#ifndef NDEBUG
    if (timing_enabled) {
        long preamble_wall_us = script_runner_wall_now_us() - preamble_start_us;
        JsMirPhaseTiming phase_timing = {};
        js_mir_get_last_phase_timing(&phase_timing);
        log_notice("script_runner_timing: phase=preamble order=-1 kind=preamble status=%s wall_us=%ld mir_total_us=%ld mir_recorded_total_us=%ld parse_us=%ld ast_us=%ld early_us=%ld imports_us=%ld mir_us=%ld link_us=%ld execute_us=%ld cleanup_us=%ld bytes=%zu src=<document-preamble>",
                   get_type_id(result) == LMD_TYPE_ERROR ? "error" : "ok",
                   preamble_wall_us,
                   script_runner_timing_total_us(&phase_timing),
                   phase_timing.total_us,
                   phase_timing.parse_us,
                   phase_timing.ast_us,
                   phase_timing.early_us,
                   phase_timing.imports_us,
                   phase_timing.mir_us,
                   phase_timing.link_us,
                   phase_timing.execute_us,
                   phase_timing.cleanup_us,
                   preamble_source_len);
    }
#endif
    if (preamble_buf) strbuf_free(preamble_buf);
    if (timing) timing->preamble_us += time_now_us() - phase_start_us;
    if (get_type_id(result) == LMD_TYPE_ERROR) {
        log_error("execute_document_scripts: document preamble execution failed");
        return result;
    }

    bool any_error = false;
    phase_start_us = timing ? time_now_us() : 0;
    JsScriptSchedulerQueues queues;
    if (!script_scheduler_queues_init(&queues)) {
        log_error("execute_document_scripts: failed to initialize scheduler queues");
        return ItemError;
    }
    if (!script_scheduler_enqueue(collection, &queues)) {
        script_scheduler_queues_free(&queues);
        log_error("execute_document_scripts: failed to build scheduler queues");
        return ItemError;
    }
    if (timing) timing->scheduler_us += time_now_us() - phase_start_us;

    phase_start_us = timing ? time_now_us() : 0;
    if (!execute_script_task_queue(runtime, queues.post_dom, preamble, "post-dom", collection)) {
        any_error = true;
    }
    if (timing) timing->user_scripts_us += time_now_us() - phase_start_us;

    phase_start_us = timing ? time_now_us() : 0;
    script_runner_set_ready_state(runtime, "interactive");
    if (!execute_lifecycle_snippet(
        runtime, preamble, lifecycle_units.interactive,
        LIFECYCLE_INTERACTIVE_SOURCE, LIFECYCLE_INTERACTIVE_FILENAME, timing)) {
        any_error = true;
    }
    if (timing) timing->interactive_us += time_now_us() - phase_start_us;

    phase_start_us = timing ? time_now_us() : 0;
    if (!execute_script_task_queue(runtime, queues.defer, preamble, "defer", collection)) {
        any_error = true;
    }
    if (timing) timing->user_scripts_us += time_now_us() - phase_start_us;

    phase_start_us = timing ? time_now_us() : 0;
    if (!execute_lifecycle_snippet(
        runtime, preamble, lifecycle_units.dom_content_loaded,
        LIFECYCLE_DOM_CONTENT_LOADED_SOURCE,
        LIFECYCLE_DOM_CONTENT_LOADED_FILENAME, timing)) {
        any_error = true;
    }
    if (timing) timing->dom_content_loaded_us += time_now_us() - phase_start_us;

    // Async classics do not block DOMContentLoaded in Radiant's post-DOM
    // model, but ready async tasks still run before the window load boundary.
    phase_start_us = timing ? time_now_us() : 0;
    if (!execute_script_task_queue(runtime, queues.async_ready, preamble, "async-ready", collection)) {
        any_error = true;
    }
    if (timing) timing->async_scripts_us += time_now_us() - phase_start_us;

    phase_start_us = timing ? time_now_us() : 0;
    if (!script_runner_wait_for_load_blockers(runtime, collection)) {
        any_error = true;
    }
    if (timing) timing->load_blockers_us += time_now_us() - phase_start_us;

    phase_start_us = timing ? time_now_us() : 0;
    script_runner_set_ready_state(runtime, "complete");
    if (!execute_lifecycle_snippet(
        runtime, preamble, lifecycle_units.complete,
        LIFECYCLE_INTERACTIVE_SOURCE, LIFECYCLE_COMPLETE_FILENAME, timing)) {
        any_error = true;
    }
    if (timing) timing->complete_us += time_now_us() - phase_start_us;

    phase_start_us = timing ? time_now_us() : 0;
    if (!execute_body_onload_tasks(runtime, collection, preamble)) {
        any_error = true;
    }
    if (timing) timing->body_onload_us += time_now_us() - phase_start_us;

    phase_start_us = timing ? time_now_us() : 0;
    if (!execute_lifecycle_snippet(
        runtime, preamble, lifecycle_units.window_load,
        LIFECYCLE_WINDOW_LOAD_SOURCE, LIFECYCLE_WINDOW_LOAD_FILENAME, timing)) {
        any_error = true;
    }
    if (timing) timing->window_load_us += time_now_us() - phase_start_us;

    script_scheduler_queues_free(&queues);
    return any_error ? ItemError : result;
}

// ============================================================================
// Main entry point
// ============================================================================

extern "C" void execute_document_scripts_profiled(Element* html_root, DomDocument* dom_doc,
                                                  Pool* pool, Url* base_url,
                                                  DocumentScriptPhaseTiming* timing) {
    if (timing) memset(timing, 0, sizeof(*timing));
    uint64_t phase_start_us = timing ? time_now_us() : 0;
    if (!html_root || !dom_doc) {
        log_debug("execute_document_scripts: null parameters, skipping");
        return;
    }
    js_batch_cleanup_unsafe = 0;

    // reset counters
    loaded_external_scripts = 0;
    failed_external_scripts = 0;

    // collect script task metadata first. The Phase 4 pipeline runs these
    // tasks as separate post-DOM units in one persistent document realm.
    JsScriptTaskCollection script_tasks;
    if (!script_task_collection_init(&script_tasks)) {
        log_error("execute_document_scripts: failed to initialize script task collection");
        return;
    }
    collect_scripts_recursive(html_root, &script_tasks, base_url, 0);
    log_script_task_diagnostics(&script_tasks);
    if (timing) timing->collect_us += time_now_us() - phase_start_us;

    if (!script_task_collection_has_executable_tasks(&script_tasks)) {
        log_debug("execute_document_scripts: no scripts found");
        script_task_collection_free(&script_tasks);
        script_runner_cleanup_source_cache();
        return;
    }
    size_t watchdog_source_len = script_task_collection_source_bytes(&script_tasks);

    // log external script loading stats
    if (loaded_external_scripts > 0 || failed_external_scripts > 0) {
        log_info("script_runner: external scripts: %d loaded, %d failed",
            loaded_external_scripts, failed_external_scripts);
    }

    size_t browser_js_limit = script_total_compile_limit_bytes();
    if (browser_js_limit != (size_t)-1 && watchdog_source_len > browser_js_limit) {
        // Large browser-app bundles still exercise unsupported LambdaJS/runtime
        // paths; skip document JS before JIT setup and render the parsed DOM/CSS.
        log_info("execute_document_scripts: skipping document JS after browser source budget: %zu > %zu",
                 watchdog_source_len,
                 browser_js_limit);
        script_task_collection_free(&script_tasks);
        script_runner_cleanup_source_cache();
        return;
    }

#ifndef NDEBUG
    log_info("execute_document_scripts: executing JS with tasks-postdom pipeline (%zu source bytes)",
        watchdog_source_len);
#endif

    phase_start_us = timing ? time_now_us() : 0;
    // set up Runtime for JS transpiler
    Runtime runtime = {};
    runtime.dom_doc = (void*)dom_doc;
    // create fresh tracked mmap pool for this JS execution
    runtime.reuse_pool = mem_pool_create_mmap((MemContext*)dom_doc->services.mem_ctx,
                                              MEM_ROLE_RUNTIME_HEAP,
                                              "script.js.reuse");
    EvalContext* saved_js_context = context;
    Context* saved_input_context = input_context;
    void* saved_js_document = js_dom_get_document();

    // Initialize the JS event loop so setTimeout/setInterval timers are queued
    // rather than silently dropped. The loop is drained after script execution.
    js_xhr_set_base_url(base_url ? url_get_href(base_url) : nullptr);
    js_event_loop_init();

    // execute document scripts via JIT transpiler
    // Install crash guard around JIT execution (catches SIGSEGV/SIGBUS in compiled code)
    // and per-script CPU-time watchdog
#ifndef _WIN32
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = js_exec_crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, &js_exec_old_segv);
    sigaction(SIGBUS, &sa, &js_exec_old_bus);

    js_exec_timed_out = 0;
    js_exec_guarded = 1;
    int timeout_seconds = js_exec_timeout_seconds(watchdog_source_len);
    if (timeout_seconds > JS_EXEC_TIMEOUT_BASE_SECONDS) {
        log_info("script_runner_timeout: source %zu bytes gets %ds watchdog",
                 watchdog_source_len, timeout_seconds);
    }
    // Parse/transpile/JIT are synchronous CPU work. A wall-clock alarm made
    // valid parallel fixtures time out while descheduled under CPU contention.
    js_exec_watchdog_arm(timeout_seconds);
#endif

    Item result = ItemNull;
    JsPreambleState* preamble = nullptr;
#ifndef _WIN32
    int jmp_val = sigsetjmp(js_exec_jmpbuf, 1);
    if (jmp_val == 0) {
#else
    {
        int jmp_val = 0; (void)jmp_val;
#endif
        // interactive documents need preamble mode so event handlers can call
        // compiled functions later. Static headless smoke loads only need
        // load-time DOM mutations, so use transient mode and avoid retaining
        // large MIR/transpiler pools for pages with big inline scripts.
        preamble = (JsPreambleState*)mem_calloc(1, sizeof(JsPreambleState), MEM_CAT_EVAL);
        log_mem_stage("js: before transpile/exec");
        if (timing) timing->runtime_setup_us += time_now_us() - phase_start_us;
        phase_start_us = timing ? time_now_us() : 0;
        result = execute_document_script_tasks_postdom(&runtime, &script_tasks, preamble, timing);
        if (timing) timing->postdom_total_us += time_now_us() - phase_start_us;
        log_mem_stage("js: after transpile/exec");
#ifndef _WIN32
        js_exec_guarded = 0;
        js_exec_watchdog_disarm();
        sigaction(SIGSEGV, &js_exec_old_segv, NULL);
        sigaction(SIGBUS, &js_exec_old_bus, NULL);
	    } else if (jmp_val == 2) {
	        log_error("execute_document_scripts: JS execution timed out after %ds", timeout_seconds);
	        result = ItemError;
	        js_batch_cleanup_unsafe = 1;
	        // siglongjmp skips the normal guarded-execution epilogue; restore
	        // handlers here so teardown does not run under the JS crash guard.
	        js_exec_guarded = 0;
	        js_exec_watchdog_disarm();
	        sigaction(SIGSEGV, &js_exec_old_segv, NULL);
	        sigaction(SIGBUS, &js_exec_old_bus, NULL);
		        // siglongjmp skips MIR/transpiler destructors; clean the active stack
		        // before releasing the wrapper so large code pages are not orphaned.
		        jm_cleanup_active_mir();
	        jm_cleanup_deferred_mir();
	        if (preamble) {
	            // siglongjmp can land after MIR preamble ownership was transferred;
	            // use the normal destroyer so source_buffer/MIR/pools are released.
	            preamble_state_destroy(preamble);
	            mem_free(preamble);
	            preamble = nullptr;
	        }
	    } else {
	        log_error("execute_document_scripts: recovered from crash in JS JIT code");
	        result = ItemError;
	        // recovered JS crashes can leave timer/runtime state inconsistent; let
	        // document teardown abandon handles instead of re-entering them later.
	        js_batch_cleanup_unsafe = 1;
	        // siglongjmp skips the normal guarded-execution epilogue; restore
	        // handlers here so teardown does not run under the JS crash guard.
	        js_exec_guarded = 0;
	        js_exec_watchdog_disarm();
	        sigaction(SIGSEGV, &js_exec_old_segv, NULL);
	        sigaction(SIGBUS, &js_exec_old_bus, NULL);
		        // the native signal may have interrupted MIR/JIT state in-place;
		        // abandon active MIR contexts instead of finalizing corrupted lists.
		        jm_abandon_active_mir_after_signal();
	        jm_cleanup_deferred_mir();
	        if (preamble) {
	            // siglongjmp can land after MIR preamble ownership was transferred;
	            // use the normal destroyer so source_buffer/MIR/pools are released.
	            preamble_state_destroy(preamble);
	            mem_free(preamble);
	            preamble = nullptr;
	        }
	    }
#else
    }
#endif

    phase_start_us = timing ? time_now_us() : 0;
    context = saved_js_context;
    input_context = saved_input_context;
    if (saved_js_context && saved_js_context->heap && saved_js_context->name_pool) {
        js_dom_set_document(saved_js_document);
    } else {
        js_dom_set_document(NULL);
    }

    TypeId result_type = get_type_id(result);
    if (result_type == LMD_TYPE_ERROR) {
        log_error("execute_document_scripts: JS execution failed");
    } else {
        log_info("execute_document_scripts: JS execution completed successfully");
        if (js_dom_is_host_driven_loop()) {
            // A long-lived host (Radiant `view`) pumps the event loop AFTER it
            // commits the first layout. Draining timers here — still inside the
            // loader, before @font-face processing and the first layout pass —
            // fires load-time setTimeout(0) callbacks against an uncommitted
            // document, so geometry APIs (getBoundingClientRect) read zero boxes
            // (e.g. an editor's post-mount overlay re-sync mis-anchors its table
            // column resizers). Flush only microtasks and leave timers queued so
            // they run post-commit with real geometry (browser semantics: a
            // setTimeout(0) queued during load fires after layout).
            js_microtask_flush();
            log_info("execute_document_scripts: microtasks flushed; timers deferred to host loop");
        } else {
            // Static one-shot render (make layout, headless smoke): no host loop
            // will pump later, so drain everything now with the 5s watchdog.
            js_event_loop_drain();
            log_mem_stage("js: after event loop drain");
            log_info("execute_document_scripts: timer queue drained");
        }
    }
    if (timing) timing->event_loop_us += time_now_us() - phase_start_us;

    phase_start_us = timing ? time_now_us() : 0;
    // Retain JS state on DomDocument for interactive event handler dispatch.
    // The MIR context, heap, and name_pool stay alive so compiled
    // functions (clicked(), toggle(), setFontFamily(), etc.) can be invoked
    // at event time without re-compilation.
    if (preamble && preamble->mir_ctx) {
        // MIR setup consumes runtime.reuse_pool into the fresh heap. Restore the
        // owner pointer so document cleanup can release that per-document pool.
        if (!runtime.reuse_pool && runtime.heap) runtime.reuse_pool = runtime.heap->pool;
        dom_doc->js.preamble_state = preamble;
        dom_doc->js.mir_ctx = preamble->mir_ctx;
        dom_doc->js.runtime_heap = runtime.heap;
        dom_doc->js.runtime_name_pool = runtime.name_pool;
        dom_doc->js.runtime_type_list = runtime.type_list;
        dom_doc->js.runtime_pool = runtime.reuse_pool;
        if (s_retain_js_state) {
            log_info("execute_document_scripts: retained MIR context for event handlers");
            // Do NOT destroy heap/pool — they're retained on the document
        } else {
            log_info("execute_document_scripts: releasing transient JS context");
            // transient scripts can leave timers rooted in this heap, sometimes
            // without a document pointer; shut down the loop before heap free.
            js_event_loop_shutdown();
            js_batch_reset();
            script_runner_cleanup_js_state(dom_doc);
        }
    } else {
        // Fallback: no valid preamble — destroy as before
        if (preamble) { mem_free(preamble); }
        if (!s_retain_js_state || script_runner_js_batch_cleanup_unsafe()) {
            // Transient document scripts still populate global/module state with
            // heap-bound functions and DOM wrappers. Clear them before tearing
            // down the per-document heap so the next batch file starts clean.
            // Timers also hold heap-root slots, so close the loop before heap free.
            if (script_runner_js_batch_cleanup_unsafe()) {
                // A signal longjmp can invalidate the retained preamble before
                // normal JS-state retention runs; force DOM wrapper/global reset.
                js_event_loop_abandon_all_timers();
            } else {
                js_event_loop_shutdown();
            }
            js_batch_reset();
        }
        if (runtime.heap && runtime.heap->gc) {
            Pool* reuse_pool = runtime.heap->gc->pool;
            heap_finalize_gc_objects(runtime.heap->gc);
            runtime.heap->gc->pool = NULL;
            gc_heap_destroy(runtime.heap->gc);
            mem_free(runtime.heap);
            if (s_retain_js_state) {
                s_js_reuse_pool = reuse_pool;
            } else if (reuse_pool) {
                mem_pool_destroy(reuse_pool);
            }
        } else if (runtime.heap) {
            mem_free(runtime.heap);
        }
        if (runtime.type_list) {
            arraylist_free(runtime.type_list);
            runtime.type_list = nullptr;
        }
    }
    if (timing) timing->runtime_cleanup_us += time_now_us() - phase_start_us;

    phase_start_us = timing ? time_now_us() : 0;
    script_task_collection_free(&script_tasks);
    script_runner_cleanup_source_cache();
    js_batch_cleanup_unsafe = 0;
    if (timing) timing->source_cleanup_us += time_now_us() - phase_start_us;
}

extern "C" void execute_document_scripts(Element* html_root, DomDocument* dom_doc, Pool* pool,
                                         Url* base_url) {
    execute_document_scripts_profiled(html_root, dom_doc, pool, base_url, nullptr);
}

extern "C" bool script_runner_js_batch_cleanup_unsafe(void) {
    return js_batch_cleanup_unsafe != 0;
}

// ============================================================================
// Phase 2: Event handler collection and compilation
// ============================================================================

// Event handler attribute names (without "on" prefix for event_type)
static const struct {
    const char* attr_name;   // HTML attribute: "onclick", "onmouseover", etc.
    const char* event_type;  // event type: "click", "mouseover", etc.
} EVENT_HANDLER_ATTRS[] = {
    {"onclick",      "click"},
    {"ondblclick",   "dblclick"},
    {"onmousedown",  "mousedown"},
    {"onmouseup",    "mouseup"},
    {"onmouseover",  "mouseover"},
    {"onmouseout",   "mouseout"},
    {"onmousemove",  "mousemove"},
    {"onkeydown",    "keydown"},
    {"onkeyup",      "keyup"},
    {"onkeypress",   "keypress"},
    {"onfocus",      "focus"},
    {"onblur",       "blur"},
    {"onchange",     "change"},
    {"oninput",      "input"},
    // textarea onselect must use the retained handler context; the generic
    // event-attribute fallback can otherwise expose a stale pre-reconcile fn.
    {"onselect",     "select"},
    {"onsubmit",     "submit"},
    {"onreset",      "reset"},
    {"onscroll",     "scroll"},
    {nullptr,        nullptr}
};

typedef struct InlineHandlerInstallEntry {
    DomElement* element;
    const char* event_type;
    const char* function_name;
    struct InlineHandlerInstallEntry* next;
} InlineHandlerInstallEntry;

typedef struct InlineHandlerInstallCollection {
    struct hashmap* element_map;
    int count;
    Pool* pool;
} InlineHandlerInstallCollection;

// hashmap callbacks for DomElement* keys
HASHMAP_DEFINE_PTRKEY(inline_handler_install, InlineHandlerInstallEntry, element)

static bool inline_handler_ident_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           c == '_' || c == '$';
}

static bool inline_handler_ident_part(char c) {
    return inline_handler_ident_start(c) || (c >= '0' && c <= '9');
}

static const char* inline_handler_skip_space(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static bool append_global_call_inline_handler(StrBuf* compile_buf,
                                              const char* attr_val) {
    const char* p = inline_handler_skip_space(attr_val);
    if (strncmp(p, "return", 6) == 0 && !inline_handler_ident_part(p[6])) {
        p = inline_handler_skip_space(p + 6);
    }
    if (!inline_handler_ident_start(*p)) return false;

    const char* name_start = p;
    p++;
    while (inline_handler_ident_part(*p)) p++;
    size_t name_len = (size_t)(p - name_start);
    if (name_len == 0 || name_len > 128) return false;

    p = inline_handler_skip_space(p);
    if (*p != '(') return false;
    p = inline_handler_skip_space(p + 1);

    bool pass_event = false;
    if (strncmp(p, "event", 5) == 0 && !inline_handler_ident_part(p[5])) {
        pass_event = true;
        p = inline_handler_skip_space(p + 5);
    }
    if (*p != ')') return false;
    p = inline_handler_skip_space(p + 1);
    if (*p == ';') p = inline_handler_skip_space(p + 1);
    if (*p != '\0') return false;

    strbuf_append_str(compile_buf,
        "var __lambda_inline_fn = (typeof globalThis !== 'undefined') ? globalThis[\"");
    strbuf_append_str_n(compile_buf, name_start, name_len);
    strbuf_append_str(compile_buf,
        "\"] : null; if (typeof __lambda_inline_fn !== 'function' && typeof window !== 'undefined') { __lambda_inline_fn = window[\"");
    strbuf_append_str_n(compile_buf, name_start, name_len);
    strbuf_append_str(compile_buf,
        "\"]; } if (typeof __lambda_inline_fn === 'function') { return __lambda_inline_fn.call(this");
    if (pass_event) strbuf_append_str(compile_buf, ", event");
    strbuf_append_str(compile_buf, "); } return undefined;");
    return true;
}

static void collect_handlers_recursive(DomElement* elem,
                                        InlineHandlerInstallCollection* handlers,
                                        StrBuf* compile_buf, int* handler_id, int depth) {
    if (!elem || depth > 100) return;

    for (int i = 0; EVENT_HANDLER_ATTRS[i].attr_name; i++) {
        const char* attr_val = elem->get_attribute(EVENT_HANDLER_ATTRS[i].attr_name);
        if (attr_val && attr_val[0]) {
            // Allocate handler entry from the transient collection pool.
            InlineHandlerInstallEntry* handler =
                (InlineHandlerInstallEntry*)pool_calloc(handlers->pool, sizeof(InlineHandlerInstallEntry));
            handler->element = elem;
            handler->event_type = EVENT_HANDLER_ATTRS[i].event_type;

            // Generate an IDL-handler-shaped function. The EventTarget
            // dispatcher calls it with `this` set to the target element and
            // the real Event as the first argument.
            int id = (*handler_id)++;
            char func_name[64];
            snprintf(func_name, sizeof(func_name), "__evt_handler_%d", id);

            strbuf_append_str(compile_buf, "function ");
            strbuf_append_str(compile_buf, func_name);
            strbuf_append_str(compile_buf, "(event) { ");
            if (!append_global_call_inline_handler(compile_buf, attr_val)) {
                strbuf_append_str(compile_buf, attr_val);
            }
            strbuf_append_str(compile_buf, " }\n");

            // store func_name on pool for later lookup
            char* stored_name = (char*)pool_alloc(handlers->pool, strlen(func_name) + 1);
            strcpy(stored_name, func_name); // UNSAFE_LIBC_OK: dst allocated with strlen(func_name)+1
            handler->function_name = stored_name;

            // Link into collection: find existing chain or create new.
            InlineHandlerInstallEntry key = {};
            key.element = elem;
            InlineHandlerInstallEntry* existing =
                (InlineHandlerInstallEntry*)hashmap_get(handlers->element_map, &key);
            if (existing) {
                // append to linked list
                InlineHandlerInstallEntry* tail = existing;
                while (tail->next) tail = tail->next;
                tail->next = handler;
            } else {
                hashmap_set(handlers->element_map, handler);
            }
            handlers->count++;

            log_debug("collect_handlers: %s on <%s> → %s()",
                      EVENT_HANDLER_ATTRS[i].attr_name,
                      elem->tag_name ? elem->tag_name : "?",
                      func_name);
        }
    }

    // recurse into children
    DomNode* child = elem->first_child;
    while (child) {
        if (child->node_type == DOM_NODE_ELEMENT) {
            collect_handlers_recursive(lam::dom_require_element(child), handlers, compile_buf, handler_id, depth + 1);
        }
        child = child->next_sibling;
    }
}

extern "C" void collect_and_compile_event_handlers(DomDocument* dom_doc) {
    if (!dom_doc || !dom_doc->root || !dom_doc->js.mir_ctx) {
        return;
    }

    // Create a transient collection used only during compile/install.
    Pool* handlers_pool = mem_pool_create_mmap((MemContext*)dom_doc->services.mem_ctx,
                                               MEM_ROLE_TEMP,
                                               "script.inline_handlers");
    InlineHandlerInstallCollection* handlers =
        (InlineHandlerInstallCollection*)pool_calloc(handlers_pool, sizeof(InlineHandlerInstallCollection));
    handlers->pool = handlers_pool;
    handlers->element_map = inline_handler_install_new(32);
    handlers->count = 0;

    // Collect all inline event handler attributes.
    StrBuf* compile_buf = strbuf_new_cap(4096);
    int handler_id = 0;
    collect_handlers_recursive(dom_doc->root, handlers, compile_buf, &handler_id, 0);

    if (handlers->count == 0) {
        log_debug("collect_and_compile_event_handlers: no event handlers found");
        strbuf_free(compile_buf);
        hashmap_free(handlers->element_map);
        mem_pool_destroy(handlers_pool);
        return;
    }

    log_info("collect_and_compile_event_handlers: found %d handlers, policy=handler-eager, compiling",
             handlers->count);

    // Compile handler wrapper functions using the retained MIR preamble context.
    // The with_preamble call creates a new MIR context that can see preamble-defined
    // functions (clicked(), toggle(), etc.). We need the thread-local EvalContext
    // set up with the retained heap so the transpiler reuses it (reusing_context=true),
    // which causes the new MIR context to be deferred rather than destroyed.
    JsPreambleState* preamble = (JsPreambleState*)dom_doc->js.preamble_state;
    Runtime runtime = {};
    runtime.dom_doc = dom_doc;
    runtime.heap = (Heap*)dom_doc->js.runtime_heap;
    runtime.name_pool = (NamePool*)dom_doc->js.runtime_name_pool;
    runtime.type_list = (ArrayList*)dom_doc->js.runtime_type_list;
    runtime.reuse_pool = (Pool*)dom_doc->js.runtime_pool;

    // Set up thread-local eval context so transpiler sees reusing_context=true.
    // This prevents the new MIR context from being destroyed — it gets deferred instead.
    EvalContext handler_compile_ctx = {};
    handler_compile_ctx.heap = runtime.heap;
    handler_compile_ctx.name_pool = runtime.name_pool;
    handler_compile_ctx.type_list = runtime.type_list;
    handler_compile_ctx.pool = runtime.reuse_pool ? runtime.reuse_pool :
        (runtime.heap ? runtime.heap->pool : nullptr);
    EvalContext* saved_ctx = context;
    context = &handler_compile_ctx;

    Item compile_result = transpile_js_to_mir_with_preamble(&runtime, compile_buf->str,
                                                             "<event-handlers>", preamble);
    strbuf_free(compile_buf);

    TypeId result_type = get_type_id(compile_result);
    if (result_type == LMD_TYPE_ERROR) {
        log_error("collect_and_compile_event_handlers: compilation failed");
        context = saved_ctx;
        hashmap_free(handlers->element_map);
        mem_pool_destroy(handlers_pool);
        return;
    }

    // Get the new MIR context before restoring the old eval context.
    // The handler functions were compiled in this new context and must be
    // retained for the Function Items installed below.
    MIR_context_t handler_mir_ctx = (MIR_context_t)jm_get_last_deferred_mir_ctx();
    if (!handler_mir_ctx) {
        log_error("collect_and_compile_event_handlers: no deferred MIR context found");
        context = saved_ctx;
        hashmap_free(handlers->element_map);
        mem_pool_destroy(handlers_pool);
        return;
    }

    // Install the compiled functions into each element's on<type> IDL slot.
    // From this point on, normal js_dom_dispatch_event() handles `this`,
    // `event`, return-false default prevention, and propagation ordering.
    Item global = js_get_global_this();
    [[maybe_unused]] int installed = 0;  // only consumed by log_info, which is a no-op in release
    size_t iter = 0;
    void* item;
    while (hashmap_iter(handlers->element_map, &iter, &item)) {
        InlineHandlerInstallEntry* h = (InlineHandlerInstallEntry*)item;
        while (h) {
            Item fn_key = (Item){.item = s2it(heap_create_name(h->function_name))};
            Item fn_item = js_property_get(global, fn_key);
            char attr_name[64];
            snprintf(attr_name, sizeof(attr_name), "on%s", h->event_type);
            if (get_type_id(fn_item) == LMD_TYPE_FUNC &&
                js_dom_set_event_handler_function(h->element, attr_name, fn_item)) {
                installed++;
                log_debug("collect_and_compile_event_handlers: installed %s on <%s>",
                          attr_name,
                          h->element && h->element->tag_name ? h->element->tag_name : "?");
            } else {
                log_error("collect_and_compile_event_handlers: failed to install %s on <%s>",
                          attr_name,
                          h->element && h->element->tag_name ? h->element->tag_name : "?");
            }
            h = h->next;
        }
    }

    context = saved_ctx;

    log_info("collect_and_compile_event_handlers: installed %d/%d handlers into EventTarget path",
             installed, handlers->count);

    // Update retained state after compilation.
    dom_doc->js.runtime_heap = runtime.heap;
    dom_doc->js.runtime_name_pool = runtime.name_pool;
    dom_doc->js.runtime_type_list = runtime.type_list;

    hashmap_free(handlers->element_map);
    mem_pool_destroy(handlers_pool);
}

// ============================================================================
// Cleanup
// ============================================================================

extern "C" void script_runner_cleanup_js_state(DomDocument* dom_doc) {
    if (!dom_doc) return;

    // Destroy retained MIR context via preamble_state_destroy
    if (dom_doc->js.preamble_state) {
        JsPreambleState* preamble = (JsPreambleState*)dom_doc->js.preamble_state;
        preamble_state_destroy(preamble);
        mem_free(preamble);
        dom_doc->js.preamble_state = nullptr;
        dom_doc->js.mir_ctx = nullptr;
    }
    // Destroy retained heap and GC metadata.
    if (dom_doc->js.runtime_heap) {
        Heap* heap = (Heap*)dom_doc->js.runtime_heap;
        if (heap->gc) {
            heap_finalize_gc_objects(heap->gc);
            heap->gc->pool = nullptr; // prevent gc_heap_destroy from destroying pool
            gc_heap_destroy(heap->gc);
            // pool is destroyed separately below
        }
        mem_free(heap);
        dom_doc->js.runtime_heap = nullptr;
    }

    if (dom_doc->js.runtime_type_list) {
        arraylist_free((ArrayList*)dom_doc->js.runtime_type_list);
        dom_doc->js.runtime_type_list = nullptr;
    }

    // Destroy retained mmap pool (native code pages, etc.)
    if (dom_doc->js.runtime_pool) {
        pool_destroy((Pool*)dom_doc->js.runtime_pool);
        dom_doc->js.runtime_pool = nullptr;
    }

    dom_doc->js.runtime_name_pool = nullptr;

    log_debug("script_runner_cleanup_js_state: cleaned up JS state");
}
