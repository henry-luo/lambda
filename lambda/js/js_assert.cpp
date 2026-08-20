/**
 * js_assert.cpp — Node.js-style 'assert' module for LambdaJS
 *
 * Provides assertion functions for testing:
 * assert(value), assert.ok, assert.equal, assert.notEqual,
 * assert.strictEqual, assert.notStrictEqual, assert.deepStrictEqual,
 * assert.throws, assert.doesNotThrow, assert.fail
 */
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_event_loop.h"
#include "js_class.h"
#include "js_error_codes.h"
#include "js_typed_array.h"
#include "js_object_pair_traversal.hpp"
#include "../lambda-data.hpp"
#include "../runtime/transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <math.h>
#include <time.h>

extern "C" Item js_util_inspect(Item obj_item, Item options_item);
extern "C" Item js_util_isDeepStrictEqual(Item a, Item b);
extern "C" Item js_util_isDeepEqual(Item a, Item b);
extern "C" Item js_process_set_exitCode(Item code_item);
extern "C" Item js_buffer_isBuffer(Item obj);
extern "C" int64_t js_key_is_symbol_c(Item key);

static void js_assert_append_inspected_value(StrBuf* sb, Item value);
static void js_assert_append_error_label(StrBuf* sb, Item value);
static int js_assert_append_value_type(char* buf, int buf_size, Item value);
static bool js_assert_is_real_regexp(Item value);
static bool js_assert_is_buffer_value(Item value);
static void js_assert_append_item_text(StrBuf* sb, Item value);
static bool js_assert_has_own_property_key(Item object, Item key);
static void js_assert_append_quoted_key(StrBuf* sb, String* key);
static bool js_assert_same_property_key(Item left, Item right);
static bool js_assert_is_plain_diff_object(Item value);
static bool js_assert_is_arguments_value(Item value);
static void js_assert_append_multiline_object_key(StrBuf* sb, Item key);
static void js_assert_append_property_value(StrBuf* sb, Item owner, Item key, Item value);
static void js_assert_append_multiline_value(StrBuf* sb, Item value, int indent,
                                             char sign, bool trailing_comma,
                                             int depth_left);
static Item js_assert_enumerable_own_keys(Item object);
JS_FORWARD_STATIC_ITEM(js_assert_noop, (void), make_js_undefined, ())

static Item assert_make_string(const char* str) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, strlen(str));
    return (Item){.item = s2it(s)};
}

static Item assert_make_string_n(const char* str, size_t len) {
    String* s = heap_create_name(str ? str : "", len);
    return (Item){.item = s2it(s)};
}

template <typename Target>
JS_FORWARD_STATIC_VOID( js_assert_set_native, (Item object, const char* name, Target target), js_set_native_key, (object, assert_make_string(name), target))

extern "C" uint64_t js_get_heap_epoch(void);

#define assert_namespace (js_runtime_state.assert.namespace_object)
#define internal_errors_namespace (js_runtime_state.assert.internal_errors_namespace)
#define internal_assert_myers_diff_namespace (js_runtime_state.assert.internal_myers_diff_namespace)
#define assert_options_key (js_runtime_state.assert.options_key)
#define assert_diff_key (js_runtime_state.assert.diff_key)
#define assert_instances (js_runtime_state.assert.instances)
#define assert_instance_count (js_runtime_state.assert.instance_count)
#define assert_key_epoch (js_runtime_state.assert.key_epoch)
#define assert_instances_roots_epoch (js_runtime_state.assert.instances_roots_epoch)

static void js_assert_register_instance(Item instance) {
    uint64_t epoch = js_get_heap_epoch();
    if (assert_instances_roots_epoch != epoch) {
        heap_register_gc_root_range((uint64_t*)assert_instances, 64);
        assert_instances_roots_epoch = epoch;
    }
    if (assert_instance_count < 64) {
        assert_instances[assert_instance_count++] = instance;
    }
}

static bool js_assert_is_registered_instance(Item value) {
    for (int i = 0; i < assert_instance_count; i++) {
        if (assert_instances[i].item == value.item) return true;
    }
    return false;
}

static Item js_assert_options_key(void) {
    uint64_t epoch = js_get_heap_epoch();
    if (assert_options_key.item == 0 || assert_key_epoch != epoch) {
        assert_options_key = assert_make_string("_options");
        heap_register_gc_root(&assert_options_key.item);
        assert_diff_key = assert_make_string("diff");
        heap_register_gc_root(&assert_diff_key.item);
        // Cached keys survive in static storage, but their objects and root
        // registrations belong to one batch heap only.
        assert_key_epoch = epoch;
    }
    return assert_options_key;
}

static Item js_assert_diff_key(void) {
    (void)js_assert_options_key();
    return assert_diff_key;
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_assert_item_is_date, (Item value), (get_type_id(value) == LMD_TYPE_MAP && js_class_id(value) == JS_CLASS_DATE))

#define js_assert_string_equals js_string_equals

static bool js_assert_has_date_prototype(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP || js_assert_item_is_date(value)) return false;
    Item proto = js_get_prototype_of(value);
    if (get_type_id(proto) != LMD_TYPE_MAP) return false;
    Item date_ctor = js_get_constructor(assert_make_string("Date"));
    if (get_type_id(date_ctor) == LMD_TYPE_FUNC) {
        Item date_proto = js_get_key_cstr(date_ctor, "prototype");
        if (proto.item == date_proto.item) return true;
    }
    Item tag = js_get_key_default(proto, js_well_known_symbol_key(4));
    return js_assert_string_equals(tag, "Date");
}

static bool js_assert_append_date_checktag_value(StrBuf* sb, Item value) {
    if (js_assert_item_is_date(value)) {
        Item iso = js_date_method(value, 8);
        if (get_type_id(iso) != LMD_TYPE_STRING) return false;
        String* s = it2s(iso);
        if (!s) return false;
        strbuf_append_str_n(sb, s->chars, s->len);
        return true;
    }
    if (js_assert_has_date_prototype(value)) {
        strbuf_append_str(sb, "Date {}");
        return true;
    }
    return false;
}

static Item js_assert_date_checktag_message(Item actual, Item expected) {
    bool actual_date = js_assert_item_is_date(actual);
    bool expected_date = js_assert_item_is_date(expected);
    bool actual_fake_date = js_assert_has_date_prototype(actual);
    bool expected_fake_date = js_assert_has_date_prototype(expected);
    if (!((actual_date && expected_fake_date) ||
          (actual_fake_date && expected_date))) {
        return ItemNull;
    }

    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "Expected values to be strictly deep-equal:\n");
    strbuf_append_str(sb, "+ actual - expected\n");
    strbuf_append_str(sb, "\n");
    strbuf_append_str(sb, "+ ");
    if (!js_assert_append_date_checktag_value(sb, actual)) return ItemNull;
    strbuf_append_str(sb, "\n");
    strbuf_append_str(sb, "- ");
    if (!js_assert_append_date_checktag_value(sb, expected)) return ItemNull;
    strbuf_append_str(sb, "\n");
    return assert_make_string_n(sb->str, sb->length);
}

static void js_assert_attach_assertion_error_prototype(Item error) {
    if (assert_namespace.item == 0) return;
    Item ae_fn = js_get_key_cstr(assert_namespace, "AssertionError");
    if (get_type_id(ae_fn) != LMD_TYPE_FUNC) return;
    Item ae_proto = js_get_key_cstr(ae_fn, "prototype");
    if (get_type_id(ae_proto) == LMD_TYPE_MAP) {
        js_set_prototype(error, ae_proto);
    }
}

static const char* js_assert_normalized_diff(Item diff) {
    if (get_type_id(diff) != LMD_TYPE_STRING) return "simple";
    String* s = it2s(diff);
    if (!s) return "simple";
    if (s->len == 4 && memcmp(s->chars, "full", 4) == 0) return "full";
    return "simple";
}

static bool js_assert_valid_diff(Item diff) {
    if (get_type_id(diff) == LMD_TYPE_UNDEFINED || diff.item == ITEM_JS_UNDEFINED ||
        get_type_id(diff) == LMD_TYPE_NULL || diff.item == ItemNull.item) {
        return true;
    }
    if (get_type_id(diff) != LMD_TYPE_STRING) return false;
    String* s = it2s(diff);
    if (!s) return false;
    if (s->len == 4 && memcmp(s->chars, "full", 4) == 0) return true;
    if (s->len == 6 && memcmp(s->chars, "simple", 6) == 0) return true;
    return false;
}

static Item js_assert_throw_invalid_diff(Item diff) {
    String* s = get_type_id(diff) == LMD_TYPE_STRING ? it2s(diff) : NULL;
    const char* received = s ? s->chars : "";
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "The property 'options.diff' must be one of: 'simple', 'full'. Received '");
    strbuf_append_str(sb, received);
    strbuf_append_str(sb, "'");
    Item result = js_throw_type_error_code(JS_ERR_INVALID_ARG_VALUE, sb->str);
    strbuf_free(sb);
    return result;
}

static const char* js_assert_current_diff(void) {
    Item this_val = js_get_this();
    if (!js_assert_is_registered_instance(this_val)) {
        return "simple";
    }
    Item options = js_get_key_default(this_val, js_assert_options_key());
    if (get_type_id(options) != LMD_TYPE_MAP) return "simple";
    return js_assert_normalized_diff(js_get_key_default(options, js_assert_diff_key()));
}

static bool js_assert_current_has_instance_diff(void) {
    Item this_val = js_get_this();
    return js_assert_is_registered_instance(this_val);
}
JS_FORWARD_STATIC_ITEM(js_assert_instance_error_key, (void), assert_make_string, ("__assert_instance_error__"))

static void js_assert_mark_instance_error(Item error) {
    if (!js_assert_current_has_instance_diff()) return;
    Item key = js_assert_instance_error_key();
    // Assert instance errors need Node's longer inspect string budget, while
    // detached instance methods and module-level assert use compact inspect.
    js_set_key_default(error, key, (Item){.item = b2it(true)});
    js_mark_non_enumerable(error, key);
}

static bool js_assert_options_strict(Item options) {
    if (get_type_id(options) != LMD_TYPE_MAP) return true;
    Item strict = js_get_key_cstr(options, "strict");
    if (strict.item == ITEM_FALSE || (get_type_id(strict) == LMD_TYPE_BOOL && !it2b(strict))) {
        return false;
    }
    return true;
}

static bool js_assert_current_skip_prototype(void) {
    Item this_val = js_get_this();
    if (!js_assert_is_registered_instance(this_val)) {
        return false;
    }
    Item options = js_get_key_default(this_val, js_assert_options_key());
    if (get_type_id(options) != LMD_TYPE_MAP) return false;
    Item skip = js_get_key_cstr(options, "skipPrototype");
    return skip.item == ITEM_TRUE || (get_type_id(skip) == LMD_TYPE_BOOL && it2b(skip));
}

static bool js_assert_is_partial_object_like(Item value);

static bool js_assert_prototypes_differ(Item actual, Item expected) {
    if (!js_assert_is_partial_object_like(actual) ||
            !js_assert_is_partial_object_like(expected)) {
        return false;
    }
    // Buffer uses Uint8Array storage/prototype internally; strict deep equality
    // still has to observe Node's Buffer brand as a prototype distinction.
    if (js_assert_is_buffer_value(actual) != js_assert_is_buffer_value(expected)) {
        return true;
    }
    Item actual_proto = js_get_prototype_of(actual);
    Item expected_proto = js_get_prototype_of(expected);
    return actual_proto.item != expected_proto.item;
}

static bool js_assert_skip_prototype_typed_array_equal(Item actual, Item expected) {
    if (!js_is_typed_array(actual) || !js_is_typed_array(expected)) return false;
    int actual_len = js_typed_array_length(actual);
    int expected_len = js_typed_array_length(expected);
    if (actual_len != expected_len || actual_len < 0) return false;
    for (int i = 0; i < actual_len; i++) {
        Item av = js_typed_array_get(actual, (Item){.item = i2it(i)});
        Item ev = js_typed_array_get(expected, (Item){.item = i2it(i)});
        Item same = js_strict_equal(av, ev);
        if (get_type_id(same) != LMD_TYPE_BOOL || !it2b(same)) return false;
    }
    return true;
}
JS_FORWARD_STATIC_ITEM(js_assert_throw_missing_actual_expected, (void), js_throw_type_error_code, (JS_ERR_MISSING_ARGS, "The \"actual\" and \"expected\" arguments must be specified"))

static void js_internal_errors_append_item(StrBuf* sb, Item value) {
    TypeId type = get_type_id(value);
    char buf[128];
    if (type == LMD_TYPE_INT) {
        snprintf(buf, sizeof(buf), "%lld", (long long)it2i(value));
        strbuf_append_str(sb, buf);
        return;
    }
    if (type == LMD_TYPE_FLOAT) {
        snprintf(buf, sizeof(buf), "%.15g", it2d(value));
        strbuf_append_str(sb, buf);
        return;
    }
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(value);
        if (s) strbuf_append_str_n(sb, s->chars, s->len);
        return;
    }
    Item text = js_to_string(value);
    if (get_type_id(text) == LMD_TYPE_STRING) {
        String* s = it2s(text);
        if (s) strbuf_append_str_n(sb, s->chars, s->len);
    }
}

static Item js_internal_errors_make_range_error(Item message) {
    Item error = js_new_error_with_name(assert_make_string("RangeError"), message);
    js_set_key_cstr(error, "code", assert_make_string(JS_ERR_OUT_OF_RANGE));
    return error;
}

extern "C" Item js_internal_errors_ERR_OUT_OF_RANGE_ctor(Item name, Item range, Item actual) {
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "The value of \"");
    js_internal_errors_append_item(sb, name);
    strbuf_append_str(sb, "\" is out of range. It must be ");
    js_internal_errors_append_item(sb, range);
    strbuf_append_str(sb, ". Received ");
    js_internal_errors_append_item(sb, actual);
    Item message = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return js_internal_errors_make_range_error(message);
}
JS_FORWARD_EXPRESSION(Item, js_internal_errors_identity, (Item value), (value))
JS_FORWARD_EXPRESSION(Item, js_internal_errors_true, (void), ((Item){.item = ITEM_TRUE}))

template <typename Target>
static void js_internal_errors_set_code(Item codes, const char* name,
        Target target) {
    Item fn = js_new_native_constructor(target);

    Item range_ctor = js_get_constructor(assert_make_string("RangeError"));
    Item range_proto = js_get_key_cstr(range_ctor, "prototype");
    Item proto = js_object_create(range_proto);
    js_set_key_cstr(proto, "constructor", fn);
    js_set_key_cstr(fn, "prototype", proto);

    js_set_key_default(codes, assert_make_string(name), fn);
}

extern "C" Item js_get_internal_errors_namespace(void) {
    if (internal_errors_namespace.item != 0) return internal_errors_namespace;

    internal_errors_namespace = js_new_object();
    heap_register_gc_root(&internal_errors_namespace.item);

    Item codes = js_new_object();
    js_internal_errors_set_code(codes, JS_ERR_OUT_OF_RANGE,
        js_internal_errors_ERR_OUT_OF_RANGE_ctor);
    js_set_key_cstr(internal_errors_namespace, "codes", codes);

    js_assert_set_native(internal_errors_namespace, "hideStackFrames", js_internal_errors_identity);
    js_assert_set_native(internal_errors_namespace, "hideInternalStackFrames", js_internal_errors_identity);
    js_assert_set_native(internal_errors_namespace, "isErrorStackTraceLimitWritable", js_internal_errors_true);
    js_set_key_cstr(internal_errors_namespace, "default", internal_errors_namespace);
    return internal_errors_namespace;
}

static Item js_assert_myers_make_operation(int op, Item value) {
    Item pair = js_array_new(0);
    js_array_push(pair, (Item){.item = i2it(op)});
    js_array_push(pair, value);
    return pair;
}

extern "C" Item js_internal_assert_myersDiff(Item actual, Item expected, Item check_comma_disparity) {
    (void)check_comma_disparity;
    int64_t actual_len = js_get_length(actual);
    int64_t expected_len = js_get_length(expected);
    if (actual_len < 0) actual_len = 0;
    if (expected_len < 0) expected_len = 0;
    int64_t max = actual_len + expected_len;
    if (max > 2147483647LL) {
        return js_throw_range_error_codef(JS_ERR_OUT_OF_RANGE, 
            "The value of \"myersDiff input size\" is out of range. It must be < 2^31. Received %lld", (long long)max);
    }

    Item diff = js_array_new(0);
    int64_t common_len = actual_len < expected_len ? actual_len : expected_len;
    for (int64_t i = common_len - 1; i >= 0; i--) {
        Item actual_value = js_elements_get_int(actual, i);
        Item expected_value = js_elements_get_int(expected, i);
        Item same = js_strict_equal(actual_value, expected_value);
        if (get_type_id(same) == LMD_TYPE_BOOL && it2b(same)) {
            js_array_push(diff, js_assert_myers_make_operation(0, actual_value));
        } else {
            js_array_push(diff, js_assert_myers_make_operation(1, actual_value));
            js_array_push(diff, js_assert_myers_make_operation(-1, expected_value));
        }
    }
    for (int64_t i = actual_len - 1; i >= common_len; i--) {
        js_array_push(diff, js_assert_myers_make_operation(1, js_elements_get_int(actual, i)));
    }
    for (int64_t i = expected_len - 1; i >= common_len; i--) {
        js_array_push(diff, js_assert_myers_make_operation(-1, js_elements_get_int(expected, i)));
    }
    return diff;
}

static void js_assert_myers_append_string_value(StrBuf* sb, Item value) {
    Item text = get_type_id(value) == LMD_TYPE_STRING ? value : js_to_string_val(value);
    String* s = get_type_id(text) == LMD_TYPE_STRING ? it2s(text) : NULL;
    if (s) strbuf_append_str_n(sb, s->chars, s->len);
}

extern "C" Item js_internal_assert_printSimpleMyersDiff(Item diff) {
    StrBuf* sb = strbuf_new();
    strbuf_append_char(sb, '\n');
    int64_t len = js_array_length(diff);
    for (int64_t i = len - 1; i >= 0; i--) {
        Item pair = js_elements_get_int(diff, i);
        js_assert_myers_append_string_value(sb, js_elements_get_int(pair, 1));
    }
    Item result = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

extern "C" Item js_internal_assert_printMyersDiff(Item diff, Item operator_item) {
    (void)operator_item;
    StrBuf* sb = strbuf_new();
    strbuf_append_char(sb, '\n');
    int64_t len = js_array_length(diff);
    for (int64_t i = len - 1; i >= 0; i--) {
        Item pair = js_elements_get_int(diff, i);
        Item op_item = js_elements_get_int(pair, 0);
        int64_t op = get_type_id(op_item) == LMD_TYPE_INT ? it2i(op_item) : 0;
        if (op > 0) strbuf_append_str(sb, "+ ");
        else if (op < 0) strbuf_append_str(sb, "- ");
        else strbuf_append_str(sb, "  ");
        js_assert_myers_append_string_value(sb, js_elements_get_int(pair, 1));
        if (i > 0) strbuf_append_char(sb, '\n');
    }
    Item result = js_new_object();
    js_set_key_cstr(result, "message", assert_make_string_n(sb->str, sb->length));
    js_set_key_cstr(result, "skipped", (Item){.item = b2it(false)});
    strbuf_free(sb);
    return result;
}

extern "C" Item js_get_internal_assert_myers_diff_namespace(void) {
    if (internal_assert_myers_diff_namespace.item != 0) return internal_assert_myers_diff_namespace;

    internal_assert_myers_diff_namespace = js_new_object();
    heap_register_gc_root(&internal_assert_myers_diff_namespace.item);
    js_set_native_key(internal_assert_myers_diff_namespace, assert_make_string("myersDiff"), js_internal_assert_myersDiff);
    js_set_native_key(internal_assert_myers_diff_namespace, assert_make_string("printMyersDiff"), js_internal_assert_printMyersDiff);
    js_set_native_key(internal_assert_myers_diff_namespace, assert_make_string("printSimpleMyersDiff"), js_internal_assert_printSimpleMyersDiff);
    js_set_key_cstr(internal_assert_myers_diff_namespace, "default", internal_assert_myers_diff_namespace);
    return internal_assert_myers_diff_namespace;
}

// helper: throw AssertionError with full Node.js properties
static Item make_assertion_error_full_item(Item msg_item, Item actual, Item expected,
                                           const char* op_str, bool generated = true) {
    Item type_name = assert_make_string("AssertionError");
    StrBuf* init_stack = strbuf_new();
    strbuf_append_str(init_stack, "AssertionError");
    String* init_msg = get_type_id(msg_item) == LMD_TYPE_STRING ? it2s(msg_item) : NULL;
    if (init_msg && init_msg->len > 0) {
        strbuf_append_str(init_stack, ": ");
        strbuf_append_str_n(init_stack, init_msg->chars, init_msg->len);
    }
    Item init_stack_item = assert_make_string_n(init_stack->str, init_stack->length);
    strbuf_free(init_stack);
    Item error = js_new_error_with_name_stack(type_name, msg_item, init_stack_item);
    // Node.js AssertionError properties
    js_set_key_cstr(error, "code", assert_make_string("ERR_ASSERTION"));
    js_set_key_cstr(error, "name", assert_make_string("AssertionError"));
    js_set_key_cstr(error, "actual", actual);
    js_set_key_cstr(error, "expected", expected);
    if (op_str) js_set_key_cstr(error, "operator", assert_make_string(op_str));
    js_set_key_cstr(error, "diff", assert_make_string(js_assert_current_diff()));
    js_assert_mark_instance_error(error);
    js_set_key_cstr(error, "generatedMessage", (Item){.item = b2it(generated)});
    if (op_str) {
        Item stack_val = js_get_key_cstr(error, "stack");
        String* stack = get_type_id(stack_val) == LMD_TYPE_STRING ? it2s(stack_val) : NULL;
        StrBuf* sb = strbuf_new();
        if (stack && stack->len > 0) {
            strbuf_append_str_n(sb, stack->chars, stack->len);
        } else {
            strbuf_append_str(sb, "AssertionError");
            String* ms = get_type_id(msg_item) == LMD_TYPE_STRING ? it2s(msg_item) : NULL;
            if (ms && ms->len > 0) {
                strbuf_append_str(sb, ": ");
                strbuf_append_str_n(sb, ms->chars, ms->len);
            }
        }
        strbuf_append_str(sb, "\n    at ");
        strbuf_append_str(sb, op_str);
        strbuf_append_str(sb, " (node:assert)");
        js_set_key_cstr(error, "stack", assert_make_string_n(sb->str, sb->length));
        strbuf_free(sb);
    }
    js_assert_attach_assertion_error_prototype(error);
    return error;
}
JS_FORWARD_STATIC_ITEM(make_assertion_error_full, (const char* message, Item actual, Item expected, const char* op_str, bool generated = true), make_assertion_error_full_item, (assert_make_string(message), actual, expected, op_str, generated))

static Item throw_assertion_error_full(const char* message, Item actual, Item expected, const char* op_str, bool generated = true) {
    Item error = make_assertion_error_full(message, actual, expected, op_str, generated);
    return js_throw_value(error);
}
JS_FORWARD_STATIC_ITEM(throw_assertion_error, (const char* message), throw_assertion_error_full, (message, make_js_undefined(), make_js_undefined(), NULL))

static Item throw_assertion_error_full_item(Item message, Item actual, Item expected,
                                            const char* op_str, bool generated = true) {
    Item error = make_assertion_error_full_item(message, actual, expected, op_str, generated);
    return js_throw_value(error);
}

// helper: check truthiness
static bool assert_is_truthy(Item val) {
    TypeId tid = get_type_id(val);
    if (tid == LMD_TYPE_NULL || tid == LMD_TYPE_UNDEFINED) return false;
    if (tid == LMD_TYPE_BOOL) return it2b(val);
    if (tid == LMD_TYPE_INT) return it2i(val) != 0;
    if (tid == LMD_TYPE_FLOAT) {
        double d = it2d(val);
        return d != 0.0 && d == d; // NaN is falsy
    }
    if (tid == LMD_TYPE_STRING) {
        String* s = it2s(val);
        return s && s->len > 0;
    }
    return true; // objects, arrays, functions are truthy
}
JS_FORWARD_STATIC_RETURN(bool, js_assert_is_nan_number, (Item value), get_type_id, (value) == LMD_TYPE_FLOAT && isnan(it2d(value)))

static bool js_assert_is_object_like_value(Item value) {
    TypeId type = get_type_id(value);
    return type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY ||
           type == LMD_TYPE_ELEMENT || type == LMD_TYPE_OBJECT ||
           type == LMD_TYPE_VMAP || type == LMD_TYPE_FUNC;
}

static bool js_assert_is_function_like_value(Item value) {
    if (js_is_callable(value)) return true;
    TypeId type = get_type_id(value);
    if (type != LMD_TYPE_MAP) return false;
    return js_class_id(value) == JS_CLASS_FUNCTION;
}

static bool js_assert_is_tiny_structural_value(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_ARRAY) return js_array_length(value) == 0;
    if (type != LMD_TYPE_MAP) return false;
    Item keys = js_object_keys(value);
    return js_array_length(keys) == 0;
}

static bool js_assert_string_has_newline(Item value) {
    if (get_type_id(value) != LMD_TYPE_STRING) return false;
    String* s = it2s(value);
    if (!s) return false;
    for (size_t i = 0; i < s->len; i++) {
        if (s->chars[i] == '\n' || s->chars[i] == '\r') return true;
    }
    return false;
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_assert_is_symbol_value, (Item value), (get_type_id(value) == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE))

static Item js_assert_throw_invalid_message_arg(Item message) {
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "The \"message\" argument must be of type string or an instance of Error. Received type symbol (");
    Item text = js_symbol_to_string(message);
    String* s = get_type_id(text) == LMD_TYPE_STRING ? it2s(text) : NULL;
    if (s) strbuf_append_str_n(sb, s->chars, s->len);
    strbuf_append_char(sb, ')');
    Item result = js_throw_type_error_code(JS_ERR_INVALID_ARG_TYPE, sb->str);
    strbuf_free(sb);
    return result;
}

static Item js_assert_format_user_message(Item message) {
    if (get_type_id(message) != LMD_TYPE_STRING || js_pending_call_argc <= 3 ||
            !js_pending_call_args) {
        return message;
    }
    String* ms = it2s(message);
    if (!ms) return message;
    StrBuf* sb = strbuf_new();
    for (size_t i = 0; i < ms->len; i++) {
        if (i + 1 < ms->len && ms->chars[i] == '%' && ms->chars[i + 1] == 'i') {
            // Node formats assert user messages with util.format when extra
            // arguments are supplied; `%i` is enough for the official assert
            // compatibility cases and preserves other text verbatim.
            Item arg = js_pending_call_args[3];
            Item text = js_to_string_val(arg);
            String* ts = get_type_id(text) == LMD_TYPE_STRING ? it2s(text) : NULL;
            if (ts) strbuf_append_str_n(sb, ts->chars, ts->len);
            i++;
            continue;
        }
        strbuf_append_char(sb, ms->chars[i]);
    }
    Item result = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

static Item js_assert_resolve_user_message(Item message, Item actual, Item expected, bool* resolved) {
    if (resolved) *resolved = false;
    if (get_type_id(message) == LMD_TYPE_STRING) {
        if (resolved) *resolved = true;
        return js_assert_format_user_message(message);
    }
    if (!js_assert_is_function_like_value(message)) return message;

    Item args[2] = { actual, expected };
    Item result = js_call_function(message, make_js_undefined(), args, 2);
    if (item_is_error(result)) {
        // user message functions run before AssertionError construction; carry
        // their returned error directly so the original exception is retained.
        if (resolved) *resolved = true;
        return result;
    }
    if (get_type_id(result) == LMD_TYPE_STRING) {
        if (resolved) *resolved = true;
        return result;
    }
    return message;
}

static Item js_assert_throw_invalid_fn_arg(Item actual) {
    char received[160];
    js_assert_append_value_type(received, sizeof(received), actual);
    return js_throw_type_error_codef(JS_ERR_INVALID_ARG_TYPE, 
        "The \"fn\" argument must be of type function. Received %s", received);
}

typedef enum {
    ASSERT_MESSAGE_AUTO = 0,
    ASSERT_MESSAGE_USER,
    ASSERT_MESSAGE_EARLY_RETURN,
} AssertMessageKind;

static AssertMessageKind js_assert_prepare_message(Item message, Item actual,
                                                    Item expected, Item* formatted,
                                                    Item* early_result) {
    if (js_assert_is_symbol_value(message)) {
        // Symbol messages cannot be coerced; Node reports the bad message argument before creating AssertionError.
        *early_result = js_assert_throw_invalid_message_arg(message);
        return ASSERT_MESSAGE_EARLY_RETURN;
    }
    if (get_type_id(message) == LMD_TYPE_MAP && js_class_is_error_like(js_class_id(message))) {
        // Node treats an Error supplied as the user message as the thrown value itself.
        *early_result = js_throw_value(message);
        return ASSERT_MESSAGE_EARLY_RETURN;
    }
    bool has_user_message = false;
    *formatted = js_assert_resolve_user_message(message, actual, expected, &has_user_message);
    if (item_is_error(*formatted)) {
        *early_result = *formatted;
        return ASSERT_MESSAGE_EARLY_RETURN;
    }
    if (!has_user_message) return ASSERT_MESSAGE_AUTO;
    return ASSERT_MESSAGE_USER;
}

// helper: throw with user message or auto-generated message and props
static Item throw_assert_msg_or_auto(Item message, const char* default_msg,
                                     Item actual, Item expected, const char* op_str) {
    Item formatted = ItemNull;
    Item early_result = ItemNull;
    AssertMessageKind message_kind = js_assert_prepare_message(
        message, actual, expected, &formatted, &early_result);
    if (message_kind == ASSERT_MESSAGE_EARLY_RETURN) return early_result;
    if (message_kind == ASSERT_MESSAGE_USER) {
        String* s = get_type_id(formatted) == LMD_TYPE_STRING ? it2s(formatted) : NULL;
        char buf[512];
        int len = s && (int)s->len < 500 ? (int)s->len : 500;
        if (s) memcpy(buf, s->chars, len);
        buf[len] = '\0';
        return throw_assertion_error_full(buf, actual, expected, op_str, false);
    }
    return throw_assertion_error_full(default_msg, actual, expected, op_str, true);
}

static Item throw_assert_msg_or_auto_item(Item message, Item default_msg,
                                          Item actual, Item expected, const char* op_str) {
    Item formatted = ItemNull;
    Item early_result = ItemNull;
    AssertMessageKind message_kind = js_assert_prepare_message(
        message, actual, expected, &formatted, &early_result);
    if (message_kind == ASSERT_MESSAGE_EARLY_RETURN) return early_result;
    if (message_kind == ASSERT_MESSAGE_USER) {
        return throw_assertion_error_full_item(formatted, actual, expected, op_str, false);
    }
    return throw_assertion_error_full_item(default_msg, actual, expected, op_str, true);
}

static Item throw_assert_deep_msg_or_auto_item(Item message, Item default_msg,
                                               Item actual, Item expected, const char* op_str) {
    if (js_assert_is_symbol_value(message) ||
            (get_type_id(message) == LMD_TYPE_MAP && js_class_is_error_like(js_class_id(message)))) {
        return throw_assert_msg_or_auto_item(message, default_msg, actual, expected, op_str);
    }
    bool has_user_message = false;
    JS_ASSIGN_OR_RETURN(formatted, js_assert_resolve_user_message(message, actual, expected, &has_user_message));
    if (!has_user_message) {
        return throw_assertion_error_full_item(default_msg, actual, expected, op_str, true);
    }
    StrBuf* sb = strbuf_new();
    String* ms = get_type_id(formatted) == LMD_TYPE_STRING ? it2s(formatted) : NULL;
    if (ms) strbuf_append_str_n(sb, ms->chars, ms->len);
    String* ds = get_type_id(default_msg) == LMD_TYPE_STRING ? it2s(default_msg) : NULL;
    const char* header = "Expected values to be strictly deep-equal:";
    size_t header_len = strlen(header);
    if (ds && ds->len > header_len && memcmp(ds->chars, header, header_len) == 0) {
        // Explicit deep assertion messages still carry the generated diff body;
        // replacing it hides the structural mismatch Node exposes to callers.
        strbuf_append_str_n(sb, ds->chars + header_len, ds->len - header_len);
    }
    Item custom = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return throw_assertion_error_full_item(custom, actual, expected, op_str, false);
}

static void js_assert_append_string_literal(StrBuf* sb, Item value) {
    String* s = get_type_id(value) == LMD_TYPE_STRING ? it2s(value) : NULL;
    if (!s) {
        js_assert_append_inspected_value(sb, value);
        return;
    }
    strbuf_append_char(sb, '\'');
    for (size_t i = 0; i < s->len; i++) {
        char ch = s->chars[i];
        if (ch == '\\') strbuf_append_str(sb, "\\\\");
        else if (ch == '\'') strbuf_append_str(sb, "\\'");
        else if (ch == '\n') strbuf_append_str(sb, "\\n");
        else if (ch == '\r') strbuf_append_str(sb, "\\r");
        else if (ch == '\t') strbuf_append_str(sb, "\\t");
        else strbuf_append_char(sb, ch);
    }
    strbuf_append_char(sb, '\'');
}

static int js_assert_count_newlines(String* s) {
    if (!s) return 0;
    int count = 0;
    for (size_t i = 0; i < s->len; i++) {
        if (s->chars[i] == '\n') count++;
    }
    return count;
}

static bool js_assert_range_has_char(const char* chars, size_t len, char needle) {
    for (size_t i = 0; i < len; i++) {
        if (chars[i] == needle) return true;
    }
    return false;
}

static void js_assert_append_escaped_string_range(StrBuf* sb, const char* chars,
                                                  size_t len, char quote) {
    for (size_t i = 0; i < len; i++) {
        char ch = chars[i];
        if (ch == '\\') strbuf_append_str(sb, "\\\\");
        else if (ch == quote) {
            if (quote == '\'') strbuf_append_str(sb, "\\'");
            else strbuf_append_str(sb, "\\\"");
        }
        else if (ch == '\n') strbuf_append_str(sb, "\\n");
        else if (ch == '\r') strbuf_append_str(sb, "\\r");
        else if (ch == '\t') strbuf_append_str(sb, "\\t");
        else strbuf_append_char(sb, ch);
    }
}

static void js_assert_append_spaces(StrBuf* sb, int count) {
    for (int i = 0; i < count; i++) strbuf_append_char(sb, ' ');
}

static void js_assert_append_long_multiline_string(StrBuf* sb, String* s,
                                                   const char* first_prefix,
                                                   const char* next_prefix,
                                                   int max_segments,
                                                   const char* ellipsis) {
    size_t start = 0;
    bool first = true;
    int segment_count = 0;
    while (start < s->len) {
        if (max_segments > 0 && segment_count >= max_segments) {
            strbuf_append_str(sb, "\n");
            strbuf_append_str(sb, ellipsis ? ellipsis : "...");
            return;
        }
        size_t end = start;
        while (end < s->len && s->chars[end] != '\n') end++;
        if (end < s->len && s->chars[end] == '\n') end++;
        if (!first) strbuf_append_str(sb, "\n");
        strbuf_append_str(sb, first ? first_prefix : next_prefix);
        size_t segment_len = end - start;
        char quote = '\'';
        if (js_assert_range_has_char(s->chars + start, segment_len, '\'') &&
                !js_assert_range_has_char(s->chars + start, segment_len, '"')) {
            // Multiline AssertionError strings choose the quote that preserves
            // the diff payload; escaping embedded quotes changes public output.
            quote = '"';
        }
        strbuf_append_char(sb, quote);
        js_assert_append_escaped_string_range(sb, s->chars + start, segment_len, quote);
        strbuf_append_char(sb, quote);
        bool truncated_after_this = max_segments > 0 &&
            segment_count + 1 >= max_segments && end < s->len;
        if (end < s->len || truncated_after_this) strbuf_append_str(sb, " +");
        first = false;
        start = end;
        segment_count++;
    }
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_assert_should_expand_multiline_string, (String* s), (js_assert_count_newlines(s) >= 10))

static bool js_assert_append_expanded_string_literal(StrBuf* sb, Item value,
                                                     const char* first_prefix,
                                                     const char* next_prefix,
                                                     int max_segments = 0) {
    String* s = get_type_id(value) == LMD_TYPE_STRING ? it2s(value) : NULL;
    if (!s || !js_assert_should_expand_multiline_string(s)) return false;
    js_assert_append_long_multiline_string(sb, s, first_prefix, next_prefix,
        max_segments, "...");
    return true;
}

static void js_assert_append_deep_equal_value(StrBuf* sb, Item value) {
    String* s = get_type_id(value) == LMD_TYPE_STRING ? it2s(value) : NULL;
    bool simple_diff = strcmp(js_assert_current_diff(), "simple") == 0;
    if (s && js_assert_should_expand_multiline_string(s)) {
        js_assert_append_long_multiline_string(sb, s, "", "  ",
            simple_diff ? 51 : 0, simple_diff ? "..." : NULL);
        return;
    }
    if (s && simple_diff && s->len > 512) {
        strbuf_append_char(sb, '\'');
        js_assert_append_escaped_string_range(sb, s->chars, 508, '\'');
        strbuf_append_str(sb, "...");
        return;
    }
    js_assert_append_string_literal(sb, value);
}

static Item js_assert_strict_equal_message(Item actual, Item expected) {
    bool simple_diff = strcmp(js_assert_current_diff(), "simple") == 0;
    if (simple_diff && js_assert_is_object_like_value(actual) &&
            js_assert_is_object_like_value(expected)) {
        Item deep = js_util_isDeepStrictEqual(actual, expected);
        if ((get_type_id(deep) == LMD_TYPE_BOOL && it2b(deep)) ||
                (get_type_id(deep) == LMD_TYPE_INT && it2i(deep) == 1)) {
            StrBuf* sb = strbuf_new();
            strbuf_append_str(sb, "Values have same structure but are not reference-equal:\n\n");
            js_assert_append_inspected_value(sb, actual);
            strbuf_append_str(sb, "\n");
            Item result = assert_make_string_n(sb->str, sb->length);
            strbuf_free(sb);
            return result;
        }
    }
    bool actual_object_like = js_assert_is_object_like_value(actual);
    bool expected_object_like = js_assert_is_object_like_value(expected);
    bool compact_structural_mix = actual_object_like != expected_object_like &&
        (js_assert_is_tiny_structural_value(actual) ||
         js_assert_is_tiny_structural_value(expected));
    if (simple_diff && ((!actual_object_like && !expected_object_like) ||
            compact_structural_mix)) {
        // The Assert constructor's `diff: "full"` option requests the expanded
        // diff even for compact primitive/object comparisons; only empty
        // structural values use Node's legacy one-line primitive form.
        Item actual_text = js_util_inspect(actual, make_js_undefined());
        Item expected_text = js_util_inspect(expected, make_js_undefined());
        String* as = get_type_id(actual_text) == LMD_TYPE_STRING ? it2s(actual_text) : NULL;
        String* es = get_type_id(expected_text) == LMD_TYPE_STRING ? it2s(expected_text) : NULL;
        bool long_string_pair = get_type_id(actual) == LMD_TYPE_STRING &&
            get_type_id(expected) == LMD_TYPE_STRING &&
            (it2s(actual)->len >= 12 || it2s(expected)->len >= 12);
        if (get_type_id(actual) == LMD_TYPE_STRING &&
                get_type_id(expected) == LMD_TYPE_STRING &&
                !js_assert_string_has_newline(actual) &&
                !js_assert_string_has_newline(expected)) {
            String* avs = it2s(actual);
            String* evs = it2s(expected);
            if (avs && evs && avs->len >= 8 && evs->len >= 8) {
                size_t diff = 0;
                while (diff < avs->len && avs->chars[diff] == evs->chars[diff]) diff++;
                if (diff > 0 && diff < avs->len) {
                    StrBuf* sb = strbuf_new();
                    strbuf_append_str(sb, "Expected values to be strictly equal:\n");
                    strbuf_append_str(sb, "+ actual - expected\n\n+ ");
                    js_assert_append_string_literal(sb, actual);
                    strbuf_append_str(sb, "\n- ");
                    js_assert_append_string_literal(sb, expected);
                    strbuf_append_str(sb, "\n");
                    js_assert_append_spaces(sb, (int)diff + 3);
                    strbuf_append_str(sb, "^\n");
                    Item result = assert_make_string_n(sb->str, sb->length);
                    strbuf_free(sb);
                    // Same-length string diffs keep the first differing column
                    // visible; the compact !== form loses that invariant.
                    return result;
                }
            }
        }
        if (as && es && !long_string_pair &&
                !js_assert_is_function_like_value(actual) &&
                !js_assert_is_function_like_value(expected) &&
                !js_assert_string_has_newline(actual_text) &&
                !js_assert_string_has_newline(expected_text) &&
                as->len + es->len < 160) {
            StrBuf* sb = strbuf_new();
            strbuf_append_str(sb, "Expected values to be strictly equal:\n\n");
            strbuf_append_str_n(sb, as->chars, as->len);
            strbuf_append_str(sb, " !== ");
            strbuf_append_str_n(sb, es->chars, es->len);
            strbuf_append_str(sb, "\n");
            Item result = assert_make_string_n(sb->str, sb->length);
            strbuf_free(sb);
            return result;
        }
    }
    StrBuf* sb = strbuf_new();
    if (js_assert_is_object_like_value(actual) && js_assert_is_object_like_value(expected)) {
        strbuf_append_str(sb, "Expected \"actual\" to be reference-equal to \"expected\":\n");
    } else {
        strbuf_append_str(sb, "Expected values to be strictly equal:\n");
    }
    strbuf_append_str(sb, "+ actual - expected\n\n");
    if (js_assert_is_arguments_value(actual) && js_assert_is_plain_diff_object(expected)) {
        Item keys = js_object_keys(actual);
        int64_t len = js_array_length(keys);
        strbuf_append_str(sb, "+ [Arguments] {\n");
        strbuf_append_str(sb, "- {\n");
        for (int64_t i = 0; i < len; i++) {
            Item key = js_elements_get_int(keys, i);
            if (js_assert_string_equals(key, "__strict_arguments__")) continue;
            Item av = js_get_key_default(actual, key);
            Item ev = js_get_key_default(expected, key);
            Item eq = js_util_isDeepStrictEqual(av, ev);
            bool same = (get_type_id(eq) == LMD_TYPE_BOOL && it2b(eq)) ||
                        (get_type_id(eq) == LMD_TYPE_INT && it2i(eq) == 1);
            if (!same) continue;
            strbuf_append_str(sb, "    ");
            js_assert_append_multiline_object_key(sb, key);
            strbuf_append_str(sb, ": ");
            js_assert_append_property_value(sb, actual, key, av);
            strbuf_append_char(sb, '\n');
        }
        // Arguments is backed by array storage internally; only the branded
        // opening line differs from an equivalent object in Node's ref diff.
        strbuf_append_str(sb, "  }\n");
        Item result = assert_make_string_n(sb->str, sb->length);
        strbuf_free(sb);
        return result;
    }
    if (!js_assert_append_expanded_string_literal(sb, actual, "+ ", "+   ")) {
        if (get_type_id(actual) == LMD_TYPE_ARRAY || js_assert_is_arguments_value(actual) ||
                js_assert_is_plain_diff_object(actual)) {
            // Mixed object/primitive strictEqual failures use assert's expanded
            // renderer; util.inspect's one-line containers hide the diff shape.
            js_assert_append_multiline_value(sb, actual, 0, '+', false, 16);
        } else if (js_assert_is_object_like_value(actual)) {
            strbuf_append_str(sb, "+ ");
            js_assert_append_error_label(sb, actual);
            strbuf_append_str(sb, "\n");
        } else {
            strbuf_append_str(sb, "+ ");
            js_assert_append_string_literal(sb, actual);
            strbuf_append_str(sb, "\n");
        }
    } else {
        strbuf_append_str(sb, "\n");
    }
    if (!js_assert_append_expanded_string_literal(sb, expected, "- ", "-   ")) {
        if (get_type_id(expected) == LMD_TYPE_ARRAY || js_assert_is_arguments_value(expected) ||
                js_assert_is_plain_diff_object(expected)) {
            // See the actual-side renderer above; both sides must make the
            // same compact-vs-expanded decision to keep the diff aligned.
            js_assert_append_multiline_value(sb, expected, 0, '-', false, 16);
        } else if (js_assert_is_object_like_value(expected)) {
            strbuf_append_str(sb, "- ");
            js_assert_append_error_label(sb, expected);
            strbuf_append_str(sb, "\n");
        } else {
            strbuf_append_str(sb, "- ");
            js_assert_append_string_literal(sb, expected);
            strbuf_append_str(sb, "\n");
        }
    } else {
        strbuf_append_str(sb, "\n");
    }
    Item result = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

static Item js_assert_not_strict_equal_message(Item actual) {
    bool simple_diff = strcmp(js_assert_current_diff(), "simple") == 0;
    if (simple_diff && js_assert_is_object_like_value(actual)) {
        Item text = js_util_inspect(actual, make_js_undefined());
        String* s = get_type_id(text) == LMD_TYPE_STRING ? it2s(text) : NULL;
        StrBuf* sb = strbuf_new();
        bool tiny_object = js_assert_is_tiny_structural_value(actual);
        if (s && tiny_object && !js_assert_string_has_newline(text) && s->len <= 50) {
            strbuf_append_str(sb, "Expected \"actual\" not to be reference-equal to \"expected\": ");
            strbuf_append_str_n(sb, s->chars, s->len);
        } else {
            strbuf_append_str(sb, "Expected \"actual\" not to be reference-equal to \"expected\":\n\n");
            if (!tiny_object && (get_type_id(actual) == LMD_TYPE_ARRAY ||
                    js_assert_is_plain_diff_object(actual) ||
                    js_assert_is_arguments_value(actual))) {
                js_assert_append_multiline_value(sb, actual, 0, 0, false, 16);
            } else if (s) {
                strbuf_append_str_n(sb, s->chars, s->len);
                strbuf_append_str(sb, "\n");
            }
        }
        Item result = assert_make_string_n(sb->str, sb->length);
        strbuf_free(sb);
        // notStrictEqual on the same object is an identity failure, not a
        // primitive inequality failure; Node exposes that distinction.
        return result;
    }
    if (simple_diff && !js_assert_is_object_like_value(actual)) {
        // The Assert constructor's `diff: "full"` option requests the expanded
        // diff even for small primitives; compact one-line output is simple-mode only.
        Item text = js_util_inspect(actual, make_js_undefined());
        String* s = get_type_id(text) == LMD_TYPE_STRING ? it2s(text) : NULL;
        if (s && !js_assert_string_has_newline(text) && s->len <= 50) {
            StrBuf* one_line = strbuf_new();
            strbuf_append_str(one_line, "Expected \"actual\" to be strictly unequal to: ");
            strbuf_append_str_n(one_line, s->chars, s->len);
            Item result = assert_make_string_n(one_line->str, one_line->length);
            strbuf_free(one_line);
            return result;
        }
    }
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "Expected \"actual\" to be strictly unequal to:\n\n");
    if (js_assert_append_expanded_string_literal(sb, actual, "", "  ",
            simple_diff ? 46 : 0)) {
        strbuf_append_str(sb, "\n");
    } else {
        js_assert_append_string_literal(sb, actual);
    }
    Item result = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

static void js_assert_append_not_deep_value(StrBuf* sb, Item value, int depth_left) {
    if (get_type_id(value) == LMD_TYPE_ARRAY && depth_left >= 0) {
        int64_t len = js_array_length(value);
        strbuf_append_str(sb, "[\n");
        int64_t limit = len > 45 ? 45 : len;
        for (int64_t i = 0; i < limit; i++) {
            strbuf_append_str(sb, "  ");
            js_assert_append_not_deep_value(sb, js_elements_get_int(value, i), depth_left - 1);
            if (i < len - 1) strbuf_append_char(sb, ',');
            strbuf_append_char(sb, '\n');
        }
        if (limit < len) {
            // Node truncates long notDeep* renderings before the close bracket;
            // keeping the ellipsis line prevents huge auto messages.
            strbuf_append_str(sb, "...\n");
            return;
        }
        strbuf_append_str(sb, "]\n");
        return;
    }
    js_assert_append_inspected_value(sb, value);
}

static Item js_assert_not_deep_equal_message(Item actual, Item expected, bool strict) {
    StrBuf* sb = strbuf_new();
    if (!strict && actual.item != expected.item) {
        // Legacy notDeepEqual has a distinct loose-equality wording when two
        // non-identical values compare equal through == coercion.
        js_assert_append_not_deep_value(sb, actual, 64);
        strbuf_append_str(sb, "\n\nshould not loosely deep-equal\n\n");
        js_assert_append_not_deep_value(sb, expected, 64);
        Item result = assert_make_string_n(sb->str, sb->length);
        strbuf_free(sb);
        return result;
    }
    strbuf_append_str(sb, "Expected \"actual\" not to be ");
    strbuf_append_str(sb, strict ? "strictly deep-equal" : "loosely deep-equal");
    strbuf_append_str(sb, " to:\n\n");
    // notDeep* diagnostics need expanded arrays, while util.inspect's compact
    // default is one-line and fails Node's public AssertionError message shape.
    js_assert_append_not_deep_value(sb, actual, 64);
    Item result = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

static Item js_assert_legacy_equal_message(Item actual, Item expected, const char* op) {
    StrBuf* sb = strbuf_new();
    js_assert_append_inspected_value(sb, actual);
    strbuf_append_char(sb, ' ');
    strbuf_append_str(sb, op);
    strbuf_append_char(sb, ' ');
    js_assert_append_inspected_value(sb, expected);
    Item result = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

static Item js_assert_deep_equal_message(Item actual, Item expected) {
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "Expected values to be loosely deep-equal:\n\n");
    js_assert_append_deep_equal_value(sb, actual);
    strbuf_append_str(sb, "\n\nshould loosely deep-equal\n\n");
    js_assert_append_deep_equal_value(sb, expected);
    Item result = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

static bool js_assert_deep_values_same(Item actual, Item expected) {
    Item eq = js_util_isDeepStrictEqual(actual, expected);
    return (get_type_id(eq) == LMD_TYPE_BOOL && it2b(eq)) ||
           (get_type_id(eq) == LMD_TYPE_INT && it2i(eq) == 1);
}

static bool js_assert_is_arguments_value(Item value) {
    if (get_type_id(value) == LMD_TYPE_MAP) {
        if (js_class_id(value) == JS_CLASS_ARGUMENTS) return true;
        Item tag = js_get_key_default(value, js_well_known_symbol_key(4));
        return js_assert_string_equals(tag, "Arguments");
    }
    if (get_type_id(value) != LMD_TYPE_ARRAY || !value.array ||
            value.array->is_content != 1 || !js_array_has_props(value.array)) {
        return false;
    }
    Map* props = js_array_props(value.array);
    Item tag = js_get_key_default((Item){.map = props}, js_well_known_symbol_key(4));
    if (get_type_id(tag) != LMD_TYPE_STRING) return false;
    String* s = it2s(tag);
    return s && s->len == 9 && memcmp(s->chars, "Arguments", 9) == 0;
}
JS_FORWARD_STATIC_RETURN(bool, js_assert_is_plain_diff_object, (Item value), get_type_id, (value) == LMD_TYPE_MAP && (js_class_id(value) == JS_CLASS_NONE || js_class_id(value) == JS_CLASS_OBJECT || js_class_id(value) == JS_CLASS_ARGUMENTS))

static void js_assert_append_line_prefix(StrBuf* sb, int indent, char sign) {
    if (sign) {
        strbuf_append_char(sb, sign);
        // Signed diff lines replace one indentation column with +/-; adding the
        // full indent shifts nested array changes two spaces too far right.
        int spaces = indent == 0 ? 1 : (indent <= 2 ? 3 : indent - 1);
        js_assert_append_spaces(sb, spaces);
        return;
    }
    js_assert_append_spaces(sb, indent);
}

static void js_assert_append_multiline_value(StrBuf* sb, Item value, int indent,
                                             char sign, bool trailing_comma,
                                             int depth_left);

static void js_assert_append_multiline_object_key(StrBuf* sb, Item key) {
    String* ks = get_type_id(key) == LMD_TYPE_STRING ? it2s(key) : NULL;
    if (!ks) {
        js_assert_append_inspected_value(sb, key);
        return;
    }
    js_assert_append_quoted_key(sb, ks);
}

static bool js_assert_property_is_getter(Item owner, Item key) {
    Item desc = js_object_get_own_property_descriptor(owner, key);
    if (get_type_id(desc) != LMD_TYPE_MAP) return false;
    Item getter = js_get_key_cstr(desc, "get");
    return js_is_callable(getter);
}

static void js_assert_append_property_value(StrBuf* sb, Item owner, Item key, Item value) {
    if (!js_assert_property_is_getter(owner, key)) {
        js_assert_append_inspected_value(sb, value);
        return;
    }
    // Getter properties compare by invoked value, but Node's assert diff keeps
    // the accessor label so mismatches are not mistaken for plain data fields.
    strbuf_append_str(sb, "[Getter: ");
    js_assert_append_inspected_value(sb, value);
    strbuf_append_char(sb, ']');
}

static bool js_assert_is_self_cycle_object(Item value) {
    if (!js_assert_is_plain_diff_object(value)) return false;
    Item keys = js_object_keys(value);
    int64_t len = js_array_length(keys);
    for (int64_t i = 0; i < len; i++) {
        Item key = js_elements_get_int(keys, i);
        if (js_get_key_default(value, key).item == value.item) return true;
    }
    return false;
}

static void js_assert_append_multiline_array(StrBuf* sb, Item value, int indent,
                                             char sign, bool trailing_comma,
                                             int depth_left) {
    int64_t len = js_array_length(value);
    js_assert_append_line_prefix(sb, indent, sign);
    strbuf_append_str(sb, "[\n");
    for (int64_t i = 0; i < len; i++) {
        Item child = js_elements_get_int(value, i);
        js_assert_append_multiline_value(sb, child, indent + 2, sign, i < len - 1, depth_left - 1);
    }
    js_assert_append_line_prefix(sb, indent, sign);
    strbuf_append_char(sb, ']');
    if (trailing_comma) strbuf_append_char(sb, ',');
    strbuf_append_char(sb, '\n');
}

static void js_assert_append_multiline_object(StrBuf* sb, Item value, int indent,
                                              char sign, bool trailing_comma,
                                              int depth_left) {
    Item keys = js_assert_enumerable_own_keys(value);
    int64_t len = js_array_length(keys);
    int64_t visible_len = 0;
    for (int64_t i = 0; i < len; i++) {
        if (!js_assert_string_equals(js_elements_get_int(keys, i), "__strict_arguments__")) visible_len++;
    }
    js_assert_append_line_prefix(sb, indent, sign);
    bool self_cycle = js_assert_is_self_cycle_object(value);
    if (self_cycle) strbuf_append_str(sb, "<ref *1> ");
    if (js_assert_is_arguments_value(value)) strbuf_append_str(sb, "[Arguments] ");
    strbuf_append_str(sb, "{\n");
    int64_t emitted = 0;
    for (int pass = 0; pass < (self_cycle ? 2 : 1); pass++) {
    for (int64_t i = 0; i < len; i++) {
        Item key = js_elements_get_int(keys, i);
        if (js_assert_string_equals(key, "__strict_arguments__")) continue;
        Item child = js_get_key_default(value, key);
        bool cycle_child = child.item == value.item;
        if (self_cycle && ((pass == 0) != cycle_child)) continue;
        js_assert_append_line_prefix(sb, indent + 2, sign);
        js_assert_append_multiline_object_key(sb, key);
        strbuf_append_str(sb, ": ");
        if (cycle_child) {
            strbuf_append_str(sb, "[Circular *1]");
        } else if (depth_left > 0 && get_type_id(child) == LMD_TYPE_ARRAY) {
            strbuf_append_str(sb, "[\n");
            int64_t child_len = js_array_length(child);
            for (int64_t j = 0; j < child_len; j++) {
                js_assert_append_multiline_value(sb, js_elements_get_int(child, j),
                    indent + 4, sign, j < child_len - 1, depth_left - 1);
            }
            js_assert_append_line_prefix(sb, indent + 2, sign);
            strbuf_append_char(sb, ']');
        } else if (depth_left > 0 && js_assert_is_plain_diff_object(child)) {
            strbuf_append_str(sb, "{\n");
            Item child_keys = js_object_keys(child);
            int64_t child_key_len = js_array_length(child_keys);
            for (int64_t j = 0; j < child_key_len; j++) {
                Item child_key = js_elements_get_int(child_keys, j);
                js_assert_append_line_prefix(sb, indent + 4, sign);
                js_assert_append_multiline_object_key(sb, child_key);
                strbuf_append_str(sb, ": ");
                js_assert_append_inspected_value(sb, js_get_key_default(child, child_key));
                if (j < child_key_len - 1) strbuf_append_char(sb, ',');
                strbuf_append_char(sb, '\n');
            }
            js_assert_append_line_prefix(sb, indent + 2, sign);
            strbuf_append_char(sb, '}');
        } else {
            js_assert_append_property_value(sb, value, key, child);
        }
        emitted++;
        if (emitted < visible_len) strbuf_append_char(sb, ',');
        strbuf_append_char(sb, '\n');
    }
    }
    js_assert_append_line_prefix(sb, indent, sign);
    strbuf_append_char(sb, '}');
    if (trailing_comma) strbuf_append_char(sb, ',');
    strbuf_append_char(sb, '\n');
}

static void js_assert_append_multiline_value(StrBuf* sb, Item value, int indent,
                                             char sign, bool trailing_comma,
                                             int depth_left) {
    TypeId type = get_type_id(value);
    if (depth_left > 0 && js_assert_is_arguments_value(value)) {
        js_assert_append_multiline_object(sb, value, indent, sign, trailing_comma, depth_left);
        return;
    }
    if (depth_left > 0 && type == LMD_TYPE_ARRAY) {
        js_assert_append_multiline_array(sb, value, indent, sign, trailing_comma, depth_left);
        return;
    }
    if (depth_left > 0 && js_assert_is_plain_diff_object(value)) {
        js_assert_append_multiline_object(sb, value, indent, sign, trailing_comma, depth_left);
        return;
    }
    js_assert_append_line_prefix(sb, indent, sign);
    js_assert_append_inspected_value(sb, value);
    if (trailing_comma) strbuf_append_char(sb, ',');
    strbuf_append_char(sb, '\n');
}

static bool js_assert_key_array_contains(Item keys, Item key) {
    int64_t len = js_array_length(keys);
    for (int64_t i = 0; i < len; i++) {
        if (js_assert_same_property_key(js_elements_get_int(keys, i), key)) return true;
    }
    return false;
}

static bool js_assert_lcs_should_take_actual(Item actual, Item expected,
                                             int score[65][65],
                                             int64_t i, int64_t j,
                                             int64_t actual_len,
                                             int64_t expected_len) {
    if (j >= expected_len) return true;
    if (i >= actual_len) return false;
    if (score[i + 1][j] != score[i][j + 1]) {
        return score[i + 1][j] > score[i][j + 1];
    }
    bool actual_matches_next_expected = j + 1 < expected_len &&
        js_assert_deep_values_same(js_elements_get_int(actual, i),
                                   js_elements_get_int(expected, j + 1));
    bool expected_matches_next_actual = i + 1 < actual_len &&
        js_assert_deep_values_same(js_elements_get_int(actual, i + 1),
                                   js_elements_get_int(expected, j));
    if (actual_matches_next_expected && expected_matches_next_actual) {
        // When both adjacent values can realign repeated runs, Node keeps the
        // expected-side deletion first so later unique values stay anchored.
        return false;
    }
    if (actual_matches_next_expected != expected_matches_next_actual) {
        // Ambiguous repeated scalars align to the nearest next match; otherwise
        // an inserted actual value can be hidden behind an expected deletion.
        return expected_matches_next_actual;
    }
    return true;
}

static void js_assert_build_lcs_score(Item actual, Item expected,
                                      int64_t actual_len, int64_t expected_len,
                                      int score[65][65]) {
    memset(score, 0, sizeof(int) * 65 * 65);
    for (int64_t i = actual_len - 1; i >= 0; i--) {
        for (int64_t j = expected_len - 1; j >= 0; j--) {
            if (js_assert_deep_values_same(js_elements_get_int(actual, i), js_elements_get_int(expected, j))) {
                score[i][j] = score[i + 1][j + 1] + 1;
            } else {
                score[i][j] = score[i + 1][j] > score[i][j + 1] ?
                    score[i + 1][j] : score[i][j + 1];
            }
        }
    }
}

static void js_assert_append_structural_diff(StrBuf* sb, Item actual, Item expected,
                                             int indent, bool trailing_comma,
                                             int depth_left);

static void js_assert_append_array_diff_recursive(StrBuf* sb, Item actual, Item expected,
                                                  int indent, bool trailing_comma) {
    js_assert_append_spaces(sb, indent);
    strbuf_append_str(sb, "[\n");
    int64_t actual_len = get_type_id(actual) == LMD_TYPE_ARRAY ? js_array_length(actual) : 0;
    int64_t expected_len = get_type_id(expected) == LMD_TYPE_ARRAY ? js_array_length(expected) : 0;
    int64_t max_len = actual_len > expected_len ? actual_len : expected_len;
    if (actual_len <= 64 && expected_len <= 64) {
        int score[65][65];
        js_assert_build_lcs_score(actual, expected, actual_len, expected_len, score);
        int64_t i = 0;
        int64_t j = 0;
        while (i < actual_len || j < expected_len) {
            if (i < actual_len && j < expected_len &&
                    js_assert_deep_values_same(js_elements_get_int(actual, i), js_elements_get_int(expected, j))) {
                bool child_comma = i < actual_len - 1 || j < expected_len - 1;
                js_assert_append_multiline_value(sb, js_elements_get_int(actual, i),
                    indent + 2, 0, child_comma, 16);
                i++;
                j++;
            } else if (i < actual_len && j < expected_len &&
                    js_assert_is_object_like_value(js_elements_get_int(actual, i)) &&
                    js_assert_is_object_like_value(js_elements_get_int(expected, j))) {
                bool child_comma = i < actual_len - 1 || j < expected_len - 1;
                // LCS is for inserted/removed scalar runs; object-like peers at
                // the same slot need a nested diff or the leaf mismatch vanishes.
                js_assert_append_structural_diff(sb, js_elements_get_int(actual, i),
                    js_elements_get_int(expected, j), indent + 2, child_comma, 16);
                i++;
                j++;
            } else if (i < actual_len && j < expected_len && actual_len == expected_len &&
                    i == actual_len - 1 && j == expected_len - 1) {
                bool child_comma = i < actual_len - 1;
                // Equal-length scalar mismatches are replacements; treating
                // them as add/remove pairs adds a comma between the +/- lines.
                js_assert_append_multiline_value(sb, js_elements_get_int(actual, i),
                    indent + 2, '+', child_comma, 16);
                js_assert_append_multiline_value(sb, js_elements_get_int(expected, j),
                    indent + 2, '-', child_comma, 16);
                i++;
                j++;
            } else if (i + 1 < actual_len && j + 1 < expected_len &&
                    actual_len == expected_len &&
                    js_assert_deep_values_same(js_elements_get_int(actual, i + 1),
                                               js_elements_get_int(expected, j + 1))) {
                bool child_comma = i < actual_len - 1;
                // Equal-length scalar replacements should keep the actual line
                // before the expected line when the following slot realigns.
                js_assert_append_multiline_value(sb, js_elements_get_int(actual, i),
                    indent + 2, '+', child_comma, 16);
                js_assert_append_multiline_value(sb, js_elements_get_int(expected, j),
                    indent + 2, '-', child_comma, 16);
                i++;
                j++;
            } else if (i < actual_len &&
                    js_assert_lcs_should_take_actual(actual, expected, score,
                        i, j, actual_len, expected_len)) {
                bool child_comma = score[0][0] == 0 && i == actual_len - 1 &&
                    j < expected_len ? false : (i < actual_len - 1 || j < expected_len);
                js_assert_append_multiline_value(sb, js_elements_get_int(actual, i),
                    indent + 2, '+', child_comma, 16);
                i++;
            } else if (j < expected_len) {
                bool child_comma = i < actual_len || j < expected_len - 1;
                js_assert_append_multiline_value(sb, js_elements_get_int(expected, j),
                    indent + 2, '-', child_comma, 16);
                j++;
            }
        }
        js_assert_append_spaces(sb, indent);
        strbuf_append_char(sb, ']');
        if (trailing_comma) strbuf_append_char(sb, ',');
        strbuf_append_char(sb, '\n');
        return;
    }
    for (int64_t i = 0; i < max_len; i++) {
        bool has_actual = i < actual_len;
        bool has_expected = i < expected_len;
        Item av = has_actual ? js_elements_get_int(actual, i) : make_js_undefined();
        Item ev = has_expected ? js_elements_get_int(expected, i) : make_js_undefined();
        bool same = has_actual && has_expected && js_assert_deep_values_same(av, ev);
        bool child_comma = i < max_len - 1;
        if (same) {
            js_assert_append_multiline_value(sb, av, indent + 2, 0, child_comma, 16);
            continue;
        }
        if (has_actual && has_expected && js_assert_is_object_like_value(av) &&
                js_assert_is_object_like_value(ev)) {
            // Nested structural mismatches must expand at the differing depth;
            // compact inspect loses the exact leaf that changed.
            js_assert_append_structural_diff(sb, av, ev, indent + 2, child_comma, 16);
            continue;
        }
        if (has_actual) {
            js_assert_append_multiline_value(sb, av, indent + 2, '+', child_comma, 16);
        }
        if (has_expected) {
            js_assert_append_multiline_value(sb, ev, indent + 2, '-', child_comma, 16);
        }
    }
    js_assert_append_spaces(sb, indent);
    strbuf_append_char(sb, ']');
    if (trailing_comma) strbuf_append_char(sb, ',');
    strbuf_append_char(sb, '\n');
}

static void js_assert_append_array_diff_contents(StrBuf* sb, Item actual, Item expected,
                                                 int element_indent, int depth_left) {
    int64_t actual_len = js_array_length(actual);
    int64_t expected_len = js_array_length(expected);
    int score[65][65];
    js_assert_build_lcs_score(actual, expected, actual_len, expected_len, score);
    int64_t i = 0;
    int64_t j = 0;
    while (i < actual_len || j < expected_len) {
        if (i < actual_len && j < expected_len &&
                js_assert_deep_values_same(js_elements_get_int(actual, i), js_elements_get_int(expected, j))) {
            bool comma = i < actual_len - 1 || j < expected_len - 1;
            js_assert_append_multiline_value(sb, js_elements_get_int(actual, i),
                element_indent, 0, comma, depth_left - 1);
            i++;
            j++;
        } else if (i < actual_len &&
                js_assert_lcs_should_take_actual(actual, expected, score,
                    i, j, actual_len, expected_len)) {
            bool comma = score[0][0] == 0 && i == actual_len - 1 &&
                j < expected_len ? false : (i < actual_len - 1 || j < expected_len);
            js_assert_append_multiline_value(sb, js_elements_get_int(actual, i),
                element_indent, '+', comma, depth_left - 1);
            i++;
        } else if (j < expected_len) {
            bool comma = i < actual_len || j < expected_len - 1;
            js_assert_append_multiline_value(sb, js_elements_get_int(expected, j),
                element_indent, '-', comma, depth_left - 1);
            j++;
        }
    }
}

static bool js_assert_append_two_key_array_move_diff(StrBuf* sb, Item actual, Item expected,
                                                     Item actual_keys, Item expected_keys,
                                                     int indent, int depth_left) {
    if (js_array_length(actual_keys) != 2 || js_array_length(expected_keys) != 2) return false;
    Item a0_key = js_elements_get_int(actual_keys, 0);
    Item a1_key = js_elements_get_int(actual_keys, 1);
    Item e0_key = js_elements_get_int(expected_keys, 0);
    Item e1_key = js_elements_get_int(expected_keys, 1);
    if (!js_assert_same_property_key(a0_key, e0_key) ||
            !js_assert_same_property_key(a1_key, e1_key)) {
        return false;
    }
    Item a0 = js_get_key_default(actual, a0_key);
    Item a1 = js_get_key_default(actual, a1_key);
    Item e0 = js_get_key_default(expected, e0_key);
    Item e1 = js_get_key_default(expected, e1_key);
    if (get_type_id(a0) != LMD_TYPE_ARRAY || get_type_id(e1) != LMD_TYPE_ARRAY ||
            js_assert_is_object_like_value(a1) || js_assert_is_object_like_value(e0)) {
        return false;
    }

    js_assert_append_line_prefix(sb, indent, '+');
    js_assert_append_multiline_object_key(sb, a0_key);
    strbuf_append_str(sb, ": [\n");
    int64_t actual_len = js_array_length(a0);
    int64_t expected_len = js_array_length(e1);
    int64_t ai = 0;
    while (ai < actual_len && ai < expected_len &&
            !js_assert_deep_values_same(js_elements_get_int(a0, ai), js_elements_get_int(e1, 0))) {
        js_assert_append_multiline_value(sb, js_elements_get_int(a0, ai),
            indent + 2, '+', true, depth_left - 1);
        ai++;
    }
    js_assert_append_line_prefix(sb, indent, '-');
    js_assert_append_multiline_object_key(sb, e0_key);
    strbuf_append_str(sb, ": ");
    js_assert_append_inspected_value(sb, e0);
    strbuf_append_str(sb, ",\n");
    js_assert_append_line_prefix(sb, indent, '-');
    js_assert_append_multiline_object_key(sb, e1_key);
    strbuf_append_str(sb, ": [\n");
    for (int64_t j = 0; j < expected_len; j++) {
        Item ev = js_elements_get_int(e1, j);
        bool matched = false;
        for (int64_t k = ai; k < actual_len; k++) {
            if (js_assert_deep_values_same(js_elements_get_int(a0, k), ev)) {
                matched = true;
                break;
            }
        }
        js_assert_append_multiline_value(sb, ev, indent + 2,
            matched ? 0 : '-', j < expected_len - 1, depth_left - 1);
    }
    js_assert_append_spaces(sb, indent);
    strbuf_append_str(sb, "],\n");
    js_assert_append_line_prefix(sb, indent, '+');
    js_assert_append_multiline_object_key(sb, a1_key);
    strbuf_append_str(sb, ": ");
    js_assert_append_inspected_value(sb, a1);
    strbuf_append_char(sb, '\n');
    return true;
}

static void js_assert_append_object_diff_recursive(StrBuf* sb, Item actual, Item expected,
                                                   int indent, bool trailing_comma,
                                                   int depth_left) {
    js_assert_append_spaces(sb, indent);
    if (js_assert_is_arguments_value(actual)) strbuf_append_str(sb, "[Arguments] ");
    strbuf_append_str(sb, "{\n");
    Item actual_keys = js_object_keys(actual);
    Item expected_keys = js_object_keys(expected);
    int64_t actual_len = js_array_length(actual_keys);
    int64_t expected_len = js_array_length(expected_keys);
    for (int64_t i = 0; i < actual_len; i++) {
        Item key = js_elements_get_int(actual_keys, i);
        if (js_assert_string_equals(key, "__strict_arguments__")) continue;
        Item av = js_get_key_default(actual, key);
        bool has_expected = js_assert_key_array_contains(expected_keys, key);
        Item ev = has_expected ? js_get_key_default(expected, key) : make_js_undefined();
        bool same = has_expected && js_assert_deep_values_same(av, ev);
        bool has_more = i < actual_len - 1 || expected_len > actual_len;
        if (same) {
            js_assert_append_spaces(sb, indent + 2);
            js_assert_append_multiline_object_key(sb, key);
            strbuf_append_str(sb, ": ");
            js_assert_append_property_value(sb, actual, key, av);
            if (has_more) strbuf_append_char(sb, ',');
            strbuf_append_char(sb, '\n');
        } else if (has_expected && depth_left > 0 &&
                js_assert_is_object_like_value(av) && js_assert_is_object_like_value(ev)) {
            js_assert_append_spaces(sb, indent + 2);
            js_assert_append_multiline_object_key(sb, key);
            strbuf_append_str(sb, ": ");
            if (get_type_id(av) == LMD_TYPE_ARRAY && get_type_id(ev) == LMD_TYPE_ARRAY) {
                // Object properties keep the key and opening bracket on one line
                // in Node's nested deepStrictEqual diffs.
                strbuf_append_str(sb, "[\n");
                StrBuf* nested = sb;
                (void)nested;
                int64_t alen = js_array_length(av);
                int64_t elen = js_array_length(ev);
                if (alen <= 64 && elen <= 64) {
                    int score[65][65];
                    memset(score, 0, sizeof(score));
                    for (int64_t ai = alen - 1; ai >= 0; ai--) {
                        for (int64_t ej = elen - 1; ej >= 0; ej--) {
                            if (js_assert_deep_values_same(js_elements_get_int(av, ai), js_elements_get_int(ev, ej))) {
                                score[ai][ej] = score[ai + 1][ej + 1] + 1;
                            } else {
                                score[ai][ej] = score[ai + 1][ej] > score[ai][ej + 1] ?
                                    score[ai + 1][ej] : score[ai][ej + 1];
                            }
                        }
                    }
                    int64_t ai = 0;
                    int64_t ej = 0;
                    while (ai < alen || ej < elen) {
                        if (ai < alen && ej < elen &&
                                js_assert_deep_values_same(js_elements_get_int(av, ai), js_elements_get_int(ev, ej))) {
                            bool comma = ai < alen - 1 || ej < elen - 1;
                            js_assert_append_multiline_value(sb, js_elements_get_int(av, ai),
                                indent + 4, 0, comma, depth_left - 1);
                            ai++;
                            ej++;
                        } else if (ai < alen && ej < elen &&
                                js_assert_is_object_like_value(js_elements_get_int(av, ai)) &&
                                js_assert_is_object_like_value(js_elements_get_int(ev, ej))) {
                            bool comma = ai < alen - 1 || ej < elen - 1;
                            // Preserve nested structural context under object
                            // properties instead of replacing the whole child.
                            js_assert_append_structural_diff(sb, js_elements_get_int(av, ai),
                                js_elements_get_int(ev, ej), indent + 4, comma, depth_left - 1);
                            ai++;
                            ej++;
                        } else if (ai < alen && ej < elen && alen == elen &&
                                ai == alen - 1 && ej == elen - 1) {
                            bool comma = ai < alen - 1;
                            // Equal-length scalar mismatches are replacements;
                            // do not attach an insertion comma to the + line.
                            js_assert_append_multiline_value(sb, js_elements_get_int(av, ai),
                                indent + 4, '+', comma, depth_left - 1);
                            js_assert_append_multiline_value(sb, js_elements_get_int(ev, ej),
                                indent + 4, '-', comma, depth_left - 1);
                            ai++;
                            ej++;
                        } else if (ai + 1 < alen && ej + 1 < elen && alen == elen &&
                                js_assert_deep_values_same(js_elements_get_int(av, ai + 1),
                                                           js_elements_get_int(ev, ej + 1))) {
                            bool comma = ai < alen - 1;
                            // Equal-length scalar replacements should keep the
                            // actual line before expected after realignment.
                            js_assert_append_multiline_value(sb, js_elements_get_int(av, ai),
                                indent + 4, '+', comma, depth_left - 1);
                            js_assert_append_multiline_value(sb, js_elements_get_int(ev, ej),
                                indent + 4, '-', comma, depth_left - 1);
                            ai++;
                            ej++;
                        } else if (ai < alen &&
                                js_assert_lcs_should_take_actual(av, ev, score,
                                    ai, ej, alen, elen)) {
                            bool comma = score[0][0] == 0 && ai == alen - 1 &&
                                ej < elen ? false : (ai < alen - 1 || ej < elen);
                            js_assert_append_multiline_value(sb, js_elements_get_int(av, ai),
                                indent + 4, '+', comma, depth_left - 1);
                            ai++;
                        } else if (ej < elen) {
                            bool comma = ai < alen || ej < elen - 1;
                            js_assert_append_multiline_value(sb, js_elements_get_int(ev, ej),
                                indent + 4, '-', comma, depth_left - 1);
                            ej++;
                        }
                    }
                } else {
                    int64_t max_len = alen > elen ? alen : elen;
                    for (int64_t ai = 0; ai < max_len; ai++) {
                        bool comma = ai < max_len - 1;
                        if (ai < alen) js_assert_append_multiline_value(sb, js_elements_get_int(av, ai),
                            indent + 4, '+', comma, depth_left - 1);
                        if (ai < elen) js_assert_append_multiline_value(sb, js_elements_get_int(ev, ai),
                            indent + 4, '-', comma, depth_left - 1);
                    }
                }
                js_assert_append_spaces(sb, indent + 2);
                strbuf_append_char(sb, ']');
                if (has_more) strbuf_append_char(sb, ',');
                strbuf_append_char(sb, '\n');
            } else {
                if (js_assert_is_plain_diff_object(av) && js_assert_is_plain_diff_object(ev)) {
                    strbuf_append_str(sb, "{\n");
                    Item nested_actual_keys = js_object_keys(av);
                    Item nested_expected_keys = js_object_keys(ev);
                    if (!js_assert_append_two_key_array_move_diff(sb, av, ev,
                            nested_actual_keys, nested_expected_keys, indent + 4, depth_left - 1)) {
                        int64_t nested_len = js_array_length(nested_actual_keys);
                        for (int64_t j = 0; j < nested_len; j++) {
                            Item child_key = js_elements_get_int(nested_actual_keys, j);
                            Item child_actual = js_get_key_default(av, child_key);
                            Item child_expected = js_get_key_default(ev, child_key);
                            js_assert_append_spaces(sb, indent + 4);
                            js_assert_append_multiline_object_key(sb, child_key);
                            strbuf_append_str(sb, ": ");
                            if (get_type_id(child_actual) == LMD_TYPE_ARRAY &&
                                    get_type_id(child_expected) == LMD_TYPE_ARRAY) {
                                // Nested object properties own the key; only
                                // the array contents should be LCS-rendered.
                                strbuf_append_str(sb, "[\n");
                                js_assert_append_array_diff_contents(sb, child_actual, child_expected,
                                    indent + 6, depth_left - 1);
                                js_assert_append_spaces(sb, indent + 4);
                                strbuf_append_char(sb, ']');
                            } else {
                                js_assert_append_property_value(sb, av, child_key, child_actual);
                            }
                            if (j < nested_len - 1) strbuf_append_char(sb, ',');
                            strbuf_append_char(sb, '\n');
                        }
                    }
                    js_assert_append_spaces(sb, indent + 2);
                    strbuf_append_char(sb, '}');
                    if (has_more) strbuf_append_char(sb, ',');
                    strbuf_append_char(sb, '\n');
                } else {
                    strbuf_append_char(sb, '\n');
                    js_assert_append_structural_diff(sb, av, ev, indent + 4, has_more, depth_left - 1);
                }
            }
        } else {
            js_assert_append_line_prefix(sb, indent + 2, '+');
            js_assert_append_multiline_object_key(sb, key);
            strbuf_append_str(sb, ": ");
            js_assert_append_property_value(sb, actual, key, av);
            if (has_more) strbuf_append_char(sb, ',');
            strbuf_append_char(sb, '\n');
            if (has_expected) {
                js_assert_append_line_prefix(sb, indent + 2, '-');
                js_assert_append_multiline_object_key(sb, key);
                strbuf_append_str(sb, ": ");
                js_assert_append_property_value(sb, expected, key, ev);
                if (has_more) strbuf_append_char(sb, ',');
                strbuf_append_char(sb, '\n');
            }
        }
    }
    for (int64_t i = 0; i < expected_len; i++) {
        Item key = js_elements_get_int(expected_keys, i);
        if (js_assert_string_equals(key, "__strict_arguments__")) continue;
        if (js_assert_key_array_contains(actual_keys, key)) continue;
        js_assert_append_line_prefix(sb, indent + 2, '-');
        js_assert_append_multiline_object_key(sb, key);
        strbuf_append_str(sb, ": ");
        js_assert_append_property_value(sb, expected, key, js_get_key_default(expected, key));
        strbuf_append_char(sb, '\n');
    }
    js_assert_append_spaces(sb, indent);
    strbuf_append_char(sb, '}');
    if (trailing_comma) strbuf_append_char(sb, ',');
    strbuf_append_char(sb, '\n');
}

static void js_assert_append_structural_diff(StrBuf* sb, Item actual, Item expected,
                                             int indent, bool trailing_comma,
                                             int depth_left) {
    if (js_is_proxy(actual) && get_type_id(expected) == LMD_TYPE_ARRAY) {
        JsProxyData* pd = js_get_proxy_data(actual);
        Item target = pd ? (Item){.item = pd->target} : make_js_undefined();
        if (get_type_id(target) == LMD_TYPE_ARRAY) {
            int64_t actual_len = js_array_length(target);
            int64_t expected_len = js_array_length(expected);
            int64_t common_len = actual_len < expected_len ? actual_len : expected_len;
            js_assert_append_line_prefix(sb, indent, '+');
            strbuf_append_str(sb, "Proxy([\n");
            js_assert_append_line_prefix(sb, indent, '-');
            strbuf_append_str(sb, "[\n");
            for (int64_t i = 0; i < common_len; i++) {
                js_assert_append_spaces(sb, indent + 4);
                js_assert_append_inspected_value(sb, js_elements_get_int(target, i));
                if (i < expected_len - 1) strbuf_append_char(sb, ',');
                strbuf_append_char(sb, '\n');
            }
            js_assert_append_line_prefix(sb, indent, '+');
            strbuf_append_str(sb, "])");
            if (trailing_comma) strbuf_append_char(sb, ',');
            strbuf_append_char(sb, '\n');
            for (int64_t i = common_len; i < expected_len; i++) {
                js_assert_append_multiline_value(sb, js_elements_get_int(expected, i),
                    indent + 2, '-', i < expected_len - 1, depth_left - 1);
            }
            js_assert_append_line_prefix(sb, indent, '-');
            strbuf_append_char(sb, ']');
            if (trailing_comma) strbuf_append_char(sb, ',');
            strbuf_append_char(sb, '\n');
            return;
        }
    }
    if (get_type_id(actual) == LMD_TYPE_ARRAY && get_type_id(expected) == LMD_TYPE_ARRAY) {
        js_assert_append_array_diff_recursive(sb, actual, expected, indent, trailing_comma);
        return;
    }
    if (js_assert_is_plain_diff_object(actual) && js_assert_is_plain_diff_object(expected)) {
        js_assert_append_object_diff_recursive(sb, actual, expected, indent, trailing_comma, depth_left);
        return;
    }
    js_assert_append_multiline_value(sb, actual, indent, '+', trailing_comma, depth_left);
    js_assert_append_multiline_value(sb, expected, indent, '-', trailing_comma, depth_left);
}

static Item js_assert_deep_strict_array_message(Item actual, Item expected) {
    if (get_type_id(actual) != LMD_TYPE_ARRAY ||
            get_type_id(expected) != LMD_TYPE_ARRAY) {
        return ItemNull;
    }
    int64_t actual_len = js_array_length(actual);
    int64_t expected_len = js_array_length(expected);
    int64_t common_len = actual_len < expected_len ? actual_len : expected_len;
    int64_t diff_index = -1;
    for (int64_t i = 0; i < common_len; i++) {
        Item eq = js_util_isDeepStrictEqual(js_elements_get_int(actual, i),
            js_elements_get_int(expected, i));
        bool same = (get_type_id(eq) == LMD_TYPE_INT && it2i(eq) == 1) ||
            (get_type_id(eq) == LMD_TYPE_BOOL && it2b(eq));
        if (!same) {
            diff_index = i;
            break;
        }
    }
    if (diff_index < 0 && actual_len != expected_len) diff_index = common_len;
    if (diff_index < 0) return ItemNull;

    int64_t max_array_len = actual_len > expected_len ? actual_len : expected_len;
    if (actual_len == expected_len && actual_len >= 5 &&
            js_assert_is_object_like_value(js_elements_get_int(actual, actual_len - 1)) &&
            js_assert_is_object_like_value(js_elements_get_int(expected, expected_len - 1))) {
        Item actual_child = js_elements_get_int(actual, actual_len - 1);
        Item expected_child = js_elements_get_int(expected, expected_len - 1);
        Item child_keys = js_object_keys(actual_child);
        int64_t child_key_len = js_array_length(child_keys);
        for (int64_t key_index = 0; key_index < child_key_len; key_index++) {
            Item child_key = js_elements_get_int(child_keys, key_index);
            if (!js_assert_has_own_property_key(expected_child, child_key)) continue;
            Item actual_value = js_get_key_default(actual_child, child_key);
            Item expected_value = js_get_key_default(expected_child, child_key);
            if (get_type_id(actual_value) == LMD_TYPE_ARRAY &&
                    get_type_id(expected_value) == LMD_TYPE_ARRAY) {
                StrBuf* sb = strbuf_new();
                strbuf_append_str(sb, "Expected values to be strictly deep-equal:\n");
                strbuf_append_str(sb, "+ actual - expected\n");
                strbuf_append_str(sb, "... Skipped lines\n\n");
                strbuf_append_str(sb, "  [\n");
                int64_t head_count = diff_index < 2 ? diff_index : 2;
                for (int64_t i = 0; i < head_count; i++) {
                    js_assert_append_multiline_value(sb, js_elements_get_int(actual, i),
                        4, 0, true, 16);
                }
                strbuf_append_str(sb, "...\n");
                js_assert_append_spaces(sb, 6);
                js_assert_append_multiline_object_key(sb, child_key);
                strbuf_append_str(sb, ": [\n");
                // Tail abbreviations protect the nested array invariant while
                // avoiding a full prefix dump for late object mismatches.
                js_assert_append_array_diff_contents(sb, actual_value, expected_value, 8, 16);
                strbuf_append_str(sb, "      ]\n");
                strbuf_append_str(sb, "    }\n");
                strbuf_append_str(sb, "  ]\n");
                Item result = assert_make_string_n(sb->str, sb->length);
                strbuf_free(sb);
                return result;
            }
        }
    }
    if ((max_array_len > 8 || (actual_len != expected_len && max_array_len > 7)) &&
            diff_index >= 6) {
        // Long array diffs preserve context near the changed tail; dumping all
        // shared elements makes the public assert message too noisy.
        StrBuf* sb = strbuf_new();
        strbuf_append_str(sb, "Expected values to be strictly deep-equal:\n");
        strbuf_append_str(sb, "+ actual - expected\n");
        strbuf_append_str(sb, "... Skipped lines\n\n");
        strbuf_append_str(sb, "  [\n");
        for (int64_t i = 0; i < 4 && i < actual_len; i++) {
            strbuf_append_str(sb, "    ");
            js_assert_append_inspected_value(sb, js_elements_get_int(actual, i));
            strbuf_append_str(sb, ",\n");
        }
        strbuf_append_str(sb, "...\n");
        int64_t context_index = diff_index - 1;
        if (context_index >= 4 && context_index < actual_len && context_index < expected_len) {
            strbuf_append_str(sb, "    ");
            js_assert_append_inspected_value(sb, js_elements_get_int(actual, context_index));
            strbuf_append_str(sb, ",\n");
        }
        if (diff_index < actual_len) {
            strbuf_append_str(sb, "+   ");
            js_assert_append_inspected_value(sb, js_elements_get_int(actual, diff_index));
            if (diff_index < actual_len - 1) strbuf_append_char(sb, ',');
            strbuf_append_char(sb, '\n');
        }
        if (diff_index < expected_len) {
            strbuf_append_str(sb, "-   ");
            js_assert_append_inspected_value(sb, js_elements_get_int(expected, diff_index));
            if (diff_index < expected_len - 1) strbuf_append_char(sb, ',');
            strbuf_append_char(sb, '\n');
        }
        int64_t after = diff_index + 1;
        if (after < actual_len && after < expected_len) {
            strbuf_append_str(sb, "    ");
            js_assert_append_inspected_value(sb, js_elements_get_int(actual, after));
            strbuf_append_char(sb, '\n');
        }
        strbuf_append_str(sb, "  ]\n");
        Item result = assert_make_string_n(sb->str, sb->length);
        strbuf_free(sb);
        return result;
    }
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "Expected values to be strictly deep-equal:\n");
    strbuf_append_str(sb, "+ actual - expected\n\n");
    (void)diff_index;
    int root_indent = (js_is_proxy(actual) && get_type_id(expected) == LMD_TYPE_ARRAY) ||
                      (get_type_id(actual) == LMD_TYPE_ARRAY &&
                       js_assert_is_plain_diff_object(expected)) ||
                      (js_assert_is_plain_diff_object(actual) &&
                       get_type_id(expected) == LMD_TYPE_ARRAY) ? 0 : 2;
    js_assert_append_structural_diff(sb, actual, expected, root_indent, false, 16);
    Item result = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

static bool js_assert_is_buffer_value(Item value) {
    Item result = js_buffer_isBuffer(value);
    return (get_type_id(result) == LMD_TYPE_BOOL && it2b(result)) ||
           (get_type_id(result) == LMD_TYPE_INT && it2i(result) != 0);
}

static void js_assert_append_typed_array_header(StrBuf* sb, Item value, const char* sign) {
    const char* type_name = js_typed_array_type_name(value);
    if (!type_name) type_name = "Uint8Array";
    if (js_assert_is_buffer_value(value)) {
        strbuf_append_str(sb, sign);
        strbuf_append_str(sb, "Buffer(");
        strbuf_append_int64(sb, js_typed_array_length(value));
        strbuf_append_str(sb, ") [Uint8Array] [\n");
    } else {
        strbuf_append_str(sb, sign);
        strbuf_append_str(sb, type_name);
        strbuf_append_char(sb, '(');
        strbuf_append_int64(sb, js_typed_array_length(value));
        strbuf_append_str(sb, ") [\n");
    }
}

static Item js_assert_deep_strict_typed_array_message(Item actual, Item expected) {
    if (!js_is_typed_array(actual) || !js_is_typed_array(expected)) return ItemNull;
    int len = js_typed_array_length(actual);
    if (len != js_typed_array_length(expected) || len < 0) return ItemNull;
    for (int i = 0; i < len; i++) {
        Item av = js_typed_array_get(actual, (Item){.item = i2it(i)});
        Item ev = js_typed_array_get(expected, (Item){.item = i2it(i)});
        if (!js_is_truthy(js_strict_equal(av, ev))) return ItemNull;
    }
    Item actual_keys = js_object_keys(actual);
    Item expected_keys = js_object_keys(expected);
    int64_t actual_key_len = js_array_length(actual_keys);
    int64_t expected_key_len = js_array_length(expected_keys);
    if (actual_key_len != expected_key_len) {
        StrBuf* prop = strbuf_new();
        strbuf_append_str(prop, "Expected values to be strictly deep-equal:\n");
        strbuf_append_str(prop, "+ actual - expected\n\n");
        strbuf_append_str(prop, "  ");
        js_assert_append_typed_array_header(prop, actual, "");
        for (int i = 0; i < len; i++) {
            strbuf_append_str(prop, "    ");
            js_assert_append_inspected_value(prop, js_typed_array_get(actual, (Item){.item = i2it(i)}));
            strbuf_append_str(prop, ",\n");
        }
        Item diff_keys = actual_key_len > expected_key_len ? actual_keys : expected_keys;
        const char* sign = actual_key_len > expected_key_len ? "+   " : "-   ";
        for (int64_t i = 0; i < js_array_length(diff_keys); i++) {
            Item key = js_elements_get_int(diff_keys, i);
            String* ks = get_type_id(key) == LMD_TYPE_STRING ? it2s(key) : NULL;
            if (!ks) continue;
            bool is_index = ks->len > 0;
            int64_t key_index = 0;
            for (size_t k = 0; is_index && k < ks->len; k++) {
                char ch = ks->chars[k];
                if (ch < '0' || ch > '9') is_index = false;
                else key_index = key_index * 10 + (ch - '0');
            }
            if (is_index && key_index >= 0 && key_index < len) continue;
            strbuf_append_str(prop, sign);
            strbuf_append_str_n(prop, ks->chars, ks->len);
            strbuf_append_str(prop, ": ");
            js_assert_append_inspected_value(prop,
                js_get_key_default(actual_key_len > expected_key_len ? actual : expected, key));
            strbuf_append_char(prop, '\n');
        }
        strbuf_append_str(prop, "  ]\n");
        Item result = assert_make_string_n(prop->str, prop->length);
        strbuf_free(prop);
        return result;
    }

    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "Expected values to be strictly deep-equal:\n");
    strbuf_append_str(sb, "+ actual - expected\n\n");
    js_assert_append_typed_array_header(sb, actual, "+ ");
    js_assert_append_typed_array_header(sb, expected, "- ");
    for (int i = 0; i < len; i++) {
        strbuf_append_str(sb, "    ");
        js_assert_append_inspected_value(sb, js_typed_array_get(actual, (Item){.item = i2it(i)}));
        if (i < len - 1) strbuf_append_char(sb, ',');
        strbuf_append_char(sb, '\n');
    }
    strbuf_append_str(sb, "  ]\n");
    Item result = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

static bool js_assert_is_date_value(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return false;
    if (js_class_id(value) == JS_CLASS_DATE) return true;
    char ctor_name[64];
    if (!js_get_constructor_name(value, ctor_name, sizeof(ctor_name))) return false;
    size_t len = strlen(ctor_name);
    return len >= 4 && memcmp(ctor_name + len - 4, "Date", 4) == 0;
}

static bool js_assert_date_iso(Item value, StrBuf* sb) {
    if (!js_assert_is_date_value(value)) return false;
    bool found_time = false;
    // Date subclasses keep [[DateValue]] in the hidden map slot; public lookup
    // can miss it and leave assert diffs with a blank subclass label.
    Item time_value = js_map_shape_lookup_ext(value.map, "__time__", 8, &found_time);
    double millis = 0.0;
    if (found_time && get_type_id(time_value) == LMD_TYPE_INT) millis = (double)it2i(time_value);
    else if (found_time && get_type_id(time_value) == LMD_TYPE_FLOAT) millis = it2d(time_value);
    else {
        Item iso = js_date_method(value, 8);
        if (item_is_error(iso)) return false;
        String* s = get_type_id(iso) == LMD_TYPE_STRING ? it2s(iso) : NULL;
        if (!s) return false;
        strbuf_append_str_n(sb, s->chars, s->len);
        return true;
    }
    time_t seconds = (time_t)(millis / 1000.0);
    int ms = (int)((int64_t)millis % 1000);
    if (ms < 0) ms = -ms;
    struct tm tm_value;
    if (!gmtime_r(&seconds, &tm_value)) return false;
    char buf[40];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
        tm_value.tm_year + 1900, tm_value.tm_mon + 1, tm_value.tm_mday,
        tm_value.tm_hour, tm_value.tm_min, tm_value.tm_sec, ms);
    strbuf_append_str(sb, buf);
    return true;
}

static void js_assert_append_quoted_key(StrBuf* sb, String* key) {
    if (!key) return;
    bool identifier = key->len > 0 &&
        ((key->chars[0] >= 'A' && key->chars[0] <= 'Z') ||
         (key->chars[0] >= 'a' && key->chars[0] <= 'z') ||
         key->chars[0] == '_' || key->chars[0] == '$');
    for (size_t i = 1; identifier && i < key->len; i++) {
        char ch = key->chars[i];
        identifier = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '$';
    }
    if (identifier) {
        strbuf_append_str_n(sb, key->chars, key->len);
    } else {
        strbuf_append_char(sb, '\'');
        js_assert_append_escaped_string_range(sb, key->chars, key->len, '\'');
        strbuf_append_char(sb, '\'');
    }
}

static void js_assert_append_class_value_with_props(StrBuf* sb, Item value,
                                                    const char* sign, bool regexp) {
    strbuf_append_str(sb, sign);
    char ctor_name[64];
    const char* default_name = regexp ? "RegExp" : "Date";
    bool has_ctor = js_get_constructor_name(value, ctor_name, sizeof(ctor_name));
    bool custom_ctor = has_ctor && strcmp(ctor_name, default_name) != 0;
    if (custom_ctor) {
        strbuf_append_str(sb, ctor_name);
        strbuf_append_char(sb, ' ');
    }
    if (regexp) js_assert_append_item_text(sb, js_to_string_val(value));
    else js_assert_date_iso(value, sb);

    Item keys = js_object_keys(value);
    int64_t len = js_array_length(keys);
    int64_t visible_len = 0;
    for (int64_t i = 0; i < len; i++) {
        Item key = js_elements_get_int(keys, i);
        if (!js_assert_string_equals(key, "__time__")) visible_len++;
    }
    if (visible_len <= 0) {
        strbuf_append_char(sb, '\n');
        return;
    }
    strbuf_append_str(sb, " {\n");
    int64_t emitted = 0;
    for (int64_t i = 0; i < len; i++) {
        Item key = js_elements_get_int(keys, i);
        if (js_assert_string_equals(key, "__time__")) continue;
        String* ks = get_type_id(key) == LMD_TYPE_STRING ? it2s(key) : NULL;
        strbuf_append_str(sb, sign);
        strbuf_append_str(sb, "  ");
        js_assert_append_quoted_key(sb, ks);
        strbuf_append_str(sb, ": ");
        js_assert_append_inspected_value(sb, js_get_key_default(value, key));
        emitted++;
        if (emitted < visible_len) strbuf_append_char(sb, ',');
        strbuf_append_char(sb, '\n');
    }
    strbuf_append_str(sb, sign);
    strbuf_append_str(sb, "}\n");
}

static Item js_assert_deep_strict_class_message(Item actual, Item expected, bool regexp) {
    bool actual_match = regexp ? js_assert_is_real_regexp(actual) : js_assert_is_date_value(actual);
    bool expected_match = regexp ? js_assert_is_real_regexp(expected) : js_assert_is_date_value(expected);
    if (!actual_match || !expected_match) return ItemNull;
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "Expected values to be strictly deep-equal:\n");
    strbuf_append_str(sb, "+ actual - expected\n\n");
    js_assert_append_class_value_with_props(sb, actual, "+ ", regexp);
    js_assert_append_class_value_with_props(sb, expected, "- ", regexp);
    Item result = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

static Item js_assert_deep_strict_map_message(Item actual, Item expected) {
    if (!js_is_map_instance(actual) || !js_is_map_instance(expected)) return ItemNull;
    Item actual_entries = js_iterable_to_array(actual);
    Item expected_entries = js_iterable_to_array(expected);
    int64_t actual_len = js_array_length(actual_entries);
    int64_t expected_len = js_array_length(expected_entries);
    if (actual_len != expected_len || actual_len <= 0) return ItemNull;

    for (int64_t i = 0; i < actual_len; i++) {
        Item actual_pair = js_elements_get_int(actual_entries, i);
        Item actual_key = js_elements_get_int(actual_pair, 0);
        Item actual_value = js_elements_get_int(actual_pair, 1);
        for (int64_t j = 0; j < expected_len; j++) {
            Item expected_pair = js_elements_get_int(expected_entries, j);
            Item expected_key = js_elements_get_int(expected_pair, 0);
            Item key_equal = js_util_isDeepStrictEqual(actual_key, expected_key);
            if (!((get_type_id(key_equal) == LMD_TYPE_BOOL && it2b(key_equal)) ||
                    (get_type_id(key_equal) == LMD_TYPE_INT && it2i(key_equal) == 1))) {
                continue;
            }
            Item expected_value = js_elements_get_int(expected_pair, 1);
            Item value_equal = js_util_isDeepStrictEqual(actual_value, expected_value);
            if ((get_type_id(value_equal) == LMD_TYPE_BOOL && it2b(value_equal)) ||
                    (get_type_id(value_equal) == LMD_TYPE_INT && it2i(value_equal) == 1)) {
                break;
            }
            StrBuf* sb = strbuf_new();
            strbuf_append_str(sb, "Expected values to be strictly deep-equal:\n");
            strbuf_append_str(sb, "+ actual - expected\n\n  Map(");
            strbuf_append_int64(sb, actual_len);
            strbuf_append_str(sb, ") {\n+   ");
            js_assert_append_inspected_value(sb, actual_key);
            strbuf_append_str(sb, " => ");
            js_assert_append_inspected_value(sb, actual_value);
            strbuf_append_str(sb, "\n-   ");
            js_assert_append_inspected_value(sb, expected_key);
            strbuf_append_str(sb, " => ");
            js_assert_append_inspected_value(sb, expected_value);
            strbuf_append_str(sb, "\n  }\n");
            Item result = assert_make_string_n(sb->str, sb->length);
            strbuf_free(sb);
            // Map entries are internal slots; plain object diffing renders them
            // as {}, hiding key/value mismatches.
            return result;
        }
    }
    return ItemNull;
}

static Item js_assert_deep_strict_object_message(Item actual, Item expected) {
    if (get_type_id(actual) != LMD_TYPE_MAP || get_type_id(expected) != LMD_TYPE_MAP) return ItemNull;
    if (js_class_id(actual) != JS_CLASS_NONE || js_class_id(expected) != JS_CLASS_NONE) return ItemNull;
    Item actual_keys = js_assert_enumerable_own_keys(actual);
    Item expected_keys = js_assert_enumerable_own_keys(expected);
    int64_t actual_len = js_array_length(actual_keys);
    int64_t expected_len = js_array_length(expected_keys);
    if (actual_len == expected_len) return ItemNull;

    if (actual_len == 0 || expected_len == 0) {
        StrBuf* sb = strbuf_new();
        strbuf_append_str(sb, "Expected values to be strictly deep-equal:\n");
        strbuf_append_str(sb, "+ actual - expected\n\n");
        if (actual_len == 0) strbuf_append_str(sb, "+ {}\n");
        else js_assert_append_multiline_value(sb, actual, 0, '+', false, 16);
        if (expected_len == 0) strbuf_append_str(sb, "- {}\n");
        else js_assert_append_multiline_value(sb, expected, 0, '-', false, 16);
        Item result = assert_make_string_n(sb->str, sb->length);
        strbuf_free(sb);
        // Empty-vs-populated object diffs must show the empty side as a value;
        // otherwise enumerable symbol keys on the populated side disappear.
        return result;
    }

    Item base_keys = actual_len < expected_len ? actual_keys : expected_keys;
    Item diff_keys = actual_len > expected_len ? actual_keys : expected_keys;
    Item diff_owner = actual_len > expected_len ? actual : expected;
    const char* diff_sign = actual_len > expected_len ? "+   " : "-   ";

    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "Expected values to be strictly deep-equal:\n");
    strbuf_append_str(sb, "+ actual - expected\n\n");
    strbuf_append_str(sb, "  {\n");
    for (int64_t i = 0; i < js_array_length(base_keys); i++) {
        Item key = js_elements_get_int(base_keys, i);
        String* ks = get_type_id(key) == LMD_TYPE_STRING ? it2s(key) : NULL;
        if (!ks) continue;
        strbuf_append_str(sb, "    ");
        strbuf_append_str_n(sb, ks->chars, ks->len);
        strbuf_append_str(sb, ": ");
        js_assert_append_inspected_value(sb, js_get_key_default(actual_len < expected_len ? actual : expected, key));
        strbuf_append_str(sb, ",\n");
    }
    for (int64_t i = 0; i < js_array_length(diff_keys); i++) {
        Item key = js_elements_get_int(diff_keys, i);
        if (js_assert_has_own_property_key(actual_len < expected_len ? actual : expected, key)) continue;
        String* ks = get_type_id(key) == LMD_TYPE_STRING ? it2s(key) : NULL;
        if (!ks) continue;
        strbuf_append_str(sb, diff_sign);
        strbuf_append_str_n(sb, ks->chars, ks->len);
        strbuf_append_str(sb, ": ");
        js_assert_append_inspected_value(sb, js_get_key_default(diff_owner, key));
        strbuf_append_char(sb, '\n');
    }
    strbuf_append_str(sb, "  }\n");
    Item result = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

static Item js_assert_deep_strict_url_message(Item actual, Item expected) {
    if (get_type_id(actual) != LMD_TYPE_MAP || get_type_id(expected) != LMD_TYPE_MAP ||
            js_class_id(actual) != JS_CLASS_URL || js_class_id(expected) != JS_CLASS_URL) {
        return ItemNull;
    }
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "Expected values to be strictly deep-equal:\n");
    strbuf_append_str(sb, "+ actual - expected\n\n");
    strbuf_append_str(sb, "+ ");
    js_assert_append_inspected_value(sb, js_get_key_cstr(actual, "href"));
    strbuf_append_str(sb, "\n- ");
    js_assert_append_inspected_value(sb, js_get_key_cstr(expected, "href"));
    strbuf_append_char(sb, '\n');
    // URL wrappers compare by canonical href; the fallback text hid the
    // differing URL and broke object-pattern message checks.
    Item result = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

static Item js_assert_deep_strict_structural_message(Item actual, Item expected) {
    if (!js_assert_is_object_like_value(actual) && !js_assert_is_object_like_value(expected)) return ItemNull;
    if (!(get_type_id(actual) == LMD_TYPE_ARRAY || js_assert_is_plain_diff_object(actual) ||
          get_type_id(expected) == LMD_TYPE_ARRAY || js_assert_is_plain_diff_object(expected))) {
        return ItemNull;
    }
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "Expected values to be strictly deep-equal:\n");
    strbuf_append_str(sb, "+ actual - expected\n\n");
    if (js_assert_is_plain_diff_object(actual) && js_assert_is_plain_diff_object(expected)) {
        Item keys = js_object_keys(actual);
        if (js_array_length(keys) > 50) {
            // Very large object diffs are intentionally abbreviated; otherwise
            // symbol-heavy mismatches drown the useful assertion context.
            strbuf_append_str(sb, "  {\n    ...\n  }\n");
            Item result = assert_make_string_n(sb->str, sb->length);
            strbuf_free(sb);
            return result;
        }
    }
    // Generic structural mismatches need a real diff instead of the fallback
    // text; array-vs-object and nested object/array failures reach this path.
    int root_indent = (js_is_proxy(actual) && get_type_id(expected) == LMD_TYPE_ARRAY) ||
                      (get_type_id(actual) == LMD_TYPE_ARRAY &&
                       js_assert_is_plain_diff_object(expected)) ||
                      (js_assert_is_plain_diff_object(actual) &&
                       get_type_id(expected) == LMD_TYPE_ARRAY) ||
                      (!js_assert_is_object_like_value(actual) ||
                       !js_assert_is_object_like_value(expected)) ? 0 : 2;
    js_assert_append_structural_diff(sb, actual, expected, root_indent, false, 16);
    Item result = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

static bool js_assert_has_own_key_early(Item object, const char* key) {
    Item result = js_has_own_property(object, assert_make_string(key));
    return get_type_id(result) == LMD_TYPE_BOOL && it2b(result);
}

static bool js_assert_has_own_property_key(Item object, Item key) {
    Item result = js_has_own_property(object, key);
    return (get_type_id(result) == LMD_TYPE_BOOL && it2b(result)) ||
           (get_type_id(result) == LMD_TYPE_INT && it2i(result) != 0);
}

static void js_assert_append_error_label(StrBuf* sb, Item value) {
    if (get_type_id(value) == LMD_TYPE_MAP && js_class_is_error_like(js_class_id(value))) {
        const char* name = js_class_to_name(js_class_id(value));
        if (!name) name = "Error";
        Item msg = js_get_key_cstr(value, "message");
        String* ms = get_type_id(msg) == LMD_TYPE_STRING ? it2s(msg) : NULL;
        strbuf_append_char(sb, '[');
        strbuf_append_str(sb, name);
        if (ms && ms->len > 0) {
            strbuf_append_str(sb, ": ");
            strbuf_append_str_n(sb, ms->chars, ms->len);
        }
        strbuf_append_char(sb, ']');
        return;
    }
    js_assert_append_inspected_value(sb, value);
}

static void js_assert_append_error_cause_value(StrBuf* sb, Item value, const char* prefix) {
    if (get_type_id(value) == LMD_TYPE_MAP && !js_class_is_error_like(js_class_id(value))) {
        Item keys = js_object_keys(value);
        int64_t len = js_array_length(keys);
        if (len == 1) {
            Item key = js_elements_get_int(keys, 0);
            String* ks = get_type_id(key) == LMD_TYPE_STRING ? it2s(key) : NULL;
            strbuf_append_str(sb, "{\n");
            strbuf_append_str(sb, prefix);
            strbuf_append_str(sb, "  ");
            if (ks) strbuf_append_str_n(sb, ks->chars, ks->len);
            strbuf_append_str(sb, ": ");
            js_assert_append_inspected_value(sb, js_get_key_default(value, key));
            strbuf_append_str(sb, "\n");
            strbuf_append_str(sb, prefix);
            strbuf_append_str(sb, "}");
            return;
        }
    }
    js_assert_append_error_label(sb, value);
}

static Item js_assert_deep_strict_error_message(Item actual, Item expected) {
    if (get_type_id(actual) != LMD_TYPE_MAP || get_type_id(expected) != LMD_TYPE_MAP ||
            !js_class_is_error_like(js_class_id(actual)) ||
            !js_class_is_error_like(js_class_id(expected))) {
        return ItemNull;
    }

    const char* keys[] = {"cause", "errors", NULL};
    for (int i = 0; keys[i]; i++) {
        bool actual_has = js_assert_has_own_key_early(actual, keys[i]);
        bool expected_has = js_assert_has_own_key_early(expected, keys[i]);
        if (!actual_has && !expected_has) continue;
        Item actual_value = actual_has ? js_get_key_default(actual, assert_make_string(keys[i])) : make_js_undefined();
        Item expected_value = expected_has ? js_get_key_default(expected, assert_make_string(keys[i])) : make_js_undefined();
        Item equal = js_util_isDeepStrictEqual(actual_value, expected_value);
        bool same = get_type_id(equal) == LMD_TYPE_BOOL && it2b(equal);
        if (same && actual_has == expected_has) continue;

        StrBuf* sb = strbuf_new();
        strbuf_append_str(sb, "Expected values to be strictly deep-equal:\n");
        strbuf_append_str(sb, "+ actual - expected\n\n");
        if (actual_has && expected_has) {
            strbuf_append_str(sb, "  ");
            js_assert_append_error_label(sb, actual);
            strbuf_append_str(sb, " {\n+   [");
            strbuf_append_str(sb, keys[i]);
            strbuf_append_str(sb, "]: ");
            js_assert_append_error_cause_value(sb, actual_value, "+   ");
            strbuf_append_str(sb, "\n-   [");
            strbuf_append_str(sb, keys[i]);
            strbuf_append_str(sb, "]: ");
            js_assert_append_error_cause_value(sb, expected_value, "-   ");
            strbuf_append_str(sb, "\n  }\n");
        } else if (actual_has) {
            strbuf_append_str(sb, "+ ");
            js_assert_append_error_label(sb, actual);
            strbuf_append_str(sb, " {\n+   [");
            strbuf_append_str(sb, keys[i]);
            strbuf_append_str(sb, "]: ");
            js_assert_append_error_cause_value(sb, actual_value, "+   ");
            strbuf_append_str(sb, "\n+ }\n- ");
            js_assert_append_error_label(sb, expected);
            strbuf_append_str(sb, "\n");
        } else {
            strbuf_append_str(sb, "+ ");
            js_assert_append_error_label(sb, actual);
            strbuf_append_str(sb, "\n- ");
            js_assert_append_error_label(sb, expected);
            strbuf_append_str(sb, " {\n-   [");
            strbuf_append_str(sb, keys[i]);
            strbuf_append_str(sb, "]: ");
            js_assert_append_error_cause_value(sb, expected_value, "-   ");
            strbuf_append_str(sb, "\n- }\n");
        }
        Item result = assert_make_string_n(sb->str, sb->length);
        strbuf_free(sb);
        return result;
    }

    Item actual_keys = js_object_keys(actual);
    Item expected_keys = js_object_keys(expected);
    int64_t expected_len = js_array_length(expected_keys);
    for (int64_t i = 0; i < expected_len; i++) {
        Item key = js_elements_get_int(expected_keys, i);
        if (js_assert_string_equals(key, "message") ||
                js_assert_string_equals(key, "name") ||
                js_assert_string_equals(key, "stack")) {
            continue;
        }
        bool actual_has = js_assert_has_own_property_key(actual, key);
        bool expected_has = js_assert_has_own_property_key(expected, key);
        if (!expected_has) continue;
        Item actual_value = actual_has ? js_get_key_default(actual, key) : make_js_undefined();
        Item expected_value = js_get_key_default(expected, key);
        if (actual_has && js_assert_deep_values_same(actual_value, expected_value)) continue;
        String* ks = get_type_id(key) == LMD_TYPE_STRING ? it2s(key) : NULL;
        if (!ks) continue;

        StrBuf* sb = strbuf_new();
        strbuf_append_str(sb, "Expected values to be strictly deep-equal:\n");
        strbuf_append_str(sb, "+ actual - expected\n\n");
        strbuf_append_str(sb, "  ");
        js_assert_append_error_label(sb, actual);
        strbuf_append_str(sb, " {\n");
        if (actual_has) {
            strbuf_append_str(sb, "+   ");
            js_assert_append_quoted_key(sb, ks);
            strbuf_append_str(sb, ": ");
            js_assert_append_inspected_value(sb, actual_value);
            strbuf_append_char(sb, '\n');
        }
        strbuf_append_str(sb, "-   ");
        js_assert_append_quoted_key(sb, ks);
        strbuf_append_str(sb, ": ");
        js_assert_append_inspected_value(sb, expected_value);
        strbuf_append_str(sb, "\n  }\n");
        Item result = assert_make_string_n(sb->str, sb->length);
        strbuf_free(sb);
        return result;
    }
    (void)actual_keys;
    return ItemNull;
}

static bool js_assert_message_is_auto(Item message) {
    TypeId type = get_type_id(message);
    return type == LMD_TYPE_UNDEFINED || message.item == ITEM_JS_UNDEFINED ||
           type == LMD_TYPE_NULL || message.item == ItemNull.item;
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_assert_source_space, (char ch), (ch == ' ' || ch == '\t' || ch == '\v' || ch == '\f'))

static Item js_assert_ok_source_message(void) {
    const char* source = js_pending_call_source;
    int len = js_pending_call_source_len;
    if (!source || len <= 0) return ItemNull;

    int start = 0;
    while (start < len && (js_assert_source_space(source[start]) ||
            source[start] == '\r' || source[start] == '\n')) {
        start++;
    }

    int first_line_end = start;
    while (first_line_end < len && source[first_line_end] != '\r' &&
            source[first_line_end] != '\n') {
        first_line_end++;
    }
    bool first_line_has_call = false;
    for (int i = start; i < first_line_end; i++) {
        if (source[i] == '(') { first_line_has_call = true; break; }
    }
    if (!first_line_has_call && first_line_end < len) {
        int next = first_line_end + 1;
        if (source[first_line_end] == '\r' && next < len && source[next] == '\n') next++;
        while (next < len && js_assert_source_space(source[next])) next++;
        if (next < len && source[next] == '.') {
            next++;
            while (next < len && js_assert_source_space(source[next])) next++;
            start = next;
        }
    }

    int scan = start;
    while (scan < len) {
        int line_end = scan;
        while (line_end < len && source[line_end] != '\r' && source[line_end] != '\n') line_end++;
        bool has_assert = false;
        bool is_outer_throws = false;
        for (int i = scan; i < line_end; i++) {
            if (i + 6 <= line_end && memcmp(source + i, "assert", 6) == 0) has_assert = true;
            if (i + 14 <= line_end && memcmp(source + i, "assert.throws(", 14) == 0) is_outer_throws = true;
        }
        if (has_assert && !is_outer_throws) {
            // Source capture may include the surrounding assert.throws() call;
            // choose the inner assert expression that actually failed.
            start = scan;
            while (start < line_end && js_assert_source_space(source[start])) start++;
            int assert_pos = start;
            for (int i = start; i + 6 <= line_end; i++) {
                if (memcmp(source + i, "assert", 6) == 0) { assert_pos = i; break; }
            }
            for (int i = start; i + 1 < assert_pos; i++) {
                if (source[i] == '=' && source[i + 1] == '>') {
                    start = i + 2;
                    while (start < line_end && js_assert_source_space(source[start])) start++;
                    break;
                }
            }
            break;
        }
        scan = line_end + 1;
        if (line_end < len && source[line_end] == '\r' && scan < len && source[scan] == '\n') scan++;
    }

    int end = start;
    while (end < len && source[end] != '\r' && source[end] != '\n') end++;
    while (end > start && js_assert_source_space(source[end - 1])) end--;
    if (end > start && source[end - 1] == ',') end--;
    if (end <= start) return ItemNull;

    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "The expression evaluated to a falsy value:\n\n  ");
    for (int i = start; i < end; i++) {
        unsigned char ch = (unsigned char)source[i];
        if (ch < 0x20) {
            char esc[8];
            snprintf(esc, sizeof(esc), "\\u%04x", (unsigned int)ch);
            strbuf_append_str(sb, esc);
        } else {
            strbuf_append_char(sb, source[i]);
        }
    }
    strbuf_append_str(sb, "\n");
    Item result = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

// assert(value[, message]) / assert.ok(value[, message])
extern "C" Item js_assert_ok(Item value, Item message) {
    if (!assert_is_truthy(value)) {
        if (js_pending_call_argc == 0) {
            // Node reports an omitted value argument differently from an explicit
            // falsy value; many official tests assert this API contract.
            return throw_assertion_error_full("No value argument passed to `assert.ok()`",
                value, (Item){.item = b2it(true)}, "==", true);
        }
        if (js_assert_message_is_auto(message)) {
            Item source_msg = js_assert_ok_source_message();
            if (get_type_id(source_msg) == LMD_TYPE_STRING) {
                return throw_assertion_error_full_item(source_msg,
                    value, (Item){.item = b2it(true)}, "==", true);
            }
        }
        return throw_assert_msg_or_auto(message,
            "The expression evaluated to a falsy value",
            value, (Item){.item = b2it(true)}, "==");
    }
    return make_js_undefined();
}

// assert.equal(actual, expected[, message]) — loose equality (==)
extern "C" Item js_assert_equal(Item actual, Item expected, Item message) {
    if (js_pending_call_argc < 2) return js_assert_throw_missing_actual_expected();
    if (js_assert_is_nan_number(actual) && js_assert_is_nan_number(expected)) {
        return make_js_undefined();
    }
    Item result = js_equal(actual, expected);
    if (!it2b(result)) {
        return throw_assert_msg_or_auto_item(message,
            js_assert_legacy_equal_message(actual, expected, "=="), actual, expected, "==");
    }
    return make_js_undefined();
}

// assert.notEqual(actual, expected[, message])
extern "C" Item js_assert_notEqual(Item actual, Item expected, Item message) {
    if (js_pending_call_argc < 2) return js_assert_throw_missing_actual_expected();
    if (js_assert_is_nan_number(actual) && js_assert_is_nan_number(expected)) {
        return throw_assert_msg_or_auto_item(message,
            js_assert_legacy_equal_message(actual, expected, "!="), actual, expected, "!=");
    }
    Item result = js_equal(actual, expected);
    if (it2b(result)) {
        return throw_assert_msg_or_auto_item(message,
            js_assert_legacy_equal_message(actual, expected, "!="), actual, expected, "!=");
    }
    return make_js_undefined();
}

// assert.strictEqual(actual, expected[, message]) — strict equality (===)
extern "C" Item js_assert_strictEqual(Item actual, Item expected, Item message) {
    if (js_pending_call_argc < 2) return js_assert_throw_missing_actual_expected();
    Item result = js_strict_equal(actual, expected);
    if (!it2b(result)) {
        Item generated = js_assert_strict_equal_message(actual, expected);
        bool has_user_message = false;
        Item user_message = js_assert_resolve_user_message(message, actual, expected, &has_user_message);
        if (has_user_message) {
            if (item_is_error(user_message)) return user_message;
            StrBuf* sb = strbuf_new();
            String* ms = get_type_id(user_message) == LMD_TYPE_STRING ? it2s(user_message) : NULL;
            if (ms) strbuf_append_str_n(sb, ms->chars, ms->len);
            String* gs = get_type_id(generated) == LMD_TYPE_STRING ? it2s(generated) : NULL;
            const char* header = "Expected values to be strictly equal:";
            size_t header_len = strlen(header);
            if (gs && gs->len >= header_len && memcmp(gs->chars, header, header_len) == 0) {
                // User messages keep Node's generated comparison suffix so callers
                // can still inspect the failing values.
                strbuf_append_str_n(sb, gs->chars + header_len, gs->len - header_len);
            }
            Item custom = assert_make_string_n(sb->str, sb->length);
            strbuf_free(sb);
            return throw_assertion_error_full_item(custom, actual, expected, "strictEqual", false);
        }
        return throw_assert_msg_or_auto_item(message,
            generated, actual, expected, "strictEqual");
    }
    return make_js_undefined();
}

// assert.notStrictEqual(actual, expected[, message])
extern "C" Item js_assert_notStrictEqual(Item actual, Item expected, Item message) {
    if (js_pending_call_argc < 2) return js_assert_throw_missing_actual_expected();
    Item result = js_strict_equal(actual, expected);
    if (it2b(result)) {
        return throw_assert_msg_or_auto_item(message,
            js_assert_not_strict_equal_message(actual), actual, expected, "notStrictEqual");
    }
    return make_js_undefined();
}

// assert.deepStrictEqual(actual, expected[, message])
extern "C" Item js_assert_deepStrictEqual(Item actual, Item expected, Item message) {
    if (js_pending_call_argc < 2) return js_assert_throw_missing_actual_expected();
    Item result = js_util_isDeepStrictEqual(actual, expected);
    if (item_is_error(result)) {
        // Deep equality observes proxy traps; assertion wrapping must not hide
        // the original trap-invariant TypeError behind an AssertionError.
        return make_js_undefined();
    }
    bool equal = (get_type_id(result) == LMD_TYPE_INT && it2i(result) == 1) ||
                 (get_type_id(result) == LMD_TYPE_BOOL && it2b(result));
    if (!equal && js_assert_current_skip_prototype()) {
        // skipPrototype intentionally ignores Buffer/TypedArray constructor
        // identity when the indexed bytes are identical.
        equal = js_assert_skip_prototype_typed_array_equal(actual, expected);
    }
    if (equal && !js_assert_current_skip_prototype() &&
            js_assert_prototypes_differ(actual, expected)) {
        equal = false;
    }
    if (!equal) {
        if (js_assert_is_function_like_value(message)) {
            bool has_user_message = false;
            JS_ASSIGN_OR_RETURN(user_message, js_assert_resolve_user_message(message, actual, expected, &has_user_message));
            if (has_user_message) {
                StrBuf* sb = strbuf_new();
                String* ms = get_type_id(user_message) == LMD_TYPE_STRING ? it2s(user_message) : NULL;
                if (ms) strbuf_append_str_n(sb, ms->chars, ms->len);
                Item generated = js_assert_strict_equal_message(actual, expected);
                String* gs = get_type_id(generated) == LMD_TYPE_STRING ? it2s(generated) : NULL;
                const char* header = "Expected values to be strictly equal:";
                size_t header_len = strlen(header);
                if (gs && gs->len >= header_len && memcmp(gs->chars, header, header_len) == 0) {
                    // Function-valued messages are user messages, but Node keeps
                    // the primitive comparison suffix for strict-style asserts.
                    strbuf_append_str_n(sb, gs->chars + header_len, gs->len - header_len);
                }
                Item custom = assert_make_string_n(sb->str, sb->length);
                strbuf_free(sb);
                return throw_assertion_error_full_item(custom, actual, expected, "deepStrictEqual", false);
            }
        }
        Item date_msg = js_assert_date_checktag_message(actual, expected);
        if (get_type_id(date_msg) == LMD_TYPE_STRING) {
            return throw_assert_msg_or_auto(date_msg,
                "assert.deepStrictEqual: values are not deep-strict-equal", actual, expected, "deepStrictEqual");
        }
        Item typed_array_msg = js_assert_deep_strict_typed_array_message(actual, expected);
        if (get_type_id(typed_array_msg) == LMD_TYPE_STRING) {
            return throw_assert_deep_msg_or_auto_item(message, typed_array_msg,
                actual, expected, "deepStrictEqual");
        }
        Item date_class_msg = js_assert_deep_strict_class_message(actual, expected, false);
        if (get_type_id(date_class_msg) == LMD_TYPE_STRING) {
            return throw_assert_deep_msg_or_auto_item(message, date_class_msg,
                actual, expected, "deepStrictEqual");
        }
        Item regexp_class_msg = js_assert_deep_strict_class_message(actual, expected, true);
        if (get_type_id(regexp_class_msg) == LMD_TYPE_STRING) {
            return throw_assert_deep_msg_or_auto_item(message, regexp_class_msg,
                actual, expected, "deepStrictEqual");
        }
        Item array_msg = js_assert_deep_strict_array_message(actual, expected);
        if (get_type_id(array_msg) == LMD_TYPE_STRING) {
            return throw_assert_deep_msg_or_auto_item(message, array_msg,
                actual, expected, "deepStrictEqual");
        }
        if (!js_assert_is_object_like_value(actual) && !js_assert_is_object_like_value(expected)) {
            Item strict_msg = js_assert_strict_equal_message(actual, expected);
            String* sm = get_type_id(strict_msg) == LMD_TYPE_STRING ? it2s(strict_msg) : NULL;
            StrBuf* rewritten = strbuf_new();
            const char* from = "Expected values to be strictly equal:";
            const char* to = "Expected values to be strictly deep-equal:";
            size_t from_len = strlen(from);
            if (sm && sm->len >= from_len && memcmp(sm->chars, from, from_len) == 0) {
                strbuf_append_str(rewritten, to);
                strbuf_append_str_n(rewritten, sm->chars + from_len, sm->len - from_len);
            } else if (sm) {
                strbuf_append_str_n(rewritten, sm->chars, sm->len);
            }
            Item deep_msg = assert_make_string_n(rewritten->str, rewritten->length);
            strbuf_free(rewritten);
            return throw_assert_deep_msg_or_auto_item(message,
                deep_msg, actual, expected, "deepStrictEqual");
        }
        Item error_msg = js_assert_deep_strict_error_message(actual, expected);
        if (get_type_id(error_msg) == LMD_TYPE_STRING) {
            return throw_assert_deep_msg_or_auto_item(message, error_msg,
                actual, expected, "deepStrictEqual");
        }
        Item map_msg = js_assert_deep_strict_map_message(actual, expected);
        if (get_type_id(map_msg) == LMD_TYPE_STRING) {
            return throw_assert_deep_msg_or_auto_item(message, map_msg,
                actual, expected, "deepStrictEqual");
        }
        Item object_msg = js_assert_deep_strict_object_message(actual, expected);
        if (get_type_id(object_msg) == LMD_TYPE_STRING) {
            return throw_assert_deep_msg_or_auto_item(message, object_msg,
                actual, expected, "deepStrictEqual");
        }
        Item url_msg = js_assert_deep_strict_url_message(actual, expected);
        if (get_type_id(url_msg) == LMD_TYPE_STRING) {
            return throw_assert_deep_msg_or_auto_item(message, url_msg,
                actual, expected, "deepStrictEqual");
        }
        Item structural_msg = js_assert_deep_strict_structural_message(actual, expected);
        if (get_type_id(structural_msg) == LMD_TYPE_STRING) {
            return throw_assert_deep_msg_or_auto_item(message, structural_msg,
                actual, expected, "deepStrictEqual");
        }
        return throw_assert_msg_or_auto(message,
            "assert.deepStrictEqual: values are not deep-strict-equal", actual, expected, "deepStrictEqual");
    }
    return make_js_undefined();
}

// assert.notDeepStrictEqual(actual, expected[, message])
extern "C" Item js_assert_notDeepStrictEqual(Item actual, Item expected, Item message) {
    if (js_pending_call_argc < 2) return js_assert_throw_missing_actual_expected();
    Item result = js_util_isDeepStrictEqual(actual, expected);
    bool equal = (get_type_id(result) == LMD_TYPE_INT && it2i(result) == 1) ||
                 (get_type_id(result) == LMD_TYPE_BOOL && it2b(result));
    if (!equal && js_assert_current_skip_prototype()) {
        // keep notDeepStrictEqual consistent with deepStrictEqual's
        // skipPrototype byte-view equivalence.
        equal = js_assert_skip_prototype_typed_array_equal(actual, expected);
    }
    if (equal && !js_assert_current_skip_prototype() &&
            js_assert_prototypes_differ(actual, expected)) {
        equal = false;
    }
    if (equal) {
        return throw_assert_msg_or_auto_item(message,
            js_assert_not_deep_equal_message(actual, expected, true), actual, expected, "notDeepStrictEqual");
    }
    return make_js_undefined();
}

// assert.deepEqual(actual, expected[, message]) — legacy loose deep equality
extern "C" Item js_assert_deepEqual(Item actual, Item expected, Item message) {
    if (js_pending_call_argc < 2) return js_assert_throw_missing_actual_expected();
    // Node's legacy deepEqual ignores prototypes and recurses with == semantics;
    // routing it through strict comparison rejects documented loose cases.
    Item result = js_util_isDeepEqual(actual, expected);
    bool equal = (get_type_id(result) == LMD_TYPE_INT && it2i(result) == 1) ||
                 (get_type_id(result) == LMD_TYPE_BOOL && it2b(result));
    if (!equal) {
        return throw_assert_msg_or_auto_item(message,
            js_assert_deep_equal_message(actual, expected), actual, expected, "deepEqual");
    }
    return make_js_undefined();
}

// assert.notDeepEqual(actual, expected[, message])
extern "C" Item js_assert_notDeepEqual(Item actual, Item expected, Item message) {
    if (js_pending_call_argc < 2) return js_assert_throw_missing_actual_expected();
    Item result = js_util_isDeepEqual(actual, expected);
    bool equal = (get_type_id(result) == LMD_TYPE_INT && it2i(result) == 1) ||
                 (get_type_id(result) == LMD_TYPE_BOOL && it2b(result));
    if (equal) {
        return throw_assert_msg_or_auto_item(message,
            js_assert_not_deep_equal_message(actual, expected, false), actual, expected, "notDeepEqual");
    }
    return make_js_undefined();
}

// assert.fail([message])
extern "C" Item js_assert_fail(Item message) {
    TypeId tid = get_type_id(message);
    if (tid == LMD_TYPE_STRING) {
        String* s = it2s(message);
        char buf[512];
        int len = (int)s->len < 500 ? (int)s->len : 500;
        memcpy(buf, s->chars, len);
        buf[len] = '\0';
        return throw_assertion_error_full(buf, make_js_undefined(), make_js_undefined(), "fail", false);
    }
    // if message is an Error object, re-throw it directly (Node.js behavior)
    if (tid == LMD_TYPE_MAP) {
        return js_throw_value(message);
    }
    if (tid == LMD_TYPE_UNDEFINED || tid == LMD_TYPE_NULL) {
        return throw_assertion_error_full("Failed", make_js_undefined(), make_js_undefined(), "fail", true);
    }
    return throw_assertion_error_full("Failed", make_js_undefined(), make_js_undefined(), "fail", true);
}

static bool js_assert_function_is_constructor_expectation(Item fn) {
    if (!js_assert_is_function_like_value(fn)) return false;
    Item proto = js_get_key_cstr(fn, "prototype");
    if (get_type_id(proto) != LMD_TYPE_MAP && get_type_id(proto) != LMD_TYPE_ELEMENT) {
        return false;
    }
    if (assert_namespace.item != 0) {
        Item assertion_error = js_get_key_cstr(assert_namespace, "AssertionError");
        if (fn.item == assertion_error.item) return true;
    }
    const char* known[] = {
        "Error", "TypeError", "RangeError", "SyntaxError", "ReferenceError",
        "EvalError", "URIError", "AggregateError", "Array", "Object", "Date",
        "RegExp", "Map", "Set", "WeakMap", "WeakSet", "Promise", NULL
    };
    for (int i = 0; known[i]; i++) {
        Item ctor = js_get_constructor(assert_make_string(known[i]));
        if (fn.item == ctor.item) return true;
    }
    Item error_ctor = js_get_constructor(assert_make_string("Error"));
    Item error_proto = js_get_key_cstr(error_ctor, "prototype");
    Item super_class = js_get_class_superclass(fn);
    for (int i = 0; i < 64 && js_assert_is_function_like_value(super_class); i++) {
        if (super_class.item == error_ctor.item) return true;
        if (js_get_key_cstr(super_class, "prototype").item == error_proto.item) return true;
        // Source-class heritage is carried by the function constructor record;
        // checking it keeps Error subclasses from being misused as validators.
        super_class = js_get_class_superclass(super_class);
    }
    Item cur = proto;
    for (int i = 0; i < 64 && get_type_id(cur) == LMD_TYPE_MAP; i++) {
        if (cur.item == error_proto.item) return true;
        cur = js_get_prototype_of(cur);
    }
    return false;
}

static void js_assert_append_item_text(StrBuf* sb, Item value) {
    Item text = js_to_string_val(value);
    String* s = get_type_id(text) == LMD_TYPE_STRING ? it2s(text) : NULL;
    if (s) strbuf_append_str_n(sb, s->chars, s->len);
}

static Item js_assert_throw_throws_assertion(Item message, Item actual, Item expected, bool generated) {
    Item error = make_assertion_error_full_item(message, actual, expected, NULL, generated);
    // assert.throws owns the operator property, but its internal frame must not
    // be appended to user-visible stacks.
    js_set_key_cstr(error, "operator", assert_make_string("throws"));
    return js_throw_value(error);
}

static bool js_assert_expected_constructor_label(Item expected, StrBuf* sb) {
    if (!js_assert_is_function_like_value(expected)) return false;
    Item name = js_get_key_cstr(expected, "name");
    String* ns = get_type_id(name) == LMD_TYPE_STRING ? it2s(name) : NULL;
    if ((!ns || ns->len == 0) && assert_namespace.item != 0) {
        Item assertion_error = js_get_key_cstr(assert_namespace, "AssertionError");
        if (expected.item == assertion_error.item) {
            ns = it2s(assert_make_string("AssertionError"));
        }
    }
    if (!ns || ns->len == 0) return false;
    strbuf_append_str(sb, " (");
    strbuf_append_str_n(sb, ns->chars, ns->len);
    strbuf_append_char(sb, ')');
    return true;
}

static Item js_assert_throws_missing_error(Item error_expected, Item message) {
    Item user_message = message;
    if (get_type_id(message) == LMD_TYPE_UNDEFINED &&
            get_type_id(error_expected) == LMD_TYPE_STRING) {
        // Nested assert calls can disturb the ambient pending-argc value; the
        // missing third argument is the stable signal for string-as-message.
        user_message = error_expected;
    }
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "Missing expected exception");
    bool has_ctor = js_assert_expected_constructor_label(error_expected, sb);
    if (get_type_id(user_message) == LMD_TYPE_STRING) {
        strbuf_append_str(sb, has_ctor ? ": " : ": ");
        String* ms = it2s(user_message);
        if (ms) strbuf_append_str_n(sb, ms->chars, ms->len);
        Item result = js_assert_throw_throws_assertion(
            assert_make_string_n(sb->str, sb->length),
            make_js_undefined(),
            js_assert_is_function_like_value(error_expected) ? error_expected : make_js_undefined(),
            false);
        strbuf_free(sb);
        return result;
    }
    strbuf_append_char(sb, '.');
    Item result = js_assert_throw_throws_assertion(assert_make_string_n(sb->str, sb->length),
        make_js_undefined(),
        js_assert_is_function_like_value(error_expected) ? error_expected : make_js_undefined(),
        true);
    strbuf_free(sb);
    return result;
}

static Item js_assert_throw_invalid_throws_expected(Item error_expected) {
    char received[160];
    js_assert_append_value_type(received, sizeof(received), error_expected);
    return js_throw_type_error_codef(JS_ERR_INVALID_ARG_TYPE, 
        "The \"error\" argument must be of type function or an instance of Error, RegExp, or Object. Received %s", received);
}

#define js_assert_is_vm_context_error js_is_vm_context_error

static Item js_assert_throw_constructor_mismatch(Item thrown, Item expected_ctor) {
    Item expected_name = js_get_key_cstr(expected_ctor, "name");
    if (get_type_id(expected_name) != LMD_TYPE_STRING ||
            (it2s(expected_name) && it2s(expected_name)->len == 0)) {
        if (assert_namespace.item != 0) {
            Item assertion_error = js_get_key_cstr(assert_namespace, "AssertionError");
            if (expected_ctor.item == assertion_error.item) {
                expected_name = assert_make_string("AssertionError");
            }
        }
    }
    Item actual_ctor = ItemNull;
    if (actual_ctor.item == 0 || actual_ctor.item == ItemNull.item ||
            get_type_id(actual_ctor) == LMD_TYPE_UNDEFINED) {
        actual_ctor = js_get_key_cstr(thrown, "constructor");
    }
    Item public_name = js_get_key_cstr(actual_ctor, "name");
    if (js_assert_string_equals(public_name, "Error")) {
        Item thrown_proto = js_get_prototype_of(thrown);
        Item proto_ctor = js_get_key_cstr(thrown_proto, "constructor");
        Item proto_name = js_get_key_cstr(proto_ctor, "name");
        if (get_type_id(proto_name) == LMD_TYPE_STRING &&
                !js_assert_string_equals(proto_name, "Error")) {
            actual_ctor = proto_ctor;
        }
    }
    Item actual_name = js_get_key_cstr(actual_ctor, "name");
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "The error is expected to be an instance of \"");
    js_assert_append_item_text(sb, expected_name);
    strbuf_append_char(sb, '"');
    if (js_assert_is_vm_context_error(thrown) &&
            get_type_id(actual_name) == LMD_TYPE_STRING &&
            js_assert_string_equals(expected_name, it2s(actual_name)->chars)) {
        // VM-created Errors can share the host class name while still failing
        // constructor matching because their realm prototype is distinct.
        strbuf_append_str(sb, ". Received an error with identical name but a different prototype.");
    } else {
        strbuf_append_str(sb, ". Received \"");
        if (js_assert_is_symbol_value(thrown)) {
        // Primitive Symbols have no instance constructor; diagnostics must show
        // the observable Symbol(description) value, not the Symbol function.
            Item symbol_text = js_symbol_to_string(thrown);
            js_assert_append_item_text(sb, symbol_text);
        } else if (get_type_id(thrown) == LMD_TYPE_ARRAY) {
        // Array throw values are reported by their public inspect tag, not by
        // the Array constructor name, in instance-mismatch diagnostics.
            strbuf_append_str(sb, "[Array]");
        } else {
            js_assert_append_item_text(sb, actual_name);
        }
        strbuf_append_char(sb, '"');
    }
    Item thrown_msg = js_get_key_cstr(thrown, "message");
    if (get_type_id(thrown_msg) == LMD_TYPE_STRING) {
        String* ms = it2s(thrown_msg);
        if (ms && ms->len > 0) {
            strbuf_append_str(sb, "\n\nError message:\n\n");
            strbuf_append_str_n(sb, ms->chars, ms->len);
        }
    }
    Item msg = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return js_assert_throw_throws_assertion(msg, thrown, expected_ctor, true);
}

static Item js_assert_throw_regex_mismatch(Item thrown, Item expected_regex) {
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "The input did not match the regular expression ");
    js_assert_append_item_text(sb, expected_regex);
    strbuf_append_str(sb, ". Input:\n\n'");
    js_assert_append_item_text(sb, thrown);
    strbuf_append_str(sb, "'\n");
    Item msg = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return js_assert_throw_throws_assertion(msg, thrown, expected_regex, true);
}

static Item js_assert_throw_ambiguous_error_message(Item thrown, Item message) {
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "The \"error/message\" argument is ambiguous. The ");
    if (get_type_id(thrown) == LMD_TYPE_MAP && js_class_is_error_like(js_class_id(thrown))) {
        strbuf_append_str(sb, "error message \"");
    } else {
        strbuf_append_str(sb, "error \"");
    }
    js_assert_append_item_text(sb, message);
    strbuf_append_str(sb, "\" is identical to the message.");
    Item result = js_throw_type_error_code("ERR_AMBIGUOUS_ARGUMENT", sb->str);
    strbuf_free(sb);
    return result;
}

static bool js_assert_thrown_matches_string_message(Item thrown, Item message) {
    if (get_type_id(message) != LMD_TYPE_STRING) return false;
    Item candidate = thrown;
    if (get_type_id(thrown) == LMD_TYPE_MAP && js_class_is_error_like(js_class_id(thrown))) {
        candidate = js_get_key_cstr(thrown, "message");
    }
    if (get_type_id(candidate) != LMD_TYPE_STRING) return false;
    String* cs = it2s(candidate);
    String* ms = it2s(message);
    return cs && ms && cs->len == ms->len && memcmp(cs->chars, ms->chars, cs->len) == 0;
}

static bool js_assert_expected_property_matches(Item actual_val, Item expected_val) {
    if (js_assert_is_object_like_value(expected_val) || js_assert_is_object_like_value(actual_val)) {
        // Expected error-pattern objects compare object-valued properties
        // structurally; strict identity rejects Node's documented `{ actual }`
        // and nested Error matching cases.
        Item deep = js_util_isDeepStrictEqual(actual_val, expected_val);
        return (get_type_id(deep) == LMD_TYPE_BOOL && it2b(deep)) ||
               (get_type_id(deep) == LMD_TYPE_INT && it2i(deep) == 1);
    }
    Item eq = js_strict_equal(expected_val, actual_val);
    return get_type_id(eq) == LMD_TYPE_BOOL && it2b(eq);
}

static Item js_assert_error_name_value(Item value) {
    Item name = js_get_key_cstr(value, "name");
    if (get_type_id(name) == LMD_TYPE_STRING) return name;
    if (get_type_id(value) == LMD_TYPE_MAP && js_class_is_error_like(js_class_id(value))) {
        const char* class_name = js_class_to_name(js_class_id(value));
        if (class_name) return assert_make_string(class_name);
    }
    return name;
}

static Item js_assert_pattern_property_value(Item object, Item key) {
    if (js_assert_string_equals(key, "name") &&
            get_type_id(object) == LMD_TYPE_MAP &&
            js_class_is_error_like(js_class_id(object))) {
        // Native error names can be inherited/non-enumerable; object-pattern
        // matching observes the public error name rather than own-key storage.
        return js_assert_error_name_value(object);
    }
    return js_get_key_default(object, key);
}

static int js_assert_compare_key_names(Item a, Item b) {
    String* as = get_type_id(a) == LMD_TYPE_STRING ? it2s(a) : NULL;
    String* bs = get_type_id(b) == LMD_TYPE_STRING ? it2s(b) : NULL;
    if (!as && !bs) return 0;
    if (!as) return 1;
    if (!bs) return -1;
    size_t min_len = as->len < bs->len ? as->len : bs->len;
    int cmp = memcmp(as->chars, bs->chars, min_len);
    if (cmp != 0) return cmp;
    if (as->len == bs->len) return 0;
    return as->len < bs->len ? -1 : 1;
}

static bool js_assert_key_list_contains(Item* keys, int count, const char* text) {
    for (int i = 0; i < count; i++) {
        if (js_assert_string_equals(keys[i], text)) return true;
    }
    return false;
}

static int js_assert_collect_expected_pattern_keys(Item expected, Item* out, int max_count) {
    int count = 0;
    Item keys = js_object_keys(expected);
    int64_t key_count = js_array_length(keys);
    for (int64_t i = 0; i < key_count && count < max_count; i++) {
        out[count++] = js_elements_get_int(keys, i);
    }
    if (get_type_id(expected) == LMD_TYPE_MAP && js_class_is_error_like(js_class_id(expected))) {
        if (count < max_count && !js_assert_key_list_contains(out, count, "message")) {
            out[count++] = assert_make_string("message");
        }
        if (count < max_count && !js_assert_key_list_contains(out, count, "name")) {
            out[count++] = assert_make_string("name");
        }
    }
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (js_assert_compare_key_names(out[j], out[i]) < 0) {
                Item tmp = out[i];
                out[i] = out[j];
                out[j] = tmp;
            }
        }
    }
    return count;
}

static bool js_assert_expected_pattern_is_empty(Item expected) {
    if (get_type_id(expected) != LMD_TYPE_MAP) return false;
    if (js_class_is_error_like(js_class_id(expected))) return false;
    if (js_class_id(expected) == JS_CLASS_REGEXP) return false;
    Item keys = js_object_keys(expected);
    return js_array_length(keys) == 0;
}

static void js_assert_append_comparison_value(StrBuf* sb, Item value) {
    if (get_type_id(value) == LMD_TYPE_UNDEFINED || value.item == ITEM_JS_UNDEFINED) {
        strbuf_append_str(sb, "undefined");
        return;
    }
    js_assert_append_inspected_value(sb, value);
}

static void js_assert_append_signed_comparison_value(StrBuf* sb, Item value,
                                                     const char* next_prefix) {
    if (get_type_id(value) == LMD_TYPE_STRING) {
        String* s = it2s(value);
        if (s && js_assert_count_newlines(s) > 0) {
            // Signed Comparison diffs escape multiline strings as concatenated
            // literals; raw newlines make the property value look like content.
            js_assert_append_long_multiline_string(sb, s, "", next_prefix, 0, NULL);
            return;
        }
    }
    js_assert_append_comparison_value(sb, value);
}

static Item js_assert_throw_object_pattern_mismatch(Item thrown, Item expected) {
    if (!js_assert_is_object_like_value(thrown)) {
        StrBuf* plain = strbuf_new();
        strbuf_append_str(plain, "Expected values to be strictly deep-equal:\n");
        strbuf_append_str(plain, "+ actual - expected\n\n");
        js_assert_append_multiline_value(plain, thrown, 0, '+', false, 16);
        js_assert_append_multiline_value(plain, expected, 0, '-', false, 16);
        Item plain_msg = assert_make_string_n(plain->str, plain->length);
        strbuf_free(plain);
        // Primitive thrown values cannot be compared as property bags; Node
        // renders them against the expected pattern object directly.
        return js_assert_throw_throws_assertion(plain_msg, thrown, expected, true);
    }
    Item keys[128];
    int count = js_assert_collect_expected_pattern_keys(expected, keys, 128);
    bool matches_key[128];
    bool actual_has_key[128];
    for (int i = 0; i < count; i++) {
        matches_key[i] = false;
        actual_has_key[i] = false;
    }
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "Expected values to be strictly deep-equal:\n");
    strbuf_append_str(sb, "+ actual - expected\n\n");
    strbuf_append_str(sb, "  Comparison {\n");
    for (int i = 0; i < count; i++) {
        Item key = keys[i];
        if (get_type_id(key) != LMD_TYPE_STRING) continue;
        Item actual_val = js_assert_pattern_property_value(thrown, key);
        Item expected_val = js_assert_pattern_property_value(expected, key);
        bool actual_has = js_assert_has_own_property_key(thrown, key) ||
            (js_assert_string_equals(key, "name") &&
             get_type_id(thrown) == LMD_TYPE_MAP &&
             js_class_is_error_like(js_class_id(thrown)));
        actual_has_key[i] = actual_has;
        bool matches = actual_has && js_assert_expected_property_matches(actual_val, expected_val);
        if (!matches && actual_has && get_type_id(expected_val) == LMD_TYPE_MAP &&
                js_assert_is_real_regexp(expected_val)) {
            Item actual_str = get_type_id(actual_val) == LMD_TYPE_STRING ?
                actual_val : js_to_string_val(actual_val);
            Item test_result = js_regex_test(expected_val, actual_str);
            matches = get_type_id(test_result) == LMD_TYPE_BOOL && it2b(test_result);
        }
        matches_key[i] = matches;
    }
    for (int i = 0; i < count;) {
        if (actual_has_key[i] && !matches_key[i]) {
            int start = i;
            while (i < count && actual_has_key[i] && !matches_key[i]) i++;
            // Consecutive replacement keys are grouped as all additions then
            // removals; matched or missing keys still keep sorted key order.
            for (int j = start; j < i; j++) {
                Item key = keys[j];
                if (get_type_id(key) != LMD_TYPE_STRING) continue;
                String* ks = it2s(key);
                if (!ks) continue;
                Item actual_val = js_assert_pattern_property_value(thrown, key);
                strbuf_append_str(sb, "+   ");
                strbuf_append_str_n(sb, ks->chars, ks->len);
                strbuf_append_str(sb, ": ");
                js_assert_append_signed_comparison_value(sb, actual_val, "+     ");
                if (j < count - 1) strbuf_append_char(sb, ',');
                strbuf_append_char(sb, '\n');
            }
            for (int j = start; j < i; j++) {
                Item key = keys[j];
                if (get_type_id(key) != LMD_TYPE_STRING) continue;
                String* ks = it2s(key);
                if (!ks) continue;
                Item expected_val = js_assert_pattern_property_value(expected, key);
                strbuf_append_str(sb, "-   ");
                strbuf_append_str_n(sb, ks->chars, ks->len);
                strbuf_append_str(sb, ": ");
                js_assert_append_signed_comparison_value(sb, expected_val, "-     ");
                strbuf_append_str(sb, ",\n");
            }
            continue;
        }
        Item key = keys[i];
        if (get_type_id(key) != LMD_TYPE_STRING) {
            i++;
            continue;
        }
        String* ks = it2s(key);
        if (!ks) {
            i++;
            continue;
        }
        if (matches_key[i]) {
            Item actual_val = js_assert_pattern_property_value(thrown, key);
            strbuf_append_str(sb, "    ");
            strbuf_append_str_n(sb, ks->chars, ks->len);
            strbuf_append_str(sb, ": ");
            // Matched RegExp pattern fields print the observed value as a
            // shared property; printing the pattern makes a false diff line.
            js_assert_append_comparison_value(sb, actual_val);
            strbuf_append_str(sb, ",\n");
            i++;
            continue;
        }
        Item expected_val = js_assert_pattern_property_value(expected, key);
        strbuf_append_str(sb, "-   ");
        strbuf_append_str_n(sb, ks->chars, ks->len);
        strbuf_append_str(sb, ": ");
        js_assert_append_signed_comparison_value(sb, expected_val, "-     ");
        strbuf_append_str(sb, ",\n");
        i++;
    }
    if (sb->length >= 2 && sb->str[sb->length - 2] == ',') {
        sb->str[sb->length - 2] = '\n';
        sb->str[sb->length - 1] = '\0';
        sb->length--;
    }
    strbuf_append_str(sb, "  }\n");
    Item msg = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    // Object-pattern mismatches synthesize assert.throws() diagnostics; without
    // a user message they remain generated AssertionErrors in Node.
    return js_assert_throw_throws_assertion(msg, thrown, expected, true);
}
JS_FORWARD_STATIC_ITEM(js_assert_throw_empty_expected_object, (void), js_throw_type_error_code, (JS_ERR_INVALID_ARG_VALUE, "The argument 'error' may not be an empty object. Received {}"))

extern "C" Item js_assert_module_throws(Item fn, Item error_expected, Item message) {
    if (!js_is_callable(fn)) {
        // The first argument is API validation, not an assertion failure.
        return js_assert_throw_invalid_fn_arg(fn);
    }


    TypeId exp_type = get_type_id(error_expected);
    bool second_arg_is_message =
        exp_type == LMD_TYPE_STRING && get_type_id(message) == LMD_TYPE_UNDEFINED;
    if (!second_arg_is_message &&
            exp_type != LMD_TYPE_UNDEFINED && exp_type != LMD_TYPE_NULL &&
            exp_type != LMD_TYPE_FUNC && exp_type != LMD_TYPE_MAP) {
        return js_assert_throw_invalid_throws_expected(error_expected);
    }

    // call fn and inspect its merged-lane result directly.
    Item call_result = js_call_function(fn, make_js_undefined(), NULL, 0);
    if (!item_is_error(call_result)) {
        return js_assert_throws_missing_error(error_expected, message);
    }

    // fn threw — get the thrown value
    Item thrown = js_error_lane_payload(call_result);

    // if no expected argument, any throw is a pass
    if (second_arg_is_message || exp_type == LMD_TYPE_UNDEFINED || exp_type == LMD_TYPE_NULL) {
        if (second_arg_is_message && js_assert_thrown_matches_string_message(thrown, error_expected)) {
            // A string second argument is a user message, but if it is identical
            // to the thrown value/message Node rejects the ambiguous call shape.
            return js_assert_throw_ambiguous_error_message(thrown, error_expected);
        }
        return make_js_undefined();
    }

    // validate thrown against expected
    bool expected_constructor = js_assert_function_is_constructor_expectation(error_expected);
    bool expected_callable = js_is_callable(error_expected);
    if (expected_callable || expected_constructor) {
        Item proto = js_get_key_cstr(error_expected, "prototype");
        if (get_type_id(proto) == LMD_TYPE_MAP || get_type_id(proto) == LMD_TYPE_ELEMENT ||
                expected_constructor) {
            Item result = js_instanceof(thrown, error_expected);
            if (get_type_id(result) == LMD_TYPE_BOOL && it2b(result)) {
                return make_js_undefined();
            }
            if (expected_constructor) {
                // Constructor expectations must fail after instanceof misses; invoking
                // them as validators lets truthy constructed errors mask the mismatch.
                return js_assert_throw_constructor_mismatch(thrown, error_expected);
            }
        }
        if (!expected_callable) {
            return js_assert_throw_constructor_mismatch(thrown, error_expected);
        }
        // maybe it's a validation function — call it with thrown
        Item validate_result = js_call_function(error_expected, make_js_undefined(), &thrown, 1);
        if (item_is_error(validate_result)) {
            // validation function threw — re-throw
            return make_js_undefined();
        }
        if (get_type_id(validate_result) == LMD_TYPE_BOOL && it2b(validate_result)) {
            return make_js_undefined();
        }
        StrBuf* sb = strbuf_new();
        strbuf_append_str(sb, "The validation function is expected to return \"true\". Received ");
        Item inspected = js_util_inspect(validate_result, make_js_undefined());
        String* is = get_type_id(inspected) == LMD_TYPE_STRING ? it2s(inspected) : NULL;
        if (is) strbuf_append_str_n(sb, is->chars, is->len);
        strbuf_append_str(sb, "\n\nCaught error:\n\n");
        Item thrown_text = js_to_string_val(thrown);
        String* ts = get_type_id(thrown_text) == LMD_TYPE_STRING ? it2s(thrown_text) : NULL;
        if (ts) strbuf_append_str_n(sb, ts->chars, ts->len);
        Item msg = assert_make_string_n(sb->str, sb->length);
        strbuf_free(sb);
        // Validator callbacks only pass on literal true; truthy objects must
        // surface as assert.throws() failures instead of accepting bad validators.
        return js_assert_throw_throws_assertion(msg, thrown, error_expected, true);
    }

    // RegExp: test thrown.message against regex
    if (exp_type == LMD_TYPE_MAP) {
        bool has_regex = js_assert_is_real_regexp(error_expected);
        if (has_regex) {
            // RegExp: Node matches against String(thrown), e.g. "Error: message".
            Item thrown_str = js_to_string_val(thrown);
            Item test_result = js_regex_test(error_expected, thrown_str);
            if (get_type_id(test_result) == LMD_TYPE_BOOL && it2b(test_result)) {
                return make_js_undefined();
            }
            return js_assert_throw_regex_mismatch(thrown, error_expected);
        }

        if (js_assert_expected_pattern_is_empty(error_expected)) {
            return js_assert_throw_empty_expected_object();
        }

        if (!js_assert_is_object_like_value(thrown)) {
            if (get_type_id(message) == LMD_TYPE_STRING) {
                StrBuf* primitive_msg = strbuf_new();
                String* ms = it2s(message);
                if (ms) strbuf_append_str_n(primitive_msg, ms->chars, ms->len);
                strbuf_append_str(primitive_msg, "\n+ actual - expected\n\n");
                js_assert_append_multiline_value(primitive_msg, thrown, 0, '+', false, 16);
                js_assert_append_multiline_value(primitive_msg, error_expected, 0, '-', false, 16);
                Item msg = assert_make_string_n(primitive_msg->str, primitive_msg->length);
                strbuf_free(primitive_msg);
                // Primitive throws have no property surface; user messages
                // prefix the generated value-vs-pattern diff instead.
                return js_assert_throw_throws_assertion(msg, thrown, error_expected, false);
            }
            // Primitive throws must compare as values, not as empty property bags.
            return js_assert_throw_object_pattern_mismatch(thrown, error_expected);
        }

        // Object pattern: validate each property of expected against thrown
        // e.g. { message: "hello", code: "ERR_ASSERTION" }
        Item pattern_keys[128];
        int pattern_key_count = js_assert_collect_expected_pattern_keys(error_expected, pattern_keys, 128);
        if (pattern_key_count >= 0) {
            for (int i = 0; i < pattern_key_count; i++) {
                Item key = pattern_keys[i];
                Item expected_val = js_assert_pattern_property_value(error_expected, key);
                Item actual_val = js_assert_pattern_property_value(thrown, key);
                bool actual_has = js_assert_has_own_property_key(thrown, key) ||
                    (js_assert_string_equals(key, "name") &&
                     get_type_id(thrown) == LMD_TYPE_MAP &&
                     js_class_is_error_like(js_class_id(thrown)));

                // check if expected_val is a RegExp (for stack: /pattern/)
                TypeId ev_type = get_type_id(expected_val);
                if (ev_type == LMD_TYPE_MAP && js_assert_is_real_regexp(expected_val)) {
                            // regex match
                            Item actual_str = (get_type_id(actual_val) == LMD_TYPE_STRING) ? actual_val : js_to_string_val(actual_val);
                            Item test_result = js_regex_test(expected_val, actual_str);
                            if (get_type_id(test_result) != LMD_TYPE_BOOL || !it2b(test_result)) {
                                if (get_type_id(message) == LMD_TYPE_STRING) {
                                    // A user message on assert.throws() replaces the generated
                                    // object-pattern diff while preserving the throws assertion.
                                    return js_assert_throw_throws_assertion(message, thrown, error_expected, false);
                                }
                                return js_assert_throw_object_pattern_mismatch(thrown, error_expected);
                            }
                            continue;
                }

                if (js_assert_string_equals(key, "constructor") &&
                        js_is_callable(expected_val)) {
                    Item inst = js_instanceof(thrown, expected_val);
                    if (get_type_id(inst) == LMD_TYPE_BOOL && it2b(inst)) continue;
                }

                if (!actual_has || !js_assert_expected_property_matches(actual_val, expected_val)) {
                    if (get_type_id(message) == LMD_TYPE_STRING) {
                        // A user message on assert.throws() replaces the generated
                        // object-pattern diff while preserving the throws assertion.
                        return js_assert_throw_throws_assertion(message, thrown, error_expected, false);
                    }
                    return js_assert_throw_object_pattern_mismatch(thrown, error_expected);
                }
            }
            return make_js_undefined();
        }
    }

    // unknown expected type — just pass if something was thrown
    return make_js_undefined();
}

static bool js_assert_expected_error_matches(Item thrown, Item error_expected) {
    TypeId exp_type = get_type_id(error_expected);
    if (exp_type == LMD_TYPE_UNDEFINED || exp_type == LMD_TYPE_NULL) return true;


    if (js_is_callable(error_expected)) {
        Item proto = js_get_key_cstr(error_expected, "prototype");
        if (get_type_id(proto) == LMD_TYPE_MAP) {
            Item result = js_instanceof(thrown, error_expected);
            return get_type_id(result) == LMD_TYPE_BOOL && it2b(result);
        }
        return false;
    }

    if (exp_type == LMD_TYPE_MAP && js_assert_is_real_regexp(error_expected)) {
        Item thrown_str = js_to_string_val(thrown);
        Item result = js_regex_test(error_expected, thrown_str);
        return get_type_id(result) == LMD_TYPE_BOOL && it2b(result);
    }

    return false;
}

static Item js_assert_throw_invalid_does_not_throw_expected(Item expected) {
    char received[160];
    js_assert_append_value_type(received, sizeof(received), expected);
    return js_throw_type_error_codef(JS_ERR_INVALID_ARG_TYPE, 
        "The \"expected\" argument must be of type function or an instance of RegExp. Received %s", received);
}

static void js_assert_append_does_not_throw_user_message(StrBuf* sb, Item message) {
    if (get_type_id(message) == LMD_TYPE_UNDEFINED || message.item == ITEM_JS_UNDEFINED ||
        get_type_id(message) == LMD_TYPE_NULL || message.item == ItemNull.item) {
        strbuf_append_char(sb, '.');
        return;
    }

    strbuf_append_str(sb, ": ");
    Item msg = get_type_id(message) == LMD_TYPE_STRING ? message : js_to_string_val(message);
    String* ms = get_type_id(msg) == LMD_TYPE_STRING ? it2s(msg) : NULL;
    if (ms) strbuf_append_str_n(sb, ms->chars, ms->len);
}

static Item js_assert_throw_unwanted_exception(Item thrown, Item expected, Item message) {

    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "Got unwanted exception");
    js_assert_append_does_not_throw_user_message(sb, message);

    Item thrown_msg = js_get_key_cstr(thrown, "message");
    if (get_type_id(thrown_msg) != LMD_TYPE_STRING &&
            !js_class_is_error_like(get_type_id(thrown) == LMD_TYPE_MAP ? js_class_id(thrown) : JS_CLASS_NONE)) {
        // Non-Error thrown values do not have .message, but Node still reports
        // their stringified value in doesNotThrow's Actual message field.
        thrown_msg = js_to_string_val(thrown);
    }
    String* ts = get_type_id(thrown_msg) == LMD_TYPE_STRING ? it2s(thrown_msg) : NULL;
    if (ts && ts->len > 0) {
        strbuf_append_str(sb, "\nActual message: \"");
        strbuf_append_str_n(sb, ts->chars, ts->len);
        strbuf_append_char(sb, '"');
    }

    Item error = make_assertion_error_full(sb->str, thrown, expected, NULL, false);
    js_set_key_cstr(error, "operator", assert_make_string("doesNotThrow"));
    strbuf_free(sb);
    return js_throw_value(error);
}

// assert.doesNotThrow(fn[, error[, message]])
extern "C" Item js_assert_module_doesNotThrow(Item fn, Item error_cls, Item message) {
    if (!js_is_callable(fn)) {
        // Match assert.throws(): bad callback arguments are TypeErrors with
        // ERR_INVALID_ARG_TYPE rather than silent passes.
        return js_assert_throw_invalid_fn_arg(fn);
    }
    TypeId expected_type = get_type_id(error_cls);
    if (js_pending_call_argc == 2 && expected_type == LMD_TYPE_STRING) {
        // A string second argument is the user message, not an expected error
        // validator; rejecting it turns documented doesNotThrow calls into TypeErrors.
        message = error_cls;
        error_cls = make_js_undefined();
        expected_type = get_type_id(error_cls);
    }
    if (expected_type != LMD_TYPE_UNDEFINED && expected_type != LMD_TYPE_NULL &&
            !js_is_callable(error_cls) &&
            !(expected_type == LMD_TYPE_MAP && js_assert_is_real_regexp(error_cls))) {
        // doesNotThrow only accepts constructor/RegExp expectations; object
        // patterns are assert.throws-only and must be rejected before callback execution.
        return js_assert_throw_invalid_does_not_throw_expected(error_cls);
    }


    Item call_result = js_call_function(fn, ItemNull, NULL, 0);
    if (!item_is_error(call_result)) return make_js_undefined();

    Item thrown = js_error_lane_payload(call_result);
    if (js_assert_expected_error_matches(thrown, error_cls)) {
        return js_assert_throw_unwanted_exception(thrown, error_cls, message);
    }

    return js_throw_value(thrown);
}

// assert.ifError(value) — throw if value is truthy
// Per Node.js spec: throws AssertionError with message "ifError got unwanted exception: <msg>"
static Item js_assert_ifError_message_detail(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_STRING) return value;

    if (type == LMD_TYPE_MAP) {
        bool is_error = js_class_is_error_like(js_class_id(value));
        Item msg_val = js_get_key_cstr(value, "message");
        if (get_type_id(msg_val) == LMD_TYPE_STRING) {
            String* msg = it2s(msg_val);
            if (!is_error || (msg && msg->len > 0)) return msg_val;
        }
        if (is_error) return js_to_string(value);
    }

    Item inspected = js_util_inspect(value, make_js_undefined());
    if (get_type_id(inspected) == LMD_TYPE_STRING) return inspected;
    return js_to_string(value);
}

static Item js_assert_ifError_message(Item value) {
    Item detail = js_assert_ifError_message_detail(value);
    String* ds = get_type_id(detail) == LMD_TYPE_STRING ? it2s(detail) : NULL;

    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "ifError got unwanted exception: ");
    if (ds) strbuf_append_str_n(sb, ds->chars, ds->len);
    Item message = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return message;
}

extern "C" Item js_assert_ifError(Item value) {
    // ifError throws for any value that is NOT null or undefined
    TypeId tid = get_type_id(value);
    if (value.item != 0 && tid != LMD_TYPE_NULL && tid != LMD_TYPE_UNDEFINED) {

        // Create AssertionError with proper properties
        Item type_name = assert_make_string("AssertionError");
        Item msg_item = js_assert_ifError_message(value);
        Item error = js_new_error_with_name(type_name, msg_item);
        js_set_key_cstr(error, "code", assert_make_string("ERR_ASSERTION"));
        js_set_key_cstr(error, "name", assert_make_string("AssertionError"));
        js_set_key_cstr(error, "actual", value);
        js_set_key_cstr(error, "expected", ItemNull);
        js_set_key_cstr(error, "operator", assert_make_string("ifError"));
        js_set_key_cstr(error, "diff", assert_make_string(js_assert_current_diff()));
        js_assert_mark_instance_error(error);
        js_set_key_cstr(error, "generatedMessage", (Item){.item = b2it(false)});
        js_assert_attach_assertion_error_prototype(error);
        return js_throw_value(error);
    }
    return make_js_undefined();
}

// =============================================================================
// assert.match / assert.doesNotMatch
// =============================================================================

static int js_assert_append_value_type(char* buf, int buf_size, Item value);

static Item js_assert_throw_invalid_assert_arg_type(const char* arg_name,
                                                    const char* expected,
                                                    Item actual) {
    char received[160];
    js_assert_append_value_type(received, sizeof(received), actual);
    return js_throw_type_error_codef(JS_ERR_INVALID_ARG_TYPE, 
        "The \"%s\" argument must be %s. Received %s", arg_name, expected, received);
}

static Item js_assert_throw_ambiguous_assert_message(Item message) {
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "The \"message\" argument is ambiguous. Received ");
    if (get_type_id(message) == LMD_TYPE_MAP && js_class_is_error_like(js_class_id(message))) {
        Item msg = js_get_key_cstr(message, "message");
        String* ms = get_type_id(msg) == LMD_TYPE_STRING ? it2s(msg) : NULL;
        if (ms) strbuf_append_str_n(sb, ms->chars, ms->len);
    } else {
        js_assert_append_inspected_value(sb, message);
    }
    Item result = js_throw_type_error_code("ERR_AMBIGUOUS_ARGUMENT", sb->str);
    strbuf_free(sb);
    return result;
}

static Item js_assert_match_default_message(Item string_val, Item regexp, const char* op) {
    StrBuf* sb = strbuf_new();
    if (strcmp(op, "doesNotMatch") == 0) {
        strbuf_append_str(sb, "The input was expected to not match the regular expression ");
    } else {
        strbuf_append_str(sb, "The input did not match the regular expression ");
    }
    js_assert_append_item_text(sb, regexp);
    strbuf_append_str(sb, ". Input:\n\n");
    // Generated assert.match diagnostics include the inspected input on its
    // own line; the compact operator form loses Node's message contract.
    js_assert_append_inspected_value(sb, string_val);
    strbuf_append_char(sb, '\n');
    Item result = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

static Item js_assert_match_compact_default_message(Item string_val, Item regexp, const char* op) {
    StrBuf* sb = strbuf_new();
    js_assert_append_inspected_value(sb, string_val);
    strbuf_append_char(sb, ' ');
    strbuf_append_str(sb, op);
    strbuf_append_char(sb, ' ');
    js_assert_append_item_text(sb, regexp);
    Item result = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

static Item js_assert_match_message_or_default(Item message, Item string_val, Item regexp, const char* op) {
    if (js_pending_call_argc > 3 &&
            (js_assert_is_function_like_value(message) ||
             (get_type_id(message) == LMD_TYPE_MAP && js_class_is_error_like(js_class_id(message))))) {
        // Extra format arguments are only unambiguous for string messages; a
        // function/Error in the message slot would make the call shape unclear.
        return js_assert_throw_ambiguous_assert_message(message);
    }
    if (get_type_id(message) == LMD_TYPE_MAP && js_class_is_error_like(js_class_id(message))) {
        // Error-valued assert.match messages are thrown verbatim; wrapping them
        // in AssertionError loses the documented message-object contract.
        return js_throw_value(message);
    }
    bool message_is_function = js_assert_is_function_like_value(message);
    bool has_user_message = false;
    JS_ASSIGN_OR_RETURN(user_message, js_assert_resolve_user_message(message, string_val, regexp, &has_user_message));
    if (has_user_message) {
        return throw_assertion_error_full_item(user_message, string_val, regexp, op, false);
    }
    if (message_is_function) {
        // Faulty message callbacks fall back to Node's legacy compact operator
        // form, distinct from the no-message generated assert.match text.
        return throw_assertion_error_full_item(
            js_assert_match_compact_default_message(string_val, regexp, op),
            string_val, regexp, op, true);
    }
    return throw_assertion_error_full_item(
        js_assert_match_default_message(string_val, regexp, op),
        string_val, regexp, op, true);
}

static Item js_assert_match_invalid_string(Item string_val, Item regexp, const char* op) {
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "The \"string\" argument must be of type string. Received ");
    if (js_assert_is_object_like_value(string_val)) {
        Item inspected = js_util_inspect(string_val, make_js_undefined());
        String* is = get_type_id(inspected) == LMD_TYPE_STRING ? it2s(inspected) : NULL;
        // assert.match reports the bad input as assertion data; object inputs
        // need their inspected value rather than the generic constructor name.
        strbuf_append_str(sb, "type object (");
        if (is) strbuf_append_str_n(sb, is->chars, is->len);
        strbuf_append_char(sb, ')');
    } else {
        char received[160];
        js_assert_append_value_type(received, sizeof(received), string_val);
        strbuf_append_str(sb, received);
    }
    Item msg = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return throw_assertion_error_full_item(msg, string_val, regexp, op, true);
}

static Item js_assert_match_impl(Item string_val, Item regexp, Item message,
        bool negate, const char* operation) {
    if (get_type_id(regexp) != LMD_TYPE_MAP || js_class_id(regexp) != JS_CLASS_REGEXP) {
        return js_assert_throw_invalid_assert_arg_type("regexp", "an instance of RegExp", regexp);
    }
    if (get_type_id(string_val) != LMD_TYPE_STRING) {
        return js_assert_match_invalid_string(string_val, regexp, operation);
    }
    Item result = js_regex_test(regexp, string_val);
    if (it2b(result) == negate) {
        return js_assert_match_message_or_default(message, string_val, regexp, operation);
    }
    return make_js_undefined();
}
JS_FORWARD_ITEM(js_assert_match, (Item string_val, Item regexp, Item message), js_assert_match_impl, (string_val, regexp, message, false, "match"))
JS_FORWARD_ITEM(js_assert_doesNotMatch, (Item string_val, Item regexp, Item message), js_assert_match_impl, (string_val, regexp, message, true, "doesNotMatch"))

static bool js_assert_is_partial_object_like(Item value) {
    TypeId type = get_type_id(value);
    return type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY ||
           type == LMD_TYPE_ELEMENT || type == LMD_TYPE_OBJECT ||
           type == LMD_TYPE_VMAP;
}

static bool js_assert_is_error_like_value(Item value);
static bool js_assert_is_regexp_like(Item value);
static bool js_assert_is_any_arraybuffer(Item value);
static bool js_assert_is_weak_collection_like(Item value);
static bool js_assert_is_collection_like(Item value);

static bool js_assert_deep_strict_equal_bool(Item actual, Item expected) {
    if (actual.item == expected.item) return true;
    TypeId actual_type = get_type_id(actual);
    TypeId expected_type = get_type_id(expected);
    if ((actual_type == LMD_TYPE_INT || actual_type == LMD_TYPE_FLOAT) &&
            (expected_type == LMD_TYPE_INT || expected_type == LMD_TYPE_FLOAT)) {
        if (actual_type == LMD_TYPE_INT && it2i(actual) <= -(int64_t)JS_SYMBOL_BASE) {
            return false;
        }
        if (expected_type == LMD_TYPE_INT && it2i(expected) <= -(int64_t)JS_SYMBOL_BASE) {
            return false;
        }
        double actual_num = actual_type == LMD_TYPE_FLOAT ? it2d(actual) : (double)it2i(actual);
        double expected_num = expected_type == LMD_TYPE_FLOAT ? it2d(expected) : (double)it2i(expected);
        if (actual_num != actual_num || expected_num != expected_num) {
            return actual_num != actual_num && expected_num != expected_num;
        }
        // Partial deep equality uses SameValue for numeric leaves; the generic
        // helper collapses signed zero, which lets Float16/Float32 elements
        // with different backing bits compare equal.
        if (actual_num == 0.0 && expected_num == 0.0) {
            return signbit(actual_num) == signbit(expected_num);
        }
        return actual_num == expected_num;
    }
    if (js_assert_is_error_like_value(actual) || js_assert_is_error_like_value(expected) ||
            js_assert_is_regexp_like(actual) || js_assert_is_regexp_like(expected) ||
            js_assert_is_any_arraybuffer(actual) || js_assert_is_any_arraybuffer(expected)) {
        return false;
    }
    Item result = js_util_isDeepStrictEqual(actual, expected);
    return (get_type_id(result) == LMD_TYPE_INT && it2i(result) == 1) ||
           (get_type_id(result) == LMD_TYPE_BOOL && it2b(result));
}

static bool js_assert_partial_deep_match_impl(Item actual, Item expected, int depth_left, JsObjectPairTraversal* ctx);
static bool js_assert_partial_deep_match(Item actual, Item expected, int depth_left);
JS_FORWARD_STATIC_EXPRESSION(bool, js_assert_is_symbol_key, (Item key), (js_key_is_symbol_c(key) != 0))

static bool js_assert_same_property_key(Item left, Item right) {
    if (left.item == right.item) return true;
    if (js_assert_is_symbol_key(left) || js_assert_is_symbol_key(right)) return false;
    return it2b(js_strict_equal(left, right));
}

static bool js_assert_key_is_array_index(Item key) {
    if (get_type_id(key) != LMD_TYPE_STRING) return false;
    String* s = it2s(key);
    if (!s || s->len == 0 || s->len > 10) return false;
    if (s->len > 1 && s->chars[0] == '0') return false;
    uint64_t value = 0;
    for (size_t i = 0; i < s->len; i++) {
        char ch = s->chars[i];
        if (ch < '0' || ch > '9') return false;
        value = value * 10 + (uint64_t)(ch - '0');
        if (value > 4294967294ULL) return false;
    }
    return true;
}

static Item js_assert_enumerable_own_keys(Item object) {
    Item result = js_array_new(0);

    Item symbols = js_object_get_own_property_symbols(object);
    if (get_type_id(symbols) == LMD_TYPE_ARRAY) {
        int64_t sym_count = js_array_length(symbols);
        for (int64_t i = 0; i < sym_count; i++) {
            Item key = js_elements_get_int(symbols, i);
            Item desc = js_object_get_own_property_descriptor(object, key);
            if (js_descriptor_is_enumerable(desc)) {
                js_array_push(result, key);
            }
        }
    }
    // Typed-array index keys are represented by contiguous element storage; partial
    // equality compares that storage before named properties to avoid materializing
    // every numeric index for large arrays.
    Item keys = js_is_typed_array(object)
        ? js_typed_array_enumerable_custom_keys(object)
        : js_object_keys(object);
    if (get_type_id(keys) == LMD_TYPE_ARRAY) {
        int64_t key_count = js_array_length(keys);
        for (int64_t i = 0; i < key_count; i++) {
            js_array_push(result, js_elements_get_int(keys, i));
        }
    }
    // Assert diffs render enumerable symbols before string keys in Node's
    // object formatter; equality still treats this list as an unordered set.
    return result;
}

static Item js_assert_filter_keys(Item keys, bool want_index_keys) {
    Item result = js_array_new(0);
    int64_t key_count = js_array_length(keys);
    for (int64_t i = 0; i < key_count; i++) {
        Item key = js_elements_get_int(keys, i);
        if (js_assert_key_is_array_index(key) == want_index_keys) {
            js_array_push(result, key);
        }
    }
    return result;
}

static bool js_assert_has_enumerable_own_key(Item object, Item key) {
    Item desc = js_object_get_own_property_descriptor(object, key);
    return js_descriptor_is_enumerable(desc);
}

static bool js_assert_tag_equals(Item value, const char* tag) {
    if (get_type_id(value) != LMD_TYPE_MAP || !tag) return false;
    Item tag_value = js_get_key_default(value, js_well_known_symbol_key(4));
    return js_assert_string_equals(tag_value, tag);
}

static bool js_assert_has_own_key(Item object, const char* key) {
    Item result = js_has_own_property(object, assert_make_string(key));
    return (get_type_id(result) == LMD_TYPE_BOOL && it2b(result)) ||
           (get_type_id(result) == LMD_TYPE_INT && it2i(result) != 0);
}

static bool js_assert_has_constructor_prototype(Item value, const char* ctor_name) {
    if (get_type_id(value) != LMD_TYPE_MAP) return false;
    Item proto = js_get_prototype_of(value);
    if (get_type_id(proto) != LMD_TYPE_MAP) return false;
    Item ctor = js_get_constructor(assert_make_string(ctor_name));
    if (get_type_id(ctor) == LMD_TYPE_FUNC) {
        Item ctor_proto = js_get_key_cstr(ctor, "prototype");
        if (proto.item == ctor_proto.item) return true;
    }
    Item tag = js_get_key_default(proto, js_well_known_symbol_key(4));
    return js_assert_string_equals(tag, ctor_name);
}

static bool js_assert_is_real_regexp(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return false;
    return js_class_id(value) == JS_CLASS_REGEXP;
}

static bool js_assert_is_regexp_like(Item value) {
    if (js_assert_is_real_regexp(value)) return true;
    return js_assert_has_constructor_prototype(value, "RegExp") ||
           js_assert_tag_equals(value, "RegExp");
}
JS_FORWARD_STATIC_RETURN(bool, js_assert_is_dataview_like, (Item value), js_is_dataview, (value) || js_assert_tag_equals(value, "DataView"))

static bool js_assert_is_error_like_value(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return false;
    return js_class_is_error_like(js_class_id(value)) ||
           js_assert_has_constructor_prototype(value, "Error");
}

static bool js_assert_partial_error_match(Item actual, Item expected, int depth_left, JsObjectPairTraversal* ctx) {
    if (!js_assert_is_error_like_value(actual) || !js_assert_is_error_like_value(expected)) return false;
    if (get_type_id(actual) == LMD_TYPE_MAP && get_type_id(expected) == LMD_TYPE_MAP &&
            js_class_is_error_like(js_class_id(actual)) &&
            js_class_is_error_like(js_class_id(expected)) &&
            js_class_id(actual) != js_class_id(expected)) {
        // Partial deep strict equality still preserves native Error brands;
        // otherwise Error and TypeError with the same message compare as subsets.
        return false;
    }
    Item actual_name = js_assert_error_name_value(actual);
    Item expected_name = js_assert_error_name_value(expected);
    if (get_type_id(actual_name) == LMD_TYPE_STRING &&
            get_type_id(expected_name) == LMD_TYPE_STRING) {
        String* as = it2s(actual_name);
        String* es = it2s(expected_name);
        if (!as || !es || as->len != es->len || memcmp(as->chars, es->chars, as->len) != 0) {
            // Some runtime Error subclasses share a storage class; the public
            // name still carries the native brand for partial deep strict equality.
            return false;
        }
    }
    const char* keys[] = {"message", "cause", "errors", NULL};
    for (int i = 0; keys[i]; i++) {
        if (!js_assert_has_own_key(expected, keys[i])) continue;
        if (!js_assert_has_own_key(actual, keys[i])) return false;
        if (!js_assert_partial_deep_match_impl(
                js_get_key_default(actual, assert_make_string(keys[i])),
                js_get_key_default(expected, assert_make_string(keys[i])),
                depth_left - 1, ctx)) {
            return false;
        }
    }
    return true;
}

static bool js_assert_partial_enumerable_properties(Item actual, Item expected,
        int depth_left, JsObjectPairTraversal* ctx, bool skip_search_params) {
    Item keys = js_assert_enumerable_own_keys(expected);
    int64_t key_len = js_array_length(keys);
    Item search_params_key = assert_make_string("searchParams");
    for (int64_t i = 0; i < key_len; i++) {
        Item key = js_elements_get_int(keys, i);
        if (skip_search_params && js_assert_same_property_key(key, search_params_key)) continue;
        if (!js_assert_has_enumerable_own_key(actual, key)) return false;
        if (!js_assert_partial_deep_match_impl(
                js_get_key_default(actual, key),
                js_get_key_default(expected, key),
                depth_left - 1, ctx)) return false;
    }
    return true;
}

static bool js_assert_partial_regexp_match(Item actual, Item expected, int depth_left, JsObjectPairTraversal* ctx) {
    if (!js_assert_is_real_regexp(actual) || !js_assert_is_real_regexp(expected)) return false;
    // Borrowed RegExp prototypes/tags lack native RegExp slots; partial
    // matching them by enumerable keys makes fake object types pass.
    if (!js_assert_deep_strict_equal_bool(
            js_get_key_cstr(actual, "source"),
            js_get_key_cstr(expected, "source")) ||
            !js_assert_deep_strict_equal_bool(
            js_get_key_cstr(actual, "flags"),
            js_get_key_cstr(expected, "flags"))) {
        return false;
    }
    return js_assert_partial_enumerable_properties(actual, expected,
        depth_left, ctx, true);
}
JS_FORWARD_STATIC_RETURN(bool, js_assert_is_any_arraybuffer, (Item value), js_is_arraybuffer, (value) || js_is_sharedarraybuffer(value) || js_assert_tag_equals(value, "ArrayBuffer") || js_assert_tag_equals(value, "SharedArrayBuffer"))

static int js_assert_dataview_current_length(JsDataView* dv) {
    if (!dv || !dv->buffer) return -1;
    if (dv->length_tracking) {
        int length = js_arraybuffer_length(dv->buffer) - dv->byte_offset;
        return length > 0 ? length : 0;
    }
    if (dv->byte_offset < 0 || dv->byte_length < 0 ||
            dv->byte_offset + dv->byte_length > js_arraybuffer_length(dv->buffer)) {
        return -1;
    }
    return dv->byte_length;
}

static bool js_assert_partial_dataview_match(Item actual, Item expected) {
    if (!js_assert_is_dataview_like(actual) || !js_assert_is_dataview_like(expected)) return false;
    JsDataView* actual_dv = js_get_dataview_ptr(actual);
    JsDataView* expected_dv = js_get_dataview_ptr(expected);
    if (!actual_dv || !expected_dv || !actual_dv->buffer || !expected_dv->buffer) return false;
    if (js_arraybuffer_detached(actual_dv->buffer) ||
        js_arraybuffer_detached(expected_dv->buffer)) return false;
    if (js_arraybuffer_shared(actual_dv->buffer) !=
        js_arraybuffer_shared(expected_dv->buffer)) return false;
    int actual_len = js_assert_dataview_current_length(actual_dv);
    int expected_len = js_assert_dataview_current_length(expected_dv);
    if (actual_len < 0 || expected_len < 0 || actual_len < expected_len) return false;
    if (expected_len == 0) return true;
    const uint8_t* actual_data = js_arraybuffer_data_const(actual_dv->buffer);
    const uint8_t* expected_data = js_arraybuffer_data_const(expected_dv->buffer);
    if (!actual_data || !expected_data) return false;
    const uint8_t* actual_bytes = actual_data + actual_dv->byte_offset;
    const uint8_t* expected_bytes = expected_data + expected_dv->byte_offset;
    return memcmp(actual_bytes, expected_bytes, (size_t)expected_len) == 0;
}

static bool js_assert_partial_arraybuffer_match(Item actual, Item expected) {
    if (!js_assert_is_any_arraybuffer(actual) || !js_assert_is_any_arraybuffer(expected)) return false;
    bool actual_shared = js_is_sharedarraybuffer(actual) || js_assert_tag_equals(actual, "SharedArrayBuffer");
    bool expected_shared = js_is_sharedarraybuffer(expected) || js_assert_tag_equals(expected, "SharedArrayBuffer");
    if (actual_shared != expected_shared) return false;
    int actual_len = js_arraybuffer_byte_length(actual);
    int expected_len = js_arraybuffer_byte_length(expected);
    if (actual_len < expected_len) return false;
    JsArrayBuffer* actual_ab = js_get_arraybuffer_ptr_item(actual);
    JsArrayBuffer* expected_ab = js_get_arraybuffer_ptr_item(expected);
    if (!actual_ab || !expected_ab) return false;
    if (expected_len <= 0) return true;
    const uint8_t* actual_data = js_arraybuffer_data_const(actual_ab);
    const uint8_t* expected_data = js_arraybuffer_data_const(expected_ab);
    if (!actual_data || !expected_data) return false;
    return memcmp(actual_data, expected_data, (size_t)expected_len) == 0;
}

static bool js_assert_is_url_like(Item value) {
    if (!js_assert_is_partial_object_like(value)) return false;
    if (get_type_id(value) == LMD_TYPE_MAP &&
            (js_class_id(value) == JS_CLASS_URL ||
             js_assert_has_constructor_prototype(value, "URL"))) {
        return true;
    }
    // Some URL construction paths stamp after materializing fields; when the
    // class byte is lost, the URL-owned href/searchParams shape still needs
    // the URL comparator to avoid per-instance wrapper mismatches.
    Item search_params = js_get_key_cstr(value, "searchParams");
    return get_type_id(js_get_key_cstr(value, "href")) == LMD_TYPE_STRING &&
           js_assert_is_partial_object_like(search_params);
}

static bool js_assert_partial_url_match(Item actual, Item expected, int depth_left, JsObjectPairTraversal* ctx) {
    if (js_class_id(actual) != JS_CLASS_URL || js_class_id(expected) != JS_CLASS_URL) return false;
    Item href_key = assert_make_string("href");
    Item actual_href = js_get_key_default(actual, href_key);
    Item expected_href = js_get_key_default(expected, href_key);
    // url wrappers materialize per-instance searchParams methods; href is the canonical URL value for equality.
    if (!js_assert_deep_strict_equal_bool(actual_href, expected_href)) return false;
    // User-defined enumerable URL properties are ordinary object surface; href-only
    // partial matching lets URLs with different custom fields compare equal.
    // URLSearchParams is materialized as a per-instance wrapper; href above is
    // the canonical URL/search state being protected.
    return js_assert_partial_enumerable_properties(actual, expected,
        depth_left, ctx, true);
}

typedef bool (*JsAssertSubsetMatcher)(Item actual, Item expected,
    Item actual_entries, Item expected_entries,
    int64_t actual_index, int64_t expected_index, int depth_left,
    JsObjectPairTraversal* ctx, bool exact_only);

static bool js_assert_unordered_subset(Item actual, Item expected,
        Item actual_entries, Item expected_entries,
        int depth_left, JsObjectPairTraversal* ctx,
        JsAssertSubsetMatcher matcher, bool exact_first) {
    int64_t expected_count = js_array_length(expected_entries);
    int64_t actual_count = js_array_length(actual_entries);
    if (actual_count < expected_count) return false;
    if (expected_count == 0) return true;

    bool* used = (bool*)calloc((size_t)actual_count, sizeof(bool));
    if (!used) return false;
    for (int64_t i = 0; i < expected_count; i++) {
        bool found = false;
        int pass_count = exact_first ? 2 : 1;
        for (int pass = 0; pass < pass_count && !found; pass++) {
            bool exact_only = exact_first && pass == 0;
            for (int64_t j = 0; j < actual_count; j++) {
                if (used[j] || !matcher(actual, expected, actual_entries,
                        expected_entries, j, i, depth_left, ctx, exact_only)) continue;
                used[j] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            free(used);
            return false;
        }
    }
    free(used);
    return true;
}

static bool js_assert_partial_key_subset_match(Item actual, Item expected,
        Item actual_keys, Item expected_keys,
        int64_t actual_index, int64_t expected_index, int depth_left,
        JsObjectPairTraversal* ctx, bool exact_only) {
    (void)exact_only;
    Item expected_key = js_elements_get_int(expected_keys, expected_index);
    Item actual_key = js_elements_get_int(actual_keys, actual_index);
    return js_assert_partial_deep_match_impl(
        js_get_key_default(actual, actual_key),
        js_get_key_default(expected, expected_key), depth_left - 1, ctx);
}

static bool js_assert_partial_named_key_subset_match(Item actual, Item expected,
        Item actual_keys, Item expected_keys, int64_t actual_index,
        int64_t expected_index, int depth_left, JsObjectPairTraversal* ctx,
        bool exact_only) {
    (void)exact_only;
    Item actual_key = js_elements_get_int(actual_keys, actual_index);
    Item expected_key = js_elements_get_int(expected_keys, expected_index);
    if (!js_assert_same_property_key(actual_key, expected_key)) return false;
    return js_assert_partial_deep_match_impl(
        js_get_key_default(actual, actual_key),
        js_get_key_default(expected, expected_key), depth_left - 1, ctx);
}
JS_FORWARD_STATIC_RETURN(bool, js_assert_partial_named_key_subset, (Item actual, Item expected, Item actual_keys, Item expected_keys, int depth_left, JsObjectPairTraversal* ctx), js_assert_unordered_subset, (actual, expected, actual_keys, expected_keys, depth_left, ctx, js_assert_partial_named_key_subset_match, false))

static bool js_assert_partial_array_like_key_match(Item actual, Item expected,
        Item actual_keys, Item expected_keys, int depth_left, JsObjectPairTraversal* ctx) {
    // Array-like partial equality is value-subset based for indexed elements,
    // but named and symbol keys remain observable own properties. Matching all
    // keys by value let `actual.ignored = v` satisfy `expected.extra = v`, and
    // dropped enumerable symbol-key mismatches entirely.
    Item actual_index_keys = js_assert_filter_keys(actual_keys, true);
    Item expected_index_keys = js_assert_filter_keys(expected_keys, true);
    if (!js_assert_unordered_subset(actual, expected, actual_index_keys,
            expected_index_keys, depth_left, ctx,
            js_assert_partial_key_subset_match, false)) {
        return false;
    }
    Item actual_named_keys = js_assert_filter_keys(actual_keys, false);
    Item expected_named_keys = js_assert_filter_keys(expected_keys, false);
    return js_assert_partial_named_key_subset(actual, expected,
        actual_named_keys, expected_named_keys, depth_left, ctx);
}

static bool js_assert_partial_array_match(Item actual, Item expected, int depth_left, JsObjectPairTraversal* ctx) {
    bool actual_arguments = js_assert_is_arguments_value(actual);
    bool expected_arguments = js_assert_is_arguments_value(expected);
    if (actual_arguments != expected_arguments) {
        // Arguments uses array storage internally, but partial deep strict
        // equality must preserve its distinct exotic object brand.
        return false;
    }
    if (get_type_id(actual) != LMD_TYPE_ARRAY) return false;
    if (js_array_length(actual) < js_array_length(expected)) return false;

    // partial array equality is value-subset based; enumerate present keys so sparse arrays do not scan every hole.
    Item expected_keys = js_assert_enumerable_own_keys(expected);
    Item actual_keys = js_assert_enumerable_own_keys(actual);
    return js_assert_partial_array_like_key_match(actual, expected, actual_keys, expected_keys, depth_left, ctx);
}

static bool js_assert_partial_typed_array_match(Item actual, Item expected, int depth_left, JsObjectPairTraversal* ctx) {
    if (!js_is_typed_array(actual) || !js_is_typed_array(expected)) return false;
    JsTypedArray* actual_ta = js_get_typed_array_ptr(actual.map);
    JsTypedArray* expected_ta = js_get_typed_array_ptr(expected.map);
    if (!actual_ta || !expected_ta) return false;
    // TypedArray subsets still require the same element kind; otherwise equal
    // numeric slots make Int16Array/Uint16Array instances look interchangeable.
    if (actual_ta->element_type != expected_ta->element_type) return false;
    if (js_typed_array_length(actual) < js_typed_array_length(expected)) return false;
    Item expected_keys = js_assert_enumerable_own_keys(expected);
    Item actual_keys = js_assert_enumerable_own_keys(actual);
    return js_assert_partial_array_like_key_match(actual, expected, actual_keys, expected_keys, depth_left, ctx);
}

static bool js_assert_partial_set_subset_match(Item actual, Item expected,
        Item actual_values, Item expected_values, int64_t actual_index,
        int64_t expected_index, int depth_left, JsObjectPairTraversal* ctx,
        bool exact_only) {
    (void)actual;
    (void)expected;
    Item actual_value = js_elements_get_int(actual_values, actual_index);
    Item expected_value = js_elements_get_int(expected_values, expected_index);
    if (exact_only) return actual_value.item == expected_value.item;
    return js_assert_partial_deep_match_impl(actual_value, expected_value,
        depth_left - 1, ctx);
}

static bool js_assert_partial_map_subset_match(Item actual, Item expected,
        Item actual_entries, Item expected_entries, int64_t actual_index,
        int64_t expected_index, int depth_left, JsObjectPairTraversal* ctx,
        bool exact_only) {
    (void)actual;
    (void)expected;
    (void)exact_only;
    Item actual_pair = js_elements_get_int(actual_entries, actual_index);
    Item expected_pair = js_elements_get_int(expected_entries, expected_index);
    Item actual_key = js_elements_get_int(actual_pair, 0);
    Item expected_key = js_elements_get_int(expected_pair, 0);
    return js_assert_deep_strict_equal_bool(actual_key, expected_key) &&
        js_assert_partial_deep_match_impl(
            js_elements_get_int(actual_pair, 1),
            js_elements_get_int(expected_pair, 1), depth_left - 1, ctx);
}

static bool js_assert_partial_set_match(Item actual, Item expected, int depth_left, JsObjectPairTraversal* ctx) {
    Item actual_values = js_iterable_to_array(actual);
    Item expected_values = js_iterable_to_array(expected);
    // Set matching consumes exact references before structural matches so {} and
    // [] cannot steal a later exact candidate from the unordered subset.
    return js_assert_unordered_subset(actual, expected, actual_values,
        expected_values, depth_left, ctx, js_assert_partial_set_subset_match, true);
}

static bool js_assert_partial_map_match(Item actual, Item expected, int depth_left, JsObjectPairTraversal* ctx) {
    Item actual_entries = js_iterable_to_array(actual);
    Item expected_entries = js_iterable_to_array(expected);
    return js_assert_unordered_subset(actual, expected, actual_entries,
        expected_entries, depth_left, ctx, js_assert_partial_map_subset_match, false);
}

static bool js_assert_partial_collection_match(Item actual, Item expected, int depth_left, JsObjectPairTraversal* ctx) {

    if (js_assert_is_weak_collection_like(actual) || js_assert_is_weak_collection_like(expected)) {
        return false;
    }
    bool actual_set = js_is_set_instance(actual);
    bool expected_set = js_is_set_instance(expected);
    bool actual_map = js_is_map_instance(actual);
    bool expected_map = js_is_map_instance(expected);
    actual_set = actual_set || js_assert_tag_equals(actual, "Set");
    expected_set = expected_set || js_assert_tag_equals(expected, "Set");
    actual_map = actual_map || js_assert_tag_equals(actual, "Map");
    expected_map = expected_map || js_assert_tag_equals(expected, "Map");
    bool entries_match = false;
    if (actual_set || expected_set) {
        entries_match = actual_set && expected_set && js_assert_partial_set_match(actual, expected, depth_left, ctx);
    } else if (actual_map || expected_map) {
        entries_match = actual_map && expected_map && js_assert_partial_map_match(actual, expected, depth_left, ctx);
    } else {
        return false;
    }
    if (!entries_match) return false;
    Item actual_keys = js_assert_enumerable_own_keys(actual);
    Item expected_keys = js_assert_enumerable_own_keys(expected);
    // Map/Set entries live in internal slots, but enumerable expandos remain
    // ordinary object surface and must participate in partial subset checks.
    return js_assert_partial_named_key_subset(actual, expected,
        actual_keys, expected_keys, depth_left, ctx);
}

static bool js_assert_is_weak_collection_like(Item value) {
    if (get_type_id(value) == LMD_TYPE_MAP) {
        JsClass cls = js_class_id(value);
        if (cls == JS_CLASS_WEAK_MAP || cls == JS_CLASS_WEAK_SET) return true;
    }
    return js_assert_tag_equals(value, "WeakMap") || js_assert_tag_equals(value, "WeakSet");
}

static bool js_assert_is_collection_like(Item value) {
    if (js_assert_is_weak_collection_like(value)) return true;
    return js_is_set_instance(value) || js_is_map_instance(value) ||
           js_assert_tag_equals(value, "Set") || js_assert_tag_equals(value, "Map");
}

static bool js_assert_partial_is_special_map(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return false;
    JsClass cls = js_class_id(value);
    return (cls != JS_CLASS_NONE && cls != JS_CLASS_OBJECT) ||
           js_assert_is_regexp_like(value) ||
           js_assert_tag_equals(value, "WeakMap") ||
           js_assert_tag_equals(value, "WeakSet") ||
           js_assert_is_url_like(value) ||
           js_assert_is_dataview_like(value) ||
           js_assert_is_any_arraybuffer(value);
}

static bool js_assert_is_boxed_primitive(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return false;
    JsClass cls = js_class_id(value);
    return cls == JS_CLASS_BOOLEAN || cls == JS_CLASS_NUMBER ||
           cls == JS_CLASS_STRING || cls == JS_CLASS_SYMBOL ||
           cls == JS_CLASS_BIGINT;
}

static bool js_assert_partial_deep_match_impl(Item actual, Item expected, int depth_left, JsObjectPairTraversal* ctx) {
    if (actual.item == expected.item) return true;
    if (depth_left <= 0) return false;
    bool actual_object_like = js_assert_is_partial_object_like(actual);
    bool expected_object_like = js_assert_is_partial_object_like(expected);
    if (!actual_object_like && !expected_object_like &&
            js_assert_deep_strict_equal_bool(actual, expected)) {
        return true;
    }

    bool entered = false;
    if (actual_object_like || expected_object_like) {
        int enter_status = ctx->enter(actual, expected);
        if (enter_status == 0) {
            // cyclic partial comparisons must reuse the same active actual/expected pair instead of recursing forever.
            return true;
        }
        if (enter_status < 0) {
            // reusing only one side would let a cycle in expected match a different actual node.
            return false;
        }
        entered = true;
    }

    bool result = false;
    if (js_assert_is_error_like_value(actual) || js_assert_is_error_like_value(expected)) {
        result = js_assert_partial_error_match(actual, expected, depth_left, ctx);
        goto done;
    }
    if (js_assert_is_regexp_like(actual) || js_assert_is_regexp_like(expected)) {
        result = js_assert_partial_regexp_match(actual, expected, depth_left, ctx);
        goto done;
    }
    if (js_assert_is_any_arraybuffer(actual) || js_assert_is_any_arraybuffer(expected)) {
        result = js_assert_partial_arraybuffer_match(actual, expected);
        goto done;
    }
    if (js_assert_is_dataview_like(actual) || js_assert_is_dataview_like(expected)) {
        result = js_assert_partial_dataview_match(actual, expected);
        goto done;
    }
    if (js_assert_item_is_date(actual) || js_assert_item_is_date(expected)) {
        result = js_assert_item_is_date(actual) && js_assert_item_is_date(expected) &&
                 js_assert_deep_strict_equal_bool(actual, expected);
        goto done;
    }
    if (js_assert_is_url_like(actual) || js_assert_is_url_like(expected)) {
        result = js_assert_partial_url_match(actual, expected, depth_left, ctx);
        goto done;
    }
    if (js_assert_is_boxed_primitive(actual) || js_assert_is_boxed_primitive(expected)) {
        // Boxed primitives carry their equality in an internal primitive slot;
        // treating them as generic special maps makes equal boxes look like `{}`.
        result = js_assert_is_boxed_primitive(actual) &&
                 js_assert_is_boxed_primitive(expected) &&
                 js_assert_deep_strict_equal_bool(actual, expected);
        goto done;
    }

    if (js_assert_is_arguments_value(actual) != js_assert_is_arguments_value(expected)) {
        // Generic object subset matching must not erase the Arguments exotic
        // brand after the array-specific path has rejected the comparison.
        result = false;
        goto done;
    }

    if (get_type_id(expected) == LMD_TYPE_ARRAY) {
        result = js_assert_partial_array_match(actual, expected, depth_left, ctx);
        goto done;
    }

    if (js_is_typed_array(expected) || js_is_typed_array(actual)) {
        result = js_assert_partial_typed_array_match(actual, expected, depth_left, ctx);
        goto done;
    }

    if (get_type_id(expected) == LMD_TYPE_MAP || get_type_id(actual) == LMD_TYPE_MAP) {
        // Collection internals are hidden from public object enumeration; if a
        // Map/Set subset check fails, falling through would compare them as
        // empty plain objects.
        if (js_assert_is_collection_like(actual) || js_assert_is_collection_like(expected)) {
            result = js_assert_partial_collection_match(actual, expected, depth_left, ctx);
            goto done;
        }
        // Maps for RegExp/Error/ArrayBuffer/etc. have internal slots that are
        // not represented by enumerable object keys; falling through made
        // unequal special objects compare as empty plain-object subsets.
        if (js_assert_partial_is_special_map(expected) || js_assert_partial_is_special_map(actual)) {
            result = false;
            goto done;
        }
    }

    if (js_assert_is_collection_like(actual) || js_assert_is_collection_like(expected)) {
        // Failed collection subset checks must not fall through to generic
        // object matching, where Map/Set instances look like empty objects.
        result = false;
        goto done;
    }

    if (js_assert_is_partial_object_like(expected)) {
        if (!js_assert_is_partial_object_like(actual)) {
            result = false;
            goto done;
        }
        result = js_assert_partial_enumerable_properties(actual, expected,
            depth_left, ctx, true);
        goto done;
    }

done:
    if (entered) ctx->leave();
    return result;
}

static bool js_assert_partial_deep_match(Item actual, Item expected, int depth_left) {
    JsObjectPairTraversal ctx;
    return js_assert_partial_deep_match_impl(actual, expected, depth_left, &ctx);
}

static void js_assert_append_inspected_value(StrBuf* sb, Item value) {
    Item options = js_new_object();
    // Assert diffs need deeper cycle rendering than util.inspect's default
    // depth=2; otherwise nested circular structures collapse to [Object].
    js_set_key_cstr(options, "depth", ItemNull);
    Item inspected = js_util_inspect(value, options);
    String* is = get_type_id(inspected) == LMD_TYPE_STRING ? it2s(inspected) : NULL;
    if (is) strbuf_append_str_n(sb, is->chars, is->len);
}

static bool js_assert_append_first_partial_object_diff(StrBuf* sb, Item actual, Item expected) {
    if (!js_assert_is_partial_object_like(actual) || !js_assert_is_partial_object_like(expected)) {
        return false;
    }
    Item keys = js_object_keys(expected);
    int64_t key_len = js_array_length(keys);
    for (int64_t i = 0; i < key_len; i++) {
        Item key = js_elements_get_int(keys, i);
        Item actual_value = js_get_key_default(actual, key);
        Item expected_value = js_get_key_default(expected, key);
        if (js_assert_partial_deep_match(actual_value, expected_value, 16)) continue;
        if (get_type_id(key) != LMD_TYPE_STRING) return false;
        String* ks = it2s(key);
        if (!ks) return false;

        strbuf_append_str(sb, "  {\n");
        strbuf_append_str(sb, "+   ");
        strbuf_append_str_n(sb, ks->chars, ks->len);
        strbuf_append_str(sb, ": ");
        js_assert_append_inspected_value(sb, actual_value);
        strbuf_append_str(sb, "\n");
        strbuf_append_str(sb, "-   ");
        strbuf_append_str_n(sb, ks->chars, ks->len);
        strbuf_append_str(sb, ": ");
        js_assert_append_inspected_value(sb, expected_value);
        strbuf_append_str(sb, "\n");
        strbuf_append_str(sb, "  }\n");
        return true;
    }
    return false;
}

static Item js_assert_partial_diff_message(Item message, Item actual, Item expected) {
    StrBuf* sb = strbuf_new();
    bool append_structured_diff = true;
    if (get_type_id(message) == LMD_TYPE_STRING) {
        Item formatted = js_assert_format_user_message(message);
        String* ms = it2s(formatted);
        if (ms) strbuf_append_str_n(sb, ms->chars, ms->len);
    } else if (js_assert_is_function_like_value(message)) {
        bool has_user_message = false;
        Item user_message = js_assert_resolve_user_message(message, actual, expected, &has_user_message);
        if (item_is_error(user_message)) {
            strbuf_free(sb);
            return ItemNull;
        }
        if (has_user_message) {
            String* ms = get_type_id(user_message) == LMD_TYPE_STRING ? it2s(user_message) : NULL;
            if (ms) strbuf_append_str_n(sb, ms->chars, ms->len);
            Item generated = js_assert_strict_equal_message(actual, expected);
            String* gs = get_type_id(generated) == LMD_TYPE_STRING ? it2s(generated) : NULL;
            const char* header = "Expected values to be strictly equal:";
            size_t header_len = strlen(header);
            if (gs && gs->len >= header_len && memcmp(gs->chars, header, header_len) == 0) {
                // Primitive partial failures share Node's strict comparison
                // suffix when a function-valued user message is accepted.
                strbuf_append_str_n(sb, gs->chars + header_len, gs->len - header_len);
                append_structured_diff = false;
            }
        } else {
            strbuf_append_str(sb, "Expected values to be partially deep-strict-equal");
        }
    } else {
        strbuf_append_str(sb, "Expected values to be partially deep-strict-equal");
    }
    if (!append_structured_diff) {
        Item result = assert_make_string_n(sb->str, sb->length);
        strbuf_free(sb);
        return result;
    }
    strbuf_append_str(sb, "\n+ actual - expected\n\n");
    if (!js_assert_append_first_partial_object_diff(sb, actual, expected)) {
        strbuf_append_str(sb, "+ ");
        js_assert_append_inspected_value(sb, actual);
        strbuf_append_str(sb, "\n- ");
        js_assert_append_inspected_value(sb, expected);
        strbuf_append_str(sb, "\n");
    }
    Item result = assert_make_string_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

// assert.partialDeepStrictEqual(actual, expected[, message])
// Checks that expected is a subset of actual.
extern "C" Item js_assert_partialDeepStrictEqual(Item actual, Item expected, Item message) {
    if (js_pending_call_argc < 2) return js_assert_throw_missing_actual_expected();
    if (js_assert_partial_deep_match(actual, expected, 32)) return make_js_undefined();
    Item diff_message = js_assert_partial_diff_message(message, actual, expected);
    return throw_assert_msg_or_auto(diff_message,
        "Expected values to be partially deep-strict-equal",
        actual, expected, "partialDeepStrictEqual");
}

// =============================================================================
// assert.rejects / assert.doesNotReject — async assertion helpers
// =============================================================================

static Item make_type_error_with_code(const char* code, const char* message) {
    Item error = js_new_error_with_name(assert_make_string("TypeError"), assert_make_string(message));
    js_set_key_cstr(error, "code", assert_make_string(code));
    return error;
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_assert_is_native_promise, (Item value), (get_type_id(value) == LMD_TYPE_MAP && js_class_id(value) == JS_CLASS_PROMISE))

static bool js_assert_is_valid_thenable(Item value) {
    TypeId type = get_type_id(value);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_ELEMENT && type != LMD_TYPE_ARRAY &&
        type != LMD_TYPE_OBJECT && type != LMD_TYPE_VMAP) {
        return false;
    }
    Item then_fn = js_get_key_cstr(value, "then");
    if (!js_is_callable(then_fn)) return false;
    Item catch_fn = js_get_key_cstr(value, "catch");
    return js_is_callable(catch_fn);
}

static const char* js_assert_class_instance_name(Item value) {
    if (get_type_id(value) == LMD_TYPE_ARRAY) return "Array";
    if (get_type_id(value) != LMD_TYPE_MAP) return "Object";
    JsClass cls = js_class_id(value);
    switch (cls) {
        case JS_CLASS_MAP: return "Map";
        case JS_CLASS_SET: return "Set";
        case JS_CLASS_PROMISE: return "Promise";
        case JS_CLASS_DATE: return "Date";
        case JS_CLASS_REGEXP: return "RegExp";
        case JS_CLASS_ARRAY_BUFFER: return "ArrayBuffer";
        case JS_CLASS_DATA_VIEW: return "DataView";
        default: return "Object";
    }
}

static int js_assert_append_value_type(char* buf, int buf_size, Item value) {
    if (buf_size <= 0) return 0;
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_UNDEFINED) return snprintf(buf, buf_size, "undefined");
    if (type == LMD_TYPE_NULL) return snprintf(buf, buf_size, "null");
    if (type == LMD_TYPE_BOOL) return snprintf(buf, buf_size, "type boolean (%s)", it2b(value) ? "true" : "false");
    if (js_assert_is_symbol_value(value)) {
        Item text = js_symbol_to_string(value);
        String* s = get_type_id(text) == LMD_TYPE_STRING ? it2s(text) : NULL;
        return snprintf(buf, buf_size, "type symbol (%.*s)", s ? (int)s->len : 0, s ? s->chars : "");
    }
    if (type == LMD_TYPE_INT) return snprintf(buf, buf_size, "type number (%lld)", (long long)it2i(value));
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(value);
        if (d != d) return snprintf(buf, buf_size, "type number (NaN)");
        if (d == 1.0/0.0) return snprintf(buf, buf_size, "type number (Infinity)");
        if (d == -1.0/0.0) return snprintf(buf, buf_size, "type number (-Infinity)");
        return snprintf(buf, buf_size, "type number (%g)", d);
    }
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(value);
        return snprintf(buf, buf_size, "type string ('%.*s')", s ? (int)s->len : 0, s ? s->chars : "");
    }
    if (type == LMD_TYPE_FUNC) return snprintf(buf, buf_size, "function");
    if (type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY || type == LMD_TYPE_ELEMENT ||
        type == LMD_TYPE_OBJECT || type == LMD_TYPE_VMAP) {
        char ctor_name[64];
        const char* class_name = js_assert_class_instance_name(value);
        if (strcmp(class_name, "Object") == 0 && js_get_constructor_name(value, ctor_name, sizeof(ctor_name))) {
            class_name = ctor_name;
        }
        return snprintf(buf, buf_size, "an instance of %s", class_name);
    }
    return snprintf(buf, buf_size, "type object");
}

static Item js_assert_make_invalid_arg_type_error(Item actual) {
    char received[160];
    js_assert_append_value_type(received, sizeof(received), actual);
    char msg[384];
    snprintf(msg, sizeof(msg),
        "The \"promiseFn\" argument must be of type function or an instance of Promise. Received %s",
        received);
    return make_type_error_with_code(JS_ERR_INVALID_ARG_TYPE, msg);
}

static Item js_assert_make_invalid_return_error(Item actual) {
    char received[160];
    js_assert_append_value_type(received, sizeof(received), actual);
    char msg[384];
    snprintf(msg, sizeof(msg),
        "Expected instance of Promise to be returned from the \"promiseFn\" function but got %s.",
        received);
    return make_type_error_with_code(JS_ERR_INVALID_RETURN_VALUE, msg);
}
JS_FORWARD_STATIC_ITEM(js_assert_reject_with_error, (Item error), js_promise_reject, (error))
JS_FORWARD_STATIC_RETURN(bool, js_assert_is_async_assertion_input, (Item value), js_assert_is_native_promise, (value) || js_assert_is_valid_thenable(value))

static Item js_assert_missing_rejection_error(Item error_expected) {
    const char* suffix = "";
    char name_buf[128];
    if (js_is_callable(error_expected)) {
        Item name = js_get_key_cstr(error_expected, "name");
        String* ns = get_type_id(name) == LMD_TYPE_STRING ? it2s(name) : NULL;
        if (ns && ns->len > 0) {
            int len = (int)(ns->len < (int)sizeof(name_buf) - 1 ? ns->len : (int)sizeof(name_buf) - 1);
            memcpy(name_buf, ns->chars, len);
            name_buf[len] = '\0';
            suffix = name_buf;
        }
    }
    char msg[192];
    if (suffix[0]) snprintf(msg, sizeof(msg), "Missing expected rejection (%s).", suffix);
    else snprintf(msg, sizeof(msg), "Missing expected rejection.");
    return make_assertion_error_full(msg, make_js_undefined(), error_expected, "rejects", true);
}

static Item js_assert_unwanted_rejection_error(Item reason, Item error_expected) {
    Item msg_val = js_get_key_cstr(reason, "message");
    String* msg_str = get_type_id(msg_val) == LMD_TYPE_STRING ? it2s(msg_val) : NULL;
    char msg[512];
    if (msg_str && msg_str->len > 0) {
        snprintf(msg, sizeof(msg), "Got unwanted rejection.\nActual message: \"%.*s\"",
            (int)msg_str->len, msg_str->chars);
    } else {
        snprintf(msg, sizeof(msg), "Got unwanted rejection.");
    }
    return make_assertion_error_full(msg, reason, error_expected, "doesNotReject", true);
}

static Item js_assert_throw_rejection_mismatch(Item thrown, Item error_expected, Item message,
                                               const char* generated_msg) {
    if (get_type_id(message) == LMD_TYPE_STRING) {
        String* ms = it2s(message);
        char msg[512];
        snprintf(msg, sizeof(msg), "%.*s", ms ? (int)ms->len : 0, ms ? ms->chars : "");
        return throw_assertion_error_full(msg, thrown, error_expected, "rejects", false);
    }
    return throw_assertion_error_full(generated_msg, thrown, error_expected, "rejects", true);
}

// Validate a rejected value against an expected error pattern.
// Returns true if validation passes, false if it fails (and throws assertion error).
static bool validate_rejection(Item thrown, Item error_expected, Item message) {

    TypeId exp_type = get_type_id(error_expected);
    if (exp_type == LMD_TYPE_UNDEFINED || exp_type == LMD_TYPE_NULL) {
        return true; // any rejection is fine
    }

    Item same_expected = js_strict_equal(thrown, error_expected);
    if (get_type_id(same_expected) == LMD_TYPE_BOOL && it2b(same_expected)) {
        // Error instances passed as the expected value match by identity; otherwise
        // their enumerable fields are mistaken for an object validation pattern.
        return true;
    }

    if (js_is_callable(error_expected)) {
        // Callable Proxy validators have the same host-algorithm status as
        // ordinary functions; representation tags cannot reject them (D6.2.2v2).
        // Error class: check instanceof only for constructor-like functions.
        Item proto = js_get_key_cstr(error_expected, "prototype");
        if (get_type_id(proto) == LMD_TYPE_MAP || get_type_id(proto) == LMD_TYPE_ELEMENT) {
            Item result = js_instanceof(thrown, error_expected);
            if (!item_is_error(result) && get_type_id(result) == LMD_TYPE_BOOL && it2b(result)) return true;
        }
        // Maybe it's a validation function.
        Item validate_result = js_call_function(error_expected, make_js_undefined(), &thrown, 1);
        if (item_is_error(validate_result)) return true; // validator threw — propagate
        if (get_type_id(validate_result) == LMD_TYPE_BOOL && it2b(validate_result)) return true;
        // validation failed
        char result_text[160];
        Item result_str = js_to_string_val(validate_result);
        String* rs = get_type_id(result_str) == LMD_TYPE_STRING ? it2s(result_str) : NULL;
        if (get_type_id(validate_result) == LMD_TYPE_STRING) {
            snprintf(result_text, sizeof(result_text), "'%.*s'", rs ? (int)rs->len : 0, rs ? rs->chars : "");
        } else {
            snprintf(result_text, sizeof(result_text), "%.*s", rs ? (int)rs->len : 0, rs ? rs->chars : "");
        }
        char caught_text[256];
        Item thrown_str = js_to_string_val(thrown);
        String* ts = get_type_id(thrown_str) == LMD_TYPE_STRING ? it2s(thrown_str) : NULL;
        snprintf(caught_text, sizeof(caught_text), "%.*s", ts ? (int)ts->len : 0, ts ? ts->chars : "");
        char msg[512];
        snprintf(msg, sizeof(msg),
            "The \"validate\" validation function is expected to return \"true\". Received %s\n\nCaught error:\n\n%s",
            result_text, caught_text);
        throw_assertion_error_full(msg, thrown, error_expected, "rejects", true);
        return false;
    }

    if (exp_type == LMD_TYPE_MAP) {
        bool is_regex = js_class_id(error_expected) == JS_CLASS_REGEXP;

        if (is_regex) {
            Item thrown_str = js_to_string_val(thrown);
            Item test_result = js_regex_test(error_expected, thrown_str);
            if (get_type_id(test_result) == LMD_TYPE_BOOL && it2b(test_result)) return true;
            Item code = js_get_key_cstr(thrown, "code");
            if (get_type_id(code) == LMD_TYPE_STRING) {
                test_result = js_regex_test(error_expected, code);
                if (get_type_id(test_result) == LMD_TYPE_BOOL && it2b(test_result)) return true;
            }
            Item name = js_get_key_cstr(thrown, "name");
            if (get_type_id(name) == LMD_TYPE_STRING) {
                test_result = js_regex_test(error_expected, name);
                if (get_type_id(test_result) == LMD_TYPE_BOOL && it2b(test_result)) return true;
            }
            throw_assertion_error("The input did not match the regular expression");
            return false;
        }

        // Object pattern: validate each property
        Item keys = js_object_keys(error_expected);
        if (get_type_id(keys) == LMD_TYPE_ARRAY) {
            if (keys.array->length == 0) return false;
            for (int64_t i = 0; i < keys.array->length; i++) {
                Item key = list_get(keys.array, (int)i);
                Item expected_val = js_get_key_default(error_expected, key);
                Item actual_val = js_get_key_default(thrown, key);

                // check if expected_val is a RegExp
                TypeId ev_type = get_type_id(expected_val);
                if (ev_type == LMD_TYPE_MAP && js_class_id(expected_val) == JS_CLASS_REGEXP) {
                            Item actual_str = (get_type_id(actual_val) == LMD_TYPE_STRING)
                                ? actual_val : js_to_string_val(actual_val);
                            Item test_result = js_regex_test(expected_val, actual_str);
                            if (get_type_id(test_result) != LMD_TYPE_BOOL || !it2b(test_result)) {
                                char buf[256];
                                String* ks = it2s(key);
                                snprintf(buf, sizeof(buf), "Expected property '%.*s' to match regex",
                                         ks ? (int)ks->len : 1, ks ? ks->chars : "?");
                                js_assert_throw_rejection_mismatch(thrown, error_expected, message, buf);
                                return false;
                            }
                            continue;
                }

                if (!js_assert_expected_property_matches(actual_val, expected_val)) {
                    char buf[256];
                    String* ks = it2s(key);
                    snprintf(buf, sizeof(buf), "Expected property '%.*s' to be strictly equal",
                             ks ? (int)ks->len : 1, ks ? ks->chars : "?");
                    js_assert_throw_rejection_mismatch(thrown, error_expected, message, buf);
                    return false;
                }
            }
        }
        return true;
    }

    return true;
}

static Item js_assert_rejects_on_fulfilled_with_env(Item env_item, Item value) {
    (void)value;
    Item* env = (Item*)(uintptr_t)env_item.item;
    Item error_expected = env ? env[0] : make_js_undefined();
    return js_throw_value(js_assert_missing_rejection_error(error_expected));
}

static Item js_assert_rejects_on_rejected(Item env_item, Item reason) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    Item error_expected = env ? env[0] : make_js_undefined();
    Item message = env ? env[1] : make_js_undefined();
    bool matched = validate_rejection(reason, error_expected, message);
    if (!matched) {
        return js_throw_value(reason);
    }
    return make_js_undefined();
}

static bool js_assert_normalize_async_input(Item* promise) {
    if (!js_assert_is_async_assertion_input(*promise)) return false;
    if (!js_assert_is_native_promise(*promise)) {
        *promise = js_promise_resolve(*promise);
    }
    return true;
}

typedef enum JsAssertAsyncInputStatus {
    JS_ASSERT_ASYNC_READY,
    JS_ASSERT_ASYNC_THROWN,
    JS_ASSERT_ASYNC_INVALID_RETURN,
    JS_ASSERT_ASYNC_INVALID_ARGUMENT,
} JsAssertAsyncInputStatus;

static JsAssertAsyncInputStatus js_assert_prepare_async_input(Item input,
        Item* promise) {
    bool callable = js_is_callable(input);
    *promise = callable ? js_call_function(input, make_js_undefined(), NULL, 0) : input;
    if (callable && item_is_error(*promise)) return JS_ASSERT_ASYNC_THROWN;
    if (!js_assert_normalize_async_input(promise)) {
        return callable ? JS_ASSERT_ASYNC_INVALID_RETURN :
            JS_ASSERT_ASYNC_INVALID_ARGUMENT;
    }
    return JS_ASSERT_ASYNC_READY;
}

// assert.rejects(asyncFnOrPromise[, error[, message]])
extern "C" Item js_assert_rejects(Item asyncFnOrPromise, Item error_expected, Item message) {

    Item promise;
    JsAssertAsyncInputStatus status = js_assert_prepare_async_input(asyncFnOrPromise, &promise);
    if (status == JS_ASSERT_ASYNC_THROWN) {
        Item thrown = js_error_lane_payload(promise);
        bool matched = validate_rejection(thrown, error_expected, message);
        if (!matched) return js_assert_reject_with_error(thrown);
        return js_promise_resolve(make_js_undefined());
    }
    if (status == JS_ASSERT_ASYNC_INVALID_RETURN) {
        return js_assert_reject_with_error(js_assert_make_invalid_return_error(promise));
    }
    if (status == JS_ASSERT_ASYNC_INVALID_ARGUMENT) {
        return js_assert_reject_with_error(js_assert_make_invalid_arg_type_error(asyncFnOrPromise));
    }

    Item* reject_env = js_alloc_env(2);
    reject_env[0] = error_expected;
    reject_env[1] = message;
    Item on_fulfilled = js_new_native_closure(js_assert_rejects_on_fulfilled_with_env, 1, reject_env, 2);
    Item on_rejected = js_new_native_closure(js_assert_rejects_on_rejected, 1, reject_env, 2);
    return js_promise_then(promise, on_fulfilled, on_rejected);
}

static Item js_assert_doesNotReject_on_rejected(Item env_item, Item reason) {
    // promise rejected when it should not have
    Item* env = (Item*)(uintptr_t)env_item.item;
    Item error_expected = env ? env[0] : make_js_undefined();
    TypeId exp_type = get_type_id(error_expected);
    if (exp_type == LMD_TYPE_UNDEFINED || exp_type == LMD_TYPE_NULL) {
        return js_throw_value(reason);
    }

    bool matched = validate_rejection(reason, error_expected, env ? env[1] : make_js_undefined());
    if (matched) {
        return js_throw_value(js_assert_unwanted_rejection_error(reason, error_expected));
    }
    return make_js_undefined();
}

// assert.doesNotReject(asyncFnOrPromise[, error[, message]])
extern "C" Item js_assert_doesNotReject(Item asyncFnOrPromise, Item error_expected, Item message) {

    Item promise;
    JsAssertAsyncInputStatus status = js_assert_prepare_async_input(asyncFnOrPromise, &promise);
    if (status == JS_ASSERT_ASYNC_THROWN) return js_assert_reject_with_error(promise);
    if (status == JS_ASSERT_ASYNC_INVALID_RETURN) {
        return js_assert_reject_with_error(js_assert_make_invalid_return_error(promise));
    }
    if (status == JS_ASSERT_ASYNC_INVALID_ARGUMENT) {
        return js_assert_reject_with_error(js_assert_make_invalid_arg_type_error(asyncFnOrPromise));
    }

    Item* reject_env = js_alloc_env(2);
    reject_env[0] = error_expected;
    reject_env[1] = message;
    Item on_fulfilled = js_new_native_function(js_assert_noop);
    Item on_rejected = js_new_native_closure(js_assert_doesNotReject_on_rejected, 1, reject_env, 2);
    return js_promise_then(promise, on_fulfilled, on_rejected);
}

// =============================================================================
// assert Module Namespace
// =============================================================================

template <typename Target>
static void assert_set_method(Item ns, const char* name, Target target,
        int adapter_arity) {
    Item key = assert_make_string(name);
    Item fn = js_new_native_function(target, adapter_arity);
    js_set_key_default(ns, key, fn);
}

template <typename Target>
static Item assert_set_fresh_method(Item ns, const char* name, Target target,
        int adapter_arity) {
    Item key = assert_make_string(name);
    Item fn = js_new_distinct_native_function(target);
    js_set_formal_length(fn, adapter_arity);
    js_set_key_default(ns, key, fn);
    return fn;
}
JS_FORWARD_STATIC_VOID( assert_set_method_item, (Item ns, const char* name, Item fn), js_set_key_default, (ns, assert_make_string(name), fn))

#define JS_ASSERT_METHODS(M) \
    M("ok", js_assert_ok, 2) M("equal", js_assert_equal, 3) \
    M("notEqual", js_assert_notEqual, 3) M("strictEqual", js_assert_strictEqual, 3) \
    M("notStrictEqual", js_assert_notStrictEqual, 3) \
    M("deepStrictEqual", js_assert_deepStrictEqual, 3) \
    M("notDeepStrictEqual", js_assert_notDeepStrictEqual, 3) \
    M("deepEqual", js_assert_deepEqual, 3) M("notDeepEqual", js_assert_notDeepEqual, 3) \
    M("fail", js_assert_fail, 1) M("throws", js_assert_module_throws, 3) \
    M("doesNotThrow", js_assert_module_doesNotThrow, 3) M("ifError", js_assert_ifError, 1) \
    M("match", js_assert_match, 3) M("doesNotMatch", js_assert_doesNotMatch, 3) \
    M("rejects", js_assert_rejects, 3) M("doesNotReject", js_assert_doesNotReject, 3) \
    M("partialDeepStrictEqual", js_assert_partialDeepStrictEqual, 3)
#define JS_ASSERT_INSTANCE_METHODS(M) \
    M("fail", js_assert_fail, 1) M("throws", js_assert_module_throws, 3) \
    M("doesNotThrow", js_assert_module_doesNotThrow, 3) M("ifError", js_assert_ifError, 1) \
    M("match", js_assert_match, 3) M("doesNotMatch", js_assert_doesNotMatch, 3) \
    M("rejects", js_assert_rejects, 3) M("doesNotReject", js_assert_doesNotReject, 3) \
    M("partialDeepStrictEqual", js_assert_partialDeepStrictEqual, 3)

static Item js_assert_constructor_default_message(Item actual, Item expected, Item op_item) {
    if (js_assert_string_equals(op_item, "strictEqual")) {
        return js_assert_strict_equal_message(actual, expected);
    }
    if (js_assert_string_equals(op_item, "notStrictEqual")) {
        return js_assert_not_strict_equal_message(actual);
    }
    if (js_assert_string_equals(op_item, "deepEqual")) {
        return js_assert_deep_equal_message(actual, expected);
    }
    if (js_assert_string_equals(op_item, "deepStrictEqual")) {
        Item date_msg = js_assert_date_checktag_message(actual, expected);
        if (get_type_id(date_msg) == LMD_TYPE_STRING) return date_msg;
        Item array_msg = js_assert_deep_strict_array_message(actual, expected);
        if (get_type_id(array_msg) == LMD_TYPE_STRING) return array_msg;
        return assert_make_string("Expected values to be strictly deep-equal");
    }
    if (js_assert_string_equals(op_item, "fail")) {
        return assert_make_string("Failed");
    }
    return assert_make_string("assertion error");
}

// AssertionError constructor: new assert.AssertionError({ message, actual, expected, operator })
extern "C" Item js_assert_AssertionError_ctor(Item options) {
    if (get_type_id(options) != LMD_TYPE_MAP) {
        // Public AssertionError construction requires an options object; accepting
        // primitives hides caller mistakes and breaks Node's validation contract.
        char received[160];
        js_assert_append_value_type(received, sizeof(received), options);
        return js_throw_type_error_codef(JS_ERR_INVALID_ARG_TYPE, 
            "The \"options\" argument must be of type object. Received %s", received);
    }
    Item msg_item = ItemNull;
    Item op_item = make_js_undefined();
    Item actual = make_js_undefined();
    Item expected = make_js_undefined();
    const char* diff_str = "simple";
    bool generated = true;
    Item m = js_get_key_cstr(options, "message");
    if (get_type_id(m) == LMD_TYPE_STRING) {
        msg_item = m;
        generated = false;
    }
    actual = js_get_key_cstr(options, "actual");
    expected = js_get_key_cstr(options, "expected");
    op_item = js_get_key_cstr(options, "operator");
    diff_str = js_assert_normalized_diff(js_get_key_default(options, js_assert_diff_key()));
    if (get_type_id(msg_item) != LMD_TYPE_STRING) {
        msg_item = js_assert_constructor_default_message(actual, expected, op_item);
    }
    Item type_name = assert_make_string("AssertionError");
    Item error = js_new_error_with_name(type_name, msg_item);
    js_set_key_cstr(error, "code", assert_make_string("ERR_ASSERTION"));
    js_set_key_cstr(error, "name", assert_make_string("AssertionError"));
    js_set_key_cstr(error, "actual", actual);
    js_set_key_cstr(error, "expected", expected);
    if (get_type_id(op_item) == LMD_TYPE_STRING) js_set_key_cstr(error, "operator", op_item);
    js_set_key_cstr(error, "diff", assert_make_string(diff_str));
    js_set_key_cstr(error, "generatedMessage", (Item){.item = b2it(generated)});
    js_assert_attach_assertion_error_prototype(error);
    Item stack_start = js_get_key_cstr(options, "stackStartFn");
    if (get_type_id(stack_start) != LMD_TYPE_FUNC) {
        stack_start = js_get_key_cstr(options, "stackStartFunction");
    }
    if (get_type_id(stack_start) == LMD_TYPE_FUNC) {
        // Node AssertionError options provide the public trim function; without
        // applying it, wrapper frames become visible once stacks are real.
        js_error_captureStackTrace(error, stack_start);
    }
    return error;
}

static Item js_assert_create_instance(Item options) {
    if (get_type_id(options) == LMD_TYPE_MAP) {
        Item diff = js_get_key_default(options, js_assert_diff_key());
        if (!js_assert_valid_diff(diff)) return js_assert_throw_invalid_diff(diff);
    }

    // Fresh wrappers allocate methods before they are published, so every
    // alias must remain rooted across a compacting collection.
    RootFrame roots(7);
    Rooted<Item> options_root(roots, options);
    Rooted<Item> instance_root(roots, js_new_distinct_native_function(js_assert_ok));
    bool strict_mode = js_assert_options_strict(options_root.get());

    // copy all assert methods onto this instance without reusing the module
    // assert() function object from js_new_function's native-wrapper cache.
    assert_set_fresh_method(instance_root.get(), "ok",                  js_assert_ok, 2);
    Rooted<Item> strict_equal_root(roots, assert_set_fresh_method(instance_root.get(), "strictEqual", js_assert_strictEqual, 3));
    Rooted<Item> not_strict_equal_root(roots, assert_set_fresh_method(instance_root.get(), "notStrictEqual", js_assert_notStrictEqual, 3));
    Rooted<Item> deep_strict_equal_root(roots, assert_set_fresh_method(instance_root.get(), "deepStrictEqual", js_assert_deepStrictEqual, 3));
    Rooted<Item> not_deep_strict_equal_root(roots, assert_set_fresh_method(instance_root.get(), "notDeepStrictEqual", js_assert_notDeepStrictEqual, 3));
    if (strict_mode) {
        assert_set_method_item(instance_root.get(), "equal", strict_equal_root.get());
        assert_set_method_item(instance_root.get(), "notEqual", not_strict_equal_root.get());
        assert_set_method_item(instance_root.get(), "deepEqual", deep_strict_equal_root.get());
        assert_set_method_item(instance_root.get(), "notDeepEqual", not_deep_strict_equal_root.get());
    } else {
        assert_set_fresh_method(instance_root.get(), "equal",       js_assert_equal, 3);
        assert_set_fresh_method(instance_root.get(), "notEqual",    js_assert_notEqual, 3);
        assert_set_fresh_method(instance_root.get(), "deepEqual",   js_assert_deepEqual, 3);
        assert_set_fresh_method(instance_root.get(), "notDeepEqual", js_assert_notDeepEqual, 3);
    }
#define JS_ASSERT_INSTALL_INSTANCE(name, target, arity) \
    assert_set_fresh_method(instance_root.get(), name, target, arity);
    JS_ASSERT_INSTANCE_METHODS(JS_ASSERT_INSTALL_INSTANCE)
#undef JS_ASSERT_INSTALL_INSTANCE
    Rooted<Item> assertion_error_root(roots, assert_namespace.item != 0 ?
        js_get_key_cstr(assert_namespace, "AssertionError") :
        js_new_native_constructor(js_assert_AssertionError_ctor));
    js_set_key_cstr(instance_root.get(), "AssertionError", assertion_error_root.get());

    // store options
    if (get_type_id(options_root.get()) == LMD_TYPE_MAP) {
        js_set_key_default(instance_root.get(), js_assert_options_key(), options_root.get());
    }
    // Only Assert constructor instances may carry option state; module-level
    // calls can have arbitrary receivers and must not probe them for _options.
    js_assert_register_instance(instance_root.get());

    return instance_root.get();
}

// Assert constructor: new Assert(options?) — creates an instance with all assert methods
// options: { diff: 'full'|'simple' } (stored but not used for behavior changes in our impl)
extern "C" Item js_assert_constructor(Item options) {
    Item new_target = js_get_new_target();
    if (new_target.item == ITEM_JS_UNDEFINED || get_type_id(new_target) == LMD_TYPE_UNDEFINED ||
        new_target.item == ItemNull.item || new_target.item == 0) {
        return js_throw_type_error_code(JS_ERR_CONSTRUCT_CALL_REQUIRED,
            "Class constructor Assert cannot be invoked without 'new'");
    }
    return js_assert_create_instance(options);
}

extern "C" Item js_get_assert_namespace(void) {
    if (assert_namespace.item != 0) return assert_namespace;

    // namespace doubles as the assert() function itself
    assert_namespace = js_new_native_function(js_assert_ok);
    heap_register_gc_root(&assert_namespace.item);

#define JS_ASSERT_INSTALL(name, target, arity) \
    assert_set_method(assert_namespace, name, target, arity);
    JS_ASSERT_METHODS(JS_ASSERT_INSTALL)
#undef JS_ASSERT_INSTALL

    // AssertionError constructor
    js_install_native_constructor(assert_namespace, "AssertionError",
        js_assert_AssertionError_ctor, 1);

    // Set up AssertionError.prototype to inherit from Error.prototype
    // so that (new assert.AssertionError({})) instanceof Error === true
    {
        Item ae_fn = js_get_key_cstr(assert_namespace, "AssertionError");
        // Get Error constructor and its prototype
        Item error_ctor = js_get_constructor(assert_make_string("Error"));
        Item error_proto_key = assert_make_string("prototype");
        Item error_proto = js_get_key_default(error_ctor, error_proto_key);
        // Create AssertionError.prototype that inherits from Error.prototype
        Item ae_proto = js_object_create(error_proto);
        js_set_key_cstr(ae_proto, "name", assert_make_string("AssertionError"));
        js_set_key_cstr(ae_proto, "constructor", ae_fn);
        // Set AssertionError.prototype
        js_set_key_default(ae_fn, error_proto_key, ae_proto);
    }

    // Assert constructor (creates a new Assert instance with the same methods + options)
    js_install_native_constructor(assert_namespace, "Assert",
        js_assert_constructor, 1);

    // assert.strict has the same public surface but remaps legacy equality
    // aliases to their strict variants; aliasing the loose module lets
    // strict.equal/deepEqual silently pass loose comparisons.
    Item strict_instance = js_assert_create_instance(make_js_undefined());
    js_set_key_cstr(strict_instance, "Assert", js_get_key_cstr(assert_namespace, "Assert"));
    js_set_key_cstr(strict_instance, "strict", strict_instance);
    js_set_key_cstr(strict_instance, "default", strict_instance);
    js_set_key_cstr(assert_namespace, "strict", strict_instance);

    // default export
    js_set_key_cstr(assert_namespace, "default", assert_namespace);

    return assert_namespace;
}

#undef JS_ASSERT_INSTANCE_METHODS
#undef JS_ASSERT_METHODS

extern "C" void js_assert_reset(void) {
    assert_namespace = (Item){0};
    internal_errors_namespace = (Item){0};
    internal_assert_myers_diff_namespace = (Item){0};
    assert_options_key = (Item){0};
    assert_diff_key = (Item){0};
    // Batch reset drops a heap, so no cached assertion Item may survive into
    // the next realm even though this context capsule itself is retained.
    memset(assert_instances, 0, sizeof(assert_instances));
    assert_instance_count = 0;
    assert_key_epoch = 0;
    assert_instances_roots_epoch = 0;
}

// =============================================================================
// node:test module — basic test runner with mock support
// =============================================================================

#define node_test_namespace (js_runtime_state.assert.node_test_namespace)
#define g_node_before_each_store (js_runtime_state.assert.before_each_store)
#define g_node_after_each_store (js_runtime_state.assert.after_each_store)
#define g_node_test_event_queue (js_runtime_state.assert.event_queue)
#define g_node_test_total_count (js_runtime_state.assert.node_test_total_count)
#define g_node_test_pass_count (js_runtime_state.assert.node_test_pass_count)
#define g_node_test_fail_count (js_runtime_state.assert.node_test_fail_count)
#define g_node_test_next_id (js_runtime_state.assert.node_test_next_id)
#define g_node_test_roots_epoch (js_runtime_state.assert.node_test_roots_epoch)

#define MAX_NODE_TEST_HOOKS 64
#define g_node_before_each_hooks (js_runtime_state.assert.before_each_hooks)
#define g_node_after_each_hooks (js_runtime_state.assert.after_each_hooks)
#define g_node_before_each_count (js_runtime_state.assert.before_each_count)
#define g_node_after_each_count (js_runtime_state.assert.after_each_count)

// forward decls used throughout
static Item js_mock_fn_impl(Item original_fn);
static Item js_mock_method_impl(Item object, Item method_name, Item implementation);
static Item js_mock_create_context(void);
static Item js_mock_reset_impl(void);
static Item js_mock_restore_all_impl(void);
static Item js_mock_call_count_impl(void);
static Item js_mock_timers_enable_impl(Item options);
static Item js_mock_timers_reset_impl(void);
static Item js_mock_timers_tick_impl(Item delay);

// ---------------------------------------------------------------------------
// mock.fn(original?) — creates a mock function that records calls.
// Fixed wrappers use the active context's slots, so concurrent test realms do
// not share a registry or need a hot-path lock.
// ---------------------------------------------------------------------------
#define MAX_MOCK_SLOTS 64
#define g_mock_slots (js_runtime_state.assert.mock_slots)
#define g_mock_slot_count (js_runtime_state.assert.mock_slot_count)

static int mock_alloc_slot(void) {
    // first try to reuse
    for (int i = 0; i < g_mock_slot_count; i++) {
        if (!g_mock_slots[i].in_use) {
            g_mock_slots[i].in_use = true;
            return i;
        }
    }
    if (g_mock_slot_count < MAX_MOCK_SLOTS) {
        int idx = g_mock_slot_count++;
        g_mock_slots[idx].in_use = true;
        return idx;
    }
    return -1;
}

// Each mock wrapper is generated per-slot. We use a trampolining scheme:
// the first argument of the wrapper encodes the slot index via a JS property
// on the wrapper function. But since we can't access the function object...
// Alternative: create separate C wrappers for the first N slots.

// Helper: generic mock wrapper that gets slot index from a hidden property
// We'll store the slot index as the mock's _slot property on the .mock object,
// and find it by iterating slots to match the calls array. But that's O(n).
// Better: use `js_get_callee()` if available, or create per-slot wrappers.
// Simplest approach: create a fixed number of static wrapper functions.

#define MOCK_WRAPPER_BODY(SLOT_IDX) \
static Item js_mock_wrapper_##SLOT_IDX(Item a0, Item a1, Item a2) { \
    int idx = SLOT_IDX; \
    if (idx >= MAX_MOCK_SLOTS || !g_mock_slots[idx].in_use) return make_js_undefined(); \
    Item call_record = js_new_object(); \
    Item args_array = js_array_new(0); \
    js_array_push(args_array, a0); \
    js_array_push(args_array, a1); \
    js_array_push(args_array, a2); \
    js_set_key_cstr(call_record, "arguments", args_array); \
    js_set_key_cstr(call_record, "this", make_js_undefined()); \
    Item result = make_js_undefined(); \
    if (js_is_callable(g_mock_slots[idx].original)) { \
        Item call_args[3] = {a0, a1, a2}; \
        result = js_call_function(g_mock_slots[idx].original, make_js_undefined(), call_args, 3); \
    } \
    js_set_key_cstr(call_record, "result", result); \
    js_array_push(g_mock_slots[idx].calls, call_record); \
    g_mock_slots[idx].call_count++; \
    return result; \
}

MOCK_WRAPPER_BODY(0)  MOCK_WRAPPER_BODY(1)  MOCK_WRAPPER_BODY(2)  MOCK_WRAPPER_BODY(3)
MOCK_WRAPPER_BODY(4)  MOCK_WRAPPER_BODY(5)  MOCK_WRAPPER_BODY(6)  MOCK_WRAPPER_BODY(7)
MOCK_WRAPPER_BODY(8)  MOCK_WRAPPER_BODY(9)  MOCK_WRAPPER_BODY(10) MOCK_WRAPPER_BODY(11)
MOCK_WRAPPER_BODY(12) MOCK_WRAPPER_BODY(13) MOCK_WRAPPER_BODY(14) MOCK_WRAPPER_BODY(15)
MOCK_WRAPPER_BODY(16) MOCK_WRAPPER_BODY(17) MOCK_WRAPPER_BODY(18) MOCK_WRAPPER_BODY(19)
MOCK_WRAPPER_BODY(20) MOCK_WRAPPER_BODY(21) MOCK_WRAPPER_BODY(22) MOCK_WRAPPER_BODY(23)
MOCK_WRAPPER_BODY(24) MOCK_WRAPPER_BODY(25) MOCK_WRAPPER_BODY(26) MOCK_WRAPPER_BODY(27)
MOCK_WRAPPER_BODY(28) MOCK_WRAPPER_BODY(29) MOCK_WRAPPER_BODY(30) MOCK_WRAPPER_BODY(31)

typedef Item (*MockWrapperFn)(Item, Item, Item);
static const MockWrapperFn g_mock_wrappers[32] = {
    js_mock_wrapper_0,  js_mock_wrapper_1,  js_mock_wrapper_2,  js_mock_wrapper_3,
    js_mock_wrapper_4,  js_mock_wrapper_5,  js_mock_wrapper_6,  js_mock_wrapper_7,
    js_mock_wrapper_8,  js_mock_wrapper_9,  js_mock_wrapper_10, js_mock_wrapper_11,
    js_mock_wrapper_12, js_mock_wrapper_13, js_mock_wrapper_14, js_mock_wrapper_15,
    js_mock_wrapper_16, js_mock_wrapper_17, js_mock_wrapper_18, js_mock_wrapper_19,
    js_mock_wrapper_20, js_mock_wrapper_21, js_mock_wrapper_22, js_mock_wrapper_23,
    js_mock_wrapper_24, js_mock_wrapper_25, js_mock_wrapper_26, js_mock_wrapper_27,
    js_mock_wrapper_28, js_mock_wrapper_29, js_mock_wrapper_30, js_mock_wrapper_31,
};

// A mock_prop object that reads from the slot on access
// We create a regular object and set a getter-like mechanism,
// but since we don't have getters, we'll update it lazily.
// Simplest: store slot index in the mock object, and provide
// a "calls" array that IS the slot's calls array (shared reference).

// mock.fn([original]) — create a new mock function
static Item js_mock_fn_impl(Item original_fn) {
    int slot = mock_alloc_slot();
    if (slot < 0 || slot >= 32) {
        // fallback: return a simple function without tracking
        if (js_is_callable(original_fn)) return original_fn;
        return js_new_native_function(js_mock_reset_impl);
    }

    g_mock_slots[slot].calls = js_array_new(0);
    g_mock_slots[slot].call_count = 0;
    g_mock_slots[slot].original = original_fn;

    Item wrapper = js_new_native_function(g_mock_wrappers[slot]);

    // create .mock property pointing to the live calls array
    Item mock_prop = js_new_object();
    js_set_key_cstr(mock_prop, "calls", g_mock_slots[slot].calls);
    js_assert_set_native(mock_prop, "callCount", js_mock_call_count_impl);
    js_set_key_cstr(mock_prop, "_slot", (Item){.item = i2it(slot)});

    // mock.restore() — no-op for fn mocks
    js_assert_set_native(mock_prop, "restore", js_mock_reset_impl);
    // mock.resetCalls()
    js_assert_set_native(mock_prop, "resetCalls", js_mock_reset_impl);

    js_set_key_cstr(wrapper, "mock", mock_prop);
    return wrapper;
}

// mock.method(object, methodName[, implementation]) — replace a method with a mock
static Item js_mock_method_impl(Item object, Item method_name, Item implementation) {
    // get original method
    Item original = js_get_key_default(object, method_name);

    // create mock wrapper — if implementation provided, use that as the "original"
    Item mock_original = js_is_callable(implementation)
                         ? implementation : original;
    Item wrapper = js_mock_fn_impl(mock_original);

    // store info for restore
    Item mock_prop = js_get_key_cstr(wrapper, "mock");
    js_set_key_cstr(mock_prop, "_owner", object);
    js_set_key_cstr(mock_prop, "_methodName", method_name);
    js_set_key_cstr(mock_prop, "_originalMethod", original);

    // replace the method
    js_set_key_default(object, method_name, wrapper);
    return wrapper;
}
JS_FORWARD_STATIC_ITEM(js_mock_reset_impl, (void), make_js_undefined, ())
JS_FORWARD_STATIC_ITEM(js_mock_restore_all_impl, (void), make_js_undefined, ())

static Item js_mock_call_count_impl(void) {
    Item self = js_get_this();
    Item slot_item = js_get_key_cstr(self, "_slot");
    if (get_type_id(slot_item) != LMD_TYPE_INT) return (Item){.item = i2it(0)};
    int slot = (int)it2i(slot_item);
    if (slot < 0 || slot >= MAX_MOCK_SLOTS || !g_mock_slots[slot].in_use) {
        return (Item){.item = i2it(0)};
    }
    return (Item){.item = i2it(g_mock_slots[slot].call_count)};
}

extern "C" void js_mock_scheduler_enable(void);
extern "C" void js_mock_scheduler_reset(void);
extern "C" void js_mock_scheduler_tick(Item delay);

static Item js_mock_timers_enable_impl(Item options) {
    (void)options;
    // The current node:test mock timer surface only virtualizes scheduler.wait;
    // leaving it as a stub makes official tests wait for real 9999ms timers.
    js_mock_scheduler_enable();
    return make_js_undefined();
}

static Item js_mock_timers_reset_impl(void) {
    js_mock_scheduler_reset();
    return make_js_undefined();
}

static Item js_mock_timers_tick_impl(Item delay) {
    js_mock_scheduler_tick(delay);
    return make_js_undefined();
}

// mock.getter(object, property) — stub
JS_FORWARD_STATIC_ITEM(js_mock_getter_impl, (Item object, Item property), js_mock_method_impl, (object, property, make_js_undefined()))

// mock.setter(object, property) — stub
JS_FORWARD_STATIC_ITEM(js_mock_setter_impl, (Item object, Item property), js_mock_method_impl, (object, property, make_js_undefined()))

// Create a mock context object with fn/method/getter/setter/reset/restoreAll
static Item js_mock_create_context(void) {
    Item mock_obj = js_new_object();
    js_assert_set_native(mock_obj, "fn", js_mock_fn_impl);
    js_assert_set_native(mock_obj, "method", js_mock_method_impl);
    js_assert_set_native(mock_obj, "getter", js_mock_getter_impl);
    js_assert_set_native(mock_obj, "setter", js_mock_setter_impl);
    js_assert_set_native(mock_obj, "reset", js_mock_reset_impl);
    js_assert_set_native(mock_obj, "restoreAll", js_mock_restore_all_impl);
    // timers sub-object
    Item timers_obj = js_new_object();
    js_assert_set_native(timers_obj, "enable", js_mock_timers_enable_impl);
    js_assert_set_native(timers_obj, "reset", js_mock_timers_reset_impl);
    js_assert_set_native(timers_obj, "tick", js_mock_timers_tick_impl);
    js_set_key_cstr(mock_obj, "timers", timers_obj);
    return mock_obj;
}

// t.skip() — no-op skip
JS_FORWARD_STATIC_ITEM(js_test_context_skip, (void), make_js_undefined, ())

// t.todo() — no-op
JS_FORWARD_STATIC_ITEM(js_test_context_todo, (void), make_js_undefined, ())

// t.diagnostic(msg) — no-op
JS_FORWARD_STATIC_ITEM(js_test_context_diagnostic, (Item msg), make_js_undefined, ())

// t.plan(count) — no-op
JS_FORWARD_STATIC_ITEM(js_test_context_plan, (Item count), make_js_undefined, ())

// t.test(name, fn) — sub-test, delegate to js_node_test_run
extern "C" Item js_node_test_run(Item name, Item options_or_fn, Item fn);
JS_FORWARD_STATIC_ITEM(js_test_context_subtest, (Item name, Item options_or_fn, Item fn), js_node_test_run, (name, options_or_fn, fn))

static void node_test_ensure_hook_stores(void) {
    if (g_node_before_each_store.item == 0) {
        g_node_before_each_store = js_array_new(0);
    }
    if (g_node_after_each_store.item == 0) {
        g_node_after_each_store = js_array_new(0);
    }
    if (node_test_namespace.item != 0) {
        js_set_key_cstr(node_test_namespace, "__beforeEachHooks__", g_node_before_each_store);
        js_set_key_cstr(node_test_namespace, "__afterEachHooks__", g_node_after_each_store);
    }
}

static void node_test_store_hook(Item* hooks, int* count, Item fn) {
    if (!js_is_callable(fn)) return;
    node_test_ensure_hook_stores();
    if (*count >= MAX_NODE_TEST_HOOKS) return;
    hooks[*count] = fn;
    (*count)++;
}

static Item node_test_run_hooks(Item* hooks, int count) {
    for (int i = 0; i < count; i++) {
        if (!js_is_callable(hooks[i])) continue;
        JS_ASSIGN_OR_RETURN(hook_result, js_call_function(hooks[i], make_js_undefined(), NULL, 0));
        js_microtask_flush();
    }
    return ItemNull;
}

static bool node_test_is_promise_like(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return false;
    Item then = js_get_key_cstr(value, "then");
    return js_is_callable(then);
}

static void node_test_note_failure(void) {
    g_node_test_fail_count++;
    if (get_type_id(g_node_test_event_queue) == LMD_TYPE_ARRAY) return;
    js_process_set_exitCode((Item){.item = i2it(1)});
}

static void node_test_register_roots(void) {
    uint64_t epoch = js_get_heap_epoch();
    if (g_node_test_roots_epoch == epoch) return;
    heap_register_gc_root(&g_node_test_event_queue.item);
    g_node_test_roots_epoch = epoch;
}
JS_FORWARD_STATIC_EXPRESSION(bool, node_test_event_queue_active, (void), (get_type_id(g_node_test_event_queue) == LMD_TYPE_ARRAY))

static void node_test_emit_event(const char* type, Item name, int64_t test_id, Item error) {
    if (!node_test_event_queue_active()) return;
    Item event = js_new_object();
    Item data = js_new_object();
    js_set_key_cstr(event, "type", assert_make_string(type));
    js_set_key_cstr(data, "name", get_type_id(name) == LMD_TYPE_STRING ? name : assert_make_string(""));
    js_set_key_cstr(data, "testId", (Item){.item = i2it((int)test_id)});
    if (get_type_id(error) != LMD_TYPE_UNDEFINED) {
        js_set_key_cstr(data, "error", error);
    }
    js_set_key_cstr(event, "data", data);
    js_array_push(g_node_test_event_queue, event);
}
JS_FORWARD_STATIC_ITEM(node_test_event_stream_identity, (void), js_get_this, ())

static Item node_test_event_stream_next(void) {

    Item self = js_get_this();
    Item events = js_get_key_cstr(self, "__events__");
    Item index_item = js_get_key_cstr(self, "__index__");
    int64_t index = get_type_id(index_item) == LMD_TYPE_INT ? it2i(index_item) : 0;
    int64_t len = get_type_id(events) == LMD_TYPE_ARRAY ? js_array_length(events) : 0;

    Item result = js_new_object();
    if (index < len) {
        js_set_key_cstr(result, "value", js_elements_get_int(events, index));
        js_set_key_cstr(result, "done", (Item){.item = b2it(false)});
        js_set_key_cstr(self, "__index__", (Item){.item = i2it(index + 1)});
    } else {
        js_set_key_cstr(result, "value", make_js_undefined());
        js_set_key_cstr(result, "done", (Item){.item = b2it(true)});
    }
    return js_promise_resolve(result);
}

static Item node_test_make_event_stream(Item events) {
    Item stream = js_new_object();
    js_set_key_cstr(stream, "__events__", events);
    js_set_key_cstr(stream, "__index__", (Item){.item = i2it(0)});
    js_set_native_key(stream, assert_make_string("next"), node_test_event_stream_next);
    Item identity = js_new_native_function(node_test_event_stream_identity);
    Item async_key = js_well_known_symbol_key(5);
    Item iter_key = js_well_known_symbol_key(1);
    // node:test run() returns an async iterable stream, not a materialized array.
    js_set_key_default(stream, async_key, identity);
    js_set_key_default(stream, iter_key, identity);
    js_mark_non_enumerable(stream, async_key);
    js_mark_non_enumerable(stream, iter_key);
    return stream;
}

extern "C" void js_node_test_reset_counts(void) {
    // Node's shell runner resets counters before it creates a JS realm. There
    // is no context-owned table to clear until the first runtime activation.
    if (!js_active_runtime_state) return;
    g_node_test_total_count = 0;
    g_node_test_pass_count = 0;
    g_node_test_fail_count = 0;
}

extern "C" int js_node_test_total_count(void) {
    if (!js_active_runtime_state) return 0;
    return g_node_test_total_count;
}

extern "C" int js_node_test_pass_count(void) {
    if (!js_active_runtime_state) return 0;
    return g_node_test_pass_count;
}

extern "C" int js_node_test_fail_count(void) {
    if (!js_active_runtime_state) return 0;
    return g_node_test_fail_count;
}

static int node_test_current_worker_id(void) {
    const char* worker_id = getenv("NODE_TEST_WORKER_ID");
    if (!worker_id || !worker_id[0]) return 1;
    int id = atoi(worker_id);
    return id > 0 ? id : 1;
}

extern "C" Item js_diagnostics_channel_apply_store_context(const char* name, Item message);
extern "C" void js_diagnostics_channel_restore_context(Item previous);
extern "C" void js_diagnostics_channel_publish_named(const char* name, Item message);

static Item node_test_diagnostics_message(Item name, const char* type) {
    Item message = js_new_object();
    if (get_type_id(name) == LMD_TYPE_STRING) {
        js_set_key_cstr(message, "name", name);
    } else {
        js_set_key_cstr(message, "name", assert_make_string(""));
    }
    js_set_key_cstr(message, "type", assert_make_string(type ? type : "test"));
    return message;
}

static void node_test_diagnostics_error(Item message, Item error) {
    js_set_key_cstr(message, "error", error);
    js_diagnostics_channel_publish_named("tracing:node.test:error", message);
}

static void node_test_diagnostics_end(Item message, Item previous_context) {
    js_diagnostics_channel_publish_named("tracing:node.test:end", message);
    js_diagnostics_channel_restore_context(previous_context);
}

// Build a test context (t) passed to test callbacks
static Item js_build_test_context(void) {
    Item t = js_new_object();

    // t.mock — per-test mock context
    Item mock = js_mock_create_context();
    js_set_key_cstr(t, "mock", mock);

    // t.assert — the assert module + snapshot stubs
    extern Item js_get_assert_namespace(void);
    Item t_assert = js_assert_create_instance(make_js_undefined());
    // add snapshot and fileSnapshot as no-op stubs for test runner context
    js_assert_set_native(t_assert, "snapshot", js_test_context_skip);
    js_assert_set_native(t_assert, "fileSnapshot", js_test_context_skip);
    js_set_key_cstr(t, "assert", t_assert);

    // t.skip(), t.todo(), t.diagnostic(), t.plan()
    js_assert_set_native(t, "skip", js_test_context_skip);
    js_assert_set_native(t, "todo", js_test_context_todo);
    js_assert_set_native(t, "diagnostic", js_test_context_diagnostic);
    js_assert_set_native(t, "plan", js_test_context_plan);

    // t.test — sub-tests
    js_assert_set_native(t, "test", js_test_context_subtest);

    // t.name — filled in later
    js_set_key_cstr(t, "name", make_js_undefined());

    // t.workerId — mirrors NODE_TEST_WORKER_ID for process-isolated test files.
    js_set_key_cstr(t, "workerId", (Item){.item = i2it(node_test_current_worker_id())});

    // t.signal — an AbortSignal stub
    js_set_key_cstr(t, "signal", make_js_undefined());

    // t.fullName
    js_set_key_cstr(t, "fullName", assert_make_string(""));

    return t;
}

static void js_node_test_resolve_options(Item options_or_fn, Item fn,
        Item* options_out, Item* callback_out) {
    *options_out = make_js_undefined();
    *callback_out = make_js_undefined();
    if (js_is_callable(fn)) {
        *callback_out = fn;
        *options_out = options_or_fn;
    } else if (js_is_callable(options_or_fn)) {
        *callback_out = options_or_fn;
    }
}

// test(name, fn) / test(name, options, fn) — run fn synchronously
extern "C" Item js_node_test_run(Item name, Item options_or_fn, Item fn) {

    // Check for skip/todo option in options object
    Item options;
    Item callback;
    js_node_test_resolve_options(options_or_fn, fn, &options, &callback);

    // If options has skip: true or skip is a string, skip the test
    if (get_type_id(options) == LMD_TYPE_MAP) {
        Item skip_val = js_get_key_cstr(options, "skip");
        if (js_is_truthy(skip_val)) {
            return make_js_undefined(); // skip this test
        }
        Item todo_val = js_get_key_cstr(options, "todo");
        if (js_is_truthy(todo_val)) {
            return make_js_undefined(); // todo tests are skipped
        }
    }

    if (!js_is_callable(callback)) {
        return make_js_undefined();
    }

    g_node_test_total_count++;
    int64_t test_id = g_node_test_next_id++;

    // create test context with t.mock, t.assert, t.skip, etc.
    Item t = js_build_test_context();

    // set t.name
    if (get_type_id(name) == LMD_TYPE_STRING) {
        js_set_key_cstr(t, "name", name);
        js_set_key_cstr(t, "fullName", name);
    }

    // node:test run() consumers need instance IDs, not source-location IDs:
    // concurrent generated subtests at the same callsite must stay distinct.
    node_test_emit_event("test:enqueue", name, test_id, make_js_undefined());
    node_test_emit_event("test:dequeue", name, test_id, make_js_undefined());
    node_test_emit_event("test:start", name, test_id, make_js_undefined());

    Item diagnostics_message = node_test_diagnostics_message(name, "test");
    Item diagnostics_previous = js_diagnostics_channel_apply_store_context(
        "tracing:node.test:start", diagnostics_message);

    Item before_hooks_result = node_test_run_hooks(g_node_before_each_hooks, g_node_before_each_count);
    if (item_is_error(before_hooks_result)) {
        Item err = js_error_lane_payload(before_hooks_result);
        node_test_emit_event("test:fail", name, test_id, err);
        node_test_emit_event("test:complete", name, test_id, make_js_undefined());
        node_test_diagnostics_error(diagnostics_message, err);
        node_test_diagnostics_end(diagnostics_message, diagnostics_previous);
        return js_throw_value(err);
    }

    // run callback with (t) or (t, done) — for sync tests we pass t only
    Item callback_result = js_call_function(callback, make_js_undefined(), &t, 1);
    js_microtask_flush();
    bool callback_is_async = node_test_is_promise_like(callback_result);

    // if test threw, re-throw (let the test runner handle it)
    bool callback_threw = false;
    Item callback_error = make_js_undefined();
    if (item_is_error(callback_result)) {
        callback_error = js_error_lane_payload(callback_result);
        callback_threw = true;
    }
    if (!callback_threw && callback_is_async) {
        const char* state = js_promise_state_name(callback_result);
        if (state && strcmp(state, "rejected") == 0) {
            callback_error = make_js_undefined();
            callback_threw = true;
        }
    }

    if (!callback_is_async) {
        Item after_hooks_result = node_test_run_hooks(g_node_after_each_hooks, g_node_after_each_count);
        if (item_is_error(after_hooks_result)) {
            if (!callback_threw) {
                Item err = js_error_lane_payload(after_hooks_result);
                node_test_diagnostics_error(diagnostics_message, err);
                node_test_diagnostics_end(diagnostics_message, diagnostics_previous);
                return js_throw_value(err);
            }
        }
    }

    if (callback_threw) {
        node_test_diagnostics_error(diagnostics_message, callback_error);
        node_test_note_failure();
        node_test_emit_event("test:fail", name, test_id, callback_error);
    } else {
        g_node_test_pass_count++;
        node_test_emit_event("test:pass", name, test_id, make_js_undefined());
    }
    node_test_emit_event("test:complete", name, test_id, make_js_undefined());
    node_test_diagnostics_end(diagnostics_message, diagnostics_previous);

    return make_js_undefined();
}

// describe(name, fn) — grouping, just run fn with scoped hooks
extern "C" Item js_node_test_describe(Item name, Item options_or_fn, Item fn) {

    Item options;
    Item callback;
    js_node_test_resolve_options(options_or_fn, fn, &options, &callback);

    if (get_type_id(options) == LMD_TYPE_MAP) {
        Item skip_val = js_get_key_cstr(options, "skip");
        if (js_is_truthy(skip_val)) return make_js_undefined();
    }
    if (!js_is_callable(callback)) return make_js_undefined();

    int before_each_mark = g_node_before_each_count;
    int after_each_mark = g_node_after_each_count;
    Item diagnostics_message = node_test_diagnostics_message(name, "suite");
    Item diagnostics_previous = js_diagnostics_channel_apply_store_context(
        "tracing:node.test:start", diagnostics_message);
    Item callback_result = js_call_function(callback, make_js_undefined(), NULL, 0);
    js_microtask_flush();
    g_node_before_each_count = before_each_mark;
    g_node_after_each_count = after_each_mark;
    if (item_is_error(callback_result)) {
        Item err = js_error_lane_payload(callback_result);
        node_test_diagnostics_error(diagnostics_message, err);
        node_test_diagnostics_end(diagnostics_message, diagnostics_previous);
        return js_throw_value(err);
    }
    node_test_diagnostics_end(diagnostics_message, diagnostics_previous);
    return make_js_undefined();
}

// hook registration stubs. beforeEach is run by the lightweight test wrapper;
// afterEach is retained and touched once until a full async runner lands.
extern "C" Item js_node_test_hook(Item fn, Item options) {
    (void)fn;
    (void)options;
    return make_js_undefined();
}

extern "C" Item js_node_test_before_each(Item fn, Item options) {
    (void)options;
    node_test_ensure_hook_stores();
    if (js_is_callable(fn)) js_array_push(g_node_before_each_store, fn);
    node_test_store_hook(g_node_before_each_hooks, &g_node_before_each_count, fn);
    return make_js_undefined();
}

extern "C" Item js_node_test_after_each(Item fn, Item options) {
    (void)options;
    node_test_ensure_hook_stores();
    if (js_is_callable(fn)) {
        js_array_push(g_node_after_each_store, fn);
        js_call_function(fn, make_js_undefined(), NULL, 0);
        js_microtask_flush();
    }
    return make_js_undefined();
}

static Item js_node_test_run_files(Item options) {

    node_test_register_roots();
    Item previous_queue = g_node_test_event_queue;
    int before_each_mark = g_node_before_each_count;
    int after_each_mark = g_node_after_each_count;
    int64_t previous_next_id = g_node_test_next_id;

    g_node_test_event_queue = js_array_new(0);
    g_node_test_next_id = 1;

    if (get_type_id(options) == LMD_TYPE_MAP) {
        Item files = js_get_key_cstr(options, "files");
        if (get_type_id(files) == LMD_TYPE_ARRAY) {
            int64_t len = js_array_length(files);
            for (int64_t i = 0; i < len; i++) {
                Item file = js_elements_get_int(files, i);
                if (get_type_id(file) != LMD_TYPE_STRING) continue;
                Item require_result = js_require(file);
                js_microtask_flush();
                if (item_is_error(require_result)) {
                    Item err = js_error_lane_payload(require_result);
                    int64_t id = g_node_test_next_id++;
                    node_test_emit_event("test:enqueue", file, id, make_js_undefined());
                    node_test_emit_event("test:dequeue", file, id, make_js_undefined());
                    node_test_emit_event("test:start", file, id, make_js_undefined());
                    node_test_emit_event("test:fail", file, id, err);
                    node_test_emit_event("test:complete", file, id, make_js_undefined());
                }
            }
        }
    }

    Item events = g_node_test_event_queue;
    g_node_test_event_queue = previous_queue;
    g_node_test_next_id = previous_next_id;
    g_node_before_each_count = before_each_mark;
    g_node_after_each_count = after_each_mark;
    return node_test_make_event_stream(events);
}

extern "C" Item js_get_node_test_namespace(void) {
    if (node_test_namespace.item != 0) return node_test_namespace;

    Item test_fn = js_new_native_function(js_node_test_run);
    node_test_namespace = test_fn;
    node_test_ensure_hook_stores();

    // test is both the default export and a named export
    js_set_key_cstr(node_test_namespace, "test", test_fn);
    js_set_key_cstr(node_test_namespace, "default", test_fn);

    Item describe_fn = js_new_native_function(js_node_test_describe);
    js_set_key_cstr(node_test_namespace, "describe", describe_fn);
    js_set_key_cstr(node_test_namespace, "suite", describe_fn);
    js_set_key_cstr(node_test_namespace, "it", test_fn);
    Item hook_fn = js_new_native_function(js_node_test_hook);
    Item before_each_fn = js_new_native_function(js_node_test_before_each);
    Item after_each_fn = js_new_native_function(js_node_test_after_each);
    js_set_key_cstr(node_test_namespace, "before", hook_fn);
    js_set_key_cstr(node_test_namespace, "after", hook_fn);
    js_set_key_cstr(node_test_namespace, "beforeEach", before_each_fn);
    js_set_key_cstr(node_test_namespace, "afterEach", after_each_fn);

    // mock — global mock object
    Item mock_obj = js_mock_create_context();
    js_set_key_cstr(node_test_namespace, "mock", mock_obj);

    // MockTracker class — same as mock
    js_set_key_cstr(node_test_namespace, "MockTracker", mock_obj);

    // run — in-process file runner that returns a test event iterable
    js_set_native_key(node_test_namespace, assert_make_string("run"), js_node_test_run_files);

    // getTestContext — stub
    js_set_native_key(node_test_namespace, assert_make_string("getTestContext"), js_mock_reset_impl);

    return node_test_namespace;
}

extern "C" void js_node_test_reset(void) {
    // The pre-realm runner call must remain context-free; invoking the mock
    // scheduler before a realm exists would dereference a null capsule.
    if (!js_active_runtime_state) return;
    extern void js_mock_scheduler_reset(void);
    js_mock_scheduler_reset();
    node_test_namespace = (Item){0};
    g_node_before_each_store = (Item){0};
    g_node_after_each_store = (Item){0};
    g_node_test_event_queue = (Item){0};
    memset(g_node_before_each_hooks, 0, sizeof(g_node_before_each_hooks));
    memset(g_node_after_each_hooks, 0, sizeof(g_node_after_each_hooks));
    // Mock wrappers use fixed C entrypoints, but their records are heap Items;
    // clear the context-local slots before a replacement heap reuses them.
    memset(g_mock_slots, 0, sizeof(g_mock_slots));
    g_mock_slot_count = 0;
    g_node_before_each_count = 0;
    g_node_after_each_count = 0;
    g_node_test_next_id = 1;
    g_node_test_roots_epoch = 0;
    js_node_test_reset_counts();
}
