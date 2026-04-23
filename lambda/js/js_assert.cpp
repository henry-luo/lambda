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

// helper: throw AssertionError with full Node.js properties
static Item throw_assertion_error_full(const char* message, Item actual, Item expected, const char* op_str, bool generated = true) {
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);
    extern Item js_property_set(Item obj, Item key, Item value);
    Item type_name = assert_make_string("AssertionError");
    Item msg_item = assert_make_string(message);
    Item error = js_new_error_with_name(type_name, msg_item);
    // Node.js AssertionError properties
    js_property_set(error, assert_make_string("code"), assert_make_string("ERR_ASSERTION"));
    js_property_set(error, assert_make_string("name"), assert_make_string("AssertionError"));
    js_property_set(error, assert_make_string("actual"), actual);
    js_property_set(error, assert_make_string("expected"), expected);
    if (op_str) js_property_set(error, assert_make_string("operator"), assert_make_string(op_str));
    js_property_set(error, assert_make_string("generatedMessage"), (Item){.item = b2it(generated)});
    js_throw_value(error);
    return make_js_undefined();
}

static Item throw_assertion_error(const char* message) {
    return throw_assertion_error_full(message, make_js_undefined(), make_js_undefined(), NULL);
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

// helper: throw with user message or auto-generated message and props
static Item throw_assert_msg_or_auto(Item message, const char* default_msg,
                                     Item actual, Item expected, const char* op_str) {
    if (get_type_id(message) == LMD_TYPE_STRING) {
        String* s = it2s(message);
        char buf[512];
        int len = (int)s->len < 500 ? (int)s->len : 500;
        memcpy(buf, s->chars, len);
        buf[len] = '\0';
        return throw_assertion_error_full(buf, actual, expected, op_str, false);
    }
    return throw_assertion_error_full(default_msg, actual, expected, op_str, true);
}

// assert(value[, message]) / assert.ok(value[, message])
extern "C" Item js_assert_ok(Item value, Item message) {
    if (!assert_is_truthy(value)) {
        return throw_assert_msg_or_auto(message,
            "The expression evaluated to a falsy value",
            value, (Item){.item = b2it(true)}, "==");
    }
    return make_js_undefined();
}

// assert.equal(actual, expected[, message]) — loose equality (==)
extern "C" Item js_assert_equal(Item actual, Item expected, Item message) {
    extern Item js_equal(Item a, Item b);
    Item result = js_equal(actual, expected);
    if (!it2b(result)) {
        return throw_assert_msg_or_auto(message,
            "assert.equal: values are not equal", actual, expected, "==");
    }
    return make_js_undefined();
}

// assert.notEqual(actual, expected[, message])
extern "C" Item js_assert_notEqual(Item actual, Item expected, Item message) {
    extern Item js_equal(Item a, Item b);
    Item result = js_equal(actual, expected);
    if (it2b(result)) {
        return throw_assert_msg_or_auto(message,
            "assert.notEqual: values are equal", actual, expected, "!=");
    }
    return make_js_undefined();
}

// assert.strictEqual(actual, expected[, message]) — strict equality (===)
extern "C" Item js_assert_strictEqual(Item actual, Item expected, Item message) {
    extern Item js_strict_equal(Item a, Item b);
    Item result = js_strict_equal(actual, expected);
    if (!it2b(result)) {
        return throw_assert_msg_or_auto(message,
            "assert.strictEqual: values are not strictly equal", actual, expected, "strictEqual");
    }
    return make_js_undefined();
}

// assert.notStrictEqual(actual, expected[, message])
extern "C" Item js_assert_notStrictEqual(Item actual, Item expected, Item message) {
    extern Item js_strict_equal(Item a, Item b);
    Item result = js_strict_equal(actual, expected);
    if (it2b(result)) {
        return throw_assert_msg_or_auto(message,
            "assert.notStrictEqual: values are strictly equal", actual, expected, "notStrictEqual");
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
        return throw_assert_msg_or_auto(message,
            "assert.deepStrictEqual: values are not deep-strict-equal", actual, expected, "deepStrictEqual");
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
        return throw_assert_msg_or_auto(message,
            "assert.notDeepStrictEqual: values are deep-strict-equal", actual, expected, "notDeepStrictEqual");
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
        return throw_assert_msg_or_auto(message,
            "assert.deepEqual: values are not deep-equal", actual, expected, "deepEqual");
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
        return throw_assert_msg_or_auto(message,
            "assert.notDeepEqual: values are deep-equal", actual, expected, "notDeepEqual");
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
        extern void js_throw_value(Item error);
        js_throw_value(message);
        return make_js_undefined();
    }
    if (tid == LMD_TYPE_UNDEFINED || tid == LMD_TYPE_NULL) {
        return throw_assertion_error_full("Failed", make_js_undefined(), make_js_undefined(), "fail", true);
    }
    return throw_assertion_error_full("Failed", make_js_undefined(), make_js_undefined(), "fail", true);
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
// Per Node.js spec: throws AssertionError with message "ifError got unwanted exception: <msg>"
extern "C" Item js_assert_ifError(Item value) {
    // ifError throws for any value that is NOT null or undefined
    TypeId tid = get_type_id(value);
    if (value.item != 0 && tid != LMD_TYPE_NULL && tid != LMD_TYPE_UNDEFINED) {
        extern void js_throw_value(Item error);
        extern Item js_property_get(Item obj, Item key);
        extern Item js_property_set(Item obj, Item key, Item value);
        extern Item js_to_string(Item val);
        extern Item js_new_error_with_name(Item type_name, Item message);

        // Build message: "ifError got unwanted exception: <original message>"
        const char* orig_msg = "";
        if (get_type_id(value) == LMD_TYPE_MAP) {
            Item msg_key = assert_make_string("message");
            Item msg_val = js_property_get(value, msg_key);
            if (get_type_id(msg_val) == LMD_TYPE_STRING) {
                orig_msg = it2s(msg_val)->chars;
            }
        } else if (get_type_id(value) == LMD_TYPE_STRING) {
            orig_msg = it2s(value)->chars;
        }
        char buf[512];
        snprintf(buf, sizeof(buf), "ifError got unwanted exception: %s", orig_msg);

        // Create AssertionError with proper properties
        Item type_name = assert_make_string("AssertionError");
        Item msg_item = assert_make_string(buf);
        Item error = js_new_error_with_name(type_name, msg_item);
        js_property_set(error, assert_make_string("code"), assert_make_string("ERR_ASSERTION"));
        js_property_set(error, assert_make_string("name"), assert_make_string("AssertionError"));
        js_property_set(error, assert_make_string("actual"), value);
        js_property_set(error, assert_make_string("expected"), ItemNull);
        js_property_set(error, assert_make_string("operator"), assert_make_string("ifError"));
        js_property_set(error, assert_make_string("generatedMessage"), (Item){.item = b2it(false)});
        js_throw_value(error);
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

// assert.partialDeepStrictEqual(actual, expected[, message])
// Checks that expected is a "subset" of actual (all keys in expected match)
extern "C" Item js_assert_partialDeepStrictEqual(Item actual, Item expected, Item message) {
    // Delegate to deepStrictEqual for now — a proper implementation
    // would only check keys present in expected
    return js_assert_deepStrictEqual(actual, expected, message);
}

// =============================================================================
// assert.rejects / assert.doesNotReject — async assertion helpers
// =============================================================================

// Shared state for promise handlers (single-threaded, safe)
static Item s_rejects_error_expected = {0};
static Item s_rejects_message = {0};

// Validate a rejected value against an expected error pattern.
// Returns true if validation passes, false if it fails (and throws assertion error).
static bool validate_rejection(Item thrown, Item error_expected, Item message) {
    extern Item js_instanceof(Item left, Item right);
    extern Item js_regex_test(Item regex, Item str);
    extern Item js_property_get(Item obj, Item key);
    extern Item js_strict_equal(Item left, Item right);
    extern Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
    extern int js_check_exception(void);
    extern Item js_object_keys(Item obj);

    TypeId exp_type = get_type_id(error_expected);
    if (exp_type == LMD_TYPE_UNDEFINED || exp_type == LMD_TYPE_NULL) {
        return true; // any rejection is fine
    }

    if (exp_type == LMD_TYPE_FUNC) {
        // Error class: check instanceof
        Item result = js_instanceof(thrown, error_expected);
        if (get_type_id(result) == LMD_TYPE_BOOL && it2b(result)) return true;
        // maybe it's a validation function
        Item validate_result = js_call_function(error_expected, make_js_undefined(), &thrown, 1);
        if (js_check_exception()) return true; // validator threw — propagate
        if (assert_is_truthy(validate_result)) return true;
        // validation failed
        throw_assertion_error("The validation function is expected to return \"true\"");
        return false;
    }

    if (exp_type == LMD_TYPE_MAP) {
        // check if RegExp
        Item cn_key = assert_make_string("__class_name__");
        Item cn_val = js_property_get(error_expected, cn_key);
        bool is_regex = false;
        if (get_type_id(cn_val) == LMD_TYPE_STRING) {
            String* cns = it2s(cn_val);
            if (cns && cns->len == 6 && strncmp(cns->chars, "RegExp", 6) == 0)
                is_regex = true;
        }

        if (is_regex) {
            extern Item js_to_string_val(Item value);
            Item msg_key = assert_make_string("message");
            Item thrown_msg = js_property_get(thrown, msg_key);
            if (get_type_id(thrown_msg) == LMD_TYPE_UNDEFINED)
                thrown_msg = js_to_string_val(thrown);
            Item test_result = js_regex_test(error_expected, thrown_msg);
            if (get_type_id(test_result) == LMD_TYPE_BOOL && it2b(test_result)) return true;
            throw_assertion_error("The input did not match the regular expression");
            return false;
        }

        // Object pattern: validate each property
        Item keys = js_object_keys(error_expected);
        if (get_type_id(keys) == LMD_TYPE_ARRAY) {
            for (int64_t i = 0; i < keys.array->length; i++) {
                Item key = list_get(keys.list, (int)i);
                Item expected_val = js_property_get(error_expected, key);
                Item actual_val = js_property_get(thrown, key);

                // check if expected_val is a RegExp
                TypeId ev_type = get_type_id(expected_val);
                if (ev_type == LMD_TYPE_MAP) {
                    Item ev_cn = js_property_get(expected_val, cn_key);
                    if (get_type_id(ev_cn) == LMD_TYPE_STRING) {
                        String* ev_cns = it2s(ev_cn);
                        if (ev_cns && ev_cns->len == 6 && strncmp(ev_cns->chars, "RegExp", 6) == 0) {
                            extern Item js_to_string_val(Item value);
                            Item actual_str = (get_type_id(actual_val) == LMD_TYPE_STRING)
                                ? actual_val : js_to_string_val(actual_val);
                            Item test_result = js_regex_test(expected_val, actual_str);
                            if (get_type_id(test_result) != LMD_TYPE_BOOL || !it2b(test_result)) {
                                char buf[256];
                                String* ks = it2s(key);
                                snprintf(buf, sizeof(buf), "Expected property '%.*s' to match regex",
                                         ks ? (int)ks->len : 1, ks ? ks->chars : "?");
                                throw_assertion_error(buf);
                                return false;
                            }
                            continue;
                        }
                    }
                }

                Item eq = js_strict_equal(expected_val, actual_val);
                if (get_type_id(eq) != LMD_TYPE_BOOL || !it2b(eq)) {
                    char buf[256];
                    String* ks = it2s(key);
                    snprintf(buf, sizeof(buf), "Expected property '%.*s' to be strictly equal",
                             ks ? (int)ks->len : 1, ks ? ks->chars : "?");
                    throw_assertion_error(buf);
                    return false;
                }
            }
        }
        return true;
    }

    return true;
}

static Item js_assert_rejects_on_fulfilled(Item value) {
    // promise fulfilled when it should have rejected
    throw_assertion_error("Missing expected rejection");
    return make_js_undefined();
}

static Item js_assert_rejects_on_rejected(Item reason) {
    Item error_expected = s_rejects_error_expected;
    Item message = s_rejects_message;
    validate_rejection(reason, error_expected, message);
    return make_js_undefined();
}

// assert.rejects(asyncFnOrPromise[, error[, message]])
extern "C" Item js_assert_rejects(Item asyncFnOrPromise, Item error_expected, Item message) {
    extern Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
    extern int js_check_exception(void);
    extern Item js_clear_exception(void);
    extern Item js_promise_resolve(Item value);
    extern Item js_promise_then(Item promise, Item on_fulfilled, Item on_rejected);

    Item promise;
    if (get_type_id(asyncFnOrPromise) == LMD_TYPE_FUNC) {
        promise = js_call_function(asyncFnOrPromise, make_js_undefined(), NULL, 0);
        if (js_check_exception()) {
            // synchronous throw — treat as rejection
            Item thrown = js_clear_exception();
            validate_rejection(thrown, error_expected, message);
            return js_promise_resolve(make_js_undefined());
        }
    } else {
        promise = asyncFnOrPromise;
    }

    s_rejects_error_expected = error_expected;
    s_rejects_message = message;

    Item on_fulfilled = js_new_function((void*)js_assert_rejects_on_fulfilled, 1);
    Item on_rejected = js_new_function((void*)js_assert_rejects_on_rejected, 1);
    return js_promise_then(promise, on_fulfilled, on_rejected);
}

static Item js_assert_doesNotReject_on_rejected(Item reason) {
    // promise rejected when it should not have
    extern void js_throw_value(Item error);
    // if reason is already an error, re-throw it
    if (get_type_id(reason) == LMD_TYPE_MAP) {
        js_throw_value(reason);
    } else {
        throw_assertion_error("Got unwanted rejection");
    }
    return make_js_undefined();
}

// assert.doesNotReject(asyncFnOrPromise[, error[, message]])
extern "C" Item js_assert_doesNotReject(Item asyncFnOrPromise, Item error_expected, Item message) {
    extern Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
    extern int js_check_exception(void);
    extern Item js_clear_exception(void);
    extern Item js_promise_resolve(Item value);
    extern Item js_promise_then(Item promise, Item on_fulfilled, Item on_rejected);

    Item promise;
    if (get_type_id(asyncFnOrPromise) == LMD_TYPE_FUNC) {
        promise = js_call_function(asyncFnOrPromise, make_js_undefined(), NULL, 0);
        if (js_check_exception()) {
            Item thrown = js_clear_exception();
            throw_assertion_error("Got unwanted rejection");
            return js_promise_resolve(make_js_undefined());
        }
    } else {
        promise = asyncFnOrPromise;
    }

    Item on_fulfilled = js_new_function(NULL, 0); // no-op
    Item on_rejected = js_new_function((void*)js_assert_doesNotReject_on_rejected, 1);
    return js_promise_then(promise, on_fulfilled, on_rejected);
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

// AssertionError constructor: new assert.AssertionError({ message, actual, expected, operator })
extern "C" Item js_assert_AssertionError_ctor(Item options) {
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern Item js_property_get(Item obj, Item key);
    extern Item js_property_set(Item obj, Item key, Item value);
    const char* msg_str = "assertion error";
    const char* op_str = NULL;
    Item actual = make_js_undefined();
    Item expected = make_js_undefined();
    if (get_type_id(options) == LMD_TYPE_MAP) {
        Item m = js_property_get(options, assert_make_string("message"));
        if (get_type_id(m) == LMD_TYPE_STRING) {
            String* ms = it2s(m);
            // use message string directly
            msg_str = ms->chars;
        }
        actual = js_property_get(options, assert_make_string("actual"));
        expected = js_property_get(options, assert_make_string("expected"));
        Item op_item = js_property_get(options, assert_make_string("operator"));
        if (get_type_id(op_item) == LMD_TYPE_STRING) {
            op_str = it2s(op_item)->chars;
        }
    } else if (get_type_id(options) == LMD_TYPE_STRING) {
        msg_str = it2s(options)->chars;
    }
    Item type_name = assert_make_string("AssertionError");
    Item msg_item = assert_make_string(msg_str);
    Item error = js_new_error_with_name(type_name, msg_item);
    js_property_set(error, assert_make_string("code"), assert_make_string("ERR_ASSERTION"));
    js_property_set(error, assert_make_string("name"), assert_make_string("AssertionError"));
    js_property_set(error, assert_make_string("actual"), actual);
    js_property_set(error, assert_make_string("expected"), expected);
    if (op_str) js_property_set(error, assert_make_string("operator"), assert_make_string(op_str));
    js_property_set(error, assert_make_string("generatedMessage"), (Item){.item = b2it(false)});
    // Set prototype chain: error.__proto__ = AssertionError.prototype (inherits from Error.prototype)
    if (assert_namespace.item != 0) {
        Item ae_fn = js_property_get(assert_namespace, assert_make_string("AssertionError"));
        if (get_type_id(ae_fn) == LMD_TYPE_FUNC) {
            Item ae_proto = js_property_get(ae_fn, assert_make_string("prototype"));
            if (get_type_id(ae_proto) == LMD_TYPE_MAP) {
                js_set_prototype(error, ae_proto);
            }
        }
    }
    return error;
}

// Assert constructor: new Assert(options?) — creates an instance with all assert methods
// options: { diff: 'full'|'simple' } (stored but not used for behavior changes in our impl)
extern "C" Item js_assert_constructor(Item options) {
    // create a callable function object (assert(value) works)
    Item instance = js_new_function((void*)js_assert_ok, 2);

    // copy all assert methods onto this instance
    assert_set_method(instance, "ok",                  (void*)js_assert_ok, 2);
    assert_set_method(instance, "equal",               (void*)js_assert_equal, 3);
    assert_set_method(instance, "notEqual",            (void*)js_assert_notEqual, 3);
    assert_set_method(instance, "strictEqual",         (void*)js_assert_strictEqual, 3);
    assert_set_method(instance, "notStrictEqual",      (void*)js_assert_notStrictEqual, 3);
    assert_set_method(instance, "deepStrictEqual",     (void*)js_assert_deepStrictEqual, 3);
    assert_set_method(instance, "notDeepStrictEqual",  (void*)js_assert_notDeepStrictEqual, 3);
    assert_set_method(instance, "deepEqual",           (void*)js_assert_deepEqual, 3);
    assert_set_method(instance, "notDeepEqual",        (void*)js_assert_notDeepEqual, 3);
    assert_set_method(instance, "fail",                (void*)js_assert_fail, 1);
    assert_set_method(instance, "throws",              (void*)js_assert_module_throws, 3);
    assert_set_method(instance, "doesNotThrow",        (void*)js_assert_module_doesNotThrow, 3);
    assert_set_method(instance, "ifError",             (void*)js_assert_ifError, 1);
    assert_set_method(instance, "match",               (void*)js_assert_match, 3);
    assert_set_method(instance, "doesNotMatch",        (void*)js_assert_doesNotMatch, 3);
    assert_set_method(instance, "rejects",             (void*)js_assert_rejects, 3);
    assert_set_method(instance, "doesNotReject",       (void*)js_assert_doesNotReject, 3);
    assert_set_method(instance, "partialDeepStrictEqual", (void*)js_assert_partialDeepStrictEqual, 3);
    assert_set_method(instance, "AssertionError",      (void*)js_assert_AssertionError_ctor, 1);

    // store options
    if (get_type_id(options) == LMD_TYPE_MAP) {
        js_property_set(instance, assert_make_string("_options"), options);
    }

    // strict alias
    js_property_set(instance, assert_make_string("strict"), instance);

    return instance;
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
    assert_set_method(assert_namespace, "rejects",             (void*)js_assert_rejects, 3);
    assert_set_method(assert_namespace, "doesNotReject",       (void*)js_assert_doesNotReject, 3);
    assert_set_method(assert_namespace, "partialDeepStrictEqual", (void*)js_assert_partialDeepStrictEqual, 3);

    // AssertionError constructor
    assert_set_method(assert_namespace, "AssertionError",      (void*)js_assert_AssertionError_ctor, 1);

    // Set up AssertionError.prototype to inherit from Error.prototype
    // so that (new assert.AssertionError({})) instanceof Error === true
    {
        Item ae_fn = js_property_get(assert_namespace, assert_make_string("AssertionError"));
        // Get Error constructor and its prototype
        Item error_ctor = js_get_constructor(assert_make_string("Error"));
        Item error_proto_key = assert_make_string("prototype");
        Item error_proto = js_property_get(error_ctor, error_proto_key);
        // Create AssertionError.prototype that inherits from Error.prototype
        Item ae_proto = js_object_create(error_proto);
        js_property_set(ae_proto, assert_make_string("name"), assert_make_string("AssertionError"));
        js_property_set(ae_proto, assert_make_string("constructor"), ae_fn);
        // Set AssertionError.prototype
        js_property_set(ae_fn, error_proto_key, ae_proto);
    }

    // Assert constructor (creates a new Assert instance with the same methods + options)
    assert_set_method(assert_namespace, "Assert", (void*)js_assert_constructor, 1);

    // assert.strict — alias for assert itself (strict mode is default in modern Node)
    js_property_set(assert_namespace, assert_make_string("strict"), assert_namespace);

    // default export
    js_property_set(assert_namespace, assert_make_string("default"), assert_namespace);

    return assert_namespace;
}

extern "C" void js_assert_reset(void) {
    assert_namespace = (Item){0};
}

// =============================================================================
// node:test module — basic test runner with mock support
// =============================================================================

static Item node_test_namespace = {0};

// forward decls used throughout
static Item js_mock_fn_impl(Item original_fn);
static Item js_mock_method_impl(Item object, Item method_name, Item implementation);
static Item js_mock_create_context(void);
static Item js_mock_reset_impl(void);
static Item js_mock_restore_all_impl(void);

// ---------------------------------------------------------------------------
// mock.fn(original?) — creates a mock function that records calls
// Uses a global registry since all mock wrappers share the same C function
// and we can't get the JS function object from within the C wrapper.
// ---------------------------------------------------------------------------
#define MAX_MOCK_SLOTS 64
static struct MockSlot {
    Item calls;       // JS array of call records
    Item original;    // original function (or undefined)
    int call_count;
    bool in_use;
} g_mock_slots[MAX_MOCK_SLOTS];
static int g_mock_slot_count = 0;

// Reset all mock slots (called between tests)
static void mock_reset_all_slots(void) {
    for (int i = 0; i < g_mock_slot_count; i++) {
        g_mock_slots[i].in_use = false;
        g_mock_slots[i].calls = (Item){0};
        g_mock_slots[i].original = (Item){0};
        g_mock_slots[i].call_count = 0;
    }
    g_mock_slot_count = 0;
}

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
    js_property_set(call_record, assert_make_string("arguments"), args_array); \
    js_property_set(call_record, assert_make_string("this"), make_js_undefined()); \
    Item result = make_js_undefined(); \
    if (get_type_id(g_mock_slots[idx].original) == LMD_TYPE_FUNC) { \
        Item call_args[3] = {a0, a1, a2}; \
        result = js_call_function(g_mock_slots[idx].original, make_js_undefined(), call_args, 3); \
    } \
    js_property_set(call_record, assert_make_string("result"), result); \
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
static MockWrapperFn g_mock_wrappers[32] = {
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
        if (get_type_id(original_fn) == LMD_TYPE_FUNC) return original_fn;
        return js_new_function((void*)js_mock_reset_impl, 0);
    }

    g_mock_slots[slot].calls = js_array_new(0);
    g_mock_slots[slot].call_count = 0;
    g_mock_slots[slot].original = original_fn;

    Item wrapper = js_new_function((void*)g_mock_wrappers[slot], 3);

    // create .mock property pointing to the live calls array
    Item mock_prop = js_new_object();
    js_property_set(mock_prop, assert_make_string("calls"), g_mock_slots[slot].calls);
    js_property_set(mock_prop, assert_make_string("callCount"), (Item){.item = i2it(0)});
    js_property_set(mock_prop, assert_make_string("_slot"), (Item){.item = i2it(slot)});

    // mock.restore() — no-op for fn mocks
    js_property_set(mock_prop, assert_make_string("restore"),
                    js_new_function((void*)js_mock_reset_impl, 0));
    // mock.resetCalls()
    js_property_set(mock_prop, assert_make_string("resetCalls"),
                    js_new_function((void*)js_mock_reset_impl, 0));

    js_property_set(wrapper, assert_make_string("mock"), mock_prop);
    return wrapper;
}

// mock.method(object, methodName[, implementation]) — replace a method with a mock
static Item js_mock_method_impl(Item object, Item method_name, Item implementation) {
    // get original method
    Item original = js_property_get(object, method_name);

    // create mock wrapper — if implementation provided, use that as the "original"
    Item mock_original = (get_type_id(implementation) == LMD_TYPE_FUNC)
                         ? implementation : original;
    Item wrapper = js_mock_fn_impl(mock_original);

    // store info for restore
    Item mock_prop = js_property_get(wrapper, assert_make_string("mock"));
    js_property_set(mock_prop, assert_make_string("_owner"), object);
    js_property_set(mock_prop, assert_make_string("_methodName"), method_name);
    js_property_set(mock_prop, assert_make_string("_originalMethod"), original);

    // replace the method
    js_property_set(object, method_name, wrapper);
    return wrapper;
}

static Item js_mock_reset_impl(void) {
    return make_js_undefined();
}

static Item js_mock_restore_all_impl(void) {
    return make_js_undefined();
}

// mock.getter(object, property) — stub
static Item js_mock_getter_impl(Item object, Item property) {
    return js_mock_method_impl(object, property, make_js_undefined());
}

// mock.setter(object, property) — stub
static Item js_mock_setter_impl(Item object, Item property) {
    return js_mock_method_impl(object, property, make_js_undefined());
}

// Create a mock context object with fn/method/getter/setter/reset/restoreAll
static Item js_mock_create_context(void) {
    Item mock_obj = js_new_object();
    js_property_set(mock_obj, assert_make_string("fn"),
                    js_new_function((void*)js_mock_fn_impl, 1));
    js_property_set(mock_obj, assert_make_string("method"),
                    js_new_function((void*)js_mock_method_impl, 3));
    js_property_set(mock_obj, assert_make_string("getter"),
                    js_new_function((void*)js_mock_getter_impl, 2));
    js_property_set(mock_obj, assert_make_string("setter"),
                    js_new_function((void*)js_mock_setter_impl, 2));
    js_property_set(mock_obj, assert_make_string("reset"),
                    js_new_function((void*)js_mock_reset_impl, 0));
    js_property_set(mock_obj, assert_make_string("restoreAll"),
                    js_new_function((void*)js_mock_restore_all_impl, 0));
    // timers sub-object
    Item timers_obj = js_new_object();
    js_property_set(timers_obj, assert_make_string("enable"),
                    js_new_function((void*)js_mock_reset_impl, 0));
    js_property_set(timers_obj, assert_make_string("reset"),
                    js_new_function((void*)js_mock_reset_impl, 0));
    js_property_set(timers_obj, assert_make_string("tick"),
                    js_new_function((void*)js_mock_reset_impl, 1));
    js_property_set(mock_obj, assert_make_string("timers"), timers_obj);
    return mock_obj;
}

// t.skip() — no-op skip
static Item js_test_context_skip(void) {
    return make_js_undefined();
}

// t.todo() — no-op
static Item js_test_context_todo(void) {
    return make_js_undefined();
}

// t.diagnostic(msg) — no-op
static Item js_test_context_diagnostic(Item msg) {
    return make_js_undefined();
}

// t.plan(count) — no-op
static Item js_test_context_plan(Item count) {
    return make_js_undefined();
}

// t.test(name, fn) — sub-test, delegate to js_node_test_run
extern "C" Item js_node_test_run(Item name, Item options_or_fn, Item fn);

static Item js_test_context_subtest(Item name, Item options_or_fn, Item fn) {
    return js_node_test_run(name, options_or_fn, fn);
}

// Build a test context (t) passed to test callbacks
static Item js_build_test_context(void) {
    Item t = js_new_object();

    // t.mock — per-test mock context
    Item mock = js_mock_create_context();
    js_property_set(t, assert_make_string("mock"), mock);

    // t.assert — the assert module + snapshot stubs
    extern Item js_get_assert_namespace(void);
    Item t_assert = js_assert_constructor(make_js_undefined());
    // add snapshot and fileSnapshot as no-op stubs for test runner context
    js_property_set(t_assert, assert_make_string("snapshot"),
                    js_new_function((void*)js_test_context_skip, 0));
    js_property_set(t_assert, assert_make_string("fileSnapshot"),
                    js_new_function((void*)js_test_context_skip, 0));
    js_property_set(t, assert_make_string("assert"), t_assert);

    // t.skip(), t.todo(), t.diagnostic(), t.plan()
    js_property_set(t, assert_make_string("skip"),
                    js_new_function((void*)js_test_context_skip, 0));
    js_property_set(t, assert_make_string("todo"),
                    js_new_function((void*)js_test_context_todo, 0));
    js_property_set(t, assert_make_string("diagnostic"),
                    js_new_function((void*)js_test_context_diagnostic, 1));
    js_property_set(t, assert_make_string("plan"),
                    js_new_function((void*)js_test_context_plan, 1));

    // t.test — sub-tests
    js_property_set(t, assert_make_string("test"),
                    js_new_function((void*)js_test_context_subtest, 3));

    // t.name — filled in later
    js_property_set(t, assert_make_string("name"), make_js_undefined());

    // t.signal — an AbortSignal stub
    js_property_set(t, assert_make_string("signal"), make_js_undefined());

    // t.fullName
    js_property_set(t, assert_make_string("fullName"), assert_make_string(""));

    return t;
}

// test(name, fn) / test(name, options, fn) — run fn synchronously
extern "C" Item js_node_test_run(Item name, Item options_or_fn, Item fn) {
    extern int js_check_exception(void);
    extern Item js_clear_exception(void);
    extern void js_throw_value(Item error);
    extern bool js_is_truthy(Item value);

    // Check for skip/todo option in options object
    Item options = make_js_undefined();
    Item callback = make_js_undefined();
    if (get_type_id(fn) == LMD_TYPE_FUNC) {
        callback = fn;
        options = options_or_fn;
    } else if (get_type_id(options_or_fn) == LMD_TYPE_FUNC) {
        callback = options_or_fn;
    }

    // If options has skip: true or skip is a string, skip the test
    if (get_type_id(options) == LMD_TYPE_MAP) {
        Item skip_val = js_property_get(options, assert_make_string("skip"));
        if (js_is_truthy(skip_val)) {
            return make_js_undefined(); // skip this test
        }
        Item todo_val = js_property_get(options, assert_make_string("todo"));
        if (js_is_truthy(todo_val)) {
            return make_js_undefined(); // todo tests are skipped
        }
    }

    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        return make_js_undefined();
    }

    // create test context with t.mock, t.assert, t.skip, etc.
    Item t = js_build_test_context();

    // set t.name
    if (get_type_id(name) == LMD_TYPE_STRING) {
        js_property_set(t, assert_make_string("name"), name);
        js_property_set(t, assert_make_string("fullName"), name);
    }

    // run callback with (t) or (t, done) — for sync tests we pass t only
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
    js_property_set(node_test_namespace, assert_make_string("suite"), describe_fn);
    js_property_set(node_test_namespace, assert_make_string("it"), test_fn);
    js_property_set(node_test_namespace, assert_make_string("before"), js_new_function((void*)js_node_test_run, 3));
    js_property_set(node_test_namespace, assert_make_string("after"), js_new_function((void*)js_node_test_run, 3));
    js_property_set(node_test_namespace, assert_make_string("beforeEach"), js_new_function((void*)js_node_test_run, 3));
    js_property_set(node_test_namespace, assert_make_string("afterEach"), js_new_function((void*)js_node_test_run, 3));

    // mock — global mock object
    Item mock_obj = js_mock_create_context();
    js_property_set(node_test_namespace, assert_make_string("mock"), mock_obj);

    // MockTracker class — same as mock
    js_property_set(node_test_namespace, assert_make_string("MockTracker"), mock_obj);

    // run — stub (just returns undefined, real runner needs stream support)
    js_property_set(node_test_namespace, assert_make_string("run"),
                    js_new_function((void*)js_mock_reset_impl, 0));

    // getTestContext — stub
    js_property_set(node_test_namespace, assert_make_string("getTestContext"),
                    js_new_function((void*)js_mock_reset_impl, 0));

    return node_test_namespace;
}

extern "C" void js_node_test_reset(void) {
    node_test_namespace = (Item){0};
}
