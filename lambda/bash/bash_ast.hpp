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

// Bash AST Node Types
typedef enum BashAstNodeType {
    BASH_AST_NODE_NULL,

    // program
    BASH_AST_NODE_PROGRAM,

    // statements / commands
    BASH_AST_NODE_COMMAND,              // simple command: cmd arg1 arg2
    BASH_AST_NODE_PIPELINE,             // cmd1 | cmd2 | cmd3
    BASH_AST_NODE_LIST,                 // cmd1 && cmd2 || cmd3 ; cmd4
    BASH_AST_NODE_COMPOUND_STATEMENT,   // { cmd1; cmd2; }
    BASH_AST_NODE_SUBSHELL,             // ( cmd1; cmd2 )

    // control flow
    BASH_AST_NODE_IF,                   // if/elif/else/fi
    BASH_AST_NODE_ELIF,                 // elif clause
    BASH_AST_NODE_ELSE,                 // else clause
    BASH_AST_NODE_FOR,                  // for var in list; do ...; done
    BASH_AST_NODE_FOR_ARITHMETIC,       // for (( i=0; i<10; i++ ))
    BASH_AST_NODE_WHILE,               // while cond; do ...; done
    BASH_AST_NODE_UNTIL,                // until cond; do ...; done
    BASH_AST_NODE_CASE,                 // case $var in ... esac
    BASH_AST_NODE_CASE_ITEM,            // pattern) commands ;;
    BASH_AST_NODE_FUNCTION_DEF,         // function foo() { }

    // expressions / words
    BASH_AST_NODE_WORD,                 // plain word / literal string
    BASH_AST_NODE_STRING,               // "double quoted" or $'...'
    BASH_AST_NODE_RAW_STRING,           // 'single quoted' (no expansion)
    BASH_AST_NODE_CONCATENATION,        // adjacent words: "$a"_"$b"
    BASH_AST_NODE_VARIABLE_REF,         // $var, ${var}
    BASH_AST_NODE_SPECIAL_VARIABLE,     // $?, $#, $@, $*, $$, $!, $0, $1-$9
    BASH_AST_NODE_EXPANSION,            // ${var:-default}, ${var##pat}, etc.
    BASH_AST_NODE_COMMAND_SUB,          // $(command) or `command`
    BASH_AST_NODE_ARITHMETIC_EXPR,      // $(( expr ))
    BASH_AST_NODE_GLOB,                 // *, ?, [a-z]

    // arithmetic sub-expressions (inside $(( )))
    BASH_AST_NODE_ARITH_BINARY,         // a + b, a * b, etc.
    BASH_AST_NODE_ARITH_UNARY,          // -a, !a, ~a
    BASH_AST_NODE_ARITH_TERNARY,        // a ? b : c
    BASH_AST_NODE_ARITH_ASSIGN,         // a = b, a += b, a++, a--
    BASH_AST_NODE_ARITH_NUMBER,         // literal number
    BASH_AST_NODE_ARITH_VARIABLE,       // variable reference

    // test expressions
    BASH_AST_NODE_TEST_COMMAND,         // [ expr ] or test expr
    BASH_AST_NODE_EXTENDED_TEST,        // [[ expr ]]
    BASH_AST_NODE_TEST_BINARY,          // a -eq b, a == b, etc.
    BASH_AST_NODE_TEST_UNARY,           // -z str, -f file, -n str
    BASH_AST_NODE_TEST_LOGICAL,         // expr && expr, expr || expr, ! expr

    // arrays
    BASH_AST_NODE_ARRAY_LITERAL,        // (a b c)
    BASH_AST_NODE_ARRAY_ACCESS,         // ${arr[idx]}
    BASH_AST_NODE_ARRAY_ALL,            // ${arr[@]} / ${arr[*]}
    BASH_AST_NODE_ARRAY_SLICE,          // ${arr[@]:off:len}
    BASH_AST_NODE_ARRAY_LENGTH,         // ${#arr[@]}
    BASH_AST_NODE_ARRAY_KEYS,           // ${!arr[@]} (keys of assoc array)

    // assignments
    BASH_AST_NODE_ASSIGNMENT,           // var=value
    BASH_AST_NODE_ARRAY_ASSIGN,         // arr=(a b c)
    BASH_AST_NODE_COMPOUND_ASSIGN,      // var+=value

    // redirections
    BASH_AST_NODE_REDIRECT,             // > >> < << 2>&1
    BASH_AST_NODE_HEREDOC,              // <<EOF ... EOF
    BASH_AST_NODE_HERESTRING,           // <<< "string"

    // control
    BASH_AST_NODE_RETURN,               // return [n]
    BASH_AST_NODE_BREAK,                // break [n]
    BASH_AST_NODE_CONTINUE,             // continue [n]
    BASH_AST_NODE_EXIT,                 // exit [n]

    // argument list (for function calls, builtins)
    BASH_AST_NODE_ARGUMENT_LIST,

    // block of statements (body of if/for/while/function)
    BASH_AST_NODE_BLOCK,

    // redirected wrapper (for pipelines/compounds with file redirects)
    BASH_AST_NODE_REDIRECTED,

    BASH_AST_NODE_COUNT
} BashAstNodeType;

// Bash operators (arithmetic context)
typedef enum BashOperator {
    // arithmetic operators
    BASH_OP_ADD,            // +
    BASH_OP_SUB,            // -
    BASH_OP_MUL,            // *
    BASH_OP_DIV,            // /
    BASH_OP_MOD,            // %
    BASH_OP_POW,            // **
    BASH_OP_NEGATE,         // unary -
    BASH_OP_POSITIVE,       // unary +
    BASH_OP_BIT_NOT,        // ~
    BASH_OP_LOGICAL_NOT,    // !
    BASH_OP_LSHIFT,         // <<
    BASH_OP_RSHIFT,         // >>
    BASH_OP_BIT_AND,        // &
    BASH_OP_BIT_OR,         // |
    BASH_OP_BIT_XOR,        // ^
    BASH_OP_LOGICAL_AND,    // &&
    BASH_OP_LOGICAL_OR,     // ||

    // comparison operators (arithmetic context)
    BASH_OP_EQ,             // ==
    BASH_OP_NE,             // !=
    BASH_OP_LT,             // <
    BASH_OP_LE,             // <=
    BASH_OP_GT,             // >
    BASH_OP_GE,             // >=

    // assignment operators (arithmetic context)
    BASH_OP_ASSIGN,         // =
    BASH_OP_ADD_ASSIGN,     // +=
    BASH_OP_SUB_ASSIGN,     // -=
    BASH_OP_MUL_ASSIGN,     // *=
    BASH_OP_DIV_ASSIGN,     // /=
    BASH_OP_MOD_ASSIGN,     // %=
    BASH_OP_INC,            // ++ (postfix/prefix)
    BASH_OP_DEC,            // -- (postfix/prefix)

    // ternary
    BASH_OP_TERNARY,        // ? :

    BASH_OP_COUNT
} BashOperator;

// Bash test operators
typedef enum BashTestOp {
    // numeric comparisons ([ ] / [[ ]])
    BASH_TEST_EQ,           // -eq
    BASH_TEST_NE,           // -ne
    BASH_TEST_GT,           // -gt
    BASH_TEST_GE,           // -ge
    BASH_TEST_LT,           // -lt
    BASH_TEST_LE,           // -le

    // string comparisons ([[ ]])
    BASH_TEST_STR_EQ,       // == or =
    BASH_TEST_STR_NE,       // !=
    BASH_TEST_STR_LT,       // <
    BASH_TEST_STR_GT,       // >
    BASH_TEST_STR_MATCH,    // =~ (regex)
    BASH_TEST_STR_GLOB,     // == with glob pattern

    // unary tests
    BASH_TEST_Z,            // -z (empty string)
    BASH_TEST_N,            // -n (non-empty string)
    BASH_TEST_F,            // -f (regular file)
    BASH_TEST_D,            // -d (directory)
    BASH_TEST_E,            // -e (exists)
    BASH_TEST_R,            // -r (readable)
    BASH_TEST_W,            // -w (writable)
    BASH_TEST_X,            // -x (executable)
    BASH_TEST_S,            // -s (non-zero size)
    BASH_TEST_L,            // -L (symlink)

    // logical
    BASH_TEST_AND,          // &&
    BASH_TEST_OR,           // ||
    BASH_TEST_NOT,          // !

    BASH_TEST_COUNT
} BashTestOp;

// Bash parameter expansion types
typedef enum BashExpansionType {
    BASH_EXPAND_DEFAULT,            // ${var:-default}
    BASH_EXPAND_ASSIGN_DEFAULT,     // ${var:=default}
    BASH_EXPAND_ALT,                // ${var:+alt}
    BASH_EXPAND_ERROR,              // ${var:?msg}
    BASH_EXPAND_LENGTH,             // ${#var}
    BASH_EXPAND_TRIM_PREFIX,        // ${var#pat}
    BASH_EXPAND_TRIM_PREFIX_LONG,   // ${var##pat}
    BASH_EXPAND_TRIM_SUFFIX,        // ${var%pat}
    BASH_EXPAND_TRIM_SUFFIX_LONG,   // ${var%%pat}
    BASH_EXPAND_REPLACE,            // ${var/pat/str}
    BASH_EXPAND_REPLACE_ALL,        // ${var//pat/str}
    BASH_EXPAND_UPPER_FIRST,        // ${var^}
    BASH_EXPAND_UPPER_ALL,          // ${var^^}
    BASH_EXPAND_LOWER_FIRST,        // ${var,}
    BASH_EXPAND_LOWER_ALL,          // ${var,,}
    BASH_EXPAND_SUBSTRING,          // ${var:off:len}
    BASH_EXPAND_INDIRECT,           // ${!var}

    BASH_EXPAND_COUNT
} BashExpansionType;

// list connector operators
typedef enum BashListOp {
    BASH_LIST_AND,          // &&
    BASH_LIST_OR,           // ||
    BASH_LIST_SEMI,         // ;
    BASH_LIST_BG,           // &
} BashListOp;

// redirect modes
typedef enum BashRedirectMode {
    BASH_REDIR_READ,        // <
    BASH_REDIR_WRITE,       // >
    BASH_REDIR_APPEND,      // >>
    BASH_REDIR_DUP,         // >&
    BASH_REDIR_HEREDOC,     // <<
    BASH_REDIR_HERESTRING,  // <<<
} BashRedirectMode;

// -----------------------------------------------------------------------
// AST Node structs
// -----------------------------------------------------------------------

// Base Bash AST node
// NOTE: Field order must match AstNode layout (node_type, type, next, node)
typedef struct BashAstNode {
    BashAstNodeType node_type;
    Type* type;                     // inferred Lambda type (reserved)
    struct BashAstNode* next;       // linked list for siblings
    TSNode node;                    // Tree-sitter node
} BashAstNode;

// Program (root node)
typedef struct BashProgramNode {
    BashAstNode base;
    BashAstNode* body;              // top-level statements (linked list)
} BashProgramNode;

// Simple command: name arg1 arg2 ...
typedef struct BashCommandNode {
    BashAstNode base;
    BashAstNode* name;              // command name word
    BashAstNode* args;              // arguments (linked list)
    int arg_count;
    BashAstNode* redirects;         // redirections (linked list)
    BashAstNode* assignments;       // prefix assignments: VAR=val cmd
} BashCommandNode;

// Pipeline: cmd1 | cmd2 | cmd3
typedef struct BashPipelineNode {
    BashAstNode base;
    BashAstNode* commands;          // linked list of commands
    int command_count;
    bool negated;                   // ! pipeline
} BashPipelineNode;

// Redirected wrapper: wraps any statement (pipeline, etc.) with file redirects
typedef struct BashRedirectedNode {
    BashAstNode base;
    BashAstNode* inner;             // wrapped statement (pipeline, compound, etc.)
    BashAstNode* redirects;         // linked list of BashRedirectNode
} BashRedirectedNode;

// List: cmd1 && cmd2 || cmd3 ; cmd4
typedef struct BashListNode {
    BashAstNode base;
    BashAstNode* left;
    BashAstNode* right;
    BashListOp op;
} BashListNode;

// Subshell: ( commands )
typedef struct BashSubshellNode {
    BashAstNode base;
    BashAstNode* body;
} BashSubshellNode;

// Compound statement: { commands; }
typedef struct BashCompoundNode {
    BashAstNode base;
    BashAstNode* body;
} BashCompoundNode;

// If statement
typedef struct BashIfNode {
    BashAstNode base;
    BashAstNode* condition;         // test command or pipeline
    BashAstNode* then_body;
    BashAstNode* elif_clauses;      // linked list of BashElifNode
    BashAstNode* else_body;         // optional
} BashIfNode;

// Elif clause
typedef struct BashElifNode {
    BashAstNode base;
    BashAstNode* condition;
    BashAstNode* body;
} BashElifNode;

// For loop: for var in words; do body; done
typedef struct BashForNode {
    BashAstNode base;
    String* variable;               // loop variable name
    BashAstNode* words;             // iteration list (linked list)
    BashAstNode* body;
} BashForNode;

// C-style for loop: for (( init; cond; step )); do body; done
typedef struct BashForArithNode {
    BashAstNode base;
    BashAstNode* init;              // initializer (arithmetic expr)
    BashAstNode* condition;         // condition (arithmetic expr)
    BashAstNode* step;              // update (arithmetic expr)
    BashAstNode* body;
} BashForArithNode;

// While loop
typedef struct BashWhileNode {
    BashAstNode base;
    BashAstNode* condition;
    BashAstNode* body;
} BashWhileNode;

// Until loop
typedef struct BashUntilNode {
    BashAstNode base;
    BashAstNode* condition;
    BashAstNode* body;
} BashUntilNode;

// Case statement
typedef struct BashCaseNode {
    BashAstNode base;
    BashAstNode* word;              // value being matched
    BashAstNode* items;             // linked list of BashCaseItemNode
} BashCaseNode;

// Case item: pattern) commands ;;
typedef struct BashCaseItemNode {
    BashAstNode base;
    BashAstNode* patterns;          // linked list of pattern words
    BashAstNode* body;              // commands
    int terminator;                 // 0=;;  1=;&  2=;;&
} BashCaseItemNode;

// Function definition
typedef struct BashFunctionDefNode {
    BashAstNode base;
    String* name;
    BashAstNode* body;              // function body (compound statement)
} BashFunctionDefNode;

// variable attribute flags for declare/typeset
typedef enum BashVarAttrFlags {
    BASH_ATTR_NONE         = 0,
    BASH_ATTR_READONLY     = 1 << 0,   // -r
    BASH_ATTR_INTEGER      = 1 << 1,   // -i
    BASH_ATTR_LOWERCASE    = 1 << 2,   // -l
    BASH_ATTR_UPPERCASE    = 1 << 3,   // -u
    BASH_ATTR_INDEXED_ARRAY = 1 << 4,  // -a
    BASH_ATTR_ASSOC_ARRAY  = 1 << 5,   // -A
    BASH_ATTR_EXPORT       = 1 << 6,   // -x
    BASH_ATTR_PRINT        = 1 << 7,   // -p
    BASH_ATTR_NAMEREF      = 1 << 8,   // -n (nameref, not fully supported)
} BashVarAttrFlags;

// Variable assignment: name=value
typedef struct BashAssignmentNode {
    BashAstNode base;
    String* name;
    BashAstNode* value;             // right-hand side (word or expression)
    BashAstNode* index;             // array index (for arr[idx]=val)
    bool is_local;                  // declared with `local`
    bool is_export;                 // declared with `export`
    bool is_append;                 // += compound assignment
    int declare_flags;              // BashVarAttrFlags from declare/typeset
} BashAssignmentNode;

// Word: plain text literal
typedef struct BashWordNode {
    BashAstNode base;
    String* text;
    bool no_backslash_escape;   // true if text has already been processed (e.g. from string fragments)
} BashWordNode;

// String: "double quoted" (with expansion)
typedef struct BashStringNode {
    BashAstNode base;
    BashAstNode* parts;             // linked list of literals and expansions
} BashStringNode;

// Raw string: 'single quoted' (no expansion)
typedef struct BashRawStringNode {
    BashAstNode base;
    String* text;
} BashRawStringNode;

// Concatenation: adjacent words joined
typedef struct BashConcatNode {
    BashAstNode base;
    BashAstNode* parts;             // linked list of parts
} BashConcatNode;

// Variable reference: $var or ${var}
typedef struct BashVarRefNode {
    BashAstNode base;
    String* name;
} BashVarRefNode;

// Special variable: $?, $#, $@, $*, $$, $!, $0, $1-$9
typedef struct BashSpecialVarNode {
    BashAstNode base;
    int special_id;                 // which special variable
} BashSpecialVarNode;

// special variable IDs
#define BASH_SPECIAL_QUESTION   0   // $?  — exit code
#define BASH_SPECIAL_HASH       1   // $#  — argument count
#define BASH_SPECIAL_AT         2   // $@  — all arguments (separate words)
#define BASH_SPECIAL_STAR       3   // $*  — all arguments (single word)
#define BASH_SPECIAL_DOLLAR     4   // $$  — PID
#define BASH_SPECIAL_BANG       5   // $!  — last background PID
#define BASH_SPECIAL_DASH       6   // $-  — shell flags
#define BASH_SPECIAL_ZERO       7   // $0  — script name
#define BASH_SPECIAL_POS_1      8   // $1
#define BASH_SPECIAL_POS_2      9   // $2
#define BASH_SPECIAL_POS_3      10  // $3
#define BASH_SPECIAL_POS_4      11  // $4
#define BASH_SPECIAL_POS_5      12  // $5
#define BASH_SPECIAL_POS_6      13  // $6
#define BASH_SPECIAL_POS_7      14  // $7
#define BASH_SPECIAL_POS_8      15  // $8
#define BASH_SPECIAL_POS_9      16  // $9

// Parameter expansion: ${var:-default}, ${var##pat}, etc.
typedef struct BashExpansionNode {
    BashAstNode base;
    String* variable;
    BashExpansionType expand_type;
    BashAstNode* argument;          // default/pattern/replacement
    BashAstNode* replacement;       // for ${var/pat/str}: the replacement string
    bool has_colon;                 // true for :- := :+ :?, false for - = + ?
    BashAstNode* inner_expr;        // if set, evaluate this instead of bash_get_var(variable)
} BashExpansionNode;

// Command substitution: $(command) or `command`
typedef struct BashCommandSubNode {
    BashAstNode base;
    BashAstNode* body;              // commands inside substitution
} BashCommandSubNode;

// Arithmetic expression: $(( expr ))
typedef struct BashArithExprNode {
    BashAstNode base;
    BashAstNode* expression;        // the arithmetic sub-expression tree
} BashArithExprNode;

// Arithmetic binary: a + b
typedef struct BashArithBinaryNode {
    BashAstNode base;
    BashOperator op;
    BashAstNode* left;
    BashAstNode* right;
} BashArithBinaryNode;

// Arithmetic unary: -a, !a, ++a, a++
typedef struct BashArithUnaryNode {
    BashAstNode base;
    BashOperator op;
    BashAstNode* operand;
    bool prefix;                    // true for prefix ops (++a), false for postfix (a++)
} BashArithUnaryNode;

// Arithmetic number literal
typedef struct BashArithNumberNode {
    BashAstNode base;
    int64_t value;
} BashArithNumberNode;

// Arithmetic variable reference
typedef struct BashArithVariableNode {
    BashAstNode base;
    String* name;
} BashArithVariableNode;

// Arithmetic assignment/update: i=0, i+=1, i++, i--
typedef struct BashArithAssignNode {
    BashAstNode base;
    String* name;                   // variable being assigned
    BashOperator op;                // BASH_OP_ASSIGN, BASH_OP_INC, BASH_OP_DEC, etc.
    BashAstNode* value;             // right-hand side (NULL for ++/--)
} BashArithAssignNode;

// Test command: [ expr ] or test expr
typedef struct BashTestCommandNode {
    BashAstNode base;
    BashAstNode* expression;        // test expression tree
} BashTestCommandNode;

// Extended test: [[ expr ]]
typedef struct BashExtendedTestNode {
    BashAstNode base;
    BashAstNode* expression;        // test expression tree
} BashExtendedTestNode;

// Test binary: a -eq b, a == b
typedef struct BashTestBinaryNode {
    BashAstNode base;
    BashTestOp op;
    BashAstNode* left;
    BashAstNode* right;
} BashTestBinaryNode;

// Test unary: -z str, -f file, -n str
typedef struct BashTestUnaryNode {
    BashAstNode base;
    BashTestOp op;
    BashAstNode* operand;
} BashTestUnaryNode;

// Test logical: expr && expr, expr || expr, ! expr
typedef struct BashTestLogicalNode {
    BashAstNode base;
    BashTestOp op;
    BashAstNode* left;
    BashAstNode* right;             // NULL for unary !
} BashTestLogicalNode;

// Array literal: (a b c)
typedef struct BashArrayLiteralNode {
    BashAstNode base;
    BashAstNode* elements;          // linked list of words
    int length;
} BashArrayLiteralNode;

// Array access: ${arr[idx]}
typedef struct BashArrayAccessNode {
    BashAstNode base;
    String* name;                   // array variable name
    BashAstNode* index;             // index expression
} BashArrayAccessNode;

// Array all: ${arr[@]} or ${arr[*]}
typedef struct BashArrayAllNode {
    BashAstNode base;
    String* name;                   // array variable name
} BashArrayAllNode;

// Array keys: ${!arr[@]}
typedef struct BashArrayKeysNode {
    BashAstNode base;
    String* name;                   // array variable name
} BashArrayKeysNode;

// Array slice: ${arr[@]:offset:length}
typedef struct BashArraySliceNode {
    BashAstNode base;
    String* name;                   // array variable name
    BashAstNode* offset;            // offset expression
    BashAstNode* length;            // length expression (optional)
} BashArraySliceNode;

// Array length: ${#arr[@]}
typedef struct BashArrayLengthNode {
    BashAstNode base;
    String* name;                   // array variable name
} BashArrayLengthNode;

// Redirect: > file, >> file, < file, 2>&1
typedef struct BashRedirectNode {
    BashAstNode base;
    int fd;                         // file descriptor (0=stdin, 1=stdout, 2=stderr)
    BashRedirectMode mode;
    BashAstNode* target;            // filename word or fd number word
} BashRedirectNode;

// Heredoc: <<EOF ... EOF
typedef struct BashHeredocNode {
    BashAstNode base;
    String* delimiter;
    BashAstNode* body;              // content (with or without expansion)
    bool expand;                    // true if delimiter is unquoted
} BashHeredocNode;

// Return/break/continue/exit
typedef struct BashControlNode {
    BashAstNode base;
    BashAstNode* value;             // optional return value / exit code / nesting level
} BashControlNode;

// Block of statements
typedef struct BashBlockNode {
    BashAstNode base;
    BashAstNode* statements;        // linked list
} BashBlockNode;
