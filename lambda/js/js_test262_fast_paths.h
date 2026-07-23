#pragma once

// The build generator sources this switch from build_lambda_config.json and
// permits an environment override for production verification builds.
#ifndef JS_TEST262_FAST_PATHS
#define JS_TEST262_FAST_PATHS 1
#endif

// One catalog owns the helper surface shared by definitions and JIT imports.
#define JS_TEST262_FAST_PATH_CATALOG(X) \
    X(js_assert_same_value) \
    X(js_assert_not_same_value) \
    X(js_assert_compare_array) \
    X(js_assert_deep_equal) \
    X(js_compare_array) \
    X(js_verify_property) \
    X(js_assert_throws) \
    X(js_assert_base) \
    X(js_donotevaluate) \
    X(js_is_constructor) \
    X(js_decimal_to_percent_hex_string) \
    X(js_test262_build_string) \
    X(js_test262_decimal_to_percent_hex_string) \
    X(js_test262_concat_percent_hex) \
    X(js_validate_native_function_source)
