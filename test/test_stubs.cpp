/**
 * @file test_stubs.cpp
 * @brief Stub implementations for test builds to avoid complex dependencies
 */


#include "../lambda/transpiler.hpp"


// Stub implementation of load_script for test builds
Script* load_script(Runtime* runtime, const char* script_path, const char* source) {
    (void)runtime;
    (void)script_path;
    (void)source;
    return nullptr;
}

