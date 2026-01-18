// format_latex_html.cpp - Unified LaTeX to HTML conversion
// This is the new unified pipeline that replaces format_latex_html_v2.cpp
// Uses tex_document_model for conversion

#include "format.h"
#include "../tex/tex_document_model.hpp"
#include "../tex/tex_latex_bridge.hpp"
#include "../input/input.hpp"
#include "../lambda-data.hpp"
#include "../mark_reader.hpp"
#include "../../lib/log.h"
#include "../../lib/arena.h"
#include "../../lib/strbuf.h"
#include <cstring>

using namespace tex;

namespace lambda {

// Generate HTML fragment from LaTeX input using unified pipeline
Item format_latex_html_v2(Input* input, bool text_mode) {
    (void)text_mode;  // Always returns text mode HTML string now
    
    if (!input || get_type_id(input->root) == LMD_TYPE_NULL) {
        log_error("format_latex_html_v2: null input or empty root");
        return ItemNull;
    }
    
    log_debug("format_latex_html_v2: starting unified pipeline conversion");
    
    // Create LaTeX context
    LaTeXContext ctx = LaTeXContext::create(input->arena, nullptr);
    
    // Build document model from parsed LaTeX tree
    TexDocumentModel* doc = doc_model_from_latex(input->root, input->arena, ctx);
    
    if (!doc || !doc->root) {
        log_error("format_latex_html_v2: failed to build document model");
        return ItemNull;
    }
    
    // Render to HTML
    StrBuf* out = strbuf_new_cap(4096);
    HtmlOutputOptions opts = HtmlOutputOptions::hybrid();
    opts.standalone = false;
    opts.pretty_print = false;
    opts.include_css = false;
    
    bool success = doc_model_to_html(doc, out, opts);
    
    if (!success || out->length == 0) {
        log_error("format_latex_html_v2: HTML rendering failed");
        strbuf_free(out);
        return ItemNull;
    }
    
    // Create result string in input's arena
    String* result = (String*)arena_alloc(input->arena, sizeof(String) + out->length + 1);
    if (!result) {
        log_error("format_latex_html_v2: failed to allocate result string");
        strbuf_free(out);
        return ItemNull;
    }
    
    result->len = out->length;
    result->ref_cnt = 0;
    memcpy(result->chars, out->str, out->length);
    result->chars[out->length] = '\0';
    strbuf_free(out);
    
    log_debug("format_latex_html_v2: generated %zu bytes of HTML", result->len);
    
    Item item;
    item.string_ptr = (uintptr_t)result | ((uintptr_t)LMD_TYPE_STRING << 48);
    return item;
}

// Generate complete HTML document with CSS and fonts
std::string format_latex_html_v2_document(Input* input, const char* doc_class,
                                           const char* asset_base_url, bool embed_css) {
    (void)asset_base_url;
    
    if (!input || get_type_id(input->root) == LMD_TYPE_NULL) {
        log_error("format_latex_html_v2_document: null input or empty root");
        return "";
    }
    
    log_debug("format_latex_html_v2_document: generating full document");
    
    // Create LaTeX context with doc class
    const char* actual_class = doc_class ? doc_class : "article";
    LaTeXContext ctx = LaTeXContext::create(input->arena, nullptr, actual_class);
    
    // Build document model
    TexDocumentModel* doc = doc_model_from_latex(input->root, input->arena, ctx);
    
    if (!doc || !doc->root) {
        log_error("format_latex_html_v2_document: failed to build document model");
        return "";
    }
    
    // Render to HTML with full document options
    StrBuf* out = strbuf_new_cap(8192);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.standalone = true;
    opts.pretty_print = true;
    opts.include_css = embed_css;
    
    bool success = doc_model_to_html(doc, out, opts);
    
    if (!success) {
        log_error("format_latex_html_v2_document: HTML rendering failed");
        strbuf_free(out);
        return "";
    }
    
    std::string result(out->str, out->length);
    strbuf_free(out);
    
    log_debug("format_latex_html_v2_document: generated %zu bytes", result.size());
    
    return result;
}

} // namespace lambda

// C API for compatibility with existing code

extern "C" {

Item format_latex_html_v2_c(Input* input, int text_mode) {
    log_debug("format_latex_html_v2_c called (unified pipeline), text_mode=%d", text_mode);
    return lambda::format_latex_html_v2(input, text_mode != 0);
}

// Generate complete HTML document with CSS - returns allocated C string (caller must free)
const char* format_latex_html_v2_document_c(Input* input, const char* doc_class,
                                             const char* asset_base_url, int embed_css) {
    std::string result = lambda::format_latex_html_v2_document(input, doc_class,
                                                                 asset_base_url, embed_css != 0);
    if (result.empty()) {
        return nullptr;
    }
    // Allocate copy in input's arena for memory management
    char* copy = (char*)arena_alloc(input->arena, result.size() + 1);
    memcpy(copy, result.c_str(), result.size() + 1);
    return copy;
}

} // extern "C"
