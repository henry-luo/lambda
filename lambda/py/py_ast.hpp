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

// Python uses the shared node-kind space for structurally identical nodes.
// Python-only syntax remains isolated after the extension sentinel until its
// core representation has the fields needed to preserve Python semantics.
typedef AstNodeType PyAstNodeType;
static const PyAstNodeType PY_AST_NODE_NULL = AST_NODE_NULL;
static const PyAstNodeType PY_AST_NODE_MODULE = AST_SCRIPT;
static const PyAstNodeType PY_AST_NODE_EXPRESSION_STATEMENT = AST_NODE_EXPR_STMT;
static const PyAstNodeType PY_AST_NODE_ASSIGNMENT = AST_NODE_ASSIGN;
static const PyAstNodeType PY_AST_NODE_RETURN = AST_NODE_RETURN_STAM;
static const PyAstNodeType PY_AST_NODE_WHILE = AST_NODE_WHILE_STAM;
static const PyAstNodeType PY_AST_NODE_FOR = AST_NODE_FOR_OF_STAM;
static const PyAstNodeType PY_AST_NODE_BREAK = AST_NODE_BREAK_STAM;
static const PyAstNodeType PY_AST_NODE_CONTINUE = AST_NODE_CONTINUE_STAM;
static const PyAstNodeType PY_AST_NODE_FUNCTION_DEF = AST_NODE_FUNC;
static const PyAstNodeType PY_AST_NODE_CLASS_DEF = AST_NODE_CLASS;
static const PyAstNodeType PY_AST_NODE_IMPORT = AST_NODE_IMPORT;
static const PyAstNodeType PY_AST_NODE_RAISE = AST_NODE_RAISE_STAM;
static const PyAstNodeType PY_AST_NODE_TRY = AST_NODE_TRY_STAM;
static const PyAstNodeType PY_AST_NODE_BLOCK = AST_NODE_BLOCK;
static const PyAstNodeType PY_AST_NODE_IDENTIFIER = AST_NODE_IDENT;
static const PyAstNodeType PY_AST_NODE_LITERAL = AST_NODE_LITERAL;
static const PyAstNodeType PY_AST_NODE_BINARY_OP = AST_NODE_BINARY;
static const PyAstNodeType PY_AST_NODE_UNARY_OP = AST_NODE_UNARY;
static const PyAstNodeType PY_AST_NODE_CALL = AST_NODE_CALL_EXPR;
static const PyAstNodeType PY_AST_NODE_ATTRIBUTE = AST_NODE_MEMBER_EXPR;
static const PyAstNodeType PY_AST_NODE_SUBSCRIPT = AST_NODE_INDEX_EXPR;
static const PyAstNodeType PY_AST_NODE_STARRED = AST_NODE_SPREAD;
static const PyAstNodeType PY_AST_NODE_LIST = AST_NODE_ARRAY;
static const PyAstNodeType PY_AST_NODE_TUPLE = AST_NODE_SEQ;
static const PyAstNodeType PY_AST_NODE_DICT = AST_NODE_MAP;
static const PyAstNodeType PY_AST_NODE_CONDITIONAL_EXPR = AST_NODE_IF_EXPR;
static const PyAstNodeType PY_AST_NODE_LAMBDA = AST_NODE_FUNC_EXPR;
static const PyAstNodeType PY_AST_NODE_KEYWORD_ARGUMENT = AST_NODE_NAMED_ARG;
static const PyAstNodeType PY_AST_NODE_PARAMETER = AST_NODE_PARAM;
static const PyAstNodeType PY_AST_NODE_PAIR = AST_NODE_PROPERTY;
static const PyAstNodeType PY_AST_NODE_MATCH = AST_NODE_MATCH_EXPR;
static const PyAstNodeType PY_AST_NODE_CASE = AST_NODE_MATCH_ARM;
static const PyAstNodeType PY_AST_NODE_YIELD = AST_NODE_YIELD;
static const PyAstNodeType PY_AST_NODE_AWAIT = AST_NODE_AWAIT;

static const PyAstNodeType PY_AST_NODE_EXTENSION_SENTINEL = (PyAstNodeType)2000;
static const PyAstNodeType PY_AST_NODE_AUGMENTED_ASSIGNMENT = (PyAstNodeType)2001;
static const PyAstNodeType PY_AST_NODE_IF = (PyAstNodeType)2002;
static const PyAstNodeType PY_AST_NODE_ELIF = (PyAstNodeType)2003;
static const PyAstNodeType PY_AST_NODE_ELSE = (PyAstNodeType)2004;
static const PyAstNodeType PY_AST_NODE_PASS = (PyAstNodeType)2005;
static const PyAstNodeType PY_AST_NODE_IMPORT_FROM = (PyAstNodeType)2006;
static const PyAstNodeType PY_AST_NODE_GLOBAL = (PyAstNodeType)2007;
static const PyAstNodeType PY_AST_NODE_NONLOCAL = (PyAstNodeType)2008;
static const PyAstNodeType PY_AST_NODE_DEL = (PyAstNodeType)2009;
static const PyAstNodeType PY_AST_NODE_ASSERT = (PyAstNodeType)2010;
static const PyAstNodeType PY_AST_NODE_EXCEPT = (PyAstNodeType)2011;
static const PyAstNodeType PY_AST_NODE_FINALLY = (PyAstNodeType)2012;
static const PyAstNodeType PY_AST_NODE_WITH = (PyAstNodeType)2013;
static const PyAstNodeType PY_AST_NODE_FSTRING = (PyAstNodeType)2014;
static const PyAstNodeType PY_AST_NODE_BOOLEAN_OP = (PyAstNodeType)2015;
static const PyAstNodeType PY_AST_NODE_COMPARE = (PyAstNodeType)2016;
static const PyAstNodeType PY_AST_NODE_SLICE = (PyAstNodeType)2017;
static const PyAstNodeType PY_AST_NODE_SET = (PyAstNodeType)2018;
static const PyAstNodeType PY_AST_NODE_LIST_COMPREHENSION = (PyAstNodeType)2019;
static const PyAstNodeType PY_AST_NODE_DICT_COMPREHENSION = (PyAstNodeType)2020;
static const PyAstNodeType PY_AST_NODE_SET_COMPREHENSION = (PyAstNodeType)2021;
static const PyAstNodeType PY_AST_NODE_GENERATOR_EXPRESSION = (PyAstNodeType)2022;
static const PyAstNodeType PY_AST_NODE_NOT = (PyAstNodeType)2023;
static const PyAstNodeType PY_AST_NODE_DEFAULT_PARAMETER = (PyAstNodeType)2024;
static const PyAstNodeType PY_AST_NODE_TYPED_PARAMETER = (PyAstNodeType)2025;
static const PyAstNodeType PY_AST_NODE_DICT_SPLAT_PARAMETER = (PyAstNodeType)2026;
static const PyAstNodeType PY_AST_NODE_LIST_SPLAT_PARAMETER = (PyAstNodeType)2027;
static const PyAstNodeType PY_AST_NODE_TUPLE_UNPACK = (PyAstNodeType)2028;
static const PyAstNodeType PY_AST_NODE_DECORATOR = (PyAstNodeType)2029;
static const PyAstNodeType PY_AST_NODE_FSTRING_EXPR = (PyAstNodeType)2030;
static const PyAstNodeType PY_AST_NODE_PATTERN = (PyAstNodeType)2031;
static const PyAstNodeType PY_AST_NODE_COUNT = (PyAstNodeType)2032;

// Shared operators retain the canonical meaning across guest frontends.
typedef Operator PyOperator;
static const PyOperator PY_OP_ADD = OPERATOR_ADD;
static const PyOperator PY_OP_SUB = OPERATOR_SUB;
static const PyOperator PY_OP_MUL = OPERATOR_MUL;
static const PyOperator PY_OP_DIV = OPERATOR_DIV;
static const PyOperator PY_OP_FLOOR_DIV = OPERATOR_IDIV;
static const PyOperator PY_OP_MOD = OPERATOR_MOD;
static const PyOperator PY_OP_POW = OPERATOR_POW;
static const PyOperator PY_OP_MATMUL = (PyOperator)3000;
static const PyOperator PY_OP_LSHIFT = OPERATOR_JS_LSHIFT;
static const PyOperator PY_OP_RSHIFT = OPERATOR_JS_RSHIFT;
static const PyOperator PY_OP_BIT_AND = OPERATOR_JS_BIT_AND;
static const PyOperator PY_OP_BIT_OR = OPERATOR_JS_BIT_OR;
static const PyOperator PY_OP_BIT_XOR = OPERATOR_JS_BIT_XOR;
static const PyOperator PY_OP_BIT_NOT = OPERATOR_JS_BIT_NOT;
static const PyOperator PY_OP_EQ = OPERATOR_EQ;
static const PyOperator PY_OP_NE = OPERATOR_NE;
static const PyOperator PY_OP_LT = OPERATOR_LT;
static const PyOperator PY_OP_LE = OPERATOR_LE;
static const PyOperator PY_OP_GT = OPERATOR_GT;
static const PyOperator PY_OP_GE = OPERATOR_GE;
static const PyOperator PY_OP_AND = OPERATOR_AND;
static const PyOperator PY_OP_OR = OPERATOR_OR;
static const PyOperator PY_OP_NOT = OPERATOR_NOT;
static const PyOperator PY_OP_IN = OPERATOR_IN;
static const PyOperator PY_OP_NOT_IN = (PyOperator)3001;
static const PyOperator PY_OP_IS = OPERATOR_IS;
static const PyOperator PY_OP_IS_NOT = (PyOperator)3002;
static const PyOperator PY_OP_NEGATE = OPERATOR_NEG;
static const PyOperator PY_OP_POSITIVE = OPERATOR_POS;
static const PyOperator PY_OP_ADD_ASSIGN = OPERATOR_JS_ADD_ASSIGN;
static const PyOperator PY_OP_SUB_ASSIGN = OPERATOR_JS_SUB_ASSIGN;
static const PyOperator PY_OP_MUL_ASSIGN = OPERATOR_JS_MUL_ASSIGN;
static const PyOperator PY_OP_DIV_ASSIGN = OPERATOR_JS_DIV_ASSIGN;
static const PyOperator PY_OP_FLOOR_DIV_ASSIGN = (PyOperator)3003;
static const PyOperator PY_OP_MOD_ASSIGN = OPERATOR_JS_MOD_ASSIGN;
static const PyOperator PY_OP_POW_ASSIGN = OPERATOR_JS_EXP_ASSIGN;
static const PyOperator PY_OP_MATMUL_ASSIGN = (PyOperator)3004;
static const PyOperator PY_OP_LSHIFT_ASSIGN = OPERATOR_JS_LSHIFT_ASSIGN;
static const PyOperator PY_OP_RSHIFT_ASSIGN = OPERATOR_JS_RSHIFT_ASSIGN;
static const PyOperator PY_OP_BIT_AND_ASSIGN = OPERATOR_JS_BIT_AND_ASSIGN;
static const PyOperator PY_OP_BIT_OR_ASSIGN = OPERATOR_JS_BIT_OR_ASSIGN;
static const PyOperator PY_OP_BIT_XOR_ASSIGN = OPERATOR_JS_BIT_XOR_ASSIGN;
static const PyOperator PY_OP_COUNT = (PyOperator)3005;

typedef AstLiteralType PyLiteralType;
static const PyLiteralType PY_LITERAL_INT = (PyLiteralType)1000;
static const PyLiteralType PY_LITERAL_FLOAT = (PyLiteralType)1001;
static const PyLiteralType PY_LITERAL_STRING = AST_LITERAL_STRING;
static const PyLiteralType PY_LITERAL_BOOLEAN = AST_LITERAL_BOOLEAN;
static const PyLiteralType PY_LITERAL_NONE = AST_LITERAL_NULL;

// The Python builder now shares the actual core base node, not a layout copy.
// Python extension structs retain their payload fields until the A2 leaf port.
typedef AstNode PyAstNode;

// Python-only function facts remain behind the shared extension handle while
// generic capture and evidence facts live in FnAnalysis.
typedef struct PyFnExt {
    bool has_star_args;
    bool has_kwargs;
    bool is_method;
    bool is_generator;
    bool is_async;
    int required_param_count;
    int default_param_count;
} PyFnExt;

typedef AstIdentNode PyIdentifierNode;

typedef struct PyLiteralNode : AstLiteralNode {
    // Python integers and floats need distinct payloads without enlarging JS literals.
    union {
        double float_value;
        int64_t int_value;
        String* string_value;
        bool boolean_value;
    } value;
    bool is_bigint_literal;
    const char* bigint_literal_str;
} PyLiteralNode;

typedef AstBinaryNode PyBinaryNode;

typedef AstUnaryNode PyUnaryNode;

// Python boolean operation node (and/or with short-circuit)
typedef struct PyBooleanNode : PyAstNode {
    PyOperator op;                  // PY_OP_AND or PY_OP_OR
    PyAstNode* left;
    PyAstNode* right;
} PyBooleanNode;

// Python chained comparison node: a < b < c
typedef struct PyCompareNode : PyAstNode {
    PyAstNode* left;                // first operand
    PyOperator* ops;                // array of comparison operators
    PyAstNode** comparators;        // array of comparison operands
    int op_count;                   // number of comparisons
} PyCompareNode;

typedef AstAssignNode PyAssignmentNode;

typedef AstAssignNode PyAugAssignmentNode;

// Python function definition node
typedef struct PyFunctionDefNode : PyAstNode {
    String* name;
    PyAstNode* params;              // parameter list
    PyAstNode* body;                // function body (block of statements)
    PyAstNode* decorators;          // decorator list (linked list)
    PyAstNode* return_annotation;   // return type annotation (ignored at runtime)
    NameScope* vars;
    FnAnalysis* analysis;
    FnExt ext;
    bool is_async;                  // true for async def (Phase D)
} PyFunctionDefNode;

typedef struct PyCallNode : AstCallNode {
    // Python builtin lowering needs an eager count; JS calls keep their original layout.
    int arg_count;
} PyCallNode;

// Python attribute access node (obj.attr)
typedef struct PyAttributeNode : PyAstNode {
    PyAstNode* object;
    String* attribute;              // attribute name
} PyAttributeNode;

// Python subscript node (obj[key])
typedef struct PySubscriptNode : PyAstNode {
    PyAstNode* object;
    PyAstNode* index;               // index or slice
} PySubscriptNode;

// Python slice node (start:stop:step)
typedef struct PySliceNode : PyAstNode {
    PyAstNode* start;               // optional
    PyAstNode* stop;                // optional
    PyAstNode* step;                // optional
} PySliceNode;

typedef AstArrayNode PySequenceNode;

// Python dict expression node
typedef struct PyDictNode : PyAstNode {
    PyAstNode* pairs;               // linked list of PyPairNode
    int length;
} PyDictNode;

typedef AstPropertyNode PyPairNode;

// Python if statement node
typedef struct PyIfNode : PyAstNode {
    PyAstNode* test;
    PyAstNode* body;                // then block
    PyAstNode* elif_clauses;        // linked list of elif nodes
    PyAstNode* else_body;           // else block (optional)
} PyIfNode;

// Python while statement node
typedef struct PyWhileNode : PyAstNode {
    PyAstNode* test;
    PyAstNode* body;
} PyWhileNode;

typedef AstForOfNode PyForNode;

typedef AstReturnNode PyReturnNode;

// Python class definition node
typedef struct PyClassDefNode : PyAstNode {
    String* name;
    PyAstNode* bases;               // base classes (linked list)
    PyAstNode* body;                // class body
    PyAstNode* decorators;
    PyAstNode* metaclass;           // metaclass= keyword argument value (Phase F5)
} PyClassDefNode;

// Python try statement node
typedef struct PyTryNode : PyAstNode {
    PyAstNode* body;                // try block
    PyAstNode* handlers;            // except clauses (linked list)
    PyAstNode* else_body;           // else block (optional)
    PyAstNode* finally_body;        // finally block (optional)
} PyTryNode;

// Python except clause node
typedef struct PyExceptNode : PyAstNode {
    PyAstNode* type;                // exception type (optional)
    String* name;                   // as name (optional)
    PyAstNode* body;                // except block
} PyExceptNode;

typedef AstRaiseNode PyRaiseNode;

// Python assert statement node
typedef struct PyAssertNode : PyAstNode {
    PyAstNode* test;
    PyAstNode* message;             // optional message expression
} PyAssertNode;

// Python with statement node
typedef struct PyWithNode : PyAstNode {
    PyAstNode* items;               // context manager expression
    String* target;                 // as-target name (optional)
    PyAstNode* body;
} PyWithNode;

// Python global/nonlocal statement node
typedef struct PyGlobalNonlocalNode : PyAstNode {
    String** names;                 // array of names
    int name_count;
} PyGlobalNonlocalNode;

// Python del statement node
typedef struct PyDelNode : PyAstNode {
    PyAstNode* targets;             // targets to delete (linked list)
} PyDelNode;

typedef AstIfNode PyConditionalNode;

// Python lambda expression node
typedef struct PyLambdaNode : PyAstNode {
    PyAstNode* params;
    PyAstNode* body;                // single expression
    NameScope* vars;
    FnAnalysis* analysis;
    FnExt ext;
} PyLambdaNode;

// Python keyword argument node (key=value)
typedef struct PyKeywordArgNode : PyAstNode {
    String* key;                    // keyword name (NULL for **kwargs splat)
    PyAstNode* value;
} PyKeywordArgNode;

// Python parameter node
typedef struct PyParamNode : PyAstNode {
    String* name;
    PyAstNode* default_value;       // optional default
    PyAstNode* annotation;          // type annotation (ignored at runtime)
} PyParamNode;

// Python decorator node
typedef struct PyDecoratorNode : PyAstNode {
    PyAstNode* expression;          // decorator expression
} PyDecoratorNode;

// Python f-string node
typedef struct PyFStringNode : PyAstNode {
    PyAstNode* parts;               // linked list of literal strings and expressions
} PyFStringNode;

// Python f-string expression with format spec: f"{expr:spec}"
typedef struct PyFStringExprNode : PyAstNode {
    PyAstNode* expression;          // the expression to format
    String* format_spec;            // the format spec string (e.g., ".2f", ">10", "05d")
} PyFStringExprNode;

// Python module node (root)
typedef struct PyModuleNode : PyAstNode {
    PyAstNode* body;                // top-level statements
} PyModuleNode;

typedef AstBlockNode PyBlockNode;

typedef AstExprStmtNode PyExpressionStatementNode;

// Python list comprehension node
typedef struct PyComprehensionNode : PyAstNode {
    PyAstNode* element;             // output expression
    PyAstNode* target;              // loop variable
    PyAstNode* iter;                // iterable
    PyAstNode* conditions;          // if conditions (linked list)
    PyAstNode* inner;               // nested for clause (optional)
} PyComprehensionNode;

// Python starred expression node (*args in call)
typedef struct PyStarredNode : PyAstNode {
    PyAstNode* value;
} PyStarredNode;

// Python import node
typedef struct PyImportNode : PyAstNode {
    String* module_name;            // dotted module name
    String* alias;                  // as alias (optional)
    PyAstNode* names;               // linked list of imported names (for from...import)
} PyImportNode;

// Python yield / yield from expression node (Phase A: generators)
typedef struct PyYieldNode : PyAstNode { // node_type == PY_AST_NODE_YIELD
    PyAstNode* value;       // yielded value (NULL for bare yield)
    bool is_from;           // true for 'yield from'
} PyYieldNode;

// Python await expression node (Phase D: async/await)
typedef struct PyAwaitNode : PyAstNode { // node_type == PY_AST_NODE_AWAIT
    PyAstNode* value;       // the awaited expression
} PyAwaitNode;

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
typedef struct PyPatternNode : PyAstNode { // node_type == PY_AST_NODE_PATTERN
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
typedef struct PyMatchNode : PyAstNode {
    PyAstNode* subject;         // subject expression
    PyAstNode* cases;           // linked list of PyCaseNode
} PyMatchNode;

// case clause node
typedef struct PyCaseNode : PyAstNode {
    PyAstNode* pattern;         // PyPatternNode
    PyAstNode* guard;           // optional if-guard expression
    PyAstNode* body;            // body block/statement
} PyCaseNode;
