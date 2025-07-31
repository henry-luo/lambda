#ifndef FORMAT_H
#define FORMAT_H

#include "../lambda-data.h"

// Common macros for extracting values from Lambda Items
#define get_pointer(item) ((void*)((item) & 0x00FFFFFFFFFFFFFF))
#define get_bool_value(item) ((bool)((item) & 0xFF))
#define get_int_value(item) ((int64_t)(((int64_t)((item) & 0x00FFFFFFFFFFFFFF)) << 8) >> 8)

// Common function declarations
Item create_item_from_field_data(void* field_data, TypeId type_id);
void format_number(StrBuf* sb, Item item);

// Format function declarations
String* format_json(VariableMemPool* pool, Item root_item);
void format_json_to_strbuf(StrBuf* sb, Item root_item);
void format_markdown(StrBuf* sb, Item root_item);
String* format_xml(VariableMemPool* pool, Item root_item);
String* format_html(VariableMemPool* pool, Item root_item);
String* format_yaml(VariableMemPool* pool, Item root_item);
String* format_toml(VariableMemPool* pool, Item root_item);
String* format_ini(VariableMemPool* pool, Item root_item);
void format_rst(StrBuf* sb, Item root_item);
String* format_rst_string(VariableMemPool* pool, Item root_item);

// Math format function declarations
String* format_math(VariableMemPool* pool, Item root_item);
String* format_math_latex(VariableMemPool* pool, Item root_item);
String* format_math_typst(VariableMemPool* pool, Item root_item);
String* format_math_ascii(VariableMemPool* pool, Item root_item);
String* format_math_mathml(VariableMemPool* pool, Item root_item);
String* format_math_unicode(VariableMemPool* pool, Item root_item);

#endif // FORMAT_H
