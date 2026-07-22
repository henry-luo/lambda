#ifndef TEST_BASELINE_MODE_HPP
#define TEST_BASELINE_MODE_HPP

#include <string.h>

// Consume the runner-specific mode before GoogleTest receives argv.
static inline bool test_parse_baseline_mode(int* argc, char** argv) {
    bool baseline_mode = false;
    int write_index = 1;
    for (int read_index = 1; read_index < *argc; read_index++) {
        if (strcmp(argv[read_index], "--baseline") == 0) {
            baseline_mode = true;
            continue;
        }
        argv[write_index++] = argv[read_index];
    }
    *argc = write_index;
    argv[write_index] = nullptr;
    return baseline_mode;
}

#endif
