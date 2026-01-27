#include "format-utils.h"
#include <string.h>
#include <stdio.h>

// ==============================================================================
// Formatter Context Implementation
// ==============================================================================

FormatterContext* formatter_context_create(Pool* pool, StringBuf* output) {
    if (!pool || !output) return NULL;
    
    FormatterContext* ctx = (FormatterContext*)pool_alloc(pool, sizeof(FormatterContext));
    if (!ctx) return NULL;
    
    ctx->output = output;
    ctx->pool = pool;
    ctx->recursion_depth = 0;
    ctx->indent_level = 0;
    ctx->compact_mode = false;
    ctx->format_specific_state = NULL;
    
    return ctx;
}

void formatter_context_destroy(FormatterContext* ctx) {
    // context is pool-allocated, so no explicit free needed
    // just clear the pointer for safety
    if (ctx) {
        ctx->format_specific_state = NULL;
    }
}

// ==============================================================================
// Formatter Dispatcher Implementation
// ==============================================================================

// entry stored in the hashmap: type name + function pointer
typedef struct {
    const char* type_name;
    ElementFormatterFunc handler;
} HandlerEntry;

// hash function for HandlerEntry - hashes the type name
static uint64_t handler_entry_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const HandlerEntry* entry = (const HandlerEntry*)item;
    return hashmap_sip(entry->type_name, strlen(entry->type_name), seed0, seed1);
}

// compare function for HandlerEntry - compares type names
static int handler_entry_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const HandlerEntry* ea = (const HandlerEntry*)a;
    const HandlerEntry* eb = (const HandlerEntry*)b;
    return strcmp(ea->type_name, eb->type_name);
}

FormatterDispatcher* dispatcher_create(Pool* pool) {
    if (!pool) return NULL;
    
    FormatterDispatcher* dispatcher = (FormatterDispatcher*)pool_alloc(pool, sizeof(FormatterDispatcher));
    if (!dispatcher) return NULL;
    
    // create hashmap with initial capacity of 32
    dispatcher->type_handlers = hashmap_new(sizeof(HandlerEntry), 32, 0, 0,
        handler_entry_hash, handler_entry_compare, NULL, NULL);
    if (!dispatcher->type_handlers) {
        return NULL;
    }
    
    dispatcher->default_handler = NULL;
    dispatcher->pool = pool;
    
    return dispatcher;
}

void dispatcher_register(FormatterDispatcher* d, const char* type, ElementFormatterFunc fn) {
    if (!d || !type || !fn || !d->type_handlers) return;
    
    // store function pointer in hashmap
    HandlerEntry entry;
    entry.type_name = type;  // assuming type is a static or pooled string
    entry.handler = fn;
    hashmap_set(d->type_handlers, &entry);
}

void dispatcher_set_default(FormatterDispatcher* d, ElementFormatterFunc fn) {
    if (!d) return;
    d->default_handler = fn;
}

void dispatcher_format(FormatterDispatcher* d, StringBuf* sb, const ElementReader& elem) {
    if (!d || !sb || !d->type_handlers) return;
    
    const char* tag_name = elem.tagName();
    if (!tag_name) {
        // no tag name, use default handler
        if (d->default_handler) {
            d->default_handler(sb, elem);
        }
        return;
    }
    
    // look up handler in map
    HandlerEntry key;
    key.type_name = tag_name;
    key.handler = NULL;
    const HandlerEntry* found = (const HandlerEntry*)hashmap_get(d->type_handlers, &key);
    
    if (found) {
        // found specific handler
        found->handler(sb, elem);
    } else if (d->default_handler) {
        // use default handler
        d->default_handler(sb, elem);
    }
    // if no handler found and no default, do nothing
}

void dispatcher_destroy(FormatterDispatcher* d) {
    if (!d) return;
    
    if (d->type_handlers) {
        hashmap_free(d->type_handlers);
        d->type_handlers = NULL;
    }
    
    // note: dispatcher itself is pool-allocated, no need to free
}

// ==============================================================================
// Common Text Processing
// ==============================================================================

// format raw text without any escaping, handling EMPTY_STRING and lambda.nil
void format_raw_text_common(StringBuf* sb, String* str) {
    if (!sb || !str || str->len == 0) return;

    // check if this is the EMPTY_STRING and handle it specially
    if (str == &EMPTY_STRING) {
        return; // don't output anything for empty string
    } else if (str->len == 10 && strncmp(str->chars, "lambda.nil", 10) == 0) {
        return; // don't output anything for lambda.nil content
    } else {
        stringbuf_append_str_n(sb, str->chars, str->len);
    }
}

// helper function to get markdown escape sequence
static const char* markdown_escape(char c) {
    switch (c) {
        case '*':
        case '_':
        case '`':
        case '#':
        case '[':
        case ']':
        case '(':
        case ')':
        case '\\':
            return NULL; // use backslash prefix
        default:
            return NULL;
    }
}

// helper function to get wiki escape sequence
static const char* wiki_escape(char c) {
    switch (c) {
        case '[':
        case ']':
        case '{':
        case '}':
        case '|':
            return NULL; // use backslash prefix
        default:
            return NULL;
    }
}

// helper function to get rst escape sequence
static const char* rst_escape(char c) {
    switch (c) {
        case '*':
        case '`':
        case '_':
        case '\\':
        case '[':
        case ']':
        case '|':
            return NULL; // use backslash prefix
        default:
            return NULL;
    }
}

// predefined escape configurations
const TextEscapeConfig MARKDOWN_ESCAPE_CONFIG = {
    .chars_to_escape = "*_`#[]()\\",
    .use_backslash_escape = true,
    .escape_fn = markdown_escape
};

const TextEscapeConfig WIKI_ESCAPE_CONFIG = {
    .chars_to_escape = "[]{}|",
    .use_backslash_escape = true,
    .escape_fn = wiki_escape
};

const TextEscapeConfig RST_ESCAPE_CONFIG = {
    .chars_to_escape = "*`_\\[]|",
    .use_backslash_escape = true,
    .escape_fn = rst_escape
};

// format text with configurable escaping
void format_text_with_escape(StringBuf* sb, String* str, const TextEscapeConfig* config) {
    if (!sb || !str || str->len == 0 || !config) return;

    const char* s = str->chars;
    size_t len = str->len;

    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        bool needs_escape = false;

        // check if character needs escaping
        if (config->chars_to_escape) {
            for (const char* p = config->chars_to_escape; *p; p++) {
                if (c == *p) {
                    needs_escape = true;
                    break;
                }
            }
        }

        if (needs_escape) {
            if (config->use_backslash_escape) {
                // use backslash escape
                stringbuf_append_char(sb, '\\');
                stringbuf_append_char(sb, c);
            } else if (config->escape_fn) {
                // use custom escape function
                const char* escaped = config->escape_fn(c);
                if (escaped) {
                    stringbuf_append_str(sb, escaped);
                } else {
                    // fallback to character itself
                    stringbuf_append_char(sb, c);
                }
            } else {
                // no escaping method specified
                stringbuf_append_char(sb, c);
            }
        } else {
            stringbuf_append_char(sb, c);
        }
    }
}

// ==============================================================================
// Element Child Iteration
// ==============================================================================

// process element children with custom text and item handlers
void format_element_children_with_processors(
    StringBuf* sb,
    const ElementReader& elem,
    TextProcessor text_proc,
    ItemProcessor item_proc
) {
    if (!sb) return;
    
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        if (child.isString()) {
            if (text_proc) {
                text_proc(sb, child.asString());
            }
        } else if (item_proc) {
            item_proc(sb, child);
        }
    }
}

// ==============================================================================
// HTML Entity Handling
// ==============================================================================

// check if position in string is start of HTML entity
bool is_html_entity(const char* str, size_t len, size_t pos, size_t* entity_end) {
    if (!str || pos >= len || str[pos] != '&') {
        return false;
    }
    
    size_t j = pos + 1;
    
    // check for numeric entity: &#123; or &#xAB;
    if (j < len && str[j] == '#') {
        j++;
        if (j < len && (str[j] == 'x' || str[j] == 'X')) {
            j++; // hex entity
            while (j < len && ((str[j] >= '0' && str[j] <= '9') ||
                               (str[j] >= 'a' && str[j] <= 'f') ||
                               (str[j] >= 'A' && str[j] <= 'F'))) {
                j++;
            }
        } else {
            // decimal entity
            while (j < len && str[j] >= '0' && str[j] <= '9') {
                j++;
            }
        }
        if (j < len && str[j] == ';') {
            if (entity_end) *entity_end = j;
            return true;
        }
    } else {
        // check for named entity: &nbsp; &lt; &gt; &frac12; etc.
        // entity names can contain letters and digits
        while (j < len && ((str[j] >= 'a' && str[j] <= 'z') ||
                           (str[j] >= 'A' && str[j] <= 'Z') ||
                           (str[j] >= '0' && str[j] <= '9'))) {
            j++;
        }
        if (j < len && str[j] == ';' && j > pos + 1) {
            if (entity_end) *entity_end = j;
            return true;
        }
    }
    
    return false;
}

// format string with HTML entity escaping (prevents double-encoding)
void format_html_string_safe(StringBuf* sb, String* str, bool is_attribute) {
    if (!sb || !str || !str->chars) return;

    const char* s = str->chars;
    size_t len = str->len;

    for (size_t i = 0; i < len; i++) {
        char c = s[i];

        // check if this is an already-encoded entity (starts with & and ends with ;)
        // this prevents double-encoding of entities like &lt; -> &amp;lt;
        if (c == '&') {
            size_t entity_end = 0;
            
            if (is_html_entity(s, len, i, &entity_end)) {
                // copy the entire entity as-is (already encoded)
                while (i <= entity_end && i < len) {
                    stringbuf_append_char(sb, s[i]);
                    i++;
                }
                i--; // adjust because loop will increment
                continue;
            } else {
                // not an entity, encode the ampersand
                stringbuf_append_str(sb, "&amp;");
            }
        } else {
            switch (c) {
            case '<':
                stringbuf_append_str(sb, "&lt;");
                break;
            case '>':
                stringbuf_append_str(sb, "&gt;");
                break;
            case '"':
                // only encode quotes when inside attribute values
                if (is_attribute) {
                    stringbuf_append_str(sb, "&quot;");
                } else {
                    stringbuf_append_char(sb, '"');
                }
                break;
            case '\'':
                // apostrophes don't need to be encoded in text content (only in attributes)
                // for HTML5, apostrophes in text are safe and don't need encoding
                stringbuf_append_char(sb, '\'');
                break;
            default:
                // use unsigned char for comparison to handle UTF-8 multibyte sequences correctly
                // UTF-8 continuation bytes (0x80-0xBF) and start bytes (0xC0-0xF7) should pass through
                if ((unsigned char)c < 0x20 && c != '\n' && c != '\r' && c != '\t') {
                    // control characters - encode as numeric character reference
                    char hex_buf[10];
                    snprintf(hex_buf, sizeof(hex_buf), "&#x%02x;", (unsigned char)c);
                    stringbuf_append_str(sb, hex_buf);
                } else {
                    stringbuf_append_char(sb, c);
                }
                break;
            }
        }
    }
}

// ==============================================================================
// Table Processing Utilities
// ==============================================================================

// analyze table structure
TableInfo* analyze_table(Pool* pool, const ElementReader& table_elem) {
    if (!pool) return NULL;
    
    TableInfo* info = (TableInfo*)pool_alloc(pool, sizeof(TableInfo));
    if (!info) return NULL;
    
    info->pool = pool;
    info->row_count = 0;
    info->column_count = 0;
    info->has_header = false;
    info->alignments = NULL;
    
    // iterate through table sections (thead, tbody)
    auto section_it = table_elem.children();
    ItemReader section_item;
    
    while (section_it.next(&section_item)) {
        if (section_item.isElement()) {
            ElementReader section = section_item.asElement();
            const char* section_tag = section.tagName();
            
            if (!section_tag) continue;
            
            // check if this is a header section
            if (strcmp(section_tag, "thead") == 0) {
                info->has_header = true;
            }
            
            // count rows in this section
            auto row_it = section.children();
            ItemReader row_item;
            
            while (row_it.next(&row_item)) {
                if (row_item.isElement()) {
                    ElementReader row = row_item.asElement();
                    info->row_count++;
                    
                    // count columns from first row
                    if (info->column_count == 0) {
                        auto cell_it = row.children();
                        ItemReader cell_item;
                        while (cell_it.next(&cell_item)) {
                            if (cell_item.isElement()) {
                                info->column_count++;
                            }
                        }
                    }
                }
            }
        }
    }
    
    // allocate alignment array (default to NONE)
    if (info->column_count > 0) {
        info->alignments = (TableAlignment*)pool_alloc(
            pool,
            sizeof(TableAlignment) * info->column_count
        );
        if (info->alignments) {
            for (int i = 0; i < info->column_count; i++) {
                info->alignments[i] = TABLE_ALIGN_NONE;
            }
        }
    }
    
    return info;
}

// free table info (currently no-op since pool-allocated)
void free_table_info(TableInfo* info) {
    // pool-allocated memory will be freed when pool is destroyed
    // this function exists for API completeness
    (void)info;
}

// iterate table rows with callback
void iterate_table_rows(
    const ElementReader& table_elem,
    StringBuf* sb,
    TableRowHandler handler,
    void* context
) {
    if (!handler) return;
    
    int row_idx = 0;
    
    // iterate through table sections (thead, tbody)
    auto section_it = table_elem.children();
    ItemReader section_item;
    
    while (section_it.next(&section_item)) {
        if (section_item.isElement()) {
            ElementReader section = section_item.asElement();
            const char* section_tag = section.tagName();
            
            if (!section_tag) continue;
            
            bool is_header = (strcmp(section_tag, "thead") == 0);
            
            // iterate rows in this section
            auto row_it = section.children();
            ItemReader row_item;
            
            while (row_it.next(&row_item)) {
                if (row_item.isElement()) {
                    ElementReader row = row_item.asElement();
                    handler(sb, row, row_idx, is_header, context);
                    row_idx++;
                }
            }
        }
    }
}
