#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <tree_sitter/api.h>
#include "../lib/strbuf.h"
#include "../lib/hashmap.h"

typedef enum LambdaTypeId {
    LMD_TYPE_NULL,
    LMD_TYPE_INT,
    LMD_TYPE_FLOAT,
    LMD_TYPE_STRING,
    LMD_TYPE_BOOL,
    LMD_TYPE_ARRAY,
    LMD_TYPE_MAP,
    LMD_TYPE_ELEMENT,
    LMD_TYPE_FUNC,
} LambdaTypeId;

typedef struct LambdaType {
    LambdaTypeId type;
    struct LambdaType* nested;  // nested type
    int length;  // length of array
} LambdaType;

typedef struct {
    StrBuf* code_buf;
    const char* source;
    HashMap* node_type_map;

    TSSymbol SYM_NULL;
    TSSymbol SYM_TRUE;
    TSSymbol SYM_FALSE;
    TSSymbol SYM_NUMBER;
    TSSymbol SYM_STRING;
    TSSymbol SYM_IF_EXPR;
    TSSymbol SYM_LET_EXPR;
    TSSymbol SYM_ASSIGNMENT_EXPR;
    TSSymbol SYM_BINARY_EXPR;
    TSSymbol SYM_PRIMARY_EXPR;

    TSFieldId ID_COND;
    TSFieldId ID_THEN;
    TSFieldId ID_ELSE;
    TSFieldId ID_LEFT;
    TSFieldId ID_RIGHT;
    TSFieldId ID_NAME;
    TSFieldId ID_BODY;

    enum TP_PHASE {
        DECLARE,  // var declaration phase
        EVALUATE,  // expr evaluation phase
    } phase;
} Transpiler;

LambdaType infer_expr(Transpiler* tp, TSNode expr_node);