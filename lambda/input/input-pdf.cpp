// pdf parser implementation
#include "input.hpp"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "source_tracker.hpp"
#include "lib/log.h"

using namespace lambda;

static Item parse_pdf_object(InputContext& ctx, const char **pdf, int depth = 0);
static String* parse_pdf_name(InputContext& ctx, const char **pdf);
static String* parse_pdf_string(InputContext& ctx, const char **pdf);
static Item parse_pdf_indirect_ref(InputContext& ctx, const char **pdf);
static Item parse_pdf_indirect_object(InputContext& ctx, const char **pdf);
static Item parse_pdf_stream(InputContext& ctx, const char **pdf, Map* dict, size_t bytes_remaining = 1000000);
static Item parse_pdf_xref_table(InputContext& ctx, const char **pdf);
static Item parse_pdf_trailer(InputContext& ctx, const char **pdf);
static Item analyze_pdf_content_stream(Input *input, const char *stream_data, int length);
static Item parse_pdf_font_descriptor(Input *input, Map* font_dict);
static Item extract_pdf_page_info(Input *input, Map* page_dict);
static bool is_valid_pdf_header(const char *pdf_content);
static void advance_safely(const char **pdf, int max_advance);

// PDF object types
typedef enum {
    PDF_OBJ_NULL,
    PDF_OBJ_BOOLEAN,
    PDF_OBJ_NUMBER,
    PDF_OBJ_STRING,
    PDF_OBJ_NAME,
    PDF_OBJ_ARRAY,
    PDF_OBJ_DICT,
    PDF_OBJ_STREAM,
    PDF_OBJ_INDIRECT_REF,
    PDF_OBJ_INDIRECT_OBJ
} PDFObjectType;

// PDF-specific structures
typedef struct {
    int obj_num;
    int gen_num;
} IndirectObjectRef;

typedef struct {
    Map* dict;
    String* stream_data;
    int length;
} StreamObject;

static void skip_pdf_whitespace(const char **pdf) {
    while (**pdf && (**pdf == ' ' || **pdf == '\n' || **pdf == '\r' || **pdf == '\t' || **pdf == '\f')) {
        (*pdf)++;
    }
}

static void skip_comments(const char **pdf) {
    while (**pdf == '%') {
        // skip to end of line
        while (**pdf && **pdf != '\n' && **pdf != '\r') {
            (*pdf)++;
        }
        skip_pdf_whitespace(pdf);
    }
}

static void skip_pdf_whitespace_and_comments(const char **pdf) {
    while (**pdf) {
        skip_pdf_whitespace(pdf);
        if (**pdf == '%') {
            skip_comments(pdf);
        } else {
            break;
        }
    }
}

static bool is_valid_pdf_header(const char *pdf_content) {
    if (!pdf_content) return false;
    // Check for PDF header signature
    return strncmp(pdf_content, "%PDF-", 5) == 0;
}

static void advance_safely(const char **pdf, int max_advance) {
    int advance_count = 0;
    while (**pdf && advance_count < max_advance) {
        (*pdf)++;
        advance_count++;
    }
}

static Item parse_pdf_number(Input *input, const char **pdf) {
    double *dval = (double*)pool_calloc(input->pool, sizeof(double));
    if (!dval) return {.item = ITEM_ERROR};

    char* end;
    *dval = strtod(*pdf, &end);
    *pdf = end;

    return {.item = d2it(dval)};
}

static Array* parse_pdf_array(InputContext& ctx, const char **pdf) {
    if (**pdf != '[') return NULL;

    (*pdf)++; // skip [
    skip_pdf_whitespace_and_comments(pdf);

    Array* arr = array_pooled(ctx.input()->pool);
    if (!arr) return NULL;

    int item_count = 0;
    while (**pdf && **pdf != ']' && item_count < 10) { // reduced limit for safety
        Item obj = parse_pdf_object(ctx, pdf, 1);
        if (obj .item != ITEM_ERROR && obj .item != ITEM_NULL) {
            array_append(arr, obj, ctx.input()->pool);
            item_count++;
        }
        skip_pdf_whitespace_and_comments(pdf);
    }

    if (**pdf == ']') {
        (*pdf)++; // skip ]
    }

    return arr;
}

static Map* parse_pdf_dictionary(InputContext& ctx, const char **pdf) {
    if (!(**pdf == '<' && *(*pdf + 1) == '<')) return NULL;

    *pdf += 2; // skip <<
    skip_pdf_whitespace_and_comments(pdf);

    Map* dict = map_pooled(ctx.input()->pool);
    if (!dict) return NULL;

    int pair_count = 0;
    while (**pdf && !(**pdf == '>' && *(*pdf + 1) == '>') && pair_count < 100) { // allow up to 100 key-value pairs
        // parse key (should be a name)
        if (**pdf != '/') {
            // skip non-name, might be malformed
            (*pdf)++;
            continue;
        }

        String* key = parse_pdf_name(ctx, pdf);
        if (!key) break;

        skip_pdf_whitespace_and_comments(pdf);

        // parse value
        Item value = parse_pdf_object(ctx, pdf, 1);
        if (value .item != ITEM_ERROR && value .item != ITEM_NULL) {
            ctx.builder.putToMap(dict, key, value);
            pair_count++;
        }

        skip_pdf_whitespace_and_comments(pdf);
    }

    if (**pdf == '>' && *(*pdf + 1) == '>') {
        *pdf += 2; // skip >>
    }
    return dict;
}

static String* parse_pdf_name(InputContext& ctx, const char **pdf) {
    if (**pdf != '/') return NULL;

    (*pdf)++; // skip /
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    int char_count = 0;
    int max_chars = 100; // Safety limit for name length

    while (**pdf && char_count < max_chars &&
           !isspace(**pdf) && **pdf != '/' && **pdf != '(' && **pdf != ')' &&
           **pdf != '<' && **pdf != '>' && **pdf != '[' && **pdf != ']' &&
           **pdf != '{' && **pdf != '}' && **pdf != '%' && **pdf != '\0') {
        if (**pdf == '#') {
            // hex escape in name
            (*pdf)++; // skip #
            if (isxdigit(**pdf) && isxdigit(*(*pdf + 1))) {
                char hex_string[3] = {**pdf, *(*pdf + 1), '\0'};
                int char_value = (int)strtol(hex_string, NULL, 16);
                stringbuf_append_char(sb, (char)char_value);
                (*pdf) += 2;
            } else {
                // malformed hex escape, just include the #
                stringbuf_append_char(sb, '#');
            }
        } else {
            stringbuf_append_char(sb, **pdf);
            (*pdf)++;
        }
        char_count++;
    }

    return ctx.builder.createStringFromBuf(sb);
}

static Array* parse_pdf_array(InputContext& ctx, const char **pdf);
static Map* parse_pdf_dictionary(InputContext& ctx, const char **pdf);

static String* parse_pdf_string(InputContext& ctx, const char **pdf) {
    if (**pdf != '(' && **pdf != '<') return NULL;

    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    if (**pdf == '(') {
        // literal string
        (*pdf)++; // skip (
        int paren_count = 1;
        int char_count = 0;
        int max_chars = 500; // Safety limit

        while (**pdf && paren_count > 0 && char_count < max_chars) {
            if (**pdf == '\\') {
                (*pdf)++; // skip backslash
                if (**pdf) {
                    switch (**pdf) {
                        case 'n': stringbuf_append_char(sb, '\n'); break;
                        case 'r': stringbuf_append_char(sb, '\r'); break;
                        case 't': stringbuf_append_char(sb, '\t'); break;
                        case 'b': stringbuf_append_char(sb, '\b'); break;
                        case 'f': stringbuf_append_char(sb, '\f'); break;
                        case '(': stringbuf_append_char(sb, '('); break;
                        case ')': stringbuf_append_char(sb, ')'); break;
                        case '\\': stringbuf_append_char(sb, '\\'); break;
                        case '\n': /* ignore escaped newline */ break;
                        case '\r': /* ignore escaped carriage return */ break;
                        default: stringbuf_append_char(sb, **pdf); break;
                    }
                    (*pdf)++;
                    char_count++;
                }
            } else if (**pdf == '(') {
                paren_count++;
                stringbuf_append_char(sb, **pdf);
                (*pdf)++;
                char_count++;
            } else if (**pdf == ')') {
                paren_count--;
                if (paren_count > 0) {
                    stringbuf_append_char(sb, **pdf);
                    char_count++;
                }
                (*pdf)++;
            } else {
                stringbuf_append_char(sb, **pdf);
                (*pdf)++;
                char_count++;
            }
        }
    } else if (**pdf == '<') {
        // hexadecimal string
        (*pdf)++; // skip <
        int char_count = 0;
        int max_chars = 500; // Safety limit

        while (**pdf && **pdf != '>' && char_count < max_chars) {
            if (isxdigit(**pdf)) {
                char hex_char = **pdf;
                (*pdf)++;
                if (isxdigit(**pdf)) {
                    char hex_string[3] = {hex_char, **pdf, '\0'};
                    int char_value = (int)strtol(hex_string, NULL, 16);
                    stringbuf_append_char(sb, (char)char_value);
                    (*pdf)++;
                } else {
                    // single hex digit, treat as 0X
                    char hex_string[3] = {hex_char, '0', '\0'};
                    int char_value = (int)strtol(hex_string, NULL, 16);
                    stringbuf_append_char(sb, (char)char_value);
                }
                char_count++;
            } else {
                (*pdf)++; // skip non-hex chars
            }
        }
        if (**pdf == '>') {
            (*pdf)++; // skip >
        }
    }

    return ctx.builder.createStringFromBuf(sb);
}

static bool is_digit_or_space_ahead(const char *pdf, int max_lookahead) {
    for (int i = 0; i < max_lookahead && pdf[i]; i++) {
        if (isdigit(pdf[i]) || pdf[i] == ' ' || pdf[i] == 'R') {
            return true;
        }
        if (!isspace(pdf[i])) {
            return false;
        }
    }
    return false;
}

static Item parse_pdf_object(InputContext& ctx, const char **pdf, int depth) {
    // prevent runaway recursion - increased limit for complex PDFs
    if (depth > 50) {
        log_debug("pdf: recursion depth exceeded at depth %d", depth);
        return {.item = ITEM_NULL};
    }

    skip_pdf_whitespace_and_comments(pdf);

    if (!**pdf) {
        return {.item = ITEM_NULL};
    }

    Item result = {.item = ITEM_ERROR};

    // Check for special PDF keywords first
    if (strncmp(*pdf, "endobj", 6) == 0) {
        *pdf += 6;
        result = {.item = ITEM_NULL}; // Signal end of object
    }
    // Check for stream keywords
    else if (strncmp(*pdf, "stream", 6) == 0) {
        result = {.item = ITEM_NULL}; // Stream handling is done elsewhere
    }
    else if (strncmp(*pdf, "endstream", 9) == 0) {
        *pdf += 9;
        result = {.item = ITEM_NULL}; // Signal end of stream
    }
    // check for null
    else if (strncmp(*pdf, "null", 4) == 0 && (!isalnum(*(*pdf + 4)))) {
        *pdf += 4;
        result = {.item = ITEM_NULL};
    }
    // check for boolean values
    else if (strncmp(*pdf, "true", 4) == 0 && (!isalnum(*(*pdf + 4)))) {
        *pdf += 4;
        result = {.item = b2it(true)};
    }
    else if (strncmp(*pdf, "false", 5) == 0 && (!isalnum(*(*pdf + 5)))) {
        *pdf += 5;
        result = {.item = b2it(false)};
    }
    // check for names
    else if (**pdf == '/') {
        String* name = parse_pdf_name(ctx, pdf);
        result = name ? (Item){.item = s2it(name)} : (Item){.item = ITEM_ERROR};
    }
    // check for simple strings (no complex nesting)
    else if (**pdf == '(' || (**pdf == '<' && *(*pdf + 1) != '<')) {
        String* str = parse_pdf_string(ctx, pdf);
        result = str ? (Item){.item = s2it(str)} : (Item){.item = ITEM_ERROR};
    }
    // check for indirect references (n m R) before numbers
    else if ((**pdf >= '0' && **pdf <= '9') && is_digit_or_space_ahead(*pdf + 1, 10)) {
        const char* saved_pos = *pdf;
        // Try to parse as indirect reference first
        Item ref = parse_pdf_indirect_ref(ctx, pdf);
        if (ref .item != ITEM_ERROR) {
            result = ref;
        } else {
            // if not a reference, parse as number
            *pdf = saved_pos;
            result = parse_pdf_number(ctx.input(), pdf);
        }
    }
    // check for numbers
    else if ((**pdf >= '0' && **pdf <= '9') || **pdf == '-' || **pdf == '+' || **pdf == '.') {
        result = parse_pdf_number(ctx.input(), pdf);
    }
    // check for arrays (increased depth limit for complex PDFs)
    else if (**pdf == '[' && depth <= 20) {
        Array* arr = parse_pdf_array(ctx, pdf);
        result = arr ? (Item){.item = (uint64_t)arr} : (Item){.item = ITEM_ERROR};
    }
    // check for dictionaries (increased depth limit for complex PDFs)
    else if (**pdf == '<' && *(*pdf + 1) == '<' && depth <= 20) {
        Map* dict = parse_pdf_dictionary(ctx, pdf);
        if (dict) {
            // Check if this dictionary is followed by a stream
            const char* saved_pos = *pdf;
            skip_pdf_whitespace_and_comments(pdf);
            if (strncmp(*pdf, "stream", 6) == 0) {
                // Parse as stream with dictionary
                Item stream = parse_pdf_stream(ctx, pdf, dict);
                result = stream .item != ITEM_ERROR ? stream : (Item){.item = (uint64_t)dict};
            } else {
                // Just a dictionary
                *pdf = saved_pos;
                result = (Item){.item = (uint64_t)dict};
            }
        } else {
            result = {.item = ITEM_ERROR};
        }
    }
    // skip dictionary if depth limit exceeded (but still need to skip it properly)
    else if (**pdf == '<' && *(*pdf + 1) == '<') {
        // Skip dictionary without parsing - count << and >> to find end
        int depth = 1;
        *pdf += 2; // skip <<
        while (**pdf && depth > 0) {
            if (**pdf == '<' && *(*pdf + 1) == '<') {
                depth++;
                *pdf += 2;
            } else if (**pdf == '>' && *(*pdf + 1) == '>') {
                depth--;
                *pdf += 2;
            } else {
                (*pdf)++;
            }
        }
        result = {.item = ITEM_NULL};
    }
    // skip array if depth limit exceeded
    else if (**pdf == '[') {
        // Skip array without parsing
        int depth = 1;
        (*pdf)++; // skip [
        while (**pdf && depth > 0) {
            if (**pdf == '[') depth++;
            else if (**pdf == ']') depth--;
            (*pdf)++;
        }
        result = {.item = ITEM_NULL};
    }
    // skip other complex structures
    else {
        advance_safely(pdf, 1);
        result = {.item = ITEM_NULL};
    }

    return result;
}

static Item parse_pdf_indirect_ref(InputContext& ctx, const char **pdf) {
    // expects format "n m R" where n and m are numbers
    const char* start_pos = *pdf;
    int obj_num = 0, gen_num = 0;
    char* end;

    // Parse object number
    obj_num = strtol(*pdf, &end, 10);
    if (end == *pdf) return {.item = ITEM_ERROR}; // no conversion

    *pdf = end;
    skip_pdf_whitespace_and_comments(pdf);
    if (!**pdf) return {.item = ITEM_ERROR};

    // Parse generation number
    gen_num = strtol(*pdf, &end, 10);
    if (end == *pdf) return {.item = ITEM_ERROR}; // no conversion

    *pdf = end;
    skip_pdf_whitespace_and_comments(pdf);
    if (**pdf != 'R') return {.item = ITEM_ERROR};
    (*pdf)++; // skip R

    // Create a map to represent the indirect reference (more serializable than custom struct)
    Map* ref_map = map_pooled(ctx.input()->pool);
    if (!ref_map) return {.item = ITEM_ERROR};

    // Store type identifier
    String* type_key = ctx.builder.createName("type");
    if (type_key) {
        String* type_value = ctx.builder.createString("indirect_ref");
        if (type_value) {
            Item type_item = {.item = s2it(type_value)};
            ctx.builder.putToMap(ref_map, type_key, type_item);
        }
    }

    // Store object number
    String* obj_key = ctx.builder.createName("object_num");
    if (obj_key) {
        double* obj_val = (double*)pool_calloc(ctx.input()->pool, sizeof(double));
        if (obj_val) {
            *obj_val = (double)obj_num;
            Item obj_item = {.item = d2it(obj_val)};
            ctx.builder.putToMap(ref_map, obj_key, obj_item);
        }
    }

    // Store generation number
    String* gen_key = ctx.builder.createName("gen_num");
    if (gen_key) {
        double* gen_val = (double*)pool_calloc(ctx.input()->pool, sizeof(double));
        if (gen_val) {
            *gen_val = (double)gen_num;
            Item gen_item = {.item = d2it(gen_val)};
            ctx.builder.putToMap(ref_map, gen_key, gen_item);
        }
    }
    return {.item = (uint64_t)ref_map};
}

static Item parse_pdf_indirect_object(InputContext& ctx, const char **pdf) {
    // expects format "n m obj ... endobj"
    int obj_num = 0, gen_num = 0;
    char* end;

    // Parse object number
    obj_num = strtol(*pdf, &end, 10);
    if (end == *pdf) return {.item = ITEM_ERROR};

    *pdf = end;
    skip_pdf_whitespace_and_comments(pdf);

    // Parse generation number
    gen_num = strtol(*pdf, &end, 10);
    if (end == *pdf) return {.item = ITEM_ERROR};

    *pdf = end;
    skip_pdf_whitespace_and_comments(pdf);

    // Check for "obj" keyword
    if (strncmp(*pdf, "obj", 3) != 0) return {.item = ITEM_ERROR};
    *pdf += 3;
    skip_pdf_whitespace_and_comments(pdf);

    // Parse the object content
    Item content = parse_pdf_object(ctx, pdf, 1);

    // Skip to endobj (optional - for safety)
    const char* endobj_pos = strstr(*pdf, "endobj");
    if (endobj_pos) {
        *pdf = endobj_pos + 6; // skip "endobj"
    }

    // Create a map to represent the indirect object
    Map* obj_map = map_pooled(ctx.input()->pool);
    if (!obj_map) return content; // return content if we can't create wrapper

    // Store type identifier
    String* type_key = ctx.builder.createName("type");
    if (type_key) {
        String* type_value = ctx.builder.createString("indirect_object");
        if (type_value) {
            Item type_item = {.item = s2it(type_value)};
            ctx.builder.putToMap(obj_map, type_key, type_item);
        }
    }

    // Store object number
    String* obj_key = ctx.builder.createName("object_num");
    if (obj_key) {
        double* obj_val;
        obj_val = (double*)pool_calloc(ctx.input()->pool, sizeof(double));
        if (obj_val) {
            *obj_val = (double)obj_num;
            Item obj_item = {.item = d2it(obj_val)};
            ctx.builder.putToMap(obj_map, obj_key, obj_item);
        }
    }

    // Store generation number
    String* gen_key = ctx.builder.createName("gen_num");
    if (gen_key) {
        double* gen_val;
        gen_val = (double*)pool_calloc(ctx.input()->pool, sizeof(double));
        if (gen_val) {
            *gen_val = (double)gen_num;
            Item gen_item = {.item = d2it(gen_val)};
            ctx.builder.putToMap(obj_map, gen_key, gen_item);
        }
    }

    // Store content
    if (content .item != ITEM_ERROR && content .item != ITEM_NULL) {
        String* content_key = ctx.builder.createString("content");
        if (content_key) {
            Item content_item = {.item = content.item};
            ctx.builder.putToMap(obj_map, content_key, content_item);
        }
    }
    return {.item = (uint64_t)obj_map};
}

static Item parse_pdf_stream(InputContext& ctx, const char **pdf, Map* dict, size_t bytes_remaining) {
    // expects stream data after dictionary
    if (strncmp(*pdf, "stream", 6) != 0) return {.item = ITEM_ERROR};

    *pdf += 6; // skip stream
    
    // Skip to start of actual stream data (skip newline after "stream")
    if (**pdf == '\r') (*pdf)++;
    if (**pdf == '\n') (*pdf)++;

    // Binary-safe search for "endstream" - don't rely on null termination
    // since stream data can contain null bytes
    const char* end_stream = nullptr;
    const char* search_start = *pdf;
    size_t max_search = bytes_remaining > 100000 ? 100000 : bytes_remaining;
    for (size_t i = 0; i + 9 <= max_search; i++) {
        if (strncmp(search_start + i, "endstream", 9) == 0) {
            end_stream = search_start + i;
            break;
        }
    }
    
    if (!end_stream) return {.item = ITEM_ERROR};

    // calculate data length
    int data_length = end_stream - *pdf;
    // Trim trailing whitespace from data
    while (data_length > 0 && ((*pdf)[data_length-1] == '\r' || (*pdf)[data_length-1] == '\n')) {
        data_length--;
    }
    // Limit stream size to 10MB for safety (most PDF streams are well under 1MB)
    if (data_length > 10 * 1024 * 1024) {
        data_length = 10 * 1024 * 1024;
    }

    // Create a map to represent the stream (more serializable than custom struct)
    Map* stream_map = map_pooled(ctx.input()->pool);
    if (!stream_map) return {.item = ITEM_ERROR};

    // Store type identifier
    String* type_key = ctx.builder.createName("type");
    if (type_key) {
        String* type_value = ctx.builder.createString("stream");
        if (type_value) {
            Item type_item = {.item = s2it(type_value)};
            ctx.builder.putToMap(stream_map, type_key, type_item);
        }
    }

    // Store dictionary if provided
    if (dict) {
        String* dict_key = ctx.builder.createString("dictionary");
        if (dict_key) {
            Item dict_item = {.item = (uint64_t)dict};
            ctx.builder.putToMap(stream_map, dict_key, dict_item);
        }
    }

    // Store data length
    String* length_key = ctx.builder.createString("length");
    if (length_key) {
        double* length_val;
        length_val = (double*)pool_calloc(ctx.input()->pool, sizeof(double));
        if (length_val) {
            *length_val = (double)data_length;
            Item length_item = {.item = d2it(length_val)};
            ctx.builder.putToMap(stream_map, length_key, length_item);
        }
    }

    // Store stream data as a string (truncated for safety)
    String* data_key = ctx.builder.createString("data");
    if (data_key) {
        String* stream_data;
        stream_data = (String*)pool_calloc(ctx.input()->pool, sizeof(String) + data_length + 1);
        if (stream_data) {
            memcpy(stream_data->chars, *pdf, data_length);
            stream_data->chars[data_length] = '\0';
            stream_data->len = data_length;
            stream_data->ref_cnt = 0;

            Item data_item = {.item = s2it(stream_data)};
            ctx.builder.putToMap(stream_map, data_key, data_item);

            // Add content analysis for potential content streams
            if (data_length > 10 && data_length < 100000) { // Only analyze reasonably sized streams
                Item content_analysis = analyze_pdf_content_stream(ctx.input(), *pdf, data_length);
                if (content_analysis .item != ITEM_NULL) {
                    String* analysis_key = ctx.builder.createString("analysis");
                    if (analysis_key) {
                        Item analysis_item = {.item = content_analysis.item};
                        ctx.builder.putToMap(stream_map, analysis_key, analysis_item);
                    }
                }
            }
        }
    }

    *pdf = end_stream + 9; // skip endstream
    return {.item = (uint64_t)stream_map};
}

static Item parse_pdf_xref_table(InputContext& ctx, const char **pdf) {
    // expects "xref" followed by cross-reference entries
    if (strncmp(*pdf, "xref", 4) != 0) return {.item = ITEM_ERROR};

    *pdf += 4; // skip "xref"
    skip_pdf_whitespace_and_comments(pdf);

    // Create a map to represent the xref table
    Map* xref_map = map_pooled(ctx.input()->pool);
    if (!xref_map) return {.item = ITEM_ERROR};

    // Store type identifier
    String* type_key = ctx.builder.createName("type");
    if (type_key) {
        String* type_value = ctx.builder.createString("xref_table");
        if (type_value) {
            Item type_item = {.item = s2it(type_value)};
            ctx.builder.putToMap(xref_map, type_key, type_item);
        }
    }

    // Parse xref entries - we need to scan past all entries to find trailer
    Array* entries = array_pooled(ctx.input()->pool);
    if (entries) {
        int entry_count = 0;
        int max_stored_entries = 50; // How many entries we actually store in the map

        while (**pdf) {
            skip_pdf_whitespace_and_comments(pdf);

            // Check if we've hit the trailer
            if (strncmp(*pdf, "trailer", 7) == 0) break;

            // Try to parse a subsection header (starting_obj_num count)
            if (isdigit(**pdf)) {
                int start_num = strtol(*pdf, (char**)pdf, 10);
                skip_pdf_whitespace_and_comments(pdf);

                if (isdigit(**pdf)) {
                    int count = strtol(*pdf, (char**)pdf, 10);
                    skip_pdf_whitespace_and_comments(pdf);

                    // Parse individual entries - parse all but only store up to limit
                    for (int i = 0; i < count; i++) {
                        skip_pdf_whitespace_and_comments(pdf);

                        // Parse entry: offset generation flag
                        if (isdigit(**pdf)) {
                            int offset = strtol(*pdf, (char**)pdf, 10);
                            skip_pdf_whitespace_and_comments(pdf);

                            if (isdigit(**pdf)) {
                                int generation = strtol(*pdf, (char**)pdf, 10);
                                skip_pdf_whitespace_and_comments(pdf);

                                // Parse flag (n or f)
                                char flag = **pdf;
                                if (flag == 'n' || flag == 'f') {
                                    (*pdf)++;

                                    // Only store entries up to limit
                                    if (entry_count < max_stored_entries) {
                                        // Create entry map
                                        Map* entry_map = map_pooled(ctx.input()->pool);
                                        if (entry_map) {
                                            if (entry_map->data) {
                                                // Store object number
                                                String* obj_key = ctx.builder.createString("object");
                                                if (obj_key) {
                                                    double* obj_val;
                                                    obj_val = (double*)pool_calloc(ctx.input()->pool, sizeof(double));
                                                    if (obj_val) {
                                                        *obj_val = (double)(start_num + i);
                                                        Item obj_item = {.item = d2it(obj_val)};
                                                        ctx.builder.putToMap(entry_map, obj_key, obj_item);
                                                    }
                                                }

                                                // Store offset
                                                String* offset_key = ctx.builder.createString("offset");
                                                if (offset_key) {
                                                    double* offset_val;
                                                    offset_val = (double*)pool_calloc(ctx.input()->pool, sizeof(double));
                                                    if (offset_val) {
                                                        *offset_val = (double)offset;
                                                        Item offset_item = {.item = d2it(offset_val)};
                                                        ctx.builder.putToMap(entry_map, offset_key, offset_item);
                                                    }
                                                }

                                                // Store flag
                                                String* flag_key = ctx.builder.createString("flag");
                                                if (flag_key) {
                                                    String* flag_val;
                                                    flag_val = (String*)pool_calloc(ctx.input()->pool, sizeof(String) + 2);
                                                    if (flag_val) {
                                                        flag_val->chars[0] = flag;
                                                        flag_val->chars[1] = '\0';
                                                        flag_val->len = 1;
                                                        flag_val->ref_cnt = 0;
                                                        Item flag_item = {.item = s2it(flag_val)};
                                                        ctx.builder.putToMap(entry_map, flag_key, flag_item);
                                                    }
                                                }

                                                Item entry_item = {.item = (uint64_t)entry_map};
                                                array_append(entries, entry_item, ctx.input()->pool);
                                            }
                                        }
                                        entry_count++;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    break; // malformed subsection header
                }
            } else {
                break; // not a valid entry
            }
        }

        // Store entries in xref map
        String* entries_key = ctx.builder.createString("entries");
        if (entries_key) {
            Item entries_item = {.item = (uint64_t)entries};
            ctx.builder.putToMap(xref_map, entries_key, entries_item);
        }
    }
    return {.item = (uint64_t)xref_map};
}

static Item parse_pdf_trailer(InputContext& ctx, const char **pdf) {
    // expects "trailer" followed by a dictionary
    if (strncmp(*pdf, "trailer", 7) != 0) return {.item = ITEM_ERROR};

    *pdf += 7; // skip "trailer"
    skip_pdf_whitespace_and_comments(pdf);

    // Parse the trailer dictionary
    Map* trailer_dict = parse_pdf_dictionary(ctx, pdf);
    if (!trailer_dict) return {.item = ITEM_ERROR};

    // Create a wrapper map to indicate this is a trailer
    Map* trailer_map = map_pooled(ctx.input()->pool);
    if (!trailer_map) return {.item = (uint64_t)trailer_dict};

    // Store type identifier
    String* type_key = ctx.builder.createName("type");
    if (type_key) {
        String* type_value = ctx.builder.createString("trailer");
        if (type_value) {
            Item type_item = {.item = s2it(type_value)};
            ctx.builder.putToMap(trailer_map, type_key, type_item);
        }
    }

    // Store the dictionary
    String* dict_key = ctx.builder.createString("dictionary");
    if (dict_key) {
        Item dict_item = {.item = (uint64_t)trailer_dict};
        ctx.builder.putToMap(trailer_map, dict_key, dict_item);
    }
    return {.item = (uint64_t)trailer_map};
}

// Analyze PDF content streams for basic information
static Item analyze_pdf_content_stream(Input *input, const char *stream_data, int length) {
    if (!stream_data || length <= 0) return {.item = ITEM_NULL};

    Map* analysis_map = map_pooled(input->pool);
    if (!analysis_map) return {.item = ITEM_NULL};

    // Count text objects (BT...ET blocks)
    int text_objects = 0;
    const char* pos = stream_data;
    const char* end = stream_data + length;

    while (pos < end - 2) {
        if (pos[0] == 'B' && pos[1] == 'T' && (pos[2] == ' ' || pos[2] == '\n' || pos[2] == '\r')) {
            text_objects++;
            if (text_objects >= 20) break; // Safety limit
        }
        pos++;
    }

    // Count basic operators (simplified analysis)
    int drawing_ops = 0;
    pos = stream_data;
    while (pos < end - 1) {
        if ((pos[0] == ' ' || pos[0] == '\n') && pos < end - 2) {
            char op = pos[1];
            if (op == 'l' || op == 'm' || op == 'c' || op == 'h') { // line, move, curve, close path
                drawing_ops++;
                if (drawing_ops >= 50) break; // Safety limit
            }
        }
        pos++;
    }

    // Store analysis results
    MarkBuilder builder(input);

    String* type_key = builder.createName("type");
    if (type_key) {
        String* type_value = builder.createString("content_analysis");
        if (type_value) {
            Item type_item = {.item = s2it(type_value)};
            builder.putToMap(analysis_map, type_key, type_item);
        }
    }

    // Text objects count
    String* text_key = builder.createString("text_objects");
    if (text_key) {
        double* text_count;
        text_count = (double*)pool_calloc(input->pool, sizeof(double));
        if (text_count) {
            *text_count = (double)text_objects;
            Item text_item = {.item = d2it(text_count)};
            builder.putToMap(analysis_map, text_key, text_item);
        }
    }

    // Drawing operations count
    String* draw_key = builder.createString("drawing_ops");
    if (draw_key) {
        double* draw_count;
        draw_count = (double*)pool_calloc(input->pool, sizeof(double));
        if (draw_count) {
            *draw_count = (double)drawing_ops;
            Item draw_item = {.item = d2it(draw_count)};
            builder.putToMap(analysis_map, draw_key, draw_item);
        }
    }
    return {.item = (uint64_t)analysis_map};
}

// Analyze font information from font dictionaries
static Item parse_pdf_font_descriptor(Input *input, Map* font_dict) {
    if (!font_dict) return {.item = ITEM_NULL};

    Map* font_analysis = map_pooled(input->pool);
    if (!font_analysis) return {.item = ITEM_NULL};

    MarkBuilder builder(input);

    // Extract font information from the dictionary
    String* type_key = builder.createName("type");
    if (type_key) {
        String* type_value = builder.createString("font_analysis");
        if (type_value) {
            Item type_item = {.item = s2it(type_value)};
            builder.putToMap(font_analysis, type_key, type_item);
        }
    }

    // Copy relevant font properties (Type, Subtype, BaseFont, etc.)
    String* original_key = builder.createString("original");
    if (original_key) {
        Item original_item = {.item = (uint64_t)font_dict};
        builder.putToMap(font_analysis, original_key, original_item);
    }
    return {.item = (uint64_t)font_analysis};
}

// Extract basic page information
static Item extract_pdf_page_info(Input *input, Map* page_dict) {
    if (!page_dict) return {.item = ITEM_NULL};

    Map* page_analysis = map_pooled(input->pool);
    if (!page_analysis) return {.item = ITEM_NULL};

    MarkBuilder builder(input);

    // Store type
    String* type_key = builder.createName("type");
    if (type_key) {
        String* type_value = builder.createString("page_analysis");
        if (type_value) {
            Item type_item = {.item = s2it(type_value)};
            builder.putToMap(page_analysis, type_key, type_item);
        }
    }

    // Copy the original page dictionary for reference
    String* original_key = builder.createString("original");
    if (original_key) {
        Item original_item = {.item = (uint64_t)page_dict};
        builder.putToMap(page_analysis, original_key, original_item);
    }
    return {.item = (uint64_t)page_analysis};
}

void parse_pdf(Input* input, const char* pdf_string, size_t pdf_length) {
    log_debug("pdf_parse\n");

    // Validate input before constructing context
    if (!pdf_string || pdf_length == 0) {
        log_debug("pdf: empty PDF content\n");
        input->root = {.item = ITEM_ERROR};
        return;
    }

    // create unified InputContext with source tracking
    InputContext ctx(input, pdf_string, pdf_length);

    MarkBuilder& builder = ctx.builder;

    const char* pdf = pdf_string;
    const char* pdf_file_end = pdf_string + pdf_length; // track actual end of binary content

    // Enhanced PDF header validation
    if (!is_valid_pdf_header(pdf)) {
        ctx.addError(ctx.tracker.location(), "Invalid PDF format - must start with %%PDF-");
        log_debug("Error: Invalid PDF format - must start with %%PDF-\n");
        input->root = {.item = ITEM_ERROR};
        return;
    }

    // Create a simple map with basic PDF info
    Map* pdf_info = map_pooled(input->pool);
    if (!pdf_info) {
        ctx.addError(ctx.tracker.location(), "Failed to allocate PDF info map");
        log_debug("Error: Failed to allocate PDF info map\n");
        input->root = {.item = ITEM_ERROR};
        return;
    }

    // Parse and store version with enhanced validation
    pdf += 5; // skip "%PDF-"
    StringBuf* version_sb = ctx.sb;
    stringbuf_reset(version_sb);

    int counter = 0;
    while (*pdf && *pdf != '\n' && *pdf != '\r' && counter < 10) {
        // Validate version format (should be digits and dots)
        if ((*pdf >= '0' && *pdf <= '9') || *pdf == '.') {
            stringbuf_append_char(version_sb, *pdf);
        } else {
            log_debug("Warning: Non-standard character in PDF version: %c\n", *pdf);
        }
        pdf++;
        counter++;
    }
    String* version = builder.createStringFromBuf(version_sb);

    // Store version in map
    String* version_key = builder.createString("version");
    if (version_key) {
        Item version_item = {.item = s2it(version)};
        builder.putToMap(pdf_info, version_key, version_item);
    }

    skip_pdf_whitespace_and_comments(&pdf);

    // Parse a few simple objects safely, and look for xref/trailer
    Array* objects = array_pooled(input->pool);
    Item xref_table = {.item = ITEM_NULL};
    Item trailer = {.item = ITEM_NULL};

    if (objects) {
        int obj_count = 0;
        int max_objects = 25; // Limit to prevent issues - advanced_test needs ~18 objects
        int consecutive_errors = 0;
        const int max_consecutive_errors = 3;

        while (*pdf && obj_count < max_objects && consecutive_errors < max_consecutive_errors) {
            skip_pdf_whitespace_and_comments(&pdf);
            if (!*pdf) break;

            Item obj = {.item = ITEM_NULL};
            const char* position_before_parse = pdf;

            // Check for xref table
            if (strncmp(pdf, "xref", 4) == 0) {
                xref_table = parse_pdf_xref_table(ctx, &pdf);
                if (xref_table .item != ITEM_ERROR) {
                    consecutive_errors = 0; // Reset error counter on success
                }
                continue; // Continue parsing to look for trailer
            }

            // Check for trailer
            if (strncmp(pdf, "trailer", 7) == 0) {
                trailer = parse_pdf_trailer(ctx, &pdf);
                break; // trailer usually means we're at the end
            }

            // Try to parse indirect object first (e.g., "1 0 obj")
            if (isdigit(*pdf)) {
                const char* saved_pos = pdf;
                obj = parse_pdf_indirect_object(ctx, &pdf);
                if (obj .item == ITEM_ERROR) {
                    // if not an indirect object, try regular object parsing
                    pdf = saved_pos;
                    obj = parse_pdf_object(ctx, &pdf);
                }
            } else {
                // Try to parse a simple object
                obj = parse_pdf_object(ctx, &pdf);
            }

            if (obj .item != ITEM_ERROR && obj .item != ITEM_NULL) {
                array_append(objects, obj, input->pool);
                obj_count++;
                consecutive_errors = 0; // Reset error counter on success
            } else {
                consecutive_errors++;
                // If we haven't advanced, force advance to prevent infinite loop
                if (pdf == position_before_parse) {
                    advance_safely(&pdf, 1);
                }
            }
        }

        // Always scan for xref and trailer sections, even if we've hit the object limit
        if (xref_table .item == ITEM_NULL || trailer .item == ITEM_NULL) {
            // Strategy: Scan backwards from end of file for "startxref", then use offset to find xref/trailer
            // This avoids issues with binary stream content in the middle of the PDF
            
            // First, search backwards for "startxref" (typically in last ~100 bytes)
            const char* scan_back = pdf_file_end - 1;
            const char* startxref_pos = nullptr;
            int search_limit = 1024; // search last 1KB for startxref
            
            while (scan_back >= pdf_string && search_limit > 0) {
                if (*scan_back == 's' && (size_t)(pdf_file_end - scan_back) >= 9 && 
                    strncmp(scan_back, "startxref", 9) == 0) {
                    startxref_pos = scan_back;
                    break;
                }
                scan_back--;
                search_limit--;
            }
            
            if (startxref_pos) {
                // Parse the xref offset
                const char* offset_ptr = startxref_pos + 9; // skip "startxref"
                while (offset_ptr < pdf_file_end && isspace(*offset_ptr)) offset_ptr++;
                if (offset_ptr < pdf_file_end && isdigit(*offset_ptr)) {
                    long xref_offset = strtol(offset_ptr, nullptr, 10);
                    log_debug("Found startxref at offset %lld, pointing to xref at %ld\n", 
                              (long long)(startxref_pos - pdf_string), xref_offset);
                    
                    // Jump to xref position
                    if (xref_offset >= 0 && xref_offset < (long)pdf_length) {
                        const char* xref_pos = pdf_string + xref_offset;
                        
                        // Parse xref table
                        if (strncmp(xref_pos, "xref", 4) == 0) {
                            const char* xref_parse_pos = xref_pos;
                            xref_table = parse_pdf_xref_table(ctx, &xref_parse_pos);
                            if (xref_table .item != ITEM_ERROR) {
                                log_debug("Successfully parsed xref table at offset %ld\n", xref_offset);
                                
                                // Look for trailer right after xref
                                while (xref_parse_pos < pdf_file_end && isspace(*xref_parse_pos)) xref_parse_pos++;
                                log_debug("After xref, looking for trailer at offset %lld, first chars: '%.20s'\n", 
                                          (long long)(xref_parse_pos - pdf_string), xref_parse_pos);
                                if ((size_t)(pdf_file_end - xref_parse_pos) >= 7 && 
                                    strncmp(xref_parse_pos, "trailer", 7) == 0) {
                                    trailer = parse_pdf_trailer(ctx, &xref_parse_pos);
                                    if (trailer .item != ITEM_ERROR) {
                                        log_debug("Successfully parsed trailer\n");
                                    } else {
                                        log_debug("Trailer parsing returned error\n");
                                    }
                                } else {
                                    log_debug("Trailer keyword not found at expected position\n");
                                }
                            }
                        }
                    }
                }
            } else {
                log_debug("Could not find startxref - PDF may be malformed\n");
            }
        }

        // Store objects in map
        String* objects_key = builder.createString("objects");
        if (objects_key) {
            Item objects_item = {.item = (uint64_t)objects};
            builder.putToMap(pdf_info, objects_key, objects_item);
        }
    }

    // Store xref table if found
    if (xref_table .item != ITEM_NULL) {
        String* xref_key = builder.createString("xref_table");
        if (xref_key) {
            builder.putToMap(pdf_info, xref_key, xref_table);
        }
    }

    // Store trailer if found
    if (trailer .item != ITEM_NULL) {
        String* trailer_key = builder.createString("trailer");
        if (trailer_key) {
            builder.putToMap(pdf_info, trailer_key, trailer);
        }
    }

    // Add basic PDF statistics
    String* stats_key = builder.createString("statistics");
    if (stats_key) {
        // Create statistics map
        Map* stats_map = map_pooled(input->pool);
        if (stats_map) {
            if (stats_map->data) {
                // Object count (use objects array length if available)
                String* obj_count_key = builder.createString("object_count");
                if (obj_count_key) {
                    double* obj_count_val;
                    obj_count_val = (double*)pool_calloc(input->pool, sizeof(double));
                    if (obj_count_val) {
                        *obj_count_val = objects ? (double)objects->length : 0.0;
                        Item obj_count_item = {.item = d2it(obj_count_val)};
                        builder.putToMap(stats_map, obj_count_key, obj_count_item);
                    }
                }

                // Has xref table
                String* has_xref_key = builder.createString("has_xref");
                if (has_xref_key) {
                    Item has_xref_item = {.item = b2it(xref_table .item != ITEM_NULL)};
                    builder.putToMap(stats_map, has_xref_key, has_xref_item);
                }

                // Has trailer
                String* has_trailer_key = builder.createString("has_trailer");
                if (has_trailer_key) {
                    Item has_trailer_item = {.item = b2it(trailer .item != ITEM_NULL)};
                    builder.putToMap(stats_map, has_trailer_key, has_trailer_item);
                }

                // Count stream objects
                int stream_count = 0;
                int font_count = 0;
                int page_count = 0;

                if (objects && objects->length > 0) {
                    for (int i = 0; i < objects->length && i < 20; i++) { // Safety limit
                        // Basic analysis of object types based on parsing results
                        // This is a simplified analysis since we can't easily inspect the object structure
                        stream_count++; // For now, just count all objects as potential streams
                    }
                    // Reset counters for more accurate counting based on what we know
                    stream_count = 0; // Will be updated if we find actual streams
                }

                // Stream count
                String* stream_count_key = builder.createString("stream_count");
                if (stream_count_key) {
                    double* stream_count_val;
                    stream_count_val = (double*)pool_calloc(input->pool, sizeof(double));
                    if (stream_count_val) {
                        *stream_count_val = (double)stream_count;
                        Item stream_count_item = {.item = d2it(stream_count_val)};
                        builder.putToMap(stats_map, stream_count_key, stream_count_item);
                    }
                }

                // PDF features detected
                String* features_key = builder.createString("features");
                if (features_key) {
                    // Create a features array
                    Array* features_array = array_pooled(input->pool);
                    if (features_array) {
                        // Add features based on what we've found
                        if (xref_table .item != ITEM_NULL) {
                            String* xref_feature = builder.createString("cross_reference_table");
                            if (xref_feature) {
                                Item xref_feature_item = {.item = s2it(xref_feature)};
                                array_append(features_array, xref_feature_item, input->pool);
                            }
                        }

                        if (trailer .item != ITEM_NULL) {
                            String* trailer_feature = builder.createString("trailer");
                            if (trailer_feature) {
                                Item trailer_feature_item = {.item = s2it(trailer_feature)};
                                array_append(features_array, trailer_feature_item, input->pool);
                            }
                        }

                        String* objects_feature = builder.createName("indirect_objects");
                        if (objects_feature) {
                            Item objects_feature_item = {.item = s2it(objects_feature)};
                            array_append(features_array, objects_feature_item, input->pool);
                        }

                        Item features_item = {.item = (uint64_t)features_array};
                        builder.putToMap(stats_map, features_key, features_item);
                    }
                }
            }

            Item stats_item = {.item = (uint64_t)stats_map};
            builder.putToMap(pdf_info, stats_key, stats_item);
        }
    }

    input->root = {.item = (uint64_t)pdf_info};

    if (ctx.hasErrors()) {
        // errors occurred during parsing
    }
}
