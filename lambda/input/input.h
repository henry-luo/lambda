#include "../lambda-data.hpp"
#include <lexbor/url/url.h>

// Input creation and management
Input* input_new(lxb_url_t* abs_url);

String* create_input_string(Input* input, const char* text, int start, int len);
String* input_create_string(Input *input, const char* text);

// Common input utility functions
void map_put(Map* mp, String* key, Item value, Input *input);
void input_skip_whitespace(const char **text);
bool input_is_whitespace_char(char c);
bool input_is_empty_line(const char* line);
int input_count_leading_chars(const char* str, char ch);
char* input_trim_whitespace(const char* str);
char** input_split_lines(const char* text, int* line_count);
void input_free_lines(char** lines, int line_count);
Element* input_create_element(Input *input, const char* tag_name);
void input_add_attribute_to_element(Input *input, Element* element, const char* attr_name, const char* attr_value);
void input_add_attribute_item_to_element(Input *input, Element* element, const char* attr_name, Item attr_value);

// Math parsing functions (from input-math.cpp)
void parse_math(Input* input, const char* math_string, const char* flavor_str);

// YAML parsing utility functions (from input-yaml.c)
void trim_string_inplace(char* str);
Item parse_scalar_value(Input *input, const char* str);
Array* parse_flow_array(Input *input, const char* str);

// Unified markup parsing functions (from input-markup.cpp)
Item input_markup(Input *input, const char* content);
