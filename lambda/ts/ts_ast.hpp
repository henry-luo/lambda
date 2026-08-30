#pragma once

// ts_ast.hpp — TypeScript nodes that survive direct C-parser lowering.

#include "../js/js_ast.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TsAstNodeType {
    // parser-stack facts; they are consumed before the executable AST exists.
    TS_AST_NODE_TYPE_FACT = 1500,
    TS_AST_NODE_TYPE_PARAMETERS,
    TS_AST_NODE_TYPE_PARAMETER,

    // retained source nodes and direct-lowering intermediates.
    TS_AST_NODE_TYPE_ALIAS,
    TS_AST_NODE_INTERFACE,
    TS_AST_NODE_ENUM_DECLARATION,
    TS_AST_NODE_ENUM_MEMBER,
    TS_AST_NODE_NAMESPACE_DECLARATION,
    TS_AST_NODE_DECORATOR,
    TS_AST_NODE_PARAMETER,

    TS_AST_NODE__MAX,
} TsAstNodeType;

// A C-parser stack fact carries Lambda's canonical type representation.
typedef struct TsTypeFactNode : JsAstNode {
    Type* resolved_type;
} TsTypeFactNode;

typedef struct TsInterfaceNode : JsAstNode {
    String* name;
    Type* resolved_type;
} TsInterfaceNode;

typedef struct TsTypeAliasNode : JsAstNode {
    String* name;
    Type* resolved_type;
} TsTypeAliasNode;

typedef struct TsEnumDeclarationNode : JsAstNode {
    bool is_const;
    String* name;
    JsAstNode** members;
    int member_count;
    Type* resolved_type;
} TsEnumDeclarationNode;

typedef struct TsEnumMemberNode : JsAstNode {
    String* name;
    JsAstNode* initializer;
    int auto_value;
} TsEnumMemberNode;

typedef struct TsNamespaceDeclarationNode : JsAstNode {
    String* name;
    JsAstNode** body;
    int body_count;
} TsNamespaceDeclarationNode;

typedef struct TsDecoratorNode : JsAstNode {
    JsAstNode* expression;
} TsDecoratorNode;

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
