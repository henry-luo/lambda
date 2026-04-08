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

// reusable pool from the most recent JS execution.
// Uses delayed-destroy pattern: the most recent pool is kept alive
// because stale JS globals may still reference its data; the PREVIOUS
// pool is fully destroyed (stale refs from 2 files ago are gone).
static Pool* s_js_reuse_pool = nullptr;
static Pool* s_js_prev_pool = nullptr;

extern "C" void script_runner_cleanup_heap() {
    // Use pool_drain which replaces mmap'd pages at the same virtual address.
    // Physical memory is released; stale pointers from unreset JS globals
    // read zeros instead of crashing.  MmapChunk metadata is kept alive
    // (negligible: ~24 bytes per chunk × ~4 chunks per file).
    if (s_js_reuse_pool) {
        pool_drain(s_js_reuse_pool);
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
                    free(local_path);
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
            // Handle setTimeout('code', delay) in body onload — extract inner code
            // Pattern: setTimeout('code()', delay) or setTimeout("code()", delay)
            const char* st = strstr(onload, "setTimeout(");
            if (st && (st == onload || *(st-1) == ' ' || *(st-1) == ';')) {
                const char* p = st + 11; // skip "setTimeout("
                char quote = *p;
                if (quote == '\'' || quote == '"') {
                    p++; // skip opening quote
                    const char* code_start = p;
                    // find closing quote
                    while (*p && *p != quote) p++;
                    if (*p == quote) {
                        int code_len = (int)(p - code_start);
                        strbuf_append_str_n(onload_buf, code_start, code_len);
                        strbuf_append_str(onload_buf, "\n");
                    } else {
                        strbuf_append_str(onload_buf, onload);
                        strbuf_append_str(onload_buf, "\n");
                    }
                } else {
                    // function form — pass through as-is, preamble handles it
                    strbuf_append_str(onload_buf, onload);
                    strbuf_append_str(onload_buf, "\n");
                }
            } else {
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
                free(content);
                loaded_external_scripts++;
            } else {
                failed_external_scripts++;
            }
            return;
        }
        // extract inline script source
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
    StrBuf* wrapped_buf = strbuf_new_cap(script_buf->length + 1024);
    // Preamble: provide browser globals
    strbuf_append_str(wrapped_buf,
        "var window = {};\n"
        "var navigator = {userAgent: ''};\n"
        "window.navigator = navigator;\n"
        "window.document = document;\n"
        "function setTimeout(fn, delay) { if (typeof fn === 'function') fn(); }\n"
        "function setInterval(fn, delay) { if (typeof fn === 'function') fn(); }\n"
        "window.setTimeout = setTimeout;\n"
        "window.setInterval = setInterval;\n"
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
    Item result = transpile_js_to_mir(&runtime, script_buf->str, "<document-scripts>");

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
        free(runtime.heap);
        s_js_reuse_pool = pool;
    } else if (runtime.heap) {
        free(runtime.heap);
    }
    if (runtime.nursery) {
        gc_nursery_destroy(runtime.nursery);
    }

    strbuf_free(script_buf);
    strbuf_free(onload_buf);
}
