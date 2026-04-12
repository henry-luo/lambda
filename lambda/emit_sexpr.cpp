// emit_sexpr.cpp — Emit Lambda AST as Redex-compatible s-expressions
// Phase 4 of Lambda formal semantics: test baseline verification bridge
//
// Parses a Lambda .ls source file, builds the AST, then walks the AST
// tree and emits s-expressions to stdout in a format that the Racket
// bridge (ast-bridge.rkt) can directly read and feed to eval-lambda
// or eval-proc.
//
#include "../lib/memtrack.h"

// Top-level format:
//   (script "fn" form ...)      ; functional script
//   (script "pn" form ...)      ; procedural script (has pn main)
//
// Top-level forms:
//   (bind name expr)            ; let binding
//   (fn-def name (params) body) ; named fn definition
//   (pn-def name (params) body) ; named pn definition
//   (type-def name ...)         ; type definition
//   expr                        ; expression (produces output)

#include "emit_sexpr.h"
#include "transpiler.hpp"
#include "lambda-decimal.hpp"
#include "../lib/file.h"
#include "../lib/log.h"

// from parse.c (C linkage)
extern "C" {
    TSParser* lambda_parser(void);
    TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
}

// forward declarations
static void emit_expr(const char* source, AstNode* node);
static void emit_top_level(const char* source, AstNode* child);
static void emit_object_fields(const char* source, AstObjectTypeNode* obj_type);

// get source text for a TSNode
static inline const char* node_src(const char* source, TSNode node, int* out_len) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    *out_len = (int)(end - start);
    return source + start;
}

// emit a Racket-safe escaped string
static void emit_escaped_string(const char* str, int len) {
    putchar('"');
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        switch (c) {
            case '"':  printf("\\\""); break;
            case '\\': printf("\\\\"); break;
            case '\n': printf("\\n"); break;
            case '\r': printf("\\r"); break;
            case '\t': printf("\\t"); break;
            case '\0': printf("\\0"); break;
            default:
                if (c < 0x20) {
                    printf("\\x%02x", c);
                } else {
                    putchar(c);
                }
        }
    }
    putchar('"');
}

// map binary operator to Redex name
static const char* binary_op_name(Operator op) {
    switch (op) {
        case OPERATOR_ADD:  return "add";
        case OPERATOR_SUB:  return "sub";
        case OPERATOR_MUL:  return "mul";
        case OPERATOR_DIV:  return "fdiv";
        case OPERATOR_IDIV: return "idiv";
        case OPERATOR_MOD:  return "mod";
        case OPERATOR_POW:  return "pow";
        case OPERATOR_EQ:   return "eq";
        case OPERATOR_NE:   return "neq";
        case OPERATOR_LT:   return "lt";
        case OPERATOR_LE:   return "le";
        case OPERATOR_GT:   return "gt";
        case OPERATOR_GE:   return "ge";
        case OPERATOR_AND:  return "l-and";
        case OPERATOR_OR:   return "l-or";
        case OPERATOR_JOIN: return "concat";
        case OPERATOR_TO:   return "to-range";
        case OPERATOR_IS:   return "is-type";
        case OPERATOR_IN:   return "in-coll";
        default:            return NULL;
    }
}

// map LMD_TYPE_* type_id to Redex type name (with -type suffix)
static const char* redex_type_name(TypeId type_id) {
    switch (type_id) {
        case LMD_TYPE_NULL:   return "null-type";
        case LMD_TYPE_BOOL:   return "bool-type";
        case LMD_TYPE_INT:    return "int-type";
        case LMD_TYPE_INT64:  return "int-type";
        case LMD_TYPE_UINT64: return "int-type";
        case LMD_TYPE_FLOAT:  return "float-type";
        case LMD_TYPE_NUMBER: return "number-type";
        case LMD_TYPE_DECIMAL:return "number-type";
        case LMD_TYPE_STRING: return "string-type";
        case LMD_TYPE_SYMBOL: return "symbol-type";
        case LMD_TYPE_ARRAY:  return "array-type";
        case LMD_TYPE_RANGE:  return "range-type";
        case LMD_TYPE_MAP:    return "map-type";
        case LMD_TYPE_FUNC:   return "func-type";
        case LMD_TYPE_ERROR:  return "error-type";
        case LMD_TYPE_ANY:    return "any-type";
        case LMD_TYPE_OBJECT: return "object-type";
        case LMD_TYPE_ELEMENT:return "element-type";
        default:              return NULL;
    }
}

// emit type expression for is-type / as-type operations
static void emit_type_expr(const char* source, AstNode* node) {
    if (!node) { printf("any-type"); return; }
    if (node->node_type == AST_NODE_TYPE) {
        // base type: get the type name from the type_id
        if (node->type && node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)node->type;
            if (tt->type) {
                // check source text to distinguish list vs array (both map to LMD_TYPE_ARRAY)
                int slen; const char* ssrc = node_src(source, node->node, &slen);
                if (slen == 4 && strncmp(ssrc, "list", 4) == 0) {
                    printf("list-type"); return;
                }
                const char* rname = redex_type_name(tt->type->type_id);
                if (rname) { printf("%s", rname); return; }
                printf("%s-type", type_info[tt->type->type_id].name);
                return;
            }
        }
        // fallback: use source text with -type suffix
        int len; const char* src = node_src(source, node->node, &len);
        printf("%.*s-type", len, src);
    }
    else if (node->node_type == AST_NODE_BINARY_TYPE) {
        // union type: T | U
        AstBinaryNode* bn = (AstBinaryNode*)node;
        if (bn->op == OPERATOR_UNION) {
            printf("(union ");
            emit_type_expr(source, bn->left);
            printf(" ");
            emit_type_expr(source, bn->right);
            printf(")");
        } else {
            int len; const char* src = node_src(source, node->node, &len);
            printf("%.*s", len, src);
        }
    }
    else if (node->node_type == AST_NODE_IDENT) {
        // named type (e.g., Point, Circle) or built-in type name (string, int, etc.)
        AstIdentNode* id = (AstIdentNode*)node;
        const char* name = id->name->chars;
        int len = (int)id->name->len;
        // Check if it's a known built-in type that needs -type suffix
        static const char* builtin_types[] = {
            "null", "bool", "int", "float", "number", "string",
            "symbol", "array", "map", "func", "error", "any",
            "object", "range", "list", "type", NULL
        };
        bool is_builtin = false;
        for (int i = 0; builtin_types[i]; i++) {
            if (len == (int)strlen(builtin_types[i]) && strncmp(name, builtin_types[i], len) == 0) {
                is_builtin = true;
                break;
            }
        }
        if (is_builtin) {
            printf("%.*s-type", len, name);
        } else {
            printf("%.*s", len, name);
        }
    }
    else if (node->node_type == AST_NODE_CONSTRAINED_TYPE) {
        AstConstrainedTypeNode* ct = (AstConstrainedTypeNode*)node;
        printf("(constrained ");
        emit_type_expr(source, ct->base);
        printf(" ");
        emit_expr(source, ct->constraint);
        printf(")");
    }
    else if (node->node_type == AST_NODE_UNARY_TYPE) {
        // nullable type: T?
        AstUnaryNode* un = (AstUnaryNode*)node;
        printf("(nullable ");
        emit_type_expr(source, un->operand);
        printf(")");
    }
    else if (node->node_type == AST_NODE_PRIMARY) {
        // primary node used as type in is-type expression
        if (node->type && node->type->type_id == LMD_TYPE_TYPE) {
            // built-in type keyword (string, int, bool, etc.)
            TypeType* tt = (TypeType*)node->type;
            if (tt->type) {
                // check source to distinguish list vs array
                int slen; const char* ssrc = node_src(source, node->node, &slen);
                if (slen == 4 && strncmp(ssrc, "list", 4) == 0) {
                    printf("list-type"); return;
                }
                const char* rname = redex_type_name(tt->type->type_id);
                if (rname) { printf("%s", rname); return; }
                printf("%s-type", type_info[tt->type->type_id].name);
                return;
            }
        }
        // not a type — emit as value expression
        emit_expr(source, node);
    }
    else {
        // fallback: emit as expression (handles arrays, literals, etc.)
        emit_expr(source, node);
    }
}

// emit system function call as Redex form
static bool emit_sys_func_call(const char* source, AstCallNode* call) {
    AstSysFuncNode* sys_node = (AstSysFuncNode*)call->function;
    const char* name = sys_node->fn_info->name;
    AstNode* arg1 = call->argument;
    AstNode* arg2 = arg1 ? arg1->next : NULL;

    // map to Redex built-in forms
    if (strcmp(name, "len") == 0 && arg1) {
        printf("(len-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "sum") == 0 && arg1) {
        printf("(sum-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "sort") == 0 && arg1) {
        if (arg2) {
            printf("(sort-expr "); emit_expr(source, arg1);
            printf(" "); emit_expr(source, arg2); printf(")");
        } else {
            printf("(sort-expr "); emit_expr(source, arg1); printf(")");
        }
        return true;
    }
    if (strcmp(name, "reverse") == 0 && arg1) {
        printf("(reverse-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "unique") == 0 && arg1) {
        printf("(unique-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "take") == 0 && arg1 && arg2) {
        printf("(take-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "drop") == 0 && arg1 && arg2) {
        printf("(drop-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    // type conversion functions
    if (strcmp(name, "int") == 0 && arg1) {
        printf("(to-int "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "float") == 0 && arg1) {
        printf("(to-float "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "string") == 0 && arg1) {
        printf("(to-string "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "bool") == 0 && arg1) {
        printf("(to-bool "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "symbol") == 0 && arg1) {
        printf("(to-symbol "); emit_expr(source, arg1); printf(")"); return true;
    }
    // error construction
    if (strcmp(name, "error") == 0 && arg1 && !arg2) {
        printf("(make-error "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "error") == 0 && arg1 && arg2) {
        printf("(make-error-2 "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    // print (procedural)
    if (strcmp(name, "print") == 0 && arg1) {
        printf("(print "); emit_expr(source, arg1); printf(")"); return true;
    }
    // type-of
    if (strcmp(name, "type") == 0 && arg1) {
        printf("(type-of "); emit_expr(source, arg1); printf(")"); return true;
    }
    // min / max
    if (strcmp(name, "min") == 0 && arg1 && arg2) {
        printf("(min-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "max") == 0 && arg1 && arg2) {
        printf("(max-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    // abs
    if (strcmp(name, "abs") == 0 && arg1) {
        printf("(abs-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    // keys / values
    if (strcmp(name, "keys") == 0 && arg1) {
        printf("(keys-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "values") == 0 && arg1) {
        printf("(values-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    // contains
    if (strcmp(name, "contains") == 0 && arg1 && arg2) {
        printf("(contains-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    // flatten
    if (strcmp(name, "flatten") == 0 && arg1) {
        printf("(flatten-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    // map / filter / reduce
    if (strcmp(name, "map") == 0 && arg1 && arg2) {
        printf("(map-fn "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "filter") == 0 && arg1 && arg2) {
        printf("(filter-fn "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "reduce") == 0 && arg1 && arg2) {
        AstNode* arg3 = arg2 ? arg2->next : NULL;
        if (arg3) {
            printf("(reduce-fn "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(" "); emit_expr(source, arg3); printf(")");
        } else {
            printf("(reduce-fn "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")");
        }
        return true;
    }
    // math functions
    if (strcmp(name, "round") == 0 && arg1) {
        printf("(round-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "floor") == 0 && arg1) {
        printf("(floor-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "ceil") == 0 && arg1) {
        printf("(ceil-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "sqrt") == 0 && arg1) {
        printf("(sqrt-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "pow") == 0 && arg1 && arg2) {
        printf("(pow "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "log") == 0 && arg1) {
        printf("(log-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "sin") == 0 && arg1) {
        printf("(sin-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "cos") == 0 && arg1) {
        printf("(cos-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "tan") == 0 && arg1) {
        printf("(tan-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "is_nan") == 0 && arg1) {
        printf("(is-nan "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "is_int") == 0 && arg1) {
        printf("(is-type "); emit_expr(source, arg1); printf(" int-type)"); return true;
    }
    if (strcmp(name, "is_float") == 0 && arg1) {
        printf("(is-type "); emit_expr(source, arg1); printf(" float-type)"); return true;
    }
    if (strcmp(name, "avg") == 0 && arg1) {
        printf("(avg-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    // string functions
    if (strcmp(name, "split") == 0 && arg1 && arg2) {
        printf("(split-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "join") == 0 && arg1) {
        if (arg2) {
            printf("(join-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")");
        } else {
            printf("(join-expr "); emit_expr(source, arg1); printf(" \"\")");
        }
        return true;
    }
    if (strcmp(name, "trim") == 0 && arg1) {
        printf("(trim-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "trim_start") == 0 && arg1) {
        printf("(trim-start-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "trim_end") == 0 && arg1) {
        printf("(trim-end-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "starts_with") == 0 && arg1 && arg2) {
        printf("(starts-with "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "ends_with") == 0 && arg1 && arg2) {
        printf("(ends-with "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "replace") == 0 && arg1 && arg2) {
        AstNode* arg3a = arg2 ? arg2->next : NULL;
        if (arg3a) {
            printf("(replace-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(" "); emit_expr(source, arg3a); printf(")");
        } else {
            printf("(replace-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(" \"\")");
        }
        return true;
    }
    if (strcmp(name, "index_of") == 0 && arg1 && arg2) {
        printf("(index-of-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "slice") == 0 && arg1 && arg2) {
        AstNode* arg3b = arg2 ? arg2->next : NULL;
        if (arg3b) {
            printf("(slice-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(" "); emit_expr(source, arg3b); printf(")");
        } else {
            printf("(slice-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")");
        }
        return true;
    }
    if (strcmp(name, "upper") == 0 && arg1) {
        printf("(upper-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "lower") == 0 && arg1) {
        printf("(lower-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "ord") == 0 && arg1) {
        printf("(ord-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "chr") == 0 && arg1) {
        printf("(chr-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "name") == 0 && arg1) {
        printf("(name-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "int64") == 0 && arg1) {
        printf("(to-int64 "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "not") == 0 && arg1) {
        printf("(l-not "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "head") == 0 && arg1) {
        printf("(head-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "last") == 0 && arg1) {
        printf("(last-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "tail") == 0 && arg1) {
        printf("(tail-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "init") == 0 && arg1) {
        printf("(init-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "zip") == 0 && arg1 && arg2) {
        printf("(zip-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "enumerate") == 0 && arg1) {
        printf("(enumerate-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "range") == 0 && arg1) {
        if (arg2) {
            printf("(range-fn "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")");
        } else {
            printf("(range-fn 0 "); emit_expr(source, arg1); printf(")");
        }
        return true;
    }
    // bitwise operations
    if (strcmp(name, "band") == 0 && arg1 && arg2) {
        printf("(bit-and "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "bor") == 0 && arg1 && arg2) {
        printf("(bit-or "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "bxor") == 0 && arg1 && arg2) {
        printf("(bit-xor "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "bnot") == 0 && arg1) {
        printf("(bit-not "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "shl") == 0 && arg1 && arg2) {
        printf("(bit-shl "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "shr") == 0 && arg1 && arg2) {
        printf("(bit-shr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }

    // sign / trunc
    if (strcmp(name, "sign") == 0 && arg1) {
        printf("(sign-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "trunc") == 0 && arg1) {
        printf("(trunc-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    // find / exists (higher-order)
    if (strcmp(name, "find") == 0 && arg1 && arg2) {
        printf("(find-fn "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "exists") == 0 && arg1 && arg2) {
        printf("(exists-fn "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    // fill
    if (strcmp(name, "fill") == 0 && arg1 && arg2) {
        printf("(fill-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    // last_index_of
    if (strcmp(name, "last_index_of") == 0 && arg1 && arg2) {
        printf("(last-index-of-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    // apply
    if (strcmp(name, "apply") == 0 && arg1 && arg2) {
        printf("(apply-fn "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    // decimal
    if (strcmp(name, "decimal") == 0 && arg1) {
        printf("(to-decimal "); emit_expr(source, arg1); printf(")"); return true;
    }
    // format
    if (strcmp(name, "format") == 0 && arg1 && arg2) {
        printf("(format-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    // input / parse (string → data)
    if (strcmp(name, "input") == 0 && arg1) {
        if (arg2) {
            printf("(input-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")");
        } else {
            printf("(input-expr "); emit_expr(source, arg1); printf(")");
        }
        return true;
    }
    if (strcmp(name, "parse") == 0 && arg1) {
        printf("(parse-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    // set (proc mutation on array/map)
    if (strcmp(name, "set") == 0 && arg1 && arg2) {
        AstNode* arg3 = arg2->next;
        if (arg3) {
            printf("(set-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(" "); emit_expr(source, arg3); printf(")");
        } else {
            printf("(set-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")");
        }
        return true;
    }
    // argmin / argmax
    if (strcmp(name, "argmin") == 0 && arg1 && arg2) {
        printf("(argmin-fn "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "argmax") == 0 && arg1 && arg2) {
        printf("(argmax-fn "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    // varg (variadic arg list)
    if (strcmp(name, "varg") == 0 && arg1) {
        printf("(varg-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    // clock / time / date
    if (strcmp(name, "clock") == 0) {
        printf("(clock-expr)"); return true;
    }
    if (strcmp(name, "time") == 0 && arg1) {
        printf("(time-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "date") == 0 && arg1) {
        printf("(date-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    // output
    if (strcmp(name, "output") == 0 && arg1) {
        if (arg2) {
            printf("(output-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")");
        } else {
            printf("(output-expr "); emit_expr(source, arg1); printf(")");
        }
        return true;
    }
    // cmd (shell command)
    if (strcmp(name, "cmd") == 0 && arg1) {
        printf("(cmd-expr "); emit_expr(source, arg1); printf(")"); return true;
    }

    // --- namespaced math aliases (math.sqrt → sys_math_sqrt etc.) ---
    if (strcmp(name, "math_sqrt") == 0 && arg1) {
        printf("(sqrt-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_log") == 0 && arg1) {
        printf("(log-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_log10") == 0 && arg1) {
        printf("(log10-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_sin") == 0 && arg1) {
        printf("(sin-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_cos") == 0 && arg1) {
        printf("(cos-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_tan") == 0 && arg1) {
        printf("(tan-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_pow") == 0 && arg1 && arg2) {
        printf("(pow "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "math_exp") == 0 && arg1) {
        printf("(exp-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_cbrt") == 0 && arg1) {
        printf("(cbrt-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_hypot") == 0 && arg1 && arg2) {
        printf("(hypot-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "math_atan") == 0 && arg1) {
        printf("(atan-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_asin") == 0 && arg1) {
        printf("(asin-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_acos") == 0 && arg1) {
        printf("(acos-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_sinh") == 0 && arg1) {
        printf("(sinh-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_cosh") == 0 && arg1) {
        printf("(cosh-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_tanh") == 0 && arg1) {
        printf("(tanh-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_asinh") == 0 && arg1) {
        printf("(asinh-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_acosh") == 0 && arg1) {
        printf("(acosh-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_atanh") == 0 && arg1) {
        printf("(atanh-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_mean") == 0 && arg1) {
        printf("(avg-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_prod") == 0 && arg1) {
        printf("(prod-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_cumsum") == 0 && arg1) {
        printf("(cumsum-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_cumprod") == 0 && arg1) {
        printf("(cumprod-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_deviation") == 0 && arg1) {
        printf("(deviation-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_variance") == 0 && arg1) {
        printf("(variance-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_median") == 0 && arg1) {
        printf("(median-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_norm") == 0 && arg1) {
        printf("(norm-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_dot") == 0 && arg1 && arg2) {
        printf("(dot-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "math_quantile") == 0 && arg1 && arg2) {
        printf("(quantile-expr "); emit_expr(source, arg1); printf(" "); emit_expr(source, arg2); printf(")"); return true;
    }
    if (strcmp(name, "math_expm") == 0 && arg1) {
        printf("(expm-expr "); emit_expr(source, arg1); printf(")"); return true;
    }
    if (strcmp(name, "math_random") == 0) {
        printf("(random-expr)"); return true;
    }

    // not a known mapping — emit as generic app
    return false;
}

// emit parameters list
static void emit_params(AstNamedNode* param) {
    printf("(");
    bool first = true;
    while (param) {
        if (!first) printf(" ");
        printf("%.*s", (int)param->name->len, param->name->chars);
        first = false;
        param = (AstNamedNode*)param->next;
    }
    printf(")");
}

// emit linked list of expressions (for arrays, function args, etc.)
static void emit_expr_list(const char* source, AstNode* node) {
    while (node) {
        printf(" ");
        emit_expr(source, node);
        node = node->next;
    }
}

// emit for-expression
static void emit_for_expr(const char* source, AstForNode* for_node) {
    AstLoopNode* loop = (AstLoopNode*)for_node->loop;
    if (!loop) { printf("(error \"empty-for\")"); return; }

    bool has_where = for_node->where != NULL;
    bool has_index = loop->index_name != NULL;
    bool is_map_iter = loop->key_filter == LOOP_KEY_SYMBOL;

    if (is_map_iter && has_index) {
        // for (k, v at map) body
        printf("(for-at-kv %.*s %.*s ",
            (int)loop->index_name->len, loop->index_name->chars,
            (int)loop->name->len, loop->name->chars);
        emit_expr(source, loop->as);
        printf(" ");
        emit_expr(source, for_node->then);
        printf(")");
    }
    else if (is_map_iter) {
        // for (k at map) body
        printf("(for-at %.*s ", (int)loop->name->len, loop->name->chars);
        emit_expr(source, loop->as);
        printf(" ");
        emit_expr(source, for_node->then);
        printf(")");
    }
    else if (has_index && has_where) {
        // for (i, x in coll where pred) body — emit as nested
        printf("(for-idx %.*s %.*s ",
            (int)loop->index_name->len, loop->index_name->chars,
            (int)loop->name->len, loop->name->chars);
        emit_expr(source, loop->as);
        printf(" ");
        emit_expr(source, for_node->then);
        printf(")");
    }
    else if (has_index) {
        // for (i, x in coll) body
        printf("(for-idx %.*s %.*s ",
            (int)loop->index_name->len, loop->index_name->chars,
            (int)loop->name->len, loop->name->chars);
        emit_expr(source, loop->as);
        printf(" ");
        emit_expr(source, for_node->then);
        printf(")");
    }
    else if (has_where) {
        // for (x in coll where pred) body
        printf("(for-where %.*s ",
            (int)loop->name->len, loop->name->chars);
        emit_expr(source, loop->as);
        printf(" ");
        emit_expr(source, for_node->where);
        printf(" ");
        emit_expr(source, for_node->then);
        printf(")");
    }
    else {
        // simple: for (x in coll) body
        printf("(for %.*s ",
            (int)loop->name->len, loop->name->chars);
        emit_expr(source, loop->as);
        printf(" ");
        emit_expr(source, for_node->then);
        printf(")");
    }
}

// emit match clause — distinguish type patterns, value patterns, and range patterns
static void emit_match_clause(const char* source, AstMatchArm* arm) {
    if (!arm->pattern) {
        // default case
        printf("(default-case ");
        emit_expr(source, arm->body);
        printf(")");
        return;
    }

    AstNode* pat = arm->pattern;

    // check for range pattern: e.g. case 90 to 100
    if (pat->node_type == AST_NODE_BINARY) {
        AstBinaryNode* bn = (AstBinaryNode*)pat;
        if (bn->op == OPERATOR_TO) {
            printf("(case-range ");
            emit_expr(source, bn->left);
            printf(" ");
            emit_expr(source, bn->right);
            printf(" ");
            emit_expr(source, arm->body);
            printf(")");
            return;
        }
    }

    // check for union pattern in match arms (e.g., case 200 to 299 | 400 to 499)
    if (pat->node_type == AST_NODE_BINARY_TYPE) {
        AstBinaryNode* bn = (AstBinaryNode*)pat;
        if (bn->op == OPERATOR_UNION) {
            printf("(case-union ");
            emit_type_expr(source, bn->left);
            printf(" ");
            emit_type_expr(source, bn->right);
            printf(" ");
            emit_expr(source, arm->body);
            printf(")");
            return;
        }
    }

    // check for type pattern: AST_NODE_TYPE (base type like int, string)
    // or AST_NODE_IDENT (named type like Circle, Point)
    if (pat->node_type == AST_NODE_TYPE) {
        printf("(case-type ");
        emit_type_expr(source, pat);
        printf(" ");
        emit_expr(source, arm->body);
        printf(")");
        return;
    }
    if (pat->node_type == AST_NODE_IDENT) {
        printf("(case-type ");
        emit_type_expr(source, pat);
        printf(" ");
        emit_expr(source, arm->body);
        printf(")");
        return;
    }

    // value pattern: literal (int, float, string, bool, symbol, null)
    if (pat->node_type == AST_NODE_PRIMARY) {
        printf("(case-val ");
        emit_expr(source, pat);
        printf(" ");
        emit_expr(source, arm->body);
        printf(")");
        return;
    }

    // fallback: treat as type pattern
    printf("(case-type ");
    emit_type_expr(source, pat);
    printf(" ");
    emit_expr(source, arm->body);
    printf(")");
}

// emit a single expression node
static void emit_expr(const char* source, AstNode* node) {
    if (!node) { printf("null"); return; }

    switch (node->node_type) {
    case AST_NODE_PRIMARY: {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        if (pri->expr) {
            // wrapper around another expression
            emit_expr(source, pri->expr);
            return;
        }
        // literal value — determine from type
        if (!node->type) { printf("null"); return; }
        switch (node->type->type_id) {
        case LMD_TYPE_NULL:
            printf("null");
            break;
        case LMD_TYPE_BOOL: {
            int len; const char* src = node_src(source, node->node, &len);
            if (len >= 4 && strncmp(src, "true", 4) == 0) printf("#t");
            else printf("#f");
            break;
        }
        case LMD_TYPE_INT: {
            int len; const char* src = node_src(source, node->node, &len);
            printf("%.*s", len, src);
            break;
        }
        case LMD_TYPE_FLOAT: {
            TypeFloat* ft = (TypeFloat*)node->type;
            // use source text for exact representation
            int len; const char* src = node_src(source, node->node, &len);
            printf("%.*s", len, src);
            break;
        }
        case LMD_TYPE_STRING: {
            // use source text (preserves original escapes, already in "..." form)
            int len; const char* src = node_src(source, node->node, &len);
            // source may include outer primary_expr wrapper; find the actual string
            const char* q = (const char*)memchr(src, '"', len);
            if (q) {
                int slen = len - (int)(q - src);
                printf("%.*s", slen, q);
            } else {
                printf("\"\"");
            }
            break;
        }
        case LMD_TYPE_SYMBOL: {
            // Lambda: 'name' → Redex: (sym "name")
            // Symbol content may have escape sequences: \' for quote, \\ for backslash
            int len; const char* src = node_src(source, node->node, &len);
            // find the quote-delimited symbol content
            const char* q = (const char*)memchr(src, '\'', len);
            if (q) {
                const char* start = q + 1;
                int remaining = len - (int)(start - src);
                const char* end = (const char*)memchr(start, '\'', remaining);
                // For symbols with escaped quotes, find the actual closing quote
                // A quote is escaped if preceded by odd number of backslashes
                while (end && end > start) {
                    int nbs = 0;
                    const char* p = end - 1;
                    while (p >= start && *p == '\\') { nbs++; p--; }
                    if (nbs % 2 == 1) {
                        // odd backslashes: quote is escaped, keep searching
                        int rem2 = remaining - (int)(end - start) - 1;
                        end = (const char*)memchr(end + 1, '\'', rem2);
                    } else {
                        break;  // even backslashes: quote is real closing quote
                    }
                }
                int slen = end ? (int)(end - start) : remaining;
                printf("(sym \"");
                // Unescape Lambda symbol escapes, then re-escape for s-expression
                for (int i = 0; i < slen; i++) {
                    if (start[i] == '\\' && i + 1 < slen) {
                        char next = start[i + 1];
                        if (next == '\'') {
                            // \' → literal quote (no special in s-expr)
                            putchar('\'');
                            i++;
                        } else if (next == '\\') {
                            // \\ → literal backslash (escape for s-expr)
                            printf("\\\\");
                            i++;
                        } else {
                            // other escape: pass through
                            printf("\\\\");
                            putchar(next);
                            i++;
                        }
                    } else if (start[i] == '"') {
                        printf("\\\"");
                    } else {
                        putchar(start[i]);
                    }
                }
                printf("\")");
            } else {
                printf("(sym \"\")");
            }
            break;
        }
        case LMD_TYPE_TYPE: {
            // type reference used as value — emit as (type-val type-name)
            TypeType* tt = (TypeType*)node->type;
            if (tt->type) {
                const char* rname = redex_type_name(tt->type->type_id);
                if (rname) {
                    printf("(type-val %s)", rname);
                } else {
                    printf("(type-val %s-type)", type_info[tt->type->type_id].name);
                }
            } else {
                int len; const char* src = node_src(source, node->node, &len);
                printf("(type-val %.*s-type)", len, src);
            }
            break;
        }
        case LMD_TYPE_DTIME: {
            int len; const char* src = node_src(source, node->node, &len);
            if (len >= 3 && src[0] == 't' && src[1] == '\'') {
                printf("(datetime ");
                emit_escaped_string(src + 2, len - 3);
                printf(")");
            } else {
                printf("(datetime \"%.*s\")", len, src);
            }
            break;
        }
        case LMD_TYPE_DECIMAL:
        case LMD_TYPE_NUMBER:
        case LMD_TYPE_NUM_SIZED:
        case LMD_TYPE_INT64:
        case LMD_TYPE_UINT64: {
            int len; const char* src = node_src(source, node->node, &len);
            printf("%.*s", len, src);
            break;
        }
        case LMD_TYPE_BINARY: {
            int len; const char* src = node_src(source, node->node, &len);
            printf("(binary \"%.*s\")", len, src);
            break;
        }
        default: {
            // fallback: emit source text as comment
            int len; const char* src = node_src(source, node->node, &len);
            printf("(unsupported \"%.*s\")", len, src);
            break;
        }
        }
        break;
    }

    case AST_NODE_IDENT: {
        AstIdentNode* id = (AstIdentNode*)node;
        printf("%.*s", (int)id->name->len, id->name->chars);
        break;
    }

    case AST_NODE_CURRENT_ITEM:
        printf("~");
        break;

    case AST_NODE_CURRENT_INDEX:
        printf("~#");
        break;

    case AST_NODE_BINARY: {
        AstBinaryNode* bn = (AstBinaryNode*)node;
        if (bn->op == OPERATOR_IS_NAN) {
            printf("(is-nan ");
            emit_expr(source, bn->left);
            printf(")");
            break;
        }
        const char* op = binary_op_name(bn->op);
        if (op) {
            if (bn->op == OPERATOR_IS) {
                // is-type: right side is a type expression
                printf("(is-type ");
                emit_expr(source, bn->left);
                printf(" ");
                emit_type_expr(source, bn->right);
                printf(")");
            } else {
                printf("(%s ", op);
                emit_expr(source, bn->left);
                printf(" ");
                emit_expr(source, bn->right);
                printf(")");
            }
        } else {
            int len; const char* src = node_src(source, node->node, &len);
            printf("(unsupported-op \"%.*s\")", len, src);
        }
        break;
    }

    case AST_NODE_UNARY: {
        AstUnaryNode* un = (AstUnaryNode*)node;
        switch (un->op) {
        case OPERATOR_NOT:
            printf("(l-not "); emit_expr(source, un->operand); printf(")"); break;
        case OPERATOR_NEG:
            printf("(neg "); emit_expr(source, un->operand); printf(")"); break;
        case OPERATOR_POS:
            // unary + is a no-op for numbers, but can be a string→number cast
            emit_expr(source, un->operand); break;
        case OPERATOR_IS_ERROR:
            printf("(is-error "); emit_expr(source, un->operand); printf(")"); break;
        default: {
            int len; const char* src = node_src(source, node->node, &len);
            printf("(unsupported-unary \"%.*s\")", len > 60 ? 60 : len, src);
            break;
        }
        }
        break;
    }

    case AST_NODE_SPREAD: {
        AstUnaryNode* un = (AstUnaryNode*)node;
        printf("(spread ");
        emit_expr(source, un->operand);
        printf(")");
        break;
    }

    case AST_NODE_PIPE: {
        AstPipeNode* pn = (AstPipeNode*)node;
        if (pn->op == OPERATOR_WHERE) {
            printf("(where ");
        } else {
            // check if right side is a function call (pipe-agg) vs transform (pipe)
            printf("(pipe ");
        }
        emit_expr(source, pn->left);
        printf(" ");
        emit_expr(source, pn->right);
        printf(")");
        break;
    }

    case AST_NODE_IF_EXPR: {
        AstIfNode* if_node = (AstIfNode*)node;
        if (if_node->otherwise) {
            printf("(if ");
            emit_expr(source, if_node->cond);
            printf(" ");
            emit_expr(source, if_node->then);
            printf(" ");
            emit_expr(source, if_node->otherwise);
            printf(")");
        } else {
            printf("(if-stam ");
            emit_expr(source, if_node->cond);
            printf(" ");
            emit_expr(source, if_node->then);
            printf(")");
        }
        break;
    }

    case AST_NODE_MATCH_EXPR: {
        AstMatchNode* match = (AstMatchNode*)node;
        printf("(match ");
        emit_expr(source, match->scrutinee);
        AstMatchArm* arm = match->first_arm;
        while (arm) {
            printf(" ");
            emit_match_clause(source, arm);
            arm = (AstMatchArm*)arm->next;
        }
        printf(")");
        break;
    }

    case AST_NODE_FOR_EXPR: {
        emit_for_expr(source, (AstForNode*)node);
        break;
    }

    case AST_NODE_ARRAY: {
        AstArrayNode* arr = (AstArrayNode*)node;
        printf("(array");
        // for-expressions and where/that clauses inside arrays act as generators (spread their results)
        AstNode* item = arr->item;
        while (item) {
            printf(" ");
            bool is_spread = (item->node_type == AST_NODE_FOR_EXPR);
            if (!is_spread && item->node_type == AST_NODE_PIPE) {
                // where/that clause — also auto-spread
                AstBinaryNode* bn = (AstBinaryNode*)item;
                if (bn->op == OPERATOR_WHERE) is_spread = true;
            }
            if (is_spread) {
                printf("(spread ");
                emit_expr(source, item);
                printf(")");
            } else {
                emit_expr(source, item);
            }
            item = item->next;
        }
        printf(")");
        break;
    }

    case AST_NODE_LIST: {
        AstListNode* list = (AstListNode*)node;
        if (list->declare) {
            // list with let bindings: (let a=1, let b=2, items...)
            printf("(let-seq (");
            AstNode* decl = list->declare;
            while (decl) {
                if (decl != list->declare) printf(" ");
                if (decl->node_type == AST_NODE_ASSIGN) {
                    AstNamedNode* asn = (AstNamedNode*)decl;
                    printf("(%.*s ", (int)asn->name->len, asn->name->chars);
                    emit_expr(source, asn->as);
                    printf(")");
                }
                decl = decl->next;
            }
            printf(") ");
            // body is the items
            if (list->item) {
                if (list->item->next) {
                    // multiple items — wrap in list-expr? Actually in Lambda, (let a=1, expr) → expr
                    // just emit the last item as body
                    AstNode* last = list->item;
                    while (last->next) last = last->next;
                    emit_expr(source, last);
                } else {
                    emit_expr(source, list->item);
                }
            } else {
                printf("null");
            }
            printf(")");
        } else {
            printf("(list-expr");
            emit_expr_list(source, list->item);
            printf(")");
        }
        break;
    }

    case AST_NODE_MAP: {
        AstMapNode* map_node = (AstMapNode*)node;
        printf("(map-expr");
        AstNode* item = map_node->item;
        while (item) {
            if (item->node_type == AST_NODE_KEY_EXPR) {
                AstNamedNode* key = (AstNamedNode*)item;
                printf(" (%.*s ", (int)key->name->len, key->name->chars);
                emit_expr(source, key->as);
                printf(")");
            } else if (item->node_type == AST_NODE_SPREAD) {
                printf(" ");
                emit_expr(source, item);
            } else {
                printf(" ");
                emit_expr(source, item);
            }
            item = item->next;
        }
        printf(")");
        break;
    }

    case AST_NODE_ELEMENT: {
        AstElementNode* elmt = (AstElementNode*)node;
        printf("(element");
        // emit tag name if present
        TypeElmt* etype = (TypeElmt*)elmt->type;
        if (etype && etype->name.length > 0) {
            printf(" (_tag %.*s)", (int)etype->name.length, etype->name.str);
        }
        AstNode* attr = elmt->item;
        while (attr) {
            printf(" ");
            emit_expr(source, attr);
            attr = attr->next;
        }
        if (elmt->content) {
            printf(" (content");
            emit_expr_list(source, (AstNode*)elmt->content);
            printf(")");
        }
        printf(")");
        break;
    }

    case AST_NODE_MEMBER_EXPR: {
        AstFieldNode* fn = (AstFieldNode*)node;
        printf("(member ");
        emit_expr(source, fn->object);
        printf(" ");
        // field is typically an ident
        if (fn->field && fn->field->node_type == AST_NODE_IDENT) {
            AstIdentNode* id = (AstIdentNode*)fn->field;
            printf("%.*s", (int)id->name->len, id->name->chars);
        } else {
            emit_expr(source, fn->field);
        }
        printf(")");
        break;
    }

    case AST_NODE_INDEX_EXPR: {
        AstFieldNode* fn = (AstFieldNode*)node;
        printf("(index ");
        emit_expr(source, fn->object);
        printf(" ");
        emit_expr(source, fn->field);
        printf(")");
        break;
    }

    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)node;

        // check for error propagation
        if (call->propagate) {
            printf("(try-prop ");
        }

        // check for system function
        if (call->function && call->function->node_type == AST_NODE_SYS_FUNC) {
            if (emit_sys_func_call(source, call)) {
                if (call->propagate) printf(")");
                break;
            }
        }

        // regular function call
        printf("(app ");
        emit_expr(source, call->function);
        AstNode* arg = call->argument;
        while (arg) {
            printf(" ");
            emit_expr(source, arg);
            arg = arg->next;
        }
        printf(")");

        if (call->propagate) printf(")");
        break;
    }

    case AST_NODE_SYS_FUNC: {
        // standalone sys func reference (not in a call)
        AstSysFuncNode* sys = (AstSysFuncNode*)node;
        printf("sys_%s", sys->fn_info->name);
        break;
    }

    case AST_NODE_FUNC_EXPR: {
        AstFuncNode* fn = (AstFuncNode*)node;
        printf("(lam ");
        emit_params(fn->param);
        printf(" ");
        emit_expr(source, fn->body);
        printf(")");
        break;
    }

    case AST_NODE_FUNC: {
        // named fn defined inside an expression context
        AstFuncNode* fn = (AstFuncNode*)node;
        printf("(def-fn %.*s ", (int)fn->name->len, fn->name->chars);
        emit_params(fn->param);
        printf(" ");
        emit_expr(source, fn->body);
        // rest: whatever follows
        printf(" null)");
        break;
    }

    case AST_NODE_ASSIGN: {
        // let binding: name = expr (used inside let-stam or list declares)
        AstNamedNode* asn = (AstNamedNode*)node;
        printf("(%.*s ", (int)asn->name->len, asn->name->chars);
        emit_expr(source, asn->as);
        printf(")");
        break;
    }

    case AST_NODE_KEY_EXPR: {
        AstNamedNode* key = (AstNamedNode*)node;
        printf("(%.*s ", (int)key->name->len, key->name->chars);
        emit_expr(source, key->as);
        printf(")");
        break;
    }

    case AST_NODE_NAMED_ARG: {
        AstNamedNode* na = (AstNamedNode*)node;
        printf("(named-arg %.*s ", (int)na->name->len, na->name->chars);
        emit_expr(source, na->as);
        printf(")");
        break;
    }

    // ---------- procedural forms ----------

    case AST_NODE_VAR_STAM: {
        AstLetNode* var_let = (AstLetNode*)node;
        AstNode* decl = var_let->declare;
        // count declarations
        int count = 0;
        for (AstNode* d = decl; d; d = d->next) count++;
        if (count > 1) printf("(seq");
        while (decl) {
            if (count > 1) printf(" ");
            if (decl->node_type == AST_NODE_ASSIGN) {
                AstNamedNode* asn = (AstNamedNode*)decl;
                printf("(var %.*s ", (int)asn->name->len, asn->name->chars);
                emit_expr(source, asn->as);
                printf(")");
            }
            decl = decl->next;
        }
        if (count > 1) printf(")");
        break;
    }

    case AST_NODE_ASSIGN_STAM: {
        AstAssignStamNode* asn = (AstAssignStamNode*)node;
        printf("(assign %.*s ", (int)asn->target->len, asn->target->chars);
        emit_expr(source, asn->value);
        printf(")");
        break;
    }

    case AST_NODE_INDEX_ASSIGN_STAM: {
        AstCompoundAssignNode* ca = (AstCompoundAssignNode*)node;
        printf("(assign-index ");
        emit_expr(source, ca->object);
        printf(" ");
        emit_expr(source, ca->key);
        printf(" ");
        emit_expr(source, ca->value);
        printf(")");
        break;
    }

    case AST_NODE_MEMBER_ASSIGN_STAM: {
        AstCompoundAssignNode* ca = (AstCompoundAssignNode*)node;
        printf("(assign-member ");
        emit_expr(source, ca->object);
        printf(" ");
        // key is the field name
        if (ca->key && ca->key->node_type == AST_NODE_IDENT) {
            AstIdentNode* id = (AstIdentNode*)ca->key;
            printf("%.*s", (int)id->name->len, id->name->chars);
        } else {
            emit_expr(source, ca->key);
        }
        printf(" ");
        emit_expr(source, ca->value);
        printf(")");
        break;
    }

    case AST_NODE_WHILE_STAM: {
        AstWhileNode* wh = (AstWhileNode*)node;
        printf("(while ");
        emit_expr(source, wh->cond);
        printf(" ");
        emit_expr(source, wh->body);
        printf(")");
        break;
    }

    case AST_NODE_BREAK_STAM:
        printf("(break)");
        break;

    case AST_NODE_CONTINUE_STAM:
        printf("(continue)");
        break;

    case AST_NODE_RETURN_STAM: {
        AstReturnNode* ret = (AstReturnNode*)node;
        printf("(return ");
        if (ret->value) emit_expr(source, ret->value);
        else printf("null");
        printf(")");
        break;
    }

    case AST_NODE_RAISE_STAM:
    case AST_NODE_RAISE_EXPR: {
        AstRaiseNode* raise = (AstRaiseNode*)node;
        printf("(raise-expr ");
        emit_expr(source, raise->value);
        printf(")");
        break;
    }

    case AST_NODE_FOR_STAM: {
        // procedural for statement — same structure as for_expr
        emit_for_expr(source, (AstForNode*)node);
        break;
    }

    // ---------- object system ----------

    case AST_NODE_OBJECT_LITERAL: {
        AstObjectLiteralNode* obj = (AstObjectLiteralNode*)node;
        printf("(make-object %.*s", (int)obj->type_name->len, obj->type_name->chars);
        AstNode* field = obj->item;
        while (field) {
            printf(" ");
            emit_expr(source, field);
            field = field->next;
        }
        printf(")");
        break;
    }

    case AST_NODE_CONTENT: {
        AstListNode* content = (AstListNode*)node;
        if (content->item && !content->item->next) {
            // single content item — unwrap
            emit_expr(source, content->item);
        } else {
            printf("(seq");
            emit_expr_list(source, content->item);
            printf(")");
        }
        break;
    }

    // ---------- let/pub statements ----------

    case AST_NODE_LET_STAM:
    case AST_NODE_PUB_STAM: {
        // these are emitted at top-level; if encountered inside an expression,
        // emit as nested let
        AstLetNode* let_node = (AstLetNode*)node;
        AstNode* decl = let_node->declare;
        // check if any binding has error destructuring
        bool has_err_binding = false;
        if (decl && decl->node_type == AST_NODE_ASSIGN) {
            AstNamedNode* asn = (AstNamedNode*)decl;
            if (asn->error_name) has_err_binding = true;
        }
        if (has_err_binding && decl->node_type == AST_NODE_ASSIGN) {
            AstNamedNode* asn = (AstNamedNode*)decl;
            printf("(let-err %.*s %.*s ",
                (int)asn->name->len, asn->name->chars,
                (int)asn->error_name->len, asn->error_name->chars);
            emit_expr(source, asn->as);
            printf(" null)");
        } else {
            printf("(let (");
            while (decl) {
                if (decl != let_node->declare) printf(" ");
                if (decl->node_type == AST_NODE_ASSIGN) {
                    AstNamedNode* asn = (AstNamedNode*)decl;
                    printf("(%.*s ", (int)asn->name->len, asn->name->chars);
                    emit_expr(source, asn->as);
                    printf(")");
                } else if (decl->node_type == AST_NODE_DECOMPOSE) {
                    AstDecomposeNode* dec = (AstDecomposeNode*)decl;
                    printf("(decompose (");
                    for (int i = 0; i < dec->name_count; i++) {
                        if (i > 0) printf(" ");
                        printf("%.*s", (int)dec->names[i]->len, dec->names[i]->chars);
                    }
                    printf(") ");
                    emit_expr(source, dec->as);
                    printf(")");
                }
                decl = decl->next;
            }
            printf(") null)");
        }
        break;
    }

    // ---------- type system ----------

    case AST_NODE_TYPE_STAM: {
        AstLetNode* type_stam = (AstLetNode*)node;
        AstNode* decl = type_stam->declare;
        if (decl && decl->node_type == AST_NODE_OBJECT_TYPE) {
            AstObjectTypeNode* obj_type = (AstObjectTypeNode*)decl;
            printf("(def-type %.*s", (int)obj_type->name->len, obj_type->name->chars);
            // emit base type if present
            if (obj_type->base_type) {
                printf(" :parent ");
                emit_type_expr(source, obj_type->base_type);
            }
            // emit fields with defaults
            emit_object_fields(source, obj_type);
            // emit methods
            if (obj_type->methods) {
                printf(" :methods (");
                AstNode* method = obj_type->methods;
                bool first_method = true;
                while (method) {
                    if (!first_method) printf(" ");
                    if (method->node_type == AST_NODE_FUNC || method->node_type == AST_NODE_PROC) {
                        AstFuncNode* fn = (AstFuncNode*)method;
                        const char* kind = method->node_type == AST_NODE_PROC ? "pn" : "fn";
                        printf("(%s %.*s ", kind, (int)fn->name->len, fn->name->chars);
                        emit_params(fn->param);
                        printf(" ");
                        emit_expr(source, fn->body);
                        printf(")");
                    }
                    first_method = false;
                    method = method->next;
                }
                printf(")");
            }
            // emit constraints
            if (obj_type->constraints) {
                printf(" :constraints (");
                AstNode* constraint = obj_type->constraints;
                bool first_c = true;
                while (constraint) {
                    if (!first_c) printf(" ");
                    emit_expr(source, constraint);
                    first_c = false;
                    constraint = constraint->next;
                }
                printf(")");
            }
            printf(")");
        } else if (decl && decl->node_type == AST_NODE_ASSIGN) {
            // type alias: type Name = TypeExpr
            AstNamedNode* asn = (AstNamedNode*)decl;
            printf("(def-type-alias %.*s ", (int)asn->name->len, asn->name->chars);
            if (asn->as) emit_type_expr(source, asn->as);
            else printf("any");
            printf(")");
        } else {
            int len; const char* src = node_src(source, node->node, &len);
            printf("(def-type-raw ");
            emit_escaped_string(src, len);
            printf(")");
        }
        break;
    }

    case AST_NODE_TYPE: {
        // standalone type reference used as expression
        if (node->type && node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)node->type;
            if (tt->type) {
                const char* rname = redex_type_name(tt->type->type_id);
                if (rname) {
                    printf("(type-val %s)", rname);
                } else {
                    printf("(type-val %s-type)", type_info[tt->type->type_id].name);
                }
                break;
            }
        }
        int len; const char* src = node_src(source, node->node, &len);
        printf("(type-val %.*s-type)", len, src);
        break;
    }

    case AST_NODE_PROC: {
        AstFuncNode* pn = (AstFuncNode*)node;
        printf("(pn-def %.*s ", (int)pn->name->len, pn->name->chars);
        emit_params(pn->param);
        printf(" ");
        emit_expr(source, pn->body);
        printf(")");
        break;
    }

    case AST_NODE_OBJECT_TYPE: {
        AstObjectTypeNode* obj_type = (AstObjectTypeNode*)node;
        printf("(def-type %.*s", (int)obj_type->name->len, obj_type->name->chars);
        if (obj_type->base_type) {
            printf(" :parent ");
            emit_type_expr(source, obj_type->base_type);
        }
        // emit fields with defaults
        emit_object_fields(source, obj_type);
        if (obj_type->methods) {
            printf(" :methods (");
            AstNode* method = obj_type->methods;
            bool first_m = true;
            while (method) {
                if (!first_m) printf(" ");
                if (method->node_type == AST_NODE_FUNC || method->node_type == AST_NODE_PROC) {
                    AstFuncNode* fn = (AstFuncNode*)method;
                    const char* kind = method->node_type == AST_NODE_PROC ? "pn" : "fn";
                    printf("(%s %.*s ", kind, (int)fn->name->len, fn->name->chars);
                    emit_params(fn->param);
                    printf(" ");
                    emit_expr(source, fn->body);
                    printf(")");
                }
                first_m = false;
                method = method->next;
            }
            printf(")");
        }
        if (obj_type->constraints) {
            printf(" :constraints (");
            AstNode* c = obj_type->constraints;
            bool first_c = true;
            while (c) {
                if (!first_c) printf(" ");
                emit_expr(source, c);
                first_c = false;
                c = c->next;
            }
            printf(")");
        }
        printf(")");
        break;
    }

    case AST_NODE_PATH_EXPR: {
        int len; const char* src = node_src(source, node->node, &len);
        printf("(path ");
        emit_escaped_string(src, len);
        printf(")");
        break;
    }

    case AST_NODE_PATH_INDEX_EXPR: {
        AstPathIndexNode* pi = (AstPathIndexNode*)node;
        printf("(path-index ");
        emit_expr(source, pi->base_path);
        printf(" ");
        emit_expr(source, pi->segment_expr);
        printf(")");
        break;
    }

    case AST_NODE_PARENT_EXPR: {
        AstParentNode* pe = (AstParentNode*)node;
        printf("(parent ");
        emit_expr(source, pe->object);
        printf(" %d)", pe->depth);
        break;
    }

    case AST_NODE_QUERY_EXPR: {
        AstQueryNode* qe = (AstQueryNode*)node;
        printf("(%s ", qe->direct ? "query-direct" : "query");
        emit_expr(source, qe->object);
        printf(" ");
        emit_type_expr(source, qe->query);
        printf(")");
        break;
    }

    case AST_NODE_STRING_PATTERN:
    case AST_NODE_SYMBOL_PATTERN: {
        AstPatternDefNode* pat = (AstPatternDefNode*)node;
        printf("(pattern-def %.*s ", (int)pat->name->len, pat->name->chars);
        emit_expr(source, pat->as);
        printf(")");
        break;
    }

    case AST_NODE_VIEW: {
        AstViewNode* view = (AstViewNode*)node;
        printf("(%s ", view->is_edit ? "edit-view" : "view-def");
        if (view->name) printf("%.*s ", (int)view->name->len, view->name->chars);
        else printf("anon ");
        if (view->pattern) emit_type_expr(source, view->pattern);
        else printf("any");
        printf(" ");
        emit_expr(source, view->body);
        printf(")");
        break;
    }

    case AST_NODE_PIPE_FILE_STAM: {
        AstBinaryNode* pf = (AstBinaryNode*)node;
        printf("(%s ", pf->op == OPERATOR_PIPE_APPEND ? "pipe-append" : "pipe-file");
        emit_expr(source, pf->left);
        printf(" ");
        emit_expr(source, pf->right);
        printf(")");
        break;
    }

    case AST_NODE_CONSTRAINED_TYPE: {
        AstConstrainedTypeNode* ct = (AstConstrainedTypeNode*)node;
        printf("(constrained ");
        emit_type_expr(source, ct->base);
        printf(" ");
        emit_expr(source, ct->constraint);
        printf(")");
        break;
    }

    case AST_NODE_BINARY_TYPE: {
        AstBinaryNode* bt = (AstBinaryNode*)node;
        if (bt->op == OPERATOR_UNION) {
            printf("(union ");
            emit_expr(source, bt->left);
            printf(" ");
            emit_expr(source, bt->right);
            printf(")");
        } else {
            int len; const char* src = node_src(source, node->node, &len);
            emit_escaped_string(src, len);
        }
        break;
    }

    case AST_NODE_UNARY_TYPE:
    case AST_NODE_PATTERN_CHAR_CLASS:
    case AST_NODE_PATTERN_SEQ:
    case AST_NODE_PATTERN_RANGE: {
        int len; const char* src = node_src(source, node->node, &len);
        emit_escaped_string(src, len);
        break;
    }

    case AST_NODE_IMPORT: {
        printf("(unsupported-import)");
        break;
    }

    default: {
        int len; const char* src = node_src(source, node->node, &len);
        printf("(unsupported-%d \"%.*s\")", node->node_type, len > 60 ? 60 : len, src);
        break;
    }
    }
}

// Emit object type fields with :default (type annotation) and :init (explicit default value).
// Looks up ShapeEntry by field name to find default_value if present.
static void emit_object_fields(const char* source, AstObjectTypeNode* obj_type) {
    // Get TypeObject from AST node's type (wrapped in TypeType)
    TypeObject* type_obj = NULL;
    if (obj_type->type && obj_type->type->type_id == LMD_TYPE_TYPE) {
        Type* inner = ((TypeType*)obj_type->type)->type;
        if (inner && inner->type_id == LMD_TYPE_OBJECT) {
            type_obj = (TypeObject*)inner;
        }
    }

    printf(" :fields (");
    AstNode* field = obj_type->item;
    while (field) {
        if (field != obj_type->item) printf(" ");
        if (field->node_type == AST_NODE_KEY_EXPR) {
            AstNamedNode* f = (AstNamedNode*)field;
            printf("(%.*s", (int)f->name->len, f->name->chars);
            if (f->as) {
                printf(" :default ");
                emit_expr(source, f->as);
            }
            // emit explicit default value from ShapeEntry if present
            if (type_obj) {
                ShapeEntry* se = type_obj->shape;
                while (se) {
                    if (se->name && se->name->length == (int64_t)f->name->len &&
                        memcmp(se->name->str, f->name->chars, f->name->len) == 0) {
                        if (se->default_value) {
                            printf(" :init ");
                            emit_expr(source, se->default_value);
                        }
                        break;
                    }
                    se = se->next;
                }
            }
            printf(")");
        } else {
            int flen; const char* fsrc = node_src(source, field->node, &flen);
            printf("(%.*s)", flen, fsrc);
        }
        field = field->next;
    }
    printf(")");
}

// emit top-level forms from script children
static void emit_top_level(const char* source, AstNode* child) {
    while (child) {
        // flatten CONTENT nodes at script level (unwrap to individual forms)
        if (child->node_type == AST_NODE_CONTENT) {
            AstListNode* content = (AstListNode*)child;
            emit_top_level(source, content->item);
            child = child->next;
            continue;
        }
        printf("\n  ");
        switch (child->node_type) {
        case AST_NODE_LET_STAM:
        case AST_NODE_PUB_STAM: {
            // top-level let binding
            AstLetNode* let_node = (AstLetNode*)child;
            AstNode* decl = let_node->declare;
            while (decl) {
                if (decl->node_type == AST_NODE_ASSIGN) {
                    AstNamedNode* asn = (AstNamedNode*)decl;
                    printf("(bind %.*s ", (int)asn->name->len, asn->name->chars);
                    if (asn->error_name) {
                        // let a^err = expr → emit with error binding info
                        printf(":error %.*s ", (int)asn->error_name->len, asn->error_name->chars);
                    }
                    emit_expr(source, asn->as);
                    printf(")");
                } else if (decl->node_type == AST_NODE_DECOMPOSE) {
                    AstDecomposeNode* dec = (AstDecomposeNode*)decl;
                    printf("(bind-decompose (");
                    for (int i = 0; i < dec->name_count; i++) {
                        if (i > 0) printf(" ");
                        printf("%.*s", (int)dec->names[i]->len, dec->names[i]->chars);
                    }
                    printf(") ");
                    emit_expr(source, dec->as);
                    printf(")");
                }
                if (decl->next) printf("\n  ");
                decl = decl->next;
            }
            break;
        }
        case AST_NODE_FUNC: {
            AstFuncNode* fn = (AstFuncNode*)child;
            printf("(fn-def %.*s ", (int)fn->name->len, fn->name->chars);
            emit_params(fn->param);
            printf(" ");
            emit_expr(source, fn->body);
            printf(")");
            break;
        }
        case AST_NODE_PROC: {
            AstFuncNode* pn = (AstFuncNode*)child;
            printf("(pn-def %.*s ", (int)pn->name->len, pn->name->chars);
            emit_params(pn->param);
            printf(" ");
            emit_expr(source, pn->body);
            printf(")");
            break;
        }
        case AST_NODE_TYPE_STAM: {
            emit_expr(source, child);
            break;
        }
        default:
            emit_expr(source, child);
            break;
        }
        child = child->next;
    }
}

// check if script is procedural (has pn main at top level)
static bool has_pn_main(AstNode* child) {
    while (child) {
        if (child->node_type == AST_NODE_CONTENT) {
            AstListNode* content = (AstListNode*)child;
            if (has_pn_main(content->item)) return true;
        }
        if (child->node_type == AST_NODE_PROC) {
            AstFuncNode* fn = (AstFuncNode*)child;
            if (fn->name && fn->name->len == 4 && memcmp(fn->name->chars, "main", 4) == 0) {
                return true;
            }
        }
        child = child->next;
    }
    return false;
}

static bool is_procedural_script(AstScript* script) {
    return has_pn_main(script->child);
}

// entry point: parse file and emit s-expressions to stdout
int emit_sexpr_file(const char* script_path) {
    // read source
    char* source = read_text_file(script_path);
    if (!source) {
        fprintf(stderr, "Error: Cannot read '%s'\n", script_path);
        return 1;
    }

    // create parser and parse
    TSParser* parser = lambda_parser();
    TSTree* tree = lambda_parse_source(parser, source);
    if (!tree) {
        fprintf(stderr, "Error: Failed to parse '%s'\n", script_path);
        mem_free(source);
        ts_parser_delete(parser);
        return 1;
    }

    TSNode root = ts_tree_root_node(tree);
    if (ts_node_has_error(root)) {
        fprintf(stderr, "Error: Syntax errors in '%s'\n", script_path);
        ts_tree_delete(tree);
        mem_free(source);
        ts_parser_delete(parser);
        return 1;
    }

    // check for imports in source (require runtime, which we don't set up)
    if (strstr(source, "\nimport ") || strncmp(source, "import ", 7) == 0) {
        fprintf(stderr, "Error: '%s' contains imports (not supported by --emit-sexpr)\n", script_path);
        ts_tree_delete(tree);
        mem_free(source);
        ts_parser_delete(parser);
        return 1;
    }

    // setup minimal Transpiler for AST building
    Input* input_base = Input::create(pool_create(), nullptr);
    if (!input_base) {
        fprintf(stderr, "Error: Failed to allocate memory\n");
        ts_tree_delete(tree);
        mem_free(source);
        ts_parser_delete(parser);
        return 1;
    }

    Transpiler tp;
    memset(&tp, 0, sizeof(Transpiler));
    tp.source = source;
    tp.parser = parser;
    tp.pool = input_base->pool;
    tp.arena = input_base->arena;
    tp.name_pool = input_base->name_pool;
    tp.type_list = input_base->type_list;
    tp.const_list = arraylist_new(16);
    tp.decimal_ctx = decimal_fixed_context();
    tp.reference = script_path;

    // build AST
    tp.ast_root = build_script(&tp, root);
    if (!tp.ast_root) {
        fprintf(stderr, "Error: Failed to build AST for '%s'\n", script_path);
        pool_destroy(tp.pool);
        ts_tree_delete(tree);
        mem_free(source);
        ts_parser_delete(parser);
        return 1;
    }

    if (tp.error_count > 0) {
        fprintf(stderr, "Error: %d errors building AST for '%s'\n", tp.error_count, script_path);
        pool_destroy(tp.pool);
        ts_tree_delete(tree);
        mem_free(source);
        ts_parser_delete(parser);
        return 1;
    }

    AstScript* script_node = (AstScript*)tp.ast_root;
    bool is_proc = is_procedural_script(script_node);

    // emit s-expressions
    printf("(script \"%s\"", is_proc ? "pn" : "fn");
    emit_top_level(source, script_node->child);
    printf(")\n");

    // cleanup
    pool_destroy(tp.pool);
    ts_tree_delete(tree);
    mem_free(source);
    ts_parser_delete(parser);

    return 0;
}
