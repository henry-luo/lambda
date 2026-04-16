/**
 * js_assert.cpp — Node.js-style 'assert' module for LambdaJS
 *
 * Provides assertion functions for testing:
 * assert(value), assert.ok, assert.equal, assert.notEqual,
 * assert.strictEqual, assert.notStrictEqual, assert.deepStrictEqual,
 * assert.throws, assert.doesNotThrow, assert.fail
 */
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"

#include <cstring>
#include <cstdio>

static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

static Item assert_make_string(const char* str) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, strlen(str));
    return (Item){.item = s2it(s)};
}

// helper: throw AssertionError
static Item throw_assertion_error(const char* message) {
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);
    Item type_name = assert_make_string("AssertionError");
    Item msg_item = assert_make_string(message);
    Item error = js_new_error_with_name(type_name, msg_item);
    js_throw_value(error);
    return make_js_undefined();
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

// assert(value[, message]) / assert.ok(value[, message])
extern "C" Item js_assert_ok(Item value, Item message) {
    if (!assert_is_truthy(value)) {
        if (get_type_id(message) == LMD_TYPE_STRING) {
            String* s = it2s(message);
            char buf[512];
            int len = (int)s->len < 500 ? (int)s->len : 500;
            memcpy(buf, s->chars, len);
            buf[len] = '\0';
            return throw_assertion_error(buf);
        }
        return throw_assertion_error("The expression evaluated to a falsy value");
    }
    return make_js_undefined();
}

// assert.equal(actual, expected[, message]) — loose equality (==)
extern "C" Item js_assert_equal(Item actual, Item expected, Item message) {
    extern Item js_equal(Item a, Item b);
    Item result = js_equal(actual, expected);
    if (!it2b(result)) {
        if (get_type_id(message) == LMD_TYPE_STRING) {
            String* s = it2s(message);
            char buf[512];
            int len = (int)s->len < 500 ? (int)s->len : 500;
            memcpy(buf, s->chars, len);
            buf[len] = '\0';
            return throw_assertion_error(buf);
        }
        return throw_assertion_error("assert.equal: values are not equal");
    }
    return make_js_undefined();
}

// assert.notEqual(actual, expected[, message])
extern "C" Item js_assert_notEqual(Item actual, Item expected, Item message) {
    extern Item js_equal(Item a, Item b);
    Item result = js_equal(actual, expected);
    if (it2b(result)) {
        if (get_type_id(message) == LMD_TYPE_STRING) {
            String* s = it2s(message);
            char buf[512];
            int len = (int)s->len < 500 ? (int)s->len : 500;
            memcpy(buf, s->chars, len);
            buf[len] = '\0';
            return throw_assertion_error(buf);
        }
        return throw_assertion_error("assert.notEqual: values are equal");
    }
    return make_js_undefined();
}

// assert.strictEqual(actual, expected[, message]) — strict equality (===)
extern "C" Item js_assert_strictEqual(Item actual, Item expected, Item message) {
    extern Item js_strict_equal(Item a, Item b);
    Item result = js_strict_equal(actual, expected);
    if (!it2b(result)) {
        if (get_type_id(message) == LMD_TYPE_STRING) {
            String* s = it2s(message);
            char buf[512];
            int len = (int)s->len < 500 ? (int)s->len : 500;
            memcpy(buf, s->chars, len);
            buf[len] = '\0';
            return throw_assertion_error(buf);
        }
        return throw_assertion_error("assert.strictEqual: values are not strictly equal");
    }
    return make_js_undefined();
}

// assert.notStrictEqual(actual, expected[, message])
extern "C" Item js_assert_notStrictEqual(Item actual, Item expected, Item message) {
    extern Item js_strict_equal(Item a, Item b);
    Item result = js_strict_equal(actual, expected);
    if (it2b(result)) {
        if (get_type_id(message) == LMD_TYPE_STRING) {
            String* s = it2s(message);
            char buf[512];
            int len = (int)s->len < 500 ? (int)s->len : 500;
            memcpy(buf, s->chars, len);
            buf[len] = '\0';
            return throw_assertion_error(buf);
        }
        return throw_assertion_error("assert.notStrictEqual: values are strictly equal");
    }
    return make_js_undefined();
}

// assert.deepStrictEqual(actual, expected[, message])
extern "C" Item js_assert_deepStrictEqual(Item actual, Item expected, Item message) {
    extern Item js_util_isDeepStrictEqual(Item a, Item b);
    Item result = js_util_isDeepStrictEqual(actual, expected);
    bool equal = (get_type_id(result) == LMD_TYPE_INT && it2i(result) == 1) ||
                 (get_type_id(result) == LMD_TYPE_BOOL && it2b(result));
    if (!equal) {
        if (get_type_id(message) == LMD_TYPE_STRING) {
            String* s = it2s(message);
            char buf[512];
            int len = (int)s->len < 500 ? (int)s->len : 500;
            memcpy(buf, s->chars, len);
            buf[len] = '\0';
            return throw_assertion_error(buf);
        }
        return throw_assertion_error("assert.deepStrictEqual: values are not deep-strict-equal");
    }
    return make_js_undefined();
}

// assert.notDeepStrictEqual(actual, expected[, message])
extern "C" Item js_assert_notDeepStrictEqual(Item actual, Item expected, Item message) {
    extern Item js_util_isDeepStrictEqual(Item a, Item b);
    Item result = js_util_isDeepStrictEqual(actual, expected);
    bool equal = (get_type_id(result) == LMD_TYPE_INT && it2i(result) == 1) ||
                 (get_type_id(result) == LMD_TYPE_BOOL && it2b(result));
    if (equal) {
        if (get_type_id(message) == LMD_TYPE_STRING) {
            String* s = it2s(message);
            char buf[512];
            int len = (int)s->len < 500 ? (int)s->len : 500;
            memcpy(buf, s->chars, len);
            buf[len] = '\0';
            return throw_assertion_error(buf);
        }
        return throw_assertion_error("assert.notDeepStrictEqual: values are deep-strict-equal");
    }
    return make_js_undefined();
}

// assert.deepEqual(actual, expected[, message]) — legacy loose deep equality
// In modern Node.js (v16+), deepEqual behaves like deepStrictEqual
extern "C" Item js_assert_deepEqual(Item actual, Item expected, Item message) {
    extern Item js_util_isDeepStrictEqual(Item a, Item b);
    Item result = js_util_isDeepStrictEqual(actual, expected);
    bool equal = (get_type_id(result) == LMD_TYPE_INT && it2i(result) == 1) ||
                 (get_type_id(result) == LMD_TYPE_BOOL && it2b(result));
    if (!equal) {
        if (get_type_id(message) == LMD_TYPE_STRING) {
            String* s = it2s(message);
            char buf[512];
            int len = (int)s->len < 500 ? (int)s->len : 500;
            memcpy(buf, s->chars, len);
            buf[len] = '\0';
            return throw_assertion_error(buf);
        }
        return throw_assertion_error("assert.deepEqual: values are not deep-equal");
    }
    return make_js_undefined();
}

// assert.notDeepEqual(actual, expected[, message])
extern "C" Item js_assert_notDeepEqual(Item actual, Item expected, Item message) {
    extern Item js_util_isDeepStrictEqual(Item a, Item b);
    Item result = js_util_isDeepStrictEqual(actual, expected);
    bool equal = (get_type_id(result) == LMD_TYPE_INT && it2i(result) == 1) ||
                 (get_type_id(result) == LMD_TYPE_BOOL && it2b(result));
    if (equal) {
        if (get_type_id(message) == LMD_TYPE_STRING) {
            String* s = it2s(message);
            char buf[512];
            int len = (int)s->len < 500 ? (int)s->len : 500;
            memcpy(buf, s->chars, len);
            buf[len] = '\0';
            return throw_assertion_error(buf);
        }
        return throw_assertion_error("assert.notDeepEqual: values are deep-equal");
    }
    return make_js_undefined();
}

// assert.fail([message])
extern "C" Item js_assert_fail(Item message) {
    if (get_type_id(message) == LMD_TYPE_STRING) {
        String* s = it2s(message);
        char buf[512];
        int len = (int)s->len < 500 ? (int)s->len : 500;
        memcpy(buf, s->chars, len);
        buf[len] = '\0';
        return throw_assertion_error(buf);
    }
    return throw_assertion_error("Failed");
}

// assert.throws(fn[, error[, message]]) — Node.js compatible
// error can be: Error class, RegExp, object with properties, or validation function
extern "C" Item js_assert_module_throws(Item fn, Item error_expected, Item message) {
    if (get_type_id(fn) != LMD_TYPE_FUNC) {
        return throw_assertion_error("assert.throws: first argument must be a function");
    }

    extern Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
    extern int js_check_exception(void);
    extern Item js_clear_exception(void);
    extern Item js_instanceof(Item left, Item right);
    extern Item js_regex_test(Item regex, Item str);
    extern Item js_property_get(Item obj, Item key);
    extern Item js_strict_equal(Item left, Item right);

    // call fn — if it throws, exception will be pending
    js_call_function(fn, make_js_undefined(), NULL, 0);

    if (!js_check_exception()) {
        // fn didn't throw — that's a failure
        if (get_type_id(message) == LMD_TYPE_STRING) {
            String* s = it2s(message);
            char buf[512];
            int len = (int)s->len < 500 ? (int)s->len : 500;
            memcpy(buf, s->chars, len);
            buf[len] = '\0';
            return throw_assertion_error(buf);
        }
        return throw_assertion_error("Missing expected exception");
    }

    // fn threw — get the thrown value
    Item thrown = js_clear_exception();

    // if no expected argument, any throw is a pass
    TypeId exp_type = get_type_id(error_expected);
    if (exp_type == LMD_TYPE_UNDEFINED || exp_type == LMD_TYPE_NULL) {
        return make_js_undefined();
    }

    // validate thrown against expected
    if (exp_type == LMD_TYPE_FUNC) {
        // Error class: check instanceof
        Item result = js_instanceof(thrown, error_expected);
        if (get_type_id(result) == LMD_TYPE_BOOL && it2b(result)) {
            return make_js_undefined();
        }
        // maybe it's a validation function — call it with thrown
        Item validate_result = js_call_function(error_expected, make_js_undefined(), &thrown, 1);
        if (js_check_exception()) {
            // validation function threw — re-throw
            return make_js_undefined();
        }
        if (assert_is_truthy(validate_result)) {
            return make_js_undefined();
        }
        // failed validation — re-throw the original
        extern void js_throw_value(Item error);
        js_throw_value(thrown);
        return make_js_undefined();
    }

    // RegExp: test thrown.message against regex
    if (exp_type == LMD_TYPE_MAP) {
        // check if it's a RegExp (has __class_name__ = "RegExp")
        bool has_regex = false;
        Item cn_key = assert_make_string("__class_name__");
        Item cn_val = js_property_get(error_expected, cn_key);
        if (get_type_id(cn_val) == LMD_TYPE_STRING) {
            String* cns = it2s(cn_val);
            if (cns && cns->len == 6 && strncmp(cns->chars, "RegExp", 6) == 0)
                has_regex = true;
        }

        if (has_regex) {
            // RegExp: test against thrown.message or String(thrown)
            extern Item js_to_string_val(Item value);
            Item msg_key = assert_make_string("message");
            Item thrown_msg = js_property_get(thrown, msg_key);
            if (get_type_id(thrown_msg) == LMD_TYPE_UNDEFINED) {
                thrown_msg = js_to_string_val(thrown);
            }
            Item test_result = js_regex_test(error_expected, thrown_msg);
            if (get_type_id(test_result) == LMD_TYPE_BOOL && it2b(test_result)) {
                return make_js_undefined();
            }
            // regex didn't match — throw assertion error
            return throw_assertion_error("The input did not match the regular expression");
        }

        // Object pattern: validate each property of expected against thrown
        // e.g. { message: "hello", code: "ERR_ASSERTION" }
        extern Item js_object_keys(Item obj);
        Item keys = js_object_keys(error_expected);
        if (get_type_id(keys) == LMD_TYPE_ARRAY) {
            for (int64_t i = 0; i < keys.array->length; i++) {
                Item key = list_get(keys.list, (int)i);
                Item expected_val = js_property_get(error_expected, key);
                Item actual_val = js_property_get(thrown, key);

                // check if expected_val is a RegExp (for stack: /pattern/)
                TypeId ev_type = get_type_id(expected_val);
                if (ev_type == LMD_TYPE_MAP) {
                    Item ev_cn = js_property_get(expected_val, cn_key);
                    if (get_type_id(ev_cn) == LMD_TYPE_STRING) {
                        String* ev_cns = it2s(ev_cn);
                        if (ev_cns && ev_cns->len == 6 && strncmp(ev_cns->chars, "RegExp", 6) == 0) {
                            // regex match
                            extern Item js_to_string_val(Item value);
                            Item actual_str = (get_type_id(actual_val) == LMD_TYPE_STRING) ? actual_val : js_to_string_val(actual_val);
                            Item test_result = js_regex_test(expected_val, actual_str);
                            if (get_type_id(test_result) != LMD_TYPE_BOOL || !it2b(test_result)) {
                                char buf[256];
                                String* ks = it2s(key);
                                snprintf(buf, sizeof(buf), "Expected property '%.*s' to match regex",
                                         ks ? (int)ks->len : 1, ks ? ks->chars : "?");
                                return throw_assertion_error(buf);
                            }
                            continue;
                        }
                    }
                }

                // strict equality check
                Item eq = js_strict_equal(expected_val, actual_val);
                if (get_type_id(eq) != LMD_TYPE_BOOL || !it2b(eq)) {
                    char buf[256];
                    String* ks = it2s(key);
                    snprintf(buf, sizeof(buf), "Expected property '%.*s' to be strictly equal",
                             ks ? (int)ks->len : 1, ks ? ks->chars : "?");
                    return throw_assertion_error(buf);
                }
            }
            return make_js_undefined();
        }
    }

    // unknown expected type — just pass if something was thrown
    return make_js_undefined();
}

// assert.doesNotThrow(fn[, error[, message]]) — simplified
extern "C" Item js_assert_module_doesNotThrow(Item fn, Item error_cls, Item message) {
    if (get_type_id(fn) != LMD_TYPE_FUNC) return make_js_undefined();

    // call fn — if it throws, it will propagate up naturally
    extern Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
    js_call_function(fn, ItemNull, NULL, 0);
    return make_js_undefined();
}

// assert.ifError(value) — throw if value is truthy
extern "C" Item js_assert_ifError(Item value) {
    if (assert_is_truthy(value)) {
        extern void js_throw_value(Item error);
        js_throw_value(value);
    }
    return make_js_undefined();
}

// =============================================================================
// assert.match / assert.doesNotMatch
// =============================================================================

extern "C" Item js_regex_test(Item regex, Item str);

extern "C" Item js_assert_match(Item string_val, Item regexp, Item message) {
    if (get_type_id(string_val) != LMD_TYPE_STRING) {
        return throw_assertion_error("The \"string\" argument must be of type string");
    }
    Item result = js_regex_test(regexp, string_val);
    if (!it2b(result)) {
        if (get_type_id(message) == LMD_TYPE_STRING) {
            String* ms = it2s(message);
            char buf[1024];
            int mlen = (int)ms->len;
            if (mlen >= (int)sizeof(buf)) mlen = (int)sizeof(buf) - 1;
            memcpy(buf, ms->chars, mlen);
            buf[mlen] = '\0';
            return throw_assertion_error(buf);
        }
        return throw_assertion_error("The input did not match the regular expression");
    }
    return make_js_undefined();
}

extern "C" Item js_assert_doesNotMatch(Item string_val, Item regexp, Item message) {
    if (get_type_id(string_val) != LMD_TYPE_STRING) {
        return throw_assertion_error("The \"string\" argument must be of type string");
    }
    Item result = js_regex_test(regexp, string_val);
    if (it2b(result)) {
        if (get_type_id(message) == LMD_TYPE_STRING) {
            String* ms = it2s(message);
            char buf[1024];
            int mlen = (int)ms->len;
            if (mlen >= (int)sizeof(buf)) mlen = (int)sizeof(buf) - 1;
            memcpy(buf, ms->chars, mlen);
            buf[mlen] = '\0';
            return throw_assertion_error(buf);
        }
        return throw_assertion_error("The input was expected to not match the regular expression");
    }
    return make_js_undefined();
}

// =============================================================================
// assert Module Namespace
// =============================================================================

static Item assert_namespace = {0};

static void assert_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = assert_make_string(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

extern "C" Item js_get_assert_namespace(void) {
    if (assert_namespace.item != 0) return assert_namespace;

    // namespace doubles as the assert() function itself
    assert_namespace = js_new_function((void*)js_assert_ok, 2);

    assert_set_method(assert_namespace, "ok",                  (void*)js_assert_ok, 2);
    assert_set_method(assert_namespace, "equal",               (void*)js_assert_equal, 3);
    assert_set_method(assert_namespace, "notEqual",            (void*)js_assert_notEqual, 3);
    assert_set_method(assert_namespace, "strictEqual",         (void*)js_assert_strictEqual, 3);
    assert_set_method(assert_namespace, "notStrictEqual",      (void*)js_assert_notStrictEqual, 3);
    assert_set_method(assert_namespace, "deepStrictEqual",     (void*)js_assert_deepStrictEqual, 3);
    assert_set_method(assert_namespace, "notDeepStrictEqual",  (void*)js_assert_notDeepStrictEqual, 3);
    assert_set_method(assert_namespace, "deepEqual",           (void*)js_assert_deepEqual, 3);
    assert_set_method(assert_namespace, "notDeepEqual",        (void*)js_assert_notDeepEqual, 3);
    assert_set_method(assert_namespace, "fail",                (void*)js_assert_fail, 1);
    assert_set_method(assert_namespace, "throws",              (void*)js_assert_module_throws, 3);
    assert_set_method(assert_namespace, "doesNotThrow",        (void*)js_assert_module_doesNotThrow, 3);
    assert_set_method(assert_namespace, "ifError",             (void*)js_assert_ifError, 1);
    assert_set_method(assert_namespace, "match",               (void*)js_assert_match, 3);
    assert_set_method(assert_namespace, "doesNotMatch",        (void*)js_assert_doesNotMatch, 3);

    // default export
    js_property_set(assert_namespace, assert_make_string("default"), assert_namespace);

    return assert_namespace;
}

extern "C" void js_assert_reset(void) {
    assert_namespace = (Item){0};
}

// =============================================================================
// node:test module — basic test runner
// =============================================================================

static Item node_test_namespace = {0};

// test(name, fn) / test(name, options, fn) — run fn synchronously
extern "C" Item js_node_test_run(Item name, Item options_or_fn, Item fn) {
    extern Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
    extern int js_check_exception(void);
    extern Item js_clear_exception(void);
    extern void js_throw_value(Item error);

    Item callback = make_js_undefined();
    if (get_type_id(fn) == LMD_TYPE_FUNC) {
        callback = fn;
    } else if (get_type_id(options_or_fn) == LMD_TYPE_FUNC) {
        callback = options_or_fn;
    }

    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        return make_js_undefined();
    }

    // create a minimal test context (t) with t.skip(), t.todo(), t.assert, etc.
    Item t = js_new_object();
    // t.skip — no-op that marks test as skipped
    // (stubbed for compatibility)

    js_call_function(callback, make_js_undefined(), &t, 1);

    // if test threw, re-throw (let the test runner handle it)
    if (js_check_exception()) {
        Item err = js_clear_exception();
        js_throw_value(err);
    }

    return make_js_undefined();
}

// describe(name, fn) — grouping, just run fn
extern "C" Item js_node_test_describe(Item name, Item options_or_fn, Item fn) {
    return js_node_test_run(name, options_or_fn, fn);
}

extern "C" Item js_get_node_test_namespace(void) {
    if (node_test_namespace.item != 0) return node_test_namespace;

    node_test_namespace = js_new_object();

    Item test_fn = js_new_function((void*)js_node_test_run, 3);
    // test is both the default export and a named export
    js_property_set(node_test_namespace, assert_make_string("test"), test_fn);
    js_property_set(node_test_namespace, assert_make_string("default"), node_test_namespace);

    Item describe_fn = js_new_function((void*)js_node_test_describe, 3);
    js_property_set(node_test_namespace, assert_make_string("describe"), describe_fn);
    js_property_set(node_test_namespace, assert_make_string("it"), test_fn);
    js_property_set(node_test_namespace, assert_make_string("before"), js_new_function((void*)js_node_test_run, 3));
    js_property_set(node_test_namespace, assert_make_string("after"), js_new_function((void*)js_node_test_run, 3));
    js_property_set(node_test_namespace, assert_make_string("beforeEach"), js_new_function((void*)js_node_test_run, 3));
    js_property_set(node_test_namespace, assert_make_string("afterEach"), js_new_function((void*)js_node_test_run, 3));

    return node_test_namespace;
}

extern "C" void js_node_test_reset(void) {
    node_test_namespace = (Item){0};
}
