#pragma once

#include <cstdlib>
#include <cstring>
#include "lib/log.h"

struct JsTestExecutionModes {
    using value_type = const char*;
    const char* const* values;
    size_t count;
    const char* const* begin() const { return values; }
    const char* const* end() const { return values + count; }
};

// the baseline selects AUTO; the parity runner selects each tier in its own process.
static inline JsTestExecutionModes js_test_execution_modes() {
    static const char* const modes[] = {"ast", "mir", "auto"};
    const char* selected = getenv("LAMBDA_JS_TEST_MODE");
    if (!selected || !selected[0] || strcmp(selected, "all") == 0) return {modes, 3};
    for (size_t i = 0; i < 3; i++) {
        if (strcmp(selected, modes[i]) == 0) return {modes + i, 1};
    }
    log_error("js-test-mode: invalid LAMBDA_JS_TEST_MODE '%s'", selected);
    abort();
}
