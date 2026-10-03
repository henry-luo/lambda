#include "../io/mark_output_builder.hpp"
#include "../core/collection_storage.h"
#include "../core/mark_reader.hpp"
#include "../core/print.h"
#include "lambda-error.h"
#include "lambda-number-runtime.hpp"
#include "../../lib/arraylist.hpp"
#include "../../lib/escape.h"
#include "../../lib/hashmap.h"
#include "../../lib/hex.h"
#include "../../lib/memtrack.h"
#include "../../lib/strbuf.h"
#include "../../lib/utf.h"
#include "../../lib/log.h"
#include <float.h>

struct PDFBuilder : MarkOutputBuilder {
    StrBuf* buffer;
    LambdaErrorCode failure;
    const char* failure_message;
    bool finished;
};

static void pdf_fail(PDFBuilder* builder, const char* message,
        LambdaErrorCode code = ERR_INVALID_OPERATION) {
    if (builder->failure) return;
    builder->failure = code;
    builder->failure_message = message;
    log_error("pdf-builder: %s", message);
}

static bool pdf_reserve(PDFBuilder* builder, size_t size) {
    if (builder->failure) return false;
    StrBuf* buffer = builder->buffer;
    if (size > SIZE_MAX - buffer->length - 1 ||
            !strbuf_ensure_cap(buffer, buffer->length + size + 1)) {
        pdf_fail(builder, "PDF output buffer allocation failed", ERR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static void pdf_write(PDFBuilder* builder, const char* data, size_t size) {
    if (pdf_reserve(builder, size)) strbuf_append_str_n(builder->buffer, data, size);
}

static void pdf_text(PDFBuilder* builder, const char* text) {
    pdf_write(builder, text, strlen(text));
}

static void pdf_hex(PDFBuilder* builder, const void* data, size_t size) {
    if (size > SIZE_MAX / 2 || !pdf_reserve(builder, size * 2)) {
        pdf_fail(builder, "PDF hex string allocation failed", ERR_OUT_OF_MEMORY);
        return;
    }
    StrBuf* buffer = builder->buffer;
    hex_encode_upper(data, size, buffer->str + buffer->length);
    buffer->length += size * 2;
    buffer->str[buffer->length] = '\0';
}

static void pdf_name(PDFBuilder* builder, const char* data, size_t size) {
    pdf_text(builder, "/");
    for (size_t i = 0; i < size && !builder->failure; i++) {
        unsigned char byte = (unsigned char)data[i];
        if (!byte) { pdf_fail(builder, "PDF names cannot contain NUL"); return; }
        if (byte <= 32 || byte >= 127 || strchr("()<>[]{}/%#", byte)) {
            pdf_text(builder, "#");
            pdf_hex(builder, &byte, 1);
        } else pdf_write(builder, data + i, 1);
    }
}

static void pdf_string(PDFBuilder* builder, String* string) {
    bool ascii = true;
    for (size_t i = 0; i < string->len; i++) {
        if ((unsigned char)string->chars[i] >= 128) { ascii = false; break; }
    }
    if (ascii) {
        if (!escape_append_pdf_literal(builder->buffer, string->chars, string->len))
            pdf_fail(builder, "PDF literal string allocation failed", ERR_OUT_OF_MEMORY);
        return;
    }

    pdf_text(builder, "<FEFF");
    for (size_t i = 0; i < string->len && !builder->failure;) {
        uint32_t codepoint;
        int read = utf8_decode(string->chars + i, string->len - i, &codepoint);
        if (read < 0) { pdf_fail(builder, "PDF object strings require valid UTF-8"); return; }
        uint16_t units[2];
        int count = utf16_encode(codepoint, units);
        for (int j = 0; j < count; j++) {
            uint8_t bytes[] = {(uint8_t)(units[j] >> 8), (uint8_t)units[j]};
            pdf_hex(builder, bytes, sizeof(bytes));
        }
        i += (size_t)read;
    }
    pdf_text(builder, ">");
}

// PDF numeric tokens have no exponent notation. Expand the canonical digits
// without passing exact integer carriers through a double.
static void pdf_number_text(PDFBuilder* builder, const char* number) {
    const char* exponent = strpbrk(number, "eE");
    if (!exponent) { pdf_text(builder, number); return; }
    if (*number == '-') { pdf_text(builder, "-"); number++; }
    const char* dot = (const char*)memchr(number, '.', (size_t)(exponent - number));
    int point = (int)((dot ? dot : exponent) - number) + atoi(exponent + 1);
    int digit = 0;
    if (point <= 0) {
        pdf_text(builder, "0.");
        if (pdf_reserve(builder, (size_t)-point))
            strbuf_append_char_n(builder->buffer, '0', (size_t)-point);
    }
    for (const char* p = number; p < exponent && !builder->failure; p++) {
        if (*p == '.') continue;
        if (digit == point && digit > 0) pdf_text(builder, ".");
        pdf_write(builder, p, 1);
        digit++;
    }
    if (point > digit && pdf_reserve(builder, (size_t)(point - digit)))
        strbuf_append_char_n(builder->buffer, '0', (size_t)(point - digit));
}

static void pdf_number(PDFBuilder* builder, Item item) {
    LambdaNumericRuntimePart part;
    if (!lambda_numeric_runtime_part(item, &part)) {
        pdf_fail(builder, "PDF decimal encoding remains deferred");
        return;
    }
    char number[32];
    if (part.kind == LAMBDA_NUM_PART_SIGNED)
        snprintf(number, sizeof(number), "%" PRId64, part.signed_value);
    else if (part.kind == LAMBDA_NUM_PART_UNSIGNED)
        snprintf(number, sizeof(number), "%" PRIu64, part.unsigned_value);
    else {
        if (!isfinite(part.float_value)) {
            pdf_fail(builder, "PDF numbers must be finite");
            return;
        }
        if (get_type_id(item) == LMD_TYPE_INT) {
            if (pdf_reserve(builder, DBL_MAX_10_EXP + 4))
                print_int_value(builder->buffer, part.float_value);
            return;
        }
        lambda_double_to_shortest(part.float_value, number, sizeof(number));
    }
    pdf_number_text(builder, number);
}

struct PDFField {
    StrView* name;
    ShapeEntry* field;
    void* data;
    size_t index;
};

static uint64_t pdf_field_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const StrView* name = ((const PDFField*)item)->name;
    return hashmap_xxhash3(name->str, name->length, seed0, seed1);
}

static int pdf_field_compare(const void* left, const void* right, void*) {
    const StrView* a = ((const PDFField*)left)->name;
    const StrView* b = ((const PDFField*)right)->name;
    if (a->length != b->length) return a->length < b->length ? -1 : 1;
    return memcmp(a->str, b->str, a->length);
}

static constexpr int PDF_MAX_NESTING = 64;

static void pdf_collect_fields(PDFBuilder* builder, Map* map, HashMap* seen,
        lam::ArrayList<PDFField>& fields, int depth) {
    if (depth >= PDF_MAX_NESTING) {
        pdf_fail(builder, "PDF dictionary nesting limit exceeded");
        return;
    }
    FOR_EACH_MAP_FIELD((TypeMap*)map->type, field) {
        if (builder->failure) return;
        if (!field->name) {
            Map* spread = map_shape_field_to_map(map->data, field);
            if (spread) pdf_collect_fields(builder, spread, seen, fields, depth + 1);
            continue;
        }
        if (field->ns || field->key_kind != NAME_KEY_STRING) {
            pdf_fail(builder, "PDF dictionary keys require unqualified text names");
            return;
        }
        PDFField entry = {field->name, field, map->data, fields.size()};
        const PDFField* previous = (const PDFField*)hashmap_get(seen, &entry);
        if (previous) {
            // map spreads keep the first key position and the last value (S2.3.1).
            entry.index = previous->index;
            fields[entry.index] = entry;
        } else {
            if (!fields.append(entry)) {
                pdf_fail(builder, "PDF dictionary allocation failed", ERR_OUT_OF_MEMORY);
                return;
            }
            hashmap_set(seen, &entry);
            if (hashmap_oom(seen)) {
                pdf_fail(builder, "PDF dictionary allocation failed", ERR_OUT_OF_MEMORY);
                return;
            }
        }
    }
}

static void pdf_object(PDFBuilder* builder, const ItemReader& reader, int depth);

static void pdf_dictionary(PDFBuilder* builder, Map* map, int depth) {
    lam::ArrayList<PDFField> fields(MEM_CAT_FORMAT, 0);
    HashMap* seen = hashmap_new(sizeof(PDFField), 0, 0, 0,
        pdf_field_hash, pdf_field_compare, NULL, NULL);
    if (!seen) { pdf_fail(builder, "PDF dictionary allocation failed", ERR_OUT_OF_MEMORY); return; }
    pdf_collect_fields(builder, map, seen, fields, depth);
    hashmap_free(seen);
    pdf_text(builder, "<<");
    for (size_t i = 0; i < fields.size() && !builder->failure; i++) {
        const PDFField& entry = fields[i];
        pdf_text(builder, " ");
        pdf_name(builder, entry.name->str, entry.name->length);
        pdf_text(builder, " ");
        pdf_object(builder, ItemReader(map_shape_field_to_item(entry.data, entry.field).to_const()), depth + 1);
    }
    pdf_text(builder, " >>");
}

static void pdf_object(PDFBuilder* builder, const ItemReader& reader, int depth) {
    if (builder->failure) return;
    if (depth >= PDF_MAX_NESTING) { pdf_fail(builder, "PDF object nesting limit exceeded"); return; }
    Item item = reader.item();
    TypeId type = reader.getType();
    if (reader.isArray()) {
        ArrayReader array = reader.asArray();
        pdf_text(builder, "[");
        for (int64_t i = 0; i < array.length() && !builder->failure; i++) {
            if (i) pdf_text(builder, " ");
            pdf_object(builder, array.get(i), depth + 1);
        }
        pdf_text(builder, "]");
    } else if (type == LMD_TYPE_MAP) pdf_dictionary(builder, item.map, depth);
    else if (type == LMD_TYPE_NULL) pdf_text(builder, "null");
    else if (type == LMD_TYPE_BOOL) pdf_text(builder, item.bool_val ? "true" : "false");
    else if (type == LMD_TYPE_STRING) pdf_string(builder, item.get_safe_string());
    else if (type == LMD_TYPE_BINARY) {
        Binary* binary = item.get_safe_binary();
        pdf_text(builder, "<");
        pdf_hex(builder, binary_data(binary), binary_length(binary));
        pdf_text(builder, ">");
    } else if (type == LMD_TYPE_SYMBOL && symbol_is_lambda_name(item.get_safe_symbol())) {
        Symbol* symbol = item.get_safe_symbol();
        pdf_name(builder, symbol->chars, symbol->len);
    } else if (IS_NUMERIC_ID(type) && type != LMD_TYPE_COMPLEX) pdf_number(builder, item);
    else pdf_fail(builder, "Unsupported PDF object value");
}

static void pdf_builder_append(MarkOutputBuilder* base, Item item) {
    PDFBuilder* builder = static_cast<PDFBuilder*>(base);
    if (builder->failure || builder->finished) return;
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_NULL || item.item == ITEM_NULL_SPREADABLE) return;

    // only content lists spread; arrays in dictionary slots stay structured.
    if ((type == LMD_TYPE_ARRAY || type == LMD_TYPE_ARRAY_NUM) &&
            item.container->is_spreadable) {
        int64_t length = type == LMD_TYPE_ARRAY ? item.array->length : item.array_num->length;
        for (int64_t i = 0; i < length && !builder->failure; i++) {
            Item value = type == LMD_TYPE_ARRAY ? array_item_read(item.array, i)
                : array_num_read_borrowed_item(item.array_num, i);
            pdf_builder_append(base, value);
        }
        return;
    }

    if (type == LMD_TYPE_MAP) {
        pdf_text(builder, " ");
        pdf_object(builder, ItemReader(item.to_const()), 0);
        pdf_text(builder, " ");
        return;
    }

    const char* data = NULL;
    size_t size = 0;
    if (type == LMD_TYPE_STRING) {
        String* text = item.get_safe_string();
        if (text) { data = text->chars; size = text->len; }
    } else if (type == LMD_TYPE_BINARY) {
        Binary* binary = item.get_safe_binary();
        if (binary) { data = (const char*)binary_data(binary); size = binary_length(binary); }
    } else {
        pdf_fail(builder, "PDF file content requires strings, binaries, maps, null, or lists of them");
        return;
    }
    if (!size) return;

    // the append is length-delimited, including embedded NUL, and cannot GC.
    if (!data) { pdf_fail(builder, "Invalid PDF byte contribution"); return; }
    pdf_write(builder, data, size);
}

static bool pdf_builder_finish(MarkOutputBuilder* base) {
    PDFBuilder* builder = static_cast<PDFBuilder*>(base);
    builder->finished = true;
    if (builder->failure) {
        // No partial byte buffer is published after a rejected contribution.
        strbuf_reset(builder->buffer);
        set_runtime_error(builder->failure, "%s", builder->failure_message);
        return false;
    }
    return true;
}

static bool pdf_builder_bytes(const MarkOutputBuilder* base, const char** data, size_t* size) {
    const PDFBuilder* builder = static_cast<const PDFBuilder*>(base);
    if (!builder->finished || builder->failure) return false;
    *data = builder->buffer->str;
    *size = builder->buffer->length;
    return true;
}

static void pdf_builder_destroy(MarkOutputBuilder* base) {
    PDFBuilder* builder = static_cast<PDFBuilder*>(base);
    if (builder->buffer) strbuf_free(builder->buffer);
    mem_free(builder);
}

static const MarkOutputBuilderOps pdf_builder_ops = {
    pdf_builder_append, pdf_builder_finish, pdf_builder_bytes, pdf_builder_destroy
};

MarkOutputBuilder* pdf_builder_create() {
    PDFBuilder* builder = (PDFBuilder*)mem_calloc(1, sizeof(PDFBuilder), MEM_CAT_FORMAT);
    if (!builder) return NULL;
    builder->ops = &pdf_builder_ops;
    builder->buffer = strbuf_new();
    if (!builder->buffer) { mem_free(builder); return NULL; }
    return builder;
}

void elmt_content_begin(Element* element) {
    if (!element || !is_file_element_type((TypeElmt*)element->type) ||
            !element->is_heap || container_is_virtual_list(element)) return;
    Item format = {.item = element->get_attr("format").item};
    TypeId type = get_type_id(format);
    if (!is_text_type_id(type)) return;
    const char* chars = format.get_chars();
    size_t length = type == LMD_TYPE_STRING ? format.get_safe_string()->len
        : format.get_safe_symbol()->len;
    if (length != 3 || memcmp(chars, "pdf", 3) != 0) return;

    // Candidate <file> allocations reserve the extension before attributes
    // run; only the evaluated selector activates its builder.
    VirtualOutputElement* output = static_cast<VirtualOutputElement*>(element);
    output->builder = pdf_builder_create();
    element->is_virtual = 1;
    if (!output->builder) set_runtime_error(ERR_OUT_OF_MEMORY, "PDF builder allocation failed");
}
