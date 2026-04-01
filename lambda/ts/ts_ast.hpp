#pragma once

// ts_ast.hpp — TypeScript AST node types extending js_ast.hpp
//
// Type annotations are fully built into the AST (not erased), so the
// transpiler can resolve them to Lambda Type* values for runtime use.

#include "../js/js_ast.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// TypeScript AST Node Types (start at 1000 to avoid collisions with JS nodes)
// ============================================================================

typedef enum TsAstNodeType {
    // --- type expression nodes (resolved to Lambda Type* during transpilation) ---
    TS_AST_NODE_TYPE_ANNOTATION = 1000,  // : SomeType
    TS_AST_NODE_TYPE_ALIAS,              // type Foo = ...
    TS_AST_NODE_INTERFACE,               // interface Foo { ... }
    TS_AST_NODE_TYPE_PARAMETERS,         // <T extends U = V>
    TS_AST_NODE_TYPE_PARAMETER,          // single T extends U = V
    TS_AST_NODE_PREDEFINED_TYPE,         // string, number, any, ...
    TS_AST_NODE_TYPE_REFERENCE,          // Foo, Array<T>, MyInterface
    TS_AST_NODE_UNION_TYPE,              // A | B
    TS_AST_NODE_INTERSECTION_TYPE,       // A & B
    TS_AST_NODE_TUPLE_TYPE,              // [A, B, C]
    TS_AST_NODE_ARRAY_TYPE,              // T[]
    TS_AST_NODE_FUNCTION_TYPE,           // (a: T) => R
    TS_AST_NODE_OBJECT_TYPE,             // { a: T; b: U }
    TS_AST_NODE_CONDITIONAL_TYPE,        // T extends U ? X : Y
    TS_AST_NODE_LITERAL_TYPE,            // literal: "hello", 42, true
    TS_AST_NODE_PARENTHESIZED_TYPE,      // (T)
    TS_AST_NODE_TYPEOF_TYPE,             // typeof x
    TS_AST_NODE_KEYOF_TYPE,              // keyof T
    TS_AST_NODE_INDEXED_ACCESS_TYPE,     // T[K]
    TS_AST_NODE_MAPPED_TYPE,             // { [K in T]: V }
    TS_AST_NODE_TEMPLATE_LITERAL_TYPE,
    TS_AST_NODE_INFER_TYPE,              // infer T

    // --- expression wrappers with type info ---
    TS_AST_NODE_AS_EXPRESSION,           // expr as T
    TS_AST_NODE_NON_NULL_EXPRESSION,     // expr!
    TS_AST_NODE_SATISFIES_EXPRESSION,    // expr satisfies T
    TS_AST_NODE_TYPE_ASSERTION,          // <T>expr (angle-bracket style)

    // --- runtime-meaningful TS constructs ---
    TS_AST_NODE_ENUM_DECLARATION,        // enum Direction { Up = 0, Down }
    TS_AST_NODE_ENUM_MEMBER,             // Up = 0
    TS_AST_NODE_NAMESPACE_DECLARATION,   // namespace Foo { ... }
    TS_AST_NODE_DECORATOR,               // @decorator
    TS_AST_NODE_AMBIENT_DECLARATION,     // declare ... (type info kept, no code emitted)
    TS_AST_NODE_PARAMETER,               // required_parameter / optional_parameter with type annotation

    TS_AST_NODE__MAX,
} TsAstNodeType;

// ============================================================================
// Type expression AST nodes — carry the resolved Lambda Type*
// ============================================================================

// base for all TS type nodes
typedef struct TsTypeNode {
    JsAstNode base;
    Type* resolved_type;    // filled during type resolution pass; NULL before
} TsTypeNode;

// : SomeType  (attached to variable_declarator, parameter, function return, etc.)
typedef struct TsTypeAnnotationNode {
    TsTypeNode base;
    TsTypeNode* type_expr;  // the type expression subtree
} TsTypeAnnotationNode;

// string, number, boolean, any, void, never, unknown, object, symbol, bigint, null, undefined
typedef struct TsPredefinedTypeNode {
    TsTypeNode base;
    TypeId predefined_id;   // resolved directly: number->FLOAT, string->STRING, etc.
} TsPredefinedTypeNode;

// Foo, Array<T>, MyInterface
typedef struct TsTypeReferenceNode {
    TsTypeNode base;
    String* name;           // type name
    TsTypeNode** type_args; // generic arguments (nullable)
    int type_arg_count;
} TsTypeReferenceNode;

// A | B
typedef struct TsUnionTypeNode {
    TsTypeNode base;
    TsTypeNode** types;     // all union members
    int type_count;
} TsUnionTypeNode;

// A & B
typedef struct TsIntersectionTypeNode {
    TsTypeNode base;
    TsTypeNode** types;
    int type_count;
} TsIntersectionTypeNode;

// T[]
typedef struct TsArrayTypeNode {
    TsTypeNode base;
    TsTypeNode* element_type;
} TsArrayTypeNode;

// [A, B, C]
typedef struct TsTupleTypeNode {
    TsTypeNode base;
    TsTypeNode** element_types;
    int element_count;
} TsTupleTypeNode;

// (x: T, y: U) => R
typedef struct TsFunctionTypeNode {
    TsTypeNode base;
    TsTypeNode** param_types;
    String** param_names;
    int param_count;
    TsTypeNode* return_type;
} TsFunctionTypeNode;

// { x: T; y: U; z?: V }
typedef struct TsObjectTypeNode {
    TsTypeNode base;
    TsTypeNode** member_types;
    String** member_names;
    bool* member_optional;  // per-member optional flag
    bool* member_readonly;  // per-member readonly flag
    int member_count;
} TsObjectTypeNode;

// T extends U ? X : Y
typedef struct TsConditionalTypeNode {
    TsTypeNode base;
    TsTypeNode* check_type;
    TsTypeNode* extends_type;
    TsTypeNode* true_type;
    TsTypeNode* false_type;
} TsConditionalTypeNode;

// literal type: "hello", 42, true
typedef struct TsLiteralTypeNode {
    TsTypeNode base;
    JsLiteralType literal_type;
    union {
        double number_value;
        String* string_value;
        bool boolean_value;
    } value;
} TsLiteralTypeNode;

// (T) — parenthesized
typedef struct TsParenthesizedTypeNode {
    TsTypeNode base;
    TsTypeNode* inner;
} TsParenthesizedTypeNode;

// --- type parameter ---
typedef struct TsTypeParamNode {
    TsTypeNode base;
    String* name;
    TsTypeNode* constraint;    // extends X (nullable)
    TsTypeNode* default_type;  // = Y (nullable)
} TsTypeParamNode;

// --- interface Foo { x: T; ... } ---
typedef struct TsInterfaceNode {
    JsAstNode base;
    String* name;
    TsTypeParamNode** type_params;
    int type_param_count;
    TsTypeNode** extends_types;    // extends clause (nullable)
    int extends_count;
    TsObjectTypeNode* body;
    Type* resolved_type;           // -> TypeMap with ShapeEntry chain
} TsInterfaceNode;

// --- type Foo = ... ---
typedef struct TsTypeAliasNode {
    JsAstNode base;
    String* name;
    TsTypeParamNode** type_params;
    int type_param_count;
    TsTypeNode* type_expr;
    Type* resolved_type;
} TsTypeAliasNode;

// --- enum Direction { Up = 0, Down } ---
typedef struct TsEnumDeclarationNode {
    JsAstNode base;
    bool is_const;
    String* name;
    JsAstNode** members;
    int member_count;
    Type* resolved_type;   // -> TypeMap representing the enum shape
} TsEnumDeclarationNode;

typedef struct TsEnumMemberNode {
    JsAstNode base;
    String* name;
    JsAstNode* initializer;
    int auto_value;
} TsEnumMemberNode;

// --- namespace Foo { ... } ---
typedef struct TsNamespaceDeclarationNode {
    JsAstNode base;
    String* name;
    JsAstNode** body;
    int body_count;
} TsNamespaceDeclarationNode;

// --- @decorator ---
typedef struct TsDecoratorNode {
    JsAstNode base;
    JsAstNode* expression;
} TsDecoratorNode;

// --- expr as T / expr satisfies T ---
typedef struct TsTypeExprNode {
    JsAstNode base;
    JsAstNode* inner;        // the expression
    TsTypeNode* target_type; // the asserted/checked type
} TsTypeExprNode;

// --- expr! (non-null assertion) ---
typedef struct TsNonNullNode {
    JsAstNode base;
    JsAstNode* inner;
} TsNonNullNode;

// Extended JS nodes with optional TS type annotations
// TsVariableDeclaratorNode: the ts_type field is now in JsVariableDeclaratorNode base
typedef JsVariableDeclaratorNode TsVariableDeclaratorNode;

typedef struct TsParameterNode {
    JsAstNode base;
    JsAstNode* pattern;            // parameter name/pattern
    TsTypeAnnotationNode* ts_type; // type annotation (nullable)
    JsAstNode* default_value;      // default value (nullable)
    int accessibility;             // 0=none, 1=public, 2=private, 3=protected
    bool readonly;
    bool optional;                 // true for optional_parameter (y?: T)
} TsParameterNode;

typedef struct TsFunctionNode {
    JsFunctionNode base;
    TsTypeAnnotationNode* return_type;    // return type annotation (nullable)
    TsTypeParamNode** type_params;        // generic type parameters (nullable)
    int type_param_count;
} TsFunctionNode;

#ifdef __cplusplus
}
#endif
