#include "html5_parser.h"
#include "../../../lib/log.h"
#include "../../mark_builder.hpp"
#include <string.h>
#include <assert.h>

// parser lifecycle
Html5Parser* html5_parser_create(Pool* pool, Arena* arena, Input* input) {
    Html5Parser* parser = (Html5Parser*)pool_calloc(pool, sizeof(Html5Parser));
    parser->pool = pool;
    parser->arena = arena;
    parser->input = input;

    // initialize stacks
    parser->open_elements = list_arena(arena);
    parser->active_formatting = list_arena(arena);
    parser->template_modes = list_arena(arena);

    // initial mode
    parser->mode = HTML5_MODE_INITIAL;
    parser->original_insertion_mode = HTML5_MODE_INITIAL;

    // flags
    parser->scripting_enabled = true;
    parser->frameset_ok = true;
    parser->foster_parenting = false;
    parser->ignore_next_lf = false;

    // temporary buffer (4KB initial capacity)
    parser->temp_buffer_capacity = 4096;
    parser->temp_buffer = (char*)arena_alloc(arena, parser->temp_buffer_capacity);
    parser->temp_buffer_len = 0;

    // text content buffering
    parser->text_buffer = stringbuf_new(pool);
    parser->pending_text_parent = nullptr;

    return parser;
}

void html5_parser_destroy(Html5Parser* parser) {
    // memory is pool/arena-managed, nothing to free explicitly
    (void)parser;
}

// stack operations - these implement the "stack of open elements" from WHATWG spec
Element* html5_current_node(Html5Parser* parser) {
    if (parser->open_elements->length == 0) {
        return nullptr;
    }
    return (Element*)parser->open_elements->items[parser->open_elements->length - 1].element;
}

void html5_push_element(Html5Parser* parser, Element* elem) {
    Item item;
    item.element = elem;
    array_append(parser->open_elements, item, parser->pool, parser->arena);
    log_debug("html5: pushed element <%s>, stack depth now %zu", ((TypeElmt*)elem->type)->name.str, parser->open_elements->length);
}

Element* html5_pop_element(Html5Parser* parser) {
    if (parser->open_elements->length == 0) {
        log_error("html5: attempted to pop from empty stack");
        return nullptr;
    }

    Element* elem = (Element*)parser->open_elements->items[parser->open_elements->length - 1].element;
    parser->open_elements->length--;
    log_debug("html5: popped element <%s>, stack depth now %zu", ((TypeElmt*)elem->type)->name.str, parser->open_elements->length);
    return elem;
}

// scope checking - implements "has an element in scope" algorithms from WHATWG spec
static bool is_scope_marker(const char* tag_name, const char** scope_list, size_t scope_len) {
    for (size_t i = 0; i < scope_len; i++) {
        if (strcmp(tag_name, scope_list[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool has_element_in_scope_generic(Html5Parser* parser, const char* target_tag_name,
                                          const char** scope_list, size_t scope_len) {
    // traverse stack from top to bottom
    for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
        Element* elem = (Element*)parser->open_elements->items[i].element;
        const char* tag_name = ((TypeElmt*)elem->type)->name.str;

        if (strcmp(tag_name, target_tag_name) == 0) {
            return true;
        }

        if (is_scope_marker(tag_name, scope_list, scope_len)) {
            return false;
        }
    }
    return false;
}

bool html5_has_element_in_scope(Html5Parser* parser, const char* tag_name) {
    // standard scope markers: applet, caption, html, table, td, th, marquee, object, template,
    // plus MathML mi, mo, mn, ms, mtext, annotation-xml, and SVG foreignObject, desc, title
    static const char* scope_markers[] = {
        "applet", "caption", "html", "table", "td", "th", "marquee", "object", "template"
    };
    return has_element_in_scope_generic(parser, tag_name, scope_markers, 9);
}

bool html5_has_element_in_button_scope(Html5Parser* parser, const char* tag_name) {
    // button scope = standard scope + button
    static const char* scope_markers[] = {
        "applet", "caption", "html", "table", "td", "th", "marquee", "object", "template", "button"
    };
    return has_element_in_scope_generic(parser, tag_name, scope_markers, 10);
}

bool html5_has_element_in_table_scope(Html5Parser* parser, const char* tag_name) {
    // table scope = html, table, template
    static const char* scope_markers[] = {"html", "table", "template"};
    return has_element_in_scope_generic(parser, tag_name, scope_markers, 3);
}

bool html5_has_element_in_list_item_scope(Html5Parser* parser, const char* tag_name) {
    // list item scope = standard scope + ol, ul
    static const char* scope_markers[] = {
        "applet", "caption", "html", "table", "td", "th", "marquee", "object", "template", "ol", "ul"
    };
    return has_element_in_scope_generic(parser, tag_name, scope_markers, 11);
}

bool html5_has_element_in_select_scope(Html5Parser* parser, const char* tag_name) {
    // select scope = all elements EXCEPT optgroup and option
    for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
        Element* elem = (Element*)parser->open_elements->items[i].element;
        const char* elem_tag = ((TypeElmt*)elem->type)->name.str;

        if (strcmp(elem_tag, tag_name) == 0) {
            return true;
        }

        if (strcmp(elem_tag, "optgroup") != 0 && strcmp(elem_tag, "option") != 0) {
            return false;
        }
    }
    return false;
}

// implied end tags - implements "generate implied end tags" from WHATWG spec
void html5_generate_implied_end_tags(Html5Parser* parser) {
    static const char* implied_tags[] = {
        "dd", "dt", "li", "optgroup", "option", "p", "rb", "rp", "rt", "rtc"
    };

    while (parser->open_elements->length > 0) {
        Element* current = html5_current_node(parser);
        const char* tag_name = ((TypeElmt*)current->type)->name.str;

        bool is_implied = false;
        for (size_t i = 0; i < 10; i++) {
            if (strcmp(tag_name, implied_tags[i]) == 0) {
                is_implied = true;
                break;
            }
        }

        if (!is_implied) {
            break;
        }

        html5_pop_element(parser);
    }
}

void html5_generate_implied_end_tags_except(Html5Parser* parser, const char* exception_tag) {
    static const char* implied_tags[] = {
        "dd", "dt", "li", "optgroup", "option", "p", "rb", "rp", "rt", "rtc"
    };

    while (parser->open_elements->length > 0) {
        Element* current = html5_current_node(parser);
        const char* tag_name = ((TypeElmt*)current->type)->name.str;

        if (strcmp(tag_name, exception_tag) == 0) {
            break;
        }

        bool is_implied = false;
        for (size_t i = 0; i < 10; i++) {
            if (strcmp(tag_name, implied_tags[i]) == 0) {
                is_implied = true;
                break;
            }
        }

        if (!is_implied) {
            break;
        }

        html5_pop_element(parser);
    }
}

// active formatting elements - implements "reconstruct the active formatting elements" from WHATWG spec
void html5_reconstruct_active_formatting_elements(Html5Parser* parser) {
    // step 1: if there are no entries in the list, stop
    if (parser->active_formatting->length == 0) {
        return;
    }

    // step 2: if the last entry is a marker or in the stack, stop
    int entry_idx = (int)parser->active_formatting->length - 1;
    Item entry = parser->active_formatting->items[entry_idx];

    if (entry.element == nullptr) {  // marker
        return;
    }

    // check if entry is in stack
    bool in_stack = false;
    for (size_t i = 0; i < parser->open_elements->length; i++) {
        if (parser->open_elements->items[i].element == entry.element) {
            in_stack = true;
            break;
        }
    }
    if (in_stack) {
        return;
    }

    // step 3-6: rewind to find first entry not in stack
    while (entry_idx > 0) {
        entry_idx--;
        entry = parser->active_formatting->items[entry_idx];

        if (entry.element == nullptr) {  // marker
            entry_idx++;
            break;
        }

        in_stack = false;
        for (size_t i = 0; i < parser->open_elements->length; i++) {
            if (parser->open_elements->items[i].element == entry.element) {
                in_stack = true;
                break;
            }
        }
        if (in_stack) {
            entry_idx++;
            break;
        }
    }

    // step 7-10: create and insert elements
    while (entry_idx < (int)parser->active_formatting->length) {
        Element* old_elem = (Element*)parser->active_formatting->items[entry_idx].element;

        // create new element with same name
        MarkBuilder builder(parser->input);
        const char* tag_name = ((TypeElmt*)old_elem->type)->name.str;
        Element* new_elem = builder.element(tag_name).final().element;

        // insert into appropriate place in stack
        Element* parent = html5_current_node(parser);
        array_append(parent, Item{.element = new_elem}, parser->pool, parser->arena);
        html5_push_element(parser, new_elem);

        // replace entry in active formatting list
        parser->active_formatting->items[entry_idx].element = new_elem;

        entry_idx++;
    }
}

void html5_clear_active_formatting_to_marker(Html5Parser* parser) {
    while (parser->active_formatting->length > 0) {
        Item entry = parser->active_formatting->items[parser->active_formatting->length - 1];
        parser->active_formatting->length--;

        if (entry.element == nullptr) {  // marker
            break;
        }
    }
}

// element insertion helpers
Element* html5_insert_html_element(Html5Parser* parser, Html5Token* token) {
    // Flush any pending text before inserting element
    html5_flush_pending_text(parser);

    MarkBuilder builder(parser->input);
    // TODO: properly handle attributes from token->attributes map
    Element* elem = builder.element(token->tag_name->chars).final().element;

    // insert into tree
    Element* parent = html5_current_node(parser);
    if (parent != nullptr) {
        array_append((Array*)parent, Item{.element = elem}, parser->pool, parser->arena);
    } else {
        // no parent - must be root html element, add to document
        array_append((Array*)parser->document, Item{.element = elem}, parser->pool, parser->arena);
    }

    // push onto stack
    html5_push_element(parser, elem);

    log_debug("html5: inserted element <%s>", token->tag_name->chars);
    return elem;
}

// Flush pending text buffer to parent element as a single text node
void html5_flush_pending_text(Html5Parser* parser) {
    if (parser->text_buffer->length == 0) {
        return;  // nothing to flush
    }

    Element* parent = parser->pending_text_parent;
    if (parent == nullptr) {
        parent = html5_current_node(parser);
    }
    if (parent == nullptr) {
        log_error("html5: cannot flush text, no parent element");
        stringbuf_reset(parser->text_buffer);
        parser->pending_text_parent = nullptr;
        return;
    }

    // Convert buffer to String and create text node
    String* text_str = stringbuf_to_string(parser->text_buffer);
    Item text_node = {.item = s2it(text_str)};
    array_append((Array*)parent, text_node, parser->pool, parser->arena);

    // Reset buffer for next text run
    stringbuf_reset(parser->text_buffer);
    parser->pending_text_parent = nullptr;
}

void html5_insert_character(Html5Parser* parser, char c) {
    Element* parent = html5_current_node(parser);
    if (parent == nullptr) {
        log_error("html5: cannot insert character, no current node");
        return;
    }

    // If parent changed, flush previous text first
    if (parser->pending_text_parent != nullptr && parser->pending_text_parent != parent) {
        html5_flush_pending_text(parser);
    }

    // Buffer the character
    stringbuf_append_char(parser->text_buffer, c);
    parser->pending_text_parent = parent;
}

void html5_insert_comment(Html5Parser* parser, Html5Token* token) {
    // Flush any pending text before inserting comment
    html5_flush_pending_text(parser);

    // comments are stored as special element nodes with name "#comment"
    MarkBuilder builder(parser->input);

    // Get comment data - might be empty or null
    const char* comment_data = "";
    size_t comment_len = 0;
    if (token->data && token->data->chars) {
        comment_data = token->data->chars;
        comment_len = token->data->len;
    }

    // Create the comment element - need to handle empty comments specially
    // because createString("") returns EMPTY_STRING which has "lambda.nil" content
    ElementBuilder elem_builder = builder.element("#comment");

    // Only set data attribute if there's actual content
    // For empty comments, we still need the data attribute but with empty value
    // We'll handle this by creating a zero-length string if needed
    if (comment_len > 0) {
        elem_builder.attr("data", comment_data);
    } else {
        // For empty comment, store a single space as placeholder
        // The test expects "<!--  -->" (with space) for empty comments
        elem_builder.attr("data", "");
    }

    Element* comment = elem_builder.final().element;

    Element* parent = html5_current_node(parser);
    if (parent == nullptr) {
        parent = parser->document;
    }

    array_append(parent, Item{.element = comment}, parser->pool, parser->arena);
    log_debug("html5: inserted comment with data len=%zu", comment_len);
}
