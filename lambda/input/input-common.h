#ifndef INPUT_COMMON_H
#define INPUT_COMMON_H

#include <stdbool.h>

// Common utility functions and definitions for input parsers

// Common LaTeX math commands and symbols - shared between input-math.c and input-latex.c
extern const char* greek_letters[];
extern const char* math_operators[];
extern const char* trig_functions[];
extern const char* log_functions[];
extern const char* latex_commands[];
extern const char* latex_environments[];
extern const char* math_environments[];
extern const char* raw_text_environments[];

// Common utility functions
bool is_greek_letter(const char* cmd_name);
bool is_math_operator(const char* cmd_name);
bool is_trig_function(const char* cmd_name);
bool is_log_function(const char* cmd_name);
bool is_latex_command(const char* cmd_name);
bool is_latex_environment(const char* env_name);
bool is_math_environment(const char* env_name);
bool is_raw_text_environment(const char* env_name);

// Common parsing utilities
void skip_common_whitespace(const char **text);
void skip_latex_comment(const char **latex);

// Common element creation functions
#define create_common_element input_create_element
#define add_attribute_to_common_element input_add_attribute_to_element

#endif // INPUT_COMMON_H
