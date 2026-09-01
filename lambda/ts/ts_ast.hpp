#pragma once

// ts_ast.hpp — TypeScript nodes that survive direct C-parser lowering.

#include "../js/js_ast.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TsAstNodeType {
    // parser-stack facts that carry Lambda's canonical type representation.
    TS_AST_NODE_TYPE_FACT = 1500,
    TS_AST_NODE_PARAMETER,

    TS_AST_NODE__MAX,
} TsAstNodeType;

// A C-parser stack fact carries Lambda's canonical type representation.
typedef struct TsTypeFactNode : JsAstNode {
    Type* resolved_type;
} TsTypeFactNode;

typedef struct TsParameterNode : JsAstNode {
    JsAstNode* pattern;
    Type* declared_type;
    JsAstNode* default_value;
    int accessibility;
    bool readonly;
    bool optional;
} TsParameterNode;

#ifdef __cplusplus
}
#endif
