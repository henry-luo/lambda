// emit_ast_dump.cpp — Emit canonical Lambda AST dumps
//
// Parses a Lambda .ls source file, builds the AST with the production C parser,
// and emits a structural dump for parser/type-inference verification.
#include "../../lib/memtrack.h"
#include "../../lib/mem_factory.h"
#include "emit_ast_dump.h"
#include "transpiler.hpp"
#include "ast_build.hpp"          // lambda_rd_build_ast: the production C parser
#include "type_contract.hpp"
#include "../core/lambda-decimal.hpp"
#include "../../lib/file.h"
#include "../../lib/log.h"

struct EmitDirectParseState {
    char* source;
    Input* input;
};

static bool emit_prepare_direct_parse(const char* script_path,
        EmitDirectParseState* state) {
    memset(state, 0, sizeof(*state));
    state->source = read_text_file(script_path);
    if (!state->source) {
        fprintf(stderr, "Error: Cannot read '%s'\n", script_path);
        return false;
    }
    // AST emission must use the direct parser so normal profiles remain
    // independent of the optional lambda-cst archive.
    // Input::create registers arena/name/shape allocators under its backing
    // pool. A raw pool leaves those root nodes dangling after pool_destroy,
    // so the factory must own the subtree until direct-AST teardown completes.
    Pool* pool = mem_pool_create(NULL, MEM_ROLE_INPUT, "emit-parse.pool");
    state->input = Input::create(pool, nullptr);
    if (!state->input) {
        fprintf(stderr, "Error: Failed to allocate memory\n");
        pool_destroy(pool);
        mem_free(state->source);
        return false;
    }
    return true;
}

static void emit_init_direct_transpiler(Transpiler* tp,
        EmitDirectParseState* parse, const char* script_path) {
    memset(tp, 0, sizeof(Transpiler));
    tp->source = parse->source;
    tp->pool = parse->input->pool;
    tp->arena = parse->input->arena;
    tp->name_pool = parse->input->name_pool;
    tp->type_list = parse->input->type_list;
    tp->url = parse->input->url;
    tp->path = parse->input->path;
    tp->root = parse->input->root;
    tp->const_list = arraylist_new(16);
    tp->decimal_ctx = decimal_fixed_context();
    tp->reference = script_path;
}

static bool emit_build_direct_ast(Transpiler* tp, const char* source) {
    AstScript* direct_root = NULL;
    LambdaParseError parse_error = {};
    LambdaParseStatus status = lambda_rd_build_ast(tp, source, strlen(source),
        &direct_root, &parse_error);
    if (status != LAMBDA_PARSE_OK || !direct_root) {
        fprintf(stderr, "Error: C parser rejected '%s': %s\n",
            tp->reference ? tp->reference : "<source>",
            parse_error.message ? parse_error.message : "direct AST reduction failed");
        return false;
    }
    tp->ast_root = (AstNode*)direct_root;
    return true;
}

// get source text from the parser-neutral span retained on every AST node.
static inline const char* node_src(const char* source, SourceSpan span, int* out_len) {
    *out_len = (int)lambda_source_span_length(span);
    return source + span.start_byte;
}

static void emit_dump_escaped_string(const char* str, int len) {
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

static void emit_dump_indent(int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
}

static const char* ast_dump_kind_name(AstNodeType type) {
    switch (type) {
        case AST_NODE_NULL: return "AST_NODE_NULL";
        case AST_SCRIPT: return "AST_SCRIPT";
        case AST_NODE_PRIMARY: return "AST_NODE_PRIMARY";
        case AST_NODE_LITERAL: return "AST_NODE_LITERAL";
        case AST_NODE_IDENT: return "AST_NODE_IDENT";
        case AST_NODE_UNARY: return "AST_NODE_UNARY";
        case AST_NODE_SPREAD: return "AST_NODE_SPREAD";
        case AST_NODE_BINARY: return "AST_NODE_BINARY";
        case AST_NODE_ASSIGN: return "AST_NODE_ASSIGN";
        case AST_NODE_CALL_EXPR: return "AST_NODE_CALL_EXPR";
        case AST_NODE_MEMBER_EXPR: return "AST_NODE_MEMBER_EXPR";
        case AST_NODE_INDEX_EXPR: return "AST_NODE_INDEX_EXPR";
        case AST_NODE_IF_EXPR: return "AST_NODE_IF_EXPR";
        case AST_NODE_ARRAY: return "AST_NODE_ARRAY";
        case AST_NODE_MAP: return "AST_NODE_MAP";
        case AST_NODE_KEY_EXPR: return "AST_NODE_KEY_EXPR";
        case AST_NODE_MATCH_EXPR: return "AST_NODE_MATCH_EXPR";
        case AST_NODE_MATCH_ARM: return "AST_NODE_MATCH_ARM";
        case AST_NODE_NEW_EXPR: return "AST_NODE_NEW_EXPR";
        case AST_NODE_SEQ: return "AST_NODE_SEQ";
        case AST_NODE_LIST: return "AST_NODE_LIST";
        case AST_NODE_BLOCK: return "AST_NODE_BLOCK";
        case AST_NODE_EXPR_STMT: return "AST_NODE_EXPR_STMT";
        case AST_NODE_PARAM: return "AST_NODE_PARAM";
        case AST_NODE_FOR_STAM: return "AST_NODE_FOR_STAM";
        case AST_NODE_WHILE_STAM: return "AST_NODE_WHILE_STAM";
        case AST_NODE_BREAK_STAM: return "AST_NODE_BREAK_STAM";
        case AST_NODE_CONTINUE_STAM: return "AST_NODE_CONTINUE_STAM";
        case AST_NODE_RETURN_STAM: return "AST_NODE_RETURN_STAM";
        case AST_NODE_RAISE_STAM: return "AST_NODE_RAISE_STAM";
        case AST_NODE_RAISE_EXPR: return "AST_NODE_RAISE_EXPR";
        case AST_NODE_VAR_STAM: return "AST_NODE_VAR_STAM";
        case AST_NODE_ASSIGN_STAM: return "AST_NODE_ASSIGN_STAM";
        case AST_NODE_LET_STAM: return "AST_NODE_LET_STAM";
        case AST_NODE_PUB_STAM: return "AST_NODE_PUB_STAM";
        case AST_NODE_IMPORT: return "AST_NODE_IMPORT";
        case AST_NODE_EXPORT: return "AST_NODE_EXPORT";
        case AST_NODE_YIELD: return "AST_NODE_YIELD";
        case AST_NODE_AWAIT: return "AST_NODE_AWAIT";
        case AST_NODE_FUNC: return "AST_NODE_FUNC";
        case AST_NODE_FUNC_EXPR: return "AST_NODE_FUNC_EXPR";
        case AST_NODE_PROC: return "AST_NODE_PROC";
        case AST_NODE_CLASS: return "AST_NODE_CLASS";
        case AST_NODE_FIELD: return "AST_NODE_FIELD";
        case AST_NODE_PIPE: return "AST_NODE_PIPE";
        case AST_NODE_CURRENT_ITEM: return "AST_NODE_CURRENT_ITEM";
        case AST_NODE_CURRENT_INDEX: return "AST_NODE_CURRENT_INDEX";
        case AST_NODE_CURRENT_ERROR: return "AST_NODE_CURRENT_ERROR";
        case AST_NODE_LAST_INDEX: return "AST_NODE_LAST_INDEX";
        case AST_NODE_CONTENT: return "AST_NODE_CONTENT";
        case AST_NODE_ELEMENT: return "AST_NODE_ELEMENT";
        case AST_NODE_DECOMPOSE: return "AST_NODE_DECOMPOSE";
        case AST_NODE_LOOP: return "AST_NODE_LOOP";
        case AST_NODE_ORDER_SPEC: return "AST_NODE_ORDER_SPEC";
        case AST_NODE_GROUP_CLAUSE: return "AST_NODE_GROUP_CLAUSE";
        case AST_NODE_GROUP_KEY: return "AST_NODE_GROUP_KEY";
        case AST_NODE_JOIN_KEY: return "AST_NODE_JOIN_KEY";
        case AST_NODE_FOR_EXPR: return "AST_NODE_FOR_EXPR";
        case AST_NODE_INDEX_ASSIGN_STAM: return "AST_NODE_INDEX_ASSIGN_STAM";
        case AST_NODE_MEMBER_ASSIGN_STAM: return "AST_NODE_MEMBER_ASSIGN_STAM";
        case AST_NODE_PIPE_FILE_STAM: return "AST_NODE_PIPE_FILE_STAM";
        case AST_NODE_TYPE_STAM: return "AST_NODE_TYPE_STAM";
        case AST_NODE_PATH_EXPR: return "AST_NODE_PATH_EXPR";
        case AST_NODE_PATH_INDEX_EXPR: return "AST_NODE_PATH_INDEX_EXPR";
        case AST_NODE_NAVIGATION_EXPR: return "AST_NODE_NAVIGATION_EXPR";
        case AST_NODE_QUERY_EXPR: return "AST_NODE_QUERY_EXPR";
        case AST_NODE_SYS_FUNC: return "AST_NODE_SYS_FUNC";
        case AST_NODE_NAMED_ARG: return "AST_NODE_NAMED_ARG";
        case AST_NODE_TYPE: return "AST_NODE_TYPE";
        case AST_NODE_CONTENT_TYPE: return "AST_NODE_CONTENT_TYPE";
        case AST_NODE_LIST_TYPE: return "AST_NODE_LIST_TYPE";
        case AST_NODE_ARRAY_TYPE: return "AST_NODE_ARRAY_TYPE";
        case AST_NODE_MAP_TYPE: return "AST_NODE_MAP_TYPE";
        case AST_NODE_ELMT_TYPE: return "AST_NODE_ELMT_TYPE";
        case AST_NODE_FUNC_TYPE: return "AST_NODE_FUNC_TYPE";
        case AST_NODE_BINARY_TYPE: return "AST_NODE_BINARY_TYPE";
        case AST_NODE_UNARY_TYPE: return "AST_NODE_UNARY_TYPE";
        case AST_NODE_CONSTRAINED_TYPE: return "AST_NODE_CONSTRAINED_TYPE";
        case AST_NODE_OBJECT_TYPE: return "AST_NODE_OBJECT_TYPE";
        case AST_NODE_OBJECT_LITERAL: return "AST_NODE_OBJECT_LITERAL";
        case AST_NODE_STRING_PATTERN: return "AST_NODE_STRING_PATTERN";
        case AST_NODE_SYMBOL_PATTERN: return "AST_NODE_SYMBOL_PATTERN";
        case AST_NODE_PATTERN_RANGE: return "AST_NODE_PATTERN_RANGE";
        case AST_NODE_PATTERN_CHAR_CLASS: return "AST_NODE_PATTERN_CHAR_CLASS";
        case AST_NODE_PATTERN_SEQ: return "AST_NODE_PATTERN_SEQ";
        case AST_NODE_VIEW: return "AST_NODE_VIEW";
        case AST_NODE_STATE_ENTRY: return "AST_NODE_STATE_ENTRY";
        case AST_NODE_START: return "AST_NODE_START";
        case AST_NODE_EVENT_HANDLER: return "AST_NODE_EVENT_HANDLER";
        case AST_NODE_HANDLER_EXPR: return "AST_NODE_HANDLER_EXPR";
        case AST_NODE_HANDLER_STAM: return "AST_NODE_HANDLER_STAM";
        default: return "AST_NODE_UNKNOWN";
    }
}

static void emit_dump_string_field(const char* label, String* str) {
    if (!str) return;
    printf(" (%s ", label);
    emit_dump_escaped_string(str->chars, (int)str->len);
    printf(")");
}

static void emit_dump_strview_field(const char* label, StrView view) {
    if (!view.str) return;
    printf(" (%s ", label);
    emit_dump_escaped_string(view.str, (int)view.length);
    printf(")");
}

static void emit_dump_type_field(const char* label, const Type* type) {
    if (!type) return;
    const char* name = type_contract_display_name(type);
    printf(" (%s ", label);
    emit_dump_escaped_string(name, (int)strlen(name));
    printf(")");
}

static void emit_dump_value_effect_field(const Type* type) {
    if (!type) return;
    printf(" (value_may_error %s)", lambda_type_accepts_error((Type*)type)
        ? "true" : "false");
}

static void emit_dump_contract_field(const char* label, const Type* type,
        bool is_explicit) {
    emit_dump_type_field(label, type);
    if (type) printf(" (%s_explicit %s)", label, is_explicit ? "true" : "false");
}

static void emit_dump_source_field(const char* source, SourceSpan span) {
    int len = 0;
    const char* src = node_src(source, span, &len);
    if (len <= 0) return;
    printf(" (source ");
    emit_dump_escaped_string(src, len);
    printf(")");
}

static void emit_lambda_dump_node(const char* source, AstNode* node, int indent);
static void emit_lambda_dump_list(const char* source, const char* label, AstNode* node, int indent) {
    printf("\n");
    emit_dump_indent(indent);
    printf("(%s", label);
    while (node) {
        printf("\n");
        emit_lambda_dump_node(source, node, indent + 1);
        node = node->next;
    }
    printf(")");
}

static void emit_lambda_dump_field(const char* source, const char* label, AstNode* node, int indent) {
    if (!node) return;
    printf("\n");
    emit_dump_indent(indent);
    printf("(%s\n", label);
    emit_lambda_dump_node(source, node, indent + 1);
    printf(")");
}

static void emit_lambda_dump_node(const char* source, AstNode* node, int indent) {
    emit_dump_indent(indent);
    if (!node) {
        printf("(null)");
        return;
    }
    printf("(%s", ast_dump_kind_name(node->node_type));

    switch (node->node_type) {
        case AST_SCRIPT:
            emit_lambda_dump_list(source, "child", ((AstScript*)node)->child, indent + 1);
            break;
        case AST_NODE_IDENT: {
            AstIdentNode* ident = (AstIdentNode*)node;
            emit_dump_string_field("name", ident->name);
            AstNode* declaration = ident->entry ? ident->entry->node : NULL;
            if (declaration && (declaration->node_type == AST_NODE_FUNC ||
                    declaration->node_type == AST_NODE_FUNC_EXPR ||
                    declaration->node_type == AST_NODE_PROC)) {
                TypeFunc* signature = (TypeFunc*)declaration->type;
                if (signature && signature->return_contract) {
                    emit_dump_contract_field("function_return_contract",
                        signature->return_contract,
                        signature->has_explicit_return_contract);
                }
                Type* inferred_return = signature && signature->inferred_return
                    ? signature->inferred_return : signature ? signature->returned : NULL;
                if (inferred_return) {
                    emit_dump_type_field("function_effective_return", inferred_return);
                }
            }
            break;
        }
        case AST_NODE_PRIMARY:
            if (((AstPrimaryNode*)node)->expr) {
                emit_lambda_dump_field(source, "expr", ((AstPrimaryNode*)node)->expr, indent + 1);
            } else {
                emit_dump_source_field(source, node->source_span);
            }
            break;
        case AST_NODE_UNARY:
        case AST_NODE_SPREAD:
        case AST_NODE_UNARY_TYPE: {
            AstUnaryNode* un = (AstUnaryNode*)node;
            emit_dump_strview_field("op", un->op_str);
            emit_lambda_dump_field(source, "operand", un->operand, indent + 1);
            break;
        }
        case AST_NODE_BINARY:
        case AST_NODE_PIPE:
        case AST_NODE_BINARY_TYPE: {
            AstBinaryNode* bin = (AstBinaryNode*)node;
            emit_dump_strview_field("op", bin->op_str);
            if (node->node_type == AST_NODE_BINARY) {
                emit_dump_type_field("value_type", node->type);
                emit_dump_value_effect_field(node->type);
            }
            emit_lambda_dump_field(source, "left", bin->left, indent + 1);
            emit_lambda_dump_field(source, "right", bin->right, indent + 1);
            break;
        }
        case AST_NODE_IF_EXPR: {
            AstIfNode* if_node = (AstIfNode*)node;
            emit_lambda_dump_field(source, "cond", if_node->cond, indent + 1);
            emit_lambda_dump_field(source, "then", if_node->then, indent + 1);
            emit_lambda_dump_field(source, "else", if_node->otherwise, indent + 1);
            break;
        }
        case AST_NODE_MATCH_EXPR: {
            AstMatchNode* match = (AstMatchNode*)node;
            emit_lambda_dump_field(source, "scrutinee", match->scrutinee, indent + 1);
            emit_lambda_dump_list(source, "arms", (AstNode*)match->first_arm, indent + 1);
            break;
        }
        case AST_NODE_MATCH_ARM: {
            AstMatchArm* arm = (AstMatchArm*)node;
            emit_lambda_dump_field(source, "pattern", arm->pattern, indent + 1);
            emit_lambda_dump_field(source, "body", arm->body, indent + 1);
            break;
        }
        case AST_NODE_LET_STAM:
        case AST_NODE_PUB_STAM:
        case AST_NODE_TYPE_STAM:
            emit_lambda_dump_list(source, "declare", ((AstLetNode*)node)->declare, indent + 1);
            break;
        case AST_NODE_ASSIGN:
        case AST_NODE_KEY_EXPR:
        case AST_NODE_NAMED_ARG: {
            AstNamedNode* named = (AstNamedNode*)node;
            emit_dump_string_field("name", named->name);
            emit_lambda_dump_field(source, "as", named->as, indent + 1);
            break;
        }
        case AST_NODE_PARAM: {
            AstNamedNode* named = (AstNamedNode*)node;
            TypeParam* parameter = (TypeParam*)named->type;
            emit_dump_string_field("name", named->name);
            if (parameter && parameter->contract_type) {
                emit_dump_contract_field("contract", parameter->contract_type,
                    parameter->has_explicit_contract);
            }
            emit_lambda_dump_field(source, "as", named->as, indent + 1);
            break;
        }
        case AST_NODE_DECOMPOSE: {
            AstDecomposeNode* dec = (AstDecomposeNode*)node;
            printf(" (names");
            for (int i = 0; i < dec->name_count; i++) {
                printf(" ");
                emit_dump_escaped_string(dec->names[i]->chars, (int)dec->names[i]->len);
            }
            printf(")");
            emit_lambda_dump_field(source, "as", dec->as, indent + 1);
            break;
        }
        case AST_NODE_LOOP: {
            AstLoopNode* loop = (AstLoopNode*)node;
            emit_dump_string_field("name", loop->name);
            emit_dump_string_field("index", loop->index_name);
            emit_lambda_dump_field(source, "as", loop->as, indent + 1);
            emit_lambda_dump_field(source, "on", loop->on, indent + 1);
            emit_lambda_dump_list(source, "join_keys", (AstNode*)loop->join_keys, indent + 1);
            break;
        }
        case AST_NODE_JOIN_KEY: {
            AstJoinKey* key = (AstJoinKey*)node;
            emit_lambda_dump_field(source, "prior", key->prior_expr, indent + 1);
            emit_lambda_dump_field(source, "new", key->new_expr, indent + 1);
            break;
        }
        case AST_NODE_FOR_EXPR:
        case AST_NODE_FOR_STAM: {
            AstForNode* for_node = (AstForNode*)node;
            emit_lambda_dump_list(source, "loop", for_node->loop, indent + 1);
            emit_lambda_dump_list(source, "let", for_node->let_clause, indent + 1);
            emit_lambda_dump_field(source, "where", for_node->where, indent + 1);
            emit_lambda_dump_list(source, "group", (AstNode*)for_node->group, indent + 1);
            emit_lambda_dump_list(source, "order", for_node->order, indent + 1);
            emit_lambda_dump_field(source, "limit", for_node->limit, indent + 1);
            emit_lambda_dump_field(source, "offset", for_node->offset, indent + 1);
            emit_lambda_dump_field(source, "then", for_node->then, indent + 1);
            break;
        }
        case AST_NODE_GROUP_CLAUSE: {
            AstGroupClause* group = (AstGroupClause*)node;
            emit_dump_string_field("name", group->name);
            emit_lambda_dump_list(source, "keys", (AstNode*)group->keys, indent + 1);
            break;
        }
        case AST_NODE_GROUP_KEY: {
            AstGroupKey* key = (AstGroupKey*)node;
            emit_dump_string_field("alias", key->alias);
            emit_lambda_dump_field(source, "expr", key->expr, indent + 1);
            break;
        }
        case AST_NODE_ORDER_SPEC:
            emit_lambda_dump_field(source, "expr", ((AstOrderSpec*)node)->expr, indent + 1);
            break;
        case AST_NODE_WHILE_STAM: {
            AstWhileNode* wh = (AstWhileNode*)node;
            emit_lambda_dump_field(source, "cond", wh->cond, indent + 1);
            emit_lambda_dump_field(source, "body", wh->body, indent + 1);
            break;
        }
        case AST_NODE_RETURN_STAM:
            emit_lambda_dump_field(source, "value", ((AstReturnNode*)node)->value, indent + 1);
            break;
        case AST_NODE_RAISE_STAM:
        case AST_NODE_RAISE_EXPR:
            emit_lambda_dump_field(source, "value", ((AstRaiseNode*)node)->value, indent + 1);
            break;
        case AST_NODE_ASSIGN_STAM: {
            AstAssignStamNode* assign = (AstAssignStamNode*)node;
            emit_dump_string_field("target", assign->target);
            emit_lambda_dump_field(source, "target_node", assign->target_node, indent + 1);
            emit_lambda_dump_field(source, "value", assign->value, indent + 1);
            break;
        }
        case AST_NODE_INDEX_ASSIGN_STAM:
        case AST_NODE_MEMBER_ASSIGN_STAM: {
            AstCompoundAssignNode* assign = (AstCompoundAssignNode*)node;
            emit_lambda_dump_field(source, "object", assign->object, indent + 1);
            emit_lambda_dump_field(source, "key", assign->key, indent + 1);
            emit_lambda_dump_field(source, "value", assign->value, indent + 1);
            break;
        }
        case AST_NODE_ARRAY:
        case AST_NODE_ARRAY_TYPE:
            emit_lambda_dump_list(source, "item", ((AstArrayNode*)node)->item, indent + 1);
            break;
        case AST_NODE_LIST:
        case AST_NODE_CONTENT:
        case AST_NODE_CONTENT_TYPE:
        case AST_NODE_LIST_TYPE: {
            AstListNode* list = (AstListNode*)node;
            emit_lambda_dump_list(source, "declare", list->declare, indent + 1);
            emit_lambda_dump_list(source, "item", list->item, indent + 1);
            break;
        }
        case AST_NODE_MAP:
        case AST_NODE_MAP_TYPE:
            emit_lambda_dump_list(source, "item", ((AstMapNode*)node)->item, indent + 1);
            break;
        case AST_NODE_ELEMENT:
        case AST_NODE_ELMT_TYPE: {
            AstElementNode* elmt = (AstElementNode*)node;
            emit_lambda_dump_list(source, "item", elmt->item, indent + 1);
            emit_lambda_dump_field(source, "content", elmt->content, indent + 1);
            break;
        }
        case AST_NODE_MEMBER_EXPR:
        case AST_NODE_INDEX_EXPR: {
            AstFieldNode* field = (AstFieldNode*)node;
            emit_lambda_dump_field(source, "object", field->object, indent + 1);
            emit_lambda_dump_field(source, "field", field->field, indent + 1);
            break;
        }
        case AST_NODE_QUERY_EXPR: {
            AstQueryNode* query = (AstQueryNode*)node;
            emit_lambda_dump_field(source, "object", query->object, indent + 1);
            emit_lambda_dump_field(source, "query", query->query, indent + 1);
            break;
        }
        case AST_NODE_PATH_INDEX_EXPR: {
            AstPathIndexNode* path = (AstPathIndexNode*)node;
            emit_lambda_dump_field(source, "base", path->base_path, indent + 1);
            emit_lambda_dump_field(source, "segment", path->segment_expr, indent + 1);
            break;
        }
        case AST_NODE_NAVIGATION_EXPR:
            emit_lambda_dump_field(source, "object", ((AstNavigationNode*)node)->object, indent + 1);
            break;
        case AST_NODE_CALL_EXPR: {
            AstCallNode* call = (AstCallNode*)node;
            emit_dump_type_field("value_type", node->type);
            emit_dump_value_effect_field(node->type);
            emit_lambda_dump_field(source, "function", call->function, indent + 1);
            emit_lambda_dump_list(source, "argument", call->argument, indent + 1);
            break;
        }
        case AST_NODE_HANDLER_EXPR:
        case AST_NODE_HANDLER_STAM: {
            AstHandlerNode* handler = (AstHandlerNode*)node;
            emit_dump_type_field("value_type", node->type);
            emit_lambda_dump_field(source, "operand", handler->operand, indent + 1);
            emit_lambda_dump_field(source, "body", handler->body, indent + 1);
            if (handler->value_body) {
                emit_lambda_dump_field(source, "value", handler->value_body, indent + 1);
            }
            break;
        }
        case AST_NODE_SYS_FUNC: {
            AstSysFuncNode* sys = (AstSysFuncNode*)node;
            if (sys->fn_info) {
                printf(" (name ");
                emit_dump_escaped_string(sys->fn_info->name, (int)strlen(sys->fn_info->name));
                printf(")");
                emit_dump_type_field("success_type", sys->fn_info->success_type);
                printf(" (may_return_error %s)",
                    sys->fn_info->may_return_error ? "true" : "false");
            }
            break;
        }
        case AST_NODE_FUNC:
        case AST_NODE_FUNC_EXPR:
        case AST_NODE_PROC:
        case AST_NODE_FUNC_TYPE: {
            AstFuncNode* fn = (AstFuncNode*)node;
            TypeFunc* signature = fn->type && fn->type->type_id == LMD_TYPE_FUNC
                ? (TypeFunc*)fn->type : NULL;
            emit_dump_string_field("name", fn->name);
            if (signature && signature->return_contract) {
                emit_dump_contract_field("return_contract", signature->return_contract,
                    signature->has_explicit_return_contract);
            }
            Type* inferred_return = signature && signature->inferred_return
                ? signature->inferred_return : signature ? signature->returned : NULL;
            if (inferred_return) {
                emit_dump_type_field("effective_return", inferred_return);
            }
            emit_lambda_dump_list(source, "param", (AstNode*)fn->param, indent + 1);
            emit_lambda_dump_field(source, "body", fn->body, indent + 1);
            break;
        }
        case AST_NODE_CONSTRAINED_TYPE: {
            AstConstrainedTypeNode* ct = (AstConstrainedTypeNode*)node;
            emit_lambda_dump_field(source, "base", ct->base, indent + 1);
            emit_lambda_dump_field(source, "constraint", ct->constraint, indent + 1);
            break;
        }
        case AST_NODE_IMPORT: {
            AstImportNode* imp = (AstImportNode*)node;
            emit_dump_string_field("alias", imp->alias);
            emit_dump_strview_field("module", imp->module);
            break;
        }
        case AST_NODE_OBJECT_TYPE: {
            AstObjectTypeNode* obj = (AstObjectTypeNode*)node;
            emit_dump_string_field("name", obj->name);
            emit_lambda_dump_list(source, "item", obj->item, indent + 1);
            emit_lambda_dump_field(source, "base", obj->base_type, indent + 1);
            emit_lambda_dump_field(source, "content", obj->content, indent + 1);
            emit_lambda_dump_list(source, "methods", obj->methods, indent + 1);
            emit_lambda_dump_list(source, "constraints", obj->constraints, indent + 1);
            break;
        }
        case AST_NODE_OBJECT_LITERAL: {
            AstObjectLiteralNode* obj = (AstObjectLiteralNode*)node;
            emit_dump_string_field("type", obj->type_name);
            emit_lambda_dump_list(source, "item", obj->item, indent + 1);
            break;
        }
        case AST_NODE_STRING_PATTERN:
        case AST_NODE_SYMBOL_PATTERN: {
            AstPatternDefNode* pat = (AstPatternDefNode*)node;
            emit_dump_string_field("name", pat->name);
            emit_lambda_dump_field(source, "as", pat->as, indent + 1);
            break;
        }
        case AST_NODE_PATTERN_RANGE: {
            AstPatternRangeNode* range = (AstPatternRangeNode*)node;
            emit_lambda_dump_field(source, "start", range->start, indent + 1);
            emit_lambda_dump_field(source, "end", range->end, indent + 1);
            break;
        }
        case AST_NODE_PATTERN_SEQ:
            emit_lambda_dump_list(source, "first", ((AstPatternSeqNode*)node)->first, indent + 1);
            break;
        case AST_NODE_VIEW: {
            AstViewNode* view = (AstViewNode*)node;
            emit_dump_string_field("name", view->name);
            emit_lambda_dump_field(source, "pattern", view->pattern, indent + 1);
            emit_lambda_dump_list(source, "param", (AstNode*)view->param, indent + 1);
            emit_lambda_dump_field(source, "body", view->body, indent + 1);
            emit_lambda_dump_list(source, "state", (AstNode*)view->state, indent + 1);
            emit_lambda_dump_list(source, "handler", (AstNode*)view->handler, indent + 1);
            break;
        }
        case AST_NODE_STATE_ENTRY: {
            AstStateEntry* state = (AstStateEntry*)node;
            emit_dump_string_field("name", state->name);
            emit_lambda_dump_field(source, "value", state->value, indent + 1);
            if (state->next_state) {
                emit_lambda_dump_list(source, "next_state", (AstNode*)state->next_state, indent + 1);
            }
            break;
        }
        case AST_NODE_START: {
            // could not have its own case until AST_NODE_START/AST_NODE_EVENT_HANDLER
            // stopped sharing enum value 541 (duplicate case label)
            AstStartNode* start = (AstStartNode*)node;
            emit_lambda_dump_field(source, "call", (AstNode*)start->call, indent + 1);
            break;
        }
        case AST_NODE_EVENT_HANDLER: {
            AstEventHandler* handler = (AstEventHandler*)node;
            emit_dump_string_field("event", handler->event);
            emit_lambda_dump_list(source, "param", (AstNode*)handler->param, indent + 1);
            emit_lambda_dump_field(source, "body", handler->body, indent + 1);
            if (handler->next_handler) {
                emit_lambda_dump_list(source, "next_handler", (AstNode*)handler->next_handler, indent + 1);
            }
            break;
        }
        default:
            emit_dump_source_field(source, node->source_span);
            break;
    }

    printf(")");
}

int emit_ast_dump_file(const char* script_path) {
    EmitDirectParseState parse;
    if (!emit_prepare_direct_parse(script_path, &parse)) return 1;
    char* source = parse.source;

    Runtime runtime;
    runtime_init(&runtime);
    // Imported modules build through load_script(), which needs the normal
    // runtime ownership/index state even though this command stops before
    // execution. A zeroed Transpiler made import-bearing AST dumps dereference
    // a null Runtime in load_script().
    runtime.use_mir_direct = true;

    char* directory = file_path_dirname(script_path);
    size_t directory_length = directory ? strlen(directory) : 0;
    bool needs_separator = directory_length > 0 &&
        directory[directory_length - 1] != '/' && directory[directory_length - 1] != '\\';
    char* import_directory = (char*)mem_alloc(directory_length + (needs_separator ? 2 : 1),
        MEM_CAT_TEMP);
    if (!import_directory) {
        if (directory) mem_free(directory);
        runtime_cleanup(&runtime);
        // The AST pool is registered in the process memory context; destroying
        // it here leaves its child arena nodes dangling for the common finish
        // path. Let mem_context_shutdown destroy the registered allocator graph.
        mem_free(source);
        return 1;
    }
    if (directory_length) memcpy(import_directory, directory, directory_length);
    if (needs_separator) import_directory[directory_length++] = '/';
    import_directory[directory_length] = '\0';
    if (directory) mem_free(directory);

    Transpiler tp;
    emit_init_direct_transpiler(&tp, &parse, script_path);
    tp.directory = import_directory;
    tp.runtime = &runtime;

    if (!emit_build_direct_ast(&tp, source)) {
        fprintf(stderr, "Error: Failed to build AST for '%s'\n", script_path);
        arraylist_free(tp.const_list);
        mem_free(import_directory);
        runtime_cleanup(&runtime);
        // the registered AST pool is released by mem_context_shutdown after
        // its child arenas and shape pools have been destroyed.
        mem_free(source);
        return 1;
    }

    if (tp.error_count > 0) {
        fprintf(stderr, "Error: %d errors building AST for '%s'\n", tp.error_count, script_path);
        arraylist_free(tp.const_list);
        mem_free(import_directory);
        runtime_cleanup(&runtime);
        // the registered AST pool is released by mem_context_shutdown after
        // its child arenas and shape pools have been destroyed.
        mem_free(source);
        return 1;
    }

    printf("(ast-dump lambda\n");
    emit_lambda_dump_node(source, tp.ast_root, 1);
    // ANY-census block [Type_Infer TI3]: the machine-readable form of the
    // compile-time report, so census fixtures can assert per-reason counts.
    {
        int any_total = 0;
        for (int r = 0; r < ANY_REASON_COUNT; r++) any_total += tp.any_census[r];
        printf("\n  (any_census (total %d)", any_total);
        for (int r = 0; r < ANY_REASON_COUNT; r++) {
            if (!tp.any_census[r]) continue;
            printf(" (%s %d)", any_reason_name((AnyReason)r), tp.any_census[r]);
        }
        printf(")\n");
    }
    printf(")\n");

    arraylist_free(tp.const_list);
    mem_free(import_directory);
    runtime_cleanup(&runtime);
    // the registered AST pool is released by mem_context_shutdown after its
    // child arenas and shape pools have been destroyed.
    mem_free(source);
    return 0;
}
