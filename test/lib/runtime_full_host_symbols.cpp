#include "../../lambda/lambda-data.hpp"
#include "../../lambda/runtime/transpiler.hpp"

// Retained relocations force archive extraction for the validator DSO's host ABI.
extern "C" {
__attribute__((used)) const Item* const lambda_runtime_full_test_item_null = &ItemNull;
__attribute__((used)) const char* const* const lambda_runtime_full_test_home_path = &g_lambda_home;
}
