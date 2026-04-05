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

// JavaScript AST Node Types
typedef enum JsAstNodeType {
    JS_AST_NODE_NULL,
    
    JS_AST_NODE_PROGRAM,
    JS_AST_NODE_FUNCTION_DECLARATION,
    JS_AST_NODE_VARIABLE_DECLARATION,
    JS_AST_NODE_EXPRESSION_STATEMENT,
    JS_AST_NODE_BLOCK_STATEMENT,
    JS_AST_NODE_IF_STATEMENT,
    JS_AST_NODE_WHILE_STATEMENT,
    JS_AST_NODE_FOR_STATEMENT,
    JS_AST_NODE_RETURN_STATEMENT,
    JS_AST_NODE_BREAK_STATEMENT,
    JS_AST_NODE_CONTINUE_STATEMENT,
    
    // Expressions
    JS_AST_NODE_IDENTIFIER,
    JS_AST_NODE_LITERAL,
    JS_AST_NODE_BINARY_EXPRESSION,
    JS_AST_NODE_UNARY_EXPRESSION,
    JS_AST_NODE_ASSIGNMENT_EXPRESSION,
    JS_AST_NODE_CALL_EXPRESSION,
    JS_AST_NODE_MEMBER_EXPRESSION,
    JS_AST_NODE_ARRAY_EXPRESSION,
    JS_AST_NODE_OBJECT_EXPRESSION,
    JS_AST_NODE_FUNCTION_EXPRESSION,
    JS_AST_NODE_ARROW_FUNCTION,
    JS_AST_NODE_CONDITIONAL_EXPRESSION,
    
    // ES6+ Features
    JS_AST_NODE_TEMPLATE_LITERAL,
    JS_AST_NODE_TEMPLATE_ELEMENT,
    JS_AST_NODE_SPREAD_ELEMENT,
    JS_AST_NODE_CLASS_DECLARATION,
    JS_AST_NODE_CLASS_EXPRESSION,
    JS_AST_NODE_METHOD_DEFINITION,
    JS_AST_NODE_FIELD_DEFINITION,
    JS_AST_NODE_STATIC_BLOCK,
    JS_AST_NODE_TRY_STATEMENT,
    JS_AST_NODE_CATCH_CLAUSE,
    JS_AST_NODE_FINALLY_CLAUSE,
    JS_AST_NODE_THROW_STATEMENT,
    JS_AST_NODE_ASSIGNMENT_PATTERN,
    JS_AST_NODE_ARRAY_PATTERN,
    JS_AST_NODE_OBJECT_PATTERN,
    
    // Patterns and declarations
    JS_AST_NODE_VARIABLE_DECLARATOR,
    JS_AST_NODE_PROPERTY,
    JS_AST_NODE_PARAMETER,
    JS_AST_NODE_REST_ELEMENT,
    JS_AST_NODE_REST_PROPERTY,
    
    // v5: New expression and control flow
    JS_AST_NODE_NEW_EXPRESSION,
    JS_AST_NODE_SWITCH_STATEMENT,
    JS_AST_NODE_SWITCH_CASE,
    JS_AST_NODE_DO_WHILE_STATEMENT,
    JS_AST_NODE_FOR_OF_STATEMENT,
    JS_AST_NODE_FOR_IN_STATEMENT,

    // v11: Sequence expression
    JS_AST_NODE_SEQUENCE_EXPRESSION,

    // v11: Labeled statement
    JS_AST_NODE_LABELED_STATEMENT,

    // v11: Regex literal
    JS_AST_NODE_REGEX,

    // v14: Generators, async/await, ES modules
    JS_AST_NODE_YIELD_EXPRESSION,
    JS_AST_NODE_AWAIT_EXPRESSION,
    JS_AST_NODE_IMPORT_DECLARATION,
    JS_AST_NODE_EXPORT_DECLARATION,
    JS_AST_NODE_IMPORT_SPECIFIER,

    // v17: with statement (for strict mode rejection)
    JS_AST_NODE_WITH_STATEMENT,

    // v20: Tagged template literals
    JS_AST_NODE_TAGGED_TEMPLATE,
} JsAstNodeType;

// JavaScript operators
typedef enum JsOperator {
    // Binary operators
    JS_OP_ADD,              // +
    JS_OP_SUB,              // -
    JS_OP_MUL,              // *
    JS_OP_DIV,              // /
    JS_OP_MOD,              // %
    JS_OP_EXP,              // **
    
    // Comparison operators
    JS_OP_EQ,               // ==
    JS_OP_NE,               // !=
    JS_OP_STRICT_EQ,        // ===
    JS_OP_STRICT_NE,        // !==
    JS_OP_LT,               // <
    JS_OP_LE,               // <=
    JS_OP_GT,               // >
    JS_OP_GE,               // >=
    
    // Logical operators
    JS_OP_AND,              // &&
    JS_OP_OR,               // ||
    
    // Bitwise operators
    JS_OP_BIT_AND,          // &
    JS_OP_BIT_OR,           // |
    JS_OP_BIT_XOR,          // ^
    JS_OP_BIT_LSHIFT,       // <<
    JS_OP_BIT_RSHIFT,       // >>
    JS_OP_BIT_URSHIFT,      // >>>
    
    // Unary operators
    JS_OP_NOT,              // !
    JS_OP_BIT_NOT,          // ~
    JS_OP_TYPEOF,           // typeof
    JS_OP_VOID,             // void
    JS_OP_DELETE,           // delete
    JS_OP_PLUS,             // +
    JS_OP_MINUS,            // -
    JS_OP_INCREMENT,        // ++
    JS_OP_DECREMENT,        // --
    
    // Assignment operators
    JS_OP_ASSIGN,           // =
    JS_OP_ADD_ASSIGN,       // +=
    JS_OP_SUB_ASSIGN,       // -=
    JS_OP_MUL_ASSIGN,       // *=
    JS_OP_DIV_ASSIGN,       // /=
    JS_OP_MOD_ASSIGN,       // %=
    
    // v5: Additional operators
    JS_OP_EXP_ASSIGN,       // **=
    JS_OP_BIT_AND_ASSIGN,   // &=
    JS_OP_BIT_OR_ASSIGN,    // |=
    JS_OP_BIT_XOR_ASSIGN,   // ^=
    JS_OP_LSHIFT_ASSIGN,    // <<=
    JS_OP_RSHIFT_ASSIGN,    // >>=
    JS_OP_URSHIFT_ASSIGN,   // >>>=
    JS_OP_INSTANCEOF,       // instanceof
    JS_OP_IN,               // in
    JS_OP_NULLISH_COALESCE, // ??

    // v11: Logical/nullish assignment operators
    JS_OP_NULLISH_ASSIGN,   // ??=
    JS_OP_AND_ASSIGN,       // &&=
    JS_OP_OR_ASSIGN,        // ||=
} JsOperator;

// JavaScript literal types
typedef enum JsLiteralType {
    JS_LITERAL_NUMBER,
    JS_LITERAL_STRING,
    JS_LITERAL_BOOLEAN,
    JS_LITERAL_NULL,
    JS_LITERAL_UNDEFINED,
} JsLiteralType;

// Base JavaScript AST node
// NOTE: Field order must match AstNode layout (node_type, type, next, node)
// because NameEntry stores AstNode* but points to JsAstNode memory
typedef struct JsAstNode {
    JsAstNodeType node_type;
    Type* type;                     // Inferred Lambda type
    struct JsAstNode* next;         // Linked list for siblings
    TSNode node;                    // Tree-sitter node
} JsAstNode;

// JavaScript identifier node
typedef struct JsIdentifierNode {
    JsAstNode base;
    String* name;                   // Identifier name
    NameEntry* entry;               // Symbol table entry
} JsIdentifierNode;

// JavaScript literal node
typedef struct JsLiteralNode {
    JsAstNode base;
    JsLiteralType literal_type;
    bool has_decimal;        // true if source text contains '.' or 'e'/'E' (fractional hint)
    union {
        double number_value;
        String* string_value;
        bool boolean_value;
    } value;
} JsLiteralNode;

// JavaScript binary expression node
typedef struct JsBinaryNode {
    JsAstNode base;
    JsOperator op;
    JsAstNode* left;
    JsAstNode* right;
} JsBinaryNode;

// JavaScript unary expression node
typedef struct JsUnaryNode {
    JsAstNode base;
    JsOperator op;
    JsAstNode* operand;
    bool prefix;                    // true for ++x, false for x++
} JsUnaryNode;

// JavaScript assignment expression node
typedef struct JsAssignmentNode {
    JsAstNode base;
    JsOperator op;                  // Assignment operator
    JsAstNode* left;                // Left-hand side (lvalue)
    JsAstNode* right;               // Right-hand side (rvalue)
} JsAssignmentNode;

// JavaScript function node (declaration or expression)
typedef struct JsFunctionNode {
    JsAstNode base;
    String* name;                   // Function name (null for anonymous)
    JsAstNode* params;              // Parameter list
    JsAstNode* body;                // Function body
    bool is_arrow;                  // Arrow function vs regular function
    bool is_async;                  // Async function
    bool is_generator;              // Generator function
    struct TsTypeAnnotationNode* ts_return_type; // TS return type annotation (NULL in JS mode)
} JsFunctionNode;

// JavaScript call expression node
typedef struct JsCallNode {
    JsAstNode base;
    JsAstNode* callee;              // Function being called
    JsAstNode* arguments;           // Argument list
    bool optional;                  // true for obj?.method()
} JsCallNode;

// JavaScript member expression node
typedef struct JsMemberNode {
    JsAstNode base;
    JsAstNode* object;              // Object being accessed
    JsAstNode* property;            // Property being accessed
    bool computed;                  // true for obj[prop], false for obj.prop
    bool optional;                  // true for obj?.prop or obj?.[prop]
} JsMemberNode;

// JavaScript array expression node
typedef struct JsArrayNode {
    JsAstNode base;
    JsAstNode* elements;            // Array elements
    int length;                     // Number of elements
} JsArrayNode;

// JavaScript object expression node
typedef struct JsObjectNode {
    JsAstNode base;
    JsAstNode* properties;          // Object properties
} JsObjectNode;

// JavaScript property node (for object literals)
typedef struct JsPropertyNode {
    JsAstNode base;
    JsAstNode* key;                 // Property key
    JsAstNode* value;               // Property value
    bool computed;                  // true for [key]: value
    bool method;                    // true for method shorthand
} JsPropertyNode;

// JavaScript variable declaration node
typedef struct JsVariableDeclarationNode {
    JsAstNode base;
    JsAstNode* declarations;        // Variable declarators
    int kind;                       // Variable declaration kind (JsVarKind)
} JsVariableDeclarationNode;

// JavaScript variable declarator node
typedef struct JsVariableDeclaratorNode {
    JsAstNode base;
    JsAstNode* id;                  // Variable identifier
    JsAstNode* init;                // Initializer expression (optional)
    struct TsTypeAnnotationNode* ts_type; // TS type annotation (NULL in JS mode)
} JsVariableDeclaratorNode;

// JavaScript if statement node
typedef struct JsIfNode {
    JsAstNode base;
    JsAstNode* test;                // Condition expression
    JsAstNode* consequent;          // Then branch
    JsAstNode* alternate;           // Else branch (optional)
} JsIfNode;

// JavaScript while statement node
typedef struct JsWhileNode {
    JsAstNode base;
    JsAstNode* test;                // Condition expression
    JsAstNode* body;                // Loop body
} JsWhileNode;

// JavaScript for statement node
typedef struct JsForNode {
    JsAstNode base;
    JsAstNode* init;                // Initialization (optional)
    JsAstNode* test;                // Condition (optional)
    JsAstNode* update;              // Update expression (optional)
    JsAstNode* body;                // Loop body
} JsForNode;

// JavaScript return statement node
typedef struct JsReturnNode {
    JsAstNode base;
    JsAstNode* argument;            // Return value (optional)
} JsReturnNode;

// JavaScript block statement node
typedef struct JsBlockNode {
    JsAstNode base;
    JsAstNode* statements;          // Statement list
} JsBlockNode;

// JavaScript expression statement node
typedef struct JsExpressionStatementNode {
    JsAstNode base;
    JsAstNode* expression;          // The expression
} JsExpressionStatementNode;

// JavaScript program node (root)
typedef struct JsProgramNode {
    JsAstNode base;
    JsAstNode* body;                // Top-level statements
} JsProgramNode;

// JavaScript conditional expression node (ternary operator)
typedef struct JsConditionalNode {
    JsAstNode base;
    JsAstNode* test;                // Condition
    JsAstNode* consequent;          // True branch
    JsAstNode* alternate;           // False branch
} JsConditionalNode;

// JavaScript template literal node
typedef struct JsTemplateLiteralNode {
    JsAstNode base;
    JsAstNode* quasis;              // Template elements (strings)
    JsAstNode* expressions;         // Interpolated expressions
} JsTemplateLiteralNode;

// JavaScript template element node
typedef struct JsTemplateElementNode {
    JsAstNode base;
    String* raw;                    // Raw string value
    String* cooked;                 // Processed string value
    bool tail;                      // Is this the last element
} JsTemplateElementNode;

// JavaScript tagged template expression node: tag`...`
typedef struct JsTaggedTemplateNode {
    JsAstNode base;
    JsAstNode* tag;                 // Tag function expression
    JsTemplateLiteralNode* quasi;   // Template literal
} JsTaggedTemplateNode;

// JavaScript spread element node
typedef struct JsSpreadElementNode {
    JsAstNode base;
    JsAstNode* argument;            // Expression being spread
} JsSpreadElementNode;

// JavaScript class declaration node
typedef struct JsClassNode {
    JsAstNode base;
    String* name;                   // Class name (optional for expressions)
    JsAstNode* superclass;          // Parent class (optional)
    JsAstNode* body;                // Class body
} JsClassNode;

// JavaScript method definition node
typedef struct JsMethodDefinitionNode {
    JsAstNode base;
    JsAstNode* key;                 // Method name
    JsAstNode* value;               // Function expression
    enum {
        JS_METHOD_METHOD,
        JS_METHOD_CONSTRUCTOR,
        JS_METHOD_GET,
        JS_METHOD_SET
    } kind;
    bool computed;                  // Computed property name
    bool static_method;             // Static method
} JsMethodDefinitionNode;

// JavaScript static field definition node (class body field)
typedef struct JsFieldDefinitionNode {
    JsAstNode base;
    JsAstNode* key;                 // Field name (identifier or computed expression)
    JsAstNode* value;               // Initializer expression (optional)
    bool is_static;                 // Whether it's a static field
    bool is_private;                // Whether it's a private field (#field)
    bool computed;                  // Whether key is a computed property [expr]
} JsFieldDefinitionNode;

// JavaScript class static block node: static { ... }
typedef struct JsStaticBlockNode {
    JsAstNode base;
    JsAstNode* body;                // Block statement body
} JsStaticBlockNode;

// JavaScript try statement node
typedef struct JsTryNode {
    JsAstNode base;
    JsAstNode* block;               // Try block
    JsAstNode* handler;             // Catch clause (optional)
    JsAstNode* finalizer;           // Finally block (optional)
} JsTryNode;

// JavaScript catch clause node
typedef struct JsCatchNode {
    JsAstNode base;
    JsAstNode* param;               // Exception parameter (optional)
    JsAstNode* body;                // Catch block
} JsCatchNode;

// JavaScript throw statement node
typedef struct JsThrowNode {
    JsAstNode base;
    JsAstNode* argument;            // Expression to throw
} JsThrowNode;

// JavaScript array pattern node (destructuring)
typedef struct JsArrayPatternNode {
    JsAstNode base;
    JsAstNode* elements;            // Pattern elements
} JsArrayPatternNode;

// JavaScript object pattern node (destructuring)
typedef struct JsObjectPatternNode {
    JsAstNode base;
    JsAstNode* properties;          // Pattern properties
} JsObjectPatternNode;

// JavaScript assignment pattern node (default parameters)
typedef struct JsAssignmentPatternNode {
    JsAstNode base;
    JsAstNode* left;                // Pattern
    JsAstNode* right;               // Default value
} JsAssignmentPatternNode;

// v5: JavaScript switch statement node
typedef struct JsSwitchNode {
    JsAstNode base;
    JsAstNode* discriminant;        // Expression to match
    JsAstNode* cases;               // Linked list of JsSwitchCaseNode
} JsSwitchNode;

// v5: JavaScript switch case/default node
typedef struct JsSwitchCaseNode {
    JsAstNode base;
    JsAstNode* test;                // Test expression (NULL for default)
    JsAstNode* consequent;          // Linked list of statements
} JsSwitchCaseNode;

// v5: JavaScript do...while statement node
typedef struct JsDoWhileNode {
    JsAstNode base;
    JsAstNode* body;                // Loop body
    JsAstNode* test;                // Condition expression
} JsDoWhileNode;

// v5: JavaScript for...of / for...in statement node
typedef struct JsForOfNode {
    JsAstNode base;
    JsAstNode* left;                // Variable declaration or pattern
    JsAstNode* right;               // Iterable expression
    JsAstNode* body;                // Loop body
    int kind;                       // Variable kind (var/let/const)
} JsForOfNode;

// Reuse same struct for for...in
typedef JsForOfNode JsForInNode;

// v11: Sequence expression node (comma operator)
typedef struct JsSequenceNode {
    JsAstNode base;
    JsAstNode* expressions;          // Linked list of expressions
} JsSequenceNode;

// v11: Break/continue with optional label
typedef struct JsBreakContinueNode {
    JsAstNode base;
    const char* label;               // Optional label name (NULL if unlabeled)
    int label_len;                   // Length of label name
} JsBreakContinueNode;

// v11: Labeled statement node
typedef struct JsLabeledStatementNode {
    JsAstNode base;
    const char* label;               // Label name
    int label_len;                   // Length of label name
    JsAstNode* body;                 // Labeled statement body
} JsLabeledStatementNode;

// v11: Regex literal node
typedef struct JsRegexNode {
    JsAstNode base;
    const char* pattern;             // Regex pattern (without slashes)
    int pattern_len;
    const char* flags;               // Regex flags (g, i, m, etc.)
    int flags_len;
} JsRegexNode;

// v14: Yield expression node (for generators)
typedef struct JsYieldNode {
    JsAstNode base;
    JsAstNode* argument;             // Yielded expression (NULL for bare yield)
    bool delegate;                   // true for yield* (delegation)
} JsYieldNode;

// v14: Await expression node
typedef struct JsAwaitNode {
    JsAstNode base;
    JsAstNode* argument;             // Awaited expression
} JsAwaitNode;

// v14: Import declaration node
typedef struct JsImportNode {
    JsAstNode base;
    String* source;                  // Module specifier string
    JsAstNode* specifiers;           // Linked list of import specifiers (identifiers)
    String* default_name;            // Default import name (NULL if none)
    String* namespace_name;          // Namespace import name for * as X (NULL if none)
} JsImportNode;

// v14: Import specifier node (for named imports: import { remote as local })  
typedef struct JsImportSpecifierNode {
    JsAstNode base;
    String* local_name;              // Local binding name (alias if present, else export name)
    String* remote_name;             // Name exported by the module
} JsImportSpecifierNode;

// v14: Export declaration node
typedef struct JsExportNode {
    JsAstNode base;
    JsAstNode* declaration;          // Exported declaration (function, class, variable)
    JsAstNode* specifiers;           // Linked list of export specifiers
    String* source;                  // Re-export source (NULL for local exports)
    bool is_default;                 // true for export default
} JsExportNode;
