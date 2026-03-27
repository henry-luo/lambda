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

// Python AST Node Types
typedef enum PyAstNodeType {
    PY_AST_NODE_NULL,

    // program
    PY_AST_NODE_MODULE,

    // statements
    PY_AST_NODE_EXPRESSION_STATEMENT,
    PY_AST_NODE_ASSIGNMENT,
    PY_AST_NODE_AUGMENTED_ASSIGNMENT,   // +=, -=, *=, etc.
    PY_AST_NODE_RETURN,
    PY_AST_NODE_IF,
    PY_AST_NODE_ELIF,
    PY_AST_NODE_ELSE,
    PY_AST_NODE_WHILE,
    PY_AST_NODE_FOR,
    PY_AST_NODE_BREAK,
    PY_AST_NODE_CONTINUE,
    PY_AST_NODE_PASS,
    PY_AST_NODE_FUNCTION_DEF,
    PY_AST_NODE_CLASS_DEF,
    PY_AST_NODE_IMPORT,
    PY_AST_NODE_IMPORT_FROM,
    PY_AST_NODE_GLOBAL,
    PY_AST_NODE_NONLOCAL,
    PY_AST_NODE_DEL,
    PY_AST_NODE_ASSERT,
    PY_AST_NODE_RAISE,
    PY_AST_NODE_TRY,
    PY_AST_NODE_EXCEPT,
    PY_AST_NODE_FINALLY,
    PY_AST_NODE_WITH,
    PY_AST_NODE_BLOCK,

    // expressions
    PY_AST_NODE_IDENTIFIER,
    PY_AST_NODE_LITERAL,
    PY_AST_NODE_FSTRING,
    PY_AST_NODE_BINARY_OP,
    PY_AST_NODE_UNARY_OP,
    PY_AST_NODE_BOOLEAN_OP,
    PY_AST_NODE_COMPARE,               // chained: a < b < c
    PY_AST_NODE_CALL,
    PY_AST_NODE_ATTRIBUTE,              // obj.attr
    PY_AST_NODE_SUBSCRIPT,              // obj[key]
    PY_AST_NODE_SLICE,                  // start:stop:step
    PY_AST_NODE_STARRED,                // *args
    PY_AST_NODE_LIST,
    PY_AST_NODE_TUPLE,
    PY_AST_NODE_DICT,
    PY_AST_NODE_SET,
    PY_AST_NODE_LIST_COMPREHENSION,
    PY_AST_NODE_DICT_COMPREHENSION,
    PY_AST_NODE_SET_COMPREHENSION,
    PY_AST_NODE_GENERATOR_EXPRESSION,
    PY_AST_NODE_CONDITIONAL_EXPR,       // x if cond else y
    PY_AST_NODE_LAMBDA,
    PY_AST_NODE_NOT,
    PY_AST_NODE_KEYWORD_ARGUMENT,       // func(key=value)
    PY_AST_NODE_PARAMETER,
    PY_AST_NODE_DEFAULT_PARAMETER,
    PY_AST_NODE_TYPED_PARAMETER,
    PY_AST_NODE_DICT_SPLAT_PARAMETER,   // **kwargs
    PY_AST_NODE_LIST_SPLAT_PARAMETER,   // *args
    PY_AST_NODE_TUPLE_UNPACK,           // a, b = ...
    PY_AST_NODE_PAIR,                   // key: value in dict literal
    PY_AST_NODE_DECORATOR,
    PY_AST_NODE_FSTRING_EXPR,           // f-string interpolation with format spec

    // Phase B: match/case
    PY_AST_NODE_MATCH,                  // match subject: cases...
    PY_AST_NODE_CASE,                   // case pattern [if guard]: body
    PY_AST_NODE_PATTERN,                // discriminated by PyPatternKind

    PY_AST_NODE_COUNT
} PyAstNodeType;

// Python operators
typedef enum PyOperator {
    PY_OP_ADD,              // +
    PY_OP_SUB,              // -
    PY_OP_MUL,              // *
    PY_OP_DIV,              // /
    PY_OP_FLOOR_DIV,        // //
    PY_OP_MOD,              // %
    PY_OP_POW,              // **
    PY_OP_MATMUL,           // @
    PY_OP_LSHIFT,           // <<
    PY_OP_RSHIFT,           // >>
    PY_OP_BIT_AND,          // &
    PY_OP_BIT_OR,           // |
    PY_OP_BIT_XOR,          // ^
    PY_OP_BIT_NOT,          // ~
    PY_OP_EQ,               // ==
    PY_OP_NE,               // !=
    PY_OP_LT,               // <
    PY_OP_LE,               // <=
    PY_OP_GT,               // >
    PY_OP_GE,               // >=
    PY_OP_AND,              // and
    PY_OP_OR,               // or
    PY_OP_NOT,              // not
    PY_OP_IN,               // in
    PY_OP_NOT_IN,           // not in
    PY_OP_IS,               // is
    PY_OP_IS_NOT,           // is not
    PY_OP_NEGATE,           // unary -
    PY_OP_POSITIVE,         // unary +

    // augmented assignment operators
    PY_OP_ADD_ASSIGN,       // +=
    PY_OP_SUB_ASSIGN,       // -=
    PY_OP_MUL_ASSIGN,       // *=
    PY_OP_DIV_ASSIGN,       // /=
    PY_OP_FLOOR_DIV_ASSIGN, // //=
    PY_OP_MOD_ASSIGN,       // %=
    PY_OP_POW_ASSIGN,       // **=
    PY_OP_MATMUL_ASSIGN,    // @=
    PY_OP_LSHIFT_ASSIGN,    // <<=
    PY_OP_RSHIFT_ASSIGN,    // >>=
    PY_OP_BIT_AND_ASSIGN,   // &=
    PY_OP_BIT_OR_ASSIGN,    // |=
    PY_OP_BIT_XOR_ASSIGN,   // ^=

    PY_OP_COUNT
} PyOperator;

// Python literal types
typedef enum PyLiteralType {
    PY_LITERAL_INT,
    PY_LITERAL_FLOAT,
    PY_LITERAL_STRING,
    PY_LITERAL_BOOLEAN,
    PY_LITERAL_NONE,
} PyLiteralType;

// Base Python AST node
// NOTE: Field order must match AstNode layout (node_type, type, next, node)
typedef struct PyAstNode {
    PyAstNodeType node_type;
    Type* type;                     // inferred Lambda type
    struct PyAstNode* next;         // linked list for siblings
    TSNode node;                    // Tree-sitter node
} PyAstNode;

// Python identifier node
typedef struct PyIdentifierNode {
    PyAstNode base;
    String* name;
    NameEntry* entry;
} PyIdentifierNode;

// Python literal node
typedef struct PyLiteralNode {
    PyAstNode base;
    PyLiteralType literal_type;
    union {
        int64_t int_value;
        double float_value;
        String* string_value;
        bool boolean_value;
    } value;
} PyLiteralNode;

// Python binary operation node
typedef struct PyBinaryNode {
    PyAstNode base;
    PyOperator op;
    PyAstNode* left;
    PyAstNode* right;
} PyBinaryNode;

// Python unary operation node
typedef struct PyUnaryNode {
    PyAstNode base;
    PyOperator op;
    PyAstNode* operand;
} PyUnaryNode;

// Python boolean operation node (and/or with short-circuit)
typedef struct PyBooleanNode {
    PyAstNode base;
    PyOperator op;                  // PY_OP_AND or PY_OP_OR
    PyAstNode* left;
    PyAstNode* right;
} PyBooleanNode;

// Python chained comparison node: a < b < c
typedef struct PyCompareNode {
    PyAstNode base;
    PyAstNode* left;                // first operand
    PyOperator* ops;                // array of comparison operators
    PyAstNode** comparators;        // array of comparison operands
    int op_count;                   // number of comparisons
} PyCompareNode;

// Python assignment node
typedef struct PyAssignmentNode {
    PyAstNode base;
    PyAstNode* targets;             // assignment targets (linked list for a = b = expr)
    PyAstNode* value;               // right-hand side
} PyAssignmentNode;

// Python augmented assignment node (+=, -=, etc.)
typedef struct PyAugAssignmentNode {
    PyAstNode base;
    PyOperator op;
    PyAstNode* target;
    PyAstNode* value;
} PyAugAssignmentNode;

// Python function definition node
typedef struct PyFunctionDefNode {
    PyAstNode base;
    String* name;
    PyAstNode* params;              // parameter list
    PyAstNode* body;                // function body (block of statements)
    PyAstNode* decorators;          // decorator list (linked list)
    PyAstNode* return_annotation;   // return type annotation (ignored at runtime)
} PyFunctionDefNode;

// Python call expression node
typedef struct PyCallNode {
    PyAstNode base;
    PyAstNode* function;            // function being called
    PyAstNode* arguments;           // argument list (linked list)
    int arg_count;
} PyCallNode;

// Python attribute access node (obj.attr)
typedef struct PyAttributeNode {
    PyAstNode base;
    PyAstNode* object;
    String* attribute;              // attribute name
} PyAttributeNode;

// Python subscript node (obj[key])
typedef struct PySubscriptNode {
    PyAstNode base;
    PyAstNode* object;
    PyAstNode* index;               // index or slice
} PySubscriptNode;

// Python slice node (start:stop:step)
typedef struct PySliceNode {
    PyAstNode base;
    PyAstNode* start;               // optional
    PyAstNode* stop;                // optional
    PyAstNode* step;                // optional
} PySliceNode;

// Python list/tuple/set expression node
typedef struct PySequenceNode {
    PyAstNode base;
    PyAstNode* elements;            // linked list of elements
    int length;
} PySequenceNode;

// Python dict expression node
typedef struct PyDictNode {
    PyAstNode base;
    PyAstNode* pairs;               // linked list of PyPairNode
    int length;
} PyDictNode;

// Python dict key:value pair node
typedef struct PyPairNode {
    PyAstNode base;
    PyAstNode* key;
    PyAstNode* value;
} PyPairNode;

// Python if statement node
typedef struct PyIfNode {
    PyAstNode base;
    PyAstNode* test;
    PyAstNode* body;                // then block
    PyAstNode* elif_clauses;        // linked list of elif nodes
    PyAstNode* else_body;           // else block (optional)
} PyIfNode;

// Python while statement node
typedef struct PyWhileNode {
    PyAstNode base;
    PyAstNode* test;
    PyAstNode* body;
} PyWhileNode;

// Python for statement node
typedef struct PyForNode {
    PyAstNode base;
    PyAstNode* target;              // loop variable(s)
    PyAstNode* iter;                // iterable expression
    PyAstNode* body;
} PyForNode;

// Python return statement node
typedef struct PyReturnNode {
    PyAstNode base;
    PyAstNode* value;               // return value (optional)
} PyReturnNode;

// Python class definition node
typedef struct PyClassDefNode {
    PyAstNode base;
    String* name;
    PyAstNode* bases;               // base classes (linked list)
    PyAstNode* body;                // class body
    PyAstNode* decorators;
} PyClassDefNode;

// Python try statement node
typedef struct PyTryNode {
    PyAstNode base;
    PyAstNode* body;                // try block
    PyAstNode* handlers;            // except clauses (linked list)
    PyAstNode* else_body;           // else block (optional)
    PyAstNode* finally_body;        // finally block (optional)
} PyTryNode;

// Python except clause node
typedef struct PyExceptNode {
    PyAstNode base;
    PyAstNode* type;                // exception type (optional)
    String* name;                   // as name (optional)
    PyAstNode* body;                // except block
} PyExceptNode;

// Python raise statement node
typedef struct PyRaiseNode {
    PyAstNode base;
    PyAstNode* exception;           // exception value (optional for re-raise)
} PyRaiseNode;

// Python assert statement node
typedef struct PyAssertNode {
    PyAstNode base;
    PyAstNode* test;
    PyAstNode* message;             // optional message expression
} PyAssertNode;

// Python with statement node
typedef struct PyWithNode {
    PyAstNode base;
    PyAstNode* items;               // context manager expression
    String* target;                 // as-target name (optional)
    PyAstNode* body;
} PyWithNode;

// Python global/nonlocal statement node
typedef struct PyGlobalNonlocalNode {
    PyAstNode base;
    String** names;                 // array of names
    int name_count;
} PyGlobalNonlocalNode;

// Python del statement node
typedef struct PyDelNode {
    PyAstNode base;
    PyAstNode* targets;             // targets to delete (linked list)
} PyDelNode;

// Python conditional expression node (x if cond else y)
typedef struct PyConditionalNode {
    PyAstNode base;
    PyAstNode* body;                // value if true
    PyAstNode* test;                // condition
    PyAstNode* else_body;           // value if false
} PyConditionalNode;

// Python lambda expression node
typedef struct PyLambdaNode {
    PyAstNode base;
    PyAstNode* params;
    PyAstNode* body;                // single expression
} PyLambdaNode;

// Python keyword argument node (key=value)
typedef struct PyKeywordArgNode {
    PyAstNode base;
    String* key;                    // keyword name (NULL for **kwargs splat)
    PyAstNode* value;
} PyKeywordArgNode;

// Python parameter node
typedef struct PyParamNode {
    PyAstNode base;
    String* name;
    PyAstNode* default_value;       // optional default
    PyAstNode* annotation;          // type annotation (ignored at runtime)
} PyParamNode;

// Python decorator node
typedef struct PyDecoratorNode {
    PyAstNode base;
    PyAstNode* expression;          // decorator expression
} PyDecoratorNode;

// Python f-string node
typedef struct PyFStringNode {
    PyAstNode base;
    PyAstNode* parts;               // linked list of literal strings and expressions
} PyFStringNode;

// Python f-string expression with format spec: f"{expr:spec}"
typedef struct PyFStringExprNode {
    PyAstNode base;
    PyAstNode* expression;          // the expression to format
    String* format_spec;            // the format spec string (e.g., ".2f", ">10", "05d")
} PyFStringExprNode;

// Python module node (root)
typedef struct PyModuleNode {
    PyAstNode base;
    PyAstNode* body;                // top-level statements
} PyModuleNode;

// Python block node (sequence of statements)
typedef struct PyBlockNode {
    PyAstNode base;
    PyAstNode* statements;
} PyBlockNode;

// Python expression statement node
typedef struct PyExpressionStatementNode {
    PyAstNode base;
    PyAstNode* expression;
} PyExpressionStatementNode;

// Python list comprehension node
typedef struct PyComprehensionNode {
    PyAstNode base;
    PyAstNode* element;             // output expression
    PyAstNode* target;              // loop variable
    PyAstNode* iter;                // iterable
    PyAstNode* conditions;          // if conditions (linked list)
    PyAstNode* inner;               // nested for clause (optional)
} PyComprehensionNode;

// Python starred expression node (*args in call)
typedef struct PyStarredNode {
    PyAstNode base;
    PyAstNode* value;
} PyStarredNode;

// Python import node
typedef struct PyImportNode {
    PyAstNode base;
    String* module_name;            // dotted module name
    String* alias;                  // as alias (optional)
    PyAstNode* names;               // linked list of imported names (for from...import)
} PyImportNode;

// ============================================================================
// Phase B: match/case pattern matching
// ============================================================================

typedef enum PyPatternKind {
    PY_PAT_LITERAL,     // integer/float/string/True/False/None literal
    PY_PAT_CAPTURE,     // single identifier binding (e.g. case x:)
    PY_PAT_WILDCARD,    // _ wildcard
    PY_PAT_OR,          // union_pattern: elements is a linked list of alternatives
    PY_PAT_SEQUENCE,    // list_pattern or tuple_pattern: elements linked list; rest_name if *rest
    PY_PAT_MAPPING,     // dict_pattern: kv_pairs linked list; rest_name if **rest
    PY_PAT_CLASS,       // class_pattern: name=class, elements=positional, kv_pairs=keyword
    PY_PAT_AS,          // as_pattern: literal=inner pattern, name=alias
    PY_PAT_VALUE,       // dotted constant (e.g. Status.OK): name=full dotted string
    PY_PAT_STAR,        // *name or *_ splat inside sequence pattern: name (NULL for _)
} PyPatternKind;

// Pattern node — discriminated by kind.
// Fields are shared/reused across kinds as documented above.
typedef struct PyPatternNode {
    PyAstNode base;             // base.node_type == PY_AST_NODE_PATTERN
    PyPatternKind kind;
    PyAstNode*  literal;        // LITERAL: expr; OR-unused; AS: inner pattern; CLASS-unused
    bool        literal_neg;    // LITERAL: true if preceded by unary minus
    String*     name;           // CAPTURE/STAR: var name; AS: alias; CLASS: class name; VALUE: dotted text
    PyAstNode*  elements;       // OR: alternatives; SEQUENCE: sub-patterns; CLASS: positionals
    String*     rest_name;      // SEQUENCE *rest name; MAPPING **rest name (NULL if absent)
    PyAstNode*  kv_pairs;       // MAPPING: linked list of PyPatternKVNode; CLASS: keyword patterns
    int32_t     rest_pos;       // SEQUENCE: index of *rest (-1 if none); all elements[0..rest_pos-1] before star
} PyPatternNode;

// Key-value pair inside a mapping pattern: {key_pattern: val_pattern}
// Also reused for class keyword patterns: attr_name=sub_pattern
// Stored as a PyPatternNode where name=attr_key (or NULL), literal=key_expr, right via kv_pairs chain.
// For simplicity, both mapping KV and class keyword share this struct.
typedef struct PyPatternKVNode {
    PyAstNode   base;           // base.node_type == PY_AST_NODE_PATTERN
    PyPatternKind kind;         // always PY_PAT_LITERAL (for mapping) or PY_PAT_CAPTURE (class kw)
    PyAstNode*  key_pat;        // mapping: key literal/value; class: NULL (name holds attr)
    PyAstNode*  val_pat;        // value pattern to match against
    String*     attr_name;      // class keyword attr name (e.g. "x" in ClassName(x=pat))
} PyPatternKVNode;

// match statement node
typedef struct PyMatchNode {
    PyAstNode base;
    PyAstNode* subject;         // subject expression
    PyAstNode* cases;           // linked list of PyCaseNode
} PyMatchNode;

// case clause node
typedef struct PyCaseNode {
    PyAstNode base;
    PyAstNode* pattern;         // PyPatternNode
    PyAstNode* guard;           // optional if-guard expression
    PyAstNode* body;            // body block/statement
} PyCaseNode;
