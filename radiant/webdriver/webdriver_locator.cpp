/**
 * @file webdriver_locator.cpp
 * @brief Element finding using CSS selectors
 */

#include "webdriver.hpp"
#include "../view.hpp"
#include "../../lambda/input/css/dom_element.hpp"
#include "../../lambda/input/css/selector_matcher.hpp"
#include "../../lambda/input/css/css_parser.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <cstring>

// ============================================================================
// Locator Strategy Parsing
// ============================================================================

LocatorStrategy webdriver_parse_strategy(const char* strategy) {
    if (!strategy) return LOCATOR_CSS_SELECTOR;
    
    if (strcmp(strategy, "css selector") == 0) {
        return LOCATOR_CSS_SELECTOR;
    } else if (strcmp(strategy, "link text") == 0) {
        return LOCATOR_LINK_TEXT;
    } else if (strcmp(strategy, "partial link text") == 0) {
        return LOCATOR_PARTIAL_LINK_TEXT;
    } else if (strcmp(strategy, "tag name") == 0) {
        return LOCATOR_TAG_NAME;
    } else if (strcmp(strategy, "xpath") == 0) {
        return LOCATOR_XPATH;
    }
    
    return LOCATOR_CSS_SELECTOR;  // Default
}

// ============================================================================
// Text Extraction Helpers
// ============================================================================

// Extract visible text from a view and its descendants
static void extract_text_recursive(View* view, StrBuf* buf) {
    if (!view) return;
    
    if (view->view_type == RDT_VIEW_TEXT) {
        DomText* text = view->as_text();
        if (text && text->text) {
            strbuf_append_str(buf, text->text);
        }
        return;
    }
    
    // For elements, traverse children
    if (view->is_element()) {
        ViewElement* elem = (ViewElement*)view;
        DomNode* child = elem->first_child;
        while (child) {
            extract_text_recursive((View*)child, buf);
            child = child->next_sibling;
        }
    }
}

static char* get_view_text(View* view, Arena* arena) {
    StrBuf* buf = strbuf_new_cap(256);
    extract_text_recursive(view, buf);
    
    char* result = (char*)arena_alloc(arena, buf->length + 1);
    if (result && buf->str) {
        memcpy(result, buf->str, buf->length);
        result[buf->length] = '\0';
    }
    
    strbuf_free(buf);
    return result;
}

// ============================================================================
// View Tree Traversal
// ============================================================================

// Traverse view tree depth-first, calling callback for each element
typedef bool (*ViewVisitor)(View* view, void* udata);

static bool traverse_views(View* view, ViewVisitor visitor, void* udata) {
    if (!view) return true;  // Continue
    
    // Visit this node
    if (view->is_element()) {
        if (!visitor(view, udata)) {
            return false;  // Stop traversal
        }
    }
    
    // Visit children
    if (view->is_block()) {
        ViewBlock* block = (ViewBlock*)view;
        View* child = block->first_child;
        while (child) {
            if (!traverse_views(child, visitor, udata)) {
                return false;
            }
            child = child->next();
        }
    } else if (view->view_type == RDT_VIEW_INLINE) {
        ViewSpan* span = (ViewSpan*)view;
        View* child = span->first_child;
        while (child) {
            if (!traverse_views(child, visitor, udata)) {
                return false;
            }
            child = child->next();
        }
    }
    
    return true;
}

// ============================================================================
// CSS Selector Matching
// ============================================================================

typedef struct {
    Arena* arena;
    CssSelector* selector;
    SelectorMatcher* matcher;
    View* result;           // For find_element (first match)
    ArrayList* results;     // For find_elements (all matches)
    bool find_all;
} CssFindContext;

// Parse a CSS selector string
static CssSelector* parse_css_selector(Pool* pool, const char* selector_text) {
    if (!pool || !selector_text) return NULL;
    
    // Tokenize the selector
    size_t token_count = 0;
    CssToken* tokens = css_tokenize(selector_text, strlen(selector_text), pool, &token_count);
    if (!tokens || token_count == 0) {
        return NULL;
    }
    
    // Parse selector using token array API
    int pos = 0;
    return css_parse_selector_with_combinators(tokens, &pos, (int)token_count, pool);
}

static bool css_find_visitor(View* view, void* udata) {
    CssFindContext* ctx = (CssFindContext*)udata;
    
    if (!view->is_element()) return true;
    
    ViewElement* elem = (ViewElement*)view;
    DomElement* dom_elem = (DomElement*)elem;
    
    // Match against selector
    if (selector_matcher_matches(ctx->matcher, ctx->selector, dom_elem, NULL)) {
        if (ctx->find_all) {
            arraylist_append(ctx->results, view);
        } else {
            ctx->result = view;
            return false;  // Stop on first match
        }
    }
    
    return true;  // Continue
}

static View* find_by_css_selector(WebDriverSession* session, const char* selector_text, View* root) {
    if (!session || !selector_text || !root) return NULL;
    
    // Parse the selector
    CssSelector* selector = parse_css_selector(session->pool, selector_text);
    if (!selector) {
        log_error("webdriver: failed to parse selector: %s", selector_text);
        return NULL;
    }
    
    // Create matcher
    SelectorMatcher* matcher = selector_matcher_create(session->pool);
    if (!matcher) {
        log_error("webdriver: failed to create selector matcher");
        return NULL;
    }
    
    CssFindContext ctx = {0};
    ctx.arena = session->arena;
    ctx.selector = selector;
    ctx.matcher = matcher;
    ctx.result = NULL;
    ctx.results = NULL;
    ctx.find_all = false;
    
    traverse_views(root, css_find_visitor, &ctx);
    
    return ctx.result;
}

static int find_all_by_css_selector(WebDriverSession* session, const char* selector_text, 
                                     View* root, ArrayList* results) {
    if (!session || !selector_text || !root || !results) return 0;
    
    // Parse the selector
    CssSelector* selector = parse_css_selector(session->pool, selector_text);
    if (!selector) {
        log_error("webdriver: failed to parse selector: %s", selector_text);
        return 0;
    }
    
    // Create matcher
    SelectorMatcher* matcher = selector_matcher_create(session->pool);
    if (!matcher) {
        log_error("webdriver: failed to create selector matcher");
        return 0;
    }
    
    CssFindContext ctx = {0};
    ctx.arena = session->arena;
    ctx.selector = selector;
    ctx.matcher = matcher;
    ctx.result = NULL;
    ctx.results = results;
    ctx.find_all = true;
    
    traverse_views(root, css_find_visitor, &ctx);
    
    return results->length;
}

// ============================================================================
// Link Text Matching
// ============================================================================

typedef struct {
    Arena* arena;
    const char* text;
    bool partial;
    View* result;
    ArrayList* results;
    bool find_all;
} LinkTextContext;

static bool link_text_visitor(View* view, void* udata) {
    LinkTextContext* ctx = (LinkTextContext*)udata;
    
    if (!view->is_element()) return true;
    
    ViewElement* elem = (ViewElement*)view;
    
    // Check if this is an anchor tag
    if (elem->tag() != HTM_TAG_A) return true;
    
    // Get text content
    char* text = get_view_text(view, ctx->arena);
    if (!text) return true;
    
    bool matches = false;
    if (ctx->partial) {
        matches = (strstr(text, ctx->text) != NULL);
    } else {
        matches = (strcmp(text, ctx->text) == 0);
    }
    
    if (matches) {
        if (ctx->find_all) {
            arraylist_append(ctx->results, view);
        } else {
            ctx->result = view;
            return false;
        }
    }
    
    return true;
}

static View* find_by_link_text(WebDriverSession* session, const char* text, 
                                View* root, bool partial) {
    if (!session || !text || !root) return NULL;
    
    LinkTextContext ctx = {0};
    ctx.arena = session->arena;
    ctx.text = text;
    ctx.partial = partial;
    ctx.result = NULL;
    ctx.results = NULL;
    ctx.find_all = false;
    
    traverse_views(root, link_text_visitor, &ctx);
    
    return ctx.result;
}

// ============================================================================
// Tag Name Matching
// ============================================================================

typedef struct {
    const char* tag_name;
    View* result;
    ArrayList* results;
    bool find_all;
} TagNameContext;

static bool tag_name_visitor(View* view, void* udata) {
    TagNameContext* ctx = (TagNameContext*)udata;
    
    if (!view->is_element()) return true;
    
    ViewElement* elem = (ViewElement*)view;
    const char* name = elem->node_name();
    
    if (name && strcasecmp(name, ctx->tag_name) == 0) {
        if (ctx->find_all) {
            arraylist_append(ctx->results, view);
        } else {
            ctx->result = view;
            return false;
        }
    }
    
    return true;
}

static View* find_by_tag_name(WebDriverSession* session, const char* tag_name, View* root) {
    if (!session || !tag_name || !root) return NULL;
    
    TagNameContext ctx = {0};
    ctx.tag_name = tag_name;
    ctx.result = NULL;
    ctx.results = NULL;
    ctx.find_all = false;
    
    traverse_views(root, tag_name_visitor, &ctx);
    
    return ctx.result;
}

// ============================================================================
// Public API
// ============================================================================

View* webdriver_find_element(WebDriverSession* session, LocatorStrategy strategy,
                              const char* value, View* root) {
    if (!session || !value) return NULL;
    
    // Use document root if not specified
    if (!root && session->document && session->document->view_tree) {
        root = session->document->view_tree->root;
    }
    if (!root) return NULL;
    
    log_debug("webdriver: find_element strategy=%d value='%s'", strategy, value);
    
    switch (strategy) {
        case LOCATOR_CSS_SELECTOR:
            return find_by_css_selector(session, value, root);
            
        case LOCATOR_LINK_TEXT:
            return find_by_link_text(session, value, root, false);
            
        case LOCATOR_PARTIAL_LINK_TEXT:
            return find_by_link_text(session, value, root, true);
            
        case LOCATOR_TAG_NAME:
            return find_by_tag_name(session, value, root);
            
        case LOCATOR_XPATH:
            // XPath not implemented
            log_warn("webdriver: XPath locator not implemented");
            return NULL;
            
        default:
            return NULL;
    }
}

int webdriver_find_elements(WebDriverSession* session, LocatorStrategy strategy,
                             const char* value, View* root, ArrayList* results) {
    if (!session || !value || !results) return 0;
    
    // Use document root if not specified
    if (!root && session->document && session->document->view_tree) {
        root = session->document->view_tree->root;
    }
    if (!root) return 0;
    
    log_debug("webdriver: find_elements strategy=%d value='%s'", strategy, value);
    
    switch (strategy) {
        case LOCATOR_CSS_SELECTOR:
            return find_all_by_css_selector(session, value, root, results);
            
        case LOCATOR_TAG_NAME: {
            TagNameContext ctx = {0};
            ctx.tag_name = value;
            ctx.result = NULL;
            ctx.results = results;
            ctx.find_all = true;
            traverse_views(root, tag_name_visitor, &ctx);
            return results->length;
        }
        
        case LOCATOR_LINK_TEXT:
        case LOCATOR_PARTIAL_LINK_TEXT: {
            LinkTextContext ctx = {0};
            ctx.arena = session->arena;
            ctx.text = value;
            ctx.partial = (strategy == LOCATOR_PARTIAL_LINK_TEXT);
            ctx.result = NULL;
            ctx.results = results;
            ctx.find_all = true;
            traverse_views(root, link_text_visitor, &ctx);
            return results->length;
        }
        
        case LOCATOR_XPATH:
            log_warn("webdriver: XPath locator not implemented");
            return 0;
            
        default:
            return 0;
    }
}
