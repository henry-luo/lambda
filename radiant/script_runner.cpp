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
 *   HTML parse → Element* tree → DomElement* tree → execute_document_scripts()
 *   → CSS cascade → layout
 */
#include "../lib/memtrack.h"

#include "script_runner.h"
#include "../lambda/lambda-data.hpp"
#include "../lambda/js/js_transpiler.hpp"
#include "../lambda/js/js_dom.h"
#include "../lambda/transpiler.hpp"
#include "../lib/gc/gc_heap.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lambda/input/input.hpp"
#include "../lib/log.h"
#include "../lib/strbuf.h"
#include "../lib/str.h"
#include "../lib/url.h"
#include "../lib/file.h"

#include <cstring>
#include <cctype>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>

// Crash guard for JS JIT execution (catches SIGSEGV/SIGBUS in compiled code)
static sigjmp_buf js_exec_jmpbuf;
static volatile sig_atomic_t js_exec_guarded = 0;
static struct sigaction js_exec_old_segv, js_exec_old_bus;
static volatile sig_atomic_t js_exec_timed_out = 0;

// Per-script timeout: SIGALRM handler for 5s execution limit
static struct sigaction js_exec_old_alrm;
#define JS_EXEC_TIMEOUT_SECONDS 5

static void js_exec_timeout_handler(int sig) {
    if (js_exec_guarded) {
        js_exec_timed_out = 1;
        const char* msg = "execute_document_scripts: JS execution timed out (5s limit)\n";
        write(STDERR_FILENO, msg, strlen(msg));
        js_exec_guarded = 0;
        sigaction(SIGSEGV, &js_exec_old_segv, NULL);
        sigaction(SIGBUS, &js_exec_old_bus, NULL);
        sigaction(SIGALRM, &js_exec_old_alrm, NULL);
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

// Pool from the most recent JS execution.
// Destroyed by script_runner_cleanup_heap() in per-file cleanup (after layout).
static Pool* s_js_reuse_pool = nullptr;

extern "C" void script_runner_cleanup_heap() {
    if (s_js_reuse_pool) {
        pool_destroy(s_js_reuse_pool);
        s_js_reuse_pool = nullptr;
    }
}

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
            if (s && s->chars && s->len > 0) {
                strbuf_append_str_n(buf, s->chars, s->len);
            }
        }
    }
}

/**
 * Resolve a script src attribute to a loadable path/URL, following the same
 * URL resolution logic as CSS stylesheet loading in collect_linked_stylesheets.
 *
 * @param src        The src attribute value (absolute, relative, or full URL)
 * @param base_url   The document base URL for resolving relative paths
 * @param out_is_http Set to true if the resolved path is an HTTP(S) URL
 * @return           Resolved path string (static buffer — copy before next call)
 */
static const char* resolve_script_url(const char* src, Url* base_url, bool* out_is_http) {
    static char resolved_path[2048];
    *out_is_http = false;

    if (src[0] == '/' && src[1] != '/') {
        // absolute local path
        strncpy(resolved_path, src, sizeof(resolved_path) - 1);
        resolved_path[sizeof(resolved_path) - 1] = '\0';
    } else if (strstr(src, "://") != nullptr) {
        // full URL
        strncpy(resolved_path, src, sizeof(resolved_path) - 1);
        resolved_path[sizeof(resolved_path) - 1] = '\0';
        *out_is_http = (strncmp(src, "http://", 7) == 0 || strncmp(src, "https://", 8) == 0);
    } else if (base_url) {
        // relative path — resolve against base URL
        Url* resolved_url = parse_url(base_url, src);
        if (resolved_url && resolved_url->is_valid) {
            if (resolved_url->scheme == URL_SCHEME_HTTP || resolved_url->scheme == URL_SCHEME_HTTPS) {
                const char* url_str = url_get_href(resolved_url);
                strncpy(resolved_path, url_str, sizeof(resolved_path) - 1);
                resolved_path[sizeof(resolved_path) - 1] = '\0';
                *out_is_http = true;
            } else {
                char* local_path = url_to_local_path(resolved_url);
                if (local_path) {
                    strncpy(resolved_path, local_path, sizeof(resolved_path) - 1);
                    resolved_path[sizeof(resolved_path) - 1] = '\0';
                    mem_free(local_path);
                } else {
                    strncpy(resolved_path, src, sizeof(resolved_path) - 1);
                    resolved_path[sizeof(resolved_path) - 1] = '\0';
                }
            }
            url_destroy(resolved_url);
        } else {
            strncpy(resolved_path, src, sizeof(resolved_path) - 1);
            resolved_path[sizeof(resolved_path) - 1] = '\0';
        }
    } else {
        strncpy(resolved_path, src, sizeof(resolved_path) - 1);
        resolved_path[sizeof(resolved_path) - 1] = '\0';
    }
    return resolved_path;
}

/**
 * Load external script content from a resolved path or URL.
 *
 * @param resolved_path  The resolved file path or URL
 * @param is_http        Whether the path is an HTTP(S) URL
 * @return               Allocated string with script content, or nullptr on failure.
 *                       Caller must free() the returned string.
 */
static char* load_script_content(const char* resolved_path, bool is_http) {
    char* content = nullptr;
    if (is_http) {
        size_t content_size = 0;
        content = download_http_content(resolved_path, &content_size, nullptr);
        if (content) {
            log_debug("script_runner: downloaded external script from URL: %s (%zu bytes)", resolved_path, content_size);
        } else {
            log_error("script_runner: failed to download external script: %s", resolved_path);
        }
    } else {
        content = read_text_file(resolved_path);
        if (content) {
            log_debug("script_runner: loaded external script from file: %s (%zu bytes)", resolved_path, strlen(content));
        } else {
            log_error("script_runner: failed to read external script file: %s", resolved_path);
        }
    }
    return content;
}

/**
 * Recursively walk the Element* tree, collecting <script> source text
 * (both inline and external) and the body onload handler in document order.
 */
static int loaded_external_scripts = 0;
static int failed_external_scripts = 0;

static void collect_scripts_recursive(Element* elem, StrBuf* script_buf, StrBuf* onload_buf, Url* base_url) {
    if (!elem) return;

    // check for <body onload="...">
    if (is_body_element(elem)) {
        const char* onload = extract_element_attribute(elem, "onload", nullptr);
        if (onload && onload[0]) {
            // Preprocess the onload handler before emitting as JS code.
            // The MIR transpiler cannot eval() strings, so the common pattern
            // setTimeout('code()', delay) must be transformed: we extract the
            // string content and emit it as direct code. The function-reference
            // form setTimeout(fn, delay) needs no transformation — the preamble's
            // setTimeout stub calls fn() directly, and inline script function
            // declarations are at global scope (no try/catch wrapping).
            const char* st = strstr(onload, "setTimeout(");
            if (st) {
                const char* p = st + 11; // skip "setTimeout("
                if (*p == '\'' || *p == '"') {
                    // String form: setTimeout('code()', delay) — extract inner code
                    char quote = *p++;
                    const char* code_start = p;
                    while (*p && *p != quote) p++;
                    if (*p == quote) {
                        strbuf_append_str_n(onload_buf, code_start, (int)(p - code_start));
                        strbuf_append_str(onload_buf, "\n");
                    } else {
                        // Malformed — emit as-is and let the transpiler handle it
                        strbuf_append_str(onload_buf, onload);
                        strbuf_append_str(onload_buf, "\n");
                    }
                } else {
                    // Function reference or expression — emit as-is
                    strbuf_append_str(onload_buf, onload);
                    strbuf_append_str(onload_buf, "\n");
                }
            } else {
                // No setTimeout — emit the handler code directly
                strbuf_append_str(onload_buf, onload);
                strbuf_append_str(onload_buf, "\n");
            }
        }
    }

    // check for <script> elements
    if (is_script_element(elem)) {
        // check type attribute - only execute text/javascript or no type
        const char* type_attr = extract_element_attribute(elem, "type", nullptr);
        if (type_attr && type_attr[0] &&
            strcasecmp(type_attr, "text/javascript") != 0 &&
            strcasecmp(type_attr, "application/javascript") != 0 &&
            strcasecmp(type_attr, "text/ecmascript") != 0 &&
            strcasecmp(type_attr, "application/ecmascript") != 0) {
            return;
        }
        // check for src attribute - load external scripts
        const char* src_attr = extract_element_attribute(elem, "src", nullptr);
        if (src_attr && src_attr[0]) {
            bool is_http = false;
            const char* resolved = resolve_script_url(src_attr, base_url, &is_http);
            char* content = load_script_content(resolved, is_http);
            if (content) {
                // Wrap external scripts in try/catch to prevent library feature-detection
                // exceptions (e.g. jQuery testing browser capabilities) from aborting
                // the entire script execution. Feature detection errors are expected and
                // non-fatal in real browsers.
                strbuf_append_str(script_buf, "try {\n");
                strbuf_append_str(script_buf, content);
                strbuf_append_str(script_buf, "\n} catch(_ext_err) {}\n");
                mem_free(content);
                loaded_external_scripts++;
            } else {
                failed_external_scripts++;
            }
            return;
        }
        // Inline scripts are emitted without try/catch wrapping so that
        // function declarations remain at global scope. This allows cross-script
        // function references (e.g. setTimeout(myFunc, 0) in onload) to resolve
        // correctly in the MIR transpiler. Signal-level guards (SIGALRM, SIGSEGV,
        // SIGBUS) still protect against crashes and infinite loops.
        extract_script_text(elem, script_buf);
        strbuf_append_str(script_buf, "\n");
        return;  // don't recurse into script children
    }

    // recurse into child elements
    for (int64_t i = 0; i < elem->length; i++) {
        Item child = elem->items[i];
        TypeId tid = get_type_id(child);
        if (tid == LMD_TYPE_ELEMENT) {
            collect_scripts_recursive(child.element, script_buf, onload_buf, base_url);
        }
    }
}

// ============================================================================
// Main entry point
// ============================================================================

extern "C" void execute_document_scripts(Element* html_root, DomDocument* dom_doc, Pool* pool, Url* base_url) {
    if (!html_root || !dom_doc) {
        log_debug("execute_document_scripts: null parameters, skipping");
        return;
    }

    // reset counters
    loaded_external_scripts = 0;
    failed_external_scripts = 0;

    // collect all scripts and onload handlers
    StrBuf* script_buf = strbuf_new_cap(4096);
    StrBuf* onload_buf = strbuf_new_cap(256);

    collect_scripts_recursive(html_root, script_buf, onload_buf, base_url);

    // append onload handler after all script definitions
    // This handles both:
    //   <body onload="doTest()">  → collected in onload_buf
    //   window.onload = function() { ... }  → set on window object below
    if (onload_buf->length > 0) {
        strbuf_append_str(script_buf, onload_buf->str);
    }

    if (script_buf->length == 0) {
        log_debug("execute_document_scripts: no scripts found");
        strbuf_free(script_buf);
        strbuf_free(onload_buf);
        return;
    }

    // log external script loading stats
    if (loaded_external_scripts > 0 || failed_external_scripts > 0) {
        log_info("script_runner: external scripts: %d loaded, %d failed",
            loaded_external_scripts, failed_external_scripts);
    }

    // Wrap script code with window object support:
    // 1. Prepend: var window = {};  (provides window.onload, window.setTimeout etc.)
    // 2. Append: extract library globals (jQuery/$) from window, then call window.onload
    StrBuf* wrapped_buf = strbuf_new_cap(script_buf->length + 2048);
    // Preamble: provide browser globals and stub unsupported APIs
    strbuf_append_str(wrapped_buf,
        "var window = {};\n"
        "var navigator = {userAgent: '', platform: '', language: 'en', languages: ['en']};\n"
        "window.navigator = navigator;\n"
        "window.document = document;\n"
        "function setTimeout(fn, delay) { if (typeof fn === 'function') fn(); }\n"
        "function setInterval(fn, delay) { if (typeof fn === 'function') fn(); return 0; }\n"
        "function clearTimeout(id) {}\n"
        "function clearInterval(id) {}\n"
        "function requestAnimationFrame(fn) { if (typeof fn === 'function') fn(0); return 0; }\n"
        "function cancelAnimationFrame(id) {}\n"
        "window.setTimeout = setTimeout;\n"
        "window.setInterval = setInterval;\n"
        "window.clearTimeout = clearTimeout;\n"
        "window.clearInterval = clearInterval;\n"
        "window.requestAnimationFrame = requestAnimationFrame;\n"
        "window.cancelAnimationFrame = cancelAnimationFrame;\n"
        // Stub storage APIs
        "var localStorage = {getItem: function(k){return null;}, setItem: function(k,v){}, removeItem: function(k){}, clear: function(){}, length: 0};\n"
        "var sessionStorage = {getItem: function(k){return null;}, setItem: function(k,v){}, removeItem: function(k){}, clear: function(){}, length: 0};\n"
        "window.localStorage = localStorage;\n"
        "window.sessionStorage = sessionStorage;\n"
        // Stub observer/constructor APIs (return no-op objects)
        "function MutationObserver(cb) { this.observe = function(){}; this.disconnect = function(){}; this.takeRecords = function(){ return []; }; }\n"
        "function IntersectionObserver(cb, opts) { this.observe = function(){}; this.unobserve = function(){}; this.disconnect = function(){}; }\n"
        "function ResizeObserver(cb) { this.observe = function(){}; this.unobserve = function(){}; this.disconnect = function(){}; }\n"
        "window.MutationObserver = MutationObserver;\n"
        "window.IntersectionObserver = IntersectionObserver;\n"
        "window.ResizeObserver = ResizeObserver;\n"
        // Stub console, performance, history, screen, location
        "var console = {log: function(){}, warn: function(){}, error: function(){}, info: function(){}, debug: function(){}, dir: function(){}, table: function(){}};\n"
        "var performance = {now: function(){ return 0; }, mark: function(){}, measure: function(){}, getEntriesByName: function(){ return []; }, timing: {}};\n"
        "var history = {pushState: function(){}, replaceState: function(){}, back: function(){}, forward: function(){}, go: function(){}, length: 1};\n"
        "var screen = {width: 1920, height: 1080, availWidth: 1920, availHeight: 1080, colorDepth: 24, pixelDepth: 24};\n"
        "var location = {href: '', protocol: 'https:', hostname: '', pathname: '/', search: '', hash: '', host: '', origin: '', reload: function(){}, assign: function(){}, replace: function(){}};\n"
        "window.console = console;\n"
        "window.performance = performance;\n"
        "window.history = history;\n"
        "window.screen = screen;\n"
        "window.location = location;\n"
        // Stub constructors that should not crash
        "function XMLHttpRequest() { this.open = function(){}; this.send = function(){}; this.setRequestHeader = function(){}; this.addEventListener = function(){}; this.status = 0; this.readyState = 0; this.response = ''; this.responseText = ''; }\n"
        "function WebSocket(url) { this.send = function(){}; this.close = function(){}; this.addEventListener = function(){}; this.readyState = 3; }\n"
        "function Worker(url) { this.postMessage = function(){}; this.terminate = function(){}; this.addEventListener = function(){}; }\n"
        "window.XMLHttpRequest = XMLHttpRequest;\n"
        "window.WebSocket = WebSocket;\n"
        "window.Worker = Worker;\n"
        // Stub event listener on window
        "window.addEventListener = function(type, fn, opts) {};\n"
        "window.removeEventListener = function(type, fn, opts) {};\n"
        "window.dispatchEvent = function(ev) { return true; };\n"
        "window.getComputedStyle = function(el, pseudo) { return {}; };\n"
        "window.matchMedia = function(q) { return {matches: false, media: q, addEventListener: function(){}, removeEventListener: function(){}}; };\n"
        "window.scrollTo = function(){};\n"
        "window.scrollBy = function(){};\n"
        "window.innerWidth = 1024;\n"
        "window.innerHeight = 768;\n"
        "window.outerWidth = 1024;\n"
        "window.outerHeight = 768;\n"
        "window.devicePixelRatio = 1;\n"
    );
    strbuf_append_str_n(wrapped_buf, script_buf->str, script_buf->length);
    // Postamble: call window.onload if set by scripts
    strbuf_append_str(wrapped_buf,
        "\nif (window.onload) { window.onload(); }\n"
    );
    strbuf_free(script_buf);
    script_buf = wrapped_buf;

    log_info("execute_document_scripts: executing %zu bytes of JS", script_buf->length);
    log_debug("execute_document_scripts: combined source:\n%.500s%s",
              script_buf->str,
              script_buf->length > 500 ? "\n..." : "");

    // set up Runtime for JS transpiler
    Runtime runtime = {};
    runtime.dom_doc = (void*)dom_doc;
    // create fresh mmap pool for this JS execution
    runtime.reuse_pool = pool_create_mmap();

    // execute the combined JS source via JIT transpiler
    // Install crash guard around JIT execution (catches SIGSEGV/SIGBUS in compiled code)
    // and per-script timeout via SIGALRM
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = js_exec_crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, &js_exec_old_segv);
    sigaction(SIGBUS, &sa, &js_exec_old_bus);

    // install SIGALRM-based timeout
    struct sigaction alrm_sa;
    memset(&alrm_sa, 0, sizeof(alrm_sa));
    alrm_sa.sa_handler = js_exec_timeout_handler;
    sigaction(SIGALRM, &alrm_sa, &js_exec_old_alrm);
    js_exec_timed_out = 0;
    js_exec_guarded = 1;
    alarm(JS_EXEC_TIMEOUT_SECONDS);

    Item result;
    int jmp_val = sigsetjmp(js_exec_jmpbuf, 1);
    if (jmp_val == 0) {
        result = transpile_js_to_mir(&runtime, script_buf->str, "<document-scripts>");
        js_exec_guarded = 0;
        alarm(0);  // cancel pending alarm
        sigaction(SIGSEGV, &js_exec_old_segv, NULL);
        sigaction(SIGBUS, &js_exec_old_bus, NULL);
        sigaction(SIGALRM, &js_exec_old_alrm, NULL);
    } else if (jmp_val == 2) {
        log_error("execute_document_scripts: JS execution timed out after %ds", JS_EXEC_TIMEOUT_SECONDS);
        result = ItemError;
    } else {
        log_error("execute_document_scripts: recovered from crash in JS JIT code");
        result = ItemError;
    }

    TypeId result_type = get_type_id(result);
    if (result_type == LMD_TYPE_ERROR) {
        log_error("execute_document_scripts: JS execution failed");
    } else {
        log_info("execute_document_scripts: JS execution completed successfully");
    }

    // properly destroy gc_heap metadata + nursery + Heap to avoid stale refs.
    // pool stays alive for now (data still needed? drain in per-file cleanup).
    if (runtime.heap && runtime.heap->gc) {
        Pool* pool = runtime.heap->gc->pool;
        runtime.heap->gc->pool = NULL;  // prevent gc_heap_destroy from destroying pool
        gc_heap_destroy(runtime.heap->gc);
        mem_free(runtime.heap);
        s_js_reuse_pool = pool;
    } else if (runtime.heap) {
        mem_free(runtime.heap);
    }
    if (runtime.nursery) {
        gc_nursery_destroy(runtime.nursery);
    }

    strbuf_free(script_buf);
    strbuf_free(onload_buf);
}
