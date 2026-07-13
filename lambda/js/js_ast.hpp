#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tree_sitter/api.h>
#include "../ast.hpp"
#include "../lambda-data.hpp"

// forward declaration so JsFunctionNode and JsVariableDeclaratorNode can reference it
struct TsTypeAnnotationNode;

// JavaScript-specific Tree-sitter symbols
#define JS_SYM_PROGRAM sym_program
#define JS_SYM_FUNCTION_DECLARATION sym_function_declaration
#define JS_SYM_VARIABLE_DECLARATION sym_variable_declaration
#define JS_SYM_LEXICAL_DECLARATION sym_lexical_declaration
#define JS_SYM_EXPRESSION_STATEMENT sym_expression_statement
#define JS_SYM_BLOCK_STATEMENT sym_statement_block
#define JS_SYM_IF_STATEMENT sym_if_statement
#define JS_SYM_WHILE_STATEMENT sym_while_statement
#define JS_SYM_FOR_STATEMENT sym_for_statement
#define JS_SYM_RETURN_STATEMENT sym_return_statement
#define JS_SYM_BREAK_STATEMENT sym_break_statement
#define JS_SYM_CONTINUE_STATEMENT sym_continue_statement

// Expression symbols
#define JS_SYM_IDENTIFIER sym_identifier
#define JS_SYM_NUMBER sym_number
#define JS_SYM_STRING sym_string
#define JS_SYM_TRUE sym_true
#define JS_SYM_FALSE sym_false
#define JS_SYM_NULL sym_null
#define JS_SYM_UNDEFINED sym_undefined
#define JS_SYM_BINARY_EXPRESSION sym_binary_expression
#define JS_SYM_UNARY_EXPRESSION sym_unary_expression
#define JS_SYM_ASSIGNMENT_EXPRESSION sym_assignment_expression
#define JS_SYM_CALL_EXPRESSION sym_call_expression
#define JS_SYM_MEMBER_EXPRESSION sym_member_expression
#define JS_SYM_SUBSCRIPT_EXPRESSION sym_subscript_expression
#define JS_SYM_ARRAY_EXPRESSION sym_array
#define JS_SYM_OBJECT_EXPRESSION sym_object
#define JS_SYM_FUNCTION_EXPRESSION sym_function_expression
#define JS_SYM_ARROW_FUNCTION sym_arrow_function
#define JS_SYM_CONDITIONAL_EXPRESSION sym_ternary_expression

// Field names
#define JS_FIELD_NAME field_name
#define JS_FIELD_VALUE field_value
#define JS_FIELD_LEFT field_left
#define JS_FIELD_RIGHT field_right
#define JS_FIELD_OPERATOR field_operator
#define JS_FIELD_OPERAND field_operand
#define JS_FIELD_FUNCTION field_function
#define JS_FIELD_ARGUMENTS field_arguments
#define JS_FIELD_OBJECT field_object
#define JS_FIELD_PROPERTY field_property
#define JS_FIELD_BODY field_body
#define JS_FIELD_PARAMETERS field_parameters
#define JS_FIELD_CONDITION field_condition
#define JS_FIELD_CONSEQUENCE field_consequence
#define JS_FIELD_ALTERNATIVE field_alternative

#ifdef __cplusplus
}
#endif

// JavaScript AST node types share AstNodeType's underlying space. Core-shaped
// nodes use core values now; JS-only or not-yet-merged variants stay in 1000–1499.
typedef AstNodeType JsAstNodeType;
static const JsAstNodeType JS_AST_NODE_NULL = AST_NODE_NULL;
static const JsAstNodeType JS_AST_NODE_PROGRAM = AST_SCRIPT;
static const JsAstNodeType JS_AST_NODE_FUNCTION_DECLARATION = AST_NODE_FUNC;
static const JsAstNodeType JS_AST_NODE_VARIABLE_DECLARATION = AST_NODE_VAR_STAM;
static const JsAstNodeType JS_AST_NODE_EXPRESSION_STATEMENT = AST_NODE_EXPR_STMT;
static const JsAstNodeType JS_AST_NODE_BLOCK_STATEMENT = AST_NODE_BLOCK;
static const JsAstNodeType JS_AST_NODE_IF_STATEMENT = AST_NODE_IF_EXPR;
static const JsAstNodeType JS_AST_NODE_WHILE_STATEMENT = AST_NODE_WHILE_STAM;
static const JsAstNodeType JS_AST_NODE_FOR_STATEMENT = AST_NODE_FOR_STAM;
static const JsAstNodeType JS_AST_NODE_RETURN_STATEMENT = AST_NODE_RETURN_STAM;
static const JsAstNodeType JS_AST_NODE_BREAK_STATEMENT = AST_NODE_BREAK_STAM;
static const JsAstNodeType JS_AST_NODE_CONTINUE_STATEMENT = AST_NODE_CONTINUE_STAM;
static const JsAstNodeType JS_AST_NODE_IDENTIFIER = AST_NODE_IDENT;
static const JsAstNodeType JS_AST_NODE_LITERAL = AST_NODE_LITERAL;
static const JsAstNodeType JS_AST_NODE_BINARY_EXPRESSION = AST_NODE_BINARY;
static const JsAstNodeType JS_AST_NODE_UNARY_EXPRESSION = AST_NODE_UNARY;
static const JsAstNodeType JS_AST_NODE_ASSIGNMENT_EXPRESSION = AST_NODE_ASSIGN;
static const JsAstNodeType JS_AST_NODE_CALL_EXPRESSION = AST_NODE_CALL_EXPR;
static const JsAstNodeType JS_AST_NODE_MEMBER_EXPRESSION = AST_NODE_MEMBER_EXPR;
static const JsAstNodeType JS_AST_NODE_ARRAY_EXPRESSION = AST_NODE_ARRAY;
static const JsAstNodeType JS_AST_NODE_OBJECT_EXPRESSION = AST_NODE_MAP;
static const JsAstNodeType JS_AST_NODE_FUNCTION_EXPRESSION = AST_NODE_FUNC_EXPR;
static const JsAstNodeType JS_AST_NODE_SPREAD_ELEMENT = AST_NODE_SPREAD;
static const JsAstNodeType JS_AST_NODE_CLASS_DECLARATION = AST_NODE_CLASS;
static const JsAstNodeType JS_AST_NODE_FIELD_DEFINITION = AST_NODE_FIELD;
static const JsAstNodeType JS_AST_NODE_THROW_STATEMENT = AST_NODE_RAISE_STAM;
static const JsAstNodeType JS_AST_NODE_PARAMETER = AST_NODE_PARAM;
static const JsAstNodeType JS_AST_NODE_NEW_EXPRESSION = AST_NODE_NEW_EXPR;
static const JsAstNodeType JS_AST_NODE_SEQUENCE_EXPRESSION = AST_NODE_SEQ;
static const JsAstNodeType JS_AST_NODE_YIELD_EXPRESSION = AST_NODE_YIELD;
static const JsAstNodeType JS_AST_NODE_AWAIT_EXPRESSION = AST_NODE_AWAIT;
static const JsAstNodeType JS_AST_NODE_IMPORT_DECLARATION = AST_NODE_IMPORT;
static const JsAstNodeType JS_AST_NODE_EXPORT_DECLARATION = AST_NODE_EXPORT;
static const JsAstNodeType JS_AST_NODE_ARROW_FUNCTION = AST_NODE_ARROW_FUNC;
static const JsAstNodeType JS_AST_NODE_CONDITIONAL_EXPRESSION = AST_NODE_CONDITIONAL_EXPR;
static const JsAstNodeType JS_AST_NODE_CLASS_EXPRESSION = AST_NODE_CLASS_EXPR;
static const JsAstNodeType JS_AST_NODE_METHOD_DEFINITION = AST_NODE_METHOD;
static const JsAstNodeType JS_AST_NODE_TRY_STATEMENT = AST_NODE_TRY_STAM;
static const JsAstNodeType JS_AST_NODE_CATCH_CLAUSE = AST_NODE_CATCH_CLAUSE;
static const JsAstNodeType JS_AST_NODE_ASSIGNMENT_PATTERN = AST_NODE_ASSIGN_PATTERN;
static const JsAstNodeType JS_AST_NODE_ARRAY_PATTERN = AST_NODE_ARRAY_PATTERN;
static const JsAstNodeType JS_AST_NODE_OBJECT_PATTERN = AST_NODE_MAP_PATTERN;
static const JsAstNodeType JS_AST_NODE_VARIABLE_DECLARATOR = AST_NODE_VARIABLE_DECLARATOR;
static const JsAstNodeType JS_AST_NODE_PROPERTY = AST_NODE_PROPERTY;
static const JsAstNodeType JS_AST_NODE_REST_ELEMENT = AST_NODE_REST_ELEMENT;
static const JsAstNodeType JS_AST_NODE_REST_PROPERTY = AST_NODE_REST_PROPERTY;
static const JsAstNodeType JS_AST_NODE_SWITCH_STATEMENT = AST_NODE_MATCH_EXPR;
static const JsAstNodeType JS_AST_NODE_SWITCH_CASE = AST_NODE_MATCH_ARM;
static const JsAstNodeType JS_AST_NODE_DO_WHILE_STATEMENT = AST_NODE_DO_WHILE_STAM;
static const JsAstNodeType JS_AST_NODE_FOR_OF_STATEMENT = AST_NODE_FOR_OF_STAM;
static const JsAstNodeType JS_AST_NODE_FOR_IN_STATEMENT = AST_NODE_FOR_IN_STAM;
static const JsAstNodeType JS_AST_NODE_IMPORT_SPECIFIER = AST_NODE_IMPORT_SPECIFIER;
static const JsAstNodeType JS_AST_NODE_EXPORT_SPECIFIER = AST_NODE_EXPORT_SPECIFIER;

static const JsAstNodeType JS_AST_NODE_TEMPLATE_LITERAL = (JsAstNodeType)1002;
static const JsAstNodeType JS_AST_NODE_TEMPLATE_ELEMENT = (JsAstNodeType)1003;
static const JsAstNodeType JS_AST_NODE_STATIC_BLOCK = (JsAstNodeType)1006;
static const JsAstNodeType JS_AST_NODE_FINALLY_CLAUSE = (JsAstNodeType)1009;
static const JsAstNodeType JS_AST_NODE_LABELED_STATEMENT = (JsAstNodeType)1022;
static const JsAstNodeType JS_AST_NODE_REGEX = (JsAstNodeType)1023;
static const JsAstNodeType JS_AST_NODE_WITH_STATEMENT = (JsAstNodeType)1026;
static const JsAstNodeType JS_AST_NODE_TAGGED_TEMPLATE = (JsAstNodeType)1027;
static const JsAstNodeType JS_AST_NODE_TS_EXTENSION_SENTINEL = (JsAstNodeType)2000;

typedef Operator JsOperator;
static const JsOperator JS_OP_ADD = OPERATOR_ADD;
static const JsOperator JS_OP_SUB = OPERATOR_SUB;
static const JsOperator JS_OP_MUL = OPERATOR_MUL;
static const JsOperator JS_OP_DIV = OPERATOR_DIV;
static const JsOperator JS_OP_MOD = OPERATOR_MOD;
static const JsOperator JS_OP_EXP = OPERATOR_JS_EXP;
static const JsOperator JS_OP_EQ = OPERATOR_EQ;
static const JsOperator JS_OP_NE = OPERATOR_NE;
static const JsOperator JS_OP_STRICT_EQ = OPERATOR_JS_STRICT_EQ;
static const JsOperator JS_OP_STRICT_NE = OPERATOR_JS_STRICT_NE;
static const JsOperator JS_OP_LT = OPERATOR_LT;
static const JsOperator JS_OP_LE = OPERATOR_LE;
static const JsOperator JS_OP_GT = OPERATOR_GT;
static const JsOperator JS_OP_GE = OPERATOR_GE;
static const JsOperator JS_OP_AND = OPERATOR_AND;
static const JsOperator JS_OP_OR = OPERATOR_OR;
static const JsOperator JS_OP_BIT_AND = OPERATOR_JS_BIT_AND;
static const JsOperator JS_OP_BIT_OR = OPERATOR_JS_BIT_OR;
static const JsOperator JS_OP_BIT_XOR = OPERATOR_JS_BIT_XOR;
static const JsOperator JS_OP_BIT_LSHIFT = OPERATOR_JS_LSHIFT;
static const JsOperator JS_OP_BIT_RSHIFT = OPERATOR_JS_RSHIFT;
static const JsOperator JS_OP_BIT_URSHIFT = OPERATOR_JS_URSHIFT;
static const JsOperator JS_OP_NOT = OPERATOR_NOT;
static const JsOperator JS_OP_BIT_NOT = OPERATOR_JS_BIT_NOT;
static const JsOperator JS_OP_TYPEOF = OPERATOR_JS_TYPEOF;
static const JsOperator JS_OP_VOID = OPERATOR_JS_VOID;
static const JsOperator JS_OP_DELETE = OPERATOR_JS_DELETE;
static const JsOperator JS_OP_PLUS = OPERATOR_POS;
static const JsOperator JS_OP_MINUS = OPERATOR_NEG;
static const JsOperator JS_OP_INCREMENT = OPERATOR_JS_INCREMENT;
static const JsOperator JS_OP_DECREMENT = OPERATOR_JS_DECREMENT;
static const JsOperator JS_OP_ASSIGN = OPERATOR_ASSIGN;
static const JsOperator JS_OP_ADD_ASSIGN = OPERATOR_JS_ADD_ASSIGN;
static const JsOperator JS_OP_SUB_ASSIGN = OPERATOR_JS_SUB_ASSIGN;
static const JsOperator JS_OP_MUL_ASSIGN = OPERATOR_JS_MUL_ASSIGN;
static const JsOperator JS_OP_DIV_ASSIGN = OPERATOR_JS_DIV_ASSIGN;
static const JsOperator JS_OP_MOD_ASSIGN = OPERATOR_JS_MOD_ASSIGN;
static const JsOperator JS_OP_EXP_ASSIGN = OPERATOR_JS_EXP_ASSIGN;
static const JsOperator JS_OP_BIT_AND_ASSIGN = OPERATOR_JS_BIT_AND_ASSIGN;
static const JsOperator JS_OP_BIT_OR_ASSIGN = OPERATOR_JS_BIT_OR_ASSIGN;
static const JsOperator JS_OP_BIT_XOR_ASSIGN = OPERATOR_JS_BIT_XOR_ASSIGN;
static const JsOperator JS_OP_LSHIFT_ASSIGN = OPERATOR_JS_LSHIFT_ASSIGN;
static const JsOperator JS_OP_RSHIFT_ASSIGN = OPERATOR_JS_RSHIFT_ASSIGN;
static const JsOperator JS_OP_URSHIFT_ASSIGN = OPERATOR_JS_URSHIFT_ASSIGN;
static const JsOperator JS_OP_INSTANCEOF = OPERATOR_JS_INSTANCEOF;
static const JsOperator JS_OP_IN = OPERATOR_IN;
static const JsOperator JS_OP_NULLISH_COALESCE = OPERATOR_JS_NULLISH_COALESCE;
static const JsOperator JS_OP_NULLISH_ASSIGN = OPERATOR_JS_NULLISH_ASSIGN;
static const JsOperator JS_OP_AND_ASSIGN = OPERATOR_JS_AND_ASSIGN;
static const JsOperator JS_OP_OR_ASSIGN = OPERATOR_JS_OR_ASSIGN;

typedef AstLiteralType JsLiteralType;
static const JsLiteralType JS_LITERAL_NUMBER = AST_LITERAL_NUMBER;
static const JsLiteralType JS_LITERAL_STRING = AST_LITERAL_STRING;
static const JsLiteralType JS_LITERAL_BOOLEAN = AST_LITERAL_BOOLEAN;
static const JsLiteralType JS_LITERAL_NULL = AST_LITERAL_NULL;
static const JsLiteralType JS_LITERAL_UNDEFINED = AST_LITERAL_UNDEFINED;

typedef AstNode JsAstNode;
typedef AstIdentNode JsIdentifierNode;
typedef AstLiteralNode JsLiteralNode;
typedef AstBinaryNode JsBinaryNode;
typedef AstUnaryNode JsUnaryNode;
typedef AstAssignNode JsAssignmentNode;
typedef AstFuncNode JsFunctionNode;
typedef AstCallNode JsCallNode;
typedef AstFieldNode JsMemberNode;
typedef AstArrayNode JsArrayNode;
typedef AstMapNode JsObjectNode;

typedef AstPropertyNode JsPropertyNode;

typedef AstVarDeclNode JsVariableDeclarationNode;
typedef AstDeclaratorNode JsVariableDeclaratorNode;
typedef AstIfNode JsIfNode;
typedef AstWhileNode JsWhileNode;

typedef AstForStmtNode JsForNode;

typedef AstReturnNode JsReturnNode;
typedef AstBlockNode JsBlockNode;
typedef AstExprStmtNode JsExpressionStatementNode;
typedef AstScript JsProgramNode;

typedef AstIfNode JsConditionalNode;

// JavaScript template literal node
typedef struct JsTemplateLiteralNode : JsAstNode {
    JsAstNode* quasis;              // Template elements (strings)
    JsAstNode* expressions;         // Interpolated expressions
} JsTemplateLiteralNode;

// JavaScript template element node
typedef struct JsTemplateElementNode : JsAstNode {
    String* raw;                    // Raw string value
    String* cooked;                 // Processed string value
    bool tail;                      // Is this the last element
} JsTemplateElementNode;

// JavaScript tagged template expression node: tag`...`
typedef struct JsTaggedTemplateNode : JsAstNode {
    JsAstNode* tag;                 // Tag function expression
    JsTemplateLiteralNode* quasi;   // Template literal
} JsTaggedTemplateNode;

typedef AstSpreadNode JsSpreadElementNode;
typedef AstClassNode JsClassNode;

typedef AstMethodNode JsMethodDefinitionNode;

typedef AstClassFieldNode JsFieldDefinitionNode;

// JavaScript class static block node: static { ... }
typedef struct JsStaticBlockNode : JsAstNode {
    JsAstNode* body;                // Block statement body
} JsStaticBlockNode;

typedef AstTryNode JsTryNode;
typedef AstCatchNode JsCatchNode;

typedef AstRaiseNode JsThrowNode;

typedef AstArrayNode JsArrayPatternNode;
typedef AstMapNode JsObjectPatternNode;
typedef AstAssignNode JsAssignmentPatternNode;
typedef AstMatchNode JsSwitchNode;
typedef AstMatchArm JsSwitchCaseNode;
typedef AstDoWhileNode JsDoWhileNode;
typedef AstForOfNode JsForOfNode;

// Reuse same struct for for...in
typedef JsForOfNode JsForInNode;

typedef AstArrayNode JsSequenceNode;

typedef AstBreakContinueNode JsBreakContinueNode;

// v11: Labeled statement node
typedef struct JsLabeledStatementNode : JsAstNode {
    const char* label;               // Label name
    int label_len;                   // Length of label name
    JsAstNode* body;                 // Labeled statement body
} JsLabeledStatementNode;

// v17: With statement node
typedef struct JsWithStatementNode : JsAstNode {
    JsAstNode* object;               // Expression in with(expr)
    JsAstNode* body;                 // Body statement
} JsWithStatementNode;

// v11: Regex literal node
typedef struct JsRegexNode : JsAstNode {
    const char* pattern;             // Regex pattern (without slashes)
    int pattern_len;
    const char* flags;               // Regex flags (g, i, m, etc.)
    int flags_len;
} JsRegexNode;

typedef AstYieldNode JsYieldNode;
typedef AstAwaitNode JsAwaitNode;
typedef AstImportDeclNode JsImportNode;

typedef AstImportSpecifierNode JsImportSpecifierNode;

typedef AstExportDeclNode JsExportNode;

typedef AstExportSpecifierNode JsExportSpecifierNode;
