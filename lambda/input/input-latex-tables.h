#ifndef LAMBDA_INPUT_LATEX_TABLES_H
#define LAMBDA_INPUT_LATEX_TABLES_H

// LaTeX command and environment classification tables.
// Implementations in input-latex-tables.cpp (binary search over sorted arrays).

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool is_greek_letter(const char* cmd_name);
bool is_math_operator(const char* cmd_name);
bool is_trig_function(const char* cmd_name);
bool is_log_function(const char* cmd_name);
bool is_latex_command(const char* cmd_name);
bool is_latex_environment(const char* env_name);
bool is_math_environment(const char* env_name);
bool is_raw_text_environment(const char* env_name);

void skip_latex_comment(const char** latex);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_INPUT_LATEX_TABLES_H
