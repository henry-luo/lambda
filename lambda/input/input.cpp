#include "input.hpp"
#include "input-parsers.h"
#include "../lambda-decimal.hpp"
#include "../mark_builder.hpp"
#include "../../lib/url.h"
#include "../../lib/stringbuf.h"
#include "../../lib/mime-detect.h"
#include "../../lib/arena.h"
#include "../../lib/log.h"  // add logging support
#include "../../lib/memtrack.h"
#include "../../lib/file.h"

// Include Target API
extern "C" {
    #include "../lambda.h"  // for Target, TargetScheme, etc.
}

#define MAX(a, b) ((a) > (b) ? (a) : (b))

__thread Context* input_context = NULL;

ShapeEntry* alloc_shape_entry(Pool* pool, String* key, TypeId type_id, ShapeEntry* prev_entry) {
    ShapeEntry* shape_entry = NULL;
    if (key) {
        // Allocate ShapeEntry + StrView + a copy of the key string data in one block.
        // This ensures the string data has the same lifetime as the ShapeEntry, even
        // when the original key String lives in a shorter-lived pool (e.g. a JS
        // transpiler's name_pool that is freed by js_transpiler_destroy).
        size_t str_copy_size = key->len + 1;
        shape_entry = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry) + sizeof(StrView) + str_copy_size);
        StrView* nv = (StrView*)((char*)shape_entry + sizeof(ShapeEntry));
        char* str_copy = (char*)nv + sizeof(StrView);
        memcpy(str_copy, key->chars, key->len);
        str_copy[key->len] = '\0';
        nv->str = str_copy;  nv->length = key->len;
        shape_entry->name = nv;
        shape_entry->type = type_info[type_id].type;
    } else {
        // no key, for nested map
        log_debug("alloc_shape_entry: null key for nested map, type_id=%d", type_id);
        shape_entry = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry));
        shape_entry->name = NULL;
        shape_entry->type = type_info[type_id].type;
    }
    if (prev_entry) {
        prev_entry->next = shape_entry;
        int prev_size = prev_entry->type ? type_info[prev_entry->type->type_id].byte_size : (int)sizeof(Item);
        shape_entry->byte_offset = prev_entry->byte_offset + prev_size;
    }
    else { shape_entry->byte_offset = 0; }
    return shape_entry;
}

// Internal helper function - not exported in header but accessible to mark_builder.cpp
void map_put(Map* mp, String* key, Item value, Input *input) {
    // note: key could be null for nested map
    TypeMap *map_type = (TypeMap*)mp->type;
    if (map_type == &EmptyMap) {
        // alloc map type and data chunk
        map_type = (TypeMap*)alloc_type(input->pool, LMD_TYPE_MAP, sizeof(TypeMap));
        if (!map_type) { return; }
        mp->type = map_type;
        arraylist_append(input->type_list, map_type);
        map_type->type_index = input->type_list->length - 1;
        int byte_cap = 64;
        mp->data = pool_calloc(input->pool, byte_cap);  mp->data_cap = byte_cap;
        if (!mp->data) return;
    }

    TypeId type_id = get_type_id(value);
    ShapeEntry* shape_entry = alloc_shape_entry(input->pool, key, type_id, map_type->last);
    if (!map_type->shape) { map_type->shape = shape_entry; }
    map_type->last = shape_entry;
    map_type->length++;

    // A1: populate inline hash table for O(1) property lookup
    typemap_hash_insert(map_type, shape_entry);

    // ensure data capacity
    int bsize = type_info[type_id].byte_size;
    int byte_offset = shape_entry->byte_offset + bsize;
    if (byte_offset > mp->data_cap) { // resize map data
        // mp->data_cap could be 0 (e.g. zero-byte-size maps from map_with_data)
        int byte_cap = MAX(mp->data_cap, byte_offset) * 2;
        void* new_data = pool_calloc(input->pool, byte_cap);
        if (!new_data) return;
        if (mp->data) {
            memcpy(new_data, mp->data, byte_offset - bsize);
            pool_free(input->pool, mp->data);
        }
        mp->data = new_data;  mp->data_cap = byte_cap;
    }
    map_type->byte_size = byte_offset;

    // store the value
    void* field_ptr = (char*)mp->data + byte_offset - bsize;
    switch (type_id) {
    case LMD_TYPE_NULL:
    case LMD_TYPE_UNDEFINED:
        // null/undefined value doesn't need to store anything - just mark the slot
        *(bool*)field_ptr = false;
        break;
    case LMD_TYPE_BOOL:
        *(bool*)field_ptr = value.bool_val;
        break;
    case LMD_TYPE_INT: {
        int64_t int_val = value.get_int56();
        *(int64_t*)field_ptr = int_val;
        break;
    }
    case LMD_TYPE_INT64:
        *(int64_t*)field_ptr = value.get_int64();
        break;
    case LMD_TYPE_FLOAT:
        *(double*)field_ptr = value.get_double();
        break;
    case LMD_TYPE_DTIME:
        *(DateTime*)field_ptr = value.get_datetime();
        break;
    case LMD_TYPE_DECIMAL:
        *(Decimal**)field_ptr = value.get_decimal();
        break;
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY:
        *(String**)field_ptr = value.get_string();
        break;
    case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_RANGE:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT:  case LMD_TYPE_OBJECT:
    case LMD_TYPE_FUNC:
        *(Map**)field_ptr = value.map;
        break;
    case LMD_TYPE_TYPE:
        *(Type**)field_ptr = value.type;
        break;
    case LMD_TYPE_PATH:
        *(Path**)field_ptr = value.path;
        break;
    case LMD_TYPE_ANY: {
        Item item = value;
        TypeId type_id = get_type_id(item);
        log_debug("set field of ANY type to type: %d", type_id);
        TypedItem titem = {.type_id = type_id, .item = item.item};
        switch (type_id) {
        case LMD_TYPE_NULL:
        case LMD_TYPE_UNDEFINED:
            break; // no extra work needed
        case LMD_TYPE_BOOL:
            titem.bool_val = item.bool_val;  break;
        case LMD_TYPE_INT:
            titem.int_val = item.int_val;  break;
        case LMD_TYPE_INT64:
            titem.long_val = item.get_int64();  break;
        case LMD_TYPE_FLOAT:
            titem.double_val = item.get_double();  break;
        case LMD_TYPE_DTIME:
            titem.datetime_val = item.get_datetime();  break;
        case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
            String *str = item.get_string();
            titem.string = str;
            break;
        }
        case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_NUM:
        case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT:  case LMD_TYPE_OBJECT: {
            Container *container = item.container;
            titem.container = container;
            break;
        }
        case LMD_TYPE_TYPE:
            titem.type = item.type;
            break;
        case LMD_TYPE_FUNC:
            titem.function = item.function;
            break;
        case LMD_TYPE_PATH:
            titem.path = item.path;
            break;
        default:
            log_error("unknown type %d in set_fields", type_id);
            // set as ERROR
            titem = {.type_id = LMD_TYPE_ERROR};
        }
        // set in map
        *(TypedItem*)field_ptr = titem;
        break;
    }
    default:
        log_debug("unknown type %d\n", value._type_id);
        return;
    }
}

extern TypeElmt EmptyElmt;
void elmt_put(Element* elmt, String* key, Item value, Pool* pool) {
    assert(elmt->type != &EmptyElmt);
    TypeId type_id = get_type_id(value);
    TypeElmt* elmt_type = (TypeElmt*)elmt->type;
    ShapeEntry* shape_entry = alloc_shape_entry(pool, key, type_id, elmt_type->last);
    if (!elmt_type->shape) { elmt_type->shape = shape_entry; }
    elmt_type->last = shape_entry;
    elmt_type->length++;

    // ensure data capacity
    int bsize = type_info[type_id].byte_size;
    int byte_offset = shape_entry->byte_offset + bsize;
    if (byte_offset > elmt->data_cap) { // resize map data
        // elmt->data_cap could be 0
        int byte_cap = MAX(elmt->data_cap, byte_offset) * 2;
        void* new_data = pool_calloc(pool, byte_cap);
        if (!new_data) return;
        if (elmt->data) {
            memcpy(new_data, elmt->data, byte_offset - bsize);
            pool_free(pool, elmt->data);
        }
        elmt->data = new_data;  elmt->data_cap = byte_cap;
    }
    elmt_type->byte_size = byte_offset;

    // store the value
    void* field_ptr = (char*)elmt->data + byte_offset - bsize;
    switch (type_id) {
    case LMD_TYPE_NULL:
    case LMD_TYPE_UNDEFINED:
        // null/undefined value doesn't need to store anything
        *(bool*)field_ptr = false;
        break;
    case LMD_TYPE_BOOL:
        *(bool*)field_ptr = value.bool_val;
        break;
    case LMD_TYPE_INT:
        *(int64_t*)field_ptr = value.get_int56();  // use get_int56() to extract full 56-bit value
        break;
    case LMD_TYPE_INT64:
        *(int64_t*)field_ptr = value.get_int64();
        break;
    case LMD_TYPE_FLOAT:
        *(double*)field_ptr = value.get_double();
        break;
    case LMD_TYPE_DTIME:
        *(DateTime*)field_ptr = value.get_datetime();
        break;
    case LMD_TYPE_DECIMAL:
        *(Decimal**)field_ptr = value.get_decimal();
        break;
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY:
        *(String**)field_ptr = value.get_string();
        break;
    case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_RANGE:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT:  case LMD_TYPE_OBJECT: {
        Container *container = value.container;
        *(void**)field_ptr = container;
        break;
    }
    case LMD_TYPE_PATH:
        *(Path**)field_ptr = value.path;
        break;
    default:
        log_debug("unknown type %d\n", value._type_id);
    }
}

// ========== Shape Finalization ==========
// These functions deduplicate map/element shapes by replacing the ShapeEntry chain
// with a pooled version from the shape_pool.

void map_finalize_shape(TypeMap* type_map, Input* input) {
    if (!type_map->shape || type_map->length == 0) {
        return;  // empty map, nothing to finalize
    }
    // collect field names and types from existing shape chain
    size_t field_count = type_map->length;
    const char** field_names = (const char**)pool_alloc(input->pool, field_count * sizeof(char*));
    TypeId* field_types = (TypeId*)pool_alloc(input->pool, field_count * sizeof(TypeId));
    if (!field_names || !field_types) { return; }

    // traverse existing shape chain to collect info
    ShapeEntry* entry = type_map->shape;
    for (size_t i = 0; i < field_count && entry; i++) {
        // entry->name could be null for nested map
        field_names[i] = entry->name ? entry->name->str : nullptr;  // StrView has 'str' field, not 'chars'
        field_types[i] = entry->type->type_id;
        entry = entry->next;
    }

    // get or create pooled shape
    struct ShapeEntry* pooled_shape = shape_pool_get_map_shape(
        input->shape_pool,
        field_names,
        field_types,
        field_count
    );

    if (pooled_shape) {
        // replace the shape chain with pooled version
        type_map->shape = pooled_shape;

        // find last entry in pooled chain
        struct ShapeEntry* last = pooled_shape;
        while (last->next) {
            last = last->next;
        }
        type_map->last = last;
    }

    // free temporary arrays (field data stays in pool)
    pool_free(input->pool, field_names);
    pool_free(input->pool, field_types);
}

void elmt_finalize_shape(TypeElmt* type_elmt, Input* input) {
    if (!type_elmt) {
        log_debug("missing element type");
        return;  // safety check
    }

    if (!type_elmt->shape || type_elmt->length == 0) {
        return;  // empty element, nothing to finalize
    }

    // collect attribute names and types from existing shape chain
    size_t attr_count = type_elmt->length;
    log_debug("elmt_finalize_shape: attr_count=%zu", attr_count);
    const char** attr_names = (const char**)pool_alloc(input->pool, attr_count * sizeof(char*));
    TypeId* attr_types = (TypeId*)pool_alloc(input->pool, attr_count * sizeof(TypeId));

    if (!attr_names || !attr_types) {
        return;  // allocation failed
    }

    // traverse existing shape chain to collect info
    ShapeEntry* entry = type_elmt->shape;
    for (size_t i = 0; i < attr_count && entry; i++) {
        attr_names[i] = entry->name ? entry->name->str : nullptr;
        attr_types[i] = entry->type->type_id;
        entry = entry->next;
    }

    // get or create pooled shape (includes element name)
    const char* element_name = type_elmt->name.str ? type_elmt->name.str : "";
    struct ShapeEntry* pooled_shape = shape_pool_get_element_shape(
        input->shape_pool, element_name, attr_names, attr_types, attr_count);

    if (pooled_shape) {
        // replace the shape chain with pooled version
        type_elmt->shape = pooled_shape;

        // find last entry in pooled chain
        struct ShapeEntry* last = pooled_shape;
        while (last->next) { last = last->next; }
        type_elmt->last = last;
    }

    // free temporary arrays
    pool_free(input->pool, attr_names);
    pool_free(input->pool, attr_types);
}


// Helper function to map MIME types to parser types
static const char* mime_to_parser_type(const char* mime_type) {
    if (!mime_type) return "text";

    // Direct mappings
    if (strcmp(mime_type, "application/json") == 0) return "json";
    if (strcmp(mime_type, "text/csv") == 0) return "csv";
    if (strcmp(mime_type, "application/xml") == 0) return "xml";
    if (strcmp(mime_type, "text/html") == 0) return "html";
    if (strcmp(mime_type, "text/markdown") == 0) return "markdown";
    if (strcmp(mime_type, "text/mdx") == 0) return "mdx";
    if (strcmp(mime_type, "text/x-rst") == 0) return "rst";
    if (strcmp(mime_type, "application/rtf") == 0) return "rtf";
    if (strcmp(mime_type, "application/pdf") == 0) return "pdf";
    if (strcmp(mime_type, "application/x-tex") == 0) return "latex";
    if (strcmp(mime_type, "application/x-latex") == 0) return "latex";
    if (strcmp(mime_type, "application/toml") == 0) return "toml";
    if (strcmp(mime_type, "application/x-yaml") == 0) return "yaml";
    if (strcmp(mime_type, "text/x-java-properties") == 0) return "properties";
    if (strcmp(mime_type, "application/x-java-properties") == 0) return "properties";
    if (strcmp(mime_type, "message/rfc822") == 0) return "eml";
    if (strcmp(mime_type, "application/eml") == 0) return "eml";
    if (strcmp(mime_type, "message/eml") == 0) return "eml";
    if (strcmp(mime_type, "text/vcard") == 0) return "vcf";
    if (strcmp(mime_type, "text/calendar") == 0) return "ics";
    if (strcmp(mime_type, "application/ics") == 0) return "ics";
    if (strcmp(mime_type, "text/textile") == 0) return "textile";
    if (strcmp(mime_type, "application/textile") == 0) return "textile";
    if (strcmp(mime_type, "text/x-org") == 0) return "org";
    if (strcmp(mime_type, "text/x-asciidoc") == 0) return "asciidoc";
    if (strcmp(mime_type, "text/x-wiki") == 0) return "wiki";
    if (strcmp(mime_type, "text/troff") == 0) return "man";
    if (strcmp(mime_type, "text/typst") == 0) return "typst";
    if (strcmp(mime_type, "application/typst") == 0) return "typst";
    if (strcmp(mime_type, "text/x-mark") == 0) return "mark";
    if (strcmp(mime_type, "application/x-mark") == 0) return "mark";
    if (strcmp(mime_type, "text/css") == 0) return "css";
    if (strcmp(mime_type, "application/css") == 0) return "css";

    // Check for XML-based formats
    if (strstr(mime_type, "+xml") || strstr(mime_type, "xml")) return "xml";

    // Check for text formats
    if (strstr(mime_type, "text/") == mime_type) {
        // Handle specific text subtypes
        if (strstr(mime_type, "x-c") || strstr(mime_type, "x-java") ||
            strstr(mime_type, "javascript") || strstr(mime_type, "x-python")) {
            return "text";
        }
        if (strstr(mime_type, "ini")) return "ini";
        return "text";
    }

    // For unsupported formats, default to text if it might be readable
    if (strstr(mime_type, "application/") == mime_type) {
        if (strstr(mime_type, "javascript") || strstr(mime_type, "typescript") ||
            strstr(mime_type, "x-sh") || strstr(mime_type, "x-bash")) {
            return "text";
        }
    }

    // Default fallback
    return "text";
}

extern "C" Input* input_from_source_n(const char* source, size_t source_len, Url* abs_url, String* type, String* flavor) {
    log_debug("input_from_source_n: ENTRY type='%s', flavor='%s', len=%zu",
              type ? type->chars : "null",
              flavor ? flavor->chars : "null",
              source_len);
    const char* effective_type = NULL;
    // Determine the effective type to use
    if (!type || strcmp(type->chars, "auto") == 0) {
        // Auto-detect MIME type
        MimeDetector* detector = mime_detector_init();
        if (detector) {
            const char* detected_mime = detect_mime_type(detector, abs_url->pathname ? abs_url->pathname->chars : "", source, source_len);
            if (detected_mime) {
                effective_type = mime_to_parser_type(detected_mime);
                log_debug("Auto-detected MIME type: %s -> parser type: %s\n", detected_mime, effective_type);
            } else {
                effective_type = "text";
                log_debug("MIME detection failed, defaulting to text\n");
            }
            mime_detector_destroy(detector);
        } else {
            effective_type = "text";
            log_debug("Failed to initialize MIME detector, defaulting to text\n");
        }
    } else {
        effective_type = type->chars;
    }

    log_debug("input_from_source: effective_type='%s'", effective_type ? effective_type : "null");

    Input* input = NULL;
    if (!effective_type || strcmp(effective_type, "text") == 0) { // treat as plain text
        // Use InputManager to properly set up the Input with a pool
        input = InputManager::create_input(abs_url);
        if (!input) {
            log_error("input_from_source: Failed to create input for plain text");
            return NULL;
        }
        // Allocate string from the pool instead of malloc
        String *str = create_string(input->pool, source);
        input->root = {.item = s2it(str)};
    }
    else {
        Context context;  Context *pa_input_context = input_context;
        input = InputManager::create_input(abs_url);
        if (!input) {
            log_error("input_from_source: Failed to create input for type '%s'", effective_type);
            return NULL;
        }
        context.pool = input->pool;  context.arena = input->arena;
        context.consts = NULL;
        context.cwd = NULL;  context.run_main = false;
        context.disable_string_merging = false;  // default: allow string merging
        input_context = &context;

        if (strcmp(effective_type, "json") == 0) {
            parse_json(input, source);
        }
        else if (strcmp(effective_type, "csv") == 0) {
            parse_csv(input, source);
        }
        else if (strcmp(effective_type, "ini") == 0) {
            parse_ini(input, source);
        }
        else if (strcmp(effective_type, "properties") == 0) {
            parse_properties(input, source);
        }
        else if (strcmp(effective_type, "toml") == 0) {
            parse_toml(input, source);
        }
        else if (strcmp(effective_type, "yaml") == 0) {
            parse_yaml(input, source);
        }
        else if (strcmp(effective_type, "xml") == 0) {
            parse_xml(input, source);
        }
        else if (strcmp(effective_type, "markdown") == 0) {
            input->root = input_markup_modular(input, source);
        }
        else if (strcmp(effective_type, "markup") == 0) {
            // Generic markup type - use flavor to select format
            const char* markup_flavor = (flavor && flavor->chars) ? flavor->chars : "markdown";
            log_debug("input_from_source markup: flavor='%s'", markup_flavor);
            if (strcmp(markup_flavor, "rst") == 0) {
                input->root = input_markup_with_format(input, source, MARKUP_RST);
            } else if (strcmp(markup_flavor, "wiki") == 0) {
                input->root = input_markup_with_format(input, source, MARKUP_WIKI);
            } else if (strcmp(markup_flavor, "asciidoc") == 0 || strcmp(markup_flavor, "adoc") == 0) {
                input->root = input_markup_with_format(input, source, MARKUP_ASCIIDOC);
            } else if (strcmp(markup_flavor, "man") == 0) {
                input->root = input_markup_with_format(input, source, MARKUP_MAN);
            } else if (strcmp(markup_flavor, "org") == 0) {
                input->root = input_markup_with_format(input, source, MARKUP_ORG);
            } else if (strcmp(markup_flavor, "textile") == 0) {
                input->root = input_markup_with_format(input, source, MARKUP_TEXTILE);
            } else if (strcmp(markup_flavor, "commonmark") == 0) {
                // Strict CommonMark mode - no GFM extensions
                log_debug("input_from_source: using commonmark mode");
                input->root = input_markup_commonmark(input, source);
            } else {
                // Default to markdown with GFM extensions
                log_debug("input_from_source: using default markdown mode");
                input->root = input_markup_modular(input, source);
            }
        }
        else if (strcmp(effective_type, "rst") == 0) {
            log_debug("input_from_source: matched 'rst' type, calling input_markup_with_format");
            input->root = input_markup_with_format(input, source, MARKUP_RST);
        }
        else if (strcmp(effective_type, "html") == 0 || strcmp(effective_type, "html5") == 0) {
            // Use HTML5 compliant parser
            Element* doc = html5_parse(input, source);
            input->root = (Item){.element = doc};
        }
        else if (strcmp(effective_type, "latex") == 0 || strcmp(effective_type, "latex-ts") == 0) {
            // Tree-sitter LaTeX parser (default)
            parse_latex_ts(input, source);
        }
        else if (strcmp(effective_type, "rtf") == 0) {
            parse_rtf(input, source);
        }
        else if (strcmp(effective_type, "pdf") == 0) {
            // PDF is binary; use the explicit length we received instead of strlen,
            // which would truncate at the first null byte inside the binary stream.
            parse_pdf(input, source, source_len);
        }
        else if (strcmp(effective_type, "wiki") == 0) {
            input->root = input_markup_with_format(input, source, MARKUP_WIKI);
        }
        else if (strcmp(effective_type, "asciidoc") == 0 || strcmp(effective_type, "adoc") == 0) {
            input->root = input_markup_with_format(input, source, MARKUP_ASCIIDOC);
        }
        else if (strcmp(effective_type, "man") == 0) {
            input->root = input_markup_with_format(input, source, MARKUP_MAN);
        }
        else if (strcmp(effective_type, "eml") == 0) {
            parse_eml(input, source);
        }
        else if (strcmp(effective_type, "vcf") == 0) {
            parse_vcf(input, source);
        }
        else if (strcmp(effective_type, "ics") == 0) {
            parse_ics(input, source);
        }
        else if (strcmp(effective_type, "textile") == 0) {
            input->root = input_markup_modular(input, source);
        }
        else if (strcmp(effective_type, "mark") == 0) {
            parse_mark(input, source);
        }
        else if (strcmp(effective_type, "org") == 0) {
            input->root = input_markup_with_format(input, source, MARKUP_ORG);
        }
        else if (strcmp(effective_type, "typst") == 0) {
            input->root = input_markup_with_format(input, source, MARKUP_TYPST);
        }
        else if (strcmp(effective_type, "css") == 0) {
            parse_css(input, source);
        }
        else if (strcmp(effective_type, "jsx") == 0) {
            parse_jsx(input, source);
        }
        else if (strcmp(effective_type, "mdx") == 0) {
            input->root = input_mdx(input, source);
        }
        else if (strcmp(effective_type, "math") == 0) {
            const char* math_flavor = (flavor && flavor->chars) ? flavor->chars : "latex";
            // Both ASCII and LaTeX math use the unified tree-sitter parser
            parse_math(input, source, math_flavor);
        }
        else if (strncmp(effective_type, "math-", 5) == 0) {
            // Handle compound math formats like "math-ascii", "math-latex", etc.
            const char* math_flavor = effective_type + 5; // Skip "math-" prefix
            parse_math(input, source, math_flavor);
        }
        else if (strncmp(effective_type, "math-", 5) == 0) {
            // Handle compound math formats like "math-ascii", "math-latex", etc.
            const char* math_flavor = effective_type + 5; // Skip "math-" prefix
            parse_math(input, source, math_flavor);
        }
        else if (strcmp(effective_type, "markup") == 0) {
            input->root = input_markup_modular(input, source);
        }
        else if (strcmp(effective_type, "graph") == 0) {
            const char* graph_flavor = (flavor && flavor->chars) ? flavor->chars : "dot";
            parse_graph(input, source, graph_flavor);
        }
        else {
            log_debug("Unknown input type: %s\n", effective_type);
        }
        input_context = pa_input_context;
    }
    // Note: don't mem_free(source) here - it's the caller's responsibility
    return input;
}

extern "C" Input* input_from_source(const char* source, Url* abs_url, String* type, String* flavor) {
    // Back-compat wrapper: assumes source is a null-terminated string (text only).
    // Binary inputs (e.g. PDF) must call input_from_source_n with the actual byte count.
    return input_from_source_n(source, source ? strlen(source) : 0, abs_url, type, flavor);
}

// Helper function to read text file from file:// URL
static char* read_file_from_url(Url* url) {
    if (!url || url->scheme != URL_SCHEME_FILE) {
        log_error("Only file:// URLs are supported for file reading");
        return NULL;
    }
    const char* pathname = url_get_pathname(url);
    if (!pathname) return NULL;
    log_debug("Reading file from path: %s", pathname);
    return read_text_file(pathname);
}

// Read a local file and parse it via input_from_source_n. Detects binary
// formats (currently PDF) and reads them with read_binary_file so that null
// bytes in the payload are preserved and the parser receives an accurate
// byte length instead of strlen() which would truncate at the first null.
static Input* input_from_local_path(const char* pathname, Url* abs_url, String* type, String* flavor) {
    bool is_binary_pdf = false;
    if (type && type->chars && strcmp(type->chars, "pdf") == 0) {
        is_binary_pdf = true;
    } else if (pathname) {
        size_t plen = strlen(pathname);
        if (plen >= 4 && strcasecmp(pathname + plen - 4, ".pdf") == 0) {
            is_binary_pdf = true;
        }
    }

    size_t src_len = 0;
    char* source = is_binary_pdf ? read_binary_file(pathname, &src_len)
                                 : read_text_file(pathname);
    if (!source) {
        log_debug("input_from_local_path: failed to read file at path: %s", pathname ? pathname : "null");
        return NULL;
    }
    if (!is_binary_pdf) src_len = strlen(source);

    Input* input = input_from_source_n(source, src_len, abs_url, type, flavor);
    mem_free(source);
    return input;
}

Input* input_from_url(String* url, String* type, String* flavor, Url* cwd) {
    log_debug("input_from_url: ENTRY url='%s', type='%s'", url ? url->chars : "null", type ? type->chars : "null");
    log_debug("input_data at: %s, type: %s, cwd: %p", url ? url->chars : "null", type ? type->chars : "null", cwd);

    Url* abs_url;
    if (cwd) {
        abs_url = url_parse_with_base(url->chars, cwd);
    } else {
        abs_url = url_parse(url->chars);
    }
    if (!abs_url) {
        log_error("Failed to parse URL\n");
        return NULL;
    }
    log_debug("Parsed URL: scheme=%d, host=%s, pathname=%s", abs_url->scheme, abs_url->host ? abs_url->host->chars : "null",
        abs_url->pathname ? abs_url->pathname->chars : "null");

    // Handle different URL schemes
    if (abs_url->scheme == URL_SCHEME_FILE) {
        // Check if URL points to a directory (only for file:// URLs)
        const char* pathname = url_get_pathname(abs_url);
        // if Windows, need to strip the starting '/' for absolute paths like /C:/path/to/file
        #ifdef _WIN32
        if (pathname && pathname[0] == '/' && isalpha(pathname[1]) && pathname[2] == ':') {
            pathname++; // Skip the leading '/'
        }
        #endif
        if (pathname) {
            if (file_is_dir(pathname)) {
                // URL points to a directory - use directory listing
                log_debug("URL points to directory, using input_from_directory\n");
                // Pass original URL string to preserve relative path info
                Input* input = input_from_directory(pathname, url ? url->chars : NULL, false, 1); // non-recursive, single level only
                url_destroy(abs_url);
                return input;
            }

            // check if file should be handled as a relational database
            const char* type_str = type ? type->chars : NULL;
            const char* rdb_driver = rdb_detect_format(pathname, type_str);
            if (rdb_driver) {
                log_debug("rdb: detected driver '%s' for path '%s'", rdb_driver, pathname);
                Input* input = input_rdb_from_path(pathname, rdb_driver);
                url_destroy(abs_url);
                return input;
            }
        }

        // URL points to a file - read as normal
        log_debug("reading file from path: %s", pathname ? pathname : "null");

        Input* input = input_from_local_path(pathname, abs_url, type, flavor);
        url_destroy(abs_url);
        return input;
    }
    else if (abs_url->scheme == URL_SCHEME_HTTP || abs_url->scheme == URL_SCHEME_HTTPS) {
        // Handle HTTP/HTTPS URLs
        log_debug("HTTP/HTTPS URL detected, using HTTP client\n");

        const char* type_str = type ? type->chars : NULL;
        const char* flavor_str = flavor ? flavor->chars : NULL;

        Input* input = input_from_http(url->chars, type_str, flavor_str, "./temp/cache");
        url_destroy(abs_url);
        return input;
    }
    else if (abs_url->scheme == URL_SCHEME_SYS) {
        // Handle sys:// URLs for system information
        log_debug("sys:// URL detected, using system information provider\n");

        // Create a variable pool for the input
        Pool* pool = pool_create();
        if (pool == NULL) {
            log_debug("Failed to create variable pool for sys:// URL\n");
            url_destroy(abs_url);
            return NULL;
        }

        Input* input = input_from_sysinfo(abs_url, pool);
        if (!input) {
            pool_destroy(pool);
        }
        url_destroy(abs_url);
        return input;
    }
    else {
        log_debug("Unsupported URL scheme for: %s\n", url ? url->chars : "null");
        url_destroy(abs_url);
        return NULL;
    }
}

/**
 * Load input from a Target (unified I/O target).
 * This function dispatches to appropriate handlers based on target scheme.
 *
 * @param target - The Target to load from (URL or Path)
 * @param type - Optional type hint (json, xml, etc.)
 * @param flavor - Optional flavor hint (for markup variants)
 * @return Input* on success, NULL on failure
 */
Input* input_from_target(Target* target, String* type, String* flavor) {
    if (!target) {
        log_error("input_from_target: target is NULL");
        return NULL;
    }

    log_debug("input_from_target: scheme=%d, type=%d", target->scheme, target->type);

    // Check if target is a directory first (for local targets)
    if (target_is_dir(target)) {
        log_debug("input_from_target: directory detected, using directory listing");
        StrBuf* path_buf = (StrBuf*)target_to_local_path(target, NULL);
        if (path_buf) {
            Input* input = input_from_directory(path_buf->str, target->original, false, 1);
            strbuf_free(path_buf);
            return input;
        }
        return NULL;
    }

    // For URL targets, use the existing URL-based dispatch
    if (target->type == TARGET_TYPE_URL && target->url) {
        Url* url = target->url;
        log_debug("input_from_target: URL target, href=%s", url->href ? url->href->chars : "null");

        // Handle different URL schemes
        if (target->scheme == TARGET_SCHEME_FILE) {
            const char* pathname = url_get_pathname(url);
            #ifdef _WIN32
            if (pathname && pathname[0] == '/' && isalpha(pathname[1]) && pathname[2] == ':') {
                pathname++; // Skip the leading '/' for Windows paths
            }
            #endif

            // check if file should be handled as a relational database
            const char* type_str = type ? type->chars : NULL;
            const char* rdb_driver = rdb_detect_format(pathname, type_str);
            if (rdb_driver) {
                log_debug("input_from_target: rdb detected driver '%s' for '%s'", rdb_driver, pathname);
                return input_rdb_from_path(pathname, rdb_driver);
            }

            log_debug("input_from_target: reading file from path: %s", pathname ? pathname : "null");
            // Create a copy of the URL for the input (input owns lifecycle of url_copy via Input)
            Url* url_copy = url_parse(url->href->chars);
            Input* input = input_from_local_path(pathname, url_copy, type, flavor);
            if (!input && url_copy) url_destroy(url_copy);
            return input;
        }
        else if (target->scheme == TARGET_SCHEME_HTTP || target->scheme == TARGET_SCHEME_HTTPS) {
            log_debug("input_from_target: HTTP/HTTPS URL detected");
            const char* type_str = type ? type->chars : NULL;
            const char* flavor_str = flavor ? flavor->chars : NULL;
            return input_from_http(url->href->chars, type_str, flavor_str, "./temp/cache");
        }
        else if (target->scheme == TARGET_SCHEME_SYS) {
            log_debug("input_from_target: sys:// URL detected");
            Pool* pool = pool_create();
            if (!pool) {
                log_error("input_from_target: failed to create pool for sys:// URL");
                return NULL;
            }
            Input* input = input_from_sysinfo(url, pool);
            if (!input) pool_destroy(pool);
            return input;
        }
        else {
            log_error("input_from_target: unsupported URL scheme %d", target->scheme);
            return NULL;
        }
    }
    // For Path targets, convert to OS path and read
    else if (target->type == TARGET_TYPE_PATH && target->path) {
        Path* path = target->path;
        log_debug("input_from_target: Path target");

        // Check scheme of the path
        PathScheme path_scheme = path_get_scheme(path);
        if (path_scheme == PATH_SCHEME_HTTP || path_scheme == PATH_SCHEME_HTTPS) {
            // Convert path to URL string and handle as HTTP
            StrBuf* url_buf = strbuf_new();
            path_to_string(path, url_buf);
            const char* type_str = type ? type->chars : NULL;
            const char* flavor_str = flavor ? flavor->chars : NULL;
            Input* input = input_from_http(url_buf->str, type_str, flavor_str, "./temp/cache");
            strbuf_free(url_buf);
            return input;
        }

        // Local file path - convert to OS path
        StrBuf* path_buf = strbuf_new();
        path_to_os_path(path, path_buf);
        const char* pathname = path_buf->str;

        // Directory case already handled by target_is_dir check above

        log_debug("input_from_target: reading file from path: %s", pathname);
        // Create URL from file path for the input
        StrBuf* url_buf = strbuf_new();
        strbuf_append_str(url_buf, "file://");
        #ifdef _WIN32
        if (pathname[0] != '/') strbuf_append_char(url_buf, '/');
        #endif
        strbuf_append_str(url_buf, pathname);
        Url* file_url = url_parse(url_buf->str);
        strbuf_free(url_buf);

        Input* input = input_from_local_path(pathname, file_url, type, flavor);
        strbuf_free(path_buf);
        if (!input && file_url) url_destroy(file_url);
        return input;
    }

    log_error("input_from_target: invalid target (type=%d)", target->type);
    return NULL;
}

Input* Input::create(Pool* pool, Url* abs_url, Input* parent) {
    if (!pool) {
        log_error("Input::create: pool is NULL");
        return NULL;
    }
    Input* input = (Input*)pool_alloc(pool, sizeof(Input));
    if (!input) {
        log_error("Failed to allocate Input structure (pool=%p, size=%zu)", (void*)pool, sizeof(Input));
        return NULL;
    }
    input->pool = pool;
    input->arena = arena_create_default(pool);
    input->name_pool = name_pool_create(pool, NULL);  // Initialize name pool for string interning
    input->shape_pool = shape_pool_create(pool, input->arena, NULL);  // Initialize shape pool
    input->type_list = arraylist_new(16);
    input->url = abs_url;
    input->path = nullptr;
    input->parent = parent;     // Set parent Input for hierarchical ownership
    input->root = (Item){.item = ITEM_NULL};
    input->doc_count = 0;
    input->ui_mode = false;
    input->xml_stylesheet_href = nullptr;
    return input;
}

// Global singleton instance
static InputManager* g_input_manager = nullptr;

InputManager::InputManager() {
    global_pool = pool_create();
    if (!global_pool) {
        log_error("InputManager: Failed to create global_pool");
    }
    inputs = arraylist_new(16);
    // Use shared global decimal context
    decimal_ctx = decimal_fixed_context();
}

InputManager::~InputManager() {
    // clean up all tracked inputs
    if (inputs) {
        for (int i = 0; i < inputs->length; i++) {
            Input* input = (Input*)inputs->data[i];
            if (input && input->type_list) {
                arraylist_free(input->type_list);
            }
        }
        arraylist_free(inputs);
    }

    // Destroy the global pool (this frees all pool-allocated memory)
    if (global_pool) {
        log_debug("InputManager::~InputManager destroying global_pool=%p", (void*)global_pool);
        pool_destroy(global_pool);
        global_pool = nullptr;
    }

    // decimal_ctx is now shared global - don't free
    decimal_ctx = nullptr;
}

mpd_context_t* InputManager::decimal_context() {
    // Lazy initialization of singleton
    if (!g_input_manager) {
        g_input_manager = new InputManager();
    }
    if (!g_input_manager) return nullptr;
    return g_input_manager->decimal_ctx;
}

// Static method to create input using global manager
Input* InputManager::create_input(Url* abs_url) {
    // Lazy initialization of singleton
    if (!g_input_manager) {
        g_input_manager = new InputManager();
    }
    if (!g_input_manager) return nullptr;
    return g_input_manager->create_input_instance(abs_url);
}

// Instance method to create input
Input* InputManager::create_input_instance(Url* abs_url) {
    if (!global_pool) {
        log_error("create_input_instance: global_pool is NULL");
        return nullptr;
    }

    // Use the static create method with the managed pool
    Input* input = Input::create(global_pool, abs_url);
    if (!input) {
        log_error("create_input_instance: Input::create returned NULL");
        return nullptr;
    }

    // Track this input for cleanup
    arraylist_append(inputs, input);

    return input;
}

// Destroy the global instance
void InputManager::destroy_global() {
    if (g_input_manager) {
        delete g_input_manager;
        g_input_manager = nullptr;
    }
}

// Get the <html> element from the #document tree
extern "C" Element* input_get_html_element(Input* input) {
    if (!input) return nullptr;

    TypeId root_type = get_type_id(input->root);

    if (root_type == LMD_TYPE_ELEMENT) {
        Element* elem = input->root.element;
        TypeElmt* type = (TypeElmt*)elem->type;

        // If it's a #document node, get the html child
        if (strcmp(type->name.str, "#document") == 0) {
            List* doc_children = elem;
            if (doc_children->length > 0) {
                Item html_item = doc_children->items[0];
                if (html_item.type_id() == LMD_TYPE_ELEMENT) {
                    return html_item.element;
                }
            }
        }
    }

    return nullptr;
}

// Get fragment element (extracts from body for fragments, returns html for full docs)
extern "C" Element* input_get_html_fragment_element(Input* input, const char* original_html) {
    if (!input) return nullptr;

    TypeId root_type = get_type_id(input->root);

    if (root_type == LMD_TYPE_ELEMENT) {
        Element* elem = input->root.element;
        TypeElmt* type = (TypeElmt*)elem->type;

        // If it's a #document node from HTML5 parser
        if (strcmp(type->name.str, "#document") == 0) {
            List* doc_children = elem;
            if (doc_children->length > 0) {
                Item html_item = doc_children->items[0];
                if (html_item.type_id() == LMD_TYPE_ELEMENT) {
                    Element* html_elem = html_item.element;
                    TypeElmt* html_type = (TypeElmt*)html_elem->type;

                    if (strcmp(html_type->name.str, "html") == 0) {
                        // Check if original HTML explicitly starts with <html>
                        if (original_html) {
                            const char* p = original_html;
                            while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;

                            bool has_explicit_html = (strncmp(p, "<html", 5) == 0);

                            // If <html> was explicit in source, return it
                            if (has_explicit_html) {
                                return html_elem;
                            }
                        }

                        // Otherwise, extract the actual fragment from body
                        List* html_children = html_elem;

                        for (int64_t i = 0; i < html_children->length; i++) {
                            Item child = html_children->items[i];
                            if (child.type_id() == LMD_TYPE_ELEMENT) {
                                Element* child_elem = child.element;
                                TypeElmt* child_type = (TypeElmt*)child_elem->type;

                                if (strcmp(child_type->name.str, "body") == 0) {
                                    List* body_children = child_elem;

                                    // Count element children (skip text nodes)
                                    int element_count = 0;
                                    Element* first_element = nullptr;
                                    for (int64_t j = 0; j < body_children->length; j++) {
                                        Item body_child = body_children->items[j];
                                        if (body_child.type_id() == LMD_TYPE_ELEMENT) {
                                            if (!first_element) {
                                                first_element = body_child.element;
                                            }
                                            element_count++;
                                        }
                                    }

                                    // If body has exactly one element child, return it
                                    if (element_count == 1 && first_element) {
                                        return first_element;
                                    }

                                    // Multiple element children -> not a simple fragment, return html
                                    return html_elem;
                                }
                            }
                        }

                        return html_elem;
                    }
                }
            }
        }

        // Fallback
        return elem;
    }
    else if (root_type == LMD_TYPE_ARRAY) {
        List* root_list = input->root.list;
        for (int64_t i = 0; i < root_list->length; i++) {
            Item item = root_list->items[i];
            if (item.type_id() == LMD_TYPE_ELEMENT) {
                Element* elem = item.element;
                TypeElmt* type = (TypeElmt*)elem->type;

                // Skip DOCTYPE and comments
                if (strcmp(type->name.str, "!DOCTYPE") != 0 &&
                    strcmp(type->name.str, "!--") != 0) {
                    return elem;
                }
            }
        }
    }

    return nullptr;
}
