#include "html5_parser.h"
#include "../../lib/log.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// ============================================================================
// Stack operations implementation
// ============================================================================

Html5Stack* html5_stack_create(Pool* pool) {
    Html5Stack* stack = (Html5Stack*)pool_calloc(pool, sizeof(Html5Stack));
    if (!stack) {
        log_error("Failed to allocate HTML5 stack");
        return NULL;
    }
    stack->top = NULL;
    stack->size = 0;
    stack->pool = pool;
    return stack;
}

void html5_stack_push(Html5Stack* stack, Element* element) {
    if (!stack || !element) return;

    Html5StackEntry* entry = (Html5StackEntry*)pool_calloc(stack->pool, sizeof(Html5StackEntry));
    if (!entry) {
        log_error("Failed to allocate stack entry");
        return;
    }

    entry->element = element;
    entry->next = stack->top;
    stack->top = entry;
    stack->size++;

    // Get tag name for logging
    TypeElmt* type = (TypeElmt*)element->type;
    const char* tag_name = type ? type->name.str : "unknown";
    log_debug("Stack push: <%s> (size: %zu)", tag_name, stack->size);
}

Element* html5_stack_pop(Html5Stack* stack) {
    if (!stack || !stack->top) return NULL;

    Html5StackEntry* entry = stack->top;
    Element* element = entry->element;
    stack->top = entry->next;
    stack->size--;

    // Get tag name for logging
    TypeElmt* type = (TypeElmt*)element->type;
    const char* tag_name = type ? type->name.str : "unknown";
    log_debug("Stack pop: <%s> (size: %zu)", tag_name, stack->size);

    // Note: We don't free the entry as it's pool-allocated
    return element;
}

Element* html5_stack_peek(Html5Stack* stack) {
    if (!stack || !stack->top) return NULL;
    return stack->top->element;
}

Element* html5_stack_peek_at(Html5Stack* stack, size_t index) {
    if (!stack || !stack->top) return NULL;

    Html5StackEntry* entry = stack->top;
    for (size_t i = 0; i < index && entry; i++) {
        entry = entry->next;
    }

    return entry ? entry->element : NULL;
}

bool html5_stack_is_empty(Html5Stack* stack) {
    return !stack || stack->size == 0;
}

size_t html5_stack_size(Html5Stack* stack) {
    return stack ? stack->size : 0;
}

void html5_stack_clear(Html5Stack* stack) {
    if (!stack) return;
    stack->top = NULL;
    stack->size = 0;
    log_debug("Stack cleared");
}

bool html5_stack_contains(Html5Stack* stack, const char* tag_name) {
    if (!stack || !tag_name) return false;

    Html5StackEntry* entry = stack->top;
    while (entry) {
        TypeElmt* type = (TypeElmt*)entry->element->type;
        if (type && strcasecmp(type->name.str, tag_name) == 0) {
            return true;
        }
        entry = entry->next;
    }

    return false;
}

Element* html5_stack_find(Html5Stack* stack, const char* tag_name) {
    if (!stack || !tag_name) return NULL;

    Html5StackEntry* entry = stack->top;
    while (entry) {
        TypeElmt* type = (TypeElmt*)entry->element->type;
        if (type && strcasecmp(type->name.str, tag_name) == 0) {
            return entry->element;
        }
        entry = entry->next;
    }

    return NULL;
}

void html5_stack_pop_until(Html5Stack* stack, const char* tag_name) {
    if (!stack || !tag_name) return;

    while (!html5_stack_is_empty(stack)) {
        Element* element = html5_stack_peek(stack);
        TypeElmt* type = (TypeElmt*)element->type;
        const char* current_tag = type ? type->name.str : NULL;

        html5_stack_pop(stack);

        if (current_tag && strcasecmp(current_tag, tag_name) == 0) {
            break;
        }
    }
}

void html5_stack_remove(Html5Stack* stack, Element* element) {
    if (!stack || !element) return;

    Html5StackEntry** indirect = &stack->top;
    while (*indirect) {
        if ((*indirect)->element == element) {
            *indirect = (*indirect)->next;
            stack->size--;

            TypeElmt* type = (TypeElmt*)element->type;
            const char* tag_name = type ? type->name.str : "unknown";
            log_debug("Stack remove: <%s> (size: %zu)", tag_name, stack->size);
            return;
        }
        indirect = &(*indirect)->next;
    }
}

// ============================================================================
// Active formatting elements operations
// ============================================================================

Html5FormattingList* html5_formatting_list_create(Pool* pool) {
    Html5FormattingList* list = (Html5FormattingList*)pool_calloc(pool, sizeof(Html5FormattingList));
    if (!list) {
        log_error("Failed to allocate formatting list");
        return NULL;
    }
    list->head = NULL;
    list->size = 0;
    list->pool = pool;
    return list;
}

void html5_formatting_list_push(Html5FormattingList* list, Element* element) {
    if (!list || !element) return;

    Html5FormattingElement* entry = (Html5FormattingElement*)pool_calloc(list->pool, sizeof(Html5FormattingElement));
    if (!entry) {
        log_error("Failed to allocate formatting element");
        return;
    }

    entry->element = element;
    entry->is_marker = false;
    entry->next = list->head;
    list->head = entry;
    list->size++;

    TypeElmt* type = (TypeElmt*)element->type;
    const char* tag_name = type ? type->name.str : "unknown";
    log_debug("Formatting list push: <%s> (size: %zu)", tag_name, list->size);
}

void html5_formatting_list_push_marker(Html5FormattingList* list) {
    if (!list) return;

    Html5FormattingElement* entry = (Html5FormattingElement*)pool_calloc(list->pool, sizeof(Html5FormattingElement));
    if (!entry) {
        log_error("Failed to allocate formatting marker");
        return;
    }

    entry->element = NULL;
    entry->is_marker = true;
    entry->next = list->head;
    list->head = entry;
    list->size++;

    log_debug("Formatting list push marker (size: %zu)", list->size);
}

Element* html5_formatting_list_pop(Html5FormattingList* list) {
    if (!list || !list->head) return NULL;

    Html5FormattingElement* entry = list->head;
    Element* element = entry->element;
    list->head = entry->next;
    list->size--;

    if (entry->is_marker) {
        log_debug("Formatting list pop marker (size: %zu)", list->size);
    } else {
        TypeElmt* type = element ? (TypeElmt*)element->type : NULL;
        const char* tag_name = type ? type->name.str : "unknown";
        log_debug("Formatting list pop: <%s> (size: %zu)", tag_name, list->size);
    }

    return element;
}

void html5_formatting_list_clear_to_marker(Html5FormattingList* list) {
    if (!list) return;

    while (list->head) {
        Html5FormattingElement* entry = list->head;
        bool is_marker = entry->is_marker;

        list->head = entry->next;
        list->size--;

        if (is_marker) {
            log_debug("Formatting list cleared to marker (size: %zu)", list->size);
            break;
        }
    }
}

bool html5_formatting_list_contains(Html5FormattingList* list, const char* tag_name) {
    if (!list || !tag_name) return false;

    Html5FormattingElement* entry = list->head;
    while (entry) {
        if (!entry->is_marker && entry->element) {
            TypeElmt* type = (TypeElmt*)entry->element->type;
            if (type && strcasecmp(type->name.str, tag_name) == 0) {
                return true;
            }
        }
        entry = entry->next;
    }

    return false;
}

Element* html5_formatting_list_find(Html5FormattingList* list, const char* tag_name) {
    if (!list || !tag_name) return NULL;

    Html5FormattingElement* entry = list->head;
    while (entry) {
        if (!entry->is_marker && entry->element) {
            TypeElmt* type = (TypeElmt*)entry->element->type;
            if (type && strcasecmp(type->name.str, tag_name) == 0) {
                return entry->element;
            }
        }
        entry = entry->next;
    }

    return NULL;
}

void html5_formatting_list_remove(Html5FormattingList* list, Element* element) {
    if (!list || !element) return;

    Html5FormattingElement** indirect = &list->head;
    while (*indirect) {
        if ((*indirect)->element == element) {
            *indirect = (*indirect)->next;
            list->size--;

            TypeElmt* type = (TypeElmt*)element->type;
            const char* tag_name = type ? type->name.str : "unknown";
            log_debug("Formatting list remove: <%s> (size: %zu)", tag_name, list->size);
            return;
        }
        indirect = &(*indirect)->next;
    }
}

void html5_formatting_list_replace(Html5FormattingList* list, Element* old_element, Element* new_element) {
    if (!list || !old_element || !new_element) return;

    Html5FormattingElement* entry = list->head;
    while (entry) {
        if (entry->element == old_element) {
            entry->element = new_element;

            TypeElmt* old_type = (TypeElmt*)old_element->type;
            TypeElmt* new_type = (TypeElmt*)new_element->type;
            const char* old_tag = old_type ? old_type->name.str : "unknown";
            const char* new_tag = new_type ? new_type->name.str : "unknown";
            log_debug("Formatting list replace: <%s> -> <%s>", old_tag, new_tag);
            return;
        }
        entry = entry->next;
    }
}

// ============================================================================
// Parser operations
// ============================================================================

Html5Parser* html5_parser_create(Input* input, const char* html, Pool* pool) {
    if (!input || !html || !pool) {
        log_error("Invalid arguments to html5_parser_create");
        return NULL;
    }

    Html5Parser* parser = (Html5Parser*)pool_calloc(pool, sizeof(Html5Parser));
    if (!parser) {
        log_error("Failed to allocate HTML5 parser");
        return NULL;
    }

    parser->input = input;
    parser->html_start = html;
    parser->html_current = html;
    parser->insertion_mode = HTML5_MODE_INITIAL;
    parser->original_insertion_mode = HTML5_MODE_INITIAL;
    parser->pool = pool;

    // Create stacks and lists
    parser->open_elements = html5_stack_create(pool);
    parser->active_formatting_elements = html5_formatting_list_create(pool);
    parser->template_insertion_modes = html5_stack_create(pool);

    // Initialize flags
    parser->scripting_enabled = true;  // Default: scripting enabled
    parser->foster_parenting = false;
    parser->frameset_ok = true;
    parser->quirks_mode = QUIRKS_MODE_NO_QUIRKS;

    // Initialize element pointers
    parser->document = NULL;
    parser->html_element = NULL;
    parser->head_element = NULL;
    parser->form_element = NULL;

    // Initialize error tracking
    parser->errors = NULL;
    parser->error_count = 0;

    log_info("HTML5 parser created");

    return parser;
}

void html5_parser_destroy(Html5Parser* parser) {
    if (!parser) return;

    // Note: All memory is pool-allocated, so we don't need to free individual items
    log_info("HTML5 parser destroyed (parsed %zu errors)", parser->error_count);
}

void html5_parser_set_mode(Html5Parser* parser, Html5InsertionMode mode) {
    if (!parser) return;

    Html5InsertionMode old_mode = parser->insertion_mode;
    parser->insertion_mode = mode;

    log_debug("Insertion mode: %s -> %s",
              html5_mode_name(old_mode),
              html5_mode_name(mode));
}

const char* html5_mode_name(Html5InsertionMode mode) {
    switch (mode) {
        case HTML5_MODE_INITIAL: return "initial";
        case HTML5_MODE_BEFORE_HTML: return "before html";
        case HTML5_MODE_BEFORE_HEAD: return "before head";
        case HTML5_MODE_IN_HEAD: return "in head";
        case HTML5_MODE_IN_HEAD_NOSCRIPT: return "in head noscript";
        case HTML5_MODE_AFTER_HEAD: return "after head";
        case HTML5_MODE_IN_BODY: return "in body";
        case HTML5_MODE_TEXT: return "text";
        case HTML5_MODE_IN_TABLE: return "in table";
        case HTML5_MODE_IN_TABLE_TEXT: return "in table text";
        case HTML5_MODE_IN_CAPTION: return "in caption";
        case HTML5_MODE_IN_COLUMN_GROUP: return "in column group";
        case HTML5_MODE_IN_TABLE_BODY: return "in table body";
        case HTML5_MODE_IN_ROW: return "in row";
        case HTML5_MODE_IN_CELL: return "in cell";
        case HTML5_MODE_IN_SELECT: return "in select";
        case HTML5_MODE_IN_SELECT_IN_TABLE: return "in select in table";
        case HTML5_MODE_IN_TEMPLATE: return "in template";
        case HTML5_MODE_AFTER_BODY: return "after body";
        case HTML5_MODE_IN_FRAMESET: return "in frameset";
        case HTML5_MODE_AFTER_FRAMESET: return "after frameset";
        case HTML5_MODE_AFTER_AFTER_BODY: return "after after body";
        case HTML5_MODE_AFTER_AFTER_FRAMESET: return "after after frameset";
        default: return "unknown";
    }
}

void html5_parser_error(Html5Parser* parser, const char* error_code, const char* message) {
    if (!parser) return;

    Html5ParseError* error = (Html5ParseError*)pool_calloc(parser->pool, sizeof(Html5ParseError));
    if (!error) {
        log_error("Failed to allocate parse error");
        return;
    }

    error->error_code = error_code;
    error->message = message;

    // Calculate line and column (simple version)
    error->line = 1;
    error->column = 1;
    for (const char* p = parser->html_start; p < parser->html_current; p++) {
        if (*p == '\n') {
            error->line++;
            error->column = 1;
        } else {
            error->column++;
        }
    }

    // Add to error list
    error->next = parser->errors;
    parser->errors = error;
    parser->error_count++;

    log_warn("Parse error at %d:%d - %s: %s",
             error->line, error->column, error_code, message);
}

// ============================================================================
// Scope checking algorithms (HTML5 spec)
// ============================================================================

static bool is_in_scope_list(const char* tag_name, const char** scope_list) {
    for (int i = 0; scope_list[i]; i++) {
        if (strcasecmp(tag_name, scope_list[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool html5_has_element_in_scope(Html5Parser* parser, const char* tag_name) {
    if (!parser || !tag_name) return false;

    // List of elements that define scope boundaries
    static const char* scope_elements[] = {
        "applet", "caption", "html", "table", "td", "th", "marquee", "object", "template",
        // SVG elements
        "mi", "mo", "mn", "ms", "mtext", "annotation-xml",
        // MathML elements
        "foreignObject", "desc", "title",
        NULL
    };

    Html5StackEntry* entry = parser->open_elements->top;
    bool first = true;  // Don't check scope boundary for the first element
    
    while (entry) {
        TypeElmt* type = (TypeElmt*)entry->element->type;
        if (!type) {
            log_debug("Scope check: element has no type, skipping");
            entry = entry->next;
            first = false;
            continue;
        }

        const char* current_tag = type->name.str;
        log_debug("Scope check for '%s': examining '%s'", tag_name, current_tag);

        // Found the target element
        if (strcasecmp(current_tag, tag_name) == 0) {
            log_debug("Scope check: found '%s'", tag_name);
            return true;
        }

        // Hit a scope boundary (but not the element we're looking for)
        // Skip boundary check for the first element (current node)
        if (!first && is_in_scope_list(current_tag, scope_elements)) {
            log_debug("Scope check: hit boundary '%s', stopping", current_tag);
            return false;
        }

        entry = entry->next;
        first = false;
    }

    log_debug("Scope check: reached end of stack without finding '%s'", tag_name);
    return false;
}

bool html5_has_element_in_button_scope(Html5Parser* parser, const char* tag_name) {
    if (!parser || !tag_name) return false;

    // Button scope is like regular scope plus "button"
    static const char* button_scope_elements[] = {
        "applet", "caption", "html", "table", "td", "th", "marquee", "object", "template",
        "button",
        // SVG elements
        "mi", "mo", "mn", "ms", "mtext", "annotation-xml",
        // MathML elements
        "foreignObject", "desc", "title",
        NULL
    };

    Html5StackEntry* entry = parser->open_elements->top;
    while (entry) {
        TypeElmt* type = (TypeElmt*)entry->element->type;
        if (!type) {
            entry = entry->next;
            continue;
        }

        const char* current_tag = type->name.str;

        if (strcasecmp(current_tag, tag_name) == 0) {
            return true;
        }

        if (is_in_scope_list(current_tag, button_scope_elements)) {
            return false;
        }

        entry = entry->next;
    }

    return false;
}

bool html5_has_element_in_list_item_scope(Html5Parser* parser, const char* tag_name) {
    if (!parser || !tag_name) return false;

    // List item scope is like regular scope plus "ol" and "ul"
    static const char* list_scope_elements[] = {
        "applet", "caption", "html", "table", "td", "th", "marquee", "object", "template",
        "ol", "ul",
        // SVG elements
        "mi", "mo", "mn", "ms", "mtext", "annotation-xml",
        // MathML elements
        "foreignObject", "desc", "title",
        NULL
    };

    Html5StackEntry* entry = parser->open_elements->top;
    while (entry) {
        TypeElmt* type = (TypeElmt*)entry->element->type;
        if (!type) {
            entry = entry->next;
            continue;
        }

        const char* current_tag = type->name.str;

        if (strcasecmp(current_tag, tag_name) == 0) {
            return true;
        }

        if (is_in_scope_list(current_tag, list_scope_elements)) {
            return false;
        }

        entry = entry->next;
    }

    return false;
}

bool html5_has_element_in_table_scope(Html5Parser* parser, const char* tag_name) {
    if (!parser || !tag_name) return false;

    // Table scope only includes "html", "table", and "template"
    static const char* table_scope_elements[] = {
        "html", "table", "template",
        NULL
    };

    Html5StackEntry* entry = parser->open_elements->top;
    while (entry) {
        TypeElmt* type = (TypeElmt*)entry->element->type;
        if (!type) {
            entry = entry->next;
            continue;
        }

        const char* current_tag = type->name.str;

        if (strcasecmp(current_tag, tag_name) == 0) {
            return true;
        }

        if (is_in_scope_list(current_tag, table_scope_elements)) {
            return false;
        }

        entry = entry->next;
    }

    return false;
}

bool html5_has_element_in_select_scope(Html5Parser* parser, const char* tag_name) {
    if (!parser || !tag_name) return false;

    // Select scope: all elements EXCEPT "optgroup" and "option" are scope boundaries
    Html5StackEntry* entry = parser->open_elements->top;
    while (entry) {
        TypeElmt* type = (TypeElmt*)entry->element->type;
        if (!type) {
            entry = entry->next;
            continue;
        }

        const char* current_tag = type->name.str;

        if (strcasecmp(current_tag, tag_name) == 0) {
            return true;
        }

        // In select scope, everything except optgroup and option is a boundary
        if (strcasecmp(current_tag, "optgroup") != 0 &&
            strcasecmp(current_tag, "option") != 0) {
            return false;
        }

        entry = entry->next;
    }

    return false;
}
