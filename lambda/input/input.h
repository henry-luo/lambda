#include "../transpiler.h"

// Common input utility functions
void input_skip_whitespace(const char **text);
bool input_is_whitespace_char(char c);
bool input_is_empty_line(const char* line);
int input_count_leading_chars(const char* str, char ch);
char* input_trim_whitespace(const char* str);
String* input_create_string(Input *input, const char* text);
char** input_split_lines(const char* text, int* line_count);
void input_free_lines(char** lines, int line_count);
Element* input_create_element(Input *input, const char* tag_name);
void input_add_attribute_to_element(Input *input, Element* element, const char* attr_name, const char* attr_value);
