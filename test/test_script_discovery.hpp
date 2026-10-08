#ifndef TEST_SCRIPT_DISCOVERY_HPP
#define TEST_SCRIPT_DISCOVERY_HPP

#include <cstring>

// Golden-driven runners (test_lambda_gtest, test_lambda_extended_gtest,
// test_lambda_std_gtest) treat every .ls file in their directories as a test.
// A script without a golden counts as a helper, module or playground only when
// its name says so: `_*`, `mod_*` or `schema_*` — the convention of the
// `ls-test-has-golden` rule in .alint.yml. Any other script without its golden
// fails (AGENTS.md rule 8) instead of being skipped silently.
inline bool is_lambda_helper_script(const char* filename) {
    return filename[0] == '_' || strncmp(filename, "mod_", 4) == 0 ||
           strncmp(filename, "schema_", 7) == 0;
}

// failure text the runners append for a discovered script with no golden
#define LAMBDA_MISSING_GOLDEN_HINT \
    "every test script needs its expected output (AGENTS.md rule 8); " \
    "give helper, module or playground scripts a _ or mod_ prefix"

#endif
