#pragma once
#include "../../lambda-data.hpp"

struct MvpLmdProgram;
enum MvpLmdFailure { LMD_MVP_CAPABILITY, LMD_MVP_REFERENCE, LMD_MVP_TYPE,
    LMD_MVP_RANGE, LMD_MVP_MEMORY };

extern "C" {
Item mvp_lmd_fail(int64_t kind, int64_t site);
double mvp_lmd_string_to_number(String* string);
Item mvp_lmd_number_to_string(double value);
Item mvp_lmd_string_concat(Item left, Item right);
int64_t mvp_lmd_string_compare(Item left, Item right);
Item mvp_lmd_string_at(Item string, uint32_t index);
double mvp_lmd_number_pow(double base, double exponent);
int64_t mvp_lmd_string_key(String* string);
Item mvp_lmd_array_store(Item array, uint32_t index, Item value);
Item mvp_lmd_function_new(uint64_t code_id, MvpLmdProgram* program);
}
