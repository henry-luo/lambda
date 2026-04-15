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

// assert.throws(fn[, error[, message]]) — simplified: check fn throws
extern "C" Item js_assert_module_throws(Item fn, Item error_cls, Item message) {
    if (get_type_id(fn) != LMD_TYPE_FUNC) {
        return throw_assertion_error("assert.throws: first argument must be a function");
    }

    // simplified: call fn, check for exception via try-catch in the transpiler
    // since we can't reliably catch in C, just call and assume it throws
    // if it returns normally, that means no throw → fail
    extern Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
    // note: if fn throws, the exception propagates up and this assertion passes
    // if fn doesn't throw, we reach here and fail
    js_call_function(fn, ItemNull, NULL, 0);

    // if we get here, fn didn't throw
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
