/**
 * @file input-html-context.cpp
 * @brief HTML parser context implementation
 */

#include "input-html-context.h"
#include "input.h"
#include "input-html-tree.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern "C" {
#include "../../lib/log.h"
}

// Head-only elements that belong in <head>
static const char* HEAD_ELEMENTS[] = {
    "title", "base", "link", "meta", "style", "script", "noscript", NULL
};

// Check if element belongs in <head>
static bool is_head_element(const char* tag_name) {
    for (int i = 0; HEAD_ELEMENTS[i]; i++) {
        if (strcasecmp(tag_name, HEAD_ELEMENTS[i]) == 0) {
            return true;
        }
    }
    return false;
}

HtmlParserContext* html_context_create(Input* input) {
    HtmlParserContext* ctx = (HtmlParserContext*)calloc(1, sizeof(HtmlParserContext));
    if (!ctx) {
        log_error("Failed to allocate HTML parser context");
        return NULL;
    }

    ctx->input = input;
    ctx->html_element = NULL;
    ctx->head_element = NULL;
    ctx->body_element = NULL;
    ctx->current_node = NULL;

    // Phase 4.2: Initialize insertion mode
    ctx->insertion_mode = HTML_MODE_INITIAL;

    // Phase 5: Initialize open element stack
    ctx->open_elements = html_stack_create(input->pool);
    if (!ctx->open_elements) {
        free(ctx);
        return NULL;
    }

    // Phase 6: Initialize active formatting elements
    ctx->active_formatting = html_formatting_create(input->pool);
    if (!ctx->active_formatting) {
        free(ctx);
        return NULL;
    }

    ctx->has_explicit_html = false;
    ctx->has_explicit_head = false;
    ctx->has_explicit_body = false;
    ctx->in_head = false;
    ctx->head_closed = false;
    ctx->in_body = false;

    return ctx;
}

void html_context_destroy(HtmlParserContext* ctx) {
    if (ctx) {
        // Phase 5: Stack is pool-allocated, will be cleaned up with pool
        if (ctx->open_elements) {
            html_stack_destroy(ctx->open_elements);
        }
        // Phase 6: Formatting list is pool-allocated, will be cleaned up with pool
        if (ctx->active_formatting) {
            html_formatting_destroy(ctx->active_formatting);
        }
        free(ctx);
    }
}

Element* html_context_ensure_html(HtmlParserContext* ctx) {
    if (!ctx) return NULL;

    if (!ctx->html_element) {
        log_debug("Creating implicit <html> element");
        ctx->html_element = input_create_element(ctx->input, "html");
        ctx->has_explicit_html = false;
    }

    return ctx->html_element;
}

Element* html_context_ensure_head(HtmlParserContext* ctx) {
    if (!ctx) return NULL;

    // Ensure html exists first
    Element* html = html_context_ensure_html(ctx);
    if (!html) return NULL;

    if (!ctx->head_element) {
        log_debug("Creating implicit <head> element");
        ctx->head_element = input_create_element(ctx->input, "head");
        ctx->has_explicit_head = false;

        // Add head to html
        html_append_child(html, (Item){.element = ctx->head_element});
        html_set_content_length(html);
    }

    return ctx->head_element;
}

Element* html_context_ensure_body(HtmlParserContext* ctx) {
    if (!ctx) return NULL;

    // Ensure html exists first
    Element* html = html_context_ensure_html(ctx);
    if (!html) return NULL;

    // Close head if it hasn't been closed yet
    // (even if we never explicitly entered head state)
    if (!ctx->head_closed) {
        log_debug("Closing <head> section (implicitly, body starting)");
        ctx->head_closed = true;
        ctx->in_head = false;
    }

    if (!ctx->body_element) {
        log_debug("Creating implicit <body> element");
        ctx->body_element = input_create_element(ctx->input, "body");
        ctx->has_explicit_body = false;

        // Add body to html
        html_append_child(html, (Item){.element = ctx->body_element});
        html_set_content_length(html);

        ctx->in_body = true;
    }

    return ctx->body_element;
}

Element* html_context_get_insertion_point(HtmlParserContext* ctx, const char* tag_name) {
    if (!ctx) return NULL;

    // Phase 4.2: Use insertion mode to determine placement
    HtmlInsertionMode mode = ctx->insertion_mode;

    // Special cases for document structure elements
    if (strcasecmp(tag_name, "html") == 0) {
        // <html> should be the root
        return NULL;  // Caller will handle as root
    }

    if (strcasecmp(tag_name, "head") == 0) {
        // <head> goes directly in <html>
        return html_context_ensure_html(ctx);
    }

    if (strcasecmp(tag_name, "body") == 0) {
        // <body> goes directly in <html>
        return html_context_ensure_html(ctx);
    }

    // Handle based on insertion mode
    switch (mode) {
        case HTML_MODE_INITIAL:
        case HTML_MODE_BEFORE_HTML:
        case HTML_MODE_BEFORE_HEAD:
            // Before head: head elements go in head, others start body
            if (is_head_element(tag_name)) {
                ctx->in_head = true;
                return html_context_ensure_head(ctx);
            } else {
                return html_context_ensure_body(ctx);
            }

        case HTML_MODE_IN_HEAD:
            // In head: head elements go in head, others close head and go to body
            if (is_head_element(tag_name)) {
                return html_context_ensure_head(ctx);
            } else {
                // Implicitly close head
                if (!ctx->head_closed) {
                    html_context_close_head(ctx);
                }
                return html_context_ensure_body(ctx);
            }

        case HTML_MODE_AFTER_HEAD:
            // After head but before body: everything goes to body
            return html_context_ensure_body(ctx);

        case HTML_MODE_IN_BODY:
        case HTML_MODE_AFTER_BODY:
        case HTML_MODE_AFTER_AFTER_BODY:
            // In or after body: everything goes to body
            // (in real HTML5, AFTER_BODY would ignore most content, but we'll be lenient)
            return html_context_ensure_body(ctx);

        default:
            // Fallback: use body
            return html_context_ensure_body(ctx);
    }
}

void html_context_set_html(HtmlParserContext* ctx, Element* element) {
    if (!ctx) return;

    log_debug("Explicit <html> element found");
    ctx->html_element = element;
    ctx->has_explicit_html = true;
}

void html_context_set_head(HtmlParserContext* ctx, Element* element) {
    if (!ctx) return;

    log_debug("Explicit <head> element found");
    ctx->head_element = element;
    ctx->has_explicit_head = true;
    ctx->in_head = true;
}

void html_context_set_body(HtmlParserContext* ctx, Element* element) {
    if (!ctx) return;

    log_debug("Explicit <body> element found");
    ctx->body_element = element;
    ctx->has_explicit_body = true;
    ctx->in_body = true;

    // Close head when body starts
    if (ctx->in_head && !ctx->head_closed) {
        html_context_close_head(ctx);
    }
}

void html_context_close_head(HtmlParserContext* ctx) {
    if (!ctx) return;

    if (ctx->in_head && !ctx->head_closed) {
        log_debug("Closing <head> section");
        ctx->head_closed = true;
        ctx->in_head = false;
    }
}

// ============================================================================
// Phase 4.2: HTML5 Insertion Mode Implementation
// ============================================================================

HtmlInsertionMode html_context_get_mode(HtmlParserContext* ctx) {
    return ctx ? ctx->insertion_mode : HTML_MODE_INITIAL;
}

void html_context_set_mode(HtmlParserContext* ctx, HtmlInsertionMode mode) {
    if (!ctx) return;

    log_debug("Insertion mode transition: %d -> %d", ctx->insertion_mode, mode);
    ctx->insertion_mode = mode;
}

void html_context_transition_mode(HtmlParserContext* ctx, const char* tag_name, bool is_closing_tag) {
    if (!ctx || !tag_name) return;

    HtmlInsertionMode current_mode = ctx->insertion_mode;

    // Handle DOCTYPE - stays in initial mode but allows progression
    if (strcasecmp(tag_name, "!doctype") == 0) {
        // DOCTYPE is allowed in initial mode, doesn't change mode
        return;
    }

    // Opening tags
    if (!is_closing_tag) {
        if (strcasecmp(tag_name, "html") == 0) {
            // Explicit <html> tag
            switch (current_mode) {
                case HTML_MODE_INITIAL:
                    html_context_set_mode(ctx, HTML_MODE_BEFORE_HEAD);
                    break;
                case HTML_MODE_BEFORE_HTML:
                    html_context_set_mode(ctx, HTML_MODE_BEFORE_HEAD);
                    break;
                default:
                    // Already past html opening, ignore duplicate
                    break;
            }
        } else if (strcasecmp(tag_name, "head") == 0) {
            // Explicit <head> tag
            switch (current_mode) {
                case HTML_MODE_INITIAL:
                case HTML_MODE_BEFORE_HTML:
                case HTML_MODE_BEFORE_HEAD:
                    html_context_set_mode(ctx, HTML_MODE_IN_HEAD);
                    break;
                default:
                    // Head already processed or we're past it
                    break;
            }
        } else if (strcasecmp(tag_name, "body") == 0) {
            // Explicit <body> tag
            switch (current_mode) {
                case HTML_MODE_INITIAL:
                case HTML_MODE_BEFORE_HTML:
                case HTML_MODE_BEFORE_HEAD:
                case HTML_MODE_IN_HEAD:
                case HTML_MODE_AFTER_HEAD:
                    html_context_set_mode(ctx, HTML_MODE_IN_BODY);
                    break;
                default:
                    // Already in body or past it
                    break;
            }
        } else if (is_head_element(tag_name)) {
            // Head content elements (title, meta, link, etc.)
            if (current_mode == HTML_MODE_INITIAL ||
                current_mode == HTML_MODE_BEFORE_HTML ||
                current_mode == HTML_MODE_BEFORE_HEAD) {
                // Implicit head start
                html_context_set_mode(ctx, HTML_MODE_IN_HEAD);
            }
        } else {
            // Body content elements
            if (current_mode != HTML_MODE_IN_BODY &&
                current_mode != HTML_MODE_AFTER_BODY &&
                current_mode != HTML_MODE_AFTER_AFTER_BODY) {
                // Any body content implicitly starts body mode
                html_context_set_mode(ctx, HTML_MODE_IN_BODY);
            }
        }
    } else {
        // Closing tags
        if (strcasecmp(tag_name, "head") == 0) {
            if (current_mode == HTML_MODE_IN_HEAD) {
                html_context_set_mode(ctx, HTML_MODE_AFTER_HEAD);
            }
        } else if (strcasecmp(tag_name, "body") == 0) {
            if (current_mode == HTML_MODE_IN_BODY) {
                html_context_set_mode(ctx, HTML_MODE_AFTER_BODY);
            }
        } else if (strcasecmp(tag_name, "html") == 0) {
            if (current_mode == HTML_MODE_AFTER_BODY) {
                html_context_set_mode(ctx, HTML_MODE_AFTER_AFTER_BODY);
            }
        }
    }
}

// ============================================================================
// Phase 5: Open Element Stack Implementation
// ============================================================================

#define INITIAL_STACK_CAPACITY 16

HtmlElementStack* html_stack_create(Pool* pool) {
    HtmlElementStack* stack = (HtmlElementStack*)pool_alloc(pool, sizeof(HtmlElementStack));
    if (!stack) {
        log_error("Failed to allocate element stack");
        return NULL;
    }

    stack->capacity = INITIAL_STACK_CAPACITY;
    stack->length = 0;
    stack->pool = pool;
    stack->elements = (Element**)pool_alloc(pool, sizeof(Element*) * stack->capacity);

    if (!stack->elements) {
        log_error("Failed to allocate element stack array");
        return NULL;
    }

    return stack;
}

void html_stack_destroy(HtmlElementStack* stack) {
    // Pool-allocated memory, no explicit free needed
    // The pool will handle cleanup
}

void html_stack_push(HtmlElementStack* stack, Element* element) {
    if (!stack || !element) return;

    // Grow if needed
    if (stack->length >= stack->capacity) {
        size_t new_capacity = stack->capacity * 2;
        Element** new_elements = (Element**)pool_alloc(stack->pool, sizeof(Element*) * new_capacity);
        if (!new_elements) {
            log_error("Failed to grow element stack");
            return;
        }

        // Copy existing elements
        memcpy(new_elements, stack->elements, sizeof(Element*) * stack->length);
        stack->elements = new_elements;
        stack->capacity = new_capacity;
    }

    stack->elements[stack->length++] = element;
}

Element* html_stack_pop(HtmlElementStack* stack) {
    if (!stack || stack->length == 0) {
        return NULL;
    }

    return stack->elements[--stack->length];
}

Element* html_stack_peek(HtmlElementStack* stack) {
    if (!stack || stack->length == 0) {
        return NULL;
    }

    return stack->elements[stack->length - 1];
}

Element* html_stack_get(HtmlElementStack* stack, size_t index) {
    if (!stack || index >= stack->length) {
        return NULL;
    }

    return stack->elements[index];
}

size_t html_stack_length(HtmlElementStack* stack) {
    return stack ? stack->length : 0;
}

bool html_stack_is_empty(HtmlElementStack* stack) {
    return !stack || stack->length == 0;
}

bool html_stack_contains(HtmlElementStack* stack, const char* tag_name) {
    if (!stack || !tag_name) return false;

    for (size_t i = 0; i < stack->length; i++) {
        Element* elem = stack->elements[i];
        if (elem && elem->type) {
            TypeElmt* type = (TypeElmt*)elem->type;
            if (strview_equal(&type->name, tag_name)) {
                return true;
            }
        }
    }

    return false;
}

int html_stack_find(HtmlElementStack* stack, const char* tag_name) {
    if (!stack || !tag_name) return -1;

    // Search from top to bottom (most recent first)
    for (int i = (int)stack->length - 1; i >= 0; i--) {
        Element* elem = stack->elements[i];
        if (elem && elem->type) {
            TypeElmt* type = (TypeElmt*)elem->type;
            if (strview_equal(&type->name, tag_name)) {
                return i;
            }
        }
    }

    return -1;
}

bool html_stack_pop_until(HtmlElementStack* stack, Element* element) {
    if (!stack || !element) return false;

    // Pop elements until we find the matching element
    while (stack->length > 0) {
        Element* top = html_stack_pop(stack);
        if (top == element) {
            return true;
        }
    }

    return false;
}

bool html_stack_pop_until_tag(HtmlElementStack* stack, const char* tag_name) {
    if (!stack || !tag_name) return false;

    // Pop elements until we find one with matching tag name
    while (stack->length > 0) {
        Element* top = html_stack_pop(stack);
        if (top && top->type) {
            TypeElmt* type = (TypeElmt*)top->type;
            if (strview_equal(&type->name, tag_name)) {
                return true;
            }
        }
    }

    return false;
}

void html_stack_clear(HtmlElementStack* stack) {
    if (stack) {
        stack->length = 0;
    }
}

Element* html_stack_current_node(HtmlElementStack* stack) {
    return html_stack_peek(stack);
}

// ============================================================================
// Phase 6: Active Formatting Elements Implementation
// ============================================================================

// List of HTML5 formatting elements that need to be tracked
static const char* HTML5_FORMATTING_ELEMENTS[] = {
    "a", "b", "big", "code", "em", "font", "i", "nobr", "s", "small",
    "strike", "strong", "tt", "u", NULL
};

bool html_is_formatting_element(const char* tag_name) {
    if (!tag_name) return false;

    for (int i = 0; HTML5_FORMATTING_ELEMENTS[i]; i++) {
        if (strcmp(tag_name, HTML5_FORMATTING_ELEMENTS[i]) == 0) {
            return true;
        }
    }
    return false;
}

HtmlFormattingList* html_formatting_create(Pool* pool) {
    if (!pool) return NULL;

    HtmlFormattingList* list = (HtmlFormattingList*)pool_alloc(pool, sizeof(HtmlFormattingList));
    if (!list) return NULL;

    list->capacity = 8;  // initial capacity
    list->elements = (HtmlFormattingElement*)pool_alloc(pool,
                                                        sizeof(HtmlFormattingElement) * list->capacity);
    if (!list->elements) return NULL;

    list->length = 0;
    list->pool = pool;

    return list;
}

void html_formatting_destroy(HtmlFormattingList* list) {
    // nothing to do - pool-allocated memory will be cleaned up with pool
    (void)list;
}

void html_formatting_push(HtmlFormattingList* list, Element* element, size_t stack_depth) {
    if (!list || !element) return;

    // grow array if needed
    if (list->length >= list->capacity) {
        size_t new_capacity = list->capacity * 2;
        HtmlFormattingElement* new_elements = (HtmlFormattingElement*)pool_alloc(
            list->pool, sizeof(HtmlFormattingElement) * new_capacity);
        if (!new_elements) return;

        // copy existing elements
        memcpy(new_elements, list->elements, sizeof(HtmlFormattingElement) * list->length);
        list->elements = new_elements;
        list->capacity = new_capacity;
    }

    // add new element
    list->elements[list->length].element = element;
    list->elements[list->length].stack_depth = stack_depth;
    list->length++;
}

bool html_formatting_remove(HtmlFormattingList* list, Element* element) {
    if (!list || !element) return false;

    // search from end to beginning (most recent first)
    for (int i = (int)list->length - 1; i >= 0; i--) {
        if (list->elements[i].element == element) {
            // shift remaining elements down
            memmove(&list->elements[i], &list->elements[i + 1],
                    sizeof(HtmlFormattingElement) * (list->length - i - 1));
            list->length--;
            return true;
        }
    }

    return false;
}

bool html_formatting_remove_tag(HtmlFormattingList* list, const char* tag_name) {
    if (!list || !tag_name) return false;

    // search from end to beginning (most recent first)
    for (int i = (int)list->length - 1; i >= 0; i--) {
        Element* elem = list->elements[i].element;
        if (elem && elem->type) {
            TypeElmt* type = (TypeElmt*)elem->type;
            if (strview_equal(&type->name, tag_name)) {
                // shift remaining elements down
                memmove(&list->elements[i], &list->elements[i + 1],
                        sizeof(HtmlFormattingElement) * (list->length - i - 1));
                list->length--;
                return true;
            }
        }
    }

    return false;
}

bool html_formatting_contains(HtmlFormattingList* list, const char* tag_name) {
    if (!list || !tag_name) return false;

    for (size_t i = 0; i < list->length; i++) {
        Element* elem = list->elements[i].element;
        if (elem && elem->type) {
            TypeElmt* type = (TypeElmt*)elem->type;
            if (strview_equal(&type->name, tag_name)) {
                return true;
            }
        }
    }

    return false;
}

void html_formatting_clear(HtmlFormattingList* list) {
    if (list) {
        list->length = 0;
    }
}

size_t html_formatting_length(HtmlFormattingList* list) {
    return list ? list->length : 0;
}
