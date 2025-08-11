#include <tree_sitter/api.h>
#include "ts-enum.h"

#define SYM_NULL sym_null
#define SYM_TRUE sym_true
#define SYM_FALSE sym_false
#define SYM_INT sym_integer
#define SYM_FLOAT sym_float
#define SYM_DECIMAL sym_decimal
#define SYM_STRING sym_string
#define SYM_SYMBOL sym_symbol
#define SYM_DATETIME sym_datetime
#define SYM_TIME sym_time
#define SYM_BINARY sym_binary

#define SYM_CONTENT sym_content
#define SYM_LIST sym_list
#define SYM_ARRAY sym_array
#define SYM_MAP_ITEM sym_map_item
#define SYM_MAP sym_map
#define SYM_ELEMENT sym_element
#define SYM_ATTR sym_attr

#define SYM_IDENT sym_identifier
#define SYM_INDEX sym_index
#define SYM_MEMBER_EXPR sym_member_expr
#define SYM_INDEX_EXPR sym_index_expr
#define SYM_CALL_EXPR sym_call_expr
#define SYM_PRIMARY_EXPR sym_primary_expr
#define SYM_UNARY_EXPR sym_unary_expr
#define SYM_BINARY_EXPR sym_binary_expr

#define SYM_ASSIGN_EXPR sym_assign_expr
#define SYM_IF_EXPR sym_if_expr
#define SYM_IF_STAM sym_if_stam
#define SYM_LET_EXPR sym_let_expr
#define SYM_LET_STAM sym_let_stam
#define SYM_PUB_STAM sym_pub_stam
#define SYM_FOR_EXPR sym_for_expr
#define SYM_FOR_STAM sym_for_stam

#define SYM_BASE_TYPE sym_base_type
#define SYM_ARRAY_TYPE sym_array_type
#define SYM_LIST_TYPE sym_list_type
#define SYM_MAP_TYPE_ITEM sym_map_type_item
#define SYM_MAP_TYPE sym_map_type
#define SYM_CONTENT_TYPE sym_content_type
#define SYM_ELEMENT_TYPE sym_element_type
#define SYM_FN_TYPE sym_fn_type
#define SYM_PRIMARY_TYPE sym_primary_type
#define SYM_BINARY_TYPE sym_binary_type
#define SYM_TYPE_DEFINE sym_type_stam

#define SYM_FUNC_STAM sym_fn_stam
#define SYM_FUNC_EXPR_STAM sym_fn_expr_stam
#define SYM_FUNC_EXPR sym_fn_expr
#define SYM_SYS_FUNC sym_sys_func
#define SYM_IMPORT_MODULE sym_import_module

#define SYM_COMMENT sym_comment

#define FIELD_COND field_cond
#define FIELD_THEN field_then
#define FIELD_ELSE field_else
#define FIELD_LEFT field_left
#define FIELD_RIGHT field_right
#define FIELD_NAME field_name
#define FIELD_AS field_as
#define FIELD_TYPE field_type
#define FIELD_OBJECT field_object
#define FIELD_FIELD field_field
#define FIELD_BODY field_body
#define FIELD_DECLARE field_declare
#define FIELD_FUNCTION field_function
#define FIELD_ARGUMENT field_argument
#define FIELD_OPERATOR field_operator
#define FIELD_OPERAND field_operand
#define FIELD_ALIAS field_alias
#define FIELD_MODULE field_module
#define FIELD_PUB field_pub

