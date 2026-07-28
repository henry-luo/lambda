/**
 * js_url_module.cpp — Node.js-style 'url' module for LambdaJS
 *
 * Provides WHATWG URL API and legacy url.parse/url.format.
 * Registered by node-core through its Jube namespace descriptor.
 */
#include "node_url.hpp"
#include "../../jube/jube_registry.h"
#include "../../../lib/url.h"
#include "../../../lib/mem.h"
#include "../../../lib/hex.h"
#include "../../../lib/log.h"

#include <cstring>
#include <cstdlib>
#include <math.h>

static const JubeHostAPI* node_url_host = NULL;

#define JS_BLOB_URL_MAX 1024
struct NodeUrlSessionState {
    void* session;
    bool namespace_rooted;
    Item blob_url_values[JS_BLOB_URL_MAX];
    char blob_url_ids[JS_BLOB_URL_MAX][64];
    int64_t blob_url_next_id;
    Item module_namespace;
};
static NodeUrlSessionState* node_url_state(void) {
    return (NodeUrlSessionState*)jube_node_current_module_state(JUBE_NODE_MODULE_STATE_URL);
}
#define node_url_session (node_url_state()->session)
#define node_url_namespace_rooted (node_url_state()->namespace_rooted)
#define js_blob_url_values (node_url_state()->blob_url_values)
#define js_blob_url_ids (node_url_state()->blob_url_ids)
#define js_blob_url_next_id (node_url_state()->blob_url_next_id)
#define url_module_namespace (node_url_state()->module_namespace)

static bool node_url_ensure_host(void) {
    if (node_url_host && node_url_state()) return true;

    Item url_namespace = ItemNull;
    // Global URL and URLSearchParams use the node-core-owned primitive even
    // when no `require("url")` preceded them. Activate the descriptor at this
    // first use so direct globals cannot dereference an uninitialized host.
    if (jube_specifier_resolve("url", &url_namespace) != JUBE_SPECIFIER_RESOLVED ||
            !node_url_host || !node_url_state()) {
        log_error("node-url: global URL primitive activation failed");
        return false;
    }
    return true;
}

// Helper: make JS undefined value
// Helper: extract C string from Item
static const char* item_to_cstr(Item value, char* buf, int buf_size) {
    if (!buf || buf_size <= 0 || get_type_id(value) != LMD_TYPE_STRING ||
            !node_url_host->value->string_copy) return NULL;
    return node_url_host->value->string_copy(value, buf, (size_t)buf_size, NULL) ? buf : NULL;
}

static Item node_url_string(const char* text, int length) {
    if (!text || length < 0 || !node_url_host || !node_url_host->value ||
            !node_url_host->value->string_from_utf8_n) return ItemNull;
    return node_url_host->value->string_from_utf8_n(text, (size_t)length);
}

static Item node_url_string(const char* text) {
    return node_url_string(text, text ? (int)strlen(text) : 0);
}

static Item node_url_undefined(void) { return (Item){.item = ITEM_JS_UNDEFINED}; }
static Item node_url_null(void) { return (Item){.item = ITEM_NULL}; }

static bool node_url_roots_begin(JubeRootFrame* frame, size_t count) {
    return node_url_host && node_url_host->node && node_url_host->node->roots &&
            node_url_host->node->roots->root_frame_begin &&
            node_url_host->node->roots->root_frame_take_slot &&
            node_url_host->node->roots->root_frame_end &&
            node_url_host->node->roots->root_frame_begin(frame, count);
}

static Item node_url_root_value(const uint64_t* slot) {
    return (Item){.item = slot ? *slot : 0};
}

static Item node_url_throw_type_error(const char* message) {
    return node_url_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", message);
}

static Item node_url_throw_parse_type_error(Item value) {
    const char* prefix = "The \"url\" argument must be of type string. ";
    char message[512] = {};
    int kind = node_url_host->value->kind(value);
    if (kind == JUBE_VALUE_UNDEFINED) {
        snprintf(message, sizeof(message), "%sReceived undefined", prefix);
    } else if (kind == JUBE_VALUE_NULL) {
        snprintf(message, sizeof(message), "%sReceived null", prefix);
    } else if (kind == JUBE_VALUE_ARRAY) {
        snprintf(message, sizeof(message), "%sReceived an instance of Array", prefix);
    } else if (kind == JUBE_VALUE_OBJECT) {
        snprintf(message, sizeof(message), "%sReceived an instance of Object", prefix);
    } else if (kind == JUBE_VALUE_FUNCTION) {
        snprintf(message, sizeof(message), "%sReceived function ", prefix);
    } else {
        char typeof_text[32] = {};
        Item typeof_value = node_url_host->script->type_of(value);
        item_to_cstr(typeof_value, typeof_text, sizeof(typeof_text));
        const char* type_name = kind == JUBE_VALUE_BOOLEAN ? "boolean" :
            kind == JUBE_VALUE_NUMBER ? "number" :
            kind == JUBE_VALUE_SYMBOL || strcmp(typeof_text, "symbol") == 0 ? "symbol" : "unknown";
        char rendered[192] = {};
        Item text = node_url_host->script->to_string(value);
        item_to_cstr(text, rendered, sizeof(rendered));
        snprintf(message, sizeof(message), "%sReceived type %s (%s)", prefix,
                 type_name, rendered);
    }
    return node_url_throw_type_error(message);
}

static Item node_url_set_string_property(Item object, const char* name, const char* value) {
    JubeRootFrame frame = {};
    if (!node_url_roots_begin(&frame, 3)) return object;
    uint64_t* object_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !key_root || !value_root) {
        node_url_host->node->roots->root_frame_end(&frame);
        return object;
    }
    *object_root = object.item;
    Item key = node_url_string(name);
    *key_root = key.item;
    Item text = node_url_string(value ? value : "");
    *value_root = text.item;
    node_url_host->value->property_set(node_url_root_value(object_root),
        node_url_root_value(key_root), node_url_root_value(value_root));
    Item result = node_url_root_value(object_root);
    node_url_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_url_set_item_property(Item object, const char* name, Item value,
                                       bool own_property) {
    JubeRootFrame frame = {};
    if (!node_url_roots_begin(&frame, 3)) return object;
    uint64_t* object_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !key_root || !value_root) {
        node_url_host->node->roots->root_frame_end(&frame);
        return object;
    }
    *object_root = object.item;
    Item key = node_url_string(name);
    *key_root = key.item;
    *value_root = value.item;
    if (own_property) {
        node_url_host->value->property_set_own(node_url_root_value(object_root),
            node_url_root_value(key_root), node_url_root_value(value_root));
    } else {
        node_url_host->value->property_set(node_url_root_value(object_root),
            node_url_root_value(key_root), node_url_root_value(value_root));
    }
    Item result = node_url_root_value(object_root);
    node_url_host->node->roots->root_frame_end(&frame);
    return result;
}

#define get_type_id(VALUE) node_url_host->value->kind(VALUE)
#define LMD_TYPE_UNDEFINED JUBE_VALUE_UNDEFINED
#define LMD_TYPE_STRING JUBE_VALUE_STRING
#define LMD_TYPE_ARRAY JUBE_VALUE_ARRAY
#define LMD_TYPE_MAP JUBE_VALUE_OBJECT
#define make_string_item node_url_string
#define make_js_undefined() node_url_undefined()
#define js_new_object() node_url_host->value->new_object()
#define js_array_new(CAPACITY) node_url_host->value->array_new(CAPACITY)
#define js_array_push(ARRAY, VALUE) node_url_host->value->array_push(ARRAY, VALUE)
#define js_array_length(ARRAY) node_url_host->value->array_length(ARRAY)
#define js_array_get_int(ARRAY, INDEX) node_url_host->value->array_get(ARRAY, INDEX)
#define js_array_set_int(ARRAY, INDEX, VALUE) node_url_host->value->array_set(ARRAY, INDEX, VALUE)
#define js_property_get(OBJECT, KEY) node_url_host->value->property_get(OBJECT, KEY)
#define js_property_set(OBJECT, KEY, VALUE) node_url_host->value->property_set(OBJECT, KEY, VALUE)
#define js_new_function(FUNCTION, COUNT) node_url_host->script->new_function(FUNCTION, COUNT)
#define js_mark_non_enumerable(OBJECT, KEY) node_url_host->script->mark_non_enumerable(OBJECT, KEY)
#define js_get_this() node_url_host->script->current_this()
#define js_to_string(VALUE) node_url_host->script->to_string(VALUE)
#define js_object_keys(OBJECT) node_url_host->script->object_keys(OBJECT)
#define js_call_function(FUNCTION, THIS, ARGS, COUNT) node_url_host->script->call_function(FUNCTION, THIS, ARGS, COUNT)
#define js_strict_equal(LEFT, RIGHT) node_url_host->script->strict_equal(LEFT, RIGHT)
#define js_is_truthy(VALUE) node_url_host->script->is_truthy(VALUE)

// Helper: create string Item
extern "C" Item js_blob_url_resolve(Item id_item) {
    if (get_type_id(id_item) != LMD_TYPE_STRING) return make_js_undefined();
    char id[64] = {};
    if (!item_to_cstr(id_item, id, sizeof(id)) || !id[0]) return make_js_undefined();
    for (int i = 0; i < JS_BLOB_URL_MAX; i++) {
        if (js_blob_url_values[i].item == 0) continue;
        if (strcmp(id, js_blob_url_ids[i]) == 0) {
            return js_blob_url_values[i];
        }
    }
    return make_js_undefined();
}

static Item js_url_createObjectURL(Item blob) {
    if (!node_url_host->script->class_is(blob, JUBE_SCRIPT_CLASS_BLOB)) {
        return node_url_throw_type_error("The \"obj\" argument must be a Blob");
    }
    for (int i = 0; i < JS_BLOB_URL_MAX; i++) {
        if (js_blob_url_values[i].item != 0) continue;
        int id_len = snprintf(js_blob_url_ids[i], sizeof(js_blob_url_ids[i]),
                              "blob:nodedata:%lld", (long long)js_blob_url_next_id++);
        if (id_len < 0) id_len = 0;
        if (id_len >= (int)sizeof(js_blob_url_ids[i])) id_len = (int)sizeof(js_blob_url_ids[i]) - 1;
        js_blob_url_values[i] = blob;
        // Blob URLs are process-local handles; the registry is the owning root
        // until revokeObjectURL clears the slot.
        return make_string_item(js_blob_url_ids[i], id_len);
    }
    return node_url_throw_type_error("Blob URL registry is full");
}

static Item js_url_revokeObjectURL(Item id_item) {
    if (get_type_id(id_item) != LMD_TYPE_STRING) return make_js_undefined();
    char id[64] = {};
    if (!item_to_cstr(id_item, id, sizeof(id))) return make_js_undefined();
    for (int i = 0; i < JS_BLOB_URL_MAX; i++) {
        if (js_blob_url_values[i].item == 0) continue;
        if (strcmp(id, js_blob_url_ids[i]) == 0) {
            js_blob_url_values[i] = (Item){0};
            js_blob_url_ids[i][0] = '\0';
            return make_js_undefined();
        }
    }
    return make_js_undefined();
}

extern "C" void js_blob_url_reset(void) {
    for (int i = 0; i < JS_BLOB_URL_MAX; i++) {
        js_blob_url_values[i] = (Item){0};
        js_blob_url_ids[i][0] = '\0';
    }
    js_blob_url_next_id = 1;
}

// Helper: convert Url* to JS object with URL properties
static Item url_to_js_object(Url* url) {
    if (!url) return ItemNull;

    Item obj = js_new_object();
    JubeRootFrame frame = {};
    if (!node_url_roots_begin(&frame, 3)) return obj;
    uint64_t* object_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* params_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !params_root || !key_root) {
        node_url_host->node->roots->root_frame_end(&frame);
        return obj;
    }
    *object_root = obj.item;
    *params_root = ItemNull.item;
    *key_root = ItemNull.item;

    // T5b: legacy `__class_name__` string write retired.
    node_url_host->script->class_stamp(node_url_root_value(object_root), JUBE_SCRIPT_CLASS_URL);

    const char* href = url_get_href(url);
    const char* origin_str = url_get_origin(url);
    const char* protocol = url_get_protocol(url);
    const char* username = url_get_username(url);
    const char* password = url_get_password(url);
    const char* host = url_get_host(url);
    const char* hostname = url_get_hostname(url);
    const char* port = url_get_port(url);
    const char* pathname = url_get_pathname(url);
    const char* search = url_get_search(url);
    const char* hash = url_get_hash(url);

    *object_root = node_url_set_string_property(node_url_root_value(object_root), "href", href).item;
    *object_root = node_url_set_string_property(node_url_root_value(object_root), "origin", origin_str).item;
    *object_root = node_url_set_string_property(node_url_root_value(object_root), "protocol", protocol).item;
    *object_root = node_url_set_string_property(node_url_root_value(object_root), "username", username).item;
    *object_root = node_url_set_string_property(node_url_root_value(object_root), "password", password).item;
    *object_root = node_url_set_string_property(node_url_root_value(object_root), "host", host).item;
    *object_root = node_url_set_string_property(node_url_root_value(object_root), "hostname", hostname).item;
    *object_root = node_url_set_string_property(node_url_root_value(object_root), "port", port).item;
    *object_root = node_url_set_string_property(node_url_root_value(object_root), "pathname", pathname).item;
    *object_root = node_url_set_string_property(node_url_root_value(object_root), "search", search).item;
    *object_root = node_url_set_string_property(node_url_root_value(object_root), "hash", hash).item;

    // searchParams — basic object (not full URLSearchParams)
    if (search && search[0] == '?' && search[1] != '\0') {
        *params_root = js_new_object().item;
        // parse query string into key-value pairs
        char query_buf[4096];
        int qlen = (int)strlen(search + 1);
        if (qlen >= (int)sizeof(query_buf)) qlen = (int)sizeof(query_buf) - 1;
        memcpy(query_buf, search + 1, qlen);
        query_buf[qlen] = '\0';

        char* pair = strtok(query_buf, "&");
        while (pair) {
            char* eq = strchr(pair, '=');
            if (eq) {
                *eq = '\0';
                *params_root = node_url_set_string_property(node_url_root_value(params_root),
                    pair, eq + 1).item;
            } else {
                *params_root = node_url_set_string_property(node_url_root_value(params_root),
                    pair, "").item;
            }
            pair = strtok(NULL, "&");
        }
        *key_root = make_string_item("searchParams").item;
        node_url_host->value->property_set(node_url_root_value(object_root),
            node_url_root_value(key_root), node_url_root_value(params_root));
        // searchParams is a per-instance wrapper; if enumerable, deep equality
        // compares wrapper identity instead of the URL's canonical href.
        js_mark_non_enumerable(node_url_root_value(object_root), node_url_root_value(key_root));
    } else {
        *params_root = js_new_object().item;
        *key_root = make_string_item("searchParams").item;
        // The outer roots stay authoritative after each property allocation;
        // reusing the original local Item here lost URL fields under forced GC.
        node_url_host->value->property_set(node_url_root_value(object_root),
            node_url_root_value(key_root), node_url_root_value(params_root));
        // searchParams is a per-instance wrapper; if enumerable, deep equality
        // compares wrapper identity instead of the URL's canonical href.
        js_mark_non_enumerable(node_url_root_value(object_root), node_url_root_value(key_root));
    }

    Item result = node_url_root_value(object_root);
    node_url_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_url_legacy_new(void);
static Item node_url_legacy_query(const char* search);

static Item url_parse_legacy_path_only(const char* url_str) {
    if (!url_str) return ItemNull;

    const char* hash = strchr(url_str, '#');
    const char* query = strchr(url_str, '?');
    if (hash && query && hash < query) query = NULL;

    const char* path_end = url_str + strlen(url_str);
    if (query && query < path_end) path_end = query;
    if (hash && hash < path_end) path_end = hash;

    int pathname_len = (int)(path_end - url_str);
    int search_len = query ? (int)((hash && hash > query ? hash : url_str + strlen(url_str)) - query) : 0;
    int hash_len = hash ? (int)strlen(hash) : 0;
    int path_len = pathname_len + search_len;

    Item obj = node_url_legacy_new();
    JubeRootFrame frame = {};
    if (!node_url_roots_begin(&frame, 1)) return obj;
    uint64_t* object_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root) {
        node_url_host->node->roots->root_frame_end(&frame);
        return obj;
    }
    *object_root = obj.item;
    obj = node_url_set_string_property(obj, "href", url_str);
    obj = node_url_set_item_property(obj, "pathname", make_string_item(url_str, pathname_len), false);
    if (search_len > 0) obj = node_url_set_item_property(obj, "search",
        make_string_item(query, search_len), false);
    if (hash_len > 0) obj = node_url_set_item_property(obj, "hash",
        make_string_item(hash, hash_len), false);
    obj = node_url_set_item_property(obj, "path", make_string_item(url_str, path_len), false);
    Item result = node_url_root_value(object_root);
    node_url_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_url_legacy_set_defaults(Item object) {
    // Legacy Url instances always expose this fixed own-field shape, including
    // null fields, so Object.keys() stays compatible with Node's Url class.
    object = node_url_set_item_property(object, "protocol", node_url_null(), false);
    object = node_url_set_item_property(object, "slashes", node_url_null(), false);
    object = node_url_set_item_property(object, "auth", node_url_null(), false);
    object = node_url_set_item_property(object, "host", node_url_null(), false);
    object = node_url_set_item_property(object, "port", node_url_null(), false);
    object = node_url_set_item_property(object, "hostname", node_url_null(), false);
    object = node_url_set_item_property(object, "hash", node_url_null(), false);
    object = node_url_set_item_property(object, "search", node_url_null(), false);
    object = node_url_set_item_property(object, "query", node_url_null(), false);
    object = node_url_set_item_property(object, "pathname", node_url_null(), false);
    object = node_url_set_item_property(object, "path", node_url_null(), false);
    return node_url_set_item_property(object, "href", node_url_null(), false);
}

static Item node_url_legacy_new(void) {
    Item object = js_new_object();
    JubeRootFrame frame = {};
    if (!node_url_roots_begin(&frame, 1)) return object;
    uint64_t* object_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root) {
        node_url_host->node->roots->root_frame_end(&frame);
        return object;
    }
    *object_root = object.item;
    node_url_host->script->class_stamp(object, JUBE_SCRIPT_CLASS_URL);
    object = node_url_legacy_set_defaults(object);
    Item result = node_url_root_value(object_root);
    node_url_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_url_legacy_query(const char* search) {
    Item query = node_url_host->script->object_create(ItemNull);
    JubeRootFrame frame = {};
    if (!node_url_roots_begin(&frame, 5)) return query;
    uint64_t* query_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* existing_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* array_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    if (!query_root || !key_root || !value_root || !existing_root || !array_root) {
        node_url_host->node->roots->root_frame_end(&frame);
        return query;
    }
    *query_root = query.item;
    if (!search || !search[0]) {
        node_url_host->node->roots->root_frame_end(&frame);
        return query;
    }

    const char* text = search[0] == '?' ? search + 1 : search;
    // The path-only fallback passes the original suffix; a fragment belongs to
    // legacy `hash`, never to the query dictionary.
    const char* fragment = strchr(text, '#');
    size_t text_length = fragment ? (size_t)(fragment - text) : strlen(text);
    char* input = (char*)mem_alloc(text_length + 1, MEM_CAT_TEMP);
    if (!input) {
        node_url_host->node->roots->root_frame_end(&frame);
        return query;
    }
    // text may end at a fragment delimiter rather than its source terminator.
    memcpy(input, text, text_length);
    input[text_length] = '\0';
    char* pair = input;
    while (pair) {
        char* next = strchr(pair, '&');
        if (next) *next = '\0';
        char* equals = strchr(pair, '=');
        char* raw_value = equals ? equals + 1 : (char*)"";
        if (equals) *equals = '\0';
        for (char* p = pair; *p; p++) if (*p == '+') *p = ' ';
        for (char* p = raw_value; *p; p++) if (*p == '+') *p = ' ';
        size_t key_length = 0;
        size_t value_length = 0;
        char* decoded_key = url_decode_component(pair, strlen(pair), &key_length);
        char* decoded_value = url_decode_component(raw_value, strlen(raw_value), &value_length);
        if (decoded_key && decoded_value) {
            Item key = make_string_item(decoded_key, (int)key_length);
            Item value = make_string_item(decoded_value, (int)value_length);
            *key_root = key.item;
            *value_root = value.item;
            Item existing = js_property_get(node_url_root_value(query_root),
                node_url_root_value(key_root));
            *existing_root = existing.item;
            if (node_url_host->value->property_has_own(node_url_root_value(query_root),
                    node_url_root_value(key_root))) {
                if (node_url_host->value->is_array(node_url_root_value(existing_root))) {
                    js_array_push(node_url_root_value(existing_root),
                        node_url_root_value(value_root));
                } else {
                    Item values = js_array_new(0);
                    *array_root = values.item;
                    js_array_push(node_url_root_value(array_root),
                        node_url_root_value(existing_root));
                    js_array_push(node_url_root_value(array_root),
                        node_url_root_value(value_root));
                    node_url_host->value->property_set_own(node_url_root_value(query_root),
                        node_url_root_value(key_root), node_url_root_value(array_root));
                }
            } else {
                node_url_host->value->property_set_own(node_url_root_value(query_root),
                    node_url_root_value(key_root), node_url_root_value(value_root));
            }
        }
        if (decoded_key) mem_free(decoded_key);
        if (decoded_value) mem_free(decoded_value);
        if (!next) break;
        pair = next + 1;
    }
    mem_free(input);
    Item result = node_url_root_value(query_root);
    node_url_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_url_legacy_set_url_fields(Item object, Url* url, const char* fallback_href,
                                           bool parse_query) {
    const char* href = url ? url_get_href(url) : fallback_href;
    const char* protocol = url ? url_get_protocol(url) : NULL;
    const char* host = url ? url_get_host(url) : NULL;
    const char* port = url ? url_get_port(url) : NULL;
    const char* hostname = url ? url_get_hostname(url) : NULL;
    const char* hash = url ? url_get_hash(url) : NULL;
    const char* search = url ? url_get_search(url) : NULL;
    const char* pathname = url ? url_get_pathname(url) : NULL;
    const char* username = url ? url_get_username(url) : NULL;
    const char* password = url ? url_get_password(url) : NULL;
    if (href && href[0]) object = node_url_set_string_property(object, "href", href);
    if (protocol && protocol[0]) object = node_url_set_string_property(object, "protocol", protocol);
    if (host && host[0]) {
        object = node_url_set_string_property(object, "host", host);
        object = node_url_set_item_property(object, "slashes", (Item){.item = b2it(true)}, false);
    }
    if (port && port[0]) object = node_url_set_string_property(object, "port", port);
    if (hostname && hostname[0]) object = node_url_set_string_property(object, "hostname", hostname);
    if (hash && hash[0]) object = node_url_set_string_property(object, "hash", hash);
    if (search && search[0]) object = node_url_set_string_property(object, "search", search);
    if (pathname && pathname[0]) object = node_url_set_string_property(object, "pathname", pathname);
    if (pathname && pathname[0]) {
        char path[8192] = {};
        int length = snprintf(path, sizeof(path), "%s%s", pathname,
                              search && search[0] ? search : "");
        if (length < 0) length = 0;
        if (length >= (int)sizeof(path)) length = (int)sizeof(path) - 1;
        object = node_url_set_string_property(object, "path", path);
    }
    if (username && username[0]) {
        char auth[2048] = {};
        int length = snprintf(auth, sizeof(auth), "%s%s%s", username,
                              password && password[0] ? ":" : "",
                              password && password[0] ? password : "");
        if (length < 0) length = 0;
        if (length >= (int)sizeof(auth)) length = (int)sizeof(auth) - 1;
        size_t decoded_length = 0;
        char* decoded = url_decode_component(auth, (size_t)length, &decoded_length);
        object = node_url_set_string_property(object, "auth", decoded ? decoded : auth);
        if (decoded) mem_free(decoded);
    }
    if (parse_query) {
        Item query = node_url_legacy_query(search);
        object = node_url_set_item_property(object, "query", query, false);
    } else if (search && search[0]) {
        object = node_url_set_string_property(object, "query", search + 1);
    }
    return object;
}

extern "C" Item js_url_legacy_construct(void) {
    return node_url_legacy_new();
}

// =============================================================================
// URL Functions
// =============================================================================

// new URL(input[, base]) — construct URL object
extern "C" Item js_url_module_construct(Item input_item, Item base_item) {
    if (!node_url_ensure_host()) return ItemNull;
    char input_buf[4096];
    const char* input = item_to_cstr(input_item, input_buf, sizeof(input_buf));
    if (!input) {
        log_error("url: URL constructor: invalid input");
        return ItemNull;
    }

    Url* url = NULL;

    if (get_type_id(base_item) == LMD_TYPE_STRING) {
        char base_buf[4096];
        const char* base = item_to_cstr(base_item, base_buf, sizeof(base_buf));
        if (base) {
            Url* base_url = url_parse(base);
            if (base_url) {
                url = url_parse_with_base(input, base_url);
                url_destroy(base_url);
            }
        }
    }

    if (!url) {
        url = url_parse(input);
    }

    if (!url || !url->is_valid) {
        if (url) url_destroy(url);
        log_error("url: URL constructor: invalid URL '%s'", input);
        return ItemNull;
    }

    Item result = url_to_js_object(url);
    url_destroy(url);
    return result;
}

// url.parse(urlString[, parseQueryString]) — legacy Node.js url.parse
extern "C" Item js_url_parse_legacy(Item url_item, Item parse_query_item) {
    char url_buf[4096];
    const char* url_str = item_to_cstr(url_item, url_buf, sizeof(url_buf));
    if (!url_str) return node_url_throw_parse_type_error(url_item);
    bool parse_query = node_url_host->script->is_truthy(parse_query_item);

    Url* url = url_parse(url_str);
    if (!url || !url->is_valid) {
        if (url) url_destroy(url);
        Item result = url_parse_legacy_path_only(url_str);
        JubeRootFrame fallback_frame = {};
        if (!node_url_roots_begin(&fallback_frame, 1)) return result;
        uint64_t* result_root = node_url_host->node->roots->root_frame_take_slot(&fallback_frame);
        if (!result_root) {
            node_url_host->node->roots->root_frame_end(&fallback_frame);
            return result;
        }
        *result_root = result.item;
        const char* hash = strchr(url_str, '#');
        const char* query = strchr(url_str, '?');
        if (hash && query && hash < query) query = NULL;
        if (parse_query) {
            Item parsed = node_url_legacy_query(query);
            result = node_url_set_item_property(node_url_root_value(result_root), "query", parsed, false);
        } else if (query) {
            const char* query_end = hash && hash > query ? hash : url_str + strlen(url_str);
            result = node_url_set_item_property(node_url_root_value(result_root), "query",
                make_string_item(query + 1, (int)(query_end - query - 1)), false);
        }
        *result_root = result.item;
        result = node_url_root_value(result_root);
        node_url_host->node->roots->root_frame_end(&fallback_frame);
        return result;
    }

    Item result = node_url_legacy_new();
    JubeRootFrame frame = {};
    if (node_url_roots_begin(&frame, 1)) {
        uint64_t* result_root = node_url_host->node->roots->root_frame_take_slot(&frame);
        if (result_root) {
            *result_root = result.item;
            result = node_url_legacy_set_url_fields(result, url, url_str, parse_query);
            result = node_url_root_value(result_root);
        }
        node_url_host->node->roots->root_frame_end(&frame);
    }
    url_destroy(url);
    return result;
}

// url.format(urlObject) — serialize URL object back to string
extern "C" Item js_url_format(Item obj_item) {
    if (get_type_id(obj_item) == LMD_TYPE_STRING) return obj_item;
    if (get_type_id(obj_item) != LMD_TYPE_MAP) return make_string_item("");

    // try to get href directly
    Item href = js_property_get(obj_item, make_string_item("href"));
    char href_buf[4096] = {};
    if (item_to_cstr(href, href_buf, sizeof(href_buf)) && href_buf[0]) {
        return href;
    }

    // reconstruct from parts
    char buf[4096];
    int pos = 0;

    char part[4096] = {};
    Item protocol = js_property_get(obj_item, make_string_item("protocol"));
    if (item_to_cstr(protocol, part, sizeof(part))) {
        int part_len = (int)strlen(part);
        memcpy(buf + pos, part, (size_t)part_len);
        pos += part_len;
        if (part_len > 0 && part[part_len - 1] != ':') buf[pos++] = ':';
        buf[pos++] = '/'; buf[pos++] = '/';
    }

    Item hostname = js_property_get(obj_item, make_string_item("hostname"));
    Item host = js_property_get(obj_item, make_string_item("host"));
    if (item_to_cstr(host, part, sizeof(part)) && part[0]) {
        int part_len = (int)strlen(part);
        memcpy(buf + pos, part, (size_t)part_len);
        pos += part_len;
    } else if (item_to_cstr(hostname, part, sizeof(part))) {
        int part_len = (int)strlen(part);
        memcpy(buf + pos, part, (size_t)part_len);
        pos += part_len;
    }

    Item pathname = js_property_get(obj_item, make_string_item("pathname"));
    if (item_to_cstr(pathname, part, sizeof(part))) {
        int part_len = (int)strlen(part);
        if (part_len > 0 && part[0] != '/') buf[pos++] = '/';
        memcpy(buf + pos, part, (size_t)part_len);
        pos += part_len;
    }

    Item search = js_property_get(obj_item, make_string_item("search"));
    if (item_to_cstr(search, part, sizeof(part)) && part[0]) {
        int part_len = (int)strlen(part);
        memcpy(buf + pos, part, (size_t)part_len);
        pos += part_len;
    }

    Item hash = js_property_get(obj_item, make_string_item("hash"));
    if (item_to_cstr(hash, part, sizeof(part)) && part[0]) {
        int part_len = (int)strlen(part);
        memcpy(buf + pos, part, (size_t)part_len);
        pos += part_len;
    }

    buf[pos] = '\0';
    return make_string_item(buf, pos);
}

static Item node_url_resolve_legacy_path(const char* base_path, const char* relative_path,
                                         const char* suffix) {
    char combined[8192] = {};
    size_t base_length = strlen(base_path);
    bool rooted = base_path[0] == '/';
    size_t directory_length = base_length;
    if (base_length > 0 && base_path[base_length - 1] != '/') {
        const char* slash = strrchr(base_path, '/');
        directory_length = slash ? (size_t)(slash - base_path + 1) : 0;
    }
    if (directory_length >= sizeof(combined)) directory_length = sizeof(combined) - 1;
    memcpy(combined, base_path, directory_length);
    size_t position = directory_length;
    if (position > 0 && combined[position - 1] != '/' && position + 1 < sizeof(combined)) {
        combined[position++] = '/';
    }
    size_t relative_length = strlen(relative_path);
    if (relative_length >= sizeof(combined) - position) {
        relative_length = sizeof(combined) - position - 1;
    }
    memcpy(combined + position, relative_path, relative_length);
    combined[position + relative_length] = '\0';

    char* segments[256] = {};
    int segment_count = 0;
    char* segment = strtok(combined, "/");
    while (segment && segment_count < 256) {
        if (strcmp(segment, ".") == 0 || segment[0] == '\0') {
            // current-directory segments do not affect the canonical path.
        } else if (strcmp(segment, "..") == 0) {
            if (segment_count > 0 && strcmp(segments[segment_count - 1], "..") != 0) {
                segment_count--;
            } else if (!rooted) {
                segments[segment_count++] = segment;
            }
        } else {
            segments[segment_count++] = segment;
        }
        segment = strtok(NULL, "/");
    }

    char result[8192] = {};
    int written = rooted ? 1 : 0;
    if (rooted) result[0] = '/';
    for (int i = 0; i < segment_count && written < (int)sizeof(result) - 1; i++) {
        if (written > 0 && result[written - 1] != '/') result[written++] = '/';
        int segment_length = (int)strlen(segments[i]);
        if (segment_length >= (int)sizeof(result) - written) {
            segment_length = (int)sizeof(result) - written - 1;
        }
        memcpy(result + written, segments[i], segment_length);
        written += segment_length;
    }
    bool preserve_directory = relative_path[0] &&
        (relative_path[strlen(relative_path) - 1] == '/' || strcmp(relative_path, ".") == 0 ||
         strcmp(relative_path, "..") == 0);
    if (preserve_directory && written > 0 && result[written - 1] != '/' &&
            written < (int)sizeof(result) - 1) {
        result[written++] = '/';
    }
    if (suffix) {
        int suffix_length = (int)strlen(suffix);
        if (suffix_length >= (int)sizeof(result) - written) {
            suffix_length = (int)sizeof(result) - written - 1;
        }
        memcpy(result + written, suffix, suffix_length);
        written += suffix_length;
    }
    result[written] = '\0';
    return make_string_item(result, written);
}

// url.resolve(from, to) — legacy url.resolve
extern "C" Item js_url_resolve(Item from_item, Item to_item) {
    char from_buf[4096], to_buf[4096];
    const char* from_str = item_to_cstr(from_item, from_buf, sizeof(from_buf));
    const char* to_str = item_to_cstr(to_item, to_buf, sizeof(to_buf));
    if (!from_str || !to_str) return ItemNull;

    Url* base = url_parse(from_str);
    if (!base || !base->is_valid) {
        if (base) url_destroy(base);
        if (!url_starts_with_scheme(to_str) &&
                to_str[0] != '/' && to_str[0] != '?' && to_str[0] != '#') {
            const char* suffix = strpbrk(to_str, "?#");
            int path_length = suffix ? (int)(suffix - to_str) : (int)strlen(to_str);
            char relative_path[4096] = {};
            if (path_length >= (int)sizeof(relative_path)) path_length = (int)sizeof(relative_path) - 1;
            memcpy(relative_path, to_str, path_length);
            if (from_str[0] != '/') {
                return node_url_resolve_legacy_path(from_str, relative_path, suffix);
            }
            char* resolved_path = url_resolve_path(from_str, relative_path);
            if (resolved_path) {
                char result[8192] = {};
                size_t resolved_length = strlen(resolved_path);
                bool preserve_directory = path_length > 0 &&
                    (relative_path[path_length - 1] == '/' ||
                     strcmp(relative_path, ".") == 0 || strcmp(relative_path, "..") == 0);
                int length = snprintf(result, sizeof(result), "%s%s%s", resolved_path,
                                      preserve_directory && resolved_length > 0 &&
                                      resolved_path[resolved_length - 1] != '/' ? "/" : "",
                                      suffix ? suffix : "");
                mem_free(resolved_path);
                if (length < 0) length = 0;
                if (length >= (int)sizeof(result)) length = (int)sizeof(result) - 1;
                return make_string_item(result, length);
            }
        }
        return make_string_item(to_str);
    }

    Url* resolved = url_parse_with_base(to_str, base);
    if (!resolved) {
        url_destroy(base);
        return make_string_item(to_str);
    }

    const char* href = url_get_href(resolved);
    Item result = href ? make_string_item(href) : make_string_item(to_str);

    url_destroy(resolved);
    url_destroy(base);
    return result;
}

// ─── fileURLToPath(url) — convert file:// URL to local file path ────────────
extern "C" Item js_url_fileURLToPath(Item url_item) {
    if (get_type_id(url_item) != LMD_TYPE_STRING) return ItemNull;
    char buf[4096];
    if (!item_to_cstr(url_item, buf, sizeof(buf))) return ItemNull;

    // strip "file://" prefix
    const char* path = buf;
    if (strncmp(path, "file://", 7) == 0) {
        path += 7;
        // handle "file:///path" -> "/path"
        // on macOS/Linux: file:///Users/foo -> /Users/foo
        // on Windows: file:///C:/foo -> C:/foo
#ifdef _WIN32
        if (path[0] == '/' && path[2] == ':') path++; // skip leading /
#endif
    }

    // URL-decode %XX sequences (paths: no '+' -> space). Fall back to the raw
    // path if it is not well-formed percent-encoding.
    size_t out_len = 0;
    char* decoded = url_decode_component(path, strlen(path), &out_len);
    Item result = decoded ? make_string_item(decoded, (int)out_len)
                          : make_string_item(path);
    if (decoded) mem_free(decoded);
    return result;
}

// ─── pathToFileURL(path) — convert local path to file:// URL ────────────────
extern "C" Item js_url_pathToFileURL(Item path_item) {
    if (get_type_id(path_item) != LMD_TYPE_STRING) return ItemNull;
    char path[4096] = {};
    if (!item_to_cstr(path_item, path, sizeof(path))) return ItemNull;

    // build a percent-encoded, cross-platform file:// URL
    char* file_url = url_from_local_path(path);
    if (!file_url) return ItemNull;
    Item result = make_string_item(file_url);
    mem_free(file_url);
    return result;
}

// url.urlToHttpOptions(url) — convert a WHATWG URL-shaped object into the
// request option record consumed by the Node HTTP surface.
extern "C" Item js_url_to_http_options(Item url_item) {
    int kind = get_type_id(url_item);
    if (kind != LMD_TYPE_MAP && kind != JUBE_VALUE_OBJECT) {
        return node_url_throw_type_error("The \"url\" argument must be of type object");
    }
    JubeRootFrame frame = {};
    if (!node_url_roots_begin(&frame, 2)) return ItemNull;
    uint64_t* options_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    if (!options_root || !value_root) {
        node_url_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item options = js_new_object();
    *options_root = options.item;
    if (!node_url_host->script->class_is(url_item, JUBE_SCRIPT_CLASS_URL)) {
        node_url_set_string_property(options, "path", "");
        Item nan = node_url_host->script->make_number(NAN);
        *value_root = nan.item;
        js_property_set(options, make_string_item("port"), nan);
        node_url_host->node->roots->root_frame_end(&frame);
        return options;
    }
    const char* names[] = {"protocol", "hostname", "pathname", "search", "hash", "href", NULL};
    for (int index = 0; names[index]; index++) {
        Item key = make_string_item(names[index]);
        Item value = js_property_get(url_item, key);
        *value_root = value.item;
        if (get_type_id(value) != LMD_TYPE_UNDEFINED) js_property_set(options, key, value);
    }
    Item hostname = js_property_get(url_item, make_string_item("hostname"));
    if (get_type_id(hostname) == LMD_TYPE_STRING) {
        char host_text[4096] = {};
        if (item_to_cstr(hostname, host_text, sizeof(host_text))) {
            size_t host_length = strlen(host_text);
            if (host_length > 1 && host_text[0] == '[' && host_text[host_length - 1] == ']') {
                host_text[host_length - 1] = '\0';
                node_url_set_string_property(options, "hostname", host_text + 1);
            }
        }
    }
    Item username = js_property_get(url_item, make_string_item("username"));
    *value_root = username.item;
    Item password = js_property_get(url_item, make_string_item("password"));
    if (get_type_id(username) == LMD_TYPE_STRING) {
        char user[1024] = {};
        char pass[1024] = {};
        item_to_cstr(username, user, sizeof(user));
        item_to_cstr(password, pass, sizeof(pass));
        if (user[0] || pass[0]) {
            char auth[2100] = {};
            snprintf(auth, sizeof(auth), "%s:%s", user, pass);
            node_url_set_string_property(options, "auth", auth);
        }
    }
    Item pathname = js_property_get(url_item, make_string_item("pathname"));
    Item search = js_property_get(url_item, make_string_item("search"));
    char path[4096] = {};
    char search_text[4096] = {};
    item_to_cstr(pathname, path, sizeof(path));
    item_to_cstr(search, search_text, sizeof(search_text));
    char request_path[8192] = {};
    snprintf(request_path, sizeof(request_path), "%s%s", path, search_text);
    node_url_set_string_property(options, "path", request_path);
    Item port = js_property_get(url_item, make_string_item("port"));
    if (get_type_id(port) == LMD_TYPE_STRING) {
        char port_text[64] = {};
        if (item_to_cstr(port, port_text, sizeof(port_text)) && port_text[0]) {
            Item number = node_url_host->script->make_number(atof(port_text));
            *value_root = number.item;
            js_property_set(options, make_string_item("port"), number);
        }
    }
    node_url_host->node->roots->root_frame_end(&frame);
    return options;
}

// =============================================================================
// URLSearchParams Implementation
// =============================================================================

// internal: parse query string "key=value&key2=value2" into entries array
static Item parse_query_entries(const char* qs, int qs_len) {
    Item entries = js_array_new(0);
    JubeRootFrame frame = {};
    if (!node_url_roots_begin(&frame, 2)) return entries;
    uint64_t* entries_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* entry_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    if (!entries_root || !entry_root) {
        node_url_host->node->roots->root_frame_end(&frame);
        return entries;
    }
    *entries_root = entries.item;
    if (!qs || qs_len == 0) {
        node_url_host->node->roots->root_frame_end(&frame);
        return entries;
    }

    char buf[4096];
    if (qs_len >= (int)sizeof(buf)) qs_len = (int)sizeof(buf) - 1;
    memcpy(buf, qs, qs_len);
    buf[qs_len] = '\0';

    // URL-decode a string in-place (application/x-www-form-urlencoded: '+' -> ' ')
    auto url_decode = [](char* s) { url_decode_inplace(s, true); };

    char* saveptr = NULL;
    char* pair = strtok_r(buf, "&", &saveptr);
    while (pair) {
        char* eq = strchr(pair, '=');
        Item entry = js_array_new(0);
        *entry_root = entry.item;
        if (eq) {
            *eq = '\0';
            url_decode(pair);
            url_decode(eq + 1);
            js_array_push(entry, make_string_item(pair));
            js_array_push(entry, make_string_item(eq + 1));
        } else {
            url_decode(pair);
            js_array_push(entry, make_string_item(pair));
            js_array_push(entry, make_string_item(""));
        }
        js_array_push(entries, entry);
        pair = strtok_r(NULL, "&", &saveptr);
    }
    node_url_host->node->roots->root_frame_end(&frame);
    return entries;
}

// URLSearchParams instance: stores entries as array of [key, value] pairs in __entries__

// URLSearchParams.prototype.append(name, value)
extern "C" Item js_usp_append(Item name_item, Item value_item) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    Item name_str = js_to_string(name_item);
    Item value_str = js_to_string(value_item);
    Item entry = js_array_new(0);
    js_array_push(entry, name_str);
    js_array_push(entry, value_str);
    js_array_push(entries, entry);
    return make_js_undefined();
}

// URLSearchParams.prototype.delete(name[, value])
extern "C" Item js_usp_delete(Item name_item, Item value_item) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    Item name_str = js_to_string(name_item);
    bool check_value = get_type_id(value_item) != LMD_TYPE_UNDEFINED;
    Item value_str = check_value ? js_to_string(value_item) : (Item){0};

    Item new_entries = js_array_new(0);
    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        Item entry = js_array_get_int(entries, i);
        Item ek = js_array_get_int(entry, 0);
        Item match = js_strict_equal(ek, name_str);
        if (js_is_truthy(match)) {
            if (check_value) {
                Item ev = js_array_get_int(entry, 1);
                Item vm = js_strict_equal(ev, value_str);
                if (js_is_truthy(vm)) continue; // delete matching
                js_array_push(new_entries, entry);
            }
            continue; // delete
        }
        js_array_push(new_entries, entry);
    }
    js_property_set(self, make_string_item("__entries__"), new_entries);
    return make_js_undefined();
}

// URLSearchParams.prototype.get(name)
extern "C" Item js_usp_get(Item name_item) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    Item name_str = js_to_string(name_item);
    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        Item entry = js_array_get_int(entries, i);
        Item ek = js_array_get_int(entry, 0);
        Item match = js_strict_equal(ek, name_str);
        if (js_is_truthy(match)) return js_array_get_int(entry, 1);
    }
    return ItemNull;
}

// URLSearchParams.prototype.getAll(name)
extern "C" Item js_usp_getAll(Item name_item) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    Item name_str = js_to_string(name_item);
    Item result = js_array_new(0);
    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        Item entry = js_array_get_int(entries, i);
        Item ek = js_array_get_int(entry, 0);
        Item match = js_strict_equal(ek, name_str);
        if (js_is_truthy(match)) js_array_push(result, js_array_get_int(entry, 1));
    }
    return result;
}

// URLSearchParams.prototype.has(name[, value])
extern "C" Item js_usp_has(Item name_item, Item value_item) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    Item name_str = js_to_string(name_item);
    bool check_value = get_type_id(value_item) != LMD_TYPE_UNDEFINED;
    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        Item entry = js_array_get_int(entries, i);
        Item ek = js_array_get_int(entry, 0);
        Item match = js_strict_equal(ek, name_str);
        if (js_is_truthy(match)) {
            if (check_value) {
                Item ev = js_array_get_int(entry, 1);
                Item value_str = js_to_string(value_item);
                Item vm = js_strict_equal(ev, value_str);
                if (js_is_truthy(vm)) return (Item){.item = b2it(true)};
            } else {
                return (Item){.item = b2it(true)};
            }
        }
    }
    return (Item){.item = b2it(false)};
}

// URLSearchParams.prototype.set(name, value)
extern "C" Item js_usp_set(Item name_item, Item value_item) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    Item name_str = js_to_string(name_item);
    Item value_str = js_to_string(value_item);
    bool found = false;
    Item new_entries = js_array_new(0);
    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        Item entry = js_array_get_int(entries, i);
        Item ek = js_array_get_int(entry, 0);
        Item match = js_strict_equal(ek, name_str);
        if (js_is_truthy(match)) {
            if (!found) {
                Item new_entry = js_array_new(0);
                js_array_push(new_entry, name_str);
                js_array_push(new_entry, value_str);
                js_array_push(new_entries, new_entry);
                found = true;
            }
            // skip duplicate entries
        } else {
            js_array_push(new_entries, entry);
        }
    }
    if (!found) {
        Item new_entry = js_array_new(0);
        js_array_push(new_entry, name_str);
        js_array_push(new_entry, value_str);
        js_array_push(new_entries, new_entry);
    }
    js_property_set(self, make_string_item("__entries__"), new_entries);
    return make_js_undefined();
}

// URLSearchParams.prototype.sort()
extern "C" Item js_usp_sort(void) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    // simple bubble sort by key
    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len - 1; i++) {
        for (int64_t j = 0; j < len - 1 - i; j++) {
            Item a = js_array_get_int(entries, j);
            Item b = js_array_get_int(entries, j + 1);
            Item ak = js_array_get_int(a, 0);
            Item bk = js_array_get_int(b, 0);
            char sa[4096] = {};
            char sb[4096] = {};
            if (item_to_cstr(ak, sa, sizeof(sa)) && item_to_cstr(bk, sb, sizeof(sb))) {
                int cmp = strcmp(sa, sb);
                if (cmp > 0) {
                    js_array_set_int(entries, j, b);
                    js_array_set_int(entries, j + 1, a);
                }
            }
        }
    }
    return make_js_undefined();
}

// URLSearchParams.prototype.toString()
extern "C" Item js_usp_toString(void) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    int64_t len = js_array_length(entries);
    if (len == 0) return make_string_item("", 0);

    char buf[8192];
    int pos = 0;
    for (int64_t i = 0; i < len && pos < (int)sizeof(buf) - 100; i++) {
        if (i > 0) buf[pos++] = '&';
        Item entry = js_array_get_int(entries, i);
        Item ek = js_array_get_int(entry, 0);
        Item ev = js_array_get_int(entry, 1);
        // URL-encode key and value
        auto url_encode = [&](Item s) {
            if (get_type_id(s) != LMD_TYPE_STRING) return;
            char text[4096] = {};
            if (!item_to_cstr(s, text, sizeof(text))) return;
            int text_len = (int)strlen(text);
            for (int j = 0; j < text_len && pos < (int)sizeof(buf) - 4; j++) {
                char c = text[j];
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '!' ||
                    c == '~' || c == '*' || c == '\'' || c == '(' || c == ')') {
                    buf[pos++] = c;
                } else if (c == ' ') {
                    buf[pos++] = '+';
                } else {
                    buf[pos++] = '%';
                    buf[pos++] = hex_encode_nibble_upper((unsigned char)c >> 4);
                    buf[pos++] = hex_encode_nibble_upper((unsigned char)c & 0x0F);
                }
            }
        };
        url_encode(ek);
        buf[pos++] = '=';
        url_encode(ev);
    }
    buf[pos] = '\0';
    return make_string_item(buf, pos);
}

// URLSearchParams.prototype.forEach(callback[, thisArg])
extern "C" Item js_usp_forEach(Item callback, Item this_arg) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    int64_t len = js_array_length(entries);
    Item this_val = get_type_id(this_arg) != LMD_TYPE_UNDEFINED ? this_arg : self;
    for (int64_t i = 0; i < len; i++) {
        Item entry = js_array_get_int(entries, i);
        Item value = js_array_get_int(entry, 1);
        Item key = js_array_get_int(entry, 0);
        Item args[3] = {value, key, self};
        js_call_function(callback, this_val, args, 3);
    }
    return make_js_undefined();
}

// URLSearchParams.prototype.keys() — returns iterator
extern "C" Item js_usp_keys(void) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    int64_t len = js_array_length(entries);
    Item result = js_array_new(0);
    for (int64_t i = 0; i < len; i++) {
        Item entry = js_array_get_int(entries, i);
        js_array_push(result, js_array_get_int(entry, 0));
    }
    return result;
}

// URLSearchParams.prototype.values() — returns iterator
extern "C" Item js_usp_values(void) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    int64_t len = js_array_length(entries);
    Item result = js_array_new(0);
    for (int64_t i = 0; i < len; i++) {
        Item entry = js_array_get_int(entries, i);
        js_array_push(result, js_array_get_int(entry, 1));
    }
    return result;
}

// URLSearchParams.prototype.entries() — returns iterator
extern "C" Item js_usp_entries(void) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    return entries;
}

// URLSearchParams.prototype[Symbol.iterator]() — same as entries
// (handled by setting Symbol.iterator to entries)

// size getter
extern "C" Item js_usp_size(void) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    return (Item){.item = i2it((int)js_array_length(entries))};
}

// new URLSearchParams([init])
extern "C" Item js_url_search_params_new(Item init) {
    if (!node_url_ensure_host()) return ItemNull;
    Item obj = js_new_object();
    JubeRootFrame frame = {};
    if (!node_url_roots_begin(&frame, 2)) return obj;
    uint64_t* object_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* entries_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !entries_root) {
        node_url_host->node->roots->root_frame_end(&frame);
        return obj;
    }
    *object_root = obj.item;
    // T5b: legacy `__class_name__` string write retired.
    node_url_host->script->class_stamp(obj, JUBE_SCRIPT_CLASS_URL_SEARCH_PARAMS);

    Item entries;
    int init_type = get_type_id(init);

    if (init_type == LMD_TYPE_STRING) {
        char query[4096] = {};
        if (!item_to_cstr(init, query, sizeof(query))) {
            node_url_host->node->roots->root_frame_end(&frame);
            return ItemNull;
        }
        const char* qs = query;
        int qs_len = (int)strlen(query);
        if (qs_len > 0 && qs[0] == '?') { qs++; qs_len--; }
        entries = parse_query_entries(qs, qs_len);
        *entries_root = entries.item;
    } else if (init_type == LMD_TYPE_MAP) {
        entries = js_array_new(0);
        *entries_root = entries.item;
        Item keys = js_object_keys(init);
        int64_t klen = js_array_length(keys);
        for (int64_t i = 0; i < klen; i++) {
            Item key = js_array_get_int(keys, i);
            Item val = js_property_get(init, key);
            Item entry = js_array_new(0);
            js_array_push(entry, js_to_string(key));
            js_array_push(entry, js_to_string(val));
            js_array_push(entries, entry);
        }
    } else if (init_type == LMD_TYPE_ARRAY) {
        entries = js_array_new(0);
        *entries_root = entries.item;
        int64_t len = js_array_length(init);
        for (int64_t i = 0; i < len; i++) {
            Item pair = js_array_get_int(init, i);
            Item entry = js_array_new(0);
            js_array_push(entry, js_to_string(js_array_get_int(pair, 0)));
            js_array_push(entry, js_to_string(js_array_get_int(pair, 1)));
            js_array_push(entries, entry);
        }
    } else {
        entries = js_array_new(0);
        *entries_root = entries.item;
    }

    Item entries_key = make_string_item("__entries__");
    js_property_set(obj, entries_key, entries);
    // URLSearchParams entries are an internal list; exposing the backing array
    // as enumerable makes assert deep-equality compare implementation storage.
    js_mark_non_enumerable(obj, entries_key);

    // set methods
    auto usp_method = [&](const char* name, void* fn, int params) {
        Item key = make_string_item(name);
        js_property_set(obj, key, js_new_function(fn, params));
        // URLSearchParams methods live on the prototype in Node; keeping these
        // fallback own methods enumerable leaks per-instance functions.
        js_mark_non_enumerable(obj, key);
    };
    usp_method("append",  (void*)js_usp_append, 2);
    usp_method("delete",  (void*)js_usp_delete, 2);
    usp_method("get",     (void*)js_usp_get, 1);
    usp_method("getAll",  (void*)js_usp_getAll, 1);
    usp_method("has",     (void*)js_usp_has, 2);
    usp_method("set",     (void*)js_usp_set, 2);
    usp_method("sort",    (void*)js_usp_sort, 0);
    usp_method("toString",(void*)js_usp_toString, 0);
    usp_method("forEach", (void*)js_usp_forEach, 2);
    usp_method("keys",    (void*)js_usp_keys, 0);
    usp_method("values",  (void*)js_usp_values, 0);
    usp_method("entries",  (void*)js_usp_entries, 0);

    // size as getter
    int64_t sz = js_array_length(entries);
    Item size_key = make_string_item("size");
    js_property_set(obj, size_key, (Item){.item = i2it((int)sz)});
    // size is observable as state, but it is not an enumerable data field.
    js_mark_non_enumerable(obj, size_key);

    node_url_host->node->roots->root_frame_end(&frame);
    return obj;
}

// =============================================================================
// url Module Namespace Object
// =============================================================================

static Item js_url_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    JubeRootFrame frame = {};
    if (!node_url_roots_begin(&frame, 2)) return ItemNull;
    uint64_t* key_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    if (!key_root || !function_root) {
        node_url_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item key = make_string_item(name);
    *key_root = key.item;
    Item fn = js_new_function(func_ptr, param_count);
    *function_root = fn.item;
    js_property_set(ns, key, fn);
    node_url_host->node->roots->root_frame_end(&frame);
    return fn;
}

Item node_url_namespace(void) {
    if (url_module_namespace.item != 0) return url_module_namespace;
    if (!node_url_host || !node_url_session) return ItemNull;

    url_module_namespace = js_new_object();
    JubeRootFrame frame = {};
    if (!node_url_roots_begin(&frame, 2)) return url_module_namespace;
    uint64_t* constructor_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_url_host->node->roots->root_frame_take_slot(&frame);
    if (!constructor_root || !key_root) {
        node_url_host->node->roots->root_frame_end(&frame);
        return url_module_namespace;
    }

    // URL constructor (as a function, not class)
    Item url_ctor = js_url_set_method(url_module_namespace, "URL", (void*)js_url_module_construct, 2);
    *constructor_root = url_ctor.item;
    js_url_set_method(url_ctor, "createObjectURL", (void*)js_url_createObjectURL, 1);
    js_url_set_method(url_ctor, "revokeObjectURL", (void*)js_url_revokeObjectURL, 1);
    js_url_set_method(url_ctor, "resolveObjectURL", (void*)js_blob_url_resolve, 1);

    // legacy methods
    js_url_set_method(url_module_namespace, "parse", (void*)js_url_parse_legacy, 2);
    js_url_set_method(url_module_namespace, "format", (void*)js_url_format, 1);
    js_url_set_method(url_module_namespace, "resolve", (void*)js_url_resolve, 2);
    js_url_set_method(url_module_namespace, "resolveObject", (void*)js_url_resolve, 2);
    js_url_set_method(url_module_namespace, "Url", (void*)js_url_legacy_construct, 0);

    // file URL conversion
    js_url_set_method(url_module_namespace, "fileURLToPath", (void*)js_url_fileURLToPath, 1);
    js_url_set_method(url_module_namespace, "pathToFileURL", (void*)js_url_pathToFileURL, 1);
    js_url_set_method(url_module_namespace, "urlToHttpOptions", (void*)js_url_to_http_options, 1);

    // URLSearchParams constructor
    js_url_set_method(url_module_namespace, "URLSearchParams", (void*)js_url_search_params_new, 1);

    // default export
    Item default_key = make_string_item("default");
    *key_root = default_key.item;
    js_property_set(url_module_namespace, default_key, url_module_namespace);
    node_url_host->node->roots->root_frame_end(&frame);

    return url_module_namespace;
}

static void node_url_cache_reset(void) {
    url_module_namespace = (Item){0};
    js_blob_url_reset();
}

int node_url_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->runtime || !host->node->roots ||
            !host->value || !host->script || !host->value->kind ||
            !host->value->new_object || !host->value->array_new ||
            !host->value->array_push || !host->value->array_get ||
            !host->value->array_set || !host->value->array_length ||
            !host->value->property_get || !host->value->property_set ||
            !host->value->property_set_own || !host->value->property_has_own ||
            !host->value->is_array ||
            !host->value->string_copy || !host->value->string_from_utf8_n ||
            !host->script->new_function || !host->script->mark_non_enumerable ||
            !host->script->current_this || !host->script->to_string ||
            !host->script->object_keys || !host->script->call_function ||
            !host->script->strict_equal || !host->script->is_truthy ||
            !host->script->class_stamp || !host->script->class_is ||
            !host->script->make_number || !host->script->object_create ||
            !host->script->type_of) return -1;
    node_url_host = host;
    return 0;
}

void node_url_shutdown(void) {
    node_url_host = NULL;
}

void node_url_runtime_attach(void* session) {
    if (!node_url_host || !node_url_host->node || !node_url_host->node->runtime ||
            !node_url_host->node->runtime->session_is_live ||
            !node_url_host->node->runtime->session_is_live(session)) return;
    NodeUrlSessionState* state = (NodeUrlSessionState*)jube_node_session_module_state_get(
        session, JUBE_NODE_MODULE_STATE_URL, sizeof(NodeUrlSessionState));
    if (!state) return;
    // A zeroed session starts blob identifiers at one without sharing a
    // counter between contexts.
    if (state->blob_url_next_id == 0) state->blob_url_next_id = 1;
    node_url_session = session;
    if (node_url_host->node->roots->persistent_root_register(session,
            &url_module_namespace.item) != 0) return;
    for (int i = 0; i < JS_BLOB_URL_MAX; i++) {
        if (node_url_host->node->roots->persistent_root_register(session,
                &js_blob_url_values[i].item) != 0) {
            // A partially registered root set would survive detach without the
            // ownership flag, retaining stale Blob values across sessions.
            for (int registered = 0; registered < i; registered++) {
                node_url_host->node->roots->persistent_root_unregister(session,
                    &js_blob_url_values[registered].item);
            }
            node_url_host->node->roots->persistent_root_unregister(session,
                &url_module_namespace.item);
            node_url_session = NULL;
            return;
        }
    }
    node_url_namespace_rooted = true;
}

void node_url_runtime_reset(void* session) {
    if (session == node_url_session) node_url_cache_reset();
}

void node_url_runtime_detach(void* session) {
    if (!node_url_host || session != node_url_session) return;
    if (node_url_namespace_rooted) {
        node_url_host->node->roots->persistent_root_unregister(session,
            &url_module_namespace.item);
        for (int i = 0; i < JS_BLOB_URL_MAX; i++) {
            node_url_host->node->roots->persistent_root_unregister(session,
                &js_blob_url_values[i].item);
        }
        node_url_namespace_rooted = false;
    }
    node_url_cache_reset();
    node_url_session = NULL;
}
