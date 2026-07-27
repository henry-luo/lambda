/**
 * js_querystring.cpp — Node.js-style 'querystring' module for LambdaJS
 *
 * Provides parse, stringify, escape, unescape.
 * Registered by node-core through its Jube namespace descriptor.
 */
#include "node_querystring.hpp"
#include "../../jube/jube_registry.h"
#include "../../../lib/url.h"
#include "../../../lib/mem.h"
#include "../../../lib/hex.h"

#include <cstring>
#include <math.h>

static const JubeHostAPI* node_querystring_host = NULL;
struct NodeQuerystringSessionState { void* session; bool rooted; Item namespace_cache; };
static NodeQuerystringSessionState* node_querystring_state(void) { return (NodeQuerystringSessionState*)jube_node_current_module_state(JUBE_NODE_MODULE_STATE_QUERYSTRING); }
#define node_querystring_session (node_querystring_state()->session)
#define node_querystring_rooted (node_querystring_state()->rooted)
#define qs_namespace (node_querystring_state()->namespace_cache)

static int node_querystring_kind(Item value) {
    return node_querystring_host && node_querystring_host->value &&
            node_querystring_host->value->kind ?
        node_querystring_host->value->kind(value) : JUBE_VALUE_OTHER;
}

static Item node_querystring_string(const char* text, int length) {
    if (!node_querystring_host || !node_querystring_host->value ||
            !node_querystring_host->value->string_from_utf8_n || !text || length < 0) {
        return ItemNull;
    }
    return node_querystring_host->value->string_from_utf8_n(text, (size_t)length);
}

static Item node_querystring_string(const char* text) {
    return node_querystring_string(text, text ? (int)strlen(text) : 0);
}

static char* node_querystring_string_bytes(Item value, size_t* out_length) {
    if (out_length) *out_length = 0;
    if (node_querystring_kind(value) != JUBE_VALUE_STRING || !node_querystring_host ||
            !node_querystring_host->value || !node_querystring_host->value->string_length ||
            !node_querystring_host->value->string_copy) return NULL;
    size_t length = node_querystring_host->value->string_length(value);
    char* bytes = (char*)mem_alloc(length + 1, MEM_CAT_TEMP);
    if (!bytes || !node_querystring_host->value->string_copy(value, bytes, length + 1, NULL)) {
        if (bytes) mem_free(bytes);
        return NULL;
    }
    if (out_length) *out_length = length;
    return bytes;
}

static Item node_querystring_throw_type_error(const char* message) {
    return node_querystring_host && node_querystring_host->script &&
            node_querystring_host->script->throw_type_error_code ?
        node_querystring_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", message) : ItemNull;
}

static Item node_querystring_throw_uri_error(const char* message) {
    return node_querystring_host && node_querystring_host->script &&
            node_querystring_host->script->throw_uri_error_code ?
        node_querystring_host->script->throw_uri_error_code("ERR_INVALID_URI", message) : ItemNull;
}

static Item node_querystring_undefined(void) {
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item node_querystring_root_value(const uint64_t* slot) {
    return (Item){.item = slot ? *slot : 0};
}

static char* node_querystring_decode_or_copy(const char* text, size_t length,
                                             size_t* out_length);

static int node_querystring_hex_value(unsigned char byte) {
    if (byte >= '0' && byte <= '9') return byte - '0';
    if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
    if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
    return -1;
}

static char* node_querystring_percent_decode(const char* text, size_t length,
                                             bool decode_spaces, size_t* out_length) {
    char* decoded = (char*)mem_alloc(length + 1, MEM_CAT_TEMP);
    if (!decoded) return NULL;
    size_t write_index = 0;
    for (size_t read_index = 0; read_index < length; read_index++) {
        unsigned char byte = (unsigned char)text[read_index];
        if (byte == '+' && decode_spaces) {
            decoded[write_index++] = ' ';
            continue;
        }
        if (byte == '%' && read_index + 2 < length) {
            int high = node_querystring_hex_value((unsigned char)text[read_index + 1]);
            int low = node_querystring_hex_value((unsigned char)text[read_index + 2]);
            if (high >= 0 && low >= 0) {
                decoded[write_index++] = (char)((high << 4) | low);
                read_index += 2;
                continue;
            }
        }
        decoded[write_index++] = (char)byte;
    }
    decoded[write_index] = '\0';
    if (out_length) *out_length = write_index;
    return decoded;
}

#define get_type_id(value) node_querystring_kind(value)
#define LMD_TYPE_UNDEFINED JUBE_VALUE_UNDEFINED
#define LMD_TYPE_NULL JUBE_VALUE_NULL
#define LMD_TYPE_BOOL JUBE_VALUE_BOOLEAN
#define LMD_TYPE_INT JUBE_VALUE_NUMBER
#define LMD_TYPE_INT64 JUBE_VALUE_NUMBER
#define LMD_TYPE_FLOAT JUBE_VALUE_NUMBER
#define LMD_TYPE_STRING JUBE_VALUE_STRING
#define LMD_TYPE_ARRAY JUBE_VALUE_ARRAY
#define LMD_TYPE_MAP JUBE_VALUE_OBJECT
#define LMD_TYPE_OBJECT JUBE_VALUE_OBJECT
#define LMD_TYPE_VMAP JUBE_VALUE_OBJECT
#define LMD_TYPE_FUNC JUBE_VALUE_FUNCTION
#define LMD_TYPE_SYMBOL JUBE_VALUE_SYMBOL
#define make_string_item node_querystring_string
#define js_property_get(ARG_OBJECT, ARG_KEY) node_querystring_host->value->property_get(ARG_OBJECT, ARG_KEY)
#define js_property_set(ARG_OBJECT, ARG_KEY, ARG_VALUE) node_querystring_host->value->property_set_own(ARG_OBJECT, ARG_KEY, ARG_VALUE)
#define js_array_new(ARG_CAPACITY) node_querystring_host->value->array_new(ARG_CAPACITY)
#define js_array_push(ARG_ARRAY, ARG_VALUE) node_querystring_host->value->array_push(ARG_ARRAY, ARG_VALUE)
#define js_array_length(ARG_ARRAY) node_querystring_host->value->array_length(ARG_ARRAY)
#define js_array_get_int(ARG_ARRAY, ARG_INDEX) node_querystring_host->value->array_get(ARG_ARRAY, ARG_INDEX)
#define js_new_object() node_querystring_host->value->new_object()
#define js_new_function(ARG_FUNCTION, ARG_COUNT) node_querystring_host->script->new_function(ARG_FUNCTION, ARG_COUNT)
#define js_object_keys(ARG_OBJECT) node_querystring_host->script->object_keys(ARG_OBJECT)
#define js_call_function(ARG_FUNCTION, ARG_THIS, ARG_ARGS, ARG_COUNT) node_querystring_host->script->call_function(ARG_FUNCTION, ARG_THIS, ARG_ARGS, ARG_COUNT)
#define js_check_exception() node_querystring_host->script->check_exception()
#define js_to_string(ARG_VALUE) node_querystring_host->script->to_string(ARG_VALUE)
#define js_throw_type_error(ARG_MESSAGE) node_querystring_throw_type_error(ARG_MESSAGE)
#define make_js_undefined() node_querystring_undefined()

// ─── querystring.escape(str) ─────────────────────────────────────────────────
// Percent-encodes a string matching Node.js querystring.escape() semantics.
// Implements Node's encodeStr() logic which handles surrogate pairs by blindly
// combining high surrogates with the next code unit (not encodeURIComponent).
extern "C" Item js_qs_escape(Item str_item) {
    if (get_type_id(str_item) != LMD_TYPE_STRING) {
        if (get_type_id(str_item) == LMD_TYPE_SYMBOL) {
            // Symbol + '' throws TypeError in JS
            return js_throw_type_error("Cannot convert a Symbol value to a string");
        }
        // Host coercion preserves JS ToPrimitive ordering without exposing
        // object/prototype storage to this native module.
        str_item = js_to_string(str_item);
        if (get_type_id(str_item) != LMD_TYPE_STRING) return make_string_item("");
    }
    size_t source_length = 0;
    char* source = node_querystring_string_bytes(str_item, &source_length);
    if (!source) return make_string_item("");
    if (source_length == 0) {
        mem_free(source);
        return str_item;
    }

    // Node's noEscape table: unreserved chars that pass through unencoded
    // A-Z a-z 0-9 - _ . ~ ! ' ( ) *
    // Worst case: every code point becomes 4 bytes → 12 percent-encoded chars
    size_t max_out = source_length * 12 + 1;
    char* out = (char*)mem_alloc(max_out, MEM_CAT_TEMP);
    if (!out) {
        mem_free(source);
        return make_string_item("");
    }
    size_t j = 0;

    // Walk the UTF-8 bytes, decoding code points
    const unsigned char* p = (const unsigned char*)source;
    const unsigned char* end = p + source_length;

    while (p < end) {
        unsigned int c;
        int cp_bytes;
        unsigned char b0 = *p;
        if (b0 < 0x80) {
            c = b0; cp_bytes = 1;
        } else if ((b0 & 0xE0) == 0xC0 && p + 1 < end) {
            c = ((b0 & 0x1F) << 6) | (p[1] & 0x3F);
            cp_bytes = 2;
        } else if ((b0 & 0xF0) == 0xE0 && p + 2 < end) {
            c = ((b0 & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            cp_bytes = 3;
        } else if ((b0 & 0xF8) == 0xF0 && p + 3 < end) {
            c = ((b0 & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
            cp_bytes = 4;
        } else {
            c = b0; cp_bytes = 1; // invalid byte, encode as-is
        }
        p += cp_bytes;

        // ASCII fast path
        if (c < 0x80) {
            unsigned char cc = (unsigned char)c;
            if ((cc >= 'A' && cc <= 'Z') || (cc >= 'a' && cc <= 'z') ||
                (cc >= '0' && cc <= '9') || cc == '-' || cc == '_' ||
                cc == '.' || cc == '~' || cc == '!' || cc == '\'' ||
                cc == '(' || cc == ')' || cc == '*') {
                out[j++] = (char)cc;
            } else {
                out[j++] = '%'; out[j++] = hex_encode_nibble_upper(cc >> 4); out[j++] = hex_encode_nibble_upper(cc & 0x0F);
            }
            continue;
        }

        // 2-byte: 0x80 - 0x7FF
        if (c < 0x800) {
            unsigned char b1 = 0xC0 | (c >> 6);
            unsigned char b2 = 0x80 | (c & 0x3F);
            out[j++] = '%'; out[j++] = hex_encode_nibble_upper(b1 >> 4); out[j++] = hex_encode_nibble_upper(b1 & 0x0F);
            out[j++] = '%'; out[j++] = hex_encode_nibble_upper(b2 >> 4); out[j++] = hex_encode_nibble_upper(b2 & 0x0F);
            continue;
        }

        // High surrogate: 0xD800 - 0xDBFF
        if (c >= 0xD800 && c <= 0xDBFF) {
            // Need next code unit - decode next code point from UTF-8
            if (p >= end) {
                // Lone surrogate at end → throw URIError
                mem_free(out);
                mem_free(source);
                node_querystring_throw_uri_error("URI malformed");
                return make_js_undefined();
            }
            // Decode next code point (treated as c2 code unit)
            unsigned int c2;
            unsigned char nb0 = *p;
            if (nb0 < 0x80) {
                c2 = nb0; p += 1;
            } else if ((nb0 & 0xE0) == 0xC0 && p + 1 < end) {
                c2 = ((nb0 & 0x1F) << 6) | (p[1] & 0x3F); p += 2;
            } else if ((nb0 & 0xF0) == 0xE0 && p + 2 < end) {
                c2 = ((nb0 & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3;
            } else if ((nb0 & 0xF8) == 0xF0 && p + 3 < end) {
                c2 = ((nb0 & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); p += 4;
            } else {
                c2 = nb0; p += 1;
            }
            // Node.js behavior: blindly combine high surrogate with next code unit
            unsigned int cp = 0x10000 + (((c & 0x3FF) << 10) | (c2 & 0x3FF));
            // Encode as 4-byte UTF-8
            unsigned char b1 = 0xF0 | (cp >> 18);
            unsigned char b2 = 0x80 | ((cp >> 12) & 0x3F);
            unsigned char b3 = 0x80 | ((cp >> 6) & 0x3F);
            unsigned char b4 = 0x80 | (cp & 0x3F);
            out[j++] = '%'; out[j++] = hex_encode_nibble_upper(b1 >> 4); out[j++] = hex_encode_nibble_upper(b1 & 0x0F);
            out[j++] = '%'; out[j++] = hex_encode_nibble_upper(b2 >> 4); out[j++] = hex_encode_nibble_upper(b2 & 0x0F);
            out[j++] = '%'; out[j++] = hex_encode_nibble_upper(b3 >> 4); out[j++] = hex_encode_nibble_upper(b3 & 0x0F);
            out[j++] = '%'; out[j++] = hex_encode_nibble_upper(b4 >> 4); out[j++] = hex_encode_nibble_upper(b4 & 0x0F);
            continue;
        }

        if (c >= 0xDC00 && c <= 0xDFFF) {
            // A lone low surrogate is not valid scalar input for Node's
            // querystring encoder and must surface ERR_INVALID_URI.
            mem_free(out);
            mem_free(source);
            node_querystring_throw_uri_error("URI malformed");
            return make_js_undefined();
        }

        // 3-byte: 0x800 - 0xFFFF (non-surrogate)
        if (c < 0x10000) {
            unsigned char b1 = 0xE0 | (c >> 12);
            unsigned char b2 = 0x80 | ((c >> 6) & 0x3F);
            unsigned char b3 = 0x80 | (c & 0x3F);
            out[j++] = '%'; out[j++] = hex_encode_nibble_upper(b1 >> 4); out[j++] = hex_encode_nibble_upper(b1 & 0x0F);
            out[j++] = '%'; out[j++] = hex_encode_nibble_upper(b2 >> 4); out[j++] = hex_encode_nibble_upper(b2 & 0x0F);
            out[j++] = '%'; out[j++] = hex_encode_nibble_upper(b3 >> 4); out[j++] = hex_encode_nibble_upper(b3 & 0x0F);
            continue;
        }

        // 4-byte: 0x10000+ (supplementary)
        {
            unsigned char b1 = 0xF0 | (c >> 18);
            unsigned char b2 = 0x80 | ((c >> 12) & 0x3F);
            unsigned char b3 = 0x80 | ((c >> 6) & 0x3F);
            unsigned char b4 = 0x80 | (c & 0x3F);
            out[j++] = '%'; out[j++] = hex_encode_nibble_upper(b1 >> 4); out[j++] = hex_encode_nibble_upper(b1 & 0x0F);
            out[j++] = '%'; out[j++] = hex_encode_nibble_upper(b2 >> 4); out[j++] = hex_encode_nibble_upper(b2 & 0x0F);
            out[j++] = '%'; out[j++] = hex_encode_nibble_upper(b3 >> 4); out[j++] = hex_encode_nibble_upper(b3 & 0x0F);
            out[j++] = '%'; out[j++] = hex_encode_nibble_upper(b4 >> 4); out[j++] = hex_encode_nibble_upper(b4 & 0x0F);
        }
    }
    out[j] = '\0';
    Item result = make_string_item(out, (int)j);
    mem_free(out);
    mem_free(source);
    return result;
}

// ─── querystring.unescape(str) ───────────────────────────────────────────────
// Decodes a percent-encoded string (same as decodeURIComponent)
extern "C" Item js_qs_unescape(Item str_item) {
    if (get_type_id(str_item) != LMD_TYPE_STRING) return make_string_item("");
    size_t source_length = 0;
    char* source = node_querystring_string_bytes(str_item, &source_length);
    if (!source) return make_string_item("");
    size_t decoded_len = 0;
    char* decoded = node_querystring_percent_decode(source, source_length, false, &decoded_len);
    mem_free(source);
    if (!decoded) return make_string_item("");
    Item result = make_string_item(decoded, (int)decoded_len);
    mem_free(decoded);
    return result;
}

// ─── querystring.parse(str, sep, eq) ─────────────────────────────────────────
// Parses a query string into an object. Default sep='&', eq='='
// In query strings, '+' is decoded as space (before percent-decoding)
static void qs_plus_to_space(char* s) {
    for (; *s; s++) {
        if (*s == '+') *s = ' ';
    }
}

static char* node_querystring_decode_or_copy(const char* text, size_t length,
                                             size_t* out_length) {
    char* decoded = url_decode_component(text, length, out_length);
    if (decoded) return decoded;
    char* copy = (char*)mem_alloc(length + 1, MEM_CAT_TEMP);
    if (!copy) return NULL;
    memcpy(copy, text, length);
    copy[length] = '\0';
    if (out_length) *out_length = length;
    return copy;
}

static Item node_querystring_decode_component(Item decoder, const char* text, size_t length,
                                              uint64_t* temporary_root) {
    if (get_type_id(decoder) == LMD_TYPE_FUNC) {
        Item encoded = make_string_item(text, (int)length);
        *temporary_root = encoded.item;
        Item args[1] = { encoded };
        Item decoded = js_call_function(decoder, node_querystring_undefined(), args, 1);
        *temporary_root = decoded.item;
        // Node falls back to the internal decoder when a custom decoder
        // throws, keeping malformed query strings parseable.
        if (!node_querystring_host->script->check_exception() &&
                get_type_id(decoded) == LMD_TYPE_STRING) return decoded;
        if (node_querystring_host->script->check_exception()) {
            node_querystring_host->script->clear_exception();
        }
    }

    size_t decoded_length = 0;
    char* decoded = node_querystring_decode_or_copy(text, length, &decoded_length);
    if (!decoded) return ItemNull;
    Item result = make_string_item(decoded, (int)decoded_length);
    mem_free(decoded);
    *temporary_root = result.item;
    return result;
}

// Buffer decoding must retain malformed percent sequences byte-for-byte; the
// string decoder intentionally rejects those sequences to preserve URI rules.
extern "C" Item node_querystring_unescape_buffer(Item str_item, Item decode_spaces_item) {
    JubeRootFrame frame = {};
    if (!node_querystring_host || !node_querystring_host->node ||
            !node_querystring_host->node->roots ||
            !node_querystring_host->node->roots->root_frame_begin(&frame, 2)) return ItemNull;
    uint64_t* string_root = node_querystring_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* spaces_root = node_querystring_host->node->roots->root_frame_take_slot(&frame);
    if (!string_root || !spaces_root) {
        node_querystring_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *string_root = str_item.item;
    *spaces_root = decode_spaces_item.item;
    if (get_type_id(node_querystring_root_value(string_root)) != LMD_TYPE_STRING) {
        str_item = js_to_string(node_querystring_root_value(string_root));
        *string_root = str_item.item;
    }
    if (get_type_id(node_querystring_root_value(string_root)) != LMD_TYPE_STRING) {
        node_querystring_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }

    size_t source_length = 0;
    char* source = node_querystring_string_bytes(node_querystring_root_value(string_root), &source_length);
    if (!source) {
        node_querystring_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    uint8_t* output = (uint8_t*)mem_alloc(source_length ? source_length : 1, MEM_CAT_TEMP);
    if (!output) {
        mem_free(source);
        node_querystring_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }

    bool decode_spaces = node_querystring_host->script->is_truthy(
        node_querystring_root_value(spaces_root));
    size_t write_index = 0;
    for (size_t read_index = 0; read_index < source_length; read_index++) {
        unsigned char byte = (unsigned char)source[read_index];
        if (byte == '+' && decode_spaces) {
            output[write_index++] = ' ';
            continue;
        }
        if (byte == '%' && read_index + 2 < source_length) {
            int high = node_querystring_hex_value((unsigned char)source[read_index + 1]);
            int low = node_querystring_hex_value((unsigned char)source[read_index + 2]);
            if (high >= 0 && low >= 0) {
                output[write_index++] = (uint8_t)((high << 4) | low);
                read_index += 2;
                continue;
            }
        }
        output[write_index++] = byte;
    }

    Item buffer = node_querystring_host->node->binary->buffer_from_bytes(output,
                                                                            (int)write_index);
    mem_free(output);
    mem_free(source);
    node_querystring_host->node->roots->root_frame_end(&frame);
    return buffer;
}

static bool node_querystring_coerce_delimiter(Item value, char* buffer, int buffer_size,
                                               const char** out_text, int* out_length) {
    if (!buffer || buffer_size <= 0 || !out_text || !out_length) return false;
    JubeRootFrame frame = {};
    if (!node_querystring_host || !node_querystring_host->node ||
            !node_querystring_host->node->roots ||
            !node_querystring_host->node->roots->root_frame_begin(&frame, 1)) return false;
    uint64_t* value_root = node_querystring_host->node->roots->root_frame_take_slot(&frame);
    if (!value_root) {
        node_querystring_host->node->roots->root_frame_end(&frame);
        return false;
    }
    *value_root = value.item;
    int kind = get_type_id(value);
    if (kind == LMD_TYPE_UNDEFINED || kind == LMD_TYPE_NULL) {
        node_querystring_host->node->roots->root_frame_end(&frame);
        return false;
    }
    Item text = kind == LMD_TYPE_STRING ? node_querystring_root_value(value_root) :
        js_to_string(node_querystring_root_value(value_root));
    if (get_type_id(text) != LMD_TYPE_STRING) {
        node_querystring_host->node->roots->root_frame_end(&frame);
        return false;
    }
    size_t length = 0;
    char* bytes = node_querystring_string_bytes(text, &length);
    if (!bytes) {
        node_querystring_host->node->roots->root_frame_end(&frame);
        return false;
    }
    if (length >= (size_t)buffer_size) length = (size_t)buffer_size - 1;
    memcpy(buffer, bytes, length);
    buffer[length] = '\0';
    mem_free(bytes);
    *out_text = buffer;
    *out_length = (int)length;
    node_querystring_host->node->roots->root_frame_end(&frame);
    return true;
}

// parse() has a fourth options argument; maxKeys must be applied before scanning all pairs.
static int64_t qs_parse_max_keys(Item options_item) {
    int64_t max_keys = 1000;
    int options_type = get_type_id(options_item);
    if (options_type != LMD_TYPE_MAP) {
        return max_keys;
    }
    Item value = js_property_get(options_item, make_string_item("maxKeys"));
    if (get_type_id(value) == LMD_TYPE_INT && node_querystring_host &&
            node_querystring_host->script && node_querystring_host->script->get_number) {
        double n = node_querystring_host->script->get_number(value);
        if (!isfinite(n)) return -1;
        return n > 0 ? (int64_t)n : -1;
    }
    return max_keys;
}

extern "C" Item js_qs_parse(Item str_item, Item sep_item, Item eq_item, Item options_item) {
    JubeRootFrame roots = {};
    if (!node_querystring_host->node->roots->root_frame_begin(&roots, 7)) return ItemNull;
    uint64_t* object_root = node_querystring_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* key_root = node_querystring_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* existing_root = node_querystring_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* value_root = node_querystring_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* array_root = node_querystring_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* decoder_root = node_querystring_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* temporary_root = node_querystring_host->node->roots->root_frame_take_slot(&roots);
    if (!object_root || !key_root || !existing_root || !value_root || !array_root ||
            !decoder_root || !temporary_root) {
        node_querystring_host->node->roots->root_frame_end(&roots);
        return ItemNull;
    }
    Item obj = node_querystring_host->script->object_create(ItemNull);
    *object_root = obj.item;
    Item decoder = node_querystring_undefined();
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        decoder = js_property_get(options_item, make_string_item("decodeURIComponent"));
    }
    if (get_type_id(decoder) != LMD_TYPE_FUNC && qs_namespace.item != 0) {
        // querystring.parse observes replacement of the public unescape hook;
        // caching the original native function would make the namespace lie.
        decoder = js_property_get(qs_namespace, make_string_item("unescape"));
    }
    *decoder_root = decoder.item;
    if (get_type_id(str_item) != LMD_TYPE_STRING) {
        node_querystring_host->node->roots->root_frame_end(&roots);
        return obj;
    }

    size_t source_length = 0;
    char* source = node_querystring_string_bytes(str_item, &source_length);
    if (!source || source_length == 0) {
        if (source) mem_free(source);
        node_querystring_host->node->roots->root_frame_end(&roots);
        return obj;
    }

    // determine separator and equals strings (support multi-char)
    const char* sep = "&";
    int sep_len = 1;
    const char* eq = "=";
    int eq_len = 1;
    char sep_buf[128] = {};
    char eq_buf[128] = {};
    node_querystring_coerce_delimiter(sep_item, sep_buf, sizeof(sep_buf), &sep, &sep_len);
    node_querystring_coerce_delimiter(eq_item, eq_buf, sizeof(eq_buf), &eq, &eq_len);

    // copy input to mutable buffer
    int len = (int)source_length;
    char* input = (char*)mem_alloc(len + 1, MEM_CAT_JS_RUNTIME);
    if (!input) {
        mem_free(source);
        node_querystring_host->node->roots->root_frame_end(&roots);
        return obj;
    }
    memcpy(input, source, len);
    mem_free(source);
    input[len] = '\0';

    // skip leading '?' if present
    char* p = input;
    if (*p == '?') p++;
    char* end = input + len;
    int64_t max_keys = qs_parse_max_keys(options_item);
    int64_t parsed_keys = 0;

    // helper lambda-like: process one key=value pair
    while (p < end) {
        if (max_keys >= 0 && parsed_keys >= max_keys) break;
        // Node's maxKeys budget counts input fields, including an empty field
        // before a separator; otherwise parse('&a', { maxKeys: 1 }) leaks a.
        parsed_keys++;
        // find next separator
        char* sep_pos = sep_len <= 0 ? NULL :
            ((sep_len == 1) ? strchr(p, sep[0]) : strstr(p, sep));
        int pair_len = sep_pos ? (int)(sep_pos - p) : (int)(end - p);

        // find equals within this pair
        char* pair_end = p + pair_len;
        char saved = *pair_end;
        *pair_end = '\0';
        char* eq_pos = eq_len == 0 ? p :
            ((eq_len == 1) ? strchr(p, eq[0]) : strstr(p, eq));
        *pair_end = saved;

        if (eq_pos && eq_pos < pair_end) {
            // key = value
            int key_raw_len = (int)(eq_pos - p);
            int val_raw_len = (int)(pair_end - eq_pos - eq_len);
            char key_buf[4096], val_buf[4096];
            if (key_raw_len >= (int)sizeof(key_buf)) key_raw_len = (int)sizeof(key_buf) - 1;
            if (val_raw_len >= (int)sizeof(val_buf)) val_raw_len = (int)sizeof(val_buf) - 1;
            memcpy(key_buf, p, key_raw_len); key_buf[key_raw_len] = '\0';
            memcpy(val_buf, eq_pos + eq_len, val_raw_len); val_buf[val_raw_len] = '\0';

            qs_plus_to_space(key_buf);
            qs_plus_to_space(val_buf);

            Item key = node_querystring_decode_component(decoder, key_buf, strlen(key_buf),
                                                         temporary_root);
            *key_root = key.item;
            Item decoded_value = node_querystring_decode_component(decoder, val_buf, strlen(val_buf),
                                                                   temporary_root);
            *value_root = decoded_value.item;
            if (get_type_id(key) == LMD_TYPE_STRING &&
                    get_type_id(decoded_value) == LMD_TYPE_STRING) {
                *key_root = key.item;
                Item existing = js_property_get(obj, key);
                *existing_root = existing.item;
                // Query values may use a zero payload, so presence must use
                // the host's own-property predicate rather than Item bits.
                if (node_querystring_host->value->property_has_own(obj, key)) {
                    if (node_querystring_host->value->is_array(existing)) {
                        js_array_push(existing, decoded_value);
                    } else {
                        Item arr = js_array_new(0);
                        *array_root = arr.item;
                        js_array_push(arr, existing);
                        js_array_push(arr, decoded_value);
                        js_property_set(obj, key, arr);
                    }
                } else {
                    js_property_set(obj, key, decoded_value);
                }
            }
        } else if (pair_len > 0) {
            // key with no value
            char key_buf[4096];
            if (pair_len >= (int)sizeof(key_buf)) pair_len = (int)sizeof(key_buf) - 1;
            memcpy(key_buf, p, pair_len); key_buf[pair_len] = '\0';
            qs_plus_to_space(key_buf);
            Item key = node_querystring_decode_component(decoder, key_buf, strlen(key_buf),
                                                         temporary_root);
            *key_root = key.item;
            if (get_type_id(key) == LMD_TYPE_STRING) {
                *key_root = key.item;
                Item value = make_string_item("");
                *value_root = value.item;
                Item existing = js_property_get(obj, key);
                *existing_root = existing.item;
                if (node_querystring_host->value->property_has_own(obj, key)) {
                    if (node_querystring_host->value->is_array(existing)) {
                        js_array_push(existing, value);
                    } else {
                        Item arr = js_array_new(0);
                        *array_root = arr.item;
                        js_array_push(arr, existing);
                        js_array_push(arr, value);
                        js_property_set(obj, key, arr);
                    }
                } else {
                    js_property_set(obj, key, value);
                }
            }
        }

        if (!sep_pos) break;
        p = sep_pos + sep_len;
    }

    mem_free(input);
    node_querystring_host->node->roots->root_frame_end(&roots);
    return obj;
}

// ─── querystring.stringify(obj, sep, eq) ─────────────────────────────────────
// Serializes an object into a query string. Default sep='&', eq='='
static char* node_querystring_encode_value(Item value, Item encoder) {
    JubeRootFrame frame = {};
    if (!node_querystring_host || !node_querystring_host->node ||
            !node_querystring_host->node->roots ||
            !node_querystring_host->node->roots->root_frame_begin(&frame, 3)) return NULL;
    uint64_t* value_root = node_querystring_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* encoder_root = node_querystring_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* encoded_root = node_querystring_host->node->roots->root_frame_take_slot(&frame);
    if (!value_root || !encoder_root || !encoded_root) {
        node_querystring_host->node->roots->root_frame_end(&frame);
        return NULL;
    }
    *value_root = value.item;
    *encoder_root = encoder.item;
    int kind = get_type_id(node_querystring_root_value(value_root));
    if (kind == LMD_TYPE_UNDEFINED || kind == LMD_TYPE_NULL ||
            kind == LMD_TYPE_MAP || kind == LMD_TYPE_FUNC) {
        // Node querystring serializes non-primitive field values as empty.
        char* empty = (char*)mem_alloc(1, MEM_CAT_TEMP);
        if (empty) empty[0] = '\0';
        node_querystring_host->node->roots->root_frame_end(&frame);
        return empty;
    }
    if (kind == LMD_TYPE_INT && node_querystring_host->script->get_number &&
            !isfinite(node_querystring_host->script->get_number(node_querystring_root_value(value_root)))) {
        char* empty = (char*)mem_alloc(1, MEM_CAT_TEMP);
        if (empty) empty[0] = '\0';
        node_querystring_host->node->roots->root_frame_end(&frame);
        return empty;
    }
    // ToString on an existing string is the identity. Avoid re-entering the
    // host coercion path here: under forced moving GC it can reallocate the
    // same string while this native encoder still owns the call frame.
    Item text = kind == LMD_TYPE_STRING ? node_querystring_root_value(value_root) :
        js_to_string(node_querystring_root_value(value_root));
    *value_root = text.item;
    if (get_type_id(node_querystring_root_value(value_root)) != LMD_TYPE_STRING) {
        node_querystring_host->node->roots->root_frame_end(&frame);
        return NULL;
    }
    Item argument = node_querystring_root_value(value_root);
    Item encoded_item = get_type_id(node_querystring_root_value(encoder_root)) == LMD_TYPE_FUNC ?
        js_call_function(node_querystring_root_value(encoder_root), node_querystring_undefined(),
            &argument, 1) : js_qs_escape(argument);
    *encoded_root = encoded_item.item;
    if (node_querystring_host->script->check_exception()) {
        node_querystring_host->node->roots->root_frame_end(&frame);
        return NULL;
    }
    char* result = node_querystring_string_bytes(node_querystring_root_value(encoded_root), NULL);
    node_querystring_host->node->roots->root_frame_end(&frame);
    return result;
}

static bool node_querystring_append(char** buffer, size_t* capacity, size_t* length,
                                    const char* text, size_t text_length) {
    if (!buffer || !*buffer || !capacity || !length || !text) return false;
    if (*length + text_length + 1 > *capacity) {
        size_t next_capacity = *capacity;
        while (*length + text_length + 1 > next_capacity) next_capacity *= 2;
        char* grown = (char*)mem_realloc(*buffer, next_capacity, MEM_CAT_TEMP);
        if (!grown) return false;
        *buffer = grown;
        *capacity = next_capacity;
    }
    memcpy(*buffer + *length, text, text_length);
    *length += text_length;
    (*buffer)[*length] = '\0';
    return true;
}

extern "C" Item js_qs_stringify(Item obj_item, Item sep_item, Item eq_item, Item options_item) {
    JubeRootFrame roots = {};
    if (!node_querystring_host || !node_querystring_host->node ||
            !node_querystring_host->node->roots ||
            !node_querystring_host->node->roots->root_frame_begin(&roots, 8)) return ItemNull;
    uint64_t* object_root = node_querystring_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* sep_root = node_querystring_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* eq_root = node_querystring_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* options_root = node_querystring_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* encoder_root = node_querystring_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* keys_root = node_querystring_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* key_root = node_querystring_host->node->roots->root_frame_take_slot(&roots);
    uint64_t* value_root = node_querystring_host->node->roots->root_frame_take_slot(&roots);
    if (!object_root || !sep_root || !eq_root || !options_root || !encoder_root ||
            !keys_root || !key_root || !value_root) {
        node_querystring_host->node->roots->root_frame_end(&roots);
        return ItemNull;
    }
    *object_root = obj_item.item;
    *sep_root = sep_item.item;
    *eq_root = eq_item.item;
    *options_root = options_item.item;
    if (obj_item.item == 0 ||
            (get_type_id(node_querystring_root_value(object_root)) != LMD_TYPE_MAP &&
             get_type_id(node_querystring_root_value(object_root)) != LMD_TYPE_ARRAY)) {
        node_querystring_host->node->roots->root_frame_end(&roots);
        return make_string_item("");
    }

    const char* sep = "&";
    int sep_len = 1;
    const char* eq = "=";
    int eq_len = 1;
    char sep_buf[128] = {};
    char eq_buf[128] = {};
    node_querystring_coerce_delimiter(node_querystring_root_value(sep_root),
        sep_buf, sizeof(sep_buf), &sep, &sep_len);
    node_querystring_coerce_delimiter(node_querystring_root_value(eq_root),
        eq_buf, sizeof(eq_buf), &eq, &eq_len);

    Item encoder = node_querystring_undefined();
    if (get_type_id(node_querystring_root_value(options_root)) == LMD_TYPE_MAP) {
        Item encoder_key = make_string_item("encodeURIComponent");
        *key_root = encoder_key.item;
        encoder = js_property_get(node_querystring_root_value(options_root),
            node_querystring_root_value(key_root));
    }
    *encoder_root = encoder.item;

    Item keys = js_object_keys(node_querystring_root_value(object_root));
    *keys_root = keys.item;
    int64_t key_count = js_array_length(node_querystring_root_value(keys_root));

    // build result string
    size_t result_capacity = 128;
    size_t result_length = 0;
    char* result = (char*)mem_alloc(result_capacity, MEM_CAT_TEMP);
    if (!result) {
        node_querystring_host->node->roots->root_frame_end(&roots);
        return ItemNull;
    }
    result[0] = '\0';
    bool wrote_pair = false;

    // The host stores Object.create(null)'s prototype sentinel separately
    // when an own __proto__ data key exists, so ordinary key enumeration
    // intentionally hides it. Querystring must still serialize that key.
    Item own_proto_key = make_string_item("__proto__");
    *key_root = own_proto_key.item;
    Item own_proto_value = ItemNull;
    if (node_querystring_host->value->property_get_own_data(
            node_querystring_root_value(object_root), node_querystring_root_value(key_root),
                                                             &own_proto_value)) {
        *value_root = own_proto_value.item;
        char* own_proto_key_encoded = node_querystring_encode_value(
            node_querystring_root_value(key_root), node_querystring_root_value(encoder_root));
        char* own_proto_value_encoded = node_querystring_encode_value(
            node_querystring_root_value(value_root), node_querystring_root_value(encoder_root));
        if (own_proto_key_encoded && own_proto_value_encoded) {
            int key_length = (int)strlen(own_proto_key_encoded);
            int value_length = (int)strlen(own_proto_value_encoded);
            if (node_querystring_append(&result, &result_capacity, &result_length,
                                        own_proto_key_encoded, (size_t)key_length) &&
                    node_querystring_append(&result, &result_capacity, &result_length,
                                            eq, (size_t)eq_len) &&
                    node_querystring_append(&result, &result_capacity, &result_length,
                                            own_proto_value_encoded, (size_t)value_length)) {
                wrote_pair = true;
            }
        }
        if (own_proto_key_encoded) mem_free(own_proto_key_encoded);
        if (own_proto_value_encoded) mem_free(own_proto_value_encoded);
    }

    if (key_count <= 0) {
        Item output = make_string_item(result, (int)result_length);
        mem_free(result);
        node_querystring_host->node->roots->root_frame_end(&roots);
        return output;
    }

    for (int64_t i = 0; i < key_count; i++) {
        Item key = js_array_get_int(node_querystring_root_value(keys_root), i);
        *key_root = key.item;
        Item val = js_property_get(node_querystring_root_value(object_root),
            node_querystring_root_value(key_root));
        *value_root = val.item;

        // encode key
        char* key_enc = node_querystring_encode_value(node_querystring_root_value(key_root),
            node_querystring_root_value(encoder_root));
        if (!key_enc) continue;

        if (node_querystring_host->value->is_array(node_querystring_root_value(value_root))) {
            // value is an array — emit key=val for each element
            int64_t arr_len = js_array_length(node_querystring_root_value(value_root));
            for (int64_t j = 0; j < arr_len; j++) {
                if (wrote_pair && !node_querystring_append(&result, &result_capacity,
                        &result_length, sep, (size_t)sep_len)) break;
                int klen = (int)strlen(key_enc);
                if (!node_querystring_append(&result, &result_capacity, &result_length,
                        key_enc, (size_t)klen) ||
                        !node_querystring_append(&result, &result_capacity, &result_length,
                                eq, (size_t)eq_len)) break;
                Item elem = js_array_get_int(node_querystring_root_value(value_root), j);
                char* value_encoded = node_querystring_encode_value(elem,
                    node_querystring_root_value(encoder_root));
                if (value_encoded) {
                    int value_length = (int)strlen(value_encoded);
                    node_querystring_append(&result, &result_capacity, &result_length,
                                            value_encoded, (size_t)value_length);
                    mem_free(value_encoded);
                }
                wrote_pair = true;
            }
        } else {
            // single value
            if (wrote_pair && !node_querystring_append(&result, &result_capacity,
                    &result_length, sep, (size_t)sep_len)) break;
            int klen = (int)strlen(key_enc);
            if (!node_querystring_append(&result, &result_capacity, &result_length,
                    key_enc, (size_t)klen) ||
                    !node_querystring_append(&result, &result_capacity, &result_length,
                            eq, (size_t)eq_len)) break;
            char* value_encoded = node_querystring_encode_value(
                node_querystring_root_value(value_root), node_querystring_root_value(encoder_root));
            if (value_encoded) {
                int value_length = (int)strlen(value_encoded);
                node_querystring_append(&result, &result_capacity, &result_length,
                                        value_encoded, (size_t)value_length);
                mem_free(value_encoded);
            }
            wrote_pair = true;
        }

        mem_free(key_enc);
    }

    Item output = make_string_item(result, (int)result_length);
    mem_free(result);
    node_querystring_host->node->roots->root_frame_end(&roots);
    return output;
}

// ─── Namespace ───────────────────────────────────────────────────────────────

static void qs_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    JubeRootFrame frame = {};
    if (!node_querystring_host || !node_querystring_host->node ||
            !node_querystring_host->node->roots ||
            !node_querystring_host->node->roots->root_frame_begin ||
            !node_querystring_host->node->roots->root_frame_begin(&frame, 2)) return;
    uint64_t* key_root = node_querystring_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_querystring_host->node->roots->root_frame_take_slot(&frame);
    if (!key_root || !function_root) {
        node_querystring_host->node->roots->root_frame_end(&frame);
        return;
    }
    Item key = make_string_item(name);
    *key_root = key.item;
    Item fn = js_new_function(func_ptr, param_count);
    *function_root = fn.item;
    js_property_set(ns, key, fn);
    node_querystring_host->node->roots->root_frame_end(&frame);
}

Item node_querystring_namespace(void) {
    if (qs_namespace.item != 0) return qs_namespace;
    if (!node_querystring_host || !node_querystring_session) return ItemNull;

    qs_namespace = js_new_object();

    qs_set_method(qs_namespace, "parse",     (void*)js_qs_parse, 4);
    qs_set_method(qs_namespace, "stringify",  (void*)js_qs_stringify, 4);
    qs_set_method(qs_namespace, "escape",    (void*)js_qs_escape, 1);
    qs_set_method(qs_namespace, "unescape",  (void*)js_qs_unescape, 1);
    qs_set_method(qs_namespace, "unescapeBuffer", (void*)node_querystring_unescape_buffer, 2);
    qs_set_method(qs_namespace, "decode",    (void*)js_qs_parse, 4);     // alias
    qs_set_method(qs_namespace, "encode",    (void*)js_qs_stringify, 4); // alias

    // default export is the namespace itself
    JubeRootFrame frame = {};
    if (node_querystring_host->node->roots->root_frame_begin(&frame, 1)) {
        uint64_t* key_root = node_querystring_host->node->roots->root_frame_take_slot(&frame);
        if (key_root) {
            Item key = make_string_item("default");
            *key_root = key.item;
            js_property_set(qs_namespace, key, qs_namespace);
        }
        node_querystring_host->node->roots->root_frame_end(&frame);
    }

    return qs_namespace;
}

static void node_querystring_cache_reset(void) {
    qs_namespace = (Item){0};
}

int node_querystring_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->runtime || !host->node->roots ||
            !host->value || !host->script || !host->value->kind ||
            !host->value->new_object || !host->value->array_new ||
            !host->value->array_get || !host->value->array_length ||
            !host->value->array_push || !host->value->property_get ||
            !host->value->property_set_own || !host->value->property_has_own ||
            !host->value->property_get_own_data ||
            !host->value->is_array ||
            !host->value->string_copy ||
            !host->value->string_length || !host->value->string_from_utf8_n ||
            !host->script->new_function || !host->script->to_string ||
            !host->script->call_function || !host->script->object_keys ||
            !host->script->object_create || !host->script->is_truthy ||
            !host->script->throw_uri_error_code || !host->script->clear_exception ||
            !host->node->binary ||
            !host->node->binary->buffer_from_bytes) return -1;
    node_querystring_host = host;
    return 0;
}

void node_querystring_shutdown(void) {
    node_querystring_host = NULL;
}

void node_querystring_runtime_attach(void* session) {
    if (!node_querystring_host || !node_querystring_host->node ||
            !node_querystring_host->node->runtime ||
            !node_querystring_host->node->runtime->session_is_live ||
            !node_querystring_host->node->runtime->session_is_live(session)) return;
    if (!jube_node_session_module_state_get(session, JUBE_NODE_MODULE_STATE_QUERYSTRING,
            sizeof(NodeQuerystringSessionState))) return;
    node_querystring_session = session;
    if (node_querystring_host->node->roots->persistent_root_register(session,
            &qs_namespace.item) == 0) {
        node_querystring_rooted = true;
    }
}

void node_querystring_runtime_reset(void* session) {
    if (session != node_querystring_session) return;
    // This namespace owns function Items from the retiring JS heap.
    node_querystring_cache_reset();
}

void node_querystring_runtime_detach(void* session) {
    if (session != node_querystring_session || !node_querystring_host) return;
    node_querystring_cache_reset();
    if (node_querystring_rooted) {
        node_querystring_host->node->roots->persistent_root_unregister(session, &qs_namespace.item);
        node_querystring_rooted = false;
    }
    node_querystring_session = NULL;
}
