/**
 * Script Runner for Radiant Layout Engine
 *
 * Extracts inline <script> source from the HTML Element* tree and executes
 * it via the Lambda JS transpiler with the DomDocument as the DOM context.
 *
 * Pipeline position:
 *   HTML parse → Element* tree → DomElement* tree → execute_document_scripts()
 *   → CSS cascade → layout
 */

#include "script_runner.h"
#include "../lambda/lambda-data.hpp"
#include "../lambda/js/js_transpiler.hpp"
#include "../lambda/js/js_dom.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lib/log.h"
#include "../lib/strbuf.h"
#include "../lib/str.h"

#include <cstring>
#include <cctype>

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
 * Recursively walk the Element* tree, collecting <script> source text
 * and the body onload handler in document order.
 */
static void collect_scripts_recursive(Element* elem, StrBuf* script_buf, StrBuf* onload_buf) {
    if (!elem) return;

    // check for <body onload="...">
    if (is_body_element(elem)) {
        const char* onload = extract_element_attribute(elem, "onload", nullptr);
        if (onload && onload[0]) {
            log_debug("script_runner: found body onload='%s'", onload);

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
                        log_debug("script_runner: extracted setTimeout code: '%.*s'", code_len, code_start);
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
            log_debug("script_runner: skipping <script type='%s'>", type_attr);
            return;
        }
        // check for src attribute - external scripts not supported yet
        const char* src_attr = extract_element_attribute(elem, "src", nullptr);
        if (src_attr && src_attr[0]) {
            log_debug("script_runner: skipping external <script src='%s'>", src_attr);
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
            collect_scripts_recursive(child.element, script_buf, onload_buf);
        }
    }
}

// ============================================================================
// Main entry point
// ============================================================================

extern "C" void execute_document_scripts(Element* html_root, DomDocument* dom_doc, Pool* pool) {
    if (!html_root || !dom_doc) {
        log_debug("execute_document_scripts: null parameters, skipping");
        return;
    }

    // collect all scripts and onload handlers
    StrBuf* script_buf = strbuf_new_cap(4096);
    StrBuf* onload_buf = strbuf_new_cap(256);

    collect_scripts_recursive(html_root, script_buf, onload_buf);

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

    // Wrap script code with window object support:
    // 1. Prepend: var window = {};  (provides window.onload, window.setTimeout etc.)
    // 2. Append: auto-call window.onload() if set by scripts
    StrBuf* wrapped_buf = strbuf_new_cap(script_buf->length + 512);
    // Preamble: provide browser globals
    strbuf_append_str(wrapped_buf,
        "var window = {};\n"
        "function setTimeout(fn, delay) { if (typeof fn === 'function') fn(); }\n"
        "function setInterval(fn, delay) { if (typeof fn === 'function') fn(); }\n"
    );
    strbuf_append_str_n(wrapped_buf, script_buf->str, script_buf->length);
    strbuf_append_str(wrapped_buf, "\nif (window.onload) { window.onload(); }\n");
    strbuf_free(script_buf);
    script_buf = wrapped_buf;

    log_info("execute_document_scripts: executing %zu bytes of JS", script_buf->length);
    log_debug("execute_document_scripts: combined source:\n%.500s%s",
              script_buf->str,
              script_buf->length > 500 ? "\n..." : "");

    // set up Runtime for JS transpiler
    Runtime runtime = {};
    runtime.dom_doc = (void*)dom_doc;

    // execute the combined JS source via JIT transpiler
    Item result = transpile_js_to_c(&runtime, script_buf->str, "<document-scripts>");

    TypeId result_type = get_type_id(result);
    if (result_type == LMD_TYPE_ERROR) {
        log_error("execute_document_scripts: JS execution failed");
    } else {
        log_info("execute_document_scripts: JS execution completed successfully");
    }

    strbuf_free(script_buf);
    strbuf_free(onload_buf);
}
