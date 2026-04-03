// rb_ast.hpp — Ruby AST node types and structures for LambdaRuby transpiler
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tree_sitter/api.h>
#include "../ast.hpp"
#include "../lambda-data.hpp"

#ifdef __cplusplus
}
#endif

// Ruby AST Node Types
typedef enum RbAstNodeType {
    RB_AST_NODE_NULL,

    // program
    RB_AST_NODE_PROGRAM,

    // statements
    RB_AST_NODE_EXPRESSION_STATEMENT,
    RB_AST_NODE_ASSIGNMENT,
    RB_AST_NODE_OP_ASSIGNMENT,          // +=, -=, *=, etc.
    RB_AST_NODE_MULTI_ASSIGNMENT,       // a, b, c = 1, 2, 3
    RB_AST_NODE_RETURN,
    RB_AST_NODE_IF,
    RB_AST_NODE_UNLESS,
    RB_AST_NODE_WHILE,
    RB_AST_NODE_UNTIL,
    RB_AST_NODE_FOR,
    RB_AST_NODE_CASE,
    RB_AST_NODE_WHEN,
    RB_AST_NODE_BREAK,
    RB_AST_NODE_NEXT,                   // Ruby's "continue"
    RB_AST_NODE_METHOD_DEF,
    RB_AST_NODE_CLASS_DEF,
    RB_AST_NODE_MODULE_DEF,
    RB_AST_NODE_BEGIN_RESCUE,           // begin/rescue/ensure
    RB_AST_NODE_RESCUE,
    RB_AST_NODE_ENSURE,
    RB_AST_NODE_RAISE,
    RB_AST_NODE_RETRY,
    RB_AST_NODE_YIELD,
    RB_AST_NODE_BLOCK,                  // do..end or { } block body

    // expressions
    RB_AST_NODE_IDENTIFIER,
    RB_AST_NODE_SELF,
    RB_AST_NODE_LITERAL,
    RB_AST_NODE_STRING_INTERPOLATION,   // "hello #{name}"
    RB_AST_NODE_BINARY_OP,
    RB_AST_NODE_UNARY_OP,
    RB_AST_NODE_BOOLEAN_OP,             // and, or, not, &&, ||, !
    RB_AST_NODE_COMPARISON,
    RB_AST_NODE_CALL,                   // method call
    RB_AST_NODE_ATTRIBUTE,              // obj.attr
    RB_AST_NODE_SUBSCRIPT,              // obj[key]
    RB_AST_NODE_ARRAY,
    RB_AST_NODE_HASH,
    RB_AST_NODE_RANGE,                  // 1..10, 1...10
    RB_AST_NODE_PROC_LAMBDA,            // proc { }, lambda { }, -> { }
    RB_AST_NODE_BLOCK_PASS,             // method(&block)
    RB_AST_NODE_SPLAT,                  // *args
    RB_AST_NODE_DOUBLE_SPLAT,           // **kwargs
    RB_AST_NODE_PAIR,                   // key => value or key: value in hash
    RB_AST_NODE_PARAMETER,
    RB_AST_NODE_DEFAULT_PARAMETER,
    RB_AST_NODE_IVAR,                   // @instance_var
    RB_AST_NODE_CVAR,                   // @@class_var
    RB_AST_NODE_GVAR,                   // $global_var
    RB_AST_NODE_CONST,                  // Constant / ClassName
    RB_AST_NODE_TERNARY,                // cond ? a : b
    RB_AST_NODE_SYMBOL,                 // :symbol

    RB_AST_NODE_COUNT
} RbAstNodeType;

// Ruby operators
typedef enum RbOperator {
    RB_OP_ADD,              // +
    RB_OP_SUB,              // -
    RB_OP_MUL,              // *
    RB_OP_DIV,              // /
    RB_OP_MOD,              // %
    RB_OP_POW,              // **
    RB_OP_EQ,               // ==
    RB_OP_NEQ,              // !=
    RB_OP_LT,               // <
    RB_OP_LE,               // <=
    RB_OP_GT,               // >
    RB_OP_GE,               // >=
    RB_OP_CMP,              // <=> (spaceship)
    RB_OP_CASE_EQ,          // ===
    RB_OP_AND,              // && or and
    RB_OP_OR,               // || or or
    RB_OP_NOT,              // ! or not
    RB_OP_BIT_AND,          // &
    RB_OP_BIT_OR,           // |
    RB_OP_BIT_XOR,          // ^
    RB_OP_BIT_NOT,          // ~
    RB_OP_LSHIFT,           // <<
    RB_OP_RSHIFT,           // >>
    RB_OP_NEGATE,           // unary -
    RB_OP_POSITIVE,         // unary +
} RbOperator;

// Ruby literal types
typedef enum RbLiteralType {
    RB_LITERAL_INT,
    RB_LITERAL_FLOAT,
    RB_LITERAL_STRING,
    RB_LITERAL_SYMBOL,
    RB_LITERAL_BOOLEAN,
    RB_LITERAL_NIL,
} RbLiteralType;

// ============================================================================
// AST Node Structures
// ============================================================================

// base node — all Ruby AST nodes start with this layout
typedef struct RbAstNode {
    RbAstNodeType node_type;
    Type* type;                 // inferred type (NULL until type pass)
    struct RbAstNode* next;     // linked list of siblings
    TSNode node;                // source location
} RbAstNode;

// program (top-level)
typedef struct RbProgramNode {
    RbAstNode base;
    RbAstNode* body;            // linked list of statements
} RbProgramNode;

// literal value
typedef struct RbLiteralNode {
    RbAstNode base;
    RbLiteralType literal_type;
    union {
        int64_t int_value;
        double float_value;
        String* string_value;
        bool boolean_value;
    } value;
} RbLiteralNode;

// identifier (local variable or method name)
typedef struct RbIdentifierNode {
    RbAstNode base;
    String* name;
    NameEntry* entry;           // resolved name entry (NULL before resolution)
} RbIdentifierNode;

// instance variable (@x)
typedef struct RbIvarNode {
    RbAstNode base;
    String* name;               // name without @ prefix
} RbIvarNode;

// class variable (@@x)
typedef struct RbCvarNode {
    RbAstNode base;
    String* name;               // name without @@ prefix
} RbCvarNode;

// global variable ($x)
typedef struct RbGvarNode {
    RbAstNode base;
    String* name;               // name without $ prefix
} RbGvarNode;

// constant (CONST or ClassName)
typedef struct RbConstNode {
    RbAstNode base;
    String* name;
} RbConstNode;

// symbol literal (:name)
typedef struct RbSymbolNode {
    RbAstNode base;
    String* name;
} RbSymbolNode;

// binary operation (a + b, a == b, etc.)
typedef struct RbBinaryNode {
    RbAstNode base;
    RbOperator op;
    RbAstNode* left;
    RbAstNode* right;
} RbBinaryNode;

// unary operation (-x, !x, ~x)
typedef struct RbUnaryNode {
    RbAstNode base;
    RbOperator op;
    RbAstNode* operand;
} RbUnaryNode;

// boolean operation (&&, ||, and, or, not)
typedef struct RbBooleanNode {
    RbAstNode base;
    RbOperator op;
    RbAstNode* left;
    RbAstNode* right;           // NULL for unary 'not'
} RbBooleanNode;

// assignment (x = expr)
typedef struct RbAssignmentNode {
    RbAstNode base;
    RbAstNode* target;          // identifier, ivar, subscript, attribute
    RbAstNode* value;
} RbAssignmentNode;

// operator assignment (x += expr, x -= expr, etc.)
typedef struct RbOpAssignmentNode {
    RbAstNode base;
    RbOperator op;
    RbAstNode* target;
    RbAstNode* value;
} RbOpAssignmentNode;

// multiple assignment (a, b, c = 1, 2, 3)
typedef struct RbMultiAssignmentNode {
    RbAstNode base;
    RbAstNode* targets;         // linked list of target nodes (identifiers, etc.)
    RbAstNode* values;          // linked list of value expressions
    int target_count;
    int value_count;
} RbMultiAssignmentNode;

// if / unless
typedef struct RbIfNode {
    RbAstNode base;
    RbAstNode* condition;
    RbAstNode* then_body;       // linked list of statements
    RbAstNode* elsif_chain;     // linked list of elsif nodes (each is RbIfNode with elsif)
    RbAstNode* else_body;       // linked list of else statements
    bool is_unless;             // true for 'unless'
    bool is_modifier;           // true for suffix form: expr if cond
} RbIfNode;

// while / until
typedef struct RbWhileNode {
    RbAstNode base;
    RbAstNode* condition;
    RbAstNode* body;
    bool is_until;              // true for 'until'
} RbWhileNode;

// for x in collection
typedef struct RbForNode {
    RbAstNode base;
    RbAstNode* variable;        // iteration variable
    RbAstNode* collection;      // iterable expression
    RbAstNode* body;
} RbForNode;

// case/when
typedef struct RbCaseNode {
    RbAstNode base;
    RbAstNode* subject;         // case expression (can be NULL)
    RbAstNode* whens;           // linked list of RbWhenNode
    RbAstNode* else_body;       // else branch
} RbCaseNode;

typedef struct RbWhenNode {
    RbAstNode base;
    RbAstNode* patterns;        // linked list of pattern expressions
    RbAstNode* body;
} RbWhenNode;

// method definition
typedef struct RbMethodDefNode {
    RbAstNode base;
    String* name;
    RbAstNode* params;          // linked list of parameter nodes
    RbAstNode* body;            // linked list of body statements
    int param_count;
    int required_count;
    bool is_class_method;       // def self.method_name
    bool has_block_param;       // def m(&block)
    bool has_splat;             // def m(*args)
    bool has_double_splat;      // def m(**kwargs)
} RbMethodDefNode;

// class definition
typedef struct RbClassDefNode {
    RbAstNode base;
    String* name;
    RbAstNode* superclass;      // superclass expression (NULL if none)
    RbAstNode* body;            // linked list of body statements
} RbClassDefNode;

// module definition
typedef struct RbModuleDefNode {
    RbAstNode base;
    String* name;
    RbAstNode* body;
} RbModuleDefNode;

// method call
typedef struct RbCallNode {
    RbAstNode base;
    RbAstNode* receiver;        // NULL for bare function calls
    String* method_name;        // method name
    RbAstNode* args;            // linked list of argument expressions
    RbAstNode* block;           // block argument (do..end or {})
    int arg_count;
    bool has_splat;
    bool has_block_pass;        // &block argument
} RbCallNode;

// attribute access (obj.attr)
typedef struct RbAttributeNode {
    RbAstNode base;
    RbAstNode* object;
    String* attr_name;
} RbAttributeNode;

// subscript (obj[key])
typedef struct RbSubscriptNode {
    RbAstNode base;
    RbAstNode* object;
    RbAstNode* index;
} RbSubscriptNode;

// array literal [a, b, c]
typedef struct RbArrayNode {
    RbAstNode base;
    RbAstNode* elements;        // linked list
    int count;
} RbArrayNode;

// hash literal {a: 1, b: 2}
typedef struct RbHashNode {
    RbAstNode base;
    RbAstNode* pairs;           // linked list of RbPairNode
    int count;
} RbHashNode;

// hash pair (key => value or key: value)
typedef struct RbPairNode {
    RbAstNode base;
    RbAstNode* key;
    RbAstNode* value;
} RbPairNode;

// range (a..b or a...b)
typedef struct RbRangeNode {
    RbAstNode base;
    RbAstNode* start;
    RbAstNode* end;
    bool exclusive;             // true for ... (exclusive end)
} RbRangeNode;

// return statement
typedef struct RbReturnNode {
    RbAstNode base;
    RbAstNode* value;           // NULL for bare 'return'
} RbReturnNode;

// yield
typedef struct RbYieldNode {
    RbAstNode base;
    RbAstNode* args;            // linked list of arguments
    int arg_count;
} RbYieldNode;

// block (do |x, y| ... end or { |x, y| ... })
typedef struct RbBlockNode {
    RbAstNode base;
    RbAstNode* params;          // block parameters
    RbAstNode* body;
    int param_count;
} RbBlockNode;

// string interpolation "hello #{name}"
typedef struct RbStringInterpNode {
    RbAstNode base;
    RbAstNode* parts;           // linked list: alternating literal strings and expressions
    int part_count;
} RbStringInterpNode;

// ternary (cond ? a : b)
typedef struct RbTernaryNode {
    RbAstNode base;
    RbAstNode* condition;
    RbAstNode* true_expr;
    RbAstNode* false_expr;
} RbTernaryNode;

// parameter
typedef struct RbParamNode {
    RbAstNode base;
    String* name;
    RbAstNode* default_value;   // NULL if no default
    bool is_splat;              // *args
    bool is_double_splat;       // **kwargs
    bool is_block;              // &block
} RbParamNode;

// begin/rescue/ensure
typedef struct RbBeginRescueNode {
    RbAstNode base;
    RbAstNode* body;            // begin body
    RbAstNode* rescues;         // linked list of RbRescueNode
    RbAstNode* else_body;       // else branch
    RbAstNode* ensure_body;     // ensure (finally) branch
} RbBeginRescueNode;

typedef struct RbRescueNode {
    RbAstNode base;
    RbAstNode* exception_classes;  // exception class list (can be NULL for bare rescue)
    String* variable_name;         // => var name (can be NULL)
    RbAstNode* body;
} RbRescueNode;

// raise statement
typedef struct RbRaiseNode {
    RbAstNode base;
    RbAstNode* exception;          // exception expression (NULL for bare re-raise)
} RbRaiseNode;

// splat expression (*expr)
typedef struct RbSplatNode {
    RbAstNode base;
    RbAstNode* operand;
} RbSplatNode;

// block pass (&expr)
typedef struct RbBlockPassNode {
    RbAstNode base;
    RbAstNode* value;           // expression being passed as block
} RbBlockPassNode;
