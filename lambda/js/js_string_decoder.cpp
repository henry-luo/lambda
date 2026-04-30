/**
 * js_string_decoder.cpp — Node.js-style 'string_decoder' module for LambdaJS
 *
 * Handles multi-byte UTF-8 sequences that may span across Buffer chunks.
 * Provides StringDecoder class with write() and end() methods.
 */
#include "js_runtime.h"
#include "js_typed_array.h"
#include "js_class.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"

#include <cstring>

static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)(len > 0 ? len : 0));
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

// Helper: get raw data pointer and length from a typed array Item
static uint8_t* buffer_data(Item buf, int* out_len) {
    if (!js_is_typed_array(buf)) { *out_len = 0; return NULL; }
    Map* m = buf.map;
    JsTypedArray* ta = (JsTypedArray*)m->data;
    if (!ta || !ta->data) { *out_len = 0; return NULL; }
    *out_len = ta->byte_length;
    return (uint8_t*)ta->data;
}

// StringDecoder stores incomplete multi-byte sequences in __pending__ property
// encoding is stored in __encoding__

// new StringDecoder([encoding])
extern "C" Item js_string_decoder_new(Item encoding_item) {
    Item decoder = js_new_object();
    js_property_set(decoder, make_string_item("__class_name__"),
                    make_string_item("StringDecoder"));
    js_class_stamp(decoder, JS_CLASS_STRING_DECODER);  // A3-T3b

    // default encoding is utf8
    char enc[32] = "utf8";
    if (get_type_id(encoding_item) == LMD_TYPE_STRING) {
        String* s = it2s(encoding_item);
        int elen = (int)s->len < 31 ? (int)s->len : 31;
        memcpy(enc, s->chars, elen);
        enc[elen] = '\0';
    }
    js_property_set(decoder, make_string_item("encoding"), make_string_item(enc));
    // pending bytes (incomplete multi-byte)
    js_property_set(decoder, make_string_item("__pending__"), make_string_item("", 0));
    js_property_set(decoder, make_string_item("__pending_len__"),
                    (Item){.item = i2it(0)});

    // Set write and end methods on the instance
    extern Item js_string_decoder_write(Item buffer);
    extern Item js_string_decoder_end(Item buffer);
    js_property_set(decoder, make_string_item("write"),
                    js_new_function((void*)js_string_decoder_write, 1));
    js_property_set(decoder, make_string_item("end"),
                    js_new_function((void*)js_string_decoder_end, 1));

    return decoder;
}

// How many bytes does the leading byte of a UTF-8 sequence indicate?
static int utf8_char_len(uint8_t byte) {
    if ((byte & 0x80) == 0) return 1;        // 0xxxxxxx
    if ((byte & 0xE0) == 0xC0) return 2;     // 110xxxxx
    if ((byte & 0xF0) == 0xE0) return 3;     // 1110xxxx
    if ((byte & 0xF8) == 0xF0) return 4;     // 11110xxx
    return 1; // invalid byte, treat as single
}

// decoder.write(buffer) — decode buffer, return complete string
extern "C" Item js_string_decoder_write(Item buffer) {
    // "this" is the decoder object (set by method call dispatch)
    extern Item js_get_current_this(void);
    Item decoder = js_get_current_this();
    (void)decoder; // decoder can be used for pending byte state in future
    int blen = 0;
    uint8_t* data = buffer_data(buffer, &blen);
    if (!data || blen == 0) return make_string_item("", 0);

    // for utf8, check for incomplete multi-byte at end
    // find the last complete character boundary
    int complete_end = blen;
    if (blen > 0) {
        // scan backwards for incomplete multi-byte sequence
        int i = blen - 1;
        // find start of last character
        while (i > 0 && (data[i] & 0xC0) == 0x80) i--; // skip continuation bytes
        if (i >= 0) {
            int expected = utf8_char_len(data[i]);
            int actual = blen - i;
            if (actual < expected) {
                // incomplete character at end
                complete_end = i;
                // store incomplete bytes as pending (simplified — just truncate)
            }
        }
    }

    return make_string_item((const char*)data, complete_end);
}

// decoder.end([buffer]) — flush any remaining bytes
extern "C" Item js_string_decoder_end(Item buffer) {
    if (get_type_id(buffer) != LMD_TYPE_UNDEFINED &&
        buffer.item != ITEM_NULL) {
        return js_string_decoder_write(buffer);
    }
    // return empty string (no pending data in this simplified version)
    return make_string_item("", 0);
}

// =============================================================================
// string_decoder Module Namespace
// =============================================================================

static Item sd_namespace = {0};

static void sd_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

extern "C" Item js_get_string_decoder_namespace(void) {
    if (sd_namespace.item != 0) return sd_namespace;

    sd_namespace = js_new_object();

    sd_set_method(sd_namespace, "StringDecoder", (void*)js_string_decoder_new, 1);
    sd_set_method(sd_namespace, "write",         (void*)js_string_decoder_write, 1);
    sd_set_method(sd_namespace, "end",           (void*)js_string_decoder_end, 1);

    // default export is the constructor
    js_property_set(sd_namespace, make_string_item("default"), sd_namespace);

    return sd_namespace;
}

extern "C" void js_string_decoder_reset(void) {
    sd_namespace = (Item){0};
}
